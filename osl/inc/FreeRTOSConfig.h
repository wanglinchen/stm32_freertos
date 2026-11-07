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
#define configMAX_PRIORITIES                  7
#define configMINIMAL_STACK_SIZE              128
#define configMAX_TASK_NAME_LEN               16
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
#define configTOTAL_HEAP_SIZE                 (32 * 1024)
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
