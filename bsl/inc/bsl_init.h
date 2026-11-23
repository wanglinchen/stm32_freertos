#ifndef BSL_INIT_H
#define BSL_INIT_H

#include "bsl_err.h"

typedef enum {
    BSL_INIT_LEVEL_POST_CLOCK = 0,
    BSL_INIT_LEVEL_BOARD,
} bsl_init_level_t;

/* 跑完某一级，返回该级里首个失败设备的错误码（全成功返回 BSL_OK）。
 * 早前是 void：某个 driver 初始化失败时调用方无法知道，只能靠它自己打日志。 */
bsl_err_t bsl_init_run(bsl_init_level_t level);

/* main 在 board_system_init 成功后调用：POST_CLOCK（UART）→ BOARD（LED/KEY/BEEP）。
 * 返回两级里"必需设备"（binding 里 `optional: false`）的首个错误码，全成功为 BSL_OK；
 * 有失败时若 debug_uart 已就绪，会在内部补一行汇总日志。
 * 日志与就绪判断都留在 bsl：放到 apl 就要 include bsl_device.h / bsl_log.h，
 * 会把 apl → bsl 的层边从 2 处变成 4 处。 */
bsl_err_t bsl_board_periph_init(void);

#endif
