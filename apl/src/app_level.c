#include "app.h"

#include "board_lcd.h"
#include "board_mpu9250.h"
#include "bsl_log.h"
#include "fml_font.h"
#include "fml_gui.h"
#include "fml_ui.h"

/*
 * 水平仪（P8c）：参考工程 `gradienter.c` 的界面壳。
 *
 * 用加速度的 ax / az 算倾角：静止时重力在 z 轴，板子一侧翘起 θ，
 * ax ≈ g·sinθ、az ≈ g·cosθ，于是 tanθ ≈ ax/az。**没有数学库**（工程不链
 * libm），所以取小角线性近似：θ(0.1°) ≈ ax·573/|az|（573 = 1800/π）。
 * 30° 以内误差约 5%，之后迅速变大 —— 只用于"准星有没有动"，不做测量仪。
 *
 * 显示语言是 ASCII（`bsl_log` 与 `fml_gui_text` 都只认 ASCII 行；屏上要写中文
 * 得走 `fml_gui_text_gbk` 并且源码里放 GBK 转义，为一个"倾角"标签不值得）。
 */
#define LEVEL_LOG_MOD "app_level"
#define LEVEL_PERIOD 4U          /* 与 app_imu 同节奏：25 Hz 刷新 */
#define LEVEL_BOX 220U           /* 准星方框边长 */
#define LEVEL_BUBBLE 26U         /* 气泡方块边长 */
#define LEVEL_GAIN 26            /* 原始 LSB → 像素：±110 LSB 铺满方框内沿 */

static fml_ui_win_t s_win;
static int s_built;
static unsigned s_tick;
static int s_angle_dd;           /* 倾角，0.1° 单位 */
static int s_have;               /* 1 = s_acc 是"算出这个倾角的那一份" */
static int16_t s_acc[3];
static int s_bx;                 /* 气泡中心（屏幕绝对） */
static int s_by;
static uint32_t s_cx;
static uint32_t s_cy;

/* 有符号整数 → 十进制，带一个千分位小点：`dd` 是 0.1° 单位，193 → "19.3"。 */
static void level_put_angle(char *dst, unsigned cap, int dd)
{
    char tmp[8];
    unsigned n = 0U;
    unsigned i = 0U;
    int a = (dd < 0) ? -dd : dd;

    if ((dst == NULL) || (cap < 8U)) {
        return;
    }
    do {
        tmp[n++] = (char)('0' + (a % 10));
        a /= 10;
    } while ((a > 0) && (n < (unsigned)sizeof(tmp)));
    if (dd < 0) {
        dst[i++] = '-';
    }
    if (n <= 1U) {
        dst[i++] = '0';
    }
    dst[i++] = '.';
    dst[i++] = tmp[(n >= 1U) ? (n - 1U) : 0U];
    if (n >= 2U) {
        dst[i++] = tmp[n - 2U];
    } else {
        dst[i++] = '0';
    }
    dst[i] = '\0';
}

static void level_cross(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint16_t color)
{
    fml_gui_frame(x, y, w, h, color);
    fml_gui_line((int32_t)(x + (w / 2U)), (int32_t)y,
                 (int32_t)(x + (w / 2U)), (int32_t)(y + h), color);
    fml_gui_line((int32_t)x, (int32_t)(y + (h / 2U)),
                 (int32_t)(x + w), (int32_t)(y + (h / 2U)), color);
}

static void level_draw(void)
{
    const fml_font_t *font = &fml_font_ascii8x16;
    char buf[10];
    uint32_t half = LEVEL_BUBBLE / 2U;

    fml_ui_draw(&s_win);
    fml_gui_fill(s_cx - (LEVEL_BOX / 2U) - 8U, s_cy - (LEVEL_BOX / 2U) - 8U,
                 LEVEL_BOX + 16U, LEVEL_BOX + 16U, s_win.bg);
    level_cross(s_cx - (LEVEL_BOX / 2U), s_cy - (LEVEL_BOX / 2U), LEVEL_BOX,
                LEVEL_BOX, s_win.line);
    /* 目标圈：中心 ±8 像素的方框，气泡落进去就算"平了"。 */
    level_cross(s_cx - 8U, s_cy - 8U, 16U, 16U, 0x07E0U);
    fml_gui_fill((uint32_t)((s_bx < (int)half) ? 0 : (s_bx - (int)half)),
                 (uint32_t)((s_by < (int)half) ? 0 : (s_by - (int)half)),
                 LEVEL_BUBBLE, LEVEL_BUBBLE, 0xFFE0U);
    level_put_angle(buf, (unsigned)sizeof(buf), s_angle_dd);
    fml_gui_text(s_win.x + 24U, s_win.y + (LEVEL_BOX / 2U) + 60U, "TILT ", font,
                 s_win.fg, s_win.bg);
    fml_gui_text(s_win.x + 24U + (5U * font->width), s_win.y + (LEVEL_BOX / 2U) + 60U,
                 buf, font, 0x07FFU, s_win.bg);
}

/* 读一次 IMU，把倾角与气泡位置算出来。 */
static void level_update(void)
{
    int16_t acc[3];
    int az;
    int32_t off_x;
    int32_t off_y;
    int32_t limit = (int32_t)((LEVEL_BOX / 2U) - (LEVEL_BUBBLE / 2U));

    if (board_mpu9250_ready() == 0) {
        s_angle_dd = 0;
        s_have = 0;
        return;
    }
    if (board_mpu9250_read(acc, NULL) != BSL_OK) {
        return;
    }
    s_acc[0] = acc[0];
    s_acc[1] = acc[1];
    s_acc[2] = acc[2];
    s_have = 1;
    az = (int)acc[2];
    if (az < 0) {
        az = -az;
    }
    if (az < 64) {
        az = 64;   /* 侧立时 az→0：不设下限会除出天文数字，屏上气泡飞出去 */
    }
    s_angle_dd = ((int)acc[0] * 573) / az;
    if (s_angle_dd > 900) {
        s_angle_dd = 900;
    } else if (s_angle_dd < -900) {
        s_angle_dd = -900;
    }
    off_x = ((int32_t)acc[0] * LEVEL_GAIN) / 1024;
    off_y = ((int32_t)acc[1] * LEVEL_GAIN) / 1024;
    if (off_x > limit) {
        off_x = limit;
    } else if (off_x < -limit) {
        off_x = -limit;
    }
    if (off_y > limit) {
        off_y = limit;
    } else if (off_y < -limit) {
        off_y = -limit;
    }
    s_bx = (int)((int32_t)s_cx + off_x);
    s_by = (int)((int32_t)s_cy + off_y);
}

static void level_enter(void)
{
    if (s_built == 0) {
        const uint32_t w = board_lcd_width();
        const uint32_t h = board_lcd_height();

        fml_ui_win_init(&s_win, 0U, 0U, w, h, "LEVEL - accelerometer bubble");
        (void)fml_ui_btn_add(&s_win, APP_ID_BACK, "BACK", 16U, 34U, 88U, 36U);
        s_cx = w / 2U;
        s_cy = (h / 2U);
        s_built = 1;
    }
    s_tick = 0U;
    level_update();
    level_draw();
}

static void level_tick(void)
{
    s_tick++;
    if ((s_tick % LEVEL_PERIOD) == 0U) {
        level_update();
        level_draw();
    }
}

static fml_ui_win_t *level_win(void)
{
    return &s_win;
}

static void level_leave(void)
{
    s_angle_dd = 0;
    s_have = 0;
}

int app_level_angle_dd(void)
{
    return s_angle_dd;
}

int app_level_acc(int16_t acc[3])
{
    unsigned i;

    if (s_have == 0) {
        return 0;
    }
    for (i = 0U; i < 3U; i++) {
        if (acc != NULL) {
            acc[i] = s_acc[i];
        }
    }
    return 1;
}

static const app_desc_t s_desc = {
    11U, "LEVEL", level_enter, level_tick, level_win, NULL, level_leave
};

const app_desc_t *app_level_ops(void)
{
    return &s_desc;
}
