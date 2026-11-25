#include "app.h"

#include "board_device_registry.h"
#include "board_lcd.h"
#include "board_touch.h"
#include "bsl_device.h"
#include "bsl_device_api.h"
#include "bsl_log.h"
#include "fml_font.h"
#include "fml_gui.h"
#include "fml_panic.h"
#include "fml_ui.h"
#include "osl_health.h"
#include "osl_thread.h"

#define MENU_LOG_MOD "menu"
#define MENU_POLL_PERIOD_MS 10U

/* app_menu 的栈：默认 256 words（1 KB）只够"读触点 + 画窗口"，P5c 起这个任务
 * 还要在 enter 里列目录、解析 BMP 头、逐行解码（每条调用都会在栈上压一层）。
 * 512 words（2 KB）是给它留出的余量；目录项数组已经挪成模块级 static
 * （见 app_picviewer.c 的 pic_find_in），所以这 2 KB 是净余量而不是被吃掉的部分。
 * 另外 health 的最低水位只看栈用量，与这里给多少无关。 */
#define MENU_STACK_WORDS 512U
#define MENU_TITLE "P5 desktop - tap twice to enter"

/*
 * 注册表：指针数组在首次访问时填。不用编译期常量表，是因为每项来自
 * app_<name>_ops()（函数调用不能当静态初始化）。
 */
static const app_desc_t *s_apps[APP_COUNT_MAX];
static unsigned s_apps_count;
static int s_apps_built;

static void app_table_build(void)
{
    unsigned i;

    if (s_apps_built != 0) {
        return;
    }
    s_apps[s_apps_count++] = app_led_ops();
    s_apps[s_apps_count++] = app_key_ops();
    s_apps[s_apps_count++] = app_beep_ops();
    s_apps[s_apps_count++] = app_calc_ops();
    s_apps[s_apps_count++] = app_settings_ops();
    s_apps[s_apps_count++] = app_picviewer_ops();
    s_apps[s_apps_count++] = app_notepad_ops();
    s_apps[s_apps_count++] = app_calendar_ops();
    s_apps[s_apps_count++] = app_paint_ops();
    s_apps[s_apps_count++] = app_imu_ops();
    s_apps[s_apps_count++] = app_level_ops();
    s_apps[s_apps_count++] = app_qr_ops();
    /* 防御：ops() 返回 NULL 说明该应用没装好。就地压缩、不留空槽 ——
     * 空槽会让桌面图标数与 POST 的 apps 计数对不上。 */
    i = 0U;
    while (i < s_apps_count) {
        if (s_apps[i] == NULL) {
            s_apps[i] = s_apps[s_apps_count - 1U];
            s_apps_count--;
        } else {
            i++;
        }
    }
    s_apps_built = 1;
}

const app_desc_t *app_at(unsigned index)
{
    app_table_build();
    return (index < s_apps_count) ? s_apps[index] : NULL;
}

unsigned app_count(void)
{
    app_table_build();
    return s_apps_count;
}

const app_desc_t *app_find(app_id_t id)
{
    unsigned i;

    app_table_build();
    if (id == 0U) {
        return NULL;
    }
    for (i = 0U; i < s_apps_count; i++) {
        if (s_apps[i]->id == id) {
            return s_apps[i];
        }
    }
    return NULL;
}

/* 桌面窗口与状态。s_current = 0 表示在桌面。 */
static fml_ui_win_t s_desktop;
static app_id_t s_current;
static app_id_t s_pending_id;
static osl_tick_t s_pending_tick;
static int s_desktop_built;
static unsigned s_leave_count;
static uint32_t s_double_click_ms = APP_DOUBLE_CLICK_MS_DEFAULT;
static int s_trace = 1;

uint32_t app_double_click_ms(void)
{
    return s_double_click_ms;
}

void app_set_double_click_ms(uint32_t ms)
{
    if (ms < APP_DOUBLE_CLICK_MS_MIN) {
        ms = APP_DOUBLE_CLICK_MS_MIN;
    } else if (ms > APP_DOUBLE_CLICK_MS_MAX) {
        ms = APP_DOUBLE_CLICK_MS_MAX;
    }
    s_double_click_ms = ms;
}

void app_set_trace(int on)
{
    s_trace = (on != 0) ? 1 : 0;
}

int app_trace_on(void)
{
    return s_trace;
}

fml_ui_win_t *app_menu_win(void)
{
    return &s_desktop;
}

fml_ui_win_t *app_menu_current_win(void)
{
    const app_desc_t *app = app_find(s_current);

    if ((app == NULL) || (app->win == NULL)) {
        return &s_desktop;
    }
    return app->win();
}

unsigned app_menu_depth(void)
{
    return (s_current == 0U) ? 0U : 1U;
}

unsigned app_menu_leave_count(void)
{
    return s_leave_count;
}

/* 退出一个应用：先让它交还执行器（灯 / 蜂鸣 / 将来的一次性动画），
 * 再清 s_current。顺序反过来就等于"在 tick 已经停了之后才通知它" ——
 * app_beep 的 40 ms 最短响正是这样留在蜂鸣器上的。 */
static void app_leave_current(void)
{
    const app_desc_t *app;

    /* 在桌面时没有应用可退出：不计数，否则 POST 的 leave=<n> 会被"每次进入前
     * 的那次空调用"抬高，看不出真的有没有交还执行器。 */
    if (s_current == 0U) {
        return;
    }
    app = app_find(s_current);
    if ((app != NULL) && (app->leave != NULL)) {
        app->leave();
    }
    s_leave_count++;
}

static void desktop_draw(const char *status)
{
    const fml_font_t *font = &fml_font_ascii8x16;

    fml_ui_draw(&s_desktop);
    if (status != NULL) {
        fml_gui_text(16U, 40U, status, font, s_desktop.fg, s_desktop.bg);
    }
}

/*
 * 桌面布局：列数与图标宽都按"条目数 + 屏尺寸"算，不写死。
 *
 * 演进事实：P5a 时 3 个图标、P5c 8 个、P8b 起 9~11 个。800×480 横屏时 4 列
 * 正好铺满；换成 480×800 竖屏后 4 列 × 160 = 688 会把 origin_x 变成负（无符号
 * 下溢，图标全落屏外，P7 实测 `ink=0 hit=6/8`）。竖屏 11 个图标若还按 160 宽
 * 排 2 列，需要 6 行 × 136 = 816 > 800，底部图标被裁 —— 所以列数要往上取。
 *
 * 规则：从 `APP_ICON_MAX_COLS` 往下试，取**第一个同时满足**"图标宽 ≥ 120
 * （再窄标签就放不下）且整表高度（含起始 120 与行间距）≤ 屏高"的列数；
 * 都不满足时退回 1 列（宁可窄，不可裁）。
 */
static void desktop_build(void)
{
    const uint32_t width = board_lcd_width();
    const uint32_t height = board_lcd_height();
    const uint32_t n = app_count();
    const uint32_t origin_y = 120U;
    uint32_t cols = 1U;
    uint32_t icon_w = APP_ICON_W;
    uint32_t pitch;
    uint32_t total_w;
    uint32_t origin_x;
    unsigned i;

    for (i = APP_ICON_MAX_COLS; i >= 1U; i--) {
        uint32_t cw;
        uint32_t rows;
        uint32_t need_h;

        if (i > 1U) {
            /* 左右各留一个间隙，列间也留一个：i 列共 i+1 个间隙。 */
            cw = (width - ((i + 1U) * APP_ICON_GAP)) / i;
        } else {
            cw = APP_ICON_W;
        }
        if (cw > APP_ICON_W) {
            cw = APP_ICON_W;
        }
        if (cw < 120U) {
            continue;
        }
        rows = ((n + i - 1U) / i);
        need_h = origin_y + (rows * APP_ICON_H) + ((rows - 1U) * APP_ICON_GAP);
        if ((rows > 0U) && (need_h <= height)) {
            cols = i;
            icon_w = cw;
            break;
        }
    }
    pitch = icon_w + APP_ICON_GAP;
    total_w = (cols * icon_w) + ((cols - 1U) * APP_ICON_GAP);
    origin_x = (width > total_w) ? ((width - total_w) / 2U) : 0U;
    fml_ui_win_init(&s_desktop, 0U, 0U, width, height, MENU_TITLE);
    for (i = 0U; i < n; i++) {
        const app_desc_t *app = app_at(i);
        const uint32_t col = i % cols;
        const uint32_t row = i / cols;

        (void)fml_ui_btn_add(&s_desktop, app->id, app->name,
                             origin_x + (col * pitch),
                             origin_y + (row * (APP_ICON_H + APP_ICON_GAP)),
                             icon_w, APP_ICON_H);
    }
}

void app_menu_init(void)
{
    if (s_desktop_built != 0) {
        return;
    }
    s_current = 0U;
    s_pending_id = 0U;
    desktop_build();
    desktop_draw(NULL);
    s_desktop_built = 1;
}

int app_menu_dispatch(const fml_ui_evt_t *ev)
{
    const app_desc_t *app;
    osl_tick_t now;

    if (ev == NULL) {
        return APP_ACTION_NONE;
    }
    if (ev->type != FML_UI_EVT_CLICK) {
        /* 非 CLICK（DOWN / MOVE / UP）不参与桌面状态机：桌面只认"点了哪个图标"，
         * 按下未抬起不该选中任何东西。应用内则原样转给它的 event —— P8b 的
         * 画板就是靠这条路径拿到 DOWN/MOVE 的连续坐标，否则 MOVE 永远到不了
         * 应用（原来这里一律 return，等于把拖动吞掉）。 */
        if (s_current == 0U) {
            return APP_ACTION_NONE;
        }
        app = app_find(s_current);
        if ((app != NULL) && (app->event != NULL)) {
            app->event(ev);
            return APP_ACTION_APP;
        }
        return APP_ACTION_NONE;
    }

    if (s_current == 0U) {
        app = app_find((app_id_t)ev->id);
        if (app == NULL) {
            return APP_ACTION_NONE;
        }
        now = osl_tick_now();
        if ((s_pending_id == app->id) &&
            ((now - s_pending_tick) <= s_double_click_ms)) {
            s_pending_id = 0U;
            /* 桌面双击才走到这里，正常路径下这一步是空转；留着是为了"应用之间
             * 直接跳转"若将来开出来，执行器也一定先交还。 */
            app_leave_current();
            if (app->enter != NULL) {
                app->enter();
            }
            s_current = app->id;
            return APP_ACTION_ENTER;
        }
        s_pending_id = app->id;
        s_pending_tick = now;
        return APP_ACTION_SELECT;
    }

    if (ev->id == APP_ID_BACK) {
        app_leave_current();
        s_current = 0U;
        s_pending_id = 0U;
        desktop_draw(NULL);
        return APP_ACTION_BACK;
    }

    app = app_find(s_current);
    if ((app != NULL) && (app->event != NULL)) {
        app->event(ev);
        return APP_ACTION_APP;
    }
    return APP_ACTION_NONE;
}

static void menu_log_tap(const fml_ui_evt_t *ev, int action)
{
    BSL_LOG_MOD_INF(MENU_LOG_MOD, "tap id=");    bsl_log_put_dec((int)ev->id);
    BSL_LOG_INF(" act=");
    bsl_log_put_dec(action);
    BSL_LOG_INF("\r\n");
}

static void menu_task(void *argument)
{
    const bsl_touch_api_t *touch = NULL;
    fml_ui_evt_t ev;
    int32_t x = 0;
    int32_t y = 0;
    unsigned n;
    int pressed;
    int last_pressed = 0;
    osl_tick_t last_wake;

    (void)argument;
    if (bsl_device_is_ready(BSL_DEV_TOUCH) != 0) {
        touch = bsl_devapi_touch();
    } else if (bsl_device_is_ready(BSL_DEV_DEBUG_UART) != 0) {
        /* 必须 ASCII：bsl_log_puts 走 UART 直出，非 ASCII 在这条链上是 '?'，
         * 一条"看得见但读不懂"的日志等于没有日志。 */
        BSL_LOG_MOD_ERR(MENU_LOG_MOD,
                        "TOUCH not ready (init failed): desktop shows only, taps ignored\r\n");
    }

    last_wake = osl_tick_now();
    for (;;) {
        const app_desc_t *app;

        if (touch != NULL) {
            n = touch->read(&x, &y);
            if (n == BOARD_TOUCH_BUS_ERROR) {
                /* 总线错按抬起处理：否则 UI 会永远停在按下态，之后每次点击都被
                 * 判成"滑出按钮"。 */
                pressed = 0;
            } else {
                pressed = (n > 0U) ? 1 : 0;
            }
            if (pressed != last_pressed) {
                last_pressed = pressed;
                BSL_LOG_MOD_INF(MENU_LOG_MOD, pressed ? "touch down x=" : "touch up x=");
                if (pressed != 0) {
                    bsl_log_put_dec((int)x);
                    BSL_LOG_INF(" y=");
                    bsl_log_put_dec((int)y);
                }
                BSL_LOG_INF("\r\n");
            }
            /* 触点喂给"当前该显示的那个窗口"：桌面或当前应用。 */
            fml_ui_feed(app_menu_current_win(), x, y, pressed);
            while (fml_ui_next(&ev) != 0) {
                int action = app_menu_dispatch(&ev);

                if (action != APP_ACTION_NONE) {
                    menu_log_tap(&ev, action);
                }
                if (action == APP_ACTION_ENTER) {
                    const app_desc_t *entered = app_find((app_id_t)ev.id);

                    BSL_LOG_MOD_INF(MENU_LOG_MOD, "enter ");
                    bsl_log_puts((entered != NULL) ? entered->name : "?");
                    BSL_LOG_INF("\r\n");
                }
            }
        }

        app = app_find(s_current);
        if ((app != NULL) && (app->tick != NULL)) {
            app->tick();
        }
        /* 把这一轮画出来的东西推到屏上。RGB 屏（LTDC 自扫）这里是空操作；
         * MCU 屏（NT35510 + FMC）只推脏矩形 —— 全屏 384000 像素约 48 ms，
         * 每轮盲目全推会把 20 ms 的轮询周期拖成 60 ms 以上，触摸就发粘。
         * 放在 tick 之后：本轮所有绘制都已落进帧缓冲，一次推完。 */
        (void)board_lcd_present();
        osl_health_stack_mark(OSL_HEALTH_SLOT_MENU);
        osl_thread_delay_until(&last_wake, MENU_POLL_PERIOD_MS);
    }
}

void app_start(void)
{
    app_menu_init();
    if (osl_thread_create(menu_task, "app_menu", MENU_STACK_WORDS, NULL, 2U) != 0) {
        fml_panic(2U);
    }
}
