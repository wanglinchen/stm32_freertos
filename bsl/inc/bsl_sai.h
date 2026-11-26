/*
 * SAI1 block A（音频播放）的寄存器级驱动。
 *
 * 分工口径与 board_lcd.c 一致：**芯片事实**（block A、DMA2 Stream 1 Ch0、
 * 寄存器位域）写在这里；**板级事实**（引脚、采样率、MCLK 分频、PLLI2S 三个数、
 * codec 地址）全部来自 DTB（board_rt_sai）。
 *
 * 时钟链（[代码推算]，DTB 里只有输入参数，输出在这里算）：
 *   PLL 输入 = HSE / PLLM（从 RCC->PLLCFGR 现读，不假设 1 MHz）
 *   VCO      = PLL 输入 × PLLI2SN
 *   SAI_CK   = VCO / PLLI2SQ / (PLLI2SDIVQ + 1)
 *   MCLK     = SAI_CK / (MCKDIV × 2)          ← HAL 的口径，与 RM 的 Fig. 一致
 *   FS       = MCLK / 128                     ← **实测**，不是 HAL 注释里的 256
 * 本板：SAI_CK = 49.125 MHz、MCKDIV=4 → MCLK=6.140625 MHz → FS=47973 Hz。
 *
 * 关于 128：HAL 的 SAI_InitI2S 注释写 "MCLK = 256 * FS"，按那个算 MCKDIV=2 就
 * 该是 47973 Hz；而 2026-11-25 在板上用 FIFO 消耗速率探针实测（MCKDIV=2、
 * 2048 帧用 21328 µs → 95.9 kHz）得到的是 2 倍，帧周期实际是 128 个 MCLK。
 * 串口证据在 docs/design/串口自检_P6c_15项_2026-11-25.txt。所以 DTB 给
 * MCKDIV=4，bsl_sai.c 里也按 128 折算、并与 DTB 声明值做 ±5% 校验。
 *
 * 为什么不用 PLLSAI：它已被 LTDC 占用（VCO 396 MHz、R=3、DIVR=/4 → 33 MHz 像素时钟）。
 * 改它的 Q 分频要先把 PLLSAI 关掉，那等于停掉屏的像素时钟 —— 为了响一声把屏搞花，
 * 不值得。PLLI2S 全仓无人使用，专门给音频。
 *
 * 播放链路：内存 → DMA2 Stream 1 Ch 0（32 位）→ SAI_A->DR → PE6(SD_A) → WM8978。
 * 每帧一个 32 位字：高 16 位左声道、低 16 位右声道。
 */
#ifndef BSL_SAI_H
#define BSL_SAI_H

#include "bsl_err.h"

#include <stdint.h>

/* 单缓冲区。音频在本工程是**独占资源**（一个 codec、一个 SAI），
 * 让驱动持有缓冲区而不是每个消费者各建一份：8 KB 只花一次。
 * 2048 帧 @47973 Hz ≈ 42.7 ms。 */
#define BSL_SAI_BUF_FRAMES 2048U

bsl_err_t bsl_sai_init(void);
int bsl_sai_ready(void);

/* 实算出来的 FS（Hz）。POST 用它判"时长对不对"，不拿 DTS 的声明值当真值。 */
uint32_t bsl_sai_sample_rate(void);

/* 帧缓冲（uint32_t × BSL_SAI_BUF_FRAMES）：消费方填样本，不自己申请内存。 */
uint32_t *bsl_sai_buffer(void);

/* 循环播放（CIRC=1），一直响到 stop。给"响 N 毫秒"这类请求：起 → 延时 → 停。 */
bsl_err_t bsl_sai_start(uint32_t n_frames);

/* 单次播放（普通模式），等 DMA 传完再返回；*elapsed_us 给出实测时长（µs）。
 * 这是 sai POST 项的判据来源：样本数已知、时长实测，两者对得上才说明位时钟在跑。 */
bsl_err_t bsl_sai_play_blocking(uint32_t n_frames, uint32_t *elapsed_us);

bsl_err_t bsl_sai_stop(void);

/* 上一次播放的实测时长（µs）与 SAI 状态寄存器里留下的错误位（OVRUDR/WCKCFG/...）。
 * 单独给 getter 而不是塞进返回值：`play` 成功但"有 underrun"也是坏消息。 */
uint32_t bsl_sai_last_us(void);
uint32_t bsl_sai_last_error(void);

/*
 * ---- 录音侧：Block B 从接收（P9e-3 勘探）----
 *
 * Block B 用 internal sync 跟 Block A（主机）同步：SCK/FS 由 A 产生，B 只把
 * SD_B(PE3) 上的数据收回来。所以**录之前必须让 A 先跑**（`bsl_sai_rx_capture`
 * 自己会把 A 起成循环静音，不用调用方操心）。
 *
 * 收到的帧和播放帧同一布局：32 位字 = 高 16 左 + 低 16 右。样本率就是 A 的 FS
 * （本板 47973 Hz），所以录下来的数据可以原样交给 `bsl_sai_buffer` 回放，
 * 不需要重采样。
 */
#define BSL_SAI_RX_BUF_FRAMES BSL_SAI_BUF_FRAMES

typedef struct {
    uint32_t frames;    /* 实际收到的 32 位字数（正常应等于请求的 n_frames） */
    /* 每个 32 位字里只有低 16 位是当前槽的样本（DS=16、SLOTSZ=16 时数据右对齐，
     * 高 16 位恒 0 —— 上板首测把高半当左声道，读出来全是 0，就是这个原因）。
     * 槽在 L/R 之间交替：偶数下标 = 左槽、奇数下标 = 右槽。 */
    uint32_t nz;        /* 非零样本数（两槽合计）—— 0 就说明 ADCDAT 一直是 0 */
    int32_t lmin;       /* 左槽 最小/最大/均值 */
    int32_t lmax;
    int32_t lmean;
    int32_t rmin;       /* 右槽 最小/最大/均值 */
    int32_t rmax;
    int32_t rmean;
    uint32_t pp;        /* 峰峰值 max(all) - min(all)：静音时只有直流，pp≈0 */
    uint32_t err;       /* Block B 的 SR 错误位（OVRUDR/WCKCFG/AFSDET/LFSDET） */
} bsl_sai_rx_stat_t;

/* 配置 Block B（可重复调用；采样率/帧形态取自 Block A，不另算）。 */
bsl_err_t bsl_sai_rx_init(void);

/* 采集选项：两个沿各收一趟就能分辨"数据对不上"是边沿选错还是器件没在驱动；
 * 一边放音一边收，则能回答"咪头到底收不收得到声音"（声耦合）。
 *   ALT_EDGE：CKSTR=0（默认用 =1，与 Block A 同沿）
 *   TONE    ：Block A 播 440 Hz 量级的方波（802 Hz，48 帧一个周期）而不是静音
 * 录一趟：Block A 起循环 → Block B + DMA2 Stream5 收 n_frames → 停两侧 → 统计。
 * 返回 BSL_OK 仅当 DMA 传输完成标志真的置起来（帧数不够返回 TIMEOUT）。 */
#define BSL_SAI_RX_ALT_EDGE 0x1U
#define BSL_SAI_RX_TONE 0x2U

bsl_err_t bsl_sai_rx_capture(uint32_t n_frames, unsigned flags,
                             bsl_sai_rx_stat_t *stat, uint32_t *elapsed_us);

#endif /* BSL_SAI_H */
