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

#define APP_COUNT_MAX 8U
#define APP_ID_BACK 0xFFU

/* 双击判定窗口：同一图标两次 CLICK 的间隔不超过这个毫秒数就算进入。
 * 350 ms 是按"手指抬起到第二次按下"的常见节奏定的：太短会把正常双击拆成
 * 两次单击，太长会让"点了两下不同图标"误进前一个。 */
#define APP_DOUBLE_CLICK_MS 350U

/* 图标网格：固定 4 列，行数按注册表条目数算。图标随注册表增长（P5a 3 个、
 * P5b 5 个、P5c 8 个），不写死 8 —— 写死就得为空槽建空壳应用。 */
#define APP_ICON_COLS 4U
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

/* 建桌面并画一次（必须在 POST 之前调，两个 POST 项都要拿它注入）；
 * app_start() 启动唯一的输入泵任务。 */
void app_menu_init(void);
void app_start(void);

/* P5a 的三个应用。P5b/P5c 各加一条 app_<name>_ops()。 */
const app_desc_t *app_led_ops(void);
const app_desc_t *app_key_ops(void);
const app_desc_t *app_beep_ops(void);

#endif
