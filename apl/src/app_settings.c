#include "app.h"

#include "board_lcd.h"
#include "bsl_log.h"
#include "fml_font.h"
#include "fml_gui.h"
#include "fml_ui.h"

/*
 * 设置页（P5b）：两个真能动到硬件的项。
 *
 * 为什么只做两项、而且必须是"有执行器"的项：设置页最容易变成"改了一个变量、
 * 屏上一行字变了、别的什么都没发生"，这种项在板上跟没做一样。这两项的落点是：
 *   - 背光：board_lcd_backlight()，PB5，能看见屏灭 / 屏亮；
 *   - 双击间隔：app_set_double_click_ms()，直接改菜单的判定窗口，能看见
 *     "两次点得慢一点也还能进应用"。
 *
 * **掉电不保持**：24C02 在 P6（board_eeprom）。这一批把值放在 RAM 里，重启回默认，
 * 不假装它是持久配置 —— 假装的话，用户改完重启发现没了，会以为写坏了。
 */
#define SET_LOG_MOD "app_settings"
#define SET_WIN_W 480U
#define SET_WIN_H 300U
#define SET_BTN_W 200U
#define SET_BTN_H 56U
#define SET_BTN_Y 96U
#define SET_ID_BL 1U
#define SET_ID_DBL 2U
#define SET_STATUS_Y 44U

/* 三档。窗口取 250/350/500：250 以下是"稍微慢一点就进不去"，500 以上是
 * "点了两下不同图标也可能误进前一个"。 */
static const uint32_t s_dbl_ms[3] = {250U, 350U, 500U};
static const char *const s_dbl_name[3] = {"FAST", "MID", "SLOW"};
#define SET_DBL_STEPS 3U

static fml_ui_win_t s_win;
static int s_built;
static int s_bl = 1;
static unsigned s_dbl_idx = 1U;   /* 上电默认 350 ms，与 APP_DOUBLE_CLICK_MS_DEFAULT 一致 */
static unsigned s_flips;          /* 生效的切换次数（POST 判据用） */

int app_settings_backlight(void)
{
    return s_bl;
}

unsigned app_settings_flips(void)
{
    return s_flips;
}

static void set_status_draw(void)
{
    const fml_font_t *font = &fml_font_ascii8x16;

    fml_gui_fill(s_win.x + 16U, s_win.y + SET_STATUS_Y, SET_WIN_W - 32U, 24U,
                 s_win.bg);
    fml_gui_text(s_win.x + 16U, s_win.y + SET_STATUS_Y, "BL=", font, s_win.fg,
                 s_win.bg);
    fml_gui_text(s_win.x + 32U, s_win.y + SET_STATUS_Y, (s_bl != 0) ? "ON" : "OFF",
                 font, (s_bl != 0) ? 0x07E0U : 0xF800U, s_win.bg);
    fml_gui_text(s_win.x + 96U, s_win.y + SET_STATUS_Y, "DBL=", font, s_win.fg,
                 s_win.bg);
    fml_gui_text(s_win.x + 128U, s_win.y + SET_STATUS_Y, s_dbl_name[s_dbl_idx], font,
                 s_win.fg, s_win.bg);
}

static void set_apply(void)
{
    board_lcd_backlight(s_bl);
    app_set_double_click_ms(s_dbl_ms[s_dbl_idx]);
}

static void set_enter(void)
{
    if (s_built == 0) {
        const uint32_t wx = (board_lcd_width() - SET_WIN_W) / 2U;
        const uint32_t wy = (board_lcd_height() - SET_WIN_H) / 2U;

        fml_ui_win_init(&s_win, wx, wy, SET_WIN_W, SET_WIN_H, "SETTINGS - ram only");
        (void)fml_ui_btn_add(&s_win, SET_ID_BL, "BL", 16U, SET_BTN_Y, SET_BTN_W,
                             SET_BTN_H);
        (void)fml_ui_btn_add(&s_win, SET_ID_DBL, "DBL", 264U, SET_BTN_Y, SET_BTN_W,
                             SET_BTN_H);
        (void)fml_ui_btn_add(&s_win, APP_ID_BACK, "BACK", 16U, SET_WIN_H - 48U, 96U,
                             40U);
        s_built = 1;
    }
    /* 进入时把当前值刷到真执行器：设置页显示的值必须等于硬件此刻的状态，
     * 否则"我改了"与"屏上写着改了"是两件事。 */
    set_apply();
    fml_ui_draw(&s_win);
    set_status_draw();
}

static void set_event(const fml_ui_evt_t *ev)
{
    if (ev->type != FML_UI_EVT_CLICK) {
        return;
    }
    if (ev->id == SET_ID_BL) {
        s_bl = (s_bl != 0) ? 0 : 1;
    } else if (ev->id == SET_ID_DBL) {
        s_dbl_idx = (s_dbl_idx + 1U) % SET_DBL_STEPS;
    } else {
        return;
    }
    s_flips++;
    set_apply();
    set_status_draw();
    if (app_trace_on() != 0) {
        BSL_LOG_MOD_INF(SET_LOG_MOD, "bl=");
        bsl_log_put_dec(s_bl);
        BSL_LOG_INF(" dbl=");
        bsl_log_put_dec((int)s_dbl_ms[s_dbl_idx]);
        BSL_LOG_INF("\r\n");
    }
}

static fml_ui_win_t *set_win(void)
{
    return &s_win;
}

/* 退出时不做任何回退：设置页的两项都是"用户刚做的选择"，退出就撤销等于
 * 用户白按一次；它们住在 RAM 里，重启回默认（见文件头"掉电不保持"）。
 * 所以 leave 留 NULL —— 这个应用没有"由 tick 维持、必须交还"的执行器。 */
static const app_desc_t s_desc = {
    5U, "SETTINGS", set_enter, NULL, set_win, set_event, NULL
};

const app_desc_t *app_settings_ops(void)
{
    return &s_desc;
}
