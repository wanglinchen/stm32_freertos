#include "app.h"

#include <string.h>

#include "board_lcd.h"
#include "bsl_log.h"
#include "fml_fatfs.h"
#include "fml_font.h"
#include "fml_gui.h"
#include "fml_picture.h"
#include "fml_ui.h"

/*
 * 图片查看器（P5c）：把卷里的 BMP 1:1 画到屏幕中央。
 *
 * 找图走"根目录 → 一层子目录"：板上那张 BMP 在中文长名目录里，而**短名是
 * `??????~1` 式占位**（FatFs 给非法字符的占位符），拿短名 open 一定
 * FR_INVALID_NAME —— 所以路径必须用 `de.name`（长名、GBK 字节）拼。
 * 判扩展名却只能用短名：长名是 GBK，`strcmp` 对它不可靠；短名是 ASCII 8.3，
 * 末尾 `.BMP` 三个字母确定。
 *
 * 不缩放、不做图片切换：本批先把"头部解析 + 逐行解码 + 像素进帧缓冲"这条链
 * 验通。卷里只有一张 BMP，切换没有第二个对象可切。
 */
#define PIC_LOG_MOD "app_picviewer"
#define PIC_PATH_MAX 96U
#define PIC_WIN_W 800U
#define PIC_WIN_H 480U
#define PIC_BACK_W 96U
#define PIC_BACK_H 40U

/* 列目录的栈上缓冲。根目录实测只有 2 项，给 4；子目录实测 16 项，给 16。
 * 两者不同时存活（根那份用完就出了作用域），所以峰值 ≈ 16 × 52 = 832 B。 */
#define PIC_ROOT_MAX 4U
#define PIC_SUB_MAX 16U

static fml_ui_win_t s_win;
static int s_built;
static char s_path[PIC_PATH_MAX];
static int s_found;
static int s_err;
static int32_t s_w;
static int32_t s_h;
static uint16_t s_bpp;
static uint32_t s_ink;

static int pic_ext_bmp(const char *sname)
{
    unsigned n = 0U;

    while ((sname[n] != '\0') && (n < FML_FATFS_SNAME_LEN)) {
        n++;
    }
    if (n < 4U) {
        return 0;
    }
    /* 短名可能是 `??????~1.BMP` 也可能是 `A.BMP`；只认最后四个字符。 */
    return ((sname[n - 4U] == '.') &&
            ((sname[n - 3U] == 'B') || (sname[n - 3U] == 'b')) &&
            ((sname[n - 2U] == 'M') || (sname[n - 2U] == 'm')) &&
            ((sname[n - 1U] == 'P') || (sname[n - 1U] == 'p'))) ? 1 : 0;
}

/* 拼 "$dir/$name"；dir 已以 '/' 结尾时不重复加。返回 0 = 成功。 */
static int pic_join(char *dst, unsigned cap, const char *dir, const char *name)
{
    unsigned n = 0U;
    unsigned i;

    for (i = 0U; (dir[i] != '\0') && ((n + 1U) < cap); i++) {
        dst[n++] = dir[i];
    }
    if ((n > 0U) && (dst[n - 1U] != '/')) {
        if ((n + 1U) >= cap) {
            return 1;
        }
        dst[n++] = '/';
    }
    for (i = 0U; (name[i] != '\0') && ((n + 1U) < cap); i++) {
        dst[n++] = name[i];
    }
    dst[n] = '\0';
    return (name[i] != '\0') ? 1 : 0;
}

static int pic_find_in(const char *dir, char *out, unsigned cap)
{
    /* 目录项数组做成模块级 static，不放栈：16 × sizeof(fml_fatfs_dirent_t)=52 ≈ 832 B，
     * 而 app_menu 的任务栈是按"只有 UI 绘制"给的（默认 256 words = 1 KB）。
     * 进 PIC 的那一刻，栈上已经压着 dispatch 与 enter 两层，再叠 832 B 就顶到
     * 边界了 —— 栈溢出的表现是"换个应用进就莫名 panic"，比多占 832 B bss 难受得多。
     * 这个函数只有 app_menu 一个任务调用（对照表 §3 的单输入泵模型）。 */
    static fml_fatfs_dirent_t s_de[PIC_SUB_MAX];
    unsigned n = 0U;
    unsigned i;

    if (fml_fatfs_list_dir(dir, s_de, PIC_SUB_MAX, &n) != BSL_OK) {
        return 0;
    }
    for (i = 0U; (i < PIC_SUB_MAX) && (i < n); i++) {
        if ((s_de[i].is_dir != 0U) || (pic_ext_bmp(s_de[i].sname) == 0)) {
            continue;
        }
        if (pic_join(out, cap, dir, s_de[i].name) == 0) {
            return 1;
        }
    }
    return 0;
}

static int pic_find(char *out, unsigned cap)
{
    fml_fatfs_dirent_t root[PIC_ROOT_MAX];
    unsigned n = 0U;
    unsigned i;

    if (fml_fatfs_list_dir("/", root, PIC_ROOT_MAX, &n) != BSL_OK) {
        return 0;
    }
    for (i = 0U; (i < PIC_ROOT_MAX) && (i < n); i++) {
        if ((root[i].is_dir == 0U) && (pic_ext_bmp(root[i].sname) != 0)) {
            if (pic_join(out, cap, "/", root[i].name) == 0) {
                return 1;
            }
        }
    }
    for (i = 0U; (i < PIC_ROOT_MAX) && (i < n); i++) {
        char sub[FML_FATFS_LNAME_LEN + 2U];

        if (root[i].is_dir == 0U) {
            continue;
        }
        if (pic_join(sub, (unsigned)sizeof(sub), "/", root[i].name) != 0) {
            continue;
        }
        if (pic_find_in(sub, out, cap) != 0) {
            return 1;
        }
    }
    return 0;
}

static void pic_draw_view(void)
{
    fml_ui_draw(&s_win);
    if (s_found == 0) {
        const fml_font_t *font = &fml_font_ascii8x16;

        fml_gui_text(s_win.x + 24U, s_win.y + 120U,
                     (s_err != 0) ? "no BMP found (see [pic] err)" : "no BMP found",
                     font, 0xF800U, s_win.bg);
        return;
    }
    /* 1:1 居中：x=(800-320)/2=240、y=(480-240)/2=120（尺寸取自实际图片，
     * 不是写死的 320/240）。 */
    (void)fml_picture_draw(s_path,
                           (int32_t)((board_lcd_width() - (uint32_t)s_w) / 2U),
                           (int32_t)((board_lcd_height() - (uint32_t)s_h) / 2U),
                           &s_ink);
}

static void pic_enter(void)
{
    fml_picture_info_t info;

    if (s_built == 0) {
        fml_ui_win_init(&s_win, 0U, 0U, PIC_WIN_W, PIC_WIN_H,
                        "PIC - BMP 1:1 view, BACK to leave");
        (void)fml_ui_btn_add(&s_win, APP_ID_BACK, "BACK", 16U, 16U, PIC_BACK_W,
                             PIC_BACK_H);
        s_built = 1;
    }

    s_ink = 0U;
    s_found = 0;
    s_err = 0;
    if (s_path[0] == '\0') {
        s_found = pic_find(s_path, PIC_PATH_MAX);
    } else {
        s_found = 1;
    }
    if (s_found != 0) {
        if (fml_picture_info(s_path, &info) == BSL_OK) {
            s_w = info.w;
            s_h = info.h;
            s_bpp = info.bpp;
        } else {
            s_found = 0;
            s_err = 1;
        }
    }
    pic_draw_view();
    if (app_trace_on() != 0) {
        BSL_LOG_MOD_INF(PIC_LOG_MOD, "open ");
        bsl_log_puts((s_path[0] != '\0') ? s_path : "-");
        BSL_LOG_INF(" ");
        bsl_log_put_dec((int)s_w);
        BSL_LOG_INF("x");
        bsl_log_put_dec((int)s_h);
        BSL_LOG_INF(" bpp=");
        bsl_log_put_dec((int)s_bpp);
        BSL_LOG_INF(" ink=");
        bsl_log_put_dec((int)s_ink);
        BSL_LOG_INF("\r\n");
    }
}

static void pic_event(const fml_ui_evt_t *ev)
{
    /* 本批只有一张图、没有可切的目标；CLICK 落在图上不做事。
     * 仍保留这个钩子：`app_desc_t` 的 event 为空时进应用后所有点击都被
     * 记成"没人要"，而提示音/状态行还是会响 —— 空实现比不实现更好查。 */
    (void)ev;
}

static fml_ui_win_t *pic_win(void)
{
    return &s_win;
}

static const app_desc_t s_ops = {
    .id = 6U,
    .name = "PIC",
    .enter = pic_enter,
    .tick = NULL,
    .win = pic_win,
    .event = pic_event,
    .leave = NULL,   /* 不占执行器：图片是画上去的，回桌面时桌面自己重绘 */
};

const app_desc_t *app_picviewer_ops(void)
{
    return &s_ops;
}

int app_picviewer_stat(int32_t *w, int32_t *h, uint16_t *bpp, uint32_t *ink)
{
    if (s_found == 0) {
        return 0;
    }
    if (w != NULL) {
        *w = s_w;
    }
    if (h != NULL) {
        *h = s_h;
    }
    if (bpp != NULL) {
        *bpp = s_bpp;
    }
    if (ink != NULL) {
        *ink = s_ink;
    }
    return 1;
}

const char *app_picviewer_path(void)
{
    return s_path;
}
