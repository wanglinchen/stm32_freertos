/*
 * 板级器件：AP3216C（ALS + PS + IR 三合一，7 位地址见 DTB）。
 *
 * 寄存器表是器件事实，写在这里；只有"在哪条总线、什么地址"来自 DTB。
 *
 * 初始化判据只有一条硬的：`0x00`（SYSTEM）写 0x04 复位 → 等 50 ms → 写 0x03
 * （ALS+PS+IR 全开）→ **回读必须等于 0x03**。AP3216C 是读写都支持的器件，
 * 所以这里能拿回读当真值 —— 与 WM8978（只写器件、只能数 ACK）不同。
 *
 * 数据手册的坑：ALS 与 PS+IR 同时开时，两次读之间要 >112.5 ms，
 * 否则读到的是上一次转换的旧值。本层不做节流（那属于消费者节奏），
 * 但在 read() 的注释里写明。
 */
#ifndef BOARD_AP3216C_H
#define BOARD_AP3216C_H

#include "bsl_err.h"

#include <stdint.h>

bsl_err_t board_ap3216c_init(void);
int board_ap3216c_ready(void);

/* 读 6 个数据寄存器（0x0A..0x0F）并按规定拼位：
 *   IR  = buf[1]<<2 | (buf[0] & 0x03)，buf[0].bit7 置起时该值无效（写 0）
 *   ALS = buf[3]<<8 | buf[2]
 *   PS  = (buf[5] & 0x3F)<<4 | (buf[4] & 0x0F)，buf[4].bit6 置起时无效（写 0）
 * 指针允许为 NULL（只关心其中一两项时不必给全）。 */
bsl_err_t board_ap3216c_read(uint16_t *ir, uint16_t *ps, uint16_t *als);

#endif
