#include "board_debug_uart.h"

#include "board_rt.h"
#include "bsl_device.h"
#include "bsl_device_api.h"
#include "stm32f4xx_hal.h"

static UART_HandleTypeDef s_huart_debug;
static USART_TypeDef *s_instance;
static char s_tx_port_letter;
static char s_rx_port_letter;
static unsigned s_tx_pin_no;
static unsigned s_rx_pin_no;
/* 引脚复用号认不出（实例不在 USART1..6 / UART4..6 表里）时置位。
 * MspInit 是 void 回调，没法直接把失败交回 HAL_UART_Init，所以落在这个标志里，
 * 由 board_debug_uart_init() 读 —— 否则 gpio.Alternate 会是 0（AF0 = SYS），
 * 引脚被配成一个与 USART 无关的功能，而 HAL_UART_Init 仍然返回 HAL_OK：
 * 串口"初始化成功"但一个字都发不出去。 */
static uint8_t s_pinmux_failed;

/* 上板诊断（2026-11-22）：banner 首段 18 字节只有 11 字节到主机，换接收栈
 * （.NET SerialPort / pyserial）丢的字节位置完全一致，说明不是宿主侧的读取问题。
 * 这里把 HAL_UART_Transmit 的返回码与剩余计数留下来，用来区分两种成因：
 *   失败一次且 remain>0  -> HAL 自己提前超时返回，字节根本没写进 DR；
 *   失败计数为 0         -> 18 字节都写进 DR 了，丢在链路/CH340/宿主驱动。 */
static volatile uint32_t s_tx_fail;
static volatile uint16_t s_tx_remain;

/*
 * osl 的 _write 通过 weak 钩子调到这里；勿在 bsl 里 include osl 头（分层）。
 */
int osl_debug_uart_write(const char *buf, int len);

static GPIO_TypeDef *port_of_letter(char letter)
{
    switch (letter) {
    case 'A': return GPIOA;
    case 'B': return GPIOB;
    case 'C': return GPIOC;
    case 'D': return GPIOD;
    case 'E': return GPIOE;
    case 'F': return GPIOF;
    case 'G': return GPIOG;
    case 'H': return GPIOH;
    case 'I': return GPIOI;
    case 'J': return GPIOJ;
    case 'K': return GPIOK;
    default: return NULL;
    }
}

static void port_clk_enable(GPIO_TypeDef *port)
{
    if (port == GPIOA) { __HAL_RCC_GPIOA_CLK_ENABLE(); }
    else if (port == GPIOB) { __HAL_RCC_GPIOB_CLK_ENABLE(); }
    else if (port == GPIOC) { __HAL_RCC_GPIOC_CLK_ENABLE(); }
    else if (port == GPIOD) { __HAL_RCC_GPIOD_CLK_ENABLE(); }
    else if (port == GPIOE) { __HAL_RCC_GPIOE_CLK_ENABLE(); }
    else if (port == GPIOF) { __HAL_RCC_GPIOF_CLK_ENABLE(); }
    else if (port == GPIOG) { __HAL_RCC_GPIOG_CLK_ENABLE(); }
    else if (port == GPIOH) { __HAL_RCC_GPIOH_CLK_ENABLE(); }
    else if (port == GPIOI) { __HAL_RCC_GPIOI_CLK_ENABLE(); }
    else if (port == GPIOJ) { __HAL_RCC_GPIOJ_CLK_ENABLE(); }
    else if (port == GPIOK) { __HAL_RCC_GPIOK_CLK_ENABLE(); }
}

static void uart_clk_enable(USART_TypeDef *instance)
{
    if (instance == USART1) { __HAL_RCC_USART1_CLK_ENABLE(); }
    else if (instance == USART2) { __HAL_RCC_USART2_CLK_ENABLE(); }
    else if (instance == USART3) { __HAL_RCC_USART3_CLK_ENABLE(); }
    else if (instance == UART4) { __HAL_RCC_UART4_CLK_ENABLE(); }
    else if (instance == UART5) { __HAL_RCC_UART5_CLK_ENABLE(); }
    else if (instance == USART6) { __HAL_RCC_USART6_CLK_ENABLE(); }
}

static uint32_t uart_af(USART_TypeDef *instance)
{
    if (instance == USART1 || instance == USART2 || instance == USART3) {
        return (instance == USART1) ? GPIO_AF7_USART1
               : (instance == USART2) ? GPIO_AF7_USART2 : GPIO_AF7_USART3;
    }
    if (instance == UART4 || instance == UART5 || instance == USART6) {
        return (instance == UART4) ? GPIO_AF8_UART4
               : (instance == UART5) ? GPIO_AF8_UART5 : GPIO_AF8_USART6;
    }
    return 0U;
}

bsl_err_t board_debug_uart_init(void)
{
    uintptr_t reg = 0U;
    uint32_t baud = 0U;

    /* 实例 / 波特率 / TX·RX 端口与脚号全部来自 DTB 的 debug_serial 节点。
     * 早前这些是构建期宏：改 DTS 必须重跑生成器，且 C 侧另有一份拷贝。 */
    if (!board_rt_uart(&reg, &baud, &s_tx_port_letter, &s_tx_pin_no,
                       &s_rx_port_letter, &s_rx_pin_no)) {
        return BSL_ERR_INVAL;
    }
    s_instance = (USART_TypeDef *)reg;
    s_pinmux_failed = 0U;
    s_huart_debug.Instance = s_instance;
    s_huart_debug.Init.BaudRate = baud;
    s_huart_debug.Init.WordLength = UART_WORDLENGTH_8B;
    s_huart_debug.Init.StopBits = UART_STOPBITS_1;
    s_huart_debug.Init.Parity = UART_PARITY_NONE;
    s_huart_debug.Init.Mode = UART_MODE_TX_RX;
    s_huart_debug.Init.HwFlowCtl = UART_HWCONTROL_NONE;
    s_huart_debug.Init.OverSampling = UART_OVERSAMPLING_16;
    /* HAL_UART_Init 的返回值早前被 (void) 丢掉：波特率/引脚配错时 UART 照样被
     * 当成"就绪"，日志一路静默消失。现在把它交给 bsl_init_run。 */
    if (HAL_UART_Init(&s_huart_debug) != HAL_OK) {
        return BSL_ERR_IO;
    }
    /* 复用号认不出时 MspInit 什么都没配，HAL_UART_Init 却已经返回 OK：
     * 这一条必须在这里判，否则"串口就绪但没有波形"没有任何读数能解释。 */
    return (s_pinmux_failed != 0U) ? BSL_ERR_INVAL : BSL_OK;
}

void HAL_UART_MspInit(UART_HandleTypeDef *huart)
{
    GPIO_TypeDef *tx_port;
    GPIO_TypeDef *rx_port;
    GPIO_InitTypeDef gpio = {0};

    if (s_instance == NULL || huart->Instance != s_instance) {
        /* 第二路 UART 也会走这个全局回调。MspInit 是 void，什么都不配等于
         * "HAL_UART_Init 返回 OK 但引脚没接上"—— 与上面 uart_af 打不出来的形态
         * 是同一类，所以把线索落进同一个标志，让 board_debug_uart_init() 的
         * 返回值把它暴露出来，而不是静默 return。 */
        s_pinmux_failed = 1U;
        return;
    }
    tx_port = port_of_letter(s_tx_port_letter);
    rx_port = port_of_letter(s_rx_port_letter);
    if (tx_port == NULL || rx_port == NULL) {
        return;
    }

    /* 时钟与 AF 都由运行期读到的实例/端口推出来，换 USART2/UART4 或换端口时
     * 不需要改 C 代码；配错则 HAL_UART_Init 失败、bsl_init_run 记 FAILED。 */
    uart_clk_enable(s_instance);
    port_clk_enable(tx_port);
    port_clk_enable(rx_port);

    gpio.Pin = (uint16_t)((1U << s_tx_pin_no) | (1U << s_rx_pin_no));
    gpio.Mode = GPIO_MODE_AF_PP;
    gpio.Pull = GPIO_PULLUP;
    gpio.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    gpio.Alternate = uart_af(s_instance);
    /* uart_af 返回 0 表示实例不在 USART1..3 / UART4..6 表里。AF0 是 SYS 功能，
     * 写下去等于把这个脚接到调试口，USART 永远拿不到引脚 —— 而这一切都发生在
     * HAL_UART_Init 返回 HAL_OK 的路径上。所以宁可不配，交给上层判失败。 */
    if (gpio.Alternate == 0U) {
        s_pinmux_failed = 1U;
        return;
    }
    HAL_GPIO_Init(tx_port, &gpio);
    if (rx_port != tx_port) {
        HAL_GPIO_Init(rx_port, &gpio);
    }
}

static int board_uart_write(const char *buf, int len)
{
    if (buf == NULL || len <= 0) {
        return 0;
    }
    /* 物理出口的唯一判断点：bsl_log_puts() 与 osl 的 _write（printf 家族）都经这里。
     * UART 没起来时 HAL_UART_Transmit 拿到的是 Instance=NULL 的 huart —— 解引用
     * 空指针，HardFault。 */
    if (!bsl_device_is_ready(BSL_DEV_DEBUG_UART)) {
        return 0;
    }
    if (HAL_UART_Transmit(&s_huart_debug, (const uint8_t *)buf, (uint16_t)len, 100U)
        != HAL_OK) {
        s_tx_fail++;
        s_tx_remain = s_huart_debug.TxXferCount;
        return 0;
    }
    return len;
}

int osl_debug_uart_write(const char *buf, int len)
{
    return board_uart_write(buf, len);
}

void board_debug_uart_puts(const char *s)
{
    int len;

    if (s == NULL) {
        return;
    }
    for (len = 0; s[len] != '\0'; len++) {
    }
    (void)board_uart_write(s, len);
}

/* 非阻塞收一个字节：返回 1 = 读到了，0 = 此刻没有数据。
 *
 * 为什么不做阻塞版/中断版：P6b 的 shell 是一条任务、10 ms 轮询一次，一轮里把
 * 缓冲区里已有的字节全部取走就够；阻塞版会让"没输入时任务不干活"，中断版要
 * 引入 NVIC/临界区与环形缓冲，而这条链路每秒最多几十字节。
 *
 * ORE 必须先清（RM0090 §30.3.9）：溢出错误发生后 RXNE 不再置起、直到 SR 被
 * 读后跟一次 DR 读。只读 RXNE 不看 ORE 的写法在"主机连发两行、间隔小于
 * 一个轮询周期"时会把接收永久卡死 —— 现场表现是"shell 只能吃第一行"。
 *
 * 不在这里加临界区：当前只有 shell 一条任务调用；将来若有第二个消费者，
 * 应当把整个"取字节→解析"搬进临界区，而不是只保护 DR 读。 */
int board_debug_uart_getc(char *out)
{
    UART_HandleTypeDef *h = &s_huart_debug;

    if (out == NULL || h->Instance == NULL) {
        return 0;
    }
    if (!bsl_device_is_ready(BSL_DEV_DEBUG_UART)) {
        return 0;
    }
    if (__HAL_UART_GET_FLAG(h, UART_FLAG_ORE) != RESET) {
        __HAL_UART_CLEAR_OREFLAG(h);
    }
    if (__HAL_UART_GET_FLAG(h, UART_FLAG_RXNE) == RESET) {
        return 0;
    }
    *out = (char)(h->Instance->DR & 0xFFU);
    return 1;
}

const bsl_device_driver_t bsl_driver_debug_uart = {
    .init = board_debug_uart_init,
};
