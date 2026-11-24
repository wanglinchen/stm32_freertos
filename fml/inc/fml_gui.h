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

/* 写一行 GBK 混排文本（P3）：ASCII 用 8×16 半角、其余用 GBK16 全角。
 *
 * 为什么单独一个函数而不是给 fml_gui_text 加参数：切字规则（首字节 ≥ 0x81 就是
 * 双字节）、缺字处理、步进宽度（8 还是 16）都跟"只有一个 font"的模型不同；
 * 塞进同一个函数会让每个调用点都多背一套分支。
 *
 * 文本必须是 GBK 字节串（C 源码是 UTF-8，写中文字面量要用 "\xD6\xD0" 这种转义，
 * 否则编译进去的是 UTF-8 字节，会被当成乱码切字）。
 *
 * 两种字形都是顶对齐（都从 y 开始画），不让基线差异把混排行拉歪。 */
void fml_gui_text_gbk(uint32_t x, uint32_t y, const char *text, uint16_t fg, uint16_t bg);

/* 同一行的像素宽度（半角 8 / 全角 16 逐字累加）。 */
uint32_t fml_gui_text_gbk_width(const char *text);

#endif
