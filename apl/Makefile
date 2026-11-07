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

FLAG_DEPS := $(wildcard $(PROJECT_ROOT)/toolchain.make) $(wildcard $(PROJECT_ROOT)/out/.config)

LOCAL_OBJS :=
ARCHIVE_OBJS :=
ifeq ($(CONFIG_APL),y)
ifeq ($(CONFIG_APL_SRC),y)
ifeq ($(CONFIG_APL_SRC_MAIN),y)
ARCHIVE_OBJS += $(BUILD_DIR)/apl/src/main.o
endif
endif
endif

.PHONY: all clean
all: $(LOCAL_OBJS)
	@$(MAKE) --no-print-directory -C $(PROJECT_ROOT)/apl/inc all
	@$(MAKE) --no-print-directory -C $(PROJECT_ROOT)/apl/src all
	@mkdir -p $(dir $(BUILD_DIR)/lib/libapl.a)
	@echo "AR $(BUILD_DIR)/lib/libapl.a"
	@rm -f $(BUILD_DIR)/lib/libapl.a
	$(AR) rcs $(BUILD_DIR)/lib/libapl.a $(ARCHIVE_OBJS)

clean:
	rm -rf $(BUILD_DIR)/apl $(BUILD_DIR)/lib/libapl.a
