#include "bsl_gpio.h"

/*
 * 端口 → 时钟。返回 0 表示"这个端口不在已知表里"（NULL 或封装外端口）。
 *
 * 早前是 void 的 if / else if 链，止于 GPIOI：两个后果都是静默的 ——
 *   1. 传 NULL 时整条链都不匹配，随后 HAL_GPIO_Init(NULL, …) 写 GPIOx->MODER
 *      就是空指针 HardFault（码 6）；
 *   2. STM32F429IG（LQFP176）有 GPIOA..GPIOK，链里没有 J/K：把某颗灯或按键配到
 *      PJ/PK 时时钟不开，HAL 写配置寄存器不生效但也不报错 —— "初始化成功，
 *      脚就是不动作"，和换 UART 实例那个坑同一族。
 * 所以返回值交给调用方决定要不要继续，而不是一路写下去。
 */
static int enable_port_clock(GPIO_TypeDef *port)
{
    if (port == GPIOA) {
        __HAL_RCC_GPIOA_CLK_ENABLE();
    } else if (port == GPIOB) {
        __HAL_RCC_GPIOB_CLK_ENABLE();
    } else if (port == GPIOC) {
        __HAL_RCC_GPIOC_CLK_ENABLE();
    } else if (port == GPIOD) {
        __HAL_RCC_GPIOD_CLK_ENABLE();
    } else if (port == GPIOE) {
        __HAL_RCC_GPIOE_CLK_ENABLE();
    } else if (port == GPIOF) {
        __HAL_RCC_GPIOF_CLK_ENABLE();
    } else if (port == GPIOG) {
        __HAL_RCC_GPIOG_CLK_ENABLE();
    } else if (port == GPIOH) {
        __HAL_RCC_GPIOH_CLK_ENABLE();
    } else if (port == GPIOI) {
        __HAL_RCC_GPIOI_CLK_ENABLE();
    } else if (port == GPIOJ) {
        __HAL_RCC_GPIOJ_CLK_ENABLE();
    } else if (port == GPIOK) {
        __HAL_RCC_GPIOK_CLK_ENABLE();
    } else {
        return 0;
    }
    return 1;
}

int bsl_gpio_port_clk_enable(GPIO_TypeDef *port)
{
    return enable_port_clock(port);
}

/* 三个 init 的共同骨架：先认端口，认不出就返回 0（没有写任何寄存器），
 * 认出来才交给 HAL。返回值必须一路传到 bsl_init_run，否则"配错端口"仍然静默。 */
static int gpio_init(GPIO_TypeDef *port, uint16_t pin, uint32_t pull,
                     uint32_t speed, uint32_t mode)
{
    GPIO_InitTypeDef gpio = {0};

    if (enable_port_clock(port) == 0) {
        return 0;
    }
    gpio.Pin = pin;
    gpio.Mode = mode;
    gpio.Pull = pull;
    gpio.Speed = speed;
    HAL_GPIO_Init(port, &gpio);
    return 1;
}

int bsl_gpio_init_output_pp(
    GPIO_TypeDef *port,
    uint16_t pin,
    uint32_t pull,
    uint32_t speed)
{
    return gpio_init(port, pin, pull, speed, GPIO_MODE_OUTPUT_PP);
}

int bsl_gpio_init_output_od(
    GPIO_TypeDef *port,
    uint16_t pin,
    uint32_t pull,
    uint32_t speed)
{
    return gpio_init(port, pin, pull, speed, GPIO_MODE_OUTPUT_OD);
}

int bsl_gpio_init_input(GPIO_TypeDef *port, uint16_t pin, uint32_t pull, uint32_t speed)
{
    return gpio_init(port, pin, pull, speed, GPIO_MODE_INPUT);
}

/* 复用功能输出。与上面三个 init 同口径：端口认不出就返回 0，不留半套配置。
 * af 越界也返回 0 —— HAL 对 Alternate 是直接写 AFR 位域，越界值会静默截断。 */
int bsl_gpio_init_af(
    GPIO_TypeDef *port,
    uint16_t pin,
    uint32_t af,
    uint32_t pull,
    uint32_t speed)
{
    GPIO_InitTypeDef gpio = {0};

    if (af > 15U) {
        return 0;
    }
    if (enable_port_clock(port) == 0) {
        return 0;
    }
    gpio.Pin = pin;
    gpio.Mode = GPIO_MODE_AF_PP;
    gpio.Pull = pull;
    gpio.Speed = speed;
    gpio.Alternate = af;
    HAL_GPIO_Init(port, &gpio);
    return 1;
}

void bsl_gpio_write(GPIO_TypeDef *port, uint16_t pin, GPIO_PinState state)
{
    if (port == NULL) {
        return;
    }
    HAL_GPIO_WritePin(port, pin, state);
}

GPIO_PinState bsl_gpio_read(GPIO_TypeDef *port, uint16_t pin)
{
    if (port == NULL) {
        return GPIO_PIN_RESET;
    }
    return HAL_GPIO_ReadPin(port, pin);
}

void bsl_gpio_write_port_pin(GPIO_TypeDef *port, uint16_t pins, GPIO_PinState state)
{
    if (port == NULL) {
        return;
    }
    HAL_GPIO_WritePin(port, pins, state);
}

void bsl_gpio_toggle(GPIO_TypeDef *port, uint16_t pin)
{
    if (port == NULL) {
        return;
    }
    HAL_GPIO_TogglePin(port, pin);
}
