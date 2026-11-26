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
INCLUDE_DIRS += $(PROJECT_ROOT)/apl/inc
INCLUDES := $(addprefix -I,$(INCLUDE_DIRS))

FLAG_DEPS := $(wildcard $(PROJECT_ROOT)/toolchain.make) $(wildcard $(PROJECT_ROOT)/out/$(BUILD_VARIANT)/.config)

LOCAL_OBJS :=
ARCHIVE_OBJS :=
ifeq ($(CONFIG_APL),y)
ifeq ($(CONFIG_APL_SRC),y)
ifeq ($(CONFIG_APL_SRC_APP),y)
ARCHIVE_OBJS += $(BUILD_DIR)/apl/src/app.o
endif
endif
endif
ifeq ($(CONFIG_APL),y)
ifeq ($(CONFIG_APL_SRC),y)
ifeq ($(CONFIG_APL_SRC_APP_BEEP),y)
ARCHIVE_OBJS += $(BUILD_DIR)/apl/src/app_beep.o
endif
endif
endif
ifeq ($(CONFIG_APL),y)
ifeq ($(CONFIG_APL_SRC),y)
ifeq ($(CONFIG_APL_SRC_APP_CALC),y)
ARCHIVE_OBJS += $(BUILD_DIR)/apl/src/app_calc.o
endif
endif
endif
ifeq ($(CONFIG_APL),y)
ifeq ($(CONFIG_APL_SRC),y)
ifeq ($(CONFIG_APL_SRC_APP_CALENDAR),y)
ARCHIVE_OBJS += $(BUILD_DIR)/apl/src/app_calendar.o
endif
endif
endif
ifeq ($(CONFIG_APL),y)
ifeq ($(CONFIG_APL_SRC),y)
ifeq ($(CONFIG_APL_SRC_APP_EBOOK),y)
ARCHIVE_OBJS += $(BUILD_DIR)/apl/src/app_ebook.o
endif
endif
endif
ifeq ($(CONFIG_APL),y)
ifeq ($(CONFIG_APL_SRC),y)
ifeq ($(CONFIG_APL_SRC_APP_IMU),y)
ARCHIVE_OBJS += $(BUILD_DIR)/apl/src/app_imu.o
endif
endif
endif
ifeq ($(CONFIG_APL),y)
ifeq ($(CONFIG_APL_SRC),y)
ifeq ($(CONFIG_APL_SRC_APP_KEY),y)
ARCHIVE_OBJS += $(BUILD_DIR)/apl/src/app_key.o
endif
endif
endif
ifeq ($(CONFIG_APL),y)
ifeq ($(CONFIG_APL_SRC),y)
ifeq ($(CONFIG_APL_SRC_APP_LED),y)
ARCHIVE_OBJS += $(BUILD_DIR)/apl/src/app_led.o
endif
endif
endif
ifeq ($(CONFIG_APL),y)
ifeq ($(CONFIG_APL_SRC),y)
ifeq ($(CONFIG_APL_SRC_APP_LEVEL),y)
ARCHIVE_OBJS += $(BUILD_DIR)/apl/src/app_level.o
endif
endif
endif
ifeq ($(CONFIG_APL),y)
ifeq ($(CONFIG_APL_SRC),y)
ifeq ($(CONFIG_APL_SRC_APP_MUSIC),y)
ARCHIVE_OBJS += $(BUILD_DIR)/apl/src/app_music.o
endif
endif
endif
ifeq ($(CONFIG_APL),y)
ifeq ($(CONFIG_APL_SRC),y)
ifeq ($(CONFIG_APL_SRC_APP_NOTEPAD),y)
ARCHIVE_OBJS += $(BUILD_DIR)/apl/src/app_notepad.o
endif
endif
endif
ifeq ($(CONFIG_APL),y)
ifeq ($(CONFIG_APL_SRC),y)
ifeq ($(CONFIG_APL_SRC_APP_PAINT),y)
ARCHIVE_OBJS += $(BUILD_DIR)/apl/src/app_paint.o
endif
endif
endif
ifeq ($(CONFIG_APL),y)
ifeq ($(CONFIG_APL_SRC),y)
ifeq ($(CONFIG_APL_SRC_APP_PICVIEWER),y)
ARCHIVE_OBJS += $(BUILD_DIR)/apl/src/app_picviewer.o
endif
endif
endif
ifeq ($(CONFIG_APL),y)
ifeq ($(CONFIG_APL_SRC),y)
ifeq ($(CONFIG_APL_SRC_APP_QR),y)
ARCHIVE_OBJS += $(BUILD_DIR)/apl/src/app_qr.o
endif
endif
endif
ifeq ($(CONFIG_APL),y)
ifeq ($(CONFIG_APL_SRC),y)
ifeq ($(CONFIG_APL_SRC_APP_SAMPLE),y)
ARCHIVE_OBJS += $(BUILD_DIR)/apl/src/app_sample.o
endif
endif
endif
ifeq ($(CONFIG_APL),y)
ifeq ($(CONFIG_APL_SRC),y)
ifeq ($(CONFIG_APL_SRC_APP_SETTINGS),y)
ARCHIVE_OBJS += $(BUILD_DIR)/apl/src/app_settings.o
endif
endif
endif
ifeq ($(CONFIG_APL),y)
ifeq ($(CONFIG_APL_SRC),y)
ifeq ($(CONFIG_APL_SRC_MAIN),y)
ARCHIVE_OBJS += $(BUILD_DIR)/apl/src/main.o
endif
endif
endif

.PHONY: all clean subdirs

LIB := $(TARGET_DIR)/lib/libapl.a

all: subdirs
	@$(MAKE) --no-print-directory $(LIB)

subdirs:
	@$(MAKE) --no-print-directory -C $(PROJECT_ROOT)/apl/src all

$(LIB): $(ARCHIVE_OBJS)
	@mkdir -p $(dir $@)
	@echo "AR $@"
	@rm -f $@
	$(AR) rcs $@ $(ARCHIVE_OBJS)

clean:
	rm -rf $(BUILD_DIR)/apl $(TARGET_DIR)/lib/libapl.a
