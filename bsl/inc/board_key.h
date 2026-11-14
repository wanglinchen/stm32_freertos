#ifndef BOARD_KEY_H
#define BOARD_KEY_H

/* 正点原子 APOLLO 底板四键 + MCU 唤醒脚（DNF429P V2.6 / IO 引脚表）：
 * WK_UP PA0 按下接 3.3V（高有效）；KEY0/1/2 经板上拉、按下接 GND（低有效）；
 * KEY2 在 PC13（后备域）：先 DBP + 关 Tamper，且 PC13 不得开内部上下拉（用 NOPULL）。
 */

typedef enum {
    BOARD_WK_UP = 0,
    BOARD_KEY0,
    BOARD_KEY1,
    BOARD_KEY2,
    BOARD_KEY_COUNT
} board_key_id_t;

void board_key_init(void);

/* 按下返回 1，松开或 id 无效返回 0。 */
int board_key_pressed(board_key_id_t id);

#endif