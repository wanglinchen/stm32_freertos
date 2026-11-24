#ifndef BSL_SPI_H
#define BSL_SPI_H

#include "bsl_err.h"

#include "stm32f4xx_hal.h"
#include <stdint.h>

/* 参数化 SPI 总线（与 bsl_i2c_gpio 同口径：不是单例）。
 *
 * 引脚给三组 port/pin 而不是一个 mask，是因为 SPI 的三根线分别属于同一个口的
 * 不同位、而且驱动要按 SCK/MISO/MOSI 各自配 AF —— 一个 mask 无法表达"哪一位是
 * MISO"，只能靠代码里硬编码索引，换一种接法就整体错位。
 *
 * 时序固定 mode 0（CPOL=0 / CPHA=0）：W25Q 系列在 mode 0 与 mode 3 都能工作，
 * 而 mode 0 是它的默认；换器件要先用 JEDEC ID 验通，不要靠猜模式。 */
typedef struct {
    SPI_TypeDef *inst;
    GPIO_TypeDef *sck_port;
    uint16_t sck_pin;
    GPIO_TypeDef *miso_port;
    uint16_t miso_pin;
    GPIO_TypeDef *mosi_port;
    uint16_t mosi_pin;
    uint32_t af;
    uint32_t prescaler; /* SPI_BAUDRATEPRESCALER_2/4/... */
} bsl_spi_bus_t;

/* 开时钟 + 配三个引脚的 AF + HAL_SPI_Init。0 = 成功（失败即端口认不出或实例未知）。 */
bsl_err_t bsl_spi_init(const bsl_spi_bus_t *bus);

/* 全双工一次传输。tx 必须非 NULL（只收也要给 0xFF 填时钟，由调用方填）——
 * HAL 会直接解引用 tx，传 NULL 是运行期崩溃而不是返回值。
 * rx 可为 NULL，表示只发不收（命令字节阶段走 HAL_SPI_Transmit）。
 * len 上限 0xFFFF（HAL 的 Size 是 16 位）；更长请自己分段，本层不偷偷拆。 */
bsl_err_t bsl_spi_xfer(const bsl_spi_bus_t *bus, const uint8_t *tx, uint8_t *rx, uint32_t len);

#endif /* BSL_SPI_H */
