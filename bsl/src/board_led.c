#include "board_led.h"

#include "stm32f4xx_hal.h"

static const uint16_t led_pin[BOARD_LED_COUNT] = {
    GPIO_PIN_1,
    GPIO_PIN_0,
    GPIO_PIN_2,
};

static int led_valid(board_led_id_t id)
{
    return (unsigned)id < (unsigned)BOARD_LED_COUNT;
}

/*
 * 初始化用的引脚掩码由 led_pin[] 求和得出，不再手写一遍。
 *
 * 以前这里是 `GPIO_PIN_10 | GPIO_PIN_11 | GPIO_PIN_12`：同一组脚在 led_pin[] 和
 * 这一行各列了一份。换脚或增减灯数时只改数组，初始化仍指向旧脚 —— 置位（board_led_set
 * 走 led_pin[id]）与初始化（走这里的掩码）会分开，表现为"某个灯怎么都不亮"或
 * "配了一个不存在的脚"，而且编译、链接、尺寸全都不报异常。
 */
static uint16_t led_pins_mask(void)
{
    uint16_t pins = 0U;
    unsigned i;

    for (i = 0U; i < (unsigned)BOARD_LED_COUNT; i++) {
        pins |= led_pin[i];
    }
    return pins;
}

void board_led_init(void)
{
    GPIO_InitTypeDef gpio = {0};
    uint16_t pins = led_pins_mask();

    __HAL_RCC_GPIOB_CLK_ENABLE();
    HAL_GPIO_WritePin(GPIOB, pins, GPIO_PIN_SET);

    gpio.Pin = pins;
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOB, &gpio);
}

void board_led_set(board_led_id_t id, int on)
{
    if (!led_valid(id)) {
        return;
    }
    HAL_GPIO_WritePin(GPIOB, led_pin[id], on ? GPIO_PIN_RESET : GPIO_PIN_SET);
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
    HAL_GPIO_TogglePin(GPIOB, led_pin[id]);
}
