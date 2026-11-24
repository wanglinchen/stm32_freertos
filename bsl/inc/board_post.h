#ifndef BOARD_POST_H
#define BOARD_POST_H

/*
 * 上电自检（POST）。
 *
 * 在 bsl_board_periph_init() 之后调用：设备初始化与自检分开，这样"设备没起来"
 * 与"设备起来了但自检不过"在串口上是两件事。
 *
 * 输出格式（与历史口径一致，方便 tests 侧正则）：
 *   [post] <item> ... PASS|FAIL
 *   [post] result <total> pass <n> fail <m>
 *
 * 只碰硬件，不调 fml/apl —— 属于 bsl 层。新增一项就在本文件加一个 post_* 函数
 * 并在 board_post_run() 里加一行调用；判据必须能从返回值算出来，不靠"看起来正常"。
 *
 * 有些项的判据在上层才拿得到（比如"屏上有没有文字"要用 fml 的字库与画布，
 * 而 bsl 不能反向依赖 fml）。这类项由上层注册进来：汇总行仍留在本文件，
 * 依赖方向不变，串口上仍是同一套 [post] 格式。
 */
void board_post_run(void);

#define BOARD_POST_EXT_MAX 4U

/* 注册一个外部 POST 项，必须在 board_post_run() 之前调用。
 *
 * fn 在 "[post] <item> " 之后被调用，自己可以用 BSL_LOG_INF 在同一行打细节；
 * 返回 1 = PASS。注册超过 BOARD_POST_EXT_MAX 项、或参数为空返回 0（失败可见）。 */
int board_post_register(const char *item, int (*fn)(void));

#endif
