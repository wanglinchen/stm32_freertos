#include "board_key.h"

#include "stm32f4xx_hal.h"

typedef struct {
    GPIO_TypeDef *port;
    uint16_t pin;
    GPIO_PinState pressed_level;
} board_key_hw_t;

/*
 * 依据底板原理图（doc/hardware/sch/野火_F429_F767_H743挑战者_底板_原理图_V2.0_2024.pdf
 * 第 10 页「SENSOR/KEY/LED」）：
 *   KEY1 = SW1：PA0/WKUP --R121(1K)-- 节点 -- SW1 -- 3V3，节点有 R195 4.7K 下拉到 GND
 *   KEY2 = SW3：PC13/TAMPER--R123(1K)-- 节点 -- SW3 -- 3V3，节点有 R196 4.7K 下拉到 GND
 * 两个按键都是「按下 = 接 3V3 = 高电平」，且板上已有 4.7K 外部下拉，
 * 所以这里用 GPIO_NOPULL（再开内部下拉会让分压和延迟都白添一份，且不影响本用例）。
 */
static const board_key_hw_t key_hw[BOARD_KEY_COUNT] = {
    {GPIOA, GPIO_PIN_0, GPIO_PIN_SET},
    {GPIOC, GPIO_PIN_13, GPIO_PIN_SET},   /* 以前写成 RESET，与原理图相反 */
};

void board_key_init(void)
{
    GPIO_InitTypeDef gpio = {0};
    unsigned i;

    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();

    gpio.Mode = GPIO_MODE_INPUT;
    gpio.Pull = GPIO_NOPULL;   /* 板上已有 4.7K 外部下拉，见文件头原理图说明 */
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
