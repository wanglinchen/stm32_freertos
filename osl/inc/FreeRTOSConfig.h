#ifndef FREERTOS_CONFIG_H
#define FREERTOS_CONFIG_H

#include "stm32f4xx.h"

extern uint32_t SystemCoreClock;

#define configCPU_CLOCK_HZ                    SystemCoreClock
#define configTICK_RATE_HZ                    1000
#define configUSE_PREEMPTION                  1
#define configUSE_TIME_SLICING                1
#define configUSE_PORT_OPTIMISED_TASK_SELECTION 1
#define configUSE_TICKLESS_IDLE               0
/* 只用到优先级 0（idle）和 1（led_blink / button），所以上界收到 2。
 * 这个值直接决定 pxReadyTasksLists[] 的元素数（List_t 20 B/项）：7 项是 140 B，
 * 多出来的 5 项永远不会被任何任务挂上，属于看不出任何迹象的 RAM 白占。
 * 改大之前先看 verify 的 task-priority 断言——它会把"配置比用法宽"直接报红。 */
#define configMAX_PRIORITIES                  2
#define configMINIMAL_STACK_SIZE              128
/* 只装得下实际任务名：最长的是 "led_blink"（9 字符 + '\0' = 10）。
 * 这个值直接决定 `TCB_t.pcTaskName[]` 的长度，IDLE + 2 个应用任务各占一份；
 * 配到 16 就是每任务 6 B 白占，而 `size` 只报 bss 总量，看不出来。
 * 配小了不会报错，只会把名字静默截断（`vTaskList` 那条 configASSERT 路径当前没开），
 * 于是 g_panic_record.task_name 里只剩前缀。
 * 改它之前先看 verify 的 configMAX_TASK_NAME_LEN 断言：太宽、太窄都会报红。 */
#define configMAX_TASK_NAME_LEN               10
#define configTICK_TYPE_WIDTH_IN_BITS         TICK_TYPE_WIDTH_32_BITS
#define configIDLE_SHOULD_YIELD               1
#define configQUEUE_REGISTRY_SIZE             0
#define configENABLE_BACKWARD_COMPATIBILITY   0

/*
 * 软件定时器不用：自研代码里一次 xTimer* 调用都没有，开着只会让 timers.c 整个编进来——
 * 一个 timer task（configMINIMAL_STACK_SIZE 词栈 + TCB）、一个 8 项定时器队列，
 * 外加整条过期链表的维护逻辑全部白占（实测约 510 B Flash + 约 0.8 KB RAM）。
 * 上面三个 configTIMER_* 一并删掉：留着只会在关掉之后仍让人以为“有定时器在用”。
 * 哪天真需要软件定时器，改回 1 并把这三项加回来，同时删掉 verify 里
 * “软件定时器不得进镜像”那条产物断言（它会直接报红，别绕过）。
 */
#define configUSE_TIMERS                      0

#define configUSE_EVENT_GROUPS                1
#define configUSE_STREAM_BUFFERS              1

#define configSUPPORT_STATIC_ALLOCATION       0
#define configSUPPORT_DYNAMIC_ALLOCATION      1
/*
 * 32 KB 是模板默认值，本工程从没有用到过：三个任务的栈（256/128/128 词）+ 三个 TCB
 * 合计约 2.5 KB，堆上其余约 29 KB 一直空闲，却整块落在 .bss 里，把 RAM 占用虚报到
 * 17.7%（34896 B 中 32768 B 就是 ucHeap）。降到 16 KB：仍有约 6 倍余量，真调大了
 * 失败也不静默——vApplicationMallocFailedHook 走 code 2 并把现场写进 .noinit。
 * 板上用 g_heap_free_min 复核真实低水位再决定是否继续收；改这个数字必须同步
 * .skills/工程规格.md 的「两套堆的边界」表。
 */
#define configTOTAL_HEAP_SIZE                 (16 * 1024)
#define configAPPLICATION_ALLOCATED_HEAP      0

#ifdef __NVIC_PRIO_BITS
#define configPRIO_BITS                       __NVIC_PRIO_BITS
#else
#define configPRIO_BITS                       4
#endif

#define configLIBRARY_LOWEST_INTERRUPT_PRIORITY         15
#define configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY    5
#define configKERNEL_INTERRUPT_PRIORITY \
    (configLIBRARY_LOWEST_INTERRUPT_PRIORITY << (8 - configPRIO_BITS))
#define configMAX_SYSCALL_INTERRUPT_PRIORITY \
    (configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY << (8 - configPRIO_BITS))

#define configUSE_IDLE_HOOK                   0
/* tick hook 承载看门狗喂狗：tick 停了就没人喂，1 s 后复位（见 osl/inc/osl_boot.h）。 */
#define configUSE_TICK_HOOK                   1
#define configUSE_MALLOC_FAILED_HOOK          1
#define configCHECK_FOR_STACK_OVERFLOW        2

#define configUSE_TASK_NOTIFICATIONS          1
#define configUSE_MUTEXES                     1
#define configUSE_RECURSIVE_MUTEXES           1
#define configUSE_COUNTING_SEMAPHORES         1

#define INCLUDE_vTaskPrioritySet              1
#define INCLUDE_uxTaskPriorityGet             1
/*
 * 栈水位：`uxTaskGetStackHighWaterMark` 只在配置为 1 时才编进内核，
 * 但“可编”不等于“进去了”—— 没人调用时会被 --gc-sections 丢掉，
 * 于是任务栈余量在板上依然不可见（码 7 的根因只能靠猜）。
 * fml 的两个任务各自周期性采样，verify 的产物断言会要求该符号在场。
 */
#define INCLUDE_uxTaskGetStackHighWaterMark   1
#define INCLUDE_vTaskDelete                   1
#define INCLUDE_vTaskSuspend                  1
#define INCLUDE_vTaskDelayUntil               1
#define INCLUDE_vTaskDelay                    1
#define INCLUDE_xTaskGetSchedulerState        1
#define INCLUDE_xTaskGetCurrentTaskHandle     1

/*
 * 断言失败必须留下证据：调用 osl 里的 vAssertFailed 记录文件名/行号并用 LED 播报，
 * 而不是原地死循环（原地死循环在板子上只能看到“不动”）。
 */
void vAssertFailed(const char *file, uint32_t line) __attribute__((noreturn));

#define configASSERT(x)                       \
    do {                                      \
        if ((x) == 0) {                       \
            vAssertFailed(__FILE__, __LINE__); \
        }                                     \
    } while (0)

#define configCHECK_HANDLER_INSTALLATION      1

#define vPortSVCHandler     SVC_Handler
#define xPortPendSVHandler  PendSV_Handler
#define xPortSysTickHandler SysTick_Handler

#endif /* FREERTOS_CONFIG_H */
