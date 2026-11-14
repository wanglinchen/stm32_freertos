# 自动生成，勿手改。刷新：python3 build/build.py update
PROJECT_ROOT := $(abspath $(dir $(lastword $(MAKEFILE_LIST)))/..)
BUILD_VARIANT ?= userdebug
BUILD_DIR := $(PROJECT_ROOT)/out/$(BUILD_VARIANT)/objects
TARGET_DIR := $(BUILD_DIR)/target
.DEFAULT_GOAL := all
include $(PROJECT_ROOT)/toolchain.make
-include $(PROJECT_ROOT)/out/$(BUILD_VARIANT)/.config

INCLUDE_DIRS :=
INCLUDE_DIRS += $(PROJECT_ROOT)/hal/arch/inc
INCLUDE_DIRS += $(PROJECT_ROOT)/hal/chip/register/inc
INCLUDE_DIRS += $(PROJECT_ROOT)/hal/chip/startup/inc
INCLUDE_DIRS += $(PROJECT_ROOT)/hal/periph/inc
INCLUDES := $(addprefix -I,$(INCLUDE_DIRS))

FLAG_DEPS := $(wildcard $(PROJECT_ROOT)/toolchain.make) $(wildcard $(PROJECT_ROOT)/out/$(BUILD_VARIANT)/.config)

LOCAL_OBJS :=
ARCHIVE_OBJS :=
ifeq ($(CONFIG_HAL),y)
ifeq ($(CONFIG_HAL_CHIP),y)
ifeq ($(CONFIG_HAL_CHIP_STARTUP),y)
ifeq ($(CONFIG_HAL_CHIP_STARTUP_SRC),y)
ifeq ($(CONFIG_HAL_CHIP_STARTUP_SRC_SYSTEM_STM32F4XX),y)
ARCHIVE_OBJS += $(BUILD_DIR)/hal/chip/startup/src/system_stm32f4xx.o
endif
endif
endif
endif
endif
ifeq ($(CONFIG_HAL),y)
ifeq ($(CONFIG_HAL_PERIPH),y)
ifeq ($(CONFIG_HAL_PERIPH_SRC),y)
ifeq ($(CONFIG_HAL_PERIPH_SRC_STM32F4XX_HAL),y)
ARCHIVE_OBJS += $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal.o
endif
endif
endif
endif
ifeq ($(CONFIG_HAL),y)
ifeq ($(CONFIG_HAL_PERIPH),y)
ifeq ($(CONFIG_HAL_PERIPH_SRC),y)
ifeq ($(CONFIG_HAL_PERIPH_SRC_STM32F4XX_HAL_ADC),y)
ARCHIVE_OBJS += $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_adc.o
endif
endif
endif
endif
ifeq ($(CONFIG_HAL),y)
ifeq ($(CONFIG_HAL_PERIPH),y)
ifeq ($(CONFIG_HAL_PERIPH_SRC),y)
ifeq ($(CONFIG_HAL_PERIPH_SRC_STM32F4XX_HAL_ADC_EX),y)
ARCHIVE_OBJS += $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_adc_ex.o
endif
endif
endif
endif
ifeq ($(CONFIG_HAL),y)
ifeq ($(CONFIG_HAL_PERIPH),y)
ifeq ($(CONFIG_HAL_PERIPH_SRC),y)
ifeq ($(CONFIG_HAL_PERIPH_SRC_STM32F4XX_HAL_CAN),y)
ARCHIVE_OBJS += $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_can.o
endif
endif
endif
endif
ifeq ($(CONFIG_HAL),y)
ifeq ($(CONFIG_HAL_PERIPH),y)
ifeq ($(CONFIG_HAL_PERIPH_SRC),y)
ifeq ($(CONFIG_HAL_PERIPH_SRC_STM32F4XX_HAL_CEC),y)
ARCHIVE_OBJS += $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_cec.o
endif
endif
endif
endif
ifeq ($(CONFIG_HAL),y)
ifeq ($(CONFIG_HAL_PERIPH),y)
ifeq ($(CONFIG_HAL_PERIPH_SRC),y)
ifeq ($(CONFIG_HAL_PERIPH_SRC_STM32F4XX_HAL_CORTEX),y)
ARCHIVE_OBJS += $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_cortex.o
endif
endif
endif
endif
ifeq ($(CONFIG_HAL),y)
ifeq ($(CONFIG_HAL_PERIPH),y)
ifeq ($(CONFIG_HAL_PERIPH_SRC),y)
ifeq ($(CONFIG_HAL_PERIPH_SRC_STM32F4XX_HAL_CRC),y)
ARCHIVE_OBJS += $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_crc.o
endif
endif
endif
endif
ifeq ($(CONFIG_HAL),y)
ifeq ($(CONFIG_HAL_PERIPH),y)
ifeq ($(CONFIG_HAL_PERIPH_SRC),y)
ifeq ($(CONFIG_HAL_PERIPH_SRC_STM32F4XX_HAL_CRYP),y)
ARCHIVE_OBJS += $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_cryp.o
endif
endif
endif
endif
ifeq ($(CONFIG_HAL),y)
ifeq ($(CONFIG_HAL_PERIPH),y)
ifeq ($(CONFIG_HAL_PERIPH_SRC),y)
ifeq ($(CONFIG_HAL_PERIPH_SRC_STM32F4XX_HAL_CRYP_EX),y)
ARCHIVE_OBJS += $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_cryp_ex.o
endif
endif
endif
endif
ifeq ($(CONFIG_HAL),y)
ifeq ($(CONFIG_HAL_PERIPH),y)
ifeq ($(CONFIG_HAL_PERIPH_SRC),y)
ifeq ($(CONFIG_HAL_PERIPH_SRC_STM32F4XX_HAL_DAC),y)
ARCHIVE_OBJS += $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_dac.o
endif
endif
endif
endif
ifeq ($(CONFIG_HAL),y)
ifeq ($(CONFIG_HAL_PERIPH),y)
ifeq ($(CONFIG_HAL_PERIPH_SRC),y)
ifeq ($(CONFIG_HAL_PERIPH_SRC_STM32F4XX_HAL_DAC_EX),y)
ARCHIVE_OBJS += $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_dac_ex.o
endif
endif
endif
endif
ifeq ($(CONFIG_HAL),y)
ifeq ($(CONFIG_HAL_PERIPH),y)
ifeq ($(CONFIG_HAL_PERIPH_SRC),y)
ifeq ($(CONFIG_HAL_PERIPH_SRC_STM32F4XX_HAL_DCMI),y)
ARCHIVE_OBJS += $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_dcmi.o
endif
endif
endif
endif
ifeq ($(CONFIG_HAL),y)
ifeq ($(CONFIG_HAL_PERIPH),y)
ifeq ($(CONFIG_HAL_PERIPH_SRC),y)
ifeq ($(CONFIG_HAL_PERIPH_SRC_STM32F4XX_HAL_DCMI_EX),y)
ARCHIVE_OBJS += $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_dcmi_ex.o
endif
endif
endif
endif
ifeq ($(CONFIG_HAL),y)
ifeq ($(CONFIG_HAL_PERIPH),y)
ifeq ($(CONFIG_HAL_PERIPH_SRC),y)
ifeq ($(CONFIG_HAL_PERIPH_SRC_STM32F4XX_HAL_DFSDM),y)
ARCHIVE_OBJS += $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_dfsdm.o
endif
endif
endif
endif
ifeq ($(CONFIG_HAL),y)
ifeq ($(CONFIG_HAL_PERIPH),y)
ifeq ($(CONFIG_HAL_PERIPH_SRC),y)
ifeq ($(CONFIG_HAL_PERIPH_SRC_STM32F4XX_HAL_DMA),y)
ARCHIVE_OBJS += $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_dma.o
endif
endif
endif
endif
ifeq ($(CONFIG_HAL),y)
ifeq ($(CONFIG_HAL_PERIPH),y)
ifeq ($(CONFIG_HAL_PERIPH_SRC),y)
ifeq ($(CONFIG_HAL_PERIPH_SRC_STM32F4XX_HAL_DMA2D),y)
ARCHIVE_OBJS += $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_dma2d.o
endif
endif
endif
endif
ifeq ($(CONFIG_HAL),y)
ifeq ($(CONFIG_HAL_PERIPH),y)
ifeq ($(CONFIG_HAL_PERIPH_SRC),y)
ifeq ($(CONFIG_HAL_PERIPH_SRC_STM32F4XX_HAL_DMA_EX),y)
ARCHIVE_OBJS += $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_dma_ex.o
endif
endif
endif
endif
ifeq ($(CONFIG_HAL),y)
ifeq ($(CONFIG_HAL_PERIPH),y)
ifeq ($(CONFIG_HAL_PERIPH_SRC),y)
ifeq ($(CONFIG_HAL_PERIPH_SRC_STM32F4XX_HAL_DSI),y)
ARCHIVE_OBJS += $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_dsi.o
endif
endif
endif
endif
ifeq ($(CONFIG_HAL),y)
ifeq ($(CONFIG_HAL_PERIPH),y)
ifeq ($(CONFIG_HAL_PERIPH_SRC),y)
ifeq ($(CONFIG_HAL_PERIPH_SRC_STM32F4XX_HAL_ETH),y)
ARCHIVE_OBJS += $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_eth.o
endif
endif
endif
endif
ifeq ($(CONFIG_HAL),y)
ifeq ($(CONFIG_HAL_PERIPH),y)
ifeq ($(CONFIG_HAL_PERIPH_SRC),y)
ifeq ($(CONFIG_HAL_PERIPH_SRC_STM32F4XX_HAL_EXTI),y)
ARCHIVE_OBJS += $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_exti.o
endif
endif
endif
endif
ifeq ($(CONFIG_HAL),y)
ifeq ($(CONFIG_HAL_PERIPH),y)
ifeq ($(CONFIG_HAL_PERIPH_SRC),y)
ifeq ($(CONFIG_HAL_PERIPH_SRC_STM32F4XX_HAL_FLASH),y)
ARCHIVE_OBJS += $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_flash.o
endif
endif
endif
endif
ifeq ($(CONFIG_HAL),y)
ifeq ($(CONFIG_HAL_PERIPH),y)
ifeq ($(CONFIG_HAL_PERIPH_SRC),y)
ifeq ($(CONFIG_HAL_PERIPH_SRC_STM32F4XX_HAL_FLASH_EX),y)
ARCHIVE_OBJS += $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_flash_ex.o
endif
endif
endif
endif
ifeq ($(CONFIG_HAL),y)
ifeq ($(CONFIG_HAL_PERIPH),y)
ifeq ($(CONFIG_HAL_PERIPH_SRC),y)
ifeq ($(CONFIG_HAL_PERIPH_SRC_STM32F4XX_HAL_FLASH_RAMFUNC),y)
ARCHIVE_OBJS += $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_flash_ramfunc.o
endif
endif
endif
endif
ifeq ($(CONFIG_HAL),y)
ifeq ($(CONFIG_HAL_PERIPH),y)
ifeq ($(CONFIG_HAL_PERIPH_SRC),y)
ifeq ($(CONFIG_HAL_PERIPH_SRC_STM32F4XX_HAL_FMPI2C),y)
ARCHIVE_OBJS += $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_fmpi2c.o
endif
endif
endif
endif
ifeq ($(CONFIG_HAL),y)
ifeq ($(CONFIG_HAL_PERIPH),y)
ifeq ($(CONFIG_HAL_PERIPH_SRC),y)
ifeq ($(CONFIG_HAL_PERIPH_SRC_STM32F4XX_HAL_FMPI2C_EX),y)
ARCHIVE_OBJS += $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_fmpi2c_ex.o
endif
endif
endif
endif
ifeq ($(CONFIG_HAL),y)
ifeq ($(CONFIG_HAL_PERIPH),y)
ifeq ($(CONFIG_HAL_PERIPH_SRC),y)
ifeq ($(CONFIG_HAL_PERIPH_SRC_STM32F4XX_HAL_FMPSMBUS),y)
ARCHIVE_OBJS += $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_fmpsmbus.o
endif
endif
endif
endif
ifeq ($(CONFIG_HAL),y)
ifeq ($(CONFIG_HAL_PERIPH),y)
ifeq ($(CONFIG_HAL_PERIPH_SRC),y)
ifeq ($(CONFIG_HAL_PERIPH_SRC_STM32F4XX_HAL_FMPSMBUS_EX),y)
ARCHIVE_OBJS += $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_fmpsmbus_ex.o
endif
endif
endif
endif
ifeq ($(CONFIG_HAL),y)
ifeq ($(CONFIG_HAL_PERIPH),y)
ifeq ($(CONFIG_HAL_PERIPH_SRC),y)
ifeq ($(CONFIG_HAL_PERIPH_SRC_STM32F4XX_HAL_GPIO),y)
ARCHIVE_OBJS += $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_gpio.o
endif
endif
endif
endif
ifeq ($(CONFIG_HAL),y)
ifeq ($(CONFIG_HAL_PERIPH),y)
ifeq ($(CONFIG_HAL_PERIPH_SRC),y)
ifeq ($(CONFIG_HAL_PERIPH_SRC_STM32F4XX_HAL_HASH),y)
ARCHIVE_OBJS += $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_hash.o
endif
endif
endif
endif
ifeq ($(CONFIG_HAL),y)
ifeq ($(CONFIG_HAL_PERIPH),y)
ifeq ($(CONFIG_HAL_PERIPH_SRC),y)
ifeq ($(CONFIG_HAL_PERIPH_SRC_STM32F4XX_HAL_HASH_EX),y)
ARCHIVE_OBJS += $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_hash_ex.o
endif
endif
endif
endif
ifeq ($(CONFIG_HAL),y)
ifeq ($(CONFIG_HAL_PERIPH),y)
ifeq ($(CONFIG_HAL_PERIPH_SRC),y)
ifeq ($(CONFIG_HAL_PERIPH_SRC_STM32F4XX_HAL_HCD),y)
ARCHIVE_OBJS += $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_hcd.o
endif
endif
endif
endif
ifeq ($(CONFIG_HAL),y)
ifeq ($(CONFIG_HAL_PERIPH),y)
ifeq ($(CONFIG_HAL_PERIPH_SRC),y)
ifeq ($(CONFIG_HAL_PERIPH_SRC_STM32F4XX_HAL_I2C),y)
ARCHIVE_OBJS += $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_i2c.o
endif
endif
endif
endif
ifeq ($(CONFIG_HAL),y)
ifeq ($(CONFIG_HAL_PERIPH),y)
ifeq ($(CONFIG_HAL_PERIPH_SRC),y)
ifeq ($(CONFIG_HAL_PERIPH_SRC_STM32F4XX_HAL_I2C_EX),y)
ARCHIVE_OBJS += $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_i2c_ex.o
endif
endif
endif
endif
ifeq ($(CONFIG_HAL),y)
ifeq ($(CONFIG_HAL_PERIPH),y)
ifeq ($(CONFIG_HAL_PERIPH_SRC),y)
ifeq ($(CONFIG_HAL_PERIPH_SRC_STM32F4XX_HAL_I2S),y)
ARCHIVE_OBJS += $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_i2s.o
endif
endif
endif
endif
ifeq ($(CONFIG_HAL),y)
ifeq ($(CONFIG_HAL_PERIPH),y)
ifeq ($(CONFIG_HAL_PERIPH_SRC),y)
ifeq ($(CONFIG_HAL_PERIPH_SRC_STM32F4XX_HAL_I2S_EX),y)
ARCHIVE_OBJS += $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_i2s_ex.o
endif
endif
endif
endif
ifeq ($(CONFIG_HAL),y)
ifeq ($(CONFIG_HAL_PERIPH),y)
ifeq ($(CONFIG_HAL_PERIPH_SRC),y)
ifeq ($(CONFIG_HAL_PERIPH_SRC_STM32F4XX_HAL_IRDA),y)
ARCHIVE_OBJS += $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_irda.o
endif
endif
endif
endif
ifeq ($(CONFIG_HAL),y)
ifeq ($(CONFIG_HAL_PERIPH),y)
ifeq ($(CONFIG_HAL_PERIPH_SRC),y)
ifeq ($(CONFIG_HAL_PERIPH_SRC_STM32F4XX_HAL_IWDG),y)
ARCHIVE_OBJS += $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_iwdg.o
endif
endif
endif
endif
ifeq ($(CONFIG_HAL),y)
ifeq ($(CONFIG_HAL_PERIPH),y)
ifeq ($(CONFIG_HAL_PERIPH_SRC),y)
ifeq ($(CONFIG_HAL_PERIPH_SRC_STM32F4XX_HAL_LPTIM),y)
ARCHIVE_OBJS += $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_lptim.o
endif
endif
endif
endif
ifeq ($(CONFIG_HAL),y)
ifeq ($(CONFIG_HAL_PERIPH),y)
ifeq ($(CONFIG_HAL_PERIPH_SRC),y)
ifeq ($(CONFIG_HAL_PERIPH_SRC_STM32F4XX_HAL_LTDC),y)
ARCHIVE_OBJS += $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_ltdc.o
endif
endif
endif
endif
ifeq ($(CONFIG_HAL),y)
ifeq ($(CONFIG_HAL_PERIPH),y)
ifeq ($(CONFIG_HAL_PERIPH_SRC),y)
ifeq ($(CONFIG_HAL_PERIPH_SRC_STM32F4XX_HAL_LTDC_EX),y)
ARCHIVE_OBJS += $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_ltdc_ex.o
endif
endif
endif
endif
ifeq ($(CONFIG_HAL),y)
ifeq ($(CONFIG_HAL_PERIPH),y)
ifeq ($(CONFIG_HAL_PERIPH_SRC),y)
ifeq ($(CONFIG_HAL_PERIPH_SRC_STM32F4XX_HAL_MMC),y)
ARCHIVE_OBJS += $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_mmc.o
endif
endif
endif
endif
ifeq ($(CONFIG_HAL),y)
ifeq ($(CONFIG_HAL_PERIPH),y)
ifeq ($(CONFIG_HAL_PERIPH_SRC),y)
ifeq ($(CONFIG_HAL_PERIPH_SRC_STM32F4XX_HAL_NAND),y)
ARCHIVE_OBJS += $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_nand.o
endif
endif
endif
endif
ifeq ($(CONFIG_HAL),y)
ifeq ($(CONFIG_HAL_PERIPH),y)
ifeq ($(CONFIG_HAL_PERIPH_SRC),y)
ifeq ($(CONFIG_HAL_PERIPH_SRC_STM32F4XX_HAL_NOR),y)
ARCHIVE_OBJS += $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_nor.o
endif
endif
endif
endif
ifeq ($(CONFIG_HAL),y)
ifeq ($(CONFIG_HAL_PERIPH),y)
ifeq ($(CONFIG_HAL_PERIPH_SRC),y)
ifeq ($(CONFIG_HAL_PERIPH_SRC_STM32F4XX_HAL_PCCARD),y)
ARCHIVE_OBJS += $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_pccard.o
endif
endif
endif
endif
ifeq ($(CONFIG_HAL),y)
ifeq ($(CONFIG_HAL_PERIPH),y)
ifeq ($(CONFIG_HAL_PERIPH_SRC),y)
ifeq ($(CONFIG_HAL_PERIPH_SRC_STM32F4XX_HAL_PCD),y)
ARCHIVE_OBJS += $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_pcd.o
endif
endif
endif
endif
ifeq ($(CONFIG_HAL),y)
ifeq ($(CONFIG_HAL_PERIPH),y)
ifeq ($(CONFIG_HAL_PERIPH_SRC),y)
ifeq ($(CONFIG_HAL_PERIPH_SRC_STM32F4XX_HAL_PCD_EX),y)
ARCHIVE_OBJS += $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_pcd_ex.o
endif
endif
endif
endif
ifeq ($(CONFIG_HAL),y)
ifeq ($(CONFIG_HAL_PERIPH),y)
ifeq ($(CONFIG_HAL_PERIPH_SRC),y)
ifeq ($(CONFIG_HAL_PERIPH_SRC_STM32F4XX_HAL_PWR),y)
ARCHIVE_OBJS += $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_pwr.o
endif
endif
endif
endif
ifeq ($(CONFIG_HAL),y)
ifeq ($(CONFIG_HAL_PERIPH),y)
ifeq ($(CONFIG_HAL_PERIPH_SRC),y)
ifeq ($(CONFIG_HAL_PERIPH_SRC_STM32F4XX_HAL_PWR_EX),y)
ARCHIVE_OBJS += $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_pwr_ex.o
endif
endif
endif
endif
ifeq ($(CONFIG_HAL),y)
ifeq ($(CONFIG_HAL_PERIPH),y)
ifeq ($(CONFIG_HAL_PERIPH_SRC),y)
ifeq ($(CONFIG_HAL_PERIPH_SRC_STM32F4XX_HAL_QSPI),y)
ARCHIVE_OBJS += $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_qspi.o
endif
endif
endif
endif
ifeq ($(CONFIG_HAL),y)
ifeq ($(CONFIG_HAL_PERIPH),y)
ifeq ($(CONFIG_HAL_PERIPH_SRC),y)
ifeq ($(CONFIG_HAL_PERIPH_SRC_STM32F4XX_HAL_RCC),y)
ARCHIVE_OBJS += $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_rcc.o
endif
endif
endif
endif
ifeq ($(CONFIG_HAL),y)
ifeq ($(CONFIG_HAL_PERIPH),y)
ifeq ($(CONFIG_HAL_PERIPH_SRC),y)
ifeq ($(CONFIG_HAL_PERIPH_SRC_STM32F4XX_HAL_RCC_EX),y)
ARCHIVE_OBJS += $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_rcc_ex.o
endif
endif
endif
endif
ifeq ($(CONFIG_HAL),y)
ifeq ($(CONFIG_HAL_PERIPH),y)
ifeq ($(CONFIG_HAL_PERIPH_SRC),y)
ifeq ($(CONFIG_HAL_PERIPH_SRC_STM32F4XX_HAL_RNG),y)
ARCHIVE_OBJS += $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_rng.o
endif
endif
endif
endif
ifeq ($(CONFIG_HAL),y)
ifeq ($(CONFIG_HAL_PERIPH),y)
ifeq ($(CONFIG_HAL_PERIPH_SRC),y)
ifeq ($(CONFIG_HAL_PERIPH_SRC_STM32F4XX_HAL_RTC),y)
ARCHIVE_OBJS += $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_rtc.o
endif
endif
endif
endif
ifeq ($(CONFIG_HAL),y)
ifeq ($(CONFIG_HAL_PERIPH),y)
ifeq ($(CONFIG_HAL_PERIPH_SRC),y)
ifeq ($(CONFIG_HAL_PERIPH_SRC_STM32F4XX_HAL_RTC_EX),y)
ARCHIVE_OBJS += $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_rtc_ex.o
endif
endif
endif
endif
ifeq ($(CONFIG_HAL),y)
ifeq ($(CONFIG_HAL_PERIPH),y)
ifeq ($(CONFIG_HAL_PERIPH_SRC),y)
ifeq ($(CONFIG_HAL_PERIPH_SRC_STM32F4XX_HAL_SAI),y)
ARCHIVE_OBJS += $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_sai.o
endif
endif
endif
endif
ifeq ($(CONFIG_HAL),y)
ifeq ($(CONFIG_HAL_PERIPH),y)
ifeq ($(CONFIG_HAL_PERIPH_SRC),y)
ifeq ($(CONFIG_HAL_PERIPH_SRC_STM32F4XX_HAL_SAI_EX),y)
ARCHIVE_OBJS += $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_sai_ex.o
endif
endif
endif
endif
ifeq ($(CONFIG_HAL),y)
ifeq ($(CONFIG_HAL_PERIPH),y)
ifeq ($(CONFIG_HAL_PERIPH_SRC),y)
ifeq ($(CONFIG_HAL_PERIPH_SRC_STM32F4XX_HAL_SD),y)
ARCHIVE_OBJS += $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_sd.o
endif
endif
endif
endif
ifeq ($(CONFIG_HAL),y)
ifeq ($(CONFIG_HAL_PERIPH),y)
ifeq ($(CONFIG_HAL_PERIPH_SRC),y)
ifeq ($(CONFIG_HAL_PERIPH_SRC_STM32F4XX_HAL_SDRAM),y)
ARCHIVE_OBJS += $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_sdram.o
endif
endif
endif
endif
ifeq ($(CONFIG_HAL),y)
ifeq ($(CONFIG_HAL_PERIPH),y)
ifeq ($(CONFIG_HAL_PERIPH_SRC),y)
ifeq ($(CONFIG_HAL_PERIPH_SRC_STM32F4XX_HAL_SMARTCARD),y)
ARCHIVE_OBJS += $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_smartcard.o
endif
endif
endif
endif
ifeq ($(CONFIG_HAL),y)
ifeq ($(CONFIG_HAL_PERIPH),y)
ifeq ($(CONFIG_HAL_PERIPH_SRC),y)
ifeq ($(CONFIG_HAL_PERIPH_SRC_STM32F4XX_HAL_SMBUS),y)
ARCHIVE_OBJS += $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_smbus.o
endif
endif
endif
endif
ifeq ($(CONFIG_HAL),y)
ifeq ($(CONFIG_HAL_PERIPH),y)
ifeq ($(CONFIG_HAL_PERIPH_SRC),y)
ifeq ($(CONFIG_HAL_PERIPH_SRC_STM32F4XX_HAL_SPDIFRX),y)
ARCHIVE_OBJS += $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_spdifrx.o
endif
endif
endif
endif
ifeq ($(CONFIG_HAL),y)
ifeq ($(CONFIG_HAL_PERIPH),y)
ifeq ($(CONFIG_HAL_PERIPH_SRC),y)
ifeq ($(CONFIG_HAL_PERIPH_SRC_STM32F4XX_HAL_SPI),y)
ARCHIVE_OBJS += $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_spi.o
endif
endif
endif
endif
ifeq ($(CONFIG_HAL),y)
ifeq ($(CONFIG_HAL_PERIPH),y)
ifeq ($(CONFIG_HAL_PERIPH_SRC),y)
ifeq ($(CONFIG_HAL_PERIPH_SRC_STM32F4XX_HAL_SRAM),y)
ARCHIVE_OBJS += $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_sram.o
endif
endif
endif
endif
ifeq ($(CONFIG_HAL),y)
ifeq ($(CONFIG_HAL_PERIPH),y)
ifeq ($(CONFIG_HAL_PERIPH_SRC),y)
ifeq ($(CONFIG_HAL_PERIPH_SRC_STM32F4XX_HAL_TIM),y)
ARCHIVE_OBJS += $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_tim.o
endif
endif
endif
endif
ifeq ($(CONFIG_HAL),y)
ifeq ($(CONFIG_HAL_PERIPH),y)
ifeq ($(CONFIG_HAL_PERIPH_SRC),y)
ifeq ($(CONFIG_HAL_PERIPH_SRC_STM32F4XX_HAL_TIM_EX),y)
ARCHIVE_OBJS += $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_tim_ex.o
endif
endif
endif
endif
ifeq ($(CONFIG_HAL),y)
ifeq ($(CONFIG_HAL_PERIPH),y)
ifeq ($(CONFIG_HAL_PERIPH_SRC),y)
ifeq ($(CONFIG_HAL_PERIPH_SRC_STM32F4XX_HAL_UART),y)
ARCHIVE_OBJS += $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_uart.o
endif
endif
endif
endif
ifeq ($(CONFIG_HAL),y)
ifeq ($(CONFIG_HAL_PERIPH),y)
ifeq ($(CONFIG_HAL_PERIPH_SRC),y)
ifeq ($(CONFIG_HAL_PERIPH_SRC_STM32F4XX_HAL_USART),y)
ARCHIVE_OBJS += $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_usart.o
endif
endif
endif
endif
ifeq ($(CONFIG_HAL),y)
ifeq ($(CONFIG_HAL_PERIPH),y)
ifeq ($(CONFIG_HAL_PERIPH_SRC),y)
ifeq ($(CONFIG_HAL_PERIPH_SRC_STM32F4XX_HAL_WWDG),y)
ARCHIVE_OBJS += $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_wwdg.o
endif
endif
endif
endif

.PHONY: all clean subdirs

LIB := $(TARGET_DIR)/lib/libhal.a

all: subdirs
	@$(MAKE) --no-print-directory $(LIB)

subdirs:
	@$(MAKE) --no-print-directory -C $(PROJECT_ROOT)/hal/arch all
	@$(MAKE) --no-print-directory -C $(PROJECT_ROOT)/hal/chip all
	@$(MAKE) --no-print-directory -C $(PROJECT_ROOT)/hal/periph all

$(LIB): $(ARCHIVE_OBJS)
	@mkdir -p $(dir $@)
	@echo "AR $@"
	@rm -f $@
	$(AR) rcs $@ $(ARCHIVE_OBJS)

clean:
	rm -rf $(BUILD_DIR)/hal $(TARGET_DIR)/lib/libhal.a
