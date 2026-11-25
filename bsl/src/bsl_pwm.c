#include "bsl_pwm.h"

#include "board_rt.h"

#include "stm32f4xx_hal.h"

#include <stddef.h>

/* PWM（P9c-2）：TIM3_CH2 / PB5 驱动 LCD 背光，寄存器直配。
 *
 * 为什么不用 HAL 的 HAL_TIM_PWM_Init：本批唯一的判据是"输出频率与占空比"，
 * HAL 的 init/msp 两层会把 PSC/ARR 的实际落值藏在 handle 里，而 POST 要报的是
 * 定时器自己的读数。SAI 走 HAL 是因为音频那条链要用它的 DMA 双缓冲，两条路径
 * 互不干扰。
 *
 * 频率怎么定（[数据手册] RM0090 §18.3.4）：
 *   定时器时钟 = APB1 时钟 × 2（只要 APB1 预分频 ≠ 1；本板 PCLK1 = 42 MHz，
 *   所以 TIM3 的计数时钟是 84 MHz）。因此 PSC 必须由"当前 PCLK1"现算，
 *   不能抄参考工程的常量 —— 参考工程是 192 MHz 内核，同样的 arr/psc 到这里
 *   频率会整体偏移 33%，屏上看只是"背光有点闪"，读数上什么都不指向时钟树。
 *
 * 占空比分辨率为 ARR+1 = 1000 档（0.1%），足够背光调光；再细的档位会挤掉
 * 最低亮度段的有效变化（人眼对低亮度段更敏感）。 */
#define PWM_ARR 999UL

/* 测频窗口上限：20 个 PWM 周期在 1 kHz 下是 20 ms；给 300 ms 的余量，
 * 足够覆盖 1 kHz 但配置成 100 Hz 这类"频率对了但差一个量级"的现场，
 * 超时返回 TIMEOUT 而不是死等 —— 定时器没起来时 POST 得能收尾。 */
#define PWM_MEAS_LIMIT_CYC 50000000UL

static board_rt_pwm_t s_cfg;
static uint32_t s_clk_hz;
static uint32_t s_freq_hz;
static uint32_t s_arr;
static int s_ready;

/* 通道 1..2 都在 CCMR1 里，驱动只做这两路；3/4 落在 CCMR2，等真有需求再开
 * （与 board_rt 的 tpad 解析同一口径，避免"配了个没验证过的通道"）。 */
static bsl_err_t pwm_clock_enable(void)
{
    switch ((uintptr_t)s_cfg.port) {
    case GPIOA_BASE: __HAL_RCC_GPIOA_CLK_ENABLE(); break;
    case GPIOB_BASE: __HAL_RCC_GPIOB_CLK_ENABLE(); break;
    case GPIOC_BASE: __HAL_RCC_GPIOC_CLK_ENABLE(); break;
    case GPIOD_BASE: __HAL_RCC_GPIOD_CLK_ENABLE(); break;
    case GPIOE_BASE: __HAL_RCC_GPIOE_CLK_ENABLE(); break;
    default: return BSL_ERR_INVAL;
    }
    if (s_cfg.inst == TIM2) {
        __HAL_RCC_TIM2_CLK_ENABLE();
    } else if (s_cfg.inst == TIM3) {
        __HAL_RCC_TIM3_CLK_ENABLE();
    } else if (s_cfg.inst == TIM4) {
        __HAL_RCC_TIM4_CLK_ENABLE();
    } else if (s_cfg.inst == TIM5) {
        __HAL_RCC_TIM5_CLK_ENABLE();
    } else {
        return BSL_ERR_NOSUPPORT;
    }
    return BSL_OK;
}

bsl_err_t bsl_pwm_init(void)
{
    board_rt_pwm_t cfg;
    GPIO_InitTypeDef gi = {0};
    uint32_t timclk;
    uint32_t per;
    uint32_t ccr;

    if (board_rt_pwm(&cfg) == 0) {
        return BSL_ERR_INVAL;
    }
    if (cfg.channel < 1U || cfg.channel > 2U) {
        return BSL_ERR_NOSUPPORT;
    }
    s_cfg = cfg;

    timclk = HAL_RCC_GetPCLK1Freq();
    /* APB1 预分频 ≠ 1 时定时器时钟是 PCLK1 的两倍（RM0090 §6.3.11 的 TIMxCLK）。 */
    if ((RCC->CFGR & RCC_CFGR_PPRE1) != 0U) {
        timclk *= 2U;
    }
    if (timclk == 0U || cfg.frequency == 0U) {
        return BSL_ERR_INVAL;
    }
    per = timclk / (cfg.frequency * (PWM_ARR + 1U));
    if (per == 0U || (per - 1U) > 0xFFFFU) {
        /* 分频比落在 0 或超过 16 位：这个目标频率本板配不出来，如实拒绝，
         * 不写一个"看起来是合法 PSC"的值让频率整体偏移。 */
        return BSL_ERR_NOSUPPORT;
    }

    bsl_err_t rc = pwm_clock_enable();
    if (rc != BSL_OK) {
        return rc;
    }

    gi.Pin = (uint16_t)(1U << cfg.pin);
    gi.Mode = GPIO_MODE_AF_PP;
    gi.Pull = GPIO_NOPULL;
    gi.Speed = GPIO_SPEED_FREQ_HIGH;
    gi.Alternate = cfg.af;
    HAL_GPIO_Init(cfg.port, &gi);

    s_clk_hz = timclk;
    s_freq_hz = cfg.frequency;
    s_arr = PWM_ARR;

    s_cfg.inst->CR1 = 0U;
    s_cfg.inst->PSC = (uint16_t)(per - 1U);
    s_cfg.inst->ARR = (uint16_t)PWM_ARR;
    s_cfg.inst->CNT = 0U;
    if (cfg.channel == 1U) {
        s_cfg.inst->CCMR1 = (uint16_t)((6U << 4) | (1U << 3));  /* OC1M=PWM1, OC1PE */
        s_cfg.inst->CCR1 = (uint16_t)(PWM_ARR + 1U);
    } else {
        s_cfg.inst->CCMR1 = (uint16_t)((6U << 12) | (1U << 11)); /* OC2M=PWM1, OC2PE */
        s_cfg.inst->CCR2 = (uint16_t)(PWM_ARR + 1U);
    }
    s_cfg.inst->CCER = (uint16_t)(cfg.channel == 1U ? (1U << 0) : (1U << 4));
    /* ARPE + CEN：预装载使能后 ARR/PSC 在下一次更新事件生效，避免改占空比时
     * 出现半周期的窄脉冲（背光上表现为一次可见的暗跳）。 */
    s_cfg.inst->EGR = 1U;
    s_cfg.inst->CR1 = (uint16_t)((1U << 7) | (1U << 0));

    /* 初始占空比 100%：PB5 在这之前是"GPIO 常高"的背光，切到 AF 后必须立刻
     * 维持同样亮度，否则载入瞬间屏会暗一下再亮（人眼看得见）。 */
    ccr = PWM_ARR + 1U;
    if (cfg.channel == 1U) {
        s_cfg.inst->CCR1 = (uint16_t)ccr;
    } else {
        s_cfg.inst->CCR2 = (uint16_t)ccr;
    }
    s_ready = 1;
    return BSL_OK;
}

static int pwm_api_ready(void)
{
    return s_ready;
}

static uint32_t pwm_api_clock_hz(void)
{
    return s_clk_hz;
}

static uint32_t pwm_api_frequency(void)
{
    return s_freq_hz;
}

static bsl_err_t pwm_set_duty(uint32_t permille)
{
    uint32_t ccr;

    if (s_ready == 0) {
        return BSL_ERR_NOINIT;
    }
    if (permille > 1000U) {
        return BSL_ERR_INVAL;
    }
    ccr = (permille * (s_arr + 1U)) / 1000U;
    if (s_cfg.channel == 1U) {
        s_cfg.inst->CCR1 = (uint16_t)ccr;
    } else {
        s_cfg.inst->CCR2 = (uint16_t)ccr;
    }
    return BSL_OK;
}

static uint32_t pwm_api_duty(void)
{
    uint32_t ccr;

    if (s_ready == 0) {
        return 0U;
    }
    ccr = (s_cfg.channel == 1U) ? (uint32_t)s_cfg.inst->CCR1
                                : (uint32_t)s_cfg.inst->CCR2;
    if (ccr > s_arr + 1U) {
        ccr = s_arr + 1U;
    }
    return (ccr * 1000U) / (s_arr + 1U);
}

/* 实测频率：逐个 PWM 周期量"相邻两次比较匹配之间的 CYCCNT 差"，9 个里取中位。
 *
 * 为什么逐周期取中位，而不是"数 N 个再除总时间"：
 *   ① 定时器一直在跑，进来时 CCxIF 可能挂着更早一次的匹配（旧周期的尾巴）。
 *      把总时间除 N 时，起点落在哪个相位是随机的，20 次可能只覆盖 19.x 个周期，
 *      读数就会凭空偏高几个百分点（实测踩到 1086 Hz、1030 Hz、1025 Hz 三个值，
 *      差值正好是相位偏移）。
 *   ② 偶发一个"多出来的"标志（例如进入测量前刚改过 CCR 的那个周期）会把总
 *      时间砍掉半周期，同样只影响总时间法。
 *   ③ 逐周期差值的偏差只来自"两次观察到标志的时刻"，两次的观察延迟相互抵消；
 *      取中位又能把偶发的一两个异常间隔（被中断拖长、或多一颗标志）直接丢掉。
 *
 * 为什么不用"读 ARR/PSC 再算一遍"：那样只是把配置抄回来，判不出"定时器到底
 * 有没有跑"。CCxIF 是硬件在 CNT 真正走到 CCR 时置的位，数它等价于数波形周期；
 * CYCCNT 由内核自由计数（osl_boot 已置 CYCCNTENA 并在复位后清零）。
 */
#define PWM_MEAS_PERIODS 9U

static bsl_err_t pwm_measure_freq(uint32_t *hz)
{
    uint32_t flag;
    uint32_t iv[PWM_MEAS_PERIODS];
    uint32_t count = 0U;
    uint32_t t_prev;
    uint32_t now;
    unsigned dropped = 0U;
    unsigned i;
    unsigned j;

    if (s_ready == 0) {
        return BSL_ERR_NOINIT;
    }
    if (hz == NULL) {
        return BSL_ERR_INVAL;
    }
    flag = (s_cfg.channel == 1U) ? (uint32_t)TIM_SR_CC1IF : (uint32_t)TIM_SR_CC2IF;
    t_prev = DWT->CYCCNT;
    /* 先丢掉两颗标志：第一颗可能是旧周期留下的挂起位，第二颗必然是一次新的
     * 匹配 —— 从它开始，后面每一颗都落在真正的周期边界上。 */
    while (count < PWM_MEAS_PERIODS) {
        if ((s_cfg.inst->SR & flag) != 0U) {
            s_cfg.inst->SR = (uint16_t)(~flag); /* rc_w0：写 0 才清 */
            now = DWT->CYCCNT;
            if (dropped < 2U) {
                dropped++;
                t_prev = now;
            } else {
                iv[count] = now - t_prev;
                t_prev = now;
                count++;
            }
        }
        if ((DWT->CYCCNT - t_prev) > PWM_MEAS_LIMIT_CYC) {
            return BSL_ERR_TIMEOUT;
        }
    }
    /* 9 个间隔的中位数（插入排序，9 个数不必上 qsort）。 */
    for (i = 1U; i < PWM_MEAS_PERIODS; i++) {
        uint32_t v = iv[i];
        j = i;
        while (j > 0U && iv[j - 1U] > v) {
            iv[j] = iv[j - 1U];
            j--;
        }
        iv[j] = v;
    }
    now = iv[PWM_MEAS_PERIODS / 2U]; /* 中位间隔 = 一个周期的内核周期数 */
    if (now == 0U) {
        return BSL_ERR_IO;
    }
    *hz = (uint32_t)((uint64_t)SystemCoreClock / (uint64_t)now);
    return BSL_OK;
}

/* 寄存器读回：POST 把"驱动以为配了什么"与"寄存器里真是什么"一起打出来。
 * 频率对不上时，先看这里 —— 若 PSC/ARR 与预期一致，那问题在时钟树或测量；
 * 若不一致，问题在写序（预装载/UG）。 */
static void pwm_api_timing(uint32_t *psc, uint32_t *arr, uint32_t *ccr)
{
    if (s_ready == 0) {
        if (psc != NULL) { *psc = 0U; }
        if (arr != NULL) { *arr = 0U; }
        if (ccr != NULL) { *ccr = 0U; }
        return;
    }
    if (psc != NULL) { *psc = (uint32_t)s_cfg.inst->PSC; }
    if (arr != NULL) { *arr = (uint32_t)s_cfg.inst->ARR; }
    if (ccr != NULL) {
        *ccr = (s_cfg.channel == 1U) ? (uint32_t)s_cfg.inst->CCR1
                                     : (uint32_t)s_cfg.inst->CCR2;
    }
}

const bsl_pwm_api_t bsl_api_brd_pwm = {
    .ready = pwm_api_ready,
    .clock_hz = pwm_api_clock_hz,
    .frequency = pwm_api_frequency,
    .set_duty = pwm_set_duty,
    .duty = pwm_api_duty,
    .measure_freq = pwm_measure_freq,
    .timing = pwm_api_timing,
};

static bsl_err_t pwm_driver_init(void)
{
    return bsl_pwm_init();
}

const bsl_device_driver_t bsl_driver_brd_pwm = {
    .init = pwm_driver_init,
};
