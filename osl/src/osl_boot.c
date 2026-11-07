#include "osl_boot.h"

#include "FreeRTOS.h"
#include "task.h"
#include "stm32f4xx_hal.h"

volatile osl_reset_cause_t g_reset_cause = OSL_RESET_UNKNOWN;
volatile uint32_t g_reset_csr;

void osl_reset_cause_capture(void)
{
    uint32_t csr = RCC->CSR;

    g_reset_csr = csr;
    if ((csr & RCC_CSR_PORRSTF) != 0U) {
        g_reset_cause = OSL_RESET_POWER_ON;
    } else if ((csr & RCC_CSR_BORRSTF) != 0U) {
        g_reset_cause = OSL_RESET_BOR;
    } else if ((csr & RCC_CSR_PINRSTF) != 0U) {
        g_reset_cause = OSL_RESET_PIN;
    } else if ((csr & RCC_CSR_SFTRSTF) != 0U) {
        g_reset_cause = OSL_RESET_SOFTWARE;
    } else if ((csr & RCC_CSR_IWDGRSTF) != 0U) {
        g_reset_cause = OSL_RESET_IWDG;
    } else if ((csr & RCC_CSR_WWDGRSTF) != 0U) {
        g_reset_cause = OSL_RESET_WWDG;
    } else if ((csr & RCC_CSR_LPWRRSTF) != 0U) {
        g_reset_cause = OSL_RESET_LOW_POWER;
    } else {
        g_reset_cause = OSL_RESET_UNKNOWN;
    }

    /* 清标志，否则下一次复位原因会和这次堆在一起，读不出新增的那一位。 */
    RCC->CSR |= RCC_CSR_RMVF;
}

/* IWDG 的键值：0x5555 允许写 PR/RLR，0xAAAA 重载，0xCCCC 启动（不可撤销）。 */
#define IWDG_KEY_WRITE_ENABLE 0x5555UL
#define IWDG_KEY_RELOAD 0xAAAAUL
#define IWDG_KEY_ENABLE 0xCCCCUL
/* PR 寄存器裸值：0=/4、1=/8、2=/16、3=/32、4=/64、5=/128、6=/256 */
#define IWDG_PR_DIV16 2UL
#define IWDG_PR_DIV256 6UL
#define IWDG_RLR_NORMAL 2000UL  /* /16 @ LSI 32 kHz → 1.0 s */
#define IWDG_RLR_RELAXED 4095UL /* /256 @ LSI 32 kHz → 32.7 s */

void osl_watchdog_start(void)
{
    IWDG->KR = IWDG_KEY_WRITE_ENABLE;
    IWDG->PR = IWDG_PR_DIV16;
    IWDG->RLR = IWDG_RLR_NORMAL;
    IWDG->KR = IWDG_KEY_RELOAD;
    IWDG->KR = IWDG_KEY_ENABLE;
}

void osl_watchdog_feed(void)
{
    IWDG->KR = IWDG_KEY_RELOAD;
}

void osl_watchdog_relax(void)
{
    IWDG->KR = IWDG_KEY_WRITE_ENABLE;
    IWDG->PR = IWDG_PR_DIV256;
    IWDG->RLR = IWDG_RLR_RELAXED;
    IWDG->KR = IWDG_KEY_RELOAD;
}

/*
 * tick hook：configUSE_TICK_HOOK = 1 时内核每个 tick 调它（中断上下文）。
 * 第一次进来才启动看门狗 —— 那意味着调度器已经开始跑、SysTick 正常。
 */
void vApplicationTickHook(void)
{
    static uint8_t watchdog_started;

    if (watchdog_started == 0U) {
        watchdog_started = 1U;
        osl_watchdog_start();
    }
    osl_watchdog_feed();
}
