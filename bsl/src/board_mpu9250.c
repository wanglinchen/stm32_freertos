#include "board_mpu9250.h"

#include "board_device_registry.h"
#include "bsl_device.h"
#include "board_rt.h"
#include "bsl_i2c_gpio.h"
#include "bsl_log.h"

#include "stm32f4xx_hal.h"

/* 寄存器表来自 MPU9250 Register Map（RM-MPU-9250A-00）：
 *   PWR_MGMT_1 0x6B / PWR_MGMT_2 0x6C / CONFIG 0x1A / SMPLRT_DIV 0x19
 *   GYRO_CONFIG 0x1B / ACCEL_CONFIG 0x1C / ACCEL_CONFIG2 0x1D
 *   INT_PIN_CFG 0x37 / USER_CTRL 0x6A / WHO_AM_I 0x75
 *   数据起点 0x3B（accel 6 字节）、0x43（gyro 6 字节）、0x41（温度 2 字节）
 *   AK8963：WIA 0x00（应为 0x48）、CNTL1 0x0A */
#define MPU_REG_SMPLRT_DIV    0x19U
#define MPU_REG_CONFIG        0x1AU
#define MPU_REG_GYRO_CONFIG   0x1BU
#define MPU_REG_ACCEL_CONFIG  0x1CU
#define MPU_REG_ACCEL_CONFIG2 0x1DU
#define MPU_REG_INT_PIN_CFG   0x37U
#define MPU_REG_ACCEL_XOUT_H  0x3BU
#define MPU_REG_GYRO_XOUT_H   0x43U
#define MPU_REG_USER_CTRL     0x6AU
#define MPU_REG_PWR_MGMT_1    0x6BU
#define MPU_REG_PWR_MGMT_2    0x6CU
#define MPU_REG_WHO_AM_I      0x75U

#define MPU_WHO_AM_I_VAL 0x71U
#define MAG_WIA_VAL      0x48U
#define MAG_REG_WIA      0x00U
#define MAG_REG_CNTL1    0x0AU

/* 复位后要等 ≥100 ms 才能再访问（数据手册 tPOR）。取 100 ms。 */
#define MPU_RESET_MS 100U

static uint8_t s_addr7;
static uint8_t s_mag_addr7;
static uint8_t s_who;
static uint8_t s_mag_wia;
static int s_ready;

static bsl_err_t wr(uint8_t reg, uint8_t val)
{
    return bsl_i2c_gpio_write_reg8(bsl_board_i2c_gpio_bus(), s_addr7, reg, val);
}

bsl_err_t board_mpu9250_init(void)
{
    board_rt_mpu9250_t m;
    const bsl_i2c_gpio_bus_t *bus;
    uint8_t who = 0U;

    s_ready = 0;
    s_who = 0U;
    s_mag_wia = 0U;
    if (board_rt_mpu9250(&m) == 0) {
        BSL_LOG_MOD_ERR("imu", "no dtb node\r\n");
        return BSL_ERR_NOINIT;
    }
    bus = bsl_board_i2c_gpio_bus();
    if (bus == NULL) {
        return BSL_ERR_NOINIT;
    }
    s_addr7 = m.addr7;
    s_mag_addr7 = m.mag_addr7;

    /* 先认芯片再动配置：WHO_AM_I 不对就立刻退出，不往一颗可能不存在的芯片里写
     * 十几个寄存器 —— 写错芯片的副作用是不可预测的（比如把别的器件配成主机模式）。 */
    if (bsl_i2c_gpio_read_reg8(bus, s_addr7, MPU_REG_WHO_AM_I, &who, 1U) != BSL_OK) {
        BSL_LOG_MOD_ERR("imu", "who nack\r\n");
        return BSL_ERR_IO;
    }
    s_who = who;
    if (who != MPU_WHO_AM_I_VAL) {
        BSL_LOG_MOD_ERR("imu", "who mismatch got=");
        bsl_log_put_hex(who, 2U);
        BSL_LOG_INF("\r\n");
        return BSL_ERR_IO;
    }

    /* 复位 → 等 tPOR → 选 PLL 时钟（0x01 = auto-select PLL，比内部 8 MHz 稳）。 */
    if (wr(MPU_REG_PWR_MGMT_1, 0x80U) != BSL_OK) {
        return BSL_ERR_IO;
    }
    HAL_Delay(MPU_RESET_MS);
    if (wr(MPU_REG_PWR_MGMT_1, 0x01U) != BSL_OK || wr(MPU_REG_PWR_MGMT_2, 0x00U) != BSL_OK) {
        return BSL_ERR_IO;
    }
    /* DLPF 41 Hz + 1 kHz 采样率：50 Hz 采样下 4 点平均正好压掉板级振动噪声；
     * 这一组是参考量（应用做姿态时若要更快，改这里并同步改换算注释）。 */
    if (wr(MPU_REG_CONFIG, 0x01U) != BSL_OK || wr(MPU_REG_SMPLRT_DIV, 0x13U) != BSL_OK) {
        return BSL_ERR_IO;
    }
    /* 陀螺 ±2000 dps、加速度 ±8 g（两者都留出"甩一下也不饱和"的余量）。 */
    if (wr(MPU_REG_GYRO_CONFIG, 0x18U) != BSL_OK || wr(MPU_REG_ACCEL_CONFIG, 0x10U) != BSL_OK) {
        return BSL_ERR_IO;
    }
    if (wr(MPU_REG_ACCEL_CONFIG2, 0x01U) != BSL_OK) {
        return BSL_ERR_IO;
    }
    /* BYPASS_EN：把 AK8963 直通到主总线上（否则磁力计完全不可见）。 */
    if (wr(MPU_REG_INT_PIN_CFG, 0x02U) != BSL_OK) {
        return BSL_ERR_IO;
    }
    if (wr(MPU_REG_USER_CTRL, 0x00U) != BSL_OK) {
        return BSL_ERR_IO;
    }

    if (s_mag_addr7 != 0U) {
        uint8_t wia = 0U;
        if (bsl_i2c_gpio_read_reg8(bus, s_mag_addr7, MAG_REG_WIA, &wia, 1U) != BSL_OK) {
            /* 旁路开了仍 NACK：不判整体失败（六轴已可用），但要把 mag 记成 0
             * 让上层看得见"磁力计不可用"，而不是静默当成 0.48 读数。 */
            BSL_LOG_MOD_ERR("imu", "mag nack at ");
            bsl_log_put_hex(s_mag_addr7, 2U);
            BSL_LOG_INF("\r\n");
        } else {
            s_mag_wia = wia;
            if (wia != MAG_WIA_VAL) {
                BSL_LOG_MOD_ERR("imu", "mag wia=");
                bsl_log_put_hex(wia, 2U);
                BSL_LOG_INF("\r\n");
            } else {
                /* 连续测量模式 2（100 Hz，16 位输出）：只开模式，不读数据 ——
                 * 读磁力计数据是应用的事，本层没有消费者就不做。 */
                (void)bsl_i2c_gpio_write_reg8(bus, s_mag_addr7, MAG_REG_CNTL1, 0x16U);
            }
        }
    }

    s_ready = 1;
    BSL_LOG_MOD_INF("imu", "mpu9250 addr=");
    bsl_log_put_hex(s_addr7, 2U);
    BSL_LOG_INF(" who=71 mag=");
    bsl_log_put_hex(s_mag_wia, 2U);
    BSL_LOG_INF("\r\n");
    return BSL_OK;
}

int board_mpu9250_ready(void)
{
    return s_ready;
}

uint8_t board_mpu9250_who(void)
{
    return s_who;
}

uint8_t board_mpu9250_mag_wia(void)
{
    return s_mag_wia;
}

bsl_err_t board_mpu9250_read(int16_t acc[3], int16_t gyr[3])
{
    uint8_t raw[6];
    unsigned i;

    if (!s_ready) {
        return BSL_ERR_NOINIT;
    }
    if (acc != NULL) {
        if (bsl_i2c_gpio_read_reg8(bsl_board_i2c_gpio_bus(), s_addr7, MPU_REG_ACCEL_XOUT_H, raw, 6U) != BSL_OK) {
            return BSL_ERR_IO;
        }
        for (i = 0U; i < 3U; i++) {
            /* 高字节在前（大端），与寄存器表一致：直接 >>8 组合会得到低高颠倒的数，
             * 现场表现成"重力方向是反的"，很难归因。 */
            acc[i] = (int16_t)(((uint16_t)raw[i * 2U] << 8) | raw[i * 2U + 1U]);
        }
    }
    if (gyr != NULL) {
        if (bsl_i2c_gpio_read_reg8(bsl_board_i2c_gpio_bus(), s_addr7, MPU_REG_GYRO_XOUT_H, raw, 6U) != BSL_OK) {
            return BSL_ERR_IO;
        }
        for (i = 0U; i < 3U; i++) {
            gyr[i] = (int16_t)(((uint16_t)raw[i * 2U] << 8) | raw[i * 2U + 1U]);
        }
    }
    return BSL_OK;
}

/* ---- 设备注册（registry 的 bsl_driver_invensense_mpu9250 / bsl_api_invensense_mpu9250） ---- */

const bsl_imu_api_t bsl_api_invensense_mpu9250 = {
    .ready = board_mpu9250_ready,
    .who = board_mpu9250_who,
    .mag_wia = board_mpu9250_mag_wia,
    .read = board_mpu9250_read,
};

const struct bsl_device_driver bsl_driver_invensense_mpu9250 = {
    .init = board_mpu9250_init,
};
