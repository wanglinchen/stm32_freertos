/*
 * USART3（P6e-1）：收发 + 空闲判帧。
 *
 * 分工口径：**芯片事实**（USART3 实例、CR1/BRR 位域、IRQ 号、判帧用"最后字节时刻"
 * 而不是 TIM7）写在这里；**板级事实**（PB10/PB11 掩码、AF7、波特率、判帧空闲毫秒）
 * 来自 DTB（board_rt_usart）。BRR 由 HAL_RCC_GetPCLK1Freq() 现场算。
 *
 * 与参考工程 `usart3.c` 的差异（都是有意的）：
 *   1) 参考工程用 TIM7 打 10 ms 判帧；本工程不开定时器 —— 空闲阈值来自 DTB，
 *      用 HAL_GetTick() 与"最后一个字节的时刻"比较即可，少一个外设、少一组
 *      NVIC 配置，判帧逻辑还能被 POST 直接注入字节来验（不需要真的对端）。
 *   2) 参考工程 `u3_printf` 用 mymalloc 申请 600 B 再 strlen；本工程直接
 *      逐字节推、按 TC 等 —— 报文短，省掉一块动态内存。
 *   3) 参考工程 BRR 用 mantissa/fraction 手算；这里用 (pclk + baud/2)/baud
 *      取整，OVER8=0 下两者等价（BRR 的低 4 位就是 1/16 分频的小数部分）。
 *
 * **抓不住的**：RX 线上是否真的收到字节需要有对端（本板没接 GSM/ATK-MODULE，
 * PB11 还与 YT8512C 的 TXEN 共用 P8 跳线）。所以 POST 只证两件事：
 * 分频对（发一个字节的 TC 耗时）与帧状态机对（注入字节 + 空闲切帧）。
 * 这层限制写在对照表 P6e-1 行，不糊在"跑通了"里。
 */
#ifndef BSL_UART_H
#define BSL_UART_H

#include "bsl_err.h"

#include <stdint.h>

/* 单帧上限。参考工程给 600 B（GSM 报文口径）；本工程当前只做帧状态机与短报文，
 * 256 B 够用且省 .bss —— 增大只改这一个宏。 */
#define BSL_UART_RX_MAX 256U

bsl_err_t bsl_uart_init(void);
int bsl_uart_ready(void);

/* DTB 给的波特率与实算出的 BRR（POST 把两个都打出来：BRR 偏了就是分频错）。 */
uint32_t bsl_uart_baud(void);
uint32_t bsl_uart_brr(void);

/* 发一个字节并等 TC（发送完成），*us 给出实测耗时（µs）。 */
bsl_err_t bsl_uart_send_byte(uint8_t b, uint32_t *us);

/* 连续发 n 个字节（TXE 排队，最后等一次 TC），*us 给出实测总耗时。
 *
 * 为什么判分频要用这个而不是逐字节：单字节的窗口里含"写 DR 到起始位上线"和
 * "帧尾到 TC 置位"两段各 0..1 bit 的量化，量出来接近 11 bit（实测 95 µs vs
 * 10 bit 理论 86.8 µs）；连发 n 个字节时这段量化只发生一次，摊薄后
 * "总耗时 vs n*10*bit_time"才真正等于分频误差。POST 用 n=16。 */
bsl_err_t bsl_uart_send(const uint8_t *buf, unsigned n, uint32_t *us);

/* 非阻塞取一帧：缓冲区里有字节、且距最后一个字节已超过 DTB 的 frame-idle-ms
 * 才算一帧完整（与参考工程的 10 ms 空闲判据同语义）。返回 1 = 取到一帧。 */
int bsl_uart_recv_frame(uint8_t *buf, unsigned max, unsigned *len);

/* 把字节直接喂进帧状态机，**绕过硬件**。只给 POST 注入自证用：
 * 没有对端时，这是唯一能证明"3 个字节 + 空闲 = 1 帧"的办法。
 * 名字里带 inject 就是为了让读代码的人一眼看到它是测试通道。 */
void bsl_uart_inject(uint8_t b);

#endif /* BSL_UART_H */
