#include "board_post.h"

#include "board_lcd.h"
#include "board_sdram.h"
#include "board_touch.h"
#include "bsl_device.h"
#include "bsl_log.h"

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
    post_ext();

    BSL_LOG_MOD_INF(POST_LOG_MOD, "result ");
    bsl_log_put_dec((int)s_total);
    BSL_LOG_INF(" pass ");
    bsl_log_put_dec((int)s_pass);
    BSL_LOG_INF(" fail ");
    bsl_log_put_dec((int)s_fail);
    BSL_LOG_INF("\r\n");
}
