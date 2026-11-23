#include "osl_health.h"

#include "osl_thread.h"

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
    uint32_t watermark;

    if (slot >= (uint32_t)OSL_HEALTH_STACK_SLOTS) {
        g_stack_slot_err++;
        return;
    }
    watermark = osl_thread_stack_high_watermark();
    if (watermark < g_stack_free_min[slot]) {
        g_stack_free_min[slot] = watermark;
    }
}