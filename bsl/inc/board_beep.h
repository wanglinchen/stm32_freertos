#ifndef BOARD_BEEP_H
#define BOARD_BEEP_H

/* 野火 F429 挑战者 V2 底板蜂鸣器（原理图 SENSOR/KEY/LED）。
 * PI11 经 1 kΩ 驱动 SS8050，高电平导通，蜂鸣器响。
 */

void board_beep_init(void);
void board_beep_set(int on);
void board_beep_on(void);
void board_beep_off(void);

#endif
