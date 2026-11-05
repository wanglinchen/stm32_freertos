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

#define configUSE_TIMERS                      1
#define configTIMER_TASK_PRIORITY             (configMAX_PRIORITIES - 1)
#define configTIMER_TASK_STACK_DEPTH          configMINIMAL_STACK_SIZE
#define configTIMER_QUEUE_LENGTH              8

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
#define configUSE_TICK_HOOK                   0
#define configUSE_MALLOC_FAILED_HOOK          1
#define configCHECK_FOR_STACK_OVERFLOW        2

#define configUSE_TASK_NOTIFICATIONS          1
#define configUSE_MUTEXES                     1
#define configUSE_RECURSIVE_MUTEXES           1
#define configUSE_COUNTING_SEMAPHORES         1

#define INCLUDE_vTaskPrioritySet              1
#define INCLUDE_uxTaskPriorityGet             1
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
