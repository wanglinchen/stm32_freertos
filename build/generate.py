"""`update`：扫描源码树并写出 Kconfig / Makefile / toolchain / defconfig。"""
from __future__ import annotations

import argparse
import json
import os
import re
import shutil
import sys
import time
from pathlib import Path

import contracts
from contracts import *  # noqa: F403
import environment as te


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
        "board": {
            "dts": "device/dts/boards/apollo-f429.dts",
            "compatible": "alientek,apollo-f429",
            "pinmap_xlsx": (
                "doc/hardware/pinmap/阿波罗V2 STM32F429开发板IO引脚分配表.xlsx"
            ),
            "gpio_bind": {
                "leds": {
                    "active_low": True,
                    "led_names": ["R", "G", "B"],
                    "pinmap_labels": ["LED0", "LED1", "BOOT1"],
                },
                "keys": [
                    {
                        "key_id": "WK_UP",
                        "node": "key-wk-up",
                        "label": "WK_UP",
                        "pinmap_label": "WK_UP",
                        "pressed_high": True,
                    },
                    {
                        "key_id": "KEY0",
                        "node": "key0",
                        "label": "KEY0",
                        "pinmap_label": "KEY0",
                        "pressed_high": True,
                    },
                    {
                        "key_id": "KEY1",
                        "node": "key1",
                        "label": "KEY1",
                        "pinmap_label": "KEY1",
                        "pressed_high": True,
                    },
                    {
                        "key_id": "KEY2",
                        "node": "key2",
                        "label": "KEY2",
                        "pinmap_label": "KEY2",
                        "pressed_high": True,
                    },
                ],
                "beep": {"port": "I", "pin": 11, "active_high": True},
            },
        },
    }
def ensure_layout() -> None:
    for name in LAYER_DIRS:
        (ROOT / name).mkdir(parents=True, exist_ok=True)
def load_info() -> dict:
    path = ROOT / "proj" / "project-info.json"
    if not path.is_file():
        path.write_text(
            json.dumps(default_info(), indent=2) + "\n", encoding="utf-8", newline="\n"
        )
    data = json.loads(path.read_text(encoding="utf-8"))
    cpu = data.setdefault("cpu", {})
    base = default_info()
    data.setdefault("project_name", base["project_name"])
    for key, value in base["cpu"].items():
        cpu.setdefault(key, value)
    board = data.setdefault("board", {})
    for key, value in base["board"].items():
        board.setdefault(key, value)
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
def needs_build_metadata(directory: Path) -> bool:
    """这个目录该不该有自己的 Makefile / Kconfig？

    判据只看「有没有东西要被这层构建描述管住」：有源文件要编，或还有子目录要下发。
    纯头文件目录（`bsl/inc`、`hal/periph/inc`、`osl/inc/freertos/portable/GCC/ARM_CM4F`
    这类）一条都不满足 —— 它只需要 `-I`，而 `-I` 来自 `header_dirs()` 走 `walk_dirs`
    的遍历，与生成物无关。此前这类目录也各生成了 Makefile + Kconfig，产物是
    `LOCAL_OBJS :=` 空 + 重列 include + `clean` 删一个不存在的目录，以及一条
    `comment "无子目录、无源文件"`，全仓库 8 个目录 × 2 个文件共 16 个纯噪声，
    其中 `apl/inc/` 连头文件都没有。
    """
    if directory == ROOT:
        return True
    return bool(list_sources(directory) or child_dirs(directory))
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
    return te.find_toolchain_bin()
def write_toolchain(info: dict) -> None:
    script = linker_script_path(info)
    rel = script.relative_to(ROOT).as_posix()
    lines = [
        GEN_BANNER,
        "# 工具链：优先 STM32_TOOLCHAIN_ROOT，否则 tool/<平台>；include 由各层 Makefile 管。",
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
        "ifeq ($(OS),Windows_NT)",
        "PYTHON := python",
        "else",
        "PYTHON := python3",
        "endif",
        "# 默认对齐 userdebug（-Og/-g3）；user 用 make user 或 build.py build（-Os/-g0）。?= 可被命令行覆盖。",
        "OPT ?= -Og",
        "DEBUG ?= -g3",
        "REPRO := -ffile-prefix-map=$(PROJECT_ROOT)=.",
        "ALLOC_GUARD := -include $(PROJECT_ROOT)/osl/inc/osl_alloc_guard.h",
        f"CPU_FLAGS := {cpu_flags(info)}",
        "CHIP_DEFS = $(foreach config,$(filter CONFIG_STM32%,$(.VARIABLES)),$(if $(filter y,$($(config))),-D$(subst XX,xx,$(patsubst CONFIG_%,%,$(config)))))",
        "CFLAGS = $(CPU_FLAGS) $(CHIP_DEFS) $(OPT) $(DEBUG) $(REPRO) $(ALLOC_GUARD) -std=c11 -ffunction-sections -fdata-sections -fno-common -Wall -Wextra -Werror -fstack-usage $(INCLUDES)",
        "CXXFLAGS = $(CPU_FLAGS) $(CHIP_DEFS) $(OPT) $(DEBUG) $(REPRO) $(ALLOC_GUARD) -std=c++17 -ffunction-sections -fdata-sections -fno-exceptions -fno-rtti -Wall -Wextra -Werror -fstack-usage $(INCLUDES)",
        "ASFLAGS = $(CPU_FLAGS) $(CHIP_DEFS) $(OPT) $(DEBUG) $(REPRO) $(INCLUDES)",
        f"LDSCRIPT := $(PROJECT_ROOT)/{rel}",
        "# 链接强制引用 _sbrk / __malloc_lock / _write，避免 --gc-sections 与 libc 空桩抢先。",
        "LDFLAGS = $(CPU_FLAGS) -T$(LDSCRIPT) -nostartfiles -Wl,--gc-sections -Wl,-Map=$(TARGET_DIR)/$(PROJECT_NAME).map "
        "-Wl,-u,_sbrk -Wl,-u,__malloc_lock -Wl,-u,_write --specs=nano.specs --specs=nosys.specs",
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
    children = [child for child in child_dirs(directory) if needs_build_metadata(child)]
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
def _strip_comments_keep_offsets(text: str) -> str:
    """剥掉注释但保留字符偏移与换行数，便于用偏移反查行号。"""
    text = re.sub(
        r"/\*.*?\*/",
        lambda m: re.sub(r"[^\n]", " ", m.group(0)),
        text,
        flags=re.S,
    )
    return re.sub(r"//[^\n]*", lambda m: " " * len(m.group(0)), text)


def guarded_lines(guards: list[str], body: list[str]) -> list[str]:
    lines: list[str] = []
    for guard in guards:
        lines.append(f"ifeq ($({guard}),y)")
    lines.extend(body)
    for _guard in guards:
        lines.append("endif")
    return lines
def config_gated_sources(sources: list[Path]) -> list[Path]:
    """按 FreeRTOSConfig.h 的 `configUSE_*` 裁掉不该存在的 FreeRTOS 编译单元。

    只对 `freertos/` 子目录下的三个文件生效，其余源文件原样返回。
    """
    config = ROOT / "osl" / "inc" / "FreeRTOSConfig.h"
    if not config.is_file():
        return sources
    text = _strip_comments_keep_offsets(config.read_text(encoding="utf-8"))
    values: dict[str, str | None] = {}
    for _, macro in FREERTOS_CONFIG_GATED:
        match = re.search(rf"#define\s+{macro}\s+[\(\s]*(\d+)", text)
        values[macro] = match.group(1) if match else None
    kept: list[Path] = []
    for source in sources:
        gated = next(
            (
                macro
                for name, macro in FREERTOS_CONFIG_GATED
                if source.name == name and "freertos" in source.parts
            ),
            None,
        )
        if gated is not None and values.get(gated) == "0":
            continue
        kept.append(source)
    return kept
def write_dir_makefile(directory: Path) -> None:
    """生成目录 Makefile。静态库目录在本层打包，子目录只编译自己的源文件。"""
    rel = directory.relative_to(ROOT)
    is_lib = len(rel.parts) == 1 and rel.parts[0] in STATIC_LIB_DIRS
    sources = config_gated_sources(list_sources(directory))
    lines = [
        GEN_BANNER,
        f"PROJECT_ROOT := {project_root_expr(directory)}",
        BUILD_VARIANT_MAKE_EXPR,
        BUILD_DIR_MAKE_EXPR,
        "TARGET_DIR := $(BUILD_DIR)/target",
        ".DEFAULT_GOAL := all",
        "include $(PROJECT_ROOT)/toolchain.make",
        KCONFIG_INCLUDE_MAKE_EXPR,
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
        # 编译参数（toolchain.make）与分档 Kconfig（out/<variant>/.config）一变须重编 .o。
        "FLAG_DEPS := $(wildcard $(PROJECT_ROOT)/toolchain.make) " + KCONFIG_FLAG_DEP_MAKE,
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
        for source in config_gated_sources(iter_sources_under(directory)):
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
            # PYTHON 必须加引号：它来自 $(sys.executable)，在 Windows 上可能落在带空格的
            # 路径（如 C:/Users/x/AppData/Local/Doubao/User Data/.../python.exe）。sh 会把
            # 未加引号的裸词按空格切开，实测报 "line 1: C:/Users/.../User: No such file or
            # directory" 并以 Error 127 中断汇编规则，startup_stm32f429xx.o 编不出来。
            "\t@\"$(PYTHON)\" -c 'import os,sys,time; t=time.time(); [os.utime(p,(t,t)) for p in sys.argv[1:]]' $@ " + dep,
        ]
        lines.extend(guarded_lines(source_guards(source), body))
        lines.append("")
    children = [child for child in child_dirs(directory) if needs_build_metadata(child)]
    lines.append(".PHONY: all clean" + (" subdirs" if children else ""))
    lines.append("")
    if is_lib:
        lib = f"$(TARGET_DIR)/lib/lib{rel.parts[0]}.a"
        # 库必须是“真文件目标”，不能把 AR 挂在 PHONY 的 all 的 recipe 里。
        # 以前是 rm -f + AR 无条件执行：`.a` 的 mtime 每次都变 → 顶层必然重链，
        # 实测“什么都没改”的一次 build 仍打印 AR libfml.a / AR libapl.a / LD，耗时 24.1 s。
        # 现在只有 $(ARCHIVE_OBJS) 里真有过期对象时才重打。
        lines.append(f"LIB := {lib}")
        lines.append("")
        if children:
            # 子目录必须先编出 .o，$(LIB) 才有可打包的对象。这里用二次 make 而不是
            # 把两者并列写进 all 的依赖：`make -j4` 下并列依赖会并行启动 $(LIB)，
            # 而 .o 还不存在、又没有本目录内的规则，make 会直接报 No rule to make target。
            lines.append("all: subdirs")
            lines.append("\t@$(MAKE) --no-print-directory $(LIB)")
            lines.append("")
            lines.append("subdirs:")
            for child in children:
                child_dir = f"$(PROJECT_ROOT)/{child.relative_to(ROOT).as_posix()}"
                lines.append(f"\t@$(MAKE) --no-print-directory -C {child_dir} all")
        else:
            lines.append("all: $(LIB)")
        lines.append("")
        lines.append("$(LIB): $(ARCHIVE_OBJS)")
        lines.append("\t@mkdir -p $(dir $@)")
        lines.append('\t@echo "AR $@"')
        lines.append("\t@rm -f $@")
        lines.append("\t$(AR) rcs $@ $(ARCHIVE_OBJS)")
    else:
        lines.append("all: $(LOCAL_OBJS)")
        for child in children:
            child_dir = f"$(PROJECT_ROOT)/{child.relative_to(ROOT).as_posix()}"
            lines.append(f"\t@$(MAKE) --no-print-directory -C {child_dir} all")
    lines.append("")
    lines.append("clean:")
    if is_lib:
        lib = f"$(TARGET_DIR)/lib/lib{rel.parts[0]}.a"
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
        "ELF := $(TARGET_DIR)/$(PROJECT_NAME).elf",
        "HEX := $(TARGET_DIR)/$(PROJECT_NAME).hex",
        "BIN := $(TARGET_DIR)/$(PROJECT_NAME).bin",
        "",
        ".PHONY: all clean modules user userdebug release " + " ".join(modules),
        "",
        # 两段式：先把各层编出来（modules），再单独判一次 ELF/HEX/BIN。
        # 不能把模块和 $(ELF) 并列写进 all 的依赖：`make -j4` 会并行启动 $(ELF)，
        # 而 .a 还没生成 → No rule to make target。$(ELF) 自身是真文件目标，
        # 所以“什么都没改”时第二段只打印 Nothing to be done，不再重链。
        "all:",
        # 把本次真正生效的优化级别落到产物目录：`OPT` 可以被命令行覆盖，
        # 编译出来的 .o 又只看 toolchain.make，所以没有这个标记就无从判断
        # 手上的 elf 到底是 -Og 还是 -Os（详见 _artifact_is_release_build）。
        "\t@mkdir -p $(BUILD_DIR) $(TARGET_DIR)",
        '\t@echo "OPT=$(OPT) DEBUG=$(DEBUG)" > $(BUILD_DIR)/.buildflags',
        "\t@$(MAKE) --no-print-directory modules",
        "\t@$(MAKE) --no-print-directory $(ELF) $(HEX) $(BIN)",
        '\t@echo "SIZE $(ELF)"',
        "\t$(SIZE) $(ELF)",
        "",
        "modules: " + " ".join(modules),
        "",
    ]
    link = "\t$(CC) $(LDFLAGS) -o $@"
    deps = []
    if startup_asm:
        deps.extend(startup_asm)
        link += " -Wl,--whole-archive " + " ".join(startup_asm) + " -Wl,--no-whole-archive"
    if normal:
        libs = " ".join(f"$(TARGET_DIR)/lib/lib{name}.a" for name in normal)
        deps.append(libs)
        # --start-group/--end-group 让 ld 反复扫描这组归档，覆盖“符号在同一归档内
        # 靠后成员定义”的情况（如 tasks.o 需要 freertos_port_st.o 的栈溢出钩子）。
        link += f" -Wl,--start-group {libs} -Wl,--end-group"
    link += " $(LDLIBS)"
    # 链接脚本与链接参数都是 $(ELF) 的真实输入。LDFLAGS 来自 toolchain.make：改了
    # -u / --specs 这类参数后，若 .o 恰好都比 elf 旧，make 会跳过重链，产物断言就在
    # 旧镜像上跑（write 组在全量顺序下就是这么假绿的）。
    deps.append("$(LDSCRIPT)")
    deps.append("$(PROJECT_ROOT)/toolchain.make")
    lines.append("$(ELF): " + " ".join(deps))
    lines.append("\t@mkdir -p $(dir $@)")
    lines.append('\t@echo "LD $@"')
    lines.append(link)
    lines.append("")
    lines.append("$(HEX): $(ELF)")
    lines.append('\t@echo "HEX $@"')
    lines.append("\t$(OBJCOPY) -O ihex $(ELF) $@")
    lines.append("")
    lines.append("$(BIN): $(ELF)")
    lines.append('\t@echo "BIN $@"')
    lines.append("\t$(OBJCOPY) -O binary $(ELF) $@")
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
            # userdebug：-Og -g3 保留完整调试信息；user：-Os -g0 追求 Flash 镜像更小。须带 -B。
            "userdebug:",
            "\t@$(MAKE) --no-print-directory -B BUILD_VARIANT=userdebug OPT=-Og DEBUG=-g3 all",
            "user:",
            "\t@$(MAKE) --no-print-directory -B BUILD_VARIANT=user OPT=-Os DEBUG=-g0 all",
            "release: user",
            "",
            "clean:",
            "\trm -rf $(PROJECT_ROOT)/out/user $(PROJECT_ROOT)/out/userdebug",
            "",
        ]
    )
    return lines
def write_root_makefile() -> None:
    lines = [
        GEN_BANNER,
        f"PROJECT_ROOT := {project_root_expr(ROOT)}",
        BUILD_VARIANT_MAKE_EXPR,
        BUILD_DIR_MAKE_EXPR,
        "TARGET_DIR := $(BUILD_DIR)/target",
        ".DEFAULT_GOAL := all",
        "include $(PROJECT_ROOT)/toolchain.make",
        KCONFIG_INCLUDE_MAKE_EXPR,
        "",
    ]
    lines.extend(root_makefile_body())
    write_generated_text(ROOT / "Makefile", "\n".join(lines))
def collect_config_symbols() -> dict[str, list[str]]:
    grouped: dict[str, list[str]] = {}
    for directory in walk_dirs():
        if directory == ROOT:
            continue
        if not needs_build_metadata(directory):
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
def write_defconfig_body(info: dict, protected: set[str]) -> str:
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
    return text


def write_defconfig(info: dict) -> None:
    for variant in BUILD_VARIANTS:
        path = project_defconfig_path(variant)
        existing = path.read_text(encoding="utf-8") if path.is_file() else ""
        user_block, suffix = split_user_region(existing)
        protected = protected_symbols(user_block)
        text = write_defconfig_body(info, protected) + user_block + suffix
        write_generated_text(path, text)
def parse_defconfig(
    text: str, *, source_rel: str = "proj/project_defconfig_userdebug"
) -> str:
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
                raise ValueError(f"defconfig 第 {line_number} 行格式无效：{line}")
            continue
        values[assignment.group(1)] = assignment.group(2)
    lines = [f"# Automatically generated from {source_rel}."]
    lines.extend(
        f"{name}={value}" if value != "n" else f"# {name} is not set"
        for name, value in values.items()
    )
    return "\n".join(lines) + "\n"
def write_config() -> None:
    for variant in BUILD_VARIANTS:
        defconfig = project_defconfig_path(variant)
        if not defconfig.is_file():
            continue
        config = out_config_path(variant)
        config.parent.mkdir(parents=True, exist_ok=True)
        source_rel = defconfig.relative_to(ROOT).as_posix()
        write_generated_text(
            config, parse_defconfig(read_text(defconfig), source_rel=source_rel)
        )
def write_generated_text(path: Path, text: str) -> None:
    """写入生成物；内容未变则不动文件，保留原 mtime。

    目录 Makefile 把 toolchain.make / out/<variant>/.config 当成每个 .o 的前置依赖
    （编译参数或 Kconfig 一变就必须重编）。因此这里必须“没变就不写”，
    否则 update 每次刷新这两个文件的 mtime，会退化成每轮全量重编。

    必须显式写 newline="\\n"：write_text 默认 newline=None，在 Windows 上会把
    \\n 翻译成 os.linesep（\\r\\n）。也就是说 69 个生成物的 CRLF 不是 core.autocrlf
    造成的，是这里自己在写 —— 只在 .gitattributes 里钉 eol=lf 而不管生成端，
    每次 update 都会把 LF 打回 CRLF，行尾断言每轮都红。
    """
    if path.is_file():
        try:
            if path.read_text(encoding="utf-8") == text:
                return
        except OSError:
            pass
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8", newline="\n")
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
        # out/<variant>/.config 是 update 写入的分档配置；勿当误生成删掉，否则会触发全量重编。
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
    for directory in walk_dirs():
        if needs_build_metadata(directory):
            continue
        for fname in ("Makefile", "Kconfig"):
            path = directory / fname
            if path.is_file() and path.read_text(encoding="utf-8").startswith(GEN_BANNER):
                path.unlink()
                print(f"删除纯头目录生成物: {path.relative_to(ROOT)}")
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
def migrate_out_profile_layout() -> None:
    """旧布局 out/object、out/release → out/userdebug/objects、out/userdebug/release。"""
    legacy_obj = ROOT / "out" / "object"
    legacy_rel = ROOT / "out" / "release"
    ud_obj = out_object_dir(BUILD_VARIANT_USERDEBUG)
    ud_rel = out_release_dir(BUILD_VARIANT_USERDEBUG)
    if legacy_obj.is_dir() and not ud_obj.exists():
        ud_obj.parent.mkdir(parents=True, exist_ok=True)
        shutil.move(str(legacy_obj), str(ud_obj))
        print(f"迁移 {legacy_obj.relative_to(ROOT)} -> {ud_obj.relative_to(ROOT)}")
    elif legacy_obj.is_dir() and ud_obj.exists():
        print(f"跳过迁移 {legacy_obj.relative_to(ROOT)}：{ud_obj.relative_to(ROOT)} 已存在")
    if legacy_rel.is_dir() and not ud_rel.exists():
        ud_rel.parent.mkdir(parents=True, exist_ok=True)
        shutil.move(str(legacy_rel), str(ud_rel))
        print(f"迁移 {legacy_rel.relative_to(ROOT)} -> {ud_rel.relative_to(ROOT)}")


def migrate_defconfig_layout() -> None:
    """proj 分档 defconfig 命名迁移；out/.config → out/<variant>/.config。"""
    for old_name, new_name in (
        ("project_userdebug_defconfig", "project_defconfig_userdebug"),
        ("project_user_defconfig", "project_defconfig_user"),
        ("userdebug_project_defconfig", "project_defconfig_userdebug"),
        ("user_project_defconfig", "project_defconfig_user"),
    ):
        old = ROOT / "proj" / old_name
        new = ROOT / "proj" / new_name
        if old.is_file() and not new.is_file():
            shutil.move(str(old), str(new))
            print(f"重命名 {old.relative_to(ROOT)} -> {new.relative_to(ROOT)}")
    legacy_dc = ROOT / "proj" / "project_defconfig"
    if legacy_dc.is_file():
        for variant in BUILD_VARIANTS:
            dest = project_defconfig_path(variant)
            if not dest.is_file():
                dest.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(legacy_dc, dest)
                print(
                    f"迁移 {legacy_dc.relative_to(ROOT)} -> {dest.relative_to(ROOT)}"
                )
        if all(project_defconfig_path(v).is_file() for v in BUILD_VARIANTS):
            legacy_dc.unlink()
            print(f"删除旧 defconfig：{legacy_dc.relative_to(ROOT)}")
    legacy_cfg = ROOT / "out" / ".config"
    if legacy_cfg.is_file():
        ud_cfg = out_config_path(BUILD_VARIANT_USERDEBUG)
        if not ud_cfg.is_file():
            ud_cfg.parent.mkdir(parents=True, exist_ok=True)
            shutil.move(str(legacy_cfg), str(ud_cfg))
            print(f"迁移 {legacy_cfg.relative_to(ROOT)} -> {ud_cfg.relative_to(ROOT)}")
        else:
            legacy_cfg.unlink()
            print(f"删除旧 out/.config（{ud_cfg.relative_to(ROOT)} 已存在）")


def migrate_objects_dir_name() -> None:
    """各档位 out/<variant>/object → out/<variant>/objects。"""
    for variant in BUILD_VARIANTS:
        old = out_profile_root(variant) / "object"
        new = out_object_dir(variant)
        if not old.is_dir():
            continue
        if new.exists():
            print(
                f"跳过迁移 {old.relative_to(ROOT)}：{new.relative_to(ROOT)} 已存在"
            )
            continue
        new.parent.mkdir(parents=True, exist_ok=True)
        shutil.move(str(old), str(new))
        print(f"迁移 {old.relative_to(ROOT)} -> {new.relative_to(ROOT)}")
    # 迁完后把默认档位的 OUT_* 指到新目录
    set_build_variant(active_build_variant())


def migrate_object_layout() -> None:
    """把旧布局（镜像与 lib 在 object 根下）迁到 object/target/；纠正 target/.buildflags。"""
    wrong_bf = OUT_ARTIFACT_DIR / ".buildflags"
    right_bf = OUT_OBJECT_DIR / ".buildflags"
    if wrong_bf.is_file():
        if right_bf.is_file():
            wrong_bf.unlink()
        else:
            shutil.move(str(wrong_bf), str(right_bf))
        print(f"迁移 {wrong_bf.relative_to(ROOT)} -> {right_bf.relative_to(ROOT)}")
    if not OUT_OBJECT_DIR.is_dir():
        return
    OUT_ARTIFACT_DIR.mkdir(parents=True, exist_ok=True)
    (OUT_ARTIFACT_DIR / "lib").mkdir(parents=True, exist_ok=True)
    legacy_lib = OUT_OBJECT_DIR / "lib"
    if legacy_lib.is_dir():
        for child in legacy_lib.iterdir():
            dest = OUT_ARTIFACT_DIR / "lib" / child.name
            if child.is_file() and not dest.exists():
                shutil.move(str(child), str(dest))
        try:
            legacy_lib.rmdir()
        except OSError:
            pass
    for suffix in (".elf", ".hex", ".bin", ".map"):
        for path in OUT_OBJECT_DIR.glob(f"*{suffix}"):
            dest = OUT_ARTIFACT_DIR / path.name
            if path.is_file() and not dest.exists():
                shutil.move(str(path), str(dest))
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
def active_modules() -> list[str]:
    return [name for name in STATIC_LIB_DIRS if (ROOT / name).is_dir()]
def expected_link_chain() -> str:
    return " → ".join(name for name in NORMAL_LINK_LIBS if name in active_modules())
def read_text(path: Path) -> str:
    if not path.is_file():
        return ""
    return path.read_text(encoding="utf-8")
def update() -> None:
    print(
        "更新构建描述：Kconfig / Makefile / project_defconfig_{user,userdebug} / "
        "out/<variant>/.config / toolchain.make"
    )
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
    migrate_out_profile_layout()
    migrate_defconfig_layout()
    migrate_objects_dir_name()
    migrate_object_layout()
    remove_stale_build_files()
    for directory in walk_dirs():
        if not needs_build_metadata(directory):
            continue
        write_kconfig(directory, info["project_name"])
        if directory != ROOT:
            write_dir_makefile(directory)
    write_root_makefile()
    write_toolchain(info)
    write_defconfig(info)
    write_config()
    restamp_generated(info)
    import dts as dts_mod

    dts_mod.sync_board_dts(info)
    import verify as verify_mod

    verify_mod.verify(check_symbols=False)

