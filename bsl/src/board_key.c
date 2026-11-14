#include "board_key.h"

#include "board_dts.h"

#include "stm32f4xx_hal.h"

typedef struct {
    GPIO_TypeDef *port;
    uint16_t pin;
    GPIO_PinState pressed_level;
} board_key_hw_t;

/*
 * KEY2 在 PC13，挂在 RTC/后备域；不配 PWR+DBP 时 MODER/IDR 可能不跟脚，
 * 表现只有 KEY2 始终读不到按下（其它键在 GPIOA/H 上不受影响）。
 */
static void board_key_prepare_rtc_gpio_pin(GPIO_TypeDef *port, uint16_t pin)
{
    if (port == GPIOC && pin == GPIO_PIN_13) {
        __HAL_RCC_PWR_CLK_ENABLE();
        HAL_PWR_EnableBkUpAccess();
        /*
         * 释放 PC13 的 Tamper/RTC_AF1，否则即使用 GPIO 模式 IDR 也可能不反映键。
         */
        __HAL_RCC_RTC_ENABLE();
        CLEAR_BIT(RTC->TAFCR, RTC_TAFCR_TAMP1E);
    }
}

static uint32_t board_key_gpio_pull(const board_key_hw_t *key)
{
    /*
     * PC13~PC15：数据手册/RM 注明无内部上下拉，只能靠板载电阻；
     * 对 PC13 开内部上拉无效，KEY2 会表现为“永远按不实”。
     */
    if (key->port == GPIOC
            && (key->pin == GPIO_PIN_13
                || key->pin == GPIO_PIN_14
                || key->pin == GPIO_PIN_15)) {
        return GPIO_NOPULL;
    }
    return (key->pressed_level == GPIO_PIN_SET)
            ? (uint32_t)GPIO_PULLDOWN
            : (uint32_t)GPIO_PULLUP;
}

_Static_assert(
    BOARD_KEY_COUNT == BOARD_DTS_KEY_COUNT,
    "BOARD_KEY_COUNT must match device DTS keys count");

static const board_key_hw_t key_hw[BOARD_DTS_KEY_COUNT] = {
    {BOARD_DTS_KEY_0_PORT, BOARD_DTS_KEY_0_PIN, BOARD_DTS_KEY_0_PRESSED},
    {BOARD_DTS_KEY_1_PORT, BOARD_DTS_KEY_1_PIN, BOARD_DTS_KEY_1_PRESSED},
    {BOARD_DTS_KEY_2_PORT, BOARD_DTS_KEY_2_PIN, BOARD_DTS_KEY_2_PRESSED},
    {BOARD_DTS_KEY_3_PORT, BOARD_DTS_KEY_3_PIN, BOARD_DTS_KEY_3_PRESSED},
};

void board_key_init(void)
{
    GPIO_InitTypeDef gpio = {0};
    unsigned i;

    BOARD_DTS_KEYS_RCC_ENABLE_ALL();

    gpio.Mode = GPIO_MODE_INPUT;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    for (i = 0U; i < (unsigned)BOARD_DTS_KEY_COUNT; i++) {
        gpio.Pin = key_hw[i].pin;
        board_key_prepare_rtc_gpio_pin(key_hw[i].port, gpio.Pin);
        gpio.Pull = board_key_gpio_pull(&key_hw[i]);
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