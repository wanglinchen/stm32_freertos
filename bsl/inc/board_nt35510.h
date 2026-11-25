#ifndef BOARD_NT35510_H
#define BOARD_NT35510_H

#include <stdint.h>

#include "bsl_err.h"

/*
 * NT35510（4.3" 480×800 MCU 屏，板上实插的那块，读回 ID 0x5510）。
 *
 * 这一层只知道"这颗 IC 的寄存器表与开窗/写 GRAM 时序"，总线在 bsl_lcd_8080 里。
 * 初始化表是从原厂固件 lcd_ex.c 的 lcd_ex_nt35510_reginit() 机械抽取的 381 条
 * （不是手抄），抽取脚本与校验计数见 P7 的文档记录：条目数少一条现场就是花屏/偏色。
 *
 * 开窗为什么按"起点 + 逐行重置"：NT35510 的列/页地址寄存器是 0x2A00/0x2A01、
 * 0x2B00/0x2B01 四个 8 位寄存器（高/低字节分开），窗口终点由初始化表设成整屏，
 * 所以每行只需把起点拍到位，写 w 个像素后地址自增不会越到下一行首。
 * 原厂 lcd_set_cursor() 对 5510 就是这么做的 —— 这里跟它一致。
 */
bsl_err_t board_nt35510_init(uint32_t expect_id);

/* 是否初始化通过（ID 对得上 + 表写成功 + 开显示）。 */
int board_nt35510_ready(void);

/* 读回驱动 IC 的 ID（0xC500/0xC501 两字节拼成 0x5510）。先发 0xF000..0xF004
 * 秘钥，否则 NT35510 不应答；读完必须等 5 ms（0xC501 对别的 IC 是软复位）。 */
uint16_t board_nt35510_id(void);

/* 初始化时读到的 ID（不再碰总线）。POST 打印它作为"读回的是这颗 IC"的证据。 */
uint16_t board_nt35510_detected_id(void);

/* 把帧缓冲里 (x,y,w,h) 这块推进 GRAM。src 是内存里的 RGB565 行主序，
 * stride 是一行的像素数（通常等于屏宽，允许比 w 大）。
 * 返回实际写入的像素数，用来和 w*h 对账。 */
uint32_t board_nt35510_blit(uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                            const uint16_t *src, uint16_t stride);

/* 读 GRAM 里一个像素（POST 的"写读一像素"判据用它）。 */
uint16_t board_nt35510_read_pixel(uint16_t x, uint16_t y);

#endif
