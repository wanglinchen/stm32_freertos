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
extern volatile uint32_t g_iwdg_sync_fail; /* IWDG SR 同步等待失败次数（PR/RLR 未被确认生效） */

/*
 * 启动阶段，跨复位保留（.noinit）：
 *   0 = board_system_init() 全程成功；
 *   1 = HAL_Init() 失败（NVIC 优先级分组 / HAL 时基没建立）；
 *   2 = SystemClock_Config() 失败（HSE 25 MHz 未起振 / PLL 未锁定）。
 *
 * 为什么需要它：码 4 只能表达「启动失败」，而这个码下面有两个失败点，排查方向
 * 互斥（一个查分组配置、一个查晶振与 PLL）。复位后调试器直接读本变量即可分辨。
 *
 * 为什么不在 record 里：panic_record_t 的尺寸下限判据（0x80）只剩 4 B 余量，
 * 加字段会让「删四个字段」的破坏点正好落在下限上，判据当场失效；而可选作破坏点
 * 的字段已被其它判据占满。所以另开一个同段变量，不动 record 一个字节。
 *
 * 由 main() 在 fml_panic(4U) 之前写入，成功路径也写 0 —— 它标了 noreturn。
 */
extern volatile uint32_t g_boot_stage;
void osl_boot_stage_note(uint32_t stage);

/*
 * 由 HAL_InitTick() 调用：落盘 RCC->CSR 并清标志（RMVF）。
 * 函数内部保证每次启动只真正采一次 —— HAL_Init() 调一次 HAL_InitTick，
 * HAL_RCC_ClockConfig() 每次改时钟还会再调一次，而第一次已经把 RMVF 置了，
 * 第二次读到的 RCC->CSR 是全零。没有这个守卫的话，g_reset_cause 会被覆写成
 * OSL_RESET_UNKNOWN、g_reset_csr 变成 0。
 * 必须早于任何可能再次复位的操作，否则“上一次为什么重启”就丢了。
 * 注意它只清标志，不影响 g_reset_cause / g_reset_csr。
 */
void osl_reset_cause_capture(void);

/* 打开 MemManage/BusFault/UsageFault 独立入口；定义在 osl/src/osl_panic.c */
void osl_fault_monitor_init(void);

/*
 * 独立看门狗（IWDG，LSI 驱动）。
 *
 * 一旦 0xCCCC 写下去就无法关闭，只能复位或改超时，所以启动时机必须谨慎：
 *   - 正常运行期**不启动默认 1 s 档**：启动点 = vApplicationTickHook 第一次被调用
 *     （调度器真的跑起来、tick 中断正常），而不是 main 里无条件启动。理由是
 *     「调度器起不来」那类故障要先把三灯播报完，1 s 档会在播报中途复位。
 *   - **panic 时才以 32.7 s 档启动**：osl_watchdog_relax() 配好 PR/RLR 之后自己写
 *     0xCCCC。启动期 panic（码 1 调度器起不来 / 码 2 启动期建任务失败 / 码 4
 *     时钟失败）从不经过 tick hook，若 relax 不写启动键，计数器根本不跑，
 *     播报完的关中断自旋会一直转下去 —— 板上表现就是永久停机、只能断电。
 *     0xCCCC 幂等：运行期重复写不重启计数器、也不改超时。
 *   - 喂狗也在 tick hook（1 kHz）。tick 停了就没人喂，1 s 后复位——
 *     这比“任务里喂狗”严格：任务停摆但 tick 正常时，任务喂狗方案会掩盖问题。
 *   - 超时按 LSI 典型 32 kHz 算：PR=/16（2 kHz）+ RLR=2000 → 1.0 s。
 *     LSI 实测范围较宽（17~47 kHz），最坏情况下窗口约 0.68 s ~ 1.88 s。
 */
void osl_watchdog_start(void);
void osl_watchdog_feed(void);

/*
 * panic 前调用：配到最长超时（PR=/256 + RLR=4095，约 32.7 s）并**启动计数器**，
 * 让三灯播报（168 MHz 下约 1.2–1.5 s）能完整播完。此后没人喂狗，32.7 s 时 IWDG
 * 复位 —— 它争取的是「播完并留下可跨复位对比的第二代现场」，不是「永久停住」；
 * 真正跨复位留下来的是 .noinit 里的 g_panic_record（见 osl/src/osl_boot.c
 * 与链接脚本 .noinit 段）。
 *
 * 启动期 panic 也安全，且正是靠最后那句 0xCCCC 才生效：只预置 PR/RLR 不启动的话，
 * 调度器起不来时计数器从未跑过，播报结束就是永久自旋。
 *
 * g_reset_cause 的消费方在 osl_panic_report()：每次 panic 都把当前复位原因写进
 * g_panic_record.reset_cause，调试器读 record 即可知道“这次 panic 之前是怎么重启的”。
 *
 * 注意本函数不保证成功：PR / RLR 写下去后要等 SR 的 PVU / RVU 清零才算进 LSI 域，
 * 等不到时只累加 g_iwdg_sync_fail（不 panic，否则 panic→relax→再等失败会递归）。
 * 所以“现场活了 32.7 s”这件事要读 g_panic_record.wd_sync_fail 才能确认：0 才是真的拉起来了。
 */
void osl_watchdog_relax(void);

/* 调度器已启动时 HAL_GetTick 的运行期毫秒累加器；定义在 osl/src/osl_boot.c */
extern volatile uint32_t g_tick_ms;

#endif
