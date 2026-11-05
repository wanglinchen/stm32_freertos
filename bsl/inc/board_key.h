#ifndef BOARD_KEY_H
#define BOARD_KEY_H

/* 野火 F429 挑战者 V2 底板按键（原理图 SENSOR/KEY/LED）。
 * KEY1：PA0，外部 4.7K 下拉（R195），按下接 3.3V，读到高电平。可作 WKUP。
 * KEY2：PC13，外部 4.7K 下拉（R196），按下接 3.3V，读到高电平。
 *      （早期注释写成"外部上拉、按下接地"，与原理图相反，已按图更正。）
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
