#include "fml_audio.h"

#include <string.h>

#include "board_device_registry.h"
#include "bsl_device.h"
#include "bsl_sai.h"
#include "fml_fatfs.h"
#include "osl_thread.h"

/* 32 点正弦表（幅度 32767 的四分之一档：0.18 满幅）。用表而不是带 FPU 的 sinf：
 * 本工程没开 FPU 上下文（FreeRTOS 的 port 没配 FPU 保存），一旦任务里浮点，
 * 换任务就可能把另一个任务的浮点寄存器踩掉。整表 32 × int16 = 64 B。 */
static const int16_t k_sine[32] = {
    0,     639,   1255,  1846,  2403,  2914,  3369,  3760,
    4085,  4339,  4521,  4632,  4672,  4632,  4521,  4339,
    4085,  3760,  3369,  2914,  2403,  1846,  1255,  639,
    0,     -639,  -1255, -1846, -2403, -2914, -3369, -3760,
};

/* 幅度：正弦表本身是 ±4672，再乘这个增益。取 4 ⇒ ±18688（约 -5 dBFS），
 * 耳机/喇叭都能清楚听到但不炸耳；音量还可以用 vol 命令再调。 */
#define AUDIO_GAIN 4

int fml_audio_ready(void)
{
    return (bsl_device_is_ready(BSL_DEV_SAI) != 0)
        && (bsl_device_is_ready(BSL_DEV_CODEC) != 0)
        && (bsl_sai_ready() != 0);
}

uint32_t fml_audio_sample_rate(void)
{
    return bsl_sai_sample_rate();
}

bsl_err_t fml_audio_stop(void)
{
    return bsl_sai_stop();
}

bsl_err_t fml_audio_set_volume(unsigned vol)
{
    if (vol > 63U) {
        return BSL_ERR_INVAL;
    }
    return bsl_devapi_codec()->set_volume((uint8_t)vol);
}

bsl_err_t fml_audio_tone(unsigned hz, unsigned ms)
{
    uint32_t *buf;
    uint32_t step;      /* 每个样本的相位增量（32 点表的"点/样本"，定点 16.16） */
    uint32_t phase = 0U;
    unsigned i;

    if (hz < 20U || hz > 20000U || ms == 0U || ms > 5000U) {
        return BSL_ERR_INVAL;
    }
    if (!fml_audio_ready()) {
        return BSL_ERR_NOINIT;
    }
    buf = bsl_sai_buffer();
    if (buf == NULL) {
        return BSL_ERR_NOINIT;
    }

    /* 表是 32 点 = 一个整周期，所以每样本推进 (hz × 32 / fs) 个"表点"。
     * 用 16.16 定点：step = (hz × 32 × 65536) / fs。fs ≈ 48 kHz 时 1 kHz → 42.7
     * 表点/样本，足够平滑（一个周期 48 个样本）。 */
    step = (uint32_t)(((uint64_t)hz * 32U * 65536U) / bsl_sai_sample_rate());
    if (step == 0U) {
        step = 1U;
    }

    for (i = 0U; i < BSL_SAI_BUF_FRAMES; i++) {
        int16_t s = (int16_t)(k_sine[(phase >> 16) & 31U] * AUDIO_GAIN);

        phase += step;
        /* 一帧 32 位：高 16 位左声道、低 16 位右声道（SAI 的 DR 布局） */
        buf[i] = ((uint32_t)(uint16_t)s << 16) | (uint32_t)(uint16_t)s;
    }

    if (bsl_sai_start(BSL_SAI_BUF_FRAMES) != BSL_OK) {
        return BSL_ERR_IO;
    }
    /* 用 osl 的延时，不用 HAL_Delay：fml 不允许直接引用 hal（check 的层边组
     * 只放行 fml→bsl/osl）。语义也对 —— 这里要的是"让出 CPU 等这么久"。 */
    osl_thread_delay_ms(ms);    (void)bsl_sai_stop();
    return BSL_OK;
}

/* ======================================================================
 * wav 流播放（P9e-2）
 *
 * 结构：解析头 → 顺序拉源帧（512 B 缓冲，卷内文件用 FatFs 顺序读、内置样本用
 * memcpy）→ 线性插值重采样到 bsl_sai_sample_rate() → 填 SAI 缓冲 →
 * bsl_sai_play_blocking 逐块阻塞。
 *
 * 为什么重采样而不是"改 SAI 的帧率"：fs = MCLK/256，MCLK 来自 PLLI2S 的整数分频，
 * 44.1 kHz 在 49.125 MHz 下除不尽（最接近的 MCKDIV 也有 0.06% 偏差，且改帧长要
 * 动 FRCR/SLOT 的整条配置）。一首歌几十秒，插值每帧只做一次乘除，代价远小于
 * 一条会漂的位时钟。
 * ==================================================================== */

#define WAV_SRC_CHUNK 512U
#define WAV_FRAME_BYTES_MAX 8U      /* 2 ch × 32 bit */

typedef struct {
    int open;
    int from_file;
    uint32_t data_off;              /* data 载荷在文件内的偏移 */
    uint32_t data_size;
    uint32_t file_size;             /* 整个文件字节数（用来夹 data_size） */
    uint16_t block_align;
    uint16_t channels;
    uint16_t bits;
    uint32_t rate;
    uint32_t src_frames;            /* 源帧总数 */
    uint32_t src_pos;               /* 下一个要读的源帧号 */
    uint32_t out_left;              /* 还没发出的输出帧数 */
    int32_t prev;                   /* 插值左端 */
    int32_t cur;                    /* 插值右端 */
    uint32_t acc;                   /* 分数相位：0 ≤ acc < rate */
    uint32_t fs;                    /* 输出帧率（SAI 的固定 fs） */
    unsigned char buf[WAV_SRC_CHUNK];
    uint32_t buf_len;
    uint32_t buf_pos;
    uint32_t buf_off;               /* buf[0] 对应 data 区内的偏移 */
    int last_err;
} wav_state_t;

static wav_state_t s_wav;

static uint16_t wav_rd16(const unsigned char *p)
{
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static uint32_t wav_rd32(const unsigned char *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16)
         | ((uint32_t)p[3] << 24);
}

/* 全文件长度：卷内文件用 FatFs 的 size，内置样本用数组长度。 */
static uint32_t wav_file_size(void)
{
    if (s_wav.from_file != 0) {
        return fml_fatfs_size();
    }
    return (uint32_t)fml_audio_sample_wav_len;
}

/* 读文件内绝对偏移 off 起的 len 字节，整段成功才返回 1。
 * 骨架期（解析头）用这个随机读；音频数据用下面的 wav_pull 顺序读。 */
static int wav_raw_read(uint32_t off, void *dst, uint32_t len)
{
    uint32_t got = 0U;

    if (s_wav.from_file != 0) {
        if (fml_fatfs_seek(off) != BSL_OK) {
            return 0;
        }
        if (fml_fatfs_read(dst, len, &got) != BSL_OK || got < len) {
            return 0;
        }
        return 1;
    }
    if (off > (uint32_t)fml_audio_sample_wav_len) {
        return 0;
    }
    if ((uint32_t)fml_audio_sample_wav_len - off < len) {
        return 0;
    }
    memcpy(dst, &fml_audio_sample_wav[off], len);
    return 1;
}

/* 从 data 区顺序取 n 字节进 dst，返回实际取到的字节数（到尾部会不足）。
 * 每次重新 seek 到 buf_off 对应的绝对位置：FatFs 的 seek 只是簇内偏移重算，
 * 顺读时下一个块恰好是紧接着的位置，代价可以忽略。 */
static uint32_t wav_pull(void *dst, uint32_t n)
{
    unsigned char *out = dst;
    uint32_t got = 0U;

    while (got < n) {
        if (s_wav.buf_pos >= s_wav.buf_len) {
            uint32_t want = s_wav.data_size - s_wav.buf_off;
            uint32_t g = 0U;

            if (want > WAV_SRC_CHUNK) {
                want = WAV_SRC_CHUNK;
            }
            if (want == 0U) {
                break;
            }
            if (s_wav.from_file != 0) {
                if (!wav_raw_read(s_wav.data_off + s_wav.buf_off, s_wav.buf, want)) {
                    s_wav.last_err = (int)BSL_ERR_IO;
                    break;
                }
                g = want;
            } else {
                uint32_t off = s_wav.data_off + s_wav.buf_off;

                if (off >= (uint32_t)fml_audio_sample_wav_len) {
                    break;
                }
                g = (uint32_t)fml_audio_sample_wav_len - off;
                if (g > want) {
                    g = want;
                }
                memcpy(s_wav.buf, &fml_audio_sample_wav[off], g);
            }
            s_wav.buf_len = g;
            s_wav.buf_pos = 0U;
            s_wav.buf_off += g;
        }
        {
            uint32_t avail = s_wav.buf_len - s_wav.buf_pos;
            uint32_t take = (n - got < avail) ? (n - got) : avail;

            memcpy(out + got, &s_wav.buf[s_wav.buf_pos], take);
            s_wav.buf_pos += take;
            got += take;
        }
    }
    return got;
}

/* 读一源帧的第一声道并转成 int16（24/32 位取高 16 位，够听、也避开浮点）。 */
static int wav_read_frame(int32_t *out)
{
    unsigned char fb[WAV_FRAME_BYTES_MAX];

    if (s_wav.src_pos >= s_wav.src_frames) {
        return 0;
    }
    if (wav_pull(fb, s_wav.block_align) < s_wav.block_align) {
        return 0;
    }
    s_wav.src_pos++;
    if (s_wav.bits == 24U) {
        *out = (int32_t)(int16_t)(((uint16_t)fb[1] << 8) | (uint16_t)fb[2]);
    } else if (s_wav.bits == 32U) {
        *out = (int32_t)(int16_t)(((uint16_t)fb[2] << 8) | (uint16_t)fb[3]);
    } else {
        *out = (int32_t)(int16_t)(((uint16_t)fb[0] | ((uint16_t)fb[1] << 8)));
    }
    return 1;
}

/* 一个输出样本：按 src_rate/fs 的比例推进源游标，跨过的源帧之间做线性插值。 */
static int16_t wav_next_sample(void)
{
    int32_t d;
    int16_t out;

    while (s_wav.acc >= s_wav.rate) {
        int32_t nxt = 0;

        s_wav.prev = s_wav.cur;
        if (s_wav.src_pos < s_wav.src_frames && wav_read_frame(&nxt)) {
            s_wav.cur = nxt;
        } else {
            s_wav.cur = 0;
        }
        s_wav.acc -= s_wav.rate;
    }
    d = s_wav.cur - s_wav.prev;
    out = (int16_t)(s_wav.prev
                    + (int32_t)(((int64_t)d * (int64_t)s_wav.acc) / (int64_t)s_wav.rate));
    s_wav.acc += s_wav.fs;
    return out;
}

/* 解析 RIFF/WAVE：只认 PCM（1）与 extensible（0xFFFE，按 PCM 读）。 */
static bsl_err_t wav_parse(fml_audio_wav_t *info)
{
    unsigned char hdr[12];
    unsigned char ck[8];
    unsigned char fmt[16];
    uint32_t off = 12U;
    uint32_t size = wav_file_size();
    int have_fmt = 0;
    int have_data = 0;

    if (!wav_raw_read(0U, hdr, 12U)) {
        return BSL_ERR_IO;
    }
    if (memcmp(hdr, "RIFF", 4) != 0 || memcmp(&hdr[8], "WAVE", 4) != 0) {
        return BSL_ERR_INVAL;
    }
    while (off + 8U <= size) {
        uint32_t ck_size;

        if (!wav_raw_read(off, ck, 8U)) {
            return BSL_ERR_IO;
        }
        ck_size = wav_rd32(&ck[4]);
        if (memcmp(ck, "fmt ", 4) == 0) {
            uint16_t af;

            if (ck_size < 16U || !wav_raw_read(off + 8U, fmt, 16U)) {
                return BSL_ERR_INVAL;
            }
            af = wav_rd16(&fmt[0]);
            /* 0xFFFE（WAVE_FORMAT_EXTENSIBLE）在参考工程里也按 PCM 播，这里同口径：
             * 真正决定能不能解的是 bits/ch 两个字段，subformat GUID 不再展开。 */
            if (af != 1U && af != 0xFFFEU) {
                return BSL_ERR_NOSUPPORT;
            }
            info->channels = wav_rd16(&fmt[2]);
            info->rate = wav_rd32(&fmt[4]);
            info->bits = wav_rd16(&fmt[14]);
            have_fmt = 1;
        } else if (memcmp(ck, "data", 4) == 0) {
            info->data_off = off + 8U;
            info->data_size = ck_size;
            have_data = 1;
            break;
        }
        off += 8U + ck_size + (ck_size & 1U);
    }
    if (have_fmt == 0 || have_data == 0) {
        return BSL_ERR_INVAL;
    }
    if (info->channels == 0U || info->channels > 2U) {
        return BSL_ERR_NOSUPPORT;
    }
    if (info->bits != 16U && info->bits != 24U && info->bits != 32U) {
        return BSL_ERR_NOSUPPORT;
    }
    if (info->rate < 4000U || info->rate > 96000U) {
        return BSL_ERR_INVAL;
    }
    /* 有的 wav 头把 data_size 写得比文件大（尾巴被截过），按实际剩余字节夹一次。 */
    if (info->data_off >= size) {
        return BSL_ERR_INVAL;
    }
    if (info->data_size > size - info->data_off) {
        info->data_size = size - info->data_off;
    }
    info->channels = (uint16_t)info->channels;
    s_wav.block_align = (uint16_t)(info->channels * (info->bits / 8U));
    if (info->data_size == 0U || s_wav.block_align == 0U) {
        return BSL_ERR_INVAL;
    }
    info->frames = info->data_size / s_wav.block_align;
    if (info->frames == 0U) {
        return BSL_ERR_INVAL;
    }
    info->ms_total = (uint32_t)(((uint64_t)info->frames * 1000U) / info->rate);
    info->from_file = s_wav.from_file;
    return BSL_OK;
}

/* 把解析结果装进播放状态并"预读第一帧"（否则首个输出样本恒是 0）。 */
static bsl_err_t wav_arm(const fml_audio_wav_t *info)
{
    int32_t first = 0;

    s_wav.open = 1;
    s_wav.data_off = info->data_off;
    s_wav.data_size = info->data_size;
    s_wav.file_size = wav_file_size();
    s_wav.channels = info->channels;
    s_wav.bits = info->bits;
    s_wav.rate = info->rate;
    s_wav.src_frames = info->frames;
    s_wav.fs = bsl_sai_sample_rate();
    if (s_wav.fs == 0U) {
        s_wav.open = 0;
        return BSL_ERR_NOINIT;
    }
    s_wav.out_left = (uint32_t)(((uint64_t)info->frames * s_wav.fs) / info->rate);
    s_wav.src_pos = 0U;
    s_wav.buf_len = 0U;
    s_wav.buf_pos = 0U;
    s_wav.buf_off = 0U;
    s_wav.acc = 0U;
    s_wav.last_err = (int)BSL_OK;
    if (wav_read_frame(&first)) {
        s_wav.prev = first;
        s_wav.cur = first;
    } else {
        s_wav.prev = 0;
        s_wav.cur = 0;
    }
    return BSL_OK;
}

bsl_err_t fml_audio_wav_open(const char *path, fml_audio_wav_t *info)
{
    fml_audio_wav_t tmp;
    bsl_err_t rc;

    if (info == NULL) {
        return BSL_ERR_INVAL;
    }
    (void)fml_audio_wav_close();
    memset(&tmp, 0, sizeof(tmp));
    memset(&s_wav, 0, sizeof(s_wav));
    s_wav.from_file = (path != NULL) ? 1 : 0;
    if (path != NULL) {
        rc = fml_fatfs_open(path);
        if (rc != BSL_OK) {
            memset(&s_wav, 0, sizeof(s_wav));
            return rc;
        }
        s_wav.file_size = fml_fatfs_size();
    }
    rc = wav_parse(&tmp);
    if (rc != BSL_OK) {
        (void)fml_audio_wav_close();
        return rc;
    }
    rc = wav_arm(&tmp);
    if (rc != BSL_OK) {
        (void)fml_audio_wav_close();
        return rc;
    }
    *info = tmp;
    return BSL_OK;
}

bsl_err_t fml_audio_wav_rewind(void)
{
    fml_audio_wav_t info;
    int32_t first = 0;

    if (s_wav.open == 0) {
        return BSL_ERR_NOINIT;
    }
    info.rate = s_wav.rate;
    info.channels = s_wav.channels;
    info.bits = s_wav.bits;
    info.data_off = s_wav.data_off;
    info.data_size = s_wav.data_size;
    info.frames = s_wav.src_frames;
    info.ms_total = (uint32_t)(((uint64_t)s_wav.src_frames * 1000U) / s_wav.rate);
    info.from_file = s_wav.from_file;
    /* wav_arm 会从 data 区开头重新拉，所以把游标与缓冲一起清掉。 */
    s_wav.src_pos = 0U;
    s_wav.buf_len = 0U;
    s_wav.buf_pos = 0U;
    s_wav.buf_off = 0U;
    s_wav.acc = 0U;
    s_wav.out_left = (uint32_t)(((uint64_t)info.frames * s_wav.fs) / info.rate);
    if (wav_read_frame(&first)) {
        s_wav.prev = first;
        s_wav.cur = first;
    }
    return BSL_OK;
}

bsl_err_t fml_audio_wav_play(unsigned blocks, uint32_t *played, uint32_t *elapsed_us)
{
    uint32_t p = 0U;
    uint32_t el = 0U;

    if (played != NULL) {
        *played = 0U;
    }
    if (elapsed_us != NULL) {
        *elapsed_us = 0U;
    }
    if (s_wav.open == 0) {
        return BSL_ERR_NOINIT;
    }
    if (!fml_audio_ready()) {
        return BSL_ERR_NOINIT;
    }
    while (blocks > 0U && s_wav.out_left > 0U) {
        uint32_t *buf = bsl_sai_buffer();
        uint32_t us = 0U;
        unsigned i;
        bsl_err_t rc;

        if (buf == NULL) {
            return BSL_ERR_NOINIT;
        }
        for (i = 0U; i < BSL_SAI_BUF_FRAMES; i++) {
            int16_t s = 0;

            if (s_wav.out_left > 0U) {
                s = wav_next_sample();
                s_wav.out_left--;
            }
            /* 一帧 32 位：高 16 左、低 16 右（与 tone 同一个 DR 布局）。 */
            buf[i] = ((uint32_t)(uint16_t)s << 16) | (uint32_t)(uint16_t)s;
        }
        rc = bsl_sai_play_blocking(BSL_SAI_BUF_FRAMES, &us);
        if (rc != BSL_OK) {
            s_wav.last_err = (int)rc;
            return rc;
        }
        p += BSL_SAI_BUF_FRAMES;
        el += us;
        blocks--;
    }
    if (played != NULL) {
        *played = p;
    }
    if (elapsed_us != NULL) {
        *elapsed_us = el;
    }
    return BSL_OK;
}

void fml_audio_wav_close(void)
{
    if (s_wav.open != 0 && s_wav.from_file != 0) {
        fml_fatfs_close();
    }
    memset(&s_wav, 0, sizeof(s_wav));
}
