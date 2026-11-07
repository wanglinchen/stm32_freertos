#ifndef OSL_HEALTH_H
#define OSL_HEALTH_H

#include <stdint.h>

/*
 * 堆与任务栈的水位记录。
 *
 * 这套板子上没有可用的输出通道（`_write` 是空壳、自研代码里没有任何 printf），
 * 所以“还剩多少”只能用调试器读 RAM 里的变量回答。两个值各有明确的消费方：
 *
 *   g_heap_free_min  —— FreeRTOS 堆的历史最低剩余（xPortGetFreeHeapSize）。
 *                       写进 g_panic_record.heap_free_min，于是码 2（任务创建失败 /
 *                       堆不足）在现场能直接看出“当时还剩几字节”。为 0 意味着
 *                       分配过一次刚好抽干的请求，之后任何一次创建都会失败。
 *
 *   g_stack_free_min[] —— 每个任务栈的历史最低剩余（单位：**word**，不是字节）。
 *                       0 表示栈已经用尽，1 表示只剩 4 字节。码 7（栈溢出）的
 *                       根因就在这个数：configCHECK_FOR_STACK_OVERFLOW=2 只能
 *                       事后发现越界，看不出“平时余量本来就不多”。
 *
 * 不放在 .noinit：它们描述的是“这次上电以来”的用量，复位后重新开始统计才合理
 * （跨复位保留的是 panic 现场，不是运行统计）。
 *
 * 必须在调度器启动前调 osl_health_init()：`.bss` 初值为 0，而 0 对“最低剩余”
 * 是无意义的值（每次采样都比它大，最低值永远记成 0）。
 */
#define OSL_HEALTH_STACK_SLOTS 4U
#define OSL_HEALTH_UNSET 0xFFFFFFFFUL

extern volatile uint32_t g_heap_free_min;
extern volatile uint32_t g_stack_free_min[OSL_HEALTH_STACK_SLOTS];

/* 把两个记录复位成 OSL_HEALTH_UNSET（“还没采过样”）。调度器启动前调用一次。 */
void osl_health_init(void);

/* 采一次堆余量并更新最低值，返回本次读数（字节）。任意任务上下文可调用。 */
uint32_t osl_health_heap_mark(void);

/* 采一次当前任务栈余量并更新 slot 的最低值。slot 越界直接返回，不做断言。 */
void osl_health_stack_mark(uint32_t slot);

#endif
