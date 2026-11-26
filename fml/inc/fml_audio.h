/*
 * 音频中间件：把"响一声"这件事从寄存器细节里抽出来。
 *
 * 分工：bsl_sai 管位时钟与 DMA、board_wm8978 管寄存器；这里只管**波形**与
 * "响多久"。缓冲区不在这里申请 —— 用 bsl_sai 的那一份，因为音频在本工程是
 * 独占资源（一个 codec、一条 SAI、一个 8 KB 缓冲）。
 *
 * 为什么蜂鸣器禁用了这里还能响：用户的要求是"蜂鸣器（PCF8574 P0 那个压电片）
 * 以后都不要打开"，依据是 `board_beep.c` 的 `BEEP_ALLOW_ON=0`。这里的出声路径是
 * WM8978 + SAI + 喇叭/耳机，与蜂鸣器不是同一个执行器，也不经过 `board_beep`。
 * 两者都"响"，但一个是被策略断路的告警器，一个是音频演示 —— 别混为一谈。
 */
#ifndef FML_AUDIO_H
#define FML_AUDIO_H

#include <stdint.h>

#include "bsl_err.h"

/* 播放正弦：hz 20..20000，ms 1..5000。阻塞（调用者等它响完）。
 * 里面是"填缓冲 → 循环 DMA → 等 ms → 停"，所以期间不要指望任务调度有多准。 */
bsl_err_t fml_audio_tone(unsigned hz, unsigned ms);

/* 停止（幂等）。 */
bsl_err_t fml_audio_stop(void);

/* 音量 0..63（WM8978 的 LOUT1/ROUT1 字段）。 */
bsl_err_t fml_audio_set_volume(unsigned vol);

/* 底层是否就绪（SAI + codec 都 READY）。shell 与后续音乐播放器都先问这一句。 */
int fml_audio_ready(void);

/* 输出帧率（= SAI 的固定 fs，P6c 实测 47973）。apl 不许直接 include bsl_sai.h，
 * 所以经这一层转出去 —— 重采样与"总输出帧数"的计算都要用它。 */
uint32_t fml_audio_sample_rate(void);

/* 一个播放块的帧数（= bsl_sai 的缓冲长度 2048）。fml_audio_wav_play 返回的
 * played 比它小，就说明这一块已经是素材尾（不足一块）。 */
#define FML_AUDIO_BLOCK_FRAMES 2048U

/*
 * ---- wav 流播放（P9e-2）----
 *
 * 采样率的硬边界：bsl_sai 的帧率由 PLLI2S → MCLK/256 定死 47973 Hz（P6c 实测），
 * F4 的 SAI 没有分数分频，44.1k 在 49.125 MHz 下除不尽。所以这里**不跟着文件改
 * 硬件**：任何采样率的素材都先做**线性插值重采样到 bsl_sai_sample_rate()**。
 * 音调与时长是对的，高采样率素材有插值失真 —— 这是本工程的边界，不假装
 * "支持 44.1 kHz 硬件播放"。
 *
 * 支持的素材：RIFF/WAVE、AudioFormat = 1（PCM，或 0xFFFE 的 extensible 按 PCM 读）、
 * 单/双声道、16/24/32 位。8 位、ADPCM、mp3/flac/ape 一律拒绝（后者是另外的解码器）。
 */
typedef struct {
    uint32_t rate;          /* 源采样率 Hz */
    uint16_t channels;      /* 1 / 2 */
    uint16_t bits;          /* 16 / 24 / 32 */
    uint32_t data_off;      /* data 块载荷在文件内的偏移 */
    uint32_t data_size;     /* data 块字节数 */
    uint32_t frames;        /* 源帧数 = data_size / block_align */
    uint32_t ms_total;      /* 源时长（按源采样率算的毫秒） */
    int from_file;          /* 1 = 卷内文件，0 = 构建期内置样本 */
} fml_audio_wav_t;

/* 打开：path != NULL 走卷内文件（需 fml_fatfs_mount 已挂、大小写按短名判 .WAV），
 * path == NULL 走内置样本。只解析头，不读音频数据；同一时刻只有一个句柄。 */
bsl_err_t fml_audio_wav_open(const char *path, fml_audio_wav_t *info);

/* 从头重播（不清 info，也不重新解析）。 */
bsl_err_t fml_audio_wav_rewind(void);

/* 顺序播 blocks 个 SAI 块（每块 BSL_SAI_BUF_FRAMES 帧 ≈ 42.7 ms）。
 * played 回填已写出的输出帧数、elapsed_us 回填 SAI 侧累计数据流时长（不含块间启动缝）。
 * 到素材尾就提前结束，played 可能不足整块。块与块之间 SAI 会停一次再起，
 * 所以听感上每 42.7 ms 有一个微小接缝 —— 不装双缓冲 DMA 的代价，写在这里。 */
bsl_err_t fml_audio_wav_play(unsigned blocks, uint32_t *played, uint32_t *elapsed_us);

void fml_audio_wav_close(void);

/* 构建期内置 wav 样本（build/gen_wav_sample.py 生成 fml/src/fml_audio_sample.c）。 */
extern const unsigned char fml_audio_sample_wav[];
extern const unsigned int fml_audio_sample_wav_len;

#endif /* FML_AUDIO_H */
