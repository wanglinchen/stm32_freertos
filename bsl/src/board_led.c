#include "board_led.h"

#include "stm32f4xx_hal.h"

static const uint16_t led_pin[BOARD_LED_COUNT] = {
    GPIO_PIN_10,
    GPIO_PIN_11,
    GPIO_PIN_12,
};

static int led_valid(board_led_id_t id)
{
    return (unsigned)id < (unsigned)BOARD_LED_COUNT;
}

void board_led_init(void)
{
    GPIO_InitTypeDef gpio = {0};
    uint16_t pins = GPIO_PIN_10 | GPIO_PIN_11 | GPIO_PIN_12;

    __HAL_RCC_GPIOH_CLK_ENABLE();
    HAL_GPIO_WritePin(GPIOH, pins, GPIO_PIN_SET);

    gpio.Pin = pins;
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOH, &gpio);
}

void board_led_set(board_led_id_t id, int on)
{
    if (!led_valid(id)) {
        return;
    }
    HAL_GPIO_WritePin(GPIOH, led_pin[id], on ? GPIO_PIN_RESET : GPIO_PIN_SET);
}

void board_led_on(board_led_id_t id)
{
    board_led_set(id, 1);
}

void board_led_off(board_led_id_t id)
{
    board_led_set(id, 0);
}

void board_led_toggle(board_led_id_t id)
{
    if (!led_valid(id)) {
        return;
    }
    HAL_GPIO_TogglePin(GPIOH, led_pin[id]);
}
