/*
 * 板级器件：TPAD（电容触摸按键，PA5 → TIM2_CH1 输入捕获）。
 *
 * 检出原理（不是 ADC 采样）：把按键当成一小块电容。
 *   1) 引脚配成推挽输出 0，放电 charge_ms；
 *   2) 切回 AF 浮空输入，外部上拉电阻开始给这块电容充电；
 *   3) 用输入捕获量"充电到 VIH 用了多少个计数"—— 手指按下增大电容，
 *      充电变慢、计数值变大。
 * 所以"键值"就是计数值，判定门限是 `default + gate_val`。
 *
 * 为什么捕获上升沿：放电到 0 之后电平由低走高，第一次越过 VIH 就是我们要的时间点。
 * 定时器自由计数、ARR 取 0xFFFF —— 计数溢出前一定已经越过 VIH，否则就是
 * "没有按键/引脚没接对"，此时读 CNT 当超时值返回（与参考工程同口径）。
 *
 * 本层不做"连击节流"以外的任何策略：`scan(mode)` 的 mode 与参考工程一致
 * （0 = 按一次要松开才能再按；1 = 可连发），节流计数留在 .c 里而不是抛给应用。
 */
#ifndef BOARD_TPAD_H
#define BOARD_TPAD_H

#include "bsl_err.h"

#include <stdint.h>

/* 上电定标：连采 10 次、去掉两端各 2 个、中间 6 个取平均作为空载基线。
 * 返回 BSL_OK 仅表示"基线在合理区间"（0 < default < arr_max/2），
 * 越界说明引脚没配好或板上有持续按压，必须当作失败上报。 */
bsl_err_t board_tpad_init(void);
int board_tpad_ready(void);

/* 空载基线（未初始化时为 0）。 */
uint16_t board_tpad_default(void);
/* 最近一次判定门限 = default + gate_val（上报用，便于现场对照）。 */
uint16_t board_tpad_threshold(void);

/* 扫描一次：返回 1 = 判定按下，0 = 未按下，负值 = 未初始化（BSL_ERR_NOINIT）。 */
int board_tpad_scan(int continuous);

#endif
