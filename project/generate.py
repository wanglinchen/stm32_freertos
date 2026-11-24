"""`update`：扫描源码树并写出 Kconfig / Makefile / toolchain / defconfig，
并由 ``docs/hardware/pinmap/gen_dts.py``（源自 stm32-lrtos）生成 default / overlay
两份 dtsi 落在 ``project/dts/``，合成 ``out/dts/apollo.dts`` 并编成 ``out/dts/apollo.dtb``。

DTS 是运行期唯一真源：固件经 ``.incbin`` 内嵌 ``.dtb``，启动时由 ``bsl_fdt`` 解析。
本文件不生成任何 C 头，也不再用正则解析 DTS 文本。
"""
from __future__ import annotations

import argparse
import dataclasses
import json
import os
import re
import subprocess
import sys
import time
import zipfile
import xml.etree.ElementTree as ET
from dataclasses import dataclass
from pathlib import Path

# 本文件在 project/ 下，contracts 仍在 build/：先把 build 加入搜索路径
_BUILD_DIR = Path(__file__).resolve().parent.parent / "build"
if str(_BUILD_DIR) not in sys.path:
    sys.path.insert(0, str(_BUILD_DIR))


# --- 工程共享常量与路径约定（原 build/contracts.py）---
# build / flash / debug 都从这里（或经 from generate import *）取路径与构建常量。
ROOT = Path(__file__).resolve().parent.parent

BUILD_VARIANT_USER = "user"
BUILD_VARIANT_USERDEBUG = "userdebug"
BUILD_VARIANTS = (BUILD_VARIANT_USERDEBUG, BUILD_VARIANT_USER)
DEFAULT_BUILD_VARIANT = BUILD_VARIANT_USERDEBUG
BUILD_VARIANT_MAKE_EXPR = "BUILD_VARIANT ?= userdebug"
OUT_OBJECTS_DIRNAME = "objects"
BUILD_DIR_MAKE_EXPR = (
    f"BUILD_DIR := $(PROJECT_ROOT)/out/$(BUILD_VARIANT)/{OUT_OBJECTS_DIRNAME}"
)
KCONFIG_INCLUDE_MAKE_EXPR = (
    "-include $(PROJECT_ROOT)/out/$(BUILD_VARIANT)/.config"
)
KCONFIG_FLAG_DEP_MAKE = (
    "$(wildcard $(PROJECT_ROOT)/out/$(BUILD_VARIANT)/.config)"
)


DEFCONFIG_STEM = "stm32f429_defconfig"


def project_defconfig_basename(variant: str) -> str:
    if variant not in BUILD_VARIANTS:
        raise ValueError(f"未知 BUILD_VARIANT: {variant!r}")
    return f"{DEFCONFIG_STEM}_{variant}"


def project_defconfig_dir() -> Path:
    return ROOT / "project" / "config"


# 公共项与档位差异拆成两份文件：
#   stm32f429_defconfig_common       —— 由源码树扫描生成，全部公共项 + 公共 USER_CUSTOM 区
#   stm32f429_defconfig_user{debug}  —— 只放档位差异（当前为空壳）+ 档位自己的 USER_CUSTOM 区
# 合并顺序由 write_config() 保证：common 在前、档位在后，同名符号后者覆盖。
# 为什么要拆：此前两档各 139 行、内容逐字相同（body 只依赖源码树，与档位无关），
# 改一个开关要同步改两处，漏一处就是「两档行为不一致且没有任何信号」。
COMMON_DEFCONFIG_SUFFIX = "common"


def project_common_defconfig_path() -> Path:
    return project_defconfig_dir() / f"{DEFCONFIG_STEM}_{COMMON_DEFCONFIG_SUFFIX}"


DEFAULT_BOARD_DTS_REL = "out/dts/apollo.dts"


def project_defconfig_path(variant: str = DEFAULT_BUILD_VARIANT) -> Path:
    return project_defconfig_dir() / f"{project_defconfig_basename(variant)}"


def out_config_path(variant: str = DEFAULT_BUILD_VARIANT) -> Path:
    return out_profile_root(variant) / ".config"
# userdebug：可调试优化 + 完整 DWARF（宏、内联展开）；user：体积优先、不带调试节。
VARIANT_MAKE_OPTS: dict[str, dict[str, str]] = {
    BUILD_VARIANT_USERDEBUG: {"OPT": "-Og", "DEBUG": "-g3"},
    BUILD_VARIANT_USER: {"OPT": "-Os", "DEBUG": "-g0"},
}
VARIANT_LAYER_LOG_DEFCONFIG: dict[str, dict[str, str]] = {
    BUILD_VARIANT_USERDEBUG: {
        "CONFIG_BSL_LOG_LEVEL": "2",
        "CONFIG_FML_LOG_LEVEL": "2",
        "CONFIG_APL_LOG_LEVEL": "2",
        "CONFIG_OSL_LOG_LEVEL": "2",
    },
    BUILD_VARIANT_USER: {
        "CONFIG_BSL_LOG_LEVEL": "1",
        "CONFIG_FML_LOG_LEVEL": "1",
        "CONFIG_APL_LOG_LEVEL": "1",
        "CONFIG_OSL_LOG_LEVEL": "1",
    },
}


def out_profile_root(variant: str) -> Path:
    return ROOT / "out" / variant


def out_object_dir(variant: str = DEFAULT_BUILD_VARIANT) -> Path:
    return out_profile_root(variant) / OUT_OBJECTS_DIRNAME


def out_artifact_dir(variant: str = DEFAULT_BUILD_VARIANT) -> Path:
    return out_object_dir(variant) / "target"


def out_release_dir(variant: str = DEFAULT_BUILD_VARIANT) -> Path:
    return out_profile_root(variant) / "release"


_active_build_variant = DEFAULT_BUILD_VARIANT


OUT_OBJECT_DIR = out_object_dir(DEFAULT_BUILD_VARIANT)
OUT_ARTIFACT_DIR = out_artifact_dir(DEFAULT_BUILD_VARIANT)
OUT_RELEASE_DIR = out_release_dir(DEFAULT_BUILD_VARIANT)
RELEASE_BUCKET_DAYS = 5
RELEASE_DIR_NAME_RE = re.compile(r"^\d{4}_\d{2}_\d{2}-\d{2}_\d{2}_\d{2}$")
EXCLUDE_DIRS = {
    "debug",
    "docs",
    "flash",
    "project",
    "tool",
    "build",
    "workspace",
    "out",
    # tests 若存在则只放 host 侧辅助文件；必须让 update 不要往里生成 Makefile/Kconfig，
    # 否则会被误当成「排除目录里的生成物」。
    "tests",
    "__pycache__",
}
SOURCE_SUFFIXES = {".c", ".cpp", ".cc", ".cxx", ".s", ".S"}
HEADER_SUFFIXES = {".h", ".hpp", ".hh"}
STATIC_LIB_DIRS = ("osl", "apl", "fml", "bsl", "hal")
NORMAL_LINK_LIBS = ("apl", "fml", "bsl", "osl", "hal")
LAYER_DEPENDENCIES = {
    # apl 是编排层：可以调功能模块（fml）与 OS 抽象（osl），也可以调板级支持（bsl）——
    # 「HAL 初始化 + 系统时钟 + 主频刷新」属于板级事实，归 bsl；apl 直连 hal 则越级。
    "apl": ("fml", "bsl", "osl"),
    # fml 只写功能逻辑（LED 闪烁节奏、按键消抖），不碰寄存器与引脚映射：板级事实一律经 bsl。
    # 这条边原来还写着 hal，但 fml 四个源/头文件对 HAL_*、GPIO*、RCC*、__HAL_*、
    # stm32f4xx_* 全部零命中——敞开它会让人「顺手 include 一个 hal 头」而不被拦。
    "fml": ("bsl", "osl"),
    "bsl": ("hal",),
    "osl": ("hal",),
}
LAYER_DIRS = (
    "apl",
    "fml",
    "bsl",
    "hal",
    "osl",
    "docs",
    "project",
    "build",
    "tool",
    "workspace",
)
USER_BEGIN = "# >>> USER_CUSTOM"
USER_END = "# <<< USER_CUSTOM"
GEN_BANNER = "# 自动生成，勿手改。刷新：python3 build/build.py update"
ASSIGN_RE = re.compile(r"^\s*(CONFIG_[A-Z0-9_]+)\s*=")
CONFIG_VALUE_RE = re.compile(r"^\s*(CONFIG_[A-Z0-9_]+)\s*=\s*(.*?)\s*$")
DISABLED_RE = re.compile(r"^\s*#\s*(CONFIG_[A-Z0-9_]+)\s+is not set\s*$")
# FreeRTOSConfig.h 里 `configUSE_*` 直接决定编译单元要不要存在的三个文件。
#
# `configUSE_TIMERS = 0` 时 timers.c 内部几乎被 `#if` 掉，产物里没有任何符号
# （`--gc-sections` 也把它丢光），但**编译单元照样参与编译**：每个 job 都在编一份
# 永远进不了镜像的文件，而按符号在场的检查因为符号不在而一律通过 —— 这就是「编了没链」
# 的典型形态，只能在编译单元层看得见。
FREERTOS_CONFIG_GATED = (
    ("timers.c", "configUSE_TIMERS"),
    ("event_groups.c", "configUSE_EVENT_GROUPS"),
    ("stream_buffer.c", "configUSE_STREAM_BUFFERS"),
)
RESTAMP_DIRTY: list[Path] = []
SKILLS_DIR = ROOT / ".skills"
SPEC_DOC = SKILLS_DIR / "工程规格.md"
SKILL_DOC = SKILLS_DIR / "SKILL.md"


def is_skipped(path: Path) -> bool:
    name = path.name
    if name.startswith(".") or name in EXCLUDE_DIRS:
        return True
    try:
        rel = path.relative_to(ROOT)
    except ValueError:
        return True
    return any(part.startswith(".") or part in EXCLUDE_DIRS for part in rel.parts)

def sanitize(name: str) -> str:
    text = re.sub(r"[^0-9A-Za-z]+", "_", name.upper()).strip("_") or "UNNAMED"
    text = re.sub(r"_+", "_", text)
    if text[0].isdigit():
        text = "N_" + text
    return text

def _strip_comments_keep_offsets(text: str) -> str:
    """剥掉注释但保留字符偏移与换行数，便于用偏移反查行号。"""
    text = re.sub(
        r"/\*.*?\*/",
        lambda m: re.sub(r"[^\n]", " ", m.group(0)),
        text,
        flags=re.S,
    )
    return re.sub(r"//[^\n]*", lambda m: " " * len(m.group(0)), text)


@dataclass(frozen=True)
class PinmapEntry:
    label: str
    alt_label: str

@dataclass(frozen=True)
class BoardDts:
    dts_path: Path
    compatible: str
    hse_hz: int
    lse_hz: int | None
    led_port: str
    led_pins: tuple[int, ...]
    led_names: tuple[str, ...]
    led_active_low: bool
    key_names: tuple[str, ...]
    keys: tuple[tuple[str, int, bool, str], ...]  # port, pin, pressed_high, label
    beep_via: str  # "gpio" | "pcf8574"
    beep_port: str
    beep_pin: int
    beep_active_high: bool
    pcf8574_addr7: int | None
    pcf8574_bit: int | None
    i2c_scl_port: str | None
    i2c_scl_pin: int | None
    i2c_sda_port: str | None
    i2c_sda_pin: int | None


@dataclass(frozen=True)
class BslBindingMeta:
    compatible: str
    node: str
    label: str | None
    dev_id: str
    driver: str
    init_level: str
    name: str
    order: int
    # api_type / api_sym：这两条决定"该设备的 api 是什么结构类型、哪个符号"，
    # 生成期据此产出类型化访问器，消费方不再把 driver->api 强转。两条要么都有、
    # 要么都没有（有 api 的设备才写）；只有一条时 _parse_binding_bsl_block 报错。
    api_type: str | None = None
    api_sym: str | None = None
    # optional：该设备缺失时是否允许降级运行。必须由 binding 显式给出——
    # 不写就代表"没人想过这个设备缺失时该不该继续跑"，那正是这个字段要消灭的
    # 形态（"beep 可选"早前只写在 fml_button.c 的一个 if 里）。
    optional: bool = True

@dataclass(frozen=True)
class BoardRegistryEntry:
    """注册表一行；字段顺序即 `M(...)` 的参数顺序。

    历史：这里曾有 `compat_enum` 字段（与 binding 的 `compat_id:` 配套），支撑一套
    运行期「按 compatible 查设备」的 API（`bsl_device_find_by_compat`）。那套 API
    全仓零调用 —— 绑定在生成期就完成了（binding YAML → 静态表），运行期不需要再按
    compatible 查一次。删掉字段与枚举，避免后来者误以为查找要走 `BSL_COMPAT_*`。
    """
    dev_enum: str
    name: str
    init_level_enum: str
    driver: str
    compatible: str
    node: str
    # api 两条不在 M(...) 里（M 只传前 4 个参数），它们用于生成类型化访问器。
    api_type: str | None = None
    api_sym: str | None = None
    # 与 binding 的 optional 同义；作为 M(...) 的第 5 个参数进设备表。
    optional: bool = True


@dataclass(frozen=True)
class BoardDtsHeaderFacts:
    compatible: str | None
    hse_hz: int
    led_port: str
    led_pins: tuple[int, ...]
    led_active_low: bool
    keys: tuple[tuple[str, int, bool], ...]
    beep_hw: tuple[str, str | int, int]


# ---------------------------------------------------------------------------
# CLI（本脚本新增，原 generate.py 无）
# ---------------------------------------------------------------------------


DEFAULT_BOARD_INFO: dict[str, str] = {
    "dts": DEFAULT_BOARD_DTS_REL,
    "compatible": "alientek,apollo-stm32f429",
    "platform_pinmap_xlsx": (
        "docs/hardware/pinmap/stm32f429_default_gpio_pinmap.xlsx"
    ),
    "board_pinmap_xlsx": (
        "docs/hardware/pinmap/apollo_alternate_gpio_pinmap.xlsx"
    ),
}


def default_info() -> dict:
    """project-info.json 的默认内容：工程名 + 芯片参数（板级绑定在 DEFAULT_BOARD_INFO）。"""
    return {
        "project_name": ROOT.name,
        "cpu": {
            "model": "STM32F429IGT6",
            "arch": "cortex-m4",
            "fpu": "fpv4-sp-d16",
            "float_abi": "hard",
            "flash_kb": 1024,
            "sram_kb": 192,
            "ccm_kb": 64,
        },
    }
def ensure_layout() -> None:
    for name in LAYER_DIRS:
        (ROOT / name).mkdir(parents=True, exist_ok=True)
def _normalize_info(data: dict) -> dict:
    """补齐内存内的默认键；不写回文件。project-info.json 只存工程名 + 芯片参数。"""
    base = default_info()
    cpu = data.setdefault("cpu", {})
    data.setdefault("project_name", ROOT.name)
    for key, value in base["cpu"].items():
        cpu.setdefault(key, value)
    board = data.setdefault("board", {})
    for key, value in DEFAULT_BOARD_INFO.items():
        board.setdefault(key, value)
    return data


def load_info() -> dict:
    path = ROOT / "project" / "project-info.json"
    if not path.is_file():
        path.write_text(
            json.dumps(default_info(), indent=2) + "\n", encoding="utf-8", newline="\n"
        )
    return _normalize_info(json.loads(path.read_text(encoding="utf-8")))
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

    这里只看「有没有东西要被这层构建描述管住」：有源文件要编，或还有子目录要下发。
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
        "CFLAGS = $(CPU_FLAGS) $(CHIP_DEFS) $(OPT) $(DEBUG) $(REPRO) $(ALLOC_GUARD) -std=c11 -ffunction-sections -fdata-sections -fno-common -Wall -Wextra -Werror -fstack-usage -Wmissing-prototypes -Wstrict-prototypes $(INCLUDES)",
        "# 第三方源码（fml/src/fatfs 等）用这一组：保留 -Wall 便于看问题，但去掉 -Werror 与"
        " -Wmissing-prototypes —— 上游代码没有本工程那条「每个非 static 函数都要有原型」的约束，"
        " 而为了过 -Werror 去改第三方源码，下次换版本就把补丁丢了。",
        "CFLAGS_PLAIN = $(CPU_FLAGS) $(CHIP_DEFS) $(OPT) $(DEBUG) $(REPRO) $(ALLOC_GUARD) -std=c11 -ffunction-sections -fdata-sections -fno-common -Wall $(INCLUDES)",
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
def is_third_party_source(source: Path) -> bool:
    """第三方源码目录：目前只有 fml/src/fatfs（FatFs R0.15）。

    判据是目录名而不是文件名单：FatFs 只有 ff.c 一个编译单元，但以后
    ffunicode.c 之类的可选单元一旦启用也要走同一套宽松标志。
    """
    return "fatfs" in source.parts


def compile_tool(source: Path) -> tuple[str, str, str]:
    if source.suffix == ".c":
        if is_third_party_source(source):
            return "CC", "$(CC) $(CFLAGS_PLAIN)", dep_path(source)
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
        # `.incbin` 嵌进来的 DTB 不是 `.include`，as 的 -MMD 不跟踪它，所以必须显式
        # 列成前置依赖：否则改 DTS → update 重生成 apollo.dtb 之后，本对象仍被判
        # up-to-date，镜像里继续嵌着旧整树（2026-11-22 现场：beep 极性改高有效后
        # 刷进去仍长鸣，objcopy .dtb 段与 out/dts/apollo.dtb 在 offset 1479 处
        # 差一个字节才被发现）。
        incbin_dep = (
            " $(PROJECT_ROOT)/out/dts/apollo.dtb"
            if source.name == "board_dtb.s"
            else ""
        )
        body = [
            f"LOCAL_OBJS += {obj}",
            f"{obj}: {src}{incbin_dep} $(FLAG_DEPS)",
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
    # -u / --specs 这类参数后，若 .o 恰好都比 elf 旧，make 会跳过重链，之后对 elf 做的
    # 检查就在旧镜像上跑（结论看着"通过"，其实没验新产物）。
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
    """写 common（公共项）与各档位差异层。

    common 的 body 照旧由源码树扫描生成；它的 USER_CUSTOM 区（HAL 裁剪等公共决策）
    由 update 原样保留。档位文件只写表头 + 自己的 USER_CUSTOM 区 —— 当前两档无差异，
    以后要分档就把差异写进档位文件的用户区（protected 机制保证 common 的 body 不会
    再给它写一行 `=y` 去打架）。
    """
    common = project_common_defconfig_path()
    existing = common.read_text(encoding="utf-8") if common.is_file() else ""
    user_block, suffix = split_user_region(existing)
    protected = protected_symbols(user_block)
    write_generated_text(
        common, write_defconfig_body(info, protected) + user_block + suffix
    )

    for variant in BUILD_VARIANTS:
        path = project_defconfig_path(variant)
        existing = path.read_text(encoding="utf-8") if path.is_file() else ""
        user_block, suffix = split_user_region(existing)
        header = [
            GEN_BANNER,
            f"# 档位差异层（{variant}）：公共项见 {common.name}",
            "# build 时按「common → 本档」顺序合并，同名符号本档优先。",
            "# 当前两档无差异项；需要分档时写进下面的 USER_CUSTOM 区。",
        ]
        write_generated_text(path, "\n".join(header) + "\n" + user_block + suffix)
def parse_defconfig(
    text: str, *, source_rel: str | None = None
) -> str:
    if source_rel is None:
        source_rel = (
            project_common_defconfig_path().relative_to(ROOT).as_posix()
            + " + "
            + project_defconfig_path(DEFAULT_BUILD_VARIANT).relative_to(ROOT).as_posix()
        )
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
    common = project_common_defconfig_path()
    for variant in BUILD_VARIANTS:
        defconfig = project_defconfig_path(variant)
        if not defconfig.is_file():
            continue
        parts: list[str] = []
        rels: list[str] = []
        if common.is_file():
            parts.append(read_text(common))
            rels.append(common.relative_to(ROOT).as_posix())
        parts.append(read_text(defconfig))
        rels.append(defconfig.relative_to(ROOT).as_posix())
        config = out_config_path(variant)
        config.parent.mkdir(parents=True, exist_ok=True)
        write_generated_text(
            config,
            parse_defconfig("\n".join(parts), source_rel=" + ".join(rels)),
        )
def write_generated_text(path: Path, text: str) -> None:
    """写入生成物；内容未变则不动文件，保留原 mtime。

    目录 Makefile 把 toolchain.make / out/<variant>/.config 当成每个 .o 的前置依赖
    （编译参数或 Kconfig 一变就必须重编）。因此这里必须“没变就不写”，
    否则 update 每次刷新这两个文件的 mtime，会退化成每轮全量重编。

    必须显式写 newline="\\n"：write_text 默认 newline=None，在 Windows 上会把
    \\n 翻译成 os.linesep（\\r\\n）。也就是说 69 个生成物的 CRLF 不是 core.autocrlf
    造成的，是这里自己在写 —— 只在 .gitattributes 里钉 eol=lf 而不管生成端，
    每次 update 都会把 LF 打回 CRLF，行尾检查每轮都会把它报出来。
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

# 板级 DTS / pinmap / 设备注册表 / 运行期事实的实现全部在本文件内
# （gen_dts.py 负责 pinmap → dtsi，sync_dts() 负责合成单树 + dtc）。

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

def active_modules() -> list[str]:
    return [name for name in STATIC_LIB_DIRS if (ROOT / name).is_dir()]
def read_text(path: Path) -> str:
    if not path.is_file():
        return ""
    return path.read_text(encoding="utf-8")
PINMAP_SCRIPT = Path("docs") / "hardware" / "pinmap" / "gen_dts.py"
PINMAP_DEFAULT_DTSI = "stm32f429_gpio_default.dtsi"
PINMAP_OVERLAY_DTSI = "apollo_gpio_overlay.dtsi"

# 板载设备节点：pinmap 表只描述「某一脚在板上的复用/连接」，不描述
# 「哪几个脚组成一个 LED / 按键组」。这份节点定义随 update 写进 out/dts/apollo.dts，
# 保证 DTS 仍是运行期唯一真源（bsl_fdt 按 compatible 找节点、读 gpios/极性）。
_BOARD_NODES = """\
    clocks {
        compatible = "brd,board-clocks";
        hse-frequency = <25000000>;
        lse-frequency = <32768>;
    };

    /* FMC Bank5 上的 SDRAM（W9825G6KH，16 位，32 MB）。
     * 寄存器值取自参考工程 sdram.c（[参考工程] sdram_init 的 sdctrlreg/sdtimereg），
     * 刷新计数按本工程时钟重算（[代码推算]）：HCLK=168 MHz、SDCLK=HCLK/2=84 MHz，
     *   COUNT = 64 ms × 84 MHz / 8192 - 20 = 636
     * 参考工程是 192 MHz（SDCLK 96 MHz）→ 730；直接抄 730 会让刷新偏慢。
     * fmc-pins 的第三个 cell 是 AF 号（FMC 全在 AF12），第二个 cell 是端口引脚掩码：
     *   PC0/2/3、PD0/1/8/9/10/14/15、PE0/1/7..15、PF0..5/11..15、PG0/1/2/4/5/8/15。 */
    sdram: sdram@c0000000 {
        compatible = "brd,fmc-sdram";
        reg = <0xc0000000 0x02000000>;
        sdcr = <0x19d9>;
        sdtr = <0x01115561>;
        refresh-count = <636>;
        fmc-pins = <&gpioc 0x000d 12>,
                   <&gpiod 0xc703 12>,
                   <&gpioe 0xff83 12>,
                   <&gpiof 0xf83f 12>,
                   <&gpiog 0x8137 12>;
    };

    /* 4.3 寸 800×480 RGB 屏（面板 ID 0x4384），LTDC 直驱。
     * 时序与引脚取自参考工程 lcd.c / ltdc.c 的 0X4384 分支（[参考工程]）：
     *   hsw/hbp/hfp = 48/88/40，vsw/vbp/vfp = 3/32/13。
     * 像素时钟（[代码推算]）：PLLSAI 输入 = HSE/PLLM = 25/25 = 1 MHz，
     *   VCO = 1 MHz × 396 = 396 MHz，R=3 → 132 MHz，DIVR=/4 → 33 MHz。
     *   PLLSAI 的 N/R 与主 PLL 的 N 无关，所以参考工程 192 MHz 下的这组值
     *   在本工程 168 MHz 下同样给出 33 MHz —— 这是推出来的，不是抄来的。
     * 帧缓冲不写地址：framebuffer 指向 sdram 节点，基址取那份 reg，
     *   于是「帧缓存在哪」只有一处事实。
     * ltdc-pins 第二个 cell 是端口掩码、第三个是 AF 号（LTDC 全在 AF14），
     *   同端口合并成一格：
     *   PF10 = DE；PI9 = VS、PI10 = HS、PI0..2 + PI4..7 = 数据；
     *   PG7 = CLK、PG6 + PG11 = 数据；PH9..15 = 数据。
     * backlight-gpios 第三 cell 是极性：0 = 高电平点亮（参考工程 LTDC_BL(1) 点亮）。 */
    /* panel 不带 unit address：它没有 reg/ranges，名字里带 @0 会被 dtc 判为
     * unit_address_vs_reg 警告，而本工程的 dtc 诊断是硬失败。 */
    panel: panel {
        compatible = "brd,rgb-panel";
        width = <800>;
        height = <480>;
        hsw = <48>;
        hbp = <88>;
        hfp = <40>;
        vsw = <3>;
        vbp = <32>;
        vfp = <13>;
        pixel-format = <2>;
        pllsai-n = <396>;
        pllsai-r = <3>;
        pllsai-divr = <1>;
        framebuffer = <&sdram>;
        backlight-gpios = <&gpiob 5 0>;
        ltdc-pins = <&gpiof 0x0400 14>,
                    <&gpioi 0x06f7 14>,
                    <&gpiog 0x08c0 14>,
                    <&gpioh 0xfe00 14>;
    };

    /* GT9147 电容触摸，走 PH6/PI3 的**软件 I2C**（与主 I2C 那条不是同一总线）。
     * 引脚/地址取自参考工程 ctiic.h + gt9xxx.h（[参考工程]）：
     *   SCL=PH6、SDA=PI3、RST=PI8、INT=PH7；7 位地址 0x14（写 0x28 / 读 0x29）。
     * 坐标范围不写在这里：它等于面板像素，唯一来源是 panel 节点的
     * width/height —— 写第二份 800/480，一旦与面板漂移，触点会被整片裁掉。
     * reset-gpios 第三 cell 是极性：1 = 低有效（GT9xxx 的 RST 低电平复位）。 */
    touch: touch {
        compatible = "goodix,gt9147";
        addr7 = <0x14>;
        max-points = <5>;
        reset-gpios = <&gpioi 8 1>;
        irq-gpios = <&gpioh 7 0>;
        scl-gpios = <&gpioh 6 0>;
        sda-gpios = <&gpioi 3 0>;
    };

    /* W25Q256（32 MB）挂在 SPI5：SCK=PF7 / MISO=PF8 / MOSI=PF9（AF5），
     * 片选 PF6（板上网络 F_CS，低有效）。三个引脚各写一条属性、值 (port, pin, af)：
     * 一个 mask 无法表达"哪一位是 MISO"，驱动只能靠硬编码位索引，换接法就整体错位。
     * 容量/页/扇区也在这里：擦写粒度错了的表现是"写进去再读变 0xFF"，
     * 比读不到更难查。 */
    nor: nor {
        compatible = "winbond,w25q256";
        spi-inst = <5>;
        sck = <&gpiof 7 5>;
        miso = <&gpiof 8 5>;
        mosi = <&gpiof 9 5>;
        cs-gpios = <&gpiof 6 1>;
        size = <0x02000000>;
        page-size = <256>;
        sector-size = <4096>;
    };

    leds {
        compatible = "brd,gpio-leds";
        active-low;
        led-names = "R", "G", "B";
        led_r: led-r {
            compatible = "brd,gpio-led";
            gpios = <&gpiob 1 1>;
            label = "LED_R";
        };
        led_g: led-g {
            compatible = "brd,gpio-led";
            gpios = <&gpiob 0 1>;
            label = "LED_G";
        };
        led_b: led-b {
            compatible = "brd,gpio-led";
            gpios = <&gpiob 2 1>;
            label = "LED_B";
        };
    };

    keys {
        compatible = "brd,gpio-keys";
        key-names = "WK_UP", "KEY0", "KEY1", "KEY2";
        key0: key-wk-up {
            compatible = "brd,gpio-key";
            gpios = <&gpioa 0 0>;
            pressed-high;
            label = "WK_UP";
            key-id = "WK_UP";
        };
        key1: key0 {
            compatible = "brd,gpio-key";
            gpios = <&gpioh 3 1>;
            pressed-low;
            label = "KEY0";
            key-id = "KEY0";
        };
        key2: key1 {
            compatible = "brd,gpio-key";
            gpios = <&gpioh 2 1>;
            pressed-low;
            label = "KEY1";
            key-id = "KEY1";
        };
        key3: key2 {
            compatible = "brd,gpio-key";
            gpios = <&gpioc 13 1>;
            pressed-low;
            label = "KEY2";
            key-id = "KEY2";
        };
    };

    apollo_i2c2: i2c {
        compatible = "brd,i2c-gpio";
        #address-cells = <1>;
        #size-cells = <0>;
        scl-gpios = <&gpioh 4 0>;
        sda-gpios = <&gpioh 5 0>;

        pcf8574: pcf8574@20 {
            compatible = "nxp,pcf8574";
            reg = <32>;
            gpio-controller;
            #gpio-cells = <2>;
        };

        /* 音频 codec：WM8978，7 位地址 0x1A（=26）。
         * 2026-11-25 实测（P6c-0，`i2c scan` / `i2c rd`）：这条总线上应答的是
         * 1a/1e/20/50/68，**没有 0x10**；而 0x1A 地址 ACK、读方向 NACK —— 正是
         * WM8978 的"只写 I2C"指纹。pinmap（手册 V1.1 表 1.5.1）把它写成 ES8388
         * 是错的，那三处注释待更正。
         * 只写器件：驱动不能用读回验证，只能说"14 次写事务全 ACK"。 */
        wm8978: wm8978@1a {
            compatible = "wolfson,wm8978";
            reg = <26>;
            status = "okay";
        };

        /* 传感器三件（P6d）。地址取自 P6c-0 的 `i2c scan`（2026-11-25）：
         * 1a/1e/20/50/68 五个应答里，0x1E 是 AP3216C、0x50 是 24C02、
         * 0x68 是 MPU9250（`i2c 68 r75=71` 的 WHO_AM_I 指纹），
         * 而 QMI8658A(0x6A/0x6B)、ST480MC(0x0C) 均无应答 —— 型号按实测改名。
         *
         * mag-addr 是 MPU9250 片内 AK8963 的地址：它**不是**总线上常驻的独立器件，
         * 只有先写 INT_PIN_CFG(0x37).BYPASS_EN 才出现。P6c-0 那次 `i2c 0c r00=nack`
         * 正是旁路未开时的正常现象，不是"板上没有磁力计"。 */
        mpu9250: imu@68 {
            compatible = "invensense,mpu9250";
            reg = <104>;
            mag-addr = <12>;
            status = "okay";
        };

        ap3216c: als@1e {
            compatible = "liteon,ap3216c";
            reg = <30>;
            status = "okay";
        };

        eeprom: eeprom@50 {
            compatible = "atmel,24c02";
            reg = <80>;
            status = "okay";
        };
    };

    beep {
        compatible = "brd,pcf8574-beep";
        /* 阿波罗 V2 硬件参考手册 V1.1 §2.1.24（第 36 页）：BEEP 直接接
         * PCF8574 的 P0，经板上驱动级后**低电平响**。
         * 2026-11-22 两次实测定标（本板唯一可靠的判据）：
         *   shadow=0x00（P0 输出低）→ 蜂鸣器长鸣；
         *   shadow=0xFF（P0 输出高）→ 静音。
         * 故本体是低有效，Linux gpios 语义里 flags bit0=1 才表示 active-low，
         * 这里必须写 1。早前误判成高有效（写 0），board_beep 于是把"关"
         * 写成低电平 → 一上电就长鸣，按键短鸣也听不出来。
         * 更早那次"长鸣"其实与极性无关：调试器 halt 恰好把 CPU 冻在
         * 上电短鸣那 80 ms 的"开"电平上。 */
        gpios = <&pcf8574 0 1>;
        i2c-bus = <&apollo_i2c2>;
    };

    debug_serial: debug_serial@40011000 {
        compatible = "brd,usart-ch340";
        status = "okay";
        reg = <0x40011000 0x400>;
        baud = <115200>;
        tx-port = "A";
        tx-pin = <9>;
        rx-port = "A";
        rx-pin = <10>;
        label = "USART1_CH340";
    };

    /* RTC：走时源与 LSE 频率是板级事实（晶振焊没焊、焊的是 32.768 k 还是
     * 32.000 k，换板只改这里）。寄存器基址与 BCD 编码是芯片事实，写在
     * bsl/rtc_source 里，不进 DTB。
     * clock-source 只认 "lse" / "lsi"：写别的形态 board_rt 会整节点拒收，
     * 而不是悄悄当 LSE —— 猜错要白等 3 s 起振、还会静默降级到分钟级精度。 */
    rtc {
        compatible = "brd,rtc";
        status = "okay";
        clock-source = "lse";
        lse-frequency = <32768>;
    };

    /* ADC 内部温度通道：只有 VREF+ 是板级事实（它决定出厂两点校准
     * TS_CAL1/TS_CAL2 是否还成立，手册口径是 3.3 V ±0.1 V）。
     * 通道号 16 与采样时间是手册事实，写在 bsl/adc 里。 */
    adc-temp {
        compatible = "brd,adc-temp";
        status = "okay";
        vref-mv = <3300>;
    };

    /* SAI1 (block A) 音频输出。引脚取自 pinmap（手册 V1.1 表 1.5.1，AF6）：
     *   PE2 = SAI1_MCLK_A、PE4 = SAI1_FS_A、PE5 = SAI1_SCK_A、
     *   PE6 = SAI1_SD_A（MCU → codec）、PE3 = SAI1_SD_B（录音，P6c 不用）。
     *   掩码 0x007C = bit2..bit6。
     * 时钟（[代码推算]）：主 PLL 输入 = HSE/PLLM = 25/25 = 1 MHz。PLLSAI 已被 LTDC
     *   占用（VCO 396 MHz、R=3），借它要先关 PLLSAI = 停掉屏的像素时钟，所以走
     *   空闲的 PLLI2S：N=393 → VCO 393 MHz，Q=8 → PLLI2SQ = 49.125 MHz；
     *   PLLI2SDIVQ=0（÷1）→ SAI_CK = 49.125 MHz；CR1.MCKDIV=4 → MCLK =
     *   SAI_CK/(MCKDIV*2) = 6.140625 MHz，FS = MCLK/128 = 47973 Hz。
     *   **注意 128 不是 256**：HAL 注释写 MCLK=256*FS，但 2026-11-25 实测（FIFO 消耗
     *   速率探针，MCKDIV=2 时 2048 帧用 21328 µs = 95.9 kHz）是 HAL 公式的 2 倍，
     *   即帧周期 = 128 个 MCLK。所以 DTB 这里给 MCKDIV=4 才落到 47973 Hz。
     *   1 MHz 的输入下 48.000 kHz 不可整除，这是能做到的最好，偏差 0.056%
     *   （人耳不可辨），不是抄来的 48k。 */
    sai: sai {
        compatible = "brd,sai";
        status = "okay";
        sai-pins = <&gpioe 0x007c 6>;
        sample-rate = <47973>;
        mck-div = <4>;
        pll2-n = <393>;
        pll2-q = <8>;
        pll2-divq = <0>;
        codec = <&wm8978>;
    };

    /* TPAD（电容触摸按键）：PA5 走 TIM2_CH1 输入捕获。
     * 硬件事实来自阿波罗 V2 硬件参考手册 V1.1 表 1.5.1（PA5 备注写「ADC 输入引脚，
     * 同时做 TPAD 检测脚」）与参考工程 `tpad.h`（TPAD_GPIO=PA5、AF1=TIM2_CH1）；
     * 检出方法不是 ADC 采样，而是"推挽输出 0 放电 → 切回复用浮空 → 用输入捕获
     * 量 RC 充电到阈值的时间"：手指按下增大电容，充电时间变长，读数值变大。
     *
     * 三个参数（prescaler / arr-max / charge-ms）取参考工程 `system_init()` 的实调值
     * （`tpad_init(20)`、`TPAD_ARR_MAX_VAL=0xFFFF`、放电 `delay_ms(5)`）；它们是
     * **板级事实**（取决于这块板的 RC 常数），换板要改这里，驱动里的时序逻辑不用动。
     *
     * **刻意没有 gate-val**：参考工程头文件里那个 `TPAD_GATE_VAL 100` 是零使用的
     * 常量（`tpad_scan` 用的是倍率判据）。本板 2026-11-25 实测空载基线只有 17 个
     * 计数（PSC=20 → 4.5 MHz，充电约 3.8 µs），"基线+100"意味着读数要放大 7 倍才
     * 算按下，手指按不到。判定口径改为倍率（> 4/3 基线），常量留在驱动里，
     * 不在这里放一个没人消费的属性。 */
    tpad: tpad {
        compatible = "brd,tpad";
        status = "okay";
        tpad-pins = <&gpioa 0x0020 1>;
        timer = "tim2";
        channel = <1>;
        arr-max = <0xffff>;
        prescaler = <20>;
        charge-ms = <5>;
    };
"""


def _soc_inner(text: str) -> str:
    """取出 `soc { ... }` 的内部文本（按花括号配对）。"""
    match = re.search(r"\bsoc\s*\{", text)
    if match is None:
        raise SystemExit("pinmap 产物里找不到 soc 节点")
    start = text.index("{", match.start())
    depth = 0
    for index in range(start, len(text)):
        char = text[index]
        if char == "{":
            depth += 1
        elif char == "}":
            depth -= 1
            if depth == 0:
                return text[start + 1:index]
    raise SystemExit("pinmap 产物的 soc 节点括号不配对")


def _find_dtc() -> Path:
    exe = ROOT / "tool" / "win_x86_64" / "bin" / "dtc.exe"
    if exe.is_file():
        return exe
    import shutil

    found = shutil.which("dtc")
    if found is None:
        raise SystemExit("找不到 dtc：既没有 tool/win_x86_64/bin/dtc.exe，PATH 里也没有 dtc")
    return Path(found)


def build_dts_text(default_src: str, overlay_src: str) -> str:
    """两份 dtsi 的文本 → 合成后的 apollo.dts 全文。

    纯函数、不碰盘：`sync_dts()` 拿它写文件，`refcheck.py` 拿它复算现存
    out/dts/apollo.dts 是否还等于「当前 dtsi 应该生成的样子」。模板只留这一份，
    不会出现「写盘的模板」和「校验的模板」各写一遍然后漂移。
    """
    soc_body = _soc_inner(default_src)
    pinctrl_body = _soc_inner(overlay_src)
    return (
        "// 自动生成，勿手改：python build/build.py update\n"
        "/* 源：docs/hardware/pinmap/*.xlsx → gen_dts.py（default/overlay）→ 本文件 */\n"
        "/dts-v1/;\n\n"
        "/ {\n"
        '    model = "Alientek Apollo STM32F429";\n'
        '    compatible = "alientek,apollo-stm32f429", "st,stm32f429";\n'
        "    #address-cells = <1>;\n"
        "    #size-cells = <1>;\n\n"
        + _BOARD_NODES
        + "\n    soc {\n"
        + soc_body
        + "    };\n\n    pinctrl {\n"
        + pinctrl_body
        + "    };\n};\n"
    )


def sync_dts(info: dict) -> None:
    """pinmap 表 → 两份 dts → 合成 apollo.dts → dtc → apollo.dtb。"""
    script = ROOT / PINMAP_SCRIPT
    if not script.is_file():
        raise SystemExit(f"缺少 pinmap 脚本：{script}")
    # 两份 dtsi 是「源」（入库、可 diff），落在 project/dts/ 平铺；
    # 合成树与 dtb 是「构建中间产物」（不入库），落在 out/dts/。
    dtsi_dir = ROOT / "project" / "dts"
    dtsi_dir.mkdir(parents=True, exist_ok=True)
    out_dir = ROOT / "out" / "dts"
    out_dir.mkdir(parents=True, exist_ok=True)
    print(f"由 {PINMAP_SCRIPT.as_posix()} 生成 default / overlay dtsi")
    subprocess.run([sys.executable, str(script), "--out", str(dtsi_dir)], check=True)
    default_dts = dtsi_dir / PINMAP_DEFAULT_DTSI
    overlay_dts = dtsi_dir / PINMAP_OVERLAY_DTSI
    for path in (default_dts, overlay_dts):
        if not path.is_file():
            raise SystemExit(f"pinmap 脚本没有产出 {path}")
    dts_text = build_dts_text(
        default_dts.read_text(encoding="utf-8"),
        overlay_dts.read_text(encoding="utf-8"),
    )
    dts_path = out_dir / "apollo.dts"
    dts_path.write_text(dts_text, encoding="utf-8", newline="\n")
    dtc = _find_dtc()
    dtb_path = out_dir / "apollo.dtb"
    result = subprocess.run(
        [str(dtc), "-I", "dts", "-O", "dtb", "-o", str(dtb_path), str(dts_path)],
        capture_output=True,
        text=True,
    )
    diag = (result.stdout + result.stderr).strip()
    if result.returncode != 0 or diag:
        raise SystemExit(f"dtc 编译失败或有诊断输出：\n{diag}")
    print(
        f"已生成 {dts_path.relative_to(ROOT).as_posix()} → "
        f"{dtb_path.relative_to(ROOT).as_posix()}（{dtb_path.stat().st_size} 字节）"
    )


def update() -> None:
    print(
        f"更新构建描述：Kconfig / Makefile / {DEFCONFIG_STEM}_{{common,user,userdebug}} / "
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
    sync_dts(info)


def set_build_variant(variant: str) -> str:
    """切换当前逻辑使用的 object/target/release 目录（build / copy 共用）。"""
    global OUT_OBJECT_DIR, OUT_ARTIFACT_DIR, OUT_RELEASE_DIR, _active_build_variant
    if variant not in BUILD_VARIANTS:
        raise ValueError(f"未知 BUILD_VARIANT: {variant!r}")
    _active_build_variant = variant
    OUT_OBJECT_DIR = out_object_dir(variant)
    OUT_ARTIFACT_DIR = out_artifact_dir(variant)
    OUT_RELEASE_DIR = out_release_dir(variant)
    return variant
