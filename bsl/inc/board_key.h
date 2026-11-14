#ifndef BOARD_KEY_H
#define BOARD_KEY_H

/* 正点原子 APOLLO 底板按键（DNF429P V2.6 DEVICE2 / IO 引脚分配表）。
 * KEY1（板级名称 BOARD_KEY1）：PA0 = WK_UP，按下为 3.3V / 读到高。
 * KEY2：PC13，按下为 3.3V / 读到高。
 * 这里只读电平，不去抖。
 */

typedef enum {
    BOARD_KEY1 = 0,
    BOARD_KEY2,
    BOARD_KEY_COUNT
} board_key_id_t;

void board_key_init(void);

/* 按下返回 1，松开或 id 无效返回 0。 */
int board_key_pressed(board_key_id_t id);

#endif
