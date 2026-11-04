#include "FreeRTOS.h"
#include "task.h"
#include "fml_led.h"
#include "system_stm32f4xx.h"

int main(void)
{
    SystemCoreClockUpdate();

    fml_led_blink_start();

    vTaskStartScheduler();

    for (;;) {
    }
}
