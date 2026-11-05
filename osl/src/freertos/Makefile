# 自动生成，勿手改。刷新：python3 build/create-project.py update
PROJECT_ROOT := $(abspath $(dir $(lastword $(MAKEFILE_LIST)))/../../..)
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
-include $(BUILD_DIR)/osl/src/freertos/event_groups.d
-include $(BUILD_DIR)/osl/src/freertos/list.d
-include $(BUILD_DIR)/osl/src/freertos/queue.d
-include $(BUILD_DIR)/osl/src/freertos/stream_buffer.d
-include $(BUILD_DIR)/osl/src/freertos/tasks.d
-include $(BUILD_DIR)/osl/src/freertos/timers.d

ifeq ($(CONFIG_OSL),y)
ifeq ($(CONFIG_OSL_SRC),y)
ifeq ($(CONFIG_OSL_SRC_FREERTOS),y)
LOCAL_OBJS += $(BUILD_DIR)/osl/src/freertos/event_groups.o
$(BUILD_DIR)/osl/src/freertos/event_groups.o: $(PROJECT_ROOT)/osl/src/freertos/event_groups.c
	@mkdir -p $(dir $@)
	@echo "CC $<"
	$(CC) $(CFLAGS) -MMD -MP -MF $(BUILD_DIR)/osl/src/freertos/event_groups.d -c $< -o $@
	@$(PYTHON) -c 'import os,sys,time; t=time.time(); [os.utime(p,(t,t)) for p in sys.argv[1:]]' $@ $(BUILD_DIR)/osl/src/freertos/event_groups.d
endif
endif
endif

ifeq ($(CONFIG_OSL),y)
ifeq ($(CONFIG_OSL_SRC),y)
ifeq ($(CONFIG_OSL_SRC_FREERTOS),y)
LOCAL_OBJS += $(BUILD_DIR)/osl/src/freertos/list.o
$(BUILD_DIR)/osl/src/freertos/list.o: $(PROJECT_ROOT)/osl/src/freertos/list.c
	@mkdir -p $(dir $@)
	@echo "CC $<"
	$(CC) $(CFLAGS) -MMD -MP -MF $(BUILD_DIR)/osl/src/freertos/list.d -c $< -o $@
	@$(PYTHON) -c 'import os,sys,time; t=time.time(); [os.utime(p,(t,t)) for p in sys.argv[1:]]' $@ $(BUILD_DIR)/osl/src/freertos/list.d
endif
endif
endif

ifeq ($(CONFIG_OSL),y)
ifeq ($(CONFIG_OSL_SRC),y)
ifeq ($(CONFIG_OSL_SRC_FREERTOS),y)
LOCAL_OBJS += $(BUILD_DIR)/osl/src/freertos/queue.o
$(BUILD_DIR)/osl/src/freertos/queue.o: $(PROJECT_ROOT)/osl/src/freertos/queue.c
	@mkdir -p $(dir $@)
	@echo "CC $<"
	$(CC) $(CFLAGS) -MMD -MP -MF $(BUILD_DIR)/osl/src/freertos/queue.d -c $< -o $@
	@$(PYTHON) -c 'import os,sys,time; t=time.time(); [os.utime(p,(t,t)) for p in sys.argv[1:]]' $@ $(BUILD_DIR)/osl/src/freertos/queue.d
endif
endif
endif

ifeq ($(CONFIG_OSL),y)
ifeq ($(CONFIG_OSL_SRC),y)
ifeq ($(CONFIG_OSL_SRC_FREERTOS),y)
LOCAL_OBJS += $(BUILD_DIR)/osl/src/freertos/stream_buffer.o
$(BUILD_DIR)/osl/src/freertos/stream_buffer.o: $(PROJECT_ROOT)/osl/src/freertos/stream_buffer.c
	@mkdir -p $(dir $@)
	@echo "CC $<"
	$(CC) $(CFLAGS) -MMD -MP -MF $(BUILD_DIR)/osl/src/freertos/stream_buffer.d -c $< -o $@
	@$(PYTHON) -c 'import os,sys,time; t=time.time(); [os.utime(p,(t,t)) for p in sys.argv[1:]]' $@ $(BUILD_DIR)/osl/src/freertos/stream_buffer.d
endif
endif
endif

ifeq ($(CONFIG_OSL),y)
ifeq ($(CONFIG_OSL_SRC),y)
ifeq ($(CONFIG_OSL_SRC_FREERTOS),y)
LOCAL_OBJS += $(BUILD_DIR)/osl/src/freertos/tasks.o
$(BUILD_DIR)/osl/src/freertos/tasks.o: $(PROJECT_ROOT)/osl/src/freertos/tasks.c
	@mkdir -p $(dir $@)
	@echo "CC $<"
	$(CC) $(CFLAGS) -MMD -MP -MF $(BUILD_DIR)/osl/src/freertos/tasks.d -c $< -o $@
	@$(PYTHON) -c 'import os,sys,time; t=time.time(); [os.utime(p,(t,t)) for p in sys.argv[1:]]' $@ $(BUILD_DIR)/osl/src/freertos/tasks.d
endif
endif
endif

ifeq ($(CONFIG_OSL),y)
ifeq ($(CONFIG_OSL_SRC),y)
ifeq ($(CONFIG_OSL_SRC_FREERTOS),y)
LOCAL_OBJS += $(BUILD_DIR)/osl/src/freertos/timers.o
$(BUILD_DIR)/osl/src/freertos/timers.o: $(PROJECT_ROOT)/osl/src/freertos/timers.c
	@mkdir -p $(dir $@)
	@echo "CC $<"
	$(CC) $(CFLAGS) -MMD -MP -MF $(BUILD_DIR)/osl/src/freertos/timers.d -c $< -o $@
	@$(PYTHON) -c 'import os,sys,time; t=time.time(); [os.utime(p,(t,t)) for p in sys.argv[1:]]' $@ $(BUILD_DIR)/osl/src/freertos/timers.d
endif
endif
endif

.PHONY: all clean
all: $(LOCAL_OBJS)
	@$(MAKE) --no-print-directory -C $(PROJECT_ROOT)/osl/src/freertos/portable all

clean:
	rm -rf $(BUILD_DIR)/osl/src/freertos
