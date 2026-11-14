#include "board_beep.h"

#include "board_dts.h"

#include "stm32f4xx_hal.h"

#if BOARD_DTS_BEEP_VIA_PCF8574

#define BEEP_BIT_MASK (1U << BOARD_DTS_PCF8574_BEEP_BIT)
#define PCF8574_ADDR_WRITE ((uint8_t)(BOARD_DTS_PCF8574_I2C_ADDR7 << 1))

static uint8_t s_pcf8574_shadow = 0xFFU;
static uint8_t s_iic_ready;

static void iic_delay(void)
{
    volatile uint32_t n = 80U;
    while (n-- > 0U) {
        (void)0;
    }
}

static void sda_out(void)
{
    GPIO_InitTypeDef gpio = {0};

    gpio.Pin = BOARD_DTS_I2C_SDA_PIN;
    gpio.Mode = GPIO_MODE_OUTPUT_OD;
    gpio.Pull = GPIO_PULLUP;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(BOARD_DTS_I2C_SDA_PORT, &gpio);
}

static void sda_high(void)
{
    HAL_GPIO_WritePin(BOARD_DTS_I2C_SDA_PORT, BOARD_DTS_I2C_SDA_PIN, GPIO_PIN_SET);
}

static void sda_low(void)
{
    HAL_GPIO_WritePin(BOARD_DTS_I2C_SDA_PORT, BOARD_DTS_I2C_SDA_PIN, GPIO_PIN_RESET);
}

static void scl_high(void)
{
    HAL_GPIO_WritePin(BOARD_DTS_I2C_SCL_PORT, BOARD_DTS_I2C_SCL_PIN, GPIO_PIN_SET);
}

static void scl_low(void)
{
    HAL_GPIO_WritePin(BOARD_DTS_I2C_SCL_PORT, BOARD_DTS_I2C_SCL_PIN, GPIO_PIN_RESET);
}

static GPIO_PinState sda_read(void)
{
    return HAL_GPIO_ReadPin(BOARD_DTS_I2C_SDA_PORT, BOARD_DTS_I2C_SDA_PIN);
}

static void iic_init_gpio(void)
{
    GPIO_InitTypeDef gpio = {0};

    BOARD_DTS_I2C_RCC_CLK_ENABLE();
    gpio.Mode = GPIO_MODE_OUTPUT_OD;
    gpio.Pull = GPIO_PULLUP;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    gpio.Pin = BOARD_DTS_I2C_SCL_PIN;
    HAL_GPIO_Init(BOARD_DTS_I2C_SCL_PORT, &gpio);
    gpio.Pin = BOARD_DTS_I2C_SDA_PIN;
    HAL_GPIO_Init(BOARD_DTS_I2C_SDA_PORT, &gpio);
    scl_high();
    sda_high();
    s_iic_ready = 1U;
}

static void iic_start(void)
{
    sda_out();
    sda_high();
    scl_high();
    iic_delay();
    sda_low();
    iic_delay();
    scl_low();
}

static void iic_stop(void)
{
    sda_out();
    scl_low();
    sda_low();
    iic_delay();
    scl_high();
    iic_delay();
    sda_high();
    iic_delay();
}

static int iic_wait_ack(void)
{
    GPIO_InitTypeDef gpio = {0};
    uint32_t timeout = 200U;

    sda_high();
    iic_delay();
    scl_high();
    iic_delay();
    gpio.Pin = BOARD_DTS_I2C_SDA_PIN;
    gpio.Mode = GPIO_MODE_INPUT;
    gpio.Pull = GPIO_PULLUP;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(BOARD_DTS_I2C_SDA_PORT, &gpio);
    while (sda_read() == GPIO_PIN_SET) {
        if (timeout-- == 0U) {
            scl_low();
            sda_out();
            iic_stop();
            return -1;
        }
    }
    scl_low();
    sda_out();
    return 0;
}

static void iic_send_byte(uint8_t byte)
{
    uint8_t i;

    sda_out();
    scl_low();
    for (i = 0U; i < 8U; i++) {
        if ((byte & 0x80U) != 0U) {
            sda_high();
        } else {
            sda_low();
        }
        byte <<= 1;
        iic_delay();
        scl_high();
        iic_delay();
        scl_low();
    }
    iic_wait_ack();
}

static void pcf8574_write(uint8_t value)
{
    if (s_iic_ready == 0U) {
        iic_init_gpio();
    }
    iic_start();
    iic_send_byte(PCF8574_ADDR_WRITE);
    iic_send_byte(value);
    iic_stop();
}

static void beep_apply_shadow(void)
{
    pcf8574_write(s_pcf8574_shadow);
}

static void beep_set_pcf8574_logical(int on)
{
    if (on != 0) {
        if (BOARD_DTS_BEEP_ACTIVE_HIGH) {
            s_pcf8574_shadow |= (uint8_t)BEEP_BIT_MASK;
        } else {
            s_pcf8574_shadow &= (uint8_t)~BEEP_BIT_MASK;
        }
    } else {
        if (BOARD_DTS_BEEP_ACTIVE_HIGH) {
            s_pcf8574_shadow &= (uint8_t)~BEEP_BIT_MASK;
        } else {
            s_pcf8574_shadow |= (uint8_t)BEEP_BIT_MASK;
        }
    }
    beep_apply_shadow();
}

void board_beep_init(void)
{
    iic_init_gpio();
    s_pcf8574_shadow = (uint8_t)(
            (BOARD_DTS_BEEP_LEVEL_IDLE == GPIO_PIN_SET) ? 0xFFU : 0x00U);
    beep_set_pcf8574_logical(0);
}

void board_beep_set(int on)
{
    beep_set_pcf8574_logical(on);
}

void board_panic_beep_silence(void)
{
    s_pcf8574_shadow |= (uint8_t)BEEP_BIT_MASK;
    beep_apply_shadow();
}

#else /* BOARD_DTS_BEEP_VIA_PCF8574 */

void board_beep_init(void)
{
    GPIO_InitTypeDef gpio = {0};

    BOARD_DTS_BEEP_RCC_CLK_ENABLE();
    HAL_GPIO_WritePin(
        BOARD_DTS_BEEP_GPIO_PORT,
        BOARD_DTS_BEEP_PIN,
        BOARD_DTS_BEEP_LEVEL_IDLE);

    gpio.Pin = BOARD_DTS_BEEP_PIN;
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(BOARD_DTS_BEEP_GPIO_PORT, &gpio);
}

void board_beep_set(int on)
{
    HAL_GPIO_WritePin(
        BOARD_DTS_BEEP_GPIO_PORT,
        BOARD_DTS_BEEP_PIN,
        on ? BOARD_DTS_BEEP_LEVEL_ON : BOARD_DTS_BEEP_LEVEL_OFF);
}

void board_panic_beep_silence(void)
{
    uint32_t bsrr;

    BOARD_DTS_BEEP_RCC_CLK_ENABLE();
    bsrr = BOARD_DTS_BEEP_ACTIVE_HIGH
            ? ((uint32_t)BOARD_DTS_BEEP_PIN << 16U)
            : (uint32_t)BOARD_DTS_BEEP_PIN;
    BOARD_DTS_BEEP_GPIO_PORT->BSRR = bsrr;
}

#endif /* BOARD_DTS_BEEP_VIA_PCF8574 */

void board_beep_on(void)
{
    board_beep_set(1);
}

void board_beep_off(void)
{
    board_beep_set(0);
}