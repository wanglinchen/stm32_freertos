#ifndef FML_GUI_H
#define FML_GUI_H

#include <stdint.h>

#include "fml_font.h"

/*
 * 画布与文字（fml 层）：apl 只跟这一层打交道，不直接碰 bsl_fb。
 *
 * 依赖方向：fml → bsl 允许（中间件用底层能力），bsl 不回看 fml。
 *
 * 这里不做窗口、不做重绘队列 —— 那是 P4 的 fml_ui。P1 只有"往帧缓冲上画一次"
 * 这一件事；加调度之前先要有人证明"画一次"是对的。
 *
 * 坐标：横屏、左上原点、RGB565。
 */

void fml_gui_clear(uint16_t bg);
void fml_gui_fill(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint16_t color);
void fml_gui_frame(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint16_t color);

/* 任意两点之间的直线（Bresenham，含两端点）。负方向也画。 */
void fml_gui_line(int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint16_t color);

/* 写一行 ASCII 文本：等宽排布，码位不在字库里留一块背景色（不画豆腐块）。
 * bg 同时用作字形的背景填充，所以文字是不透明的。 */
void fml_gui_text(uint32_t x, uint32_t y, const char *text, const fml_font_t *font,
                  uint16_t fg, uint16_t bg);

uint32_t fml_gui_text_width(const char *text, const fml_font_t *font);

#endif
