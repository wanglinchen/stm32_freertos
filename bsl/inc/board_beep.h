#ifndef BOARD_BEEP_H
#define BOARD_BEEP_H

#include "bsl_err.h"

/* 正点原子阿波罗 V2 板载有源蜂鸣器：经 I2C 扩展 PCF8574 的 P0，PH4/PH5 软件 IIC。 */

bsl_err_t board_beep_init(void);
void board_beep_set(int on);
void board_beep_on(void);
void board_beep_off(void);

/* panic 播报前的静音钩子：调用方（osl_panic）按下述 DTS 生成的声明调用，
 * 定义在 bsl/src/board_beep.c。两条分支（PCF8574 / GPIO）都提供本符号，
 * 这里补一条 bsl 侧声明，让定义处能被编译器按原型检查（否则 -Wmissing-prototypes）。 */
void board_panic_beep_silence(void);

#endif