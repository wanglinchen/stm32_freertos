#ifndef OSL_PANIC_H
#define OSL_PANIC_H

/*
 * 统一的 panic 码表与播报入口。
 *
 * 三段位输出（PB1 红 / PB0 绿 / PB2 码位）只能表达 3 位二进制，码值空间 0..7。
 * 在此之前 osl 和 fml 各有一份码表，且 1 与 2 直接撞码：
 *   osl: 1 栈溢出 / 2 malloc 失败 / 3 assert / 5 堆耗尽 / 6 硬件异常
 *   fml: 1 调度器启动失败 / 2 任务创建失败 / 4 时钟失败
 * 现场看到"闪 1 次"根本分不出是栈溢出还是调度器起不来。
 *
 * 现在以应用语义为主：1 只表示"调度器起不来"，2 同时覆盖"任务创建失败 / FreeRTOS
 * 堆不足"，栈溢出挪到 7。osl 与 fml 都调用 osl_panic_report()，播报方式只有一种
 * （逐位二进制 ×4 轮 + 全亮收尾 + 关中断自旋），不再靠"闪法不同"区分来源。
 * 需要细分来源时读 g_panic_record（fault_kind / assert_file / task_name）。
 */
typedef enum {
    OSL_PANIC_SCHEDULER_FAILED = 1, /* 调度器启动失败（IDLE/Timer 任务创建不出来） */
    OSL_PANIC_CREATE_FAILED = 2,    /* 任务创建失败 / FreeRTOS 堆不足（含原 malloc 失败） */
    OSL_PANIC_ASSERT = 3,           /* configASSERT，文件名/行号见 g_panic_record */
    OSL_PANIC_CLOCK_FAILED = 4,     /* 系统时钟配置失败（HSE 未起振 / PLL 未锁定） */
    OSL_PANIC_HEAP_EXHAUSTED = 5,   /* newlib 的 _Min_Heap_Size 越界 */
    OSL_PANIC_HARDWARE_FAULT = 6,   /* 硬件异常，细分读 g_panic_record.fault_kind */
    OSL_PANIC_STACK_OVERFLOW = 7,   /* 栈溢出：任务栈（钩子，带任务名）或 MSP 主栈（守卫，task 为 NULL） */
} osl_panic_code_t;

/*
 * 播报：三灯逐位二进制闪 4 轮 → 全亮 → 关中断自旋，永不返回。
 * 灯脚会在播报前被配成输出，因此在 board_led_init() 之前的早期故障同样可见。
 */
void osl_panic_report(osl_panic_code_t code) __attribute__((noreturn));

#endif
