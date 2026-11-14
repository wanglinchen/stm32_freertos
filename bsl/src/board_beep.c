#include "board_beep.h"

#include "stm32f4xx_hal.h"

void board_beep_init(void)
{
    GPIO_InitTypeDef gpio = {0};

    __HAL_RCC_GPIOI_CLK_ENABLE();
    HAL_GPIO_WritePin(GPIOI, GPIO_PIN_11, GPIO_PIN_RESET);

    gpio.Pin = GPIO_PIN_11;
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOI, &gpio);
}

void board_beep_set(int on)
{
    HAL_GPIO_WritePin(GPIOI, GPIO_PIN_11, on ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

void board_beep_on(void)
{
    board_beep_set(1);
}

void board_beep_off(void)
{
    board_beep_set(0);
}
