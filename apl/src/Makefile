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
INCLUDE_DIRS += $(PROJECT_ROOT)/fml/inc
INCLUDE_DIRS += $(PROJECT_ROOT)/fml/src/fatfs
INCLUDE_DIRS += $(PROJECT_ROOT)/bsl/inc
INCLUDE_DIRS += $(PROJECT_ROOT)/apl/inc
INCLUDES := $(addprefix -I,$(INCLUDE_DIRS))

FLAG_DEPS := $(wildcard $(PROJECT_ROOT)/toolchain.make) $(wildcard $(PROJECT_ROOT)/out/$(BUILD_VARIANT)/.config)

LOCAL_OBJS :=
-include $(BUILD_DIR)/apl/src/app.d
-include $(BUILD_DIR)/apl/src/app_beep.d
-include $(BUILD_DIR)/apl/src/app_key.d
-include $(BUILD_DIR)/apl/src/app_led.d
-include $(BUILD_DIR)/apl/src/app_sample.d
-include $(BUILD_DIR)/apl/src/main.d

ifeq ($(CONFIG_APL),y)
ifeq ($(CONFIG_APL_SRC),y)
ifeq ($(CONFIG_APL_SRC_APP),y)
LOCAL_OBJS += $(BUILD_DIR)/apl/src/app.o
$(BUILD_DIR)/apl/src/app.o: $(PROJECT_ROOT)/apl/src/app.c $(FLAG_DEPS)
	@mkdir -p $(dir $@)
	@echo "CC $<"
	$(CC) $(CFLAGS) -MMD -MP -MF $(BUILD_DIR)/apl/src/app.d -c $< -o $@
	@"$(PYTHON)" -c 'import os,sys,time; t=time.time(); [os.utime(p,(t,t)) for p in sys.argv[1:]]' $@ $(BUILD_DIR)/apl/src/app.d
endif
endif
endif

ifeq ($(CONFIG_APL),y)
ifeq ($(CONFIG_APL_SRC),y)
ifeq ($(CONFIG_APL_SRC_APP_BEEP),y)
LOCAL_OBJS += $(BUILD_DIR)/apl/src/app_beep.o
$(BUILD_DIR)/apl/src/app_beep.o: $(PROJECT_ROOT)/apl/src/app_beep.c $(FLAG_DEPS)
	@mkdir -p $(dir $@)
	@echo "CC $<"
	$(CC) $(CFLAGS) -MMD -MP -MF $(BUILD_DIR)/apl/src/app_beep.d -c $< -o $@
	@"$(PYTHON)" -c 'import os,sys,time; t=time.time(); [os.utime(p,(t,t)) for p in sys.argv[1:]]' $@ $(BUILD_DIR)/apl/src/app_beep.d
endif
endif
endif

ifeq ($(CONFIG_APL),y)
ifeq ($(CONFIG_APL_SRC),y)
ifeq ($(CONFIG_APL_SRC_APP_KEY),y)
LOCAL_OBJS += $(BUILD_DIR)/apl/src/app_key.o
$(BUILD_DIR)/apl/src/app_key.o: $(PROJECT_ROOT)/apl/src/app_key.c $(FLAG_DEPS)
	@mkdir -p $(dir $@)
	@echo "CC $<"
	$(CC) $(CFLAGS) -MMD -MP -MF $(BUILD_DIR)/apl/src/app_key.d -c $< -o $@
	@"$(PYTHON)" -c 'import os,sys,time; t=time.time(); [os.utime(p,(t,t)) for p in sys.argv[1:]]' $@ $(BUILD_DIR)/apl/src/app_key.d
endif
endif
endif

ifeq ($(CONFIG_APL),y)
ifeq ($(CONFIG_APL_SRC),y)
ifeq ($(CONFIG_APL_SRC_APP_LED),y)
LOCAL_OBJS += $(BUILD_DIR)/apl/src/app_led.o
$(BUILD_DIR)/apl/src/app_led.o: $(PROJECT_ROOT)/apl/src/app_led.c $(FLAG_DEPS)
	@mkdir -p $(dir $@)
	@echo "CC $<"
	$(CC) $(CFLAGS) -MMD -MP -MF $(BUILD_DIR)/apl/src/app_led.d -c $< -o $@
	@"$(PYTHON)" -c 'import os,sys,time; t=time.time(); [os.utime(p,(t,t)) for p in sys.argv[1:]]' $@ $(BUILD_DIR)/apl/src/app_led.d
endif
endif
endif

ifeq ($(CONFIG_APL),y)
ifeq ($(CONFIG_APL_SRC),y)
ifeq ($(CONFIG_APL_SRC_APP_SAMPLE),y)
LOCAL_OBJS += $(BUILD_DIR)/apl/src/app_sample.o
$(BUILD_DIR)/apl/src/app_sample.o: $(PROJECT_ROOT)/apl/src/app_sample.c $(FLAG_DEPS)
	@mkdir -p $(dir $@)
	@echo "CC $<"
	$(CC) $(CFLAGS) -MMD -MP -MF $(BUILD_DIR)/apl/src/app_sample.d -c $< -o $@
	@"$(PYTHON)" -c 'import os,sys,time; t=time.time(); [os.utime(p,(t,t)) for p in sys.argv[1:]]' $@ $(BUILD_DIR)/apl/src/app_sample.d
endif
endif
endif

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
