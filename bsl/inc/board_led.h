#ifndef BOARD_LED_H
#define BOARD_LED_H

/* 野火 F429 挑战者 V2 底板彩色灯（原理图 SENSOR/KEY/LED）。
 * 共阳经跳帽 J73 接 3.3V，阴极接 GPIO，低电平点亮。
 * PH10 红、PH11 绿、PH12 蓝。这三脚同时是摄像头 DCMI_D1/D2/D3。
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
