#ifndef OSL_PANIC_RECORD_H
#define OSL_PANIC_RECORD_H

#include <stdint.h>

#include "FreeRTOS.h"
#include "osl_health.h"

/* panic 记录里的任务名固定 16 B，**故意**不与 configMAX_TASK_NAME_LEN 耦合 */
#define PANIC_TASK_NAME_MAX 16U

typedef struct {
    uint32_t magic;
    uint32_t code;
    TaskHandle_t task;
    char task_name[PANIC_TASK_NAME_MAX];
    const char *assert_file;
    uint32_t assert_line;
    uint32_t fault_kind;
    uint32_t fault_cfsr;
    uint32_t fault_hfsr;
    uint32_t fault_mmfar;
    uint32_t fault_bfar;
    uint32_t fault_exc_return;
    uint32_t fault_frame_type;
    uint32_t unhandled_irq;
    uint32_t fault_sp;
    uint32_t fault_r0;
    uint32_t fault_r1;
    uint32_t fault_r2;
    uint32_t fault_r3;
    uint32_t fault_r12;
    uint32_t fault_lr;
    uint32_t fault_pc;
    uint32_t fault_xpsr;
    uint32_t seq;
    uint32_t reset_cause;
    uint32_t ipsr;
    uint32_t reset_csr;
    uint32_t wd_sync_fail;
    uint32_t heap_free_min;
    uint32_t stack_free_min[OSL_HEALTH_STACK_SLOTS];
    uint32_t stack_slot_err;
} panic_record_t;

#define PANIC_MAGIC 0x50414E43UL /* "PANC" */

extern volatile panic_record_t g_panic_record;
extern volatile char g_last_write[64];

#endif
