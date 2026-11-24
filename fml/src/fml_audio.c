#include "fml_audio.h"

#include "board_device_registry.h"
#include "bsl_device.h"
#include "bsl_sai.h"
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
