#ifndef FML_FONT_H
#define FML_FONT_H

#include <stdint.h>

/*
 * 字库访问层：把「码位 → 一份字形位图」抽成唯一入口，数据来自哪个源
 * （P1 的内嵌 ASCII 表 / P3 的 NOR 上 GBK16 字库）由 fml_font_t 的数据指针决定，
 * 渲染侧不关心。
 *
 * 为什么要抽：P3 接 NOR 字库时，字形是按码位算出偏移、再从 SPI NOR 读的。
 * 一旦渲染侧习惯"glyphs 是 const 数组、能直接索引"，换成"每次取字先发 4 字节
 * 地址"就得挨处改；现在只改 fml_font_glyph 的实现。
 */

typedef struct {
    uint32_t width;         /* 字形宽（像素） */
    uint32_t height;        /* 字形高（像素） */
    const uint8_t *glyphs;  /* 行优先，每行 stride 字节，bit7 = 该字节最左像素 */
    uint32_t first;         /* 首个码位 */
    uint32_t count;         /* 码位个数 */
} fml_font_t;

/* 每行的字节数 = ceil(width/8)。8 宽的字库是 1，GBK 16×16 是 2。 */
uint32_t fml_font_stride(const fml_font_t *font);

/* 取字形数据；码位不在字库里返回 NULL（渲染侧跳过，不画豆腐块）。 */
const uint8_t *fml_font_glyph(const fml_font_t *font, uint32_t codepoint);

int fml_font_has(const fml_font_t *font, uint32_t codepoint);

/* 一段 ASCII 文本的像素宽度（等宽字库：字宽 × 字符数）。 */
uint32_t fml_font_text_width(const fml_font_t *font, const char *text);

/* P1 的内嵌 ASCII 表（8×16，0x20..0x7E），由 fml/tools/gen_ascii8x16.py 生成。
 * 只服务 P1 的"屏上真的出现文字"这一条验收；P3 之后由 NOR 字库接管。 */
extern const fml_font_t fml_font_ascii8x16;

#endif
