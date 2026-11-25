#ifndef BOARD_LCD_H
#define BOARD_LCD_H

#include <stdint.h>

#include "bsl_err.h"

/*
 * 面板：一台"屏"设备，两种总线形态，由 DTB 的 panel 节点 bus 决定。
 *
 *   - RGB 屏（LTDC 直驱）：LTDC 按同步时序自己扫 SDRAM 帧缓冲，写完即显示。
 *   - MCU 屏（NT35510 + FMC 8080 并口，板上实插）：帧缓冲只是"待推的副本"，
 *     改完必须调 board_lcd_present() 把脏矩形推进 GRAM。
 *
 * 与 sdram 同构：init 返回 bsl_err_t，由 bsl_init_run() 写成 READY/FAILED；
 * 操作面（base/width/height/fill/present）给 fml/apl 用，消费方先看
 * bsl_device_is_ready()。
 *
 * 依赖 SDRAM：帧缓冲落在 sdram 节点的基址上（DTB 的 framebuffer phandle 指向它），
 * 所以注册表里本项排在 sdram 之后 —— 两者同为 BOARD 级，轮内按宏行序初始化。
 *
 * 宽高、像素格式、引脚、背光脚全部来自 DTB（board_rt_panel），代码里不出现
 * 第二份 480/800 或 33 MHz。
 */

bsl_err_t board_lcd_init(void);

/* 帧缓冲地址；未初始化返回 NULL。 */
void *board_lcd_base(void);
uint32_t board_lcd_width(void);
uint32_t board_lcd_height(void);

/* 背光开关：1 点亮。极性来自 DTB 的 backlight-gpios 第三 cell。 */
void board_lcd_backlight(int on);

/* 在帧缓冲上填一个 RGB565 矩形：坐标按屏坐标、左上原点，(x,y) 含、(x+w,y+h) 不含。
 * 越界部分裁掉，不做回绕。线/字/控件不在这里 —— 那是 bsl_fb 与 fml_gui。
 * MCU 屏下这一步只改内存，屏上要等 present()。 */
void board_lcd_fill(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint16_t rgb565);

/* 登记脏矩形（内部把多次调用并成一个大矩形）。
 * 所有写帧缓冲的路径都要过这里，否则 present() 会漏推，屏上停在旧画面。 */
void board_lcd_dirty_mark(uint32_t x, uint32_t y, uint32_t w, uint32_t h);

/* 把脏矩形推进屏。返回本次写入的像素数；0 = 没有脏区（或 RGB 屏无需推）。
 * MCU 屏一次全屏约 48 ms，所以按脏矩形推而不是每帧全推。 */
int board_lcd_present(void);

/* 整屏重推：切应用、清屏后不确定脏区时用。 */
void board_lcd_present_all(void);

/* 自检：返回不一致项数，0 = 通过；未初始化返回 0xFFFFFFFF。
 *
 *   - RGB 屏：按 DTB 重算 SSCR/BPCR/AWCR/TWCR 的期望值逐项比对寄存器，再核对
 *     第一层的 CFBAR/CFBLNR/PFCR。读回寄存器而不是"画一块再看"：屏对不对由人眼
 *     判断，寄存器对不对由固件判断，写错时序的画面只会"看起来不对劲"。
 *   - MCU 屏：没有时序寄存器可读，判据是端到端 —— 把帧缓冲中间那个点的颜色推进
 *     GRAM 再读回比对，同时验证总线、开窗与 GRAM 三条链。 */
unsigned board_lcd_selftest(void);

#endif
