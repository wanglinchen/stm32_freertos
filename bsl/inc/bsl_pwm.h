#ifndef BSL_PWM_H
#define BSL_PWM_H

#include "bsl_device.h"
#include "bsl_device_api.h"

/* PWM（P9c-2）：TIM3_CH2 驱动 LCD 背光（PB5，AF2）。
 *
 * 为什么是这颗定时器 + 这根脚：参考工程 `timer.c` 的 `tim3_ch2_pwm_init()` 就把
 * 背光 PWM 放在 TIM3_CH2 / PB5 上（`timer.h` 里 `LCD_BLPWM_VAL` 直接指 `TIM3->CCR2`），
 * 板上 PB5 → 背光使能这条链路本来就是按 PWM 设计的。本工程 P7 起把 PB5 当"GPIO 开关"
 * 用，只是当时没有亮度需求；本批把控制权交回定时器，`board_lcd_backlight()`
 * 改成设占空比（详见 bsl/src/board_lcd.c）。
 *
 * 本批只做"通道 + 占空比 + 可测量"三件事：外设通道（DMA/SPI 等）不在这里开，
 * 那是别的模块的事。 */
bsl_err_t bsl_pwm_init(void);

extern const bsl_device_driver_t bsl_driver_brd_pwm;
extern const bsl_pwm_api_t bsl_api_brd_pwm;

#endif /* BSL_PWM_H */
