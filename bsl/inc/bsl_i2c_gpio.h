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

/* 16 位寄存器地址的读/写（GT9xxx 触摸 IC 的口径）：
 *   写：START, addr<<1, regH, regL, data..., STOP
 *   读：START, addr<<1, regH, regL, 重复START, (addr<<1)|1, 读 len, STOP
 * len 上限 255（len 是 uint8_t）；失败返回非 0（NACK 或 ACK 超时）。
 *
 * 这两条是**总线原语**，不是触摸专用：任何 16 位寄存器地址的软件 I2C 器件
 * 都走这里。触摸驱动不自己翻转 GPIO —— 一份时序只有一处实现，
 * 否则调过的 ACK 延时会在第二条总线上原样复现一次没有。 */
bsl_err_t bsl_i2c_gpio_write_reg16(const bsl_i2c_gpio_bus_t *bus, uint8_t addr7,
                                   uint16_t reg16, const uint8_t *data, uint8_t len);
bsl_err_t bsl_i2c_gpio_read_reg16(const bsl_i2c_gpio_bus_t *bus, uint8_t addr7,
                                  uint16_t reg16, uint8_t *buf, uint8_t len);

/* 单地址探测：START + 地址字节（写方向）+ 取 ACK + STOP。
 * BSL_OK = 该 7 位地址上确实有从机应答；非 0 = 无应答（NACK）或总线错。
 *
 * 只发地址、不发数据：I2C 的"地址应答"本身就是器件在场的证据，而多写一个
 * 数据字节会顺手改掉一个寄存器 —— 探测必须是只读动作。 */
bsl_err_t bsl_i2c_gpio_probe(const bsl_i2c_gpio_bus_t *bus, uint8_t addr7);

/* 8 位寄存器地址的**读**（音频 codec 的口径：ES8388 / WM8978 都是
 * "1 字节寄存器地址 + 数据"，寄存器宽度 8 位）。
 *
 * 与 reg16 是两种**不同的事务形状**，不能互相顶替：拿 reg16 读 reg 0x00
 * 会发出 0x00 0x00，从机把第二个字节当成数据收下，寄存器 0 被改写、
 * 而且读回来的是它后面的寄存器。
 *
 * 不提供 `write_reg8`：P6c-0 只做只读排查（写 codec 寄存器在排查阶段拿不到
 * 信息、写错会把功放拉静音），而"头里声明、镜像里没符号"的面子接口会被
 * `check` 的 G 组抓出来。等 P6c 的驱动真的需要写时再加，那时它才有消费者。 */
bsl_err_t bsl_i2c_gpio_read_reg8(const bsl_i2c_gpio_bus_t *bus, uint8_t addr7,
                                 uint8_t reg8, uint8_t *buf, uint8_t len);

/* 8 位寄存器地址 + 1 字节数据的**写**（P6d 起有消费者：AP3216C / MPU9250 / 24C02
 * 三家都是"1 字节字地址 + 1 字节数据"）。
 *
 * 与 codec 的 `write_reg7_word9` 是两种事务：那家寄存器号 7 位、数据 9 位，硬套这条
 * 会把控制字写进另一个寄存器。反过来也不行 —— 拿这条写 codec 会丢掉数据最高位。
 * 所以两条都留着，由器件决定走哪条，而不是"看着像就复用"。
 *
 * 事务形状：START, addr<<1|W, reg8, data, STOP。失败返回非 0（NACK 或超时）。
 * 注意 24C02 这类 EEPROM 的写还需要等内部写周期（约 5 ms）才算落盘，
 * 那是器件语义，不是总线语义，所以本函数**不等**，由 board_eeprom 自己等。 */
bsl_err_t bsl_i2c_gpio_write_reg8(const bsl_i2c_gpio_bus_t *bus, uint8_t addr7,
                                  uint8_t reg8, uint8_t data);

/* 「7 位寄存器 + 9 位数据」的两字节写事务（WM8978 的口径）：一次 16 位控制字，
 * 传输顺序：
 *   字节 1 = reg7[6:0] << 1 | data9[8]
 *   字节 2 = data9[7:0]
 * 这就是 P6c-0 里没加的那个 `write_reg8` 的**真实形态**：codec 的寄存器地址只有
 * 7 位、数据 9 位，硬套"8 位地址 + 8 位数据"会写错寄存器（与拿 reg16 读 reg8 同型）。
 * 现在有消费者（`board_wm8978`），所以它在这里；没有消费者的 API 一律不加。
 * 越界（reg7 > 0x7F 或 data9 > 0x1FF）返回 BSL_ERR_INVAL，且什么都不发。 */
bsl_err_t bsl_i2c_gpio_write_reg7_word9(const bsl_i2c_gpio_bus_t *bus, uint8_t addr7,
                                        uint8_t reg7, uint16_t data9);

const bsl_i2c_gpio_bus_t *bsl_board_i2c_gpio_bus(void);
uint8_t bsl_board_i2c_gpio_ready(void);
/* bsl_driver_brd_i2c_gpio 的声明在 board_device_registry.h（唯一来源），
 * 这里不再重复声明：两处同时可见时是 -Wredundant-decls。 */

#endif