# 自动生成，勿手改。刷新：python3 build/build.py update
PROJECT_ROOT := $(abspath $(dir $(lastword $(MAKEFILE_LIST)))/../..)
BUILD_VARIANT ?= userdebug
BUILD_DIR := $(PROJECT_ROOT)/out/$(BUILD_VARIANT)/objects
TARGET_DIR := $(BUILD_DIR)/target
.DEFAULT_GOAL := all
include $(PROJECT_ROOT)/toolchain.make
-include $(PROJECT_ROOT)/out/$(BUILD_VARIANT)/.config

INCLUDE_DIRS :=
INCLUDE_DIRS += $(PROJECT_ROOT)/hal/arch/inc
INCLUDE_DIRS += $(PROJECT_ROOT)/hal/chip/register/inc
INCLUDE_DIRS += $(PROJECT_ROOT)/hal/chip/startup/inc
INCLUDE_DIRS += $(PROJECT_ROOT)/hal/periph/inc
INCLUDE_DIRS += $(PROJECT_ROOT)/bsl/inc
INCLUDES := $(addprefix -I,$(INCLUDE_DIRS))

FLAG_DEPS := $(wildcard $(PROJECT_ROOT)/toolchain.make) $(wildcard $(PROJECT_ROOT)/out/$(BUILD_VARIANT)/.config)

LOCAL_OBJS :=
-include $(BUILD_DIR)/bsl/src/board_beep.d
-include $(BUILD_DIR)/bsl/src/board_debug_uart.d
-include $(BUILD_DIR)/bsl/src/board_dtb.d
-include $(BUILD_DIR)/bsl/src/board_key.d
-include $(BUILD_DIR)/bsl/src/board_lcd.d
-include $(BUILD_DIR)/bsl/src/board_led.d
-include $(BUILD_DIR)/bsl/src/board_post.d
-include $(BUILD_DIR)/bsl/src/board_rt.d
-include $(BUILD_DIR)/bsl/src/board_sdram.d
-include $(BUILD_DIR)/bsl/src/board_system.d
-include $(BUILD_DIR)/bsl/src/bsl_device.d
-include $(BUILD_DIR)/bsl/src/bsl_fdt.d
-include $(BUILD_DIR)/bsl/src/bsl_gpio.d
-include $(BUILD_DIR)/bsl/src/bsl_i2c_gpio.d
-include $(BUILD_DIR)/bsl/src/bsl_init.d
-include $(BUILD_DIR)/bsl/src/bsl_log.d
-include $(BUILD_DIR)/bsl/src/bsl_pcf8574.d

ifeq ($(CONFIG_BSL),y)
ifeq ($(CONFIG_BSL_SRC),y)
ifeq ($(CONFIG_BSL_SRC_BOARD_BEEP),y)
LOCAL_OBJS += $(BUILD_DIR)/bsl/src/board_beep.o
$(BUILD_DIR)/bsl/src/board_beep.o: $(PROJECT_ROOT)/bsl/src/board_beep.c $(FLAG_DEPS)
	@mkdir -p $(dir $@)
	@echo "CC $<"
	$(CC) $(CFLAGS) -MMD -MP -MF $(BUILD_DIR)/bsl/src/board_beep.d -c $< -o $@
	@"$(PYTHON)" -c 'import os,sys,time; t=time.time(); [os.utime(p,(t,t)) for p in sys.argv[1:]]' $@ $(BUILD_DIR)/bsl/src/board_beep.d
endif
endif
endif

ifeq ($(CONFIG_BSL),y)
ifeq ($(CONFIG_BSL_SRC),y)
ifeq ($(CONFIG_BSL_SRC_BOARD_DEBUG_UART),y)
LOCAL_OBJS += $(BUILD_DIR)/bsl/src/board_debug_uart.o
$(BUILD_DIR)/bsl/src/board_debug_uart.o: $(PROJECT_ROOT)/bsl/src/board_debug_uart.c $(FLAG_DEPS)
	@mkdir -p $(dir $@)
	@echo "CC $<"
	$(CC) $(CFLAGS) -MMD -MP -MF $(BUILD_DIR)/bsl/src/board_debug_uart.d -c $< -o $@
	@"$(PYTHON)" -c 'import os,sys,time; t=time.time(); [os.utime(p,(t,t)) for p in sys.argv[1:]]' $@ $(BUILD_DIR)/bsl/src/board_debug_uart.d
endif
endif
endif

ifeq ($(CONFIG_BSL),y)
ifeq ($(CONFIG_BSL_SRC),y)
ifeq ($(CONFIG_BSL_SRC_BOARD_DTB),y)
LOCAL_OBJS += $(BUILD_DIR)/bsl/src/board_dtb.o
$(BUILD_DIR)/bsl/src/board_dtb.o: $(PROJECT_ROOT)/bsl/src/board_dtb.s $(PROJECT_ROOT)/out/dts/apollo.dtb $(FLAG_DEPS)
	@mkdir -p $(dir $@)
	@echo "AS $<"
	$(AS) $(ASFLAGS) -MMD -MP -MF $(BUILD_DIR)/bsl/src/board_dtb.d -c $< -o $@
	@"$(PYTHON)" -c 'import os,sys,time; t=time.time(); [os.utime(p,(t,t)) for p in sys.argv[1:]]' $@ $(BUILD_DIR)/bsl/src/board_dtb.d
endif
endif
endif

ifeq ($(CONFIG_BSL),y)
ifeq ($(CONFIG_BSL_SRC),y)
ifeq ($(CONFIG_BSL_SRC_BOARD_KEY),y)
LOCAL_OBJS += $(BUILD_DIR)/bsl/src/board_key.o
$(BUILD_DIR)/bsl/src/board_key.o: $(PROJECT_ROOT)/bsl/src/board_key.c $(FLAG_DEPS)
	@mkdir -p $(dir $@)
	@echo "CC $<"
	$(CC) $(CFLAGS) -MMD -MP -MF $(BUILD_DIR)/bsl/src/board_key.d -c $< -o $@
	@"$(PYTHON)" -c 'import os,sys,time; t=time.time(); [os.utime(p,(t,t)) for p in sys.argv[1:]]' $@ $(BUILD_DIR)/bsl/src/board_key.d
endif
endif
endif

ifeq ($(CONFIG_BSL),y)
ifeq ($(CONFIG_BSL_SRC),y)
ifeq ($(CONFIG_BSL_SRC_BOARD_LCD),y)
LOCAL_OBJS += $(BUILD_DIR)/bsl/src/board_lcd.o
$(BUILD_DIR)/bsl/src/board_lcd.o: $(PROJECT_ROOT)/bsl/src/board_lcd.c $(FLAG_DEPS)
	@mkdir -p $(dir $@)
	@echo "CC $<"
	$(CC) $(CFLAGS) -MMD -MP -MF $(BUILD_DIR)/bsl/src/board_lcd.d -c $< -o $@
	@"$(PYTHON)" -c 'import os,sys,time; t=time.time(); [os.utime(p,(t,t)) for p in sys.argv[1:]]' $@ $(BUILD_DIR)/bsl/src/board_lcd.d
endif
endif
endif

ifeq ($(CONFIG_BSL),y)
ifeq ($(CONFIG_BSL_SRC),y)
ifeq ($(CONFIG_BSL_SRC_BOARD_LED),y)
LOCAL_OBJS += $(BUILD_DIR)/bsl/src/board_led.o
$(BUILD_DIR)/bsl/src/board_led.o: $(PROJECT_ROOT)/bsl/src/board_led.c $(FLAG_DEPS)
	@mkdir -p $(dir $@)
	@echo "CC $<"
	$(CC) $(CFLAGS) -MMD -MP -MF $(BUILD_DIR)/bsl/src/board_led.d -c $< -o $@
	@"$(PYTHON)" -c 'import os,sys,time; t=time.time(); [os.utime(p,(t,t)) for p in sys.argv[1:]]' $@ $(BUILD_DIR)/bsl/src/board_led.d
endif
endif
endif

ifeq ($(CONFIG_BSL),y)
ifeq ($(CONFIG_BSL_SRC),y)
ifeq ($(CONFIG_BSL_SRC_BOARD_POST),y)
LOCAL_OBJS += $(BUILD_DIR)/bsl/src/board_post.o
$(BUILD_DIR)/bsl/src/board_post.o: $(PROJECT_ROOT)/bsl/src/board_post.c $(FLAG_DEPS)
	@mkdir -p $(dir $@)
	@echo "CC $<"
	$(CC) $(CFLAGS) -MMD -MP -MF $(BUILD_DIR)/bsl/src/board_post.d -c $< -o $@
	@"$(PYTHON)" -c 'import os,sys,time; t=time.time(); [os.utime(p,(t,t)) for p in sys.argv[1:]]' $@ $(BUILD_DIR)/bsl/src/board_post.d
endif
endif
endif

ifeq ($(CONFIG_BSL),y)
ifeq ($(CONFIG_BSL_SRC),y)
ifeq ($(CONFIG_BSL_SRC_BOARD_RT),y)
LOCAL_OBJS += $(BUILD_DIR)/bsl/src/board_rt.o
$(BUILD_DIR)/bsl/src/board_rt.o: $(PROJECT_ROOT)/bsl/src/board_rt.c $(FLAG_DEPS)
	@mkdir -p $(dir $@)
	@echo "CC $<"
	$(CC) $(CFLAGS) -MMD -MP -MF $(BUILD_DIR)/bsl/src/board_rt.d -c $< -o $@
	@"$(PYTHON)" -c 'import os,sys,time; t=time.time(); [os.utime(p,(t,t)) for p in sys.argv[1:]]' $@ $(BUILD_DIR)/bsl/src/board_rt.d
endif
endif
endif

ifeq ($(CONFIG_BSL),y)
ifeq ($(CONFIG_BSL_SRC),y)
ifeq ($(CONFIG_BSL_SRC_BOARD_SDRAM),y)
LOCAL_OBJS += $(BUILD_DIR)/bsl/src/board_sdram.o
$(BUILD_DIR)/bsl/src/board_sdram.o: $(PROJECT_ROOT)/bsl/src/board_sdram.c $(FLAG_DEPS)
	@mkdir -p $(dir $@)
	@echo "CC $<"
	$(CC) $(CFLAGS) -MMD -MP -MF $(BUILD_DIR)/bsl/src/board_sdram.d -c $< -o $@
	@"$(PYTHON)" -c 'import os,sys,time; t=time.time(); [os.utime(p,(t,t)) for p in sys.argv[1:]]' $@ $(BUILD_DIR)/bsl/src/board_sdram.d
endif
endif
endif

ifeq ($(CONFIG_BSL),y)
ifeq ($(CONFIG_BSL_SRC),y)
ifeq ($(CONFIG_BSL_SRC_BOARD_SYSTEM),y)
LOCAL_OBJS += $(BUILD_DIR)/bsl/src/board_system.o
$(BUILD_DIR)/bsl/src/board_system.o: $(PROJECT_ROOT)/bsl/src/board_system.c $(FLAG_DEPS)
	@mkdir -p $(dir $@)
	@echo "CC $<"
	$(CC) $(CFLAGS) -MMD -MP -MF $(BUILD_DIR)/bsl/src/board_system.d -c $< -o $@
	@"$(PYTHON)" -c 'import os,sys,time; t=time.time(); [os.utime(p,(t,t)) for p in sys.argv[1:]]' $@ $(BUILD_DIR)/bsl/src/board_system.d
endif
endif
endif

ifeq ($(CONFIG_BSL),y)
ifeq ($(CONFIG_BSL_SRC),y)
ifeq ($(CONFIG_BSL_SRC_BSL_DEVICE),y)
LOCAL_OBJS += $(BUILD_DIR)/bsl/src/bsl_device.o
$(BUILD_DIR)/bsl/src/bsl_device.o: $(PROJECT_ROOT)/bsl/src/bsl_device.c $(FLAG_DEPS)
	@mkdir -p $(dir $@)
	@echo "CC $<"
	$(CC) $(CFLAGS) -MMD -MP -MF $(BUILD_DIR)/bsl/src/bsl_device.d -c $< -o $@
	@"$(PYTHON)" -c 'import os,sys,time; t=time.time(); [os.utime(p,(t,t)) for p in sys.argv[1:]]' $@ $(BUILD_DIR)/bsl/src/bsl_device.d
endif
endif
endif

ifeq ($(CONFIG_BSL),y)
ifeq ($(CONFIG_BSL_SRC),y)
ifeq ($(CONFIG_BSL_SRC_BSL_FDT),y)
LOCAL_OBJS += $(BUILD_DIR)/bsl/src/bsl_fdt.o
$(BUILD_DIR)/bsl/src/bsl_fdt.o: $(PROJECT_ROOT)/bsl/src/bsl_fdt.c $(FLAG_DEPS)
	@mkdir -p $(dir $@)
	@echo "CC $<"
	$(CC) $(CFLAGS) -MMD -MP -MF $(BUILD_DIR)/bsl/src/bsl_fdt.d -c $< -o $@
	@"$(PYTHON)" -c 'import os,sys,time; t=time.time(); [os.utime(p,(t,t)) for p in sys.argv[1:]]' $@ $(BUILD_DIR)/bsl/src/bsl_fdt.d
endif
endif
endif

ifeq ($(CONFIG_BSL),y)
ifeq ($(CONFIG_BSL_SRC),y)
ifeq ($(CONFIG_BSL_SRC_BSL_GPIO),y)
LOCAL_OBJS += $(BUILD_DIR)/bsl/src/bsl_gpio.o
$(BUILD_DIR)/bsl/src/bsl_gpio.o: $(PROJECT_ROOT)/bsl/src/bsl_gpio.c $(FLAG_DEPS)
	@mkdir -p $(dir $@)
	@echo "CC $<"
	$(CC) $(CFLAGS) -MMD -MP -MF $(BUILD_DIR)/bsl/src/bsl_gpio.d -c $< -o $@
	@"$(PYTHON)" -c 'import os,sys,time; t=time.time(); [os.utime(p,(t,t)) for p in sys.argv[1:]]' $@ $(BUILD_DIR)/bsl/src/bsl_gpio.d
endif
endif
endif

ifeq ($(CONFIG_BSL),y)
ifeq ($(CONFIG_BSL_SRC),y)
ifeq ($(CONFIG_BSL_SRC_BSL_I2C_GPIO),y)
LOCAL_OBJS += $(BUILD_DIR)/bsl/src/bsl_i2c_gpio.o
$(BUILD_DIR)/bsl/src/bsl_i2c_gpio.o: $(PROJECT_ROOT)/bsl/src/bsl_i2c_gpio.c $(FLAG_DEPS)
	@mkdir -p $(dir $@)
	@echo "CC $<"
	$(CC) $(CFLAGS) -MMD -MP -MF $(BUILD_DIR)/bsl/src/bsl_i2c_gpio.d -c $< -o $@
	@"$(PYTHON)" -c 'import os,sys,time; t=time.time(); [os.utime(p,(t,t)) for p in sys.argv[1:]]' $@ $(BUILD_DIR)/bsl/src/bsl_i2c_gpio.d
endif
endif
endif

ifeq ($(CONFIG_BSL),y)
ifeq ($(CONFIG_BSL_SRC),y)
ifeq ($(CONFIG_BSL_SRC_BSL_INIT),y)
LOCAL_OBJS += $(BUILD_DIR)/bsl/src/bsl_init.o
$(BUILD_DIR)/bsl/src/bsl_init.o: $(PROJECT_ROOT)/bsl/src/bsl_init.c $(FLAG_DEPS)
	@mkdir -p $(dir $@)
	@echo "CC $<"
	$(CC) $(CFLAGS) -MMD -MP -MF $(BUILD_DIR)/bsl/src/bsl_init.d -c $< -o $@
	@"$(PYTHON)" -c 'import os,sys,time; t=time.time(); [os.utime(p,(t,t)) for p in sys.argv[1:]]' $@ $(BUILD_DIR)/bsl/src/bsl_init.d
endif
endif
endif

ifeq ($(CONFIG_BSL),y)
ifeq ($(CONFIG_BSL_SRC),y)
ifeq ($(CONFIG_BSL_SRC_BSL_LOG),y)
LOCAL_OBJS += $(BUILD_DIR)/bsl/src/bsl_log.o
$(BUILD_DIR)/bsl/src/bsl_log.o: $(PROJECT_ROOT)/bsl/src/bsl_log.c $(FLAG_DEPS)
	@mkdir -p $(dir $@)
	@echo "CC $<"
	$(CC) $(CFLAGS) -MMD -MP -MF $(BUILD_DIR)/bsl/src/bsl_log.d -c $< -o $@
	@"$(PYTHON)" -c 'import os,sys,time; t=time.time(); [os.utime(p,(t,t)) for p in sys.argv[1:]]' $@ $(BUILD_DIR)/bsl/src/bsl_log.d
endif
endif
endif

ifeq ($(CONFIG_BSL),y)
ifeq ($(CONFIG_BSL_SRC),y)
ifeq ($(CONFIG_BSL_SRC_BSL_PCF8574),y)
LOCAL_OBJS += $(BUILD_DIR)/bsl/src/bsl_pcf8574.o
$(BUILD_DIR)/bsl/src/bsl_pcf8574.o: $(PROJECT_ROOT)/bsl/src/bsl_pcf8574.c $(FLAG_DEPS)
	@mkdir -p $(dir $@)
	@echo "CC $<"
	$(CC) $(CFLAGS) -MMD -MP -MF $(BUILD_DIR)/bsl/src/bsl_pcf8574.d -c $< -o $@
	@"$(PYTHON)" -c 'import os,sys,time; t=time.time(); [os.utime(p,(t,t)) for p in sys.argv[1:]]' $@ $(BUILD_DIR)/bsl/src/bsl_pcf8574.d
endif
endif
endif

.PHONY: all clean

all: $(LOCAL_OBJS)

clean:
	rm -rf $(BUILD_DIR)/bsl/src
