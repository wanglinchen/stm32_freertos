#include "bsl_dma.h"

#include "board_rt.h"

#include "stm32f4xx_hal.h"

#include <stddef.h>

/* DMA（P9c-1）：DMA2 上的内存→内存搬运，寄存器直配（不借 HAL handle）。
 *
 * 为什么寄存器直配：本批唯一的判据是"搬了 N 个字、内容逐字相同、TC 标志置位"，
 * 用 HAL 的 init/msp 两层包住反而会把"到底哪一位没配上"藏起来。SAI 的音频通道
 * 已经走 HAL，两条路径互不干扰。
 *
 * 关键位（RM0090 §10.3.5）：
 *   SxCR.DIR   = 10 表示内存→内存（只有 DMA2 支持）
 *   SxCR.MSIZE/PSIZE = 10（32 位），MINC/PINC = 1
 *   SxCR.PL = 2（高优先级，纯搬运没有竞争）
 *   SxCR.TCIE = 1，完成靠 LIFCR 的 TCIF 轮询
 * M2M 模式下 CHSEL 无意义（硬件忽略），因此不填。 */
#define BSL_DMA2_BASE 0x40026400UL
#define DMA_LISR (BSL_DMA2_BASE + 0x00UL)
#define DMA_LIFCR (BSL_DMA2_BASE + 0x08UL)

static volatile uint32_t *const s_sxcr = (volatile uint32_t *)(BSL_DMA2_BASE + 0x10UL);
static volatile uint32_t *const s_sxndtr = (volatile uint32_t *)(BSL_DMA2_BASE + 0x14UL);
static volatile uint32_t *const s_sxpar = (volatile uint32_t *)(BSL_DMA2_BASE + 0x18UL);
static volatile uint32_t *const s_sxm0ar = (volatile uint32_t *)(BSL_DMA2_BASE + 0x1CUL);
static volatile uint32_t *const s_sxfcr = (volatile uint32_t *)(BSL_DMA2_BASE + 0x24UL);

#define DMA_TCIF0 (1UL << 5)
#define DMA_ALL_FLAGS0 0x3DUL /* FEIF|DMEIF|TEIF|HTIF|TCIF of stream 0 */

/* 256 个字 ≈ 1 µs 级；给 100000 次轮询（168 MHz 下约数毫秒），超时返回 TIMEOUT
 * 而不是死等 —— 板上如果 DMA 没开时钟，POST 得能收尾。 */
#define DMA_POLL_LIMIT 100000UL

static int s_ready;

bsl_err_t bsl_dma_init(void)
{
    board_rt_dma_t cfg;

    if (board_rt_dma(&cfg) == 0) {
        return BSL_ERR_INVAL;
    }
    if (cfg.instance != 2U || cfg.stream != 0U) {
        /* 本批只实现 DMA2 Stream0：换成别的实例要另算标志位偏移，
         * 那种"配了但判不出"的路径没有价值，直接拒绝并落 FAILED。 */
        return BSL_ERR_NOSUPPORT;
    }
    __HAL_RCC_DMA2_CLK_ENABLE();
    /* 上电清一次全部标志：复位后 LISR 可能残留，先清才能让后面的 TCIF 有意义。 */
    *(volatile uint32_t *)DMA_LIFCR = DMA_ALL_FLAGS0;
    s_ready = 1;
    return BSL_OK;
}

static int dma_api_ready(void)
{
    return s_ready;
}

/* 内存→内存搬运：words 个 32 位字。成功返回 OK 且 *done_words = 实际搬运数
 * （NDTR 归零即全部搬完）。 */
static bsl_err_t dma_memcopy(const void *src, void *dst, unsigned words,
                             unsigned *done_words)
{
    if (s_ready == 0) {
        return BSL_ERR_NOINIT;
    }
    if (src == NULL || dst == NULL || words == 0U) {
        return BSL_ERR_INVAL;
    }

    *s_sxcr = 0U; /* 先关流再改寄存器 */
    *(volatile uint32_t *)DMA_LIFCR = DMA_ALL_FLAGS0;
    *s_sxpar = (uint32_t)(uintptr_t)src;
    *s_sxm0ar = (uint32_t)(uintptr_t)dst;
    *s_sxndtr = (uint32_t)words;
    *s_sxfcr = 0U; /* 直接模式，FIFO 不参与 */
    *s_sxcr = (2UL << 16)   /* PL = 高 */
              | (2UL << 13) /* MSIZE = 32 位 */
              | (2UL << 11) /* PSIZE = 32 位 */
              | (1UL << 10) /* MINC */
              | (1UL << 9)  /* PINC */
              | (2UL << 6)  /* DIR = 内存→内存 */
              | (1UL << 4)  /* TCIE */
              | 1UL;        /* EN */

    uint32_t n = 0U;
    while (n < DMA_POLL_LIMIT) {
        uint32_t isr = *(volatile uint32_t *)DMA_LISR;
        if ((isr & DMA_TCIF0) != 0U) {
            break;
        }
        if ((isr & (1UL << 1)) != 0U || (isr & (1UL << 3)) != 0U) {
            /* FIFO error / transfer error：关流并如实报错。 */
            *s_sxcr &= ~1UL;
            *(volatile uint32_t *)DMA_LIFCR = DMA_ALL_FLAGS0;
            return BSL_ERR_IO;
        }
        n++;
    }
    if (n >= DMA_POLL_LIMIT) {
        *s_sxcr &= ~1UL;
        return BSL_ERR_TIMEOUT;
    }
    *s_sxcr &= ~1UL;
    *(volatile uint32_t *)DMA_LIFCR = DMA_ALL_FLAGS0;
    if (done_words != NULL) {
        *done_words = words - (unsigned)*s_sxndtr;
    }
    return BSL_OK;
}

const bsl_dma_api_t bsl_api_brd_dma = {
    .ready = dma_api_ready,
    .memcopy = dma_memcopy,
};

static bsl_err_t dma_driver_init(void)
{
    return bsl_dma_init();
}

const bsl_device_driver_t bsl_driver_brd_dma = {
    .init = dma_driver_init,
};
