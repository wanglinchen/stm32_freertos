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
    /* 用减法判界，不用 codepoint < first + count：后者在 first 很大时会溢出，
     * 溢出后 0xFFFFFFFF 这类码位会被判成"在字库内"，读出一块越界内存当字形画。 */
    if (codepoint < font->first) {
        return NULL;
    }
    index = codepoint - font->first;
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
