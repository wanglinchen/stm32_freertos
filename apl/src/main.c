#include "FreeRTOS.h"
#include "task.h"
#include "system_stm32f4xx.h"

static void vIdleWorkTask(void *argument)
{
    (void)argument;
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

int main(void)
{
    SystemCoreClockUpdate();

    xTaskCreate(vIdleWorkTask, "work", configMINIMAL_STACK_SIZE * 2U, NULL, tskIDLE_PRIORITY + 1U, NULL);

    vTaskStartScheduler();

    for (;;) {
    }
}
