#include "osl_health.h"

#include "FreeRTOS.h"
#include "task.h"

/*
 * 两个记录都是「历史最低」，只在读到更小的值时更新。
 * 堆口径统一为 heap_4 的 xPortGetMinimumEverFreeHeapSize()（tick hook + 任务侧 mark）。
 */
volatile uint32_t g_heap_free_min;
volatile uint32_t g_stack_free_min[OSL_HEALTH_STACK_SLOTS];
volatile uint32_t g_stack_slot_err;

void osl_health_init(void)
{
    uint32_t i;

    g_heap_free_min = OSL_HEALTH_UNSET;
    g_stack_slot_err = 0U;
    for (i = 0U; i < (uint32_t)OSL_HEALTH_STACK_SLOTS; i++) {
        g_stack_free_min[i] = OSL_HEALTH_UNSET;
    }
}

void osl_health_stack_mark(uint32_t slot)
{
    UBaseType_t watermark;

    if (slot >= (uint32_t)OSL_HEALTH_STACK_SLOTS) {
        /* 静默 return 会让写错槽号的调用彻底隐身：数组里那个槽永远停在初值，
         * 读起来和"这个任务栈余量很大"一模一样。累加计数，调试器一读就分得清。 */
        g_stack_slot_err++;
        return;
    }
    /* NULL = 当前任务。返回值单位是 word（StackType_t），不是字节。 */
    watermark = uxTaskGetStackHighWaterMark(NULL);
    if ((uint32_t)watermark < g_stack_free_min[slot]) {
        g_stack_free_min[slot] = (uint32_t)watermark;
    }
}
