# 自动生成，勿手改。刷新：python3 build/build.py update
PROJECT_ROOT := $(abspath $(dir $(lastword $(MAKEFILE_LIST)))/../../../..)
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
-include $(BUILD_DIR)/hal/chip/startup/src/startup_stm32f429xx.d
-include $(BUILD_DIR)/hal/chip/startup/src/system_stm32f4xx.d

ifeq ($(CONFIG_HAL),y)
ifeq ($(CONFIG_HAL_CHIP),y)
ifeq ($(CONFIG_HAL_CHIP_STARTUP),y)
ifeq ($(CONFIG_HAL_CHIP_STARTUP_SRC),y)
ifeq ($(CONFIG_HAL_CHIP_STARTUP_SRC_STARTUP_STM32F429XX),y)
LOCAL_OBJS += $(BUILD_DIR)/hal/chip/startup/src/startup_stm32f429xx.o
$(BUILD_DIR)/hal/chip/startup/src/startup_stm32f429xx.o: $(PROJECT_ROOT)/hal/chip/startup/src/startup_stm32f429xx.s $(FLAG_DEPS)
	@mkdir -p $(dir $@)
	@echo "AS $<"
	$(AS) $(ASFLAGS) -MMD -MP -MF $(BUILD_DIR)/hal/chip/startup/src/startup_stm32f429xx.d -c $< -o $@
	@"$(PYTHON)" -c 'import os,sys,time; t=time.time(); [os.utime(p,(t,t)) for p in sys.argv[1:]]' $@ $(BUILD_DIR)/hal/chip/startup/src/startup_stm32f429xx.d
endif
endif
endif
endif
endif

ifeq ($(CONFIG_HAL),y)
ifeq ($(CONFIG_HAL_CHIP),y)
ifeq ($(CONFIG_HAL_CHIP_STARTUP),y)
ifeq ($(CONFIG_HAL_CHIP_STARTUP_SRC),y)
ifeq ($(CONFIG_HAL_CHIP_STARTUP_SRC_SYSTEM_STM32F4XX),y)
LOCAL_OBJS += $(BUILD_DIR)/hal/chip/startup/src/system_stm32f4xx.o
$(BUILD_DIR)/hal/chip/startup/src/system_stm32f4xx.o: $(PROJECT_ROOT)/hal/chip/startup/src/system_stm32f4xx.c $(FLAG_DEPS)
	@mkdir -p $(dir $@)
	@echo "CC $<"
	$(CC) $(CFLAGS) -MMD -MP -MF $(BUILD_DIR)/hal/chip/startup/src/system_stm32f4xx.d -c $< -o $@
	@"$(PYTHON)" -c 'import os,sys,time; t=time.time(); [os.utime(p,(t,t)) for p in sys.argv[1:]]' $@ $(BUILD_DIR)/hal/chip/startup/src/system_stm32f4xx.d
endif
endif
endif
endif
endif

.PHONY: all clean

all: $(LOCAL_OBJS)

clean:
	rm -rf $(BUILD_DIR)/hal/chip/startup/src
