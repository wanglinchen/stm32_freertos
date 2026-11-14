# 自动生成，勿手改。刷新：python3 build/build.py update
PROJECT_ROOT := $(abspath $(dir $(lastword $(MAKEFILE_LIST)))/../../..)
BUILD_DIR := $(PROJECT_ROOT)/out/object
TARGET_DIR := $(BUILD_DIR)/target
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

FLAG_DEPS := $(wildcard $(PROJECT_ROOT)/toolchain.make) $(wildcard $(PROJECT_ROOT)/out/.config)

LOCAL_OBJS :=
-include $(BUILD_DIR)/osl/src/freertos/event_groups.d
-include $(BUILD_DIR)/osl/src/freertos/heap_4.d
-include $(BUILD_DIR)/osl/src/freertos/list.d
-include $(BUILD_DIR)/osl/src/freertos/port_m4.d
-include $(BUILD_DIR)/osl/src/freertos/queue.d
-include $(BUILD_DIR)/osl/src/freertos/stream_buffer.d
-include $(BUILD_DIR)/osl/src/freertos/tasks.d

ifeq ($(CONFIG_OSL),y)
ifeq ($(CONFIG_OSL_SRC),y)
ifeq ($(CONFIG_OSL_SRC_FREERTOS),y)
ifeq ($(CONFIG_OSL_SRC_FREERTOS_EVENT_GROUPS),y)
LOCAL_OBJS += $(BUILD_DIR)/osl/src/freertos/event_groups.o
$(BUILD_DIR)/osl/src/freertos/event_groups.o: $(PROJECT_ROOT)/osl/src/freertos/event_groups.c $(FLAG_DEPS)
	@mkdir -p $(dir $@)
	@echo "CC $<"
	$(CC) $(CFLAGS) -MMD -MP -MF $(BUILD_DIR)/osl/src/freertos/event_groups.d -c $< -o $@
	@"$(PYTHON)" -c 'import os,sys,time; t=time.time(); [os.utime(p,(t,t)) for p in sys.argv[1:]]' $@ $(BUILD_DIR)/osl/src/freertos/event_groups.d
endif
endif
endif
endif

ifeq ($(CONFIG_OSL),y)
ifeq ($(CONFIG_OSL_SRC),y)
ifeq ($(CONFIG_OSL_SRC_FREERTOS),y)
ifeq ($(CONFIG_OSL_SRC_FREERTOS_HEAP_4),y)
LOCAL_OBJS += $(BUILD_DIR)/osl/src/freertos/heap_4.o
$(BUILD_DIR)/osl/src/freertos/heap_4.o: $(PROJECT_ROOT)/osl/src/freertos/heap_4.c $(FLAG_DEPS)
	@mkdir -p $(dir $@)
	@echo "CC $<"
	$(CC) $(CFLAGS) -MMD -MP -MF $(BUILD_DIR)/osl/src/freertos/heap_4.d -c $< -o $@
	@"$(PYTHON)" -c 'import os,sys,time; t=time.time(); [os.utime(p,(t,t)) for p in sys.argv[1:]]' $@ $(BUILD_DIR)/osl/src/freertos/heap_4.d
endif
endif
endif
endif

ifeq ($(CONFIG_OSL),y)
ifeq ($(CONFIG_OSL_SRC),y)
ifeq ($(CONFIG_OSL_SRC_FREERTOS),y)
ifeq ($(CONFIG_OSL_SRC_FREERTOS_LIST),y)
LOCAL_OBJS += $(BUILD_DIR)/osl/src/freertos/list.o
$(BUILD_DIR)/osl/src/freertos/list.o: $(PROJECT_ROOT)/osl/src/freertos/list.c $(FLAG_DEPS)
	@mkdir -p $(dir $@)
	@echo "CC $<"
	$(CC) $(CFLAGS) -MMD -MP -MF $(BUILD_DIR)/osl/src/freertos/list.d -c $< -o $@
	@"$(PYTHON)" -c 'import os,sys,time; t=time.time(); [os.utime(p,(t,t)) for p in sys.argv[1:]]' $@ $(BUILD_DIR)/osl/src/freertos/list.d
endif
endif
endif
endif

ifeq ($(CONFIG_OSL),y)
ifeq ($(CONFIG_OSL_SRC),y)
ifeq ($(CONFIG_OSL_SRC_FREERTOS),y)
ifeq ($(CONFIG_OSL_SRC_FREERTOS_PORT_M4),y)
LOCAL_OBJS += $(BUILD_DIR)/osl/src/freertos/port_m4.o
$(BUILD_DIR)/osl/src/freertos/port_m4.o: $(PROJECT_ROOT)/osl/src/freertos/port_m4.c $(FLAG_DEPS)
	@mkdir -p $(dir $@)
	@echo "CC $<"
	$(CC) $(CFLAGS) -MMD -MP -MF $(BUILD_DIR)/osl/src/freertos/port_m4.d -c $< -o $@
	@"$(PYTHON)" -c 'import os,sys,time; t=time.time(); [os.utime(p,(t,t)) for p in sys.argv[1:]]' $@ $(BUILD_DIR)/osl/src/freertos/port_m4.d
endif
endif
endif
endif

ifeq ($(CONFIG_OSL),y)
ifeq ($(CONFIG_OSL_SRC),y)
ifeq ($(CONFIG_OSL_SRC_FREERTOS),y)
ifeq ($(CONFIG_OSL_SRC_FREERTOS_QUEUE),y)
LOCAL_OBJS += $(BUILD_DIR)/osl/src/freertos/queue.o
$(BUILD_DIR)/osl/src/freertos/queue.o: $(PROJECT_ROOT)/osl/src/freertos/queue.c $(FLAG_DEPS)
	@mkdir -p $(dir $@)
	@echo "CC $<"
	$(CC) $(CFLAGS) -MMD -MP -MF $(BUILD_DIR)/osl/src/freertos/queue.d -c $< -o $@
	@"$(PYTHON)" -c 'import os,sys,time; t=time.time(); [os.utime(p,(t,t)) for p in sys.argv[1:]]' $@ $(BUILD_DIR)/osl/src/freertos/queue.d
endif
endif
endif
endif

ifeq ($(CONFIG_OSL),y)
ifeq ($(CONFIG_OSL_SRC),y)
ifeq ($(CONFIG_OSL_SRC_FREERTOS),y)
ifeq ($(CONFIG_OSL_SRC_FREERTOS_STREAM_BUFFER),y)
LOCAL_OBJS += $(BUILD_DIR)/osl/src/freertos/stream_buffer.o
$(BUILD_DIR)/osl/src/freertos/stream_buffer.o: $(PROJECT_ROOT)/osl/src/freertos/stream_buffer.c $(FLAG_DEPS)
	@mkdir -p $(dir $@)
	@echo "CC $<"
	$(CC) $(CFLAGS) -MMD -MP -MF $(BUILD_DIR)/osl/src/freertos/stream_buffer.d -c $< -o $@
	@"$(PYTHON)" -c 'import os,sys,time; t=time.time(); [os.utime(p,(t,t)) for p in sys.argv[1:]]' $@ $(BUILD_DIR)/osl/src/freertos/stream_buffer.d
endif
endif
endif
endif

ifeq ($(CONFIG_OSL),y)
ifeq ($(CONFIG_OSL_SRC),y)
ifeq ($(CONFIG_OSL_SRC_FREERTOS),y)
ifeq ($(CONFIG_OSL_SRC_FREERTOS_TASKS),y)
LOCAL_OBJS += $(BUILD_DIR)/osl/src/freertos/tasks.o
$(BUILD_DIR)/osl/src/freertos/tasks.o: $(PROJECT_ROOT)/osl/src/freertos/tasks.c $(FLAG_DEPS)
	@mkdir -p $(dir $@)
	@echo "CC $<"
	$(CC) $(CFLAGS) -MMD -MP -MF $(BUILD_DIR)/osl/src/freertos/tasks.d -c $< -o $@
	@"$(PYTHON)" -c 'import os,sys,time; t=time.time(); [os.utime(p,(t,t)) for p in sys.argv[1:]]' $@ $(BUILD_DIR)/osl/src/freertos/tasks.d
endif
endif
endif
endif

.PHONY: all clean

all: $(LOCAL_OBJS)

clean:
	rm -rf $(BUILD_DIR)/osl/src/freertos
