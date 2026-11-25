#include "bsl_fb.h"

#include "board_lcd.h"

/* 帧缓冲只有一个来源：board_lcd。这里不缓存指针与宽高 —— 缓存之后一旦
 * 面板重新初始化（P4 切分辨率或双缓冲），bsl_fb 会继续往旧地址写，
 * 而屏上看不出"写错了地方"。 */
static volatile uint16_t *fb_ptr(void)
{
    return (volatile uint16_t *)board_lcd_base();
}

void bsl_fb_clear(uint16_t color)
{
    if (fb_ptr() == NULL) {
        return;
    }
    board_lcd_fill(0U, 0U, board_lcd_width(), board_lcd_height(), color);
}

void bsl_fb_pixel(uint32_t x, uint32_t y, uint16_t color)
{
    volatile uint16_t *fb = fb_ptr();

    if (fb == NULL || x >= board_lcd_width() || y >= board_lcd_height()) {
        return;
    }
    fb[y * board_lcd_width() + x] = color;
    board_lcd_dirty_mark(x, y, 1U, 1U);
}

void bsl_fb_hline(uint32_t x, uint32_t y, uint32_t w, uint16_t color)
{
    volatile uint16_t *fb = fb_ptr();
    uint32_t width;
    uint32_t end;
    uint32_t i;

    if (fb == NULL) {
        return;
    }
    width = board_lcd_width();
    if (y >= board_lcd_height() || x >= width) {
        return;
    }
    end = x + w;
    if (end > width) {
        end = width;
    }
    for (i = x; i < end; i++) {
        fb[y * width + i] = color;
    }
    board_lcd_dirty_mark(x, y, end - x, 1U);
}

void bsl_fb_vline(uint32_t x, uint32_t y, uint32_t h, uint16_t color)
{
    volatile uint16_t *fb = fb_ptr();
    uint32_t width;
    uint32_t height;
    uint32_t end;
    uint32_t i;

    if (fb == NULL) {
        return;
    }
    width = board_lcd_width();
    height = board_lcd_height();
    if (x >= width || y >= height) {
        return;
    }
    end = y + h;
    if (end > height) {
        end = height;
    }
    for (i = y; i < end; i++) {
        fb[i * width + x] = color;
    }
    board_lcd_dirty_mark(x, y, 1U, end - y);
}

void bsl_fb_fill(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint16_t color)
{
    if (fb_ptr() == NULL) {
        return;
    }
    board_lcd_fill(x, y, w, h, color);
}

void bsl_fb_frame(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint16_t color)
{
    if (w == 0U || h == 0U) {
        return;
    }
    bsl_fb_hline(x, y, w, color);
    if (h > 1U) {
        bsl_fb_hline(x, y + h - 1U, w, color);
    }
    bsl_fb_vline(x, y, h, color);
    if (w > 1U) {
        bsl_fb_vline(x + w - 1U, y, h, color);
    }
}

void bsl_fb_mono(uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                 const uint8_t *bits, uint16_t fg, uint16_t bg, int opaque)
{
    volatile uint16_t *fb = fb_ptr();
    uint32_t stride;
    uint32_t width;
    uint32_t height;
    uint32_t row;
    uint32_t col;

    if (fb == NULL || bits == NULL || w == 0U || h == 0U) {
        return;
    }
    width = board_lcd_width();
    height = board_lcd_height();
    if (x >= width || y >= height) {
        return;
    }
    stride = (w + 7U) / 8U;
    for (row = 0U; row < h; row++) {
        uint32_t py = y + row;
        if (py >= height) {
            break;
        }
        for (col = 0U; col < w; col++) {
            uint32_t px = x + col;
            uint32_t byte = bits[row * stride + col / 8U];
            int on = ((byte >> (7U - (col & 7U))) & 1U) != 0U;

            if (px >= width) {
                break;
            }
            if (on) {
                fb[py * width + px] = fg;
            } else if (opaque) {
                fb[py * width + px] = bg;
            }
        }
    }
    /* 只标真正落到屏内的那块：y 越界时循环会 break，整块标脏会白推一遍。 */
    board_lcd_dirty_mark(x, y, w, (y + h > height) ? (height - y) : h);
}

uint32_t bsl_fb_count_nonbg(uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                            uint16_t bg)
{
    volatile uint16_t *fb = fb_ptr();
    uint32_t width;
    uint32_t height;
    uint32_t row;
    uint32_t col;
    uint32_t count = 0U;

    if (fb == NULL) {
        return 0U;
    }
    width = board_lcd_width();
    height = board_lcd_height();
    for (row = 0U; row < h; row++) {
        uint32_t py = y + row;
        if (py >= height) {
            break;
        }
        for (col = 0U; col < w; col++) {
            uint32_t px = x + col;
            if (px >= width) {
                break;
            }
            if (fb[py * width + px] != bg) {
                count++;
            }
        }
    }
    return count;
}
