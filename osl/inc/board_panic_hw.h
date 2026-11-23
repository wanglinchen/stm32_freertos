/* 手写常量头（不再是构建期生成物）。
 *
 * panic 播报时栈可能已经崩了，不能再跑 FDT 遍历，也不该依赖 bsl；
 * 这里只能放编译期常量，直接写 GPIOB 的寄存器。
 *
 * 与 DTS 的一致性靠人工维护：改 project/dts 里 leds 的顺序/引脚后，
 * 必须同步本文件的 PANIC_LED_MASK / panic_led_pin，并在板上验证播报顺序。
 * 其余设备事实一律走 bsl/inc/board_rt.h（运行期读 DTB）。 */
#ifndef BOARD_PANIC_HW_H
#define BOARD_PANIC_HW_H

/* 须在包含本头之前已 include stm32f4xx_hal.h（层级上 panic 路径不入 bsl）。 */

#define PANIC_LED_GPIO GPIOB
#define PANIC_LED_MASK 0x0007U
#define PANIC_LED_MODE_MASK 0x0000003FU
#define PANIC_LED_MODE_OUT 0x00000015U
#define PANIC_LED_PIN_COUNT 3
static const uint8_t panic_led_pin[PANIC_LED_PIN_COUNT] = {1U, 0U, 2U};

#define PANIC_LED_RCC_CLK_ENABLE() __HAL_RCC_GPIOB_CLK_ENABLE()

void board_panic_beep_silence(void);

#endif /* BOARD_PANIC_HW_H */
