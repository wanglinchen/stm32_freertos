#ifndef BOARD_LED_H
#define BOARD_LED_H

/* 正点原子 APOLLO STM32F429（doc/hardware/sch/DNF429P_F767P_H743P V2.6.pdf
 * DEVICE2 / doc/hardware/pinmap/阿波罗V2 STM32F429开发板IO引脚分配表.xlsx）：
 * DS0 红 = PB1，DS1 绿 = PB0，阴极接 GPIO、阳极经 510Ω 接 3.3V，低电平点亮。
 * 板上另有 PWR 蓝灯（非 GPIO）。panic / 三段位仍占 3 bit，第三位用 PB2（BOOT1
 * 网络）仅作码位输出，不驱动板载蓝灯。
 */

typedef enum {
    BOARD_LED_R = 0,
    BOARD_LED_G,
    BOARD_LED_B,
    BOARD_LED_COUNT
} board_led_id_t;

void board_led_init(void);
void board_led_set(board_led_id_t id, int on);
void board_led_on(board_led_id_t id);
void board_led_off(board_led_id_t id);
void board_led_toggle(board_led_id_t id);

#endif
