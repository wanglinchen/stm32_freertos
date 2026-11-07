#include "osl_health.h"

#include "FreeRTOS.h"
#include "task.h"

/*
 * 两个记录都是"历史最低"，只在读到更小的值时更新：一次性的峰值分配最能说明
 * 堆够不够，瞬时读数反而会漏掉它（比如 vTaskStartScheduler 建 IDLE/Timer 任务
 * 那一瞬间的亏空，跑起来之后又还回去了）。
 */
volatile uint32_t g_heap_free_min;
volatile uint32_t g_stack_free_min[OSL_HEALTH_STACK_SLOTS];

void osl_health_init(void)
{
    uint32_t i;

    g_heap_free_min = OSL_HEALTH_UNSET;
    for (i = 0U; i < (uint32_t)OSL_HEALTH_STACK_SLOTS; i++) {
        g_stack_free_min[i] = OSL_HEALTH_UNSET;
    }
}

uint32_t osl_health_heap_mark(void)
{
    uint32_t free_now = (uint32_t)xPortGetFreeHeapSize();

    if (free_now < g_heap_free_min) {
        g_heap_free_min = free_now;
    }
    return free_now;
}

void osl_health_stack_mark(uint32_t slot)
{
    UBaseType_t watermark;

    if (slot >= (uint32_t)OSL_HEALTH_STACK_SLOTS) {
        return;
    }
    /* NULL = 当前任务。返回值单位是 word（StackType_t），不是字节。 */
    watermark = uxTaskGetStackHighWaterMark(NULL);
    if ((uint32_t)watermark < g_stack_free_min[slot]) {
        g_stack_free_min[slot] = (uint32_t)watermark;
    }
}
