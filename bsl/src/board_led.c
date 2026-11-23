#include "board_led.h"

#include "board_rt.h"
#include "bsl_device.h"
#include "bsl_device_api.h"
#include "bsl_gpio.h"

/*
 * 板级 DTS 是唯一真源：端口 / 引脚 / 极性全部在运行期从内嵌 DTB 读出
 * （board_rt 内部走 bsl_fdt）。旧写法是构建期由 Python 把 leds 节点摊成
 * BOARD_DTS_LED_PIN_n 常量：改 DTS 必须重跑生成器，且常量是 DTS 的第二份拷贝。
 *
 * panic 播报仍读 osl/inc/board_panic_hw.h 的手写常量（栈已崩，不能再遍历 FDT）。
 */
static uint16_t led_pin[BOARD_LED_COUNT];
static GPIO_TypeDef *led_port;
static GPIO_PinState led_on;
static GPIO_PinState led_off;

static int led_valid(board_led_id_t id)
{
    return (unsigned)id < (unsigned)BOARD_LED_COUNT;
}

bsl_err_t board_led_init(void)
{
    unsigned count = board_rt_led_count();
    unsigned i;
    uint16_t mask = 0U;

    /* DTS 灯数与枚举不一致时绝不半初始化。 */
    if (count != (unsigned)BOARD_LED_COUNT) {
        return BSL_ERR_INVAL;
    }
    led_port = board_rt_led_port();
    if (led_port == NULL) {
        return BSL_ERR_IO;
    }
    /* 掩码恒非 0（1U << 0..15），`led_pin[i] == 0` 这条早前是不可能成立的死检查；
     * 真正会失败的是"端口认不出"，它现在由下面两个返回值暴露。 */
    for (i = 0U; i < count; i++) {
        led_pin[i] = board_rt_led_pin(i);
        mask |= led_pin[i];
    }
    led_on = board_rt_led_active_low() ? GPIO_PIN_RESET : GPIO_PIN_SET;
    led_off = (led_on == GPIO_PIN_RESET) ? GPIO_PIN_SET : GPIO_PIN_RESET;

    /* 先开时钟再写 ODR：GPIO 时钟关闭时写 BSRR 不生效，顺序反了会留下
     * "模式配好了但上电瞬间灯闪一下"的毛刺。 */
    if (bsl_gpio_port_clk_enable(led_port) == 0) {
        return BSL_ERR_IO;
    }
    bsl_gpio_write_port_pin(led_port, mask, led_off);
    if (bsl_gpio_init_output_pp(
            led_port, mask, GPIO_NOPULL, GPIO_SPEED_FREQ_LOW) == 0) {
        return BSL_ERR_IO;
    }
    return BSL_OK;
}

void board_led_set(board_led_id_t id, int on)
{
    if (led_port == NULL || !led_valid(id)) {
        return;
    }
    bsl_gpio_write(led_port, led_pin[id], on ? led_on : led_off);
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
    if (led_port == NULL || !led_valid(id)) {
        return;
    }
    bsl_gpio_toggle(led_port, led_pin[id]);
}

const bsl_gpio_leds_api_t bsl_api_brd_gpio_leds = {
    .set = board_led_set,
    .on = board_led_on,
    .off = board_led_off,
    .toggle = board_led_toggle,
};

const bsl_device_driver_t bsl_driver_brd_gpio_leds = {
    .init = board_led_init,
};
