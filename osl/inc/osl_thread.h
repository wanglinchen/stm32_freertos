#ifndef OSL_THREAD_H
#define OSL_THREAD_H

#include <stdint.h>

typedef void (*osl_thread_fn_t)(void *argument);
typedef uint32_t osl_tick_t;

/* 任务池的**统一池宽**（词）：池里每一格都是这么宽，调用方传更大的值直接被拒。
 * 取 512 而不是 256，是因为 `app_menu`（`apl/src/app.c` 的 `MENU_STACK_WORDS`）
 * 在 P5c 就从 256 抬到 512 了 —— 它要画桌面、跑应用回调，1 KB 不够；池宽留在 256
 * 会让 `app_start()` 建任务失败并走 `fml_panic(2U)`，屏上什么都出不来、串口只在
 * 每轮 POST 后打一段 ring 回放。池宽必须 ≥ 各任务真实最大需求，否则这个"编译期
 * 定死总量"的设计就变成了运行时随机失败。 */
#define OSL_THREAD_STACK_WORDS_DEFAULT 512U

int osl_thread_create(
    osl_thread_fn_t fn,
    const char *name,
    uint32_t stack_words,
    void *arg,
    unsigned priority);
void osl_thread_delay_until(osl_tick_t *previous_wake, uint32_t period_ms);
void osl_thread_delay_ms(uint32_t ms);
osl_tick_t osl_tick_now(void);
void osl_scheduler_start(void);

/* 当前任务栈剩余高水位（word），须在任务上下文调用。 */
uint32_t osl_thread_stack_high_watermark(void);

#endif