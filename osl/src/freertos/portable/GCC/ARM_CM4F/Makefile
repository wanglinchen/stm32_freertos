# 自动生成，勿手改。刷新：python3 build/create-project.py update
PROJECT_ROOT := $(abspath $(dir $(lastword $(MAKEFILE_LIST)))/../../../../../..)
BUILD_DIR := $(PROJECT_ROOT)/out/object
.DEFAULT_GOAL := all
include $(PROJECT_ROOT)/toolchain.make
-include $(PROJECT_ROOT)/out/.config

INCLUDE_DIRS :=
INCLUDE_DIRS += $(PROJECT_ROOT)/osl/inc
INCLUDE_DIRS += $(PROJECT_ROOT)/osl/inc/freertos
INCLUDE_DIRS += $(PROJECT_ROOT)/osl/inc/freertos/portable/GCC/ARM_CM4F
INCLUDE_DIRS += $(PROJECT_ROOT)/hal/arch/inc
INCLUDE_DIRS += $(PROJECT_ROOT)/hal/chip/register/inc
INCLUDE_DIRS += $(PROJECT_ROOT)/hal/chip/startup/inc
INCLUDE_DIRS += $(PROJECT_ROOT)/hal/periph/inc
INCLUDES := $(addprefix -I,$(INCLUDE_DIRS))

FLAG_DEPS := $(wildcard $(PROJECT_ROOT)/toolchain.make) $(wildcard $(PROJECT_ROOT)/out/.config)

LOCAL_OBJS :=
-include $(BUILD_DIR)/osl/src/freertos/portable/GCC/ARM_CM4F/port.d

ifeq ($(CONFIG_OSL),y)
ifeq ($(CONFIG_OSL_SRC),y)
ifeq ($(CONFIG_OSL_SRC_FREERTOS),y)
ifeq ($(CONFIG_OSL_SRC_FREERTOS_PORTABLE),y)
ifeq ($(CONFIG_OSL_SRC_FREERTOS_PORTABLE_GCC),y)
ifeq ($(CONFIG_OSL_SRC_FREERTOS_PORTABLE_GCC_ARM_CM4F),y)
ifeq ($(CONFIG_OSL_SRC_FREERTOS_PORTABLE_GCC_ARM_CM4F_PORT),y)
LOCAL_OBJS += $(BUILD_DIR)/osl/src/freertos/portable/GCC/ARM_CM4F/port.o
$(BUILD_DIR)/osl/src/freertos/portable/GCC/ARM_CM4F/port.o: $(PROJECT_ROOT)/osl/src/freertos/portable/GCC/ARM_CM4F/port.c $(FLAG_DEPS)
	@mkdir -p $(dir $@)
	@echo "CC $<"
	$(CC) $(CFLAGS) -MMD -MP -MF $(BUILD_DIR)/osl/src/freertos/portable/GCC/ARM_CM4F/port.d -c $< -o $@
	@$(PYTHON) -c 'import os,sys,time; t=time.time(); [os.utime(p,(t,t)) for p in sys.argv[1:]]' $@ $(BUILD_DIR)/osl/src/freertos/portable/GCC/ARM_CM4F/port.d
endif
endif
endif
endif
endif
endif
endif

.PHONY: all clean
all: $(LOCAL_OBJS)

clean:
	rm -rf $(BUILD_DIR)/osl/src/freertos/portable/GCC/ARM_CM4F
