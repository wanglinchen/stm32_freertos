/*
 * 片内 DAC1_OUT1（PA4）+ 出厂电压闭环自证。
 *
 * 为什么只做 OUT1：DAC1_OUT2 固定是 PA5，而本板 PA5 归 TPAD 的 TIM2_CH1
 * 输入捕获（对照表 §1.4 第 146 行）。两路一起开就是一根脚两个主人，
 * 所以 DTS 里 `channel` 只认 1，其余值直接 NOSUPPORT。
 *
 * 为什么这是"自检类"而不进设备枚举：唯一的消费者是本批的 POST 行。
 * 应用侧没有任何"要一路模拟电压"的需求，注册成 BSL_DEV_DAC 只会多一个
 * "注册了没人用"的面子接口（对照表 §1.4「注册门槛」原文）。
 */
#ifndef BSL_DAC_H
#define BSL_DAC_H

#include <stdint.h>

#include "bsl_err.h"

/* 12 位码的上界。POST 判据与驱动共用这一个常量，避免两处各写一个 4095。 */
#define BSL_DAC_CODE_MAX 4095U

/* 幂等初始化：PA4 置模拟态（数字缓冲关掉，DAC 直接驱动引脚，同时兼作
 * ADC1_IN4 的输入）、开 DAC 时钟、CH1 普通模式 + 软件触发 + 输出缓冲开。 */
bsl_err_t bsl_dac_init(void);

/* DAC 的输入时钟：APB1（PCLK1）。参考工程注释写 42 MHz，本板也是 42 MHz，
 * 但这里现算 —— 抄常量在换时钟树那天会静默偏。 */
uint32_t bsl_dac_clock_hz(void);

/* 写 DHR12R1（0..4095）。越界返回 INVAL，不静默截断。 */
bsl_err_t bsl_dac_set_code(uint16_t code);

/* 回读 DOR1（转换后的 12 位码）。内部先等建立时间（~1 µs）再读：
 * 写完立刻读会拿到上一拍的码，现象是"写 3584 读 2048"，很像驱动坏了。 */
uint16_t bsl_dac_code(void);

#endif /* BSL_DAC_H */
