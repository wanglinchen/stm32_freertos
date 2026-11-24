#include "app.h"

#include "board_device_registry.h"
#include "board_key.h"
#include "board_lcd.h"
#include "bsl_device.h"
#include "bsl_device_api.h"
#include "fml_font.h"
#include "fml_gui.h"
#include "fml_ui.h"
#include "osl_thread.h"

/*
 * KEY 应用（P5a）：四个键的实时状态。
 *
 * 与 LED 应用的区别是它没有"按钮"可点（除了 BACK）—— 内容靠 tick 刷新。
 * tick 由 app_menu 每 10 ms 调一次，这里自己降到 100 ms 再刷：一次全窗重画
 * 是 480×300 的 SDRAM 写，10 ms 一次会把总线时间吃掉，而按键变化人眼看不出来
 * 那么快。
 */
#define KEY_WIN_W 480U
#define KEY_WIN_H 300U
#define KEY_REFRESH_MS 100U
#define KEY_BOX_W 96U
#define KEY_BOX_H 96U
#define KEY_BOX_GAP 16U
#define KEY_BOX_Y 90U

static fml_ui_win_t s_win;
static int s_built;
static osl_tick_t s_last_draw;
static int s_last_state = -1;
static const char *const s_names[4] = {"WKU", "K0", "K1", "K2"};

static const bsl_gpio_keys_api_t *key_ops(void)
{
    if (bsl_device_is_ready(BSL_DEV_KEYS) == 0) {
        return NULL;
    }
    return bsl_devapi_keys();
}

static int key_read(unsigned idx, const bsl_gpio_keys_api_t *keys)
{
    switch (idx) {
    case 0U:
        return keys->pressed(BOARD_WK_UP);
    case 1U:
        return keys->pressed(BOARD_KEY0);
    case 2U:
        return keys->pressed(BOARD_KEY1);
    default:
        return keys->pressed(BOARD_KEY2);
    }
}

static void key_refresh(void)
{
    const fml_font_t *font = &fml_font_ascii8x16;
    const bsl_gpio_keys_api_t *keys = key_ops();
    const uint32_t total_w = (4U * KEY_BOX_W) + (3U * KEY_BOX_GAP);
    const uint32_t x0 = (KEY_WIN_W - total_w) / 2U;
    unsigned i;
    int any = 0;

    if (keys == NULL) {
        fml_gui_text(s_win.x + 16U, s_win.y + 44U, "KEYS not ready", font, 0xF800U, s_win.bg);
        return;
    }
    for (i = 0U; i < 4U; i++) {
        const uint32_t bx = s_win.x + x0 + (i * (KEY_BOX_W + KEY_BOX_GAP));
        const uint32_t by = s_win.y + KEY_BOX_Y;
        const int down = key_read(i, keys);

        /* 按下时填充亮色、松开时空心：只反色不重画整窗，四个方块各画各的。 */
        fml_gui_fill(bx, by, KEY_BOX_W, KEY_BOX_H, down ? 0x07E0U : s_win.bg);
        fml_gui_frame(bx, by, KEY_BOX_W, KEY_BOX_H, s_win.line);
        fml_gui_text(bx + ((KEY_BOX_W - (8U * 3U)) / 2U), by + 40U, s_names[i], font,
                     down ? 0x0000U : s_win.fg, down ? 0x07E0U : s_win.bg);
        any |= down;
    }
    /* 状态行只在"有键按下"时写字，避免每 100 ms 刷一遍无意义的内容。 */
    if (any != 0) {
        fml_gui_text(s_win.x + 16U, s_win.y + 44U, "pressed", font, 0x07E0U, s_win.bg);
    } else {
        fml_gui_text(s_win.x + 16U, s_win.y + 44U, "release", font, s_win.fg, s_win.bg);
    }
}

static void key_tick(void)
{
    const bsl_gpio_keys_api_t *keys = key_ops();
    osl_tick_t now = osl_tick_now();
    int state = 0;
    unsigned i;

    if (keys != NULL) {
        for (i = 0U; i < 4U; i++) {
            state |= key_read(i, keys) << i;
        }
    }
    /* 状态没变就不重画：省总线，也让屏上看不到闪烁。 */
    if ((state == s_last_state) && ((now - s_last_draw) < KEY_REFRESH_MS)) {
        return;
    }
    s_last_state = state;
    s_last_draw = now;
    key_refresh();
}

static void key_enter(void)
{
    if (s_built == 0) {
        const uint32_t wx = (board_lcd_width() - KEY_WIN_W) / 2U;
        const uint32_t wy = (board_lcd_height() - KEY_WIN_H) / 2U;

        fml_ui_win_init(&s_win, wx, wy, KEY_WIN_W, KEY_WIN_H, "KEY - live state");
        (void)fml_ui_btn_add(&s_win, APP_ID_BACK, "BACK", 16U, KEY_WIN_H - 64U, 96U, 40U);
        s_built = 1;
    }
    s_last_state = -1;
    fml_ui_draw(&s_win);
    key_tick();
}

static void key_event(const fml_ui_evt_t *ev)
{
    /* 只有 BACK 会走到这里之外的事件；BACK 由 app_menu 拦截，所以这里没有可做的。 */
    (void)ev;
}

static fml_ui_win_t *key_win(void)
{
    return &s_win;
}

static const app_desc_t s_desc = {
    /* 没有执行器可交还：这个应用只是"读键、画方块"，灯与蜂鸣都不归它动。
     * leave 留 NULL，app_menu 会跳过回调但仍计数 —— 用 leave=<n> 判
     * "退出路径真的走过"，而不是判这台设备关掉了什么。 */
    2U, "KEY", key_enter, key_tick, key_win, key_event, NULL
};

const app_desc_t *app_key_ops(void)
{
    return &s_desc;
}
