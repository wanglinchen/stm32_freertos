# 自动生成，勿手改。刷新：python3 build/build.py update
PROJECT_ROOT := $(abspath $(dir $(lastword $(MAKEFILE_LIST)))/../../..)
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
-include $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal.d
-include $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_adc.d
-include $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_adc_ex.d
-include $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_can.d
-include $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_cec.d
-include $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_cortex.d
-include $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_crc.d
-include $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_cryp.d
-include $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_cryp_ex.d
-include $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_dac.d
-include $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_dac_ex.d
-include $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_dcmi.d
-include $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_dcmi_ex.d
-include $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_dfsdm.d
-include $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_dma.d
-include $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_dma2d.d
-include $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_dma_ex.d
-include $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_dsi.d
-include $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_eth.d
-include $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_exti.d
-include $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_flash.d
-include $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_flash_ex.d
-include $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_flash_ramfunc.d
-include $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_fmpi2c.d
-include $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_fmpi2c_ex.d
-include $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_fmpsmbus.d
-include $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_fmpsmbus_ex.d
-include $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_gpio.d
-include $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_hash.d
-include $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_hash_ex.d
-include $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_hcd.d
-include $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_i2c.d
-include $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_i2c_ex.d
-include $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_i2s.d
-include $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_i2s_ex.d
-include $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_irda.d
-include $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_iwdg.d
-include $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_lptim.d
-include $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_ltdc.d
-include $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_ltdc_ex.d
-include $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_mmc.d
-include $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_nand.d
-include $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_nor.d
-include $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_pccard.d
-include $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_pcd.d
-include $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_pcd_ex.d
-include $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_pwr.d
-include $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_pwr_ex.d
-include $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_qspi.d
-include $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_rcc.d
-include $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_rcc_ex.d
-include $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_rng.d
-include $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_rtc.d
-include $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_rtc_ex.d
-include $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_sai.d
-include $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_sai_ex.d
-include $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_sd.d
-include $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_sdram.d
-include $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_smartcard.d
-include $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_smbus.d
-include $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_spdifrx.d
-include $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_spi.d
-include $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_sram.d
-include $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_tim.d
-include $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_tim_ex.d
-include $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_uart.d
-include $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_usart.d
-include $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_wwdg.d

ifeq ($(CONFIG_HAL),y)
ifeq ($(CONFIG_HAL_PERIPH),y)
ifeq ($(CONFIG_HAL_PERIPH_SRC),y)
ifeq ($(CONFIG_HAL_PERIPH_SRC_STM32F4XX_HAL),y)
LOCAL_OBJS += $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal.o
$(BUILD_DIR)/hal/periph/src/stm32f4xx_hal.o: $(PROJECT_ROOT)/hal/periph/src/stm32f4xx_hal.c $(FLAG_DEPS)
	@mkdir -p $(dir $@)
	@echo "CC $<"
	$(CC) $(CFLAGS) -MMD -MP -MF $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal.d -c $< -o $@
	@"$(PYTHON)" -c 'import os,sys,time; t=time.time(); [os.utime(p,(t,t)) for p in sys.argv[1:]]' $@ $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal.d
endif
endif
endif
endif

ifeq ($(CONFIG_HAL),y)
ifeq ($(CONFIG_HAL_PERIPH),y)
ifeq ($(CONFIG_HAL_PERIPH_SRC),y)
ifeq ($(CONFIG_HAL_PERIPH_SRC_STM32F4XX_HAL_ADC),y)
LOCAL_OBJS += $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_adc.o
$(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_adc.o: $(PROJECT_ROOT)/hal/periph/src/stm32f4xx_hal_adc.c $(FLAG_DEPS)
	@mkdir -p $(dir $@)
	@echo "CC $<"
	$(CC) $(CFLAGS) -MMD -MP -MF $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_adc.d -c $< -o $@
	@"$(PYTHON)" -c 'import os,sys,time; t=time.time(); [os.utime(p,(t,t)) for p in sys.argv[1:]]' $@ $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_adc.d
endif
endif
endif
endif

ifeq ($(CONFIG_HAL),y)
ifeq ($(CONFIG_HAL_PERIPH),y)
ifeq ($(CONFIG_HAL_PERIPH_SRC),y)
ifeq ($(CONFIG_HAL_PERIPH_SRC_STM32F4XX_HAL_ADC_EX),y)
LOCAL_OBJS += $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_adc_ex.o
$(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_adc_ex.o: $(PROJECT_ROOT)/hal/periph/src/stm32f4xx_hal_adc_ex.c $(FLAG_DEPS)
	@mkdir -p $(dir $@)
	@echo "CC $<"
	$(CC) $(CFLAGS) -MMD -MP -MF $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_adc_ex.d -c $< -o $@
	@"$(PYTHON)" -c 'import os,sys,time; t=time.time(); [os.utime(p,(t,t)) for p in sys.argv[1:]]' $@ $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_adc_ex.d
endif
endif
endif
endif

ifeq ($(CONFIG_HAL),y)
ifeq ($(CONFIG_HAL_PERIPH),y)
ifeq ($(CONFIG_HAL_PERIPH_SRC),y)
ifeq ($(CONFIG_HAL_PERIPH_SRC_STM32F4XX_HAL_CAN),y)
LOCAL_OBJS += $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_can.o
$(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_can.o: $(PROJECT_ROOT)/hal/periph/src/stm32f4xx_hal_can.c $(FLAG_DEPS)
	@mkdir -p $(dir $@)
	@echo "CC $<"
	$(CC) $(CFLAGS) -MMD -MP -MF $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_can.d -c $< -o $@
	@"$(PYTHON)" -c 'import os,sys,time; t=time.time(); [os.utime(p,(t,t)) for p in sys.argv[1:]]' $@ $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_can.d
endif
endif
endif
endif

ifeq ($(CONFIG_HAL),y)
ifeq ($(CONFIG_HAL_PERIPH),y)
ifeq ($(CONFIG_HAL_PERIPH_SRC),y)
ifeq ($(CONFIG_HAL_PERIPH_SRC_STM32F4XX_HAL_CEC),y)
LOCAL_OBJS += $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_cec.o
$(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_cec.o: $(PROJECT_ROOT)/hal/periph/src/stm32f4xx_hal_cec.c $(FLAG_DEPS)
	@mkdir -p $(dir $@)
	@echo "CC $<"
	$(CC) $(CFLAGS) -MMD -MP -MF $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_cec.d -c $< -o $@
	@"$(PYTHON)" -c 'import os,sys,time; t=time.time(); [os.utime(p,(t,t)) for p in sys.argv[1:]]' $@ $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_cec.d
endif
endif
endif
endif

ifeq ($(CONFIG_HAL),y)
ifeq ($(CONFIG_HAL_PERIPH),y)
ifeq ($(CONFIG_HAL_PERIPH_SRC),y)
ifeq ($(CONFIG_HAL_PERIPH_SRC_STM32F4XX_HAL_CORTEX),y)
LOCAL_OBJS += $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_cortex.o
$(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_cortex.o: $(PROJECT_ROOT)/hal/periph/src/stm32f4xx_hal_cortex.c $(FLAG_DEPS)
	@mkdir -p $(dir $@)
	@echo "CC $<"
	$(CC) $(CFLAGS) -MMD -MP -MF $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_cortex.d -c $< -o $@
	@"$(PYTHON)" -c 'import os,sys,time; t=time.time(); [os.utime(p,(t,t)) for p in sys.argv[1:]]' $@ $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_cortex.d
endif
endif
endif
endif

ifeq ($(CONFIG_HAL),y)
ifeq ($(CONFIG_HAL_PERIPH),y)
ifeq ($(CONFIG_HAL_PERIPH_SRC),y)
ifeq ($(CONFIG_HAL_PERIPH_SRC_STM32F4XX_HAL_CRC),y)
LOCAL_OBJS += $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_crc.o
$(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_crc.o: $(PROJECT_ROOT)/hal/periph/src/stm32f4xx_hal_crc.c $(FLAG_DEPS)
	@mkdir -p $(dir $@)
	@echo "CC $<"
	$(CC) $(CFLAGS) -MMD -MP -MF $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_crc.d -c $< -o $@
	@"$(PYTHON)" -c 'import os,sys,time; t=time.time(); [os.utime(p,(t,t)) for p in sys.argv[1:]]' $@ $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_crc.d
endif
endif
endif
endif

ifeq ($(CONFIG_HAL),y)
ifeq ($(CONFIG_HAL_PERIPH),y)
ifeq ($(CONFIG_HAL_PERIPH_SRC),y)
ifeq ($(CONFIG_HAL_PERIPH_SRC_STM32F4XX_HAL_CRYP),y)
LOCAL_OBJS += $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_cryp.o
$(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_cryp.o: $(PROJECT_ROOT)/hal/periph/src/stm32f4xx_hal_cryp.c $(FLAG_DEPS)
	@mkdir -p $(dir $@)
	@echo "CC $<"
	$(CC) $(CFLAGS) -MMD -MP -MF $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_cryp.d -c $< -o $@
	@"$(PYTHON)" -c 'import os,sys,time; t=time.time(); [os.utime(p,(t,t)) for p in sys.argv[1:]]' $@ $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_cryp.d
endif
endif
endif
endif

ifeq ($(CONFIG_HAL),y)
ifeq ($(CONFIG_HAL_PERIPH),y)
ifeq ($(CONFIG_HAL_PERIPH_SRC),y)
ifeq ($(CONFIG_HAL_PERIPH_SRC_STM32F4XX_HAL_CRYP_EX),y)
LOCAL_OBJS += $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_cryp_ex.o
$(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_cryp_ex.o: $(PROJECT_ROOT)/hal/periph/src/stm32f4xx_hal_cryp_ex.c $(FLAG_DEPS)
	@mkdir -p $(dir $@)
	@echo "CC $<"
	$(CC) $(CFLAGS) -MMD -MP -MF $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_cryp_ex.d -c $< -o $@
	@"$(PYTHON)" -c 'import os,sys,time; t=time.time(); [os.utime(p,(t,t)) for p in sys.argv[1:]]' $@ $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_cryp_ex.d
endif
endif
endif
endif

ifeq ($(CONFIG_HAL),y)
ifeq ($(CONFIG_HAL_PERIPH),y)
ifeq ($(CONFIG_HAL_PERIPH_SRC),y)
ifeq ($(CONFIG_HAL_PERIPH_SRC_STM32F4XX_HAL_DAC),y)
LOCAL_OBJS += $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_dac.o
$(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_dac.o: $(PROJECT_ROOT)/hal/periph/src/stm32f4xx_hal_dac.c $(FLAG_DEPS)
	@mkdir -p $(dir $@)
	@echo "CC $<"
	$(CC) $(CFLAGS) -MMD -MP -MF $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_dac.d -c $< -o $@
	@"$(PYTHON)" -c 'import os,sys,time; t=time.time(); [os.utime(p,(t,t)) for p in sys.argv[1:]]' $@ $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_dac.d
endif
endif
endif
endif

ifeq ($(CONFIG_HAL),y)
ifeq ($(CONFIG_HAL_PERIPH),y)
ifeq ($(CONFIG_HAL_PERIPH_SRC),y)
ifeq ($(CONFIG_HAL_PERIPH_SRC_STM32F4XX_HAL_DAC_EX),y)
LOCAL_OBJS += $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_dac_ex.o
$(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_dac_ex.o: $(PROJECT_ROOT)/hal/periph/src/stm32f4xx_hal_dac_ex.c $(FLAG_DEPS)
	@mkdir -p $(dir $@)
	@echo "CC $<"
	$(CC) $(CFLAGS) -MMD -MP -MF $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_dac_ex.d -c $< -o $@
	@"$(PYTHON)" -c 'import os,sys,time; t=time.time(); [os.utime(p,(t,t)) for p in sys.argv[1:]]' $@ $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_dac_ex.d
endif
endif
endif
endif

ifeq ($(CONFIG_HAL),y)
ifeq ($(CONFIG_HAL_PERIPH),y)
ifeq ($(CONFIG_HAL_PERIPH_SRC),y)
ifeq ($(CONFIG_HAL_PERIPH_SRC_STM32F4XX_HAL_DCMI),y)
LOCAL_OBJS += $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_dcmi.o
$(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_dcmi.o: $(PROJECT_ROOT)/hal/periph/src/stm32f4xx_hal_dcmi.c $(FLAG_DEPS)
	@mkdir -p $(dir $@)
	@echo "CC $<"
	$(CC) $(CFLAGS) -MMD -MP -MF $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_dcmi.d -c $< -o $@
	@"$(PYTHON)" -c 'import os,sys,time; t=time.time(); [os.utime(p,(t,t)) for p in sys.argv[1:]]' $@ $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_dcmi.d
endif
endif
endif
endif

ifeq ($(CONFIG_HAL),y)
ifeq ($(CONFIG_HAL_PERIPH),y)
ifeq ($(CONFIG_HAL_PERIPH_SRC),y)
ifeq ($(CONFIG_HAL_PERIPH_SRC_STM32F4XX_HAL_DCMI_EX),y)
LOCAL_OBJS += $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_dcmi_ex.o
$(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_dcmi_ex.o: $(PROJECT_ROOT)/hal/periph/src/stm32f4xx_hal_dcmi_ex.c $(FLAG_DEPS)
	@mkdir -p $(dir $@)
	@echo "CC $<"
	$(CC) $(CFLAGS) -MMD -MP -MF $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_dcmi_ex.d -c $< -o $@
	@"$(PYTHON)" -c 'import os,sys,time; t=time.time(); [os.utime(p,(t,t)) for p in sys.argv[1:]]' $@ $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_dcmi_ex.d
endif
endif
endif
endif

ifeq ($(CONFIG_HAL),y)
ifeq ($(CONFIG_HAL_PERIPH),y)
ifeq ($(CONFIG_HAL_PERIPH_SRC),y)
ifeq ($(CONFIG_HAL_PERIPH_SRC_STM32F4XX_HAL_DFSDM),y)
LOCAL_OBJS += $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_dfsdm.o
$(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_dfsdm.o: $(PROJECT_ROOT)/hal/periph/src/stm32f4xx_hal_dfsdm.c $(FLAG_DEPS)
	@mkdir -p $(dir $@)
	@echo "CC $<"
	$(CC) $(CFLAGS) -MMD -MP -MF $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_dfsdm.d -c $< -o $@
	@"$(PYTHON)" -c 'import os,sys,time; t=time.time(); [os.utime(p,(t,t)) for p in sys.argv[1:]]' $@ $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_dfsdm.d
endif
endif
endif
endif

ifeq ($(CONFIG_HAL),y)
ifeq ($(CONFIG_HAL_PERIPH),y)
ifeq ($(CONFIG_HAL_PERIPH_SRC),y)
ifeq ($(CONFIG_HAL_PERIPH_SRC_STM32F4XX_HAL_DMA),y)
LOCAL_OBJS += $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_dma.o
$(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_dma.o: $(PROJECT_ROOT)/hal/periph/src/stm32f4xx_hal_dma.c $(FLAG_DEPS)
	@mkdir -p $(dir $@)
	@echo "CC $<"
	$(CC) $(CFLAGS) -MMD -MP -MF $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_dma.d -c $< -o $@
	@"$(PYTHON)" -c 'import os,sys,time; t=time.time(); [os.utime(p,(t,t)) for p in sys.argv[1:]]' $@ $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_dma.d
endif
endif
endif
endif

ifeq ($(CONFIG_HAL),y)
ifeq ($(CONFIG_HAL_PERIPH),y)
ifeq ($(CONFIG_HAL_PERIPH_SRC),y)
ifeq ($(CONFIG_HAL_PERIPH_SRC_STM32F4XX_HAL_DMA2D),y)
LOCAL_OBJS += $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_dma2d.o
$(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_dma2d.o: $(PROJECT_ROOT)/hal/periph/src/stm32f4xx_hal_dma2d.c $(FLAG_DEPS)
	@mkdir -p $(dir $@)
	@echo "CC $<"
	$(CC) $(CFLAGS) -MMD -MP -MF $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_dma2d.d -c $< -o $@
	@"$(PYTHON)" -c 'import os,sys,time; t=time.time(); [os.utime(p,(t,t)) for p in sys.argv[1:]]' $@ $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_dma2d.d
endif
endif
endif
endif

ifeq ($(CONFIG_HAL),y)
ifeq ($(CONFIG_HAL_PERIPH),y)
ifeq ($(CONFIG_HAL_PERIPH_SRC),y)
ifeq ($(CONFIG_HAL_PERIPH_SRC_STM32F4XX_HAL_DMA_EX),y)
LOCAL_OBJS += $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_dma_ex.o
$(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_dma_ex.o: $(PROJECT_ROOT)/hal/periph/src/stm32f4xx_hal_dma_ex.c $(FLAG_DEPS)
	@mkdir -p $(dir $@)
	@echo "CC $<"
	$(CC) $(CFLAGS) -MMD -MP -MF $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_dma_ex.d -c $< -o $@
	@"$(PYTHON)" -c 'import os,sys,time; t=time.time(); [os.utime(p,(t,t)) for p in sys.argv[1:]]' $@ $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_dma_ex.d
endif
endif
endif
endif

ifeq ($(CONFIG_HAL),y)
ifeq ($(CONFIG_HAL_PERIPH),y)
ifeq ($(CONFIG_HAL_PERIPH_SRC),y)
ifeq ($(CONFIG_HAL_PERIPH_SRC_STM32F4XX_HAL_DSI),y)
LOCAL_OBJS += $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_dsi.o
$(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_dsi.o: $(PROJECT_ROOT)/hal/periph/src/stm32f4xx_hal_dsi.c $(FLAG_DEPS)
	@mkdir -p $(dir $@)
	@echo "CC $<"
	$(CC) $(CFLAGS) -MMD -MP -MF $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_dsi.d -c $< -o $@
	@"$(PYTHON)" -c 'import os,sys,time; t=time.time(); [os.utime(p,(t,t)) for p in sys.argv[1:]]' $@ $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_dsi.d
endif
endif
endif
endif

ifeq ($(CONFIG_HAL),y)
ifeq ($(CONFIG_HAL_PERIPH),y)
ifeq ($(CONFIG_HAL_PERIPH_SRC),y)
ifeq ($(CONFIG_HAL_PERIPH_SRC_STM32F4XX_HAL_ETH),y)
LOCAL_OBJS += $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_eth.o
$(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_eth.o: $(PROJECT_ROOT)/hal/periph/src/stm32f4xx_hal_eth.c $(FLAG_DEPS)
	@mkdir -p $(dir $@)
	@echo "CC $<"
	$(CC) $(CFLAGS) -MMD -MP -MF $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_eth.d -c $< -o $@
	@"$(PYTHON)" -c 'import os,sys,time; t=time.time(); [os.utime(p,(t,t)) for p in sys.argv[1:]]' $@ $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_eth.d
endif
endif
endif
endif

ifeq ($(CONFIG_HAL),y)
ifeq ($(CONFIG_HAL_PERIPH),y)
ifeq ($(CONFIG_HAL_PERIPH_SRC),y)
ifeq ($(CONFIG_HAL_PERIPH_SRC_STM32F4XX_HAL_EXTI),y)
LOCAL_OBJS += $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_exti.o
$(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_exti.o: $(PROJECT_ROOT)/hal/periph/src/stm32f4xx_hal_exti.c $(FLAG_DEPS)
	@mkdir -p $(dir $@)
	@echo "CC $<"
	$(CC) $(CFLAGS) -MMD -MP -MF $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_exti.d -c $< -o $@
	@"$(PYTHON)" -c 'import os,sys,time; t=time.time(); [os.utime(p,(t,t)) for p in sys.argv[1:]]' $@ $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_exti.d
endif
endif
endif
endif

ifeq ($(CONFIG_HAL),y)
ifeq ($(CONFIG_HAL_PERIPH),y)
ifeq ($(CONFIG_HAL_PERIPH_SRC),y)
ifeq ($(CONFIG_HAL_PERIPH_SRC_STM32F4XX_HAL_FLASH),y)
LOCAL_OBJS += $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_flash.o
$(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_flash.o: $(PROJECT_ROOT)/hal/periph/src/stm32f4xx_hal_flash.c $(FLAG_DEPS)
	@mkdir -p $(dir $@)
	@echo "CC $<"
	$(CC) $(CFLAGS) -MMD -MP -MF $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_flash.d -c $< -o $@
	@"$(PYTHON)" -c 'import os,sys,time; t=time.time(); [os.utime(p,(t,t)) for p in sys.argv[1:]]' $@ $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_flash.d
endif
endif
endif
endif

ifeq ($(CONFIG_HAL),y)
ifeq ($(CONFIG_HAL_PERIPH),y)
ifeq ($(CONFIG_HAL_PERIPH_SRC),y)
ifeq ($(CONFIG_HAL_PERIPH_SRC_STM32F4XX_HAL_FLASH_EX),y)
LOCAL_OBJS += $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_flash_ex.o
$(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_flash_ex.o: $(PROJECT_ROOT)/hal/periph/src/stm32f4xx_hal_flash_ex.c $(FLAG_DEPS)
	@mkdir -p $(dir $@)
	@echo "CC $<"
	$(CC) $(CFLAGS) -MMD -MP -MF $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_flash_ex.d -c $< -o $@
	@"$(PYTHON)" -c 'import os,sys,time; t=time.time(); [os.utime(p,(t,t)) for p in sys.argv[1:]]' $@ $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_flash_ex.d
endif
endif
endif
endif

ifeq ($(CONFIG_HAL),y)
ifeq ($(CONFIG_HAL_PERIPH),y)
ifeq ($(CONFIG_HAL_PERIPH_SRC),y)
ifeq ($(CONFIG_HAL_PERIPH_SRC_STM32F4XX_HAL_FLASH_RAMFUNC),y)
LOCAL_OBJS += $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_flash_ramfunc.o
$(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_flash_ramfunc.o: $(PROJECT_ROOT)/hal/periph/src/stm32f4xx_hal_flash_ramfunc.c $(FLAG_DEPS)
	@mkdir -p $(dir $@)
	@echo "CC $<"
	$(CC) $(CFLAGS) -MMD -MP -MF $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_flash_ramfunc.d -c $< -o $@
	@"$(PYTHON)" -c 'import os,sys,time; t=time.time(); [os.utime(p,(t,t)) for p in sys.argv[1:]]' $@ $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_flash_ramfunc.d
endif
endif
endif
endif

ifeq ($(CONFIG_HAL),y)
ifeq ($(CONFIG_HAL_PERIPH),y)
ifeq ($(CONFIG_HAL_PERIPH_SRC),y)
ifeq ($(CONFIG_HAL_PERIPH_SRC_STM32F4XX_HAL_FMPI2C),y)
LOCAL_OBJS += $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_fmpi2c.o
$(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_fmpi2c.o: $(PROJECT_ROOT)/hal/periph/src/stm32f4xx_hal_fmpi2c.c $(FLAG_DEPS)
	@mkdir -p $(dir $@)
	@echo "CC $<"
	$(CC) $(CFLAGS) -MMD -MP -MF $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_fmpi2c.d -c $< -o $@
	@"$(PYTHON)" -c 'import os,sys,time; t=time.time(); [os.utime(p,(t,t)) for p in sys.argv[1:]]' $@ $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_fmpi2c.d
endif
endif
endif
endif

ifeq ($(CONFIG_HAL),y)
ifeq ($(CONFIG_HAL_PERIPH),y)
ifeq ($(CONFIG_HAL_PERIPH_SRC),y)
ifeq ($(CONFIG_HAL_PERIPH_SRC_STM32F4XX_HAL_FMPI2C_EX),y)
LOCAL_OBJS += $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_fmpi2c_ex.o
$(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_fmpi2c_ex.o: $(PROJECT_ROOT)/hal/periph/src/stm32f4xx_hal_fmpi2c_ex.c $(FLAG_DEPS)
	@mkdir -p $(dir $@)
	@echo "CC $<"
	$(CC) $(CFLAGS) -MMD -MP -MF $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_fmpi2c_ex.d -c $< -o $@
	@"$(PYTHON)" -c 'import os,sys,time; t=time.time(); [os.utime(p,(t,t)) for p in sys.argv[1:]]' $@ $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_fmpi2c_ex.d
endif
endif
endif
endif

ifeq ($(CONFIG_HAL),y)
ifeq ($(CONFIG_HAL_PERIPH),y)
ifeq ($(CONFIG_HAL_PERIPH_SRC),y)
ifeq ($(CONFIG_HAL_PERIPH_SRC_STM32F4XX_HAL_FMPSMBUS),y)
LOCAL_OBJS += $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_fmpsmbus.o
$(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_fmpsmbus.o: $(PROJECT_ROOT)/hal/periph/src/stm32f4xx_hal_fmpsmbus.c $(FLAG_DEPS)
	@mkdir -p $(dir $@)
	@echo "CC $<"
	$(CC) $(CFLAGS) -MMD -MP -MF $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_fmpsmbus.d -c $< -o $@
	@"$(PYTHON)" -c 'import os,sys,time; t=time.time(); [os.utime(p,(t,t)) for p in sys.argv[1:]]' $@ $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_fmpsmbus.d
endif
endif
endif
endif

ifeq ($(CONFIG_HAL),y)
ifeq ($(CONFIG_HAL_PERIPH),y)
ifeq ($(CONFIG_HAL_PERIPH_SRC),y)
ifeq ($(CONFIG_HAL_PERIPH_SRC_STM32F4XX_HAL_FMPSMBUS_EX),y)
LOCAL_OBJS += $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_fmpsmbus_ex.o
$(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_fmpsmbus_ex.o: $(PROJECT_ROOT)/hal/periph/src/stm32f4xx_hal_fmpsmbus_ex.c $(FLAG_DEPS)
	@mkdir -p $(dir $@)
	@echo "CC $<"
	$(CC) $(CFLAGS) -MMD -MP -MF $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_fmpsmbus_ex.d -c $< -o $@
	@"$(PYTHON)" -c 'import os,sys,time; t=time.time(); [os.utime(p,(t,t)) for p in sys.argv[1:]]' $@ $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_fmpsmbus_ex.d
endif
endif
endif
endif

ifeq ($(CONFIG_HAL),y)
ifeq ($(CONFIG_HAL_PERIPH),y)
ifeq ($(CONFIG_HAL_PERIPH_SRC),y)
ifeq ($(CONFIG_HAL_PERIPH_SRC_STM32F4XX_HAL_GPIO),y)
LOCAL_OBJS += $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_gpio.o
$(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_gpio.o: $(PROJECT_ROOT)/hal/periph/src/stm32f4xx_hal_gpio.c $(FLAG_DEPS)
	@mkdir -p $(dir $@)
	@echo "CC $<"
	$(CC) $(CFLAGS) -MMD -MP -MF $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_gpio.d -c $< -o $@
	@"$(PYTHON)" -c 'import os,sys,time; t=time.time(); [os.utime(p,(t,t)) for p in sys.argv[1:]]' $@ $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_gpio.d
endif
endif
endif
endif

ifeq ($(CONFIG_HAL),y)
ifeq ($(CONFIG_HAL_PERIPH),y)
ifeq ($(CONFIG_HAL_PERIPH_SRC),y)
ifeq ($(CONFIG_HAL_PERIPH_SRC_STM32F4XX_HAL_HASH),y)
LOCAL_OBJS += $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_hash.o
$(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_hash.o: $(PROJECT_ROOT)/hal/periph/src/stm32f4xx_hal_hash.c $(FLAG_DEPS)
	@mkdir -p $(dir $@)
	@echo "CC $<"
	$(CC) $(CFLAGS) -MMD -MP -MF $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_hash.d -c $< -o $@
	@"$(PYTHON)" -c 'import os,sys,time; t=time.time(); [os.utime(p,(t,t)) for p in sys.argv[1:]]' $@ $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_hash.d
endif
endif
endif
endif

ifeq ($(CONFIG_HAL),y)
ifeq ($(CONFIG_HAL_PERIPH),y)
ifeq ($(CONFIG_HAL_PERIPH_SRC),y)
ifeq ($(CONFIG_HAL_PERIPH_SRC_STM32F4XX_HAL_HASH_EX),y)
LOCAL_OBJS += $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_hash_ex.o
$(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_hash_ex.o: $(PROJECT_ROOT)/hal/periph/src/stm32f4xx_hal_hash_ex.c $(FLAG_DEPS)
	@mkdir -p $(dir $@)
	@echo "CC $<"
	$(CC) $(CFLAGS) -MMD -MP -MF $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_hash_ex.d -c $< -o $@
	@"$(PYTHON)" -c 'import os,sys,time; t=time.time(); [os.utime(p,(t,t)) for p in sys.argv[1:]]' $@ $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_hash_ex.d
endif
endif
endif
endif

ifeq ($(CONFIG_HAL),y)
ifeq ($(CONFIG_HAL_PERIPH),y)
ifeq ($(CONFIG_HAL_PERIPH_SRC),y)
ifeq ($(CONFIG_HAL_PERIPH_SRC_STM32F4XX_HAL_HCD),y)
LOCAL_OBJS += $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_hcd.o
$(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_hcd.o: $(PROJECT_ROOT)/hal/periph/src/stm32f4xx_hal_hcd.c $(FLAG_DEPS)
	@mkdir -p $(dir $@)
	@echo "CC $<"
	$(CC) $(CFLAGS) -MMD -MP -MF $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_hcd.d -c $< -o $@
	@"$(PYTHON)" -c 'import os,sys,time; t=time.time(); [os.utime(p,(t,t)) for p in sys.argv[1:]]' $@ $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_hcd.d
endif
endif
endif
endif

ifeq ($(CONFIG_HAL),y)
ifeq ($(CONFIG_HAL_PERIPH),y)
ifeq ($(CONFIG_HAL_PERIPH_SRC),y)
ifeq ($(CONFIG_HAL_PERIPH_SRC_STM32F4XX_HAL_I2C),y)
LOCAL_OBJS += $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_i2c.o
$(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_i2c.o: $(PROJECT_ROOT)/hal/periph/src/stm32f4xx_hal_i2c.c $(FLAG_DEPS)
	@mkdir -p $(dir $@)
	@echo "CC $<"
	$(CC) $(CFLAGS) -MMD -MP -MF $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_i2c.d -c $< -o $@
	@"$(PYTHON)" -c 'import os,sys,time; t=time.time(); [os.utime(p,(t,t)) for p in sys.argv[1:]]' $@ $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_i2c.d
endif
endif
endif
endif

ifeq ($(CONFIG_HAL),y)
ifeq ($(CONFIG_HAL_PERIPH),y)
ifeq ($(CONFIG_HAL_PERIPH_SRC),y)
ifeq ($(CONFIG_HAL_PERIPH_SRC_STM32F4XX_HAL_I2C_EX),y)
LOCAL_OBJS += $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_i2c_ex.o
$(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_i2c_ex.o: $(PROJECT_ROOT)/hal/periph/src/stm32f4xx_hal_i2c_ex.c $(FLAG_DEPS)
	@mkdir -p $(dir $@)
	@echo "CC $<"
	$(CC) $(CFLAGS) -MMD -MP -MF $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_i2c_ex.d -c $< -o $@
	@"$(PYTHON)" -c 'import os,sys,time; t=time.time(); [os.utime(p,(t,t)) for p in sys.argv[1:]]' $@ $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_i2c_ex.d
endif
endif
endif
endif

ifeq ($(CONFIG_HAL),y)
ifeq ($(CONFIG_HAL_PERIPH),y)
ifeq ($(CONFIG_HAL_PERIPH_SRC),y)
ifeq ($(CONFIG_HAL_PERIPH_SRC_STM32F4XX_HAL_I2S),y)
LOCAL_OBJS += $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_i2s.o
$(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_i2s.o: $(PROJECT_ROOT)/hal/periph/src/stm32f4xx_hal_i2s.c $(FLAG_DEPS)
	@mkdir -p $(dir $@)
	@echo "CC $<"
	$(CC) $(CFLAGS) -MMD -MP -MF $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_i2s.d -c $< -o $@
	@"$(PYTHON)" -c 'import os,sys,time; t=time.time(); [os.utime(p,(t,t)) for p in sys.argv[1:]]' $@ $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_i2s.d
endif
endif
endif
endif

ifeq ($(CONFIG_HAL),y)
ifeq ($(CONFIG_HAL_PERIPH),y)
ifeq ($(CONFIG_HAL_PERIPH_SRC),y)
ifeq ($(CONFIG_HAL_PERIPH_SRC_STM32F4XX_HAL_I2S_EX),y)
LOCAL_OBJS += $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_i2s_ex.o
$(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_i2s_ex.o: $(PROJECT_ROOT)/hal/periph/src/stm32f4xx_hal_i2s_ex.c $(FLAG_DEPS)
	@mkdir -p $(dir $@)
	@echo "CC $<"
	$(CC) $(CFLAGS) -MMD -MP -MF $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_i2s_ex.d -c $< -o $@
	@"$(PYTHON)" -c 'import os,sys,time; t=time.time(); [os.utime(p,(t,t)) for p in sys.argv[1:]]' $@ $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_i2s_ex.d
endif
endif
endif
endif

ifeq ($(CONFIG_HAL),y)
ifeq ($(CONFIG_HAL_PERIPH),y)
ifeq ($(CONFIG_HAL_PERIPH_SRC),y)
ifeq ($(CONFIG_HAL_PERIPH_SRC_STM32F4XX_HAL_IRDA),y)
LOCAL_OBJS += $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_irda.o
$(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_irda.o: $(PROJECT_ROOT)/hal/periph/src/stm32f4xx_hal_irda.c $(FLAG_DEPS)
	@mkdir -p $(dir $@)
	@echo "CC $<"
	$(CC) $(CFLAGS) -MMD -MP -MF $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_irda.d -c $< -o $@
	@"$(PYTHON)" -c 'import os,sys,time; t=time.time(); [os.utime(p,(t,t)) for p in sys.argv[1:]]' $@ $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_irda.d
endif
endif
endif
endif

ifeq ($(CONFIG_HAL),y)
ifeq ($(CONFIG_HAL_PERIPH),y)
ifeq ($(CONFIG_HAL_PERIPH_SRC),y)
ifeq ($(CONFIG_HAL_PERIPH_SRC_STM32F4XX_HAL_IWDG),y)
LOCAL_OBJS += $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_iwdg.o
$(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_iwdg.o: $(PROJECT_ROOT)/hal/periph/src/stm32f4xx_hal_iwdg.c $(FLAG_DEPS)
	@mkdir -p $(dir $@)
	@echo "CC $<"
	$(CC) $(CFLAGS) -MMD -MP -MF $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_iwdg.d -c $< -o $@
	@"$(PYTHON)" -c 'import os,sys,time; t=time.time(); [os.utime(p,(t,t)) for p in sys.argv[1:]]' $@ $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_iwdg.d
endif
endif
endif
endif

ifeq ($(CONFIG_HAL),y)
ifeq ($(CONFIG_HAL_PERIPH),y)
ifeq ($(CONFIG_HAL_PERIPH_SRC),y)
ifeq ($(CONFIG_HAL_PERIPH_SRC_STM32F4XX_HAL_LPTIM),y)
LOCAL_OBJS += $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_lptim.o
$(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_lptim.o: $(PROJECT_ROOT)/hal/periph/src/stm32f4xx_hal_lptim.c $(FLAG_DEPS)
	@mkdir -p $(dir $@)
	@echo "CC $<"
	$(CC) $(CFLAGS) -MMD -MP -MF $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_lptim.d -c $< -o $@
	@"$(PYTHON)" -c 'import os,sys,time; t=time.time(); [os.utime(p,(t,t)) for p in sys.argv[1:]]' $@ $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_lptim.d
endif
endif
endif
endif

ifeq ($(CONFIG_HAL),y)
ifeq ($(CONFIG_HAL_PERIPH),y)
ifeq ($(CONFIG_HAL_PERIPH_SRC),y)
ifeq ($(CONFIG_HAL_PERIPH_SRC_STM32F4XX_HAL_LTDC),y)
LOCAL_OBJS += $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_ltdc.o
$(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_ltdc.o: $(PROJECT_ROOT)/hal/periph/src/stm32f4xx_hal_ltdc.c $(FLAG_DEPS)
	@mkdir -p $(dir $@)
	@echo "CC $<"
	$(CC) $(CFLAGS) -MMD -MP -MF $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_ltdc.d -c $< -o $@
	@"$(PYTHON)" -c 'import os,sys,time; t=time.time(); [os.utime(p,(t,t)) for p in sys.argv[1:]]' $@ $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_ltdc.d
endif
endif
endif
endif

ifeq ($(CONFIG_HAL),y)
ifeq ($(CONFIG_HAL_PERIPH),y)
ifeq ($(CONFIG_HAL_PERIPH_SRC),y)
ifeq ($(CONFIG_HAL_PERIPH_SRC_STM32F4XX_HAL_LTDC_EX),y)
LOCAL_OBJS += $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_ltdc_ex.o
$(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_ltdc_ex.o: $(PROJECT_ROOT)/hal/periph/src/stm32f4xx_hal_ltdc_ex.c $(FLAG_DEPS)
	@mkdir -p $(dir $@)
	@echo "CC $<"
	$(CC) $(CFLAGS) -MMD -MP -MF $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_ltdc_ex.d -c $< -o $@
	@"$(PYTHON)" -c 'import os,sys,time; t=time.time(); [os.utime(p,(t,t)) for p in sys.argv[1:]]' $@ $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_ltdc_ex.d
endif
endif
endif
endif

ifeq ($(CONFIG_HAL),y)
ifeq ($(CONFIG_HAL_PERIPH),y)
ifeq ($(CONFIG_HAL_PERIPH_SRC),y)
ifeq ($(CONFIG_HAL_PERIPH_SRC_STM32F4XX_HAL_MMC),y)
LOCAL_OBJS += $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_mmc.o
$(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_mmc.o: $(PROJECT_ROOT)/hal/periph/src/stm32f4xx_hal_mmc.c $(FLAG_DEPS)
	@mkdir -p $(dir $@)
	@echo "CC $<"
	$(CC) $(CFLAGS) -MMD -MP -MF $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_mmc.d -c $< -o $@
	@"$(PYTHON)" -c 'import os,sys,time; t=time.time(); [os.utime(p,(t,t)) for p in sys.argv[1:]]' $@ $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_mmc.d
endif
endif
endif
endif

ifeq ($(CONFIG_HAL),y)
ifeq ($(CONFIG_HAL_PERIPH),y)
ifeq ($(CONFIG_HAL_PERIPH_SRC),y)
ifeq ($(CONFIG_HAL_PERIPH_SRC_STM32F4XX_HAL_NAND),y)
LOCAL_OBJS += $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_nand.o
$(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_nand.o: $(PROJECT_ROOT)/hal/periph/src/stm32f4xx_hal_nand.c $(FLAG_DEPS)
	@mkdir -p $(dir $@)
	@echo "CC $<"
	$(CC) $(CFLAGS) -MMD -MP -MF $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_nand.d -c $< -o $@
	@"$(PYTHON)" -c 'import os,sys,time; t=time.time(); [os.utime(p,(t,t)) for p in sys.argv[1:]]' $@ $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_nand.d
endif
endif
endif
endif

ifeq ($(CONFIG_HAL),y)
ifeq ($(CONFIG_HAL_PERIPH),y)
ifeq ($(CONFIG_HAL_PERIPH_SRC),y)
ifeq ($(CONFIG_HAL_PERIPH_SRC_STM32F4XX_HAL_NOR),y)
LOCAL_OBJS += $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_nor.o
$(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_nor.o: $(PROJECT_ROOT)/hal/periph/src/stm32f4xx_hal_nor.c $(FLAG_DEPS)
	@mkdir -p $(dir $@)
	@echo "CC $<"
	$(CC) $(CFLAGS) -MMD -MP -MF $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_nor.d -c $< -o $@
	@"$(PYTHON)" -c 'import os,sys,time; t=time.time(); [os.utime(p,(t,t)) for p in sys.argv[1:]]' $@ $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_nor.d
endif
endif
endif
endif

ifeq ($(CONFIG_HAL),y)
ifeq ($(CONFIG_HAL_PERIPH),y)
ifeq ($(CONFIG_HAL_PERIPH_SRC),y)
ifeq ($(CONFIG_HAL_PERIPH_SRC_STM32F4XX_HAL_PCCARD),y)
LOCAL_OBJS += $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_pccard.o
$(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_pccard.o: $(PROJECT_ROOT)/hal/periph/src/stm32f4xx_hal_pccard.c $(FLAG_DEPS)
	@mkdir -p $(dir $@)
	@echo "CC $<"
	$(CC) $(CFLAGS) -MMD -MP -MF $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_pccard.d -c $< -o $@
	@"$(PYTHON)" -c 'import os,sys,time; t=time.time(); [os.utime(p,(t,t)) for p in sys.argv[1:]]' $@ $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_pccard.d
endif
endif
endif
endif

ifeq ($(CONFIG_HAL),y)
ifeq ($(CONFIG_HAL_PERIPH),y)
ifeq ($(CONFIG_HAL_PERIPH_SRC),y)
ifeq ($(CONFIG_HAL_PERIPH_SRC_STM32F4XX_HAL_PCD),y)
LOCAL_OBJS += $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_pcd.o
$(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_pcd.o: $(PROJECT_ROOT)/hal/periph/src/stm32f4xx_hal_pcd.c $(FLAG_DEPS)
	@mkdir -p $(dir $@)
	@echo "CC $<"
	$(CC) $(CFLAGS) -MMD -MP -MF $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_pcd.d -c $< -o $@
	@"$(PYTHON)" -c 'import os,sys,time; t=time.time(); [os.utime(p,(t,t)) for p in sys.argv[1:]]' $@ $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_pcd.d
endif
endif
endif
endif

ifeq ($(CONFIG_HAL),y)
ifeq ($(CONFIG_HAL_PERIPH),y)
ifeq ($(CONFIG_HAL_PERIPH_SRC),y)
ifeq ($(CONFIG_HAL_PERIPH_SRC_STM32F4XX_HAL_PCD_EX),y)
LOCAL_OBJS += $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_pcd_ex.o
$(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_pcd_ex.o: $(PROJECT_ROOT)/hal/periph/src/stm32f4xx_hal_pcd_ex.c $(FLAG_DEPS)
	@mkdir -p $(dir $@)
	@echo "CC $<"
	$(CC) $(CFLAGS) -MMD -MP -MF $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_pcd_ex.d -c $< -o $@
	@"$(PYTHON)" -c 'import os,sys,time; t=time.time(); [os.utime(p,(t,t)) for p in sys.argv[1:]]' $@ $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_pcd_ex.d
endif
endif
endif
endif

ifeq ($(CONFIG_HAL),y)
ifeq ($(CONFIG_HAL_PERIPH),y)
ifeq ($(CONFIG_HAL_PERIPH_SRC),y)
ifeq ($(CONFIG_HAL_PERIPH_SRC_STM32F4XX_HAL_PWR),y)
LOCAL_OBJS += $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_pwr.o
$(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_pwr.o: $(PROJECT_ROOT)/hal/periph/src/stm32f4xx_hal_pwr.c $(FLAG_DEPS)
	@mkdir -p $(dir $@)
	@echo "CC $<"
	$(CC) $(CFLAGS) -MMD -MP -MF $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_pwr.d -c $< -o $@
	@"$(PYTHON)" -c 'import os,sys,time; t=time.time(); [os.utime(p,(t,t)) for p in sys.argv[1:]]' $@ $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_pwr.d
endif
endif
endif
endif

ifeq ($(CONFIG_HAL),y)
ifeq ($(CONFIG_HAL_PERIPH),y)
ifeq ($(CONFIG_HAL_PERIPH_SRC),y)
ifeq ($(CONFIG_HAL_PERIPH_SRC_STM32F4XX_HAL_PWR_EX),y)
LOCAL_OBJS += $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_pwr_ex.o
$(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_pwr_ex.o: $(PROJECT_ROOT)/hal/periph/src/stm32f4xx_hal_pwr_ex.c $(FLAG_DEPS)
	@mkdir -p $(dir $@)
	@echo "CC $<"
	$(CC) $(CFLAGS) -MMD -MP -MF $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_pwr_ex.d -c $< -o $@
	@"$(PYTHON)" -c 'import os,sys,time; t=time.time(); [os.utime(p,(t,t)) for p in sys.argv[1:]]' $@ $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_pwr_ex.d
endif
endif
endif
endif

ifeq ($(CONFIG_HAL),y)
ifeq ($(CONFIG_HAL_PERIPH),y)
ifeq ($(CONFIG_HAL_PERIPH_SRC),y)
ifeq ($(CONFIG_HAL_PERIPH_SRC_STM32F4XX_HAL_QSPI),y)
LOCAL_OBJS += $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_qspi.o
$(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_qspi.o: $(PROJECT_ROOT)/hal/periph/src/stm32f4xx_hal_qspi.c $(FLAG_DEPS)
	@mkdir -p $(dir $@)
	@echo "CC $<"
	$(CC) $(CFLAGS) -MMD -MP -MF $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_qspi.d -c $< -o $@
	@"$(PYTHON)" -c 'import os,sys,time; t=time.time(); [os.utime(p,(t,t)) for p in sys.argv[1:]]' $@ $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_qspi.d
endif
endif
endif
endif

ifeq ($(CONFIG_HAL),y)
ifeq ($(CONFIG_HAL_PERIPH),y)
ifeq ($(CONFIG_HAL_PERIPH_SRC),y)
ifeq ($(CONFIG_HAL_PERIPH_SRC_STM32F4XX_HAL_RCC),y)
LOCAL_OBJS += $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_rcc.o
$(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_rcc.o: $(PROJECT_ROOT)/hal/periph/src/stm32f4xx_hal_rcc.c $(FLAG_DEPS)
	@mkdir -p $(dir $@)
	@echo "CC $<"
	$(CC) $(CFLAGS) -MMD -MP -MF $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_rcc.d -c $< -o $@
	@"$(PYTHON)" -c 'import os,sys,time; t=time.time(); [os.utime(p,(t,t)) for p in sys.argv[1:]]' $@ $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_rcc.d
endif
endif
endif
endif

ifeq ($(CONFIG_HAL),y)
ifeq ($(CONFIG_HAL_PERIPH),y)
ifeq ($(CONFIG_HAL_PERIPH_SRC),y)
ifeq ($(CONFIG_HAL_PERIPH_SRC_STM32F4XX_HAL_RCC_EX),y)
LOCAL_OBJS += $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_rcc_ex.o
$(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_rcc_ex.o: $(PROJECT_ROOT)/hal/periph/src/stm32f4xx_hal_rcc_ex.c $(FLAG_DEPS)
	@mkdir -p $(dir $@)
	@echo "CC $<"
	$(CC) $(CFLAGS) -MMD -MP -MF $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_rcc_ex.d -c $< -o $@
	@"$(PYTHON)" -c 'import os,sys,time; t=time.time(); [os.utime(p,(t,t)) for p in sys.argv[1:]]' $@ $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_rcc_ex.d
endif
endif
endif
endif

ifeq ($(CONFIG_HAL),y)
ifeq ($(CONFIG_HAL_PERIPH),y)
ifeq ($(CONFIG_HAL_PERIPH_SRC),y)
ifeq ($(CONFIG_HAL_PERIPH_SRC_STM32F4XX_HAL_RNG),y)
LOCAL_OBJS += $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_rng.o
$(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_rng.o: $(PROJECT_ROOT)/hal/periph/src/stm32f4xx_hal_rng.c $(FLAG_DEPS)
	@mkdir -p $(dir $@)
	@echo "CC $<"
	$(CC) $(CFLAGS) -MMD -MP -MF $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_rng.d -c $< -o $@
	@"$(PYTHON)" -c 'import os,sys,time; t=time.time(); [os.utime(p,(t,t)) for p in sys.argv[1:]]' $@ $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_rng.d
endif
endif
endif
endif

ifeq ($(CONFIG_HAL),y)
ifeq ($(CONFIG_HAL_PERIPH),y)
ifeq ($(CONFIG_HAL_PERIPH_SRC),y)
ifeq ($(CONFIG_HAL_PERIPH_SRC_STM32F4XX_HAL_RTC),y)
LOCAL_OBJS += $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_rtc.o
$(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_rtc.o: $(PROJECT_ROOT)/hal/periph/src/stm32f4xx_hal_rtc.c $(FLAG_DEPS)
	@mkdir -p $(dir $@)
	@echo "CC $<"
	$(CC) $(CFLAGS) -MMD -MP -MF $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_rtc.d -c $< -o $@
	@"$(PYTHON)" -c 'import os,sys,time; t=time.time(); [os.utime(p,(t,t)) for p in sys.argv[1:]]' $@ $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_rtc.d
endif
endif
endif
endif

ifeq ($(CONFIG_HAL),y)
ifeq ($(CONFIG_HAL_PERIPH),y)
ifeq ($(CONFIG_HAL_PERIPH_SRC),y)
ifeq ($(CONFIG_HAL_PERIPH_SRC_STM32F4XX_HAL_RTC_EX),y)
LOCAL_OBJS += $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_rtc_ex.o
$(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_rtc_ex.o: $(PROJECT_ROOT)/hal/periph/src/stm32f4xx_hal_rtc_ex.c $(FLAG_DEPS)
	@mkdir -p $(dir $@)
	@echo "CC $<"
	$(CC) $(CFLAGS) -MMD -MP -MF $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_rtc_ex.d -c $< -o $@
	@"$(PYTHON)" -c 'import os,sys,time; t=time.time(); [os.utime(p,(t,t)) for p in sys.argv[1:]]' $@ $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_rtc_ex.d
endif
endif
endif
endif

ifeq ($(CONFIG_HAL),y)
ifeq ($(CONFIG_HAL_PERIPH),y)
ifeq ($(CONFIG_HAL_PERIPH_SRC),y)
ifeq ($(CONFIG_HAL_PERIPH_SRC_STM32F4XX_HAL_SAI),y)
LOCAL_OBJS += $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_sai.o
$(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_sai.o: $(PROJECT_ROOT)/hal/periph/src/stm32f4xx_hal_sai.c $(FLAG_DEPS)
	@mkdir -p $(dir $@)
	@echo "CC $<"
	$(CC) $(CFLAGS) -MMD -MP -MF $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_sai.d -c $< -o $@
	@"$(PYTHON)" -c 'import os,sys,time; t=time.time(); [os.utime(p,(t,t)) for p in sys.argv[1:]]' $@ $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_sai.d
endif
endif
endif
endif

ifeq ($(CONFIG_HAL),y)
ifeq ($(CONFIG_HAL_PERIPH),y)
ifeq ($(CONFIG_HAL_PERIPH_SRC),y)
ifeq ($(CONFIG_HAL_PERIPH_SRC_STM32F4XX_HAL_SAI_EX),y)
LOCAL_OBJS += $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_sai_ex.o
$(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_sai_ex.o: $(PROJECT_ROOT)/hal/periph/src/stm32f4xx_hal_sai_ex.c $(FLAG_DEPS)
	@mkdir -p $(dir $@)
	@echo "CC $<"
	$(CC) $(CFLAGS) -MMD -MP -MF $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_sai_ex.d -c $< -o $@
	@"$(PYTHON)" -c 'import os,sys,time; t=time.time(); [os.utime(p,(t,t)) for p in sys.argv[1:]]' $@ $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_sai_ex.d
endif
endif
endif
endif

ifeq ($(CONFIG_HAL),y)
ifeq ($(CONFIG_HAL_PERIPH),y)
ifeq ($(CONFIG_HAL_PERIPH_SRC),y)
ifeq ($(CONFIG_HAL_PERIPH_SRC_STM32F4XX_HAL_SD),y)
LOCAL_OBJS += $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_sd.o
$(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_sd.o: $(PROJECT_ROOT)/hal/periph/src/stm32f4xx_hal_sd.c $(FLAG_DEPS)
	@mkdir -p $(dir $@)
	@echo "CC $<"
	$(CC) $(CFLAGS) -MMD -MP -MF $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_sd.d -c $< -o $@
	@"$(PYTHON)" -c 'import os,sys,time; t=time.time(); [os.utime(p,(t,t)) for p in sys.argv[1:]]' $@ $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_sd.d
endif
endif
endif
endif

ifeq ($(CONFIG_HAL),y)
ifeq ($(CONFIG_HAL_PERIPH),y)
ifeq ($(CONFIG_HAL_PERIPH_SRC),y)
ifeq ($(CONFIG_HAL_PERIPH_SRC_STM32F4XX_HAL_SDRAM),y)
LOCAL_OBJS += $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_sdram.o
$(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_sdram.o: $(PROJECT_ROOT)/hal/periph/src/stm32f4xx_hal_sdram.c $(FLAG_DEPS)
	@mkdir -p $(dir $@)
	@echo "CC $<"
	$(CC) $(CFLAGS) -MMD -MP -MF $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_sdram.d -c $< -o $@
	@"$(PYTHON)" -c 'import os,sys,time; t=time.time(); [os.utime(p,(t,t)) for p in sys.argv[1:]]' $@ $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_sdram.d
endif
endif
endif
endif

ifeq ($(CONFIG_HAL),y)
ifeq ($(CONFIG_HAL_PERIPH),y)
ifeq ($(CONFIG_HAL_PERIPH_SRC),y)
ifeq ($(CONFIG_HAL_PERIPH_SRC_STM32F4XX_HAL_SMARTCARD),y)
LOCAL_OBJS += $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_smartcard.o
$(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_smartcard.o: $(PROJECT_ROOT)/hal/periph/src/stm32f4xx_hal_smartcard.c $(FLAG_DEPS)
	@mkdir -p $(dir $@)
	@echo "CC $<"
	$(CC) $(CFLAGS) -MMD -MP -MF $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_smartcard.d -c $< -o $@
	@"$(PYTHON)" -c 'import os,sys,time; t=time.time(); [os.utime(p,(t,t)) for p in sys.argv[1:]]' $@ $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_smartcard.d
endif
endif
endif
endif

ifeq ($(CONFIG_HAL),y)
ifeq ($(CONFIG_HAL_PERIPH),y)
ifeq ($(CONFIG_HAL_PERIPH_SRC),y)
ifeq ($(CONFIG_HAL_PERIPH_SRC_STM32F4XX_HAL_SMBUS),y)
LOCAL_OBJS += $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_smbus.o
$(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_smbus.o: $(PROJECT_ROOT)/hal/periph/src/stm32f4xx_hal_smbus.c $(FLAG_DEPS)
	@mkdir -p $(dir $@)
	@echo "CC $<"
	$(CC) $(CFLAGS) -MMD -MP -MF $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_smbus.d -c $< -o $@
	@"$(PYTHON)" -c 'import os,sys,time; t=time.time(); [os.utime(p,(t,t)) for p in sys.argv[1:]]' $@ $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_smbus.d
endif
endif
endif
endif

ifeq ($(CONFIG_HAL),y)
ifeq ($(CONFIG_HAL_PERIPH),y)
ifeq ($(CONFIG_HAL_PERIPH_SRC),y)
ifeq ($(CONFIG_HAL_PERIPH_SRC_STM32F4XX_HAL_SPDIFRX),y)
LOCAL_OBJS += $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_spdifrx.o
$(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_spdifrx.o: $(PROJECT_ROOT)/hal/periph/src/stm32f4xx_hal_spdifrx.c $(FLAG_DEPS)
	@mkdir -p $(dir $@)
	@echo "CC $<"
	$(CC) $(CFLAGS) -MMD -MP -MF $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_spdifrx.d -c $< -o $@
	@"$(PYTHON)" -c 'import os,sys,time; t=time.time(); [os.utime(p,(t,t)) for p in sys.argv[1:]]' $@ $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_spdifrx.d
endif
endif
endif
endif

ifeq ($(CONFIG_HAL),y)
ifeq ($(CONFIG_HAL_PERIPH),y)
ifeq ($(CONFIG_HAL_PERIPH_SRC),y)
ifeq ($(CONFIG_HAL_PERIPH_SRC_STM32F4XX_HAL_SPI),y)
LOCAL_OBJS += $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_spi.o
$(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_spi.o: $(PROJECT_ROOT)/hal/periph/src/stm32f4xx_hal_spi.c $(FLAG_DEPS)
	@mkdir -p $(dir $@)
	@echo "CC $<"
	$(CC) $(CFLAGS) -MMD -MP -MF $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_spi.d -c $< -o $@
	@"$(PYTHON)" -c 'import os,sys,time; t=time.time(); [os.utime(p,(t,t)) for p in sys.argv[1:]]' $@ $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_spi.d
endif
endif
endif
endif

ifeq ($(CONFIG_HAL),y)
ifeq ($(CONFIG_HAL_PERIPH),y)
ifeq ($(CONFIG_HAL_PERIPH_SRC),y)
ifeq ($(CONFIG_HAL_PERIPH_SRC_STM32F4XX_HAL_SRAM),y)
LOCAL_OBJS += $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_sram.o
$(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_sram.o: $(PROJECT_ROOT)/hal/periph/src/stm32f4xx_hal_sram.c $(FLAG_DEPS)
	@mkdir -p $(dir $@)
	@echo "CC $<"
	$(CC) $(CFLAGS) -MMD -MP -MF $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_sram.d -c $< -o $@
	@"$(PYTHON)" -c 'import os,sys,time; t=time.time(); [os.utime(p,(t,t)) for p in sys.argv[1:]]' $@ $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_sram.d
endif
endif
endif
endif

ifeq ($(CONFIG_HAL),y)
ifeq ($(CONFIG_HAL_PERIPH),y)
ifeq ($(CONFIG_HAL_PERIPH_SRC),y)
ifeq ($(CONFIG_HAL_PERIPH_SRC_STM32F4XX_HAL_TIM),y)
LOCAL_OBJS += $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_tim.o
$(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_tim.o: $(PROJECT_ROOT)/hal/periph/src/stm32f4xx_hal_tim.c $(FLAG_DEPS)
	@mkdir -p $(dir $@)
	@echo "CC $<"
	$(CC) $(CFLAGS) -MMD -MP -MF $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_tim.d -c $< -o $@
	@"$(PYTHON)" -c 'import os,sys,time; t=time.time(); [os.utime(p,(t,t)) for p in sys.argv[1:]]' $@ $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_tim.d
endif
endif
endif
endif

ifeq ($(CONFIG_HAL),y)
ifeq ($(CONFIG_HAL_PERIPH),y)
ifeq ($(CONFIG_HAL_PERIPH_SRC),y)
ifeq ($(CONFIG_HAL_PERIPH_SRC_STM32F4XX_HAL_TIM_EX),y)
LOCAL_OBJS += $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_tim_ex.o
$(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_tim_ex.o: $(PROJECT_ROOT)/hal/periph/src/stm32f4xx_hal_tim_ex.c $(FLAG_DEPS)
	@mkdir -p $(dir $@)
	@echo "CC $<"
	$(CC) $(CFLAGS) -MMD -MP -MF $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_tim_ex.d -c $< -o $@
	@"$(PYTHON)" -c 'import os,sys,time; t=time.time(); [os.utime(p,(t,t)) for p in sys.argv[1:]]' $@ $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_tim_ex.d
endif
endif
endif
endif

ifeq ($(CONFIG_HAL),y)
ifeq ($(CONFIG_HAL_PERIPH),y)
ifeq ($(CONFIG_HAL_PERIPH_SRC),y)
ifeq ($(CONFIG_HAL_PERIPH_SRC_STM32F4XX_HAL_UART),y)
LOCAL_OBJS += $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_uart.o
$(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_uart.o: $(PROJECT_ROOT)/hal/periph/src/stm32f4xx_hal_uart.c $(FLAG_DEPS)
	@mkdir -p $(dir $@)
	@echo "CC $<"
	$(CC) $(CFLAGS) -MMD -MP -MF $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_uart.d -c $< -o $@
	@"$(PYTHON)" -c 'import os,sys,time; t=time.time(); [os.utime(p,(t,t)) for p in sys.argv[1:]]' $@ $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_uart.d
endif
endif
endif
endif

ifeq ($(CONFIG_HAL),y)
ifeq ($(CONFIG_HAL_PERIPH),y)
ifeq ($(CONFIG_HAL_PERIPH_SRC),y)
ifeq ($(CONFIG_HAL_PERIPH_SRC_STM32F4XX_HAL_USART),y)
LOCAL_OBJS += $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_usart.o
$(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_usart.o: $(PROJECT_ROOT)/hal/periph/src/stm32f4xx_hal_usart.c $(FLAG_DEPS)
	@mkdir -p $(dir $@)
	@echo "CC $<"
	$(CC) $(CFLAGS) -MMD -MP -MF $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_usart.d -c $< -o $@
	@"$(PYTHON)" -c 'import os,sys,time; t=time.time(); [os.utime(p,(t,t)) for p in sys.argv[1:]]' $@ $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_usart.d
endif
endif
endif
endif

ifeq ($(CONFIG_HAL),y)
ifeq ($(CONFIG_HAL_PERIPH),y)
ifeq ($(CONFIG_HAL_PERIPH_SRC),y)
ifeq ($(CONFIG_HAL_PERIPH_SRC_STM32F4XX_HAL_WWDG),y)
LOCAL_OBJS += $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_wwdg.o
$(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_wwdg.o: $(PROJECT_ROOT)/hal/periph/src/stm32f4xx_hal_wwdg.c $(FLAG_DEPS)
	@mkdir -p $(dir $@)
	@echo "CC $<"
	$(CC) $(CFLAGS) -MMD -MP -MF $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_wwdg.d -c $< -o $@
	@"$(PYTHON)" -c 'import os,sys,time; t=time.time(); [os.utime(p,(t,t)) for p in sys.argv[1:]]' $@ $(BUILD_DIR)/hal/periph/src/stm32f4xx_hal_wwdg.d
endif
endif
endif
endif

.PHONY: all clean

all: $(LOCAL_OBJS)

clean:
	rm -rf $(BUILD_DIR)/hal/periph/src
