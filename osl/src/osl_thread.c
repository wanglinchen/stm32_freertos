#include "osl_thread.h"

#include "osl_boot.h"

#include "FreeRTOS.h"
#include "task.h"

/*
 * 任务栈静态化：栈与 TCB 从 `ucHeap` 挪到 `.bss`。
 *
 * 动机：动态分配时"每个任务占多少栈"只能上板读 `uxTaskGetStackHighWaterMark`
 * 才知道，编译期和 map 都算不出来，而栈溢出恰好是这里最贵的一类问题。改成静态
 * 后 `s_task_stack` 的尺寸直接进 map，产物判据可按符号尺寸核对总量。
 *
 * 池宽固定为 `OSL_THREAD_STACK_WORDS_DEFAULT`：调用方传更大的值直接拒绝，不再
 * "按请求分配" —— 静态分配的前提就是总量在编译期定死，否则只是把
 * "运行期可能失败"藏进一个更晚的失败点。
 */
#define OSL_THREAD_MAX 2U

static StaticTask_t s_task_tcb[OSL_THREAD_MAX];
static StackType_t s_task_stack[OSL_THREAD_MAX][OSL_THREAD_STACK_WORDS_DEFAULT];
static unsigned s_task_used;

/*
 * idle 任务的静态内存：`configSUPPORT_STATIC_ALLOCATION=1` 时由内核回调索取。
 * 不给这个实现就是链接期缺符号（`xTaskCreateIdleTasks` 直接引用它）。
 * `configUSE_TIMERS=0`，所以不需要 `vApplicationGetTimerTaskMemory`。
 * 原型写在定义之前，满足 `-Wmissing-prototypes`（`osl_thread.h` 不引入
 * FreeRTOS 类型，`StaticTask_t` 在那里不可见）。
 */
static StaticTask_t s_idle_tcb;
static StackType_t s_idle_stack[configMINIMAL_STACK_SIZE];

void vApplicationGetIdleTaskMemory(
    StaticTask_t **ppxIdleTaskTCBBuffer,
    StackType_t **ppxIdleTaskStackBuffer,
    uint32_t *pulIdleTaskStackSize);

void vApplicationGetIdleTaskMemory(
    StaticTask_t **ppxIdleTaskTCBBuffer,
    StackType_t **ppxIdleTaskStackBuffer,
    uint32_t *pulIdleTaskStackSize)
{
    *ppxIdleTaskTCBBuffer = &s_idle_tcb;
    *ppxIdleTaskStackBuffer = s_idle_stack;
    *pulIdleTaskStackSize = (uint32_t)configMINIMAL_STACK_SIZE;
}

int osl_thread_create(
    osl_thread_fn_t fn,
    const char *name,
    uint32_t stack_words,
    void *arg,
    unsigned priority)
{
    if (s_task_used >= OSL_THREAD_MAX) {
        return -1;
    }
    if (stack_words > (uint32_t)OSL_THREAD_STACK_WORDS_DEFAULT) {
        return -1;
    }
    /* 下界也要判：xTaskCreateStatic 不做栈尺寸校验，传 0 或极小值会拿到一个
     * 合法 TCB + 栈指针，然后第一次压栈就越界 —— configCHECK_FOR_STACK_OVERFLOW
     * 只在切换时检查，首拍之前就已经踩坏了。idle 任务的下限就是当前的最小可信值。 */
    if (stack_words < (uint32_t)configMINIMAL_STACK_SIZE) {
        return -1;
    }
    if (xTaskCreateStatic(
            fn,
            name,
            stack_words,
            arg,
            priority,
            s_task_stack[s_task_used],
            &s_task_tcb[s_task_used]) == NULL) {
        return -1;
    }
    s_task_used++;
    return 0;
}

void osl_thread_delay_until(osl_tick_t *previous_wake, uint32_t period_ms)
{
    vTaskDelayUntil((TickType_t *)previous_wake, pdMS_TO_TICKS(period_ms));
}

void osl_thread_delay_ms(uint32_t ms)
{
    vTaskDelay(pdMS_TO_TICKS(ms));
}

osl_tick_t osl_tick_now(void)
{
    return (osl_tick_t)xTaskGetTickCount();
}

void osl_scheduler_start(void)
{
    /* 必须先对齐毫秒累加器再启动调度器：调度器一 RUNNING，HAL_GetTick() 就改走
     * g_tick_ms，而首拍对齐在那一刻还没到 —— 中间这段窗口里 HAL_GetTick() 返回
     * 0，之后的跳变会让所有跨窗口的 HAL 超时立刻成立（banner 少发 7 字节
     * 就是这么丢的，见 osl_boot.h）。 */
    osl_tick_ms_align();
    vTaskStartScheduler();
}

uint32_t osl_thread_stack_high_watermark(void)
{
    return (uint32_t)uxTaskGetStackHighWaterMark(NULL);
}
