#include "board_eeprom.h"

#include "board_device_registry.h"
#include "bsl_device.h"
#include "board_rt.h"
#include "bsl_i2c_gpio.h"
#include "bsl_log.h"

#include "stm32f4xx_hal.h"

/* 24C02：2 kbit = 256 字节，8 位字地址（一页 8 字节）。 */
#define EEPROM_BYTES 256U
/* 内部写周期典型 5 ms、最大 5 ms（数据手册 tWR）。取 6 ms：多 1 ms 的代价是
 * 上电自检慢 6 ms，少 1 ms 的代价是"写进去的值读回来是旧值"，后者难查得多。 */
#define EEPROM_TWR_MS 6U

static uint8_t s_addr7;
static int s_ready;

bsl_err_t board_eeprom_init(void)
{
    board_rt_eeprom_t e;
    const bsl_i2c_gpio_bus_t *bus;

    s_ready = 0;
    if (board_rt_eeprom(&e) == 0) {
        BSL_LOG_MOD_ERR("eeprom", "no dtb node\r\n");
        return BSL_ERR_NOINIT;
    }
    bus = bsl_board_i2c_gpio_bus();
    if (bus == NULL) {
        BSL_LOG_MOD_ERR("eeprom", "i2c bus not ready\r\n");
        return BSL_ERR_NOINIT;
    }
    /* 只探地址、不读不写：探测必须是只读动作（写错一个字节就是改用户数据）。 */
    if (bsl_i2c_gpio_probe(bus, e.addr7) != BSL_OK) {
        BSL_LOG_MOD_ERR("eeprom", "nack at ");
        bsl_log_put_hex(e.addr7, 2U);
        BSL_LOG_INF("\r\n");
        return BSL_ERR_IO;
    }
    s_addr7 = e.addr7;
    s_ready = 1;
    BSL_LOG_MOD_INF("eeprom", "24c02 addr=");
    bsl_log_put_hex(s_addr7, 2U);
    BSL_LOG_INF(" bytes=");
    bsl_log_put_dec((int)EEPROM_BYTES);
    BSL_LOG_INF("\r\n");
    return BSL_OK;
}

int board_eeprom_ready(void)
{
    return s_ready;
}

bsl_err_t board_eeprom_read(uint8_t addr8, uint8_t *buf, unsigned len)
{
    if (!s_ready) {
        return BSL_ERR_NOINIT;
    }
    if (buf == NULL || len == 0U || len > 255U) {
        return BSL_ERR_INVAL;
    }
    if ((unsigned)addr8 + len > EEPROM_BYTES) {
        return BSL_ERR_INVAL;
    }
    /* 随机读：字地址 + 重复 START + 读方向。地址越界已经在上游挡掉，
     * 这里不再做"回绕"，否则一次越界读会静默读到页首，看起来像数据被改。 */
    return bsl_i2c_gpio_read_reg8(bsl_board_i2c_gpio_bus(), s_addr7, addr8, buf, (uint8_t)len);
}

bsl_err_t board_eeprom_write(uint8_t addr8, uint8_t value)
{
    bsl_err_t rc;

    if (!s_ready) {
        return BSL_ERR_NOINIT;
    }
    /* 地址类型就是 uint8_t，天生落在 0..255 —— 这里不需要再比一次 EEPROM_BYTES
     * （编出来会被 -Wtype-limits 判成"永远为假"，那条告警本身就是在提醒"这个检查是空的"）。
     * 真正要挡的是 read 里的 addr8 + len 越界。 */
    rc = bsl_i2c_gpio_write_reg8(bsl_board_i2c_gpio_bus(), s_addr7, addr8, value);
    if (rc != BSL_OK) {
        return rc;
    }
    HAL_Delay(EEPROM_TWR_MS);
    return BSL_OK;
}

/* ---- 设备注册（registry 的 bsl_driver_atmel_24c02 / bsl_api_atmel_24c02） ---- */

const bsl_eeprom_api_t bsl_api_atmel_24c02 = {
    .ready = board_eeprom_ready,
    .read = board_eeprom_read,
    .write = board_eeprom_write,
};

const struct bsl_device_driver bsl_driver_atmel_24c02 = {
    .init = board_eeprom_init,
};
