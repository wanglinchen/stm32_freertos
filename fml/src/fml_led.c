#include "fml_led.h"

#include "board_debug_uart.h"
#include "board_led.h"
#include "fml_panic.h"
#include "FreeRTOS.h"
#include "task.h"
#include "osl_health.h"

/* 红/绿互斥：每 1000 ms 切换一次（红亮绿灭 ↔ 红灭绿亮）；PB2 码位正常运行时保持灭。 */
#define FML_LED_BLINK_PERIOD_MS 1000U

static void fml_led_blink_task(void *argument)
{
    int red_on = 1;
    TickType_t last_wake;
    static int uart_banner_done;

    (void)argument;
    if (uart_banner_done == 0) {
        uart_banner_done = 1;
        board_debug_uart_puts("\r\nstm32-freertos: USART1 115200 debug ready\r\n");
    }
    /*
     * 用 vTaskDelayUntil 而不是 vTaskDelay：基准点固定在首拍，循环里的执行时间
     * （一次水位采样 + 灯脚写入）不会累加进周期。用 vTaskDelay 时每拍都把
     * 这段开销叠在周期上，会越来越慢且无法从外部察觉。
     * 注意基准变量的赋值必须留在循环外：每拍重取 last_wake 会让
     * vTaskDelayUntil 退化成 vTaskDelay（verify 的 period 断言守的就是这一点）。
     */
    last_wake = xTaskGetTickCount();
    for (;;) {
        board_led_set(BOARD_LED_R, red_on);
        board_led_set(BOARD_LED_G, red_on ? 0 : 1);
        board_led_off(BOARD_LED_B);
        red_on = !red_on;
        /*
         * 1 s 切换节拍兼作栈余量采样；堆水位只在 tick hook 采。
         * 栈余量单位是 word（见 osl/inc/osl_health.h），slot 0 专供本任务。
         */
        osl_health_stack_mark(OSL_HEALTH_SLOT_LED);
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(FML_LED_BLINK_PERIOD_MS));
    }
}

void fml_led_blink_start(void)
{
    board_led_init();
    /* 创建失败必须看得见：这里以前是 (void)xTaskCreate，堆不足时表现为“板子什么都不做”。 */
    if (xTaskCreate(
            fml_led_blink_task,
            "led_blink",
            configMINIMAL_STACK_SIZE * 2U,
            NULL,
            tskIDLE_PRIORITY + 1U,
            NULL) != pdPASS) {
        fml_panic(2U);
    }
}
