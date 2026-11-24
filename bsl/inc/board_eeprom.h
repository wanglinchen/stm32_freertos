/*
 * 板级器件：AT24C02（256 B I2C EEPROM），挂在主软件 I2C（PH4/PH5）上。
 *
 * 为什么单独一层而不是直接调 bsl_i2c_gpio：
 *   1) 地址只从 DTB 拿（board_rt_eeprom），C 里不留第二份常量；
 *   2) EEPROM 的"写"有器件语义 —— 写完要等内部写周期（tWR≈5 ms）才落盘，
 *      这期间从机不应答。把等待放在这里，总线原语保持"只做时序"。
 *
 * 容量 256 B 是器件事实（24C02 = 2 kbit），写在 .c 里而不是 DTB：
 * 换 24C16 时节点 compatible 一改，这里也就换了，不存在"DTB 说 256、
 * 驱动按 1024 算"这种两份事实打架的形态。
 */
#ifndef BOARD_EEPROM_H
#define BOARD_EEPROM_H

#include "bsl_err.h"

#include <stdint.h>

/* 读 WHO 之前先建地址，不发任何总线事务。返回 BSL_OK 仅表示 DTB 给了合法地址。 */
bsl_err_t board_eeprom_init(void);
int board_eeprom_ready(void);

/* 从 8 位字地址起连续读 len 字节（len <= 256，不跨页限制由器件自己处理）。
 * 跨出容量时返回 BSL_ERR_INVAL，不发事务。 */
bsl_err_t board_eeprom_read(uint8_t addr8, uint8_t *buf, unsigned len);

/* 写单字节并等写周期结束。**只在 POST 自检与将来"设置持久化"里用**：
 * 每次写都有擦写寿命消耗，应用层不要把它当内存用。 */
bsl_err_t board_eeprom_write(uint8_t addr8, uint8_t value);

#endif
