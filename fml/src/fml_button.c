#include "fml_button.h"

#include "board_beep.h"
#include "board_key.h"
#include "fml_led.h"
#include "FreeRTOS.h"
#include "task.h"

/*
 * 10 ms 采样 + 连续 3 次一致（30 ms）才认。
 * 以前这里没有去抖，注释写“靠任务周期天然滤掉大部分抖动毛刺”——那只是把采样
 * 周期当去抖，机械抖动本身就是 1~10 ms 量级，落在采样点上时蜂鸣器会跟着咔哒。
 */
#define FML_BUTTON_POLL_PERIOD_MS 10U
#define FML_BUTTON_DEBOUNCE_SAMPLES 3U

static void fml_button_task(void *argument)
{
    int stable = 0;
    int candidate = 0;
    uint32_t agree = 0U;

    (void)argument;
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
        vTaskDelay(pdMS_TO_TICKS(FML_BUTTON_POLL_PERIOD_MS));
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
