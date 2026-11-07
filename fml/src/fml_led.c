#include "fml_led.h"

#include "board_led.h"
#include "FreeRTOS.h"
#include "task.h"

#define FML_LED_BLINK_PERIOD_MS 1000U

/* 未调度时的粗延时：只用于故障播报，不依赖 tick。 */
static void fml_led_busy_delay(void)
{
    volatile uint32_t spin;

    for (spin = 0U; spin < 2000000U; spin++) {
        __asm volatile("nop");
    }
}

/*
 * 错误码（闪 N 次后常亮停机，本函数永不返回）：
 *   1 = 调度器启动失败（IDLE/Timer 任务创建不出来，一般是 FreeRTOS 堆不足）
 *   2 = 应用任务创建失败
 *   3 = configASSERT 失败（由 osl 的 vAssertFailed 直接播报，不走这里）
 *   4 = 系统时钟配置失败（HSE 未起振 / PLL 未锁定，停留在 HSI 16 MHz）
 */
void fml_led_panic(unsigned code)
{
    unsigned round;
    unsigned i;

    /* 可能在任何时刻被调用（包括 fml_led_blink_start 之前），所以先保证 LED 引脚可用。 */
    board_led_init();

    for (round = 0U; round < code; round++) {
        for (i = 0U; i < (unsigned)BOARD_LED_COUNT; i++) {
            board_led_set((board_led_id_t)i, 1);
        }
        fml_led_busy_delay();
        for (i = 0U; i < (unsigned)BOARD_LED_COUNT; i++) {
            board_led_set((board_led_id_t)i, 0);
        }
        fml_led_busy_delay();
    }
    for (i = 0U; i < (unsigned)BOARD_LED_COUNT; i++) {
        board_led_set((board_led_id_t)i, 1); /* 常亮 = 故障已播报、系统已停机 */
    }

    /*
     * 必须关中断再自旋：本函数可能从任务上下文调用（例如 blink_start 的
     * xTaskCreate 失败），此时调度器还活着，blink 任务下一拍就会把常亮翻回灭，
     * 故障证据当场消失。关中断后 SysTick 不再触发重调度，灯与被中断的现场一起冻住。
     * 调度器未启动时 BASEPRI 同样可用，不需要判断 xTaskGetSchedulerState。
     */
    taskDISABLE_INTERRUPTS();
    for (;;) {
        __asm volatile("nop");
    }
}

static void fml_led_blink_task(void *argument)
{
    int on = 0;

    (void)argument;
    for (;;) {
        unsigned i;

        for (i = 0U; i < (unsigned)BOARD_LED_COUNT; i++) {
            board_led_set((board_led_id_t)i, on);
        }
        on = !on;
        vTaskDelay(pdMS_TO_TICKS(FML_LED_BLINK_PERIOD_MS));
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
        fml_led_panic(2U);
    }
}
