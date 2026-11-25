#include "bsl_can.h"

#include "board_rt.h"
#include "bsl_device.h"
#include "bsl_device_api.h"
#include "bsl_log.h"
#include "stm32f4xx_hal.h"

#include <stddef.h>

/* 同步段固定 1 tq（SJW-1 = 0）：CAN 的 SJW 只影响重同步能吸收多少相位误差，
 * 本板没有对端、回环又是核内直连，SJW 取最小即可，不引入一个没人调的参数。 */
#define CAN_SJW 1U

static int s_ready;
static uint32_t s_bit_rate;
static uint32_t s_brp;
static uint32_t s_tseg;
static uint32_t s_ts1 = 1U;
static uint32_t s_ts2 = 1U;

/* 位率上限 1 Mbit/s、下限 10 kbit/s 由 board_rt 校验；这里只信 DTB 给的值。
 * tq 总数 = PCLK1 / bitrate，再找 tseg（1+TS1+TS2）能整除它的一组。
 * tseg 从大到小试：tq 越多采样点越细，但 TS1 上限 16（RM0090）。
 * 本板 PCLK1 = 42 MHz、500 kbit/s ⇒ 84 tq ⇒ tseg=14（1 同步 + 12 TS1 + 1 TS2，
 * 采样点在 13/14 = 92.8%），整除关系精确，bit_rate 折算回来正好 500000。 */
static int btr_choose(uint32_t pclk, uint32_t bitrate, uint32_t *brp, uint32_t *tseg)
{
    uint32_t q;
    uint32_t t;
    uint32_t ts2;
    uint32_t ts1;

    if (pclk == 0U || bitrate == 0U) {
        return 0;
    }
    q = pclk / bitrate;
    if (q < 8U) {
        return 0;
    }
    for (t = 20U; t >= 8U; t--) {
        if ((q % t) != 0U) {
            continue;
        }
        *brp = q / t;
        if (*brp < 1U || *brp > 1024U) {
            continue;
        }
        /* 采样点取 7/8 处：ts2 = tseg/8（至少 1），ts1 = tseg-1-ts2。
         * ts1 超 16 就不是合法的 BTR 值，换下一个 tseg。
         * 注意 BTR 三个字段存的都是「值 - 1」：TS1[3:0]=ts1-1、TS2[2:0]=ts2-1、
         * BRP[9:0]=brp-1；直接写原值会把位率变成 375 kbit/s，而回环自发自收
         * 照样 PASS（核内直连不看外部参考），是典型的"看起来对、其实错"。 */
        ts2 = t / 8U;
        if (ts2 < 1U) {
            ts2 = 1U;
        }
        ts1 = t - 1U - ts2;
        if (ts1 < 1U || ts1 > 16U) {
            continue;
        }
        *tseg = t;
        s_ts1 = ts1;
        s_ts2 = ts2;
        return 1;
    }
    return 0;
}

/* 端口时钟：端口由 DTB 给，所以只能按指针分支。
 * 漏一个分支 → 引脚配置写进一个时钟没开的端口，寄存器静默丢弃，
 * 现场表现成"CAN 发不出、也收不到"，没有任何读数指向这里。 */
static int port_clock(GPIO_TypeDef *port)
{
    switch ((uintptr_t)port) {
    case GPIOA_BASE: __HAL_RCC_GPIOA_CLK_ENABLE(); return 1;
    case GPIOB_BASE: __HAL_RCC_GPIOB_CLK_ENABLE(); return 1;
    case GPIOC_BASE: __HAL_RCC_GPIOC_CLK_ENABLE(); return 1;
    case GPIOD_BASE: __HAL_RCC_GPIOD_CLK_ENABLE(); return 1;
    case GPIOE_BASE: __HAL_RCC_GPIOE_CLK_ENABLE(); return 1;
    case GPIOH_BASE: __HAL_RCC_GPIOH_CLK_ENABLE(); return 1;
    default: return 0;
    }
}

bsl_err_t bsl_can_init(void)
{
    board_rt_can_t cfg;
    GPIO_InitTypeDef gpio;
    uint32_t pclk;
    uint32_t guard;

    s_ready = 0;
    s_bit_rate = 0U;
    s_brp = 0U;
    s_tseg = 0U;

    if (board_rt_can(&cfg) == 0) {
        BSL_LOG_MOD_ERR("can", "no dtb node");
        return BSL_ERR_NOINIT;
    }
    if (port_clock(cfg.port) == 0) {
        BSL_LOG_MOD_ERR("can", "bad port");
        return BSL_ERR_NOINIT;
    }
    pclk = HAL_RCC_GetPCLK1Freq();
    if (btr_choose(pclk, cfg.bitrate, &s_brp, &s_tseg) == 0) {
        BSL_LOG_MOD_ERR("can", "no timing for bitrate");
        return BSL_ERR_NOSUPPORT;
    }
    s_bit_rate = pclk / (s_brp * s_tseg);

    /* PA11/PA12 是 CAN 的 RX/TX（AF9）。上拉 + 高速：CAN 是差分总线，
     * 单端这两根脚在没有收发器时是悬空的，上拉避免"浮空脚上的噪声
     * 被当成帧起始"；有收发器时上拉也无害。 */
    gpio.Pin = cfg.mask;
    gpio.Mode = GPIO_MODE_AF_PP;
    gpio.Pull = GPIO_PULLUP;
    gpio.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    gpio.Alternate = cfg.af;
    HAL_GPIO_Init(cfg.port, &gpio);

    __HAL_RCC_CAN1_CLK_ENABLE();

    /* 进初始化模式：RM0090 要求 BTR/过滤器只能在 INRQ=1 时改。 */
    CAN1->MCR = 0U;
    CAN1->MCR |= CAN_MCR_INRQ;
    guard = 0U;
    while ((CAN1->MSR & CAN_MSR_INAK) == 0U) {
        if (++guard > 200U) {
            BSL_LOG_MOD_ERR("can", "inak timeout");
            return BSL_ERR_TIMEOUT;
        }
    }
    /* TTCM/AWUM = 0；ABOM = 0（与参考工程同口径：出错不自动离线，便于观察）；
     * NART = 1（只试一次，失败即置错误位，不做无限重传）。 */
    CAN1->MCR |= CAN_MCR_NART;
    CAN1->MCR &= ~(CAN_MCR_TTCM | CAN_MCR_ABOM | CAN_MCR_AWUM | CAN_MCR_TXFP | CAN_MCR_RFLM);

    CAN1->BTR = 0U;
    if (cfg.loopback != 0) {
        CAN1->BTR |= CAN_BTR_LBKM;
    }
    CAN1->BTR |= (uint32_t)((CAN_SJW - 1U) << 24);
    CAN1->BTR |= (uint32_t)((s_ts2 - 1U) << 20);
    CAN1->BTR |= (uint32_t)((s_ts1 - 1U) << 16);
    CAN1->BTR |= (s_brp - 1U);

    CAN1->MCR &= ~CAN_MCR_INRQ;
    guard = 0U;
    while ((CAN1->MSR & CAN_MSR_INAK) != 0U) {
        if (++guard > 200U) {
            BSL_LOG_MOD_ERR("can", "exit init timeout");
            return BSL_ERR_TIMEOUT;
        }
    }

    /* 过滤器 0：32 位、屏蔽位模式、全部 don't-care、挂 FIFO0（收所有帧）。
     * 回环自证必须收得到自己发的帧，所以这里不能滤任何 ID。 */
    CAN1->FMR |= CAN_FMR_FINIT;
    CAN1->FA1R &= ~CAN_FA1R_FACT0;
    CAN1->FS1R |= CAN_FS1R_FSC0;
    CAN1->FM1R &= ~CAN_FM1R_FBM0;
    CAN1->FFA1R &= ~CAN_FFA1R_FFA0;
    CAN1->sFilterRegister[0].FR1 = 0U;
    CAN1->sFilterRegister[0].FR2 = 0U;
    CAN1->FA1R |= CAN_FA1R_FACT0;
    /* 离开过滤器初始化态：**参考工程这里写的是 `FMR &= 0 << 0`（空操作）**，
     * 过滤器组会一直停在初始化态、帧全被丢弃；本工程必须显式清 FINIT。 */
    CAN1->FMR &= ~CAN_FMR_FINIT;

    s_ready = 1;
    BSL_LOG_MOD_INF("can", "init: bitrate=");
    bsl_log_put_dec((int)s_bit_rate);
    BSL_LOG_INF(" brp=");
    bsl_log_put_dec((int)s_brp);
    BSL_LOG_INF(" tseg=");
    bsl_log_put_dec((int)s_tseg);
    BSL_LOG_INF(" loop=");
    bsl_log_put_dec((int)cfg.loopback);
    BSL_LOG_INF("\r\n");
    return BSL_OK;
}

int bsl_can_ready(void)
{
    return s_ready;
}

uint32_t bsl_can_bit_rate(void)
{
    return s_bit_rate;
}

void bsl_can_timing(uint32_t *brp, uint32_t *tseg)
{
    if (brp != NULL) {
        *brp = s_brp;
    }
    if (tseg != NULL) {
        *tseg = s_tseg;
    }
}

bsl_err_t bsl_can_send(uint32_t id, const uint8_t *data, uint8_t len)
{
    uint8_t mbox;
    uint32_t tir;

    if (s_ready == 0) {
        return BSL_ERR_NOINIT;
    }
    /* 标准帧 ID 只有 11 位（TIR 的 STID[10:0]）。越界的 ID 写进去会被硬件
     * 静默截断（实测发 0x800 收到的 ID 变成 0x000），所以在这里挡掉，
     * 不留给"发出去看着成功、对方收到另一个 ID"这种局面。 */
    if (id > 0x7FFU) {
        return BSL_ERR_INVAL;
    }
    if (data == NULL || len > 8U) {
        return BSL_ERR_INVAL;
    }
    if ((CAN1->TSR & CAN_TSR_TME0) != 0U) {
        mbox = 0U;
    } else if ((CAN1->TSR & CAN_TSR_TME1) != 0U) {
        mbox = 1U;
    } else if ((CAN1->TSR & CAN_TSR_TME2) != 0U) {
        mbox = 2U;
    } else {
        /* 三个邮箱都满：这是"暂时发不出去"，不是参数错，按 TIMEOUT 报。 */
        return BSL_ERR_TIMEOUT;
    }

    tir = (id & 0x7FFU) << 21; /* 标准帧：STID[10:0] 在 31:21，IDE/RTR = 0 */
    CAN1->sTxMailBox[mbox].TDTR = (uint32_t)len;
    CAN1->sTxMailBox[mbox].TDLR = ((uint32_t)data[3] << 24) | ((uint32_t)data[2] << 16)
                                | ((uint32_t)data[1] << 8) | (uint32_t)data[0];
    CAN1->sTxMailBox[mbox].TDHR = ((uint32_t)data[7] << 24) | ((uint32_t)data[6] << 16)
                                | ((uint32_t)data[5] << 8) | (uint32_t)data[4];
    /* TIR 最后写：TXRQ 一置位硬件立刻取走邮箱内容，先写数据再置位，
     * 顺序反了会发出上一帧的残留数据。 */
    tir |= CAN_TI0R_TXRQ;
    CAN1->sTxMailBox[mbox].TIR = tir;
    return BSL_OK;
}

int bsl_can_recv(uint32_t *id, uint8_t *data, uint8_t *len)
{
    uint32_t rir;
    uint32_t rdtr;
    uint32_t lo;
    uint32_t hi;
    unsigned n;
    unsigned i;

    if (s_ready == 0) {
        return 0;
    }
    if ((CAN1->RF0R & CAN_RF0R_FMP0) == 0U) {
        return 0;
    }
    rir = CAN1->sFIFOMailBox[0].RIR;
    rdtr = CAN1->sFIFOMailBox[0].RDTR;
    lo = CAN1->sFIFOMailBox[0].RDLR;
    hi = CAN1->sFIFOMailBox[0].RDHR;
    n = rdtr & 0x0FU;
    if (n > 8U) {
        n = 8U;
    }
    if (data != NULL) {
        for (i = 0U; i < n; i++) {
            data[i] = (uint8_t)(((i < 4U) ? (lo >> (8U * i)) : (hi >> (8U * (i - 4U)))) & 0xFFU);
        }
    }
    if (id != NULL) {
        *id = (rir >> 21) & 0x7FFU; /* 只认标准帧：IDE 位为 1 的扩展帧按 11 位截断上报 */
    }
    if (len != NULL) {
        *len = (uint8_t)n;
    }
    /* 释放 FIFO 输出邮箱：不写 RFOM 的话 FMP 永远不清，下一次 recv 还读同一帧。 */
    CAN1->RF0R |= CAN_RF0R_RFOM0;
    return 1;
}

/* ---- 设备注册（registry 的 bsl_driver_brd_can / bsl_api_brd_can） ---- */

const bsl_can_api_t bsl_api_brd_can = {
    .ready = bsl_can_ready,
    .bit_rate = bsl_can_bit_rate,
    .timing = bsl_can_timing,
    .send = bsl_can_send,
    .recv = bsl_can_recv,
};

const struct bsl_device_driver bsl_driver_brd_can = {
    .init = bsl_can_init,
};
