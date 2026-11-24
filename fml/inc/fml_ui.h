#ifndef FML_UI_H
#define FML_UI_H

#include <stdint.h>

/*
 * 窗口 / 控件 / 事件（P4）。
 *
 * 只做三件事：登记几何、算命中、维护一条事件队列。**不持有像素缓冲，也不做
 * 重绘调度** —— 每次 draw 直接画到帧缓冲上，谁在什么时候重画由调用方决定。
 * 与 fml_gui 同一个理由：双缓冲要先有"谁负责重绘"的确定答案，现在加一层
 * 只会让每个调用点多一次整屏拷贝。
 *
 * 触点由调用方 feed 进来，本模块不自己读触摸。两个原因：
 *   1. POST 要能在没有手指的情况下注入坐标验命中（判据不能等用户来按）；
 *   2. 事件语义（按下 / 抬起 / 点击）不该与 10 ms 的轮询周期绑死，谁来喂、
 *      多快喂，是应用的事。
 *
 * **单活动窗口**：事件状态（s_pressed / s_press_btn）是模块级的 static，不是
 * 每个窗口一份。P5 真要做多窗口时，这些状态要么挂到窗口对象上，要么改成
 * "焦点窗口"模型 —— 现在提前做等于在只有一个消费者的场景里猜需求。
 *
 * 坐标：屏幕像素、横屏、左上原点，与 bsl_fb / fml_gui 一致。窗口内的按钮用
 * **相对窗口**的坐标登记，绘制与命中时加窗口偏移 —— 这样窗口挪位置不必逐控件改。
 *
 * 无临界区的前提：同一时刻只有一个任务喂点（app_sample 的 10 ms 轮询），且
 * POST 注入发生在同一上下文里、注入完立刻取事件。若将来在中断里读触摸再 feed，
 * 队列必须补 __disable_irq 保存-恢复式临界区（见 .skills/SKILL.md 第 9 条）。
 */

/*
 * 按钮上限 20：P4 的三个按钮够用，P5b 的 4×4 计算器要 16 个键 + BACK = 17，
 * 4 装不下。取 20 而不是 17，是给"以后某一页还要多两个键"留一点余量，
 * 又不至于让每个窗口都多背几百字节 —— fml_ui_btn_t 24 B，每个窗口
 * 4→20 多 384 B，九个窗口合计约 +3.5 KB bss（清单见对照表 §4 P5b 行）。
 */
#define FML_UI_BTN_MAX 20U
#define FML_UI_EVT_MAX 8U

/* 事件类型。id 为 0 表示"没命中任何按钮"：DOWN/UP 照常产生，但 CLICK 不会。 */
typedef enum {
    FML_UI_EVT_NONE = 0,
    FML_UI_EVT_DOWN = 1,
    FML_UI_EVT_UP = 2,
    FML_UI_EVT_CLICK = 3
} fml_ui_evt_type_t;

typedef struct {
    uint16_t id;          /* 命中回打的就是它；0 保留给"无按钮" */
    uint32_t x;           /* 相对窗口左上 */
    uint32_t y;
    uint32_t w;
    uint32_t h;
    const char *label;    /* ASCII；NULL = 不画字 */
} fml_ui_btn_t;

typedef struct {
    uint32_t x;           /* 窗口左上（屏幕绝对坐标） */
    uint32_t y;
    uint32_t w;
    uint32_t h;
    const char *title;
    uint16_t bg;
    uint16_t fg;          /* 标题与状态行文字 */
    uint16_t line;        /* 边框与分隔线 */
    uint16_t btn_bg;      /* 按钮常态底色 */
    uint16_t btn_fg;      /* 按钮文字 */
    uint16_t hot_bg;      /* 按下时的底色（反色），与 btn_bg 反差越大越好认 */
    fml_ui_btn_t btn[FML_UI_BTN_MAX];
    unsigned n_btn;
    int hot;              /* 当前按下按钮下标，-1 = 无 */
} fml_ui_win_t;

typedef struct {
    fml_ui_evt_type_t type;
    uint16_t id;
    int32_t x;            /* 产生事件时的触点坐标（屏幕绝对） */
    int32_t y;
} fml_ui_evt_t;

/* 初始化窗口几何与配色。hot 置 -1；不清事件队列（那是 fml_ui_reset 的事，
 * 否则"新建窗口"会悄悄丢掉另一个窗口已经产生的事件）。 */
void fml_ui_win_init(fml_ui_win_t *win, uint32_t x, uint32_t y, uint32_t w,
                     uint32_t h, const char *title);

/* 加按钮（相对窗口）。返回 1 = 成功，0 = 表满 / 参数非法。 */
int fml_ui_btn_add(fml_ui_win_t *win, uint16_t id, const char *label, uint32_t x,
                   uint32_t y, uint32_t w, uint32_t h);

/* 命中测试：返回按钮下标，-1 = 未命中。重叠时取先登记的那个（后登记的压在
 * 下面，与绘制顺序相反 —— 想让某个按钮优先命中就把它先加进去）。 */
int fml_ui_hit(const fml_ui_win_t *win, int32_t x, int32_t y);

/* 按 id 找按钮下标，-1 = 没有。POST 用它核对"注入的 id 是不是这个按钮的"。 */
int fml_ui_btn_by_id(const fml_ui_win_t *win, uint16_t id);

/* 画/重画单个按钮（按下高亮只重画这一个，不必整窗刷）。 */
void fml_ui_draw_btn(const fml_ui_win_t *win, unsigned idx);

/* 整窗重画：底色、边框、标题、分隔线、全部按钮、底部 `hit=<id>` 状态行。
 * 状态行的作用是给手指一个当场可读的反馈，与串口 `[ui] hit btn=` 对得上。 */
void fml_ui_draw(const fml_ui_win_t *win);

/* 喂一个触点。pressed != 0 表示按下。边沿语义：
 *   0→1：记下按在哪个按钮，高亮它，产 DOWN（id = 命中按钮 id 或 0）
 *   1→1：手指在按钮间滑动时只换高亮，不产事件（拖动留给 P5）
 *   1→0：产 UP；抬起时高亮仍落在某个按钮上就再产 CLICK（id = 该按钮 id）。
 *        **抬起帧不重算命中**：总线上此刻没有触点，坐标是上一帧残值，拿它
 *        重算会把正常点击判成"滑出"。滑动检测在按下期间逐帧做，抬手只认结果。
 * pressed 为 0 时坐标不参与判断（总线上此刻没有触点，x/y 是上一帧的残值）。 */
void fml_ui_feed(fml_ui_win_t *win, int32_t x, int32_t y, int pressed);

/* 取一个事件：1 = 取到，0 = 队列空。 */
int fml_ui_next(fml_ui_evt_t *ev);

/* 队列溢出计数（push 时队满就丢，并把这个数 +1）。POST 要求它为 0：溢出说明
 * 消费端比产生端慢，屏上会有"点了没反应"，而串口上看不出区别。 */
unsigned fml_ui_overflow(void);

/* 清空事件队列与按压状态，并丢弃溢出计数。POST 注入前调用，避免与真实手指
 * 的残留事件混在一起。不碰窗口的 hot（那要由 draw 的调用方决定怎么显示）。 */
void fml_ui_reset(void);

#endif
