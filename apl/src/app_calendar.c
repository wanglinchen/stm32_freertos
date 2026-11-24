#include "app.h"

#include <string.h>

#include "bsl_log.h"
#include "fml_font.h"
#include "fml_gui.h"
#include "fml_ui.h"
#include "osl_thread.h"

/*
 * 日历（P5c）：月历网格 + 走时。
 *
 * **没有真实时间源**：板上那颗 RTC（ST 内部 RTC）要到 P6 才建 `rtc` 节点并
 * 配 LSE。这一版把"起点"取成**编译时刻**（`__DATE__` / `__TIME__`），之后按
 * `osl_tick` 累加 —— 所以它显示的是"从这份固件编译出来到现在过了多久"，
 * 不是墙钟。掉电不保持、也不假装保持：界面上把起点写在标题栏，观者一眼能
 * 看出这是哪种时间。
 *
 * 日期换算用 Howard Hinnant 的 civil_from_days（纯整数、无查表、闰年规则
 * 内置），不用自己推"哪年是闰年" —— 那种手写规则在 2100 年那种整百年上会错，
 * 虽然不是本项目会活到的年份，但错法很难被测试发现。
 */
#define CAL_LOG_MOD "app_calendar"
#define CAL_WIN_W 800U
#define CAL_WIN_H 480U
#define CAL_CELL_W 40U
#define CAL_CELL_H 32U
#define CAL_GRID_X 240U
#define CAL_GRID_Y 140U
#define CAL_ID_PREV 1U
#define CAL_ID_NEXT 2U

static fml_ui_win_t s_win;
static int s_built;
static int s_month_shift;   /* 相对"当前月"的偏移：0 = 本月，-1 = 上月 */
static int s_last_sec = -1;

/* ---- 编译时刻 → 起点 ------------------------------------------------- */

static const char s_build_date[] = __DATE__;   /* "Nov 24 2026" */
static const char s_build_time[] = __TIME__;   /* "07:40:12" */

static int cal_month_from_name(const char *s)
{
    static const char *const names[12] = {
        "Jan", "Feb", "Mar", "Apr", "May", "Jun",
        "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
    };
    unsigned i;

    for (i = 0U; i < 12U; i++) {
        const char *p = names[i];
        unsigned k = 0U;

        while ((p[k] != '\0') && (s[k] == p[k])) {
            k++;
        }
        if (p[k] == '\0') {
            return (int)i + 1;
        }
    }
    return 0;
}

static int cal_two_digits(const char *s)
{
    int v = 0;

    if ((s[0] < '0') || (s[0] > '9') || (s[1] < '0') || (s[1] > '9')) {
        return -1;
    }
    v = (s[0] - '0') * 10 + (s[1] - '0');
    return v;
}

/* 四位年份。`__DATE__` 的 %Y 不是零填充的（2026 就是 "2026"），
 * 但固定四位，所以可以直接按四个字符取。 */
static int cal_four_digits(const char *s)
{
    int v = 0;
    unsigned i;

    for (i = 0U; i < 4U; i++) {
        if ((s[i] < '0') || (s[i] > '9')) {
            return 0;
        }
        v = v * 10 + (s[i] - '0');
    }
    return v;
}

/* days_from_civil：1970-01-01 起的天数（向下取整的除法由 + 偏移修正）。 */
static int64_t cal_days_from_civil(int y, int m, int d)
{
    int64_t era;
    int64_t yoe;
    int64_t doy;
    int64_t doe;
    int yy = y - ((m <= 2) ? 1 : 0);

    era = (yy >= 0 ? yy : (yy - 399)) / 400;
    yoe = (int64_t)yy - era * 400;
    doy = (153 * (m + ((m > 2) ? -3 : 9)) + 2) / 5 + d - 1;
    doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

static void cal_civil_from_days(int64_t z, int *y, int *m, int *d)
{
    int64_t era;
    int64_t doe;
    int64_t yoe;
    int64_t doy;
    int64_t mp;

    z += 719468;
    era = (z >= 0 ? z : (z - 146096)) / 146097;
    doe = z - era * 146097;
    yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    *y = (int)(yoe + era * 400);
    doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    mp = (5 * doy + 2) / 153;
    *d = (int)(doy - (153 * mp + 2) / 5 + 1);
    *m = (int)(mp + ((mp < 10) ? 3 : -9));
    if (*m <= 2) {
        (*y)++;
    }
}

/* 编译时刻的绝对秒（1970 起）+ 当天第几秒拆开存。 */
static int64_t cal_build_epoch(void)
{
    int month = cal_month_from_name(s_build_date);
    int day = cal_two_digits(&s_build_date[4]);
    int year = 0;
    int hh = cal_two_digits(&s_build_time[0]);
    int mm = cal_two_digits(&s_build_time[3]);
    int ss = cal_two_digits(&s_build_time[6]);

    /* 年份四位数：`__DATE__` 的 %Y 不是零填充的（2026 就是 "2026"）。 */
    year = cal_four_digits(&s_build_date[7]);

    if ((month == 0) || (day <= 0) || (year == 0) || (hh < 0) || (mm < 0) || (ss < 0)) {
        return 0;
    }
    return cal_days_from_civil(year, month, day) * 86400 + hh * 3600 + mm * 60 + ss;
}

/* 当前时刻：编译时刻 + 开机以来的秒。 */
static void cal_now(int *y, int *m, int *d, int *hh, int *mm, int *ss)
{
    const int64_t base = cal_build_epoch();
    const int64_t add = (int64_t)(osl_tick_now() / 1000U);   /* tick 是 ms？见下 */
    int64_t total = base + add;
    int64_t sod;
    int64_t day;

    if (total < 0) {
        total = 0;
    }
    day = total / 86400;
    sod = total - day * 86400;
    cal_civil_from_days(day, y, m, d);
    *hh = (int)(sod / 3600);
    *mm = (int)((sod / 60) % 60);
    *ss = (int)(sod % 60);
}

/* 当月 1 号是星期几（0 = 周日）。 */
static int cal_weekday_of(int y, int m)
{
    int64_t days = cal_days_from_civil(y, m, 1);

    /* 1970-01-01 是周四（=4，0 起算周日）。 */
    return (int)((days + 4) % 7);
}

static int cal_days_in_month(int y, int m)
{
    static const uint8_t base[12] = {31U, 28U, 31U, 30U, 31U, 30U,
                                     31U, 31U, 30U, 31U, 30U, 31U};

    if (m == 2) {
        const int leap = (((y % 4) == 0) && (((y % 100) != 0) || ((y % 400) == 0))) ? 1 : 0;

        return 28 + leap;
    }
    return (int)base[m - 1];
}

/* ---- 界面 ------------------------------------------------------------ */

static void cal_draw(void)
{
    const fml_font_t *font = &fml_font_ascii8x16;
    int y = 1970;
    int m = 1;
    int d = 1;
    int hh = 0;
    int mm = 0;
    int ss = 0;
    int vy = 1970;
    int vm = 1;
    int first_wd;
    int ndays;
    char buf[24];
    unsigned i;

    cal_now(&y, &m, &d, &hh, &mm, &ss);
    fml_ui_draw(&s_win);

    /* 时刻：手拼，不用 sprintf（会把格式化引擎整块拖进镜像）。 */
    buf[0] = (char)('0' + (y / 1000) % 10);
    buf[1] = (char)('0' + (y / 100) % 10);
    buf[2] = (char)('0' + (y / 10) % 10);
    buf[3] = (char)('0' + y % 10);
    buf[4] = '-';
    buf[5] = (char)('0' + (m / 10) % 10);
    buf[6] = (char)('0' + m % 10);
    buf[7] = '-';
    buf[8] = (char)('0' + (d / 10) % 10);
    buf[9] = (char)('0' + d % 10);
    buf[10] = ' ';
    buf[11] = (char)('0' + (hh / 10) % 10);
    buf[12] = (char)('0' + hh % 10);
    buf[13] = ':';
    buf[14] = (char)('0' + (mm / 10) % 10);
    buf[15] = (char)('0' + mm % 10);
    buf[16] = ':';
    buf[17] = (char)('0' + (ss / 10) % 10);
    buf[18] = (char)('0' + ss % 10);
    buf[19] = '\0';
    fml_gui_text(s_win.x + 24U, s_win.y + 56U, buf, font, 0xFFE0U, s_win.bg);

    /* 月历：把"当前月 + 偏移"按周网格排。 */
    vm = m + s_month_shift;
    vy = y;
    while (vm < 1) {
        vm += 12;
        vy--;
    }
    while (vm > 12) {
        vm -= 12;
        vy++;
    }
    first_wd = cal_weekday_of(vy, vm);
    ndays = cal_days_in_month(vy, vm);

    buf[0] = (char)('0' + (vy / 1000) % 10);
    buf[1] = (char)('0' + (vy / 100) % 10);
    buf[2] = (char)('0' + (vy / 10) % 10);
    buf[3] = (char)('0' + vy % 10);
    buf[4] = '-';
    buf[5] = (char)('0' + (vm / 10) % 10);
    buf[6] = (char)('0' + vm % 10);
    buf[7] = '\0';
    fml_gui_text(s_win.x + CAL_GRID_X, s_win.y + 110U, buf, font, s_win.fg, s_win.bg);

    for (i = 0U; i < 7U; i++) {
        static const char *const wd[7] = {"S", "M", "T", "W", "T", "F", "S"};

        fml_gui_text(s_win.x + CAL_GRID_X + i * CAL_CELL_W, s_win.y + CAL_GRID_Y - 20U,
                     wd[i], font, s_win.fg, s_win.bg);
    }
    for (i = 0U; i < (unsigned)ndays; i++) {
        const unsigned cell = (unsigned)first_wd + i;
        const unsigned cx = s_win.x + CAL_GRID_X + (cell % 7U) * CAL_CELL_W;
        const unsigned cy = s_win.y + CAL_GRID_Y + (cell / 7U) * CAL_CELL_H;
        const int daynum = (int)i + 1;
        const uint8_t is_today = ((s_month_shift == 0) && (daynum == d)) ? 1U : 0U;

        buf[0] = (char)('0' + (daynum / 10) % 10);
        if (daynum < 10) {
            buf[0] = (char)('0' + daynum);
            buf[1] = '\0';
        } else {
            buf[1] = (char)('0' + daynum % 10);
            buf[2] = '\0';
        }
        fml_gui_text(cx + 12U, cy + 6U, buf, font,
                     (is_today != 0U) ? 0x07E0U : s_win.fg,
                     (is_today != 0U) ? s_win.bg : s_win.bg);
    }
}

static void cal_enter(void)
{
    if (s_built == 0) {
        fml_ui_win_init(&s_win, 0U, 0U, CAL_WIN_W, CAL_WIN_H,
                        "CALENDAR - tick from build time");
        (void)fml_ui_btn_add(&s_win, APP_ID_BACK, "BACK", CAL_WIN_W - 96U, 8U, 80U, 32U);
        (void)fml_ui_btn_add(&s_win, CAL_ID_PREV, "<", CAL_GRID_X - 56U, 104U, 40U, 28U);
        (void)fml_ui_btn_add(&s_win, CAL_ID_NEXT, ">", CAL_GRID_X + 300U, 104U, 40U, 28U);
        s_built = 1;
    }
    s_last_sec = -1;
    cal_draw();
}

static void cal_tick(void)
{
    int y = 1970;
    int m = 1;
    int d = 1;
    int hh = 0;
    int mm = 0;
    int ss = 0;

    cal_now(&y, &m, &d, &hh, &mm, &ss);
    if (ss != s_last_sec) {
        s_last_sec = ss;
        cal_draw();
    }
}

static void cal_event(const fml_ui_evt_t *ev)
{
    if ((ev->type != FML_UI_EVT_CLICK) ||
        ((ev->id != CAL_ID_PREV) && (ev->id != CAL_ID_NEXT))) {
        return;
    }
    if (ev->id == CAL_ID_PREV) {
        s_month_shift--;
    } else {
        s_month_shift++;
    }
    cal_draw();
}

static fml_ui_win_t *cal_win(void)
{
    return &s_win;
}

static const app_desc_t s_ops = {
    .id = 8U,
    .name = "CAL",
    .enter = cal_enter,
    .tick = cal_tick,
    .win = cal_win,
    .event = cal_event,
    .leave = NULL,
};

const app_desc_t *app_calendar_ops(void)
{
    return &s_ops;
}

void app_calendar_now(int *y, int *m, int *d, int *hh, int *mm, int *ss)
{
    cal_now(y, m, d, hh, mm, ss);
}
