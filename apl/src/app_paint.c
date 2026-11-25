#include "app.h"

#include "board_lcd.h"
#include "bsl_log.h"
#include "fml_font.h"
#include "fml_gui.h"
#include "fml_ui.h"

/*
 * 绘画（P8b）：参考工程 `paint.c` 的界面壳 —— 自由笔迹 + 四种颜色 + 清屏。
 *
 * **画布不是按钮**：fml_ui 的命中只认矩形按钮，而画板要的是"按下点之后连续
 * 的每一个坐标"。所以画布区不登记按钮，DOWN/MOVE/UP 的 id 都是 0，坐标是
 * 屏幕绝对坐标；应用自己判"这个坐标在不在画布内"（见 paint_in_canvas）。
 *
 * **不落盘**：参考工程把画布存到 SD 的 BMP，本工程还没有写文件的 API
 * （`fml_fatfs` 只有只读，见 P5c 的账），硬做一个"保存"按钮只会点下去没反应。
 *
 * 重绘策略：只画笔迹的那一段（`fml_gui_line` 直接落帧缓冲），不整窗重刷 ——
 * 整窗 480×800 在 MCU 屏上是 48 ms，每画一笔刷一次会把手感拖成幻灯片。
 * app_menu 任务每轮末尾的 `board_lcd_present()` 只推脏矩形，正好配套。
 */
#define PAINT_LOG_MOD "app_paint"

#define PAINT_PAL_Y 34U      /* 调色板顶（标题线之下） */
#define PAINT_PAL_H 44U
#define PAINT_PAL_W 64U
#define PAINT_PAL_GAP 8U
#define PAINT_BRUSH 3U       /* 笔宽（像素）：1 像素在 480 宽的屏上手感像"没画" */
#define PAINT_CLR_ID 20U

static fml_ui_win_t s_win;
static int s_built;
static uint16_t s_color = 0x07E0U;   /* 默认绿：深灰底上最清楚 */
static int s_drawing;                /* 1 = 这一笔从画布内按下，正在拖 */
static int32_t s_last_x;
static int32_t s_last_y;
/* 画布矩形（屏幕绝对坐标）。窗口建一次就不再变，因此与 s_win 同生命周期。 */
static uint32_t s_cv_x;
static uint32_t s_cv_y;
static uint32_t s_cv_w;
static uint32_t s_cv_h;

static int paint_in_canvas(int32_t x, int32_t y)
{
    return ((x >= (int32_t)s_cv_x) && (x < (int32_t)(s_cv_x + s_cv_w)) &&
            (y >= (int32_t)s_cv_y) && (y < (int32_t)(s_cv_y + s_cv_h))) ? 1 : 0;
}

/* 一笔上的一个点：以 (x,y) 为心画 PAINT_BRUSH×PAINT_BRUSH 的实心块，
 * 再把上一个点到这一个点连成线 —— 只画点的话，快速拖动会变成断续的珠链。 */
static void paint_dab(int32_t x, int32_t y)
{
    uint32_t half = PAINT_BRUSH / 2U;
    uint32_t px = (uint32_t)((x < (int32_t)half) ? 0 : (x - (int32_t)half));
    uint32_t py = (uint32_t)((y < (int32_t)half) ? 0 : (y - (int32_t)half));

    fml_gui_fill(px, py, PAINT_BRUSH, PAINT_BRUSH, s_color);
}

static void paint_stroke(int32_t x0, int32_t y0, int32_t x1, int32_t y1)
{
    /* 直线两端各补一个方块：`fml_gui_line` 是 1 像素宽，只画线的话笔迹细得
     * 像铅笔划痕，与"手指涂画"的预期不符。 */
    fml_gui_line(x0, y0, x1, y1, s_color);
    paint_dab(x1, y1);
}

static void paint_clear(void)
{
    fml_gui_fill(s_cv_x, s_cv_y, s_cv_w, s_cv_h, 0x0000U);
    fml_gui_frame(s_cv_x - 1U, s_cv_y - 1U, s_cv_w + 2U, s_cv_h + 2U, s_win.line);
}

static void paint_enter(void)
{
    static const char *const label[4] = {"GRN", "RED", "BLU", "WHT"};

    if (s_built == 0) {
        const uint32_t w = board_lcd_width();
        const uint32_t h = board_lcd_height();
        const uint32_t total = (4U * PAINT_PAL_W) + (3U * PAINT_PAL_GAP) + PAINT_PAL_GAP +
                               PAINT_PAL_W;
        const uint32_t x0 = ((w > total) ? ((w - total) / 2U) : 0U);
        unsigned i;

        fml_ui_win_init(&s_win, 0U, 0U, w, h, "PAINT - drag on canvas, BACK to leave");
        for (i = 0U; i < 4U; i++) {
            (void)fml_ui_btn_add(&s_win, (uint16_t)(i + 1U), label[i],
                                 x0 + (i * (PAINT_PAL_W + PAINT_PAL_GAP)),
                                 PAINT_PAL_Y, PAINT_PAL_W, PAINT_PAL_H);
        }
        (void)fml_ui_btn_add(&s_win, PAINT_CLR_ID, "CLR",
                             x0 + (4U * (PAINT_PAL_W + PAINT_PAL_GAP)),
                             PAINT_PAL_Y, PAINT_PAL_W, PAINT_PAL_H);
        (void)fml_ui_btn_add(&s_win, APP_ID_BACK, "BACK", w - 96U,
                             PAINT_PAL_Y, 80U, PAINT_PAL_H);

        s_cv_x = 8U;
        s_cv_y = PAINT_PAL_Y + PAINT_PAL_H + 12U;
        s_cv_w = w - 16U;
        s_cv_h = h - s_cv_y - 8U;
        s_built = 1;
    }
    s_drawing = 0;
    fml_ui_draw(&s_win);
    paint_clear();
}

static void paint_event(const fml_ui_evt_t *ev)
{
    /* 调色板：CLICK 才换色。画布上的 DOWN/MOVE/UP 走下面按坐标判的那条路。 */
    if (ev->type == FML_UI_EVT_CLICK) {
        static const uint16_t color[4] = {0x07E0U, 0xF800U, 0x001FU, 0xFFFFU};

        if ((ev->id >= 1U) && (ev->id <= 4U)) {
            s_color = color[ev->id - 1U];
            BSL_LOG_MOD_INF(PAINT_LOG_MOD, "color ");
            bsl_log_put_dec((int)ev->id);
            BSL_LOG_INF("\r\n");
        } else if (ev->id == PAINT_CLR_ID) {
            paint_clear();
            BSL_LOG_MOD_INF(PAINT_LOG_MOD, "clear\r\n");
        }
        return;
    }

    if ((ev->type != FML_UI_EVT_DOWN) && (ev->type != FML_UI_EVT_MOVE) &&
        (ev->type != FML_UI_EVT_UP)) {
        return;
    }
    /* 从按钮上按下再拖进画布不算画画：那一笔的 id 是按钮 id，不是 0。 */
    if ((ev->type == FML_UI_EVT_DOWN) && (ev->id != 0U)) {
        s_drawing = 0;
        return;
    }
    if ((ev->type == FML_UI_EVT_DOWN) && (paint_in_canvas(ev->x, ev->y) == 0)) {
        s_drawing = 0;
        return;
    }
    if (ev->type == FML_UI_EVT_DOWN) {
        s_drawing = 1;
        s_last_x = ev->x;
        s_last_y = ev->y;
        paint_dab(ev->x, ev->y);
        return;
    }
    if (s_drawing == 0) {
        /* 抬手：只是收尾，不画。MOVE 落在画布外（手指滑出画布）时也不画 ——
         * 滑出去的那一段不能画到边框上。 */
        if (ev->type == FML_UI_EVT_UP) {
            s_drawing = 0;
        }
        return;
    }
    if (ev->type == FML_UI_EVT_UP) {
        if (paint_in_canvas(ev->x, ev->y) != 0) {
            paint_stroke(s_last_x, s_last_y, ev->x, ev->y);
        }
        s_drawing = 0;
        return;
    }
    if (paint_in_canvas(ev->x, ev->y) != 0) {
        paint_stroke(s_last_x, s_last_y, ev->x, ev->y);
        s_last_x = ev->x;
        s_last_y = ev->y;
    }
}

static fml_ui_win_t *paint_win(void)
{
    return &s_win;
}

/* 退出：清画布。回桌面后画布已经不可见，但下一次进来是"新的一张纸" ——
 * 参考工程也是每次进入重新初始化画布。 */
static void paint_leave(void)
{
    s_drawing = 0;
    if (s_built != 0) {
        paint_clear();
    }
}

void app_paint_canvas(int *x, int *y, int *w, int *h)
{
    if (x != NULL) {
        *x = (int)s_cv_x;
    }
    if (y != NULL) {
        *y = (int)s_cv_y;
    }
    if (w != NULL) {
        *w = (int)s_cv_w;
    }
    if (h != NULL) {
        *h = (int)s_cv_h;
    }
}

static const app_desc_t s_desc = {
    9U, "PAINT", paint_enter, NULL, paint_win, paint_event, paint_leave
};

const app_desc_t *app_paint_ops(void)
{
    return &s_desc;
}
