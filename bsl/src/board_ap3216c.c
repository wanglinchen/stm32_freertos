#include "board_ap3216c.h"

#include "board_device_registry.h"
#include "bsl_device.h"
#include "board_rt.h"
#include "bsl_i2c_gpio.h"
#include "bsl_log.h"

#include "stm32f4xx_hal.h"

#define AP3216C_REG_SYSTEM 0x00U
#define AP3216C_SYS_RESET  0x04U
#define AP3216C_SYS_ENABLE 0x03U /* ALS + PS + IR 全开 */
#define AP3216C_REG_DATA   0x0AU

/* 复位后至少 10 ms 才可再写（数据手册）；取 50 ms 与参考工程一致：
 * 这条延时只在上电自检里付一次，省它没有任何收益。 */
#define AP3216C_RESET_MS 50U

static uint8_t s_addr7;
static int s_ready;

bsl_err_t board_ap3216c_init(void)
{
    board_rt_ap3216c_t a;
    const bsl_i2c_gpio_bus_t *bus;
    uint8_t sys = 0U;

    s_ready = 0;
    if (board_rt_ap3216c(&a) == 0) {
        BSL_LOG_MOD_ERR("als", "no dtb node\r\n");
        return BSL_ERR_NOINIT;
    }
    bus = bsl_board_i2c_gpio_bus();
    if (bus == NULL) {
        return BSL_ERR_NOINIT;
    }
    if (bsl_i2c_gpio_write_reg8(bus, a.addr7, AP3216C_REG_SYSTEM, AP3216C_SYS_RESET) != BSL_OK) {
        BSL_LOG_MOD_ERR("als", "reset nack\r\n");
        return BSL_ERR_IO;
    }
    HAL_Delay(AP3216C_RESET_MS);
    if (bsl_i2c_gpio_write_reg8(bus, a.addr7, AP3216C_REG_SYSTEM, AP3216C_SYS_ENABLE) != BSL_OK) {
        BSL_LOG_MOD_ERR("als", "enable nack\r\n");
        return BSL_ERR_IO;
    }
    /* 回读是唯一的硬判据：写成功不等于写进去（地址错、总线被拉低都可能
     * 表现成"发出去了没回应"）。读回来不是 0x03 就当场失败。 */
    if (bsl_i2c_gpio_read_reg8(bus, a.addr7, AP3216C_REG_SYSTEM, &sys, 1U) != BSL_OK) {
        BSL_LOG_MOD_ERR("als", "readback nack\r\n");
        return BSL_ERR_IO;
    }
    if (sys != AP3216C_SYS_ENABLE) {
        BSL_LOG_MOD_ERR("als", "sys mismatch got=");
        bsl_log_put_hex(sys, 2U);
        BSL_LOG_INF("\r\n");
        return BSL_ERR_IO;
    }
    s_addr7 = a.addr7;
    s_ready = 1;
    BSL_LOG_MOD_INF("als", "ap3216c addr=");
    bsl_log_put_hex(s_addr7, 2U);
    BSL_LOG_INF(" sys=03\r\n");
    return BSL_OK;
}

int board_ap3216c_ready(void)
{
    return s_ready;
}

bsl_err_t board_ap3216c_read(uint16_t *ir, uint16_t *ps, uint16_t *als)
{
    uint8_t buf[6];
    uint16_t v_ir;
    uint16_t v_ps;
    uint16_t v_als;

    if (!s_ready) {
        return BSL_ERR_NOINIT;
    }
    if (bsl_i2c_gpio_read_reg8(bsl_board_i2c_gpio_bus(), s_addr7, AP3216C_REG_DATA, buf, 6U) != BSL_OK) {
        return BSL_ERR_IO;
    }
    /* 位拼接按数据手册的寄存器定义（不是参考工程的口径抄一遍就算）：
     *   buf[0] bit7 = IR_OF，置起表示 IR 数据溢出/无效；
     *   buf[4] bit6 = PS_OF，同上。无效值一律报 0，而不是把脏位当数据用 ——
     *   溢出时 buf[1]<<2 会给出一个"看起来正常"的大数，最难查。 */
    v_ir = (buf[0] & 0x80U) ? 0U : (uint16_t)(((uint16_t)buf[1] << 2) | (buf[0] & 0x03U));
    v_als = (uint16_t)(((uint16_t)buf[3] << 8) | buf[2]);
    v_ps = (buf[4] & 0x40U) ? 0U : (uint16_t)(((uint16_t)(buf[5] & 0x3FU) << 4) | (buf[4] & 0x0FU));

    if (ir != NULL) { *ir = v_ir; }
    if (ps != NULL) { *ps = v_ps; }
    if (als != NULL) { *als = v_als; }
    return BSL_OK;
}

/* ---- 设备注册（registry 的 bsl_driver_liteon_ap3216c / bsl_api_liteon_ap3216c） ---- */

const bsl_als_api_t bsl_api_liteon_ap3216c = {
    .ready = board_ap3216c_ready,
    .read = board_ap3216c_read,
};

const struct bsl_device_driver bsl_driver_liteon_ap3216c = {
    .init = board_ap3216c_init,
};
