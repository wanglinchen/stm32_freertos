#include "fml_gui.h"

#include "bsl_fb.h"

void fml_gui_clear(uint16_t bg)
{
    bsl_fb_clear(bg);
}

void fml_gui_fill(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint16_t color)
{
    bsl_fb_fill(x, y, w, h, color);
}

void fml_gui_frame(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint16_t color)
{
    bsl_fb_frame(x, y, w, h, color);
}

/*
 * Bresenham：dx/dy 取绝对值，用 err 累积决定下一个点走对角线还是直线。
 * 不用"按长边逐点算 y = y0 + k·(x-x0)"：那要浮点，且斜率大时会在竖直方向
 * 留下 1 像素断点 —— 画窗口边框时肉眼可见。
 *
 * 负坐标不出屏：bsl_fb_pixel 自己会丢弃越界点，这里不做第二次裁剪。
 */
void fml_gui_line(int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint16_t color)
{
    int32_t dx = (x1 > x0) ? (x1 - x0) : (x0 - x1);
    int32_t dy = (y1 > y0) ? (y1 - y0) : (y0 - y1);
    int32_t sx = (x0 < x1) ? 1 : -1;
    int32_t sy = (y0 < y1) ? 1 : -1;
    int32_t err = dx - dy;

    for (;;) {
        if (x0 >= 0 && y0 >= 0) {
            bsl_fb_pixel((uint32_t)x0, (uint32_t)y0, color);
        }
        if (x0 == x1 && y0 == y1) {
            break;
        }
        /* err2 是 err 的两倍，省一次乘法；阈值判断与经典写法等价。 */
        {
            int32_t err2 = err * 2;
            if (err2 > -dy) {
                err -= dy;
                x0 += sx;
            }
            if (err2 < dx) {
                err += dx;
                y0 += sy;
            }
        }
    }
}

void fml_gui_text(uint32_t x, uint32_t y, const char *text, const fml_font_t *font,
                  uint16_t fg, uint16_t bg)
{
    uint32_t cx = x;

    if (font == NULL || text == NULL) {
        return;
    }
    while (*text != '\0') {
        uint32_t cp = (uint32_t)(unsigned char)(*text);
        const uint8_t *glyph = fml_font_glyph(font, cp);

        if (glyph != NULL) {
            bsl_fb_mono(cx, y, font->width, font->height, glyph, fg, bg, 1);
        } else {
            /* 缺字：留背景色块而不是跳过，否则后面的字会整体左移，
             * 一行里"少一个字"看起来像"字库错位"。 */
            bsl_fb_fill(cx, y, font->width, font->height, bg);
        }
        cx += font->width;
        text++;
    }
}

uint32_t fml_gui_text_width(const char *text, const fml_font_t *font)
{
    return fml_font_text_width(font, text);
}

void fml_gui_text_gbk(uint32_t x, uint32_t y, const char *text, uint16_t fg, uint16_t bg)
{
    const fml_font_t *cjk = &fml_font_gbk16;
    const fml_font_t *asc = &fml_font_ascii8x16;
    uint32_t cx = x;

    if (text == NULL) {
        return;
    }
    while (*text != '\0') {
        unsigned char c = (unsigned char)*text;

        if (c < 0x80U) {
            const uint8_t *glyph = fml_font_glyph(asc, (uint32_t)c);

            if (glyph != NULL) {
                bsl_fb_mono(cx, y, asc->width, asc->height, glyph, fg, bg, 1);
            } else {
                bsl_fb_fill(cx, y, asc->width, asc->height, bg);
            }
            cx += asc->width;
            text++;
        } else {
            uint32_t cp;
            const uint8_t *glyph;

            /* 落单的首字节（串到这里就结束）：吞掉不画。当作半个字去查表会
             * 查到"区号越界"或邻区字形，屏幕上出现一个莫名其妙的全角字。 */
            if (text[1] == '\0') {
                break;
            }
            cp = ((uint32_t)c << 8) | (uint32_t)(unsigned char)text[1];
            glyph = fml_font_glyph(cjk, cp);
            if (glyph != NULL) {
                bsl_fb_mono(cx, y, cjk->width, cjk->height, glyph, fg, bg, 1);
            } else {
                bsl_fb_fill(cx, y, cjk->width, cjk->height, bg);
            }
            cx += cjk->width;
            text += 2;
        }
    }
}

uint32_t fml_gui_text_gbk_width(const char *text)
{
    uint32_t width = 0U;

    if (text == NULL) {
        return 0U;
    }
    while (*text != '\0') {
        if ((unsigned char)*text < 0x80U) {
            width += fml_font_ascii8x16.width;
            text++;
        } else {
            if (text[1] == '\0') {
                break;
            }
            width += fml_font_gbk16.width;
            text += 2;
        }
    }
    return width;
}
