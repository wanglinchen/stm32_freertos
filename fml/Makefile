# 自动生成，勿手改。刷新：python3 build/create-project.py update
PROJECT_ROOT := $(abspath $(dir $(lastword $(MAKEFILE_LIST)))/..)
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

LOCAL_OBJS :=
ARCHIVE_OBJS :=
ifeq ($(CONFIG_FML),y)
ifeq ($(CONFIG_FML_SRC),y)
ifeq ($(CONFIG_FML_SRC_FML_LED),y)
ARCHIVE_OBJS += $(BUILD_DIR)/fml/src/fml_led.o
endif
endif
endif

.PHONY: all clean
all: $(LOCAL_OBJS)
	@$(MAKE) --no-print-directory -C $(PROJECT_ROOT)/fml/inc all
	@$(MAKE) --no-print-directory -C $(PROJECT_ROOT)/fml/src all
	@mkdir -p $(dir $(BUILD_DIR)/lib/libfml.a)
	@echo "AR $(BUILD_DIR)/lib/libfml.a"
	@rm -f $(BUILD_DIR)/lib/libfml.a
	$(AR) rcs $(BUILD_DIR)/lib/libfml.a $(ARCHIVE_OBJS)

clean:
	rm -rf $(BUILD_DIR)/fml $(BUILD_DIR)/lib/libfml.a
