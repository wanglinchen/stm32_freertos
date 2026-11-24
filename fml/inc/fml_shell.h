/*
 * 串口命令行（参考工程 USMART 的重写版）。
 *
 * 为什么值得单独一套而不是"随手加几个调试打印"：本工程此前每一次验证都要靠
 * 拔掉烧写器、改代码、重烧；有了命令行，`rtc` / `temp` / `led` 这些能当场看，
 * 而它们在 POST 里也各有一条判据 —— 同一个执行器、两条入口（POST 注入 与
 * 人手敲键），不会出现"POST 通过、人敲不通过"的两套逻辑。
 *
 * 分层：fml 可以依赖 bsl / osl，所以命令处理直接调 `bsl_devapi_*`；本模块
 * **不碰硬件寄存器**，只经 bsl 的 api 与 `board_debug_uart_getc()`。
 *
 * 三条硬约束（都踩过或差点踩）：
 *   1. 输出必须"整行写完再走"：`bsl_log_puts()` 一次调用独占 UART 直到写完
 *      （内部 busy 标志 + 50 ms 超时自愈）。因此本模块把一条回复先在内存里攒
 *      成字符串，再一次性交出去；逐字符打印会被别的任务的日志撕成两半。
 *   2. 命令表是**数据**（const 表）而不是 switch：`help` 的列表与"命令数"
 *      直接从表里数出来，不会出现"表里少一个、help 也不显示"这种半隐形态。
 *   3. 执行函数与任务分离：`fml_shell_exec()` 是纯函数（给定一行文本 + 输出
 *      缓冲），POST 在调度器起来之前就能调它；`fml_shell_start()` 才建任务。
 *      两者共用同一张命令表。
 */
#ifndef FML_SHELL_H
#define FML_SHELL_H

#include <stdint.h>

#include "bsl_err.h"

#define FML_SHELL_LINE_MAX 64U
#define FML_SHELL_OUT_MAX 256U

/* 幂等。目前只重置行缓冲；命令表是 const，没有可"初始化"的状态。
 * POST 与任务都会调，所以必须幂等。 */
bsl_err_t fml_shell_init(void);

/* 命令条数（供 POST 判据与 help 行使用，不重复维护一份数字）。 */
unsigned fml_shell_cmd_count(void);

/* 执行一行（不含回车换行），把回复写进 out（NUL 结尾，首字符无前导空格）。
 * 返回 BSL_OK = 命令存在且执行器返回成功；
 *      BSL_ERR_INVAL   = 空行 / 未知命令 / 参数不合法；
 *      BSL_ERR_NOSUPPORT = 命令存在但执行器报告失败（如外设未就绪）。 */
bsl_err_t fml_shell_exec(const char *line, char *out, unsigned cap);

/* 取走串口里所有已到的字节、按行执行。须在任务上下文反复调用（本模块的
 * 任务里 10 ms 一次）。 */
void fml_shell_poll(void);

/* 建 shell 任务（`"shell"`，栈 OSL_THREAD_STACK_WORDS_DEFAULT，优先级 1 —— 与
 * app_sample 同档，低于 app_menu 的 2，保证触摸轮询不被命令行拖慢）。
 * 返回 0 = 已创建或已在运行。 */
int fml_shell_start(void);

#endif /* FML_SHELL_H */
