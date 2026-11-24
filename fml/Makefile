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
INCLUDE_DIRS += $(PROJECT_ROOT)/fml/inc
INCLUDE_DIRS += $(PROJECT_ROOT)/fml/src/fatfs
INCLUDE_DIRS += $(PROJECT_ROOT)/bsl/inc
INCLUDES := $(addprefix -I,$(INCLUDE_DIRS))

FLAG_DEPS := $(wildcard $(PROJECT_ROOT)/toolchain.make) $(wildcard $(PROJECT_ROOT)/out/$(BUILD_VARIANT)/.config)

LOCAL_OBJS :=
ARCHIVE_OBJS :=
ifeq ($(CONFIG_FML),y)
ifeq ($(CONFIG_FML_SRC),y)
ARCHIVE_OBJS += $(BUILD_DIR)/fml/src/fml_fatfs.o
endif
endif
ifeq ($(CONFIG_FML),y)
ifeq ($(CONFIG_FML_SRC),y)
ARCHIVE_OBJS += $(BUILD_DIR)/fml/src/fml_font.o
endif
endif
ifeq ($(CONFIG_FML),y)
ifeq ($(CONFIG_FML_SRC),y)
ARCHIVE_OBJS += $(BUILD_DIR)/fml/src/fml_font_ascii8x16.o
endif
endif
ifeq ($(CONFIG_FML),y)
ifeq ($(CONFIG_FML_SRC),y)
ARCHIVE_OBJS += $(BUILD_DIR)/fml/src/fml_gui.o
endif
endif
ifeq ($(CONFIG_FML),y)
ifeq ($(CONFIG_FML_SRC),y)
ARCHIVE_OBJS += $(BUILD_DIR)/fml/src/fml_panic.o
endif
endif
ifeq ($(CONFIG_FML),y)
ifeq ($(CONFIG_FML_SRC),y)
ifeq ($(CONFIG_FML_SRC_FATFS),y)
ifeq ($(CONFIG_FML_SRC_FATFS_FF),y)
ARCHIVE_OBJS += $(BUILD_DIR)/fml/src/fatfs/ff.o
endif
endif
endif
endif
ifeq ($(CONFIG_FML),y)
ifeq ($(CONFIG_FML_SRC),y)
ifeq ($(CONFIG_FML_SRC_FATFS),y)
ifeq ($(CONFIG_FML_SRC_FATFS_FFUNICODE),y)
ARCHIVE_OBJS += $(BUILD_DIR)/fml/src/fatfs/ffunicode.o
endif
endif
endif
endif

.PHONY: all clean subdirs

LIB := $(TARGET_DIR)/lib/libfml.a

all: subdirs
	@$(MAKE) --no-print-directory $(LIB)

subdirs:
	@$(MAKE) --no-print-directory -C $(PROJECT_ROOT)/fml/src all

$(LIB): $(ARCHIVE_OBJS)
	@mkdir -p $(dir $@)
	@echo "AR $@"
	@rm -f $@
	$(AR) rcs $@ $(ARCHIVE_OBJS)

clean:
	rm -rf $(BUILD_DIR)/fml $(TARGET_DIR)/lib/libfml.a
