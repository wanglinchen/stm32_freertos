# 自动生成，勿手改。刷新：python3 build/create-project.py update
# 头文件目录和 .d 依赖由各层 Makefile 管理。
# 若 build/install-toolchain.py 已安装到 tool/，优先使用工程内工具链。
# 可由 build/set-buildenv.py 设置 STM32_TOOLCHAIN_ROOT；Windows 优先 tool/win_x86_64。
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
OPT := -Og
DEBUG := -g3
CPU_FLAGS := -mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard
CHIP_DEFS = $(foreach config,$(filter CONFIG_STM32%,$(.VARIABLES)),$(if $(filter y,$($(config))),-D$(subst XX,xx,$(patsubst CONFIG_%,%,$(config)))))
CFLAGS = $(CPU_FLAGS) $(CHIP_DEFS) $(OPT) $(DEBUG) -std=c11 -ffunction-sections -fdata-sections -fno-common -Wall $(INCLUDES)
CXXFLAGS = $(CPU_FLAGS) $(CHIP_DEFS) $(OPT) $(DEBUG) -std=c++17 -ffunction-sections -fdata-sections -fno-exceptions -fno-rtti -Wall $(INCLUDES)
ASFLAGS = $(CPU_FLAGS) $(CHIP_DEFS) $(OPT) $(DEBUG) $(INCLUDES)
LDSCRIPT := $(PROJECT_ROOT)/hal/chip/linker/stm32f429igx_flash.ld
LDFLAGS = $(CPU_FLAGS) -T$(LDSCRIPT) -nostartfiles -Wl,--gc-sections --specs=nano.specs --specs=nosys.specs
LDLIBS := -lc -lm -lgcc
