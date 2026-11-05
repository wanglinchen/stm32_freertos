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
# 编译规则收尾（回写时间戳）用的解释器：Windows 的 MSYS sh 里 python3 多为失效别名。
ifeq ($(OS),Windows_NT)
PYTHON := python
else
PYTHON := python3
endif
OPT := -Og
DEBUG := -g3
CPU_FLAGS := -mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard
CHIP_DEFS = $(foreach config,$(filter CONFIG_STM32%,$(.VARIABLES)),$(if $(filter y,$($(config))),-D$(subst XX,xx,$(patsubst CONFIG_%,%,$(config)))))
CFLAGS = $(CPU_FLAGS) $(CHIP_DEFS) $(OPT) $(DEBUG) -std=c11 -ffunction-sections -fdata-sections -fno-common -Wall $(INCLUDES)
CXXFLAGS = $(CPU_FLAGS) $(CHIP_DEFS) $(OPT) $(DEBUG) -std=c++17 -ffunction-sections -fdata-sections -fno-exceptions -fno-rtti -Wall $(INCLUDES)
ASFLAGS = $(CPU_FLAGS) $(CHIP_DEFS) $(OPT) $(DEBUG) $(INCLUDES)
LDSCRIPT := $(PROJECT_ROOT)/hal/chip/linker/stm32f429igx_flash.ld
# -u _sbrk：make 在 --start-group 扫描时还没有 _sbrk 这个未定义符号（那时只有 malloc 未定义），
# 等 -lc 把 _sbrk_r 拉进来，-lnosys 的空桩已经先满足了它，libosl.a 里不会再有提取机会。
# 显式声明 _sbrk 为未定义，才会让 libosl.a 的那份强定义胜出；顺带保证它不被 --gc-sections 丢掉。
# -u __malloc_lock 同理：不强制引用的话，libc_nano.a 的 mlock.o（空锁）会先满足 malloc 的引用，
# 任务里并发 malloc 就会踩坏堆。
LDFLAGS = $(CPU_FLAGS) -T$(LDSCRIPT) -nostartfiles -Wl,--gc-sections -Wl,-Map=$(BUILD_DIR)/$(PROJECT_NAME).map -Wl,-u,_sbrk -Wl,-u,__malloc_lock --specs=nano.specs --specs=nosys.specs
LDLIBS := -lc -lm -lgcc
