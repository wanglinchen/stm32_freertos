/*
 * RTC（片内实时时钟，BKP 域）：走时源选择 + 日历读写 + 走时观测。
 *
 * 为什么单独一个模块而不是塞进 board_post：RTC 不是"一次性探针"，它的值是
 * 应用要长期消费的（`app_calendar` 现在还是编译时刻，将来换成墙钟就得走这里）。
 * 一次性探针与可复用驱动的区别就在于"有没有第二个消费者"—— 后者有。
 *
 * 走时源：LSE 是板上 32.768 kHz 晶振（精度 ±20 ppm 级）；LSI 是片内 RC
 * （F429 手册档位 17~47 kHz，且随温度漂）。LSE 不起振时**降级到 LSI 并照样
 * 走时**，但必须把实际生效的源暴露出来（`bsl_rtc_src()`）写进 POST 行 ——
 * 降级不是错误，静默降级才是。
 */
#ifndef BSL_RTC_H
#define BSL_RTC_H

#include <stdint.h>

#include "bsl_err.h"

typedef enum {
    BSL_RTC_SRC_LSE = 0,
    BSL_RTC_SRC_LSI = 1
} bsl_rtc_src_t;

/* 日历时间。年给 2000..2099：RTC 的 YT/YU 只有两位十进制，"世纪"是软件约定
 * 而不是硬件事实，所以这里不猜 19xx / 21xx。 */
typedef struct {
    uint16_t year;
    uint8_t month;   /* 1..12 */
    uint8_t day;     /* 1..31 */
    uint8_t weekday; /* 1=周一 .. 7=周日，由硬件 WDU 给出 */
    uint8_t hour;
    uint8_t minute;
    uint8_t second;
} bsl_rtc_time_t;

/* 上电初始化（幂等）。BKP0R 里有本模块的魔数且 RTCEN 已开时视为"已在走"，
 * 只做同步、不重设时间 —— 有电池时重设会把已走的日历抹掉。
 * 首次初始化把日期种成**编译时刻**（本机没有墙钟来源，这是唯一诚实的起点）。
 * LSE 起振超时（约 3 s）后降级 LSI；LSI 也不起振才返回 BSL_ERR_TIMEOUT。 */
bsl_err_t bsl_rtc_init(void);

/* 实际生效的走时源。未初始化时返回 BSL_RTC_SRC_LSE（"应然"值，不代表已生效）。 */
bsl_rtc_src_t bsl_rtc_src(void);

/* 初始化是否成功过。0 = 没成功（POST 判 FAIL，应用不该读）。 */
int bsl_rtc_ready(void);

/* 读日历。0 成功。失败原因：未初始化 / 寄存器同步超时 / BCD 字段非法
 * （非法值直接拒读，不猜"大概是 13 月"）。 */
bsl_err_t bsl_rtc_get(bsl_rtc_time_t *out);

/* 观测走时：读一次 → 等 ms 毫秒 → 再读一次，*delta_s = 两次的秒差
 * （0..3 视为正常，本函数不做"是否算走时"的判断，只报数）。
 * `first` 可为 NULL。用这个而不是让 POST 自己 sleep：等待方式与时基口径
 * 都归本模块，POST 只解读结果。 */
bsl_err_t bsl_rtc_measure(uint32_t ms, bsl_rtc_time_t *first, int *delta_s);

#endif /* BSL_RTC_H */
