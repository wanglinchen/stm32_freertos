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
INCLUDES := $(addprefix -I,$(INCLUDE_DIRS))

LOCAL_OBJS :=
ARCHIVE_OBJS :=
ifeq ($(CONFIG_OSL),y)
ifeq ($(CONFIG_OSL_SRC),y)
ARCHIVE_OBJS += $(BUILD_DIR)/osl/src/freertos_port_st.o
endif
endif
ifeq ($(CONFIG_OSL),y)
ifeq ($(CONFIG_OSL_SRC),y)
ifeq ($(CONFIG_OSL_SRC_FREERTOS),y)
ARCHIVE_OBJS += $(BUILD_DIR)/osl/src/freertos/event_groups.o
endif
endif
endif
ifeq ($(CONFIG_OSL),y)
ifeq ($(CONFIG_OSL_SRC),y)
ifeq ($(CONFIG_OSL_SRC_FREERTOS),y)
ARCHIVE_OBJS += $(BUILD_DIR)/osl/src/freertos/list.o
endif
endif
endif
ifeq ($(CONFIG_OSL),y)
ifeq ($(CONFIG_OSL_SRC),y)
ifeq ($(CONFIG_OSL_SRC_FREERTOS),y)
ARCHIVE_OBJS += $(BUILD_DIR)/osl/src/freertos/queue.o
endif
endif
endif
ifeq ($(CONFIG_OSL),y)
ifeq ($(CONFIG_OSL_SRC),y)
ifeq ($(CONFIG_OSL_SRC_FREERTOS),y)
ARCHIVE_OBJS += $(BUILD_DIR)/osl/src/freertos/stream_buffer.o
endif
endif
endif
ifeq ($(CONFIG_OSL),y)
ifeq ($(CONFIG_OSL_SRC),y)
ifeq ($(CONFIG_OSL_SRC_FREERTOS),y)
ARCHIVE_OBJS += $(BUILD_DIR)/osl/src/freertos/tasks.o
endif
endif
endif
ifeq ($(CONFIG_OSL),y)
ifeq ($(CONFIG_OSL_SRC),y)
ifeq ($(CONFIG_OSL_SRC_FREERTOS),y)
ARCHIVE_OBJS += $(BUILD_DIR)/osl/src/freertos/timers.o
endif
endif
endif
ifeq ($(CONFIG_OSL),y)
ifeq ($(CONFIG_OSL_SRC),y)
ifeq ($(CONFIG_OSL_SRC_FREERTOS),y)
ifeq ($(CONFIG_OSL_SRC_FREERTOS_PORTABLE),y)
ifeq ($(CONFIG_OSL_SRC_FREERTOS_PORTABLE_GCC),y)
ifeq ($(CONFIG_OSL_SRC_FREERTOS_PORTABLE_GCC_ARM_CM4F),y)
ifeq ($(CONFIG_OSL_SRC_FREERTOS_PORTABLE_GCC_ARM_CM4F_PORT),y)
ARCHIVE_OBJS += $(BUILD_DIR)/osl/src/freertos/portable/GCC/ARM_CM4F/port.o
endif
endif
endif
endif
endif
endif
endif
ifeq ($(CONFIG_OSL),y)
ifeq ($(CONFIG_OSL_SRC),y)
ifeq ($(CONFIG_OSL_SRC_FREERTOS),y)
ifeq ($(CONFIG_OSL_SRC_FREERTOS_PORTABLE),y)
ifeq ($(CONFIG_OSL_SRC_FREERTOS_PORTABLE_MEMMANG),y)
ifeq ($(CONFIG_OSL_SRC_FREERTOS_PORTABLE_MEMMANG_HEAP_4),y)
ARCHIVE_OBJS += $(BUILD_DIR)/osl/src/freertos/portable/MemMang/heap_4.o
endif
endif
endif
endif
endif
endif

.PHONY: all clean
all: $(LOCAL_OBJS)
	@$(MAKE) --no-print-directory -C $(PROJECT_ROOT)/osl/inc all
	@$(MAKE) --no-print-directory -C $(PROJECT_ROOT)/osl/src all
	@mkdir -p $(dir $(BUILD_DIR)/lib/libosl.a)
	@echo "AR $(BUILD_DIR)/lib/libosl.a"
	@rm -f $(BUILD_DIR)/lib/libosl.a
	$(AR) rcs $(BUILD_DIR)/lib/libosl.a $(ARCHIVE_OBJS)

clean:
	rm -rf $(BUILD_DIR)/osl $(BUILD_DIR)/lib/libosl.a
