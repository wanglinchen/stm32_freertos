#include "FreeRTOS.h"
#include "task.h"
#include "board_system.h"
#include "fml_button.h"
#include "fml_led.h"
#include "fml_panic.h"
#include "osl_boot.h"
#include "osl_health.h"

int main(void)
{
    int rc;

    /*
     * 水位记录必须先复位成“未采样”：它们放在 .bss（复位为 0），而 0 对
     * “历史最低剩余”是无意义的值 —— 每次采样都会比它大，最低值就永远记成 0。
     * 这一步必须早于 HAL_Init()，所以它留在 main() 里，不进 board_system_init()。
     */
    osl_health_init();

    /*
     * HAL 初始化 + 系统时钟（16 MHz HSI → 168 MHz）+ 主频变量刷新，三步一起
     * 收在 bsl 的 board_system_init()：apl 直接 include hal 头文件会越过
     * 「apl 只依赖 fml/bsl/osl」这条分层约定（bsl 是本层允许的边、hal 不是），
     * 而链接顺序会把越级掩盖掉。
     *
     * 失败时先把「哪一步失败」写进 .noinit 的 g_boot_stage，再按码 4 播报并停机：
     * 两个失败点（HAL_Init 的 NVIC 分组 / SystemClock_Config 的晶振与 PLL）排查
     * 方向互斥，而三灯只能播「码 4」这一点信息。写点必须在 fml_panic 之前 ——
     * 它标了 noreturn，进了自旋就再没机会写。成功路径也写 0：.noinit 里的
     * 上电随机值与上一轮旧值都必须被覆盖，否则「读到 0」不再是「启动正常」。
     */
    rc = board_system_init();
    osl_boot_stage_note((rc == 0) ? 0U : (uint32_t)rc);
    if (rc != 0) {
        fml_panic(4U);
    }

    fml_led_blink_start();

    /*
     * 按键/蜂鸣器任务：没有它，bsl 的 board_key_* 与 board_beep_* 会被 --gc-sections
     * 整段丢掉——源码在、归档在、镜像里却什么都没有，按 KEY 毫无反应也不报错。
     */
    fml_button_start();

    vTaskStartScheduler();

    /*
     * vTaskStartScheduler 正常不会返回；返回意味着空闲/Timer 任务创建失败
     * （FreeRTOS 堆不足），按码 1 播报并停机。fml_panic 现在是 noreturn
     * （内部关中断自旋），下面的 for(;;) 只是双保险：万一将来有人把 noreturn
     * 与自旋删掉，这里仍然不会让 main 返回进 bx lr 的死循环。
     */
    fml_panic(1U);
    for (;;) {
    }
}
