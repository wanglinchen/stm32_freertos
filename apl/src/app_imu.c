#include "app.h"

#include "board_lcd.h"
#include "board_mpu9250.h"
#include "bsl_log.h"
#include "fml_font.h"
#include "fml_gui.h"
#include "fml_ui.h"

/*
 * 陀螺仪（P8c）：参考工程 `gyroscope.c` 的界面壳 —— 六轴原始值实时显示。
 *
 * 只显示**原始 16 位值**，不换算成 mg / dps：换算系数取决于量程（本工程没改
 * `ACCEL_CONFIG/GYRO_CONFIG`，默认 ±2 g / ±250 dps），把系数写死在这里，以后
 * 换一次量程就得改两个文件，而且屏上会静默显示错单位。参考工程也是直接打原始值。
 *
 * 采样节奏：`app_menu` 每轮 10 ms 调一次 tick，而 I2C 读 14 字节约 0.3 ms —
 * 每轮都读不是不能，但屏上数字以 100 Hz 跳变，人眼只会看到乱码。每 4 轮读一次
 * （≈40 ms，25 Hz）刚好"看得出在动、又读得清"。显示同样是 4 轮刷一次：每轮重画
 * 三行字在 MCU 屏上要推几百像素，没必要。
 */
#define IMU_LOG_MOD "app_imu"
#define IMU_WIN_W 440U
#define IMU_WIN_H 300U
#define IMU_PERIOD 4U

static fml_ui_win_t s_win;
static int s_built;
static unsigned s_tick;
static int s_ok;                 /* 最近一次读是否成功 */
static int16_t s_acc[3];
static int16_t s_gyr[3];
static char s_line_a[40];
static char s_line_g[40];

/* 有符号十进制：`app_fmt` 在 main.c 里是 static，应用层不能借；这里只拼三个
 * 短整数，自己写一个 20 行的不如直接内联（-Werror 下也不会漏原型）。 */
static void imu_put_int(char *dst, unsigned cap, int v)
{
    char tmp[6];
    unsigned n = 0U;
    unsigned i = 0U;
    unsigned a;

    if ((dst == NULL) || (cap < 4U)) {
        return;
    }
    if (v < 0) {
        dst[i++] = '-';
        a = (unsigned)(-v);
    } else {
        a = (unsigned)v;
    }
    do {
        tmp[n++] = (char)('0' + (int)(a % 10U));
        a /= 10U;
    } while ((a > 0U) && (n < (unsigned)sizeof(tmp)));
    while ((n > 0U) && ((i + 1U) < cap)) {
        dst[i++] = tmp[--n];
    }
    dst[i] = '\0';
}

/* 三个整数用空格拼成一行："215 174 3797"。字符串缓冲由调用方给，写入时按
 * 剩余容量截断 —— 显示挤掉几个字符比越界写坏别的 static 强。 */
static void imu_join3(char *dst, unsigned cap, const int16_t v[3])
{
    unsigned i;
    unsigned p = 0U;

    if ((dst == NULL) || (cap == 0U)) {
        return;
    }
    dst[0] = '\0';
    for (i = 0U; i < 3U; i++) {
        if ((i > 0U) && (p < (cap - 1U))) {
            dst[p++] = ' ';
            dst[p] = '\0';
        }
        if (p >= (cap - 1U)) {
            break;
        }
        imu_put_int(&dst[p], cap - p, (int)v[i]);
        while ((p < cap) && (dst[p] != '\0')) {
            p++;
        }
    }
}

static void imu_build_lines(void)
{
    imu_join3(s_line_a, (unsigned)sizeof(s_line_a), s_acc);
    imu_join3(s_line_g, (unsigned)sizeof(s_line_g), s_gyr);
}

static void imu_draw(void)
{
    const fml_font_t *font = &fml_font_ascii8x16;
    char who[8];

    fml_ui_draw(&s_win);
    imu_put_int(who, (unsigned)sizeof(who), (int)board_mpu9250_who());
    fml_gui_text(s_win.x + 16U, s_win.y + 40U, "WHO", font, s_win.fg, s_win.bg);
    fml_gui_text(s_win.x + 16U + (4U * font->width), s_win.y + 40U, who, font,
                 0x07FFU, s_win.bg);
    fml_gui_text(s_win.x + 16U, s_win.y + 76U, "ACC", font, s_win.fg, s_win.bg);
    fml_gui_text(s_win.x + 16U + (4U * font->width), s_win.y + 76U, s_line_a, font,
                 0xFFE0U, s_win.bg);
    fml_gui_text(s_win.x + 16U, s_win.y + 112U, "GYR", font, s_win.fg, s_win.bg);
    fml_gui_text(s_win.x + 16U + (4U * font->width), s_win.y + 112U, s_line_g, font,
                 0x07E0U, s_win.bg);
    if (s_ok == 0) {
        fml_gui_text(s_win.x + 16U, s_win.y + 148U, "read failed", font, 0xF800U,
                     s_win.bg);
    }
}

static void imu_enter(void)
{
    if (s_built == 0) {
        const uint32_t wx = (board_lcd_width() - IMU_WIN_W) / 2U;
        const uint32_t wy = (board_lcd_height() - IMU_WIN_H) / 2U;

        fml_ui_win_init(&s_win, wx, wy, IMU_WIN_W, IMU_WIN_H, "IMU - MPU9250 raw");
        (void)fml_ui_btn_add(&s_win, APP_ID_BACK, "BACK", 16U, IMU_WIN_H - 52U, 88U, 36U);
        s_built = 1;
    }
    s_tick = 0U;
    s_ok = 0;
    if (board_mpu9250_read(s_acc, s_gyr) == BSL_OK) {
        /* enter 就置 s_ok：POST 在 enter 之后读 `app_imu_sample()`，而这个应用的
         * tick 只由 app_menu 任务每 10 ms 调一次 —— POST 跑在调度器启动之前，
         * tick 一次都不会执行。不在这里读，判据拿到的永远是"没读到"。 */
        s_ok = 1;
    }
    imu_build_lines();
    imu_draw();
}

static void imu_tick(void)
{
    s_tick++;
    if ((s_tick % IMU_PERIOD) != 0U) {
        return;
    }
    if (board_mpu9250_ready() == 0) {
        return;
    }
    s_ok = (board_mpu9250_read(s_acc, s_gyr) == BSL_OK) ? 1 : 0;
    imu_build_lines();
    imu_draw();
}

static fml_ui_win_t *imu_win(void)
{
    return &s_win;
}

static void imu_leave(void)
{
    /* 退出时清掉读数：POST 在 BACK 之后读 `app_imu_sample()`，若不清就会把
     * "上一次进入时的样本"当成"这次也读到了"。 */
    s_ok = 0;
    s_acc[0] = 0;
    s_acc[1] = 0;
    s_acc[2] = 0;
    s_gyr[0] = 0;
    s_gyr[1] = 0;
    s_gyr[2] = 0;
}

int app_imu_sample(int16_t acc[3], int16_t gyr[3])
{
    unsigned i;

    if (s_ok == 0) {
        return 0;
    }
    for (i = 0U; i < 3U; i++) {
        if (acc != NULL) {
            acc[i] = s_acc[i];
        }
        if (gyr != NULL) {
            gyr[i] = s_gyr[i];
        }
    }
    return 1;
}

static const app_desc_t s_desc = {
    10U, "IMU", imu_enter, imu_tick, imu_win, NULL, imu_leave
};

const app_desc_t *app_imu_ops(void)
{
    return &s_desc;
}
