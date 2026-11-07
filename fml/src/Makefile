# 自动生成，勿手改。刷新：python3 build/create-project.py update
PROJECT_ROOT := $(abspath $(dir $(lastword $(MAKEFILE_LIST)))/../..)
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
INCLUDE_DIRS += $(PROJECT_ROOT)/fml/inc
INCLUDE_DIRS += $(PROJECT_ROOT)/bsl/inc
INCLUDES := $(addprefix -I,$(INCLUDE_DIRS))

FLAG_DEPS := $(wildcard $(PROJECT_ROOT)/toolchain.make) $(wildcard $(PROJECT_ROOT)/out/.config)

LOCAL_OBJS :=
-include $(BUILD_DIR)/fml/src/fml_button.d
-include $(BUILD_DIR)/fml/src/fml_led.d

ifeq ($(CONFIG_FML),y)
ifeq ($(CONFIG_FML_SRC),y)
ifeq ($(CONFIG_FML_SRC_FML_BUTTON),y)
LOCAL_OBJS += $(BUILD_DIR)/fml/src/fml_button.o
$(BUILD_DIR)/fml/src/fml_button.o: $(PROJECT_ROOT)/fml/src/fml_button.c $(FLAG_DEPS)
	@mkdir -p $(dir $@)
	@echo "CC $<"
	$(CC) $(CFLAGS) -MMD -MP -MF $(BUILD_DIR)/fml/src/fml_button.d -c $< -o $@
	@$(PYTHON) -c 'import os,sys,time; t=time.time(); [os.utime(p,(t,t)) for p in sys.argv[1:]]' $@ $(BUILD_DIR)/fml/src/fml_button.d
endif
endif
endif

ifeq ($(CONFIG_FML),y)
ifeq ($(CONFIG_FML_SRC),y)
ifeq ($(CONFIG_FML_SRC_FML_LED),y)
LOCAL_OBJS += $(BUILD_DIR)/fml/src/fml_led.o
$(BUILD_DIR)/fml/src/fml_led.o: $(PROJECT_ROOT)/fml/src/fml_led.c $(FLAG_DEPS)
	@mkdir -p $(dir $@)
	@echo "CC $<"
	$(CC) $(CFLAGS) -MMD -MP -MF $(BUILD_DIR)/fml/src/fml_led.d -c $< -o $@
	@$(PYTHON) -c 'import os,sys,time; t=time.time(); [os.utime(p,(t,t)) for p in sys.argv[1:]]' $@ $(BUILD_DIR)/fml/src/fml_led.d
endif
endif
endif

.PHONY: all clean

all: $(LOCAL_OBJS)

clean:
	rm -rf $(BUILD_DIR)/fml/src
