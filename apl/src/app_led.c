#include "app.h"

#include "board_device_registry.h"
#include "board_lcd.h"
#include "board_led.h"
#include "bsl_device.h"
#include "bsl_device_api.h"
#include "bsl_log.h"
#include "fml_font.h"
#include "fml_gui.h"
#include "fml_ui.h"

/*
 * LED 应用（P5a）：点 RED / GREEN / BLUE 切换对应灯，BACK 回桌面。
 *
 * 三条灯都用 `bsl_devapi_leds()`（函数指针表），不直接调 `board_led_set` ——
 * 与 app_sample 同一条纪律：设备"在表里"不等于"初始化成功"，判 ready 再取 api，
 * 失败时应用自己降级成"界面能开、按钮不生效"，而不是把整机拖进 panic。
 */
#define LED_LOG_MOD "app_led"
#define LED_WIN_W 480U
#define LED_WIN_H 300U
#define LED_BTN_W 120U
#define LED_BTN_H 56U
#define LED_BTN_GAP 16U
#define LED_BTN_Y 90U

static fml_ui_win_t s_win;
static int s_built;
static int s_on[3];   /* 红 / 绿 / 蓝 */

static const bsl_gpio_leds_api_t *led_ops(void)
{
    if (bsl_device_is_ready(BSL_DEV_LEDS) == 0) {
        return NULL;
    }
    return bsl_devapi_leds();
}

static void led_draw_status(void)
{
    const fml_font_t *font = &fml_font_ascii8x16;

    /* 状态行：三个灯的当前值，右对齐到窗口右下角上面一行，不压 BACK。
     * 用固定三个字符 `RGB`，读起来就是"哪个亮"。
     */
    fml_gui_text(s_win.x + 16U, s_win.y + 40U, "R", font, s_win.fg, s_win.bg);
    fml_gui_text(s_win.x + 32U, s_win.y + 40U, (s_on[0] != 0) ? "1" : "0", font,
                 (s_on[0] != 0) ? 0xF800U : s_win.fg, s_win.bg);
    fml_gui_text(s_win.x + 56U, s_win.y + 40U, "G", font, s_win.fg, s_win.bg);
    fml_gui_text(s_win.x + 72U, s_win.y + 40U, (s_on[1] != 0) ? "1" : "0", font,
                 (s_on[1] != 0) ? 0x07E0U : s_win.fg, s_win.bg);
    fml_gui_text(s_win.x + 96U, s_win.y + 40U, "B", font, s_win.fg, s_win.bg);
    fml_gui_text(s_win.x + 112U, s_win.y + 40U, (s_on[2] != 0) ? "1" : "0", font,
                 (s_on[2] != 0) ? 0x001FU : s_win.fg, s_win.bg);
}

static void led_apply(void)
{
    const bsl_gpio_leds_api_t *leds = led_ops();

    if (leds == NULL) {
        return;
    }
    leds->set(BOARD_LED_R, s_on[0]);
    leds->set(BOARD_LED_G, s_on[1]);
    leds->set(BOARD_LED_B, s_on[2]);
}

static void led_enter(void)
{
    static const char *const label[3] = {"RED", "GREEN", "BLUE"};

    if (s_built == 0) {
        const uint32_t total_w = (3U * LED_BTN_W) + (2U * LED_BTN_GAP);
        const uint32_t x0 = (LED_WIN_W - total_w) / 2U;
        const uint32_t wx = (board_lcd_width() - LED_WIN_W) / 2U;
        const uint32_t wy = (board_lcd_height() - LED_WIN_H) / 2U;
        unsigned i;

        fml_ui_win_init(&s_win, wx, wy, LED_WIN_W, LED_WIN_H, "LED - tap to toggle");
        for (i = 0U; i < 3U; i++) {
            (void)fml_ui_btn_add(&s_win, (uint16_t)(i + 1U), label[i],
                                 x0 + (i * (LED_BTN_W + LED_BTN_GAP)), LED_BTN_Y,
                                 LED_BTN_W, LED_BTN_H);
        }
        (void)fml_ui_btn_add(&s_win, APP_ID_BACK, "BACK", 16U, LED_WIN_H - 64U, 96U, 40U);
        s_built = 1;
    }
    /* 进入时把当前状态刷到灯上：桌面期间 app_sample 已不再动这三条灯，
     * 这里刷一次才能保证"屏上显示 = 灯的实际状态"。 */
    led_apply();
    fml_ui_draw(&s_win);
    led_draw_status();
}

static void led_event(const fml_ui_evt_t *ev)
{
    if ((ev->type != FML_UI_EVT_CLICK) || (ev->id < 1U) || (ev->id > 3U)) {
        return;
    }
    s_on[ev->id - 1U] = (s_on[ev->id - 1U] == 0) ? 1 : 0;
    led_apply();
    fml_ui_draw(&s_win);
    led_draw_status();
    BSL_LOG_MOD_INF(LED_LOG_MOD, "toggle ");
    bsl_log_put_dec((int)ev->id);
    BSL_LOG_INF(" r=");
    bsl_log_put_dec(s_on[0]);
    BSL_LOG_INF(" g=");
    bsl_log_put_dec(s_on[1]);
    BSL_LOG_INF(" b=");
    bsl_log_put_dec(s_on[2]);
    BSL_LOG_INF("\r\n");
}

static fml_ui_win_t *led_win(void)
{
    return &s_win;
}

/* 退出：三条灯全灭并清屏上的状态位。
 * 灯亮着而屏上已经回桌面，观者无法从任何地方读出"这三条灯是谁点亮的" ——
 * 违反"屏上显示 = 执行器实际状态"。也因为 app_sample 的 1 s 心跳已删，
 * 没有别人会替它收尾。
 *
 * 这里**不打日志**：POST 的 `menu` 项会在打 ` apps=` 这类半行的中途调进来，
 * 一打就把 `[post] menu apps=3 ...` 切成两行，POST 行当场不可解析
 * （P5a 首版实测：`[post] menu[app_led] leave r=0 g=0 b=0` + ` apps=3 ...`）。
 * 交还没交还由 `menu` 项的 `leave=<n>` 计数证明，不需要回调自己喊。 */
static void led_leave(void)
{
    s_on[0] = 0;
    s_on[1] = 0;
    s_on[2] = 0;
    led_apply();
}

static const app_desc_t s_desc = {
    1U, "LED", led_enter, NULL, led_win, led_event, led_leave
};

const app_desc_t *app_led_ops(void)
{
    return &s_desc;
}
