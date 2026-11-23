#include "board_system.h"

#include "board_rt.h"
#include "stm32f4xx_hal.h"
#include "system_stm32f4xx.h"

int board_system_init(void)
{
    /* HAL_Init() 的返回值不能丢：NVIC 优先级分组（NVIC_PRIORITYGROUP_4）就是在这一步
     * 设好的，分组没设成时 FreeRTOS 的 BASEPRI 屏蔽会按错的位域工作，而且全程静默
     * （没有 panic、没有码）。跟下面的 SystemClock_Config 同口径：失败原样交给调用方
     * 播报（main 按码 4 闪灯）。
     */
    if (HAL_Init() != HAL_OK) {
        return 1;
    }

    /* 168 MHz 是为了让 PLLQ 给出整数 48 MHz（336 / 7）给 USB/SDIO；180 MHz 做不到
     * （360 / 48 = 7.5）。主频差 10.5 倍属于故障，不允许降级到 HSI 16 MHz 继续跑，
     * 所以把失败原样交给调用方去播报（码 4）。
     */
    /* DTB 的 clocks.hse-frequency 与编译期 HSE_VALUE 是**同一事实的两份声明**：
     * DTB 那份是板级记录（换板改 xlsx/DTS），编译期那份是 PLL 计算的输入
     * （换板改 stm32f4xx_hal_conf.h）。早前 DTB 那份只被解析进 s_rt.hse_hz，
     * 全工程没有一个消费者 —— 等于板级记录写错了也没人会知道。这里对拍：
     *   0       = DTB 未声明，不约束（编译期宏仍是唯一事实）
     *   非 0 且不等 = 有人只改了其中一处，直接失败，别让错的 PLL 参数跑起来
     * 放在 SystemClock_Config() 之前：这时读的是 Flash 里的 DTB，不依赖时钟。 */
    {
        uint32_t hse_dtb = board_rt_hse_hz();

        if (hse_dtb != 0U && hse_dtb != (uint32_t)HSE_VALUE) {
            return 3;
        }
    }

    if (SystemClock_Config() != 0) {
        return 2;
    }

    SystemCoreClockUpdate();

    return 0;
}
