#include "board_post.h"

#include "board_lcd.h"
#include "board_sdram.h"
#include "board_touch.h"
#include "bsl_adc.h"
#include "bsl_crc.h"
#include "bsl_device.h"
#include "bsl_log.h"
#include "bsl_rng.h"
#include "bsl_rtc.h"
#include "bsl_sai.h"

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
 *   - READY 但寄存器与 DTB 不一致 → FAIL，并打出不一致项数
 * 判 PASS 只覆盖「寄存器按 DTB 写对了」，不覆盖「面板真的亮了」：后者由屏上的
 * 三色块用眼睛验收。这就是要画色块的原因 —— 读回全对但屏全黑，说明问题在
 * 面板供电/背光/数据线，而不是时序配置，两者不能被同一行 PASS 混在一起。
 * 宽高经 api 取（bsl_devapi_panel），不直接调 board_lcd_width()：api 结构必须有
 * 消费者，否则 `--gc-sections` 会把 bsl_api_brd_rgb_panel 丢掉。
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

    post_begin("lcd w=");
    bsl_log_put_dec((int)w);
    BSL_LOG_INF(" h=");
    bsl_log_put_dec((int)h);
    BSL_LOG_INF(" bad=");
    bsl_log_put_dec((int)bad);
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
    post_end((rc == BSL_OK && api->last_error() == 0U
              && us + tol_us >= expect_us && us <= expect_us + tol_us) ? 1 : 0);
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

void board_post_run(void)
{
    s_total = 0U;
    s_pass = 0U;
    s_fail = 0U;

    post_sdram();
    post_lcd();
    post_touch();
    post_nor();
    post_rtc();
    post_adc();
    post_crc();
    post_rng();
    post_codec();
    post_sai();
    post_ext();

    BSL_LOG_MOD_INF(POST_LOG_MOD, "result ");
    bsl_log_put_dec((int)s_total);
    BSL_LOG_INF(" pass ");
    bsl_log_put_dec((int)s_pass);
    BSL_LOG_INF(" fail ");
    bsl_log_put_dec((int)s_fail);
    BSL_LOG_INF("\r\n");
}
