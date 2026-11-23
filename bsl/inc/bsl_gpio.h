#ifndef BSL_GPIO_H
#define BSL_GPIO_H

#include "stm32f4xx_hal.h"

/*
 * init 系列返回 1 = 配置已写进寄存器；0 = 端口不在 GPIOA..GPIOK 表里（NULL 或
 * 本芯片没有的端口），此时什么都没写。
 *
 * 早前全部是 void：enable_port_clock 失败后函数直接 return，调用方一律当成
 * "初始化成功" —— board_led_init / board_key_init / i2c 总线都会返回 BSL_OK，
 * 于是 bsl_init_run 记 READY、fml 照常跑，现场只表现为"这颗灯/这个键不动作"，
 * 没有任何状态位或日志能指出是引脚配错（与本文件上方 1、2 两条同族）。
 * 现在把"端口是否认得出"变成可传播的返回值，让失败在 bsl_init_run 里落成 FAILED。
 */
int bsl_gpio_port_clk_enable(GPIO_TypeDef *port);
int bsl_gpio_init_output_pp(
    GPIO_TypeDef *port,
    uint16_t pin,
    uint32_t pull,
    uint32_t speed);
int bsl_gpio_init_output_od(
    GPIO_TypeDef *port,
    uint16_t pin,
    uint32_t pull,
    uint32_t speed);
int bsl_gpio_init_input(GPIO_TypeDef *port, uint16_t pin, uint32_t pull, uint32_t speed);
void bsl_gpio_write(GPIO_TypeDef *port, uint16_t pin, GPIO_PinState state);
GPIO_PinState bsl_gpio_read(GPIO_TypeDef *port, uint16_t pin);
void bsl_gpio_write_port_pin(GPIO_TypeDef *port, uint16_t pins, GPIO_PinState state);
void bsl_gpio_toggle(GPIO_TypeDef *port, uint16_t pin);

#endif
