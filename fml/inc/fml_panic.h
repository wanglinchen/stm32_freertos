#ifndef FML_PANIC_H
#define FML_PANIC_H

/*
 * 故障播报：转调 osl_panic_report()，三灯逐位二进制闪 4 轮 + 全亮，随后关中断、
 * 自旋，永不返回。用在调度器起不来、任务创建失败、系统时钟失败这类“本来会静默
 * 失败”的路径上。
 *
 * 为什么在这里停机而不是把“负责停机”留给调用方：调用方一旦继续往下走，
 * fml_led_blink_start 起的 blink 任务会立刻把默认灭灯翻成红/绿每 1 s 互斥交替，现场唯一的
 * 故障证据就没了；在 16 MHz 降级状态下继续运行对使用者也没有任何价值。
 * 停机 + 三灯 + g_panic_record 才是证据。
 *
 * 本函数与 LED 亮灭无关：三灯播报由 osl 实现，这里只做码的语义映射。早先它叫
 * fml_led_panic 并定义在 fml_led.c —— 名字带 led，函数体却一行都不碰 LED，
 * 读名字的人会以为播报里含 LED 逻辑，按键模块为了一行调用还得 include 整个
 * LED 头。名字与处所都改掉之后，“为什么停机”这条设计判断才有一个说得通的家。
 *
 * code 的取值见 osl_panic_code_t（唯一一份码表）：
 *   1 调度器启动失败 / 2 任务创建失败 / 3 configASSERT / 4 系统时钟配置失败 /
 *   5 newlib 堆耗尽 / 6 硬件异常 / 7 任务栈溢出。
 * 本层只做语义映射，播报与停机都由 osl 实现，不再有两套码表。
 */
void fml_panic(unsigned code) __attribute__((noreturn));

#endif
