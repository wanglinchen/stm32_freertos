#include "board_post.h"

#include "board_lcd.h"
#include "board_sdram.h"
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

void board_post_run(void)
{
    s_total = 0U;
    s_pass = 0U;
    s_fail = 0U;

    post_sdram();
    post_lcd();

    BSL_LOG_MOD_INF(POST_LOG_MOD, "result ");
    bsl_log_put_dec((int)s_total);
    BSL_LOG_INF(" pass ");
    bsl_log_put_dec((int)s_pass);
    BSL_LOG_INF(" fail ");
    bsl_log_put_dec((int)s_fail);
    BSL_LOG_INF("\r\n");
}
