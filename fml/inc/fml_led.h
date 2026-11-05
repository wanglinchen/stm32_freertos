#ifndef FML_LED_H
#define FML_LED_H

/* 底板 RGB 用户灯：FreeRTOS 任务内每 1s 同步亮灭翻转（依赖 bsl board_led）。 */

void fml_led_blink_start(void);

/*
 * 故障播报：三灯同步闪 code 次后返回（调用方负责停机）。
 * 用在调度器起不来、任务创建失败这类“本来会静默失败”的路径上。
 * 错误码：1 = 调度器启动失败（通常是 FreeRTOS 堆不足），2 = LED 任务创建失败。
 */
void fml_led_panic(unsigned code);

#endif
