#include "board_nor.h"

#include "board_rt.h"
#include "bsl_device.h"
#include "bsl_gpio.h"
#include "bsl_spi.h"

#include <stddef.h>

/* 命令集（W25Q 系列，[数据手册 §7.1]）：
 *   0x9F  JEDEC ID：3 字节，W25Q256 = EF 40 19（0x19 = 2^25 = 32 MB）
 *   0x13  Read Data with 4-Byte Address：0x13 + 4 字节地址 + 连续读
 * 不用 0x03/0x0B：那是 3 字节地址，越过 16 MB 会静默回绕到 0，读出来的不是
 * 失败而是"前半段的内容"，比读不到更难查。
 * 也不先切 4 字节地址模式（0xB7）：0x13 本身就是 4 字节地址命令，
 * 切模式只是多一个可能没生效的状态。 */
#define NOR_CMD_JEDEC 0x9FU
#define NOR_CMD_READ4B 0x13U
#define NOR_CMD_WREN 0x06U
#define NOR_CMD_RDSR1 0x05U
#define NOR_CMD_PP4B 0x12U
#define NOR_CMD_SE4B 0x21U

#define NOR_SR1_BUSY 0x01U

/* 擦/写的最长等待（W25Q256 数据手册：扇区擦典型 45 ms / 最大 400 ms；
 * 页写典型 0.4 ms / 最大 3 ms）。用断电时间而不是指令周期数：
 * 换器件或换主频时这里不需要重算。 */
#define NOR_ERASE_TIMEOUT_MS 500U
#define NOR_WRITE_TIMEOUT_MS 20U

/* 单次读的块大小：256 B = 页大小。分块重新发命令而不是靠一次连续读跨块 ——
 * 跨块要维持片选与地址自增，出错时读回的数据会整体偏移，而偏移 0 字节与
 * 偏移 1 字节在数据上几乎看不出来。 */
#define NOR_READ_CHUNK 256U

static bsl_spi_bus_t s_bus;
static board_rt_nor_t s_cfg;
static uint32_t s_jedec;
static int s_ready;

static void cs_assert(void)
{
    bsl_gpio_write(s_cfg.cs_port, (uint16_t)(1U << s_cfg.cs_pin),
                   (s_cfg.cs_active_low != 0) ? GPIO_PIN_RESET : GPIO_PIN_SET);
}

static void cs_release(void)
{
    bsl_gpio_write(s_cfg.cs_port, (uint16_t)(1U << s_cfg.cs_pin),
                   (s_cfg.cs_active_low != 0) ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

/* 片选脚的空闲电平必须先摆对再配成输出：上电默认是浮空输入，配成输出的瞬间
 * ODR 是 0 —— 对低有效的 CS 来说就是"一上电就选中"，器件会把随后的时钟当成
 * 一条命令的前半截。这一步在 bsl_gpio_init_output_pp 之前写 ODR 做不到
 * （它每次配完都会按 state 置位），所以先配、再立刻写空闲。 */
static bsl_err_t cs_init(void)
{
    if (bsl_gpio_init_output_pp(s_cfg.cs_port, (uint16_t)(1U << s_cfg.cs_pin),
                                GPIO_PULLUP, GPIO_SPEED_FREQ_VERY_HIGH) == 0) {
        return BSL_ERR_IO;
    }
    cs_release();
    return BSL_OK;
}

bsl_err_t board_nor_init(void)
{
    uint8_t cmd[4];
    uint8_t id[BOARD_NOR_JEDEC_LEN];

    if (board_rt_nor(&s_cfg) == 0) {
        return BSL_ERR_INVAL;
    }

    s_bus = (bsl_spi_bus_t){
        .inst = s_cfg.inst,
        .sck_port = s_cfg.sck_port,
        .sck_pin = (uint16_t)(1U << s_cfg.sck_pin),
        .miso_port = s_cfg.miso_port,
        .miso_pin = (uint16_t)(1U << s_cfg.miso_pin),
        .mosi_port = s_cfg.mosi_port,
        .mosi_pin = (uint16_t)(1U << s_cfg.mosi_pin),
        .af = s_cfg.af,
        /* APB2 = 84 MHz。分频 8 → 10.5 MHz：W25Q256 支持 133 MHz，但杜邦线
         * / 排线长度和 3.3 V 电平下 10 MHz 已经足够稳；再快对 FATFS 没有收益。 */
        .prescaler = SPI_BAUDRATEPRESCALER_8,
    };
    if (cs_init() != BSL_OK) {
        return BSL_ERR_IO;
    }
    if (bsl_spi_init(&s_bus) != BSL_OK) {
        return BSL_ERR_IO;
    }

    cmd[0] = NOR_CMD_JEDEC;
    cs_assert();
    if (bsl_spi_xfer(&s_bus, cmd, NULL, 1U) != BSL_OK) {
        cs_release();
        return BSL_ERR_TIMEOUT;
    }
    /* 读 3 字节要发 3 个哑元：MISO 上只有时钟才会移出数据。 */
    cmd[0] = 0xFFU;
    cmd[1] = 0xFFU;
    cmd[2] = 0xFFU;
    if (bsl_spi_xfer(&s_bus, cmd, id, BOARD_NOR_JEDEC_LEN) != BSL_OK) {
        cs_release();
        return BSL_ERR_TIMEOUT;
    }
    cs_release();

    s_jedec = ((uint32_t)id[0] << 16) | ((uint32_t)id[1] << 8) | (uint32_t)id[2];
    /* 只认 W25Q（0xEF）且容量码 ≥ 0x18：ID 读成 0x000000 / 0xFFFFFF 是"线没接
     * 或 MISO 悬空"，不是"器件型号不认识"，两者的后续处理完全不同。 */
    if ((s_jedec & 0xFF0000U) != 0xEF0000U) {
        return BSL_ERR_INVAL;
    }
    if (((s_jedec >> 8) & 0xFFU) != 0x40U) {
        return BSL_ERR_INVAL;
    }
    s_ready = 1;
    return BSL_OK;
}

uint32_t board_nor_jedec(void)
{
    return (s_ready != 0) ? s_jedec : 0U;
}

uint32_t board_nor_size(void)
{
    return (s_ready != 0) ? s_cfg.size : 0U;
}

bsl_err_t board_nor_read(uint32_t addr, void *buf, uint32_t len)
{
    uint8_t cmd[5];
    static uint8_t s_dummy[NOR_READ_CHUNK];
    uint8_t *p = (uint8_t *)buf;
    uint32_t done = 0U;
    uint32_t chunk;
    unsigned i;

    if ((s_ready == 0) || (buf == NULL) || (len == 0U)) {
        return (len == 0U) ? BSL_OK : BSL_ERR_INVAL;
    }
    if ((addr > s_cfg.size) || (len > (s_cfg.size - addr))) {
        return BSL_ERR_INVAL;
    }
    for (i = 0U; i < NOR_READ_CHUNK; i++) {
        s_dummy[i] = 0xFFU;
    }

    /* 读数据阶段要发哑元：MISO 只有收到时钟才会移出数据。哑元缓冲与命令缓冲
     * 必须分开 —— 复用 cmd 时 chunk 一超过 5 字节就越界读了栈上的内容，
     * 而 SPI 不会因此报错，数据只是错的。 */
    while (done < len) {
        chunk = len - done;
        if (chunk > NOR_READ_CHUNK) {
            chunk = NOR_READ_CHUNK;
        }
        cmd[0] = NOR_CMD_READ4B;
        cmd[1] = (uint8_t)((addr + done) >> 24);
        cmd[2] = (uint8_t)((addr + done) >> 16);
        cmd[3] = (uint8_t)((addr + done) >> 8);
        cmd[4] = (uint8_t)(addr + done);
        cs_assert();
        if (bsl_spi_xfer(&s_bus, cmd, NULL, 5U) != BSL_OK) {
            cs_release();
            return BSL_ERR_TIMEOUT;
        }
        if (bsl_spi_xfer(&s_bus, s_dummy, p + done, chunk) != BSL_OK) {
            cs_release();
            return BSL_ERR_TIMEOUT;
        }
        cs_release();
        done += chunk;
    }
    return BSL_OK;
}

static bsl_err_t wait_not_busy(uint32_t timeout_ms)
{
    uint8_t cmd = NOR_CMD_RDSR1;
    uint8_t sr = 0xFFU;
    uint32_t waited = 0U;

    while (waited <= timeout_ms) {
        cs_assert();
        if (bsl_spi_xfer(&s_bus, &cmd, NULL, 1U) != BSL_OK) {
            cs_release();
            return BSL_ERR_TIMEOUT;
        }
        if (bsl_spi_xfer(&s_bus, &cmd, &sr, 1U) != BSL_OK) {
            cs_release();
            return BSL_ERR_TIMEOUT;
        }
        cs_release();
        if ((sr & NOR_SR1_BUSY) == 0U) {
            return BSL_OK;
        }
        HAL_Delay(1U);
        waited++;
    }
    /* 超时不是"慢"，是"命令没被接受"：SR1 读回 0xFF 时 bit0 恒为 1，
     * 现场表现就是这里超时。所以返回 TIMEOUT 让上层的 FAIL 可读。 */
    return BSL_ERR_TIMEOUT;
}

static bsl_err_t write_enable(void)
{
    uint8_t cmd = NOR_CMD_WREN;

    cs_assert();
    if (bsl_spi_xfer(&s_bus, &cmd, NULL, 1U) != BSL_OK) {
        cs_release();
        return BSL_ERR_TIMEOUT;
    }
    cs_release();
    return BSL_OK;
}

bsl_err_t board_nor_erase_sector(uint32_t addr)
{
    uint8_t cmd[5];

    if (s_ready == 0) {
        return BSL_ERR_INVAL;
    }
    /* 未对齐不猜：芯片会向下取整，擦掉的是调用方没打算动的那 4 KB。 */
    if ((addr >= s_cfg.size) || ((addr % s_cfg.sector_size) != 0U)) {
        return BSL_ERR_INVAL;
    }
    if (write_enable() != BSL_OK) {
        return BSL_ERR_TIMEOUT;
    }
    cmd[0] = NOR_CMD_SE4B;
    cmd[1] = (uint8_t)(addr >> 24);
    cmd[2] = (uint8_t)(addr >> 16);
    cmd[3] = (uint8_t)(addr >> 8);
    cmd[4] = (uint8_t)addr;
    cs_assert();
    if (bsl_spi_xfer(&s_bus, cmd, NULL, 5U) != BSL_OK) {
        cs_release();
        return BSL_ERR_TIMEOUT;
    }
    cs_release();
    return wait_not_busy(NOR_ERASE_TIMEOUT_MS);
}

bsl_err_t board_nor_write(uint32_t addr, const void *buf, uint32_t len)
{
    const uint8_t *p = (const uint8_t *)buf;
    uint8_t cmd[5];
    uint32_t done = 0U;
    uint32_t chunk;
    uint32_t room;

    if ((s_ready == 0) || (buf == NULL) || (len == 0U)) {
        return (len == 0U) ? BSL_OK : BSL_ERR_INVAL;
    }
    if ((addr >= s_cfg.size) || (len > (s_cfg.size - addr))) {
        return BSL_ERR_INVAL;
    }
    /* 按页拆：页写命令内部会回卷到页首，跨页写不拆的话第二页的头会被写到
     * 第一页开头，而返回值仍是成功 —— 只有事后读回才看得出错位。 */
    while (done < len) {
        room = s_cfg.page_size - ((addr + done) % s_cfg.page_size);
        chunk = len - done;
        if (chunk > room) {
            chunk = room;
        }
        if (write_enable() != BSL_OK) {
            return BSL_ERR_TIMEOUT;
        }
        cmd[0] = NOR_CMD_PP4B;
        cmd[1] = (uint8_t)((addr + done) >> 24);
        cmd[2] = (uint8_t)((addr + done) >> 16);
        cmd[3] = (uint8_t)((addr + done) >> 8);
        cmd[4] = (uint8_t)(addr + done);
        cs_assert();
        if (bsl_spi_xfer(&s_bus, cmd, NULL, 5U) != BSL_OK) {
            cs_release();
            return BSL_ERR_TIMEOUT;
        }
        if (bsl_spi_xfer(&s_bus, p + done, NULL, chunk) != BSL_OK) {
            cs_release();
            return BSL_ERR_TIMEOUT;
        }
        cs_release();
        if (wait_not_busy(NOR_WRITE_TIMEOUT_MS) != BSL_OK) {
            return BSL_ERR_TIMEOUT;
        }
        done += chunk;
    }
    return BSL_OK;
}

uint32_t board_nor_page_size(void)
{
    return (s_ready != 0) ? s_cfg.page_size : 0U;
}

uint32_t board_nor_sector_size(void)
{
    return (s_ready != 0) ? s_cfg.sector_size : 0U;
}

const bsl_nor_api_t bsl_api_winbond_w25q256 = {
    .jedec = board_nor_jedec,
    .size = board_nor_size,
    .page_size = board_nor_page_size,
    .sector_size = board_nor_sector_size,
    .read = board_nor_read,
    .erase_sector = board_nor_erase_sector,
    .write = board_nor_write,
};

const bsl_device_driver_t bsl_driver_winbond_w25q256 = {
    .init = board_nor_init,
};
