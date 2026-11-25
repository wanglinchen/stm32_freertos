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
INCLUDE_DIRS += $(PROJECT_ROOT)/bsl/inc
INCLUDES := $(addprefix -I,$(INCLUDE_DIRS))

FLAG_DEPS := $(wildcard $(PROJECT_ROOT)/toolchain.make) $(wildcard $(PROJECT_ROOT)/out/$(BUILD_VARIANT)/.config)

LOCAL_OBJS :=
ARCHIVE_OBJS :=
ifeq ($(CONFIG_BSL),y)
ifeq ($(CONFIG_BSL_SRC),y)
ifeq ($(CONFIG_BSL_SRC_BOARD_AP3216C),y)
ARCHIVE_OBJS += $(BUILD_DIR)/bsl/src/board_ap3216c.o
endif
endif
endif
ifeq ($(CONFIG_BSL),y)
ifeq ($(CONFIG_BSL_SRC),y)
ifeq ($(CONFIG_BSL_SRC_BOARD_BEEP),y)
ARCHIVE_OBJS += $(BUILD_DIR)/bsl/src/board_beep.o
endif
endif
endif
ifeq ($(CONFIG_BSL),y)
ifeq ($(CONFIG_BSL_SRC),y)
ifeq ($(CONFIG_BSL_SRC_BOARD_DEBUG_UART),y)
ARCHIVE_OBJS += $(BUILD_DIR)/bsl/src/board_debug_uart.o
endif
endif
endif
ifeq ($(CONFIG_BSL),y)
ifeq ($(CONFIG_BSL_SRC),y)
ifeq ($(CONFIG_BSL_SRC_BOARD_DTB),y)
ARCHIVE_OBJS += $(BUILD_DIR)/bsl/src/board_dtb.o
endif
endif
endif
ifeq ($(CONFIG_BSL),y)
ifeq ($(CONFIG_BSL_SRC),y)
ifeq ($(CONFIG_BSL_SRC_BOARD_EEPROM),y)
ARCHIVE_OBJS += $(BUILD_DIR)/bsl/src/board_eeprom.o
endif
endif
endif
ifeq ($(CONFIG_BSL),y)
ifeq ($(CONFIG_BSL_SRC),y)
ifeq ($(CONFIG_BSL_SRC_BOARD_KEY),y)
ARCHIVE_OBJS += $(BUILD_DIR)/bsl/src/board_key.o
endif
endif
endif
ifeq ($(CONFIG_BSL),y)
ifeq ($(CONFIG_BSL_SRC),y)
ifeq ($(CONFIG_BSL_SRC_BOARD_LCD),y)
ARCHIVE_OBJS += $(BUILD_DIR)/bsl/src/board_lcd.o
endif
endif
endif
ifeq ($(CONFIG_BSL),y)
ifeq ($(CONFIG_BSL_SRC),y)
ifeq ($(CONFIG_BSL_SRC_BOARD_LED),y)
ARCHIVE_OBJS += $(BUILD_DIR)/bsl/src/board_led.o
endif
endif
endif
ifeq ($(CONFIG_BSL),y)
ifeq ($(CONFIG_BSL_SRC),y)
ifeq ($(CONFIG_BSL_SRC_BOARD_MPU9250),y)
ARCHIVE_OBJS += $(BUILD_DIR)/bsl/src/board_mpu9250.o
endif
endif
endif
ifeq ($(CONFIG_BSL),y)
ifeq ($(CONFIG_BSL_SRC),y)
ifeq ($(CONFIG_BSL_SRC_BOARD_NAND),y)
ARCHIVE_OBJS += $(BUILD_DIR)/bsl/src/board_nand.o
endif
endif
endif
ifeq ($(CONFIG_BSL),y)
ifeq ($(CONFIG_BSL_SRC),y)
ifeq ($(CONFIG_BSL_SRC_BOARD_NOR),y)
ARCHIVE_OBJS += $(BUILD_DIR)/bsl/src/board_nor.o
endif
endif
endif
ifeq ($(CONFIG_BSL),y)
ifeq ($(CONFIG_BSL_SRC),y)
ifeq ($(CONFIG_BSL_SRC_BOARD_NT35510),y)
ARCHIVE_OBJS += $(BUILD_DIR)/bsl/src/board_nt35510.o
endif
endif
endif
ifeq ($(CONFIG_BSL),y)
ifeq ($(CONFIG_BSL_SRC),y)
ifeq ($(CONFIG_BSL_SRC_BOARD_POST),y)
ARCHIVE_OBJS += $(BUILD_DIR)/bsl/src/board_post.o
endif
endif
endif
ifeq ($(CONFIG_BSL),y)
ifeq ($(CONFIG_BSL_SRC),y)
ifeq ($(CONFIG_BSL_SRC_BOARD_RT),y)
ARCHIVE_OBJS += $(BUILD_DIR)/bsl/src/board_rt.o
endif
endif
endif
ifeq ($(CONFIG_BSL),y)
ifeq ($(CONFIG_BSL_SRC),y)
ifeq ($(CONFIG_BSL_SRC_BOARD_SDRAM),y)
ARCHIVE_OBJS += $(BUILD_DIR)/bsl/src/board_sdram.o
endif
endif
endif
ifeq ($(CONFIG_BSL),y)
ifeq ($(CONFIG_BSL_SRC),y)
ifeq ($(CONFIG_BSL_SRC_BOARD_SYSTEM),y)
ARCHIVE_OBJS += $(BUILD_DIR)/bsl/src/board_system.o
endif
endif
endif
ifeq ($(CONFIG_BSL),y)
ifeq ($(CONFIG_BSL_SRC),y)
ifeq ($(CONFIG_BSL_SRC_BOARD_TOUCH),y)
ARCHIVE_OBJS += $(BUILD_DIR)/bsl/src/board_touch.o
endif
endif
endif
ifeq ($(CONFIG_BSL),y)
ifeq ($(CONFIG_BSL_SRC),y)
ifeq ($(CONFIG_BSL_SRC_BOARD_TPAD),y)
ARCHIVE_OBJS += $(BUILD_DIR)/bsl/src/board_tpad.o
endif
endif
endif
ifeq ($(CONFIG_BSL),y)
ifeq ($(CONFIG_BSL_SRC),y)
ifeq ($(CONFIG_BSL_SRC_BOARD_WM8978),y)
ARCHIVE_OBJS += $(BUILD_DIR)/bsl/src/board_wm8978.o
endif
endif
endif
ifeq ($(CONFIG_BSL),y)
ifeq ($(CONFIG_BSL_SRC),y)
ifeq ($(CONFIG_BSL_SRC_BSL_ADC),y)
ARCHIVE_OBJS += $(BUILD_DIR)/bsl/src/bsl_adc.o
endif
endif
endif
ifeq ($(CONFIG_BSL),y)
ifeq ($(CONFIG_BSL_SRC),y)
ifeq ($(CONFIG_BSL_SRC_BSL_CAN),y)
ARCHIVE_OBJS += $(BUILD_DIR)/bsl/src/bsl_can.o
endif
endif
endif
ifeq ($(CONFIG_BSL),y)
ifeq ($(CONFIG_BSL_SRC),y)
ifeq ($(CONFIG_BSL_SRC_BSL_CRC),y)
ARCHIVE_OBJS += $(BUILD_DIR)/bsl/src/bsl_crc.o
endif
endif
endif
ifeq ($(CONFIG_BSL),y)
ifeq ($(CONFIG_BSL_SRC),y)
ifeq ($(CONFIG_BSL_SRC_BSL_DAC),y)
ARCHIVE_OBJS += $(BUILD_DIR)/bsl/src/bsl_dac.o
endif
endif
endif
ifeq ($(CONFIG_BSL),y)
ifeq ($(CONFIG_BSL_SRC),y)
ifeq ($(CONFIG_BSL_SRC_BSL_DEVICE),y)
ARCHIVE_OBJS += $(BUILD_DIR)/bsl/src/bsl_device.o
endif
endif
endif
ifeq ($(CONFIG_BSL),y)
ifeq ($(CONFIG_BSL_SRC),y)
ifeq ($(CONFIG_BSL_SRC_BSL_DMA),y)
ARCHIVE_OBJS += $(BUILD_DIR)/bsl/src/bsl_dma.o
endif
endif
endif
ifeq ($(CONFIG_BSL),y)
ifeq ($(CONFIG_BSL_SRC),y)
ifeq ($(CONFIG_BSL_SRC_BSL_EXTI),y)
ARCHIVE_OBJS += $(BUILD_DIR)/bsl/src/bsl_exti.o
endif
endif
endif
ifeq ($(CONFIG_BSL),y)
ifeq ($(CONFIG_BSL_SRC),y)
ifeq ($(CONFIG_BSL_SRC_BSL_FB),y)
ARCHIVE_OBJS += $(BUILD_DIR)/bsl/src/bsl_fb.o
endif
endif
endif
ifeq ($(CONFIG_BSL),y)
ifeq ($(CONFIG_BSL_SRC),y)
ifeq ($(CONFIG_BSL_SRC_BSL_FDT),y)
ARCHIVE_OBJS += $(BUILD_DIR)/bsl/src/bsl_fdt.o
endif
endif
endif
ifeq ($(CONFIG_BSL),y)
ifeq ($(CONFIG_BSL_SRC),y)
ifeq ($(CONFIG_BSL_SRC_BSL_GPIO),y)
ARCHIVE_OBJS += $(BUILD_DIR)/bsl/src/bsl_gpio.o
endif
endif
endif
ifeq ($(CONFIG_BSL),y)
ifeq ($(CONFIG_BSL_SRC),y)
ifeq ($(CONFIG_BSL_SRC_BSL_I2C_GPIO),y)
ARCHIVE_OBJS += $(BUILD_DIR)/bsl/src/bsl_i2c_gpio.o
endif
endif
endif
ifeq ($(CONFIG_BSL),y)
ifeq ($(CONFIG_BSL_SRC),y)
ifeq ($(CONFIG_BSL_SRC_BSL_INIT),y)
ARCHIVE_OBJS += $(BUILD_DIR)/bsl/src/bsl_init.o
endif
endif
endif
ifeq ($(CONFIG_BSL),y)
ifeq ($(CONFIG_BSL_SRC),y)
ifeq ($(CONFIG_BSL_SRC_BSL_LCD_8080),y)
ARCHIVE_OBJS += $(BUILD_DIR)/bsl/src/bsl_lcd_8080.o
endif
endif
endif
ifeq ($(CONFIG_BSL),y)
ifeq ($(CONFIG_BSL_SRC),y)
ifeq ($(CONFIG_BSL_SRC_BSL_LOG),y)
ARCHIVE_OBJS += $(BUILD_DIR)/bsl/src/bsl_log.o
endif
endif
endif
ifeq ($(CONFIG_BSL),y)
ifeq ($(CONFIG_BSL_SRC),y)
ifeq ($(CONFIG_BSL_SRC_BSL_PCF8574),y)
ARCHIVE_OBJS += $(BUILD_DIR)/bsl/src/bsl_pcf8574.o
endif
endif
endif
ifeq ($(CONFIG_BSL),y)
ifeq ($(CONFIG_BSL_SRC),y)
ifeq ($(CONFIG_BSL_SRC_BSL_PWM),y)
ARCHIVE_OBJS += $(BUILD_DIR)/bsl/src/bsl_pwm.o
endif
endif
endif
ifeq ($(CONFIG_BSL),y)
ifeq ($(CONFIG_BSL_SRC),y)
ifeq ($(CONFIG_BSL_SRC_BSL_RNG),y)
ARCHIVE_OBJS += $(BUILD_DIR)/bsl/src/bsl_rng.o
endif
endif
endif
ifeq ($(CONFIG_BSL),y)
ifeq ($(CONFIG_BSL_SRC),y)
ifeq ($(CONFIG_BSL_SRC_BSL_RTC),y)
ARCHIVE_OBJS += $(BUILD_DIR)/bsl/src/bsl_rtc.o
endif
endif
endif
ifeq ($(CONFIG_BSL),y)
ifeq ($(CONFIG_BSL_SRC),y)
ifeq ($(CONFIG_BSL_SRC_BSL_SAI),y)
ARCHIVE_OBJS += $(BUILD_DIR)/bsl/src/bsl_sai.o
endif
endif
endif
ifeq ($(CONFIG_BSL),y)
ifeq ($(CONFIG_BSL_SRC),y)
ifeq ($(CONFIG_BSL_SRC_BSL_SPI),y)
ARCHIVE_OBJS += $(BUILD_DIR)/bsl/src/bsl_spi.o
endif
endif
endif
ifeq ($(CONFIG_BSL),y)
ifeq ($(CONFIG_BSL_SRC),y)
ifeq ($(CONFIG_BSL_SRC_BSL_UART),y)
ARCHIVE_OBJS += $(BUILD_DIR)/bsl/src/bsl_uart.o
endif
endif
endif

.PHONY: all clean subdirs

LIB := $(TARGET_DIR)/lib/libbsl.a

all: subdirs
	@$(MAKE) --no-print-directory $(LIB)

subdirs:
	@$(MAKE) --no-print-directory -C $(PROJECT_ROOT)/bsl/src all

$(LIB): $(ARCHIVE_OBJS)
	@mkdir -p $(dir $@)
	@echo "AR $@"
	@rm -f $@
	$(AR) rcs $@ $(ARCHIVE_OBJS)

clean:
	rm -rf $(BUILD_DIR)/bsl $(TARGET_DIR)/lib/libbsl.a
