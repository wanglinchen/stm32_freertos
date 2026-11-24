#include "app_sample.h"

#include "board_device_registry.h"
#include "board_key.h"
#include "bsl_device.h"
#include "bsl_device_api.h"
#include "bsl_log.h"
#include "fml_panic.h"
#include "osl_health.h"
#include "osl_thread.h"

/*
 * 样例应用（P5a 之后只剩按键与蜂鸣）。
 *
 * P0~P4 时这里还管三件事：LED 1 s 红绿交替、四键去抖、触摸边沿日志。三件都搬走了：
 *   - LED 归 `app_led`：它要在应用里独占三条灯，再让一个 1 s 心跳去改同一组led，
 *     屏上显示的状态与灯的实际状态会在 1 s 内对不上；
 *   - 触摸归 `app_menu`：`board_touch_read` 读完 0x814E 要写 0 清标志，两个任务
 *     各自读，后读的必然读到 0 点 —— P4 时这里读一次、菜单再读一次就会互相吞帧；
 *   - 启动 banner 留在这里打（原在 led_blink 任务里，那任务已删）。
 *
 * 剩下的按键去抖与提示音是"硬件演示"，不是 UI 的一部分：按键在 P5 没有对应界面，
 * 而 app_key 那个应用只是"显示键状态"。
 */
#define SAMPLE_POLL_PERIOD_MS 10U
#define SAMPLE_DEBOUNCE_SAMPLES 3U
#define SAMPLE_HEALTH_DIVIDER 100U
#define SAMPLE_LOG_MOD_BTN "btn"

static const bsl_gpio_keys_api_t *sample_keys_ops(void)
{
    /* 设备"在表里"不等于"初始化成功"：判 ready 之后再取 api，panic 码 2 的现场
     * 才能区分"设备缺失"与"driver->init 失败"。 */
    if (bsl_device_is_ready(BSL_DEV_KEYS) == 0) {
        return NULL;
    }
    return bsl_devapi_keys();
}

static const bsl_beep_api_t *sample_beep_ops(void)
{
    if (bsl_device_is_ready(BSL_DEV_BEEP) == 0) {
        return NULL;
    }
    return bsl_devapi_beep();
}

#if BSL_LOG_LEVEL >= BSL_LOG_LEVEL_INF
/* 续行用不带前缀的 BSL_LOG_INF：bsl_log_mod_puts() 每调一次都会重新打 `[mod] `，
 * 一段日志若逐段调 MOD_INF，串口上就是 `[btn] stable=[btn] 3[btn]  raw WKU=...`。
 * 模块前缀只该出现在行首。 */
static void sample_log_keys(int held, const bsl_gpio_keys_api_t *keys)
{
    BSL_LOG_MOD_INF(SAMPLE_LOG_MOD_BTN, "stable=");
    bsl_log_put_dec(held);
    BSL_LOG_INF(" raw WKU=");
    bsl_log_put_dec(keys->pin_high(BOARD_WK_UP));
    BSL_LOG_INF(" K0=");
    bsl_log_put_dec(keys->pin_high(BOARD_KEY0));
    BSL_LOG_INF(" K1=");
    bsl_log_put_dec(keys->pin_high(BOARD_KEY1));
    BSL_LOG_INF(" K2=");
    bsl_log_put_dec(keys->pin_high(BOARD_KEY2));
    BSL_LOG_INF("\r\n");
}
#else
static void sample_log_keys(int held, const bsl_gpio_keys_api_t *keys)
{
    (void)held;
    (void)keys;
}
#endif

/* beep 是独立可选设备：缺失时按键照常工作，只是没有提示音。
 * 早前 `keys == NULL || beep == NULL` 一起判降级，把"蜂鸣器坏了"放大成
 * "按键整段没反应" —— 两个设备的失败被绑成一个故障，现场无法区分。 */
static void beep_try(const bsl_beep_api_t *beep, int on)
{
    if (beep != NULL) {
        beep->set(on);
    }
}

static void sample_button_task(void *argument)
{
    const bsl_gpio_keys_api_t *keys = sample_keys_ops();
    const bsl_beep_api_t *beep = sample_beep_ops();
    int stable = 0;
    int candidate = 0;
    uint32_t agree = 0U;
    uint32_t health_div = 0U;
    osl_tick_t last_wake;

    (void)argument;
    /* banner 文案与其中的实例名/波特率都在 bsl 侧运行期从 DTB 读
     * （bsl_log.c 的 debug_uart_instance_name + board_rt_uart），这里只触发一次。 */
    if (bsl_device_is_ready(BSL_DEV_DEBUG_UART) != 0) {
        bsl_log_startup_banner();
    }
    if (keys == NULL) {
        /* 设备没起来不把整机拖进复位循环：任务继续跑、只喂健康心跳，按键降级。
         * debug_uart 自己没起来时不打日志，否则会把 Instance=NULL 的 huart
         * 交给 HAL_UART_Transmit。 */
        if (bsl_device_is_ready(BSL_DEV_DEBUG_UART) != 0) {
            BSL_LOG_MOD_ERR(SAMPLE_LOG_MOD_BTN,
                            "KEYS not ready (init failed): key input degraded\r\n");
        }
        for (;;) {
            osl_health_stack_mark(OSL_HEALTH_SLOT_BUTTON);
            osl_thread_delay_ms(SAMPLE_POLL_PERIOD_MS);
        }
    }
    if (beep == NULL && bsl_device_is_ready(BSL_DEV_DEBUG_UART) != 0) {
        /* ASCII：非 ASCII 在这条 UART 直出链上是 '?'，等于没日志 */
        BSL_LOG_MOD_ERR(SAMPLE_LOG_MOD_BTN,
                        "BEEP not ready (init failed): keys continue, no tone\r\n");
    }
    last_wake = osl_tick_now();
    beep_try(beep, 1);
    osl_thread_delay_ms(80);
    beep_try(beep, 0);
    for (;;) {
        int held = keys->pressed(BOARD_WK_UP)
                || keys->pressed(BOARD_KEY0)
                || keys->pressed(BOARD_KEY1)
                || keys->pressed(BOARD_KEY2);

        if (held == candidate) {
            if (agree < SAMPLE_DEBOUNCE_SAMPLES) {
                agree++;
            }
        } else {
            candidate = held;
            agree = 1U;
        }
        if ((agree >= SAMPLE_DEBOUNCE_SAMPLES) && (candidate != stable)) {
            stable = candidate;
            beep_try(beep, stable);
            sample_log_keys(stable, keys);
        }
        if (++health_div >= SAMPLE_HEALTH_DIVIDER) {
            health_div = 0U;
            osl_health_stack_mark(OSL_HEALTH_SLOT_BUTTON);
        }
        osl_thread_delay_until(&last_wake, SAMPLE_POLL_PERIOD_MS);
    }
}

static void sample_task_create(osl_thread_fn_t fn, const char *name)
{
    if (osl_thread_create(
            fn,
            name,
            OSL_THREAD_STACK_WORDS_DEFAULT,
            NULL,
            1U) != 0) {
        fml_panic(2U);
    }
}

void app_sample_start(void)
{
    sample_task_create(sample_button_task, "button");
}
