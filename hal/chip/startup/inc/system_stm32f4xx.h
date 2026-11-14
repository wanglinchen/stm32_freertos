/**
  ******************************************************************************
  * @file    system_stm32f4xx.h
  * @author  MCD Application Team
  * @brief   CMSIS Cortex-M4 Device System Source File for STM32F4xx devices.
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2017 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */

/** @addtogroup CMSIS
  * @{
  */

/** @addtogroup stm32f4xx_system
  * @{
  */

/**
  * @brief Define to prevent recursive inclusion
  */
#ifndef __SYSTEM_STM32F4XX_H
#define __SYSTEM_STM32F4XX_H

#ifdef __cplusplus
extern "C" {
#endif

/** @addtogroup STM32F4xx_System_Includes
  * @{
  */

/**
  * @}
  */


/** @addtogroup STM32F4xx_System_Exported_types
  * @{
  */
/* This variable is updated in three ways:
    1) by calling CMSIS function SystemCoreClockUpdate()
    2) by calling HAL API function HAL_RCC_GetSysClockFreq()
    3) each time HAL_RCC_ClockConfig() is called to configure the system clock frequency
       Note: If you use this function to configure the system clock; then there
             is no need to call the 2 first functions listed above, since SystemCoreClock
             variable is updated automatically.
*/
extern uint32_t SystemCoreClock;          /*!< System Clock Frequency (Core Clock) */

extern const uint8_t  AHBPrescTable[16];    /*!< AHB prescalers table values */
extern const uint8_t  APBPrescTable[8];     /*!< APB prescalers table values */

/**
  * @}
  */

/** @addtogroup STM32F4xx_System_Exported_Constants
  * @{
  */

/**
  * @}
  */

/** @addtogroup STM32F4xx_System_Exported_Macros
  * @{
  */

/**
  * @}
  */

/** @addtogroup STM32F4xx_System_Exported_Functions
  * @{
  */

extern void SystemInit(void);
extern void SystemCoreClockUpdate(void);
/* HSE 25 MHz → PLL 168 MHz（PLLQ 同时给出 48 MHz 给 USB FS / SDIO）；返回 0 表示成功，非 0 表示仍运行在 HSI 16 MHz。 */
extern int SystemClock_Config(void);
/* SystemClock_Config 失败且 g_boot_stage==2 时：1=VOSRDY 2=HSE/PLL 3=切 SYSCLK。 */
extern volatile uint32_t g_clock_fail_sub;
extern volatile uint32_t g_clock_fail_rcc_cr;
/**
  * @}
  */

#ifdef __cplusplus
}
#endif

#endif /*__SYSTEM_STM32F4XX_H */

/**
  * @}
  */

/**
  * @}
  */
