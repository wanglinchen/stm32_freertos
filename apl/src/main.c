#include "FreeRTOS.h"
#include "task.h"
#include "stm32f4xx_hal.h"
#include "fml_button.h"
#include "fml_led.h"
#include "system_stm32f4xx.h"

int main(void)
{
    /*
     * HAL_Init 做三件以前漏掉的事：
     *   1) 设定 NVIC 优先级分组为 NVIC_PRIORITYGROUP_4（4 位抢占、0 位子优先级），
     *      与 FreeRTOS 的 configMAX_SYSCALL_INTERRUPT_PRIORITY = 5 语义一致；
     *      不设时按复位默认值走，一旦 HAL 驱动里用 HAL_NVIC_SetPriority 就会错位。
     *   2) 初始化 HAL 时基：本工程由 osl 的 HAL_InitTick 切到 DWT 周期计数器，不占 SysTick。
     *   3) 调用 HAL_MspInit（本工程未实现，保持默认）。
     */
    HAL_Init();

    /*
     * 系统时钟：HSE 25 MHz → PLL → HCLK 168 MHz（APB1 42 MHz / APB2 84 MHz）。
     * 168 MHz 是为了让 PLLQ 给出整数 48 MHz（336 / 7）给 USB/SDIO；180 MHz 做不到
     * （360 / 48 = 7.5）。失败时按码 4 播报并停机（fml_led_panic 已标 noreturn）：
     * 主频差 10.5 倍属于故障，不允许降级到 HSI 16 MHz 继续跑。
     */
    if (SystemClock_Config() != 0) {
        fml_led_panic(4U);
    }

    /* 兜底更新：FreeRTOS 的 configCPU_CLOCK_HZ 与 DWT 的每毫秒周期数都取这个变量。 */
    SystemCoreClockUpdate();

    fml_led_blink_start();

    /*
     * 按键/蜂鸣器任务：没有它，bsl 的 board_key_* 与 board_beep_* 会被 --gc-sections
     * 整段丢掉——源码在、归档在、镜像里却什么都没有，按 KEY 毫无反应也不报错。
     */
    fml_button_start();

    vTaskStartScheduler();

    /*
     * vTaskStartScheduler 正常不会返回；返回意味着空闲/Timer 任务创建失败
     * （FreeRTOS 堆不足），按码 1 播报并停机。fml_led_panic 现在是 noreturn
     * （内部关中断自旋），下面的 for(;;) 只是双保险：万一将来有人把 noreturn
     * 与自旋删掉，这里仍然不会让 main 返回进 bx lr 的死循环。
     */
    fml_led_panic(1U);
    for (;;) {
    }
}
