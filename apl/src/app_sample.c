#include "app_sample.h"

#include "board_device_registry.h"
#include "board_key.h"
#include "board_led.h"
#include "bsl_device.h"
#include "bsl_device_api.h"
#include "bsl_log.h"
#include "fml_panic.h"
#include "osl_health.h"
#include "osl_thread.h"

#define SAMPLE_LED_PERIOD_MS 1000U
#define SAMPLE_POLL_PERIOD_MS 10U
#define SAMPLE_DEBOUNCE_SAMPLES 3U
#define SAMPLE_HEALTH_DIVIDER 100U
#define SAMPLE_LOG_MOD_LED "led"
#define SAMPLE_LOG_MOD_BTN "btn"

static const bsl_gpio_leds_api_t *sample_leds_ops(void)
{
    /* 设备"在表里"不等于"初始化成功"：判 ready 之后再取 api，panic 码 2 的现场
     * 才能区分"设备缺失"与"driver->init 失败"。 */
    if (bsl_device_is_ready(BSL_DEV_LEDS) == 0) {
        return NULL;
    }
    return bsl_devapi_leds();
}

static const bsl_gpio_keys_api_t *sample_keys_ops(void)
{
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

#if BSL_LOG_LEVEL >= BSL_LOG_LEVEL_INF
/* 红绿翻转各打一行，让串口侧和眼见灯态对得上：`R=1 G=0` 表示红亮绿灭。
 * 一次 MOD_INF 打完整行（前缀只出现一次）。1 Hz、一行 20 字节，
 * 115200 下占用不到 2% 带宽，也不会挤占按键日志。 */
static void sample_log_leds(int red_on)
{
    BSL_LOG_MOD_INF(SAMPLE_LOG_MOD_LED, red_on ? "R=1 G=0\r\n" : "R=0 G=1\r\n");
}
#else
static void sample_log_leds(int red_on)
{
    (void)red_on;
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

static void sample_led_task(void *argument)
{
    const bsl_gpio_leds_api_t *leds = sample_leds_ops();
    int red_on = 1;
    osl_tick_t last_wake;

    (void)argument;
    if (leds == NULL) {
        /* 设备没起来不把整机拖进复位循环：任务继续跑、只喂健康心跳，闪灯降级。
         * debug_uart 自己没起来时不打日志，否则会把 Instance=NULL 的 huart
         * 交给 HAL_UART_Transmit。 */
        if (bsl_device_is_ready(BSL_DEV_DEBUG_UART)) {
            BSL_LOG_MOD_ERR(SAMPLE_LOG_MOD_LED,
                            "LEDS 未就绪（init 失败），闪灯降级\r\n");
        }
        for (;;) {
            osl_health_stack_mark(OSL_HEALTH_SLOT_LED);
            osl_thread_delay_ms(SAMPLE_LED_PERIOD_MS);
        }
    }
    /* banner 文案与其中的实例名/波特率都在 bsl 侧运行期从 DTB 读
     * （bsl_log.c 的 debug_uart_instance_name + board_rt_uart），这里只触发一次。
     * 放在样例应用而不是 fml：banner 属于"系统启动提示"，由应用决定何时打。 */
    bsl_log_startup_banner();
    last_wake = osl_tick_now();
    for (;;) {
        leds->set(BOARD_LED_R, red_on);
        leds->set(BOARD_LED_G, red_on ? 0 : 1);
        leds->off(BOARD_LED_B);
        /* 先打日志再翻状态：打印的内容就是刚刚写进端口的那一态，
         * 串口上看到的 `R=1 G=0` 与眼睛看到的灯一致。 */
        sample_log_leds(red_on);
        red_on = !red_on;
        osl_health_stack_mark(OSL_HEALTH_SLOT_LED);
        osl_thread_delay_until(&last_wake, SAMPLE_LED_PERIOD_MS);
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
    if (keys == NULL) {
        /* 按键轮询整段跳过，任务只喂健康心跳 —— 否则看门狗会把"设备坏了"
         * 表现成"任务卡死"，现场看到的是复位，真正原因反而更难查。 */
        if (bsl_device_is_ready(BSL_DEV_DEBUG_UART)) {
            BSL_LOG_MOD_ERR(SAMPLE_LOG_MOD_BTN,
                            "KEYS 未就绪（init 失败），按键降级\r\n");
        }
        for (;;) {
            osl_health_stack_mark(OSL_HEALTH_SLOT_BUTTON);
            osl_thread_delay_ms(SAMPLE_POLL_PERIOD_MS);
        }
    }
    if (beep == NULL && bsl_device_is_ready(BSL_DEV_DEBUG_UART)) {
        BSL_LOG_MOD_ERR(SAMPLE_LOG_MOD_BTN,
                        "BEEP 未就绪（init 失败），按键继续、无提示音\r\n");
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
    sample_task_create(sample_led_task, "led_blink");
    sample_task_create(sample_button_task, "button");
}
