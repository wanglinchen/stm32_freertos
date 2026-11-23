#ifndef OSL_PANIC_INTERNAL_H
#define OSL_PANIC_INTERNAL_H

#include "osl_panic_record.h"

/* osl_boot.c（newlib _sbrk）在越界 panic 前需与 panic 入口同款关中断 */
void panic_mask_all_irqs(void);

/*
 * osl_boot.c 的 MSP 主栈越界守卫专用入口。
 *
 * 码 7 有两个来源：vApplicationStackOverflowHook（FreeRTOS 传入任务身份）与
 * MSP 越界（中断上下文，拿不到任务）。osl_panic_report 对码 7 不清任务族，
 * 前提是「入口已填好」—— 对前者成立、对后者不成立。这个入口负责先把
 * task / task_name 清掉再报告，让「没有任务」成为一条能被读到的证据。
 */
void osl_panic_stack_guard_report(void);

/*
 * 五个异常入口（定义在 osl/src/osl_panic.c，票号/名字由 startup_stm32f429xx.s
 * 的向量表按名引用）。
 *
 * 为什么在头里声明：向量表是汇编按名链接的，名字拼错时那个槽位会落到
 * Default_Handler（本工程改成 UnhandledIRQ_Handler），现场只剩"码 6，fault_kind
 * 说不清入口"，而且构建、链接、size 全都不报异常。声明放在这里之后，
 * -Wmissing-prototypes 拿回类型检查，改签名或写错名字在编译期就能看出来。
 *
 * noreturn 是事实：fault_report() → osl_panic_report() 全程不返回（关中断自旋，
 * 由 IWDG 收尾）。
 */
void HardFault_Handler(void) __attribute__((noreturn));
void MemManage_Handler(void) __attribute__((noreturn));
void BusFault_Handler(void) __attribute__((noreturn));
void UsageFault_Handler(void) __attribute__((noreturn));
void UnhandledIRQ_Handler(void) __attribute__((noreturn));

#endif
