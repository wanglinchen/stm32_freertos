#ifndef FML_LED_H
#define FML_LED_H

/* 底板 RGB 用户灯：FreeRTOS 任务内每 1s 同步亮灭翻转（依赖 bsl board_led）。 */

#include "osl_panic.h"

void fml_led_blink_start(void);

/*
 * 故障播报：转调 osl_panic_report()，三灯逐位二进制闪 4 轮 + 全亮，随后关中断、
 * 自旋，永不返回。用在调度器起不来、任务创建失败、系统时钟失败这类“本来会静默
 * 失败”的路径上。
 *
 * 为什么在这里停机而不是把“负责停机”留给调用方：调用方一旦继续往下走，
 * fml_led_blink_start 起的 blink 任务会立刻把“常亮”翻成 1 Hz 闪烁，现场唯一的
 * 故障证据就没了；在 16 MHz 降级状态下继续运行对使用者也没有任何价值。
 * 停机 + 三灯 + g_panic_record 才是证据。
 *
 * code 的取值见 osl_panic_code_t（唯一一份码表）：
 *   1 调度器启动失败 / 2 任务创建失败 / 3 configASSERT / 4 系统时钟配置失败 /
 *   5 newlib 堆耗尽 / 6 硬件异常 / 7 任务栈溢出。
 * 本层只做语义映射，播报与停机都由 osl 实现，不再有两套码表。
 */
void fml_led_panic(unsigned code) __attribute__((noreturn));

#endif
