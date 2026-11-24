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

/* 名额 5 而不是 4：P6b 的 shell 项要注册进来（前四个是 font/fatfs/ui/menu）。
 * 加名额不是"多留余地"——每项占 4 B 指针 + 4 B 函数指针，5 项共 40 B .bss；
 * 真正的理由是 shell 的判据必须在上层（命令表和执行器都在 fml），bsl 看不见。
 * 仍然固定上限：无上限的注册表就是"谁都能塞一项"的暗门。 */
#define BOARD_POST_EXT_MAX 5U

/* 注册一个外部 POST 项，必须在 board_post_run() 之前调用。
 *
 * **fn 不写串口**：它把细节格式化进 buf（NUL 结尾），返回 1 = PASS。bsl 侧
 * 再一次性打 `[post] <item> <buf> PASS|FAIL`。
 *
 * 为什么不像内置项那样"fn 在 [post] 前缀之后自己打"：外部项的判据要注入事件、
 * 调应用的 enter/leave/event，而这些回调自己会打日志（`[app_calc] key = disp=3`）。
 * 前缀已经先打出去了，回调插进来的那行就把 `[post] menu ...` 切成两三行，
 * POST 行当场不可解析（P5a、P5b 各踩到一次）。改成"先在内存里攒结果"以后，
 * 回调日志自然排在 POST 行之前，两者互不干扰。
 *
 * 注册超过 BOARD_POST_EXT_MAX 项、或参数为空返回 0（失败可见）。
 *
 * buf 的首个字符不要写前导空格：bsl 侧在项名与 buf 之间已经补了一个，
 * 多补就是 `[post] font  c0=48` 这种双空格。cap 当前是 512（`post_ext` 内部的
 * static 缓冲，不是栈 —— main 任务栈只有 2 KB）。 */
int board_post_register(const char *item, int (*fn)(char *buf, unsigned cap));

#endif
