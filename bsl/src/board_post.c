#include "board_post.h"

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

void board_post_run(void)
{
    s_total = 0U;
    s_pass = 0U;
    s_fail = 0U;

    post_sdram();

    BSL_LOG_MOD_INF(POST_LOG_MOD, "result ");
    bsl_log_put_dec((int)s_total);
    BSL_LOG_INF(" pass ");
    bsl_log_put_dec((int)s_pass);
    BSL_LOG_INF(" fail ");
    bsl_log_put_dec((int)s_fail);
    BSL_LOG_INF("\r\n");
}
