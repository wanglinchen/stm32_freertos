#include "bsl_exti.h"

#include "board_rt.h"

#include "stm32f4xx_hal.h"

/* EXTI（P9c-2）：EXTI0 ← PA0（WK_UP）。
 *
 * 寄存器直配而非 HAL 的 HAL_GPIO_EXTI_*：本批不做"回调注册"，ISR 只数边沿，
 * 走 HAL 会多一层指针回调，出问题时既看不见是哪一步也没好处。
 *
 * 引脚微秒级延时用 DWT->CYCCNT：osl_boot 已置 CYCCNTENA（GET），这里只做差。
 * 脉冲宽度 10 µs 远大于 EXTI 的采样周期，也远小于人手与按键消抖的尺度。 */
#define EXTI_PULSE_US 10UL

static board_rt_exti_t s_cfg;
static volatile unsigned s_count;
static int s_ready;

static void exti_delay_us(uint32_t us)
{
    uint32_t per_us = SystemCoreClock / 1000000UL;
    uint32_t t0 = DWT->CYCCNT;
    uint32_t wait = us * per_us;

    if (wait == 0U) {
        wait = 1U;
    }
    while ((DWT->CYCCNT - t0) < wait) {
    }
}

bsl_err_t bsl_exti_init(void)
{
    board_rt_exti_t cfg;
    GPIO_InitTypeDef gi = {0};
    IRQn_Type irq;
    unsigned shift;
    unsigned index;

    if (board_rt_exti(&cfg) == 0) {
        return BSL_ERR_INVAL;
    }
    if (cfg.line > 4U) {
        return BSL_ERR_NOSUPPORT;
    }
    s_cfg = cfg;

    switch ((uintptr_t)cfg.port) {
    case GPIOA_BASE: __HAL_RCC_GPIOA_CLK_ENABLE(); break;
    case GPIOB_BASE: __HAL_RCC_GPIOB_CLK_ENABLE(); break;
    case GPIOC_BASE: __HAL_RCC_GPIOC_CLK_ENABLE(); break;
    case GPIOD_BASE: __HAL_RCC_GPIOD_CLK_ENABLE(); break;
    case GPIOE_BASE: __HAL_RCC_GPIOE_CLK_ENABLE(); break;
    default: return BSL_ERR_INVAL;
    }
    __HAL_RCC_SYSCFG_CLK_ENABLE();

    /* 输入 + 无上下拉：WK_UP 板上自带下拉，开内部下拉会与外部并联，
     * 按下时电平仍对，但松开后的回落变慢，电平边沿会拖出毛刺。 */
    gi.Pin = (uint16_t)(1U << cfg.pin);
    gi.Mode = GPIO_MODE_INPUT;
    gi.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(cfg.port, &gi);

    /* EXTICR 选源：每 4 条线一个寄存器，每线 4 位，值是端口序号（A=0）。 */
    index = cfg.line / 4U;
    shift = (cfg.line % 4U) * 4U;
    SYSCFG->EXTICR[index] = (SYSCFG->EXTICR[index] & ~(0xFUL << shift))
                            | ((uint32_t)cfg.src << shift);

    EXTI->IMR = (EXTI->IMR & ~(1UL << cfg.line)) | (1UL << cfg.line);
    if (cfg.rising != 0) {
        EXTI->RTSR = (EXTI->RTSR & ~(1UL << cfg.line)) | (1UL << cfg.line);
        EXTI->FTSR &= ~(1UL << cfg.line);
    } else {
        EXTI->FTSR = (EXTI->FTSR & ~(1UL << cfg.line)) | (1UL << cfg.line);
        EXTI->RTSR &= ~(1UL << cfg.line);
    }
    EXTI->PR = (1UL << cfg.line); /* rc_w1：残留挂起位必须先清，否则第一次进中断
                                   * 会读到上一次的旧边沿，计数从 1 开始。 */

    switch (cfg.line) {
    case 0U: irq = EXTI0_IRQn; break;
    case 1U: irq = EXTI1_IRQn; break;
    case 2U: irq = EXTI2_IRQn; break;
    case 3U: irq = EXTI3_IRQn; break;
    default: irq = EXTI4_IRQn; break;
    }
    /* 优先级 5/0 与 USART3 一致：这条线上的编码是 "中断优先级"，数值越大越低。
     * ISR 不调任何 RTOS API，所以不受 configMAX_SYSCALL_INTERRUPT_PRIORITY 限制；
     * 但仍要给一个明确的数，不让它落在复位默认 0（那会压过调度器）。 */
    HAL_NVIC_SetPriority(irq, 5U, 0U);
    HAL_NVIC_EnableIRQ(irq);

    s_count = 0U;
    s_ready = 1;
    return BSL_OK;
}

static int exti_api_ready(void)
{
    return s_ready;
}

static unsigned exti_api_count(void)
{
    return s_count;
}

static void exti_api_clear(void)
{
    s_count = 0U;
}

static unsigned exti_api_pending(void)
{
    return (unsigned)((EXTI->PR & (1UL << s_cfg.line)) != 0U);
}

static void exti_api_info(unsigned *line, unsigned *src, int *rising)
{
    if (line != NULL) {
        *line = s_cfg.line;
    }
    if (src != NULL) {
        *src = s_cfg.src;
    }
    if (rising != NULL) {
        *rising = s_cfg.rising;
    }
}

/* 真引脚脉冲：把引脚临时切成推挽输出，打 n 个"高 10 µs / 低 10 µs"，
 * 期间边沿经真正的引脚进 EXTI；打完恢复原来的 MODER（输入）。
 *
 * 为什么要恢复而不是留着输出：这根脚可能是按键输入（本板正是 WK_UP），
 * 自检结束后必须物归原主，否则后面的按键轮询读到的是"自己写出去的电平"。 */
static unsigned exti_api_pulse_pin(unsigned n)
{
    uint32_t save;
    uint32_t bit = 1UL << s_cfg.pin;
    uint32_t mask = 3UL << (s_cfg.pin * 2U);
    unsigned i;

    if (s_ready == 0 || n == 0U) {
        return s_count;
    }
    save = s_cfg.port->MODER;
    for (i = 0U; i < n; i++) {
        s_cfg.port->MODER = (save & ~mask) | (1UL << (s_cfg.pin * 2U)); /* 输出 */
        s_cfg.port->BSRR = bit;                                          /* 高 */
        exti_delay_us(EXTI_PULSE_US);
        s_cfg.port->BSRR = (bit << 16);                                  /* 低 */
        exti_delay_us(EXTI_PULSE_US);
        s_cfg.port->MODER = save;                                        /* 回输入 */
        exti_delay_us(EXTI_PULSE_US);
    }
    return s_count;
}

/* 软件挂起：直接置 SWIER 的对应位，硬件把 PR 置起来，之后走与引脚边沿
 * 完全相同的中断路径。用来单独验证"PR → NVIC → ISR"后半段。 */
static unsigned exti_api_trigger_sw(unsigned n)
{
    unsigned i;

    if (s_ready == 0 || n == 0U) {
        return s_count;
    }
    for (i = 0U; i < n; i++) {
        EXTI->SWIER = (1UL << s_cfg.line);
        exti_delay_us(EXTI_PULSE_US);
    }
    return s_count;
}

/* 向量名由启动文件的弱符号提供，不出头文件（外部没人调它）。 */
void EXTI0_IRQHandler(void);
void EXTI0_IRQHandler(void)
{
    if ((EXTI->PR & EXTI_PR_PR0) != 0U) {
        EXTI->PR = EXTI_PR_PR0; /* rc_w1：写 1 清挂起 */
        s_count++;
    }
}

void EXTI1_IRQHandler(void);
void EXTI1_IRQHandler(void)
{
    if ((EXTI->PR & EXTI_PR_PR1) != 0U) {
        EXTI->PR = EXTI_PR_PR1;
        s_count++;
    }
}

void EXTI2_IRQHandler(void);
void EXTI2_IRQHandler(void)
{
    if ((EXTI->PR & EXTI_PR_PR2) != 0U) {
        EXTI->PR = EXTI_PR_PR2;
        s_count++;
    }
}

void EXTI3_IRQHandler(void);
void EXTI3_IRQHandler(void)
{
    if ((EXTI->PR & EXTI_PR_PR3) != 0U) {
        EXTI->PR = EXTI_PR_PR3;
        s_count++;
    }
}

void EXTI4_IRQHandler(void);
void EXTI4_IRQHandler(void)
{
    if ((EXTI->PR & EXTI_PR_PR4) != 0U) {
        EXTI->PR = EXTI_PR_PR4;
        s_count++;
    }
}

const bsl_exti_api_t bsl_api_brd_exti = {
    .ready = exti_api_ready,
    .count = exti_api_count,
    .clear = exti_api_clear,
    .pending = exti_api_pending,
    .info = exti_api_info,
    .pulse_pin = exti_api_pulse_pin,
    .trigger_sw = exti_api_trigger_sw,
};

static bsl_err_t exti_driver_init(void)
{
    return bsl_exti_init();
}

const bsl_device_driver_t bsl_driver_brd_exti = {
    .init = exti_driver_init,
};
