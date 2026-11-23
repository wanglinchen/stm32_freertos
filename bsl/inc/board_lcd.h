#ifndef BOARD_LCD_H
#define BOARD_LCD_H

#include <stdint.h>

#include "bsl_err.h"

/*
 * LTDC 直驱的 4.3 寸 800×480 RGB 面板（面板 ID 0x4384）。
 *
 * 与 sdram 同构：init 返回 bsl_err_t，由 bsl_init_run() 写成 READY/FAILED；
 * 操作面（base/width/height/fill）给 fml/apl 用，消费方先看 bsl_device_is_ready()。
 *
 * 依赖 SDRAM：帧缓冲落在 sdram 节点的基址上（DTB 的 framebuffer phandle 指向它），
 * 所以注册表里本项排在 sdram 之后 —— 两者同为 BOARD 级，轮内按宏行序初始化。
 *
 * 时序、像素时钟的 PLLSAI 参数、像素格式、引脚、背光脚全部来自 DTB（board_rt_panel），
 * 代码里不出现第二份 800/480 或 33 MHz。
 */

bsl_err_t board_lcd_init(void);

/* 帧缓冲地址；未初始化返回 NULL。 */
void *board_lcd_base(void);
uint32_t board_lcd_width(void);
uint32_t board_lcd_height(void);

/* 背光开关：1 点亮。极性来自 DTB 的 backlight-gpios 第三 cell。 */
void board_lcd_backlight(int on);

/* 在帧缓冲上填一个 RGB565 矩形：坐标按横屏、左上原点，(x,y) 含、(x+w,y+h) 不含。
 * 越界部分裁掉，不做回绕。线/字/控件不在这里 —— 那是 P1 的 bsl_fb 与 fml_gui。 */
void board_lcd_fill(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint16_t rgb565);

/* 寄存器读回自检：按 DTB 重算 SSCR/BPCR/AWCR/TWCR 的期望值逐项比对，再核对第一层
 * 的 CFBAR 是否等于帧缓冲、CFBLNR 是否等于高度、PFCR 是否等于像素格式。
 * 返回不一致项数，0 = 通过；未初始化返回 0xFFFFFFFF。
 *
 * 为什么读回寄存器而不是"画一块再看"：屏对不对由人眼判断，寄存器对不对由固件判断。
 * 时序写错时画面可以是"整体偏移"或"边缘抖动"，肉眼只能看到"不对劲"，读回能指出
 * 是哪一项。 */
unsigned board_lcd_selftest(void);

#endif
