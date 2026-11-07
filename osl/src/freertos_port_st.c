#include <stddef.h>

#include "FreeRTOS.h"
#include "task.h"
#include "stm32f4xx_hal.h"
#include "system_stm32f4xx.h"
#include "osl_panic.h"

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
 *   - 单个测量区间的上限是 CYCCNT 回绕周期（16 MHz 下约 268 s），
 *     HAL 的毫秒级超时远小于该值；超时比较用的是无符号差值，回绕安全。
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
    uint32_t fault_sp;
    uint32_t fault_r0;
    uint32_t fault_r1;
    uint32_t fault_r2;
    uint32_t fault_r3;
    uint32_t fault_r12;
    uint32_t fault_lr;
    uint32_t fault_pc;
    uint32_t fault_xpsr;
} panic_record_t;

#define PANIC_MAGIC 0x50414E43UL /* "PANC" */

volatile panic_record_t g_panic_record;
volatile char g_last_write[64]; /* _write 的最近一段输出，调试器可直接读 */

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

/* configCHECK_FOR_STACK_OVERFLOW == 2 要求应用提供该钩子，否则 tasks.c 无法链接。 */
void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName)
{
    uint32_t i;

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
    g_panic_record.magic = PANIC_MAGIC;
    g_panic_record.code = (uint32_t)OSL_PANIC_CREATE_FAILED;
    osl_panic_report(OSL_PANIC_CREATE_FAILED);
}

/* configASSERT 的落点：记录文件名/行号（调试器可读 g_panic_record）后播报。 */
void vAssertFailed(const char *file, uint32_t line)
{
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
    intptr_t next = (intptr_t)previous + (intptr_t)incr;

    /* malloc 返回的地址要按 8 字节对齐（Cortex-M4 上的 double / uint64_t）。 */
    next = (intptr_t)(((uintptr_t)next + 7U) & ~(uintptr_t)7U);
    if (next < (intptr_t)__heap_start__ || next > (intptr_t)__heap_end__) {
        g_panic_record.magic = PANIC_MAGIC;
        g_panic_record.code = (uint32_t)OSL_PANIC_HEAP_EXHAUSTED;
        osl_panic_report(OSL_PANIC_HEAP_EXHAUSTED);
    }
    g_newlib_brk = (char *)next;
    return (void *)previous;
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
