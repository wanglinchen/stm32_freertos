#include "app.h"

#include "board_device_registry.h"
#include "board_lcd.h"
#include "bsl_device.h"
#include "bsl_device_api.h"
#include "bsl_log.h"
#include "fml_ui.h"
#include "osl_thread.h"

/*
 * BEEP 应用（P5a）：点一下响一声，手松开就停。
 *
 * **2026-11-25 起蜂鸣器被策略永久禁用**（`bsl/board_beep.c` 的 `BEEP_ALLOW_ON = 0`，
 * 规则见 `.skills/SKILL.md`）。这个应用保留但不再发声：它仍然是按压 → 状态的
 * 演示（`[app_beep] req on (muted)` 证明请求真的到了执行器），而屏上的标题直接
 * 写明"disabled"，不装成"能响但坏了"。
 *
 * 为什么不用"点一下响 100 ms 再自己关"：app_menu 任务只有 10 ms 周期，没有一个
 * 独立的定时者；用阻塞 delay 会把整个输入泵卡住（其余应用也会一起失去响应）。
 * 所以响的时长交给手指：按下响、抬起停 —— 与 app_sample 的按键提示音同一套语义。
 *
 * 事件侧只能看到 CLICK（DOWN/UP 没有被分发给应用，fml_ui 只产 CLICK），
 * 所以这里用 tick 读"按钮是否处于按下高亮"来判断手指，而不是等 UP 事件。
 */
#define BEEP_LOG_MOD "app_beep"
#define BEEP_WIN_W 360U
#define BEEP_WIN_H 240U

static fml_ui_win_t s_win;
static int s_built;
static int s_sounding;
static osl_tick_t s_min_until;   /* 快速单击也要响到这一刻，否则听不到 */

static const bsl_beep_api_t *beep_ops(void)
{
    if (bsl_device_is_ready(BSL_DEV_BEEP) == 0) {
        return NULL;
    }
    return bsl_devapi_beep();
}

static void beep_set(int on)
{
    const bsl_beep_api_t *beep = beep_ops();

    if (beep == NULL) {
        return;
    }
    beep->set(on);
    if (on != s_sounding) {
        s_sounding = on;
        /* 打"req on (muted)"而不是"on"：写 "on" 会让人以为真的响了 —— 现场的
         * 事实是这一声被 bsl 的断路器吞掉了，日志必须说同一件事。 */
        BSL_LOG_MOD_INF(BEEP_LOG_MOD, (on != 0) ? "req on (muted)\r\n" : "off\r\n");
    }
}

static void beep_enter(void)
{
    if (s_built == 0) {
        const uint32_t wx = (board_lcd_width() - BEEP_WIN_W) / 2U;
        const uint32_t wy = (board_lcd_height() - BEEP_WIN_H) / 2U;

        fml_ui_win_init(&s_win, wx, wy, BEEP_WIN_W, BEEP_WIN_H, "BEEP - disabled");
        (void)fml_ui_btn_add(&s_win, 1U, "BEEP", (BEEP_WIN_W - 160U) / 2U, 90U, 160U, 72U);
        (void)fml_ui_btn_add(&s_win, APP_ID_BACK, "BACK", 16U, BEEP_WIN_H - 64U, 96U, 40U);
        s_built = 1;
    }
    beep_set(0);
    fml_ui_draw(&s_win);
}

static void beep_tick(void)
{
    /* 手指还在 BEEP 按钮上（hot == 0 是该按钮的下标）就响；或还在 40 ms 最短响声内。
     * hot 只在"当前窗口是 beep"时有效 —— 触点由 app_menu 喂给当前窗口。 */
    const int holding = (s_win.hot == 0);
    const int min_win = ((int32_t)(s_min_until - osl_tick_now()) > 0) ? 1 : 0;

    beep_set(((holding != 0) || (min_win != 0)) ? 1 : 0);
}

static void beep_event(const fml_ui_evt_t *ev)
{
    /* 按住期间由 tick 维持响声；这里只补一个最短 40 ms，让点得再快也听得见。
     * 不在这里 delay：10 ms 的输入泵被阻塞会让所有应用一起失去响应。 */
    if ((ev->type == FML_UI_EVT_CLICK) && (ev->id == 1U)) {
        s_min_until = osl_tick_now() + 40U;
        beep_set(1);
    }
}

static fml_ui_win_t *beep_win(void)
{
    return &s_win;
}

/* 退出：静默关断并存回"没在响"的状态。
 * 两个理由：(1) 停响本身必须做 —— 退出后 tick 不再被调，40 ms 尾巴会一直响到
 * 下次进 BEEP；(2) 不能走 beep_set()，它带日志，而 POST 的 `menu` 项会在打
 * ` apps=` 这类半行的中途调进来，一打就把 POST 行切成两行（P5a 里 app_led
 * 的 leave 实测踩到了：`[post] menu[app_led] leave ...` + ` apps=3 ...`）。 */
static void beep_leave(void)
{
    const bsl_beep_api_t *beep = beep_ops();

    if (beep != NULL) {
        beep->set(0);
    }
    s_sounding = 0;
    s_min_until = osl_tick_now();
}

static const app_desc_t s_desc = {
    3U, "BEEP", beep_enter, beep_tick, beep_win, beep_event, beep_leave
};

const app_desc_t *app_beep_ops(void)
{
    return &s_desc;
}
