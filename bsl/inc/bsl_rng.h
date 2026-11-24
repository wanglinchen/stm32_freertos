/*
 * 硬件随机数发生器（RNG，AHB2）。
 *
 * 时钟来自 PLL48CK（本工程 PLLQ=7 → 25 MHz × 336/25 / 7 = 48 MHz），
 * 未配 PLL48CK 时 DRDY 永远不起来 —— 那是"超时"而不是"随机数坏了"，
 * 所以两种失败必须给不同的码。
 *
 * 与 ADC/CRC 一样属于自检类，不进设备枚举（对照表 §1.4「注册门槛」）。
 */
#ifndef BSL_RNG_H
#define BSL_RNG_H

#include <stdint.h>

#include "bsl_err.h"

bsl_err_t bsl_rng_init(void);

/* 读一个 32 位随机数。种子错误（SECS）返回 BSL_ERR_IO，并清掉错误位 ——
 * 不清的话 DRDY 再也不会起来，后续调用全部 TIMEOUT，现场看起来像"RNG 死了"。 */
bsl_err_t bsl_rng_read(uint32_t *out);

#endif /* BSL_RNG_H */
