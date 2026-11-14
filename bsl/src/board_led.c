#include "board_led.h"

#include "board_dts.h"

#include "stm32f4xx_hal.h"

_Static_assert(
    BOARD_LED_COUNT == BOARD_DTS_LED_PIN_COUNT,
    "BOARD_LED_COUNT must match device DTS leds.pins count");

static const uint16_t led_pin[BOARD_DTS_LED_PIN_COUNT] = {
    BOARD_DTS_LED_PINS_INITIALIZER,
};

static int led_valid(board_led_id_t id)
{
    return (unsigned)id < (unsigned)BOARD_LED_COUNT;
}

static uint16_t led_pins_mask(void)
{
    uint16_t pins = 0U;
    unsigned i;

    for (i = 0U; i < (unsigned)BOARD_DTS_LED_PIN_COUNT; i++) {
        pins |= led_pin[i];
    }
    return pins;
}

void board_led_init(void)
{
    GPIO_InitTypeDef gpio = {0};
    uint16_t pins = led_pins_mask();

    BOARD_DTS_LED_RCC_CLK_ENABLE();
    HAL_GPIO_WritePin(BOARD_DTS_LED_GPIO_PORT, pins, BOARD_DTS_LED_LEVEL_IDLE);

    gpio.Pin = pins;
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(BOARD_DTS_LED_GPIO_PORT, &gpio);
}

void board_led_set(board_led_id_t id, int on)
{
    if (!led_valid(id)) {
        return;
    }
    HAL_GPIO_WritePin(
        BOARD_DTS_LED_GPIO_PORT,
        led_pin[id],
        on ? BOARD_DTS_LED_LEVEL_ON : BOARD_DTS_LED_LEVEL_OFF);
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
    HAL_GPIO_TogglePin(BOARD_DTS_LED_GPIO_PORT, led_pin[id]);
}