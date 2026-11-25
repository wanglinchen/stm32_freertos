#include "app.h"

#include "board_lcd.h"
#include "bsl_log.h"
#include "fml_font.h"
#include "fml_gui.h"
#include "fml_qr.h"
#include "fml_ui.h"

/*
 * 二维码（P6e-2）：参考工程 `qrplay.c` 的界面壳 —— 取一段字符串、编码、把
 * 点阵画到屏上。
 *
 * 参考工程用 T9 九宫格输入任意字符串（它自己那套 memo 控件）。本工程没有
 * 通用文本输入控件（记事本那套九宫格是拼音候选，只出汉字、不出网址），
 * 所以这里用**三条预设短串**轮换：一条数字、一条网址、一条纯大写字母 ——
 * 正好把算法的三条编码路径（数字 / 8bit / 字母）都走到。POST 另可注入任意
 * 短串（`app_qr_show`），判据就是两条不同内容编出来的黑模块数必须不同。
 *
 * 编码参数与参考工程一致：纠错等级 L、掩码 0、版本随内容自动选（fml_qr 内部
 * 固定，见 fml_qr.h）。
 *
 * 画法：白底板 + 黑模块，四周留 4 模块静区（QR 规范要求，参考工程也留）。
 * 模块边长按"底板塞得进窗口"算，一个模块一个 `fml_gui_fill` —— 版本 3
 * （29×29）在 480 宽上是 12 像素/模块，整屏一次画完约 29² 次矩形填充。
 *
 * 重绘：只在进入、换串、编码失败时整块重画；不随 tick 刷新（没有动画）。
 */
#define QR_LOG_MOD "app_qr"

#define QR_BAR_Y 34U
#define QR_BAR_H 44U
#define QR_BTN_W 64U
#define QR_BTN_GAP 8U
#define QR_TEXT_Y (QR_BAR_Y + QR_BAR_H + 8U)
#define QR_TEXT_H 22U
#define QR_QUIET 4U              /* 静区宽度（模块数），QR 规范下限 */
#define QR_ID_PREV 21U
#define QR_ID_NEXT 22U
#define QR_ID_ENC 23U

/* 三条预设：长度 10 / 20 / 17，版本分别是 1 / 2 / 2 —— 故意不都一样长，
 * 让"版本随内容变"这件事在屏上和日志里都看得见。 */
static const char *const s_preset[] = {
    "1234567890",
    "https://openedv.com",
    "ALIENTEK-STM32F429"
};
#define QR_PRESET_COUNT (sizeof(s_preset) / sizeof(s_preset[0]))

static fml_ui_win_t s_win;
static int s_built;
static unsigned s_idx;
static const char *s_text = "1234567890";   /* 当前该编的串；初值是第一条预设，
                                              * 保证 qr_enter 第一句画字时它不为 NULL */
static char s_inject[64];        /* POST 注入串的落点：调用方的缓冲可能在栈上 */
static int s_enc_ok;             /* 最近一次编码：1 成功 */
static unsigned s_size;          /* 边长（模块） */
static unsigned s_version;
static unsigned s_dark;
/* 白底板矩形（屏幕绝对坐标）。POST 用它在帧缓冲里数黑模块像素 ——
 * "编码算对了"与"真的画到屏上"是两件事：模块坐标算错、被文字行盖掉、
 * 底板颜色写反，编码结果照样全对。 */
static int s_plate_x;
static int s_plate_y;
static int s_plate_w;
static int s_plate_h;

/* 十进制追加到 info[n..]（n 为当前长度，cap 为缓冲容量），返回新的长度。
 * 不用 sprintf：工程里没有 printf 家族，屏上只需两三个十进制数。 */
static unsigned qr_put_dec(char *buf, unsigned n, unsigned cap, unsigned v)
{
    char tmp[10];
    unsigned k = 0U;

    do {
        tmp[k++] = (char)('0' + (char)(v % 10U));
        v /= 10U;
    } while ((v != 0U) && (k < sizeof(tmp)));

    while ((k > 0U) && (n < (cap - 1U))) {
        buf[n++] = tmp[--k];
    }
    return n;
}

/* 画点阵：白底板 + 黑模块。坐标按窗口原点 0 算（窗口即整屏）。 */
static void qr_paint(void)
{
    const uint32_t w = s_win.w;
    const uint32_t h = s_win.h;
    const uint32_t top = QR_TEXT_Y + QR_TEXT_H + 6U;
    uint32_t avail_w;
    uint32_t avail_h;
    uint32_t mod;
    uint32_t plate;
    uint32_t x0;
    uint32_t y0;
    unsigned mx;
    unsigned my;

    /* 底板区（含静区）先用窗口底色盖掉上一次的点阵。 */
    fml_gui_fill(0U, top, w, h - top, s_win.bg);

    if ((s_enc_ok == 0) || (s_size == 0U)) {
        fml_gui_text(16U, top + 8U, "encode failed (text too long?)",
                     &fml_font_ascii8x16, 0xF800U, s_win.bg);
        return;
    }

    avail_w = (w > 32U) ? (w - 32U) : w;
    avail_h = (h > (top + 16U)) ? (h - top - 16U) : 0U;
    if (avail_h == 0U) {
        return;
    }
    mod = avail_w / (s_size + (2U * QR_QUIET));
    {
        uint32_t mod_h = avail_h / (s_size + (2U * QR_QUIET));

        if (mod_h < mod) {
            mod = mod_h;
        }
    }
    if (mod == 0U) {
        /* 版本太大塞不进屏：屏上给一句可读的失败，不留空白让人猜。 */
        fml_gui_text(16U, top + 8U, "matrix too large for this panel",
                     &fml_font_ascii8x16, 0xF800U, s_win.bg);
        return;
    }

    plate = (s_size + (2U * QR_QUIET)) * mod;
    x0 = (w - plate) / 2U;
    y0 = top + ((avail_h - plate) / 2U);
    fml_gui_fill(x0, y0, plate, plate, 0xFFFFU);
    s_plate_x = (int)x0;
    s_plate_y = (int)y0;
    s_plate_w = (int)plate;
    s_plate_h = (int)plate;

    for (my = 0U; my < s_size; my++) {
        for (mx = 0U; mx < s_size; mx++) {
            if (fml_qr_module(mx, my) != 0) {
                fml_gui_fill(x0 + ((mx + QR_QUIET) * mod),
                             y0 + ((my + QR_QUIET) * mod), mod, mod, 0x0000U);
            }
        }
    }

    /* 版本与边长印在点阵上方：换串时一眼能看出"版本有没有跟着变"。 */
    {
        char info[16];
        unsigned n = 0U;

        info[n++] = 'v';
        n = qr_put_dec(info, n, sizeof(info), s_version);
        if (n < (sizeof(info) - 1U)) {
            info[n++] = ' ';
        }
        n = qr_put_dec(info, n, sizeof(info), s_size);
        info[n] = '\0';
        fml_gui_text(16U, top, info, &fml_font_ascii8x16, s_win.fg, s_win.bg);
    }
}

/* 编码并（已建窗时）重画。失败不静默：返回负值，屏上也有可读提示。 */
int app_qr_show(const char *text)
{
    int rc;

    if (text == NULL) {
        return -1;
    }
    rc = fml_qr_encode(text);
    if (rc != 0) {
        s_enc_ok = 0;
        s_size = 0U;
        s_dark = 0U;
        BSL_LOG_MOD_INF(QR_LOG_MOD, "encode fail rc=");
        bsl_log_put_dec(rc);
        BSL_LOG_INF("\r\n");
        if (s_built != 0) {
            qr_paint();
        }
        return rc;
    }
    s_text = text;
    s_enc_ok = 1;
    s_size = fml_qr_size();
    s_version = fml_qr_version();
    s_dark = fml_qr_dark();
    BSL_LOG_MOD_INF(QR_LOG_MOD, "enc v=");
    bsl_log_put_dec((int)s_version);
    BSL_LOG_INF(" n=");
    bsl_log_put_dec((int)s_size);
    BSL_LOG_INF(" dark=");
    bsl_log_put_dec((int)s_dark);
    BSL_LOG_INF("\r\n");
    if (s_built != 0) {
        qr_paint();
    }
    return 0;
}

static void qr_use_preset(unsigned idx)
{
    s_idx = idx % (unsigned)QR_PRESET_COUNT;
    (void)app_qr_show(s_preset[s_idx]);
}

static void qr_enter(void)
{
    if (s_built == 0) {
        const uint32_t w = board_lcd_width();
        const uint32_t h = board_lcd_height();
        const uint32_t total = (3U * QR_BTN_W) + (3U * QR_BTN_GAP) + 80U;
        const uint32_t x0 = ((w > total) ? ((w - total) / 2U) : 0U);

        fml_ui_win_init(&s_win, 0U, 0U, w, h, "QR - preset text, PREV/NEXT to switch");
        (void)fml_ui_btn_add(&s_win, QR_ID_PREV, "<", x0, QR_BAR_Y, QR_BTN_W, QR_BAR_H);
        (void)fml_ui_btn_add(&s_win, QR_ID_NEXT, ">",
                             x0 + (QR_BTN_W + QR_BTN_GAP), QR_BAR_Y, QR_BTN_W, QR_BAR_H);
        (void)fml_ui_btn_add(&s_win, QR_ID_ENC, "ENC",
                             x0 + (2U * (QR_BTN_W + QR_BTN_GAP)), QR_BAR_Y, QR_BTN_W, QR_BAR_H);
        (void)fml_ui_btn_add(&s_win, APP_ID_BACK, "BACK", w - 96U, QR_BAR_Y, 80U, QR_BAR_H);
        s_built = 1;
    }
    fml_ui_draw(&s_win);
    qr_use_preset(s_idx);

    /* 串本身印在按钮与点阵之间：换串时不用去猜"现在编的是哪条"。 */
    fml_gui_text(16U, QR_TEXT_Y, s_text, &fml_font_ascii8x16, s_win.fg, s_win.bg);
}

static void qr_event(const fml_ui_evt_t *ev)
{
    if (ev->type != FML_UI_EVT_CLICK) {
        return;
    }
    if (ev->id == QR_ID_PREV) {
        qr_use_preset((s_idx == 0U) ? ((unsigned)QR_PRESET_COUNT - 1U) : (s_idx - 1U));
    } else if (ev->id == QR_ID_NEXT) {
        qr_use_preset(s_idx + 1U);
    } else if (ev->id == QR_ID_ENC) {
        (void)app_qr_show(s_text);
    } else {
        return;
    }
    /* 换串后文字行要跟着换：点阵上方只重画了行号信息，文字行在这里补。 */
    fml_gui_fill(0U, QR_TEXT_Y, s_win.w, QR_TEXT_H, s_win.bg);
    fml_gui_text(16U, QR_TEXT_Y, s_text, &fml_font_ascii8x16, s_win.fg, s_win.bg);
}

static fml_ui_win_t *qr_win(void)
{
    return &s_win;
}

/* 退出不清编码结果：POST 常在"进应用 → 取数 → 退"之后还要读这几个数，
 * 清了就变成"取到的永远是 0"。下次进入由 qr_enter 重新编码。 */
static void qr_leave(void)
{
}

unsigned app_qr_version(void)
{
    return s_version;
}

unsigned app_qr_size(void)
{
    return s_size;
}

unsigned app_qr_dark(void)
{
    return s_dark;
}

void app_qr_plate(int *x, int *y, int *w, int *h)
{
    if (x != NULL) {
        *x = s_plate_x;
    }
    if (y != NULL) {
        *y = s_plate_y;
    }
    if (w != NULL) {
        *w = s_plate_w;
    }
    if (h != NULL) {
        *h = s_plate_h;
    }
}

int app_qr_inject(const char *text)
{
    unsigned i;

    if (text == NULL) {
        return -1;
    }
    for (i = 0U; (i < (sizeof(s_inject) - 1U)) && (text[i] != '\0'); i++) {
        s_inject[i] = text[i];
    }
    s_inject[i] = '\0';
    return app_qr_show(s_inject);
}

static const app_desc_t s_desc = {
    12U, "QR", qr_enter, NULL, qr_win, qr_event, qr_leave
};

const app_desc_t *app_qr_ops(void)
{
    return &s_desc;
}
