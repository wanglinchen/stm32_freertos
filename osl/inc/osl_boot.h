#ifndef OSL_BOOT_H
#define OSL_BOOT_H

#include <stdint.h>

/*
 * 启动期的两个事实来源：复位原因与独立看门狗。
 *
 * 为什么放在 osl：两者都只依赖 RCC / IWDG 寄存器，不需要 HAL 驱动，也不该让
 * 应用层自己写寄存器；而调用点（HAL_InitTick、tick hook、panic 播报）都在 osl 里。
 */

/* 复位原因：一次复位可能多个标志同时置位，按“越底层越优先”的顺序判定。 */
typedef enum {
    OSL_RESET_UNKNOWN = 0,
    OSL_RESET_LOW_POWER = 1, /* LPWRRSTF：低功耗模式下复位 */
    OSL_RESET_WWDG = 2,      /* WWDGRSTF：窗口看门狗（本工程未启用） */
    OSL_RESET_IWDG = 3,      /* IWDGRSTF：独立看门狗，说明有人卡死过 */
    OSL_RESET_SOFTWARE = 4,  /* SFTRSTF：NVIC_SystemReset / 软件复位 */
    OSL_RESET_PIN = 5,       /* PINRSTF：NRST 引脚（复位键） */
    OSL_RESET_BOR = 6,       /* BORRSTF：欠压复位 */
    OSL_RESET_POWER_ON = 7,  /* PORRSTF：上电 */
} osl_reset_cause_t;

extern volatile osl_reset_cause_t g_reset_cause;
extern volatile uint32_t g_reset_csr; /* RCC->CSR 原始值，对照手册用 */

/*
 * 由 HAL_InitTick() 早期调用一次：落盘 RCC->CSR 并清标志（RMVF）。
 * 必须早于任何可能再次复位的操作，否则“上一次为什么重启”就丢了。
 * 注意它只清标志，不影响 g_reset_cause / g_reset_csr。
 */
void osl_reset_cause_capture(void);

/*
 * 独立看门狗（IWDG，LSI 驱动）。
 *
 * 一旦 0xCCCC 写下去就无法关闭，只能复位或改超时，所以启动时机必须谨慎：
 *   - 启动点 = vApplicationTickHook 第一次被调用（调度器真的跑起来、tick 中断正常），
 *     而不是 main 里无条件启动。调度器起不来时（FreeRTOS 堆不足那类）不启动看门狗，
 *     否则 1 s 后复位会把三灯 panic 播报循环掉，现场永远看不到错误码。
 *   - 喂狗也在 tick hook（1 kHz）。tick 停了就没人喂，1 s 后复位——
 *     这比“任务里喂狗”严格：任务停摆但 tick 正常时，任务喂狗方案会掩盖问题。
 *   - 超时按 LSI 典型 32 kHz 算：PR=/16（2 kHz）+ RLR=2000 → 1.0 s。
 *     LSI 实测范围较宽（17~47 kHz），最坏情况下窗口约 0.68 s ~ 1.88 s。
 */
void osl_watchdog_start(void);
void osl_watchdog_feed(void);

/*
 * panic 前调用：把超时拉到最长（PR=/256 + RLR=4095，约 32 s）并喂一次，
 * 让三灯播报与“全亮停机”能稳定保留到调试器连上。
 * 未启动看门狗时这些写是空操作，所以早期 panic（HAL_Init 之前）也能安全调用。
 */
void osl_watchdog_relax(void);

#endif
