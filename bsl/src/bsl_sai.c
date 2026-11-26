#include "bsl_sai.h"

#include <stddef.h>

#include "board_rt.h"
#include "bsl_device.h"
#include "bsl_device_api.h"
#include "bsl_gpio.h"
#include "bsl_log.h"
#include "stm32f4xx_hal.h"

#define SAI_BLK SAI1_Block_A
#define SAI_STREAM DMA2_Stream1

/* stream 1 在 LISR 里占 bit6..bit11：FEIF/DMEIF/TEIF/HTIF/TCIF。
 * 清标志一律写整段：只清自己关心的那一位，会让上一次失败的残留位一直亮着，
 * 下一次失败现场就分不清"这次出的"还是"上次留的"。 */
#define SAI_DMA_FLAGS (DMA_LISR_FEIF1 | DMA_LISR_DMEIF1 | DMA_LISR_TEIF1 \
                       | DMA_LISR_HTIF1 | DMA_LISR_TCIF1)

#define SAI_DMA_CLR (DMA_LIFCR_CFEIF1 | DMA_LIFCR_CDMEIF1 | DMA_LIFCR_CTEIF1 \
                     | DMA_LIFCR_CHTIF1 | DMA_LIFCR_CTCIF1)

/* SAI 状态里会让"声音变坏"的位：OVRUDR（欠载/过载）、WCKCFG（时钟配置非法）、
 * AFSDET/LFSDET（帧同步异常）。主模式的 TX 只有前两位可能出，后两位一起报，
 * 免得将来加录音时又换一套判据。 */
#define SAI_ERR_MASK (SAI_xSR_OVRUDR | SAI_xSR_WCKCFG | SAI_xSR_AFSDET | SAI_xSR_LFSDET)

/* PLLI2S 锁定与 FIFO 就绪的等待上限。PLL 锁定时长按手册是 µs 级，给 2 ms 富余；
 * FIFO 那一步只在 DMA 已经把第一个字搬进去之后才会成立，50 ms 足够。 */
#define SAI_PLL_TIMEOUT_MS 2U
#define SAI_FIFO_TIMEOUT_MS 50U

static uint32_t s_buf[BSL_SAI_BUF_FRAMES];
static uint32_t s_fs;
static uint32_t s_last_us;
static uint32_t s_last_err;
static int s_ready;

uint32_t *bsl_sai_buffer(void)
{
    return s_buf;
}

uint32_t bsl_sai_sample_rate(void)
{
    return s_fs;
}

uint32_t bsl_sai_last_us(void)
{
    return s_last_us;
}

uint32_t bsl_sai_last_error(void)
{
    return s_last_err;
}

int bsl_sai_ready(void)
{
    return s_ready;
}

static int wait_clear(volatile uint32_t *reg, uint32_t mask, uint32_t ms)
{
    uint32_t t0 = HAL_GetTick();

    while ((*reg & mask) != 0U) {
        if ((HAL_GetTick() - t0) > ms) {
            return 0;
        }
    }
    return 1;
}

static int wait_set(volatile uint32_t *reg, uint32_t mask, uint32_t ms)
{
    uint32_t t0 = HAL_GetTick();

    while ((*reg & mask) == 0U) {
        if ((HAL_GetTick() - t0) > ms) {
            return 0;
        }
    }
    return 1;
}

/* 把 PLLI2S 配成音频时钟源。**必须先关再写**：RM0090 明确 PLL 配置寄存器只能在
 * PLL 关闭时写，否则写进去的值被忽略而 RDY 照样置起 —— 现场就是"时钟不对、
 * 但一切看起来已就绪"。 */
static bsl_err_t sai_clock_setup(const board_rt_sai_t *cfg)
{
    uint32_t pllm;
    uint32_t pllin;
    uint32_t vco;
    uint32_t saick;
    uint32_t fs;
    uint32_t cfgr;

    RCC->CR &= ~RCC_CR_PLLI2SON;
    if (wait_clear(&RCC->CR, RCC_CR_PLLI2SRDY, SAI_PLL_TIMEOUT_MS) == 0) {
        return BSL_ERR_TIMEOUT;
    }

    /* 保留 R 分频（本工程不用 PLLI2SR，但改写整个寄存器会顺手把它清成 0，
     * 而 0 是非法分频值）。 */
    cfgr = RCC->PLLI2SCFGR & RCC_PLLI2SCFGR_PLLI2SR;
    cfgr |= ((cfg->pll2_n & 0x1FFU) << RCC_PLLI2SCFGR_PLLI2SN_Pos);
    cfgr |= ((cfg->pll2_q & 0x0FU) << RCC_PLLI2SCFGR_PLLI2SQ_Pos);
    RCC->PLLI2SCFGR = cfgr;

    RCC->CR |= RCC_CR_PLLI2SON;
    if (wait_set(&RCC->CR, RCC_CR_PLLI2SRDY, SAI_PLL_TIMEOUT_MS) == 0) {
        return BSL_ERR_TIMEOUT;
    }

    /* SAI1 时钟源 = PLLI2SQ（SAI1ASRC = 01），再过 PLLI2SDIVQ。
     * 注意 CMSIS 里这两个字段叫 SAI1ASRC/SAI1BSRC（不是 HAL 注释里的 SAI1SRC）。 */
    MODIFY_REG(RCC->DCKCFGR, RCC_DCKCFGR_PLLI2SDIVQ | RCC_DCKCFGR_SAI1ASRC,
               (cfg->pll2_divq & 0x1FU) | RCC_DCKCFGR_SAI1ASRC_0);

    /* 现算 FS，而不是引用 DTS 的声明值：声明值只是"想要的"，算出来才是"配成的"。
     * 两者对不上说明 DTB 的 pll2-* 与 mck-div 自相矛盾，init 直接失败。 */
    pllm = RCC->PLLCFGR & RCC_PLLCFGR_PLLM;
    if (pllm == 0U) {
        return BSL_ERR_INVAL;
    }
    pllin = board_rt_hse_hz() / pllm;
    if (pllin == 0U) {
        return BSL_ERR_INVAL;
    }
    vco = pllin * cfg->pll2_n;
    /* F429 的 PLLI2S VCO 必须落在 100..432 MHz，越界时锁不起来或输出不可用 */
    if (vco < 100000000U || vco > 432000000U) {
        return BSL_ERR_INVAL;
    }
    saick = vco / cfg->pll2_q;
    saick /= (cfg->pll2_divq + 1U);
    /* [实测 2026-11-25] 帧率是 SAI_CK/(256·MCKDIV)，不是 HAL 注释里写的
     * SAI_CK/(512·MCKDIV)。证据是 FIFO 消耗速率探针：MCKDIV=2 时 2048 帧走完用了
     * 25741 µs（去掉启动段 4413 µs 后 2047 帧 / 21328 µs），折算 95.9 kHz —— 正好是
     * HAL 公式 47.97 kHz 的 2 倍。也就是说实际帧周期是 128 个 MCLK（HAL 注释写 256）。
     * 按实测值算，本板 MCKDIV=4 → 47973 Hz，MCLK=6.14 MHz ≈ 128·FS，SCK≈1.53 MHz=32·FS
     * （16 bit × 2 槽，与 FRL=31 自洽）。 */
    fs = saick / (cfg->mck_div * 256U);
    if (fs == 0U) {
        return BSL_ERR_INVAL;
    }
    /* 5% 是"配出来的 FS 与 DTB 声明值"的容差。0.056% 的本板偏差远在门内；
     * 真出错时（比如 Q 写成 4）偏差是 100%，一眼就出来。 */
    if (fs + (cfg->sample_rate / 20U) < cfg->sample_rate
        || fs > cfg->sample_rate + (cfg->sample_rate / 20U)) {
        return BSL_ERR_INVAL;
    }
    s_fs = fs;
    return BSL_OK;
}

bsl_err_t bsl_sai_init(void)
{
    board_rt_sai_t cfg;
    unsigned groups;
    unsigned i;

    s_ready = 0;
    s_last_us = 0U;
    s_last_err = 0U;
    s_fs = 0U;

    if (board_rt_sai(&cfg) == 0) {
        BSL_LOG_MOD_ERR("sai", "init: sai node missing in dtb\r\n");
        return BSL_ERR_INVAL;
    }
    groups = board_rt_sai_pin_groups();
    if (groups == 0U) {
        return BSL_ERR_INVAL;
    }
    __HAL_RCC_SAI1_CLK_ENABLE();
    /* SAI1_A 的数据通路走 DMA2 Stream 1 Channel 0（ST AN4031 表 2），所以 DMA2
     * 的时钟也必须开。漏了它不会报错：DMA 寄存器写入被静默丢弃（读回全 0），
     * 现场表现是"NDTR=0、传输瞬间完成、FIFO 始终空"—— 像是时序问题，其实是没上电。 */
    __HAL_RCC_DMA2_CLK_ENABLE();
    for (i = 0U; i < groups; i++) {
        /* 复用输出、无上下拉、最高速：MCLK 12.28 MHz / SCK 1.53 MHz 的边沿，
         * 低速档会把方角磨圆；上下拉在推挽输出上没有意义。 */
        if (bsl_gpio_init_af(board_rt_sai_pin_port(i), board_rt_sai_pin_mask(i),
                             board_rt_sai_pin_af(i), GPIO_NOPULL,
                             GPIO_SPEED_FREQ_VERY_HIGH) == 0) {
            return BSL_ERR_INVAL;
        }
    }

    if (sai_clock_setup(&cfg) != BSL_OK) {
        BSL_LOG_MOD_ERR("sai", "init: clock setup failed\r\n");
        return BSL_ERR_INVAL;
    }

    (void)bsl_sai_stop();

    /* CR1：主机 TX（MODE=00）、自由协议（PRTCFG=00，I2S 的时序由 FRCR 拼出来）、
     * 16 位数据（DS=010）、MSB 先出、TX 用下降沿选通（CKSTR=1，与 HAL 的
     * SAI_InitI2S 完全同口径：I2S 数据在下降沿变化、上升沿采样）。
     * MCKDIV 来自 DTB：MCLK = SAI_CK/(MCKDIV*2)。SAIEN/DMAEN 留到播放时再开。 */
    SAI_BLK->CR1 = SAI_xCR1_DS_1
                 | SAI_xCR1_CKSTR
                 | ((cfg.mck_div & 0x0FU) << SAI_xCR1_MCKDIV_Pos);
    /* FTH=000：TX 的 FIFO 阈值取"空"，与 HAL 的 I2S 口径一致 —— 阈值抬高会让
     * 第一个 FS 到来时 FIFO 里还没数据，白送一次欠载。 */
    SAI_BLK->CR2 = 0U;
    /* FRCR：帧长 32 bit（16 bit × 2 槽）→ FRL=31；FS 有效长度 16 bit → FSALL=15；
     * FSDEF=1（帧同步靠通道识别）、FSPOL=0（低有效）、FSOFF=1（FS 比第一位早一拍）
     * —— 这三项正是 I2S 飞利浦标准的形态，与 HAL 的 SAI_InitI2S 逐项相同。 */
    SAI_BLK->FRCR = (31U & SAI_xFRCR_FRL_Msk)
                  | ((15U << SAI_xFRCR_FSALL_Pos) & SAI_xFRCR_FSALL_Msk)
                  | SAI_xFRCR_FSDEF
                  | SAI_xFRCR_FSOFF;
    /* SLOTR：槽大小 = 数据大小（00）、2 个槽（NBSLOT=1）、两槽都激活。 */
    SAI_BLK->SLOTR = ((1U << SAI_xSLOTR_NBSLOT_Pos) & SAI_xSLOTR_NBSLOT_Msk)
                   | (0x0003U << SAI_xSLOTR_SLOTEN_Pos);

    s_ready = 1;
    BSL_LOG_MOD_INF("sai", "init: fs=");
    bsl_log_put_dec((int)s_fs);
    BSL_LOG_INF(" hz mck-div=");
    bsl_log_put_dec((int)cfg.mck_div);
    BSL_LOG_INF(" codec=");
    bsl_log_put_hex(cfg.codec_addr7, 2U);
    /* 回读三个形态寄存器：写进去的值被硬件"悄悄纠正"过（例如 FRL 越界被截）时，
     * 后面算出来的帧率会不对，而这里不打印就只能从声音/时长反推。 */
    BSL_LOG_INF(" cr1=");
    bsl_log_put_hex(SAI_BLK->CR1, 8U);
    BSL_LOG_INF(" frcr=");
    bsl_log_put_hex(SAI_BLK->FRCR, 8U);
    BSL_LOG_INF(" slotr=");
    bsl_log_put_hex(SAI_BLK->SLOTR, 8U);
    BSL_LOG_INF("\r\n");
    return BSL_OK;
}

/* DMA 与 SAI 的使能顺序照抄 HAL_SAI_Transmit_DMA（并在注释里写明为什么）：
 *   1) 起 DMA stream（数据先进 FIFO）
 *   2) CR1.DMAEN
 *   3) 等 FIFO 非空（FLVL != EMPTY）
 *   4) 最后开 SAIEN
 * 反过来（先开 SAIEN）在主机模式下会立刻开始出位时钟，而 FIFO 还是空的 ——
 * 上电第一次播放必然先欠载一次，OVRUDR 亮起，sai 项的判据直接变红。 */
static bsl_err_t sai_play_start(uint32_t n_frames, int circular)
{
    uint32_t t0;

    if (!s_ready || n_frames == 0U || n_frames > BSL_SAI_BUF_FRAMES) {
        return BSL_ERR_INVAL;
    }
    (void)bsl_sai_stop();

    SAI_STREAM->PAR = (uint32_t)&SAI_BLK->DR;
    SAI_STREAM->M0AR = (uint32_t)&s_buf[0];
    SAI_STREAM->NDTR = n_frames;
    /* 直接模式（DMDIS=1）：SAI 每次要一个 32 位字，用 FIFO 打包只会让首字延迟；
     * 32 位宽两端一致（MSIZE=PSIZE=10），内存递增、外设固定。 */
    SAI_STREAM->FCR = DMA_SxFCR_DMDIS;
    SAI_STREAM->CR = DMA_SxCR_MSIZE_1 | DMA_SxCR_PSIZE_1 | DMA_SxCR_MINC
                   | DMA_SxCR_DIR_0 | DMA_SxCR_PL_1
                   | (circular ? DMA_SxCR_CIRC : 0U);
    DMA2->LIFCR = SAI_DMA_CLR;
    /* 清 SAI 的残留错误位：上一次 stop 时留下的 OVRUDR 会让这一次的判据永远红 */
    SAI_BLK->CLRFR = SAI_ERR_MASK;

    SAI_STREAM->CR |= DMA_SxCR_EN;
    SAI_BLK->CR1 |= SAI_xCR1_DMAEN;

    /* 只在"写进去的值没站住"时报：NDTR 不是刚写的 n_frames、PAR 不是 DR 的地址，
     * 说明这几次写被硬件忽略了（最典型的原因是 DMA 时钟没开 —— 那时读回全 0）。
     * 正常路径不打印：`tone` 每敲一次都刷两行会把串口日志淹掉。 */
    if ((SAI_STREAM->NDTR != n_frames) || (SAI_STREAM->PAR != (uint32_t)&SAI_BLK->DR)) {
        BSL_LOG_MOD_ERR("sai", "start reg ignored: ndtr=");
        bsl_log_put_dec((int)SAI_STREAM->NDTR);
        BSL_LOG_INF(" cr=");
        bsl_log_put_hex(SAI_STREAM->CR, 8U);
        BSL_LOG_INF(" par=");
        bsl_log_put_hex(SAI_STREAM->PAR, 8U);
        BSL_LOG_INF("\r\n");
    }

    t0 = HAL_GetTick();
    while ((SAI_BLK->SR & SAI_xSR_FLVL_Msk) == 0U) {
        if ((HAL_GetTick() - t0) > SAI_FIFO_TIMEOUT_MS) {
            (void)bsl_sai_stop();
            return BSL_ERR_TIMEOUT;
        }
    }
    SAI_BLK->CR1 |= SAI_xCR1_SAIEN;
    return BSL_OK;
}

bsl_err_t bsl_sai_stop(void)
{
    SAI_BLK->CR1 &= ~(SAI_xCR1_SAIEN | SAI_xCR1_DMAEN);
    /* 关 stream 前先清 EN，再等它真的落下：RM0090 要求 EN 清零后硬件可能要
     * 几个周期才完成，期间写 NDTR/PAR 会被忽略。 */
    SAI_STREAM->CR &= ~DMA_SxCR_EN;
    (void)wait_clear(&SAI_STREAM->CR, DMA_SxCR_EN, 2U);
    DMA2->LIFCR = SAI_DMA_CLR;
    return BSL_OK;
}

bsl_err_t bsl_sai_start(uint32_t n_frames)
{
    return sai_play_start(n_frames, 1);
}

/* 微秒级的时间戳：HAL_GetTick 只有 1 ms 分辨率，判 ±5% 的时长对不上（42 ms 的
 * 5% 才 2 ms，量化误差就吃掉一半）。DWT->CYCCNT 由内核自由计数，osl_boot 已经把它
 * 起来并在复位后清零，这里只做差 —— 差值按无符号减法天然跨越一次 32 位回绕（25.6 s），
 * 而一次播放最多 5 s，够用。 */
static uint32_t cyc_u32(void)
{
    return DWT->CYCCNT;
}

static uint32_t cyc_to_us(uint32_t cycles)
{
    return cycles / (SystemCoreClock / 1000000U);
}

bsl_err_t bsl_sai_play_blocking(uint32_t n_frames, uint32_t *elapsed_us)
{
    bsl_err_t rc;
    uint32_t t0;
    uint32_t timeout;
    uint32_t expect_ms;
    uint32_t expect_us;
    uint32_t c0;
    uint32_t c_all;
    uint32_t ndtr0;
    uint32_t us_start;

    s_last_us = 0U;
    s_last_err = 0U;
    c_all = cyc_u32();
    rc = sai_play_start(n_frames, 0);
    if (rc != BSL_OK) {
        BSL_LOG_MOD_ERR("sai", "play rc=");
        bsl_log_put_dec((int)rc);
        BSL_LOG_INF(" ndtr=");
        bsl_log_put_dec((int)SAI_STREAM->NDTR);
        BSL_LOG_INF(" cr=");
        bsl_log_put_hex(SAI_STREAM->CR, 8U);
        BSL_LOG_INF(" sr=");
        bsl_log_put_hex(SAI_BLK->SR, 8U);
        BSL_LOG_INF("\r\n");
        return rc;
    }
    /* us_start = 启动段（配参 + 起 DMA + 等 FIFO 非空）。这一段**不是常数**：
     * 实测在 2 µs ~ 4.4 ms 之间跳（取决于 stop 时 EN 是否已经落下、FIFO 首字到得多快），
     * 而整段播放的 5% 只有 2 ms —— 所以"帧率对不对"只量数据流这一段，启动段只作为
     * 诊断量打印，不参与判据。 */
    us_start = cyc_to_us(cyc_u32() - c_all);
    ndtr0 = SAI_STREAM->NDTR;
    c0 = cyc_u32();
    /* 数据流窗口里还剩 ndtr0 帧没发完（读完 NDTR 就直接开计），所以期望时长按
     * ndtr0 算。若 NDTR 已经是 0（启动段里全都发完了），期望仍按 n_frames 算 ——
     * 那样实测必然≈0，判据自然失败，这正是要报出来的现场。 */
    expect_us = ((ndtr0 != 0U ? ndtr0 : n_frames) * 1000000U) / s_fs;
    expect_ms = expect_us / 1000U;
    /* 超时取期望时长的 3 倍 + 200 ms：位时钟没跑（DMA 一点不动）时不必干等到天荒，
     * 而正常运行有充足余量。 */
    timeout = (expect_ms * 3U) + 200U;
    t0 = HAL_GetTick();
    while ((DMA2->LISR & DMA_LISR_TCIF1) == 0U) {
        if ((HAL_GetTick() - t0) > timeout) {
            s_last_us = cyc_to_us(cyc_u32() - c0);
            s_last_err = SAI_BLK->SR & SAI_ERR_MASK;
            (void)bsl_sai_stop();
            return BSL_ERR_TIMEOUT;
        }
    }
    s_last_us = cyc_to_us(cyc_u32() - c0);
    /* 错误位要在 stop 之前采样：stop 会关掉 SAIEN，之后读到的 SR 已经不代表
     * "播放期间的"状态。 */
    s_last_err = SAI_BLK->SR & SAI_ERR_MASK;
    (void)bsl_sai_stop();
    BSL_LOG_MOD_INF("sai", "play frames=");
    bsl_log_put_dec((int)n_frames);
    BSL_LOG_INF(" us=");
    bsl_log_put_dec((int)s_last_us);
    BSL_LOG_INF(" us_start=");
    bsl_log_put_dec((int)us_start);
    BSL_LOG_INF(" ndtr0=");
    bsl_log_put_dec((int)ndtr0);
    BSL_LOG_INF(" err=");
    bsl_log_put_dec((int)s_last_err);
    BSL_LOG_INF("\r\n");
    if (elapsed_us != NULL) {
        *elapsed_us = s_last_us;
    }
    return BSL_OK;
}

/* ======================================================================
 * 录音侧：Block B 从接收（P9e-3 勘探）
 *
 * 为什么必须"Block A 先响、Block B 再收"：SAI 的 SCK/FS/MCLK 只有一个来源。
 * Block A 配成主机（P6c 的播放口径），Block B 用 SYNCEN=internal 跟它同步 ——
 * 这样录和放共用同一条位时钟与帧时钟，采到的样本率天然就是那个 47973 Hz，
 * 不需要第二套时钟，也不需要重采样就能直接回放。
 *
 * 数据布局与发送完全对称：每帧一个 32 位字，高 16 位是左声道、低 16 位是右。
 * DMA 走 DMA2 Stream 5 Channel 0（参考工程 sai.h 的 SAI1_RX_DMASx 同一映射，
 * 那块板子用的也是这颗芯片，映射不是我推的）。
 * ==================================================================== */

#define SAI_RX_BLK SAI1_Block_B
#define SAI_RX_STREAM DMA2_Stream5

#define SAI_RX_DMA_CLR (DMA_HIFCR_CFEIF5 | DMA_HIFCR_CDMEIF5 | DMA_HIFCR_CTEIF5 \
                        | DMA_HIFCR_CTCIF5)

static uint32_t s_rx_buf[BSL_SAI_RX_BUF_FRAMES];

bsl_err_t bsl_sai_rx_init(void)
{
    if (s_ready == 0) {
        return BSL_ERR_NOINIT;
    }
    __HAL_RCC_DMA2_CLK_ENABLE();
    /* CR1：从接收（MODE=11）+ **内部同步（SYNCEN=01）** + 16 位数据（DS=010）+ CKSTR=1。
     *
     * SYNCEN 的编码按 hal/periph/inc/stm32f4xx_hal_sai.h 的枚举：
     *   0=异步、1=与同一 SAI 的另一个 block 同步（内部）、2=与 SAI1 外部同步、
     *   3=与 SAI2 外部同步。所以"内部同步"是 0b01 = SYNCEN_0。
     * 首版按寄存器位序的直觉写成 0b10（= 外部 SAI1 同步），Block B 去等一个不存在的
     * 外部触发，SCK/FS 一个都没来 —— 上板现场：`rx frames=0 tc=0 err=0`，NDTR 一动不动。
     * CKSTR=1 与 Block A 一致：接收侧在 SCK 上升沿采样，发送侧在下降沿换数据。
     * CR2 的 FSL=01（1/4 FIFO，即两个 16 位槽）与参考工程同一口径。
     * 位而不是 HAL 的 SAI_MODESLAVE_RX：那个宏在 hal_sai 模块头里，本驱动只用寄存器口径。 */
    SAI_RX_BLK->CR1 = SAI_xCR1_DS_1
                    | SAI_xCR1_MODE_1 | SAI_xCR1_MODE_0
                    | SAI_xCR1_CKSTR | SAI_xCR1_SYNCEN_0;
    SAI_RX_BLK->CR2 = 1U;
    /* 帧形态逐位照抄 Block A（同一个 SCK/FS 里的两个 16 位槽）；不自己再算一遍，
     * 两处各算一次就有"改一处忘另一处"的空间。 */
    SAI_RX_BLK->FRCR = SAI_BLK->FRCR;
    SAI_RX_BLK->SLOTR = SAI_BLK->SLOTR;
    SAI_RX_BLK->CLRFR = SAI_ERR_MASK;
    return BSL_OK;
}

bsl_err_t bsl_sai_rx_capture(uint32_t n_frames, unsigned flags,
                             bsl_sai_rx_stat_t *stat, uint32_t *elapsed_us)
{
    bsl_err_t rc;
    uint32_t i;
    uint32_t t0;
    uint32_t c0;
    uint32_t timeout;
    uint32_t expect_ms;
    uint32_t got;
    uint32_t bcr1;
    uint32_t bsr;
    uint32_t w[8];
    uint32_t scr;
    uint32_t hisr;
    uint32_t ndtr0;
    int64_t sum_l = 0;
    int64_t sum_r = 0;
    int32_t lmin = 0;
    int32_t lmax = 0;
    int32_t rmin = 0;
    int32_t rmax = 0;
    int32_t gmin = 0;
    int32_t gmax = 0;
    uint32_t nl = 0U;
    uint32_t nr = 0U;
    uint32_t nz = 0U;
    int tc;

    if ((stat == NULL) || (n_frames == 0U) || (n_frames > BSL_SAI_RX_BUF_FRAMES)) {
        return BSL_ERR_INVAL;
    }
    if (s_ready == 0) {
        return BSL_ERR_NOINIT;
    }
    if (bsl_sai_rx_init() != BSL_OK) {
        return BSL_ERR_NOINIT;
    }
    if ((flags & BSL_SAI_RX_ALT_EDGE) != 0U) {
        /* 只在"还没使能"的时候改采样沿：SAIEN=1 后 CR1 的 CKSTR 是写保护的。 */
        SAI_RX_BLK->CR1 &= ~SAI_xCR1_CKSTR;
    }
    if (elapsed_us != NULL) {
        *elapsed_us = 0U;
    }

    /* 1) Block A 先播静音（循环）。它是主机：停着的时候 Block B 收不到任何位时钟，
     *    NDTR 一动不动，现场看起来像"RX DMA 坏了"，其实是"没人发时钟"。 */
    /* 默认静音；TONE 时填方波，让喇叭真的出声 —— 此时收上来的样本里若出现
     * 大幅起伏，就是"咪头 → PGA → ADC → SD_B"整条路在动，而不是只有直流。 */
    for (i = 0U; i < BSL_SAI_BUF_FRAMES; i++) {
        if ((flags & BSL_SAI_RX_TONE) != 0U) {
            int16_t v = (((i % 48U) < 24U) ? 0x1800 : -0x1800);
            s_buf[i] = ((uint32_t)(uint16_t)v << 16) | (uint32_t)(uint16_t)v;
        } else {
            s_buf[i] = 0U;
        }
    }
    rc = sai_play_start(BSL_SAI_BUF_FRAMES, 1);
    if (rc != BSL_OK) {
        return rc;
    }
    /* A 刚起的那一个帧周期里 FS 的相位还没稳定，此时开 B 会被判成 WCKCFG（错时钟
     * 配置）并就此停收 —— 上一轮第二趟 `rx frames=0` + `bsr=0x10020`（WCKCFG 且
     * FIFO 停在 4 字）就是这个现场。等 1 ms（≈48 帧）再清掉错误位、开 B。 */
    HAL_Delay(1U);
    SAI_RX_BLK->CLRFR = SAI_ERR_MASK;

    /* 2) 先把上一趟留下的状态彻底停干净：SAIEN/DMAEN 先关，等 stream 的 EN 真的
     *    落下（只等两个循环不够 —— 第三次连采时 EN 还在，后面写 PAR/NDTR 会被忽略，
     *    现场是 `rx frames=2` 之后 FIFO 停在 4 个字不动），再刷掉 RX FIFO 的残留字。 */
    SAI_RX_BLK->CR1 &= ~(SAI_xCR1_SAIEN | SAI_xCR1_DMAEN);
    SAI_RX_STREAM->CR &= ~DMA_SxCR_EN;
    {
        uint32_t t_end = HAL_GetTick() + 5U;

        while (((SAI_RX_STREAM->CR & DMA_SxCR_EN) != 0U)
               && (HAL_GetTick() < t_end)) {
            /* 等 EN 自清 */
        }
    }
    SAI_RX_BLK->CR2 |= SAI_xCR2_FFLUSH;
    DMA2->HIFCR = SAI_RX_DMA_CLR;
    SAI_RX_STREAM->PAR = (uint32_t)&SAI_RX_BLK->DR;
    SAI_RX_STREAM->M0AR = (uint32_t)&s_rx_buf[0];
    SAI_RX_STREAM->NDTR = n_frames;
    SAI_RX_STREAM->FCR = DMA_SxFCR_DMDIS;
    SAI_RX_STREAM->CR = DMA_SxCR_MSIZE_1 | DMA_SxCR_PSIZE_1 | DMA_SxCR_MINC
                      | DMA_SxCR_PL_1;
    SAI_RX_STREAM->CR |= DMA_SxCR_EN;
    SAI_RX_BLK->CR1 |= SAI_xCR1_DMAEN;
    SAI_RX_BLK->CR1 |= SAI_xCR1_SAIEN;

    expect_ms = ((n_frames * 1000U) / s_fs) + 1U;
    timeout = (expect_ms * 3U) + 200U;
    c0 = cyc_u32();
    t0 = HAL_GetTick();
    while ((DMA2->HISR & DMA_HISR_TCIF5) == 0U) {
        if ((HAL_GetTick() - t0) > timeout) {
            break;
        }
    }
    tc = ((DMA2->HISR & DMA_HISR_TCIF5) != 0U) ? 1 : 0;
    got = n_frames - SAI_RX_STREAM->NDTR;
    /* 在关 SAIEN 之前抓 Block B 的 CR1/SR：失败时"没使能 / 时钟没来 / 帧格式对不上"
     * 全靠这两个数分辨。CR1 里 SAIEN 在位而 SR 干净、NDTR 不动 ⇒ 时钟根本没到。 */
    bcr1 = SAI_RX_BLK->CR1;
    bsr = SAI_RX_BLK->SR;
    scr = SAI_RX_STREAM->CR;
    hisr = DMA2->HISR;
    ndtr0 = SAI_RX_STREAM->NDTR;
    /* 前 8 个原始字：位序/对齐/边沿这类问题看十六进制最快（首版就是靠它看出
     * 数据全在低半字）。 */
    for (i = 0U; (i < 8U) && (i < got); i++) {
        w[i] = s_rx_buf[i];
    }

    /* 3) 收完先关接收侧，再停 Block A（反过来的话最后几个字会跟着时钟一起没了）。 */
    SAI_RX_BLK->CR1 &= ~(SAI_xCR1_SAIEN | SAI_xCR1_DMAEN);
    SAI_RX_STREAM->CR &= ~DMA_SxCR_EN;
    (void)wait_clear(&SAI_RX_STREAM->CR, DMA_SxCR_EN, 2U);
    (void)bsl_sai_stop();

    /* 逐字取低 16 位，按下标奇偶分左右槽；同时记全局峰峰值（分不出"有信号"和
     * "只有直流"的时候，pp 是最快的判据）。 */
    for (i = 0U; i < got; i++) {
        int32_t v = (int32_t)(int16_t)(s_rx_buf[i] & 0xFFFFU);
        int left = ((i & 1U) == 0U) ? 1 : 0;

        if (i == 0U) {
            gmin = v;
            gmax = v;
        } else {
            if (v < gmin) {
                gmin = v;
            }
            if (v > gmax) {
                gmax = v;
            }
        }
        if (left) {
            if (nl == 0U || v < lmin) {
                lmin = v;
            }
            if (nl == 0U || v > lmax) {
                lmax = v;
            }
            if (v != 0) {
                nz++;
            }
            nl++;
            sum_l += v;
        } else {
            if (nr == 0U || v < rmin) {
                rmin = v;
            }
            if (nr == 0U || v > rmax) {
                rmax = v;
            }
            if (v != 0) {
                nz++;
            }
            nr++;
            sum_r += v;
        }
    }
    stat->frames = got;
    stat->nz = nz;
    stat->lmin = lmin;
    stat->lmax = lmax;
    stat->rmin = rmin;
    stat->rmax = rmax;
    stat->pp = (uint32_t)(gmax - gmin);
    stat->lmean = (nl != 0U) ? (int32_t)(sum_l / (int64_t)nl) : 0;
    stat->rmean = (nr != 0U) ? (int32_t)(sum_r / (int64_t)nr) : 0;
    stat->err = SAI_RX_BLK->SR & SAI_ERR_MASK;

    if (elapsed_us != NULL) {
        /* 只作诊断（含启动段），不参与判据：判据是 TC 与帧数。 */
        *elapsed_us = cyc_to_us(cyc_u32() - c0);
    }
    BSL_LOG_MOD_INF("sai", "rx frames=");
    bsl_log_put_dec((int)stat->frames);
    BSL_LOG_INF(" tc=");
    bsl_log_put_dec(tc);
    BSL_LOG_INF(" l=");
    bsl_log_put_dec((int)stat->lmin);
    BSL_LOG_INF("..");
    bsl_log_put_dec((int)stat->lmax);
    BSL_LOG_INF("@");
    bsl_log_put_dec((int)stat->lmean);
    BSL_LOG_INF(" r=");
    bsl_log_put_dec((int)stat->rmin);
    BSL_LOG_INF("..");
    bsl_log_put_dec((int)stat->rmax);
    BSL_LOG_INF("@");
    bsl_log_put_dec((int)stat->rmean);
    BSL_LOG_INF(" pp=");
    bsl_log_put_dec((int)stat->pp);
    BSL_LOG_INF(" nz=");
    bsl_log_put_dec((int)stat->nz);
    BSL_LOG_INF(" err=");
    bsl_log_put_dec((int)stat->err);
    BSL_LOG_INF(" bcr1=");
    bsl_log_put_hex(bcr1, 8U);
    BSL_LOG_INF(" bsr=");
    bsl_log_put_hex(bsr, 8U);
    BSL_LOG_INF(" scr=");
    bsl_log_put_hex(scr, 8U);
    BSL_LOG_INF(" hisr=");
    bsl_log_put_hex(hisr, 8U);
    BSL_LOG_INF(" ndtr=");
    bsl_log_put_dec((int)ndtr0);
    BSL_LOG_INF(" w=");
    for (i = 0U; i < 8U; i++) {
        if (i < got) {
            bsl_log_put_hex(w[i], 4U);
        } else {
            BSL_LOG_INF("----");
        }
        if (i != 7U) {
            BSL_LOG_INF(",");
        }
    }
    BSL_LOG_INF("\r\n");
    return (tc != 0) ? BSL_OK : BSL_ERR_TIMEOUT;
}

/* ---- 设备注册（registry 的 bsl_driver_brd_sai / bsl_api_brd_sai） ---- */

const bsl_sai_api_t bsl_api_brd_sai = {
    .sample_rate = bsl_sai_sample_rate,
    .buffer = bsl_sai_buffer,
    .start = bsl_sai_start,
    .play_blocking = bsl_sai_play_blocking,
    .stop = bsl_sai_stop,
    .last_us = bsl_sai_last_us,
    .last_error = bsl_sai_last_error,
};

const struct bsl_device_driver bsl_driver_brd_sai = {
    .init = bsl_sai_init,
};
