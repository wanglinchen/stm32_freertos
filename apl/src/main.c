#include "app_sample.h"
#include "board_lcd.h"
#include "board_post.h"
#include "board_system.h"
#include "bsl_fb.h"
#include "bsl_init.h"
#include "bsl_log.h"
#include "fml_font.h"
#include "fml_fatfs.h"
#include "fml_gui.h"
#include "fml_panic.h"
#include "osl_boot.h"
#include "osl_health.h"
#include "osl_thread.h"

/*
 * 显示自检：POST 的 font 项，由 apl 注册（字库与画布都在 fml，bsl 看不见它们）。
 *
 * 判据双向：
 *   blank —— 画一个空格，区域内的非背景像素必须为 0（字库表没串位）
 *   ink   —— 画 'M'，区域内必须有非背景像素（字真的写进帧缓冲了）
 * 只看 ink > 0 是不够的：一张整体错位（每个码位取到相邻字形）的表也会让 ink > 0；
 * 而 blank ≠ 0 说明字形里有脏行，会把每个字都糊成一团。
 *
 * 文字落在蓝区的一块黑条上，不动上面红/绿两块 —— 一次 POST 之后屏上同时留着
 * "色块对不对"和"文字对不对"两份人眼证据。
 *
 * 细节在 "[post] font " 之后由本函数自己打，与 bsl 的其它项同一行。
 */
static int app_display_check(void)
{
    const fml_font_t *font = &fml_font_ascii8x16;
    const uint16_t bg = 0x0000U;
    const uint16_t fg = 0xFFFFU;
    uint32_t x = 16U;
    uint32_t y = 344U;
    uint32_t blank;
    uint32_t ink;

    bsl_fb_fill(0U, y, board_lcd_width(), 128U, bg);

    fml_gui_text(x, y + 4U, "STM32-FREERTOS", font, fg, bg);
    fml_gui_text(x, y + 4U + font->height + 8U, "full-demo P1 display", font, fg, bg);

    y = y + 4U + (font->height + 8U) * 2U;
    fml_gui_text(x, y, " ", font, fg, bg);
    blank = bsl_fb_count_nonbg(x, y, font->width, font->height, bg);
    x += font->width;
    fml_gui_text(x, y, "M", font, fg, bg);
    ink = bsl_fb_count_nonbg(x, y, font->width, font->height, bg);

    BSL_LOG_INF(" w=");
    bsl_log_put_dec((int)font->width);
    BSL_LOG_INF(" h=");
    bsl_log_put_dec((int)font->height);
    BSL_LOG_INF(" ink=");
    bsl_log_put_dec((int)ink);
    BSL_LOG_INF(" blank=");
    bsl_log_put_dec((int)blank);
    return ((ink > 0U) && (blank == 0U)) ? 1 : 0;
}

/*
 * 文件系统自检：POST 的 fatfs 项。
 *
 * 判据两条：`f_mount` 返回 FR_OK，且根目录能列出来。
 * 只验挂载不够 —— f_mount(opt=1) 只读引导扇区、解析 BPB，FAT 表本身与根目录
 * 一个字节都没碰；BPB 完好而 FAT 表被写坏时它能报"挂载成功"，第一次读目录
 * 才暴雷。所以再走一遍 f_opendir("/") + f_readdir，把根目录项数和前几项名字
 * 打出来。files=0 仍然算 PASS：空卷是合法卷，不能因为"没东西"判故障。
 *
 * 失败时打 `fr=`（FRESULT 原值），不翻译成文字：FRESULT 与 ff.h 一一对应，
 * 多一张对照表反而容易和上游对不上。
 *
 * 不在 POST 里格式化：`f_mkfs` 会把板上 NOR 现有的内容（出厂写过的 GBK 字库）
 * 整片擦掉。要格式化必须由人在串口命令行显式发起，不能藏在自检里。
 */
/* 打一条目录项：`[NAME 123B]`，目录再加 ` DIR`。 */
static void app_fatfs_print_dirent(const fml_fatfs_dirent_t *de)
{
    BSL_LOG_INF(" [");
    bsl_log_puts(de->name);
    BSL_LOG_INF(" ");
    bsl_log_put_dec((int)de->size);
    BSL_LOG_INF("B");
    if (de->is_dir != 0U) {
        BSL_LOG_INF(" DIR");
    }
    BSL_LOG_INF("]");
}

static int app_fatfs_check(void)
{
    fml_fatfs_dirent_t de[4];
    const unsigned show = (unsigned)(sizeof(de) / sizeof(de[0]));
    unsigned total = 0U;
    unsigned i;

    if (fml_fatfs_mount() != BSL_OK) {
        BSL_LOG_INF(" fr=");
        bsl_log_put_dec(fml_fatfs_last_error());
        return 0;
    }

    BSL_LOG_INF(" total=");
    bsl_log_put_dec((int)fml_fatfs_total_kb());
    BSL_LOG_INF("KB");

    if (fml_fatfs_list_dir("/", de, show, &total) != BSL_OK) {
        BSL_LOG_INF(" list fr=");
        bsl_log_put_dec(fml_fatfs_last_error());
        return 0;
    }
    /* 叫 ents 而不是 files：根目录里的条目混着目录（"SYSTEM" 这种），
     * 打 files= 会让人以为里面有 2 个文件。 */
    BSL_LOG_INF(" ents=");
    bsl_log_put_dec((int)total);
    for (i = 0U; (i < show) && (i < total); i++) {
        app_fatfs_print_dirent(&de[i]);
    }

    /* 子目录：只是证据，不参与判据（判据是上面 mount + 根目录两步）。
     * 列出来是因为板上根目录只有一个 SYSTEM 目录，字库文件在它里面 ——
     * 这一行走的是 FAT 链（读数据簇），比只读根目录更能证明卷是真的能读。 */
    BSL_LOG_INF(" /SYSTEM:");
    if (fml_fatfs_list_dir("/SYSTEM", de, show, &total) != BSL_OK) {
        BSL_LOG_INF("fr=");
        bsl_log_put_dec(fml_fatfs_last_error());
    } else {
        BSL_LOG_INF("n=");
        bsl_log_put_dec((int)total);
        for (i = 0U; (i < show) && (i < total); i++) {
            app_fatfs_print_dirent(&de[i]);
        }
    }
    return 1;
}

int main(void)
{
    int rc;

    osl_health_init();

    rc = board_system_init();
    osl_boot_stage_note((rc == 0) ? 0U : (uint32_t)rc);
    if (rc != 0) {
        fml_panic(4U);
    }

    /* periph init 的失败必须被消费：bsl_board_periph_init() 在内部把
     * "必需设备"（binding 里 optional: false）的失败打一行日志，可选设备的
     * 失败由 fml 各自降级。返回值不必外传，(void) 明示"已消费"。这里不 panic
     * —— panic 会把单点外设故障放大成复位循环，而 boot-stage 码位是
     * board_system_init 专用的（复用会把"哪一级失败"搅在一起）。 */
    (void)bsl_board_periph_init();

    /* 注册失败要看得见：静默丢项会让 result 的 total 少 1，而没人知道为什么。 */
    if (board_post_register("font", app_display_check) == 0) {
        BSL_LOG_INF("[post] font register FAIL\r\n");
    }
    if (board_post_register("fatfs", app_fatfs_check) == 0) {
        BSL_LOG_INF("[post] fatfs register FAIL\r\n");
    }

    /* 上电自检：设备初始化之后单独跑一轮，结果只走串口。
     * 与 periph init 分开的原因：init 失败与 selftest 失败在串口上是两行。 */
    board_post_run();

    /* 样例应用：LED 1 s 红绿交替 + 四键去抖 + 蜂鸣器随按键（apl/src/app_sample.c）。
     * 框架层（fml）只提供 panic 播报，演示行为不放功能层。 */
    app_sample_start();

    osl_scheduler_start();

    fml_panic(1U);
    for (;;) {
    }
}
