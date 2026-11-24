#include "fml_font.h"

uint32_t fml_font_stride(const fml_font_t *font)
{
    if (font == NULL || font->width == 0U) {
        return 0U;
    }
    return (font->width + 7U) / 8U;
}

const uint8_t *fml_font_glyph(const fml_font_t *font, uint32_t codepoint)
{
    uint32_t index;
    uint32_t stride;

    if (font == NULL || font->glyphs == NULL) {
        return NULL;
    }
    if (font->enc == FML_FONT_ENC_GB2312) {
        /* 双字节 GBK 码：高字节减 0xA1 得"区"（0 基），低字节减 0xA1 得"位"。
         * 小于 0xA1 的任何一半都不是 GB2312 区（0x81..0xA0 起首的码是 GBK 扩展
         * 区，1..87 区里根本没有那些字），直接判缺字而不是偏移到邻区。 */
        uint32_t hi = (codepoint >> 8) & 0xFFU;
        uint32_t lo = codepoint & 0xFFU;
        uint32_t qu;
        uint32_t wei;

        if ((hi < 0xA1U) || (lo < 0xA1U)) {
            return NULL;
        }
        qu = hi - 0xA1U;
        wei = lo - 0xA1U;
        if ((qu >= FML_FONT_GB2312_QU) || (wei >= FML_FONT_GB2312_WEI)) {
            return NULL;
        }
        index = qu * FML_FONT_GB2312_WEI + wei;
    } else {
        /* 用减法判界，不用 codepoint < first + count：后者在 first 很大时会溢出，
         * 溢出后 0xFFFFFFFF 这类码位会被判成"在字库内"，读出一块越界内存当字形画。 */
        if (codepoint < font->first) {
            return NULL;
        }
        index = codepoint - font->first;
    }
    if (index >= font->count) {
        return NULL;
    }
    stride = fml_font_stride(font);
    return font->glyphs + index * stride * font->height;
}

int fml_font_has(const fml_font_t *font, uint32_t codepoint)
{
    return (fml_font_glyph(font, codepoint) != NULL) ? 1 : 0;
}

uint32_t fml_font_text_width(const fml_font_t *font, const char *text)
{
    uint32_t chars = 0U;

    if (font == NULL || text == NULL) {
        return 0U;
    }
    while (text[chars] != '\0') {
        chars++;
    }
    return chars * font->width;
}
