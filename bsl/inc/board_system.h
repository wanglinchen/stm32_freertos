#ifndef BOARD_SYSTEM_H
#define BOARD_SYSTEM_H

/* 板级运行环境门面：HAL 初始化 + 系统时钟 + 主频变量刷新。
 *
 * 存在的理由不是「少写两行」：apl 原本直接 include hal 的 stm32f4xx_hal.h 与
 * system_stm32f4xx.h，越过了 LAYER_DEPENDENCIES 里 apl 的允许边（fml/bsl/osl，
 * 不含 hal），而链接顺序（apl 排最前、hal 排最后）把这个越级掩盖住了 ——
 * collect_layer_dependency_errors() 现在按 LAYER_DEPENDENCIES 直接查源码依赖，
 * 所以这三步必须从 apl 收回到 bsl：apl → bsl 是允许的边，换芯片/换时钟树只动 bsl。
 *
 * 顺序契约（不可交换）：
 *   1) osl_health_init() 必须在 HAL_Init() 之前（水位记录先复位成「未采样」，
 *      因为 .bss 初值 0 对「历史最低剩余」是无意义的值）；
 *   2) HAL_Init() 建立 NVIC 优先级分组（NVIC_PRIORITYGROUP_4，与
 *      configMAX_SYSCALL_INTERRUPT_PRIORITY = 5 语义一致）并初始化 HAL 时基
 *      （本工程由 osl 的 HAL_InitTick 切到 DWT，不占 SysTick）；
 *   3) 调用 hal 中 `SystemClock_Config()`（实现在 `system_stm32f4xx.c`，HSE 25 MHz → 168 MHz）；
 *      实现留在 hal、编排留在 bsl——换板时改 bsl 调用顺序或换 hal 内板级参数，apl 仍只 include 本头。
 *   4) SystemCoreClockUpdate()：FreeRTOS 的 configCPU_CLOCK_HZ 与 DWT 每毫秒
 *      周期数都读这个变量，漏了会按旧主频折算。
 *
 * 所以这里只封装 2)~4)，第 1 步仍由 main() 自己保证顺序。
 */

/* 0 = 已切到 168 MHz；非 0 = 启动失败，调用方按故障处理（main 统一播报码 4）。
 *
 * 失败码**分开**，因为两个失败点的排查方向互斥：
 *   1 = HAL_Init() 失败：NVIC 优先级分组（NVIC_PRIORITYGROUP_4）或 HAL 时基没建立，
 *       芯片停在 HSI 16 MHz。先查分组配置，此时去查 25 MHz 晶振与 PLL 是白查；
 *   2 = SystemClock_Config() 失败：再读 `g_clock_fail_sub`（1=VOSRDY 2=HSE/PLL 3=切 SYSCLK）、
 *       `g_clock_fail_rcc_cr`（RCC->CR 快照）；HSE 未起振时芯片停在 HSI 16 MHz，
 *       这时才该查晶振与 PLLQ。
 *
 * main() 把这个值写进 `.noinit` 的 `g_boot_stage`（定义在 osl 的启动模块），
 * 复位后调试器读它即可知道上一次是哪一步失败 —— 三灯只能播「码 4」，码本身不含阶段。
 * 早先两个分支都 return 1：注释写着「两种来源、方向完全不同」，现场却无从分辨；
 * 现在注释与机制一致。 */
int board_system_init(void);

#endif
