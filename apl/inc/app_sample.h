#ifndef APP_SAMPLE_H
#define APP_SAMPLE_H

#include "fml_ui.h"

/* 样例应用：底板 LED 红绿 1 s 交替 + 四键去抖 + 蜂鸣器随按键。
 *
 * 为什么住在 apl：这三件事是"演示行为"，不是框架能力。
 *   - bsl 已经提供设备能力（board_led / board_key / board_beep 的 api 与 init）；
 *   - 节奏（1 s 交替）、去抖门限（10 ms × 3）、提示音时机（按下响、松开停）
 *     都属于具体应用策略，放在 fml 会让功能层绑定这一块板与这一套演示习惯，
 *     换成自家产品时 fml 要整段删改。
 * fml 保留真正跨应用的基础能力：panic 播报（fml_panic）。
 *
 * 失败语义（与 framework 一致）：
 *   - 任务创建失败 → fml_panic(2U)（启动期致命）；
 *   - 设备未就绪 → 该任务降级、继续喂健康心跳，**不 panic**
 *     （避免单点外设把整机拖进复位循环，策略见 apl/src/main.c 的 periph 注释）。
 */
void app_sample_start(void);

/*
 * P4 的演示窗口（登录在 app_sample 名下，不是 fml 里）。
 *
 * 为什么要放在这里：真手指需要一个轮询者把触点喂给 fml_ui，而按键任务本来就
 * 有 10 ms 的触摸轮询周期（sample_log_touch 就是为触摸准备的）。为它单开一条
 * 线程就得同步扩 osl_health 的槽位表，代价与收益不成比例。P5 有了菜单任务之后
 * 这两个函数连同窗口一起搬过去。
 *
 * app_sample_ui_init() 幂等：main 在 POST 之前先调用一次（ui 项要拿窗口注入点击），
 * app_sample_start() 里再调一次不会重复加按钮。
 */
void app_sample_ui_init(void);

/* 窗口实例（POST 注入与事件泵共用同一份几何，避免两处各写一套坐标）。 */
fml_ui_win_t *app_sample_ui_win(void);

#endif
