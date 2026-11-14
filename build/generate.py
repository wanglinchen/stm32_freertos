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
        "# 头文件目录和 .d 依赖由各层 Makefile 管理。",
        "# 若 build/build.py install-toolchain 已安装到 tool/，优先使用工程内工具链。",
        "# 可由 build/build.py set-buildenv 设置 STM32_TOOLCHAIN_ROOT；Windows 优先 tool/win_x86_64。",
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
        # 默认构建是调试版：-Og 既不是 -O0 的逐句搬运，也不是 -O2 的激进重排，
        # 反汇编级断言（tick-source 的 `add #1`、iwdg-str 的 `str`、observ 的 `bl`）
        # 依赖的正是这种可读且不内联的落地形态。发布版要更小，用 `make release`
        # （即 OPT=-Os DEBUG=-g1），这里用 ?= 让命令行可覆盖。
        "OPT ?= -Og",
        "DEBUG ?= -g3",
        "# 可复现构建：把工程根的绝对前缀折叠成 '.'。configASSERT 把 __FILE__ 传给 vAssertFailed，",
        "# -g3 又把源码路径写进 debug 段，两者都会让同一提交在不同目录/机器编出不同字节的镜像",
        "# （实测 Windows 与 Linux 的 .text 相差 136 字节，差异全部落在 .rodata 的路径字符串）。",
        "REPRO := -ffile-prefix-map=$(PROJECT_ROOT)=.",
        "# 编译期禁用 newlib 的分配入口：newlib 堆只有 512 B，而 __heap_end__ 到 _estack",
        "# 之间约 159 KB 空闲 RAM（栈从 _estack 向下长，并不紧贴堆），大块 malloc 越界后",
        "# 既不会撞到栈也不会报错。用 -include 注入，避免依赖每个编译单元记得包含；",
        "# 不进 ASFLAGS（汇编没有这个概念）。守卫头自己先包含 <stdlib.h>/<string.h> 再 poison，",
        "# 否则会在系统头的函数声明处报错而不是在真正的调用点报错。",
        "ALLOC_GUARD := -include $(PROJECT_ROOT)/osl/inc/osl_alloc_guard.h",
        f"CPU_FLAGS := {cpu_flags(info)}",
        "CHIP_DEFS = $(foreach config,$(filter CONFIG_STM32%,$(.VARIABLES)),$(if $(filter y,$($(config))),-D$(subst XX,xx,$(patsubst CONFIG_%,%,$(config)))))",
        "CFLAGS = $(CPU_FLAGS) $(CHIP_DEFS) $(OPT) $(DEBUG) $(REPRO) $(ALLOC_GUARD) -std=c11 -ffunction-sections -fdata-sections -fno-common -Wall -Wextra -Werror -fstack-usage $(INCLUDES)",
        "CXXFLAGS = $(CPU_FLAGS) $(CHIP_DEFS) $(OPT) $(DEBUG) $(REPRO) $(ALLOC_GUARD) -std=c++17 -ffunction-sections -fdata-sections -fno-exceptions -fno-rtti -Wall -Wextra -Werror -fstack-usage $(INCLUDES)",
        "ASFLAGS = $(CPU_FLAGS) $(CHIP_DEFS) $(OPT) $(DEBUG) $(REPRO) $(INCLUDES)",
        f"LDSCRIPT := $(PROJECT_ROOT)/{rel}",
        "# -u _sbrk：make 在 --start-group 扫描时还没有 _sbrk 这个未定义符号（那时只有 malloc 未定义），",
        "# 等 -lc 把 _sbrk_r 拉进来，-lnosys 的空桩已经先满足了它，libosl.a 里不会再有提取机会。",
        "# 显式声明 _sbrk 为未定义，才会让 libosl.a 的那份强定义胜出；顺带保证它不被 --gc-sections 丢掉。",
        "# -u __malloc_lock 同理：不强制引用的话，libc_nano.a 的 mlock.o（空锁）会先满足 malloc 的引用，",
        "# 任务里并发 malloc 就会踩坏堆。",
        "# -u _write 是为了让 osl 的 _write（把最近输出留在 g_last_write 里给调试器）真的进镜像：",
        "# 当前没有任何调用方，--gc-sections 会把 _write 和 g_last_write 一起丢掉（实测 nm 里两者都无），",
        "# 那样“留最近输出”就是空承诺。verify 会断言镜像里存在 _write。",
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
        "BUILD_DIR := $(PROJECT_ROOT)/out/object",
        "TARGET_DIR := $(BUILD_DIR)/target",
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
        ".PHONY: all clean modules release " + " ".join(modules),
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
            # 发布构建：只换优化与调试级别。必须带 -B —— `OPT` / `DEBUG` 是命令行
            # 覆盖，不改 toolchain.make 的 mtime，而 .o 只依赖 FLAG_DEPS 那个文件，
            # 所以不带 -B 时 make 会判定 elf 已是最新，用 -Og 编出来的旧对象重链一遍，
            # 产物根本没变（实测 text 仍 10980、只打印 up to date）。
            # 体积上限（ARTIFACT_TEXT_CEIL / ARTIFACT_FLASH_CEIL）由
            # `python build/build.py verify --artifacts` 在产物分支里判，
            # 不在这里判 —— make 只管编译链接，断言统一归创建器。
            "release:",
            "\t@$(MAKE) --no-print-directory -B -C $(PROJECT_ROOT) OPT=-Os DEBUG=-g1 all",
            '\t@echo "发布构建完成（-Os -g1，尺寸见上面的 SIZE 行）。"',
            '\t@echo "注意：3 条指令形态断言在 -Os 下会跳过（osl_reset_cause_capture 早退 / osl_panic_report 重入闸 / iwdg_wait_sync 符号）；"',
            '\t@echo "verify --artifacts 会逐条列出被跳过的判据，其余判据照跑。这些机制只在 -Og 下强制，发版前仍需人工核对一遍。"',
            "",
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
        "TARGET_DIR := $(BUILD_DIR)/target",
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
def write_generated_text(path: Path, text: str) -> None:
    """写入生成物；内容未变则不动文件，保留原 mtime。

    目录 Makefile 把 toolchain.make / out/.config 当成每个 .o 的前置依赖
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
def migrate_object_layout() -> None:
    """把旧布局（镜像与 lib 在 out/object 根下）迁到 out/object/target/；纠正 target/.buildflags。"""
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
    import verify as verify_mod

    verify_mod.verify(check_symbols=False)

