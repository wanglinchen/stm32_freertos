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
#include "fml_ui.h"
#include "osl_boot.h"
#include "osl_health.h"
#include "osl_thread.h"

/*
 * 显示自检：POST 的 font 项，由 apl 注册（字库与画布都在 fml，bsl 看不见它们）。
 *
 * ASCII 判据双向：
 *   blank —— 画一个空格，区域内的非背景像素必须为 0（字库表没串位）
 *   ink   —— 画 'M'，区域内必须有非背景像素（字真的写进帧缓冲了）
 * 只看 ink > 0 是不够的：一张整体错位（每个码位取到相邻字形）的表也会让 ink > 0；
 * 而 blank ≠ 0 说明字形里有脏行，会把每个字都糊成一团。
 *
 * GBK 判据（P3）：画一行四个汉字，逐字统计 ink，每个都必须 ≥ 20。
 * 16×16 宋体一个常用字的黑点实测 40~90（"永"47 / "中"48 / "国"84），
 * 缺字、空区（10..15 区）、下标错位全部落在 0 附近 —— 只看"整行 ink > 0"
 * 抓不住"四个字里有一个没画出来"，逐字才抓得住。
 *
 * 文字落在蓝区的一块黑条上，不动上面红/绿两块 —— 一次 POST 之后屏上同时留着
 * "色块对不对"和"文字对不对（ASCII + 中文）"两份人眼证据。
 *
 * 细节在 "[post] font " 之后由本函数自己打，与 bsl 的其它项同一行。
 */
#define APP_CJK_MIN_INK 20U

static int app_display_check(void)
{
    /* "中文显示" 的 GBK 字节：D6D0 / CEC4 / CFD4 / CABE。
     * 必须写转义而不是中文字面量：C 源码是 UTF-8，直接写中文编译进去是 UTF-8
     * 字节，fml_gui_text_gbk 会把它当双字节乱切。 */
    static const char cjk_line[] = "\xD6\xD0\xCE\xC4\xCF\xD4\xCA\xBE";
    const fml_font_t *font = &fml_font_ascii8x16;
    const fml_font_t *cjk = &fml_font_gbk16;
    const uint16_t bg = 0x0000U;
    const uint16_t fg = 0xFFFFU;
    uint32_t x = 16U;
    uint32_t y = 344U;
    uint32_t blank;
    uint32_t ink;
    uint32_t min_ink = 0xFFFFFFFFU;
    uint32_t cx;
    unsigned k;

    bsl_fb_fill(0U, y, board_lcd_width(), 128U, bg);

    fml_gui_text(x, y + 4U, "STM32-FREERTOS", font, fg, bg);
    fml_gui_text(x, y + 4U + font->height + 8U, "full-demo P1 display", font, fg, bg);

    y = y + 4U + (font->height + 8U) * 2U;
    fml_gui_text(x, y, " ", font, fg, bg);
    blank = bsl_fb_count_nonbg(x, y, font->width, font->height, bg);
    x += font->width;
    fml_gui_text(x, y, "M", font, fg, bg);
    ink = bsl_fb_count_nonbg(x, y, font->width, font->height, bg);

    /* 中文一行：混排函数自己切字（ASCII 8 宽、汉字 16 宽），这里只负责统计。 */
    y += cjk->height + 12U;
    fml_gui_text_gbk(16U, y, cjk_line, fg, bg);
    cx = 16U;
    for (k = 0U; k < 4U; k++) {
        uint32_t one = bsl_fb_count_nonbg(cx, y, cjk->width, cjk->height, bg);

        if (one < min_ink) {
            min_ink = one;
        }
        BSL_LOG_INF(" c");
        bsl_log_put_dec((int)k);
        BSL_LOG_INF("=");
        bsl_log_put_dec((int)one);
        cx += cjk->width;
    }

    BSL_LOG_INF(" w=");
    bsl_log_put_dec((int)font->width);
    BSL_LOG_INF(" h=");
    bsl_log_put_dec((int)font->height);
    BSL_LOG_INF(" ink=");
    bsl_log_put_dec((int)ink);
    BSL_LOG_INF(" blank=");
    bsl_log_put_dec((int)blank);
    BSL_LOG_INF(" cjk_min=");
    bsl_log_put_dec((int)min_ink);
    return ((ink > 0U) && (blank == 0U) && (min_ink >= APP_CJK_MIN_INK)) ? 1 : 0;
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
/* 每个目录最多显示多少项。16 是照着板上最"胖"的那一层定的（长名目录里
 * 有 16 项）；超过的部分不显示但 total 仍然报全部，所以"少显示"不会被
 * 误读成"只有这么多"。栈代价：16 × sizeof(dirent) ≈ 832 B/层。 */
#define APP_FATFS_SHOW 16U
#define APP_FATFS_SUB_SHOW 4U

/* 打一条目录项：`[SHORTNAME 123B]`，目录再加 ` DIR`。
 * 用短名而不是长名：长名是 GBK 字节，落进 UTF-8 的日志文件就是乱码；
 * 短名是 ASCII，且足够定位"这个目录里有什么"。 */
static void app_fatfs_print_dirent(const fml_fatfs_dirent_t *de)
{
    BSL_LOG_INF(" [");
    bsl_log_puts(de->sname);
    BSL_LOG_INF(" ");
    bsl_log_put_dec((int)de->size);
    BSL_LOG_INF("B");
    if (de->is_dir != 0U) {
        BSL_LOG_INF(" DIR");
    }
    BSL_LOG_INF("]");
}

/* 拼 "$dir/$name"（dir 已以 "/" 结尾时不重复加）。返回 0 = 成功。 */
static int app_fatfs_join(char *dst, unsigned cap, const char *dir, const char *name)
{
    unsigned n = 0U;
    unsigned i;

    for (i = 0U; dir[i] != '\0'; i++) {
        if ((n + 1U) >= cap) {
            return 1;
        }
        dst[n++] = dir[i];
    }
    if ((n > 0U) && (dst[n - 1U] != '/')) {
        if ((n + 1U) >= cap) {
            return 1;
        }
        dst[n++] = '/';
    }
    for (i = 0U; name[i] != '\0'; i++) {
        if ((n + 1U) >= cap) {
            return 1;
        }
        dst[n++] = name[i];
    }
    dst[n] = '\0';
    return 0;
}

static int app_fatfs_check(void)
{
    fml_fatfs_dirent_t de[APP_FATFS_SHOW];
    fml_fatfs_dirent_t sub_de[APP_FATFS_SUB_SHOW];
    unsigned total = 0U;
    unsigned sub_total = 0U;
    unsigned i;
    unsigned j;
    unsigned shown = 0U;

    if (fml_fatfs_mount() != BSL_OK) {
        BSL_LOG_INF(" fr=");
        bsl_log_put_dec(fml_fatfs_last_error());
        return 0;
    }

    BSL_LOG_INF(" total=");
    bsl_log_put_dec((int)fml_fatfs_total_kb());
    BSL_LOG_INF("KB");

    if (fml_fatfs_list_dir("/", de, APP_FATFS_SHOW, &total) != BSL_OK) {
        BSL_LOG_INF(" list fr=");
        bsl_log_put_dec(fml_fatfs_last_error());
        return 0;
    }
    /* 叫 ents 而不是 files：根目录里的条目混着目录（"SYSTEM" 这种），
     * 打 files= 会让人以为里面有 2 个文件。 */
    BSL_LOG_INF(" ents=");
    bsl_log_put_dec((int)total);
    for (i = 0U; (i < APP_FATFS_SHOW) && (i < total); i++) {
        app_fatfs_print_dirent(&de[i]);
    }

    /* 子目录只是证据，不参与判据（判据是上面 mount + 根目录两步）。它走的是
     * FAT 链（读数据簇），比只读根目录更能证明卷真的能读。
     *
     * 只进前两个目录、每个最多 4 项：这一项每次启动都跑，探目录树那套递归
     * 打印会把启动日志刷成几千字符，而且它已经完成使命（P3 勘探时用过一次，
     * 结论见 docs/design/串口自检_P3勘探_6项_2026-11-24.txt）。
     *
     * 路径拼的是长名（de.name）而不是短名：短名可能是 `??????~1`（`?` 是
     * FatFs 给非法字符的占位），拿它 f_opendir 会 FR_INVALID_NAME。 */
    for (i = 0U; (i < APP_FATFS_SHOW) && (i < total) && (shown < 2U); i++) {
        char sub[64];

        if (de[i].is_dir == 0U) {
            continue;
        }
        if (app_fatfs_join(sub, (unsigned)sizeof(sub), "/", de[i].name) != 0) {
            BSL_LOG_INF(" path-too-long");
            continue;
        }
        BSL_LOG_INF(" ");
        bsl_log_puts(de[i].sname);
        BSL_LOG_INF(":");
        if (fml_fatfs_list_dir(sub, sub_de, APP_FATFS_SUB_SHOW, &sub_total) != BSL_OK) {
            BSL_LOG_INF("fr=");
            bsl_log_put_dec(fml_fatfs_last_error());
        } else {
            BSL_LOG_INF("n=");
            bsl_log_put_dec((int)sub_total);
            for (j = 0U; (j < APP_FATFS_SUB_SHOW) && (j < sub_total); j++) {
                app_fatfs_print_dirent(&sub_de[j]);
            }
        }
        shown++;
    }
    return 1;
}

/*
 * UI 自检：POST 的 ui 项（§1.1 第 21 行，自研补充项 —— 参考工程没有这一项）。
 *
 * 判据三条，缺一条都会让"看起来能用"混过去：
 *   hit —— 程序内注入 3 次点击（坐标取控件矩形中心），每次都要产出 id 与按钮
 *          对得上的 CLICK。只验"有事件"不够：id 全错的事件也能让计数非零。
 *   ink —— 每个按钮内部必须有多于背景色的像素（居中文字）。命中全对而屏上
 *          空白，说明窗口根本没画，手指点的是看不见的靶子。统计时扣掉 1 像素
 *          边框：边框本身就是非背景色，连着数会把"只有框、没有字"也算成画好了。
 *   ovf —— 事件队列溢出必须为 0。溢出等于"点了没反应"，而串口上看不出区别。
 *
 * 注入前 fml_ui_reset()：清掉可能残留的按压状态与事件，免得把上一帧的残留
 * 算进本次结果。
 */
static int app_ui_check(void)
{
    fml_ui_win_t *win = app_sample_ui_win();
    unsigned i;
    unsigned hit = 0U;
    uint32_t ink = 0U;
    unsigned ovf;

    if ((win == NULL) || (win->n_btn == 0U)) {
        BSL_LOG_INF(" win=0");
        return 0;
    }
    fml_ui_reset();
    fml_ui_draw(win);

    for (i = 0U; i < win->n_btn; i++) {
        const fml_ui_btn_t *b = &win->btn[i];

        ink += bsl_fb_count_nonbg(win->x + b->x + 1U, win->y + b->y + 1U,
                                  b->w - 2U, b->h - 2U, win->btn_bg);
    }

    for (i = 0U; i < win->n_btn; i++) {
        const fml_ui_btn_t *b = &win->btn[i];
        int32_t cx = (int32_t)(win->x + b->x + (b->w / 2U));
        int32_t cy = (int32_t)(win->y + b->y + (b->h / 2U));
        fml_ui_evt_t ev;
        unsigned ok = 0U;

        /* id 表往返：`fml_ui_btn_by_id` 返回第一个匹配的下标，两个按钮用了同一个
         * id 时这里对不上 —— 那种情况下串口会打出"点按钮 2 却报 btn=1"，
         * 只看 hit 计数是抓不住的。它同时也给这个 API 一个真实消费者，
         * 否则 --gc-sections 会把它丢掉（头里声明、镜像里没有符号）。 */
        if (fml_ui_btn_by_id(win, b->id) != (int)i) {
            continue;
        }

        fml_ui_feed(win, cx, cy, 1);
        fml_ui_feed(win, cx, cy, 0);
        while (fml_ui_next(&ev) != 0) {
            if ((ev.type == FML_UI_EVT_CLICK) && (ev.id == b->id)) {
                ok = 1U;
            }
        }
        hit += ok;
    }

    ovf = fml_ui_overflow();
    BSL_LOG_INF(" win=1 btn=");
    bsl_log_put_dec((int)win->n_btn);
    BSL_LOG_INF(" hit=");
    bsl_log_put_dec((int)hit);
    BSL_LOG_INF("/");
    bsl_log_put_dec((int)win->n_btn);
    BSL_LOG_INF(" ink=");
    bsl_log_put_dec((int)ink);
    BSL_LOG_INF(" ovf=");
    bsl_log_put_dec((int)ovf);
    return ((hit == win->n_btn) && (ink > 0U) && (ovf == 0U)) ? 1 : 0;
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

    /* P4 的演示窗口先建好：POST 的 ui 项要拿它注入点击，应用任务还没起来。
     * 幂等，app_sample_start() 会再调一次。 */
    app_sample_ui_init();

    /* 注册失败要看得见：静默丢项会让 result 的 total 少 1，而没人知道为什么。 */
    if (board_post_register("font", app_display_check) == 0) {
        BSL_LOG_INF("[post] font register FAIL\r\n");
    }
    if (board_post_register("fatfs", app_fatfs_check) == 0) {
        BSL_LOG_INF("[post] fatfs register FAIL\r\n");
    }
    if (board_post_register("ui", app_ui_check) == 0) {
        BSL_LOG_INF("[post] ui register FAIL\r\n");
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

