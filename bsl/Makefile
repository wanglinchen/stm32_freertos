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
INCLUDE_DIRS += $(PROJECT_ROOT)/bsl/inc
INCLUDES := $(addprefix -I,$(INCLUDE_DIRS))

FLAG_DEPS := $(wildcard $(PROJECT_ROOT)/toolchain.make) $(wildcard $(PROJECT_ROOT)/out/.config)

LOCAL_OBJS :=
ARCHIVE_OBJS :=
ifeq ($(CONFIG_BSL),y)
ifeq ($(CONFIG_BSL_SRC),y)
ifeq ($(CONFIG_BSL_SRC_BOARD_BEEP),y)
ARCHIVE_OBJS += $(BUILD_DIR)/bsl/src/board_beep.o
endif
endif
endif
ifeq ($(CONFIG_BSL),y)
ifeq ($(CONFIG_BSL_SRC),y)
ifeq ($(CONFIG_BSL_SRC_BOARD_KEY),y)
ARCHIVE_OBJS += $(BUILD_DIR)/bsl/src/board_key.o
endif
endif
endif
ifeq ($(CONFIG_BSL),y)
ifeq ($(CONFIG_BSL_SRC),y)
ifeq ($(CONFIG_BSL_SRC_BOARD_LED),y)
ARCHIVE_OBJS += $(BUILD_DIR)/bsl/src/board_led.o
endif
endif
endif

.PHONY: all clean subdirs

LIB := $(BUILD_DIR)/lib/libbsl.a

all: subdirs
	@$(MAKE) --no-print-directory $(LIB)

subdirs:
	@$(MAKE) --no-print-directory -C $(PROJECT_ROOT)/bsl/inc all
	@$(MAKE) --no-print-directory -C $(PROJECT_ROOT)/bsl/src all

$(LIB): $(ARCHIVE_OBJS)
	@mkdir -p $(dir $@)
	@echo "AR $@"
	@rm -f $@
	$(AR) rcs $@ $(ARCHIVE_OBJS)

clean:
	rm -rf $(BUILD_DIR)/bsl $(BUILD_DIR)/lib/libbsl.a
