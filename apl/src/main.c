#include "app_sample.h"
#include "board_post.h"
#include "board_system.h"
#include "bsl_init.h"
#include "fml_panic.h"
#include "osl_boot.h"
#include "osl_health.h"
#include "osl_thread.h"

int main(void)
{
    int rc;

    osl_health_init();

    rc = board_system_init();
    osl_boot_stage_note((rc == 0) ? 0U : (uint32_t)rc);
    if (rc != 0) {
        fml_panic(4U);
    }

    /* periph init 的失败必须被消费：bsl_board_periph_init() 在内部把
     * "必需设备"（binding 里 optional: false）的失败打一行日志，可选设备的
     * 失败由 fml 各自降级。返回值不必外传，(void) 明示"已消费"。这里不 panic
     * —— panic 会把单点外设故障放大成复位循环，而 boot-stage 码位是
     * board_system_init 专用的（复用会把"哪一级失败"搅在一起）。 */
    (void)bsl_board_periph_init();

    /* 上电自检：设备初始化之后单独跑一轮，结果只走串口。
     * 与 periph init 分开的原因：init 失败与 selftest 失败在串口上是两行。 */
    board_post_run();

    /* 样例应用：LED 1 s 红绿交替 + 四键去抖 + 蜂鸣器随按键（apl/src/app_sample.c）。
     * 框架层（fml）只提供 panic 播报，演示行为不放功能层。 */
    app_sample_start();

    osl_scheduler_start();

    fml_panic(1U);
    for (;;) {
    }
}