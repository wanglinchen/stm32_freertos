#include "fml_led.h"

#include "board_led.h"
#include "FreeRTOS.h"
#include "task.h"

#define FML_LED_BLINK_PERIOD_MS 1000U

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
    (void)xTaskCreate(
        fml_led_blink_task,
        "led_blink",
        configMINIMAL_STACK_SIZE * 2U,
        NULL,
        tskIDLE_PRIORITY + 1U,
        NULL);
}
