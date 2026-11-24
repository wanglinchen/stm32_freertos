#include "app.h"

#include <string.h>

#include "bsl_log.h"
#include "fml_font.h"
#include "fml_gui.h"
#include "fml_t9.h"
#include "fml_ui.h"

/*
 * 记事本（P5c）：九宫格拼音输入 + 文本显示。
 *
 * 输入链：数字键 → `fml_t9_match_*`（数字串 → **一组**音节）→ 候选条（每页 5 个
 * 汉字，跨音节连续编号）→ 点候选把 GBK 两字节追加到文本。DEL 先退拼音、拼音空了
 * 再退文本的最后一字。
 *
 * 为什么要"一组"而不是"一个"：同一串数字往往对应多个音节（94664 = xiong 和
 * zhong，x/z 在同一个键上、i/h 在同一个键上）。P5c 第一版取"表里第一个"，
 * 94664 就只出 xiong 的字，"中"永远打不出来 —— 上板日志里 note=2:d0d6（"胸"）
 * 就是它。所以候选条按"所有匹配音节的字顺序摊开"，翻页跨音节连续。
 *
 * **只存 RAM，不写盘**：`disk_write` 的读改写路径至今没实测过（见 fml_fatfs.h），
 * 拿它往板上 25 MB 素材卷里写文件，是拿现成数据做赌注。要持久化先补 P2b 的
 * 写验证，再让记事本 `f_open(FA_WRITE|FA_CREATE_ALWAYS)`。
 *
 * 按钮数 19（BACK 1 + 候选 5 + 翻页 2 + 数字 11），在 `FML_UI_BTN_MAX 20` 之内 ——
 * 再多两个候选就得扩上限，而每扩一次所有窗口都跟着涨 bss（§4 P5b 行的账）。
 */
#define NOTE_LOG_MOD "app_notepad"
#define NOTE_WIN_W 800U
#define NOTE_WIN_H 480U
#define NOTE_TEXT_MAX 64U    /* 字节；一个汉字 2 字节，即最多 32 字 */
#define NOTE_CAND_PAGE 5U

#define NOTE_CAND_W 112U
#define NOTE_CAND_H 44U
#define NOTE_CAND_GAP 8U
#define NOTE_CAND_Y 180U
#define NOTE_CAND_X0 56U

#define NOTE_KEY_W 96U
#define NOTE_KEY_H 48U
#define NOTE_KEY_GAP 8U
#define NOTE_KEY_X0 ((NOTE_WIN_W - (3U * NOTE_KEY_W + 2U * NOTE_KEY_GAP)) / 2U)
#define NOTE_KEY_Y0 244U

static fml_ui_win_t s_win;
static int s_built;
static char s_text[NOTE_TEXT_MAX + 1U];
static unsigned s_len;
static char s_digits[FML_T9_MAX_DIGITS + 1U];
static unsigned s_ndigits;
/* 匹配结果的缓存：s_nmatch 个音节、字总数 s_ntotal。两者只在 s_digits 变了
 * （note_refresh）时才重算 —— 每次画候选都重扫 396 条音节没必要，而候选条
 * 每页要查 5 个字。 */
static unsigned s_nmatch;
static unsigned s_ntotal;
static unsigned s_page;

/* 候选项 id 100..104；翻页 110/111；数字键 1..9 与 10(0) / 11(DEL)。
 * 用 id 而不是下标，是因为 fml_ui 的事件只带 id。这些常量定义在 app.h 里：
 * POST 要按 id 点候选（label 为 NULL，汉字不能用 ASCII 标签），两边共用一份。 */
#define NOTE_ID_CAND0 APP_NOTE_ID_CAND0
#define NOTE_ID_PREV APP_NOTE_ID_PREV
#define NOTE_ID_NEXT APP_NOTE_ID_NEXT
#define NOTE_ID_ZERO APP_NOTE_ID_ZERO
#define NOTE_ID_DEL APP_NOTE_ID_DEL

/* 匹配集的字总数。候选条按"所有匹配音节的字顺序摊开"编号，所以先要知道总数
 * 才能算页数、才能判"这一格有没有字"。 */
static unsigned note_ntotal(void)
{
    unsigned n = 0U;
    unsigned i;

    for (i = 0U; i < s_nmatch; i++) {
        n += fml_t9_hans_total(fml_t9_match_at(s_digits, s_ndigits, i));
    }
    return n;
}

/* 第 flat 个候选字的 GBK 码；越界返回 0。跨音节连续编号：xiong 的 7 个字占
 * flat 0..6，zhong 的 11 个字紧接着从 7 开始 —— POST 就靠这个把"打中"点到。 */
static uint16_t note_cand_at(unsigned flat)
{
    unsigned i;

    for (i = 0U; i < s_nmatch; i++) {
        const char *syl = fml_t9_match_at(s_digits, s_ndigits, i);
        const unsigned c = fml_t9_hans_total(syl);

        if (flat < c) {
            return fml_t9_hans_at(syl, flat);
        }
        flat -= c;
    }
    return 0U;
}

static void note_draw(void)
{
    const fml_font_t *font = &fml_font_ascii8x16;
    unsigned i;

    fml_ui_draw(&s_win);

    /* 文本区：两行，每行最多 48 个汉字（768 px / 16）。超出部分截断显示，
     * 但 s_text 里仍然完整 —— 显示裁剪不影响已经输入的内容。 */
    fml_gui_text(s_win.x + 16U, s_win.y + 48U, "TEXT:", font, s_win.fg, s_win.bg);
    fml_gui_text_gbk(s_win.x + 16U, s_win.y + 66U, s_text, s_win.fg, s_win.bg);

    /* 拼音行：已按的数字 + 首个匹配音节 + 匹配个数。候选条为空时这一行是唯一
     * 的反馈；`x2` 这类后缀说明"这串数字还有别的音节，候选条里都排着"。 */
    fml_gui_text(s_win.x + 16U, s_win.y + 152U, "PINYIN:", font, s_win.fg, s_win.bg);
    fml_gui_text(s_win.x + 96U, s_win.y + 152U, s_digits, font, 0xFFE0U, s_win.bg);
    {
        /* 显示行只用"第一个匹配音节"（`fml_t9_resolve` 就是它的便利包装）；
         * `+` 表示这串数字还匹配别的音节，候选条里那些字排在后面。 */
        const char *syl0 = NULL;
        int hit = 0;

        if (s_ndigits > 0U) {
            hit = fml_t9_resolve(s_digits, s_ndigits, &syl0);
        }
        fml_gui_text(s_win.x + 200U, s_win.y + 152U, (hit != 0) ? syl0 : "-", font,
                     0x07FFU, s_win.bg);
        if (s_nmatch > 1U) {
            fml_gui_text(s_win.x + 200U + (fml_font_ascii8x16.width * 8U), s_win.y + 152U,
                         "+", font, 0x07FFU, s_win.bg);
        }
    }

    /* 候选条：按钮底色已由 fml_ui_draw 画好（label 为 NULL 时只填底色），
     * 这里把 GBK 汉字叠上去。GBK 不能用 fml_ui 的 ASCII 标签画。 */
    for (i = 0U; i < NOTE_CAND_PAGE; i++) {
        const uint16_t code = note_cand_at(s_page * NOTE_CAND_PAGE + i);

        if (code == 0U) {
            break;
        }
        {
            char buf[3];

            buf[0] = (char)(code >> 8);
            buf[1] = (char)(code & 0xFFU);
            buf[2] = '\0';
            fml_gui_text_gbk(s_win.x + NOTE_CAND_X0 + i * (NOTE_CAND_W + NOTE_CAND_GAP),
                             s_win.y + NOTE_CAND_Y + 12U, buf, s_win.btn_fg,
                             s_win.btn_bg);
        }
    }
}

static void note_refresh(void)
{
    s_page = 0U;
    s_nmatch = (s_ndigits > 0U) ? fml_t9_match_count(s_digits, s_ndigits) : 0U;
    s_ntotal = note_ntotal();
    note_draw();
}

static void note_enter(void)
{
    if (s_built == 0) {
        static const char *const key_label[11] = {
            "1", "2", "3", "4", "5", "6", "7", "8", "9", "0", "DEL"
        };
        unsigned i;

        fml_ui_win_init(&s_win, 0U, 0U, NOTE_WIN_W, NOTE_WIN_H,
                        "NOTEPAD - 9-key pinyin, BACK to leave");
        (void)fml_ui_btn_add(&s_win, APP_ID_BACK, "BACK", NOTE_WIN_W - 96U, 8U, 80U, 32U);

        for (i = 0U; i < NOTE_CAND_PAGE; i++) {
            (void)fml_ui_btn_add(&s_win, (uint16_t)(NOTE_ID_CAND0 + i), NULL,
                                 NOTE_CAND_X0 + i * (NOTE_CAND_W + NOTE_CAND_GAP),
                                 NOTE_CAND_Y, NOTE_CAND_W, NOTE_CAND_H);
        }
        (void)fml_ui_btn_add(&s_win, NOTE_ID_PREV, "<",
                             NOTE_CAND_X0 + NOTE_CAND_PAGE * (NOTE_CAND_W + NOTE_CAND_GAP),
                             NOTE_CAND_Y, 40U, NOTE_CAND_H);
        (void)fml_ui_btn_add(&s_win, NOTE_ID_NEXT, ">",
                             NOTE_CAND_X0 + NOTE_CAND_PAGE * (NOTE_CAND_W + NOTE_CAND_GAP),
                             NOTE_CAND_Y + NOTE_CAND_H + NOTE_CAND_GAP, 40U, NOTE_CAND_H);

        /* 11 个数字键：前三行 1..9，第四行 DEL / 0 / 空。 */
        for (i = 0U; i < 11U; i++) {
            unsigned col = i % 3U;
            unsigned row = i / 3U;
            uint16_t id = (i < 9U) ? (uint16_t)(i + 1U)
                                   : ((i == 9U) ? NOTE_ID_ZERO : NOTE_ID_DEL);

            if (i == 10U) {
                continue;   /* 第三格留给 spacer，不建按钮 */
            }
            (void)fml_ui_btn_add(&s_win, id, key_label[i],
                                 NOTE_KEY_X0 + col * (NOTE_KEY_W + NOTE_KEY_GAP),
                                 NOTE_KEY_Y0 + row * (NOTE_KEY_H + NOTE_KEY_GAP),
                                 NOTE_KEY_W, NOTE_KEY_H);
        }
        s_built = 1;
    }
    note_refresh();
}

static int note_insert_han(uint16_t code)
{
    if ((code == 0U) || ((s_len + 2U) > NOTE_TEXT_MAX)) {
        return 0;
    }
    s_text[s_len++] = (char)(code >> 8);
    s_text[s_len++] = (char)(code & 0xFFU);
    s_text[s_len] = '\0';
    return 1;
}

static void note_del(void)
{
    if (s_ndigits > 0U) {
        s_ndigits--;
        s_digits[s_ndigits] = '\0';
        return;
    }
    if (s_len == 0U) {
        return;
    }
    /* 汉字两字节、ASCII 一字节。看最后一个字节的哨兵位决定退几个字节：
     * 汉字尾字节是 0x40..0xFE，ASCII 是 0x20..0x7E，两者在 0x80 以上才算汉字。 */
    s_len--;
    if ((s_len > 0U) && ((uint8_t)(s_text[s_len - 1U] & 0xFFU) >= 0x80U)) {
        s_len--;
    }
    s_text[s_len] = '\0';
}

static void note_event(const fml_ui_evt_t *ev)
{
    if (ev->type != FML_UI_EVT_CLICK) {
        return;
    }
    if ((ev->id >= 1U) && (ev->id <= 9U)) {
        if (s_ndigits < FML_T9_MAX_DIGITS) {
            s_digits[s_ndigits++] = (char)('0' + ev->id);
            s_digits[s_ndigits] = '\0';
        }
        note_refresh();
        return;
    }
    if (ev->id == NOTE_ID_ZERO) {
        /* 0：拼音状态下当分隔/无效键，空拼音时插入一个空格。 */
        if ((s_ndigits == 0U) && ((s_len + 1U) <= NOTE_TEXT_MAX)) {
            s_text[s_len++] = ' ';
            s_text[s_len] = '\0';
        }
        note_refresh();
        return;
    }
    if (ev->id == NOTE_ID_DEL) {
        note_del();
        note_refresh();
        return;
    }
    if ((ev->id >= NOTE_ID_CAND0) && (ev->id < (NOTE_ID_CAND0 + NOTE_CAND_PAGE))) {
        const unsigned idx = s_page * NOTE_CAND_PAGE + (unsigned)(ev->id - NOTE_ID_CAND0);

        if (note_insert_han(note_cand_at(idx)) != 0) {
            s_ndigits = 0U;
            s_digits[0] = '\0';
        }
        note_refresh();
        return;
    }
    if ((ev->id == NOTE_ID_PREV) || (ev->id == NOTE_ID_NEXT)) {
        const unsigned pages = (s_ntotal + NOTE_CAND_PAGE - 1U) / NOTE_CAND_PAGE;

        if (pages == 0U) {
            return;
        }
        if (ev->id == NOTE_ID_NEXT) {
            s_page = (s_page + 1U >= pages) ? 0U : (s_page + 1U);
        } else {
            s_page = (s_page == 0U) ? (pages - 1U) : (s_page - 1U);
        }
        note_draw();
        return;
    }
}

static fml_ui_win_t *note_win(void)
{
    return &s_win;
}

static const app_desc_t s_ops = {
    .id = 7U,
    .name = "NOTE",
    .enter = note_enter,
    .tick = NULL,
    .win = note_win,
    .event = note_event,
    .leave = NULL,
};

const app_desc_t *app_notepad_ops(void)
{
    return &s_ops;
}

unsigned app_notepad_len(void)
{
    return s_len;
}

const char *app_notepad_text(void)
{
    return s_text;
}

unsigned app_notepad_syl_count(void)
{
    return s_nmatch;
}

unsigned app_notepad_cand_total(void)
{
    return s_ntotal;
}
