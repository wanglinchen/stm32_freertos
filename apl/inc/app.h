#ifndef APP_H
#define APP_H

#include <stdint.h>

#include "fml_ui.h"

/*
 * 应用注册表与桌面（P5a）。
 *
 * 模型（决策见 docs/design/功能实现对照表.md §3）：**全机只有一个输入泵**，
 * 就是 app_menu 任务。应用不建任务，只提供四个回调。
 *
 * 为什么不是"每个应用一条线程":
 *   1. 触点只有一份 —— board_touch_read 读完 0x814E 要写 0 清标志，两个任务
 *      各自读，后一个必然读到 0 点，表现为"这个应用点不动"；
 *   2. osl 没有删除任务的接口，8 个常驻应用就是 8 份栈与 8 个 health 槽位；
 *   3. 这些应用全是"画 + 点"，没有长阻塞工作。真正长阻塞的（NES、音频）到
 *      P6 单独建任务，那时它自己就是唯一的输入消费者。
 *
 * BACK 约定：每个应用的窗口最后加一个 id = APP_ID_BACK 的按钮（label "BACK"）。
 * app_menu_dispatch 拦截它回桌面，应用自己的 event 收不到这个 id。
 */

/*
 * 上限从 11 扩到 13：P6e-2 的 QR 占第 12 项，P6e-3 的 NES 预留第 13 项。
 * 为什么不一次只扩到 12 再等下一批扩到 13：这张表每个空槽只多 24 B（一个
 * app_desc_t），而"每加一个应用就改一次上限"会让每次改动的 diff 都碰同一个
 * 常量 —— 一次扩到位，P6e-3 落地时不必再动它。
 */
#define APP_COUNT_MAX 13U
#define APP_ID_BACK 0xFFU

/* 双击判定窗口：同一图标两次 CLICK 的间隔不超过这个毫秒数就算进入。
 * 350 ms 是按"手指抬起到第二次按下"的常见节奏定的：太短会把正常双击拆成
 * 两次单击，太长会让"点了两下不同图标"误进前一个。
 *
 * P5b 起改为**运行期变量**（设置页可改），这个宏只是上电默认值。
 * 为什么不在没改之前就做成变量：P5a 时没有消费者，做出来就是死代码。 */
#define APP_DOUBLE_CLICK_MS_DEFAULT 350U
#define APP_DOUBLE_CLICK_MS_MIN 200U
#define APP_DOUBLE_CLICK_MS_MAX 800U

uint32_t app_double_click_ms(void);
/* 限幅到 [MIN, MAX]：设置页只能给三个档位，但接口不能假定调用方守规矩 ——
 * 传 0 会让同一图标"永远不双击"，而屏上没有任何提示。 */
void app_set_double_click_ms(uint32_t ms);

/* 图标网格：固定 4 列，行数按注册表条目数算。图标随注册表增长（P5a 3 个、
 * P5b 5 个、P5c 8 个），不写死 8 —— 写死就得为空槽建空壳应用。 */
/* 桌面图标网格：列数不再写死。800×480 时 4 列×160 宽刚好铺满；换到 480×800
 * 竖屏后 4 列总宽 688 会让 origin_x 往下溢，图标被摆到屏外 —— POST 的 ui 项
 * 报的 "ink=0 hit=6/8" 就是这个。列数按屏宽算（见 app.c 的 desktop_build）。 */
#define APP_ICON_MAX_COLS 4U
#define APP_ICON_W 160U
#define APP_ICON_H 120U
#define APP_ICON_GAP 16U

typedef uint8_t app_id_t;

typedef struct {
    app_id_t id;                            /* 1..254，0 保留给"无应用" */
    const char *name;                       /* 图标标签（ASCII，8×16 字库） */
    void (*enter)(void);                    /* 进入：填自己的窗口并画一次 */
    void (*tick)(void);                     /* 可选：每轮调用，用于实时状态；NULL 允许 */
    fml_ui_win_t *(*win)(void);             /* 当前窗口，事件泵把触点 feed 给它 */
    void (*event)(const fml_ui_evt_t *ev);  /* CLICK（BACK 已被菜单拦截，收不到） */
    void (*leave)(void);                    /* 退出：关掉 tick 维持的执行器（见下）；NULL 允许 */
} app_desc_t;

/*
 * leave 为什么必须有（2026-11-24 P5a 收工核对时补，规则见对照表 §3）：
 * 退出应用后它的 tick 不再被调，于是"由 tick 维持的状态"没人关 ——
 * app_beep 的 40 ms 最短响会一直响到下次进 BEEP，app_led 点亮的灯会亮着
 * 而屏上已经没有任何 LED 状态（违反"屏上显示 = 执行器实际状态"）。
 * app_menu_dispatch 在 ENTER（进下一个之前）与 BACK（s_current 归零之前）各调一次。
 */

/* app_menu_dispatch 的返回值：既是"处理了没有"，也是 POST 能判死的动作码。 */
#define APP_ACTION_NONE 0
#define APP_ACTION_SELECT 1  /* 桌面单击：记下候选 */
#define APP_ACTION_ENTER 2   /* 桌面双击：已进入应用 */
#define APP_ACTION_BACK 3    /* 应用内 BACK：已回桌面 */
#define APP_ACTION_APP 4     /* 事件已转交给当前应用 */

const app_desc_t *app_at(unsigned index);
unsigned app_count(void);
const app_desc_t *app_find(app_id_t id);

/* 桌面窗口（`[post] ui` 与 `[post] menu` 共用），以及"当前该喂哪个窗口"。 */
fml_ui_win_t *app_menu_win(void);
fml_ui_win_t *app_menu_current_win(void);
unsigned app_menu_depth(void);   /* 0 = 桌面，1 = 正在某个应用里 */
unsigned app_menu_leave_count(void);  /* 已调过几次 leave（POST 判据） */

/* 事件路由：桌面（单击选中、双击进入）与应用内（BACK 返回、其余转给应用）。
 * app_menu 任务与 POST 的 menu 项都调它 —— 一份实现，两处不会漂移。 */
int app_menu_dispatch(const fml_ui_evt_t *ev);

/*
 * 应用事件日志的总开关（默认开）。
 *
 * POST 的 menu 项要注入点击，而点击会落到应用的事件回调里；回调若照常打
 * `[app_calc] key ...`，那几行就会插进 POST 的项与判定之间 —— 项名和结果已经
 * 由 bsl 侧一次性打出来了，中间再冒出别的模块的行，读日志的人就分不清哪些
 * 属于这一项。所以 POST 期间关掉（apl/src/main.c 在 board_post_run() 前后拨）。
 *
 * 关的是"应用回调里的日志"，不是 bsl 的：POST 自己的项照常输出。
 */
void app_set_trace(int on);
int app_trace_on(void);


/* 建桌面并画一次（必须在 POST 之前调，两个 POST 项都要拿它注入）；
 * app_start() 启动唯一的输入泵任务。 */
void app_menu_init(void);
void app_start(void);

/* P5a 的三个应用。P5b/P5c 各加一条 app_<name>_ops()。 */
const app_desc_t *app_led_ops(void);
const app_desc_t *app_key_ops(void);
const app_desc_t *app_beep_ops(void);

/* P5b 的两个应用。 */
const app_desc_t *app_calc_ops(void);
const app_desc_t *app_settings_ops(void);

/* 计算器显示值（只读）。POST 用它核对"注入 1 + 2 = 之后屏上真的是 3" ——
 * 没有这个出口，POST 只能判"按钮画出来了"，判不出算得对不对。 */
const char *app_calc_display(void);

/* 设置页当前背光状态（1 = 亮）。POST 用它核对"切两次之后真的回到原值"。 */
int app_settings_backlight(void);

/* 设置页累计"生效的切换次数"。POST 判据不能只看终值：两次都没生效时终值
 * 同样等于初值，光看终值会把"完全坏了"判成通过。 */
unsigned app_settings_flips(void);

/* P5c 的第一个应用：图片查看器。另外两个（记事本 / 日历）在同批后续加。 */
const app_desc_t *app_picviewer_ops(void);

/* 图片查看器这次打开的结果：1 = 找到并解码成功，0 = 没找到或解码失败。
 * POST 的 `pic=` 字段靠它算：w×h×3+54 必须等于文件长度（那张图是 320×240/24 位），
 * 头部解析错一位这个等式就不成立 —— 比"屏上看着像"硬得多。
 * ink 是非黑像素数：全黑图与"根本没画"在屏上一样，只有计数分得开。 */
int app_picviewer_stat(int32_t *w, int32_t *h, uint16_t *bpp, uint32_t *ink);

/* 找到的文件路径（长名；空串 = 没找到）。POST 把它打进 `pic=` 便于现场核对。 */
const char *app_picviewer_path(void);

/* P5c 的另两个应用：记事本（fml_t9 九宫格拼音）与日历（osl_tick 走时，
 * 真实 RTC 在 P6）。 */
const app_desc_t *app_notepad_ops(void);
const app_desc_t *app_calendar_ops(void);

/* 记事本已输入的 GBK 字节数与内容。POST 的 `note=` 用它判"94664 → 候选里的
 * '中' 真的落进了文本缓冲"：只判按钮点得动，判不出输入法整条链通没通。 */
unsigned app_notepad_len(void);
const char *app_notepad_text(void);

/* 当前数字串的匹配音节数与候选字总数。94664 上应该是 2 个音节 / 18 个字
 * （xiong 7 + zhong 11）—— POST 把这两个数也打进日志，词库一换就当场可见，
 * 免得再去猜"为什么候选条里的第 8 格变成了别的字"。 */
unsigned app_notepad_syl_count(void);
unsigned app_notepad_cand_total(void);

/* 日历当前读数。没有真实时间源，起点是**编译时刻**（见 app_calendar.c），
 * POST 只判字段范围合法 —— 判"时间是准的"需要 RTC，那是 P6 的事。 */
void app_calendar_now(int *y, int *m, int *d, int *hh, int *mm, int *ss);

/*
 * 记事本的按钮 id（**公开契约**）。候选键的 label 是 NULL —— 汉字要用 GBK 渲染，
 * 而 `fml_ui_btn_t.label` 只认 ASCII。POST 因此只能按 id 点候选；把 id 写在
 * app_notepad.c 里当私有的，main.c 就得再抄一份 100，两处迟早漂移。
 */
#define APP_NOTE_ID_CAND0 100U
#define APP_NOTE_ID_PREV 110U
#define APP_NOTE_ID_NEXT 111U
#define APP_NOTE_ID_ZERO 10U
#define APP_NOTE_ID_DEL 11U

/*
 * P8b：绘画（`paint_play`）。画布不是按钮（命中返回 -1、事件 id = 0），笔迹
 * 靠 `FML_UI_EVT_DOWN/MOVE/UP` 的坐标落点 —— 所以它的"画上了没有"只能看像素：
 * POST 注入一段 DOWN→MOVE→UP 后读 `app_paint_ink()`，判它从 0 变成 > 0。
 */
const app_desc_t *app_paint_ops(void);
/* 画布矩形（屏幕绝对坐标）。POST 用它做 `bsl_fb_count_nonbg(…, 0x0000)` ——
 * 判的是"帧缓冲里真有笔迹"，而不是应用自己报的计数器：计数器即使在画之前
 * 被 +1 也照样能通过，像素数骗不了人。 */
void app_paint_canvas(int *x, int *y, int *w, int *h);

/*
 * P8c：陀螺仪与水平仪（`gyro_play` / `grad_play`）。
 * 两个应用都把 `board_mpu9250_read` 的结果留在模块级 static 里，POST 直接取 —
 * 不重读一次总线（重读会让"应用读到的那一份"和"POST 判的那一份"变成两次采样，
 * 板子一晃就不一致）。
 */
const app_desc_t *app_imu_ops(void);
/* 返回 1 = 这个 tick 真的读到了（0 表示 IMU 未 ready 或读失败）。 */
int app_imu_sample(int16_t acc[3], int16_t gyr[3]);

const app_desc_t *app_level_ops(void);
/* 倾角，单位 0.1°（-900..900）。没有数学库，用 ax/az 的小角线性近似：
 * dd ≈ (ax * 573) / |az|，量程内误差随角度增大而变大，只作"准星动了"的判据。 */
int app_level_angle_dd(void);
/* 算出这个倾角所用的那一份加速度原始值（与 `app_level_angle_dd()` 同一次采样）。
 * POST 用它做自洽判读：**不能借 `app_imu_sample()`** —— 那是另一个应用实例的
 * static，陀螺仪应用 BACK 时会在 leave 里清零，这里取到的就是 0，
 * 于是"倾角 3.9° vs 期望 0°"被判不自洽（P8c 首版就撞在这上面）。 */
int app_level_acc(int16_t acc[3]);

/*
 * P6e-2：二维码（`qr_play`）。
 *
 * 参考工程用 T9 九宫格输入任意文本；本工程没有通用文本输入控件，所以应用内
 * 是三条预设串轮换（数字 / 网址 / 大写字母，正好三条编码路径各走一遍），
 * POST 可用 `app_qr_inject` 注入任意短串。
 *
 * 编码结果的三个数（版本 / 边长 / 黑模块数）由应用保存 `fml_qr` 最近一次的
 * 结果 —— POST 不重新编码一遍再读，否则"应用画的那一份"和"判据读的那一份"
 * 会变成两次编码。
 */
const app_desc_t *app_qr_ops(void);
/* 编码一段文本并重画（0 成功）。`app_qr_inject` 先把串拷进应用自己的缓冲：
 * POST 的字符串可能在栈上，应用要留着它在换串时重编码。 */
int app_qr_show(const char *text);
int app_qr_inject(const char *text);
unsigned app_qr_version(void);
unsigned app_qr_size(void);
unsigned app_qr_dark(void);
/* 点阵白底板矩形（屏幕绝对坐标，含静区）。POST 用它数"板上的黑模块像素"：
 * 编码算对 ≠ 真的画到屏上。 */
void app_qr_plate(int *x, int *y, int *w, int *h);

#endif
