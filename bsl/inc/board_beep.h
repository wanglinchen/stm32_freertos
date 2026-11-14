#ifndef BOARD_BEEP_H
#define BOARD_BEEP_H

/* 正点原子 APOLLO 底板蜂鸣器（DNF429P V2.6 DEVICE2「BEEP」）：
 * PI11 → R25(1K) → Q2(SS8050) 基极；高电平导通，蜂鸣器响（与 fml_button.h / 原理图 DEVICE2 一致）。
 */

void board_beep_init(void);
void board_beep_set(int on);
void board_beep_on(void);
void board_beep_off(void);

#endif
