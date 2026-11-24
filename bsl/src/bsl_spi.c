#include "bsl_spi.h"

#include "bsl_gpio.h"

#include <stddef.h>

/* 一份 handle 覆盖当前所有调用：本工程同时只会有一条 SPI 在跑（SPI5 的 NOR）。
 * 换总线时 s_h 按 inst 重建，不缓存第二份 —— 两条 SPI 并发读写的场景还没有
 * 消费者，现在开两张 handle 只是多占 RAM 和一条没人测的分支。 */

static SPI_HandleTypeDef s_h;
static const bsl_spi_bus_t *s_cur;
static int s_ready;

/* HAL_SPI_Init 会回调 MSP 去开时钟。这里不配引脚：引脚在 bus 里、MSP 没有 bus
 * 参数，所以引脚由 bsl_spi_init 自己配，MSP 只负责时钟。 */
void HAL_SPI_MspInit(SPI_HandleTypeDef *hspi)
{
    if (hspi == NULL) {
        return;
    }
    if (hspi->Instance == SPI1) {
        __HAL_RCC_SPI1_CLK_ENABLE();
    } else if (hspi->Instance == SPI2) {
        __HAL_RCC_SPI2_CLK_ENABLE();
    } else if (hspi->Instance == SPI3) {
        __HAL_RCC_SPI3_CLK_ENABLE();
    } else if (hspi->Instance == SPI4) {
        __HAL_RCC_SPI4_CLK_ENABLE();
    } else if (hspi->Instance == SPI5) {
        __HAL_RCC_SPI5_CLK_ENABLE();
    } else if (hspi->Instance == SPI6) {
        __HAL_RCC_SPI6_CLK_ENABLE();
    } else {
        return;
    }
}

/* 实例是否认识：认不出就不要往下走 —— 时钟没开时 HAL_SPI_Init 会写一个
 * 不供电的外设，读回来全是复位值，现场只表现为"ID 读成 0"。 */
static int spi_known(SPI_TypeDef *inst)
{
    return (inst == SPI1) || (inst == SPI2) || (inst == SPI3)
        || (inst == SPI4) || (inst == SPI5) || (inst == SPI6);
}

bsl_err_t bsl_spi_init(const bsl_spi_bus_t *bus)
{
    if ((bus == NULL) || (spi_known(bus->inst) == 0) || (bus->prescaler == 0U)) {
        return BSL_ERR_INVAL;
    }
    if (bsl_gpio_init_af(bus->sck_port, bus->sck_pin, bus->af,
                         GPIO_NOPULL, GPIO_SPEED_FREQ_VERY_HIGH) == 0) {
        return BSL_ERR_IO;
    }
    if (bsl_gpio_init_af(bus->miso_port, bus->miso_pin, bus->af,
                         GPIO_NOPULL, GPIO_SPEED_FREQ_VERY_HIGH) == 0) {
        return BSL_ERR_IO;
    }
    if (bsl_gpio_init_af(bus->mosi_port, bus->mosi_pin, bus->af,
                         GPIO_NOPULL, GPIO_SPEED_FREQ_VERY_HIGH) == 0) {
        return BSL_ERR_IO;
    }

    s_h.Instance = bus->inst;
    s_h.Init.Mode = SPI_MODE_MASTER;
    s_h.Init.Direction = SPI_DIRECTION_2LINES;
    s_h.Init.DataSize = SPI_DATASIZE_8BIT;
    s_h.Init.CLKPolarity = SPI_POLARITY_LOW;
    s_h.Init.CLKPhase = SPI_PHASE_1EDGE;
    s_h.Init.NSS = SPI_NSS_SOFT; /* CS 由 board_nor 用 GPIO 驱动 */
    s_h.Init.BaudRatePrescaler = bus->prescaler;
    s_h.Init.FirstBit = SPI_FIRSTBIT_MSB;
    s_h.Init.TIMode = SPI_TIMODE_DISABLE;
    s_h.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;
    s_h.Init.CRCPolynomial = 7U;
    if (HAL_SPI_Init(&s_h) != HAL_OK) {
        s_ready = 0;
        return BSL_ERR_IO;
    }
    s_cur = bus;
    s_ready = 1;
    return BSL_OK;
}

bsl_err_t bsl_spi_xfer(const bsl_spi_bus_t *bus, const uint8_t *tx, uint8_t *rx, uint32_t len)
{
    if ((bus == NULL) || (len == 0U)) {
        return BSL_OK;
    }
    if ((s_ready == 0) || (s_cur != bus) || (tx == NULL) || (len > 0xFFFFU)) {
        return BSL_ERR_INVAL;
    }
    /* rx 为 NULL = 只发不收（命令字节阶段）。HAL_SPI_TransmitReceive 会往 NULL
     * 写数据，所以这一支必须走 Transmit，不能靠"传个空指针"糊过去。 */
    if (rx == NULL) {
        if (HAL_SPI_Transmit(&s_h, (uint8_t *)tx, (uint16_t)len, 100U) != HAL_OK) {
            return BSL_ERR_TIMEOUT;
        }
        return BSL_OK;
    }
    if (HAL_SPI_TransmitReceive(&s_h, (uint8_t *)tx, rx, (uint16_t)len, 100U) != HAL_OK) {
        return BSL_ERR_TIMEOUT;
    }
    return BSL_OK;
}
