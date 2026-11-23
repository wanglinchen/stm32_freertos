#include "board_beep.h"

#include "board_rt.h"
#include "bsl_device.h"
#include "bsl_device_api.h"

#include "stm32f4xx_hal.h"

#include "bsl_err.h"
#include "bsl_i2c_gpio.h"
#include "bsl_log.h"

/*
 * 蜂鸣器挂在 PCF8574 的一位上，地址 / 位号 / 极性全部运行期从 DTB 读
 * （beep 节点 gpios = <&pcf8574 0 1>，扩展器 addr 由它自己的 reg 决定）。
 * 原先这里按 BOARD_DTS_BEEP_VIA_PCF8574 分成「经扩展器 / 直驱 GPIO」两条
 * 编译期分支；现在板子只有前者，直驱分支连同生成宏一起删除。
 */
static unsigned s_beep_bit;
static uint8_t s_addr7;
static int s_active_high;
static uint8_t s_pcf8574_shadow = 0xFFU;
static uint8_t s_i2c_fail_logged;

#define BEEP_BIT_MASK ((uint8_t)(1U << s_beep_bit))

static bsl_err_t pcf8574_write(uint8_t value)
{
    bsl_err_t rc;
    const bsl_i2c_gpio_bus_t *bus;

    if (bsl_board_i2c_gpio_ready() == 0U) {
        return BSL_ERR_IO;
    }
    bus = bsl_board_i2c_gpio_bus();
    rc = bsl_i2c_gpio_write_byte(bus, s_addr7, value);
    if (rc != BSL_OK && s_i2c_fail_logged == 0U) {
        s_i2c_fail_logged = 1U;
        BSL_LOG_MOD_ERR("beep", "pcf8574 i2c write failed rc=");
        bsl_log_put_dec((int)rc);
        BSL_LOG_ERR("\r\n");
    }
    return rc;
}

static bsl_err_t beep_apply_shadow(void)
{
    return pcf8574_write(s_pcf8574_shadow);
}

static bsl_err_t beep_set_pcf8574_logical(int on)
{
    if (on != 0) {
        if (s_active_high) {
            s_pcf8574_shadow |= BEEP_BIT_MASK;
        } else {
            s_pcf8574_shadow &= (uint8_t)~BEEP_BIT_MASK;
        }
    } else {
        if (s_active_high) {
            s_pcf8574_shadow &= (uint8_t)~BEEP_BIT_MASK;
        } else {
            s_pcf8574_shadow |= BEEP_BIT_MASK;
        }
    }
    return beep_apply_shadow();
}

/* 返回 bsl_err_t：I2C 总线没起来时这里会拿到 BSL_ERR_IO。
 * 早前是 void —— 写失败被丢弃，bsl_init_run 于是把蜂鸣器记成"初始化成功"，
 * 之后每次 set 都静默无效，现场只表现为"蜂鸣器坏了"。 */
bsl_err_t board_beep_init(void)
{
    if (board_rt_beep_via_pcf8574() == 0) {
        return BSL_ERR_INVAL;
    }
    s_beep_bit = board_rt_beep_bit();
    s_addr7 = board_rt_pcf8574_addr7();
    s_active_high = board_rt_beep_active_high();
    s_pcf8574_shadow = s_active_high ? 0x00U : 0xFFU;
    return beep_set_pcf8574_logical(0);
}

void board_beep_set(int on)
{
    (void)beep_set_pcf8574_logical(on);
}

/*
 * 静音按与 set 相同的极性函数算，不硬编码「置 1」。
 * 早前这里是 `s_pcf8574_shadow |= BIT`：active-low（当前配置）下恰好是静音，
 * 但改成 active-high 之后，panic 播报的静音步骤反而会把蜂鸣器打开
 * （关中断后的播报路径，听起来就是「故障码伴随长鸣」）。
 */
void board_panic_beep_silence(void)
{
    (void)beep_set_pcf8574_logical(0);
}

void board_beep_on(void)
{
    board_beep_set(1);
}

void board_beep_off(void)
{
    board_beep_set(0);
}

const bsl_beep_api_t bsl_api_brd_beep = {
    .set = board_beep_set,
    .on = board_beep_on,
    .off = board_beep_off,
};

const bsl_device_driver_t bsl_driver_brd_pcf8574_beep = {
    .init = board_beep_init,
};
