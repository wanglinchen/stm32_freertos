#ifndef BOARD_SDRAM_H
#define BOARD_SDRAM_H

#include <stdint.h>

#include "bsl_err.h"

/*
 * FMC Bank5 上的 SDRAM（W9825G6KH，16 位，32 MB）。
 *
 * 与 led/key 同构：init 返回 bsl_err_t，由 bsl_init_run() 写成 READY/FAILED；
 * 驱动操作面（base/size）给 fml/apl 用，消费方先看 bsl_device_is_ready()。
 *
 * 初始化顺序不可换：引脚 AF12 → FMC 时钟 → SDCR/SDTR → 时钟使能命令 → 预充电 →
 * 自动刷新 → 加载模式寄存器 → SDRTR。少一步的现场表现都是"写进去读不出来"。
 */

bsl_err_t board_sdram_init(void);

/* 帧缓冲可用的地址与容量；未初始化时分别返回 NULL / 0。 */
void *board_sdram_base(void);
uint32_t board_sdram_size(void);

/* 分页步长自检：每 4 KB 写一个模式字再读回，覆盖整个容量。
 * 返回不一致的点数，0 = 通过；未初始化返回 0xFFFFFFFF。
 *
 * 有意不逐字节：32 MB 全字节写读在 168 MHz 上要数秒，而 SDRAM 的典型故障
 * （数据线粘死、地址线错位、DQM 失效）在 4 KB 步长上一样会暴露。 */
unsigned board_sdram_selftest(void);

#endif
