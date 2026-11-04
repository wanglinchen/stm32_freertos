#include "board_key.h"

#include "stm32f4xx_hal.h"

typedef struct {
    GPIO_TypeDef *port;
    uint16_t pin;
    GPIO_PinState pressed_level;
} board_key_hw_t;

static const board_key_hw_t key_hw[BOARD_KEY_COUNT] = {
    {GPIOA, GPIO_PIN_0, GPIO_PIN_SET},
    {GPIOC, GPIO_PIN_13, GPIO_PIN_RESET},
};

void board_key_init(void)
{
    GPIO_InitTypeDef gpio = {0};
    unsigned i;

    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();

    gpio.Mode = GPIO_MODE_INPUT;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    for (i = 0U; i < (unsigned)BOARD_KEY_COUNT; i++) {
        gpio.Pin = key_hw[i].pin;
        HAL_GPIO_Init(key_hw[i].port, &gpio);
    }
}

int board_key_pressed(board_key_id_t id)
{
    const board_key_hw_t *key;

    if ((unsigned)id >= (unsigned)BOARD_KEY_COUNT) {
        return 0;
    }
    key = &key_hw[id];
    return HAL_GPIO_ReadPin(key->port, key->pin) == key->pressed_level;
}
