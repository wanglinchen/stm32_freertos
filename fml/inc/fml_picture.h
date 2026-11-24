#ifndef FML_PICTURE_H
#define FML_PICTURE_H

#include "bsl_err.h"

#include <stdint.h>

/*
 * 位图解码（P5c：图片查看器）。首版只做 **BMP / 未压缩 / 24 或 32 位**。
 *
 * 为什么从 BMP 开始：卷里现成的那张就是 `*.BMP 230454 B`，54 + 320×240×3 正好
 * 对上 —— 头部字段可以被"文件总长"反向验证，POST 因此有一条不依赖人眼的硬判据。
 * JPEG 要 IDCT、GIF 要 LZW，都不属于"一条 POST 行能判死"的量级。
 *
 * 不做缩放：1:1 画到指定位置。缩放要按目标行反算源行并逐行 seek，会把这批的
 * 验证面从"头部解析对不对"扩大到"重采样实现对不对"，等 1:1 版上板过了再加。
 *
 * 内存：只占一个**行缓冲**（`FML_PICTURE_MAX_W × 4`），不解码整幅到 RAM。
 * 卷里那张 320×240 ×3 = 230400 B，整幅缓冲要么吃掉 SDRAM 一大块、要么挤 .bss；
 * 逐行读只要 3 KB，代价是每行一次 f_read（FatFs 自己按 512 B 扇区缓冲，连续读
 * 命中同一扇区的次数由它内部吸收）。
 */

/* 支持的最大像素宽度。行缓冲 = MAX_W × 4（32 位 BMP 每像素 4 字节）。
 * 800 是面板宽度：比屏还宽的图 1:1 画本来就会溢出，不如当场拒绝。 */
#define FML_PICTURE_MAX_W 800U

typedef struct {
    uint32_t file_size;   /* 文件长度（不含目录项，就是 FatFs 报的 fsize） */
    uint32_t data_off;    /* 像素数据起始偏移（BMP 头里的 bfOffBits） */
    int32_t w;            /* 像素宽（BMP 头里存的是有符号，负数非法） */
    int32_t h;            /* 像素高；**负数 = 自上而下存储**，这里已取绝对值 */
    uint16_t bpp;         /* 每像素位数：24 或 32 */
    uint8_t top_down;     /* 1 = 行序自上而下（头部 h < 0） */
} fml_picture_info_t;

/* 解析头部。路径必须是 **长名**（卷里那张图在中文长名目录里，短名是
 * `??????~1` 式占位，拿它 open 会 FR_INVALID_NAME）。
 * 返回 BSL_OK / BSL_ERR_INVAL（参数）/ BSL_ERR_IO（打不开或读失败）/
 * BSL_ERR_NOSUPPORT（不是 BMP、压缩过、位深不支持、尺寸越界）。 */
bsl_err_t fml_picture_info(const char *path, fml_picture_info_t *info);

/* 1:1 画到 (x, y)，左上角对齐。*ink_out（可为 NULL）收到画出的**非黑**像素数：
 * 全黑图与"根本没画"在屏上一样，只有计数分得开。 */
bsl_err_t fml_picture_draw(const char *path, int32_t x, int32_t y, uint32_t *ink_out);

#endif /* FML_PICTURE_H */
