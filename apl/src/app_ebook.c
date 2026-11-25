#include "app.h"

#include <string.h>

#include "board_lcd.h"
#include "bsl_fb.h"
#include "bsl_log.h"
#include "fml_fatfs.h"
#include "fml_font.h"
#include "fml_gui.h"
#include "fml_ui.h"

/*
 * 电子书（P9e-1）：把卷里的第一个 .TXT 读进 RAM，按「GBK 双字节 16 px / ASCII 8 px」
 * 逐字折行、分页，屏上一页一页翻。
 *
 * 为什么折行在应用里做、不进 fml_gui：`fml_gui_text_gbk` 只负责"把一行串画出去"，
 * 它不知道可用宽度，也没有"这两个字节是一个字"的概念（折在双字节中间会画出半个
 * 字）。折行需要字宽信息，那是本模块与字库的合同，不属于渲染原语。
 *
 * 文本来源两级：
 *   1) 卷里第一个 .TXT（根目录 → 一层子目录；判扩展名只看短名，取路径用长名 ——
 *      与看图器的同一套理由：中文长名目录的短名是 `??????~1` 式占位）；
 *   2) 找不到就用固件内置样本文本（ASCII）。这一级不是"演示壳"：卷是 FATFS 唯一
 *      的卷（NOR），卷里没有 txt 时应用仍要能翻页，POST 才有可判的东西。
 *
 * 只读前 BOOK_TEXT_MAX 字节：2486 B 的那份 txt 整份进得来；更大的文件截断后
 * 仍能翻，`trunc=1` 会打在状态行上，不让"读到一半"看起来像"书就这么短"。
 */
#define BOOK_LOG_MOD "app_ebook"

#define BOOK_TEXT_MAX 8192U
#define BOOK_LINE_MAX 640U
#define BOOK_LINE_H 20U
#define BOOK_PATH_MAX 96U
#define BOOK_LINE_BYTES 64U

#define BOOK_TEXT_X 16U
#define BOOK_TEXT_Y 72U
#define BOOK_MARGIN 16U

#define BOOK_BTN_W 72U
#define BOOK_BTN_H 40U
#define BOOK_ID_PREV 0x01U
#define BOOK_ID_NEXT 0x02U

typedef struct {
    uint16_t off; /* 在 s_text 里的起点 */
    uint8_t len;  /* 字节数（≤ BOOK_LINE_BYTES） */
} book_line_t;

static fml_ui_win_t s_win;
static int s_built;
static int s_drawn_once;

static char s_text[BOOK_TEXT_MAX];
static uint32_t s_len;
static book_line_t s_lines[BOOK_LINE_MAX];
static uint32_t s_lines_n;
static uint32_t s_page_lines;
static uint32_t s_pages;
static uint32_t s_page;
static uint32_t s_chars;
static uint32_t s_avail_w;
static uint32_t s_ink;
static int s_from_file;
static int s_trunc;
static char s_path[BOOK_PATH_MAX];

/* 内置样本：纯 ASCII（本文件是 UTF-8 源码，中文字面量在源码里是 UTF-8 字节，
 * 而字库按 GBK 取码 —— 混进来会画成乱码。中文路径由卷里的 txt 覆盖）。 */
static const char s_sample[] =
    "ALIENTEK APOLLO F429 - EBOOK DEMO\n"
    "\n"
    "This page is drawn by fml_font: ASCII 8x16 and GB2312 16x16.\n"
    "Every line is wrapped here, not by the renderer: a line break\n"
    "inside a double-byte GBK character would draw half a glyph.\n"
    "\n"
    "Pagination\n"
    "----------\n"
    "Lines per page are computed from the visible text area height,\n"
    "so a 480x800 portrait panel and a 800x480 landscape panel both\n"
    "fill their own screen without a hardcoded rows constant.\n"
    "\n"
    "Controls\n"
    "--------\n"
    "PREV / NEXT buttons change one page. BACK returns to the desk.\n"
    "No scrollbar: the status line at the bottom shows page / total.\n"
    "\n"
    "Why a built-in sample at all\n"
    "----------------------------\n"
    "The NOR volume has exactly one txt in it today. If that file is\n"
    "renamed or removed, the reader must still paginate something;\n"
    "otherwise the POST item would turn red for a missing file\n"
    "rather than for a broken reader. The status line\n"
    "prints src=file or src=sample, so the two cases stay apart.\n"
    "\n"
    "Reading path\n"
    "------------\n"
    "fml_fatfs opens one file at a time; we read at most 8 KB, close,\n"
    "then paginate in RAM. No file handle is kept across redraws:\n"
    "a redraw that seeks the volume would stutter on SPI NOR.\n"
    "\n"
    "End of sample\n"
    "-------------\n"
    "Scroll to the last page to see the status line read n/n.\n";

static int book_ext_txt(const char *sname)
{
    unsigned n = 0U;

    while ((sname[n] != '\0') && (n < FML_FATFS_SNAME_LEN)) {
        n++;
    }
    if (n < 4U) {
        return 0;
    }
    return ((sname[n - 4U] == '.') &&
            ((sname[n - 3U] == 'T') || (sname[n - 3U] == 't')) &&
            ((sname[n - 2U] == 'X') || (sname[n - 2U] == 'x')) &&
            ((sname[n - 1U] == 'T') || (sname[n - 1U] == 't'))) ? 1 : 0;
}

static int book_join(char *dst, unsigned cap, const char *dir, const char *name)
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

/* 目录项放模块级 static：app_menu 的任务栈只有 1 KB 级，栈上再放 16 项 × 52 B
 * 就顶边界（看图器同一理由）。本函数只有 app_menu 一个任务调用。 */
static fml_fatfs_dirent_t s_de[16];

static int book_find_in(const char *dir, char *out, unsigned cap)
{
    unsigned n = 0U;
    unsigned i;

    if (fml_fatfs_list_dir(dir, s_de, 16U, &n) != BSL_OK) {
        return 0;
    }
    for (i = 0U; (i < 16U) && (i < n); i++) {
        if ((s_de[i].is_dir != 0U) || (book_ext_txt(s_de[i].sname) == 0)) {
            continue;
        }
        if (book_join(out, cap, dir, s_de[i].name) == 0) {
            return 1;
        }
    }
    return 0;
}

static int book_find(char *out, unsigned cap)
{
    fml_fatfs_dirent_t root[4];
    unsigned n = 0U;
    unsigned i;

    if (fml_fatfs_list_dir("/", root, 4U, &n) != BSL_OK) {
        return 0;
    }
    for (i = 0U; (i < 4U) && (i < n); i++) {
        if ((root[i].is_dir == 0U) && (book_ext_txt(root[i].sname) != 0)) {
            if (book_join(out, cap, "/", root[i].name) == 0) {
                return 1;
            }
        }
    }
    for (i = 0U; (i < 4U) && (i < n); i++) {
        char sub[FML_FATFS_LNAME_LEN + 2U];

        if (root[i].is_dir == 0U) {
            continue;
        }
        if (book_join(sub, (unsigned)sizeof(sub), "/", root[i].name) != 0) {
            continue;
        }
        if (book_find_in(sub, out, cap) != 0) {
            return 1;
        }
    }
    return 0;
}

/* 读卷里的 txt 到 s_text。返回 1 = 真读到（s_trunc 说明是否被 8 KB 截断）。 */
static int book_load_file(void)
{
    uint32_t size;
    uint32_t got = 0U;

    if (book_find(s_path, BOOK_PATH_MAX) == 0) {
        return 0;
    }
    if (fml_fatfs_open(s_path) != BSL_OK) {
        s_path[0] = '\0';
        return 0;
    }
    size = fml_fatfs_size();
    if (fml_fatfs_read(s_text, BOOK_TEXT_MAX, &got) != BSL_OK) {
        (void)fml_fatfs_close();
        s_path[0] = '\0';
        return 0;
    }
    (void)fml_fatfs_close();
    s_len = (got > BOOK_TEXT_MAX) ? BOOK_TEXT_MAX : got;
    s_trunc = (size > s_len) ? 1 : 0;
    return 1;
}

static void book_push_line(uint32_t off, uint32_t end)
{
    uint32_t n = end - off;

    if ((s_lines_n >= BOOK_LINE_MAX) || (n > BOOK_LINE_BYTES)) {
        /* 超长行（整段没有换行的长文本）截到一行容量。 */
        n = BOOK_LINE_BYTES;
    }
    s_lines[s_lines_n].off = (uint16_t)(off & 0xFFFFU);
    s_lines[s_lines_n].len = (uint8_t)n;
    s_lines_n++;
}

static void book_layout(void)
{
    uint32_t i = 0U;
    uint32_t w = 0U;
    uint32_t start = 0U;

    s_lines_n = 0U;
    s_chars = 0U;
    s_page_lines = (board_lcd_height() - BOOK_TEXT_Y - 56U) / BOOK_LINE_H;
    if (s_page_lines == 0U) {
        s_page_lines = 1U;
    }
    if (s_len == 0U) {
        s_pages = 1U;
        s_page = 0U;
        return;
    }
    while ((i < s_len) && (s_lines_n < BOOK_LINE_MAX)) {
        uint8_t c = (uint8_t)s_text[i];
        uint32_t adv = 1U;
        uint32_t cw = 8U;

        if (c == (uint8_t)'\r') {
            i++;
            continue;
        }
        if (c == (uint8_t)'\n') {
            book_push_line(start, i);
            i++;
            start = i;
            w = 0U;
            continue;
        }
        if ((c >= 0x81U) && ((i + 1U) < s_len)) {
            /* GBK 双字节：>=0x81 的字节与后一个字节构成一个字（字库按 GB2312
             * 88 区 × 94 位取码，扩展区不在表里，画出来是空白 —— 不画豆腐块）。 */
            adv = 2U;
            cw = 16U;
        }
        if ((w + cw > s_avail_w) && (i > start)) {
            book_push_line(start, i);
            start = i;
            w = 0U;
            continue; /* 该字留到下一行，不消耗 */
        }
        w += cw;
        i += adv;
        s_chars++;
    }
    if ((start < s_len) && (s_lines_n < BOOK_LINE_MAX)) {
        book_push_line(start, s_len);
    }
    s_pages = (s_lines_n + s_page_lines - 1U) / s_page_lines;
    if (s_pages == 0U) {
        s_pages = 1U;
    }
    if (s_page >= s_pages) {
        s_page = s_pages - 1U;
    }
}

/* 把 value 写成 10 进制（低位在前再倒过来），返回位数。不引 printf。 */
static unsigned book_dec(char *buf, unsigned value, unsigned cap)
{
    unsigned d = 0U;
    unsigned j;

    if (value == 0U) {
        if (cap > 1U) {
            buf[0] = '0';
            buf[1] = '\0';
        }
        return 1U;
    }
    while ((value > 0U) && (d < (cap - 1U))) {
        buf[d++] = (char)('0' + (unsigned)(value % 10U));
        value /= 10U;
    }
    for (j = 0U; j < (d / 2U); j++) {
        char c = buf[j];

        buf[j] = buf[d - 1U - j];
        buf[d - 1U - j] = c;
    }
    buf[d] = '\0';
    return d;
}

static void book_draw_page(void)
{
    uint32_t first = s_page * s_page_lines;
    uint32_t shown = 0U;
    uint32_t i;
    char tmp[BOOK_LINE_BYTES + 1U];
    char num[16];
    uint32_t y_status = board_lcd_height() - 40U;

    fml_ui_draw(&s_win);
    for (i = 0U; (i < s_page_lines) && ((first + i) < s_lines_n); i++) {
        const book_line_t *ln = &s_lines[first + i];
        uint32_t n = ln->len;
        uint32_t k;

        if (n > BOOK_LINE_BYTES) {
            n = BOOK_LINE_BYTES;
        }
        for (k = 0U; k < n; k++) {
            tmp[k] = s_text[(uint32_t)ln->off + k];
        }
        tmp[n] = '\0';
        (void)fml_gui_text_gbk(BOOK_TEXT_X, BOOK_TEXT_Y + (i * BOOK_LINE_H), tmp,
                               0xFFFFU, s_win.bg);
        shown++;
    }
    /* 状态行：src / trunc 在左，`p/t` 在右。全部用 ASCII 字库自己拼。 */
    (void)fml_gui_text(BOOK_TEXT_X, y_status, (s_from_file != 0) ? "src=file" : "src=sample",
                       &fml_font_ascii8x16, 0x7BEFU, s_win.bg);
    (void)fml_gui_text(BOOK_TEXT_X + 152U, y_status, (s_trunc != 0) ? "trunc" : "full",
                       &fml_font_ascii8x16, 0x7BEFU, s_win.bg);
    (void)book_dec(num, s_page + 1U, (unsigned)sizeof(num));
    (void)fml_gui_text(BOOK_TEXT_X + 224U, y_status, num, &fml_font_ascii8x16, 0x07E0U,
                       s_win.bg);
    (void)fml_gui_text(BOOK_TEXT_X + 280U, y_status, "/", &fml_font_ascii8x16, 0x07E0U,
                       s_win.bg);
    (void)book_dec(num, s_pages, (unsigned)sizeof(num));
    (void)fml_gui_text(BOOK_TEXT_X + 296U, y_status, num, &fml_font_ascii8x16, 0x07E0U,
                       s_win.bg);
    /* 帧缓冲里数「非窗口底色」的像素：底色取 s_win.bg，不写死黑 —— 窗口底色是
     * 0x2104，按 0x0000 去数会把每个文字格的 bg 也算成墨，一行空白就能过判。
     * 文字行数与字数都对、但一个像素没落到屏上时（坐标越界、颜色等于底色），
     * 只有这一条能判出来。 */
    s_ink = bsl_fb_count_nonbg(BOOK_TEXT_X, BOOK_TEXT_Y, s_avail_w,
                               shown * BOOK_LINE_H, s_win.bg);
}

static void book_enter(void)
{
    if (s_built == 0) {
        fml_ui_win_init(&s_win, 0U, 0U, board_lcd_width(), board_lcd_height(),
                        "EBOOK - PREV/NEXT to page, BACK to leave");
        (void)fml_ui_btn_add(&s_win, BOOK_ID_PREV, "PREV", BOOK_MARGIN,
                             board_lcd_height() - 56U, BOOK_BTN_W, BOOK_BTN_H);
        (void)fml_ui_btn_add(&s_win, BOOK_ID_NEXT, "NEXT", BOOK_MARGIN + BOOK_BTN_W + 8U,
                             board_lcd_height() - 56U, BOOK_BTN_W, BOOK_BTN_H);
        (void)fml_ui_btn_add(&s_win, APP_ID_BACK, "BACK",
                             board_lcd_width() - BOOK_BTN_W - 16U, 16U, BOOK_BTN_W,
                             BOOK_BTN_H);
        s_built = 1;
    }
    s_avail_w = board_lcd_width() - (2U * BOOK_TEXT_X);
    s_page = 0U;
    s_len = 0U;
    s_trunc = 0;
    s_from_file = book_load_file();
    if (s_from_file == 0) {
        s_len = (uint32_t)sizeof(s_sample) - 1U;
        if (s_len > BOOK_TEXT_MAX) {
            s_len = BOOK_TEXT_MAX;
        }
        memcpy(s_text, s_sample, s_len);
    }
    book_layout();
    s_drawn_once = 1;
    book_draw_page();
    if (app_trace_on() != 0) {
        BSL_LOG_MOD_INF(BOOK_LOG_MOD, "load ");
        bsl_log_puts((s_from_file != 0) ? s_path : "sample");
        BSL_LOG_INF(" bytes=");
        bsl_log_put_dec((int)s_len);
        BSL_LOG_INF(" chars=");
        bsl_log_put_dec((int)s_chars);
        BSL_LOG_INF(" lines=");
        bsl_log_put_dec((int)s_lines_n);
        BSL_LOG_INF(" pages=");
        bsl_log_put_dec((int)s_pages);
        BSL_LOG_INF(" ink=");
        bsl_log_put_dec((int)s_ink);
        BSL_LOG_INF("\r\n");
    }
}

static int book_turn(int dir)
{
    uint32_t before = s_page;

    if (dir < 0) {
        if (s_page > 0U) {
            s_page--;
        }
    } else if ((s_page + 1U) < s_pages) {
        s_page++;
    }
    if (s_page == before) {
        return 1; /* 到头了：页面没动 */
    }
    book_draw_page();
    return 0;
}

static void book_event(const fml_ui_evt_t *ev)
{
    if (ev->type != FML_UI_EVT_CLICK) {
        return;
    }
    if (ev->id == BOOK_ID_PREV) {
        (void)book_turn(-1);
    } else if (ev->id == BOOK_ID_NEXT) {
        (void)book_turn(1);
    }
    /* 其余点击（含落在文字区）不做事；空实现比不实现更好查（同看图器）。 */
}

static fml_ui_win_t *book_win(void)
{
    return &s_win;
}

static const app_desc_t s_ops = {
    .id = 13U,
    .name = "BOOK",
    .enter = book_enter,
    .tick = NULL,
    .win = book_win,
    .event = book_event,
    .leave = NULL, /* 不占执行器：文字画上去就完了，桌面自己重绘 */
};

const app_desc_t *app_ebook_ops(void)
{
    return &s_ops;
}

int app_ebook_turn(int dir)
{
    if (s_drawn_once == 0) {
        return 1;
    }
    return book_turn(dir);
}

int app_ebook_info(app_ebook_info_t *out)
{
    uint32_t i;
    uint32_t b = 0U;

    if ((out == NULL) || (s_drawn_once == 0)) {
        return 0;
    }
    for (i = 0U; i < s_lines_n; i++) {
        if (s_lines[i].len == 0U) {
            b++;
        }
    }
    out->pages = s_pages;
    out->chars = s_chars;
    out->lines = s_lines_n;
    out->blank = b;
    out->page = s_page;
    out->ink = s_ink;
    out->from_file = s_from_file;
    out->trunc = s_trunc;
    return 1;
}
