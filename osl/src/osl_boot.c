#include <stdbool.h>

#include "osl_boot.h"

#include "FreeRTOS.h"
#include "task.h"
#include "stm32f4xx_hal.h"
#include "osl_panic.h"

volatile osl_reset_cause_t g_reset_cause = OSL_RESET_UNKNOWN;
volatile uint32_t g_reset_csr;
/*
 * IWDG 的 SR 同步等待失败次数（PR / RLR 没被确认写进 LSI 域）。
 *
 * 单独成全局而不是直接 panic：panic 路径自己会调 osl_watchdog_relax()，
 * 而 relax() 内部也要等 SR —— 在那里 panic 会递归。所以失败只累加计数，
 * 由 osl_panic_report() 抄进 g_panic_record.wd_sync_fail，调试器读 record
 * 就知道“这次现场的 32.7 s 到底有没有真的拉起来”。
 */
volatile uint32_t g_iwdg_sync_fail;

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

/*
 * PR / RLR 是跨到 LSI 域同步的寄存器：写下去之后 SR 的 PVU / RVU 置 1，
 * 状态干净前写重载键（0xAAAA）可能仍按旧值重载。
 *
 * 这不是理论问题：以前 start() 是 5 条连续 store（反汇编里
 * `str 0x5555 → str PR → str RLR → str 0xAAAA → str 0xCCCC`，一次 SR 都没读），
 * 于是 RLR 可能没生效就重载 —— 超时不是 1.0 s 而是复位默认（/4 + 4095 ≈ 0.5 s），
 * relax 也可能拉不上去，panic 播报会在 1 s 内被复位打断。
 *
 * 等待带计数上界：LSI 万一没起振，SR 永远不会清，这里不能变成又一个死等。
 */
#define IWDG_SR_SYNC_MASK (IWDG_SR_PVU | IWDG_SR_RVU)
#define IWDG_SYNC_GUARD 0xFFFFUL

/*
 * 返回 true：SR 的 PVU / RVU 已确认清零，这一笔 PR / RLR 真的进了 LSI 域。
 * 返回 false：0xFFFF 次轮询都没等到（LSI 没起振或 SR 卡住），此时 PR / RLR
 * 很可能没生效，看门狗按复位默认（/4 + 4095 ≈ 0.5 s）跑。
 *
 * 失败不 panic：panic 路径会先调 osl_watchdog_relax()，而 relax() 内部也要等 SR，
 * 在那里 panic 就是递归。所以只累加 g_iwdg_sync_fail，由 osl_panic_report()
 * 抄进 g_panic_record.wd_sync_fail —— 现场能看出“32.7 s 到底有没有拉起来”。
 */
static bool iwdg_wait_sync(void)
{
    uint32_t guard = IWDG_SYNC_GUARD;

    while (((IWDG->SR & IWDG_SR_SYNC_MASK) != 0U) && (guard-- != 0U)) {
    }

    /* “等”与“等到了没有”必须分开判：只写 while 时两者共用同一个出口，
     * 失败完全不可见（反汇编里就是一条 bx lr），后果与修复前一模一样。 */
    if ((IWDG->SR & IWDG_SR_SYNC_MASK) != 0U) {
        g_iwdg_sync_fail++;
        return false;
    }
    return true;
}

void osl_watchdog_start(void)
{
    IWDG->KR = IWDG_KEY_WRITE_ENABLE;
    (void)iwdg_wait_sync();
    IWDG->PR = IWDG_PR_DIV16;
    (void)iwdg_wait_sync();
    IWDG->RLR = IWDG_RLR_NORMAL;
    (void)iwdg_wait_sync();
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
    (void)iwdg_wait_sync();
    IWDG->PR = IWDG_PR_DIV256;
    (void)iwdg_wait_sync();
    IWDG->RLR = IWDG_RLR_RELAXED;
    (void)iwdg_wait_sync();
    IWDG->KR = IWDG_KEY_RELOAD;
}

/*
 * MSP 栈底守卫。
 *
 * 链接脚本里 `_Min_Stack_Size = 0x400` 只是"在 ._user_heap_stack 段里预留 1 KB"，
 * 不是保护：MSP 从 _estack(0x20030000) 向下长，越界后落进 __heap_end__(0x20008460)
 * 到栈顶之间那约 159 KB 真空，不会撞到任何东西，也不会报错（configCHECK_FOR_STACK_OVERFLOW
 * 只管任务栈，管不到中断用的 MSP）。这里每个 tick 比一次 MSP，跌破预留线立刻按 7 号播报。
 */
extern char _estack[];
extern char _Min_Stack_Size[];

static void stack_guard_check(void)
{
    if ((uintptr_t)__get_MSP() < (uintptr_t)_estack - (uintptr_t)_Min_Stack_Size) {
        osl_panic_report(OSL_PANIC_STACK_OVERFLOW);
    }
}

/*
 * tick hook：configUSE_TICK_HOOK = 1 时内核每个 tick 调它（中断上下文）。
 * 第一次进来才启动看门狗 —— 那意味着调度器已经开始跑、SysTick 正常。
 */
void vApplicationTickHook(void)
{
    static uint8_t watchdog_started;

    stack_guard_check();
    if (watchdog_started == 0U) {
        watchdog_started = 1U;
        osl_watchdog_start();
    }
    osl_watchdog_feed();
}
