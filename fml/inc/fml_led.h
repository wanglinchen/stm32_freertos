#ifndef FML_LED_H
#define FML_LED_H

/* 底板 DS0/DS1：红亮绿灭与红灭绿亮每 1 s 交替（依赖 bsl board_led）。 */

void fml_led_blink_start(void);

#endif
