#include "bsl_uart.h"

#include "board_rt.h"
#include "bsl_device.h"
#include "bsl_device_api.h"
#include "bsl_log.h"
#include "stm32f4xx_hal.h"

#include <stddef.h>

/* USART3 是**芯片事实**：compatible "brd,usart" 在本工程只对应这一个实例
 * （USART2 不做的理由写在对照表 P6e-1 行）。要加第二个实例就把它做成
 * 实例表 + 索引，而不是在这里多一个 if。 */
#define UART_INST USART3
#define UART_IRQ USART3_IRQn

static int s_ready;
static uint32_t s_baud;
static uint32_t s_brr;
static uint32_t s_idle_ms = 10U;

/* 帧缓冲与状态：只留最近一帧。参考工程也是这个口径（RX 未取走时新字节丢弃），
 * 因为上层是"轮询取帧"：留多帧没人取也是丢，不如让 buffer 满时明确丢。 */
static uint8_t s_rx[BSL_UART_RX_MAX];
static unsigned s_rx_len;
static uint32_t s_last_ms;

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

/* 微秒时间戳：HAL_GetTick 只有 1 ms 分辨率，判 86.8 µs 的 +-10% 根本不够用。
 * DWT->CYCCNT 由 osl_boot 起好并在复位后清零，这里只做差。 */
static uint32_t cyc_now(void)
{
    return DWT->CYCCNT;
}

static uint32_t cyc_to_us(uint32_t cycles)
{
    return cycles / (SystemCoreClock / 1000000U);
}

static void frame_push(uint8_t b)
{
    if (s_rx_len < BSL_UART_RX_MAX) {
        s_rx[s_rx_len++] = b;
    }
    /* 满了就丢新字节（与参考工程 `g_usart3_rx_sta` 达上限后的行为一致）：
     * 保留"已经收到的这一帧"，让上层至少能取到它。 */
    s_last_ms = HAL_GetTick();
}

/* 中断里只做"取走 DR + 入缓冲"：不做判帧、不打日志。
 * 打日志会卡在 bsl_log_puts 的独占与 50 ms 超时里，而中断里不能等。 */
void USART3_IRQHandler(void); /* 向量名由启动文件的弱符号提供，不出头文件（外部没人调它） */
void USART3_IRQHandler(void)
{
    uint32_t sr = UART_INST->SR;

    if ((sr & USART_SR_RXNE) != 0U) {
        frame_push((uint8_t)(UART_INST->DR & 0xFFU));
    }
    /* ORE 必须清，否则新字节永远进不来（ORE 只有"读 SR 再读 DR"才清）。 */
    if ((sr & USART_SR_ORE) != 0U) {
        (void)UART_INST->DR;
    }
}

bsl_err_t bsl_uart_init(void)
{
    board_rt_usart_t cfg;
    GPIO_InitTypeDef gpio;
    uint32_t pclk;

    s_ready = 0;
    s_baud = 0U;
    s_brr = 0U;
    s_rx_len = 0U;

    if (board_rt_usart(&cfg) == 0) {
        BSL_LOG_MOD_ERR("usart", "no dtb node");
        return BSL_ERR_NOINIT;
    }
    if (port_clock(cfg.port) == 0) {
        BSL_LOG_MOD_ERR("usart", "bad port");
        return BSL_ERR_NOINIT;
    }
    pclk = HAL_RCC_GetPCLK1Freq();
    s_brr = (pclk + (cfg.baud / 2U)) / cfg.baud;
    if (s_brr == 0U || s_brr > 0xFFFFU) {
        BSL_LOG_MOD_ERR("usart", "bad brr");
        return BSL_ERR_NOSUPPORT;
    }
    s_baud = cfg.baud;
    s_idle_ms = cfg.frame_idle_ms;

    /* TX + RX 都是 AF 推挽：RM0090 要求 RX 也配成复用（配成输入模式时
     * 复用器没接通，帧收不到，参考工程 `usart2.c` 正好踩过这个坑的注释里写着
     * "RX脚 必须设置成输入模式"，实际那版连 RE 都没开）。 */
    gpio.Pin = cfg.mask;
    gpio.Mode = GPIO_MODE_AF_PP;
    gpio.Pull = GPIO_PULLUP;
    gpio.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    gpio.Alternate = cfg.af;
    HAL_GPIO_Init(cfg.port, &gpio);

    __HAL_RCC_USART3_CLK_ENABLE();

    UART_INST->CR1 = 0U;
    UART_INST->BRR = s_brr;
    UART_INST->CR1 = USART_CR1_TE | USART_CR1_RE | USART_CR1_RXNEIE | USART_CR1_UE;

    HAL_NVIC_SetPriority(UART_IRQ, 5U, 0U);
    HAL_NVIC_EnableIRQ(UART_IRQ);

    s_ready = 1;
    BSL_LOG_MOD_INF("usart", "init: baud=");
    bsl_log_put_dec((int)s_baud);
    BSL_LOG_INF(" brr=");
    bsl_log_put_dec((int)s_brr);
    BSL_LOG_INF(" idle=");
    bsl_log_put_dec((int)s_idle_ms);
    BSL_LOG_INF("ms\r\n");
    return BSL_OK;
}

int bsl_uart_ready(void)
{
    return s_ready;
}

uint32_t bsl_uart_baud(void)
{
    return s_baud;
}

uint32_t bsl_uart_brr(void)
{
    return s_brr;
}

bsl_err_t bsl_uart_send_byte(uint8_t b, uint32_t *us)
{
    return bsl_uart_send(&b, 1U, us);
}

/*
 * 连续发 n 个字节，**按 TXE 排队**而不是每字节等 TC。
 *
 * 两种口径的差别在测量上很关键：write DR 之后到起始位真正上线的延迟、以及帧尾到
 * TC 置位的延迟，各是 0..1 bit 的量化（内部波特率时钟对齐），合起来单字节看起来
 * 会多出将近 1 bit（10 bit 的帧量出 ~10.9 bit）。发 n 个字节时这段量化只发生一次，
 * 摊到 16 帧上就只剩 6%，于是"实测总时长 vs n*10*bit_time"才能证明分频是对的。
 * 业务侧同样需要这个：一条 AT 指令就是一次多字节连发，逐字节等 TC 只是白等。
 */
bsl_err_t bsl_uart_send(const uint8_t *buf, unsigned n, uint32_t *us)
{
    uint32_t c0;
    uint32_t guard = 0U;
    unsigned i;

    if (s_ready == 0) {
        return BSL_ERR_NOINIT;
    }
    if (buf == NULL || n == 0U) {
        return BSL_ERR_INVAL;
    }
    c0 = cyc_now();
    /* 先等 TC：确保总线空闲，否则这次连发的第一个字节会插进上一帧。 */
    while ((UART_INST->SR & USART_SR_TC) == 0U) {
        if (++guard > 100000U) {
            return BSL_ERR_TIMEOUT;
        }
    }
    guard = 0U;
    for (i = 0U; i < n; i++) {
        while ((UART_INST->SR & USART_SR_TXE) == 0U) {
            if (++guard > 100000U) {
                return BSL_ERR_TIMEOUT;
            }
        }
        UART_INST->DR = (uint32_t)buf[i];
    }
    guard = 0U;
    while ((UART_INST->SR & USART_SR_TC) == 0U) {
        if (++guard > 100000U) {
            return BSL_ERR_TIMEOUT;
        }
    }
    if (us != NULL) {
        *us = cyc_to_us(cyc_now() - c0);
    }
    return BSL_OK;
}

int bsl_uart_recv_frame(uint8_t *buf, unsigned max, unsigned *len)
{
    unsigned n;
    unsigned i;
    uint32_t now;

    if (s_ready == 0 || s_rx_len == 0U) {
        return 0;
    }
    now = HAL_GetTick();
    /* 无符号减法天然跨 1 ms tick 回绕（49.7 天），不会出现"刚收到却判未空闲"。 */
    if ((uint32_t)(now - s_last_ms) < s_idle_ms) {
        return 0;
    }
    n = s_rx_len;
    if (n > max) {
        n = max;
    }
    for (i = 0U; i < n; i++) {
        buf[i] = s_rx[i];
    }
    if (len != NULL) {
        *len = n;
    }
    s_rx_len = 0U;
    return 1;
}

void bsl_uart_inject(uint8_t b)
{
    frame_push(b);
}

/* ---- 设备注册（registry 的 bsl_driver_brd_usart / bsl_api_brd_usart） ---- */

const bsl_uart_api_t bsl_api_brd_usart = {
    .ready = bsl_uart_ready,
    .baud = bsl_uart_baud,
    .brr = bsl_uart_brr,
    .send_byte = bsl_uart_send_byte,
    .send = bsl_uart_send,
    .recv_frame = bsl_uart_recv_frame,
    .inject = bsl_uart_inject,
};

const struct bsl_device_driver bsl_driver_brd_usart = {
    .init = bsl_uart_init,
};
