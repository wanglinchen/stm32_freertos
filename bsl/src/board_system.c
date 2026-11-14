#include "board_system.h"

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
    if (SystemClock_Config() != 0) {
        return 2;
    }

    SystemCoreClockUpdate();
    return 0;
}
