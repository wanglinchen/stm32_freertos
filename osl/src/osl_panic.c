#include <stddef.h>
#include <string.h>

#include "FreeRTOS.h"
#include "task.h"
#include "stm32f4xx_hal.h"
#include "board_panic_hw.h"
#include "system_stm32f4xx.h"
#include "osl_panic.h"
#include "osl_boot.h"
#include "osl_health.h"
#include "osl_panic_record.h"
#include "osl_panic_internal.h"

/* ------------------------------------------------------------------------- */
/* 故障现场：钩子必须留下证据，否则板子只会“安静地不动”                      */
/* ------------------------------------------------------------------------- */
/* 码值定义在 osl/inc/osl_panic.h，与 fml 共用一份；这里不再各自定义。 */

/*
 * 放 .noinit 而不是 .bss：panic 记录是复位后唯一还剩的证据。
 * Reset_Handler 只清 _sbss.._ebss（链接脚本的 .noinit 段在 .bss 之外、NOLOAD），
 * 所以 IWDG 复位后重启时，上一次的 code / task_name / fault_pc 仍在 RAM 里。
 * 首次上电内容随机，因此一律以 magic 作为"这里真的有记录"的判据。
 */
volatile panic_record_t g_panic_record __attribute__((section(".noinit")));
/*
 * 同样放 .noinit：_write 是 panic 播报之外唯一的调试出口（只把最近 64 B 抄进这里，
 * 既不打印也不走串口）。放 .bss 时 IWDG 复位后调试器连上来读到的是一片零，
 * 上一次运行最后写的那段就丢了——而这正是想看的东西。
 * 首次上电内容随机，没有 magic 可判，读到非零即视为上一次运行留下的。
 */
volatile char g_last_write[64] __attribute__((section(".noinit")));

/*
 * 尺寸下限是硬契约，不是建议值。panic_last_write() 的前缀段
 * （'P' + code 一位 + ' ' + 'S' + seq 至多 4 位 + ' '）共 9 B 且**无条件写**，
 * 之后还要写 1 B 终止符 —— 缓冲小于 10 B 时写就越过末尾。
 *
 * 为什么这不是"最多截断几个字符"的小事：g_last_write 与 g_panic_record 同在
 * .noinit 段、紧邻排列（64 B 配置下实测 +0x64 恰好是 g_panic_record 的起点，
 * 中间没有对齐填充），而 panic_record_t 的第一个字段就是 magic。越界那一字节
 * 会直接清掉 magic，让这条记录被读成"从未发生过"。缩到非 4 倍数尺寸时链接器
 * 会插入 0–3 B 对齐填充吸收越界 —— 但那是运气，不是保证。
 *
 * 所以用编译期断言把它钉在这里：尺寸被改小时构建就失败，而不是等到某一次
 * 真正的 panic 现场才暴露（panic 不返回、不可重试，那一次就是最后一次机会）。
 */
_Static_assert(sizeof(g_last_write) >= 10U,
               "g_last_write needs >= 10 bytes: 9-byte prefix + NUL");

/* 功能门槛：不截断任务名需要有 9 + 15 + 1 = 25 B（见 panic_last_write 的 name 循环）。 */
_Static_assert(sizeof(g_last_write) >= 10U + (PANIC_TASK_NAME_MAX - 1U),
               "g_last_write needs >= 25 bytes: 9 prefix + 15 task name + NUL");

/*
 * panic 通道的重入闸。放 .bss 而不是 .noinit：.noinit 跨复位保留，第二次真 panic
 * 会被当成"已在播报"直接停住，记录永远写不进去；.bss 由 Reset_Handler 清零，
 * 正好只挡"同一次运行内"的重入。
 */
static volatile uint32_t g_panic_reporting;

/*
 * 关掉全部中断。写 record 与三灯播报（约 1.2–1.5 s）期间，任何能打断它的路径都会
 * 把现场撕坏：第二个未实现外设触发会重入 UnhandledIRQ_Handler 再走一遍 osl_panic_report，
 * 把同一份 record 交错写坏；SysTick 会切走任务上下文触发的播报，让别的任务把灯改掉。
 *
 * 分两类，各自用不同机制：
 *   0..89 号外部 IRQ —— NVIC 的 ICER/ICPR[0..PANIC_NVIC_WORDS-1]。
 *   16 号以下的系统异常 —— 它们不在 ICER 里，使能位在 SCB 层：
 *     - BASEPRI = configMAX_SYSCALL_INTERRUPT_PRIORITY 屏蔽所有数值 >= 0x50 的异常，
 *       SysTick / PendSV（都是 0xF0）必然落在里面；HardFault / NMI 优先级是 -1 / -2，
 *       不受 BASEPRI 约束，二次 fault 仍能进来落记录。
 *     - 已经挂起的 SysTick / PendSV 要在 BASEPRI 建立后清掉，否则那次切换会立刻补执行。
 * 顺序不能换：BASEPRI 必须是第一条，否则清 ICER 的途中就可能有 SysTick 进来。
 * ICER 与 ICPR 一起清：只关使能会留下挂起位，重入路径仍未完全关闭。
 */
/*
 * NVIC 的 ICER/ICPR 各是 8 个 32 位字，但 F429 只实现了 90 个外部 IRQ：
 *   (90 + 31) / 32 = 3 个字（第 3 个字只用到低 26 位，即 IRQ64..89）。
 * 系统异常（异常号 0..15）不在 ICER 的管辖里，它们的使能位在 SCB 层，所以这个
 * 上界只按 90 个外部 IRQ 算 —— 把 16 也加进去会多算一个字，而多写的第 4 个字是
 * 保留位：不报错、不生效，还把真实上限藏在一个看起来更大的数字里。
 */
#define PANIC_EXTI_IRQ_COUNT 90U
#define PANIC_NVIC_WORDS (((PANIC_EXTI_IRQ_COUNT) + 31U) / 32U)

void panic_mask_all_irqs(void)
{
    uint32_t index;

    __set_BASEPRI(configMAX_SYSCALL_INTERRUPT_PRIORITY);

    for (index = 0U; index < PANIC_NVIC_WORDS; index++) {
        NVIC->ICER[index] = 0xFFFFFFFFU;
        NVIC->ICPR[index] = 0xFFFFFFFFU;
    }

    /* ICSR 里 PENDSVCLR / PENDSTCLR / PENDSTSET 都是写 1 有效，其余位写 0 无副作用。 */
    SCB->ICSR = SCB_ICSR_PENDSVCLR_Msk | SCB_ICSR_PENDSTCLR_Msk;
}

/*
 * 灯脚 / 掩码 / 蜂鸣器静音：见 osl/inc/board_panic_hw.h（由 device DTS 生成）。
 * 这里故意绕过 HAL_GPIO / 板级驱动：栈可能已溢出，只写寄存器。
 */
#define PANIC_SPIN 2000000U    /* 未校时的粗延时：只求肉眼可辨，主频越高闪得越快 */

static void panic_leds(uint16_t low_mask)
{
    uint32_t spin;

    PANIC_LED_RCC_CLK_ENABLE();

    /* 蜂鸣器静音：阿波罗 V2 经 PCF8574，其它走 GPIO BSRR（实现于 bsl/board_beep.c）。 */
    board_panic_beep_silence();

    /*
     * 复位后 GPIOB 的 MODER 是 0（输入态），此时只写 BSRR 不会驱动引脚，
     * 三灯根本不会亮。故障如果发生在 board_led_init() 之前（HAL_Init /
     * SystemClock_Config 阶段的 configASSERT、早期 HardFault、_sbrk 越界），
     * 播报就完全失效，只剩一个没人看得见的死循环。
     *
     * 这里直接把三脚配成“推挽输出 / 低速 / 无上下拉”，与 bsl 的 board_led_init
     * 一致，但只写寄存器、不走 HAL_GPIO_Init：栈溢出时不能再压栈调用多层函数。
     */
    PANIC_LED_GPIO->MODER = (PANIC_LED_GPIO->MODER & (uint32_t)~PANIC_LED_MODE_MASK)
        | (uint32_t)PANIC_LED_MODE_OUT;
    PANIC_LED_GPIO->OTYPER &= ~(uint32_t)PANIC_LED_MASK;
    PANIC_LED_GPIO->OSPEEDR &= ~(uint32_t)PANIC_LED_MODE_MASK;
    PANIC_LED_GPIO->PUPDR &= ~(uint32_t)PANIC_LED_MODE_MASK;

    /* BSRR 低 16 位给 1 = 该脚置高（共阳接法下为灭），高 16 位给 1 = 置低（亮）。 */
    PANIC_LED_GPIO->BSRR = (uint32_t)PANIC_LED_MASK & ~(uint32_t)low_mask;
    PANIC_LED_GPIO->BSRR = ((uint32_t)low_mask & (uint32_t)PANIC_LED_MASK) << 16U;
    for (spin = 0U; spin < PANIC_SPIN; spin++) {
        __asm volatile("nop");
    }
}

/*
 * 以“闪灯次数 = 错误码”输出，3 位二进制用三颗灯，
 * 末段全亮表示故障码播报结束，之后停机。
 * 对外入口，fml 的 fml_panic 也走这里（码表见 osl/inc/osl_panic.h）。
 * 注意：本函数会自己配灯脚，所以不依赖 board_led_init() 是否已经跑过。
 */
/*
 * 把一行定长摘要写进 .noinit 的 g_last_write。
 *
 * 为什么需要：这个 64 B 缓冲是本工程唯一的"复位后能读到上一轮现场"通道 ——
 * 没有串口、没有 SEGGER RTT、printf 家族也不在镜像里（nm 实测 printf /
 * vprintf / iprintf / puts / fwrite 一个都没有）。但它唯一的写入方 _write
 * 没有任何调用方，所以调试器连上来读到的永远是初值或上一轮的遗留值。
 * panic 是不返回的（末尾关中断自旋），摘要必须落在三灯播报之前，否则永远没机会写。
 *
 * 格式：P<code> S<seq> <task_name>，例如 "P7 S3 led_blink"。
 *   code 取原始值（可能 >7，那是传参越界而不是真栈溢出，与 g_panic_record.code 同源）；
 *   seq 是同一故障反复复位后的累计次数，>1 说明不是一次性事件；
 *   task_name 最多抄 PANIC_TASK_NAME_MAX - 1 字节，不依赖它自带 '\0'。
 *
 * 参数用 const volatile：g_panic_record 整体是 volatile（它在 .noinit，取值必须
 * 真的从内存读，不能被优化成寄存器缓存），所以 task_name 退化成 volatile char[]。
 *
 * 全部手写拼接：调 snprintf 会把格式化引擎整个拉进镜像（text 一次涨几千字节），
 * 而这里只需要十进制与 ASCII。写入目标是 volatile char[]，每次写入都是字节写，
 * 门禁据此判"有人在写"而不是"符号还在"。
 */
static void panic_last_write(uint32_t code, uint32_t seq,
                             const volatile char *name)
{
    uint32_t i = 0U;
    uint32_t value;
    uint32_t divisor;
    uint32_t digit;
    uint32_t name_chars = 0U;

    g_last_write[i++] = 'P';
    value = (code > 9U) ? 9U : code;
    g_last_write[i++] = (char)('0' + value);

    g_last_write[i++] = ' ';
    g_last_write[i++] = 'S';
    value = (seq > 9999U) ? 9999U : seq;
    divisor = 1000U;
    while ((divisor > 1U) && (value < divisor)) {
        divisor /= 10U;
    }
    while (divisor != 0U) {
        digit = value / divisor;
        g_last_write[i++] = (char)('0' + digit);
        value -= digit * divisor;
        divisor = (divisor == 1U) ? 0U : (divisor / 10U);
    }

    g_last_write[i++] = ' ';
    while ((name[name_chars] != '\0') &&
           (name_chars + 1U < PANIC_TASK_NAME_MAX) &&
           (i + 1U < (uint32_t)(sizeof(g_last_write)))) {
        g_last_write[i++] = name[name_chars];
        name_chars++;
    }
    g_last_write[i] = '\0';
}

/*
 * 把「故障族」字段整族清成无证据值。
 *
 * g_panic_record 在 .noinit：Reset_Handler 不清它，而 panic 之后板子必然复位。
 * 于是 magic 只能证明「这里有过一条记录」，证明不了「这些字段属于本次故障」——
 * 上一次 HardFault 的 fault_pc / fault_kind 会原地冒充这一次栈溢出或未实现中断的
 * 现场。本文件自己写过「错误的 PC 比没有更糟」，所以读不到就留 0。
 *
 * 必须是「一次清整族」，不许各入口各清几条：漏掉的那条就是旧值，而漏了哪条无法
 * 从现场反推。以下字段不在清理范围内 —— 它们是 osl_panic_report 统一落盘的
 * 「跨代际继承值」，清掉就等于丢历史：seq / magic / reset_cause / reset_csr /
 * ipsr / wd_sync_fail / heap_free_min / stack_free_min[] / stack_slot_err。
 */
static void panic_clear_fault_scene(void)
{
    g_panic_record.fault_kind = 0U;
    g_panic_record.fault_cfsr = 0U;
    g_panic_record.fault_hfsr = 0U;
    g_panic_record.fault_mmfar = 0U;
    g_panic_record.fault_bfar = 0U;
    g_panic_record.fault_exc_return = 0U;
    g_panic_record.fault_frame_type = 0U;
    g_panic_record.fault_sp = 0U;
    g_panic_record.fault_r0 = 0U;
    g_panic_record.fault_r1 = 0U;
    g_panic_record.fault_r2 = 0U;
    g_panic_record.fault_r3 = 0U;
    g_panic_record.fault_r12 = 0U;
    g_panic_record.fault_lr = 0U;
    g_panic_record.fault_pc = 0U;
    g_panic_record.fault_xpsr = 0U;
    g_panic_record.unhandled_irq = 0U;
}

/* 产物断言在它的指令流里核对「先关中断、再置重入闸」的顺序，禁止内联：
 * 发布构建（-Os）会把入口并进调用者，那个置位与顺序就再也证明不了。 */
__attribute__((noinline)) void osl_panic_report(osl_panic_code_t code)
{
    uint32_t bit;
    uint32_t round;
    uint32_t raw = (uint32_t)code;
    uint32_t shown;
    uint16_t mask;
    uint32_t slot;
    uint32_t heap_min;

    /*
     * 入口的顺序：先关中断，再判重入 —— 不能换。
     *
     * panic_mask_all_irqs() 用的是 BASEPRI = configMAX_SYSCALL_INTERRUPT_PRIORITY（0x50），
     * 只屏蔽数值 >= 5 的异常；HardFault / NMI 优先级是 -1 / -2，不受 BASEPRI 约束，
     * 任何时候都能进来。若先判重入、后关中断，那么判的瞬间 g_panic_reporting 还是 0
     * 而 BASEPRI 还没建立 —— 一个 HardFault 就能插进这几条指令之间：它自己先
     * panic_mask_all_irqs() 再进本函数，读到的仍然是 0，于是两层都通过重入闸、都去写
     * g_panic_record，.noinit 里的现场被撕成一半旧一半新（code / seq / ipsr 互相覆盖）。
     *
     * 这条路是可达的：stack_guard_check() 在 vApplicationTickHook() 里跑（SysTick
     * 上下文，BASEPRI 此时为 0），它判的正是"MSP 已越过 _estack - _Min_Stack_Size" ——
     * MSP 越界本身就是压栈失败，HardFault 多半已经挂起，会在下一条指令边界立刻进来。
     * 缺陷态反汇编实测：第一条条件分支在第 3 条指令、置位在第 7 条、
     * panic_mask_all_irqs 在第 9 条 —— 前 8 条指令全在无屏蔽状态下执行。
     *
     * mask 是幂等的（重复设同一个 BASEPRI、重复清同一批 ICER/ICPR 位），提前执行的代价
     * 只有几条指令；fault 入口里已经先 mask 过一次的路径会调两遍，同样无害。
     */
    panic_mask_all_irqs();
    if (g_panic_reporting != 0U) {
        for (;;) {
            __asm volatile("nop");
        }
    }
    g_panic_reporting = 1U;

    /*
     * 三灯只有 3 位，码值空间是 0..7（码表见 osl/inc/osl_panic.h）。传进来的值若越界
     * （例如将来有人写 fml_panic(9U)），直接拿去闪灯会被 `& 0x7` 截成 1，读灯的人
     * 就会去查"调度器起不来"——方向直接错。这里把**播报值**钳到上界 7，而
     * g_panic_record.code 仍记原始值：从调试器读到 >7 就知道这是传参越界，不是真栈溢出。
     */
    shown = (raw > (uint32_t)OSL_PANIC_STACK_OVERFLOW) ? (uint32_t)OSL_PANIC_STACK_OVERFLOW : raw;

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
    /*
     * 这一行必须在 magic 被写成 PANIC_MAGIC **之前**执行，且各入口都不许抢在前面写。
     *
     * 判据是「上一轮有没有留下记录」：.noinit 跨复位保留，上一轮 panic 结束时
     * magic 已经是 PANIC_MAGIC。若入口在调用本函数之前就把 magic 写掉，判据恒真 ——
     * 首次上电（.noinit 是随机内容）读到的 seq 会是「随机值 + 1」，看起来像已经
     * 反复复位过几亿次，而实际是第一次。magic 只在下面落盘时统一写一次。
     */
    g_panic_record.seq = (g_panic_record.magic == PANIC_MAGIC) ? (g_panic_record.seq + 1U) : 1U;
    g_panic_record.reset_cause = (uint32_t)g_reset_cause;
    g_panic_record.ipsr = (uint32_t)__get_IPSR();
    g_panic_record.reset_csr = g_reset_csr;
    g_panic_record.wd_sync_fail = g_iwdg_sync_fail;
    /*
     * 「别的族」在这里清掉（故障族由各硬件入口调 panic_clear_fault_scene 清，
     * 因为码 6 有两种来源、fault 与 unhandled 各清自己那一半）。判据是「本次码是否
     * 属于该族」：属于就保留（入口已经填好），不属于就清空。
     *
     *   * assert_file / assert_line 只有 vAssertFailed 写；
     *   * task / task_name 只有 malloc 钩子、栈溢出钩子、vAssertFailed 写，
     *     而 fml_panic(1U/4U) 这类直调路径完全不写。
     *
     * 不清的下场与故障族同源：.noinit 里上一次的 assert 行号、上一次的任务名会留到
     * 这一次的现场里，把人指向错误的创建点。
     */
    /*
     * 不是硬件 fault 的码必须自己清故障族。
     *
     * 故障族（fault_kind / fault_pc / fault_sp …）只在两个硬件入口里由
     * panic_clear_fault_scene() 清。本函数是**所有**码的汇合点，码 1/2/3/4/5/7
     * 都不经过那两个入口 —— 于是「上一次是 HardFault、这一次是栈溢出」的记录里，
     * code = 7 却带着上一次的 fault_kind = 1 和上一次的 fault_pc，
     * 读到的人会把栈溢出当成硬件异常去查那条早已不存在的访存指令。
     *
     * 码 6 不能清：两条入口已经在调用前清过并填好现场，这里再清一次会把刚取到的
     * 异常帧整个冲掉（fault_report 的清理必须早于读帧，同理这里必须晚于填字段）。
     */
    if (raw != (uint32_t)OSL_PANIC_HARDWARE_FAULT) {
        panic_clear_fault_scene();
    }
    if (raw != (uint32_t)OSL_PANIC_ASSERT) {
        g_panic_record.assert_file = NULL;
        g_panic_record.assert_line = 0U;
    }
    if ((raw != (uint32_t)OSL_PANIC_CREATE_FAILED) &&
        (raw != (uint32_t)OSL_PANIC_ASSERT) &&
        (raw != (uint32_t)OSL_PANIC_STACK_OVERFLOW)) {
        g_panic_record.task = NULL;
        g_panic_record.task_name[0] = '\0';
    }
    /*
     * 堆水位一起落盘：码 2 只说“创建失败 / 堆不足”，说不了“当时还剩多少”。
     * 码 7 的栈余量与槽号计数在下面单独搬 —— 那两项的源变量仍在 .bss，
     * 必须显式拷进 .noinit 才能跨复位，光在结构体里声明字段不够。
     *
     * 码 1 / 码 2 必须补采：g_heap_free_min 由 tick hook 更新，而 tick 只在调度器跑
     * 起来之后才走；码 1（调度器起不来）与启动期码 2 时变量仍为 OSL_HEALTH_UNSET。
     *
     * 补采用 xPortGetMinimumEverFreeHeapSize()：heap_4.c 里它只是
     * `return xMinimumEverFreeBytesRemaining;` —— 一次全局变量读，不遍历空闲链表、
     * 不加锁、不压栈调用多层函数，在 panic 上下文（栈可能已坏）里安全。
     */
    if (g_heap_free_min == (uint32_t)OSL_HEALTH_UNSET) {
        heap_min = (uint32_t)xPortGetMinimumEverFreeHeapSize();
    } else {
        heap_min = g_heap_free_min;
    }
    /*
     * 补采源本身也可能「没有读数」：heap_4 的 xMinimumEverFreeBytesRemaining
     * 初值就是 0，prvHeapInit() 要等第一次 pvPortMalloc 才跑。码 4（时钟失败）
     * 发生在任何 malloc 之前，此时它返回 0 —— 而 0 在 heap_4 里的语义是
     * 「堆正好抽干」，比一个无意义的初值更像一条结论，会把排查方向带偏。
     * 所以 0 一律改判成 UNSET：从未成功分配过内存时，「剩多少」这个问题的
     * 正确答案是「还没开始统计」。
     */
    if (heap_min == 0U) {
        heap_min = (uint32_t)OSL_HEALTH_UNSET;
    }
    g_panic_record.heap_free_min = heap_min;
    /*
     * 码 7 的根因一起落盘。两个来源都在 .bss：一次复位就清零，而 panic 之后
     * 板子必然复位（末尾关中断自旋 + IWDG），所以这里必须搬一份到 .noinit。
     * 只读全局、不调 FreeRTOS API —— panic 上下文里栈可能已经坏了，
     * uxTaskGetStackHighWaterMark 之类的调用不再安全。
     */
    for (slot = 0U; slot < (uint32_t)OSL_HEALTH_STACK_SLOTS; slot++) {
        g_panic_record.stack_free_min[slot] = g_stack_free_min[slot];
    }
    g_panic_record.stack_slot_err = g_stack_slot_err;
    g_panic_record.magic = PANIC_MAGIC;
    g_panic_record.code = raw;
    /* 三灯播报之后就再没有机会了（末尾关中断自旋），摘要必须在播报之前落盘。 */
    panic_last_write(raw, g_panic_record.seq, g_panic_record.task_name);
    for (round = 0U; round < 4U; round++) {
        for (bit = 0U; bit < 3U; bit++) {
            mask = (uint16_t)(1U << panic_led_pin[bit]);
            panic_leds(((shown >> bit) & 1U) != 0U ? mask : 0U);
        }
    }
    panic_leds(0x0007U); /* 全亮收尾 */
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
 * osl_boot.o 因为 _sbrk / __malloc_lock / 钩子被无条件拉进链接，再由它引用本文件，
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
void osl_fault_monitor_init(void)
{
    SCB->SHCSR |= SCB_SHCSR_MEMFAULTENA_Msk | SCB_SHCSR_BUSFAULTENA_Msk |
                  SCB_SHCSR_USGFAULTENA_Msk;
    __DSB();
    __ISB();
}

/* 扩展帧 26 字 = 0x68 字节（S0..S15 + FPSCR + 保留字 + R0..xPSR） */
#define PANIC_FRAME_MAX_BYTES 0x68U

/* RAM 边界由链接脚本给出：`_sdata` = .data 起点（RAM 低端），`_estack` = RAM 顶端。
 * SP 合法性判据必须跟着布局走。写死地址会在改 RAM 容量 / 换芯片 / 启用 CCMRAM
 * 之后静默失效，而且两头都不报错：判小了把合法帧判成"栈已崩"（现场直接留空），
 * 判大了把越界 SP 当合法去读（读本身又是一次 fault）。 */
extern char _sdata[];
extern char _estack[];

static void fault_report(uint32_t kind, uint32_t exc_return)
    __attribute__((noreturn));

static void fault_report(uint32_t kind, uint32_t exc_return)
{
    uint32_t sp;
    uint32_t *frame;
    uint32_t frame_type;
    uint32_t frame_offset;

    /* 全局屏蔽要早于对 g_panic_record 的第一次写入（含 panic_clear_fault_scene）。 */
    panic_mask_all_irqs();

    /*
     * 先清整族再填自己那一份。不清的下场：下面 SP 非法时会跳过读帧，8 个帧字段
     * 保留上一次 panic 的值，调试器把上一次的 PC 当成本次的现场 —— 本文件上方
     * 已经写过「错误的 PC 比没有更糟」。
     */
    panic_clear_fault_scene();
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

    /* SP 必须留在 RAM 且整帧不越过栈顶，否则说明栈已经崩了，再去读只会二次 fault，
     * 宁可留空。边界取自链接脚本的 _sdata / _estack，不写死地址。 */
    if (sp >= (uint32_t)(uintptr_t)_sdata &&
        sp <= (uint32_t)(uintptr_t)_estack - PANIC_FRAME_MAX_BYTES) {
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
    uint32_t irqn;

    /* 全局屏蔽要早于下面两条写：只关 VECTACTIVE 那一个源，挡不住其它未实现外设。 */
    panic_mask_all_irqs();
    /*
     * fault_kind 归故障族：上一次 HardFault 留下 fault_kind = 1 时，这一次的未实现
     * 中断现场会被读成「硬件框架帧故障」，归因方向直接错。整族清掉，只写中断号。
     */
    panic_clear_fault_scene();
    g_panic_record.unhandled_irq = irq;

    /*
     * 先关掉这个中断源再播报。osl_panic_report 里的 taskDISABLE_INTERRUPTS 只把
     * BASEPRI 提到 configMAX_SYSCALL_INTERRUPT_PRIORITY(=0x50)，优先级 0..4 的中断
     * 仍然可以进来。如果引发本次异常的外设一直拉着中断线（最典型的是"没人清标志
     * 的未实现外设"），这里会被反复重入，把 MSP 压穿后变成二次 HardFault，
     * 三灯播报就永远闪不完。
     *
     * 索引必须先从**异常号**换算成 **IRQn**：ICSR.VECTACTIVE 给的是异常号
     * （16..105 才是外部 IRQ），而 NVIC->ICER[n] 的 n 是 IRQn >> 5，IRQn = 异常号 - 16。
     * 少减 16 就整体偏 16 位：异常号 53（IRQ37）会去清 ICER[1] 的 bit21（= IRQ53），
     * 而真正在拉线的 IRQ37 仍开着 —— 正是这一步要防的重入没有被挡住。
     * 写法对照 core_cm4.h 的 NVIC_DisableIRQ：索引取自 IRQn，不是异常号。
     *
     * 16 以下编号是系统异常（VECTACTIVE 只可能是真正的 IRQ，0..15 不会走到
     * Default_Handler），所以只对 IRQ 操作；上界取 16 + 90 = 106，与
     * PANIC_EXTI_IRQ_COUNT 同源，不再写 128 这种与芯片无关的魔法数。
     */
    if (irq >= 16U && irq < (16U + PANIC_EXTI_IRQ_COUNT)) {
        irqn = irq - 16U;
        NVIC->ICER[irqn >> 5U] = (uint32_t)1U << (irqn & 0x1FU);
        NVIC->ICPR[irqn >> 5U] = (uint32_t)1U << (irqn & 0x1FU);
    }

    osl_panic_report(OSL_PANIC_HARDWARE_FAULT);
}

/* 把任务名抄进 panic 记录。上限取调用方给的最大长度，且不超过 record 自己的
 * PANIC_TASK_NAME_MAX——两者独立：前者跟实际任务名收窄，后者是产物尺寸判据。 */
static void panic_copy_task_name(const char *name, uint32_t limit)
{
    uint32_t i;

    if (name == NULL) {
        return;
    }
    for (i = 0U; i + 1U < limit && i + 1U < PANIC_TASK_NAME_MAX; i++) {
        g_panic_record.task_name[i] = name[i];
        if (name[i] == '\0') {
            break;
        }
    }
    g_panic_record.task_name[i] = '\0';
}

/*
 * 码 7 的第二个来源：MSP 主栈越界（osl_boot.c 的 stack_guard_check）。
 *
 * 第一个来源 vApplicationStackOverflowHook 自己写 task / task_name；而
 * osl_panic_report() 的清理条件把码 7 归进「入口已把身份填好」那一类，因此
 * 不清任务族。这个前提对钩子成立、对 MSP 越界不成立 —— 后者在中断上下文里
 * 压穿主栈，根本拿不到任何任务身份。
 *
 * 所以这里先清、再报告，让「没有任务」本身成为一条证据（task == NULL、
 * task_name 为空串）。不清的后果就是读错方向：上一次 led_blink 栈溢出、
 * 这一次主栈压穿，现场记成「led_blink 又溢出」，task 还指向一个可能已失效的 TCB。
 *
 * 清理放在 mask 之后：BASEPRI 挡不住 HardFault / NMI，先关中断再写，
 * 两个来源之间不留可插进来的窗口（与 report 入口「先关中断再判重入」同一条约束）。
 */
void osl_panic_stack_guard_report(void)
{
    panic_mask_all_irqs();
    g_panic_record.task = NULL;
    g_panic_record.task_name[0] = '\0';
    osl_panic_report(OSL_PANIC_STACK_OVERFLOW);
}

/* configCHECK_FOR_STACK_OVERFLOW == 2 要求应用提供该钩子，否则 tasks.c 无法链接。 */
void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName)
{
    panic_mask_all_irqs();
    g_panic_record.task = xTask;
    panic_copy_task_name(pcTaskName, (uint32_t)configMAX_TASK_NAME_LEN);
    osl_panic_report(OSL_PANIC_STACK_OVERFLOW);
}

/* 任务/堆创建失败：同样要看得见，否则“什么都没发生”是唯一的症状。 */
void vApplicationMallocFailedHook(void)
{
    TaskHandle_t current;

    panic_mask_all_irqs();
    g_panic_record.code = (uint32_t)OSL_PANIC_CREATE_FAILED;
    /*
     * 码 2 是最需要“哪个任务”的场合：失败可能发生在启动期建任务，也可能是某个
     * 运行中的任务再建任务。这里**不能**直接 pcTaskGetName(NULL)——它解引用
     * pxCurrentTCB，而调度器启动前那个指针还是 NULL，会二次 fault 把现场盖掉。
     * 所以先取 handle 判空；拿不到就留空名，绝不猜。
     */
    current = xTaskGetCurrentTaskHandle();
    if (current != NULL) {
        g_panic_record.task = current;
        panic_copy_task_name(pcTaskGetName(current), (uint32_t)configMAX_TASK_NAME_LEN);
    } else {
        /*
         * 拿不到就**留空名**，不是留上一次的名字。.noinit 不清零，什么都不写等于
         * 把上一次 panic 的任务名搬进这一次的现场 —— 正是上面注释说的「指向错误的
         * 创建点」。这一条与 osl_panic_report 里按码清空互为兜底，两条都必须在。
         */
        g_panic_record.task = NULL;
        g_panic_record.task_name[0] = '\0';
    }
    osl_panic_report(OSL_PANIC_CREATE_FAILED);
}

/*
 * FreeRTOS configASSERT 与 HAL assert_param 的共用落点：只写 record、走 panic，不再调 HAL。
 * assert_failed() 在 USE_FULL_ASSERT 打开时由 HAL 驱动调用；默认宏关闭时符号仍保留，
 * 便于一键启用而不改链接。
 */
static void __attribute__((noreturn)) panic_assert_from(const char *file, uint32_t line)
{
    TaskHandle_t current;

    panic_mask_all_irqs();
    g_panic_record.assert_file = file;
    g_panic_record.assert_line = line;
    current = xTaskGetCurrentTaskHandle();
    if (current != NULL) {
        g_panic_record.task = current;
        panic_copy_task_name(pcTaskGetName(current), (uint32_t)configMAX_TASK_NAME_LEN);
    } else {
        g_panic_record.task = NULL;
        g_panic_record.task_name[0] = '\0';
    }
    osl_panic_report(OSL_PANIC_ASSERT);
}

void vAssertFailed(const char *file, uint32_t line)
{
    panic_assert_from(file, line);
}

void assert_failed(uint8_t *file, uint32_t line)
{
    panic_assert_from((const char *)(uintptr_t)file, line);
}
