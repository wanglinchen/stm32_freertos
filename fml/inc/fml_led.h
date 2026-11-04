#ifndef FML_LED_H
#define FML_LED_H

/* 底板 RGB 用户灯：FreeRTOS 任务内每 1s 同步亮灭翻转（依赖 bsl board_led）。 */

void fml_led_blink_start(void);

#endif
