#ifndef BSL_LOG_H
#define BSL_LOG_H

#include <stdint.h>

#define BSL_LOG_LEVEL_ERR 1U
#define BSL_LOG_LEVEL_INF 2U
#define BSL_LOG_LEVEL_DBG 3U

#ifndef BSL_LOG_LEVEL
#define BSL_LOG_LEVEL BSL_LOG_LEVEL_INF
#endif

void bsl_log_puts(const char *s);
void bsl_log_put_dec(int value);
void bsl_log_mod_puts(const char *mod, const char *s);

/* 把环形里保留的最后若干行日志倒到 UART（只给 panic 路径用）。
 * 早前日志写完即消失，panic 时"故障前发生了什么"无从得知。 */
void bsl_log_dump_ring(void);

/*
 * 启动 banner（"stm32-freertos: <实例> <波特率> debug ready"）只在这里实现。
 *
 * 文案里的实例名与波特率在运行期从 DTB 的 debug_serial 节点读（`bsl_log.c` 的
 * `debug_uart_instance_name()` 按 reg 基址反查实例名，波特率走 `board_rt_uart()`）。
 * 早前它们是 EDT 摊出来的构建期宏（`BOARD_DEBUG_UART_BANNER`），改 DTS 必须重跑
 * 生成器、C 侧另有一份拷贝；现在没有第二份。
 *
 * fml 拿不到实例名（C 没有把 `USART1` 宏字符串化的通道），所以文案留在 bsl 侧，
 * fml 只负责调一次。未就绪时不消耗这次机会：UART 没起来就什么都不打，也不记账。
 */
void bsl_log_startup_banner(void);

#if BSL_LOG_LEVEL >= BSL_LOG_LEVEL_ERR
#define BSL_LOG_ERR(msg) bsl_log_puts(msg)
#define BSL_LOG_MOD_ERR(mod, msg) bsl_log_mod_puts(mod, msg)
#else
#define BSL_LOG_ERR(msg) ((void)0)
#define BSL_LOG_MOD_ERR(mod, msg) ((void)0)
#endif

#if BSL_LOG_LEVEL >= BSL_LOG_LEVEL_INF
#define BSL_LOG_INF(msg) bsl_log_puts(msg)
#define BSL_LOG_MOD_INF(mod, msg) bsl_log_mod_puts(mod, msg)
#else
#define BSL_LOG_INF(msg) ((void)0)
#define BSL_LOG_MOD_INF(mod, msg) ((void)0)
#endif

#if BSL_LOG_LEVEL >= BSL_LOG_LEVEL_DBG
#define BSL_LOG_DBG(msg) bsl_log_puts(msg)
#define BSL_LOG_MOD_DBG(mod, msg) bsl_log_mod_puts(mod, msg)
#else
#define BSL_LOG_DBG(msg) ((void)0)
#define BSL_LOG_MOD_DBG(mod, msg) ((void)0)
#endif

#endif