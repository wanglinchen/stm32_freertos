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

#endif /* FML_AUDIO_H */
