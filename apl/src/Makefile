# 自动生成，勿手改。刷新：python3 build/build.py update
PROJECT_ROOT := $(abspath $(dir $(lastword $(MAKEFILE_LIST)))/../..)
BUILD_DIR := $(PROJECT_ROOT)/out/object
TARGET_DIR := $(BUILD_DIR)/target
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
INCLUDE_DIRS += $(PROJECT_ROOT)/fml/inc
INCLUDE_DIRS += $(PROJECT_ROOT)/bsl/inc
INCLUDES := $(addprefix -I,$(INCLUDE_DIRS))

FLAG_DEPS := $(wildcard $(PROJECT_ROOT)/toolchain.make) $(wildcard $(PROJECT_ROOT)/out/.config)

LOCAL_OBJS :=
-include $(BUILD_DIR)/apl/src/main.d

ifeq ($(CONFIG_APL),y)
ifeq ($(CONFIG_APL_SRC),y)
ifeq ($(CONFIG_APL_SRC_MAIN),y)
LOCAL_OBJS += $(BUILD_DIR)/apl/src/main.o
$(BUILD_DIR)/apl/src/main.o: $(PROJECT_ROOT)/apl/src/main.c $(FLAG_DEPS)
	@mkdir -p $(dir $@)
	@echo "CC $<"
	$(CC) $(CFLAGS) -MMD -MP -MF $(BUILD_DIR)/apl/src/main.d -c $< -o $@
	@"$(PYTHON)" -c 'import os,sys,time; t=time.time(); [os.utime(p,(t,t)) for p in sys.argv[1:]]' $@ $(BUILD_DIR)/apl/src/main.d
endif
endif
endif

.PHONY: all clean

all: $(LOCAL_OBJS)

clean:
	rm -rf $(BUILD_DIR)/apl/src
