#include "fml_button.h"

#include "board_beep.h"
#include "board_key.h"
#include "fml_led.h"
#include "FreeRTOS.h"
#include "task.h"
#include "osl_health.h"

/*
 * 10 ms 采样 + 连续 3 次一致（30 ms）才认。
 * 以前这里没有去抖，注释写“靠任务周期天然滤掉大部分抖动毛刺”——那只是把采样
 * 周期当去抖，机械抖动本身就是 1~10 ms 量级，落在采样点上时蜂鸣器会跟着咔哒。
 */
#define FML_BUTTON_POLL_PERIOD_MS 10U
#define FML_BUTTON_DEBOUNCE_SAMPLES 3U
/* 栈水位每 100 次轮询（约 1 s）采一次：uxTaskGetStackHighWaterMark 要扫描整段栈，
 * 10 ms 一拍纯属浪费；1 s 的粒度足够看出余量。 */
#define FML_BUTTON_HEALTH_DIVIDER 100U

static void fml_button_task(void *argument)
{
    int stable = 0;
    int candidate = 0;
    uint32_t agree = 0U;
    uint32_t health_div = 0U;
    TickType_t last_wake;

    (void)argument;
    /*
     * 与 fml_led_blink_task 同理：基准固定在首拍，轮询本身的执行时间
     * （两次按键读 + 每秒一次的栈水位扫描）不会累加进 10 ms 采样周期。
     * 用 vTaskDelay 时周期会随负载浮动，30 ms 的去抖窗口跟着变长变短。
     */
    last_wake = xTaskGetTickCount();
    for (;;) {
        int held = board_key_pressed(BOARD_KEY1) || board_key_pressed(BOARD_KEY2);

        if (held == candidate) {
            if (agree < FML_BUTTON_DEBOUNCE_SAMPLES) {
                agree++;
            }
        } else {
            candidate = held;
            agree = 1U;
        }
        if ((agree >= FML_BUTTON_DEBOUNCE_SAMPLES) && (candidate != stable)) {
            stable = candidate;
            board_beep_set(stable);
        }
        if (++health_div >= FML_BUTTON_HEALTH_DIVIDER) {
            health_div = 0U;
            osl_health_stack_mark(1U);
        }
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(FML_BUTTON_POLL_PERIOD_MS));
    }
}

void fml_button_start(void)
{
    board_key_init();
    board_beep_init();

    /*
     * 创建失败必须看得见：和 fml_led_blink_start 一样，堆不足时不能退化成"按键没反应"。
     * 这里复用 LED 播报码 2（应用任务创建失败），避免为同一类故障再加一个错误码。
     */
    if (xTaskCreate(
            fml_button_task,
            "button",
            configMINIMAL_STACK_SIZE,
            NULL,
            tskIDLE_PRIORITY + 1U,
            NULL) != pdPASS) {
        fml_led_panic(2U);
    }
}
