#include "fml_ui.h"

#include "fml_font.h"
#include "fml_gui.h"

/*
 * 事件队列与按压状态（单生产 / 单消费，无锁 —— 前提写在 fml_ui.h 末尾）。
 *
 * 静态初始化直接给成"未按下、没有按钮"：调用方即使忘了先 fml_ui_reset，
 * s_press_btn 也不会被当成按钮 0（否则抬起时会凭空产出一个 id=1 的 CLICK）。
 */
static fml_ui_evt_t s_q[FML_UI_EVT_MAX];
static unsigned s_head;
static unsigned s_tail;
static unsigned s_ovf;

static int s_pressed;
static int s_press_btn = -1;
static uint16_t s_press_id;
static int s_hit_id = -1;

static void ui_push(fml_ui_evt_type_t type, uint16_t id, int32_t x, int32_t y)
{
    unsigned next = (s_head + 1U) % FML_UI_EVT_MAX;

    /* 队满即丢：保留"溢出计数"而不是覆盖最旧的，是因为 POST 要判它必须为 0；
     * 覆盖式环形队列会让溢出变成静默的事件顺序错乱。 */
    if (next == s_tail) {
        s_ovf++;
        return;
    }
    s_q[s_head].type = type;
    s_q[s_head].id = id;
    s_q[s_head].x = x;
    s_q[s_head].y = y;
    s_head = next;
}

/* 小整数转十进制串：只用于状态行的 `hit=<id>`，不引 printf（浮点格式化会把
 * 几 KB 的 _printf_float 拖进 flash，这个工程已经在 53%）。 */
static void ui_put_int(char *dst, unsigned cap, int value)
{
    char tmp[8];
    unsigned n = 0U;
    unsigned i = 0U;
    unsigned v;

    if ((dst == NULL) || (cap == 0U)) {
        return;
    }
    v = (unsigned)((value < 0) ? -value : value);
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

void fml_ui_win_init(fml_ui_win_t *win, uint32_t x, uint32_t y, uint32_t w,
                     uint32_t h, const char *title)
{
    if (win == NULL) {
        return;
    }
    win->x = x;
    win->y = y;
    win->w = w;
    win->h = h;
    win->title = title;
    /* 深灰底：与红/绿/蓝色块和黑条上的白字都能分开，又不抢眼。 */
    win->bg = 0x2104U;
    win->fg = 0xFFFFU;
    win->line = 0x8410U;
    win->btn_bg = 0x4208U;
    win->btn_fg = 0xFFFFU;
    win->hot_bg = 0xFFE0U;
    win->n_btn = 0U;
    win->hot = -1;
}

int fml_ui_btn_add(fml_ui_win_t *win, uint16_t id, const char *label, uint32_t x,
                   uint32_t y, uint32_t w, uint32_t h)
{
    fml_ui_btn_t *b;

    /* id 0 保留给"未命中"：允许登记一个 id=0 的按钮，会让 CLICK 与"点了空白"
     * 在串口上长得一样。 */
    if ((win == NULL) || (id == 0U) || (w == 0U) || (h == 0U)) {
        return 0;
    }
    if (win->n_btn >= FML_UI_BTN_MAX) {
        return 0;
    }
    b = &win->btn[win->n_btn];
    b->id = id;
    b->x = x;
    b->y = y;
    b->w = w;
    b->h = h;
    b->label = label;
    win->n_btn++;
    return 1;
}

int fml_ui_hit(const fml_ui_win_t *win, int32_t x, int32_t y)
{
    unsigned i;

    if (win == NULL) {
        return -1;
    }
    for (i = 0U; i < win->n_btn; i++) {
        const fml_ui_btn_t *b = &win->btn[i];
        int32_t bx = (int32_t)(win->x + b->x);
        int32_t by = (int32_t)(win->y + b->y);

        /* 半开区间：两个相邻按钮共边时不会同时命中。 */
        if ((x >= bx) && (x < (bx + (int32_t)b->w)) &&
            (y >= by) && (y < (by + (int32_t)b->h))) {
            return (int)i;
        }
    }
    return -1;
}

int fml_ui_btn_by_id(const fml_ui_win_t *win, uint16_t id)
{
    unsigned i;

    if ((win == NULL) || (id == 0U)) {
        return -1;
    }
    for (i = 0U; i < win->n_btn; i++) {
        if (win->btn[i].id == id) {
            return (int)i;
        }
    }
    return -1;
}

void fml_ui_draw_btn(const fml_ui_win_t *win, unsigned idx)
{
    const fml_ui_btn_t *b;
    const fml_font_t *font = &fml_font_ascii8x16;
    uint32_t ax;
    uint32_t ay;
    uint32_t lw;
    uint32_t tx;
    uint32_t ty;
    uint16_t bg;
    uint16_t fg;

    if ((win == NULL) || (idx >= win->n_btn)) {
        return;
    }
    b = &win->btn[idx];
    ax = win->x + b->x;
    ay = win->y + b->y;
    bg = ((int)idx == win->hot) ? win->hot_bg : win->btn_bg;
    fg = ((int)idx == win->hot) ? win->fg : win->btn_fg;
    fml_gui_fill(ax, ay, b->w, b->h, bg);
    fml_gui_frame(ax, ay, b->w, b->h, win->line);
    if (b->label != NULL) {
        /* 文字比按钮宽时左对齐而不是溢出：溢出会把字画到相邻按钮上，
         * 屏上看着像两个按钮都坏了。 */
        lw = fml_gui_text_width(b->label, font);
        tx = (lw < b->w) ? (ax + ((b->w - lw) / 2U)) : ax;
        ty = (b->h > font->height) ? (ay + ((b->h - font->height) / 2U)) : ay;
        fml_gui_text(tx, ty, b->label, font, fg, bg);
    }
}

void fml_ui_draw(const fml_ui_win_t *win)
{
    const fml_font_t *font = &fml_font_ascii8x16;
    char buf[12];
    uint32_t sx;
    uint32_t sy;
    unsigned i;

    if (win == NULL) {
        return;
    }
    fml_gui_fill(win->x, win->y, win->w, win->h, win->bg);
    fml_gui_frame(win->x, win->y, win->w, win->h, win->line);
    if (win->title != NULL) {
        fml_gui_text(win->x + 8U, win->y + 6U, win->title, font, win->fg, win->bg);
    }
    /* 标题分隔线：1 像素高，fill 与 hline 是同一件事，不为此加原型。 */
    if (win->h > 30U) {
        fml_gui_fill(win->x + 4U, win->y + 26U, win->w - 8U, 1U, win->line);
    }
    for (i = 0U; i < win->n_btn; i++) {
        fml_ui_draw_btn(win, i);
    }
    if (win->h > 30U) {
        sx = win->x + 8U;
        sy = win->y + win->h - 22U;
        fml_gui_text(sx, sy, "hit=", font, win->fg, win->bg);
        ui_put_int(buf, (unsigned)sizeof(buf), s_hit_id);
        fml_gui_text(sx + (4U * font->width), sy, buf, font, win->fg, win->bg);
    }
}

void fml_ui_feed(fml_ui_win_t *win, int32_t x, int32_t y, int pressed)
{
    int idx;

    if (win == NULL) {
        return;
    }
    idx = (pressed != 0) ? fml_ui_hit(win, x, y) : -1;

    if (pressed != 0) {
        if (s_pressed == 0) {
            s_pressed = 1;
            s_press_btn = idx;
            s_press_id = (idx >= 0) ? win->btn[idx].id : 0U;
            win->hot = idx;
            if (idx >= 0) {
                fml_ui_draw_btn(win, (unsigned)idx);
            }
            ui_push(FML_UI_EVT_DOWN, s_press_id, x, y);
        } else if (idx != s_press_btn) {
            /* 手指在按钮之间滑动：只换高亮，不产事件。抬起时若已滑出，就不会
             * 出现 CLICK —— 与 PC 上"按下后拖走再松开"的语义一致。 */
            int old = s_press_btn;

            s_press_btn = idx;
            s_press_id = (idx >= 0) ? win->btn[idx].id : 0U;
            win->hot = idx;
            if (old >= 0) {
                fml_ui_draw_btn(win, (unsigned)old);
            }
            if (idx >= 0) {
                fml_ui_draw_btn(win, (unsigned)idx);
            }
        }
        return;
    }

    if (s_pressed == 0) {
        return;
    }
    s_pressed = 0;
    win->hot = -1;
    if (s_press_btn >= 0) {
        fml_ui_draw_btn(win, (unsigned)s_press_btn);
    }
    ui_push(FML_UI_EVT_UP, s_press_id, x, y);
    /* CLICK 只看"抬起时高亮还落在哪个按钮上"，**不重算抬起点的命中**：
     * 抬起帧总线上已经没有触点，x/y 是上一帧残值甚至清零，拿它重算会把正常
     * 点击判成"滑出"。滑动检测已经在按下期间逐帧做过了（s_press_btn 跟着手指走），
     * 这里是那份状态的最后一帧。 */
    if (s_press_btn >= 0) {
        s_hit_id = (int)s_press_id;
        ui_push(FML_UI_EVT_CLICK, s_press_id, x, y);
        /* 整窗重画一次，把状态行的 hit=<id> 刷新成这一次的 id。整窗 340×140
         * 是 SDRAM 上的纯内存写，比管理"哪一块脏了"便宜得多。 */
        fml_ui_draw(win);
    }
    s_press_btn = -1;
    s_press_id = 0U;
}

int fml_ui_next(fml_ui_evt_t *ev)
{
    if ((ev == NULL) || (s_tail == s_head)) {
        return 0;
    }
    *ev = s_q[s_tail];
    s_tail = (s_tail + 1U) % FML_UI_EVT_MAX;
    return 1;
}

unsigned fml_ui_overflow(void)
{
    return s_ovf;
}

void fml_ui_reset(void)
{
    s_head = 0U;
    s_tail = 0U;
    s_ovf = 0U;
    s_pressed = 0;
    s_press_btn = -1;
    s_press_id = 0U;
    s_hit_id = -1;
}
