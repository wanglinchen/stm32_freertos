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
 */
void board_post_run(void);

#endif
