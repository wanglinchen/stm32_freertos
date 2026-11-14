# 自动生成，勿手改。刷新：python3 build/build.py update
PROJECT_ROOT := $(abspath $(dir $(lastword $(MAKEFILE_LIST)))/..)
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
ARCHIVE_OBJS :=
ifeq ($(CONFIG_OSL),y)
ifeq ($(CONFIG_OSL_SRC),y)
ARCHIVE_OBJS += $(BUILD_DIR)/osl/src/osl_boot.o
endif
endif
ifeq ($(CONFIG_OSL),y)
ifeq ($(CONFIG_OSL_SRC),y)
ARCHIVE_OBJS += $(BUILD_DIR)/osl/src/osl_health.o
endif
endif
ifeq ($(CONFIG_OSL),y)
ifeq ($(CONFIG_OSL_SRC),y)
ARCHIVE_OBJS += $(BUILD_DIR)/osl/src/osl_panic.o
endif
endif
ifeq ($(CONFIG_OSL),y)
ifeq ($(CONFIG_OSL_SRC),y)
ifeq ($(CONFIG_OSL_SRC_FREERTOS),y)
ifeq ($(CONFIG_OSL_SRC_FREERTOS_EVENT_GROUPS),y)
ARCHIVE_OBJS += $(BUILD_DIR)/osl/src/freertos/event_groups.o
endif
endif
endif
endif
ifeq ($(CONFIG_OSL),y)
ifeq ($(CONFIG_OSL_SRC),y)
ifeq ($(CONFIG_OSL_SRC_FREERTOS),y)
ifeq ($(CONFIG_OSL_SRC_FREERTOS_HEAP_4),y)
ARCHIVE_OBJS += $(BUILD_DIR)/osl/src/freertos/heap_4.o
endif
endif
endif
endif
ifeq ($(CONFIG_OSL),y)
ifeq ($(CONFIG_OSL_SRC),y)
ifeq ($(CONFIG_OSL_SRC_FREERTOS),y)
ifeq ($(CONFIG_OSL_SRC_FREERTOS_LIST),y)
ARCHIVE_OBJS += $(BUILD_DIR)/osl/src/freertos/list.o
endif
endif
endif
endif
ifeq ($(CONFIG_OSL),y)
ifeq ($(CONFIG_OSL_SRC),y)
ifeq ($(CONFIG_OSL_SRC_FREERTOS),y)
ifeq ($(CONFIG_OSL_SRC_FREERTOS_PORT_M4),y)
ARCHIVE_OBJS += $(BUILD_DIR)/osl/src/freertos/port_m4.o
endif
endif
endif
endif
ifeq ($(CONFIG_OSL),y)
ifeq ($(CONFIG_OSL_SRC),y)
ifeq ($(CONFIG_OSL_SRC_FREERTOS),y)
ifeq ($(CONFIG_OSL_SRC_FREERTOS_QUEUE),y)
ARCHIVE_OBJS += $(BUILD_DIR)/osl/src/freertos/queue.o
endif
endif
endif
endif
ifeq ($(CONFIG_OSL),y)
ifeq ($(CONFIG_OSL_SRC),y)
ifeq ($(CONFIG_OSL_SRC_FREERTOS),y)
ifeq ($(CONFIG_OSL_SRC_FREERTOS_STREAM_BUFFER),y)
ARCHIVE_OBJS += $(BUILD_DIR)/osl/src/freertos/stream_buffer.o
endif
endif
endif
endif
ifeq ($(CONFIG_OSL),y)
ifeq ($(CONFIG_OSL_SRC),y)
ifeq ($(CONFIG_OSL_SRC_FREERTOS),y)
ifeq ($(CONFIG_OSL_SRC_FREERTOS_TASKS),y)
ARCHIVE_OBJS += $(BUILD_DIR)/osl/src/freertos/tasks.o
endif
endif
endif
endif

.PHONY: all clean subdirs

LIB := $(TARGET_DIR)/lib/libosl.a

all: subdirs
	@$(MAKE) --no-print-directory $(LIB)

subdirs:
	@$(MAKE) --no-print-directory -C $(PROJECT_ROOT)/osl/inc all
	@$(MAKE) --no-print-directory -C $(PROJECT_ROOT)/osl/src all

$(LIB): $(ARCHIVE_OBJS)
	@mkdir -p $(dir $@)
	@echo "AR $@"
	@rm -f $@
	$(AR) rcs $@ $(ARCHIVE_OBJS)

clean:
	rm -rf $(BUILD_DIR)/osl $(TARGET_DIR)/lib/libosl.a
