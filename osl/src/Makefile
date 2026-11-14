# 自动生成，勿手改。刷新：python3 build/build.py update
PROJECT_ROOT := $(abspath $(dir $(lastword $(MAKEFILE_LIST)))/../..)
BUILD_VARIANT ?= userdebug
BUILD_DIR := $(PROJECT_ROOT)/out/$(BUILD_VARIANT)/objects
TARGET_DIR := $(BUILD_DIR)/target
.DEFAULT_GOAL := all
include $(PROJECT_ROOT)/toolchain.make
-include $(PROJECT_ROOT)/out/$(BUILD_VARIANT)/.config

INCLUDE_DIRS :=
INCLUDE_DIRS += $(PROJECT_ROOT)/osl/inc
INCLUDE_DIRS += $(PROJECT_ROOT)/osl/inc/freertos
INCLUDE_DIRS += $(PROJECT_ROOT)/osl/inc/freertos/portable/GCC/ARM_CM4F
INCLUDE_DIRS += $(PROJECT_ROOT)/hal/arch/inc
INCLUDE_DIRS += $(PROJECT_ROOT)/hal/chip/register/inc
INCLUDE_DIRS += $(PROJECT_ROOT)/hal/chip/startup/inc
INCLUDE_DIRS += $(PROJECT_ROOT)/hal/periph/inc
INCLUDES := $(addprefix -I,$(INCLUDE_DIRS))

FLAG_DEPS := $(wildcard $(PROJECT_ROOT)/toolchain.make) $(wildcard $(PROJECT_ROOT)/out/$(BUILD_VARIANT)/.config)

LOCAL_OBJS :=
-include $(BUILD_DIR)/osl/src/osl_boot.d
-include $(BUILD_DIR)/osl/src/osl_health.d
-include $(BUILD_DIR)/osl/src/osl_panic.d

ifeq ($(CONFIG_OSL),y)
ifeq ($(CONFIG_OSL_SRC),y)
LOCAL_OBJS += $(BUILD_DIR)/osl/src/osl_boot.o
$(BUILD_DIR)/osl/src/osl_boot.o: $(PROJECT_ROOT)/osl/src/osl_boot.c $(FLAG_DEPS)
	@mkdir -p $(dir $@)
	@echo "CC $<"
	$(CC) $(CFLAGS) -MMD -MP -MF $(BUILD_DIR)/osl/src/osl_boot.d -c $< -o $@
	@"$(PYTHON)" -c 'import os,sys,time; t=time.time(); [os.utime(p,(t,t)) for p in sys.argv[1:]]' $@ $(BUILD_DIR)/osl/src/osl_boot.d
endif
endif

ifeq ($(CONFIG_OSL),y)
ifeq ($(CONFIG_OSL_SRC),y)
LOCAL_OBJS += $(BUILD_DIR)/osl/src/osl_health.o
$(BUILD_DIR)/osl/src/osl_health.o: $(PROJECT_ROOT)/osl/src/osl_health.c $(FLAG_DEPS)
	@mkdir -p $(dir $@)
	@echo "CC $<"
	$(CC) $(CFLAGS) -MMD -MP -MF $(BUILD_DIR)/osl/src/osl_health.d -c $< -o $@
	@"$(PYTHON)" -c 'import os,sys,time; t=time.time(); [os.utime(p,(t,t)) for p in sys.argv[1:]]' $@ $(BUILD_DIR)/osl/src/osl_health.d
endif
endif

ifeq ($(CONFIG_OSL),y)
ifeq ($(CONFIG_OSL_SRC),y)
LOCAL_OBJS += $(BUILD_DIR)/osl/src/osl_panic.o
$(BUILD_DIR)/osl/src/osl_panic.o: $(PROJECT_ROOT)/osl/src/osl_panic.c $(FLAG_DEPS)
	@mkdir -p $(dir $@)
	@echo "CC $<"
	$(CC) $(CFLAGS) -MMD -MP -MF $(BUILD_DIR)/osl/src/osl_panic.d -c $< -o $@
	@"$(PYTHON)" -c 'import os,sys,time; t=time.time(); [os.utime(p,(t,t)) for p in sys.argv[1:]]' $@ $(BUILD_DIR)/osl/src/osl_panic.d
endif
endif

.PHONY: all clean subdirs

all: $(LOCAL_OBJS)
	@$(MAKE) --no-print-directory -C $(PROJECT_ROOT)/osl/src/freertos all

clean:
	rm -rf $(BUILD_DIR)/osl/src
