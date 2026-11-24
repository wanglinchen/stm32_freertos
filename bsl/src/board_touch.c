#include "board_touch.h"

#include "board_rt.h"
#include "bsl_device.h"
#include "bsl_gpio.h"
#include "bsl_i2c_gpio.h"
#include "bsl_log.h"

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

/* 一条老实的诊断行：`[touch] pid raw=00 00 00 00`。
 * 一次 bsl_log_puts 写完（多调用之间会被别的任务的行插进去）。 */
static char s_pid_line[48];

static const char *pid_raw_line(const uint8_t *pid)
{
    static const char s_hex[] = "0123456789abcdef";
    const char *pfx = "[touch] pid raw=";
    unsigned k = 0U;
    unsigned i;

    while (pfx[k] != '\0') {
        s_pid_line[k] = pfx[k];
        k++;
    }
    for (i = 0U; i < 4U; i++) {
        if (i != 0U) {
            s_pid_line[k++] = ' ';
        }
        s_pid_line[k++] = s_hex[(pid[i] >> 4) & 0x0FU];
        s_pid_line[k++] = s_hex[pid[i] & 0x0FU];
    }
    s_pid_line[k++] = '\r';
    s_pid_line[k++] = '\n';
    s_pid_line[k] = '\0';
    return s_pid_line;
}

bsl_err_t board_touch_init(void)
{
    uint8_t pid[4];
    uint8_t cmd;
    uint16_t rst_mask;
    unsigned attempt;
    int got = 0;
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
    /* INT 必须在**复位释放那一刻**处于高电平：GT9xxx 用它锁存自己的 I2C 地址
     * （INT 高 → 0x14，INT 低 → 0x5D）。原先的顺序是"先释放复位、再把 INT 配成
     * 浮空输入"，于是释放瞬间 INT 还停在复位默认态，锁到的地址不确定 ——
     * 实测表现就是"有时读到 9147，有时 pid raw=0b 00 00 00"，POST 的 touch 项
     * 随机 FAIL。参考工程也是先把 INT 配成上拉、再动 RST。
     *
     * 做法：先把 INT 当推挽输出拉高并保持到释放之后，再切成浮空输入（只用来读，
     * 不用它的中断）。 */
    if (s_cfg.irq_port != NULL) {
        const uint16_t irq_mask = (uint16_t)(1U << s_cfg.irq_pin);

        if (bsl_gpio_init_output_pp(s_cfg.irq_port, irq_mask, GPIO_NOPULL,
                                    GPIO_SPEED_FREQ_LOW) == 0) {
            return BSL_ERR_IO;
        }
        bsl_gpio_write(s_cfg.irq_port, irq_mask, GPIO_PIN_SET);
    } else {
        return BSL_ERR_INVAL;
    }

    bsl_gpio_write(s_cfg.rst_port, rst_mask, rst_assert);
    HAL_Delay(10U);
    bsl_gpio_write(s_cfg.rst_port, rst_mask, rst_release);
    HAL_Delay(10U);

    /* 释放之后地址已锁存，INT 才可以放开（浮空输入：脚不能一直当输出用）。 */
    if (bsl_gpio_init_input(s_cfg.irq_port,
                            (uint16_t)(1U << s_cfg.irq_pin),
                            GPIO_NOPULL, GPIO_SPEED_FREQ_LOW) == 0) {
        return BSL_ERR_IO;
    }
    HAL_Delay(100U);

    /* PID 读失败重试 3 次、间隔 50 ms。理由：GT9147 在"MCU 热复位、触摸 IC 没断电"
     * 的场合会偶发不应答（实测 4 次连续复位里 1 次 PID 读超时 rc=-2，POST 的 touch
     * 项跟着 FAIL）。单次读失败就判整个触摸失败，等于把 POST 做成 flaky 的——
     * "有时过有时不过"比"一直不过"更难查，而松一口气又没代价：成功的第一次立即返回。
     *
     * 重试的判据是**读到了且认得**，不是"总线没报错"：IC 上电还没初始化完时，
     * 读到 4 个 0x00 / 0xFF 也是 BSL_OK —— 只看 rc 会把这种"读成功但内容是垃圾"
     * 直接判成型号不匹配，和"总线上没有这颗 IC"挤在同一行日志里。 */
    for (attempt = 0U; attempt < 3U; attempt++) {
        if (bsl_i2c_gpio_read_reg16(&s_bus, s_cfg.addr7, GT_PID_REG, pid, 4U) == BSL_OK) {
            got = 1;
            if (pid_matches(pid) != 0) {
                break;
            }
        }
        HAL_Delay(50U);
    }
    if (attempt >= 3U) {
        /* 把读到的 4 个原始字节打出来：全是 0x00（IC 没启动）、全是 0xff（SDA 悬空）
         * 与"读到一个不认识的型号"是三件不同的坏事，混成一句 rc=-2 就查不下去了。 */
        BSL_LOG_INF(pid_raw_line(pid));
        return (got != 0) ? BSL_ERR_INVAL : BSL_ERR_TIMEOUT;
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
