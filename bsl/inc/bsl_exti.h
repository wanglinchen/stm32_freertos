#ifndef BSL_EXTI_H
#define BSL_EXTI_H

#include "bsl_device.h"
#include "bsl_device_api.h"

/* EXTI（P9c-2）：EXTI0 源自 PA0（WK_UP）。
 *
 * 判据为什么"自己打脉冲"：EXTI 的整条链是「引脚边沿 → AFIO/EXTICR 选源 →
 * 边沿检测 → 挂起位 PR → NVIC → ISR」，要证明它，必须有真实边沿。板上
 * WK_UP 是等人手指的，自动化跑不到，所以驱动在自检里把 PA0 **短暂**改成推挽
 * 输出、打一个高脉冲再收回输入 —— 边沿走的是真正的引脚与真正的 EXTI 线，
 * 不是绕过硬件直接写软件挂起位。
 *
 * 软件挂起（SWIER）另做一段：它证明的是"PR → NVIC → ISR → 清 PR"这后半段
 * 在中断使能、优先级、向量上都对。两段分开报数（pin=/sw=），哪一段断了
 * 串口上直接读得出来。
 *
 * 只实现 line 0..4（各自有独立 IRQ 号 6..10）；5..15 共用 EXTI9_5/EXTI15_10，
 * 等真有需求再开，不预先配一条没验证过的向量。 */
bsl_err_t bsl_exti_init(void);

extern const bsl_device_driver_t bsl_driver_brd_exti;
extern const bsl_exti_api_t bsl_api_brd_exti;

#endif /* BSL_EXTI_H */
