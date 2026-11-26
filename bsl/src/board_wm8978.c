#include "board_wm8978.h"

#include <stddef.h>

#include "board_rt.h"
#include "bsl_device.h"
#include "bsl_device_api.h"
#include "bsl_i2c_gpio.h"
#include "bsl_log.h"
#include "stm32f4xx_hal.h"

/* 寄存器值逐条说明出处，避免"一串魔法数"：
 *
 * R1  0x12F  VMIDSEL=11（最快启动）、BUFIOEN=1、BIASEN=1（不置它模拟放大器不工作）、
 *            BUFDCOPEN=1（否则上电有爆音）。PLLEN=0：MCLK 由 MCU 给，codec 不用内部 PLL。
 * R2  0x1B3  LOUT1EN/ROUT1EN=1（耳机），BOOST 与 ADC 的位保持惯例值（本批不用录音）。
 * R3  0x06F  DACENL/R=1、LMIXEN/RMIXEN=1、LOUT2EN/ROUT2EN=1（喇叭）。
 * R4  0x010  FMT=10（飞利浦 I2S）、WL=00（16 位）。
 * R6  0x000  MS=0 从模式：MCLK/BCLK/LRC 全由 SAI 主机产生（SAI 侧 MCKDIV=2）。
 * R10 0x008  DACOSR128=1（最好 SNR）；SOFTMUTE=0。
 * R14 0x108  ADCOSR128=1 + HPFEN=1（录音侧，P6e 若做录音直接可用）。
 * R43 0x010  INVROUT2=1：喇叭输出反相，推喇叭的摆幅更对称。
 * R49 0x002  TSDEN=1（过热保护）；SPKBOOST=0 —— 开机那 200 ms 的自检声不该太响。
 * R50 0x001  DACL2LMIX=1：左 DAC 进左混合器。**不写这一位耳机/喇叭全程无声**。
 * R51 0x001  DACR2RMIX=1：右声道同理。
 * R52 0x11E  HPVU=1（立即生效）+ LOUT1VOL=0x1E(30)。音量取中低档：POST 会出声，
 *            但不需要吵；调音量走 board_wm8978_set_volume()/shell 的 vol 命令。
 * R53 0x11E  ROUT1VOL 同上。
 */
typedef struct {
    uint8_t reg;
    uint16_t val;
} wm8978_reg_t;

static const wm8978_reg_t k_init[] = {
    {0x00U, 0x0000U}, /* 软复位：写任意值即可；写完等 1 ms 再配其余寄存器 */
    {0x01U, 0x012FU},
    {0x02U, 0x01B3U},
    {0x03U, 0x006FU},
    {0x04U, 0x0010U},
    {0x06U, 0x0000U},
    {0x0AU, 0x0008U},
    {0x0EU, 0x0108U},
    {0x2BU, 0x0010U},
    {0x31U, 0x0002U},
    {0x32U, 0x0001U},
    {0x33U, 0x0001U},
    {0x34U, 0x011EU},
    {0x35U, 0x011EU},
};

_Static_assert((sizeof(k_init) / sizeof(k_init[0])) == BOARD_WM8978_REG_COUNT,
               "k_init[] 长度与 BOARD_WM8978_REG_COUNT 不一致");

static uint8_t s_addr7;
static unsigned s_ack;
static int s_ready;

bsl_err_t board_wm8978_init(void)
{
    const bsl_i2c_gpio_bus_t *bus;
    board_rt_sai_t cfg;
    unsigned i;

    s_ready = 0;
    s_ack = 0U;
    if (bsl_board_i2c_gpio_ready() == 0U) {
        /* 总线没起来就写不了：返回 NOINIT 而不是 IO —— "没跑到"与"写到一半失败"
         * 是两件事，现场分别指向 bsl_init 的顺序与 codec 本身。 */
        BSL_LOG_MOD_ERR("codec", "wm8978 init: i2c bus not ready\r\n");
        return BSL_ERR_NOINIT;
    }
    if (board_rt_sai(&cfg) == 0 || cfg.codec_addr7 == 0U) {
        BSL_LOG_MOD_ERR("codec", "wm8978 init: no codec node in dtb\r\n");
        return BSL_ERR_INVAL;
    }
    s_addr7 = cfg.codec_addr7;
    bus = bsl_board_i2c_gpio_bus();

    for (i = 0U; i < BOARD_WM8978_REG_COUNT; i++) {
        if (bsl_i2c_gpio_write_reg7_word9(bus, s_addr7, k_init[i].reg, k_init[i].val)
            != BSL_OK) {
            BSL_LOG_MOD_ERR("codec", "wm8978 write nack at reg 0x");
            bsl_log_put_hex(k_init[i].reg, 2U);
            BSL_LOG_ERR("\r\n");
            return BSL_ERR_IO;
        }
        s_ack++;
        if (i == 0U) {
            /* 软复位后器件需要一点时间：手册给的是"复位完成即可写"，实测 1 ms
             * 足够；不延时会在个别板子上丢掉紧跟其后的第一条电源配置。 */
            HAL_Delay(1U);
        }
    }
    s_ready = 1;
    BSL_LOG_MOD_INF("codec", "wm8978 1a regs=");
    bsl_log_put_dec((int)BOARD_WM8978_REG_COUNT);
    BSL_LOG_INF(" ack=");
    bsl_log_put_dec((int)s_ack);
    BSL_LOG_INF("\r\n");
    return BSL_OK;
}

int board_wm8978_ready(void)
{
    return s_ready;
}

unsigned board_wm8978_ack_count(void)
{
    return s_ack;
}

unsigned board_wm8978_reg_count(void)
{
    return BOARD_WM8978_REG_COUNT;
}

bsl_err_t board_wm8978_set_volume(uint8_t vol)
{
    const bsl_i2c_gpio_bus_t *bus;
    uint16_t val;

    if (vol > 63U) {
        return BSL_ERR_INVAL;
    }
    if (!s_ready) {
        return BSL_ERR_NOINIT;
    }
    bus = bsl_board_i2c_gpio_bus();
    /* HPVU=1 让 6 位音量字段立即生效（否则要等下一次零交叉，听起来像"没调"） */
    val = (uint16_t)(0x0100U | vol);
    if (bsl_i2c_gpio_write_reg7_word9(bus, s_addr7, 0x34U, val) != BSL_OK
        || bsl_i2c_gpio_write_reg7_word9(bus, s_addr7, 0x35U, val) != BSL_OK) {
        return BSL_ERR_IO;
    }
    return BSL_OK;
}

/* ---- 录音侧：ADC / 输入 PGA 使能（P9e-3 勘探） ---- */

/* 播放口径的两个值，disable 时还原用（与 k_init[] 里 R2/R44 相同；R44 播放时是 0） */
#define WM8978_R1_PLAY 0x012FU
#define WM8978_R2_PLAY 0x01B3U
#define WM8978_R44_PLAY 0x0000U

static unsigned s_adc_ack;

/* 六条写事务的语义（[参考]，见头文件的出处说明）：
 *   R2  0x1BF  = 播放值 0x1B3 | INPPGAENL/R(bit2/3)：把输入 PGA 打开
 *   R44 0x0033 = LIN/LIP/RIN/RIP 四路进 PGA（MIC 是伪差分接法，板载咪头就在这两对脚上）
 *   R45 0x1BF = 0x180（更新位）| 0x3F（左 PGA 增益最大，约 0.9 dB/step）
 *   R46 0x1BF = 同上，右声道
 *   R47 0x170 = bit8 + bits[6:4]=111：左输入 boost（[参考] 约 +6 dB，位 8 为更新/使能）
 *   R48 0x170 = 同上，右声道
 */
bsl_err_t board_wm8978_adc_enable(void)
{
    const bsl_i2c_gpio_bus_t *bus;
    static const wm8978_reg_t k_adc[] = {
        {0x01U, 0x013FU},   /* R1：播放口径 0x12F | MICBEN(bit4) —— 不给咪头偏置，
                             * 它输出就是一条直流。上板首测：不写这条时两槽恒在
                             * 0x00FF / 0x0000，pp 只有 255，正是"有器件在驱动
                             * 但没有信号"的样子。 */
        {0x02U, 0x01BFU},
        {0x2CU, 0x0033U},
        {0x2DU, 0x01BFU},
        {0x2EU, 0x01BFU},
        {0x2FU, 0x0170U},
        {0x30U, 0x0170U},
    };
    unsigned i;

    s_adc_ack = 0U;
    if (!s_ready) {
        return BSL_ERR_NOINIT;
    }
    bus = bsl_board_i2c_gpio_bus();
    for (i = 0U; i < (unsigned)(sizeof(k_adc) / sizeof(k_adc[0])); i++) {
        if (bsl_i2c_gpio_write_reg7_word9(bus, s_addr7, k_adc[i].reg, k_adc[i].val)
            != BSL_OK) {
            return BSL_ERR_IO;
        }
        s_adc_ack++;
    }
    return BSL_OK;
}

bsl_err_t board_wm8978_adc_disable(void)
{
    const bsl_i2c_gpio_bus_t *bus;

    if (!s_ready) {
        return BSL_ERR_NOINIT;
    }
    bus = bsl_board_i2c_gpio_bus();
    if (bsl_i2c_gpio_write_reg7_word9(bus, s_addr7, 0x2CU, WM8978_R44_PLAY) != BSL_OK
        || bsl_i2c_gpio_write_reg7_word9(bus, s_addr7, 0x02U, WM8978_R2_PLAY) != BSL_OK
        || bsl_i2c_gpio_write_reg7_word9(bus, s_addr7, 0x01U, WM8978_R1_PLAY) != BSL_OK) {
        return BSL_ERR_IO;
    }
    return BSL_OK;
}

unsigned board_wm8978_adc_ack(void)
{
    return s_adc_ack;
}

/* ---- 设备注册（registry 的 bsl_driver_wolfson_wm8978 / bsl_api_wolfson_wm8978） ---- */

const bsl_codec_api_t bsl_api_wolfson_wm8978 = {
    .ready = board_wm8978_ready,
    .ack_count = board_wm8978_ack_count,
    .reg_count = board_wm8978_reg_count,
    .set_volume = board_wm8978_set_volume,
};

const struct bsl_device_driver bsl_driver_wolfson_wm8978 = {
    .init = board_wm8978_init,
};
