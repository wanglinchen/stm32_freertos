#include <stddef.h>

#include "FreeRTOS.h"
#include "task.h"
#include "stm32f4xx_hal.h"
#include "system_stm32f4xx.h"
#include "osl_panic.h"
#include "osl_boot.h"
#include "osl_health.h"

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

static void fault_monitor_init(void);

static void hal_time_base_init(void)
{
    /* TRCENA 打开 DWT 时钟；没有调试器连接时也必须置位，否则 CYCCNT 不走。 */
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0U;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
}

HAL_StatusTypeDef HAL_InitTick(uint32_t TickPriority)
{
    (void)TickPriority;
    /* 最先做：把上一次的复位原因从 RCC->CSR 取走，晚一步就可能被新的复位覆盖。 */
    osl_reset_cause_capture();
    hal_time_base_init();
    fault_monitor_init(); /* 打开 MemManage/BusFault/UsageFault，否则三类都升级成 HardFault */
    return HAL_OK;
}

uint32_t HAL_GetTick(void)
{
    uint32_t cycles_per_ms = SystemCoreClock / 1000U;

    if ((DWT->CTRL & DWT_CTRL_CYCCNTENA_Msk) == 0U) {
        /* 有些启动路径（当前 main 未调 HAL_Init）不会走到 HAL_InitTick，这里兜底。 */
        hal_time_base_init();
        fault_monitor_init();
    }
    if (cycles_per_ms == 0U) {
        return 0U;
    }
    return DWT->CYCCNT / cycles_per_ms;
}

/* ------------------------------------------------------------------------- */
/* 故障现场：钩子必须留下证据，否则板子只会“安静地不动”                      */
/* ------------------------------------------------------------------------- */
/* 码值定义在 osl/inc/osl_panic.h，与 fml 共用一份；这里不再各自定义。 */

typedef struct {
    uint32_t magic;
    uint32_t code;
    TaskHandle_t task;
    char task_name[configMAX_TASK_NAME_LEN];
    const char *assert_file;
    uint32_t assert_line;
    /* 硬件异常现场（vHardwareFault 一类入口写，调试器直接读）。 */
    uint32_t fault_kind;      /* 1 HardFault / 2 MemManage / 3 BusFault / 4 UsageFault */
    uint32_t fault_cfsr;
    uint32_t fault_hfsr;
    uint32_t fault_mmfar;
    uint32_t fault_bfar;
    uint32_t fault_exc_return;
    uint32_t fault_frame_type; /* 1 基本帧（8 字）/ 2 扩展帧（26 字，用了 FPU） */
    uint32_t unhandled_irq;    /* Default_Handler 收到未实现的中断时，记录 ICSR.VECTACTIVE */
    uint32_t fault_sp;
    uint32_t fault_r0;
    uint32_t fault_r1;
    uint32_t fault_r2;
    uint32_t fault_r3;
    uint32_t fault_r12;
    uint32_t fault_lr;
    uint32_t fault_pc;
    uint32_t fault_xpsr;
    /* 以下两项不是硬件现场，是"跨复位"用的：record 本身在 .noinit 里不清零。 */
    uint32_t seq;         /* 第几次 panic：同一故障反复重启时能看出累计次数 */
    uint32_t reset_cause; /* 进 panic 之前那一次复位的原因（g_reset_cause 的唯一消费点） */
    /* 以下三项补的是“context”：这次 panic 从哪儿来、原始标志是什么、看门狗到底改成了没有。 */
    uint32_t ipsr;         /* 0=任务上下文，>0=异常号：assert/栈溢出发生在中断里也能分清 */
    uint32_t reset_csr;    /* RCC->CSR 原始值：reset_cause 是由它推出来的枚举，多标志同置时以此回溯 */
    uint32_t wd_sync_fail; /* 进 panic 前的 IWDG SR 等待失败次数：0 才说明 32.7 s 真的拉起来了 */
    uint32_t heap_free_min; /* panic 时 FreeRTOS 堆的历史最低剩余（字节）：码 2 的根因读数 */
} panic_record_t;

#define PANIC_MAGIC 0x50414E43UL /* "PANC" */

/*
 * 放 .noinit 而不是 .bss：panic 记录是复位后唯一还剩的证据。
 * Reset_Handler 只清 _sbss.._ebss（链接脚本的 .noinit 段在 .bss 之外、NOLOAD），
 * 所以 IWDG 复位后重启时，上一次的 code / task_name / fault_pc 仍在 RAM 里。
 * 首次上电内容随机，因此一律以 magic 作为"这里真的有记录"的判据。
 */
volatile panic_record_t g_panic_record __attribute__((section(".noinit")));
volatile char g_last_write[64]; /* _write 的最近一段输出，调试器可直接读 */

/*
 * panic 通道的重入闸。放 .bss 而不是 .noinit：.noinit 跨复位保留，第二次真 panic
 * 会被当成"已在播报"直接停住，记录永远写不进去；.bss 由 Reset_Handler 清零，
 * 正好只挡"同一次运行内"的重入。
 */
static volatile uint32_t g_panic_reporting;

/*
 * 关掉全部外部中断。写 record 与三灯播报（约 0.5 s）期间，0..4 优先级的中断不受
 * BASEPRI 约束，而 taskDISABLE_INTERRUPTS 只把 BASEPRI 提到 0x50、且是在播报之后
 * 才调——这段时间里第二个未实现外设一旦触发，就会重入 UnhandledIRQ_Handler 再走
 * 一遍 osl_panic_report，把同一份 record 交错写坏。
 *
 * NVIC 只有 ICER[0..7]（最多 240 个 IRQ），F429 的 90 个全在范围内，不会越界。
 * 16 号以下是系统异常（HardFault / SVC / PendSV / SysTick），不在 ICER 里，
 * 它们由 panic 记录里的 IPSR 反映，这里不处理。ICPR 一起清：只关使能会留下挂起位。
 */
static void panic_mask_all_irqs(void)
{
    uint32_t index;

    for (index = 0U; index < 8U; index++) {
        NVIC->ICER[index] = 0xFFFFFFFFU;
        NVIC->ICPR[index] = 0xFFFFFFFFU;
    }
}

/*
 * 故障指示灯：PH10/PH11/PH12（共阳、低电平点亮），与 bsl/src/board_led.c 的接法一致。
 * 这里故意绕过 HAL_GPIO / 板级驱动：栈已经溢出时不能再压栈调用多层函数，
 * 只写 BSRR 寄存器，栈开销为 0。
 */
#define PANIC_LED_MASK 0x1C00U /* PH10 | PH11 | PH12 */
/* 每脚在 MODER/OSPEEDR/PUPDR 里占 2 位：PH10→bit20、PH11→bit22、PH12→bit24 */
#define PANIC_LED_MODE_MASK 0x03F00000UL
#define PANIC_LED_MODE_OUT 0x01500000UL /* 每脚 01 = 通用输出 */
#define PANIC_SPIN 2000000U    /* 未校时的粗延时：只求肉眼可辨，主频越高闪得越快 */

static void panic_leds(uint16_t low_mask)
{
    uint32_t spin;

    __HAL_RCC_GPIOH_CLK_ENABLE();

    /*
     * 复位后 GPIOH 的 MODER 是 0（输入态），此时只写 BSRR 不会驱动引脚，
     * 三灯根本不会亮。故障如果发生在 board_led_init() 之前（HAL_Init /
     * SystemClock_Config 阶段的 configASSERT、早期 HardFault、_sbrk 越界），
     * 播报就完全失效，只剩一个没人看得见的死循环。
     *
     * 这里直接把三脚配成“推挽输出 / 低速 / 无上下拉”，与 bsl 的 board_led_init
     * 一致，但只写寄存器、不走 HAL_GPIO_Init：栈溢出时不能再压栈调用多层函数。
     */
    GPIOH->MODER = (GPIOH->MODER & (uint32_t)~PANIC_LED_MODE_MASK) | (uint32_t)PANIC_LED_MODE_OUT;
    GPIOH->OTYPER &= ~(uint32_t)PANIC_LED_MASK;
    GPIOH->OSPEEDR &= ~(uint32_t)PANIC_LED_MODE_MASK;
    GPIOH->PUPDR &= ~(uint32_t)PANIC_LED_MODE_MASK;

    /* BSRR 低 16 位给 1 = 该脚置高（共阳接法下为灭），高 16 位给 1 = 置低（亮）。 */
    GPIOH->BSRR = (uint32_t)PANIC_LED_MASK & ~(uint32_t)low_mask;              /* 置高：灭 */
    GPIOH->BSRR = ((uint32_t)low_mask & (uint32_t)PANIC_LED_MASK) << 16U;      /* 置低：亮 */
    for (spin = 0U; spin < PANIC_SPIN; spin++) {
        __asm volatile("nop");
    }
}

/*
 * 以“闪灯次数 = 错误码”输出，3 位二进制用三颗灯，
 * 末段全亮表示故障码播报结束，之后停机。
 * 对外入口，fml 的 fml_led_panic 也走这里（码表见 osl/inc/osl_panic.h）。
 * 注意：本函数会自己配灯脚，所以不依赖 board_led_init() 是否已经跑过。
 */
void osl_panic_report(osl_panic_code_t code)
{
    uint32_t bit;
    uint32_t round;
    uint32_t raw = (uint32_t)code;
    uint16_t mask;

    /*
     * 入口先做两件事，顺序不能换：
     *   1) 重入闸——已在播报中的第二个上下文到此停住，不再碰 record；
     *   2) 关掉全部外部中断——必须早于写 record，否则写的途中就可能有 0..4
     *      优先级的中断进来再走一遍本函数，把同一份 record 撕成一半旧一半新。
     * 两步都不依赖 SysTick：播报用 nop 粗延时，HAL_GetTick 走 DWT 的 CYCCNT。
     */
    if (g_panic_reporting != 0U) {
        for (;;) {
            __asm volatile("nop");
        }
    }
    g_panic_reporting = 1U;
    panic_mask_all_irqs();

    /* 先把看门狗超时拉到最长再播报：否则 1 s 后就复位，三灯还没来得及闪完。 */
    osl_watchdog_relax();

    /*
     * seq / reset_cause 都以"上一次留在 .noinit 里的 magic 是否有效"为前提：
     * 同一个故障把板子复位后再次触发 panic 时，seq 会累加（现场能分清第一次和第 N 次），
     * reset_cause 把 g_reset_cause（从 RCC->CSR 采到的复位原因）写进现场 ——
     * 这是该全局唯一的消费点，否则它就是"采集了没人读"。
     *
     * 同一处把"这次是任务还是中断触发的 panic"（IPSR）、CSR 原始值、
     * 以及看门狗超时是否真的改成功（wd_sync_fail）一起落盘：
     * 所有入口（assert / 栈溢出 / malloc 失败 / HardFault / 未实现中断 / _sbrk）
     * 最终都走到这里，一处写入即覆盖全部路径，不必让每个钩子各写一遍。
     */
    g_panic_record.seq = (g_panic_record.magic == PANIC_MAGIC) ? (g_panic_record.seq + 1U) : 1U;
    g_panic_record.reset_cause = (uint32_t)g_reset_cause;
    g_panic_record.ipsr = (uint32_t)__get_IPSR();
    g_panic_record.reset_csr = g_reset_csr;
    g_panic_record.wd_sync_fail = g_iwdg_sync_fail;
    /*
     * 堆水位一起落盘：码 2 只说“创建失败 / 堆不足”，说不了“当时还剩多少”。
     * 这里读的是历史最低（0xFFFFFFFF = 一次都没采到，说明采样本身没接上），
     * 与 g_stack_free_min[] 一起构成码 2 / 码 7 的根因读数。
     */
    g_panic_record.heap_free_min = g_heap_free_min;
    g_panic_record.magic = PANIC_MAGIC;
    g_panic_record.code = raw;
    for (round = 0U; round < 4U; round++) {
        for (bit = 0U; bit < 3U; bit++) {
            mask = (uint16_t)(1U << (10U + bit));
            panic_leds(((raw >> bit) & 1U) != 0U ? mask : 0U);
        }
    }
    panic_leds(0x1C00U); /* 全亮收尾 */
    taskDISABLE_INTERRUPTS();
    for (;;) {
        __asm volatile("nop");
    }
}

/* ------------------------------------------------------------------------- */
/* 硬件异常：Cortex-M4 的四个 fault 入口必须由本工程接管                      */
/* ------------------------------------------------------------------------- */
/*
 * startup_stm32f429xx.s 把 HardFault_Handler / MemManage_Handler / BusFault_Handler /
 * UsageFault_Handler 都 .weak 别名到 Default_Handler，而 Default_Handler 只是
 * `Infinite_Loop: b Infinite_Loop`。也就是说：一旦空指针、非对齐访问、除零、非法
 * 指令真的发生，板子只会"安静地不动"，g_panic_record 里什么都不会留下 —— 前面
 * 做的三灯播报对最容易发生的一类故障完全无效。
 *
 * 这里给出强定义来压过 startup 的弱别名。能压过的前提和 HAL_GetTick 覆盖是同一件事：
 * 本文件（freertos_port_st.o）因为 _sbrk / __malloc_lock / 钩子被无条件拉进链接，
 * 强定义才会出现在链接集合里；不要把这些东西拆到别的编译单元。
 *
 * 现场信息按"先能读到寄存器，再谈可读性"的顺序落盘：
 *   CFSR / HFSR / MMFAR / BFAR —— 说明是哪类 fault、访问了哪个地址；
 *   EXC_RETURN + 异常帧 —— 说明 %PC 停在哪条指令、%LR 来自哪次调用。
 * 三灯只闪 code 6（3 位编码装不下 4 类细分），具体类别读 fault_kind。
 */
#define FAULT_KIND_HARD 1U
#define FAULT_KIND_MEM 2U
#define FAULT_KIND_BUS 3U
#define FAULT_KIND_USAGE 4U

/*
 * 默认 SHCSR 里 MemManage / BusFault / UsageFault 是关闭的，三类都升级成 HardFault，
 * 现场只剩一个 CFSR 说明不了来源。这里显式打开，让 fault_kind 真的能区分。
 * 由 HAL_InitTick() 调用（main 的 HAL_Init 必经），不额外引入新的启动顺序依赖。
 */
static void fault_monitor_init(void)
{
    SCB->SHCSR |= SCB_SHCSR_MEMFAULTENA_Msk | SCB_SHCSR_BUSFAULTENA_Msk |
                  SCB_SHCSR_USGFAULTENA_Msk;
    __DSB();
    __ISB();
}

/* 扩展帧 26 字 = 0x68 字节（S0..S15 + FPSCR + 保留字 + R0..xPSR） */
#define PANIC_FRAME_MAX_BYTES 0x68U

static void fault_report(uint32_t kind, uint32_t exc_return)
    __attribute__((noreturn));

static void fault_report(uint32_t kind, uint32_t exc_return)
{
    uint32_t sp;
    uint32_t *frame;
    uint32_t frame_type;
    uint32_t frame_offset;

    panic_mask_all_irqs();
    g_panic_record.magic = PANIC_MAGIC;
    g_panic_record.fault_kind = kind;
    g_panic_record.fault_cfsr = SCB->CFSR;
    g_panic_record.fault_hfsr = SCB->HFSR;
    g_panic_record.fault_mmfar = SCB->MMFAR;
    g_panic_record.fault_bfar = SCB->BFAR;
    g_panic_record.fault_exc_return = exc_return;

    /* EXC_RETURN 的 bit2 选择异常帧所在的栈：0 = MSP，1 = PSP。 */
    sp = ((exc_return & 0x4U) != 0U) ? __get_PSP() : __get_MSP();
    g_panic_record.fault_sp = sp;

    /*
     * EXC_RETURN 的 bit4 决定帧类型：
     *   1 = 基本帧（8 字）：R0 在 +0x00、LR 在 +0x14、PC 在 +0x18；
     *   0 = 扩展帧（26 字）：硬件先把 S0..S15/FPSCR 压到低地址，
     *       R0 在 +0x48、LR 在 +0x5C、PC 在 +0x60。
     * M4F port 开了 FPU 且默认懒压栈：只要被中断的上下文用过浮点，硬件就压
     * 扩展帧。以前恒按 8 字取值，这时 fault_pc 里存的是 S6 —— 错误的 PC 比没有更糟。
     */
    frame_type = ((exc_return & 0x10U) != 0U) ? 1U : 2U;
    frame_offset = (frame_type == 1U) ? 0U : 0x48U;
    g_panic_record.fault_frame_type = frame_type;

    /* SP 必须留在 RAM（0x20000000..0x20030000，192 KB）且整帧不越过栈顶，
     * 否则说明栈已经崩了，再去读只会二次 fault，宁可留空。 */
    if (sp >= 0x20000000U && sp <= 0x20030000U - PANIC_FRAME_MAX_BYTES) {
        frame = (uint32_t *)(sp + frame_offset);
        g_panic_record.fault_r0 = frame[0];
        g_panic_record.fault_r1 = frame[1];
        g_panic_record.fault_r2 = frame[2];
        g_panic_record.fault_r3 = frame[3];
        g_panic_record.fault_r12 = frame[4];
        g_panic_record.fault_lr = frame[5];
        g_panic_record.fault_pc = frame[6];
        g_panic_record.fault_xpsr = frame[7];
    }

    osl_panic_report(OSL_PANIC_HARDWARE_FAULT);
}

/*
 * 用宏生成四个入口。EXC_RETURN 必须在本函数的第一条语句里取出：
 * 那时还没有任何 BL 执行过，LR 仍是异常进入时硬件写入的 EXC_RETURN；
 * 一旦先调用别的函数，LR 就被覆盖成返回地址。
 */
#define DEFINE_FAULT_HANDLER(name, kind)                                        \
    void name(void)                                                             \
    {                                                                           \
        uint32_t exc_return;                                                    \
        __asm volatile("mov %0, lr" : "=r"(exc_return));                        \
        fault_report(kind, exc_return);                                         \
    }

DEFINE_FAULT_HANDLER(HardFault_Handler, FAULT_KIND_HARD)
DEFINE_FAULT_HANDLER(MemManage_Handler, FAULT_KIND_MEM)
DEFINE_FAULT_HANDLER(BusFault_Handler, FAULT_KIND_BUS)
DEFINE_FAULT_HANDLER(UsageFault_Handler, FAULT_KIND_USAGE)

/*
 * startup_stm32f429xx.s 的 Default_Handler 原本是 `b Infinite_Loop`：90 个没有实现的
 * 中断、以及向量表里未定义的槽位全落在那里，表现和硬故障一模一样——板子安静停住，
 * 什么都不留。现在 .s 里改成跳到这里：记录中断号后按 code 6 播报停机。
 * 中断号可以对照启动文件里的 .thumb_set 顺序，或直接看 SCB->ICSR 的 VECTACTIVE。
 */
void UnhandledIRQ_Handler(void)
{
    uint32_t irq = SCB->ICSR & SCB_ICSR_VECTACTIVE_Msk;

    /* 全局屏蔽要早于下面两条写：只关 VECTACTIVE 那一个源，挡不住其它未实现外设。 */
    panic_mask_all_irqs();
    g_panic_record.magic = PANIC_MAGIC;
    g_panic_record.unhandled_irq = irq;

    /*
     * 先关掉这个中断源再播报。osl_panic_report 里的 taskDISABLE_INTERRUPTS 只把
     * BASEPRI 提到 configMAX_SYSCALL_INTERRUPT_PRIORITY(=0x50)，优先级 0..4 的中断
     * 仍然可以进来。如果引发本次异常的外设一直拉着中断线（最典型的是"没人清标志
     * 的未实现外设"），这里会被反复重入，把 MSP 压穿后变成二次 HardFault，
     * 三灯播报就永远闪不完。
     *
     * 16 以下编号是系统异常（这时 VECTACTIVE 只可能是真正的 IRQ，0..15 不会走到
     * Default_Handler），所以只对 IRQ 操作；STM32F429 最多 90 个 IRQ，< 128 的
     * 上界顺带避免了 NVIC->ICER 的数组越界。
     */
    if (irq >= 16U && irq < 128U) {
        NVIC->ICER[(irq >> 5U)] = (uint32_t)1U << (irq & 0x1FU);
        NVIC->ICPR[(irq >> 5U)] = (uint32_t)1U << (irq & 0x1FU);
    }

    osl_panic_report(OSL_PANIC_HARDWARE_FAULT);
}

/* configCHECK_FOR_STACK_OVERFLOW == 2 要求应用提供该钩子，否则 tasks.c 无法链接。 */
void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName)
{
    uint32_t i;

    panic_mask_all_irqs();
    g_panic_record.magic = PANIC_MAGIC;
    g_panic_record.task = xTask;
    if (pcTaskName != NULL) {
        for (i = 0U; i + 1U < (uint32_t)configMAX_TASK_NAME_LEN; i++) {
            g_panic_record.task_name[i] = pcTaskName[i];
            if (pcTaskName[i] == '\0') {
                break;
            }
        }
        g_panic_record.task_name[i] = '\0';
    }
    osl_panic_report(OSL_PANIC_STACK_OVERFLOW);
}

/* 任务/堆创建失败：同样要看得见，否则“什么都没发生”是唯一的症状。 */
void vApplicationMallocFailedHook(void)
{
    panic_mask_all_irqs();
    g_panic_record.magic = PANIC_MAGIC;
    g_panic_record.code = (uint32_t)OSL_PANIC_CREATE_FAILED;
    osl_panic_report(OSL_PANIC_CREATE_FAILED);
}

/* configASSERT 的落点：记录文件名/行号（调试器可读 g_panic_record）后播报。 */
void vAssertFailed(const char *file, uint32_t line)
{
    panic_mask_all_irqs();
    g_panic_record.magic = PANIC_MAGIC;
    g_panic_record.assert_file = file;
    g_panic_record.assert_line = line;
    osl_panic_report(OSL_PANIC_ASSERT);
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
    g_panic_record.magic = PANIC_MAGIC;
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
 * 符合 newlib 要求“可递归”的语义，调度器启动前也能用。
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
