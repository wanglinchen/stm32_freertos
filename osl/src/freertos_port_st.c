#include "FreeRTOS.h"
#include "task.h"
#include "stm32f4xx_hal.h"

HAL_StatusTypeDef HAL_InitTick(uint32_t TickPriority)
{
    (void)TickPriority;
    return HAL_OK;
}

uint32_t HAL_GetTick(void)
{
    if (xTaskGetSchedulerState() == taskSCHEDULER_RUNNING) {
        return (uint32_t)xTaskGetTickCount();
    }
    return 0U;
}
