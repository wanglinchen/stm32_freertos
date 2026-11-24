#include "bsl_rtc.h"

#include <string.h>

#include "board_rt.h"
#include "bsl_device.h"
#include "bsl_device_api.h"
#include "stm32f4xx_hal.h"

#define RTC_WPR_KEY1 0x00CAU
#define RTC_WPR_KEY2 0x0053U
#define RTC_WPR_LOCK 0x00FFU

/* BKP0R 里的"本模块已初始化过"标记。BKP 域掉电全清，所以它同时承担
 * "有没有电池/上次是否被 BDRST 复位"的判据：有它 = 日历在接着走，别重设。 */
#define RTC_BKP_MAGIC 0x52544331U /* "RTC1" */

#define RTC_LSE_START_MS 3000U
#define RTC_LSI_START_MS 500U
#define RTC_REG_SYNC_MS 10U
#define RTC_INIT_MS 100U

static bsl_rtc_src_t s_src = BSL_RTC_SRC_LSE;
static int s_ready;

static void rtc_unlock(void)
{
    RTC->WPR = RTC_WPR_KEY1;
    RTC->WPR = RTC_WPR_KEY2;
}

static void rtc_lock(void)
{
    RTC->WPR = RTC_WPR_LOCK;
}

/* 等某寄存器的位被硬件置起。用 HAL_GetTick（DWT 驱动、单调），不做周期估算 ——
 * 周期数随主频变，写死循环次数在换频时就成了"看起来在等、其实一秒就超时"。 */
static int wait_flag(volatile uint32_t *reg, uint32_t mask, uint32_t ms)
{
    uint32_t start = HAL_GetTick();

    while ((*reg & mask) == 0U) {
        if ((HAL_GetTick() - start) > ms) {
            return 0;
        }
    }
    return 1;
}

/* __DATE__ 形如 "Nov 24 2026"，__TIME__ 形如 "10:11:12"。
 * 本机没有墙钟来源，首次初始化只能种编译时刻；它不是"现在的时间"，
 * 而是"这台机器最后一次被编译的时间"—— POST 行里如实打出来，不装作墙钟。 */
static const char k_mon[12][4] = {
    "Jan", "Feb", "Mar", "Apr", "May", "Jun",
    "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
};

static void rtc_build_time(bsl_rtc_time_t *t)
{
    const char *d = __DATE__;
    const char *c = __TIME__;
    unsigned m;

    t->year = 2000U + (uint16_t)((d[7] - '0') * 1000 + (d[8] - '0') * 100
                                 + (d[9] - '0') * 10 + (d[10] - '0') - 2000);
    for (m = 0U; m < 12U; m++) {
        if (strncmp(&d[0], k_mon[m], 3) == 0) {
            break;
        }
    }
    t->month = (uint8_t)((m < 12U) ? (m + 1U) : 1U);
    /* "Nov 24" 与 "Nov  4" 的日期都是两字符宽，前导位可能是空格 */
    t->day = (uint8_t)((d[4] == ' ' ? 0 : (d[4] - '0')) * 10 + (d[5] - '0'));
    t->weekday = 1U; /* 硬件不校验；下一次写入后由 WDU 自行推进 */
    t->hour = (uint8_t)((c[0] - '0') * 10 + (c[1] - '0'));
    t->minute = (uint8_t)((c[3] - '0') * 10 + (c[4] - '0'));
    t->second = (uint8_t)((c[6] - '0') * 10 + (c[7] - '0'));
}

/* BCD：TR/DR 每个字段由"十位 nibble + 个位 nibble"拼成，中间夹着保留位与
 * 星期字段，所以只能按字段各自拼，不能整体转换。 */
static uint32_t bcd_pair(uint8_t v)
{
    return ((uint32_t)(v / 10U) << 4) | (uint32_t)(v % 10U);
}

bsl_err_t bsl_rtc_init(void)
{
    board_rt_rtc_t cfg;
    uint32_t lse_hz;
    uint32_t sync_div;
    uint32_t rtcsel;
    bsl_rtc_time_t t;
    int lse_ok;

    /* DBP 不置位时 RTC/WPR/BKP 全部写不进，而且读回来的还是旧值 —— 表现为
     * "配置写了、时间不动"，没有任何报错。 */
    __HAL_RCC_PWR_CLK_ENABLE();
    PWR->CR |= PWR_CR_DBP;

    rtcsel = RCC->BDCR & RCC_BDCR_RTCSEL;
    if ((RCC->BDCR & RCC_BDCR_RTCEN) != 0U && rtcsel != 0U
        && RTC->BKP0R == RTC_BKP_MAGIC) {
        /* 已在走：只解锁做同步，绝不重设时间 */
        s_src = (rtcsel == RCC_BDCR_RTCSEL_0) ? BSL_RTC_SRC_LSE : BSL_RTC_SRC_LSI;
        rtc_unlock();
        s_ready = 1;
        return BSL_OK;
    }

    /* 改 RTCSEL 前必须复位备份域（RM0090 §5.3.7）：否则时钟源切换被硬件忽略。
     * 代价是 BKP 与日历寄存器全清 —— 这也是上面那条"魔数在就别进来"的原因。 */
    RCC->BDCR |= RCC_BDCR_BDRST;
    RCC->BDCR &= ~RCC_BDCR_BDRST;

    if (board_rt_rtc(&cfg) != 0 && cfg.use_lsi == 0) {
        RCC->BDCR |= RCC_BDCR_LSEON;
        lse_ok = wait_flag(&RCC->BDCR, RCC_BDCR_LSERDY, RTC_LSE_START_MS);
    } else {
        lse_ok = 0;
    }

    if (lse_ok) {
        s_src = BSL_RTC_SRC_LSE;
        lse_hz = (cfg.lse_hz != 0U) ? cfg.lse_hz : 32768U;
    } else {
        /* 降级：LSI 照样走时，但精度差（17~47 kHz、随温度漂）必须报到 POST 行 */
        RCC->BDCR &= ~RCC_BDCR_LSEON;
        RCC->CSR |= RCC_CSR_LSION;
        if (!wait_flag(&RCC->CSR, RCC_CSR_LSIRDY, RTC_LSI_START_MS)) {
            return BSL_ERR_TIMEOUT; /* 两个源都起不来：RTC 不可用 */
        }
        s_src = BSL_RTC_SRC_LSI;
        lse_hz = 32768U; /* LSI 无标定值，按 32768 折算；偏差是已知代价 */
    }

    RCC->BDCR = (RCC->BDCR & ~RCC_BDCR_RTCSEL)
              | ((s_src == BSL_RTC_SRC_LSE) ? RCC_BDCR_RTCSEL_0 : RCC_BDCR_RTCSEL_1)
              | RCC_BDCR_RTCEN;

    rtc_unlock();
    RTC->ISR |= RTC_ISR_INIT;
    if (!wait_flag(&RTC->ISR, RTC_ISR_INITF, RTC_INIT_MS)) {
        rtc_lock();
        return BSL_ERR_TIMEOUT;
    }

    /* PREDIV_A=127 → 异步级输出 lse_hz/128；PREDIV_S 再除到 1 Hz。
     * 频率取自 DTB（换板可能是 32.000 kHz 而非 32.768 kHz），这里不再假设 32768。 */
    sync_div = (lse_hz / 128U) - 1U;
    if (sync_div < 1U || sync_div > 32767U) {
        rtc_lock();
        return BSL_ERR_INVAL;
    }
    RTC->PRER = (127U << 16) | sync_div;

    rtc_build_time(&t);
    RTC->TR = (bcd_pair(t.hour) << 16) | (bcd_pair(t.minute) << 8) | bcd_pair(t.second);
    /* DR：YT/YU 各 4 位 → 年两位；MT 是 1 位（bit12）、MU 4 位（11:8）；
     * DT 2 位（5:4）、DU 4 位（3:0）；WDU 3 位（15:13）。 */
    RTC->DR = ((uint32_t)((t.year % 100U) / 10U) << 20)
            | ((uint32_t)(t.year % 10U) << 16)
            | (1U << 13)
            | ((uint32_t)(t.month / 10U) << 12)
            | ((uint32_t)(t.month % 10U) << 8)
            | ((uint32_t)(t.day / 10U) << 4)
            | (uint32_t)(t.day % 10U);

    RTC->ISR &= ~RTC_ISR_INIT; /* 出初始化态即开始走时 */
    RTC->BKP0R = RTC_BKP_MAGIC;
    s_ready = 1;
    return BSL_OK;
}

bsl_rtc_src_t bsl_rtc_src(void)
{
    return s_src;
}

int bsl_rtc_ready(void)
{
    return s_ready;
}

bsl_err_t bsl_rtc_get(bsl_rtc_time_t *out)
{
    uint32_t tr;
    uint32_t dr;
    bsl_rtc_time_t v;

    if (out == NULL) {
        return BSL_ERR_INVAL;
    }
    if (!s_ready) {
        return BSL_ERR_NOINIT;
    }

    /* 清 RSF 再等它由硬件置起：这一拍保证 TR/DR 影子寄存器与日历对齐，
     * 否则跨秒读到的可能是"分钟已加、秒还是 59"的半新半旧值。 */
    RTC->ISR &= ~RTC_ISR_RSF;
    if (!wait_flag(&RTC->ISR, RTC_ISR_RSF, RTC_REG_SYNC_MS)) {
        return BSL_ERR_TIMEOUT;
    }
    tr = RTC->TR;
    dr = RTC->DR;

    v.hour = (uint8_t)(((tr >> 20) & 0x3U) * 10U + ((tr >> 16) & 0xFU));
    v.minute = (uint8_t)(((tr >> 12) & 0x7U) * 10U + ((tr >> 8) & 0xFU));
    v.second = (uint8_t)(((tr >> 4) & 0x7U) * 10U + (tr & 0xFU));
    v.year = (uint16_t)(2000U + ((dr >> 20) & 0xFU) * 10U + ((dr >> 16) & 0xFU));
    v.month = (uint8_t)(((dr >> 12) & 0x1U) * 10U + ((dr >> 8) & 0xFU));
    v.day = (uint8_t)(((dr >> 4) & 0x3U) * 10U + (dr & 0xFU));
    v.weekday = (uint8_t)((dr >> 13) & 0x7U);

    /* 非法字段一律拒读：把 13 月/32 日当数据交出去，只会让上层画出错日历 */
    if (v.month < 1U || v.month > 12U || v.day < 1U || v.day > 31U
        || v.hour > 23U || v.minute > 59U || v.second > 59U
        || v.weekday < 1U || v.weekday > 7U) {
        return BSL_ERR_INVAL;
    }
    *out = v;
    return BSL_OK;
}

bsl_err_t bsl_rtc_measure(uint32_t ms, bsl_rtc_time_t *first, int *delta_s)
{
    bsl_rtc_time_t a;
    bsl_rtc_time_t b;
    bsl_err_t err;

    err = bsl_rtc_get(&a);
    if (err != BSL_OK) {
        return err;
    }
    if (first != NULL) {
        *first = a;
    }
    HAL_Delay(ms);
    err = bsl_rtc_get(&b);
    if (err != BSL_OK) {
        return err;
    }
    if (delta_s != NULL) {
        int s1 = ((int)a.hour * 60 + (int)a.minute) * 60 + (int)a.second;
        int s2 = ((int)b.hour * 60 + (int)b.minute) * 60 + (int)b.second;

        *delta_s = (s2 - s1 + 86400) % 86400; /* 跨零点不为负 */
    }
    return BSL_OK;
}

/* ---- 设备注册（registry 的 bsl_driver_brd_rtc / bsl_api_brd_rtc） ---- */

/* 走时源与频率直方图这类"板级事实"在 DTB 里，寄存器级操作只在这里 —— 与
 * board_nor.c 同形态：文件尾给 api + driver 两个常量，registry 只引用符号。 */
const bsl_rtc_api_t bsl_api_brd_rtc = {
    .clock_source = bsl_rtc_src,
    .ready = bsl_rtc_ready,
    .get = bsl_rtc_get,
};

const struct bsl_device_driver bsl_driver_brd_rtc = {
    .init = bsl_rtc_init,
};
