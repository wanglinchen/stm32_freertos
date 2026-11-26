/*
 * WM8978 音频 codec（只写 I2C）。
 *
 * 为什么文件名是 board_wm8978 而不是表里原先写的 board_es8388：
 * P6c-0（2026-11-25）在真板子上量过 —— 主 I2C（PH4/PH5）应答清单是
 * 1a/1e/20/50/68，**没有 0x10**；而 0x1A 地址 ACK、读方向 NACK，正是 WM8978
 * "2 线控制接口只写"的指纹。pinmap（手册 V1.1 表 1.5.1）把 codec 写成 ES8388
 * 与实测冲突，按 §0.1 第 3 项先改表再改代码，不把型号钉在两个地方。
 *
 * 只写器件 ⇒ 驱动无法靠"读回来对不对"自证，只能靠写事务的 ACK 数：
 *   POST 的 codec 项判据 = 已 ACK 的写事务数 == 期望写事务数（14/14）。
 * 这条判据是这场"没有回读通道"的调试里唯一的硬证据。
 */
#ifndef BOARD_WM8978_H
#define BOARD_WM8978_H

#include "bsl_err.h"
#include <stdint.h>

/* 初始化写的事务数（R0 软复位 + 电源 + 输出路径 + 接口 + 时钟 + 音量共 14 条）。
 * 与 .c 里的 k_init[] 长度由 _Static_assert 对齐 —— 改表不改数会让判据变成空话。 */
#define BOARD_WM8978_REG_COUNT 14U

/* 初始化：R0 软复位 → 停 1 ms → 其余 13 条。返回 BSL_OK 仅当 14 条全部 ACK。
 * 地址来自 DTB（sai 节点的 codec 引用 → wm8978 节点的 reg），不写死 0x1A。 */
bsl_err_t board_wm8978_init(void);

int board_wm8978_ready(void);

/* 已 ACK 的写事务数 / 期望事务数（POST 与 shell 都读它）。 */
unsigned board_wm8978_ack_count(void);
unsigned board_wm8978_reg_count(void);

/* 音量 0..63（LOUT1/ROUT1 的 6 位字段，0 = 最小）。写 R52/R53。
 * 越界返回 BSL_ERR_INVAL；这两个寄存器不进 POST 计数（初始化后单独可调）。 */
bsl_err_t board_wm8978_set_volume(uint8_t vol);

/*
 * ---- 录音侧（P9e-3 勘探）----
 *
 * P6c 的播放序列里 ADCENL/R（R2 bit0/1）与 BOOSTENL/R（bit4/5）**本来就是开的**
 * （R2=0x1B3），所以录音只差三件事：输入 PGA（R2 bit2/3）、输入路由（R44）、
 * 与 PGA/boost 的增益（R45/R46/R47/R48）。
 *
 * 口径来源：[参考] 野火《WM8978 录音》页的位说明 + 正点原子论坛的
 * `WM8978_Input_Cfg` 值（R2 |= 3<<2 开 INPPGA、R44 |= 3<<4|3<<0 走 LIN/LIP/
 * RIN/RIP）。本地无 WM8978 手册，所以这些值标 [参考] 而不是 [数据手册]。
 *
 * 写序列共 7 条（R1 的 MICBEN + R2/R44/R45/R46/R47/R48），逐条算 ACK；
 * `..._adc_ack()` 给 POST 打出来。`..._adc_disable()` 把 R1/R2/R44 还原成播放口径
 * —— 不为录音把咪头偏置留在喇叭通路上。
 */
bsl_err_t board_wm8978_adc_enable(void);
bsl_err_t board_wm8978_adc_disable(void);
unsigned board_wm8978_adc_ack(void);

#endif /* BOARD_WM8978_H */
