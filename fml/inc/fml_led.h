#ifndef FML_LED_H
#define FML_LED_H

/* 底板 RGB 用户灯：FreeRTOS 任务内每 1s 同步亮灭翻转（依赖 bsl board_led）。 */

void fml_led_blink_start(void);

/*
 * 故障播报：三灯同步闪 code 次，随后常亮、关中断、自旋，永不返回。
 * 用在调度器起不来、任务创建失败、系统时钟失败这类“本来会静默失败”的路径上。
 *
 * 为什么在这里停机而不是把“负责停机”留给调用方：调用方一旦继续往下走，
 * fml_led_blink_start 起的 blink 任务会立刻把“常亮”翻成 1 Hz 闪烁，现场唯一的
 * 故障证据就没了；在 16 MHz 降级状态下继续运行对使用者也没有任何价值。
 * 停机 + 三灯 + g_panic_record 才是证据。
 *
 * 错误码：1 = 调度器启动失败（通常是 FreeRTOS 堆不足），2 = 任务创建失败，
 *         3 = configASSERT（由 osl 的 vAssertFailed 直接播报，不走这里），
 *         4 = 系统时钟配置失败（HSE 未起振 / PLL 未锁定）。
 *         注意 1/2 与 osl 的 panic 码（1 栈溢出 / 2 malloc 失败）语义不同，
 *         两套码表待统一，当前靠“闪法不同 + fault_kind”区分来源。
 */
void fml_led_panic(unsigned code) __attribute__((noreturn));

#endif
