#include "board_tpad.h"

#include "board_device_registry.h"
#include "bsl_device.h"
#include "board_rt.h"
#include "bsl_log.h"

#include "stm32f4xx_hal.h"

/* 定标采样：10 次里去掉两端各 2 个、中间 6 个取平均 —— 与参考工程一致。
 * 直接用 10 次平均会被偶发的极值拉偏（板上一次中断抖动就能差出上百个计数），
 * 而基线偏一点，门限就整体偏移，表现为"灵敏度忽然变了"。 */
#define TPAD_CAL_SAMPLES 10U
#define TPAD_CAL_TRIM    2U

static board_rt_tpad_t s_cfg;
static uint16_t s_default;
static uint16_t s_threshold;
static int s_ready;

/* 开时钟：端口与定时器都来自 DTB，所以不能写死 GPIOA/TIM2 ——
 * DTB 换成 TIM3 而这里还开着 TIM2，现象就是"寄存器全 0、永远超时"。
 * 只收录本工程可能用到的定时器（与 board_rt 的 timer_of_name 同一张表）。 */
static void tpad_clock_enable(void)
{
    switch ((uintptr_t)s_cfg.port) {
    case GPIOA_BASE: __HAL_RCC_GPIOA_CLK_ENABLE(); break;
    case GPIOB_BASE: __HAL_RCC_GPIOB_CLK_ENABLE(); break;
    case GPIOC_BASE: __HAL_RCC_GPIOC_CLK_ENABLE(); break;
    case GPIOD_BASE: __HAL_RCC_GPIOD_CLK_ENABLE(); break;
    case GPIOE_BASE: __HAL_RCC_GPIOE_CLK_ENABLE(); break;
    default: break;
    }
    if (s_cfg.inst == TIM2) {
        __HAL_RCC_TIM2_CLK_ENABLE();
    } else if (s_cfg.inst == TIM3) {
        __HAL_RCC_TIM3_CLK_ENABLE();
    } else if (s_cfg.inst == TIM4) {
        __HAL_RCC_TIM4_CLK_ENABLE();
    } else if (s_cfg.inst == TIM5) {
        __HAL_RCC_TIM5_CLK_ENABLE();
    }
}

/* 切到"推挽输出 0"放电，再切回"复用浮空"开始充电。
 * 顺序不能颠倒：先切 AF 再放电，会把捕获脚和输出脚同时接到引脚上。 */
static void tpad_discharge(void)
{
    GPIO_InitTypeDef gpio;

    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Pull = GPIO_PULLDOWN;
    gpio.Speed = GPIO_SPEED_FREQ_MEDIUM;
    gpio.Pin = (uint16_t)(1U << s_cfg.pin);
    HAL_GPIO_Init(s_cfg.port, &gpio);
    HAL_GPIO_WritePin(s_cfg.port, (uint16_t)(1U << s_cfg.pin), GPIO_PIN_RESET);

    HAL_Delay(s_cfg.charge_ms);

    s_cfg.inst->SR = 0U;
    s_cfg.inst->CNT = 0U;

    gpio.Mode = GPIO_MODE_AF_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Alternate = s_cfg.af;
    HAL_GPIO_Init(s_cfg.port, &gpio);
}

/* 量一次充电时间。超时（CNT 越过 arr_max-500 仍未捕获）时直接把 CNT 当结果返回：
 * 这个值一定大于门限，会被判成"按下" —— 听起来反直觉，但它正是参考工程的取舍：
 * 引脚悬空时会一直判按下，从而在自检里暴露成"基线异常"，而不是静默不响应。 */
static uint16_t tpad_val(void)
{
    tpad_discharge();
    while ((s_cfg.inst->SR & (1U << s_cfg.channel)) == 0U) {
        if (s_cfg.inst->CNT > (s_cfg.arr_max - 500U)) {
            return (uint16_t)s_cfg.inst->CNT;
        }
    }
    switch (s_cfg.channel) {
    case 1U: return (uint16_t)s_cfg.inst->CCR1;
    case 2U: return (uint16_t)s_cfg.inst->CCR2;
    default: return 0U;
    }
}

/* 定时器输入捕获配置：CCyS=01（IC 映射到 TI）、不分频、不滤波、上升沿。
 * 自由计数（ARR 给到 arr_max），PSC = prescaler-1（与 tpad_init(psc) 同口径）。 */
static void tpad_hw_init(void)
{
    GPIO_InitTypeDef gpio;
    uint32_t shift = 8U * (s_cfg.channel - 1U);

    gpio.Mode = GPIO_MODE_AF_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_MEDIUM;
    gpio.Alternate = s_cfg.af;
    gpio.Pin = (uint16_t)(1U << s_cfg.pin);
    HAL_GPIO_Init(s_cfg.port, &gpio);

    /* 通道 1/2 落在 CCMR1；3/4 在 CCMR2（board_rt 已把 channel 限在 1..2） */
    s_cfg.inst->CCMR1 |= (1U << shift);        /* CCyS = 01 */
    s_cfg.inst->CCMR1 &= ~(3U << (shift + 2U)); /* ICyPSC = 00 不分频 */
    s_cfg.inst->CCMR1 &= ~(0xFU << (shift + 4U)); /* ICyF = 0000 不滤波 */
    s_cfg.inst->CCER |= (1U << (4U * (s_cfg.channel - 1U)));  /* CCyE = 1 */
    s_cfg.inst->CCER &= ~(1U << (1U + 4U * (s_cfg.channel - 1U))); /* CCyP = 0 上升沿 */

    s_cfg.inst->ARR = s_cfg.arr_max;
    s_cfg.inst->PSC = s_cfg.prescaler - 1U;
    s_cfg.inst->CR1 |= TIM_CR1_CEN;
}

bsl_err_t board_tpad_init(void)
{
    uint16_t buf[TPAD_CAL_SAMPLES];
    uint32_t sum = 0U;
    unsigned i;
    unsigned j;

    s_ready = 0;
    if (board_rt_tpad(&s_cfg) == 0) {
        BSL_LOG_MOD_ERR("tpad", "no dtb node\r\n");
        return BSL_ERR_NOINIT;
    }
    /* 时钟必须在配寄存器之前开：不开时定时器寄存器写入被静默丢弃，
     * 现象是"CNT 永远 0、永远超时"，和 P6c 那次 DMA2 时钟漏开同型。 */
    tpad_clock_enable();

    tpad_hw_init();

    for (i = 0U; i < TPAD_CAL_SAMPLES; i++) {
        buf[i] = tpad_val();
        /* 两次采样之间留一点时间让电容回到静止电位；参考工程是 10 ms。 */
        HAL_Delay(10U);
    }
    /* 升序排序后掐头去尾 */
    for (i = 0U; i < TPAD_CAL_SAMPLES - 1U; i++) {
        for (j = i + 1U; j < TPAD_CAL_SAMPLES; j++) {
            if (buf[i] > buf[j]) {
                uint16_t t = buf[i];
                buf[i] = buf[j];
                buf[j] = t;
            }
        }
    }
    for (i = TPAD_CAL_TRIM; i < TPAD_CAL_SAMPLES - TPAD_CAL_TRIM; i++) {
        sum += buf[i];
    }
    s_default = (uint16_t)(sum / (TPAD_CAL_SAMPLES - 2U * TPAD_CAL_TRIM));
    /* 判定门限用**倍率**口径（> 4/3 基线），与参考工程 tpad_scan 相同。
     *
     * 不用"基线 + 固定增量"的理由是实测：本板空载基线 17 个计数（PSC=20 →
     * 4.5 MHz，充电约 3.8 µs），加 100 就要求读数变成 7 倍 —— 手指按到极限
     * 也达不到，而且这种"永远按不到"在 POST 里仍然会显示 PASS（基线合法），
     * 是最难查的一类错。门限按比例走，基线多小都成立。 */
    s_threshold = (uint16_t)(s_default + (s_default + 2U) / 3U);

    BSL_LOG_MOD_INF("tpad", "default=");
    bsl_log_put_dec((int)s_default);
    BSL_LOG_INF(" thr=");
    bsl_log_put_dec((int)s_threshold);
    BSL_LOG_INF(" tim=");
    bsl_log_put_dec((int)s_cfg.prescaler);
    BSL_LOG_INF("\r\n");

    /* 判据取自参考工程 system_init()：default 超过 arr_max/2 或超过 1000 都算失败。
     * 前者是"引脚根本没配对"（充电时间几乎打满计数），后者是"板上有持续按压"。 */
    if (s_default == 0U || s_default > (uint16_t)(s_cfg.arr_max / 2U) || s_default > 1000U) {
        BSL_LOG_MOD_ERR("tpad", "baseline out of range -> fail\r\n");
        return BSL_ERR_IO;
    }
    s_ready = 1;
    return BSL_OK;
}

int board_tpad_ready(void)
{
    return s_ready;
}

uint16_t board_tpad_default(void)
{
    return s_default;
}

uint16_t board_tpad_threshold(void)
{
    return s_threshold;
}

int board_tpad_scan(int continuous)
{
    /* 节流计数：>0 时忽略按键，避免一次按压被连续判成多次 */
    static uint8_t hold;
    uint16_t best = 0U;
    uint16_t v;
    unsigned n;
    unsigned ok = 0U;
    unsigned need;
    unsigned i;

    if (!s_ready) {
        return BSL_ERR_NOINIT;
    }
    /* 连发模式采 6 次、单发采 3 次；2/3 的采样要越过 5/4 基线才算有效，
     * 与参考工程同口径（一次抖动不该被当成按键）。 */
    n = continuous ? 6U : 3U;
    need = (n * 2U) / 3U;

    for (i = 0U; i < n; i++) {
        v = tpad_val();
        /* 5/4 基线：与参考工程 tpad_get_maxval 的"有效"口径一致，比 4/3 判定门限松，
         * 用来先筛掉噪声，避免一次抖动把 2/3 的采样都算成有效。 */
        if (v > (uint16_t)(s_default + (s_default / 4U))) {
            ok++;
        }
        if (v > best) {
            best = v;
        }
    }
    if (ok < need) {
        hold = 0U;
        return 0;
    }
    /* 下界用倍率门限（> 4/3），上界 10×：手指按住不放/引脚被短路时读数会大得
     * 离谱，那种"按下"不能当按键用。两个上/下界都是参考工程的原口径。 */
    if (best <= s_threshold || best >= (uint16_t)(s_default * 10U)) {
        hold = 0U;
        return 0;
    }
    if (hold != 0U) {
        hold--;
        return 0;
    }
    /* 单发模式：按下后必须至少 3 次扫描不成立才能再触发 */
    hold = continuous ? 0U : 3U;
    return 1;
}

/* ---- 设备注册（registry 的 bsl_driver_brd_tpad / bsl_api_brd_tpad） ---- */

const bsl_tpad_api_t bsl_api_brd_tpad = {
    .ready = board_tpad_ready,
    .baseline = board_tpad_default,
    .threshold = board_tpad_threshold,
    .scan = board_tpad_scan,
};

const struct bsl_device_driver bsl_driver_brd_tpad = {
    .init = board_tpad_init,
};
