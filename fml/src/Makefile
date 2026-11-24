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
INCLUDES := $(addprefix -I,$(INCLUDE_DIRS))

FLAG_DEPS := $(wildcard $(PROJECT_ROOT)/toolchain.make) $(wildcard $(PROJECT_ROOT)/out/$(BUILD_VARIANT)/.config)

LOCAL_OBJS :=
-include $(BUILD_DIR)/fml/src/fml_fatfs.d
-include $(BUILD_DIR)/fml/src/fml_font.d
-include $(BUILD_DIR)/fml/src/fml_font_ascii8x16.d
-include $(BUILD_DIR)/fml/src/fml_gui.d
-include $(BUILD_DIR)/fml/src/fml_panic.d

ifeq ($(CONFIG_FML),y)
ifeq ($(CONFIG_FML_SRC),y)
LOCAL_OBJS += $(BUILD_DIR)/fml/src/fml_fatfs.o
$(BUILD_DIR)/fml/src/fml_fatfs.o: $(PROJECT_ROOT)/fml/src/fml_fatfs.c $(FLAG_DEPS)
	@mkdir -p $(dir $@)
	@echo "CC $<"
	$(CC) $(CFLAGS) -MMD -MP -MF $(BUILD_DIR)/fml/src/fml_fatfs.d -c $< -o $@
	@"$(PYTHON)" -c 'import os,sys,time; t=time.time(); [os.utime(p,(t,t)) for p in sys.argv[1:]]' $@ $(BUILD_DIR)/fml/src/fml_fatfs.d
endif
endif

ifeq ($(CONFIG_FML),y)
ifeq ($(CONFIG_FML_SRC),y)
LOCAL_OBJS += $(BUILD_DIR)/fml/src/fml_font.o
$(BUILD_DIR)/fml/src/fml_font.o: $(PROJECT_ROOT)/fml/src/fml_font.c $(FLAG_DEPS)
	@mkdir -p $(dir $@)
	@echo "CC $<"
	$(CC) $(CFLAGS) -MMD -MP -MF $(BUILD_DIR)/fml/src/fml_font.d -c $< -o $@
	@"$(PYTHON)" -c 'import os,sys,time; t=time.time(); [os.utime(p,(t,t)) for p in sys.argv[1:]]' $@ $(BUILD_DIR)/fml/src/fml_font.d
endif
endif

ifeq ($(CONFIG_FML),y)
ifeq ($(CONFIG_FML_SRC),y)
LOCAL_OBJS += $(BUILD_DIR)/fml/src/fml_font_ascii8x16.o
$(BUILD_DIR)/fml/src/fml_font_ascii8x16.o: $(PROJECT_ROOT)/fml/src/fml_font_ascii8x16.c $(FLAG_DEPS)
	@mkdir -p $(dir $@)
	@echo "CC $<"
	$(CC) $(CFLAGS) -MMD -MP -MF $(BUILD_DIR)/fml/src/fml_font_ascii8x16.d -c $< -o $@
	@"$(PYTHON)" -c 'import os,sys,time; t=time.time(); [os.utime(p,(t,t)) for p in sys.argv[1:]]' $@ $(BUILD_DIR)/fml/src/fml_font_ascii8x16.d
endif
endif

ifeq ($(CONFIG_FML),y)
ifeq ($(CONFIG_FML_SRC),y)
LOCAL_OBJS += $(BUILD_DIR)/fml/src/fml_gui.o
$(BUILD_DIR)/fml/src/fml_gui.o: $(PROJECT_ROOT)/fml/src/fml_gui.c $(FLAG_DEPS)
	@mkdir -p $(dir $@)
	@echo "CC $<"
	$(CC) $(CFLAGS) -MMD -MP -MF $(BUILD_DIR)/fml/src/fml_gui.d -c $< -o $@
	@"$(PYTHON)" -c 'import os,sys,time; t=time.time(); [os.utime(p,(t,t)) for p in sys.argv[1:]]' $@ $(BUILD_DIR)/fml/src/fml_gui.d
endif
endif

ifeq ($(CONFIG_FML),y)
ifeq ($(CONFIG_FML_SRC),y)
LOCAL_OBJS += $(BUILD_DIR)/fml/src/fml_panic.o
$(BUILD_DIR)/fml/src/fml_panic.o: $(PROJECT_ROOT)/fml/src/fml_panic.c $(FLAG_DEPS)
	@mkdir -p $(dir $@)
	@echo "CC $<"
	$(CC) $(CFLAGS) -MMD -MP -MF $(BUILD_DIR)/fml/src/fml_panic.d -c $< -o $@
	@"$(PYTHON)" -c 'import os,sys,time; t=time.time(); [os.utime(p,(t,t)) for p in sys.argv[1:]]' $@ $(BUILD_DIR)/fml/src/fml_panic.d
endif
endif

.PHONY: all clean subdirs

all: $(LOCAL_OBJS)
	@$(MAKE) --no-print-directory -C $(PROJECT_ROOT)/fml/src/fatfs all

clean:
	rm -rf $(BUILD_DIR)/fml/src
