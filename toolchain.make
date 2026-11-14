# 自动生成，勿手改。刷新：python3 build/build.py update
# 工具链：优先 STM32_TOOLCHAIN_ROOT，否则 tool/<平台>；include 由各层 Makefile 管。
ifdef STM32_TOOLCHAIN_ROOT
TOOLCHAIN_ROOT := $(STM32_TOOLCHAIN_ROOT)
else ifeq ($(OS),Windows_NT)
TOOLCHAIN_ROOT := $(firstword $(wildcard $(PROJECT_ROOT)/tool/win_x86_64) $(wildcard $(PROJECT_ROOT)/tool/arm-gnu-toolchain-*))
else
TOOLCHAIN_ROOT := $(firstword $(wildcard $(PROJECT_ROOT)/tool/linux_x86_64) $(wildcard $(PROJECT_ROOT)/tool/arm-gnu-toolchain-*))
endif
ifeq ($(OS),Windows_NT)
ifeq ($(wildcard $(TOOLCHAIN_ROOT)/bin/arm-none-eabi-gcc.exe),)
PREFIX := arm-none-eabi-
else
PREFIX := $(TOOLCHAIN_ROOT)/bin/arm-none-eabi-
endif
else
ifeq ($(wildcard $(TOOLCHAIN_ROOT)/bin/arm-none-eabi-gcc),)
PREFIX := arm-none-eabi-
else
PREFIX := $(TOOLCHAIN_ROOT)/bin/arm-none-eabi-
endif
endif
CC := $(PREFIX)gcc
CXX := $(PREFIX)g++
AS := $(PREFIX)gcc -x assembler-with-cpp
AR := $(PREFIX)ar
OBJCOPY := $(PREFIX)objcopy
GDB := $(PREFIX)gdb
SIZE := $(PREFIX)size
ifeq ($(OS),Windows_NT)
PYTHON := python
else
PYTHON := python3
endif
# 默认对齐 userdebug（-Og/-g3）；user 用 make user 或 build.py build（-Os/-g0）。?= 可被命令行覆盖。
OPT ?= -Og
DEBUG ?= -g3
REPRO := -ffile-prefix-map=$(PROJECT_ROOT)=.
ALLOC_GUARD := -include $(PROJECT_ROOT)/osl/inc/osl_alloc_guard.h
CPU_FLAGS := -mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard
CHIP_DEFS = $(foreach config,$(filter CONFIG_STM32%,$(.VARIABLES)),$(if $(filter y,$($(config))),-D$(subst XX,xx,$(patsubst CONFIG_%,%,$(config)))))
CFLAGS = $(CPU_FLAGS) $(CHIP_DEFS) $(OPT) $(DEBUG) $(REPRO) $(ALLOC_GUARD) -std=c11 -ffunction-sections -fdata-sections -fno-common -Wall -Wextra -Werror -fstack-usage $(INCLUDES)
CXXFLAGS = $(CPU_FLAGS) $(CHIP_DEFS) $(OPT) $(DEBUG) $(REPRO) $(ALLOC_GUARD) -std=c++17 -ffunction-sections -fdata-sections -fno-exceptions -fno-rtti -Wall -Wextra -Werror -fstack-usage $(INCLUDES)
ASFLAGS = $(CPU_FLAGS) $(CHIP_DEFS) $(OPT) $(DEBUG) $(REPRO) $(INCLUDES)
LDSCRIPT := $(PROJECT_ROOT)/hal/chip/linker/stm32f429igx_flash.ld
# 链接强制引用 _sbrk / __malloc_lock / _write，避免 --gc-sections 与 libc 空桩抢先。
LDFLAGS = $(CPU_FLAGS) -T$(LDSCRIPT) -nostartfiles -Wl,--gc-sections -Wl,-Map=$(TARGET_DIR)/$(PROJECT_NAME).map -Wl,-u,_sbrk -Wl,-u,__malloc_lock -Wl,-u,_write --specs=nano.specs --specs=nosys.specs
LDLIBS := -lc -lm -lgcc
