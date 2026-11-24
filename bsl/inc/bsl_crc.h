/*
 * 硬件 CRC32（STM32 CRC 单元）。
 *
 * 单元参数固定为 CRC-32/MPEG-2：poly 0x04C11DB7、init 0xFFFFFFFF、输入输出都不
 * 反射、结果不取反。F429 的 CRC 只有 DR/IDR/CR 三个寄存器，**没有** INIT 寄存器
 * （与 F0/L4 不同），改初值只能走 CR.RESET —— 复位后 DR 即为 0xFFFFFFFF。
 *
 * 只提供"按 32 位字算"这一种形态：单元本身只吃字，按字节算要自己打包尾字节，
 * 而"补 0 会改变结果"这件事一旦藏进驱动，调用方就永远对不上任何标准测试向量。
 */
#ifndef BSL_CRC_H
#define BSL_CRC_H

#include <stdint.h>

/* 对 count 个 32 位字做硬件 CRC32，返回结果。每次调用先复位单元（DR = init）。 */
uint32_t bsl_crc32_words(const uint32_t *words, unsigned count);

#endif /* BSL_CRC_H */
