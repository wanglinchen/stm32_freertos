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

/* 码位怎么换算成表内下标 —— 这是字库唯一的"元数据"，渲染侧不必知道。
 *   U8     ：单字节码位，index = codepoint - first（ASCII 表就是这样）
 *   GB2312 ：双字节 GBK 码，index = (qu-1)*94 + (wei-1)，qu/wei 由码值减 0xA1 得到
 *            （GB2312 的 GBK 编码就是"区位码 + 0xA0A0"，1..87 区 × 94 位）
 * 为什么不能像 U8 那样"减 first 线性索引"：GBK 码值不是连续的 —— 每个区里
 * 0xFF 与 0x00 是空洞，87 个区只在码值空间里散着 8178 个点。
 */
#define FML_FONT_ENC_U8 0U
#define FML_FONT_ENC_GB2312 1U
#define FML_FONT_GB2312_QU 87U
#define FML_FONT_GB2312_WEI 94U

typedef struct {
    uint32_t width;         /* 字形宽（像素） */
    uint32_t height;        /* 字形高（像素） */
    const uint8_t *glyphs;  /* 行优先，每行 stride 字节，bit7 = 该字节最左像素 */
    uint32_t first;         /* 首个码位（GB2312 表填 0xA1A1，仅作文档） */
    uint32_t count;         /* 码位个数（GB2312 表 = 87 × 94） */
    uint8_t enc;            /* FML_FONT_ENC_* */
} fml_font_t;

/* 每行的字节数 = ceil(width/8)。8 宽的字库是 1，GBK 16×16 是 2。 */
uint32_t fml_font_stride(const fml_font_t *font);

/* 取字形数据；码位不在字库里返回 NULL（渲染侧跳过，不画豆腐块）。 */
const uint8_t *fml_font_glyph(const fml_font_t *font, uint32_t codepoint);

int fml_font_has(const fml_font_t *font, uint32_t codepoint);

/* 一段 ASCII 文本的像素宽度（等宽字库：字宽 × 字符数）。 */
uint32_t fml_font_text_width(const fml_font_t *font, const char *text);

/* P1 的内嵌 ASCII 表（8×16，0x20..0x7E），由 fml/tools/gen_ascii8x16.py 生成。 */
extern const fml_font_t fml_font_ascii8x16;

/* P3 的内嵌 GB2312 表（16×16，1..87 区 × 94 位 = 8178 格），由
 * fml/tools/gen_gbk16.py 用系统宋体生成。属于"自带字库"：板上 NOR 卷里
 * 没有任何字库文件（P3 勘探结论），所以不依赖 `fml_fatfs`。
 *
 * 空区（10..15 区）也占格子（全 0 字形），这样下标计算不需要第二张偏移表；
 * 空区取字返回的是一块全 0 数据，画出来就是空白 —— 渲染侧不用特判。 */
extern const fml_font_t fml_font_gbk16;

#endif
