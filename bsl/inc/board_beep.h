#ifndef BOARD_BEEP_H
#define BOARD_BEEP_H

/* 正点原子阿波罗 V2 板载有源蜂鸣器：经 I2C 扩展 PCF8574 的 P0，PH4/PH5 软件 IIC。 */

void board_beep_init(void);
void board_beep_set(int on);
void board_beep_on(void);
void board_beep_off(void);

#endif