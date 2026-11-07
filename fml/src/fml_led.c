#include "fml_led.h"

#include "board_led.h"
#include "FreeRTOS.h"
#include "task.h"
#include "osl_panic.h"
#include "osl_health.h"

#define FML_LED_BLINK_PERIOD_MS 1000U

/*
 * 故障播报。
 *
 * 码表只有一份，在 osl/inc/osl_panic.h，osl 与 fml 都调用 osl_panic_report()。
 * 本层不再自己实现闪灯：以前这里用“同步闪 N 次 + 常亮”，而 osl 用“逐位二进制 +
 * 全亮”，两套码表的 1 和 2 直接撞码（osl 1 = 栈溢出，fml 1 = 调度器起不来），
 * 现场看到闪 1 次分不出来源。现在播报方式唯一，来源靠 g_panic_record 区分。
 *
 * 本函数永不返回：播报完关中断自旋。关中断是必需的 —— 它可能从任务上下文调用
 * （例如 blink_start 里 xTaskCreate 失败），调度器还活着时 blink 任务下一拍就会
 * 把常亮翻回灭，故障证据当场消失。
 */
void fml_led_panic(unsigned code)
{
    osl_panic_report((osl_panic_code_t)code);
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
        /*
         * 1 s 一拍，正好当健康采样节拍：这条任务周期最长、优先级又高于 IDLE，
         * 把堆余量和自己的栈余量记在这里，板上不需要额外任务也不用加定时器。
         * 栈余量单位是 word（见 osl/inc/osl_health.h），slot 0 专供本任务。
         */
        (void)osl_health_heap_mark();
        osl_health_stack_mark(0U);
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
