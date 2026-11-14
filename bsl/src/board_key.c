#include "board_key.h"

#include "stm32f4xx_hal.h"

typedef struct {
    GPIO_TypeDef *port;
    uint16_t pin;
    GPIO_PinState pressed_level;
} board_key_hw_t;

/*
 * 依据 doc/hardware/sch/DNF429P_F767P_H743P V2.6.pdf 第 4 页（DEVICE2 / KEY）与
 * doc/hardware/pinmap/阿波罗V2 STM32F429开发板IO引脚分配表.xlsx：
 *   WK_UP / KEY_UP：PA0/WK_UP，按下接 3.3V，默认松开为低。
 *   KEY2：PC13，按下接 3.3V，默认松开为低。
 * 应用验收用 WK_UP 与 KEY2（BOARD_KEY1 / BOARD_KEY2），不占用 PH2/PH3 上的 KEY1/KEY0。
 */
static const board_key_hw_t key_hw[BOARD_KEY_COUNT] = {
    {GPIOA, GPIO_PIN_0, GPIO_PIN_SET},
    {GPIOC, GPIO_PIN_13, GPIO_PIN_SET},
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
