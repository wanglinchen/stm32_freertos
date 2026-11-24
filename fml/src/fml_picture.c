#include "fml_picture.h"

#include <string.h>

#include "bsl_fb.h"
#include "fml_fatfs.h"

/* BMP 两段头部：
 *   文件头 14 B：'B''M' + size(4) + reserved(4) + data_off(4)
 *   DIB  头 ≥40 B：w(4,有符号) + h(4,有符号) + planes(2) + bpp(2) + compression(4)
 *                  + image_size(4) + xppm + yppm + clr_used + clr_important
 * 后面的调色板/掩码字段在不压缩 24/32 位时用不到，不解析。 */
#define BMP_FILE_HDR 14U
#define BMP_DIB_MIN 40U
#define BMP_HDR_TOTAL (BMP_FILE_HDR + BMP_DIB_MIN)

#define BMP_COMPRESSION_RGB 0U

static uint8_t s_row[(FML_PICTURE_MAX_W * 4U) + 8U];
static uint8_t s_hdr[BMP_HDR_TOTAL];

static uint16_t rd_u16(const uint8_t *p)
{
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static uint32_t rd_u32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

static int32_t rd_i32(const uint8_t *p)
{
    return (int32_t)rd_u32(p);
}

/* 每行字节数：BMP 行按 4 字节对齐（不是压缩格式才有对齐，未压缩也有）。 */
static uint32_t row_bytes(int32_t w, uint16_t bpp)
{
    uint32_t bits = (uint32_t)w * (uint32_t)bpp;

    return (bits + 31U) / 32U * 4U;
}

bsl_err_t fml_picture_info(const char *path, fml_picture_info_t *info)
{
    uint32_t got = 0U;
    uint32_t rb;
    uint32_t payload;
    int32_t h_raw;
    int32_t w;

    if ((path == NULL) || (info == NULL)) {
        return BSL_ERR_INVAL;
    }
    (void)memset(info, 0, sizeof(*info));

    if (fml_fatfs_open(path) != BSL_OK) {
        return BSL_ERR_IO;
    }
    info->file_size = fml_fatfs_size();
    if ((info->file_size < BMP_HDR_TOTAL) ||
        (fml_fatfs_read(s_hdr, BMP_HDR_TOTAL, &got) != BSL_OK) ||
        (got < BMP_HDR_TOTAL)) {
        fml_fatfs_close();
        return BSL_ERR_IO;
    }
    fml_fatfs_close();

    if ((s_hdr[0] != 'B') || (s_hdr[1] != 'M')) {
        return BSL_ERR_NOSUPPORT;
    }
    info->data_off = rd_u32(&s_hdr[10]);
    w = rd_i32(&s_hdr[18]);
    h_raw = rd_i32(&s_hdr[22]);
    info->bpp = rd_u16(&s_hdr[28]);
    /* 负数高度 = 自上而下存储。取绝对值存进 h，方向单独记一位：
     * 让"图的尺寸"只有一个正数表示，方向不混进算数里。 */
    info->top_down = (h_raw < 0) ? 1U : 0U;
    info->h = (h_raw < 0) ? -h_raw : h_raw;
    info->w = w;

    if (rd_u32(&s_hdr[30]) != BMP_COMPRESSION_RGB) {
        return BSL_ERR_NOSUPPORT;
    }
    if ((info->bpp != 24U) && (info->bpp != 32U)) {
        return BSL_ERR_NOSUPPORT;
    }
    if ((w <= 0) || (info->h <= 0) || (w > (int32_t)FML_PICTURE_MAX_W)) {
        return BSL_ERR_NOSUPPORT;
    }

    /* 长度反推：data_off + 行字节 × 高 必须 ≤ 文件长度。少一行的文件会让
     * 解码读到 0xFF（空白 NOR），画出来是一条亮边而不报错 —— 这里直接判死。 */
    rb = row_bytes(w, info->bpp);
    if ((info->h > 0) && ((uint32_t)info->h > (0xFFFFFFFFU / rb))) {
        return BSL_ERR_NOSUPPORT;
    }
    payload = rb * (uint32_t)info->h;
    if ((info->data_off > info->file_size) ||
        (payload > (info->file_size - info->data_off))) {
        return BSL_ERR_NOSUPPORT;
    }
    return BSL_OK;
}

bsl_err_t fml_picture_draw(const char *path, int32_t x, int32_t y, uint32_t *ink_out)
{
    fml_picture_info_t info;
    uint32_t rb;
    uint32_t got = 0U;
    uint32_t ink = 0U;
    int32_t row;

    if (ink_out != NULL) {
        *ink_out = 0U;
    }
    if (fml_picture_info(path, &info) != BSL_OK) {
        return BSL_ERR_NOSUPPORT;
    }
    rb = row_bytes(info.w, info.bpp);
    if (rb > (uint32_t)sizeof(s_row)) {
        return BSL_ERR_NOSUPPORT;
    }
    if ((fml_fatfs_open(path) != BSL_OK) || (fml_fatfs_seek(info.data_off) != BSL_OK)) {
        fml_fatfs_close();
        return BSL_ERR_IO;
    }

    /* 顺序读，不逐行 seek：BMP 的像素数据在文件里就是连续的一片，
     * 只有"行内是从左到右"这一条是固定的；行与行的上下顺序由 top_down 决定，
     * 在**写到帧缓冲时**翻转即可。逐行 seek 会把 240 次块内定位的开销白付一遍。 */
    for (row = 0; row < info.h; row++) {
        uint32_t col;
        uint32_t dy;

        if ((fml_fatfs_read(s_row, rb, &got) != BSL_OK) || (got < rb)) {
            fml_fatfs_close();
            return BSL_ERR_IO;
        }
        dy = (info.top_down != 0U) ? (uint32_t)(y + row)
                                   : (uint32_t)(y + (info.h - 1 - row));
        for (col = 0U; col < (uint32_t)info.w; col++) {
            const uint8_t *px = &s_row[col * (uint32_t)(info.bpp / 8U)];
            /* BMP 存的是 BGR(A)；RGB565 取高 5/6/5 位。 */
            uint16_t color = (uint16_t)(((uint16_t)(px[2] >> 3) << 11) |
                                        ((uint16_t)(px[1] >> 2) << 5) |
                                        (uint16_t)(px[0] >> 3));

            if (color != 0U) {
                ink++;
            }
            bsl_fb_pixel((uint32_t)x + col, dy, color);
        }
    }
    fml_fatfs_close();
    if (ink_out != NULL) {
        *ink_out = ink;
    }
    return BSL_OK;
}
