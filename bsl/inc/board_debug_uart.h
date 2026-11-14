#ifndef BOARD_DEBUG_UART_H
#define BOARD_DEBUG_UART_H

/* 正点原子 Apollo 板上 CH340 调试口：USART1 PA9/PA10，115200 8N1
 *（与 device/dts/include/apollo-f429-peripherals.dtsi usart1_debug 一致）。
 */

void board_debug_uart_init(void);

/* 不经 newlib stdio，供 fml/apl 在任务里打一行日志 */
void board_debug_uart_puts(const char *s);

#endif