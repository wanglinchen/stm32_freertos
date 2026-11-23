/*
 * 板级事实的运行期访问器：数据全部来自内嵌 DTB（bsl_fdt 解析），
 * 不再有任何构建期生成的 C 常量头。
 *
 * 为什么要单独一层：固件里 bsl/board_*、osl 的 panic 需要「端口 / 引脚 / 极性 /
 * 地址 / 波特率」这些板级事实，而它们现在只存在于 DTS。把取值集中到这里，
 * 各驱动只依赖函数，不依赖具体节点名，也不各自复制一份解析逻辑。
 *
 * 例外：panic 播报路径不能用本层（栈可能已崩、不能再遍历 FDT），
 * 它仍读 osl/inc/board_panic_hw.h 的手写常量。
 */
#ifndef BOARD_RT_H
#define BOARD_RT_H

#include <stdint.h>

#include "stm32f4xx_hal.h"

/* HSE 频率（Hz）；DTB 里没有 clocks 节点时返回 0。 */
uint32_t board_rt_hse_hz(void);

unsigned board_rt_led_count(void);
GPIO_TypeDef *board_rt_led_port(void);
uint16_t board_rt_led_pin(unsigned index);
int board_rt_led_active_low(void);

unsigned board_rt_key_count(void);
GPIO_TypeDef *board_rt_key_port(unsigned index);
uint16_t board_rt_key_pin(unsigned index);
GPIO_PinState board_rt_key_pressed(unsigned index);

GPIO_TypeDef *board_rt_i2c_scl_port(void);
uint16_t board_rt_i2c_scl_pin(void);
GPIO_TypeDef *board_rt_i2c_sda_port(void);
uint16_t board_rt_i2c_sda_pin(void);
int board_rt_i2c_ready(void);

int board_rt_beep_via_pcf8574(void);
uint8_t board_rt_pcf8574_addr7(void);
unsigned board_rt_beep_bit(void);
int board_rt_beep_active_high(void);

/* 调试串口：reg 基址 / 波特率 / TX·RX 端口字母与脚号。任一项缺失返回 0。 */
int board_rt_uart(uintptr_t *reg, uint32_t *baud, char *tx_port,
                  unsigned *tx_pin, char *rx_port, unsigned *rx_pin);

/* phandle（gpios 首 cell）→ GPIO 端口；找不到返回 NULL。 */
GPIO_TypeDef *board_rt_port_of_phandle(uint32_t phandle);

#endif /* BOARD_RT_H */
