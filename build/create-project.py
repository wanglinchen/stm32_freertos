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
    "__pycache__",
}
SOURCE_SUFFIXES = {".c", ".cpp", ".cc", ".cxx", ".s", ".S"}
HEADER_SUFFIXES = {".h", ".hpp", ".hh"}
STATIC_LIB_DIRS = ("osl", "apl", "fml", "bsl", "hal")
NORMAL_LINK_LIBS = ("hal", "osl", "apl", "fml", "bsl")
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
        "OPT := -Og",
        "DEBUG := -g3",
        f"CPU_FLAGS := {cpu_flags(info)}",
        "CHIP_DEFS = $(foreach config,$(filter CONFIG_STM32%,$(.VARIABLES)),$(if $(filter y,$($(config))),-D$(subst XX,xx,$(patsubst CONFIG_%,%,$(config)))))",
        "CFLAGS = $(CPU_FLAGS) $(CHIP_DEFS) $(OPT) $(DEBUG) -std=c11 -ffunction-sections -fdata-sections -fno-common -Wall $(INCLUDES)",
        "CXXFLAGS = $(CPU_FLAGS) $(CHIP_DEFS) $(OPT) $(DEBUG) -std=c++17 -ffunction-sections -fdata-sections -fno-exceptions -fno-rtti -Wall $(INCLUDES)",
        "ASFLAGS = $(CPU_FLAGS) $(CHIP_DEFS) $(OPT) $(DEBUG) $(INCLUDES)",
        f"LDSCRIPT := $(PROJECT_ROOT)/{rel}",
        "LDFLAGS = $(CPU_FLAGS) -T$(LDSCRIPT) -nostartfiles -Wl,--gc-sections --specs=nano.specs --specs=nosys.specs",
        "LDLIBS := -lc -lm -lgcc",
        "",
    ]
    (ROOT / "toolchain.make").write_text("\n".join(lines), encoding="utf-8")


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
    (directory / "Kconfig").write_text("\n".join(lines), encoding="utf-8")


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
            f"{obj}: {src}",
            "\t@mkdir -p $(dir $@)",
            f'\t@echo "{tag} $<"',
            f"\t{tool} -MMD -MP -MF {dep} -c $< -o $@",
            "\t@python3 -c 'import os,sys,time; t=time.time(); [os.utime(p,(t,t)) for p in sys.argv[1:]]' $@ " + dep,
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
    (directory / "Makefile").write_text("\n".join(lines), encoding="utf-8")


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
        link += f" {libs}"
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
    (ROOT / "Makefile").write_text("\n".join(lines), encoding="utf-8")


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
    path.write_text(text + user_block + suffix, encoding="utf-8")


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
    config.write_text(parse_defconfig(read_text(defconfig)), encoding="utf-8")


def restamp_generated(info: dict) -> None:
    """Windows 盘上新文件的时间戳会超前于 WSL 时钟，make 会因此报 clock skew。"""
    now = time.time()
    paths = [
        ROOT / "proj" / "project-info.json",
        ROOT / "proj" / "project_defconfig",
        ROOT / "toolchain.make",
        ROOT / "out" / ".config",
    ]
    for directory in walk_dirs():
        paths.append(directory / "Makefile")
        paths.append(directory / "Kconfig")
    for path in paths:
        if path.is_file():
            os.utime(path, (now, now))


def remove_stale_build_files() -> None:
    """排除目录里若残留自动生成的 Makefile/Kconfig，update 时删掉。"""
    for name in EXCLUDE_DIRS:
        directory = ROOT / name
        if not directory.is_dir():
            continue
        for fname in ("Makefile", "Kconfig", ".config"):
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
    if project_name.casefold() != ROOT.name.casefold():
        errors.append(
            f"proj/project-info.json 的 project_name={project_name!r} 与工程根目录名 {ROOT.name!r} 不一致"
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


def verify() -> None:
    print("自检：代码生成物与 .skills 规格")
    errors = collect_verify_errors()
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
    verify()


def copy_outputs() -> None:
    out = ROOT / "out"
    target = out / "target"
    if target.exists():
        shutil.rmtree(target)
    target.mkdir(parents=True)
    artifacts = [
        path
        for path in (out / "object").rglob("*")
        if path.is_file() and path.suffix in {".bin", ".hex", ".elf", ".a"} and target not in path.parents
    ]
    if not artifacts:
        raise SystemExit("编译结束，但 out/ 下没有 .elf/.hex/.bin/.a 可复制")
    for path in artifacts:
        dest = target / path.name
        shutil.copy2(path, dest)
        print(f"复制 {path.relative_to(ROOT)} -> {dest.relative_to(ROOT)}")


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
    copy_outputs()
    setenv.remove_msys_tmp()


def clean() -> None:
    print("清理编译产物")
    makefile = ROOT / "Makefile"
    if makefile.is_file():
        subprocess.run(["make", "-C", str(ROOT), "clean"], check=False)
    out = ROOT / "out"
    if out.exists():
        shutil.rmtree(out)


def main(argv: list[str] | None = None) -> None:
    parser = argparse.ArgumentParser(
        description="生成 STM32 工程描述并编译。",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""\
子命令说明:
  update  扫描源码树，生成 Kconfig、Makefile、toolchain.make、project_defconfig、out/.config，并自检
  verify  检查生成物、层目录、链接顺序与 .skills/工程规格.md、SKILL.md 是否一致
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
    args = parser.parse_args(argv)
    if args.option == "build":
        build()
    elif args.option == "update":
        update()
    elif args.option == "verify":
        verify()
    else:
        clean()


if __name__ == "__main__":
    main()
