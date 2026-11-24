#include "bsl_i2c_gpio.h"

#include "bsl_device.h"
#include "bsl_gpio.h"
#include "board_rt.h"

static void iic_delay(void)
{
    volatile uint32_t n = 80U;
    while (n-- > 0U) {
        (void)0;
    }
}

/* 返回 1 = SDA 已切到开漏输出；0 = 端口认不出（HAL 什么都没写）。 */
static int sda_out(const bsl_i2c_gpio_bus_t *bus)
{
    return bsl_gpio_init_output_od(bus->sda_port, bus->sda_pin, GPIO_PULLUP,
                                   GPIO_SPEED_FREQ_HIGH);
}

static void sda_high(const bsl_i2c_gpio_bus_t *bus)
{
    bsl_gpio_write(bus->sda_port, bus->sda_pin, GPIO_PIN_SET);
}

static void sda_low(const bsl_i2c_gpio_bus_t *bus)
{
    bsl_gpio_write(bus->sda_port, bus->sda_pin, GPIO_PIN_RESET);
}

static void scl_high(const bsl_i2c_gpio_bus_t *bus)
{
    bsl_gpio_write(bus->scl_port, bus->scl_pin, GPIO_PIN_SET);
}

static void scl_low(const bsl_i2c_gpio_bus_t *bus)
{
    bsl_gpio_write(bus->scl_port, bus->scl_pin, GPIO_PIN_RESET);
}

static GPIO_PinState sda_read(const bsl_i2c_gpio_bus_t *bus)
{
    return bsl_gpio_read(bus->sda_port, bus->sda_pin);
}

int bsl_i2c_gpio_init(const bsl_i2c_gpio_bus_t *bus)
{
    if (bus == NULL) {
        return 0;
    }
    if (bsl_gpio_init_output_od(bus->scl_port, bus->scl_pin, GPIO_PULLUP,
                                GPIO_SPEED_FREQ_HIGH) == 0) {
        return 0;
    }
    if (sda_out(bus) == 0) {
        return 0;
    }
    scl_high(bus);
    sda_high(bus);
    return 1;
}

static void iic_start(const bsl_i2c_gpio_bus_t *bus)
{
    (void)sda_out(bus);
    sda_high(bus);
    scl_high(bus);
    iic_delay();
    sda_low(bus);
    iic_delay();
    scl_low(bus);
}

static void iic_stop(const bsl_i2c_gpio_bus_t *bus)
{
    (void)sda_out(bus);
    scl_low(bus);
    sda_low(bus);
    iic_delay();
    scl_high(bus);
    iic_delay();
    sda_high(bus);
    iic_delay();
}

static bsl_err_t iic_wait_ack(const bsl_i2c_gpio_bus_t *bus)
{
    uint32_t timeout = 200U;

    sda_high(bus);
    iic_delay();
    scl_high(bus);
    iic_delay();
    (void)bsl_gpio_init_input(bus->sda_port, bus->sda_pin, GPIO_PULLUP,
                              GPIO_SPEED_FREQ_HIGH);
    while (sda_read(bus) == GPIO_PIN_SET) {
        /*
         * 每轮必须等一个单位再采样。早前这里是紧轮询：200 次迭代只花了约 30 µs
         * （每次 ≈ 25 个周期），从机把 SDA 拉低稍慢一点（总线电容大、上拉偏弱，
         * 或想 clock stretching 而本实现从不检测 SCL）就被判成 BSL_ERR_TIMEOUT，
         * 表现为蜂鸣器 / panic 静音的 I2C 写间歇失败，而代码里没有任何报错。
         * 加上延时后窗口约 400 µs；正常从机的 ACK 在第一轮就到位，不进循环，
         * 不增加正常路径的代价。
         */
        iic_delay();
        if (timeout-- == 0U) {
            scl_low(bus);
            (void)sda_out(bus);
            iic_stop(bus);
            return BSL_ERR_TIMEOUT;
        }
    }
    scl_low(bus);
    (void)sda_out(bus);
    return BSL_OK;
}

static bsl_err_t iic_send_byte(const bsl_i2c_gpio_bus_t *bus, uint8_t byte)
{
    uint8_t i;

    (void)sda_out(bus);
    scl_low(bus);
    for (i = 0U; i < 8U; i++) {
        if ((byte & 0x80U) != 0U) {
            sda_high(bus);
        } else {
            sda_low(bus);
        }
        byte <<= 1;
        iic_delay();
        scl_high(bus);
        iic_delay();
        scl_low(bus);
    }
    return iic_wait_ack(bus);
}

/* 读一个字节：SDA 切输入，取 8 位，再给第 9 个时钟的 ACK/NACK。
 * SCK 的相位与 iic_send_byte 对称：拉低 → 高电平期间采样 → 拉低。 */
static uint8_t iic_read_byte(const bsl_i2c_gpio_bus_t *bus, int ack)
{
    uint8_t i;
    uint8_t byte = 0U;

    (void)bsl_gpio_init_input(bus->sda_port, bus->sda_pin, GPIO_PULLUP,
                              GPIO_SPEED_FREQ_HIGH);
    for (i = 0U; i < 8U; i++) {
        scl_low(bus);
        iic_delay();
        scl_high(bus);
        iic_delay();
        byte = (uint8_t)(byte << 1);
        if (sda_read(bus) == GPIO_PIN_SET) {
            byte |= 1U;
        }
        scl_low(bus);
    }
    /* ACK=0 拉低，NACK=1 释放；主控读最后一个字节必须 NACK，否则从机会继续
     * 驱动 SDA，紧接着的 STOP 会被它当成数据位。 */
    (void)sda_out(bus);
    if (ack != 0) {
        sda_low(bus);
    } else {
        sda_high(bus);
    }
    iic_delay();
    scl_high(bus);
    iic_delay();
    scl_low(bus);
    (void)sda_out(bus);
    return byte;
}

bsl_err_t bsl_i2c_gpio_write_byte(const bsl_i2c_gpio_bus_t *bus, uint8_t addr7, uint8_t data)
{
    bsl_err_t rc;

    if (bus == NULL) {
        return BSL_ERR_INVAL;
    }
    iic_start(bus);
    rc = iic_send_byte(bus, (uint8_t)(addr7 << 1));
    if (rc != BSL_OK) {
        /* iic_wait_ack 的非应答出口已经发过 STOP：这里再发一次会让总线在
         * "已停"之后再收到一个 START/STOP 对，某些从机会把它识别成新事务。 */
        return rc;
    }
    rc = iic_send_byte(bus, data);
    if (rc != BSL_OK) {
        return rc;
    }
    iic_stop(bus);
    return BSL_OK;
}

bsl_err_t bsl_i2c_gpio_write_reg16(const bsl_i2c_gpio_bus_t *bus, uint8_t addr7,
                                   uint16_t reg16, const uint8_t *data, uint8_t len)
{
    bsl_err_t rc;
    uint8_t i;

    if (bus == NULL || (len > 0U && data == NULL)) {
        return BSL_ERR_INVAL;
    }
    iic_start(bus);
    rc = iic_send_byte(bus, (uint8_t)(addr7 << 1));
    if (rc != BSL_OK) {
        return rc;
    }
    rc = iic_send_byte(bus, (uint8_t)(reg16 >> 8));
    if (rc != BSL_OK) {
        return rc;
    }
    rc = iic_send_byte(bus, (uint8_t)(reg16 & 0xFFU));
    if (rc != BSL_OK) {
        return rc;
    }
    for (i = 0U; i < len; i++) {
        rc = iic_send_byte(bus, data[i]);
        if (rc != BSL_OK) {
            return rc;
        }
    }
    iic_stop(bus);
    return BSL_OK;
}

bsl_err_t bsl_i2c_gpio_read_reg16(const bsl_i2c_gpio_bus_t *bus, uint8_t addr7,
                                  uint16_t reg16, uint8_t *buf, uint8_t len)
{
    bsl_err_t rc;
    uint8_t i;

    if (bus == NULL || buf == NULL || len == 0U) {
        return BSL_ERR_INVAL;
    }
    iic_start(bus);
    rc = iic_send_byte(bus, (uint8_t)(addr7 << 1));
    if (rc != BSL_OK) {
        return rc;
    }
    rc = iic_send_byte(bus, (uint8_t)(reg16 >> 8));
    if (rc != BSL_OK) {
        return rc;
    }
    rc = iic_send_byte(bus, (uint8_t)(reg16 & 0xFFU));
    if (rc != BSL_OK) {
        return rc;
    }
    /* 重复 START 切到读方向。这里不能发 STOP 再 START：GT9xxx 只在同一次
     * 事务里认"先写地址再读"，STOP 会清掉它内部待读的地址指针。 */
    iic_start(bus);
    rc = iic_send_byte(bus, (uint8_t)((addr7 << 1) | 1U));
    if (rc != BSL_OK) {
        return rc;
    }
    for (i = 0U; i < len; i++) {
        buf[i] = iic_read_byte(bus, (i + 1U < len) ? 1 : 0);
    }
    iic_stop(bus);
    return BSL_OK;
}

bsl_err_t bsl_i2c_gpio_probe(const bsl_i2c_gpio_bus_t *bus, uint8_t addr7)
{
    bsl_err_t rc;

    if (bus == NULL) {
        return BSL_ERR_INVAL;
    }
    iic_start(bus);
    rc = iic_send_byte(bus, (uint8_t)(addr7 << 1));
    if (rc != BSL_OK) {
        /* iic_wait_ack 的非应答出口已经补过 STOP，这里不再发第二次 */
        return rc;
    }
    iic_stop(bus);
    return BSL_OK;
}

bsl_err_t bsl_i2c_gpio_read_reg8(const bsl_i2c_gpio_bus_t *bus, uint8_t addr7,
                                 uint8_t reg8, uint8_t *buf, uint8_t len)
{
    bsl_err_t rc;
    uint8_t i;

    if (bus == NULL || buf == NULL || len == 0U) {
        return BSL_ERR_INVAL;
    }
    iic_start(bus);
    rc = iic_send_byte(bus, (uint8_t)(addr7 << 1));
    if (rc != BSL_OK) {
        return rc;
    }
    rc = iic_send_byte(bus, reg8);
    if (rc != BSL_OK) {
        return rc;
    }
    /* 重复 START（不是 STOP+START）：codec 的寄存器指针只在同一次事务里保留 */
    iic_start(bus);
    rc = iic_send_byte(bus, (uint8_t)((addr7 << 1) | 1U));
    if (rc != BSL_OK) {
        return rc;
    }
    for (i = 0U; i < len; i++) {
        buf[i] = iic_read_byte(bus, (i + 1U < len) ? 1 : 0);
    }
    iic_stop(bus);
    return BSL_OK;
}

bsl_err_t bsl_i2c_gpio_write_reg8(const bsl_i2c_gpio_bus_t *bus, uint8_t addr7,
                                  uint8_t reg8, uint8_t data)
{
    bsl_err_t rc;

    if (bus == NULL) {
        return BSL_ERR_INVAL;
    }
    iic_start(bus);
    rc = iic_send_byte(bus, (uint8_t)(addr7 << 1));
    if (rc != BSL_OK) {
        return rc;
    }
    rc = iic_send_byte(bus, reg8);
    if (rc != BSL_OK) {
        return rc;
    }
    rc = iic_send_byte(bus, data);
    if (rc != BSL_OK) {
        return rc;
    }
    iic_stop(bus);
    return BSL_OK;
}

bsl_err_t bsl_i2c_gpio_write_reg7_word9(const bsl_i2c_gpio_bus_t *bus, uint8_t addr7,
                                        uint8_t reg7, uint16_t data9)
{
    bsl_err_t rc;

    if (bus == NULL || reg7 > 0x7FU || data9 > 0x1FFU) {
        return BSL_ERR_INVAL;
    }
    iic_start(bus);
    rc = iic_send_byte(bus, (uint8_t)(addr7 << 1));
    if (rc != BSL_OK) {
        return rc;
    }
    /* 控制字的第一个字节里同时装着寄存器号（高 7 位）与数据的最高位 ——
     * 这正是"9 位数据"的形态，拆成两半写会写进两个不同的寄存器。 */
    rc = iic_send_byte(bus, (uint8_t)((reg7 << 1) | (uint8_t)((data9 >> 8) & 0x01U)));
    if (rc != BSL_OK) {
        return rc;
    }
    rc = iic_send_byte(bus, (uint8_t)(data9 & 0x00FFU));
    if (rc != BSL_OK) {
        return rc;
    }
    iic_stop(bus);
    return BSL_OK;
}

static bsl_i2c_gpio_bus_t s_board_i2c_bus;
static uint8_t s_board_i2c_ready;
const bsl_i2c_gpio_bus_t *bsl_board_i2c_gpio_bus(void)
{
    return &s_board_i2c_bus;
}

uint8_t bsl_board_i2c_gpio_ready(void)
{
    return s_board_i2c_ready;
}

static bsl_err_t bsl_board_i2c_gpio_init(void)
{
    GPIO_TypeDef *scl_port;
    GPIO_TypeDef *sda_port;

    /* SCL/SDA 端口与脚号来自 DTB 的 i2c 节点（scl-gpios/sda-gpios），
     * 不再是构建期宏；节点缺失时拒绝初始化，不留半套总线。 */
    if (board_rt_i2c_ready() == 0) {
        return BSL_ERR_INVAL;
    }
    scl_port = board_rt_i2c_scl_port();
    sda_port = board_rt_i2c_sda_port();
    if (scl_port == NULL || sda_port == NULL) {
        return BSL_ERR_IO;
    }
    s_board_i2c_bus = (bsl_i2c_gpio_bus_t){
        .scl_port = scl_port,
        .scl_pin = board_rt_i2c_scl_pin(),
        .sda_port = sda_port,
        .sda_pin = board_rt_i2c_sda_pin(),
    };
    /* 开漏输出靠外部/内部上拉，端口时钟必须在第一次写之前开。端口认不出
     * （不在 GPIOA..GPIOK 表里）时这里返回失败，让 bsl_init_run 记 FAILED ——
     * 早前 enable 的返回值被丢弃，总线"初始化成功"但一脚都驱动不了。 */
    if (bsl_gpio_port_clk_enable(scl_port) == 0 ||
        bsl_gpio_port_clk_enable(sda_port) == 0) {
        return BSL_ERR_IO;
    }
    if (bsl_i2c_gpio_init(&s_board_i2c_bus) == 0) {
        return BSL_ERR_IO;
    }
    s_board_i2c_ready = 1U;
    return BSL_OK;
}

const bsl_device_driver_t bsl_driver_brd_i2c_gpio = {
    .init = bsl_board_i2c_gpio_init,
};