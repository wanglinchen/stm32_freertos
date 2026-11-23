#ifndef BSL_I2C_GPIO_H
#define BSL_I2C_GPIO_H

#include "bsl_err.h"

#include "stm32f4xx_hal.h"
#include <stdint.h>

typedef struct {
    GPIO_TypeDef *scl_port;
    uint16_t scl_pin;
    GPIO_TypeDef *sda_port;
    uint16_t sda_pin;
} bsl_i2c_gpio_bus_t;

/* 1 = SCL/SDA 两个脚都配置成功；0 = 端口认不出（总线没起来，调用方必须失败）。 */
int bsl_i2c_gpio_init(const bsl_i2c_gpio_bus_t *bus);
/* 写单字节到 7bit 地址设备，后跟一字节数据；失败返回非 0。 */
bsl_err_t bsl_i2c_gpio_write_byte(const bsl_i2c_gpio_bus_t *bus, uint8_t addr7, uint8_t data);

const bsl_i2c_gpio_bus_t *bsl_board_i2c_gpio_bus(void);
uint8_t bsl_board_i2c_gpio_ready(void);
/* bsl_driver_brd_i2c_gpio 的声明在 board_device_registry.h（唯一来源），
 * 这里不再重复声明：两处同时可见时是 -Wredundant-decls。 */

#endif