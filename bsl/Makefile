# 自动生成，勿手改。刷新：python3 build/create-project.py update
PROJECT_ROOT := $(abspath $(dir $(lastword $(MAKEFILE_LIST)))/..)
BUILD_DIR := $(PROJECT_ROOT)/out/object
.DEFAULT_GOAL := all
include $(PROJECT_ROOT)/toolchain.make
-include $(PROJECT_ROOT)/out/.config

INCLUDE_DIRS :=
INCLUDE_DIRS += $(PROJECT_ROOT)/hal/arch/inc
INCLUDE_DIRS += $(PROJECT_ROOT)/hal/chip/register/inc
INCLUDE_DIRS += $(PROJECT_ROOT)/hal/chip/startup/inc
INCLUDE_DIRS += $(PROJECT_ROOT)/hal/periph/inc
INCLUDES := $(addprefix -I,$(INCLUDE_DIRS))

LOCAL_OBJS :=
ARCHIVE_OBJS :=

.PHONY: all clean
all: $(LOCAL_OBJS)
	@mkdir -p $(dir $(BUILD_DIR)/lib/libbsl.a)
	@echo "AR $(BUILD_DIR)/lib/libbsl.a"
	@rm -f $(BUILD_DIR)/lib/libbsl.a
	$(AR) rcs $(BUILD_DIR)/lib/libbsl.a $(ARCHIVE_OBJS)

clean:
	rm -rf $(BUILD_DIR)/bsl $(BUILD_DIR)/lib/libbsl.a
