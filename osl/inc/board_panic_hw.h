/* 自动生成，勿手改。刷新：python3 build/build.py update */
/* 源：device/dts/boards/apollo-f429.dts */
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
