#include "app.h"
#include "app_sample.h"
#include "board_lcd.h"
#include "board_mpu9250.h"
#include "board_post.h"
#include "board_system.h"
#include "bsl_fb.h"
#include "bsl_init.h"
#include "bsl_log.h"
#include "fml_font.h"
#include "fml_fatfs.h"
#include "fml_gui.h"
#include "fml_picture.h"
#include "fml_panic.h"
#include "fml_qr.h"
#include "fml_shell.h"
#include "fml_ui.h"
#include "osl_boot.h"
#include "osl_health.h"
#include "osl_thread.h"

/*
 * POST 外部项的内容缓冲。
 *
 * 外部项不能自己往串口打：它的判据会注入事件、调应用的 enter/leave/event，
 * 那些回调自己会打日志。bsl 侧已经把 `[post] <item>` 前缀先打出去了，回调插
 * 进来的行就把 POST 行切成两三行（P5a、P5b 各踩到一次）。所以这里在内存里攒，
 * 交回 bsl 一次性输出（见 board_post.h 的 board_post_register 注释）。
 *
 * 不引 printf / snprintf：整数格式化够用，而 snprintf 会把格式化引擎整个拖进来。
 * 容量由 bsl 侧给（BOARD_POST_EXT_BUF）；fatfs 那一行要列两个子目录，实测约 240
 * 字符，是最长的一项。
 */
typedef struct {
    char *p;
    unsigned cap;
    unsigned len;
} app_fmt_t;

#define APP_FMT_INIT(buf_, cap_) {(buf_), (cap_), 0U}

static void app_fmt_puts(app_fmt_t *f, const char *s)
{
    while ((*s != '\0') && ((f->len + 1U) < f->cap)) {
        f->p[f->len++] = *s++;
    }
    f->p[f->len] = '\0';
}

static void app_fmt_dec(app_fmt_t *f, int value)
{
    char tmp[12];
    unsigned n = 0U;
    unsigned v = (unsigned)((value < 0) ? -value : value);

    do {
        tmp[n++] = (char)('0' + (int)(v % 10U));
        v /= 10U;
    } while ((v > 0U) && (n < (unsigned)sizeof(tmp)));
    if ((value < 0) && ((f->len + 1U) < f->cap)) {
        f->p[f->len++] = '-';
    }
    while ((n > 0U) && ((f->len + 1U) < f->cap)) {
        f->p[f->len++] = tmp[--n];
    }
    f->p[f->len] = '\0';
}

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

static int app_display_check(char *buf, unsigned cap)
{
    app_fmt_t f = APP_FMT_INIT(buf, cap);
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
        /* buf 的第一个 token 不带前导空格 —— bsl 侧在项名与内容之间已经补了一个。
         * 多补一个就会出现 "[post] font  c0=48" 这种双空格。 */
        app_fmt_puts(&f, (k == 0U) ? "c" : " c");
        app_fmt_dec(&f, (int)k);
        app_fmt_puts(&f, "=");
        app_fmt_dec(&f, (int)one);
        cx += cjk->width;
    }

    app_fmt_puts(&f, " w=");
    app_fmt_dec(&f, (int)font->width);
    app_fmt_puts(&f, " h=");
    app_fmt_dec(&f, (int)font->height);
    app_fmt_puts(&f, " ink=");
    app_fmt_dec(&f, (int)ink);
    app_fmt_puts(&f, " blank=");
    app_fmt_dec(&f, (int)blank);
    app_fmt_puts(&f, " cjk_min=");
    app_fmt_dec(&f, (int)min_ink);
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
static void app_fatfs_print_dirent(app_fmt_t *f, const fml_fatfs_dirent_t *de)
{
    app_fmt_puts(f, " [");
    app_fmt_puts(f, de->sname);
    app_fmt_puts(f, " ");
    app_fmt_dec(f, (int)de->size);
    app_fmt_puts(f, "B");
    if (de->is_dir != 0U) {
        app_fmt_puts(f, " DIR");
    }
    app_fmt_puts(f, "]");
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

static int app_fatfs_check(char *buf, unsigned cap)
{
    app_fmt_t f = APP_FMT_INIT(buf, cap);
    fml_fatfs_dirent_t de[APP_FATFS_SHOW];
    fml_fatfs_dirent_t sub_de[APP_FATFS_SUB_SHOW];
    unsigned total = 0U;
    unsigned sub_total = 0U;
    unsigned i;
    unsigned j;
    unsigned shown = 0U;

    if (fml_fatfs_mount() != BSL_OK) {
        app_fmt_puts(&f, "fr=");
        app_fmt_dec(&f, fml_fatfs_last_error());
        return 0;
    }

    app_fmt_puts(&f, "total=");
    app_fmt_dec(&f, (int)fml_fatfs_total_kb());
    app_fmt_puts(&f, "KB");

    if (fml_fatfs_list_dir("/", de, APP_FATFS_SHOW, &total) != BSL_OK) {
        app_fmt_puts(&f, " list fr=");
        app_fmt_dec(&f, fml_fatfs_last_error());
        return 0;
    }
    /* 叫 ents 而不是 files：根目录里的条目混着目录（"SYSTEM" 这种），
     * 打 files= 会让人以为里面有 2 个文件。 */
    app_fmt_puts(&f, " ents=");
    app_fmt_dec(&f, (int)total);
    for (i = 0U; (i < APP_FATFS_SHOW) && (i < total); i++) {
        app_fatfs_print_dirent(&f, &de[i]);
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
            app_fmt_puts(&f, " path-too-long");
            continue;
        }
        app_fmt_puts(&f, " ");
        app_fmt_puts(&f, de[i].sname);
        app_fmt_puts(&f, ":");
        if (fml_fatfs_list_dir(sub, sub_de, APP_FATFS_SUB_SHOW, &sub_total) != BSL_OK) {
            app_fmt_puts(&f, "fr=");
            app_fmt_dec(&f, fml_fatfs_last_error());
        } else {
            app_fmt_puts(&f, "n=");
            app_fmt_dec(&f, (int)sub_total);
            for (j = 0U; (j < APP_FATFS_SUB_SHOW) && (j < sub_total); j++) {
                app_fatfs_print_dirent(&f, &sub_de[j]);
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
static int app_ui_check(char *buf, unsigned cap)
{
    app_fmt_t f = APP_FMT_INIT(buf, cap);
    fml_ui_win_t *win = app_menu_win();
    unsigned i;
    unsigned hit = 0U;
    uint32_t ink = 0U;
    unsigned ovf;

    if ((win == NULL) || (win->n_btn == 0U)) {
        app_fmt_puts(&f, "win=0");
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
    app_fmt_puts(&f, "win=1 btn=");
    app_fmt_dec(&f, (int)win->n_btn);
    app_fmt_puts(&f, " hit=");
    app_fmt_dec(&f, (int)hit);
    app_fmt_puts(&f, "/");
    app_fmt_dec(&f, (int)win->n_btn);
    app_fmt_puts(&f, " ink=");
    app_fmt_dec(&f, (int)ink);
    app_fmt_puts(&f, " ovf=");
    app_fmt_dec(&f, (int)ovf);
    return ((hit == win->n_btn) && (ink > 0U) && (ovf == 0U)) ? 1 : 0;
}

/*
 * 桌面与应用切换：POST 的 menu 项（§1.1 第 21 行）。
 *
 * 判据用动作码而不是"看屏"：同一图标注入两次 CLICK，第一次必须是 SELECT、
 * 第二次必须是 ENTER（双击判定），然后注入 BACK(0xFF) 必须得到 BACK，
 * 且 depth 回到 0。四件事缺一条都说明状态机没走通：
 *   - 两次都 SELECT → 双击窗口没生效（阈值/时间戳写错）；
 *   - 第一次就 ENTER → 残留的 pending 没被清，用户单击就会误进应用；
 *   - BACK 不返回 → 用户进了应用回不来，只能复位。
 *
 * 这里走的是 app_menu_dispatch，与任务里那条路径是同一个函数 —— 不会出现
 * "POST 通过、手指不通过"的两套逻辑。
 *
 * P5b 起再加三项，都走同一条注入路径：
 *   - enter2：注册表第 2、3 项也各走一遍进入-返回（P5a 只验了第 1 项，
 *     另两个应用的 enter 画面与 leave 从未被执行过）；
 *   - calc：按**标签**取 id 注入 1 + 2 =，读 app_calc_display() 判 3 ——
 *     判据不能停在"按钮画出来了"，那与"算得对不对"无关；
 *   - set：设置页连切两次背光，判 flips==2 且状态回到初值。用翻转计数而不是
 *     只看终值：两次都没生效时终值同样等于初值，光看终值会把"完全坏了"判成通过。
 */
static int menu_label_is(const char *a, const char *b)
{
    while ((*a != '\0') && (*a == *b)) {
        a++;
        b++;
    }
    return (*a == *b) ? 1 : 0;
}

static uint16_t menu_id_by_label(const fml_ui_win_t *win, const char *label)
{
    unsigned i;

    if (win == NULL) {
        return 0U;
    }
    for (i = 0U; i < win->n_btn; i++) {
        if ((win->btn[i].label != NULL) && (menu_label_is(win->btn[i].label, label) != 0)) {
            return win->btn[i].id;
        }
    }
    return 0U;
}

/* 4 位小写十六进制。给 `note=` 用：只打字节数说明不了"落进去的是哪两个字"，
 * 而这条判据的全部意义就是那对字节。 */
static void app_fmt_hex4(app_fmt_t *f, unsigned v)
{
    static const char hx[] = "0123456789abcdef";
    char s[5];

    s[0] = hx[(v >> 12) & 0xFU];
    s[1] = hx[(v >> 8) & 0xFU];
    s[2] = hx[(v >> 4) & 0xFU];
    s[3] = hx[v & 0xFU];
    s[4] = '\0';
    app_fmt_puts(f, s);
}

/* 同一个图标注入两次 CLICK = 双击。两次调用的间隔是 0，必然落在任何合法的
 * 双击窗口内 —— 这里验的是"双击状态机通"，不是"阈值大小合适"。 */
static int menu_enter_app(const app_desc_t *app)
{
    fml_ui_evt_t ev;

    if (app == NULL) {
        return 0;
    }
    ev.type = FML_UI_EVT_CLICK;
    ev.id = app->id;
    ev.x = 0;
    ev.y = 0;
    (void)app_menu_dispatch(&ev);
    return (app_menu_dispatch(&ev) == APP_ACTION_ENTER) ? 1 : 0;
}

static int menu_back(void)
{
    fml_ui_evt_t ev;

    ev.type = FML_UI_EVT_CLICK;
    ev.id = APP_ID_BACK;
    ev.x = 0;
    ev.y = 0;
    return (app_menu_dispatch(&ev) == APP_ACTION_BACK) ? 1 : 0;
}

/* 应用窗口是否整块落在屏幕上。
 *
 * 这三个应用（看图 / 记事本 / 日历）的窗口尺寸原来写死 800×480，P8a 改成按屏取。
 * 写死的后果在 480×800 竖屏上是"右侧 320 像素连同 BACK 按钮一起没了"，而窗口
 * 本身照样建得出来、按钮照样点得动 —— 光判 "进来 / 退出" 一个都发现不了。
 * 所以把窗口宽高与 board_lcd_width()/height() 逐项对拍，并把实测值打进串口：
 * 判据失败时能当场看出是宽不对、高不对，还是两个都不对。 */
static int menu_win_full(const app_desc_t *app, uint32_t *w, uint32_t *h)
{
    fml_ui_win_t *win;

    if ((app == NULL) || (app->win == NULL)) {
        return 0;
    }
    win = app->win();
    if (win == NULL) {
        return 0;
    }
    *w = win->w;
    *h = win->h;
    return ((win->w == board_lcd_width()) && (win->h == board_lcd_height())) ? 1 : 0;
}

/* 在当前应用的窗口里按标签点一次。返回 1 = 事件确实转交给了应用。 */
static int menu_click_label(const char *label)
{
    fml_ui_evt_t ev;
    uint16_t id = menu_id_by_label(app_menu_current_win(), label);

    if (id == 0U) {
        return 0;
    }
    ev.type = FML_UI_EVT_CLICK;
    ev.id = id;
    ev.x = 0;
    ev.y = 0;
    return (app_menu_dispatch(&ev) == APP_ACTION_APP) ? 1 : 0;
}

/* 按 id 直接点一次。候选按钮的 label 是 NULL（汉字要 GBK 渲染，fml_ui 的
 * ASCII 标签画不了），所以这些键只能按 id 找 —— `menu_click_label` 对它们是
 * 瞎的，这不是退路而是唯一路径。 */
static int menu_click_id(uint16_t id)
{
    fml_ui_evt_t ev;

    if (id == 0U) {
        return 0;
    }
    ev.type = FML_UI_EVT_CLICK;
    ev.id = id;
    ev.x = 0;
    ev.y = 0;
    return (app_menu_dispatch(&ev) == APP_ACTION_APP) ? 1 : 0;
}

/*
 * 二维码的三个定位图案：左上 / 右上 / 左下各一个 7×7 的"回"字。
 *
 * 判据为什么挑它：编码器把位流、纠错码、掩码都算完之后，最后一定要把这三个
 * 图案盖回去（任何合法的 QR 符号都有）。若挖 "1/3/4 位越界" 这类 bug ——
 * 矩阵错位、掩码盖到角上、行列转置 —— 第一个坏的就是这三块；而"黑模块
 * 占比在 25%~75%"这种统计判据，错位后往往照样落在区间里。
 */
static int menu_qr_finder(unsigned x0, unsigned y0)
{
    unsigned r;
    unsigned c;

    for (r = 0U; r < 7U; r++) {
        for (c = 0U; c < 7U; c++) {
            const int ring = ((r == 0U) || (r == 6U) || (c == 0U) || (c == 6U)) ? 1 : 0;
            const int core = ((r >= 2U) && (r <= 4U) && (c >= 2U) && (c <= 4U)) ? 1 : 0;
            const int want = (ring != 0 || core != 0) ? 1 : 0;

            if (fml_qr_module(x0 + c, y0 + r) != want) {
                return 0;
            }
        }
    }
    return 1;
}

static int app_menu_check(char *buf, unsigned cap)
{
    app_fmt_t f = APP_FMT_INIT(buf, cap);
    /* pic / note / cal 三段的读数先攒进同一条尾巴串：它们是在各自的"进入应用"
     * 段落里算出来的，而输出顺序要按 apps→enter→…→depth 排。把 f 拿在那几段里
     * 直接写，字段就会跑到 apps 前面，读日志的人得来回找。 */
    /* 尾部字段串。P5c 时只有 pic/note/cal 三段、48 字节够；P8a 加 view、
     * P8b 加 paint 之后已经超过 48 —— 放不下时 app_fmt_puts 直接截断，
     * 结果是 `[post] menu` 那一行后半段消失、解析器读不到 depth（判 FAIL
     * 却看不出为什么）。128 是"再多一两个字段也不用改"的余量，栈上 128 B。 */
    char tail_s[256];
    app_fmt_t ft = APP_FMT_INIT(tail_s, (unsigned)sizeof(tail_s));
    const unsigned apps = app_count();
    const app_desc_t *first = app_at(0U);
    int enter = 0;
    int back = 0;
    int enter2 = 0;
    int calc_ok = 0;
    int set_flips = 0;
    int pic_ok = 0;
    int note_ok = 0;
    int cal_ok = 0;
    int paint_ok = 0;
    int imu_ok = 0;
    int level_ok = 0;
    int qr_ok = 0;
    int book_ok = 0;
    int music_ok = 0;
    int view_ok = 1;
    uint32_t vw_p = 0U;
    uint32_t vh_p = 0U;
    uint32_t vw_n = 0U;
    uint32_t vh_n = 0U;
    uint32_t vw_c = 0U;
    uint32_t vh_c = 0U;
    unsigned leave;
    unsigned depth;
    unsigned i;

    if ((apps == 0U) || (first == NULL)) {
        app_fmt_puts(&f, "apps=0");
        return 0;
    }
    /* 第 1 项：单击一次 → SELECT（不产 ENTER），再点一次 → ENTER。 */
    enter = menu_enter_app(first);
    back = menu_back();

    /* 第 2、3 项：各走一遍进入-返回。 */
    for (i = 1U; (i < apps) && (i < 3U); i++) {
        if ((menu_enter_app(app_at(i)) != 0) && (menu_back() != 0)) {
            enter2++;
        }
    }

    /* 计算器（第 4 项）：1 + 2 = 应该是 3。 */
    if ((apps > 3U) && (menu_enter_app(app_at(3U)) != 0)) {
        if ((menu_click_label("1") != 0) && (menu_click_label("+") != 0) &&
            (menu_click_label("2") != 0) && (menu_click_label("=") != 0) &&
            (menu_label_is(app_calc_display(), "3") != 0)) {
            calc_ok = 1;
        }
        (void)menu_back();
    }

    /* 设置页（第 5 项）：切两次背光。 */
    if ((apps > 4U) && (menu_enter_app(app_at(4U)) != 0)) {
        const unsigned flips0 = app_settings_flips();
        const int bl0 = app_settings_backlight();

        (void)menu_click_label("BL");
        (void)menu_click_label("BL");
        /* 两次都生效 ⇒ 计数恰好 +2，且状态回到初值（1 → 0 → 1）。
         * 只判终值会把"两次都没生效"判成通过。 */
        if ((app_settings_flips() == (flips0 + 2U)) &&
            (app_settings_backlight() == bl0)) {
            set_flips = 2;
        }
        (void)menu_back();
    }

    /* 图片查看器（第 6 项）：进入后读它解码出的头部。
     *
     * 判据是**长度反推**：`行字节 × 高 + data_off` 必须正好等于文件长度。
     * 卷里唯一那张图是 320×240 / 24 位 / data_off 54 / 230454 B，等式一成立，
     * 说明 bpp、宽、高、数据偏移四个字段全解析对了；任何一位错位，乘积就对不上
     * 230400。这比"屏上看着像张图"硬得多 —— 错位解码出来的画面仍然"像"图。
     *
     * 再加 ink>0：全黑图（或根本没画）在屏上也是黑的，只有像素计数分得开。 */
    if ((apps > 5U) && (menu_enter_app(app_at(5U)) != 0)) {
        int32_t pw = 0;
        int32_t ph = 0;
        uint16_t pbpp = 0U;
        uint32_t ink = 0U;
        fml_picture_info_t pi;

        if (menu_win_full(app_at(5U), &vw_p, &vh_p) == 0) {
            view_ok = 0;
        }

        if ((app_picviewer_stat(&pw, &ph, &pbpp, &ink) != 0) && (ink > 0U) &&
            (fml_picture_info(app_picviewer_path(), &pi) == BSL_OK)) {
            const uint16_t b = pi.bpp;
            const uint32_t rowb = ((uint32_t)pi.w * (uint32_t)b + 31U) / 32U * 4U;
            const uint32_t payload = rowb * (uint32_t)pi.h;

            if ((payload + pi.data_off) == pi.file_size) {
                pic_ok = 1;
            }
            app_fmt_puts(&ft, "pic=");
            app_fmt_dec(&ft, (int)pi.w);
            app_fmt_puts(&ft, "x");
            app_fmt_dec(&ft, (int)pi.h);
            app_fmt_puts(&ft, "b");
            app_fmt_dec(&ft, (int)b);
        } else {
            app_fmt_puts(&ft, "pic=0");
        }
        (void)menu_back();
    } else {
        app_fmt_puts(&ft, "pic=-");
    }

    /* 记事本（第 7 项）：94664 = zhong 的数字映射（z9 h4 o6 n6 g4）。
     *
     * 但这串数字同时也是 **xiong**（x9 i4 o6 n6 g4）—— 九宫格的固有歧义，
     * P5c 第一版"只取表里第一个音节"，于是打出的是 xiong 首字"胸"(0xD0D6)，
     * 上板日志 note=2:d0d6 就是它。现在候选条跨音节连续编号：
     * xiong 的 7 个字占 0..6，zhong 从 7 开始，所以"中"在第 2 页第 3 格
     * （flat 7 = 1*5+2 ⇒ id 102）。
     *
     * 判据三件套一起进：文本字节对 0xD6D0、音节数 2、候选总数 18。
     * 词库重生成、映射改错、翻页逻辑错位，都会让其中之一当场变。 */
    if ((apps > 6U) && (menu_enter_app(app_at(6U)) != 0)) {
        unsigned syls;
        unsigned cand;
        const char *t;
        unsigned len;

        if (menu_win_full(app_at(6U), &vw_n, &vh_n) == 0) {
            view_ok = 0;
        }

        (void)menu_click_label("9");
        (void)menu_click_label("4");
        (void)menu_click_label("6");
        (void)menu_click_label("6");
        (void)menu_click_label("4");
        /* 匹配集要在"点候选之前"读：选中一个字之后拼音串就被清空、界面上的
         * 候选条也跟着空了，那时候再问就是 0 —— 不是没匹配，是已经用掉了。 */
        syls = app_notepad_syl_count();
        cand = app_notepad_cand_total();
        (void)menu_click_id(APP_NOTE_ID_NEXT);
        (void)menu_click_id((uint16_t)(APP_NOTE_ID_CAND0 + 2U));

        t = app_notepad_text();
        len = app_notepad_len();
        if ((len == 2U) && (t != NULL) &&
            ((uint8_t)t[0] == 0xD6U) && ((uint8_t)t[1] == 0xD0U) &&
            (syls == 2U) && (cand == 18U)) {
            note_ok = 1;
        }
        app_fmt_puts(&ft, " note=");
        app_fmt_dec(&ft, (int)len);
        /* 尾部补字节对：len==2 却 FAIL 时，光看 2 分不出"拼音解析成了别的字"、
         * "字节序反了"还是"插进去了但被 DEL 削掉一字节"。 */
        if ((len >= 2U) && (t != NULL)) {
            unsigned pair = ((unsigned)(uint8_t)t[0] << 8) | (unsigned)(uint8_t)t[1];

            app_fmt_puts(&ft, ":");
            app_fmt_hex4(&ft, pair);
        }
        app_fmt_puts(&ft, "/");
        app_fmt_dec(&ft, (int)syls);
        app_fmt_puts(&ft, ":");
        app_fmt_dec(&ft, (int)cand);
        (void)menu_back();
    } else {
        app_fmt_puts(&ft, " note=-");
    }

    /* 日历（第 8 项）：没有 RTC，起点是编译时刻，所以只判"字段范围合法"。
     * 判"时间准不准"需要 LSE + 备用电池，那是 P6 的 rtc 项。 */
    if ((apps > 7U) && (menu_enter_app(app_at(7U)) != 0)) {
        int cy = 0;
        int cm = 0;
        int cd = 0;
        int chh = 0;
        int cmm = 0;
        int css = 0;

        if (menu_win_full(app_at(7U), &vw_c, &vh_c) == 0) {
            view_ok = 0;
        }
        app_calendar_now(&cy, &cm, &cd, &chh, &cmm, &css);
        if ((cy >= 2020) && (cy <= 2200) && (cm >= 1) && (cm <= 12) && (cd >= 1) &&
            (cd <= 31) && (chh >= 0) && (chh < 24) && (cmm >= 0) && (cmm < 60) &&
            (css >= 0) && (css < 60)) {
            cal_ok = 1;
        }
        app_fmt_puts(&ft, " cal=");
        app_fmt_dec(&ft, cy);
        if (cm < 10) {
            app_fmt_puts(&ft, "0");
        }
        app_fmt_dec(&ft, cm);
        if (cd < 10) {
            app_fmt_puts(&ft, "0");
        }
        app_fmt_dec(&ft, cd);
        (void)menu_back();
    } else {
        app_fmt_puts(&ft, " cal=-");
    }

    /* 绘画（第 9 项）：往画布注入一条 DOWN → MOVE → MOVE → UP 的斜线，
     * 然后**在帧缓冲里数非黑像素**。判"应用自己记的笔数"是不够的：计数器
     * 即使在画之前 +1 也照样 PASS，而像素数为 0 说明笔迹根本没落到屏上
     * （坐标越界、颜色等于底色、被窗口重绘盖掉，都长这样）。 */
    if ((apps > 8U) && (menu_enter_app(app_at(8U)) != 0)) {
        int cx = 0;
        int cy = 0;
        int cw = 0;
        int ch = 0;
        fml_ui_evt_t pev;
        uint32_t inked;

        app_paint_canvas(&cx, &cy, &cw, &ch);
        pev.type = FML_UI_EVT_DOWN;
        pev.id = 0U;
        pev.x = cx + 8;
        pev.y = cy + 8;
        (void)app_menu_dispatch(&pev);
        pev.type = FML_UI_EVT_MOVE;
        pev.x = cx + (cw / 2);
        pev.y = cy + (ch / 2);
        (void)app_menu_dispatch(&pev);
        pev.type = FML_UI_EVT_MOVE;
        pev.x = cx + cw - 8;
        pev.y = cy + ch - 8;
        (void)app_menu_dispatch(&pev);
        pev.type = FML_UI_EVT_UP;
        (void)app_menu_dispatch(&pev);
        /* 去掉 1 像素边框：边框与画布底色不同，混进判据会让"一笔没画"也 > 0。 */
        inked = bsl_fb_count_nonbg((uint32_t)(cx + 1), (uint32_t)(cy + 1),
                                   (uint32_t)(cw - 2), (uint32_t)(ch - 2), 0x0000U);
        if (inked > 0U) {
            paint_ok = 1;
        }
        app_fmt_puts(&ft, " paint=");
        app_fmt_dec(&ft, (int)inked);
        (void)menu_back();
    } else {
        app_fmt_puts(&ft, " paint=-");
    }

    /* 陀螺仪（第 10 项）与水平仪（第 11 项）。
     *
     * 两者都从应用的模块级 static 取数（应用在 enter 里读过一次），**不重读总线**：
     * 重读会让"应用显示的那一份"和"POST 判的那一份"变成两次采样，板子一晃就
     * 对不上。判据分工：
     *   imu   —— WHO_AM_I == 0x71（应用拿到的确实是 MPU9250）且三轴不全为 0
     *            （全 0 说明读回来的是一批默认值，I2C 通但数据没来）；
     *   level —— 倾角在 [-90.0, 90.0] 且与 imu 的 ax/az 自洽（|dd| 与
     *            ax·573/|az| 相差不超过 20，即允许两次采样间隔里的抖动）。 */
    if ((apps > 9U) && (menu_enter_app(app_at(9U)) != 0)) {
        int16_t acc[3];
        int16_t gyr[3];

        acc[0] = 0;
        acc[1] = 0;
        acc[2] = 0;
        gyr[0] = 0;
        gyr[1] = 0;
        gyr[2] = 0;
        if ((app_imu_sample(acc, gyr) != 0) &&
            (board_mpu9250_who() == 0x71U) &&
            ((acc[0] != 0) || (acc[1] != 0) || (acc[2] != 0))) {
            imu_ok = 1;
        }
        app_fmt_puts(&ft, " imu=");
        app_fmt_hex4(&ft, (unsigned)board_mpu9250_who());
        app_fmt_puts(&ft, "/");
        app_fmt_dec(&ft, (int)acc[0]);
        app_fmt_puts(&ft, ",");
        app_fmt_dec(&ft, (int)acc[1]);
        app_fmt_puts(&ft, ",");
        app_fmt_dec(&ft, (int)acc[2]);
        (void)menu_back();
    } else {
        app_fmt_puts(&ft, " imu=-");
    }

    if ((apps > 10U) && (menu_enter_app(app_at(10U)) != 0)) {
        const int dd = app_level_angle_dd();
        int16_t acc[3];
        int expect = 0;

        acc[0] = 0;
        acc[1] = 0;
        acc[2] = 0;
        if (app_level_acc(acc) != 0) {
            int az = (acc[2] < 0) ? -(int)acc[2] : (int)acc[2];

            if (az < 64) {
                az = 64;
            }
            expect = ((int)acc[0] * 573) / az;
        }
        if ((dd >= -900) && (dd <= 900)) {
            int diff = dd - expect;

            if (diff < 0) {
                diff = -diff;
            }
            /* 水平仪自己在 enter 里读了一次、这里又取 imu 的样本：两次采样之间
             * 板子没动，差 20（=2.0°）以内算自洽。 */
            if (diff <= 20) {
                level_ok = 1;
            }
        }
        app_fmt_puts(&ft, " level=");
        if (dd < 0) {
            app_fmt_puts(&ft, "-");
        }
        app_fmt_dec(&ft, ((dd < 0) ? -dd : dd) / 10);
        app_fmt_puts(&ft, ".");
        app_fmt_dec(&ft, ((dd < 0) ? -dd : dd) % 10);
        (void)menu_back();
    } else {
        app_fmt_puts(&ft, " level=-");
    }

    /* 二维码（第 12 项）。四项判据，各挡一类错：
     *   1. 边长 = 21 + 4×(版本-1)：版本表/符号尺寸算错时这里第一枪；
     *   2. 黑模块占比 25%~75%：全黑/全白/大半张空（编码只写了一半位流）都过不去；
     *   3. 三个 7×7 定位图案原样：挡矩阵错位、转置、掩码盖角；
     *   4. 换一条串重编码，黑模块数必须变：挡"不管输入什么都画同一张图"
     *      （矩阵没写全、编码结果被上一次的 static 覆盖都会长这样）。
     * 另加一条自洽：应用缓存的 dark 与 fml_qr 现算的必须相等 —— 应用若在
     * 编码失败后还报旧值，这里当场露馅。 */
    if ((apps > 11U) && (menu_enter_app(app_at(11U)) != 0)) {
        unsigned v1 = app_qr_version();
        unsigned s1 = app_qr_size();
        unsigned d1 = app_qr_dark();
        unsigned d2 = 0U;
        int qx = 0;
        int qy = 0;
        int qw = 0;
        int qh = 0;
        uint32_t plate_ink = 0U;
        int ok = 0;

        if ((v1 >= 1U) && (v1 <= 40U) && (s1 == (21U + (4U * (v1 - 1U)))) &&
            (s1 <= FML_QR_MAX_MODULES)) {
            ok++;
        }
        if ((s1 > 0U) && (d1 <= (s1 * s1)) && (d1 == fml_qr_dark())) {
            const unsigned pct = (d1 * 100U) / (s1 * s1);

            if ((pct >= 25U) && (pct <= 75U)) {
                ok++;
            }
        }
        if ((s1 >= 7U) && (menu_qr_finder(0U, 0U) != 0) &&
            (menu_qr_finder(s1 - 7U, 0U) != 0) && (menu_qr_finder(0U, s1 - 7U) != 0)) {
            ok++;
        }
        /* 第五项：白底板里的非白像素必须 > 0。这一步不看编码结果，只看帧缓冲：
         * 底板坐标算错、被文字行盖掉、颜色写反，前四项都照样绿。 */
        app_qr_plate(&qx, &qy, &qw, &qh);
        if ((qw > 0) && (qh > 0)) {
            plate_ink = bsl_fb_count_nonbg((uint32_t)qx, (uint32_t)qy,
                                           (uint32_t)qw, (uint32_t)qh, 0xFFFFU);
            if (plate_ink > 0U) {
                ok++;
            }
        }
        if ((app_qr_inject("https://openedv.com") == 0) &&
            (app_qr_dark() != d1) && (app_qr_dark() == fml_qr_dark())) {
            d2 = app_qr_dark();
            ok++;
        }
        qr_ok = (ok == 5) ? 1 : 0;
        app_fmt_puts(&ft, " qr=");
        app_fmt_dec(&ft, (int)v1);
        app_fmt_puts(&ft, "/");
        app_fmt_dec(&ft, (int)s1);
        app_fmt_puts(&ft, "/");
        app_fmt_dec(&ft, (int)d1);
        app_fmt_puts(&ft, "/");
        app_fmt_dec(&ft, (int)d2);
        app_fmt_puts(&ft, "/");
        app_fmt_dec(&ft, (int)plate_ink);
        (void)menu_back();
    } else {
        app_fmt_puts(&ft, " qr=-");
    }

    /* 电子书（第 13 项，P9e-1）。判据不用"文件在不在"代替"阅读器通不通"：
     *   pages —— 分页算出的总页数（>= 2：太短或折行把整段算成一行都会掉到这）
     *   chars —— 折行时逐字计出的字形个数（>= 200：防"读到 0 字节"或把所有字节
     *            都当空白跳过）
     *   ink1  —— 首页文字区的非黑像素（> 0：行数对而一个像素没落屏也拦得住）
     *   turn  —— 真翻到第 2 页、第 2 页也见墨、再翻回首页。只读 pages 的话，
     *            NEXT 按钮接不上、或者翻页后画的是同一页，全部照样绿
     */
    if ((apps > 12U) && (menu_enter_app(app_at(12U)) != 0)) {
        app_ebook_info_t b1;
        app_ebook_info_t b2;
        int turn_ok = 0;

        (void)memset(&b1, 0, sizeof(b1));
        (void)memset(&b2, 0, sizeof(b2));
        if (app_ebook_info(&b1) != 0) {
            if (app_ebook_turn(1) == 0) {
                (void)app_ebook_info(&b2);
                if ((b2.ink > 0U) && (b2.page == (b1.page + 1U))) {
                    turn_ok = 1;
                }
                (void)app_ebook_turn(-1); /* 回首页，留给后续段一个干净状态 */
            }
        }
        if ((b1.pages >= 2U) && (b1.chars >= 200U) && (b1.ink > 0U) && (turn_ok != 0)) {
            book_ok = 1;
        }
        /* p/c/ink1/ink2/page2/L/B/src：page2 是翻页后的页号（应为 1），
         * 它大于 1 说明"页号没动"而不是"翻页后画空了"，两种故障分开看。 */
        app_fmt_puts(&ft, " book=");
        app_fmt_dec(&ft, (int)b1.pages);
        app_fmt_puts(&ft, "/");
        app_fmt_dec(&ft, (int)b1.chars);
        app_fmt_puts(&ft, "/");
        app_fmt_dec(&ft, (int)b1.ink);
        app_fmt_puts(&ft, "/");
        app_fmt_dec(&ft, (int)b2.ink);
        app_fmt_puts(&ft, "/");
        app_fmt_dec(&ft, (int)b2.page);
        app_fmt_puts(&ft, "/");
        app_fmt_dec(&ft, (int)b1.lines);
        app_fmt_puts(&ft, "/");
        app_fmt_dec(&ft, (int)b1.blank);
        app_fmt_puts(&ft, ((b1.from_file != 0) ? "/f" : "/s"));
        (void)menu_back();
    } else {
        app_fmt_puts(&ft, " book=-");
    }

    /* 音乐播放（第 14 项，P9e-2）。判据不在这里复制一份：参数合法性、两块是否真
     * 发完、SAI 有没有报错、数据流时长与 fs 的 ±5%，全在 app_music_selftest 里
     * （那是"知道 fs 与要发多少帧"的那一层）。这里只做"进门 → 取报告 → 拼字段"。
     *   rate/ch/bps —— 解析出来的素材参数（合法范围由 selftest 判）
     *   frames      —— 源帧数（0 说明头解析失败或 data 块空）
     *   played      —— 自检实际发出的输出帧（< 4096 说明第二块没发完）
     *   ms          —— SAI 侧数据流毫秒（2 块应 ≈ 85）
     *   err         —— BSL 错误码取正打印（0 = 没出错）
     *   f/s         —— 素材来自卷里文件还是内置样本
     */
    if ((apps > 13U) && (menu_enter_app(app_at(13U)) != 0)) {
        app_music_report_t mr;

        (void)memset(&mr, 0, sizeof(mr));
        if (app_music_selftest(&mr) != 0) {
            music_ok = 1;
        }
        app_fmt_puts(&ft, " music=");
        app_fmt_dec(&ft, (int)mr.rate);
        app_fmt_puts(&ft, "/");
        app_fmt_dec(&ft, (int)mr.channels);
        app_fmt_puts(&ft, "/");
        app_fmt_dec(&ft, (int)mr.bits);
        app_fmt_puts(&ft, "/");
        app_fmt_dec(&ft, (int)mr.frames);
        app_fmt_puts(&ft, "/");
        app_fmt_dec(&ft, (int)mr.played);
        app_fmt_puts(&ft, "/");
        app_fmt_dec(&ft, (int)(mr.elapsed_us / 1000U));
        app_fmt_puts(&ft, "/");
        app_fmt_dec(&ft, (int)((mr.err < 0) ? -mr.err : mr.err));
        app_fmt_puts(&ft, ((mr.from_file != 0) ? "/f" : "/s"));
        (void)menu_back();
    } else {
        app_fmt_puts(&ft, " music=-");
    }

    depth = app_menu_depth();
    leave = app_menu_leave_count();    app_fmt_puts(&f, "apps=");
    app_fmt_dec(&f, (int)apps);
    app_fmt_puts(&f, " enter=");
    app_fmt_dec(&f, enter);
    app_fmt_puts(&f, " back=");
    app_fmt_dec(&f, back);
    app_fmt_puts(&f, " leave=");
    app_fmt_dec(&f, (int)leave);
    app_fmt_puts(&f, " enter2=");
    app_fmt_dec(&f, enter2);
    app_fmt_puts(&f, " calc=");
    app_fmt_puts(&f, app_calc_display());
    app_fmt_puts(&f, " set=");
    app_fmt_dec(&f, set_flips);
    /* 三项读数挤在一条尾巴串里，规范是"每个字段前一个空格、整串前面再无空格"：
     * bsl 会在项名与内容之间补一个分隔空格，这里再带前导空格就成双空格了
     * （P5b 踩过）。 */
    app_fmt_puts(&f, " ");
    app_fmt_puts(&f, tail_s);
    /* 三个应用窗口的实测尺寸，按 看图/记事本/日历 的顺序打。三者都必须等于
     * 屏尺寸：P8a 之前它们写死 800×480，竖屏下窗口比屏宽 320 像素，
     * 右半边（含 BACK）直接落在屏外 —— 而 enter/back/leave 三项照样全绿。 */
    app_fmt_puts(&f, " view=");
    app_fmt_dec(&f, (int)vw_p);
    app_fmt_puts(&f, "x");
    app_fmt_dec(&f, (int)vh_p);
    app_fmt_puts(&f, "/");
    app_fmt_dec(&f, (int)vw_n);
    app_fmt_puts(&f, "x");
    app_fmt_dec(&f, (int)vh_n);
    app_fmt_puts(&f, "/");
    app_fmt_dec(&f, (int)vw_c);
    app_fmt_puts(&f, "x");
    app_fmt_dec(&f, (int)vh_c);
    app_fmt_puts(&f, " depth=");
    app_fmt_dec(&f, (int)depth);
    /* leave 必须等于 apps：POST 把每个应用都走了一遍，每次 BACK 都该经过
     * app_leave_current。若它小于 apps，说明某个应用的退出路径没走全，
     * 而 depth 也可能碰巧是 0，光看 depth 判不出来。
     * calc_ok / set_flips 也进判据：这两项各自有独立的读数，不并进去就等于
     * "打出来给人看、机器不判"，回归时没人会去比对那两列。 */
    return (((enter != 0) && (back != 0)) &&
            ((leave >= apps) && (depth == 0U)) &&
            (calc_ok != 0) && (set_flips == 2) && (pic_ok != 0) &&
            (note_ok != 0) && (cal_ok != 0) && (view_ok != 0) &&
            (paint_ok != 0) && (imu_ok != 0) && (level_ok != 0) && (qr_ok != 0) &&
            (book_ok != 0) && (music_ok != 0)) ? 1 : 0;
}

/*
 * 串口命令行自检：POST 的 shell 项（P6b，自研补充项 —— 参考工程的 USMART 不在
 * system_init 的 20 项里，按登记规则落在第 23 行之后）。
 *
 * **这一项验不到"串口能收"**，必须先说清：POST 在 `osl_scheduler_start()` 之前
 * 运行（见 main 末尾），shell 的接收任务那时还不存在；而且接收需要人手在终端
 * 敲字。POST 能验的是"命令表在、分发通、执行器真能执行"三件事 —— 走的就是
 * 人敲键时那条 `fml_shell_exec()`。真接收只能靠 HIL（见 上板验证.md 第 32 项）。
 *
 * 判据三条：
 *   cmd    —— 命令表条数 > 0（表被 --gc-sections 整段丢掉时这里是 0）
 *   help   —— `help` 必须返回 OK 且输出非空：证明查找 + 执行器 + 输出拼接全通
 *   uptime —— 一个**会返回值**的执行器（osl tick），把数字解析出来；只判
 *             "help 有输出"不够，那只能证明有一个命令能跑
 */
static int app_shell_dec(const char *s, const char *key, int *out)
{
    unsigned i = 0U;
    unsigned k = 0U;
    int v = 0;

    while ((s[i] != '\0') && (key[k] != '\0') && (s[i] == key[k])) {
        i++;
        k++;
    }
    if ((key[k] != '\0') || (s[i] != '=')) {
        return 1;
    }
    i++;
    if ((s[i] < '0') || (s[i] > '9')) {
        return 1;
    }
    while ((s[i] >= '0') && (s[i] <= '9')) {
        v = v * 10 + (int)(s[i] - '0');
        i++;
    }
    *out = v;
    return 0;
}

static int app_shell_check(char *buf, unsigned cap)
{
    app_fmt_t f = APP_FMT_INIT(buf, cap);
    char out_help[96];
    char out_up[64];
    unsigned cmds;
    int help_ok = 0;
    int up_ok = 0;
    int uptime = -1;

    if (fml_shell_init() != BSL_OK) {
        app_fmt_puts(&f, "init=err");
        return 0;
    }
    cmds = fml_shell_cmd_count();

    if ((fml_shell_exec("help", out_help, (unsigned)sizeof(out_help)) == BSL_OK)
        && (out_help[0] != '\0')) {
        help_ok = 1;
    }
    if (fml_shell_exec("uptime", out_up, (unsigned)sizeof(out_up)) == BSL_OK
        && (app_shell_dec(out_up, "uptime", &uptime) == 0) && (uptime >= 0)) {
        up_ok = 1;
    }

    app_fmt_puts(&f, "cmd=");
    app_fmt_dec(&f, (int)cmds);
    app_fmt_puts(&f, " help=");
    app_fmt_dec(&f, help_ok);
    app_fmt_puts(&f, " uptime=");
    app_fmt_dec(&f, (uptime < 0) ? 0 : uptime);
    app_fmt_puts(&f, "ms");
    return ((cmds > 0U) && (help_ok != 0) && (up_ok != 0)) ? 1 : 0;
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

    /* 桌面先建好：POST 的 ui 与 menu 两项都要拿它注入点击，应用任务还没起来。
     * 幂等，app_start() 会再调一次。 */
    app_menu_init();

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
    if (board_post_register("menu", app_menu_check) == 0) {
        BSL_LOG_INF("[post] menu register FAIL\r\n");
    }
    if (board_post_register("shell", app_shell_check) == 0) {
        BSL_LOG_INF("[post] shell register FAIL\r\n");
    }

    /* 上电自检：设备初始化之后单独跑一轮，结果只走串口。
     * 与 periph init 分开的原因：init 失败与 selftest 失败在串口上是两行。
     *
     * menu 项要注入点击，点击会进到应用的 event 回调；回调自己的日志会插在
     * `[post] menu` 与它的判定之间。项名与结果已由 bsl 一次性输出，中间夹别人
     * 的行就没法读了 —— 所以 POST 期间关掉应用侧日志，跑完立刻恢复。 */
    app_set_trace(0);
    board_post_run();
    app_set_trace(1);

    /* 样例应用：四键去抖 + 蜂鸣（apl/src/app_sample.c）。
     * 桌面与应用切换：唯一的输入泵任务（apl/src/app.c）。
     * 任务优先级都低于 app_menu，保证触摸轮询不被按键日志拖慢。 */
    app_sample_start();
    app_start();
    /* 命令行任务：POST 之后才建（POST 用的是同一套 exec 纯函数，不需要任务）。
     * 建失败不 panic —— 只是少一个调试入口，不影响应用跑；但必须留一行。 */
    if (fml_shell_start() != 0) {
        BSL_LOG_INF("[shell] task create FAIL\r\n");
    }

    osl_scheduler_start();

    fml_panic(1U);
    for (;;) {
    }
}

