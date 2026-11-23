#ifndef APP_SAMPLE_H
#define APP_SAMPLE_H

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

#endif
