# 自动生成，勿手改。刷新：python3 build/build.py update
# 头文件目录和 .d 依赖由各层 Makefile 管理。
# 若 build/build.py install-toolchain 已安装到 tool/，优先使用工程内工具链。
# 可由 build/build.py set-buildenv 设置 STM32_TOOLCHAIN_ROOT；Windows 优先 tool/win_x86_64。
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
OPT ?= -Og
DEBUG ?= -g3
# 可复现构建：把工程根的绝对前缀折叠成 '.'。configASSERT 把 __FILE__ 传给 vAssertFailed，
# -g3 又把源码路径写进 debug 段，两者都会让同一提交在不同目录/机器编出不同字节的镜像
# （实测 Windows 与 Linux 的 .text 相差 136 字节，差异全部落在 .rodata 的路径字符串）。
REPRO := -ffile-prefix-map=$(PROJECT_ROOT)=.
# 编译期禁用 newlib 的分配入口：newlib 堆只有 512 B，而 __heap_end__ 到 _estack
# 之间约 159 KB 空闲 RAM（栈从 _estack 向下长，并不紧贴堆），大块 malloc 越界后
# 既不会撞到栈也不会报错。用 -include 注入，避免依赖每个编译单元记得包含；
# 不进 ASFLAGS（汇编没有这个概念）。守卫头自己先包含 <stdlib.h>/<string.h> 再 poison，
# 否则会在系统头的函数声明处报错而不是在真正的调用点报错。
ALLOC_GUARD := -include $(PROJECT_ROOT)/osl/inc/osl_alloc_guard.h
CPU_FLAGS := -mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard
CHIP_DEFS = $(foreach config,$(filter CONFIG_STM32%,$(.VARIABLES)),$(if $(filter y,$($(config))),-D$(subst XX,xx,$(patsubst CONFIG_%,%,$(config)))))
CFLAGS = $(CPU_FLAGS) $(CHIP_DEFS) $(OPT) $(DEBUG) $(REPRO) $(ALLOC_GUARD) -std=c11 -ffunction-sections -fdata-sections -fno-common -Wall -Wextra -Werror -fstack-usage $(INCLUDES)
CXXFLAGS = $(CPU_FLAGS) $(CHIP_DEFS) $(OPT) $(DEBUG) $(REPRO) $(ALLOC_GUARD) -std=c++17 -ffunction-sections -fdata-sections -fno-exceptions -fno-rtti -Wall -Wextra -Werror -fstack-usage $(INCLUDES)
ASFLAGS = $(CPU_FLAGS) $(CHIP_DEFS) $(OPT) $(DEBUG) $(REPRO) $(INCLUDES)
LDSCRIPT := $(PROJECT_ROOT)/hal/chip/linker/stm32f429igx_flash.ld
# -u _sbrk：make 在 --start-group 扫描时还没有 _sbrk 这个未定义符号（那时只有 malloc 未定义），
# 等 -lc 把 _sbrk_r 拉进来，-lnosys 的空桩已经先满足了它，libosl.a 里不会再有提取机会。
# 显式声明 _sbrk 为未定义，才会让 libosl.a 的那份强定义胜出；顺带保证它不被 --gc-sections 丢掉。
# -u __malloc_lock 同理：不强制引用的话，libc_nano.a 的 mlock.o（空锁）会先满足 malloc 的引用，
# 任务里并发 malloc 就会踩坏堆。
# -u _write 是为了让 osl 的 _write（把最近输出留在 g_last_write 里给调试器）真的进镜像：
# 当前没有任何调用方，--gc-sections 会把 _write 和 g_last_write 一起丢掉（实测 nm 里两者都无），
# 那样“留最近输出”就是空承诺。verify 会断言镜像里存在 _write。
LDFLAGS = $(CPU_FLAGS) -T$(LDSCRIPT) -nostartfiles -Wl,--gc-sections -Wl,-Map=$(TARGET_DIR)/$(PROJECT_NAME).map -Wl,-u,_sbrk -Wl,-u,__malloc_lock -Wl,-u,_write --specs=nano.specs --specs=nosys.specs
LDLIBS := -lc -lm -lgcc
