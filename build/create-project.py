#!/usr/bin/env python3
"""按源码树生成 Kconfig、Makefile、defconfig 和 .config，并执行 build / update / clean。

  python3 build/create-project.py build     # 先 update（含自检），再编译，产物拷到 out/target
  python3 build/create-project.py update    # 刷新描述文件并自检。这不是传给 make 的参数
  python3 build/create-project.py verify    # 仅自检代码生成物与 .skills 规格
  python3 build/create-project.py clean     # 删除 out/
"""
from __future__ import annotations

import argparse
import json
import os
import re
import shutil
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
_BUILD = Path(__file__).resolve().parent
def _load_setenv():
    import importlib.util

    path = _BUILD / "set-buildenv.py"
    spec = importlib.util.spec_from_file_location("setenv", path)
    if spec is None or spec.loader is None:
        raise ImportError(f"无法加载 {path}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


setenv = _load_setenv()

# 指南要求跳过这些目录。out 是产物；点目录和 __pycache__ 不是源码模块。
EXCLUDE_DIRS = {
    "doc",
    "proj",
    "tool",
    "build",
    "workspace",
    "out",
    # release 是打包输出（release/<日期>/*.bin/.elf/.hex/.map），与 out 同类：
    # 由构建/发版脚本产生，不参与源码树扫描，也不该进版本控制。
    "release",
    "__pycache__",
}
SOURCE_SUFFIXES = {".c", ".cpp", ".cc", ".cxx", ".s", ".S"}
HEADER_SUFFIXES = {".h", ".hpp", ".hh"}
STATIC_LIB_DIRS = ("osl", "apl", "fml", "bsl", "hal")
# 静态库顺序必须“消费者在前、提供者在后”：ld 单遍扫描归档，只解析当时仍未定义的符号。
# apl 依赖 fml/osl，fml 依赖 bsl/osl/hal，故 apl 必须排最前、hal 排最后。
NORMAL_LINK_LIBS = ("apl", "fml", "bsl", "osl", "hal")
LAYER_DEPENDENCIES = {
    "apl": ("fml", "osl"),
    "fml": ("bsl", "hal", "osl"),
    "bsl": ("hal",),
    "osl": ("hal",),
}
LAYER_DIRS = (
    "apl",
    "fml",
    "bsl",
    "hal",
    "osl",
    "doc",
    "proj",
    "build",
    "tool",
    "workspace",
)
USER_BEGIN = "# >>> USER_CUSTOM"
USER_END = "# <<< USER_CUSTOM"
GEN_BANNER = "# 自动生成，勿手改。刷新：python3 build/create-project.py update"
ASSIGN_RE = re.compile(r"^\s*(CONFIG_[A-Z0-9_]+)\s*=")
CONFIG_VALUE_RE = re.compile(r"^\s*(CONFIG_[A-Z0-9_]+)\s*=\s*(.*?)\s*$")
DISABLED_RE = re.compile(r"^\s*#\s*(CONFIG_[A-Z0-9_]+)\s+is not set\s*$")
NAME_SEP_RE = re.compile(r"[^a-z0-9]+")


def same_project_name(left: str, right: str) -> bool:
    """工程名比较：忽略大小写，并把 -、_、空格视为同一分隔符。

    目录名不构成工程身份——clone 到 stm32_freertos/ 或任意路径都是同一份工程。
    仓库名习惯用下划线、本地目录习惯用连字符，归一化后必须判定为同一个名字，
    否则每次在 CI 里检出都会误报"工程名与目录名不一致"。
    """
    return NAME_SEP_RE.sub("-", left.casefold()).strip("-") == NAME_SEP_RE.sub(
        "-", right.casefold()
    ).strip("-")


def default_info() -> dict:
    return {
        "project_name": ROOT.name,
        "cpu": {
            "model": "STM32F429IGT6",
            "arch": "cortex-m4",
            "fpu": "fpv4-sp-d16",
            "float_abi": "hard",
            "flash_kb": 2048,
            "sram_kb": 192,
            "ccm_kb": 64,
        },
    }


def ensure_layout() -> None:
    for name in LAYER_DIRS:
        (ROOT / name).mkdir(parents=True, exist_ok=True)


def load_info() -> dict:
    path = ROOT / "proj" / "project-info.json"
    if not path.is_file():
        path.write_text(json.dumps(default_info(), indent=2) + "\n", encoding="utf-8")
    data = json.loads(path.read_text(encoding="utf-8"))
    cpu = data.setdefault("cpu", {})
    base = default_info()
    data.setdefault("project_name", base["project_name"])
    for key, value in base["cpu"].items():
        cpu.setdefault(key, value)
    return data


def linker_script_path(info: dict) -> Path:
    directory = ROOT / "hal" / "chip" / "linker"
    scripts = sorted(directory.glob("*.ld")) if directory.is_dir() else []
    if not scripts:
        raise FileNotFoundError(f"未在 {directory.relative_to(ROOT)} 找到链接脚本（*.ld）")
    model = info.get("cpu", {}).get("model", "").upper()
    family = model[5:9].lower() if model.startswith("STM32F") and len(model) >= 9 else ""
    matching = [script for script in scripts if family and family in script.name.lower()]
    candidates = matching or scripts
    if len(candidates) != 1:
        names = "、".join(script.name for script in candidates)
        raise ValueError(
            f"{directory.relative_to(ROOT)} 中有多个适用链接脚本：{names}；请保留唯一匹配 {model} 的脚本"
        )
    return candidates[0]


def is_skipped(path: Path) -> bool:
    name = path.name
    if name.startswith(".") or name in EXCLUDE_DIRS:
        return True
    try:
        rel = path.relative_to(ROOT)
    except ValueError:
        return True
    return any(part.startswith(".") or part in EXCLUDE_DIRS for part in rel.parts)


def is_linker_script_dir(directory: Path) -> bool:
    return any(
        child.is_file() and child.suffix.lower() == ".ld"
        for child in directory.iterdir()
    )


def child_dirs(directory: Path) -> list[Path]:
    if not directory.is_dir():
        return []
    found = []
    for child in sorted(directory.iterdir(), key=lambda p: p.name):
        if (
            child.is_dir()
            and not is_skipped(child)
            and not is_linker_script_dir(child)
            and not is_empty_source_dir(child)
        ):
            found.append(child)
    return found


def is_empty_source_dir(directory: Path) -> bool:
    if directory.name != "src" or list_sources(directory):
        return False
    return not any(
        child.is_dir() and not is_skipped(child)
        for child in directory.iterdir()
    )


def list_sources(directory: Path) -> list[Path]:
    if not directory.is_dir():
        return []
    return sorted(
        (
            child
            for child in directory.iterdir()
            if child.is_file() and child.suffix in SOURCE_SUFFIXES
        ),
        key=lambda p: p.name,
    )


def walk_dirs(root: Path = ROOT, *, include_linker_script_dirs: bool = False) -> list[Path]:
    found = [root]
    if include_linker_script_dirs:
        stack = [
            child
            for child in sorted(root.iterdir(), key=lambda p: p.name)
            if child.is_dir() and not is_skipped(child)
        ]
    else:
        stack = child_dirs(root)
    while stack:
        current = stack.pop()
        found.append(current)
        if include_linker_script_dirs:
            children = [
                child
                for child in sorted(current.iterdir(), key=lambda p: p.name)
                if child.is_dir() and not is_skipped(child)
            ]
            stack.extend(reversed(children))
        else:
            stack.extend(reversed(child_dirs(current)))
    return found


def sanitize(name: str) -> str:
    text = re.sub(r"[^0-9A-Za-z]+", "_", name.upper()).strip("_") or "UNNAMED"
    text = re.sub(r"_+", "_", text)
    if text[0].isdigit():
        text = "N_" + text
    return text


def dir_symbol(directory: Path) -> str:
    parts = [sanitize(part) for part in directory.relative_to(ROOT).parts]
    return "CONFIG_" + "_".join(parts)


def source_symbols(directory: Path) -> list[tuple[Path, str]]:
    sources = list_sources(directory)
    stems: dict[str, int] = {}
    for source in sources:
        stems[source.stem] = stems.get(source.stem, 0) + 1
    parent = [sanitize(part) for part in directory.relative_to(ROOT).parts]
    pairs = []
    for source in sources:
        if stems[source.stem] > 1:
            stem = sanitize(f"{source.stem}_{source.suffix.lstrip('.')}")
        else:
            stem = sanitize(source.stem)
        pairs.append((source, "CONFIG_" + "_".join(parent + [stem])))
    return pairs


def source_guards(source: Path) -> list[str]:
    guards = []
    parent = source.parent
    parts = parent.relative_to(ROOT).parts
    for index in range(1, len(parts) + 1):
        guards.append(dir_symbol(ROOT.joinpath(*parts[:index])))
    if not child_dirs(parent):
        for candidate, symbol in source_symbols(parent):
            if candidate == source:
                guards.append(symbol)
                break
    return guards


def iter_sources_under(directory: Path) -> list[Path]:
    found = list_sources(directory)
    for child in child_dirs(directory):
        found.extend(iter_sources_under(child))
    return found


def cpu_flags(info: dict) -> str:
    cpu = info["cpu"]
    flags = [f"-mcpu={cpu['arch']}", "-mthumb"]
    fpu = cpu.get("fpu") or ""
    abi = cpu.get("float_abi") or ""
    if fpu and abi and abi != "soft":
        flags.append(f"-mfpu={fpu}")
        flags.append(f"-mfloat-abi={abi}")
    return " ".join(flags)


def local_toolchain_bin() -> Path | None:
    return setenv.find_toolchain_bin()


def write_toolchain(info: dict) -> None:
    script = linker_script_path(info)
    rel = script.relative_to(ROOT).as_posix()
    lines = [
        GEN_BANNER,
        "# 头文件目录和 .d 依赖由各层 Makefile 管理。",
        "# 若 build/install-toolchain.py 已安装到 tool/，优先使用工程内工具链。",
        "# 可由 build/set-buildenv.py 设置 STM32_TOOLCHAIN_ROOT；Windows 优先 tool/win_x86_64。",
        "ifdef STM32_TOOLCHAIN_ROOT",
        "TOOLCHAIN_ROOT := $(STM32_TOOLCHAIN_ROOT)",
        "else ifeq ($(OS),Windows_NT)",
        "TOOLCHAIN_ROOT := $(firstword $(wildcard $(PROJECT_ROOT)/tool/win_x86_64) $(wildcard $(PROJECT_ROOT)/tool/arm-gnu-toolchain-*))",
        "else",
        "TOOLCHAIN_ROOT := $(firstword $(wildcard $(PROJECT_ROOT)/tool/linux_x86_64) $(wildcard $(PROJECT_ROOT)/tool/arm-gnu-toolchain-*))",
        "endif",
        "ifeq ($(OS),Windows_NT)",
        "ifeq ($(wildcard $(TOOLCHAIN_ROOT)/bin/arm-none-eabi-gcc.exe),)",
        "PREFIX := arm-none-eabi-",
        "else",
        "PREFIX := $(TOOLCHAIN_ROOT)/bin/arm-none-eabi-",
        "endif",
        "else",
        "ifeq ($(wildcard $(TOOLCHAIN_ROOT)/bin/arm-none-eabi-gcc),)",
        "PREFIX := arm-none-eabi-",
        "else",
        "PREFIX := $(TOOLCHAIN_ROOT)/bin/arm-none-eabi-",
        "endif",
        "endif",
        "CC := $(PREFIX)gcc",
        "CXX := $(PREFIX)g++",
        "AS := $(PREFIX)gcc -x assembler-with-cpp",
        "AR := $(PREFIX)ar",
        "OBJCOPY := $(PREFIX)objcopy",
        "GDB := $(PREFIX)gdb",
        "SIZE := $(PREFIX)size",
        "# 编译规则收尾（回写时间戳）用的解释器：Windows 的 MSYS sh 里 python3 多为失效别名。",
        "ifeq ($(OS),Windows_NT)",
        "PYTHON := python",
        "else",
        "PYTHON := python3",
        "endif",
        "OPT := -Og",
        "DEBUG := -g3",
        "# 可复现构建：把工程根的绝对前缀折叠成 '.'。configASSERT 把 __FILE__ 传给 vAssertFailed，",
        "# -g3 又把源码路径写进 debug 段，两者都会让同一提交在不同目录/机器编出不同字节的镜像",
        "# （实测 Windows 与 Linux 的 .text 相差 136 字节，差异全部落在 .rodata 的路径字符串）。",
        "REPRO := -ffile-prefix-map=$(PROJECT_ROOT)=.",
        "# 编译期禁用 newlib 的分配入口：newlib 堆只有 512 B 且紧贴 1 KB 栈，",
        "# 任何大块 malloc 都会静默踩栈。用 -include 注入，避免依赖每个编译单元记得包含；",
        "# 不进 ASFLAGS（汇编没有这个概念）。守卫头自己先包含 <stdlib.h>/<string.h> 再 poison，",
        "# 否则会在系统头的函数声明处报错而不是在真正的调用点报错。",
        "ALLOC_GUARD := -include $(PROJECT_ROOT)/osl/inc/osl_alloc_guard.h",
        f"CPU_FLAGS := {cpu_flags(info)}",
        "CHIP_DEFS = $(foreach config,$(filter CONFIG_STM32%,$(.VARIABLES)),$(if $(filter y,$($(config))),-D$(subst XX,xx,$(patsubst CONFIG_%,%,$(config)))))",
        "CFLAGS = $(CPU_FLAGS) $(CHIP_DEFS) $(OPT) $(DEBUG) $(REPRO) $(ALLOC_GUARD) -std=c11 -ffunction-sections -fdata-sections -fno-common -Wall $(INCLUDES)",
        "CXXFLAGS = $(CPU_FLAGS) $(CHIP_DEFS) $(OPT) $(DEBUG) $(REPRO) $(ALLOC_GUARD) -std=c++17 -ffunction-sections -fdata-sections -fno-exceptions -fno-rtti -Wall $(INCLUDES)",
        "ASFLAGS = $(CPU_FLAGS) $(CHIP_DEFS) $(OPT) $(DEBUG) $(REPRO) $(INCLUDES)",
        f"LDSCRIPT := $(PROJECT_ROOT)/{rel}",
        "# -u _sbrk：make 在 --start-group 扫描时还没有 _sbrk 这个未定义符号（那时只有 malloc 未定义），",
        "# 等 -lc 把 _sbrk_r 拉进来，-lnosys 的空桩已经先满足了它，libosl.a 里不会再有提取机会。",
        "# 显式声明 _sbrk 为未定义，才会让 libosl.a 的那份强定义胜出；顺带保证它不被 --gc-sections 丢掉。",
        "# -u __malloc_lock 同理：不强制引用的话，libc_nano.a 的 mlock.o（空锁）会先满足 malloc 的引用，",
        "# 任务里并发 malloc 就会踩坏堆。",
        "LDFLAGS = $(CPU_FLAGS) -T$(LDSCRIPT) -nostartfiles -Wl,--gc-sections -Wl,-Map=$(BUILD_DIR)/$(PROJECT_NAME).map "
        "-Wl,-u,_sbrk -Wl,-u,__malloc_lock --specs=nano.specs --specs=nosys.specs",
        "LDLIBS := -lc -lm -lgcc",
        "",
    ]
    write_generated_text(ROOT / "toolchain.make", "\n".join(lines))


def header_dirs() -> list[Path]:
    found = []
    for directory in walk_dirs():
        if any(child.is_file() and child.suffix in HEADER_SUFFIXES for child in directory.iterdir()):
            found.append(directory)
    return found


def kconfig_block(symbol: str, prompt: str) -> list[str]:
    name = symbol.removeprefix("CONFIG_")
    return [
        f"config {name}",
        f'\tbool "{prompt}"',
        "\tdefault y",
    ]


def write_kconfig(directory: Path, project_name: str) -> None:
    children = child_dirs(directory)
    sources = list_sources(directory)
    lines = [GEN_BANNER]
    if directory == ROOT:
        lines.append(f'mainmenu "{project_name}"')
        lines.append("")
        for child in children:
            symbol = dir_symbol(child)
            lines.extend(kconfig_block(symbol, child.name))
            lines.append(f"if {symbol.removeprefix('CONFIG_')}")
            rel = child.relative_to(ROOT).as_posix()
            lines.append(f'source "{rel}/Kconfig"')
            lines.append("endif")
            lines.append("")
    else:
        lines.append(f'menu "{directory.name}"')
        if children:
            for child in children:
                symbol = dir_symbol(child)
                lines.append("")
                lines.extend(kconfig_block(symbol, child.name))
                lines.append(f"if {symbol.removeprefix('CONFIG_')}")
                rel = child.relative_to(ROOT).as_posix()
                lines.append(f'source "{rel}/Kconfig"')
                lines.append("endif")
        elif sources:
            for source, symbol in source_symbols(directory):
                lines.append("")
                lines.extend(kconfig_block(symbol, source.name))
        else:
            lines.append("")
            lines.append('comment "无子目录、无源文件"')
        lines.append("")
        lines.append("endmenu")
        lines.append("")
    write_generated_text(directory / "Kconfig", "\n".join(lines))


def project_root_expr(directory: Path) -> str:
    if directory == ROOT:
        return "$(abspath $(dir $(lastword $(MAKEFILE_LIST))))"
    ups = "/".join([".."] * len(directory.relative_to(ROOT).parts))
    return f"$(abspath $(dir $(lastword $(MAKEFILE_LIST)))/{ups})"


def object_path(source: Path) -> str:
    rel = source.relative_to(ROOT)
    suffix = ""
    if is_asm_source(source) and any(
        candidate.stem == source.stem and not is_asm_source(candidate)
        for candidate in list_sources(source.parent)
    ):
        suffix = "_asm"
    return f"$(BUILD_DIR)/{rel.parent.as_posix()}/{source.stem}{suffix}.o"


def dep_path(source: Path) -> str:
    rel = source.relative_to(ROOT)
    suffix = ""
    if is_asm_source(source) and any(
        candidate.stem == source.stem and not is_asm_source(candidate)
        for candidate in list_sources(source.parent)
    ):
        suffix = "_asm"
    return f"$(BUILD_DIR)/{rel.parent.as_posix()}/{source.stem}{suffix}.d"


def is_asm_source(source: Path) -> bool:
    return source.suffix in {".s", ".S"}


def startup_asm_sources() -> list[Path]:
    return [
        source
        for source in iter_sources_under(ROOT / "hal")
        if is_asm_source(source) and source.name.startswith("startup_")
    ]


def startup_asm_objects() -> list[str]:
    return [object_path(source) for source in startup_asm_sources()]


def layer_dependencies(modules: list[str]) -> dict[str, tuple[str, ...]]:
    available = set(modules)
    dependencies = {
        name: tuple(dep for dep in LAYER_DEPENDENCIES.get(name, ()) if dep in available)
        for name in modules
    }
    visiting: set[str] = set()
    visited: set[str] = set()

    def visit(name: str) -> None:
        if name in visiting:
            raise ValueError(f"层依赖存在循环：{name}")
        if name in visited:
            return
        visiting.add(name)
        for dependency in dependencies[name]:
            visit(dependency)
        visiting.remove(name)
        visited.add(name)

    for name in modules:
        visit(name)
    return dependencies


def transitive_layer_dependencies(
    layer: str, dependencies: dict[str, tuple[str, ...]]
) -> list[str]:
    ordered: list[str] = []
    visited: set[str] = set()

    def visit(name: str) -> None:
        for dependency in dependencies.get(name, ()):
            if dependency in visited:
                continue
            visited.add(dependency)
            ordered.append(dependency)
            visit(dependency)

    visit(layer)
    return ordered


def layer_header_dirs(layer: str) -> list[Path]:
    modules = active_modules()
    dependencies = layer_dependencies(modules)
    visible_layers = {layer, *transitive_layer_dependencies(layer, dependencies)}
    return [
        directory
        for directory in header_dirs()
        if not directory.relative_to(ROOT).parts
        or directory.relative_to(ROOT).parts[0] in visible_layers
    ]


def compile_tool(source: Path) -> tuple[str, str, str]:
    if source.suffix == ".c":
        return "CC", "$(CC) $(CFLAGS)", dep_path(source)
    if source.suffix in {".cpp", ".cc", ".cxx"}:
        return "CXX", "$(CXX) $(CXXFLAGS)", dep_path(source)
    return "AS", "$(AS) $(ASFLAGS)", dep_path(source)


def guarded_lines(guards: list[str], body: list[str]) -> list[str]:
    lines: list[str] = []
    for guard in guards:
        lines.append(f"ifeq ($({guard}),y)")
    lines.extend(body)
    for _guard in guards:
        lines.append("endif")
    return lines


def write_dir_makefile(directory: Path) -> None:
    """生成目录 Makefile。静态库目录在本层打包，子目录只编译自己的源文件。"""
    rel = directory.relative_to(ROOT)
    is_lib = len(rel.parts) == 1 and rel.parts[0] in STATIC_LIB_DIRS
    sources = list_sources(directory)
    lines = [
        GEN_BANNER,
        f"PROJECT_ROOT := {project_root_expr(directory)}",
        "BUILD_DIR := $(PROJECT_ROOT)/out/object",
        ".DEFAULT_GOAL := all",
        "include $(PROJECT_ROOT)/toolchain.make",
        "-include $(PROJECT_ROOT)/out/.config",
        "",
        "INCLUDE_DIRS :=",
    ]
    for include_dir in layer_header_dirs(rel.parts[0]):
        include_rel = include_dir.relative_to(ROOT).as_posix()
        if include_rel == ".":
            lines.append("INCLUDE_DIRS += $(PROJECT_ROOT)")
        else:
            lines.append(f"INCLUDE_DIRS += $(PROJECT_ROOT)/{include_rel}")
    lines += [
        "INCLUDES := $(addprefix -I,$(INCLUDE_DIRS))",
        "",
        # 编译参数（toolchain.make）和功能裁剪（out/.config）一改变，所有 .o 都必须重编：
        # 只依赖源文件会让“改了 flags 却复用旧对象”静默混编。
        # 用 wildcard 而不是直接列关键字：clean 之后 out/.config 不存在，
        # 直接列路径会让 make 报 "No rule to make target ... out/.config"。
        "FLAG_DEPS := $(wildcard $(PROJECT_ROOT)/toolchain.make) "
        "$(wildcard $(PROJECT_ROOT)/out/.config)",
        "",
        "LOCAL_OBJS :=",
    ]
    for source in sources:
        lines.append(f"-include {dep_path(source)}")
    if sources:
        lines.append("")
    if is_lib:
        lines.append("ARCHIVE_OBJS :=")
        startup_sources = set(startup_asm_sources())
        for source in iter_sources_under(directory):
            if source in startup_sources:
                continue
            lines.extend(
                guarded_lines(source_guards(source), [f"ARCHIVE_OBJS += {object_path(source)}"])
            )
        lines.append("")
    for source in sources:
        obj = object_path(source)
        tag, tool, dep = compile_tool(source)
        src = f"$(PROJECT_ROOT)/{source.relative_to(ROOT).as_posix()}"
        body = [
            f"LOCAL_OBJS += {obj}",
            f"{obj}: {src} $(FLAG_DEPS)",
            "\t@mkdir -p $(dir $@)",
            f'\t@echo "{tag} $<"',
            f"\t{tool} -MMD -MP -MF {dep} -c $< -o $@",
            "\t@$(PYTHON) -c 'import os,sys,time; t=time.time(); [os.utime(p,(t,t)) for p in sys.argv[1:]]' $@ " + dep,
        ]
        lines.extend(guarded_lines(source_guards(source), body))
        lines.append("")
    lines.append(".PHONY: all clean")
    lines.append("all: $(LOCAL_OBJS)")
    for child in child_dirs(directory):
        child_dir = f"$(PROJECT_ROOT)/{child.relative_to(ROOT).as_posix()}"
        lines.append(f"\t@$(MAKE) --no-print-directory -C {child_dir} all")
    if is_lib:
        lib = f"$(BUILD_DIR)/lib/lib{rel.parts[0]}.a"
        lines.append(f"\t@mkdir -p $(dir {lib})")
        lines.append(f'\t@echo "AR {lib}"')
        lines.append(f"\t@rm -f {lib}")
        lines.append(f"\t$(AR) rcs {lib} $(ARCHIVE_OBJS)")
    lines.append("")
    lines.append("clean:")
    if is_lib:
        lib = f"$(BUILD_DIR)/lib/lib{rel.parts[0]}.a"
        lines.append(f"\trm -rf $(BUILD_DIR)/{rel.as_posix()} {lib}")
    else:
        lines.append(f"\trm -rf $(BUILD_DIR)/{rel.as_posix()}")
    lines.append("")
    write_generated_text(directory / "Makefile", "\n".join(lines))


def root_makefile_body() -> list[str]:
    modules = [name for name in STATIC_LIB_DIRS if (ROOT / name).is_dir()]
    normal = [name for name in NORMAL_LINK_LIBS if name in modules]
    dependencies = layer_dependencies(modules)
    startup_asm = startup_asm_objects()
    lines = [
        "PROJECT_NAME := $(CONFIG_PROJECT_NAME)",
        "ELF := $(BUILD_DIR)/$(PROJECT_NAME).elf",
        "HEX := $(BUILD_DIR)/$(PROJECT_NAME).hex",
        "BIN := $(BUILD_DIR)/$(PROJECT_NAME).bin",
        "",
        ".PHONY: all clean " + " ".join(modules),
        "all: " + " ".join(modules),
        "\t@mkdir -p $(dir $(ELF))",
        '\t@echo "LD $(ELF)"',
    ]
    link = "\t$(CC) $(LDFLAGS) -o $(ELF)"
    if startup_asm:
        link += " -Wl,--whole-archive " + " ".join(startup_asm) + " -Wl,--no-whole-archive"
    if normal:
        libs = " ".join(f"$(BUILD_DIR)/lib/lib{name}.a" for name in normal)
        # --start-group/--end-group 让 ld 反复扫描这组归档，覆盖“符号在同一归档内
        # 靠后成员定义”的情况（如 tasks.o 需要 freertos_port_st.o 的栈溢出钩子）。
        link += f" -Wl,--start-group {libs} -Wl,--end-group"
    link += " $(LDLIBS)"
    lines.append(link)
    lines.append('\t@echo "HEX $(HEX)"')
    lines.append("\t$(OBJCOPY) -O ihex $(ELF) $(HEX)")
    lines.append('\t@echo "BIN $(BIN)"')
    lines.append("\t$(OBJCOPY) -O binary $(ELF) $(BIN)")
    lines.append('\t@echo "SIZE $(ELF)"')
    lines.append("\t$(SIZE) $(ELF)")
    lines.append("")
    if modules:
        lines.append(" ".join(modules) + ":")
        lines.append("\t@$(MAKE) --no-print-directory -C $(PROJECT_ROOT)/$@ all")
        lines.append("")
        for name, required in dependencies.items():
            if required:
                lines.append(f"{name}: {' '.join(required)}")
        lines.append("")
    lines.extend(
        [
            "clean:",
            "\trm -rf $(PROJECT_ROOT)/out",
            "",
        ]
    )
    return lines


def write_root_makefile() -> None:
    lines = [
        GEN_BANNER,
        f"PROJECT_ROOT := {project_root_expr(ROOT)}",
        "BUILD_DIR := $(PROJECT_ROOT)/out/object",
        ".DEFAULT_GOAL := all",
        "include $(PROJECT_ROOT)/toolchain.make",
        "-include $(PROJECT_ROOT)/out/.config",
        "",
    ]
    lines.extend(root_makefile_body())
    write_generated_text(ROOT / "Makefile", "\n".join(lines))


def collect_config_symbols() -> dict[str, list[str]]:
    grouped: dict[str, list[str]] = {}
    for directory in walk_dirs():
        if directory == ROOT:
            continue
        rel = directory.relative_to(ROOT)
        category = rel.parts[0]
        grouped.setdefault(category, [])
        if len(rel.parts) >= 1:
            grouped[category].append(dir_symbol(directory))
        if not child_dirs(directory):
            for _source, symbol in source_symbols(directory):
                grouped[category].append(symbol)
    for category in grouped:
        seen = set()
        ordered = []
        for symbol in grouped[category]:
            if symbol not in seen:
                seen.add(symbol)
                ordered.append(symbol)
        grouped[category] = ordered
    return grouped


def default_user_block() -> str:
    return (
        f"{USER_BEGIN}\n"
        "# 此区域由用户维护，update 不会覆盖。\n"
        "# 屏蔽写法：CONFIG_XXX=n ，或单独一行：# CONFIG_XXX is not set\n"
        "# 出现在这里的宏不会在上方自动创建。\n"
        f"{USER_END}\n"
    )


def split_user_region(text: str) -> tuple[str, str]:
    start = text.find(USER_BEGIN)
    if start < 0:
        return default_user_block(), ""
    end = text.find(USER_END, start)
    if end < 0:
        print("警告：用户自定义区缺少结束标记，已保留从起始标记到文件末尾的内容", file=sys.stderr)
        block = text[start:]
        if not block.endswith("\n"):
            block += "\n"
        return block, ""
    end += len(USER_END)
    if end < len(text) and text[end] == "\n":
        end += 1
    return text[start:end], text[end:]


def protected_symbols(user_block: str) -> set[str]:
    found = set()
    for line in user_block.splitlines():
        matched = ASSIGN_RE.match(line) or DISABLED_RE.match(line)
        if matched:
            found.add(matched.group(1))
    return found


def write_defconfig(info: dict) -> None:
    path = ROOT / "proj" / "project_defconfig"
    existing = path.read_text(encoding="utf-8") if path.is_file() else ""
    user_block, suffix = split_user_region(existing)
    protected = protected_symbols(user_block)
    grouped = collect_config_symbols()
    categories = [name for name in STATIC_LIB_DIRS if name in grouped]
    categories.extend(sorted(name for name in grouped if name not in categories))
    lines = [
        GEN_BANNER,
        "# 工程信息",
        f"CONFIG_PROJECT_NAME={info['project_name']}",
        f"CONFIG_CPU_MODEL={info['cpu']['model']}",
        f"CONFIG_CPU_ARCH={info['cpu']['arch']}",
    ]
    model = info["cpu"].get("model", "")
    if model.startswith(("STM32F429", "STM32F439")):
        lines.append("CONFIG_STM32F429XX=y")
    lines.append("")
    for category in categories:
        symbols = [symbol for symbol in grouped[category] if symbol not in protected]
        if not symbols:
            continue
        lines.append(f"# {category}")
        for symbol in symbols:
            lines.append(f"{symbol}=y")
        lines.append("")
    text = "\n".join(lines)
    if not text.endswith("\n"):
        text += "\n"
    write_generated_text(path, text + user_block + suffix)


def parse_defconfig(text: str) -> str:
    values: dict[str, str] = {}
    for line_number, line in enumerate(text.splitlines(), start=1):
        if not line.strip() or line.lstrip().startswith("#"):
            disabled = DISABLED_RE.match(line)
            if disabled:
                values[disabled.group(1)] = "n"
            continue
        assignment = CONFIG_VALUE_RE.match(line)
        if not assignment:
            if line.lstrip().startswith("CONFIG_"):
                raise ValueError(f"project_defconfig 第 {line_number} 行格式无效：{line}")
            continue
        values[assignment.group(1)] = assignment.group(2)
    lines = ["# Automatically generated from proj/project_defconfig."]
    lines.extend(
        f"{name}={value}" if value != "n" else f"# {name} is not set"
        for name, value in values.items()
    )
    return "\n".join(lines) + "\n"


def write_config() -> None:
    defconfig = ROOT / "proj" / "project_defconfig"
    config = ROOT / "out" / ".config"
    config.parent.mkdir(parents=True, exist_ok=True)
    write_generated_text(config, parse_defconfig(read_text(defconfig)))


RESTAMP_DIRTY: list[Path] = []


def write_generated_text(path: Path, text: str) -> None:
    """写入生成物；内容未变则不动文件，保留原 mtime。

    目录 Makefile 把 toolchain.make / out/.config 当成每个 .o 的前置依赖
    （编译参数或 Kconfig 一变就必须重编）。因此这里必须“没变就不写”，
    否则 update 每次刷新这两个文件的 mtime，会退化成每轮全量重编。
    """
    if path.is_file():
        try:
            if path.read_text(encoding="utf-8") == text:
                return
        except OSError:
            pass
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8")
    RESTAMP_DIRTY.append(path)


def restamp_generated(info: dict) -> None:
    """Windows 盘上新写出的文件时间戳会超前于 WSL 时钟，make 会因此报 clock skew。

    只处理本次真正写过的生成物：没变的文件保持原 mtime，否则 flags 依赖会把所有 .o
    变成过期对象，把所有增量构建吃掉。
    """
    now = time.time()
    for path in RESTAMP_DIRTY:
        if path.is_file():
            os.utime(path, (now, now))
    RESTAMP_DIRTY.clear()


def remove_stale_build_files() -> None:
    """排除目录里若残留自动生成的 Makefile/Kconfig，update 时删掉。"""
    for name in EXCLUDE_DIRS:
        directory = ROOT / name
        if not directory.is_dir():
            continue
        # out/ 是构建输出目录，但 out/.config 是 update 正常生成的配置真源：
        # 它不是“排除目录里残留的误生成”，删掉会让它每轮重新创建、mtime 刷新，
        # 而目录 Makefile 已把 out/.config 作为 .o 的前置依赖 → 全量重编。
        stale_names = ("Makefile", "Kconfig") if directory == ROOT / "out" else ("Makefile", "Kconfig", ".config")
        for fname in stale_names:
            path = directory / fname
            if path.is_file():
                path.unlink()
                print(f"删除误生成: {path.relative_to(ROOT)}")
    for directory in walk_dirs(include_linker_script_dirs=True):
        if not is_linker_script_dir(directory):
            continue
        for fname in ("Makefile", "Kconfig"):
            path = directory / fname
            if path.is_file() and path.read_text(encoding="utf-8").startswith(GEN_BANNER):
                path.unlink()
                print(f"删除链接脚本目录生成物: {path.relative_to(ROOT)}")
    for directory in walk_dirs(include_linker_script_dirs=True):
        if not is_empty_source_dir(directory):
            continue
        for fname in ("Makefile", "Kconfig"):
            path = directory / fname
            if path.is_file() and path.read_text(encoding="utf-8").startswith(GEN_BANNER):
                path.unlink()
                print(f"删除空 src 目录生成物: {path.relative_to(ROOT)}")
        try:
            directory.rmdir()
            print(f"删除无源文件的空目录: {directory.relative_to(ROOT)}")
        except OSError:
            pass


def remove_legacy_output_dir() -> None:
    legacy = ROOT / "out" / "build"
    if not legacy.exists():
        return
    allowed_suffixes = {".o", ".d", ".a", ".elf", ".hex", ".bin"}
    unexpected = [
        path
        for path in legacy.rglob("*")
        if path.is_file() and path.suffix not in allowed_suffixes
    ]
    if unexpected:
        names = ", ".join(path.relative_to(ROOT).as_posix() for path in unexpected[:5])
        raise SystemExit(f"旧 out/build/ 含非构建产物，未自动删除：{names}")
    shutil.rmtree(legacy)
    print("删除旧构建输出目录：out/build/")


SKILLS_DIR = ROOT / ".skills"
SPEC_DOC = SKILLS_DIR / "工程规格.md"
SKILL_DOC = SKILLS_DIR / "SKILL.md"
DESIGN_DOC = SKILLS_DIR / "设计手稿.md"
DOC_SKILL_PATHS = (SPEC_DOC, SKILL_DOC, DESIGN_DOC)


def active_modules() -> list[str]:
    return [name for name in STATIC_LIB_DIRS if (ROOT / name).is_dir()]


def expected_link_chain() -> str:
    return " → ".join(name for name in NORMAL_LINK_LIBS if name in active_modules())


def read_text(path: Path) -> str:
    if not path.is_file():
        return ""
    return path.read_text(encoding="utf-8")


def collect_verify_errors() -> list[str]:
    errors: list[str] = []
    modules = active_modules()
    info = load_info()
    try:
        selected_linker = linker_script_path(info)
        if not selected_linker.is_file():
            errors.append(f"链接脚本不存在：{selected_linker.relative_to(ROOT).as_posix()}")
    except (FileNotFoundError, ValueError) as error:
        errors.append(str(error))
        selected_linker = None

    for directory in walk_dirs():
        relative_parts = directory.relative_to(ROOT).parts
        for source in list_sources(directory):
            if "src" not in relative_parts:
                errors.append(
                    f"{source.relative_to(ROOT).as_posix()} 应放在所属模块的 src/ 目录中"
                )
        makefile = directory / "Makefile"
        if makefile.is_file() and "python3 -c 'import os,sys,time" in read_text(makefile):
            errors.append(
                f"{makefile.relative_to(ROOT).as_posix()} 仍硬编码 python3 收尾步骤（Windows 需 $(PYTHON)）"
            )
        for header in directory.iterdir():
            if header.is_file() and header.suffix in HEADER_SUFFIXES and "inc" not in relative_parts:
                errors.append(
                    f"{header.relative_to(ROOT).as_posix()} 应放在所属模块的 inc/ 目录中"
                )

    for name in EXCLUDE_DIRS:
        directory = ROOT / name
        if not directory.is_dir():
            continue
        for fname in ("Makefile", "Kconfig"):
            if (directory / fname).is_file():
                errors.append(f"排除目录 {name}/ 不应存在自动生成的 {fname}")

    for directory in walk_dirs(include_linker_script_dirs=True):
        if is_linker_script_dir(directory):
            for fname in ("Makefile", "Kconfig"):
                path = directory / fname
                if path.is_file() and read_text(path).startswith(GEN_BANNER):
                    errors.append(
                        f"链接脚本目录 {directory.relative_to(ROOT).as_posix()}/ 不应生成 {fname}"
                    )
        if is_empty_source_dir(directory):
            errors.append(
                f"无源文件的空 src/ 目录不应存在：{directory.relative_to(ROOT).as_posix()}"
            )

    root_makefile = ROOT / "Makefile"
    if not root_makefile.is_file():
        errors.append("缺少根 Makefile，请先运行 update")
    else:
        link_text = read_text(root_makefile)
        if "BUILD_DIR := $(PROJECT_ROOT)/out/object" not in link_text:
            errors.append("根 Makefile 的构建输出目录不是 out/object")
        if "-include $(PROJECT_ROOT)/out/.config" not in link_text:
            errors.append("根 Makefile 未加载 out/.config")
        for name in NORMAL_LINK_LIBS:
            if name in modules:
                lib = f"lib{name}.a"
                if lib not in link_text:
                    errors.append(f"根 Makefile 链接行缺少 {lib}")
    obsolete_linker_script = ROOT / "proj" / "project-target.ld"
    if obsolete_linker_script.exists():
        errors.append("旧链接脚本 proj/project-target.ld 应删除；链接脚本应位于 hal/chip/linker/")
    if (ROOT / "out" / "build").exists():
        errors.append("旧构建输出目录 out/build/ 应迁移并清理；当前输出目录为 out/object/")
    toolchain_text = read_text(ROOT / "toolchain.make")
    if selected_linker is not None:
        selected_rel = selected_linker.relative_to(ROOT).as_posix()
        if f"LDSCRIPT := $(PROJECT_ROOT)/{selected_rel}" not in toolchain_text:
            errors.append(f"toolchain.make 未使用链接脚本 {selected_rel}")

    for name in modules:
        makefile = ROOT / name / "Makefile"
        if not makefile.is_file():
            errors.append(f"缺少 {name}/Makefile")
            continue
        makefile_text = read_text(makefile)
        lib = f"lib{name}.a"
        if lib not in makefile_text:
            errors.append(f"{name}/Makefile 未产出 {lib}")
        if "Depend.make" in makefile_text:
            errors.append(f"{name}/Makefile 仍引用已移除的 Depend.make")

    root_makefile_text = read_text(ROOT / "Makefile")
    if (ROOT / "startup").exists():
        errors.append("根目录 startup/ 已移除；启动文件应放在 hal/startup/")
    startup_sources = startup_asm_sources()
    if not startup_sources:
        errors.append("缺少 hal/ 下的 startup_*.s 启动汇编")
    for obj in startup_asm_objects():
        if f"--whole-archive {obj} -Wl,--no-whole-archive" not in root_makefile_text:
            errors.append(f"根 Makefile 未 whole-archive CMSIS 启动对象：{obj}")
    if "Depend.make" in root_makefile_text:
        errors.append("根 Makefile 仍引用已移除的 Depend.make")
    try:
        dependencies = layer_dependencies(modules)
    except ValueError as error:
        errors.append(str(error))
        dependencies = {}
    for name, required in dependencies.items():
        expected = f"{name}: {' '.join(required)}"
        if required and expected not in root_makefile_text.splitlines():
            errors.append(f"根 Makefile 缺少层依赖规则：{expected}")

    defconfig = ROOT / "proj" / "project_defconfig"
    if defconfig.is_file():
        dc = read_text(defconfig)
        for name in modules:
            if f"# {name}" not in dc:
                errors.append(f"project_defconfig 缺少分类注释 # {name}")
    else:
        errors.append("缺少 proj/project_defconfig")
    config = ROOT / "out" / ".config"
    if not config.is_file():
        errors.append("缺少解析后的 out/.config，请先运行 update")
    elif defconfig.is_file():
        try:
            expected_config = parse_defconfig(read_text(defconfig))
            if read_text(config) != expected_config:
                errors.append("out/.config 与 proj/project_defconfig 解析结果不一致")
        except ValueError as error:
            errors.append(str(error))

    project_name = info.get("project_name", "")
    if not same_project_name(project_name, ROOT.name):
        errors.append(
            f"proj/project-info.json 的 project_name={project_name!r} 与工程根目录名 {ROOT.name!r} 不一致"
            "（比较时忽略大小写，并把 -/_ 视为同一分隔符）"
        )
    if defconfig.is_file() and f"CONFIG_PROJECT_NAME={project_name}" not in read_text(defconfig):
        errors.append("project_defconfig 中 CONFIG_PROJECT_NAME 与 project-info.json 不一致")

    toolchain_make = ROOT / "toolchain.make"
    if not toolchain_make.is_file():
        errors.append("缺少根目录 toolchain.make，请先运行 update")
    if (ROOT / "build" / "toolchain.make").exists():
        errors.append("build/toolchain.make 已移至工程根目录")
    if (ROOT / "build" / ".config").exists():
        errors.append("build/.config 已废弃，有效配置仅 out/.config（由 update 从 project_defconfig 解析生成）")
    if (ROOT / "build" / "Depend.make").exists():
        errors.append("build/Depend.make 已废弃，应由各层 Makefile 管理头文件和 .d 依赖")
    for directory in walk_dirs():
        if directory == ROOT:
            continue
        makefile = directory / "Makefile"
        makefile_text = read_text(makefile)
        if "BUILD_DIR := $(PROJECT_ROOT)/out/object" not in makefile_text:
            errors.append(f"{makefile.relative_to(ROOT)} 的构建输出目录不是 out/object")
        if "-include $(PROJECT_ROOT)/out/.config" not in makefile_text:
            errors.append(f"{makefile.relative_to(ROOT)} 未加载 out/.config")
        if "Depend.make" in makefile_text:
            errors.append(f"{makefile.relative_to(ROOT)} 仍引用已移除的 Depend.make")
        layer = directory.relative_to(ROOT).parts[0]
        for include_dir in layer_header_dirs(layer):
            include_rel = include_dir.relative_to(ROOT).as_posix()
            expected = (
                "INCLUDE_DIRS += $(PROJECT_ROOT)"
                if include_rel == "."
                else f"INCLUDE_DIRS += $(PROJECT_ROOT)/{include_rel}"
            )
            if expected not in makefile_text.splitlines():
                errors.append(
                    f"{makefile.relative_to(ROOT)} 缺少可见头文件目录：{include_rel}"
                )
        for source in list_sources(directory):
            expected = f"-include {dep_path(source)}"
            if expected not in makefile_text.splitlines():
                errors.append(
                    f"{makefile.relative_to(ROOT)} 缺少源文件依赖清单：{source.name}"
                )

    allowed_root = set(LAYER_DIRS) | {".git", ".skills"}
    for child in ROOT.iterdir():
        if not child.is_dir() or child.name.startswith("."):
            continue
        if child.name not in allowed_root and child.name not in EXCLUDE_DIRS:
            if child.name not in STATIC_LIB_DIRS:
                # 空目录 git 根本不跟踪（checkout 之后不会出现），也不含任何可构建内容；
                # 只按"里面有文件"才判定未知顶层目录，避免一个空壳把整个门禁卡死。
                if next(child.rglob("*"), None) is None:
                    continue
                errors.append(f"未识别的顶层目录 {child.name}/（不在 LAYER_DIRS 或 STATIC_LIB_DIRS）")

    link_chain = expected_link_chain()
    spec = read_text(SPEC_DOC)
    if not spec:
        errors.append(f"缺少 {SPEC_DOC.relative_to(ROOT)}")
    else:
        if link_chain and link_chain not in spec:
            errors.append(
                f"工程规格.md 未描述当前链接顺序（应包含：{link_chain}）"
            )
        for name in modules:
            if name not in spec:
                errors.append(f"工程规格.md 未提及层目录 {name}")
        for name in EXCLUDE_DIRS:
            if name == "__pycache__":
                continue
            if name not in spec:
                errors.append(f"工程规格.md 未提及扫描排除目录 {name}")

    skill = read_text(SKILL_DOC)
    if not skill:
        errors.append(f"缺少 {SKILL_DOC.relative_to(ROOT)}")
    else:
        if "verify" not in skill:
            errors.append("SKILL.md 未说明 verify 子命令")
        for name in modules:
            if name not in skill:
                errors.append(f"SKILL.md 未提及层目录 {name}")

    design = read_text(DESIGN_DOC)
    if design and link_chain:
        lib_chain = " → ".join(f"lib{name}.a" for name in link_chain.split(" → "))
        if lib_chain not in design and link_chain not in design:
            errors.append("设计手稿.md 链接顺序/框架图可能与当前层不一致（请核对 lib 链）")

    obsolete = ("bll", "bdl")
    for doc_path in (SPEC_DOC, DESIGN_DOC):
        if not doc_path.is_file():
            continue
        doc = read_text(doc_path)
        for old in obsolete:
            if old in modules:
                continue
            if re.search(rf"\b{old}\b", doc):
                errors.append(f"{doc_path.name} 仍引用已移除的层 {old}，请与当前目录对齐")

    return errors


def verify(artifacts: bool = False, *, check_symbols: bool = True) -> None:
    """校验生成物与规格；artifacts 为真时额外断言现有 elf 里的应用代码。

    check_symbols=False 供 update() 内部使用：那一步发生在 make 之前，
    归档/产物还是上一轮的，检查它们只会把 build 自己锁死。
    """
    print("自检：代码生成物与 .skills 规格")
    errors = collect_verify_errors()
    errors.extend(collect_stale_dependency_errors())
    errors.extend(collect_hal_module_errors())
    errors.extend(collect_heap_errors())
    errors.extend(collect_fault_errors())
    errors.extend(collect_memory_metadata_errors())
    errors.extend(collect_doc_reference_errors())
    if check_symbols:
        errors.extend(collect_heap_artifact_errors())
        errors.extend(collect_fault_artifact_errors())
        errors.extend(collect_bsp_artifact_errors())
    if artifacts:
        errors.extend(collect_artifact_errors())
    if errors:
        print("自检失败：", file=sys.stderr)
        for item in errors:
            print(f"  - {item}", file=sys.stderr)
        print("请修正 create-project.py / 源码树 / .skills 后重新运行 update 或 verify。", file=sys.stderr)
        raise SystemExit(1)
    print("自检通过")


def update() -> None:
    print("更新构建描述：Kconfig / Makefile / project_defconfig / out/.config / toolchain.make")
    ensure_layout()
    info = load_info()
    try:
        script = linker_script_path(info)
    except (FileNotFoundError, ValueError) as error:
        raise SystemExit(str(error)) from error
    if not script.is_file():
        raise SystemExit(f"缺少链接脚本：{script}")
    obsolete_linker_script = ROOT / "proj" / "project-target.ld"
    if obsolete_linker_script.is_file():
        obsolete_linker_script.unlink()
        print("删除旧链接脚本：proj/project-target.ld")
    obsolete_depend = ROOT / "build" / "Depend.make"
    if obsolete_depend.is_file():
        obsolete_depend.unlink()
    obsolete_toolchain = ROOT / "build" / "toolchain.make"
    if obsolete_toolchain.is_file():
        obsolete_toolchain.unlink()
    remove_legacy_output_dir()
    remove_stale_build_files()
    for directory in walk_dirs():
        write_kconfig(directory, info["project_name"])
        if directory != ROOT:
            write_dir_makefile(directory)
    write_root_makefile()
    write_toolchain(info)
    write_defconfig(info)
    write_config()
    restamp_generated(info)
    verify(check_symbols=False)


def copy_outputs() -> None:
    """只复制发布产物。

    静态库（尤其带 -g3 的 libhal.a / libosl.a）属于构建中间物，体积近百 MB，
    以前被一起复制进 out/target，让产物目录看起来像工程垃圾场。
    """
    out = ROOT / "out"
    target = out / "target"
    if target.exists():
        shutil.rmtree(target)
    target.mkdir(parents=True)
    artifacts = [
        path
        for path in (out / "object").rglob("*")
        if path.is_file()
        and path.suffix in {".bin", ".hex", ".elf", ".map"}
        and target not in path.parents
    ]
    if not artifacts:
        raise SystemExit("编译结束，但 out/ 下没有 .elf/.hex/.bin/.map 可复制")
    for path in artifacts:
        dest = target / path.name
        shutil.copy2(path, dest)
        if dest.suffix == ".map":
            fold_root_in_map(dest)
        print(f"复制 {path.relative_to(ROOT)} -> {dest.relative_to(ROOT)} → {dest.stat().st_size} 字节")


def fold_root_in_map(dest: Path) -> None:
    """把 .map 里的工程根绝对路径折成 "."，让产物在别的目录/机器上也一致。

    链接器只记录它收到的路径，-ffile-prefix-map 管不到 map 文件：实测同一提交
    Windows 266831 B、Linux 325007 B、GitHub runner 304955 B，差异全是根路径长度。
    map 不参与镜像，这里只做文本归一化，保证 artifact 里四个文件都可复现。
    """
    text = dest.read_text(encoding="utf-8", errors="replace")
    for prefix in (
        ROOT.as_posix(),
        str(ROOT),
        ROOT.as_posix().upper(),
        str(ROOT).upper(),
    ):
        text = text.replace(prefix, ".")
    dest.write_text(text, encoding="utf-8", newline="\n")


ARTIFACT_TEXT_FLOOR = 2048
"""应用代码参与链接后 .text 至少应有这么多字节；明显低于此值说明镜像里只有启动代码。"""

NEWLIB_ALLOC_SYMBOLS = ("_malloc_r", "_calloc_r", "_realloc_r", "_memalign_r")
"""newlib 的分配入口。它们按需提取：只有真有代码引用 malloc 才会进镜像。

因此"符号在不在 elf 里"就是"有没有人用 newlib 堆"的机器事实，不依赖文本扫描，
也不怕宏包装或函数指针。"""

NEWLIB_HEAP_FLOOR = 0x800
"""newlib 堆的最小可用尺寸（2048 B）。

触发条件只有一种：镜像里出现了 newlib 的分配入口。此时若堆仍是链接脚本里那个
512 B 的默认值，一次中等分配就会越过 __heap_end__ 写进紧跟其后的栈区 ——
链接不报错、运行时崩在别处。"""


def collect_hal_module_errors() -> list[str]:
    """检查 HAL 模块开关（hal_conf.h）与include 块、源文件、目标文件是否自洽。

    这套开关只有一处（stm32f4xx_hal_conf.h），但它有三处"影子"必须同时对得上：
      1) 同文件底部的 `#ifdef HAL_X_MODULE_ENABLED / #include "stm32f4xx_hal_x.h"` 块
         —— 少了它，顶层头不会引入该模块，用到的 .c 只会得到隐式声明（指针参数被当成 int，
         静默截断，链接还能过）；
      2) `hal/periph/src/stm32f4xx_hal_x.c` —— 少了它，编译期没有任何提示，链接期才报 undefined；
      3) 对应的 `.o` —— 目录没参与编译时同样只在链接期暴露。
    """
    errors: list[str] = []
    confs = sorted((ROOT / "hal").rglob("stm32f4xx_hal_conf.h"))
    if not confs:
        return errors
    conf = confs[0]
    text = conf.read_text(encoding="utf-8", errors="replace")
    enabled: set[str] = set()
    disabled: set[str] = set()
    for match in re.finditer(r"^(/\*\s*)?#define\s+(HAL_[A-Z0-9_]+_MODULE_ENABLED)", text, re.M):
        (disabled if match.group(1) else enabled).add(match.group(2))
    guarded = set(
        re.findall(
            r"#ifdef\s+(HAL_[A-Z0-9_]+_MODULE_ENABLED)\s*\n\s*#include\s+\"stm32f4xx_hal_",
            text,
        )
    )
    src_dir = conf.parent.parent / "src"
    obj_dir = ROOT / "out" / "object" / "hal" / "periph" / "src"

    missing_include = sorted(enabled - guarded)
    if missing_include:
        errors.append(
            f"{len(missing_include)} 个已启用的 HAL 模块在 {conf.name} 里缺 `#ifdef ... / #include` 块："
            + "、".join(missing_include)
            + "（顶层头不会引入它们，使用时只会得到隐式声明）"
        )
    orphan_guard = sorted(guarded - enabled - disabled)
    if orphan_guard:
        errors.append(
            "以下 include 块的开关既没启用也没有注释，说明开关名拼错或漏写："
            + "、".join(orphan_guard)
        )

    missing_src: list[str] = []
    missing_obj: list[str] = []
    for symbol in sorted(enabled):
        stem = symbol[len("HAL_"):-len("_MODULE_ENABLED")].lower()
        if not (src_dir / f"stm32f4xx_hal_{stem}.c").is_file():
            missing_src.append(symbol)
        if obj_dir.is_dir() and not (obj_dir / f"stm32f4xx_hal_{stem}.o").is_file():
            missing_obj.append(symbol)
    if missing_src:
        errors.append(
            f"{len(missing_src)} 个已启用的 HAL 模块没有实现文件（hal/periph/src 下找不到 .c）："
            + "、".join(missing_src)
        )
    if missing_obj:
        errors.append(
            f"{len(missing_obj)} 个已启用的 HAL 模块没有生成目标文件（hal/periph 目录未参与编译？）："
            + "、".join(missing_obj)
        )
    return errors


def collect_stale_dependency_errors() -> list[str]:
    """检查 -MMD 产出的 .d 里记录的依赖路径是否仍然存在。

    .d 记录的是绝对路径。工程被整目录复制到另一台机器（或另一个根路径）后，
    make 找不到旧路径的头文件就"看不见"头文件变化：改了 FreeRTOSConfig.h，
    tasks.o 仍然是几天前的产物，而 make 全程沉默。实测出现过 77 个这样的 .d。
    """
    errors: list[str] = []
    objdir = ROOT / "out" / "object"
    if not objdir.is_dir():
        return errors
    missing_examples: list[str] = []
    broken = 0
    for dep in sorted(objdir.rglob("*.d")):
        try:
            text = dep.read_text(encoding="utf-8", errors="replace")
        except OSError:
            continue
        parts = text.replace("\\\n", " ").replace("\\", "/").split()
        pruned = [part for part in parts if "/" in part and not part.endswith(":")]
        absent = [part for part in pruned if not Path(part).exists()]
        if absent:
            broken += 1
            if len(missing_examples) < 3:
                missing_examples.append(f"{dep.relative_to(ROOT).as_posix()} -> {absent[0]}")
    if broken:
        errors.append(
            f"{broken} 个 .d 依赖文件中记录的路径已不存在（工程目录被移动/复制过），"
            "make 会因此漏编改动过的头文件；请先 clean 再 build。"
            + "".join(f"\n      例如 {item}" for item in missing_examples)
        )
    return errors


def tool_binary(name: str) -> Path | None:
    """定位 arm-none-eabi-<name>：优先本机工具链 bin（按主机 OS），其次 PATH。"""
    stem = f"arm-none-eabi-{name}"
    candidates = (f"{stem}.exe", stem)
    bin_dir = setenv.find_toolchain_bin()
    dirs = [bin_dir] if bin_dir is not None else sorted((ROOT / "tool").glob("*/bin"))
    for base in dirs:
        for fname in candidates:
            path = base / fname
            if path.is_file():
                return path
    for fname in candidates:
        found = shutil.which(fname)
        if found:
            return Path(found)
    return None


def run_tool(exe: Path, elf: Path) -> str:
    """执行 arm-none-eabi-nm/size；无法执行时返回空串。"""
    try:
        return subprocess.run(
            [str(exe), str(elf)], capture_output=True, text=True, check=False
        ).stdout
    except OSError:
        return ""


def parse_capacity(text: str) -> int:
    """把链接脚本里的 192K / 1024K / 0x30000 解析成字节数。"""
    match = re.fullmatch(r"(0x[0-9a-fA-F]+|\d+)([KkMm]?)", text.strip())
    if match is None:
        return 0
    value = int(match.group(1), 0)
    scale = {"": 1, "k": 1024, "K": 1024, "m": 1024 * 1024, "M": 1024 * 1024}[match.group(2)]
    return value * scale


def memory_regions(info: dict) -> dict[str, int]:
    """从链接脚本的 MEMORY 块读出各区域容量；解析不到时返回空字典（跳过容量断言）。"""
    script = linker_script_path(info)
    if not script.is_file():
        return {}
    text = script.read_text(encoding="utf-8", errors="replace")
    block = re.search(r"MEMORY\s*\{(.*?)\}", text, re.DOTALL)
    if block is None:
        return {}
    capacity: dict[str, int] = {}
    for match in re.finditer(
        r"(\w+)\s*(?:\([^)]*\))?\s*:\s*ORIGIN\s*=\s*[^,]+,\s*LENGTH\s*=\s*(\w+)", block.group(1)
    ):
        capacity[match.group(1)] = parse_capacity(match.group(2))
    return capacity


def collect_artifact_errors() -> list[str]:
    """产物断言：镜像里必须真有应用入口与调度器，且不能超出 Flash/RAM。

    弱符号抢占（例如 static 库里的 __attribute__((weak)) main）或链接顺序反了时，
    ld 会“成功”产出只含启动代码的镜像；只看 make 退出码发现不了。
    容量断言补的是另一半：text 够大但 bss 吃掉 RAM 时，链接同样会“成功”。
    """
    errors: list[str] = []
    info = load_info()
    elf = ROOT / "out" / "object" / f"{info.get('project_name', '')}.elf"
    if not elf.is_file():
        return errors
    nm = tool_binary("nm")
    size = tool_binary("size")
    if nm is None or size is None:
        errors.append("未找到 arm-none-eabi-nm / size，无法校验产物")
        return errors
    symbols = run_tool(nm, elf)
    if not symbols:
        errors.append(f"无法执行 {nm.name}，无法校验产物")
        return errors
    for symbol in ("main", "vTaskStartScheduler"):
        if not re.search(rf"^[0-9a-fA-F]+\s+T\s+{symbol}$", symbols, re.MULTILINE):
            errors.append(
                f"产物 {elf.relative_to(ROOT).as_posix()} 缺少强符号 {symbol}（T），应用层未参与链接"
            )
    text = 0
    data = 0
    bss = 0
    rows = [line for line in run_tool(size, elf).splitlines() if line.strip()]
    if rows:
        parts = rows[-1].split()
        if len(parts) >= 4 and parts[0].isdigit() and parts[1].isdigit() and parts[2].isdigit():
            text = int(parts[0])
            data = int(parts[1])
            bss = int(parts[2])
    if text < ARTIFACT_TEXT_FLOOR:
        errors.append(
            f"产物 {elf.name} 的 .text 仅 {text} 字节，低于 {ARTIFACT_TEXT_FLOOR} 字节下限；镜像疑似空壳"
        )
        return errors
    regions = memory_regions(info)
    rom = regions.get("ROM", 0)
    ram = regions.get("RAM", 0)
    flash_used = text + data
    if rom and flash_used > rom:
        errors.append(
            f"Flash 用量 {flash_used} 字节超出 ROM 容量 {rom} 字节（text {text} + data {data}）"
        )
    # size 的 bss 已经把 ._user_heap_stack 里的 _Min_Heap_Size / _Min_Stack_Size 算在内。
    ram_used = data + bss
    if ram and ram_used > ram:
        errors.append(
            f"RAM 用量 {ram_used} 字节超出 RAM 容量 {ram} 字节（data {data} + bss {bss}）"
        )
    # newlib 堆：实际边界必须与链接脚本声明一致，且"有人在用堆"时尺寸得够。
    # 这两条都只能从产物判：链接脚本里的 _Min_Heap_Size 是个魔数，
    # 而 __heap_start__/__heap_end__ 才是链接器真正排出来的边界。
    heap_bytes = 0
    heap_declared = 0
    bounds: dict[str, int] = {}
    for match in re.finditer(
        r"^([0-9a-fA-F]+)\s+[A-Za-z]\s+(__heap_start__|__heap_end__)$",
        symbols,
        re.MULTILINE,
    ):
        bounds[match.group(2)] = int(match.group(1), 16)
    if len(bounds) == 2:
        heap_bytes = bounds["__heap_end__"] - bounds["__heap_start__"]
    else:
        errors.append(
            f"产物 {elf.name} 里没有 __heap_start__ / __heap_end__ 边界符号，"
            "osl 的 _sbrk 将无从判断 newlib 堆的尽头"
        )
    script = linker_script_path(info)
    if script.is_file():
        declared = re.search(
            r"_Min_Heap_Size\s*=\s*(0x[0-9a-fA-F]+|\d+)\s*;",
            script.read_text(encoding="utf-8", errors="replace"),
        )
        heap_declared = int(declared.group(1), 0) if declared else 0
        if declared is None:
            errors.append(
                f"{script.relative_to(ROOT).as_posix()} 里没有 _Min_Heap_Size 声明，"
                "newlib 堆的尺寸失去唯一来源"
            )
    if heap_bytes and heap_declared and heap_bytes != heap_declared:
        errors.append(
            f"产物 {elf.name} 的 newlib 堆实际 {heap_bytes} 字节，"
            f"但 {script.relative_to(ROOT).as_posix()} 声明 _Min_Heap_Size = {heap_declared} 字节；"
            "链接脚本与产物边界已不一致，先查 ._user_heap_stack 的布置"
        )
    alloc_used = [
        name
        for name in NEWLIB_ALLOC_SYMBOLS
        if re.search(rf"^[0-9a-fA-F]+\s+[A-Za-z]\s+{name}$", symbols, re.MULTILINE)
    ]
    if alloc_used and heap_bytes < NEWLIB_HEAP_FLOOR:
        errors.append(
            f"产物 {elf.name} 里链接了 newlib 分配器（{', '.join(alloc_used)}），"
            f"但 newlib 堆只有 {heap_bytes} 字节（下限 {NEWLIB_HEAP_FLOOR}）；"
            "一次中等 malloc 就会越过 __heap_end__ 写进紧跟其后的栈区。"
            "要么改用 pvPortMalloc（osl_alloc_guard.h 已编译期禁用 malloc），"
            "要么把 _Min_Heap_Size 提到下限以上并确认 RAM 预算"
        )
    if not errors:
        heap_note = ""
        if heap_bytes:
            used = f"已使用：{', '.join(alloc_used)}" if alloc_used else "未使用"
            heap_note = f"；newlib 堆 {heap_bytes} 字节（{used}）"
        print(
            f"产物校验：{elf.name} text={text} bytes，main / vTaskStartScheduler 均为强符号；"
            f"Flash {flash_used}/{rom} 字节（{flash_used * 100.0 / rom:.2f}%），"
            f"RAM {ram_used}/{ram} 字节（{ram_used * 100.0 / ram:.2f}%）{heap_note}"
            if rom and ram
            else f"产物校验：{elf.name} text={text} bytes，main / vTaskStartScheduler 均为强符号{heap_note}"
        )
    return errors


def collect_heap_errors() -> list[str]:
    """newlib 的堆必须由本工程供给且有明确边界（静态部分：链接脚本与 LDFLAGS）。

    `--specs=nosys.specs` 自带一个 `_sbrk` 桩：一旦有人调用 malloc，什么也拿不到，
    调用方不检查返回值就在别处零星崩 —— 这条路径在链接期完全看不出来。
    因此这里断言两件事：链接脚本给出 __heap_start__/__heap_end__ 边界，
    且 libosl.a 里 `_sbrk` 是强符号（保证它排在 -lnosys 之前被提取）。
    """
    errors: list[str] = []
    lds = sorted((ROOT / "hal").rglob("*_flash.ld"))
    if not lds:
        errors.append("未找到 hal 下的 *_flash.ld 链接脚本，无法确认 newlib 堆边界")
    else:
        script = lds[0]
        text = script.read_text(encoding="utf-8", errors="replace")
        for symbol in ("__heap_start__", "__heap_end__"):
            if not re.search(rf"\b{symbol}\s*=\s*\.\s*;", text):
                errors.append(
                    f"{script.relative_to(ROOT).as_posix()} 没有输出 {symbol}，"
                    "osl 的 _sbrk 将无从判断 newlib 堆边界"
                )

    toolchain = ROOT / "toolchain.make"
    if toolchain.is_file():
        text = toolchain.read_text(encoding="utf-8", errors="replace")
        if "-Wl,-u,_sbrk" not in text and "-u,_sbrk" not in text:
            errors.append(
                "toolchain.make 的 LDFLAGS 里没有 `-Wl,-u,_sbrk`；libnosys 的 _sbrk 空桩会抢先，"
                "malloc 拿不到任何内存（详见 osl/src/freertos_port_st.c 的 _sbrk 注释）"
            )
    if toolchain.is_file():
        text = toolchain.read_text(encoding="utf-8", errors="replace")
        if "-Wl,-u,__malloc_lock" not in text and "-u,__malloc_lock" not in text:
            errors.append(
                "toolchain.make 的 LDFLAGS 里没有 `-Wl,-u,__malloc_lock`；"
                "libc_nano.a(mlock.o) 的空锁会先满足 _malloc_r 的引用，"
                "本工程的锁进不了镜像（详见 osl/src/freertos_port_st.c 的锁注释）"
            )

    return errors


def collect_memory_metadata_errors() -> list[str]:
    """project-info.json 里的容量必须与链接脚本 MEMORY 一致。

    这两个数字描述同一块芯片，历史上却对不上过：json 写 flash_kb=2048，
    链接脚本 ROM 只有 1024K。json 只被生成器读取，错了不会有任何构建报错，
    所以必须在自检里盯住它，并且以链接脚本为准。
    """
    errors: list[str] = []
    info = load_info()
    regions = memory_regions(info)
    if not regions:
        return errors
    for key, region in (("flash_kb", "ROM"), ("sram_kb", "RAM"), ("ccm_kb", "CCMRAM")):
        declared = info.get("cpu", {}).get(key)
        actual = regions.get(region)
        if not isinstance(declared, int) or not actual:
            continue
        if declared * 1024 != actual:
            errors.append(
                f"proj/project-info.json 的 cpu.{key} = {declared} KB，"
                f"而链接脚本的 {region} 是 {actual // 1024} KB（{actual} 字节）；"
                "以链接脚本为准改 json"
            )
    return errors


def collect_heap_artifact_errors() -> list[str]:
    """重编之后才能判定的部分：归档里的强定义、map 里的空锁、产物里的 _sbrk。

    与 collect_heap_errors() 分开，是因为 update 里的自检发生在 make 之前：
    那时归档还是上一轮的，检查必然失败，会把 build 自己锁死。
    """
    errors: list[str] = []
    nm = tool_binary("nm")
    archive = ROOT / "out" / "object" / "lib" / "libosl.a"
    if nm is None or not archive.is_file():
        return errors
    symbols = run_tool(nm, archive)
    for symbol, reason in (
        (
            "_sbrk",
            "newlib 会退回到 -lnosys 的空桩，malloc 静默失败"
            "（osl/src/freertos_port_st.c 的 _sbrk 未参与编译？）",
        ),
        (
            "__malloc_lock",
            "newlib 会用 libc_nano.a(mlock.o) 的空锁，任务里并发 malloc 会踩坏堆",
        ),
        (
            "__malloc_unlock",
            "锁只有加没有减，第二次 malloc 就会永久关中断",
        ),
    ):
        if symbols and not re.search(rf"^[0-9a-fA-F]+\s+T\s+{symbol}$", symbols, re.MULTILINE):
            errors.append(f"libosl.a 里没有强符号 {symbol}；{reason}")
    maps = sorted((ROOT / "out" / "object").glob("*.map"))
    for map_file in maps:
        if "libc_a-mlock.o" in map_file.read_text(encoding="utf-8", errors="replace"):
            errors.append(
                f"{map_file.name} 里链接了 libc_nano.a(libc_a-mlock.o)（newlib 的空锁）；"
                "本工程的 __malloc_lock 没有被采用，检查 LDFLAGS 是否缺少 -Wl,-u,__malloc_lock"
            )
    for elf in sorted((ROOT / "out" / "object").glob("*.elf")):
        elf_symbols = run_tool(nm, elf)
        if not elf_symbols or not re.search(r"^[0-9a-fA-F]+\s+T\s+_sbrk$", elf_symbols, re.MULTILINE):
            errors.append(f"产物 {elf.name} 里没有强符号 _sbrk（被 -lnosys 的空桩覆盖）")
    return errors


FAULT_HANDLER_NAMES = (
    "HardFault_Handler",
    "MemManage_Handler",
    "BusFault_Handler",
    "UsageFault_Handler",
)
"""Cortex-M4 的四个异常入口；startup 里全部是 .weak 别名到 Default_Handler。"""

FAULT_SOURCE = ROOT / "osl" / "src" / "freertos_port_st.c"

DOC_SCAN_SKIP_PREFIXES = ("tool/", "out/", "examples/", "doc/reference/")
"""文档里这些路径属于工具链目录、上游示例或未跟踪的大体积参考资料，不做存在性断言。"""

DOC_REF_PREFIXES = (".skills/", "build/", "proj/", "doc/", "apl/", "fml/", "bsl/", "osl/", "hal/")
"""只有"仓库相对路径"才断言存在。裸文件名（`FreeRTOSConfig.h`、`Depend.make` 这类
正文提及）不在此列——它们不是在说"去看这个文件"，而是把文件名当术语用。"""

DOC_REF_EXTENSIONS = (
    ".md", ".c", ".h", ".py", ".ld", ".s", ".json", ".make", ".mk", ".txt",
    ".cfg", ".xlsx", ".pdf", ".png", ".bin", ".hex", ".elf", ".map",
)


def collect_fault_errors() -> list[str]:
    """硬件异常必须由本工程接管，不能落到 startup 的 Default_Handler。

    startup_stm32f429xx.s 把四个异常入口都 .weak 别名到 Default_Handler，
    而 Default_Handler 只是 `Infinite_Loop: b Infinite_Loop`。这条路径在编译期
    和链接期都没有任何提示：真出现空指针 / 总线错误 / 除零时，板子只会安静停机，
    g_panic_record 里一个字都不会留下。

    静态部分断言两件事：源码里四个入口是强定义；startup 仍保持弱别名。
    后者是"覆盖关系"的另一半——弱别名被删掉，覆盖就不再是有意设计（而是巧合）。
    """
    errors: list[str] = []
    if not FAULT_SOURCE.is_file():
        return [f"缺少 {FAULT_SOURCE.relative_to(ROOT).as_posix()}，无法确认硬件异常是否被接管"]
    text = read_text(FAULT_SOURCE)
    for name in FAULT_HANDLER_NAMES:
        defined = re.search(rf"^void\s+{name}\s*\(void\)", text, re.M) or re.search(
            rf"^DEFINE_FAULT_HANDLER\(\s*{name}\s*,", text, re.M
        )
        if not defined:
            errors.append(
                f"{FAULT_SOURCE.relative_to(ROOT).as_posix()} 没有定义 {name}；"
                "它会落到 startup 的 Default_Handler（b Infinite_Loop），"
                "故障现场不留任何证据"
            )
    startups = sorted((ROOT / "hal").rglob("startup_*.s"))
    if not startups:
        errors.append("未找到 hal 下的 startup_*.s，无法确认异常入口的弱别名")
    else:
        asm = read_text(startups[0])
        for name in FAULT_HANDLER_NAMES:
            if not re.search(rf"\.thumb_set\s+{name}\s*,\s*Default_Handler", asm):
                errors.append(
                    f"{startups[0].relative_to(ROOT).as_posix()} 里 {name} 不再弱别名到 "
                    "Default_Handler；启动文件被改过，请重新确认异常覆盖关系"
                )
    return errors


def collect_fault_artifact_errors() -> list[str]:
    """链接之后：四个异常入口必须真的进镜像，且地址不能等于 Default_Handler。

    与静态检查分开的原因同 collect_heap_artifact_errors()：update 里的自检发生在
    make 之前，产物还是上一轮的。这里能抓到"源码写了、但没被链接进来"的情形
    （例如将来把实现挪出 freertos_port_st.o，那个成员就不再被无条件拉入）。
    """
    errors: list[str] = []
    nm = tool_binary("nm")
    elfs = sorted((ROOT / "out" / "object").glob("*.elf"))
    if nm is None or not elfs:
        return errors
    for elf in elfs:
        symbols = run_tool(nm, elf)
        if not symbols:
            continue
        table: dict[str, tuple[str, str]] = {}
        for line in symbols.splitlines():
            parts = line.split()
            if len(parts) == 3:
                table[parts[2]] = (parts[0].lower(), parts[1])
        default_handler = table.get("Default_Handler")
        for name in FAULT_HANDLER_NAMES:
            entry = table.get(name)
            if entry is None or entry[1] != "T":
                errors.append(
                    f"产物 {elf.name} 里 {name} 不是强符号 T；"
                    "异常会落到 startup 的弱别名 Default_Handler（b Infinite_Loop）"
                )
            elif default_handler is not None and entry[0] == default_handler[0]:
                errors.append(
                    f"产物 {elf.name} 里 {name} 与 Default_Handler 同地址 "
                    f"0x{int(entry[0], 16):08x}（仍是 Infinite_Loop），说明强定义没生效"
                )
    return errors


BSP_ARTIFACT_SYMBOLS = (
    "board_key_init",
    "board_key_pressed",
    "board_beep_init",
    "board_beep_set",
)


def collect_bsp_artifact_errors() -> list[str]:
    """链接之后：按键/蜂鸣器实现必须真的进镜像。

    历史事实：bsl 的 board_key_init/pressed、board_beep_init/on/off 曾经全仓零引用，
    被 -Wl,--gc-sections 整段丢弃。源码、头文件、归档全都在，elf 里却查不到符号，
    板上按 KEY 毫无反应，也没有任何门禁发现——这正是"实现写完但没人接"最典型的假绿。
    """
    errors: list[str] = []
    nm = tool_binary("nm")
    elfs = sorted((ROOT / "out" / "object").glob("*.elf"))
    if nm is None or not elfs:
        return errors
    for elf in elfs:
        symbols = run_tool(nm, elf)
        if not symbols:
            continue
        defined = set()
        for line in symbols.splitlines():
            parts = line.split()
            if len(parts) == 3 and parts[1].upper() in {"T", "D", "B", "W"}:
                defined.add(parts[2])
        missing = [name for name in BSP_ARTIFACT_SYMBOLS if name not in defined]
        if missing:
            errors.append(
                f"产物 {elf.name} 里找不到 {', '.join(missing)}；"
                "bsl 的按键/蜂鸣器实现被 --gc-sections 丢弃（没有任何任务引用它）"
            )
    return errors


def collect_doc_reference_errors() -> list[str]:
    """文档里反引号包住的路径必须真实存在。

    历史事实：`.skills/工程规格.md` 同时写了 `doc/工具链安装.md`（不存在）和
    `doc/开发环境配置.md`（实际在 doc/design/ 下），两者都没被任何检查发现，
    因为 verify 只管"文档里的层名/链接顺序"，不管路径。
    """
    errors: list[str] = []
    candidates: list[Path] = []
    for root in (ROOT / ".skills", ROOT / "doc"):
        if not root.is_dir():
            continue
        for path in sorted(root.rglob("*.md")):
            relative = path.relative_to(ROOT).as_posix()
            if any(relative.startswith(prefix) for prefix in DOC_SCAN_SKIP_PREFIXES):
                continue
            candidates.append(path)
    for path in candidates:
        relative = path.relative_to(ROOT).as_posix()
        text = read_text(path)
        for lineno, line in enumerate(text.splitlines(), start=1):
            for match in re.finditer(r"`([^`\n]+)`", line):
                ref = match.group(1).strip()
                if not ref or " " in ref or ref.startswith(("$", "http", "-", "0x")):
                    continue
                if any(ch in ref for ch in ("*", "<", ">", "{", "}", "|", "%")):
                    continue
                ref = ref.split(":", 1)[0]
                if ref.endswith("/"):
                    continue
                if not ref.lower().endswith(DOC_REF_EXTENSIONS):
                    continue
                if not ref.startswith(DOC_REF_PREFIXES):
                    continue
                if any(ref.startswith(prefix) for prefix in DOC_SCAN_SKIP_PREFIXES):
                    continue
                if re.match(r"^[a-z0-9_/]*_x\.(c|h)$", ref):
                    continue  # 规格里的占位模块名（stm32f4xx_hal_x.c 之类）
                if (ROOT / ref).exists() or (path.parent / ref).exists():
                    continue
                errors.append(f"{relative}:{lineno} 引用的路径不存在：{ref}")
    return errors


def build_env() -> dict[str, str]:
    return setenv.apply(os.environ)


def resolve_compiler() -> str | None:
    gcc = setenv.resolve_gcc()
    if gcc is not None:
        return str(gcc)
    return shutil.which("arm-none-eabi-gcc")


def build() -> None:
    update()
    compiler = resolve_compiler()
    if compiler is None:
        raise SystemExit(
            "未找到 arm-none-eabi-gcc。请先运行: python3 build/install-toolchain.py\n"
            "或安装系统包: sudo apt install gcc-arm-none-eabi binutils-arm-none-eabi"
        )
    make_cmd = "make"
    make_argv: list[str] = []
    if setenv.host_os() == "win32":
        make_path = setenv.resolve_make()
        if make_path is None:
            raise SystemExit(
                "未找到 tool/win_x86_64/bin/make.exe。"
                "请运行: python build/install-toolchain.py --make-only --from \"<Git>\\usr\\bin\""
            )
        make_cmd = str(make_path)
        tool_bin = setenv.find_toolchain_bin()
        make_argv.append(f"PROJECT_ROOT={ROOT.resolve().as_posix()}")
        if tool_bin is not None:
            sh = tool_bin / "sh.exe"
            if sh.is_file():
                make_argv.append(f"SHELL={setenv._path_for_env(sh)}")
    print("编译工程")
    result = subprocess.run(
        [make_cmd, "-C", str(ROOT), *make_argv, "all"],
        check=False,
        env=build_env(),
    )
    if result.returncode != 0:
        raise SystemExit(result.returncode)
    errors = collect_artifact_errors()
    errors.extend(collect_heap_artifact_errors())
    errors.extend(collect_fault_artifact_errors())
    errors.extend(collect_bsp_artifact_errors())
    if errors:
        print("产物断言失败：", file=sys.stderr)
        for item in errors:
            print(f"  - {item}", file=sys.stderr)
        raise SystemExit(
            "产物断言失败：编译通过不等于镜像里有对应实现，逐条看上面列出的项"
            "（应用代码未进镜像 / 弱符号被抢占 / 实现被 --gc-sections 丢弃 / 资源越界）。"
        )
    copy_outputs()
    setenv.remove_msys_tmp()


def clean() -> None:
    print("清理编译产物")
    makefile = ROOT / "Makefile"
    make_cmd = setenv.resolve_make() if setenv.host_os() == "win32" else shutil.which("make")
    if makefile.is_file() and make_cmd is not None:
        # 直接写 "make" 在 Windows 会抛 FileNotFoundError 并让 clean 半途而废（out/ 反而没删）。
        subprocess.run([str(make_cmd), "-C", str(ROOT), "clean"], check=False, env=build_env())
    out = ROOT / "out"
    if out.exists():
        shutil.rmtree(out)
    print("已删除 out/")


def main(argv: list[str] | None = None) -> None:
    parser = argparse.ArgumentParser(
        description="生成 STM32 工程描述并编译。",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""\
子命令说明:
  update  扫描源码树，生成 Kconfig、Makefile、toolchain.make、project_defconfig、out/.config，并自检
  verify  检查生成物、层目录、链接顺序与 .skills/工程规格.md、SKILL.md 是否一致
          （加 --artifacts 时额外校验现有 elf 里的应用代码）
  build   先 update（含自检），再 make 全量编译，产物复制到 out/target
  clean   删除 out/ 及编译中间文件

示例:
  python3 build/create-project.py update
  python3 build/create-project.py verify
  python3 build/create-project.py build
  python3 build/create-project.py clean

未找到编译器时请先: python3 build/install-toolchain.py
""",
    )
    parser.add_argument(
        "option",
        choices=("build", "update", "verify", "clean"),
        metavar="{build|update|verify|clean}",
        help="build 会先 update；update 刷新描述并自检；verify 仅自检",
    )
    parser.add_argument(
        "--artifacts",
        action="store_true",
        help="verify 时额外校验现有 elf 产物（强 main、vTaskStartScheduler、.text 下限）",
    )
    args = parser.parse_args(argv)
    if args.option == "build":
        build()
    elif args.option == "update":
        update()
    elif args.option == "verify":
        verify(artifacts=args.artifacts)
    else:
        clean()


if __name__ == "__main__":
    main()
