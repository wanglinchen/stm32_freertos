#ifndef OSL_THREAD_H
#define OSL_THREAD_H

#include <stdint.h>

typedef void (*osl_thread_fn_t)(void *argument);
typedef uint32_t osl_tick_t;

/* 与 configMINIMAL_STACK_SIZE(128) ×2 对齐，供 fml 创建任务。 */
#define OSL_THREAD_STACK_WORDS_DEFAULT 256U

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