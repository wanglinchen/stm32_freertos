#include "board_key.h"

#include "board_rt.h"
#include "bsl_device.h"
#include "bsl_device_api.h"
#include "bsl_gpio.h"

#include "stm32f4xx_hal.h"

typedef struct {
    GPIO_TypeDef *port;
    uint16_t pin;
    GPIO_PinState pressed_level;
} board_key_hw_t;

static void board_key_prepare_rtc_gpio_pin(GPIO_TypeDef *port, uint16_t pin)
{
    if (port == GPIOC && pin == GPIO_PIN_13) {
        __HAL_RCC_PWR_CLK_ENABLE();
        HAL_PWR_EnableBkUpAccess();
        __HAL_RCC_RTC_ENABLE();
        CLEAR_BIT(RTC->TAFCR, RTC_TAFCR_TAMP1E);
    }
}

static uint32_t board_key_gpio_pull(const board_key_hw_t *key)
{
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

/*
 * 键表由运行期从 DTB 填充（bsl_fdt → board_rt），不再由构建期生成初始化器。
 * 数组长度仍取 BOARD_KEY_* 枚举数：DTS 键数对不上时直接拒绝初始化，
 * 避免「少一个越界读 / 多一个永远读不到」这类静默错。
 */
static board_key_hw_t key_hw[BOARD_KEY_COUNT];
static unsigned key_count;

bsl_err_t board_key_init(void)
{
    unsigned count = board_rt_key_count();
    unsigned i;

    if (count != (unsigned)BOARD_KEY_COUNT) {
        return BSL_ERR_INVAL;
    }
    for (i = 0U; i < count; i++) {
        GPIO_TypeDef *port = board_rt_key_port(i);
        uint16_t pin = board_rt_key_pin(i);

        /* pin 是位掩码（1U << 0..15），恒非 0 —— 早前的 `pin == 0` 是不可能成立的
         * 死检查。真正会失败的是端口认不出，由下面两个返回值暴露。 */
        if (port == NULL) {
            return BSL_ERR_IO;
        }
        key_hw[i].port = port;
        key_hw[i].pin = pin;
        key_hw[i].pressed_level = board_rt_key_pressed(i);

        board_key_prepare_rtc_gpio_pin(port, pin);
        if (bsl_gpio_port_clk_enable(port) == 0) {
            return BSL_ERR_IO;
        }
        if (bsl_gpio_init_input(
                port,
                pin,
                board_key_gpio_pull(&key_hw[i]),
                GPIO_SPEED_FREQ_LOW) == 0) {
            return BSL_ERR_IO;
        }
    }
    key_count = count;
    return BSL_OK;
}

int board_key_pin_high(board_key_id_t id)
{
    if ((unsigned)id >= key_count) {
        return 0;
    }
    return bsl_gpio_read(key_hw[id].port, key_hw[id].pin) == GPIO_PIN_SET ? 1 : 0;
}

int board_key_pressed(board_key_id_t id)
{
    if ((unsigned)id >= key_count) {
        return 0;
    }
    return bsl_gpio_read(key_hw[id].port, key_hw[id].pin) == key_hw[id].pressed_level;
}

const bsl_gpio_keys_api_t bsl_api_brd_gpio_keys = {
    .pressed = board_key_pressed,
    .pin_high = board_key_pin_high,
};

const bsl_device_driver_t bsl_driver_brd_gpio_keys = {
    .init = board_key_init,
};
