#include "app.h"

#include "board_lcd.h"
#include "bsl_log.h"
#include "fml_font.h"
#include "fml_gui.h"
#include "fml_ui.h"

/*
 * 计算器（P5b）：4×4 键 + BACK，整数四则，链式即时运算。
 *
 * 为什么不做表达式解析：这一批的验收是"屏上能按出数字"，解析器（括号、优先级、
 * 错误恢复）是另一个量级的工作，而它跟"窗口/控件/事件能不能带动一个真应用"
 * 没有关系。先做链式：按运算符时把前一段结算掉，按 = 出结果 —— 与板上电话机
 * 那种计算器的行为一致，用户按出来的就是预期的数。
 *
 * 键位（id 从左到右、从上到下 1..16，BACK = APP_ID_BACK）：
 *     7   8   9   /
 *     4   5   6   *
 *     1   2   3   -
 *     0   C   =   +
 * POST 不认这些数值：它按**标签**找 id（"1" 的 id 是多少由这张表说了算），
 * 所以以后改布局不必同步改判据。
 *
 * 没有小数点：整数四则够这一批用，加了小数就要带定点格式化与除零/精度处理。
 * 'C' 放在 '.' 的位置 —— 没有清空键的计算器按错一次就只能退出去重进。
 */
#define CALC_LOG_MOD "app_calc"
#define CALC_WIN_W 424U
#define CALC_WIN_H 400U
#define CALC_KEY_W 92U
#define CALC_KEY_H 56U
#define CALC_KEY_GAP 8U
#define CALC_KEY_X0 16U
#define CALC_KEY_Y0 96U
#define CALC_KEY_ROWS 4U
#define CALC_KEY_COLS 4U
#define CALC_KEY_COUNT (CALC_KEY_ROWS * CALC_KEY_COLS)
#define CALC_DISP_X 16U
#define CALC_DISP_Y 44U
#define CALC_DISP_W (CALC_WIN_W - 32U)
#define CALC_DISP_H 36U
#define CALC_TEXT_MAX 12U
#define CALC_VAL_MAX 9999999L

static fml_ui_win_t s_win;
static int s_built;

static int32_t s_acc;      /* 结算到当前的累计值 */
static int32_t s_val;      /* 正在输入的这一个数 */
static char s_op;          /* 待结算的运算符，0 = 没有 */
static int s_entering;     /* 1 = 手指正在输入 s_val */
static int s_err;          /* 除零等错误：屏上打 Err，任意数字键/清空键可退出 */
static char s_text[CALC_TEXT_MAX];

/* 标签即键义：单字符，标签表同时用来登记与解析，不存在"标签改了、逻辑没改"的错位。 */
static const char *const s_key_label[CALC_KEY_COUNT] = {
    "7", "8", "9", "/",
    "4", "5", "6", "*",
    "1", "2", "3", "-",
    "0", "C", "=", "+"
};

/* 整数转字符串：不引 printf（浮点格式化会拖进几十 KB，这个工程已经在 52%）。
 * 只处理 [-9999999, 9999999]，更长的输入在数字键里就被挡住了。 */
static void calc_itoa(int32_t value, char *dst, unsigned cap)
{
    char tmp[10];
    unsigned n = 0U;
    unsigned i = 0U;
    unsigned v = (unsigned)((value < 0) ? -value : value);

    if ((dst == NULL) || (cap == 0U)) {
        return;
    }
    do {
        tmp[n++] = (char)('0' + (int)(v % 10U));
        v /= 10U;
    } while ((v > 0U) && (n < (unsigned)sizeof(tmp)));
    if ((value < 0) && ((i + 1U) < cap)) {
        dst[i++] = '-';
    }
    while ((n > 0U) && ((i + 1U) < cap)) {
        dst[i++] = tmp[--n];
    }
    dst[i] = '\0';
}

static void calc_refresh_text(void)
{
    const fml_font_t *font = &fml_font_ascii8x16;

    if (s_err != 0) {
        s_text[0] = 'E';
        s_text[1] = 'r';
        s_text[2] = 'r';
        s_text[3] = '\0';
    } else {
        calc_itoa((s_entering != 0) ? s_val : s_acc, s_text, CALC_TEXT_MAX);
    }
    fml_gui_fill(s_win.x + CALC_DISP_X, s_win.y + CALC_DISP_Y, CALC_DISP_W,
                 CALC_DISP_H, 0x0000U);
    fml_gui_frame(s_win.x + CALC_DISP_X, s_win.y + CALC_DISP_Y, CALC_DISP_W,
                  CALC_DISP_H, s_win.line);
    /* 右对齐：数字从右边增长，与真计算器一致。 */
    fml_gui_text(s_win.x + CALC_DISP_X + CALC_DISP_W - 8U -
                     fml_gui_text_width(s_text, font),
                 s_win.y + CALC_DISP_Y + ((CALC_DISP_H - font->height) / 2U),
                 s_text, font, s_win.fg, 0x0000U);
}

static int32_t calc_apply(int32_t a, char op, int32_t b)
{
    switch (op) {
    case '+':
        return a + b;
    case '-':
        return a - b;
    case '*':
        return a * b;
    case '/':
        if (b == 0) {
            s_err = 1;
            return 0;
        }
        return a / b;
    default:
        return b;
    }
}

static void calc_digit(int32_t d)
{
    s_err = 0;
    if (s_entering == 0) {
        s_val = d;
        s_entering = 1;
        return;
    }
    /* 上限挡住溢出：32 位加法本身不检查，越界后屏上会显示一个看着像真的负数。 */
    if ((s_val > (CALC_VAL_MAX / 10L)) || (s_val < (-CALC_VAL_MAX / 10L))) {
        return;
    }
    s_val = (s_val * 10L) + d;
}

static void calc_operator(char op)
{
    s_err = 0;
    if (s_op != 0) {
        if (s_entering != 0) {
            s_acc = calc_apply(s_acc, s_op, s_val);
        }
    } else {
        s_acc = s_val;
    }
    s_op = op;
    s_entering = 0;
    s_val = 0;
}

static void calc_equals(void)
{
    if (s_err != 0) {
        return;
    }
    if (s_op != 0) {
        s_acc = calc_apply(s_acc, s_op, s_val);
        s_op = 0;
    } else {
        s_acc = s_val;
    }
    s_val = s_acc;
    s_entering = 0;
}

static void calc_clear(void)
{
    s_acc = 0;
    s_val = 0;
    s_op = 0;
    s_entering = 0;
    s_err = 0;
}

const char *app_calc_display(void)
{
    return s_text;
}

static void calc_enter(void)
{
    if (s_built == 0) {
        const uint32_t wx = (board_lcd_width() - CALC_WIN_W) / 2U;
        const uint32_t wy = (board_lcd_height() - CALC_WIN_H) / 2U;
        unsigned i;

        fml_ui_win_init(&s_win, wx, wy, CALC_WIN_W, CALC_WIN_H, "CALC - integer");
        for (i = 0U; i < CALC_KEY_COUNT; i++) {
            const unsigned col = i % CALC_KEY_COLS;
            const unsigned row = i / CALC_KEY_COLS;

            (void)fml_ui_btn_add(&s_win, (uint16_t)(i + 1U), s_key_label[i],
                                 CALC_KEY_X0 + (col * (CALC_KEY_W + CALC_KEY_GAP)),
                                 CALC_KEY_Y0 + (row * (CALC_KEY_H + CALC_KEY_GAP)),
                                 CALC_KEY_W, CALC_KEY_H);
        }
        (void)fml_ui_btn_add(&s_win, APP_ID_BACK, "BACK", CALC_KEY_X0,
                             CALC_WIN_H - 48U, 96U, 40U);
        s_built = 1;
    }
    calc_clear();
    fml_ui_draw(&s_win);
    calc_refresh_text();
}

static void calc_event(const fml_ui_evt_t *ev)
{
    char token;

    if ((ev->type != FML_UI_EVT_CLICK) || (ev->id < 1U) ||
        (ev->id > CALC_KEY_COUNT)) {
        return;
    }
    token = s_key_label[ev->id - 1U][0];
    if ((token >= '0') && (token <= '9')) {
        calc_digit((int32_t)(token - '0'));
    } else if (token == 'C') {
        calc_clear();
    } else if (token == '=') {
        calc_equals();
    } else {
        calc_operator(token);
    }
    calc_refresh_text();
    if (app_trace_on() != 0) {
        BSL_LOG_MOD_INF(CALC_LOG_MOD, "key ");
        bsl_log_puts(s_key_label[ev->id - 1U]);
        BSL_LOG_INF(" disp=");
        bsl_log_puts(s_text);
        BSL_LOG_INF("\r\n");
    }
}

static fml_ui_win_t *calc_win(void)
{
    return &s_win;
}

/* 退出时什么都不做：`calc_enter` 已经清过状态，屏在回桌面时也已被重画。
 * 这里曾经写过"顺手再清一次"，但它会让 POST 的 `calc=` 读数变成 0 ——
 * 判据在 BACK 之前算对了、打印在 BACK 之后拿到的是清空后的值，两处对不上。
 * 没有执行器可交还的应用，leave 就该是 NULL。 */
static const app_desc_t s_desc = {
    4U, "CALC", calc_enter, NULL, calc_win, calc_event, NULL
};

const app_desc_t *app_calc_ops(void)
{
    return &s_desc;
}
