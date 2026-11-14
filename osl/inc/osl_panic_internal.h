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

#endif
