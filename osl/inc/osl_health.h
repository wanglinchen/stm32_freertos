#ifndef OSL_HEALTH_H
#define OSL_HEALTH_H

#include <stdint.h>

/*
 * 堆与任务栈的水位记录。
 *
 * 调试输出走 USART1（CH340，见 bsl 的 board_debug_uart）；`_write` 仍抄最近 64 B
 * 进 `g_last_write` 供调试器/panic 使用。堆/栈水位本身不通过串口打印，
 * “还剩多少”仍用调试器读 RAM 变量或 panic 记录。两个值各有明确的消费方：
 *
 *   g_heap_free_min  —— FreeRTOS 堆自启动以来的历史最低剩余（字节），口径统一为
 *                       xPortGetMinimumEverFreeHeapSize()（heap_4 在每次分配后维护）。
 *                       正常运行时在 .bss 里读；跨复位 / 事故后以 g_panic_record.heap_free_min
 *                       为准（osl_panic_report 会从本变量或现场补采后落盘）。
 *                       码 2（创建失败 / 堆不足）的根因读数即此字段。
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
/* 只留实际用到的槽：0 = led_blink，1 = button。多一个槽就是 4 B 永远写不进值，
 * 而 `size` 只报 bss 总量，看不出来。verify 会比对"槽数"与"调用点用到的槽号"。 */
#define OSL_HEALTH_STACK_SLOTS 2U

/* 槽号的单一出处：调用点不再写裸数字，改槽数时只需改这里 + 上面一行。 */
#define OSL_HEALTH_SLOT_LED 0U    /* fml_led_blink_task */
#define OSL_HEALTH_SLOT_BUTTON 1U /* fml_button_task */

#define OSL_HEALTH_UNSET 0xFFFFFFFFUL

extern volatile uint32_t g_heap_free_min;
extern volatile uint32_t g_stack_free_min[OSL_HEALTH_STACK_SLOTS];

/*
 * 槽号越界的次数。调试器里看到它非 0，说明有人调用时写错了槽号——这次采样已经被
 * 丢掉，`g_stack_free_min[]` 里那个槽停在初值（看起来像"栈很富余"）。
 * 之所以要专门记一个计数：越界分支是静默 return，板上没有任何别的读数能区分
 * "槽号写错"和"这个任务栈余量一直很大"。
 */
extern volatile uint32_t g_stack_slot_err;

/* 把两个记录复位成 OSL_HEALTH_UNSET（“还没采过样”）。调度器启动前调用一次。 */
void osl_health_init(void);

/* 采一次当前任务栈余量并更新 slot 的最低值。槽号越界时只累加 g_stack_slot_err，
 * 不写数组——写错槽号必须是可读的事件，不能退化成"没有采样"。 */
void osl_health_stack_mark(uint32_t slot);

#endif
