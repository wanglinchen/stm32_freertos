#ifndef BSL_LCD_8080_H
#define BSL_LCD_8080_H

#include <stdint.h>

#include "bsl_err.h"

/*
 * 8080 并行总线（FMC Bank1 + A18 当 RS）的底层访问。
 *
 * 为什么单独成层：面板是 MCU 屏，像素不在内存里，而是靠"写命令/写数据"推进去。
 * 这条总线是板级的、与面板型号无关；NT35510 之上还能挂 ILI9806/SSD1963 一类。
 * 所以这里只管三件事：FMC 时序、16 位读写、命令与数据的分界，不含任何型号寄存器。
 *
 * 地址为什么是 0x6007FFFE / 0x60080000：Bank1 基址 0x60000000，RS 接 A18，
 * 厂商给的 LCD_BASE = 0x60000000 | ((1 << 18) * 2 - 2) —— 即 A18 = 0 落在
 * 0x6007FFFE（命令），A18 = 1 落在 0x60080000（数据）。两份地址相差 2 字节，
 * 是因为 FMC 按 16 位总线寻址（HADDR 的最低位不参与译码）。
 */
#define BSL_LCD_8080_CMD_ADDR ((volatile uint16_t *)0x6007FFFEUL)
#define BSL_LCD_8080_DAT_ADDR ((volatile uint16_t *)0x60080000UL)

/* 引脚/时序全部取自 DTB 的 panel 节点（lcd-*-pins + lcd-data-pins）。
 * 返回 BSL_OK 之外的值表示：DTB 缺控制脚/数据脚、端口认不出、或 FMC 配置前后
 * 读回来的值与写下去的不一致。 */
bsl_err_t bsl_lcd_8080_init(void);

/* 总线是否已配好。未配好时下面的访问函数是空操作（不能往没上电的总线写）。 */
int bsl_lcd_8080_ready(void);

void bsl_lcd_8080_cmd(uint16_t cmd);
void bsl_lcd_8080_data(uint16_t value);
/* 连续写 n 个像素/参数：一次命令 + 一串数据，比逐点拼命令快一个数量级。 */
void bsl_lcd_8080_data_burst(const uint16_t *values, unsigned count);
uint16_t bsl_lcd_8080_read(void);

#endif
