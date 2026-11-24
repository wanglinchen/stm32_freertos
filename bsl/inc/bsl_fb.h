#ifndef BSL_FB_H
#define BSL_FB_H

#include <stdint.h>

/*
 * 帧缓冲原语：全部落在 board_lcd 的帧缓冲上（SDRAM），横屏、左上原点、RGB565。
 *
 * 不持有自己的缓冲，也不做双缓冲：那要等 P4 的窗口系统有了明确的"谁负责重绘"
 * 才定得下来，现在加一层只会让每个调用点多一次整屏拷贝。
 *
 * 面板未初始化时全部静默返回：调用方（fml_gui / apl）不必每处都查
 * bsl_device_is_ready(BSL_DEV_PANEL)，状态由 POST 的 lcd 项统一兜底。
 *
 * 矩形填充这里不重写：board_lcd_fill 已经有裁剪与越界处理，再写一份就是
 * 两处裁剪规则各自漂移。
 */

void bsl_fb_clear(uint16_t color);
void bsl_fb_pixel(uint32_t x, uint32_t y, uint16_t color);
void bsl_fb_hline(uint32_t x, uint32_t y, uint32_t w, uint16_t color);
void bsl_fb_vline(uint32_t x, uint32_t y, uint32_t h, uint16_t color);
void bsl_fb_fill(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint16_t color);

/* 只画矩形边框（四边各 1 像素），不填内部。 */
void bsl_fb_frame(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint16_t color);

/* 单色位图：bits 行优先，每行 ceil(w/8) 字节，bit7 = 最左像素。
 * opaque=1 时背景像素也写 bg；=0 时只写前景，用于叠字/透明贴图。 */
void bsl_fb_mono(uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                 const uint8_t *bits, uint16_t fg, uint16_t bg, int opaque);

/* 统计矩形内非 bg 的像素数。
 * POST 的 font 项用它做双向判据：空格区域必须为 0（证明字库没串位）、
 * 字母区域必须 >0（证明真的画进去了）。只看"画完后像素数 >0"会让一张
 * 全错位的表也通过。 */
uint32_t bsl_fb_count_nonbg(uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                            uint16_t bg);

#endif
