/*
 * CAN1（P6e-1）。
 *
 * 分工口径：**芯片事实**（CAN1 实例、邮箱/FIFO/过滤器位域、APB1 挂载）写在这里；
 * **板级事实**（PA11/PA12 掩码、AF9、目标位率、是否回环）全部来自 DTB
 * （board_rt_can）。BTR 的 brp / tseg 由 HAL_RCC_GetPCLK1Freq() 现场算，
 * 代码里不留第二份时钟常量（PLLM/PLLN 一改，这里的数就过时）。
 *
 * 为什么默认回环：本板 PA11/PA12 与 USB 的 D-/D+ 共用 P9 跳线，且没有第二个
 * CAN 节点可对话。回环（BTR.LBKM=1）由 CAN 核内部把 Tx 直接送进 Rx，与跳线、
 * 收发器都无关 —— 这使"发一帧、自己收到同一帧"成为可上板自证的判据，而不是
 * 只能读寄存器的探针。真有对端时把 DTB 的 loopback 改 0。
 *
 * 参考工程有两处照抄会静默失效，这里明确避开：
 *   1) `CAN1->FMR &= 0 << 0;` 是空操作（任何数 & 0 后与 0 左移仍是 0），
 *      过滤器组一直停在初始化态、过滤器不生效 → 回环收到的帧全被丢弃。
 *      这里写 `FMR &= ~CAN_FMR_FINIT`（RM0090 要求离开初始化态过滤器才生效）。
 *   2) 参考工程 `CAN_RX0_INT_ENABLE=0`，收帧全靠查询；本工程同样走查询
 *      （POST 与 shell 都是拉取式），不占用中断。
 */
#ifndef BSL_CAN_H
#define BSL_CAN_H

#include "bsl_err.h"

#include <stdint.h>

/* 位率与 BTR 分频：成功返回 BSL_OK；PCLK1 与目标位率凑不出整数 tq
 * （tseg 在 8..20 里没有能整除的）返回 BSL_ERR_NOSUPPORT —— 宁可初始化失败，
 * 也不配一个"差 3%"的位率去和别人的 500 k 对话。 */
bsl_err_t bsl_can_init(void);
int bsl_can_ready(void);

/* 实算位率（Hz），以及 BTR 的两个关键数：brp（分频）、tseg（1+TS1+TS2）。
 * POST 把这三个数一起打出来 —— 位率对不上时，一眼能看出是 brp 还是 tseg 偏了。 */
uint32_t bsl_can_bit_rate(void);
void bsl_can_timing(uint32_t *brp, uint32_t *tseg);

/* 标准帧发送：id 取低 11 位，len 0..8。返回 BSL_OK = 已请求发送且邮箱被接收。
 * 回环模式下发送即刻完成；正常模式下没人 ACK 会一直重试到错误计数满，
 * 所以这里只判"请求被受理"，判"对方收到"要靠 recv。 */
bsl_err_t bsl_can_send(uint32_t id, const uint8_t *data, uint8_t len);

/* 非阻塞收帧：FIFO0 里有帧就取出来（最多 8 字节），返回 1；没有返回 0。 */
int bsl_can_recv(uint32_t *id, uint8_t *data, uint8_t *len);

#endif /* BSL_CAN_H */
