#include "board_debug_uart.h"

#include "stm32f4xx_hal.h"

static UART_HandleTypeDef s_huart_debug;

/*
 * osl 的 _write 通过 weak 钩子调到这里；勿在 bsl 里 include osl 头（分层）。
 */
int osl_debug_uart_write(const char *buf, int len);

void board_debug_uart_init(void)
{
    s_huart_debug.Instance = USART1;
    s_huart_debug.Init.BaudRate = 115200;
    s_huart_debug.Init.WordLength = UART_WORDLENGTH_8B;
    s_huart_debug.Init.StopBits = UART_STOPBITS_1;
    s_huart_debug.Init.Parity = UART_PARITY_NONE;
    s_huart_debug.Init.Mode = UART_MODE_TX_RX;
    s_huart_debug.Init.HwFlowCtl = UART_HWCONTROL_NONE;
    s_huart_debug.Init.OverSampling = UART_OVERSAMPLING_16;
    (void)HAL_UART_Init(&s_huart_debug);
}

void HAL_UART_MspInit(UART_HandleTypeDef *huart)
{
    GPIO_InitTypeDef gpio = {0};

    if (huart->Instance != USART1) {
        return;
    }

    __HAL_RCC_USART1_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();

    gpio.Pin = GPIO_PIN_9 | GPIO_PIN_10;
    gpio.Mode = GPIO_MODE_AF_PP;
    gpio.Pull = GPIO_PULLUP;
    gpio.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    gpio.Alternate = GPIO_AF7_USART1;
    HAL_GPIO_Init(GPIOA, &gpio);
}

static int board_uart_write(const char *buf, int len)
{
    if (buf == NULL || len <= 0) {
        return 0;
    }
    if (HAL_UART_Transmit(&s_huart_debug, (const uint8_t *)buf, (uint16_t)len, 100U)
        != HAL_OK) {
        return 0;
    }
    return len;
}

int osl_debug_uart_write(const char *buf, int len)
{
    return board_uart_write(buf, len);
}

void board_debug_uart_puts(const char *s)
{
    int len;

    if (s == NULL) {
        return;
    }
    for (len = 0; s[len] != '\0'; len++) {
    }
    (void)board_uart_write(s, len);
}