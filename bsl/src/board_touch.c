#include "board_touch.h"

#include "board_rt.h"
#include "bsl_device.h"
#include "bsl_gpio.h"
#include "bsl_i2c_gpio.h"

/*
 * 寄存器地址与命令都来自参考工程 gt9xxx.c / gt9xxx.h（[参考工程]）：
 *   0x8140 产品 ID（4 字节 ASCII）
 *   0x8040 控制寄存器：先写 0x02 软复位，10 ms 后写 0x00 进入读坐标
 *   0x814E 状态：bit7 = 有新数据，[3:0] = 点数（最多 max-points）
 *   0x8150 第一个触点 8 字节：track id, x_lo, x_hi, y_lo, y_hi, size..., 保留
 * 引脚（PH6 SCL / PI3 SDA / PI8 RST / PH7 INT）来自 DTB touch 节点，代码里
 * 不出现第二个 GPIOH/GPIOI。
 *
 * 与参考工程的一处偏离：不做全局 tp_dev，也不在这里做"横竖屏坐标变换"。
 * 参考工程的 [0x5510/0x9806/0x7796] 分支与 lcddev 耦合；本工程面板只有一种
 * （DTB 的 panel），坐标就是面板像素，变换留给 P4 的窗口层。
 */

#define GT_PID_REG 0x8140U
#define GT_CTRL_REG 0x8040U
#define GT_STA_REG 0x814EU
#define GT_TP1_REG 0x8150U

#define GT_CTRL_SOFT_RESET 0x02U
#define GT_CTRL_NORMAL 0x00U
#define GT_STA_NEW_DATA 0x80U
#define GT_STA_POINTS_MASK 0x0FU

static bsl_i2c_gpio_bus_t s_bus;
static board_rt_touch_t s_cfg;
static uint32_t s_x_max;
static uint32_t s_y_max;
static char s_pid[5];
static int s_ready;

/* PID 是 ASCII 的 3~4 字符：GT911→"911"、GT9147→"9147"、GT1158→"1158"、
 * GT9271→"9271"。第 4 个字节在不同型号上可能是 '7'/'8'/'1' 或 0x00，
 * 所以只比 3 位前缀，不要求整串相等 —— 早期用 strcmp 比 4 字节，
 * 换一片 ID 少一位的屏就直接判失败，而现场只看到"触摸没起来"。 */
static int pid_matches(const uint8_t *pid)
{
    static const char *const known[4] = {"911", "914", "115", "927"};
    unsigned i;

    for (i = 0U; i < 4U; i++) {
        if (pid[0] == (uint8_t)known[i][0]
            && pid[1] == (uint8_t)known[i][1]
            && pid[2] == (uint8_t)known[i][2]) {
            return 1;
        }
    }
    return 0;
}

bsl_err_t board_touch_init(void)
{
    uint8_t pid[4];
    uint8_t cmd;
    uint16_t rst_mask;
    GPIO_PinState rst_assert;
    GPIO_PinState rst_release;

    if (board_rt_touch(&s_cfg) == 0) {
        return BSL_ERR_INVAL;
    }
    /* 坐标范围取面板像素，与 panel 节点同源。 */
    {
        board_rt_panel_t panel;

        if (board_rt_panel(&panel) == 0) {
            return BSL_ERR_INVAL;
        }
        s_x_max = panel.width;
        s_y_max = panel.height;
    }
    s_bus = (bsl_i2c_gpio_bus_t){
        .scl_port = s_cfg.scl_port,
        .scl_pin = (uint16_t)(1U << s_cfg.scl_pin),
        .sda_port = s_cfg.sda_port,
        .sda_pin = (uint16_t)(1U << s_cfg.sda_pin),
    };
    if (bsl_i2c_gpio_init(&s_bus) == 0) {
        return BSL_ERR_IO;
    }

    /* 复位脚：低有效时"拉低 = 复位、拉高 = 释放"。 */
    rst_assert = (s_cfg.rst_active_low != 0) ? GPIO_PIN_RESET : GPIO_PIN_SET;
    rst_release = (rst_assert == GPIO_PIN_SET) ? GPIO_PIN_RESET : GPIO_PIN_SET;
    rst_mask = (uint16_t)(1U << s_cfg.rst_pin);
    if (bsl_gpio_init_output_pp(s_cfg.rst_port, rst_mask, GPIO_PULLUP,
                               GPIO_SPEED_FREQ_LOW) == 0) {
        return BSL_ERR_IO;
    }
    bsl_gpio_write(s_cfg.rst_port, rst_mask, rst_assert);
    HAL_Delay(10U);
    bsl_gpio_write(s_cfg.rst_port, rst_mask, rst_release);
    HAL_Delay(10U);

    /* INT 浮空输入：不接中断也能轮询，但脚不能悬着当输出用。 */
    if (s_cfg.irq_port != NULL) {
        (void)bsl_gpio_init_input(s_cfg.irq_port,
                                  (uint16_t)(1U << s_cfg.irq_pin),
                                  GPIO_NOPULL, GPIO_SPEED_FREQ_LOW);
    }
    HAL_Delay(100U);

    if (bsl_i2c_gpio_read_reg16(&s_bus, s_cfg.addr7, GT_PID_REG, pid, 4U) != BSL_OK) {
        return BSL_ERR_TIMEOUT;
    }
    if (pid_matches(pid) == 0) {
        return BSL_ERR_INVAL;
    }
    s_pid[0] = (char)pid[0];
    s_pid[1] = (char)pid[1];
    s_pid[2] = (char)pid[2];
    s_pid[3] = ((pid[3] >= 0x20U) && (pid[3] < 0x7FU)) ? (char)pid[3] : '\0';
    s_pid[4] = '\0';

    cmd = GT_CTRL_SOFT_RESET;
    if (bsl_i2c_gpio_write_reg16(&s_bus, s_cfg.addr7, GT_CTRL_REG, &cmd, 1U) != BSL_OK) {
        return BSL_ERR_TIMEOUT;
    }
    HAL_Delay(10U);
    cmd = GT_CTRL_NORMAL;
    if (bsl_i2c_gpio_write_reg16(&s_bus, s_cfg.addr7, GT_CTRL_REG, &cmd, 1U) != BSL_OK) {
        return BSL_ERR_TIMEOUT;
    }

    s_ready = 1;
    return BSL_OK;
}

unsigned board_touch_read(int32_t *x, int32_t *y)
{
    uint8_t sta = 0U;
    uint8_t buf[4];
    uint8_t zero = 0U;
    unsigned points;

    if ((s_ready == 0) || (x == NULL) || (y == NULL)) {
        return 0U;
    }
    if (bsl_i2c_gpio_read_reg16(&s_bus, s_cfg.addr7, GT_STA_REG, &sta, 1U) != BSL_OK) {
        return BOARD_TOUCH_BUS_ERROR;
    }
    points = (unsigned)(sta & GT_STA_POINTS_MASK);
    if ((sta & GT_STA_NEW_DATA) != 0U) {
        /* 必须清：不清的话 IC 认为这一帧没被取走，下一帧不再更新。 */
        (void)bsl_i2c_gpio_write_reg16(&s_bus, s_cfg.addr7, GT_STA_REG, &zero, 1U);
    }
    if ((points == 0U) || (points > s_cfg.max_points)) {
        return 0U;
    }
    if (bsl_i2c_gpio_read_reg16(&s_bus, s_cfg.addr7, GT_TP1_REG, buf, 4U) != BSL_OK) {
        return BOARD_TOUCH_BUS_ERROR;
    }
    *x = (int32_t)(((uint16_t)buf[1] << 8) | (uint16_t)buf[0]);
    *y = (int32_t)(((uint16_t)buf[3] << 8) | (uint16_t)buf[2]);
    /* 越界坐标丢弃而不是夹到边缘：复位后的头几帧会给未定标的值，
     * 夹边会让它们看起来像"点在左上角"，而实际上一次有效触摸都没有。 */
    if (((uint32_t)*x >= s_x_max) || ((uint32_t)*y >= s_y_max)) {
        return 0U;
    }
    return points;
}

const char *board_touch_product_id(void)
{
    return (s_ready != 0) ? s_pid : NULL;
}

const bsl_touch_api_t bsl_api_goodix_gt9147 = {
    .read = board_touch_read,
    .product_id = board_touch_product_id,
};

const bsl_device_driver_t bsl_driver_goodix_gt9147 = {
    .init = board_touch_init,
};
