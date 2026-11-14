#include <stdbool.h>
#include <stddef.h>

#include "osl_boot.h"
#include "system_stm32f4xx.h"

#include "FreeRTOS.h"
#include "task.h"
#include "stm32f4xx_hal.h"
#include "osl_panic.h"
#include "osl_panic_internal.h"
#include "osl_health.h"

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

/*
 * 启动阶段：0 = board_system_init() 全程成功；1 = HAL_Init() 失败；
 * 2 = SystemClock_Config() 失败（HSE 未起振 / PLL 未锁定）。
 *
 * 为什么不塞进 g_panic_record：panic_record_t 现在 140 B，加 4 B 顶到 144 B ——
 * 而 record-size 组的下限是 0x80，破坏时删四个上下文字段共 16 B 正好落在 128 B
 * 的下限上，「破坏后应当报红」立刻不成立；可选作破坏点的第 5 个字段也一个不剩
 * （17 个故障族字段全被 panic_clear_fault_scene 点名，assert / task / seq /
 * heap / stack 各有判据盯着）。所以另开一个 .noinit 变量：同段、同样跨复位、
 * 调试器同样直接可读，且不动 record 一个字节。
 *
 * 放 .noinit 而不是 .bss：码 4 触发 panic → 32.7 s 后 IWDG 复位 → 调试器连上来时
 * main 还没执行到重新赋值那一行，.bss 版本已被 Reset_Handler 清零，读到 0 会被
 * 误读成「启动正常」。跨复位这件事只有 .noinit 做得到。
 *
 * 写入点必须由 main 自己完成：board_system_init() 只返回、不 panic；而
 * fml_panic(4U) 标了 noreturn（内部关中断自旋），进了它就再没机会写。
 */
volatile uint32_t g_boot_stage __attribute__((section(".noinit")));

void osl_boot_stage_note(uint32_t stage)
{
    g_boot_stage = stage;
}

/* 产物断言按指令形态核对「第二次进来会早退」这条分支，所以禁止内联：
 * 发布构建（-Os）会把这个只调两次的函数折成直线，早退分支随之消失。 */
__attribute__((noinline)) void osl_reset_cause_capture(void)
{
    /*
     * 只真正采一次。
     *
     * HAL_InitTick 不是"启动早期只调一次"：HAL_Init() 调一次，
     * HAL_RCC_ClockConfig() 每次改时钟还要在末尾再调一次。这里第一次读完就置了
     * RMVF 把标志清掉，第二次进来读到的 RCC->CSR 是全零 —— 会把 g_reset_cause
     * 覆写成 OSL_RESET_UNKNOWN、g_reset_csr 覆写成 0。而 osl_panic_report()
     * 落盘进 g_panic_record 的正是这两个值，等于时钟切换之后所有 panic
     * （栈溢出 / assert / 硬件 fault / malloc 失败 / 未实现中断 / 堆耗尽）
     * 记录的复位原因都是假的。
     *
     * 守卫用函数内静态变量：它在 .bss，复位后归零，所以每次上电/每次看门狗复位
     * 后的第一次调用仍然会真采 —— "只采一次"的范围是一次启动，不是整个芯片寿命。
     */
    static uint8_t captured;
    uint32_t csr;

    if (captured != 0U) {
        return;
    }
    captured = 1U;

    csr = RCC->CSR;

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
/* 断言要求它在镜像里有独立符号与失败分支，禁止内联与跨调用合并。 */
__attribute__((noinline)) static bool iwdg_wait_sync(void)
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

/* tick hook 的产物断言要求看到 `bl osl_watchdog_feed`，所以禁止内联（-Os 会把它变成一条 str）。 */
__attribute__((noinline)) void osl_watchdog_feed(void)
{
    IWDG->KR = IWDG_KEY_RELOAD;
}

/*
 * panic 前把超时拉到最长，并且**真的把计数器打开**。
 *
 * 早期实现只写 PR / RLR / 0xAAAA。这对「运行期 panic」是对的：那时 tick hook 早已
 * 通过 osl_watchdog_start() 写过 0xCCCC。但启动期的 panic 根本不经过 tick hook ——
 * 码 1（vTaskStartScheduler() 返回）、码 2（启动期 xTaskCreate 失败）、码 4
 * （SystemClock_Config 失败）发生时 IWDG 从未 ENABLE，PR / RLR 只是预置寄存器，
 * 计数器不跑。播报结束进入 taskDISABLE_INTERRUPTS() + 自旋之后超时永远不会到，
 * 于是「32.7 s 后复位、留下第二代现场」这条设计承诺对这些码根本不成立 ——
 * 板上表现是永久停机、只能断电。
 *
 * 0xCCCC 是幂等的：已经启动再写不会重启计数器、也不改超时，所以运行期重复调用
 * 无副作用，代价只是报错路径上多一条 store。
 *
 * 禁止内联：产物断言要在本函数的指令流里看到 0xCCCC（`movw #52428`），
 * -Os 会把它折进 osl_panic_report，那条判据就找不到独立函数体。
 */
__attribute__((noinline)) void osl_watchdog_relax(void)
{
    IWDG->KR = IWDG_KEY_WRITE_ENABLE;
    (void)iwdg_wait_sync();
    IWDG->PR = IWDG_PR_DIV256;
    (void)iwdg_wait_sync();
    IWDG->RLR = IWDG_RLR_RELAXED;
    (void)iwdg_wait_sync();
    IWDG->KR = IWDG_KEY_RELOAD;
    IWDG->KR = IWDG_KEY_ENABLE;
}

/*
 * MSP 栈底守卫。
 *
 * 链接脚本里 `_Min_Stack_Size = 0x400` 只是"在 ._user_heap_stack 段里预留 1 KB"，
 * 不是保护：MSP 从 _estack(0x20030000) 向下长，越界后落进 __heap_end__(0x200043e8)
 * 到栈顶之间那约 175 KB 真空，不会撞到任何东西，也不会报错（configCHECK_FOR_STACK_OVERFLOW
 * 只管任务栈，管不到中断用的 MSP）。每个 tick（约 1 ms）比一次 MSP，跌破预留线按 7 号播报。
 */
extern char _estack[];
extern char _Min_Stack_Size[];

/*
 * osl_tick_ms_advance 定义在本节后面的 HAL 时基段（hal_time_base_init 之后）。
 * 这里必须先声明：vApplicationTickHook 定义在它前面并调用它，缺了声明就会退化成
 * C 的隐式声明，-Werror 直接把这一条编译错误抖出来。
 */
extern void osl_tick_ms_advance(void);

/* tick hook 的产物断言要求看到 `bl stack_guard_check`，禁止内联（-Os 会把它并进 hook）。 */
__attribute__((noinline)) static void stack_guard_check(void)
{
    if ((uintptr_t)__get_MSP() < (uintptr_t)_estack - (uintptr_t)_Min_Stack_Size) {
        /*
         * 必须走 osl_panic_stack_guard_report()，不能直接调 osl_panic_report(7)。
         *
         * report 的清理条件把码 7 归进「入口已把任务身份填好」那一类、因此不清
         * task / task_name —— 这个前提只对 vApplicationStackOverflowHook 成立
         * （FreeRTOS 把 xTask 与 pcTaskName 传给它）。MSP 越界发生在这里：中断上下文、
         * 主栈压穿，拿不到任何任务身份，直接调 report 就会读到 .noinit 里上一代的
         * 名字 —— 上一次 led_blink 栈溢出、这一次主栈压穿，现场记成「led_blink 又溢出」，
         * task 还是个可能已失效的 TCB 地址。
         */
        osl_panic_stack_guard_report();
    }
}

/*
 * tick hook：configUSE_TICK_HOOK = 1 时内核每个 tick 调它（中断上下文）。
 * 第一次进来才启动看门狗 —— 那意味着调度器已经开始跑、SysTick 正常。
 */
void vApplicationTickHook(void)
{
    static uint8_t watchdog_started;
    uint32_t heap_ever_min;

    /*
     * 先推进毫秒累加器：调度器起来之后 HAL_GetTick 完全依赖它。
     * 放在最前面 —— 后面的 stack_guard_check() 一旦 panic，这条 tick 就不再被
     * 记账，而那种情况下 HAL 时基本身已经没有意义。
     */
    osl_tick_ms_advance();
    stack_guard_check();
    /*
     * 堆历史最低（xPortGetMinimumEverFreeHeapSize）：这是 g_heap_free_min 的**唯一**
     * 采样写入口。tick 上 1 kHz 读一个单调不增的全局量，不依赖任何任务是否还活着；
     * 单写者，所以不存在两个采样点互相覆盖、把下界抬高一个 tick 的窗口。
     */
    heap_ever_min = (uint32_t)xPortGetMinimumEverFreeHeapSize();
    if (heap_ever_min < g_heap_free_min) {
        g_heap_free_min = heap_ever_min;
    }
    if (watchdog_started == 0U) {
        watchdog_started = 1U;
        osl_watchdog_start();
    }
    osl_watchdog_feed();
}

/*
 * 时基归属（唯一所有者为 HAL，FreeRTOS 只拥有自己的 tick）：
 *
 * 本工程把 SysTick 交给了 FreeRTOS port（见 FreeRTOSConfig.h 的
 * xPortSysTickHandler -> SysTick_Handler 别名），所以 HAL 的 uwTick 不会有人递增，
 * 而 HAL_GetTick 又长期被“只有调度器运行才返回 tick”的实现覆盖：
 * 调度器启动之前 HAL_Delay 里的 while((0-0) < N) 恒真，等于死等。
 *
 * 现在改用 DWT 内核周期计数器做 HAL 时基：
 *   - 不占用中断，不参与 SysTick 归属之争；
 *   - 复位后即可用，调度器启动前 HAL_Delay / 各驱动的超时判断都是真实的；
 *   - CYCCNT 是 32 位、按 HCLK 计数，回绕周期是 2^32 / HCLK：168 MHz 下约 25.6 s
 *     （以前这里按 16 MHz 写的 268 s，与实际主频不符）。所以 HAL_GetTick 的返回值
 *     每 25.6 s 回到 0，任何超过这一长度的延时/超时都会被截断；本工程没有
 *     >25 s 的 HAL 调用。差值仍按无符号算，跨越回绕点不会被算成负数。
 *
 * 注意：uwTick / uwTickPrio 不再被维护（HAL_GetTickPrio 会返回无效值 0xFF）。
 * 本工程不使用 HAL 的 tick 优先级语义，若将来要用，必须改为 SysTick + HAL_IncTick 方案。
 */


static void hal_time_base_init(void)
{
    /*
     * 只初始化一次，判据用硬件位 DWT_CTRL_CYCCNTENA。
     *
     * 为什么不能每次都做：HAL 的契约是"改时钟就重装时基"，所以
     * HAL_RCC_ClockConfig() 末尾会再调一次 HAL_InitTick。若这里无条件执行
     * DWT->CYCCNT = 0，毫秒原点会被推到时钟切换完成那一刻，HAL_GetTick()
     * 在启动期就不再单调 —— 任何跨过这两个点的超时起点都会算错（无符号相减
     * 会回绕成巨大值，表现为"立刻超时"）。
     *
     * 不归零的代价是标度不连续：切换前累计的周期按 16 MHz 折算、切换后按
     * 168 MHz 折算。本工程里这段只有 HAL_Init 到 SystemClock_Config 之间的
     * PWR 使能与 HSE 起振，最多几万周期，折算差约 0.2 ms。让 tick 单调比
     * 让标度绝对精确更重要。
     *
     * 用硬件位而不是静态变量：硬件位天然幂等，不占 .data/.bss，也扛得住
     * HAL_GetTick 兜底路径与中断上下文里的任何重复调用。
     */
    if ((DWT->CTRL & DWT_CTRL_CYCCNTENA_Msk) != 0U) {
        return;
    }
    /* TRCENA 打开 DWT 时钟；没有调试器连接时也必须置位，否则 CYCCNT 不走。 */
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0U;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
}

/*
 * 毫秒累加器：调度器起来之后，HAL 时基改由 SysTick 的 tick hook 推进。
 *
 * 为什么需要它 —— 两条都是改之前就存在的实测事实，不是理论风险：
 *
 *   1. **时钟切换处 tick 会倒退。** SystemClock_Config 把 HCLK 从 16 MHz 提到
 *      168 MHz，SystemCoreClock/1000 随之从 16000 变 168000，同一个 CYCCNT 读数
 *      折算出的毫秒数被压小 10.5 倍。切换前取到的 tickstart 在切换后"变小了"，
 *      无符号相减 (now - tickstart) 回绕成约 2^32，判据立刻成立 —— 表现是这条
 *      超时一开始就超时。VOSRDY 那段等待恰好跨在这个窗口里。
 *   2. **CYCCNT 每 25.6 s 回绕**（2^32 / 168 MHz），HAL_GetTick 跟着归零，
 *      任何长于 25.6 s 的超时都会被截断。
 *
 * 累加器每 1 ms 加一（configTICK_RATE_HZ = 1000），标度恒定、49.7 天才回绕。
 * 放 .bss 而不是 .noinit：复位后必须从 0 起算，带上上一轮的毫秒数会让所有超时错。
 */
volatile uint32_t g_tick_ms;

/* CYCCNT 折算成毫秒。启动期（调度器未起）与首拍对齐共用这一条。 */
uint32_t osl_hal_tick_ms_from_dwt(void)
{
    uint32_t cycles_per_ms = SystemCoreClock / 1000U;

    if (cycles_per_ms == 0U) {
        return 0U;
    }
    return DWT->CYCCNT / cycles_per_ms;
}

/*
 * tick hook 每拍调一次（SysTick 上下文）。
 *
 * 首拍必须先把累加器对齐到"调度器起来之前已经走过的毫秒数"：那段时间
 * HAL_GetTick 走的是 CYCCNT 折算，累加器若从 0 开始，tick 会在调度器启动那一刻
 * 倒退到 0，跨过这个点的超时起点全部算错。
 *
 * 判据用独立标志而不是 g_tick_ms == 0：49.7 天后累加器自己会回绕到 0，那时若
 * 再判 ==0 就会拿 CYCCNT（25.6 s 就回绕）去重填，造成更大的跳变。
 */
void osl_tick_ms_advance(void)
{
    static uint8_t synced;

    if (synced == 0U) {
        synced = 1U;
        g_tick_ms = osl_hal_tick_ms_from_dwt();
    }
    g_tick_ms += 1U;
}

HAL_StatusTypeDef HAL_InitTick(uint32_t TickPriority)
{
    (void)TickPriority;
    /* 最先做：把上一次的复位原因从 RCC->CSR 取走，晚一步就可能被新的复位覆盖。 */
    osl_reset_cause_capture();
    hal_time_base_init();
    osl_fault_monitor_init(); /* 打开 MemManage/BusFault/UsageFault，否则三类都升级成 HardFault */
    return HAL_OK;
}

uint32_t HAL_GetTick(void)
{
    /*
     * 调度器已起来：直接返回毫秒累加器。
     *
     * 这一支不做除法、不读 DWT —— 每次 HAL_GetTick 从「udiv + 三次内存读
     * （SystemCoreClock / DWT->CTRL / DWT->CYCCNT）」降到一次 ldr。更重要的是
     * 标度不再跟着 SystemCoreClock 变：时钟切换不会再让 tick 倒退，CYCCNT 的
     * 25.6 s 回绕也不再影响 HAL 的超时判据。
     */
    if (xTaskGetSchedulerState() == taskSCHEDULER_RUNNING) {
        return g_tick_ms;
    }

    /*
     * 启动期（调度器还没起、SysTick 还没跳）：只能靠 CYCCNT。
     * 这段最长到调度器启动，量级是几十到几百毫秒，标度突变带来的误差只在这个
     * 窗口内存在，且远小于 HAL 启动期的超时量级（HSE 起振、VOSRDY 等待都是
     * 微秒到毫秒级）。
     */
    if ((DWT->CTRL & DWT_CTRL_CYCCNTENA_Msk) == 0U) {
        /* 兜底：绕过 HAL_Init() 的启动路径不会走到 HAL_InitTick。
         * 当前 main() 是正常先调 HAL_Init() 的，这条只为不让更早的调用点拿到 0。 */
        hal_time_base_init();
        osl_fault_monitor_init();
    }
    return osl_hal_tick_ms_from_dwt();
}

/*
 * newlib 的堆：只有 _Min_Heap_Size（512 字节），边界由链接脚本显式定义。
 *
 * 在此之前的两条失败路径都是"不响"的：`--specs=nosys.specs` 自带一个 `_sbrk` 桩，
 * 任何 malloc 都拿不到内存，调用方不检查返回值就会在别处零星崩；
 * 而 ._user_heap_stack 里预留的那 512 字节也永远没人用。
 *
 * 现在：__heap_start__ / __heap_end__ 由 stm32f429igx_flash.ld 在
 * ._user_heap_stack 段内输出（两者之差正好 _Min_Heap_Size），越界立刻
 * panic(5)（三灯按二进制播报 5），让"谁在大块 malloc"成为现场可见的事实。
 *
 * 规则：这个堆只配给 newlib 内部的临时对象（当前镜像里没有任何 printf 系列符号，
 * 实际使用者只有偶尔的 malloc）；任何跨函数、长期持有的分配一律走
 * FreeRTOS 的 pvPortMalloc。
 */

extern char __heap_start__[];
extern char __heap_end__[];

static char *g_newlib_brk = __heap_start__;

void *_sbrk(ptrdiff_t incr)
{
    char *previous = g_newlib_brk;
    uintptr_t start = (uintptr_t)__heap_start__;
    uintptr_t end = (uintptr_t)__heap_end__;
    uintptr_t cur = (uintptr_t)previous;
    uintptr_t next;

    /*
     * 全用无符号算：以前写的是 `(intptr_t)previous + (intptr_t)incr`，
     * 本机 intptr_t 是 32 位有符号（__PTRDIFF_MAX__ = 0x7fffffff，指针 4 B），
     * 一次超大 incr 就是有符号溢出（UB，编译器可以假设它不发生），溢出回绕后
     * 结果可能重新落回 [start, end]，越界检查就被绕过去了。
     * 这里先比较"要多少 / 剩多少"，再动指针，任何分支都不依赖回绕值。
     */
    if (incr >= 0) {
        if ((uintptr_t)incr > (end - cur)) {
            goto exhausted;
        }
        next = cur + (uintptr_t)incr;
    } else {
        /* 0 - (uintptr_t)incr 得到 |incr|，避免对 PTRDIFF_MIN 取负的溢出。 */
        uintptr_t back = (uintptr_t)0 - (uintptr_t)incr;
        if (back > (cur - start)) {
            goto exhausted;
        }
        next = cur - back;
    }

    /* malloc 返回的地址要按 8 字节对齐（Cortex-M4 上的 double / uint64_t）。 */
    next = (next + 7U) & ~(uintptr_t)7U;
    if (next > end) {
        goto exhausted;
    }

    g_newlib_brk = (char *)next;
    return (void *)previous;

exhausted:
    /* 只在越界出口关中断：放到函数开头会让每次 malloc 都屏蔽全部中断。 */
    panic_mask_all_irqs();
    g_panic_record.code = (uint32_t)OSL_PANIC_HEAP_EXHAUSTED;
    osl_panic_report(OSL_PANIC_HEAP_EXHAUSTED);
}

/*
 * newlib 的 malloc 锁。
 *
 * libc_nano.a 的 mlock.o 把它实现成“空锁”：`__malloc_lock` 只是转调
 * `__retarget_lock_acquire_recursive`，反汇编里就是一条 b.w。调度器跑起来之后，
 * 两个任务同时 malloc 就会一起改 sbrk 链和空闲链表，导致堆被踩坏。
 *
 * 这里覆盖成 FreeRTOS 临界区：临界区天然可嵌套（uxCriticalNesting 计数），
 * 符合 newlib 要求“可递归”的语义。
 *
 * 但它**只有在调度器启动之后**才保证退出时不会把中断留在关闭状态。port_m4.c 里
 * `uxCriticalNesting` 的初值是 0xaaaaaaaa（port_m4.c:161 的哨兵），只在
 * `xPortStartScheduler()` 里被置 0（port_m4.c:443）。调度器启动前调进来：ENTER 把它
 * 变成 0xaaaaaaab，EXIT 退回 0xaaaaaaaa —— `if (uxCriticalNesting == 0)
 * portENABLE_INTERRUPTS()` 永不成立，中断被**静默**保持关闭，全程没有一个断言会响。
 * 所以 newlib 的分配/格式化调用必须等到 `vTaskStartScheduler()` 之后；
 * `newlib-preboot` 判据守的就是这一条（main() 里调度器之前、
 * board_system_init() 与 osl_health_init() 的整函数体都不得出现这类调用）。
 * 代价是 malloc 期间会屏蔽 ≤ configMAX_SYSCALL_INTERRUPT_PRIORITY 的中断；
 * newlib 堆只是 ≤512 B 的首次适配，耗时在微秒级，可以接受。
 *
 * 注意：它不保护 ISR（更高优先级的中断仍然能进来）。ISR 里本来就不允许 malloc。
 */
struct _reent;

void __malloc_lock(struct _reent *ptr)
{
    (void)ptr;
    taskENTER_CRITICAL();
}

void __malloc_unlock(struct _reent *ptr)
{
    (void)ptr;
    taskEXIT_CRITICAL();
}

/*
 * newlib 的 _write 最小实现：没有串口，就把最近一段输出留在内存里给调试器看。
 * 返回 len 让 printf 认为写成功，避免返回错误码导致重试或异常路径。
 *
 * 注意：本函数没有调用方，--gc-sections 会把它和 g_last_write 一起丢掉
 * （实测 nm 里两者都不存在，等于"留最近输出"是空承诺）。所以 toolchain.make
 * 的 LDFLAGS 里加了 -Wl,-u,_write 强制保留，verify 也会断言镜像里有该符号。
 */
int _write(int file, char *ptr, int len)
{
    int i;

    (void)file;
    for (i = 0; i < len; i++) {
        g_last_write[i] = ptr[i];
        if (i + 1 >= (int)(sizeof(g_last_write) / sizeof(g_last_write[0]))) {
            break;
        }
    }
    if (i < (int)(sizeof(g_last_write) / sizeof(g_last_write[0]))) {
        g_last_write[i] = '\0';
    }
    return len;
}
