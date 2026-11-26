#include "board_post.h"

#include <string.h>

#include "board_lcd.h"
#include "board_nt35510.h"
#include "board_rt.h"
#include "board_sdram.h"
#include "board_touch.h"
#include "board_wm8978.h"
#include "bsl_adc.h"
#include "bsl_crc.h"
#include "bsl_dac.h"
#include "bsl_device.h"
#include "bsl_log.h"
#include "bsl_rng.h"
#include "bsl_rtc.h"
#include "bsl_sai.h"
#include "stm32f4xx_hal.h"

#define POST_LOG_MOD "post"

static unsigned s_total;
static unsigned s_pass;
static unsigned s_fail;

static void post_begin(const char *item)
{
    BSL_LOG_MOD_INF(POST_LOG_MOD, item);
}

static void post_end(int ok)
{
    s_total++;
    if (ok) {
        s_pass++;
        BSL_LOG_INF(" PASS\r\n");
    } else {
        s_fail++;
        BSL_LOG_INF(" FAIL\r\n");
    }
}

/*
 * SDRAM：初始化失败与自检失败分开报。
 *   - 设备没 READY → 直接 FAIL，不调 selftest（未初始化时它返回 0xFFFFFFFF）
 *   - READY 但读回不一致 → FAIL，并把坏点数打出来；只看 PASS/FAIL 无法区分
 *     "整片不通"与"个别地址错位"。
 * 容量经 api 取（bsl_devapi_sdram），不直接调 board_sdram_size()：api 结构必须
 * 有消费者，否则 `--gc-sections` 会把 bsl_api_brd_fmc_sdram 丢掉 —— 头里声明、
 * 镜像里没有符号，就是"看起来能用"的空面接口。
 * 容量按 MB 打（(size >> 20)），32 MB 板上读 32。
 */
static void post_sdram(void)
{
    uint32_t size;
    unsigned bad;

    if (bsl_device_is_ready(BSL_DEV_SDRAM) == 0) {
        post_begin("sdram ready=0");
        post_end(0);
        return;
    }
    size = bsl_devapi_sdram()->size();
    bad = board_sdram_selftest();

    post_begin("sdram size=");
    bsl_log_put_dec((int)(size >> 20U));
    BSL_LOG_INF("M bad=");
    bsl_log_put_dec((int)bad);
    post_end((size != 0U) && (bad == 0U));
}

/*
 * LCD：初始化失败与读回不一致分开报 —— 与 sdram 同一条纪律。
 *   - 设备没 READY → 直接 FAIL，不调 selftest（未初始化时返回 0xFFFFFFFF）
 *   - READY 但自检不一致 → FAIL，并打出不一致项数
 * 判 PASS 只覆盖「面板按 DTB 描述通了」，不覆盖「面板真的亮了」：后者由屏上的
 * 三色块用眼睛验收。这就是要画色块的原因 —— 读回全对但屏全黑，说明问题在
 * 面板供电/背光/数据线，而不是配置，两者不能被同一行 PASS 混在一起。
 *
 * 两种面板形态的判据不同（由 board_lcd_selftest 内部按 bus 分派）：
 *   RGB 屏读回时序寄存器；MCU 屏（NT35510）读回的是 ID 0x5510（初始化时已比对），
 * 自检再做一次「帧缓冲一个点 → GRAM → 读回」的端到端比对。
 * 色块画完必须 present()：MCU 屏的 GRAM 不会自己跟着内存变。
 * 宽高经 api 取（bsl_devapi_panel），不直接调 board_lcd_width()：api 结构必须有
 * 消费者，否则 `--gc-sections` 会把 bsl_api_brd_panel 丢掉。
 */
static void post_lcd(void)
{
    uint32_t w;
    uint32_t h;
    uint32_t third;
    unsigned bad;

    if (bsl_device_is_ready(BSL_DEV_PANEL) == 0) {
        post_begin("lcd ready=0");
        post_end(0);
        return;
    }
    w = bsl_devapi_panel()->width();
    h = bsl_devapi_panel()->height();
    bad = board_lcd_selftest();

    /* 三色块：上红、中绿、下蓝，各占 1/3 高度。RGB565 值取自参考工程 lcd.h 的
     * RED/GREEN/BLUE 宏（0xF800 / 0x07E0 / 0x001F）。 */
    third = h / 3U;
    board_lcd_fill(0U, 0U, w, third, 0xF800U);
    board_lcd_fill(0U, third, w, third, 0x07E0U);
    board_lcd_fill(0U, third * 2U, w, h - third * 2U, 0x001FU);
    board_lcd_present_all();

    post_begin("lcd w=");
    bsl_log_put_dec((int)w);
    BSL_LOG_INF(" h=");
    bsl_log_put_dec((int)h);
    BSL_LOG_INF(" bad=");
    bsl_log_put_dec((int)bad);
    if (board_nt35510_ready() != 0) {
        BSL_LOG_INF(" id=0x");
        bsl_log_put_hex((uint32_t)board_nt35510_detected_id(), 4U);
    }
    post_end((w != 0U) && (h != 0U) && (bad == 0U));
}

/*
 * touch：静止态读一帧。
 *
 * PASS 的判据只有一条 —— 不返回 BOARD_TOUCH_BUS_ERROR，即"控件在总线上、
 * 状态寄存器 0x814E 可读"。0 点说明此刻没人按，是正常态，不能判失败：
 * POST 与手指是并发的。PID 在 init 里已比过前缀，这里打出来只为现场对 ID，
 * 不参与判据。所以这一项不假装验手指 —— 手指按下时的坐标由 sample 任务的
 * `[btn] touch down x=.. y=..` 行给出。
 */
static void post_touch(void)
{
    int32_t x = 0;
    int32_t y = 0;
    unsigned n;
    const char *pid;

    if (bsl_device_is_ready(BSL_DEV_TOUCH) == 0) {
        post_begin("touch ready=0");
        post_end(0);
        return;
    }
    pid = bsl_devapi_touch()->product_id();
    n = bsl_devapi_touch()->read(&x, &y);
    post_begin("touch id=");
    BSL_LOG_INF((pid != NULL) ? pid : "?");
    BSL_LOG_INF(" points=");
    bsl_log_put_dec((n == BOARD_TOUCH_BUS_ERROR) ? -1 : (int)n);
    if ((n != BOARD_TOUCH_BUS_ERROR) && (n > 0U)) {
        BSL_LOG_INF(" x=");
        bsl_log_put_dec((int)x);
        BSL_LOG_INF(" y=");
        bsl_log_put_dec((int)y);
    }
    post_end((n != BOARD_TOUCH_BUS_ERROR) ? 1 : 0);
}

/*
 * nor：读 3 字节 JEDEC ID。
 *
 * PASS 判据是"ID 等于 0xEF4019 且容量等于 32 MB"：0xEF = Winbond，
 * 0x40 = SPI NOR 容量族，0x19 = 2^25 = 32 MB。ID 读成 0x000000 / 0xFFFFFF
 * 是 MISO 悬空或 CS 没动，与"型号不对"是两件事 —— 这里只把两者都判 FAIL，
 * 具体是哪一种由 board_nor_init 的返回值在 bsl_init 的 FAILED 状态里区分。
 *
 * 不做读写一致性：写要先擦扇区，POST 阶段擦掉板上已有的数据是不可接受的副作用。
 */
static void post_nor(void)
{
    uint32_t jedec;
    uint32_t size;
    const bsl_nor_api_t *nor;

    if (bsl_device_is_ready(BSL_DEV_NOR) == 0) {
        post_begin("nor ready=0");
        post_end(0);
        return;
    }
    nor = bsl_devapi_nor();
    jedec = nor->jedec();
    size = nor->size();
    post_begin("nor id=0x");
    bsl_log_put_hex(jedec, 6U);
    BSL_LOG_INF(" size=");
    bsl_log_put_dec((int)(size >> 20));
    BSL_LOG_INF("M");
    post_end(((jedec == 0xEF4019U) && (size == 0x02000000U)) ? 1 : 0);
}

/* ---- 定宽/定点打印小工具 --------------------------------------------------
 *
 * bsl_log 只有 put_dec（不留前导零），而 RTC/温度这些值的可读性全在定宽上：
 * `2026-11-24 9:5:3` 读起来像坏数据，`09:05:03` 才是现场能直接对的东西。
 * 三个小函数故意留在本文件：它们只服务 POST 行，进 bsl_log 会让"日志层"承担
 * 数值格式化，而 fml 的 app_fmt_* 已经有另一套（两套并存的代价更大）。 */
static void post_put_2(unsigned v)
{
    if (v < 10U) {
        bsl_log_put_dec(0);
    }
    bsl_log_put_dec((int)v);
}

/* 定点 ×10：31.2 / -0.5。负数单独处理符号，避免 -3/10 = 0 的截断把 0.5 打成 0 */
static void post_put_x10(int32_t v)
{
    if (v < 0) {
        bsl_log_puts("-");
        v = -v;
    }
    bsl_log_put_dec((int)(v / 10));
    bsl_log_puts(".");
    bsl_log_put_dec((int)(v % 10));
}

/*
 * rtc：RTC 走时。
 *
 * 判据三条，缺一条都不算过：
 *   1) init 成功（bsl_rtc_ready）—— LSE 与 LSI 都不起振时 init 才会失败；
 *   2) 日历字段合法（bsl_rtc_get 自己就拒非法 BCD，这里不重复验证）；
 *   3) 真的在走 —— 等约 1.1 s 再读一次，秒差必须落在 1..3。
 * 第 3 条是把"寄存器能读"与"晶振在振"分开的唯一办法：RTC 停振时日历寄存器
 * 照样保持上次的值，只读一次永远 PASS。
 *
 * 时间起点是**编译时刻**（本机没有墙钟来源，见 bsl_rtc.c 注释），所以这一行
 * 里的日期是"固件被编译的那天"，不是"现在"。src=lsi 表示 LSE 没起振、已降级，
 * 走时精度掉到分钟级 —— 这是已知代价，不是错误，但必须打印出来。
 */
static void post_rtc(void)
{
    const bsl_rtc_api_t *rtc = bsl_devapi_rtc();
    bsl_rtc_time_t t;
    int delta = 0;

    post_begin("rtc ");
    if (rtc->ready() == 0) {
        BSL_LOG_INF("ready=0");
        post_end(0);
        return;
    }
    if (bsl_rtc_measure(1100U, &t, &delta) != BSL_OK) {
        BSL_LOG_INF("read=err");
        post_end(0);
        return;
    }
    /* 冷启动时 LSE 起振要 1~2 s，第一次 1.1 s 窗口内可能一秒都没走（delta=0），
     * 复位后同一项就 PASS。POST 必须每次复位同结果（stability 要求三轮一致），
     * 所以 delta=0 时再测一次，而不是把一次偶发当成"RTC 坏"。 */
    if (delta < 1) {
        if (bsl_rtc_measure(1100U, &t, &delta) != BSL_OK) {
            BSL_LOG_INF("read=err");
            post_end(0);
            return;
        }
    }

    BSL_LOG_INF("src=");
    bsl_log_puts((rtc->clock_source() == BSL_RTC_SRC_LSE) ? "lse" : "lsi");
    BSL_LOG_INF(" ");
    bsl_log_put_dec((int)t.year);
    BSL_LOG_INF("-");
    post_put_2(t.month);
    BSL_LOG_INF("-");
    post_put_2(t.day);
    BSL_LOG_INF(" ");
    post_put_2(t.hour);
    BSL_LOG_INF(":");
    post_put_2(t.minute);
    BSL_LOG_INF(":");
    post_put_2(t.second);
    BSL_LOG_INF(" walk=");
    bsl_log_put_dec(delta);
    BSL_LOG_INF("s");
    post_end(((delta >= 1) && (delta <= 3)) ? 1 : 0);
}

/*
 * adc：内部温度通道。
 *
 * 判 PASS 的条件是**换算可信**（cal_valid = 标定点自洽且 VREF+ 在窗口内）
 * 加一个物理区间 [-40, 125] °C。不判"温度等于室温"：板子刚上电、机箱里的
 * 实际温度未知，用室温当判据只会得到一个假 FAIL。这里验的是"通道通、校准
 * 值读得到、换算走得通"，趋势对不对由人手摸板子对一下即可。
 */
static void post_adc(void)
{
    bsl_adc_temp_t s;
    bsl_err_t err;

    post_begin("adc ");
    err = bsl_adc_temp_init();
    if (err != BSL_OK) {
        BSL_LOG_INF("init=");
        bsl_log_put_dec((int)err);
        post_end(0);
        return;
    }
    err = bsl_adc_temp_read(&s);

    BSL_LOG_INF("raw=");
    bsl_log_put_dec((int)s.raw);
    BSL_LOG_INF(" cal=");
    bsl_log_put_dec((int)s.cal30);
    BSL_LOG_INF("/");
    bsl_log_put_dec((int)s.cal110);
    if (s.cal_valid) {
        BSL_LOG_INF(" t=");
        post_put_x10(s.celsius_x10);
        BSL_LOG_INF("C");
    } else {
        BSL_LOG_INF(" t=inv");
    }
    post_end((err == BSL_OK) && s.cal_valid
             && (s.celsius_x10 >= -400) && (s.celsius_x10 <= 1250));
}

/*
 * dac：DAC1_OUT1（PA4）+ PA4 自身的 ADC1_IN4 闭环。
 *
 * 判据分两段，缺一不可：
 *   1) 写 DHR12R1 → 读 DOR1：三个码 512 / 2048 / 3584 必须原样回来 ——
 *      证"码真进了转换器"（只看这一段，寄存器自洽也能过，所以不够）；
 *   2) PA4 既是 DAC 输出又是 ADC1_IN4 输入，读回 12 位码与按同一个 VREF 折算的
 *      期望值（= 写入码）相差 ≤ 64 LSB 且三点单调 —— 证"引脚上真有电压"。
 *
 * 为什么取 512 / 2048 / 3584 而不取 0 / 4095：输出缓冲开着时两端各有一段
 * 非线性区（靠近轨的几十毫伏），用满量程端点判误差会得到一个和驱动无关的 FAIL。
 * 64 LSB ≈ 51 mV，覆盖缓冲失调 + ADC 偏置 + 噪声，又不至于放过"写 3584 出 2048"
 * 这类真错。
 */
#define POST_DAC_CODES 3U
#define POST_DAC_ERR_LSB 64

static void post_dac(void)
{
    static const uint16_t codes[POST_DAC_CODES] = { 512U, 2048U, 3584U };
    uint16_t dor[POST_DAC_CODES];
    uint16_t adc[POST_DAC_CODES];
    unsigned i;
    int dor_ok = 1;
    int adc_ok = 1;
    int mono_ok = 1;
    int worst = 0;

    post_begin("dac ch=1 ");
    if (bsl_dac_init() != BSL_OK) {
        board_rt_dac_t probe;
        int rt = board_rt_dac(&probe);

        BSL_LOG_INF("init=fail rt=");
        bsl_log_put_dec(rt);
        if (rt != 0) {
            BSL_LOG_INF(" port=");
            bsl_log_put_hex((uint32_t)(uintptr_t)probe.port, 8U);
            BSL_LOG_INF(" pin=");
            bsl_log_put_dec((int)probe.pin);
            BSL_LOG_INF(" ch=");
            bsl_log_put_dec((int)probe.channel);
        }
        post_end(0);
        return;
    }
    /* 温度通道与 DAC 闭环共用 ADC1：init 幂等，这里再叫一次是为了顺序无关。 */
    if (bsl_adc_temp_init() != BSL_OK) {
        BSL_LOG_INF("adc=fail");
        post_end(0);
        return;
    }

    for (i = 0U; i < POST_DAC_CODES; i++) {
        if (bsl_dac_set_code(codes[i]) != BSL_OK) {
            dor_ok = 0;
            adc_ok = 0;
            dor[i] = 0U;
            adc[i] = 0U;
            continue;
        }
        dor[i] = bsl_dac_code();
        if (dor[i] != codes[i]) {
            dor_ok = 0;
        }
        if (bsl_adc_read_channel(4U, &adc[i]) != BSL_OK) {
            adc_ok = 0;
            continue;
        }
        /* 期望值就是写入码：DAC 与 ADC 同一颗 VREF，转移函数理想为 1:1 */
        if ((int)adc[i] - (int)codes[i] > POST_DAC_ERR_LSB
            || (int)codes[i] - (int)adc[i] > POST_DAC_ERR_LSB) {
            adc_ok = 0;
        }
        if ((int)adc[i] - (int)codes[i] > worst) {
            worst = (int)adc[i] - (int)codes[i];
        } else if ((int)codes[i] - (int)adc[i] > worst) {
            worst = (int)codes[i] - (int)adc[i];
        }
    }
    /* 相邻两点至少要拉开 1024 码；DAC 真在变，ADC 才会跟着变 */
    for (i = 1U; i < POST_DAC_CODES; i++) {
        if ((int)adc[i] - (int)adc[i - 1] < 1024) {
            mono_ok = 0;
        }
    }

    BSL_LOG_INF("clk=");
    bsl_log_put_dec((int)(bsl_dac_clock_hz() / 1000U));
    BSL_LOG_INF("k");
    BSL_LOG_INF(" code=");
    for (i = 0U; i < POST_DAC_CODES; i++) {
        if (i != 0U) {
            BSL_LOG_INF("/");
        }
        bsl_log_put_dec((int)codes[i]);
    }
    BSL_LOG_INF(" dor=");
    for (i = 0U; i < POST_DAC_CODES; i++) {
        if (i != 0U) {
            BSL_LOG_INF("/");
        }
        bsl_log_put_dec((int)dor[i]);
    }
    BSL_LOG_INF(" adc=");
    for (i = 0U; i < POST_DAC_CODES; i++) {
        if (i != 0U) {
            BSL_LOG_INF("/");
        }
        bsl_log_put_dec((int)adc[i]);
    }
    BSL_LOG_INF(" err=");
    bsl_log_put_dec(worst);

    /* 收尾把输出回到 0，不给后续应用留一个悬着的 1.4 V */
    (void)bsl_dac_set_code(0U);

    post_end(dor_ok && adc_ok && mono_ok);
}

/*
 * crc：硬件 CRC32 与一条**独立**的期望值对拍。
 *
 * 期望值 0x4C40E0D7 不是 CRC 单元自己算出来的 —— 它由一份纯软件的位运算
 * 实现（poly 0x04C11DB7 / init 0xFFFFFFFF / 不反射 / 不取反）算出后写死在这里。
 * 拿硬件结果跟它比，才能发现"单元没使能、DR 读成常量 0xFFFFFFFF"这类故障；
 * 若期望值也来自同一硬件，那这一项只是"我自己等于我自己"。
 *
 * 输入是 4 个整字，正好是单元的原生粒度：按字节喂要自己补尾字节，而补 0 会
 * 改变结果 —— 那种"看起来是标准向量、其实不是"的坑见 bsl_crc.h 注释。
 */
static void post_crc(void)
{
    static const uint32_t k_words[4] = {
        0x31323334U, 0x35363738U, 0x9ABCDEF0U, 0x13579BDFU
    };
    const uint32_t expect = 0x4C40E0D7U;
    uint32_t got = bsl_crc32_words(k_words, 4U);

    post_begin("crc 0x");
    bsl_log_put_hex(got, 8U);
    BSL_LOG_INF(" expect=0x");
    bsl_log_put_hex(expect, 8U);
    post_end((got == expect) ? 1 : 0);
}

/*
 * rng：连读 16 个随机数，判"够散"。
 *
 * 判据不看单个值（32 位随机数对任何具体值都没有可比性），看三条统计特征：
 *   - 16 次读都成功（DRDY 起来 → PLL48CK 在、种子没报错）；
 *   - 去重后 >= 15 个（16 个 32 位随机数撞一次的期望值约 0，撞 2 次以上基本
 *     只可能是单元返回了常量、计数器或只有低位在变）；
 *   - 没有出现 0x00000000 / 0xFFFFFFFF —— 这两个值正是"单元没使能、DR 读成
 *     复位值"的典型产物。
 * 这不是随机性测试（那要 NIST 套件），只是"单元活着"的判据。
 */
static void post_rng(void)
{
    uint32_t v[16];
    unsigned i;
    unsigned j;
    unsigned distinct = 0U;
    unsigned degenerate = 0U;
    int ok = 1;

    if (bsl_rng_init() != BSL_OK) {
        post_begin("rng init=err");
        post_end(0);
        return;
    }
    for (i = 0U; i < 16U; i++) {
        if (bsl_rng_read(&v[i]) != BSL_OK) {
            ok = 0;
            break;
        }
        if (v[i] == 0U || v[i] == 0xFFFFFFFFU) {
            degenerate++;
        }
    }
    for (i = 0U; i < 16U; i++) {
        for (j = 0U; j < i; j++) {
            if (v[j] == v[i]) {
                break;
            }
        }
        if (j == i) {
            distinct++;
        }
    }

    post_begin("rng n=16 distinct=");
    bsl_log_put_dec((int)distinct);
    BSL_LOG_INF(" degen=");
    bsl_log_put_dec((int)degenerate);
    post_end((ok && distinct >= 15U && degenerate == 0U) ? 1 : 0);
}

/*
 * codec：WM8978 是**只写** I2C 器件（P6c-0 实测：0x1A 地址 ACK、读方向 NACK），
 * 所以它没有"读回来对不对"这条判据。能做的硬判据只有一个：初始化那 14 条写事务
 * 是不是全部被从机 ACK。少一条就说明总线/地址/时序里有一处不对 —— 那不是"声音
 * 小一点"，是"根本没配上"。
 */
static void post_codec(void)
{
    const bsl_codec_api_t *api = bsl_devapi_codec();
    unsigned ack;
    unsigned want;

    if (bsl_device_is_ready(BSL_DEV_CODEC) == 0) {
        post_begin("codec dev=not-ready");
        post_end(0);
        return;
    }
    ack = api->ack_count();
    want = api->reg_count();
    post_begin("codec 1a regs=");
    bsl_log_put_dec((int)want);
    BSL_LOG_INF(" ack=");
    bsl_log_put_dec((int)ack);
    post_end((want != 0U && ack == want) ? 1 : 0);
}

/*
 * sai：位时钟与数据流是否真的在跑。
 *
 * 判据不看"有没有配置成功"（那只是寄存器写进去了），看**样本被消耗的速度**：
 * 一次性喂 2048 帧（32 位/帧），DMA 传完所用时间必须与 帧数/FS 对得上（±5%）。
 * 这个时长只有位时钟真的在跑才可能成立 —— DMA 自己是不知道节拍的，它由 SAI 的
 * 请求推动。同时报 SAI 状态里的 OVRUDR/WCKCFG：欠载说明 FIFO 供不上（听起来是
 * 断续），时钟配置非法说明分频算错。
 *
 * 波形用 1 kHz 方波（左右同幅、幅度 ±0x1800 ≈ -12 dBFS）：这一项要真的出声，
 * 人耳那一关只能人来判；串口这一行只证明"数据按时钟流走了"。**这不是蜂鸣器**：
 * 出声的是 WM8978 → 喇叭/耳机，蜂鸣器那条路径已被 BEEP_ALLOW_ON=0 断路。
 */
static void post_sai(void)
{
    const bsl_sai_api_t *api = bsl_devapi_sai();
    uint32_t *buf;
    uint32_t frames = 2048U;
    uint32_t fs;
    uint32_t us = 0U;
    uint32_t expect_us;
    uint32_t tol_us;
    bsl_err_t rc;
    unsigned i;
    bsl_sai_rx_stat_t rx;
    bsl_sai_rx_stat_t rx2;
    uint32_t rx_us = 0U;
    uint32_t rx2_us = 0U;
    bsl_err_t rx_rc;
    unsigned adc_ack = 0U;

    if (bsl_device_is_ready(BSL_DEV_SAI) == 0) {
        post_begin("sai dev=not-ready");
        post_end(0);
        return;
    }
    fs = api->sample_rate();
    buf = api->buffer();
    if (fs == 0U || buf == NULL || frames > BSL_SAI_BUF_FRAMES) {
        post_begin("sai cfg=bad");
        post_end(0);
        return;
    }
    /* 半个周期的方波：48 个样本一个周期，前 24 个 +A、后 24 个 -A */
    for (i = 0U; i < frames; i++) {
        int16_t s = (((i % 48U) < 24U) ? 0x1800 : -0x1800);
        buf[i] = ((uint32_t)(uint16_t)s << 16) | (uint32_t)(uint16_t)s;
    }

    rc = api->play_blocking(frames, &us);

    /* ---- 录音侧能力测量（P9e-3）：不建应用，只问"这条路通不通"。
     * 顺序有讲究：先把 codec 的输入 PGA 打开，再让 SAI 收；收完立刻还原播放口径，
     * 否则剩下的 POST 项都在 ADC 通路上跑。 */
    (void)memset(&rx, 0, sizeof(rx));
    (void)memset(&rx2, 0, sizeof(rx2));
    if (board_wm8978_adc_enable() == BSL_OK) {
        /* 两趟：先 CKSTR=1（与 Block A 同沿），再 CKSTR=0。两趟都收满 2048 说明
         * 时钟与 SD_B 都在动；两趟的读数差别说明"采到的东西"对边沿敏不敏感。 */
        /* 上板实测：CKSTR=1（与 Block A 同沿）收上来全是 0x0000，CKSTR=0 才拿到
         * 可解释的样本 —— 所以两趟都用 ALT 沿，把"静音基线"和"边放边录"放在同一
         * 个边沿下比较。第一趟静音 = 底噪；第二趟喇叭放方波 = 声耦合。 */
        /* 顺序上让"放音 + 录音"当第一趟：它的上一手是刚结束的 play_blocking，状态最干净；
         * 静音底噪放第二趟（上一轮里同一个第二趟位置用静音是通的）。 */
        rx_rc = bsl_sai_rx_capture(frames, BSL_SAI_RX_ALT_EDGE | BSL_SAI_RX_TONE,
                                   &rx, &rx_us);
        (void)bsl_sai_rx_capture(frames, BSL_SAI_RX_ALT_EDGE, &rx2, &rx2_us);
        adc_ack = board_wm8978_adc_ack();
        (void)board_wm8978_adc_disable();
    } else {
        rx_rc = BSL_ERR_IO;
        adc_ack = board_wm8978_adc_ack();
    }
    expect_us = (frames * 1000000U) / fs;
    /* 5% + 200 µs：CYCCNT 折算成 µs 后仍有 ±1 µs 量化，再给总线抖动一点余量。 */
    tol_us = (expect_us / 20U) + 200U;
    post_begin("sai fs=");
    bsl_log_put_dec((int)fs);
    BSL_LOG_INF(" frames=");
    bsl_log_put_dec((int)frames);
    BSL_LOG_INF(" us=");
    bsl_log_put_dec((int)us);
    BSL_LOG_INF(" expect=");
    bsl_log_put_dec((int)expect_us);
    BSL_LOG_INF(" err=");
    bsl_log_put_dec((int)api->last_error());
    BSL_LOG_INF(" rxt=");
    bsl_log_put_dec((int)rx.frames);
    BSL_LOG_INF("/l");
    bsl_log_put_dec((int)rx.lmin);
    BSL_LOG_INF("..");
    bsl_log_put_dec((int)rx.lmax);
    BSL_LOG_INF("@");
    bsl_log_put_dec((int)rx.lmean);
    BSL_LOG_INF("/r");
    bsl_log_put_dec((int)rx.rmin);
    BSL_LOG_INF("..");
    bsl_log_put_dec((int)rx.rmax);
    BSL_LOG_INF("@");
    bsl_log_put_dec((int)rx.rmean);
    BSL_LOG_INF("/pp");
    bsl_log_put_dec((int)rx.pp);
    BSL_LOG_INF("/nz");
    bsl_log_put_dec((int)rx.nz);
    BSL_LOG_INF(" rxs=");
    bsl_log_put_dec((int)rx2.frames);
    BSL_LOG_INF("/l");
    bsl_log_put_dec((int)rx2.lmin);
    BSL_LOG_INF("..");
    bsl_log_put_dec((int)rx2.lmax);
    BSL_LOG_INF("@");
    bsl_log_put_dec((int)rx2.lmean);
    BSL_LOG_INF("/r");
    bsl_log_put_dec((int)rx2.rmin);
    BSL_LOG_INF("..");
    bsl_log_put_dec((int)rx2.rmax);
    BSL_LOG_INF("@");
    bsl_log_put_dec((int)rx2.rmean);
    BSL_LOG_INF("/pp");
    bsl_log_put_dec((int)rx2.pp);
    BSL_LOG_INF("/nz");
    bsl_log_put_dec((int)rx2.nz);
    BSL_LOG_INF(" rxus=");
    bsl_log_put_dec((int)rx_us);
    BSL_LOG_INF(" adc=");
    bsl_log_put_dec((int)adc_ack);
    /* 判据只加"帧数到了"这一条：两趟各 2048 字说明 SCK/FS 在跑、SD_B 的数据被
     * DMA 收满。**不判有没有声音** —— 板上咪头有没有、增益够不够，是这个测量
     * 要回答的问题本身，不是它的前提。l/r 的 min..max@mean 与 pp 照原样打出来供人读。 */
    post_end((rc == BSL_OK && api->last_error() == 0U
              && us + tol_us >= expect_us && us <= expect_us + tol_us
              && rx_rc == BSL_OK && rx.frames == frames
              && rx2.frames == frames) ? 1 : 0);
}

/*
 * imu（P6d）：MPU9250 的身份与两条硬事实。
 *
 * 判据不看"初始化有没有返回 OK"（那可能是驱动自己的乐观），看三个固定值：
 *   WHO_AM_I(0x75) = 0x71 —— 型号指纹（MPU6050 是 0x68、MPU6500 是 0x70）；
 *   初始化后再读一次 WHO_AM_I 仍为 0x71 —— 证明前面那串配置没把器件写崩
 *   （写错寄存器把 MPU 配成 slave 模式时，它会从总线上"消失"）；
 *   AK8963 的 WIA = 0x48 —— 只有 INT_PIN_CFG(0x37).BYPASS_EN 打开后才可见，
 *   所以这一条同时证了"旁路真的生效"。DTB 没声明磁力计时该条不参与判定
 *   （报 mag=00 并在判据里说明），不因此判 FAIL —— 那是"没验"，不是"坏了"。
 *
 * 加速度/陀螺读数只上报（板子静止时 acc 应接近重力方向、gyr 应接近 0），
 * 不做"是否倾斜"的自动判定：那需要定标，属于应用。
 */
static void post_imu(void)
{
    int16_t acc[3];
    int16_t gyr[3];
    uint8_t who;
    uint8_t mag;
    int ok;

    if (bsl_device_is_ready(BSL_DEV_IMU) == 0) {
        post_begin("imu dev=not-ready");
        post_end(0);
        return;
    }
    who = bsl_devapi_imu()->who();
    mag = bsl_devapi_imu()->mag_wia();
    if (bsl_devapi_imu()->read(acc, gyr) != BSL_OK) {
        acc[0] = 0; acc[1] = 0; acc[2] = 0;
        gyr[0] = 0; gyr[1] = 0; gyr[2] = 0;
        ok = 0;
    } else {
        ok = 1;
    }
    post_begin("imu who=");
    bsl_log_put_hex(who, 2U);
    BSL_LOG_INF(" mag=");
    bsl_log_put_hex(mag, 2U);
    BSL_LOG_INF(" acc=");
    bsl_log_put_dec((int)acc[0]);
    BSL_LOG_INF(",");
    bsl_log_put_dec((int)acc[1]);
    BSL_LOG_INF(",");
    bsl_log_put_dec((int)acc[2]);
    BSL_LOG_INF(" gyr=");
    bsl_log_put_dec((int)gyr[0]);
    BSL_LOG_INF(",");
    bsl_log_put_dec((int)gyr[1]);
    BSL_LOG_INF(",");
    bsl_log_put_dec((int)gyr[2]);
    post_end((ok && who == 0x71U && mag == 0x48U) ? 1 : 0);
}

/*
 * als（P6d）：AP3216C 的系统寄存器可读回，所以这条判据是"回读等于写入"，
 * 与 codec（只写器件只能数 ACK）不同。三个数据值一并上报：
 *   als = 环境光（遮住板子读数应显著下降，这是人验手段）；
 *   ps  = 接近距离；ir = 红外强度。
 * 数据值不参与判定 —— 暗处 als 可以接近 0，拿它当判据会把"黑屋子"判成坏件。
 */
static void post_als(void)
{
    uint16_t ir = 0U;
    uint16_t ps = 0U;
    uint16_t als_v = 0U;
    int ok;

    if (bsl_device_is_ready(BSL_DEV_ALS) == 0) {
        post_begin("als dev=not-ready");
        post_end(0);
        return;
    }
    ok = (bsl_devapi_als()->read(&ir, &ps, &als_v) == BSL_OK) ? 1 : 0;
    post_begin("als ir=");
    bsl_log_put_dec((int)ir);
    BSL_LOG_INF(" ps=");
    bsl_log_put_dec((int)ps);
    BSL_LOG_INF(" als=");
    bsl_log_put_dec((int)als_v);
    post_end(ok);
}

/*
 * eeprom（P6d）：24C02 的写-读-还原三段验证。
 *
 * 只碰**地址 0x00 这 1 个字节**，而且必须还原成原值 —— EEPROM 是用户数据，
 * 自检不能把它当草稿纸。判据三件事：
 *   1) 写反码后读回 = 反码（证明写真的落盘且读数指向同一字节）；
 *   2) 还原后读回 = 原值（证明没留副作用）；
 *   3) 全过程无 NACK。
 * 任一步失败都要尽力还原（失败路径里再写一次原值），否则下次上电用户会看到
 * 一个被自检改过的字节，而现场完全不知道是谁改的。
 */
static void post_eeprom(void)
{
    uint8_t orig = 0U;
    uint8_t back = 0U;
    uint8_t flip;
    int ok = 0;

    if (bsl_device_is_ready(BSL_DEV_EEPROM) == 0) {
        post_begin("eeprom dev=not-ready");
        post_end(0);
        return;
    }
    if (bsl_devapi_eeprom()->read(0x00U, &orig, 1U) != BSL_OK) {
        post_begin("eeprom read fail");
        post_end(0);
        return;
    }
    flip = (uint8_t)(orig ^ 0xA5U);
    if (bsl_devapi_eeprom()->write(0x00U, flip) == BSL_OK
        && bsl_devapi_eeprom()->read(0x00U, &back, 1U) == BSL_OK
        && back == flip) {
        ok = 1;
    }
    /* 还原：无论上一步成败都要写回去，再用读回确认真的还回去了 */
    (void)bsl_devapi_eeprom()->write(0x00U, orig);
    if (bsl_devapi_eeprom()->read(0x00U, &back, 1U) != BSL_OK || back != orig) {
        ok = 0;
    }
    post_begin("eeprom wr=");
    bsl_log_put_hex(flip, 2U);
    BSL_LOG_INF(" rd=");
    bsl_log_put_hex((uint8_t)((ok != 0) ? flip : back), 2U);
    BSL_LOG_INF(" restore=");
    bsl_log_put_hex(orig, 2U);
    post_end(ok);
}

/*
 * tpad（P6d）：基线是否落在合理区间。
 *
 * 判据 = 参考工程 system_init() 的同一条：`default` 在 (0, arr_max/2] 且 <= 1000。
 * 上界 1000 是"这块板空载充电时间"的实测量级，超过它说明引脚被持续按压/短路。
 * 「真的用手指按一下」验不到 —— 那需要人手，POST 只证"定标成功、门限算得出来"；
 * HIL 里敲 `tpad` 命令看实时计数值，人按一下即可确认灵敏度。
 */
static void post_tpad(void)
{
    const bsl_tpad_api_t *api = bsl_devapi_tpad();
    uint16_t base;

    if (bsl_device_is_ready(BSL_DEV_TPAD) == 0) {
        post_begin("tpad dev=not-ready");
        post_end(0);
        return;
    }
    base = api->baseline();
    post_begin("tpad default=");
    bsl_log_put_dec((int)base);
    BSL_LOG_INF(" thr=");
    bsl_log_put_dec((int)api->threshold());
    post_end((base != 0U && base <= 1000U) ? 1 : 0);
}

/*
 * can（P6e-1）：回环自发自收。
 *
 * 判据 = 发出 8 字节标准帧（ID 0x123），在回环模式下自己收到**同一 ID、同一 8 字节**。
 * 这一条能抓住的故障：BTR 分频算错（自收的帧位时序错 → 错误计数器涨、收不到）、
 * 邮箱寄存器写序错（TIR 的 TXRQ 先置位会发出上一帧残留）、过滤器没生效
 * （参考工程 `FMR &= 0 << 0` 是空操作，过滤器停在初始化态 → 帧全被丢弃）、
 * FIFO 释放漏写 RFOM（第二次收同一帧）。
 *
 * 为什么用回环而不是等对端：本板 PA11/PA12 与 USB 的 D-/D+ 共用 P9 跳线，
 * 也没有第二个 CAN 节点。回环由 CAN 核内部把 Tx 送进 Rx，**与跳线、收发器无关**，
 * 所以这一项在"什么都没接"的桌面上也能跑出真结果。
 */
static void post_can(void)
{
    static const uint8_t tx[8] = {0x01U, 0x23U, 0x45U, 0x67U, 0x89U, 0xABU, 0xCDU, 0xEFU};
    uint8_t rx[8];
    uint8_t rxlen = 0U;
    uint32_t rxid = 0U;
    uint32_t brp = 0U;
    uint32_t tseg = 0U;
    unsigned i;
    int got = 0;
    int same = 0;

    if (bsl_device_is_ready(BSL_DEV_CAN) == 0) {
        post_begin("can dev=not-ready");
        post_end(0);
        return;
    }
    bsl_devapi_can()->timing(&brp, &tseg);
    if (bsl_devapi_can()->send(0x123U, tx, 8U) == BSL_OK) {
        for (i = 0U; i < 20U; i++) {
            if (bsl_devapi_can()->recv(&rxid, rx, &rxlen) != 0) {
                got = 1;
                break;
            }
            HAL_Delay(1U);
        }
    }
    if (got != 0 && rxid == 0x123U && rxlen == 8U && memcmp(rx, tx, 8U) == 0) {
        same = 1;
    }
    post_begin("can bt=");
    bsl_log_put_dec((int)bsl_devapi_can()->bit_rate());
    BSL_LOG_INF(" brp=");
    bsl_log_put_dec((int)brp);
    BSL_LOG_INF(" tseg=");
    bsl_log_put_dec((int)tseg);
    BSL_LOG_INF(" btr=");
    bsl_log_put_hex((uint8_t)((CAN1->BTR >> 24) & 0xFFU), 2U);
    bsl_log_put_hex((uint8_t)((CAN1->BTR >> 16) & 0xFFU), 2U);
    bsl_log_put_hex((uint8_t)((CAN1->BTR >> 8) & 0xFFU), 2U);
    bsl_log_put_hex((uint8_t)(CAN1->BTR & 0xFFU), 2U);
    BSL_LOG_INF(" tx=1 rx=");
    bsl_log_put_dec(got);
    BSL_LOG_INF(" id=");
    bsl_log_put_hex((uint8_t)((rxid >> 8) & 0xFFU), 2U);
    bsl_log_put_hex((uint8_t)(rxid & 0xFFU), 2U);
    BSL_LOG_INF(" data=");
    for (i = 0U; i < (unsigned)rxlen; i++) {
        bsl_log_put_hex(rx[i], 2U);
    }
    post_end(same);
}

/*
 * usart（P6e-1）：两件事。
 *
 *   1) 分频对不对：**连发 16 个字节**量总时长。
 *      为什么不是发一个字节：单字节的测量窗口里含两段各 0..1 bit 的量化 —— 写 DR
 *      到起始位真正上线（内部波特率时钟对齐）、帧尾到 TC 置位。8N1 一帧是 10 bit，
 *      115200 下 86.8 µs，但逐字节量出来是 92~95 µs（≈11 bit），拿它去判 ±10%
 *      会把"分频正确"判成失败（本轮就是这么撞上的）。连发时量化只发生一次，
 *      摊到 16 帧上只剩 6%，总时长 vs 16*10*bit_time 才是真的在量分频。
 *   2) 帧状态机对不对：往状态机注入 3 个字节，等超过 DTB 的空闲阈值，应取出 1 帧 3 字节。
 *      **这一步绕过硬件**（没有对端就收不到真字节），所以判据措辞是"帧状态机"，
 *      不是"串口能收" —— 线上接收要接 GSM/ATK-MODULE 才能验，这条限制写进证据文件。
 */
static void post_usart(void)
{
    const bsl_uart_api_t *api = bsl_devapi_usart3();
    uint8_t tx[16];
    uint8_t frame[8];
    unsigned len = 0U;
    uint32_t us = 0U;
    uint32_t us1 = 0U;
    uint32_t expect;
    uint32_t tol;
    int tx_ok;
    int frame_ok = 0;
    unsigned i;

    if (bsl_device_is_ready(BSL_DEV_USART3) == 0) {
        post_begin("usart dev=not-ready");
        post_end(0);
        return;
    }
    for (i = 0U; i < 16U; i++) {
        tx[i] = (uint8_t)(i + 0x30U);
    }
    /* 单字节读数只作诊断打出来（说明"逐字节口径含量化"这件事本身），不参与判据。 */
    (void)api->send_byte(0x55U, &us1);
    expect = (160U * 1000000U) / api->baud(); /* 16 帧 × 10 bit */
    tol = expect / 50U;                      /* ±2%：量化摊薄后只剩 ~1% */
    tx_ok = (api->send(tx, 16U, &us) == BSL_OK)
            && (us + tol >= expect) && (us <= expect + tol);

    api->inject('A');
    HAL_Delay(1U);
    api->inject('B');
    HAL_Delay(1U);
    api->inject('C');
    HAL_Delay(12U); /* > frame-idle-ms(10) */
    if (api->recv_frame(frame, (unsigned)sizeof(frame), &len) != 0
        && len == 3U && frame[0] == 'A' && frame[1] == 'B' && frame[2] == 'C') {
        frame_ok = 1;
    }
    post_begin("usart baud=");
    bsl_log_put_dec((int)api->baud());
    BSL_LOG_INF(" brr=");
    bsl_log_put_dec((int)api->brr());
    BSL_LOG_INF(" burst16_us=");
    bsl_log_put_dec((int)us);
    BSL_LOG_INF(" exp=");
    bsl_log_put_dec((int)expect);
    BSL_LOG_INF(" one_us=");
    bsl_log_put_dec((int)us1);
    BSL_LOG_INF(" frame=");
    bsl_log_put_dec((int)((frame_ok != 0) ? len : 0U));
    BSL_LOG_INF("/3 idle=10ms");
    post_end((tx_ok != 0 && frame_ok != 0) ? 1 : 0);
}

/*
 * 外部项（apl 注册）：
 *
 * "屏上有没有文字"必须用 fml 的字库与画布才验得出来，而 bsl 不能反向依赖 fml
 * （bsl/src/Makefile 的 INCLUDE_DIRS 里没有 fml/inc）。所以项从上面注册进来，
 * 汇总与格式留在本文件 —— 依赖方向不变，串口上仍是同一套 [post] 行。
 *
 * 外部项与内置项走同一对 post_begin/post_end：否则 result 里的 total 会把它们
 * 漏掉，"2 pass" 与屏上实际验过的项数对不上。
 */
static const char *s_ext_item[BOARD_POST_EXT_MAX];
static int (*s_ext_fn[BOARD_POST_EXT_MAX])(char *buf, unsigned cap);
static unsigned s_ext_count;

int board_post_register(const char *item, int (*fn)(char *buf, unsigned cap))
{
    if (item == NULL || fn == NULL) {
        return 0;
    }
    /* 上限由头文件公开，注册方据此判断"没有位置"而不是静默丢项。 */
    if (s_ext_count >= BOARD_POST_EXT_MAX) {
        return 0;
    }
    s_ext_item[s_ext_count] = item;
    s_ext_fn[s_ext_count] = fn;
    s_ext_count++;
    return 1;
}

/* 外部项：算在前、打印在后。
 *
 * 顺序不能反 —— fn 的运行期会调到应用的 enter / leave / event，这些回调自己
 * 打日志（`[app_calc] key = disp=3`）。先把 `[post] <item>` 前缀打出去再调 fn，
 * 那些行就插进 POST 半行中间，`[post] menu apps=5 ...` 被切成两三行，正则与
 * 人工都读不出这一项到底过没过。攒到 buf 里再一次性输出，回调日志就自然排在
 * POST 行之前。 */
/* 外部项的格式化缓冲。做成 static 而不是栈上：post_ext 只在启动时单线程跑一次，
 * 512 B 放栈会把 main 任务的栈顶到 2 KB 边上；fatfs 那一行实测约 240 字符。 */
#define POST_EXT_BUF 512U
static char s_ext_buf[POST_EXT_BUF];

static void post_ext(void)
{
    unsigned i;
    int ok;

    for (i = 0U; i < s_ext_count; i++) {
        s_ext_buf[0] = '\0';
        ok = s_ext_fn[i](s_ext_buf, POST_EXT_BUF);
        post_begin(s_ext_item[i]);
        /* 分隔空格由这里补：内置项把值拼进了 item 字符串（"sdram size="），
         * 外部项只报项名，内容另存 buf，不补空格就会变成 "fatfstotal="。 */
        if (s_ext_buf[0] != '\0') {
            bsl_log_puts(" ");
        }
        bsl_log_puts(s_ext_buf);
        post_end(ok);
    }
}

/*
 * NAND（P9a）：只读判据 —— ID / 几何 / 读 page0 的 main + spare。
 * 不擦不写：原厂固件在这片 NAND 上放着系统文件，检验性擦写会毁数据。
 * 判据 = ID 合法（首字节非 0x00/0xFF）+ 几何自洽（页 = main+spare、
 * 总容量 = blocks×block_pages×page）+ 两段读都不超时。
 * sum/ff 是"数据真的被时钟搬进来了"的读数：整片擦净时 ff == 总字节数，
 * 有内容时 ff 会小；两个极端都能从这一行看出来，不必靠猜。
 */
static void post_nand(void)
{
    static uint8_t page[2048U + 64U];
    uint8_t id[5];
    uint16_t page_main = 0U;
    uint16_t page_spare = 0U;
    uint32_t block_pages = 0U;
    uint32_t blocks = 0U;
    unsigned bank = 0U;
    uint32_t sum = 0U;
    unsigned ff = 0U;
    uint32_t wr_blk = 0U;
    uint32_t wr_mism = 0U;
    unsigned wr_pk = 0U;
    unsigned wr_bad = 0U;
    unsigned wr_preff = 0U;
    unsigned wr_er = 0U;
    unsigned wr_prg = 0U;
    unsigned wr_re = 0U;
    unsigned i;
    int ok = 0;

    if (bsl_device_is_ready(BSL_DEV_NAND) == 0) {
        post_begin("nand dev=not-ready");
        post_end(0);
        return;
    }
    bsl_devapi_nand()->info(id, &page_main, &page_spare, &block_pages, &blocks,
                            &bank);
    if (bsl_devapi_nand()->read_page(0U, 0U, page, page_main) == BSL_OK
        && bsl_devapi_nand()->read_page(0U, page_main, page, 8U) == BSL_OK) {
        ok = 1;
    }
    if (page_main != 0U) {
        if (bsl_devapi_nand()->read_page(0U, 0U, page, page_main) == BSL_OK) {
            for (i = 0U; i < page_main; i++) {
                sum += page[i];
                if (page[i] == 0xFFU) {
                    ff++;
                }
            }
        } else {
            ok = 0;
        }
    }
    if (id[0] == 0x00U || id[0] == 0xFFU || page_main == 0U || page_spare == 0U
        || block_pages == 0U || blocks == 0U) {
        ok = 0;
    }

    /* —— P9a-2 写通道：单块擦 / 写 / 读回比 / 再擦 ——
     * 选块：从最后一块往前扫，最多 8 块，取第一个「坏块标记 0xFF 且页 0 主区+spare
     * 全 0xFF（本来就是空的）」的块；扫不到就退到最后一块，让 preff=0 把「这一块
     * 原来有数据、已被本批覆盖」如实暴露出来。每次上电按同一策略选同一块，磨耗
     * 就集中在这一个块上（代价记账，不藏）。
     * 顺序：擦 → 验全 FF → 写图案 → 读回逐字节比 → 再擦 → 验全 FF（留空块收工）。
     * 只碰 1 个块、不写 spare、不开 ECC —— 这三条是这批的边界，不是遗漏。 */
    if (ok != 0 && page_main > 0U && page_spare > 0U) {
        wr_blk = blocks - 1U;

        for (uint32_t k = 0U; (k < 64U) && (k < blocks); k++) {
            uint32_t cand = blocks - 1U - k;
            unsigned allff = 1U;

            wr_pk++;
            if (bsl_devapi_nand()->is_bad_block(cand) != 0) {
                wr_bad++;
                continue;
            }
            if (bsl_devapi_nand()->read_page(cand * block_pages, 0U, page,
                                             page_main) != BSL_OK) {
                continue;
            }
            if (bsl_devapi_nand()->read_page(cand * block_pages, page_main,
                                             &page[page_main],
                                             page_spare) != BSL_OK) {
                continue;
            }
            for (i = 0U; i < (unsigned)(page_main + page_spare); i++) {
                if (page[i] != 0xFFU) {
                    allff = 0U;
                    break;
                }
            }
            if (allff != 0U) {
                wr_blk = cand;
                wr_preff = 1U;
                break;
            }
        }

        {
            uint32_t wr_off = wr_blk * block_pages;
            unsigned allff = 1U;
            unsigned erase1_ok = 0U;

            if (bsl_devapi_nand()->erase_block(wr_blk) == BSL_OK) {
                erase1_ok = 1U;
                if (bsl_devapi_nand()->read_page(wr_off, 0U, page, page_main)
                        == BSL_OK
                    && bsl_devapi_nand()->read_page(wr_off, page_main,
                                                    &page[page_main],
                                                    page_spare) == BSL_OK) {
                    for (i = 0U; i < (unsigned)(page_main + page_spare); i++) {
                        if (page[i] != 0xFFU) {
                            allff = 0U;
                            break;
                        }
                    }
                } else {
                    allff = 0U;
                }
                wr_er = (erase1_ok != 0U && allff != 0U) ? 1U : 0U;
            }
        }

        if (wr_er != 0U) {
            uint32_t wr_off = wr_blk * block_pages;
            static uint8_t wr_rd[2048U];

            for (i = 0U; i < page_main; i++) {
                page[i] = (uint8_t)i;
            }
            if (bsl_devapi_nand()->write_page(wr_off, 0U, page, page_main)
                == BSL_OK) {
                wr_prg = 1U;
                if (bsl_devapi_nand()->read_page(wr_off, 0U, wr_rd, page_main)
                    == BSL_OK) {
                    for (i = 0U; i < page_main; i++) {
                        if (wr_rd[i] != (uint8_t)i) {
                            wr_mism++;
                        }
                    }
                } else {
                    wr_mism = page_main; /* 读都读不出来 = 全不符 */
                }
            }
        }

        /* 收工：再擦一次，把块还原成空白（不留测试图案）。 */
        if (wr_er != 0U) {
            uint32_t wr_off = wr_blk * block_pages;
            unsigned allff = 1U;

            if (bsl_devapi_nand()->erase_block(wr_blk) == BSL_OK) {
                if (bsl_devapi_nand()->read_page(wr_off, 0U, page, page_main)
                        == BSL_OK
                    && bsl_devapi_nand()->read_page(wr_off, page_main,
                                                    &page[page_main],
                                                    page_spare) == BSL_OK) {
                    for (i = 0U; i < (unsigned)(page_main + page_spare); i++) {
                        if (page[i] != 0xFFU) {
                            allff = 0U;
                            break;
                        }
                    }
                } else {
                    allff = 0U;
                }
                wr_re = allff;
            }
        }

        if ((wr_er == 0U) || (wr_prg == 0U) || (wr_mism != 0U)
            || (wr_re == 0U)) {
            ok = 0;
        }
    }

    post_begin("nand id=");
    for (i = 0U; i < 5U; i++) {
        if (i != 0U) {
            BSL_LOG_INF(":");
        }
        bsl_log_put_hex(id[i], 2U);
    }
    BSL_LOG_INF(" page=");
    bsl_log_put_dec((int)page_main);
    BSL_LOG_INF("/");
    bsl_log_put_dec((int)page_spare);
    BSL_LOG_INF(" blk=");
    bsl_log_put_dec((int)block_pages);
    BSL_LOG_INF("/");
    bsl_log_put_dec((int)blocks);
    BSL_LOG_INF(" bank=");
    bsl_log_put_dec((int)bank);
    BSL_LOG_INF(" sum=");
    bsl_log_put_hex(sum, 8U);
    BSL_LOG_INF(" ff=");
    bsl_log_put_dec((int)ff);
    BSL_LOG_INF(" pr=");
    bsl_log_put_dec((int)wr_pk);
    BSL_LOG_INF("/");
    bsl_log_put_dec((int)wr_bad);
    BSL_LOG_INF(" wr=");
    bsl_log_put_dec((int)wr_blk);
    BSL_LOG_INF("/");
    bsl_log_put_dec((int)wr_preff);
    BSL_LOG_INF("/");
    bsl_log_put_dec((int)wr_er);
    BSL_LOG_INF("/");
    bsl_log_put_dec((int)wr_prg);
    BSL_LOG_INF("/");
    bsl_log_put_dec((int)wr_mism);
    BSL_LOG_INF("/");
    bsl_log_put_dec((int)wr_re);
    post_end(ok);
}
/*
 * DMA（P9c-1）：DMA2 内存→内存搬 256 个字，逐字比对。
 * 判据三件：返回值 OK、done==256（NDTR 真的走到 0）、dst[i]==i 逐字相同。
 * 只判"函数返回成功"是不够的 —— 传输没完成时也可能返回 OK（早停会留下半份数据）。
 * 放在 bss 而不是栈：POST 任务栈 512 字，两份 1 KB 缓冲装不下。
 */
static void post_dma(void)
{
    static uint32_t src[256];
    static uint32_t dst[256];
    unsigned done = 0U;
    unsigned ok = 0U;
    unsigned i;

    if (bsl_device_is_ready(BSL_DEV_DMA) == 0) {
        post_begin("dma dev=not-ready");
        post_end(0);
        return;
    }
    for (i = 0U; i < 256U; i++) {
        src[i] = i;
        dst[i] = 0xDEADBEEFUL;
    }
    bsl_err_t rc = bsl_devapi_dma()->memcopy(src, dst, 256U, &done);
    if (rc == BSL_OK && done == 256U) {
        for (i = 0U; i < 256U; i++) {
            if (dst[i] == i) {
                ok++;
            }
        }
    }
    post_begin("dma n=256 done=");
    bsl_log_put_dec((int)done);
    BSL_LOG_INF(" ok=");
    bsl_log_put_dec((int)ok);
    post_end((rc == BSL_OK && ok == 256U) ? 1 : 0);
}

/*
 * PWM（P9c-2）：TIM3_CH2 / PB5 驱动背光。
 * 判据：DTS 目标频率与**实测**频率相差 ≤3%，且占空比设 50% 后回读 500‰。
 * 实测频率是这样拿到的：驱动数 20 个 CC2IF（硬件在 CNT 真走到 CCR 时置位），
 * 窗口用 DWT->CYCCNT 计时 —— 只把 PSC/ARR 抄回来重算，判不出"定时器到底跑没跑"。
 * 打完把占空比放回 100%：背光必须保持全亮，不能因为自检把屏调暗。
 */
static void post_pwm(void)
{
    const bsl_pwm_api_t *api = bsl_devapi_pwm();
    uint32_t want;
    uint32_t meas = 0U;
    uint32_t duty;
    uint32_t diff;
    uint32_t psc = 0U;
    uint32_t arr = 0U;
    uint32_t ccr = 0U;

    if (bsl_device_is_ready(BSL_DEV_PWM) == 0) {
        post_begin("pwm dev=not-ready");
        post_end(0);
        return;
    }
    want = api->frequency();
    if (api->set_duty(500U) != BSL_OK || api->measure_freq(&meas) != BSL_OK) {
        post_begin("pwm cfg=bad");
        post_end(0);
        (void)api->set_duty(1000U);
        return;
    }
    duty = api->duty();
    api->timing(&psc, &arr, &ccr);
    (void)api->set_duty(1000U);
    diff = (meas > want) ? (meas - want) : (want - meas);
    post_begin("pwm f=");
    bsl_log_put_dec((int)want);
    BSL_LOG_INF(" meas=");
    bsl_log_put_dec((int)meas);
    BSL_LOG_INF(" clk=");
    bsl_log_put_dec((int)(api->clock_hz() / 1000U));
    BSL_LOG_INF("k psc=");
    bsl_log_put_dec((int)psc);
    BSL_LOG_INF(" arr=");
    bsl_log_put_dec((int)arr);
    BSL_LOG_INF(" ccr=");
    bsl_log_put_dec((int)ccr);
    BSL_LOG_INF(" duty=");
    bsl_log_put_dec((int)duty);
    post_end((want != 0U && meas != 0U
              && (diff * 100U) <= (want * 3U)
              && duty >= 490U && duty <= 510U) ? 1 : 0);
}

/*
 * EXTI（P9c-2）：EXTI0 ← PA0。
 * 两段激励分开报数，哪一段断了一眼看得出：
 *   pin=：把 PA0 临时改成推挽输出打 2 个高脉冲 —— 走的是真引脚与真 EXTI 线；
 *   sw= ：EXTI->SWIER0 软件置挂起 2 次 —— 只验"PR → NVIC → ISR"后半段。
 * 两段都要求 ISR 计数与打进去的边沿数相等，且最后 PR 已清。
 */
static void post_exti(void)
{
    static const char *const src_name[5] = { "A", "B", "C", "D", "E" };
    const bsl_exti_api_t *api = bsl_devapi_exti();
    unsigned line = 0U;
    unsigned src = 0U;
    int rising = 1;
    unsigned pin_edges;
    unsigned sw_edges;
    unsigned pend;

    if (bsl_device_is_ready(BSL_DEV_EXTI) == 0) {
        post_begin("exti dev=not-ready");
        post_end(0);
        return;
    }
    api->info(&line, &src, &rising);
    api->clear();
    pin_edges = api->pulse_pin(2U);
    api->clear();
    sw_edges = api->trigger_sw(2U);
    pend = api->pending();

    post_begin("exti line=");
    bsl_log_put_dec((int)line);
    BSL_LOG_INF(" src=");
    BSL_LOG_INF((src < 5U) ? src_name[src] : "?");
    BSL_LOG_INF((rising != 0) ? " edge=r pin=" : " edge=f pin=");
    bsl_log_put_dec((int)pin_edges);
    BSL_LOG_INF("/2 sw=");
    bsl_log_put_dec((int)sw_edges);
    BSL_LOG_INF("/2 pr=");
    bsl_log_put_dec((int)pend);
    post_end((pin_edges == 2U && sw_edges == 2U && pend == 0U) ? 1 : 0);
}

void board_post_run(void)
{
    s_total = 0U;
    s_pass = 0U;
    s_fail = 0U;

    post_sdram();
    post_lcd();
    post_touch();
    post_nor();
    post_nand();
    post_dma();
    post_pwm();
    post_exti();
    post_rtc();
    post_adc();
    post_dac();
    post_crc();
    post_rng();
    post_codec();
    post_sai();
    post_imu();
    post_als();
    post_eeprom();
    post_tpad();
    post_can();
    post_usart();
    post_ext();

    BSL_LOG_MOD_INF(POST_LOG_MOD, "result ");
    bsl_log_put_dec((int)s_total);
    BSL_LOG_INF(" pass ");
    bsl_log_put_dec((int)s_pass);
    BSL_LOG_INF(" fail ");
    bsl_log_put_dec((int)s_fail);
    BSL_LOG_INF("\r\n");
}
