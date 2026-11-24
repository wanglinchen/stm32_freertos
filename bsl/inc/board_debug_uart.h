#ifndef BOARD_DEBUG_UART_H
#define BOARD_DEBUG_UART_H

#include "bsl_err.h"

/* 正点原子 Apollo 板上 CH340 调试口：USART1 PA9/PA10，115200 8N1
 *（与 project/dts/boards/apollo.dts 内 debug_serial 节点一致）。
 */

bsl_err_t board_debug_uart_init(void);

/* 不经 newlib stdio，供 fml/apl 在任务里打一行日志 */
void board_debug_uart_puts(const char *s);

/* 非阻塞收一个字节（P6b 的 shell 用）：1 = 读到，0 = 此刻没有数据。
 *
 * 只有 TX 是老口径（此前没有任何消费者）；USART1 的 `Init.Mode` 一直是
 * TX_RX、RX 脚在 DTB 里也一直配着，缺的只是这个读入口。调用方自己是唯一
 * 消费者（shell 一条任务轮询），所以这里不设临界区、不做环形缓冲。 */
int board_debug_uart_getc(char *out);

#endif