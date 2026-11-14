"""`verify`：`VERIFY_COLLECTORS` 与其余全部 `collect_*` 自检。"""
from __future__ import annotations

import argparse
import ast
import atexit
import codecs
import contextlib
import fnmatch
import hashlib
import json
import os
import re
import shutil
import tempfile
import subprocess
import sys
import time
from pathlib import Path

import contracts
from contracts import *  # noqa: F403
import generate
import environment as te
from generate import *  # noqa: F403
from generate import _strip_comments_keep_offsets

def collect_layout_errors() -> list[str]:
    """仓库形状：文件在不在该在的位置、有没有该删的生成物或空目录。

    拆出来的理由不是码风：这 50 条判据原先挤在一个 234 行的函数里时，门禁的
    `--assert-map` 只能看到「collect_verify_errors 报了 N 条」，看不出是目录卫生、
    构建描述、配置一致性还是文档契约出的问题；也看不出某一类是否一个组都没覆盖。
    """
    errors: list[str] = []
    info = load_info()
    try:
        selected_linker = linker_script_path(info)
    except (FileNotFoundError, ValueError) as error:
        errors.append(str(error))
        selected_linker = None
    if selected_linker is not None and not selected_linker.is_file():
        errors.append(f"链接脚本不存在：{selected_linker.relative_to(ROOT).as_posix()}")
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

    for directory in walk_dirs():
        if not needs_build_metadata(directory):
            stale = [
                fname
                for fname in ("Makefile", "Kconfig")
                if (directory / fname).is_file()
            ]
            if stale:
                errors.append(
                    f"纯头目录 {directory.relative_to(ROOT).as_posix()}/ 不该有自动生成的"
                    f" {' / '.join(stale)}（它没有源文件也没有子目录，只需要 -I）"
                )

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
    archive_dir = ROOT / "doc" / "archive"
    if archive_dir.is_dir():
        errors.append(
            "doc/archive/ 已废止：勿在此存放归档文档；历史见 git，机制见 .skills/架构缺陷.md"
        )
    return errors
def collect_makefile_errors() -> list[str]:
    """构建描述：根/各层 Makefile、链接脚本引用、层依赖规则、startup 装配。"""
    errors: list[str] = []
    modules = active_modules()
    try:
        selected_linker = linker_script_path(load_info())
    except (FileNotFoundError, ValueError):
        selected_linker = None
    root_makefile = ROOT / "Makefile"
    if not root_makefile.is_file():
        errors.append("缺少根 Makefile，请先运行 update")
    else:
        link_text = read_text(root_makefile)
        if BUILD_DIR_MAKE_EXPR not in link_text:
            errors.append(f"根 Makefile 缺少 {BUILD_DIR_MAKE_EXPR}")
        if BUILD_VARIANT_MAKE_EXPR not in link_text:
            errors.append(f"根 Makefile 缺少 {BUILD_VARIANT_MAKE_EXPR}")
        if KCONFIG_INCLUDE_MAKE_EXPR not in link_text:
            errors.append(f"根 Makefile 缺少 {KCONFIG_INCLUDE_MAKE_EXPR}")
        for name in NORMAL_LINK_LIBS:
            if name in modules:
                lib = f"lib{name}.a"
                if lib not in link_text:
                    errors.append(f"根 Makefile 链接行缺少 {lib}")
    obsolete_linker_script = ROOT / "proj" / "project-target.ld"
    if obsolete_linker_script.exists():
        errors.append("旧链接脚本 proj/project-target.ld 应删除；链接脚本应位于 hal/chip/linker/")
    if (ROOT / "out" / "build").exists():
        errors.append("旧构建输出目录 out/build/ 应迁移并清理")
    legacy_object = ROOT / "out" / "object"
    if legacy_object.exists():
        errors.append(
            "旧输出目录 out/object/ 仍存在；请运行 update 迁移到 out/userdebug/objects/"
        )
    for variant in BUILD_VARIANTS:
        legacy_profile_obj = out_profile_root(variant) / "object"
        if legacy_profile_obj.exists():
            errors.append(
                f"out/{variant}/object/ 应迁为 objects/；请运行 update"
            )
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
    for variant, opts in VARIANT_MAKE_OPTS.items():
        expected = (
            f"BUILD_VARIANT={variant} OPT={opts['OPT']} DEBUG={opts['DEBUG']}"
        )
        if expected not in root_makefile_text:
            errors.append(
                f"根 Makefile 的 {variant} 目标应使用 OPT={opts['OPT']} DEBUG={opts['DEBUG']}"
            )
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

    for directory in walk_dirs():
        if directory == ROOT:
            continue
        if not needs_build_metadata(directory):
            # 纯头目录不该有自己的 Makefile，缺它是正确状态；「有没有残留」由
            # collect_layout_errors 单独一条判据盯，这里不重复报。
            continue
        makefile = directory / "Makefile"
        makefile_text = read_text(makefile)
        if BUILD_DIR_MAKE_EXPR not in makefile_text:
            errors.append(
                f"{makefile.relative_to(ROOT)} 缺少 {BUILD_DIR_MAKE_EXPR}"
            )
        if KCONFIG_INCLUDE_MAKE_EXPR not in makefile_text:
            errors.append(f"{makefile.relative_to(ROOT)} 缺少 {KCONFIG_INCLUDE_MAKE_EXPR}")
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
        # 按 config 裁掉的编译单元不应要求出现（timers.c 在 configUSE_TIMERS=0 时
        # 不进 makefile，见 config_gated_sources）。
        for source in config_gated_sources(list_sources(directory)):
            expected = f"-include {dep_path(source)}"
            if expected not in makefile_text.splitlines():
                errors.append(
                    f"{makefile.relative_to(ROOT)} 缺少源文件依赖清单：{source.name}"
                )

    return errors
def collect_config_errors() -> list[str]:
    """配置一致性：分档 defconfig / out/<variant>/.config / project-info.json 交叉核对。"""
    errors: list[str] = []
    modules = active_modules()
    info = load_info()
    legacy_dc = ROOT / "proj" / "project_defconfig"
    if legacy_dc.is_file():
        errors.append(
            "proj/project_defconfig 已废弃；请 update 迁为 proj/project_defconfig_userdebug 与 "
            "proj/project_defconfig_user"
        )
    for variant in BUILD_VARIANTS:
        defconfig = project_defconfig_path(variant)
        rel = defconfig.relative_to(ROOT).as_posix()
        if not defconfig.is_file():
            errors.append(f"缺少 {rel}")
            continue
        dc = read_text(defconfig)
        for name in modules:
            if f"# {name}" not in dc:
                errors.append(f"{rel} 缺少分类注释 # {name}")
        config = out_config_path(variant)
        if not config.is_file():
            errors.append(f"缺少 {config.relative_to(ROOT).as_posix()}，请先运行 update")
        else:
            try:
                expected_config = parse_defconfig(
                    read_text(defconfig), source_rel=rel
                )
                if read_text(config) != expected_config:
                    errors.append(f"{config.relative_to(ROOT)} 与 {rel} 解析结果不一致")
            except ValueError as error:
                errors.append(str(error))

    project_name = info.get("project_name", "")
    if not same_project_name(project_name, ROOT.name):
        errors.append(
            f"proj/project-info.json 的 project_name={project_name!r} 与工程根目录名 {ROOT.name!r} 不一致"
            "（比较时忽略大小写，并把 -/_ 视为同一分隔符）"
        )
    for variant in BUILD_VARIANTS:
        defconfig = project_defconfig_path(variant)
        if defconfig.is_file() and f"CONFIG_PROJECT_NAME={project_name}" not in read_text(
            defconfig
        ):
            errors.append(
                f"{defconfig.relative_to(ROOT)} 中 CONFIG_PROJECT_NAME 与 project-info.json 不一致"
            )

    toolchain_make = ROOT / "toolchain.make"
    if not toolchain_make.is_file():
        errors.append("缺少根目录 toolchain.make，请先运行 update")
    if (ROOT / "build" / "toolchain.make").exists():
        errors.append("build/toolchain.make 已移至工程根目录")
    if (ROOT / "out" / ".config").is_file():
        errors.append(
            "out/.config 已废弃；有效配置为 out/<variant>/.config（由 update 从分档 defconfig 生成）"
        )
    if (ROOT / "build" / ".config").exists():
        errors.append("build/.config 已废弃，有效配置为 out/<variant>/.config")
    errors.extend(collect_defconfig_user_custom_sync_errors())
    if (ROOT / "build" / "Depend.make").exists():
        errors.append("build/Depend.make 已废弃，应由各层 Makefile 管理头文件和 .d 依赖")
    return errors


def collect_defconfig_user_custom_sync_errors() -> list[str]:
    """两档 defconfig 的 USER_CUSTOM 须一致（HAL/源裁剪同步改，优化档位不同走 VARIANT_MAKE_OPTS）。"""
    blocks: list[str] = []
    names: list[str] = []
    for variant in BUILD_VARIANTS:
        path = project_defconfig_path(variant)
        if not path.is_file():
            return []
        names.append(path.relative_to(ROOT).as_posix())
        text = read_text(path)
        user_block, _ = split_user_region(text)
        blocks.append(user_block.strip())
    if len(blocks) < 2 or blocks[0] == blocks[1]:
        return []
    return [
        f"{names[0]} 与 {names[1]} 的 # >>> USER_CUSTOM 区块不一致；"
        "功能裁剪（HAL 源文件等）须两档同步修改，发布/调试差异只用 OPT/DEBUG（见 build.py build）"
    ]
def collect_topology_errors() -> list[str]:
    """顶层目录是否都在 LAYER_DIRS / STATIC_LIB_DIRS 里（空壳目录不算）。"""
    errors: list[str] = []
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
    return errors
def collect_doc_contract_errors() -> list[str]:
    """文档契约：工程规格 / SKILL 是否覆盖当前链接顺序、层目录与排除目录。"""
    errors: list[str] = []
    modules = active_modules()
    link_chain = expected_link_chain()
    spec = read_text(SPEC_DOC)
    if not spec:
        errors.append(f"缺少 {SPEC_DOC.relative_to(ROOT)}")
    else:
        if link_chain:
            if link_chain not in spec:
                errors.append(
                    f"工程规格.md 未描述当前链接顺序（应包含：{link_chain}）"
                )
            lib_chain = " → ".join(f"lib{name}.a" for name in link_chain.split(" → "))
            if lib_chain not in spec:
                errors.append(
                    "工程规格.md 链接顺序/框架图可能与当前层不一致（请核对 lib 链）"
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

    obsolete = ("bll", "bdl")
    for old in obsolete:
        if old in modules:
            continue
        if re.search(rf"\b{old}\b", spec):
            errors.append(f"{SPEC_DOC.name} 仍引用已移除的层 {old}，请与当前目录对齐")

    return errors
def _enum_member_count(header: str, count_name: str) -> int | None:
    """数枚举里 `count_name` 之前的成员个数；找不到枚举块返回 None。"""
    for block in re.findall(r"typedef\s+enum\s*\{(.*?)\}", header, re.S):
        if count_name not in block:
            continue
        members = [item.strip() for item in block.split(",") if item.strip()]
        for index, member in enumerate(members):
            if count_name in member:
                return index
    return None
def collect_board_polarity_errors() -> list[str]:
    """bsl 的物理极性：LED 低电平亮、蜂鸣器高电平响、按键高电平按下。

    这三条只写在源码里，此前没有任何断言守。极性写反时编译、链接、尺寸、产物
    符号全都不报 —— 板子上表现为「灯一直亮 / 蜂鸣器一直响 / 按 KEY 没反应」，
    而门禁一片绿。源码级还能再强一层：`board_led_set` 走 `on ? RESET : SET`，
    把两个分支互换就是最典型的一次回退。

    读源码前先剥注释：文件头与 `key_hw[]` 行尾的注释里写着旧写法（"以前写成
    RESET，与原理图相反"），直接用原文匹配会把注释当逻辑。
    """
    errors: list[str] = []

    def read(rel: str) -> str:
        return _strip_comments_keep_offsets(
            (ROOT / rel).read_text(encoding="utf-8")
        )

    # --- LED：共阳经跳帽接 3.3V，低电平点亮 ---
    led_h = (ROOT / "bsl" / "inc" / "board_led.h").read_text(encoding="utf-8")
    led_c = read("bsl/src/board_led.c")
    led_pins: list[int] = []
    led_count = _enum_member_count(led_h, "BOARD_LED_COUNT")
    if led_count is None:
        errors.append("board_led.h 的枚举里找不到 BOARD_LED_COUNT，无法核对灯数量")
    else:
        block = re.search(
            r"led_pin\s*\[\s*BOARD_LED_COUNT\s*\]\s*=\s*\{(.*?)\}", led_c, re.S
        )
        if block is None:
            errors.append("board_led.c 里找不到 led_pin[BOARD_LED_COUNT] 的初始化块")
        else:
            pins = re.findall(r"GPIO_PIN_\d+", block.group(1))
            led_pins = [int(p.rsplit("_", 1)[1]) for p in pins]
            if len(pins) != led_count:
                errors.append(
                    f"board_led.c 的 led_pin[] 有 {len(pins)} 个引脚，"
                    f"但 BOARD_LED_COUNT = {led_count}：多出来的脚不会被 init "
                    "拿到的掩码覆盖（led_pins_mask 只按 COUNT 遍历）"
                )
    if "on ? GPIO_PIN_RESET : GPIO_PIN_SET" not in led_c:
        errors.append(
            "board_led_set 不是 `on ? GPIO_PIN_RESET : GPIO_PIN_SET`："
            "灯是共阳低电平点亮，极性反了会表现为灯常亮/常灭"
        )
    if "HAL_GPIO_WritePin(GPIOB, pins, GPIO_PIN_SET)" not in led_c:
        errors.append(
            "board_led_init 没有把 led 引脚预置为 GPIO_PIN_SET（上电应熄灭）"
        )

    # --- panic 播报的三份掩码必须与 led_pin[] 指向同一组脚 ---
    # panic_leds 故意绕过 bsl 与 HAL（栈可能已经溢出，不能再压栈调用多层函数），
    # 所以 PANIC_LED_MASK / PANIC_LED_MODE_MASK / PANIC_LED_MODE_OUT 是同一组脚的
    # 第三份独立定义 —— 而这三份之间没有任何东西在比对。只把 led_pin[] 里的一个脚
    # 改掉（例如 PB2 → PB9）时：上面那两条断言看的是"数量"与"极性"、panic-beep 组看的是
    # PI11，全都过；而 panic 仍写旧 PANIC_LED_MASK，播报就去闪一个已经不是灯的脚。
    # 按 led_pin[] 正着把三个掩码算一遍再逐位比对，才锁得住这份重复。
    fault_c = read("osl/src/osl_panic.c")

    def _hex_define(where: str, name: str) -> int | None:
        found = re.search(rf"#define\s+{name}\s+0x([0-9A-Fa-f]+)", where)
        return int(found.group(1), 16) if found else None

    want_bsrr = sum(1 << pin for pin in led_pins)
    want_mode_mask = sum(0b11 << (2 * pin) for pin in led_pins)
    want_mode_out = sum(0b01 << (2 * pin) for pin in led_pins)
    for define_name, want, hint in (
        ("PANIC_LED_MASK", want_bsrr, "BSRR 低 16 位脚号掩码"),
        ("PANIC_LED_MODE_MASK", want_mode_mask, "MODER/OSPEEDR/PUPDR 每脚 2 位的掩码"),
        ("PANIC_LED_MODE_OUT", want_mode_out, "每脚 01 = 通用输出的取值"),
    ):
        actual = _hex_define(fault_c, define_name)
        if actual is None:
            errors.append(
                f"osl/src/osl_panic.c 里找不到 {define_name}："
                "panic 播报与 bsl 的灯脚无从比对，改一处漏一处将无人发现"
            )
        elif led_pins and actual != want:
            errors.append(
                f"osl/src/osl_panic.c 的 {define_name} = 0x{actual:08X}，"
                f"但按 bsl/src/board_led.c 的 led_pin[] 算应当是 0x{want:08X}（{hint}）："
                "两处是同一组脚的独立定义，改一处、漏一处会让 panic 播报闪错灯，"
                "而 board-polarity 与 panic-beep 两组都看不出来"
            )

    # --- 蜂鸣器：PI11 经 SS8050 驱动，高电平导通 ---
    beep_c = read("bsl/src/board_beep.c")
    if "on ? GPIO_PIN_SET : GPIO_PIN_RESET" not in beep_c:
        errors.append(
            "board_beep_set 不是 `on ? GPIO_PIN_SET : GPIO_PIN_RESET`："
            "蜂鸣器高电平导通，极性反了会表现为上电一直响"
        )
    if "HAL_GPIO_WritePin(GPIOI, GPIO_PIN_11, GPIO_PIN_RESET)" not in beep_c:
        errors.append(
            "board_beep_init 没有把 PI11 预置为 GPIO_PIN_RESET（上电应静音）"
        )

    # --- 按键：外部 4.7K 下拉，按下接 3.3V 读高 ---
    key_h = (ROOT / "bsl" / "inc" / "board_key.h").read_text(encoding="utf-8")
    key_c = read("bsl/src/board_key.c")
    key_count = _enum_member_count(key_h, "BOARD_KEY_COUNT")
    if key_count is None:
        errors.append("board_key.h 的枚举里找不到 BOARD_KEY_COUNT，无法核对按键数量")
    else:
        block = re.search(
            r"key_hw\s*\[\s*BOARD_KEY_COUNT\s*\]\s*=\s*\{(.*?)\}\s*;", key_c, re.S
        )
        if block is None:
            errors.append("board_key.c 里找不到 key_hw[BOARD_KEY_COUNT] 的初始化块")
        else:
            triples = re.findall(
                r"\{\s*(GPIO\w+)\s*,\s*(GPIO_PIN_\d+)\s*,\s*(GPIO_PIN_(?:SET|RESET))\s*\}",
                block.group(1),
            )
            if len(triples) != key_count:
                errors.append(
                    f"board_key.c 的 key_hw[] 有 {len(triples)} 项，"
                    f"但 BOARD_KEY_COUNT = {key_count}"
                )
            for port, pin, level in triples:
                if level != "GPIO_PIN_SET":
                    errors.append(
                        f"board_key.c 的 {port}/{pin} 按下电平写成 {level}："
                        "两块按键都是按下接 3.3V 读高，写成 RESET 会反相"
                    )
    if "== key->pressed_level" not in key_c:
        errors.append(
            "board_key_pressed 不是 `HAL_GPIO_ReadPin(...) == key->pressed_level`："
            "比较方向写反，按下会返回 0"
        )

    return errors
def _self_layer_header_owners() -> dict[str, set[str]]:
    """本仓自研头文件的 basename → 定义它的层集合。

    同一个 basename 可能在两层各有一份（各是不同契约），所以收集成集合：
    判定要求「目标层全部落在允许集合内」，报错时把全部归属一起打出来。

    枚举用 `STATIC_LIB_DIRS` 而不是 `LAYER_DIRS`：后者是**顶层目录白名单**，
    里混着 `doc` / `proj` / `build` / `tool` / `workspace` 这些非层目录 ——
    `tool/` 下有 4070 个工具链自带的 .c/.h，把它当「层」会让 `queue.h` 这种
    与 FreeRTOS 同名的头被登记成 `{osl, tool}`，于是 apl 只要 include 合法的
    FreeRTOS `queue.h` 就被判「越级（属于 osl、tool 层）」。判据的判定域必须
    只有真正的五层，否则误报的文案还会把人引向 `tool` 层。
    """
    owners: dict[str, set[str]] = {}
    for layer in STATIC_LIB_DIRS:
        for path in sorted((ROOT / layer).rglob("*")):
            if path.is_file() and path.suffix in HEADER_SUFFIXES:
                owners.setdefault(path.name, set()).add(layer)
    return owners
def collect_layer_dependency_errors() -> list[str]:
    """按 LAYER_DEPENDENCIES 查跨层 include —— 只查链接顺序看不见这一层。

    越级的后果在编译期毫无提示、在链接期被 `--start-group` 掩盖：apl 直接调
    hal 的 `HAL_Init()` / `SystemClock_Config()` 时，`apl → hal` 这条边谁都看不见，
    hal 换芯片或改接口时，第一个坏掉的是一个「看起来只依赖 fml/bsl/osl」的 apl。
    判定前先剥注释（注释里写的 include 示例不算依赖）；`#include <...>` 形式的
    系统头不判越级，但**若目标是本仓层头则报「写法违规」** —— 见下。
    `LAYER_DEPENDENCIES` 里没有的层（hal）视作叶子，只允许同层 include。

    为什么尖括号要单列一条而不是直接并入越级判定：尖括号在 C 里的默认语义是
    「系统/外部头」，若真用它引 `<queue.h>`，那可能是 POSIX 的队列而非 osl 层的
    FreeRTOS 队列，直接判越级就会把一个合法用法报成违规。但引**本仓层头**时
    两者必是同一个目标（工程里不会同时有两个同名层头），此时它一定绕过了层依赖
    判定 —— 于是报写法违规（要求改成引号形式），而不是猜它想引哪一个。
    """
    errors: list[str] = []
    owners = _self_layer_header_owners()
    for layer in STATIC_LIB_DIRS:
        allowed = set(LAYER_DEPENDENCIES.get(layer, ()))
        for path in sorted((ROOT / layer).rglob("*")):
            if not path.is_file() or path.suffix not in (SOURCE_SUFFIXES | HEADER_SUFFIXES):
                continue
            text = _strip_comments_keep_offsets(
                path.read_text(encoding="utf-8", errors="replace")
            )
            for match in INCLUDE_QUOTED.finditer(text):
                name = match.group(1)
                targets = owners.get(name.replace("\\", "/").rsplit("/", 1)[-1])
                if not targets or layer in targets or targets <= allowed:
                    continue
                line = text.count("\n", 0, match.start()) + 1
                shown = "、".join(sorted(targets))
                allowed_text = "、".join(sorted(allowed)) if allowed else "无（叶子层）"
                errors.append(
                    f"{path.relative_to(ROOT).as_posix()}:{line} 越级 include \"{name}\""
                    f"（属于 {shown} 层）：{layer} 的允许依赖是 {allowed_text}；"
                    "改走本层或允许层提供的门面，或同步 LAYER_DEPENDENCIES"
                )
            for match in INCLUDE_ANGLE.finditer(text):
                base = match.group(1).replace("\\", "/").rsplit("/", 1)[-1]
                if base not in owners:
                    continue
                line = text.count("\n", 0, match.start()) + 1
                shown = "、".join(sorted(owners[base]))
                errors.append(
                    f"{path.relative_to(ROOT).as_posix()}:{line} "
                    f"用尖括号引用本仓层头 <{match.group(1)}>（属于 {shown} 层）："
                    "尖括号形式不参与层依赖判定，越级会被静默放过；"
                    f"请改成 #include \"{base}\""
                )
            for match in EXTERN_DECL.finditer(text):
                symbol = match.group(1)
                owner = next(
                    (lay for lay, prefix in LAYER_SYMBOL_PREFIXES.items()
                     if symbol.startswith(prefix)),
                    None,
                )
                if owner is None or owner == layer or owner in allowed:
                    continue
                line = text.count("\n", 0, match.start()) + 1
                allowed_text = "、".join(sorted(allowed)) if allowed else "无（叶子层）"
                errors.append(
                    f"{path.relative_to(ROOT).as_posix()}:{line} 用 extern 声明引用 "
                    f"{owner} 层符号 {symbol}：{layer} 的允许依赖是 {allowed_text}；"
                    "include 判据只看得见 #include 边，符号级依赖会整段绕过它 —— "
                    f"请改成 include {owner} 层的头，或同步 LAYER_DEPENDENCIES"
                )
    return errors
def _function_body_keep_offsets(text: str, signature: str) -> tuple[int, int] | None:
    """按签名定位函数体的 [start, end) 偏移，剥注释但**偏移保持与原文一致**。

    与 `_function_body()` 的分工：那个返回的是剥注释后的字符串，偏移对不上原文，
    没法报行号。这里专供需要行号的判据用；对前向声明同样逐个候选重试
    （`void f(void);` 后面没有 '{'，不能把下一个函数的函数体当成它）。
    """
    code = _strip_comments_keep_offsets(text)
    search_from = 0
    while True:
        match = re.search(signature, code[search_from:])
        if match is None:
            return None
        after = search_from + match.end()
        paren = 0 if signature.rstrip().endswith(")") else 1
        pos = after
        while pos < len(code) and paren > 0:
            if code[pos] == "(":
                paren += 1
            elif code[pos] == ")":
                paren -= 1
            pos += 1
        while pos < len(code) and code[pos].isspace():
            pos += 1
        if pos < len(code) and code[pos] == "{":
            depth = 0
            start = pos
            while pos < len(code):
                if code[pos] == "{":
                    depth += 1
                elif code[pos] == "}":
                    depth -= 1
                    if depth == 0:
                        return start, pos + 1
                pos += 1
            return None
        search_from = after
def _scan_newlib_calls(
    code: str, rel: str, label: str, text: str, errors: list[str], offset: int = 0
) -> None:
    for match in NEWLIB_PREBOOT_CALLS.finditer(code):
        line = text.count("\n", 0, offset + match.start()) + 1
        errors.append(
            f"{rel}:{line} 在 {label} 里调用了 {match.group(1)}()："
            "newlib 的 `__malloc_lock` 在 vTaskStartScheduler() 之前不可用 —— "
            "`uxCriticalNesting` 的哨兵初值是 0xaaaaaaaa（port.c:161），只在 "
            "xPortStartScheduler() 里被置 0（port.c:443），所以启动前 ENTER/EXIT "
            "一圈之后 `if (uxCriticalNesting == 0) portENABLE_INTERRUPTS()` 永不成立，"
            "中断被静默保持关闭且没有断言会响；请挪到调度器启动之后"
        )
def collect_newlib_preboot_errors() -> list[str]:
    """调度器启动前不许调 newlib 的分配/格式化 —— `__malloc_lock` 在那之前不可用。

    `__malloc_lock` / `__malloc_unlock`（`osl/src/freertos_port_st.c`）走
    `taskENTER_CRITICAL` / `taskEXIT_CRITICAL`。port.c 里 `uxCriticalNesting` 的初值
    是 0xaaaaaaaa（`:161`），只在 `xPortStartScheduler()` 里被置 0（`:443`）。调度器
    启动前调一次 malloc：ENTER 把它变成 0xaaaaaaab，EXIT 退回 0xaaaaaaaa，于是
    `if (uxCriticalNesting == 0) portENABLE_INTERRUPTS()` 永不成立 —— 中断静默保持
    关闭，全程没有任何断言会响。

    这条路径为什么可达：`_sbrk` / `__malloc_lock` 被 `-Wl,-u` 强制拉进镜像，所以
    「符号在」不等于「路径不可达」。当前镜像里没有 malloc / printf 调用方
    （`nm` 实测 printf / vprintf / malloc / _malloc_r / free / snprintf / puts /
    fwrite 全无），照现状是安全的；这条判据守的是「别在启动期加第一个调用方」。

    判定范围只有三处真正跑在 `vTaskStartScheduler()` 之前：
      `main()` 里 `vTaskStartScheduler()` 出现之前的部分
      `board_system_init()` 整函数体
      `osl_health_init()` 整函数体
    刻意不扫 `osl_boot.c`：里面的 `vApplicationTickHook` 是调度器启动后的 tick
    上下文，那里 malloc 合法；`osl_health_stack_mark` 同理（任务上下文）。
    """
    errors: list[str] = []

    main_rel = "apl/src/main.c"
    text = (ROOT / main_rel).read_text(encoding="utf-8", errors="replace")
    code = _strip_comments_keep_offsets(text)
    sched = code.find("vTaskStartScheduler()")
    if sched < 0:
        errors.append(
            f"{main_rel} 里找不到 vTaskStartScheduler()："
            "启动期判据失去了边界，整个 main() 会被静默放过"
        )
    else:
        _scan_newlib_calls(code[:sched], main_rel, "main()", text, errors)

    for rel, signature, label in (
        ("bsl/src/board_system.c", r"int\s+board_system_init\s*\(", "board_system_init()"),
        ("osl/src/osl_health.c", r"void\s+osl_health_init\s*\(", "osl_health_init()"),
    ):
        text = (ROOT / rel).read_text(encoding="utf-8", errors="replace")
        code = _strip_comments_keep_offsets(text)
        span = _function_body_keep_offsets(text, signature)
        if span is None:
            errors.append(f"{rel} 里找不到 {label} 的定义：启动期判据失去了范围")
            continue
        _scan_newlib_calls(code[span[0]:span[1]], rel, label, text, errors, span[0])

    return errors
ARCH_OVERVIEW = ROOT / "doc" / "design" / "系统设计.md"
VENDORED_KERNEL_SUBTREE = "osl/inc/freertos/"


def _self_layer_header_owners_excluding_vendored() -> dict[str, set[str]]:
    """同 `_self_layer_header_owners()`，但剔除 vendored FreeRTOS 内核头。

    文档口径（系统设计.md「口径注意」）写明只数自研层头、`FreeRTOS.h` / `task.h`
    这类 vendored 内核头不进那张表。判据必须用同一口径，否则 apl / fml 引
    `FreeRTOS.h` 会被记成 apl → osl / fml → osl，处数与文档永远对不上 —— 实测
    含 vendored 是 28 处、按文档口径是 22 处。
    """
    owners: dict[str, set[str]] = {}
    for layer in STATIC_LIB_DIRS:
        for path in sorted((ROOT / layer).rglob("*")):
            if not path.is_file() or path.suffix not in HEADER_SUFFIXES:
                continue
            if path.relative_to(ROOT).as_posix().startswith(VENDORED_KERNEL_SUBTREE):
                continue
            owners.setdefault(path.name, set()).add(layer)
    return owners


def _self_layer_edge_counts() -> dict[tuple[str, str], int]:
    """按文档口径数每一处跨层 include，返回 (引用方, 被引用方) → 处数。"""
    owners = _self_layer_header_owners_excluding_vendored()
    counts: dict[tuple[str, str], int] = {}
    for layer in STATIC_LIB_DIRS:
        for path in sorted((ROOT / layer).rglob("*")):
            if not path.is_file() or path.suffix not in (SOURCE_SUFFIXES | HEADER_SUFFIXES):
                continue
            text = _strip_comments_keep_offsets(
                path.read_text(encoding="utf-8", errors="replace")
            )
            for match in INCLUDE_QUOTED.finditer(text):
                name = match.group(1).replace("\\", "/").rsplit("/", 1)[-1]
                for target in owners.get(name, ()):
                    if target != layer:
                        counts[(layer, target)] = counts.get((layer, target), 0) + 1
    return counts


def collect_doc_count_errors() -> list[str]:
    """系统设计.md 的层边处数：与实测一致，且表 / 矩阵 / mermaid / 合计四处互相对得上。

    为什么需要它：`collect_layer_dependency_errors()` 只判「有没有违规边」，文档里
    写几处它完全不管。实测 `apl → osl` 写 1（真实 2）、矩阵 `osl → hal` 写 6
    （真实 5、同表却写 5）、合计写 21（真实 22）—— 同一份文档四处互斥，三轮全量
    始终是绿的。数字漂移此前没有任何机器守卫。
    """
    errors: list[str] = []
    if not ARCH_OVERVIEW.is_file():
        return [f"{ARCH_OVERVIEW.relative_to(ROOT).as_posix()} 不存在：层边处数失去事实源"]
    text = ARCH_OVERVIEW.read_text(encoding="utf-8", errors="replace")

    measured = _self_layer_edge_counts()

    # 1) 表格：| N | `a → b` | 处数 | ...
    table: dict[tuple[str, str], int] = {}
    for m in re.finditer(r"^\|\s*\d+\s*\|\s*`([a-z]+)\s*→\s*([a-z]+)`\s*\|\s*(\d+)\s*\|", text, re.MULTILINE):
        table[(m.group(1), m.group(2))] = int(m.group(3))

    # 2) 合计：合计 **7 条边 / N 处**（算式）
    total_value = None
    total_sum = None
    m = re.search(r"合计\s*\*\*\s*7\s*条边\s*/\s*(\d+)\s*处\s*\*\*\s*（([0-9+]+)）", text)
    if m:
        total_value = int(m.group(1))
        total_sum = sum(int(x) for x in m.group(2).split("+"))

    # 3) 标题：## 允许的跨层边：7 条 / N 处
    head_value = None
    m = re.search(r"##\s*允许的跨层边：7\s*条\s*/\s*(\d+)\s*处", text)
    if m:
        head_value = int(m.group(1))

    # 4) 矩阵：表头给列顺序，数据行给行名与各格
    layer_order = ("apl", "fml", "bsl", "osl", "hal")
    matrix: dict[tuple[str, str], int] = {}
    cols: list[str] = []
    for line in text.splitlines():
        if not line.startswith("|"):
            if cols:
                break
            continue
        cells = [c.strip() for c in line.strip().strip("|").split("|")]
        bare = [c.replace("*", "").replace("`", "") for c in cells]
        if not cols:
            if len(cells) == len(layer_order) + 1 and cells[0] == "" and tuple(bare[1:]) == layer_order:
                cols = bare[1:]
            continue
        if len(cells) != len(cols) + 1 or bare[0] not in layer_order:
            continue
        for col_name, cell in zip(cols, cells[1:]):
            if cell.isdigit():
                matrix[(bare[0], col_name)] = int(cell)

    # 5) mermaid：A -->|"N 处"| B
    mmd: dict[tuple[str, str], int] = {}
    for m in re.finditer(r'([A-Z]+)\s*-->\|"(\d+)\s*处"\|\s*([A-Z]+)', text):
        mmd[(m.group(1).lower(), m.group(3).lower())] = int(m.group(2))

    rel = ARCH_OVERVIEW.relative_to(ROOT).as_posix()
    if not table:
        errors.append(f"{rel} 里一条层边表格行都没解析到：处数判据失去了范围")
    if not matrix:
        errors.append(f"{rel} 里依赖矩阵没解析到任何非零格")
    if not mmd:
        errors.append(f"{rel} 里 mermaid 没解析到任何 `N 处` 标注")
    if total_value is None:
        errors.append(f"{rel} 里找不到「合计 **7 条边 / N 处**（算式）」这一行")

    # 逐步：每条边在四处口径上都必须相等，且等于实测
    for edge, count in sorted(table.items()):
        real = measured.get(edge, 0)
        if count != real:
            errors.append(
                f"{rel} 表格写 {edge[0]} → {edge[1]} 是 {count} 处，实测 {real} 处"
            )
        if matrix.get(edge) not in (None, count):
            errors.append(
                f"{rel} 矩阵写 {edge[0]} → {edge[1]} 是 {matrix[edge]}，表格写 {count}"
            )
        if mmd.get(edge) not in (None, count):
            errors.append(
                f"{rel} mermaid 写 {edge[0]} → {edge[1]} 是 {mmd[edge]} 处，表格写 {count}"
            )

    # 实测里出现、表格里漏掉的边
    for edge in sorted(measured):
        if measured[edge] and edge not in table:
            errors.append(
                f"{rel} 表格漏了 {edge[0]} → {edge[1]}（实测 {measured[edge]} 处）"
            )

    if total_value is not None and total_value != sum(table.values()):
        errors.append(
            f"{rel} 合计写 {total_value} 处，表格各行相加是 {sum(table.values())} 处"
        )
    if total_sum is not None and total_sum != sum(table.values()):
        errors.append(
            f"{rel} 合计算式各项相加是 {total_sum} 处，表格各行相加是 {sum(table.values())} 处"
        )
    if head_value is not None and total_value is not None and head_value != total_value:
        errors.append(f"{rel} 标题写 {head_value} 处，合计写 {total_value} 处")

    return errors


# `collect_verify_errors` 的七类判据，顺序与原函数的段序一致。
VERIFY_COLLECTORS = (
    collect_layout_errors,
    collect_makefile_errors,
    collect_config_errors,
    collect_topology_errors,
    collect_doc_contract_errors,
    collect_newlib_preboot_errors,
    collect_doc_count_errors,
)
def collect_verify_errors() -> list[str]:
    """工程结构总检：只负责按类汇总，判据都在上面七个函数里。"""
    errors: list[str] = []
    for collect in VERIFY_COLLECTORS:
        errors.extend(collect())
    return errors
def verify(artifacts: bool = False, *, check_symbols: bool = True) -> None:
    """校验生成物与规格；artifacts 为真时额外断言现有 elf 里的应用代码。

    check_symbols=False 供 update() 内部使用：那一步发生在 make 之前，
    归档/产物还是上一轮的，检查它们只会把 build 自己锁死。
    """
    print("自检：代码生成物与 .skills 规格")
    RELEASE_SKIPPED.clear()
    errors = collect_verify_errors()
    errors.extend(collect_stale_dependency_errors())
    errors.extend(collect_hal_module_errors())
    errors.extend(collect_hal_assert_errors())
    errors.extend(collect_heap_errors())
    errors.extend(collect_fault_errors())
    errors.extend(collect_warning_gate_errors())
    errors.extend(collect_isr_priority_errors())
    errors.extend(collect_periodic_task_errors())
    errors.extend(collect_task_priority_errors())
    errors.extend(collect_panic_code_errors())
    errors.extend(collect_panic_entry_ownership_errors())
    errors.extend(collect_observability_errors())
    errors.extend(collect_panic_task_name_errors())
    errors.extend(collect_panic_assert_task_errors())
    errors.extend(collect_panic_scene_errors())
    errors.extend(collect_panic_generation_errors())
    errors.extend(collect_health_slot_errors())
    errors.extend(collect_health_writer_errors())
    errors.extend(collect_task_name_len_errors())
    errors.extend(collect_fault_range_errors())
    errors.extend(collect_memory_metadata_errors())
    errors.extend(collect_ccmram_state_errors())
    errors.extend(collect_doc_reference_errors())
    errors.extend(collect_eol_contract_errors())
    errors.extend(collect_line_ending_errors())
    errors.extend(collect_board_polarity_errors())
    errors.extend(collect_layer_dependency_errors())
    tools_ok = True
    if check_symbols or artifacts:
        tool_errors, tools_ok = artifact_tool_errors()
        errors.extend(tool_errors)
    elf_variants = list(BUILD_VARIANTS) if artifacts else [DEFAULT_BUILD_VARIANT]
    if artifacts and tools_ok:
        errors.extend(collect_build_variant_buildflags_errors())
    if (check_symbols or artifacts) and tools_ok:
        for variant in elf_variants:
            set_build_variant(variant)
            if variant != DEFAULT_BUILD_VARIANT:
                RELEASE_SKIPPED.clear()
            variant_errors: list[str] = []
            if check_symbols:
                variant_errors.extend(collect_heap_artifact_errors())
                variant_errors.extend(collect_lib_member_errors())
                variant_errors.extend(collect_freertos_unit_errors())
                variant_errors.extend(collect_unlinked_member_errors())
                variant_errors.extend(collect_fault_artifact_errors())
                variant_errors.extend(collect_observ_artifact_errors())
                variant_errors.extend(collect_persist_record_errors())
                variant_errors.extend(collect_boot_init_errors())
                variant_errors.extend(collect_tick_source_errors())
                variant_errors.extend(collect_startup_guard_errors())
                variant_errors.extend(collect_last_write_errors())
                variant_errors.extend(collect_panic_order_errors())
                variant_errors.extend(collect_bsp_artifact_errors())
            if artifacts:
                variant_errors.extend(collect_artifact_errors())
            if len(elf_variants) > 1:
                variant_errors = [f"[{variant}] {item}" for item in variant_errors]
            errors.extend(variant_errors)
    if errors:
        print("自检失败：", file=sys.stderr)
        for item in errors:
            print(f"  - {item}", file=sys.stderr)
        for item in RELEASE_SKIPPED:
            print(f"  [跳过] {item}", file=sys.stderr)
        print("请修正 build/ 脚本 / 源码树 / .skills 后重新运行 update 或 verify。", file=sys.stderr)
        raise SystemExit(1)
    if RELEASE_SKIPPED:
        # 跳过必须留痕：静默免检和「真的通过」在输出上长得一模一样。
        print(
            f"自检通过（发布档位 -Os：跳过 {len(RELEASE_SKIPPED)} 条指令形态判据；"
            "这些机制只在 -Og 下强制，发布产物仍需人工核对）"
        )
        for item in RELEASE_SKIPPED:
            print(f"  [跳过] {item}")
    else:
        print("自检通过")


def collect_build_variant_buildflags_errors() -> list[str]:
    """现有产物目录里的 .buildflags 须与 VARIANT_MAKE_OPTS 一致（仅在有产物时检查）。"""
    errors: list[str] = []
    for variant, opts in VARIANT_MAKE_OPTS.items():
        flags_path = out_object_dir(variant) / ".buildflags"
        if not flags_path.is_file():
            continue
        text = flags_path.read_text(encoding="utf-8")
        if opts["OPT"] not in text:
            errors.append(
                f"out/{variant}/objects/.buildflags 未含 {opts['OPT']}（{text.strip()!r}）"
            )
        if opts["DEBUG"] not in text:
            errors.append(
                f"out/{variant}/objects/.buildflags 未含 {opts['DEBUG']}（{text.strip()!r}）"
            )
    return errors


def _artifact_is_release_build() -> bool:
    """当前档位的 elf 是否为 user（-Os）构建。

    证据只能取自构建过程写下的标记，不能取自 map：map 里有没有 `_aeabi_ldivmod.o`
    恰恰是本判据要判的东西，用它来判档位等于自证清白（破坏态一出现就自动合法）。
    根 Makefile 每次 `all` 都会把本次的 `$(OPT) $(DEBUG)` 写进
    `out/<档位>/objects/.buildflags`，所以这里读的是一个独立事实。
    读不到标记时按默认（-Og）处理：宁可报红，也不静默放行。
    """
    flags = OUT_OBJECT_DIR / ".buildflags"
    try:
        return "-Os" in flags.read_text(encoding="utf-8")
    except OSError:
        return False
# 本轮 verify 里因发布档位（-Os）而被跳过的指令形态判据。
#
# **跳过不等于免检。** 这些判据守的机制（HAL_InitTick 幂等早退、panic 重入闸置位、
# IWDG 同步等待的失败分支）在 -Og 下仍逐条强制，只是 -Os 会把它们内联/折叠，
# 按指令形态判必然假红。每次跳过都记在这里，`verify` 汇总结尾会逐条列出——
# 否则「跳过」就退化成静默免检，而这三条恰恰是最不能静默放过的机制。
RELEASE_SKIPPED: list[str] = []
def _skip_on_release_build(what: str) -> bool:
    """发布档位下跳过一条指令形态判据；返回 True 表示本次跳过。

    双轨制（见 `.skills/工程规格.md`「发布构建与体积上限」）：
    调试档位（-Og）指令级判据全开；发布档位（-Os）跳过这些只能靠指令形态
    证明的机制，改由同一批机制在 -Og 下的强制 + 人工核对发布产物兜底。
    """
    if not _artifact_is_release_build():
        return False
    RELEASE_SKIPPED.append(what)
    return True
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
    raw = conf.read_text(encoding="utf-8", errors="replace")
    # enabled / disabled 看原文（行首 `/*` 就是「这个开关被注释掉了」）；
    # guarded 必须看剥注释后的文本：整块 `#ifdef … #include …` 被注释包裹时，
    # 顶层头其实没有引入该模块，只看原文会把注释当成生效。
    text = _strip_comments_keep_offsets(raw)
    enabled: set[str] = set()
    disabled: set[str] = set()
    for match in re.finditer(r"^(/\*\s*)?#define\s+(HAL_[A-Z0-9_]+_MODULE_ENABLED)", raw, re.M):
        (disabled if match.group(1) else enabled).add(match.group(2))
    guarded = set(
        re.findall(
            r"#ifdef\s+(HAL_[A-Z0-9_]+_MODULE_ENABLED)\s*\n\s*#include\s+\"stm32f4xx_hal_",
            text,
        )
    )
    src_dir = conf.parent.parent / "src"
    obj_dir = out_object_dir(DEFAULT_BUILD_VARIANT) / "hal" / "periph" / "src"

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
    # 反方向也要查：禁用模块的 .o 不该出现在归档里。启用模块被打开了没人管，
    # 反过来「开关关着、构建照样编」同样不会让任何断言叫 —— 只会在归档里多一堆
    # 永远不被链接的成员。
    if obj_dir.is_dir():
        enabled_stems = {
            symbol[len("HAL_"):-len("_MODULE_ENABLED")].lower() for symbol in enabled
        }
        for obj in sorted(obj_dir.glob("stm32f4xx_hal_*.o")):
            stem = obj.stem[len("stm32f4xx_hal_"):]
            base = stem[:-3] if stem.endswith("_ex") else stem
            if not base or base in enabled_stems:
                continue
            errors.append(
                f"{obj.relative_to(ROOT).as_posix()} 被编译，但 {conf.name} 里 "
                f"HAL_{base.upper()}_MODULE_ENABLED 没有启用："
                "编译清单与模块开关脱节，禁用模块的代码仍会进归档"
            )
    return errors
def collect_hal_assert_errors() -> list[str]:
    """HAL 全量断言（`USE_FULL_ASSERT`）必须保持启用。

    打开它之后 `assert_param` 从 `((void)0U)` 变成真正的参数检查：HAL 的每个外设
    入口在拿到非法句柄/非法参数时都会走到 `assert_failed()` → `panic_assert_from()`
    → 码 3 + `g_panic_record`。代价是 `text` 约 +880 B（`-Os` 实测 11108 → 11992）。

    关掉它没有任何东西会叫：`assert_param` 退回 `((void)0U)`，HAL 的参数检查整片
    消失，而镜像里的 `assert_failed` 强符号仍然存在——它定义在 `osl_panic.c`，
    与这个宏无关。所以"产物里有没有 assert_failed"这类符号级判据在这里恒真，
    守不住任何东西；能守住的只有配置本身。

    必须剥注释后再匹配：`/* #define USE_FULL_ASSERT 1U */` 在原文里同样是这几个
    字符，只看原文会把注释掉的开关当成启用（`hal-module` 段的第二子判据踩的就是
    同一个陷阱）。实测过：把这一行注释掉，`verify` 原本 EXIT=0 全绿。
    """
    errors: list[str] = []
    confs = sorted((ROOT / "hal").rglob("stm32f4xx_hal_conf.h"))
    if not confs:
        errors.append("找不到 stm32f4xx_hal_conf.h，无法确认 USE_FULL_ASSERT 是否启用")
        return errors
    conf = confs[0]
    relative = conf.relative_to(ROOT).as_posix()
    code = _strip_comments_keep_offsets(conf.read_text(encoding="utf-8", errors="replace"))
    if not re.search(r"^[ \t]*#define[ \t]+USE_FULL_ASSERT[ \t]+1U\b", code, re.M):
        errors.append(
            f"{relative} 没有启用 USE_FULL_ASSERT（应为 `#define USE_FULL_ASSERT    1U`）："
            "`assert_param` 会退回 `((void)0U)`，HAL 的全部参数检查静默消失；"
            "镜像里 `assert_failed` 符号仍然存在，产物级断言看不出来"
        )
    return errors
def collect_health_writer_errors() -> list[str]:
    """健康采样只允许在 osl 层写。

    `g_heap_free_min` 的历史最低只有**一个**合法采样写者：`osl_boot.c` 的 tick hook
    （`osl_health_init()` 那次是复位，不是采样）。此前 `osl_health_heap_mark()` 也写
    同一个变量：两处读同一个单调不增量，tick 1 kHz、mark 1 Hz —— 1 ms 内该量只会
    更小或相等，mark 的写入不可能带来 tick 下一秒就会带来的信息；而它挂在 LED 任务上，
    任务一卡死采样就停，tick 还活着。删掉之后剩下的风险是"再有人在上层补一个采样点"：
    那会原地长成 `g_panic_record` 六入口各写各写那种形态（M3 / M4 同源）。

    这条查的是层边界而不是某一处调用：`apl` / `bsl` / `fml` 的源码（剥注释后）不得
    出现这两个全局量的名字。上层要读就走 osl 的访问器，不直接摸变量。
    """
    errors: list[str] = []
    names = ("g_heap_free_min", "g_stack_free_min")
    for layer in HEALTH_SAMPLE_LAYERS:
        for source in sorted((ROOT / layer / "src").rglob("*.c")):
            code = _strip_comments_keep_offsets(
                source.read_text(encoding="utf-8", errors="replace")
            )
            for name in names:
                if name in code:
                    errors.append(
                        f"{source.relative_to(ROOT).as_posix()} 直接引用了 {name}："
                        "健康采样只能在 osl 层写（堆水位的唯一采样写者是 tick hook）；"
                        "上层再补一个采样点，两个写者就各自维护自己的下界了"
                    )
    return errors
def collect_stale_dependency_errors() -> list[str]:
    """检查 -MMD 产出的 .d 里记录的依赖路径是否仍然存在。

    .d 记录的是绝对路径。工程被整目录复制到另一台机器（或另一个根路径）后，
    make 找不到旧路径的头文件就"看不见"头文件变化：改了 FreeRTOSConfig.h，
    tasks.o 仍然是几天前的产物，而 make 全程沉默。实测出现过 77 个这样的 .d。
    """
    errors: list[str] = []
    missing_examples: list[str] = []
    broken = 0
    dep_files: list[Path] = []
    for variant in BUILD_VARIANTS:
        objdir = out_object_dir(variant)
        if objdir.is_dir():
            dep_files.extend(sorted(objdir.rglob("*.d")))
    if not dep_files:
        return errors
    for dep in dep_files:
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
    bin_dir = te.find_toolchain_bin()
    if bin_dir is not None:
        dirs = [bin_dir]
    else:
        # 只认本机平台的工具链目录：tool/ 下同时存在 linux_x86_64 与 win_x86_64，
        # 不按主平台过滤就会把另一个平台的同名二进制当成可用工具（Windows 上执行
        # 会 OSError → run_tool 返回空串 → 下游把"拿不到输出"当成"符号不在镜像里"）。
        host_dir = te.host_toolchain_dirname()
        dirs = [
            candidate
            for candidate in sorted((ROOT / "tool").glob("*/bin"))
            if candidate.parent.name == host_dir
        ]
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
def _tool_runs(exe: Path | None) -> bool:
    """路径存在不等于能执行：真跑一次 --version 才算可用。"""
    if exe is None:
        return False
    try:
        result = subprocess.run(
            [str(exe), "--version"], capture_output=True, timeout=20, check=False
        )
    except (OSError, subprocess.SubprocessError):
        return False
    return result.returncode == 0
def artifact_tool_errors() -> tuple[list[str], bool]:
    """产物断言依赖的 binutils 是否可用；不可用时由调用方短路所有符号级断言。

    为什么必须短路：run_tool() / run_tool_args() 执行失败只返回空串，下游会把这个
    空串当成"符号不在镜像里"，一次报出「产物里没有 g_tick_ms」「找不到
    SystemClock_Config 的函数体」「没有 g_last_write」等一串假红，真正的根因
    （工具不可用）被埋在最后一条，排障方向被带到"去改源码"。
    实测：把 tool/win_x86_64 移走后跑 verify --artifacts，正是这 8 条假红。

    判据用「能不能跑起来」而不是「文件在不在」：tool/ 下另一平台的同名二进制
    在 Windows 上会 OSError，存在性判断会误判为可用。
    """
    missing = [name for name in ARTIFACT_TOOL_NAMES if not _tool_runs(tool_binary(name))]
    if not missing:
        return [], True
    return (
        [
            "缺少 " + "、".join(f"arm-none-eabi-{name}" for name in missing)
            + "：读 elf 需要它。当前没有在做任何符号级校验 —— "
            "这是「没有校验」，不是「产物正确」；先跑 python3 build/build.py install-toolchain"
        ],
        False,
    )
def run_tool(exe: Path, elf: Path) -> str:
    """执行 arm-none-eabi-nm/size；无法执行时返回空串。"""
    try:
        return subprocess.run(
            [str(exe), str(elf)], capture_output=True, text=True, check=False
        ).stdout
    except OSError:
        return ""
def run_tool_args(exe: Path, *args: str) -> str:
    """执行带参数的 arm-none-eabi-*（objdump 之类）；无法执行时返回空串。"""
    try:
        return subprocess.run(
            [str(exe), *args], capture_output=True, text=True, check=False
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
    elf = OUT_ARTIFACT_DIR / f"{info.get('project_name', '')}.elf"
    if not elf.is_file():
        # 静默返回空表会让 `verify --artifacts` 在没有产物的干净树上"通过"：
        # 断言一次都没跑，绿色却和"全部通过"长得一模一样。调用方明确要了产物校验，
        # 就得说清产物不存在 —— 想跳过产物断言是不加 --artifacts。
        errors.append(
            f"产物 {elf.relative_to(ROOT).as_posix()} 不存在，--artifacts 无从校验（先跑 build）"
        )
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
    if text > ARTIFACT_TEXT_CEIL:
        errors.append(
            f"产物 {elf.name} 的 .text 为 {text} 字节，超出 {ARTIFACT_TEXT_CEIL} 字节上限；"
            "先确认是不是整包拖进了未裁剪的模块（新功能若确实必需，同步抬高 ARTIFACT_TEXT_CEIL）"
        )
    regions = memory_regions(info)
    rom = regions.get("ROM", 0)
    ram = regions.get("RAM", 0)
    flash_used = text + data
    if flash_used > ARTIFACT_FLASH_CEIL:
        errors.append(
            f"Flash 用量 {flash_used} 字节，超出 {ARTIFACT_FLASH_CEIL} 字节上限"
            f"（text {text} + data {data}）"
        )
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
            "一次中等 malloc 就会越过 __heap_end__（它到 _estack 之间是约 159 KB 无保护真空，"
            "既不会撞到栈也不会报错，只有 _sbrk 自己的边界判断在挡）。"
            "要么改用 pvPortMalloc（osl_alloc_guard.h 已编译期禁用 malloc），"
            "要么把 _Min_Heap_Size 提到下限以上并确认 RAM 预算"
        )
    # panic 记录必须落在 .noinit：链接脚本的 .noinit 段输出 _snoinit/_enoinit，
    # 而 Reset_Handler 只清 _sbss.._ebss。若有人把 section 属性删掉，record 会退回
    # .bss，复位后现场立刻归零 —— 这是本工程最贵的一条静默失效，只能用产物判。
    noinit: dict[str, int] = {}
    for match in re.finditer(
        r"^([0-9a-fA-F]+)\s+[A-Za-z]\s+(_snoinit|_enoinit|g_panic_record)$",
        symbols,
        re.MULTILINE,
    ):
        noinit[match.group(2)] = int(match.group(1), 16)
    if len(noinit) == 3:
        if not (noinit["_snoinit"] <= noinit["g_panic_record"] < noinit["_enoinit"]):
            errors.append(
                f"产物 {elf.name} 里 g_panic_record（0x{noinit['g_panic_record']:08x}）不在 .noinit 段"
                f" [0x{noinit['_snoinit']:08x}, 0x{noinit['_enoinit']:08x}) 内："
                "Reset_Handler 会把它当 .bss 清零，上一次 panic 的现场保不下来"
            )
    else:
        errors.append(
            f"产物 {elf.name} 里缺少 _snoinit / _enoinit / g_panic_record 之一，"
            "无法确认 panic 记录跨复位保留（查链接脚本 .noinit 段与 freertos_port_st.c 的 section 属性）"
        )
    if not re.search(r"^[0-9a-fA-F]+\s+T\s+_write$", symbols, re.MULTILINE):
        errors.append(
            f"产物 {elf.name} 里没有 _write：--gc-sections 把它和 g_last_write 一起丢了，"
            "“把最近一段输出留给调试器”就是空承诺（检查 LDFLAGS 的 -Wl,-u,_write）"
        )
    # panic 现场的“上下文”字段必须真的编进结构体：ipsr（任务/中断）、
    # reset_csr（RCC->CSR 原始值）、wd_sync_fail（看门狗超时是否真的改成功）。
    # 判据用符号尺寸而不是 DWARF 字段名：三者任一缺失，尺寸就从 0x7C 回落到 0x70。
    sized = run_tool_args(nm, "-S", elf.as_posix())
    record_bytes = 0
    match = re.search(
        r"^[0-9a-fA-F]+\s+([0-9a-fA-F]+)\s+[A-Za-z]\s+g_panic_record$", sized, re.MULTILINE
    )
    if match:
        record_bytes = int(match.group(1), 16)
    if record_bytes < PANIC_RECORD_MIN_BYTES:
        errors.append(
            f"产物 {elf.name} 里 g_panic_record 只有 {record_bytes} 字节（下限 "
            f"{PANIC_RECORD_MIN_BYTES}）：ipsr / reset_csr / wd_sync_fail / heap_free_min "
            "至少有一个没编进去，"
            "调试器读现场时看不到“这次是中断还是任务触发 / 原始复位标志 / 看门狗有没有改成功 / "
            "panic 时堆还剩多少”"
        )
    # 堆与任务栈的水位必须在镜像里可观测：panic 码 2（堆不足）和 7（栈溢出）的根因
    # 只能靠“当时还剩多少”回答。只是把 config 打开不算数 ——
    # INCLUDE_uxTaskGetStackHighWaterMark=1 只让函数可编，没人调用时
    # --gc-sections 会把它和记录变量一起丢掉，配置与源码全在、镜像里什么都没有。
    # 四个符号同时在场才算真的接上：两个采样入口 + 两个记录变量。
    observing = [
        name
        for name in (
            "xPortGetMinimumEverFreeHeapSize",
            "uxTaskGetStackHighWaterMark",
            "g_heap_free_min",
            "g_stack_free_min",
        )
        if not re.search(rf"^[0-9a-fA-F]+\s+[A-Za-z]\s+{name}$", symbols, re.MULTILINE)
    ]
    if observing:
        errors.append(
            f"产物 {elf.name} 里没有 {', '.join(observing)}：堆/任务栈的水位在板上不可观测"
            "（没人调用就会被 --gc-sections 丢掉，配置项开着也进不了镜像）；"
            "检查 FreeRTOSConfig.h 的 INCLUDE_uxTaskGetStackHighWaterMark 与 osl_health 的接线"
        )
    # IWDG 的同步等待必须带失败分支：只写 while 循环时，成功与 0xFFFF 次耗尽共用
    # 同一个出口（反汇编里就一条 bx lr），LSI 没起振、PR/RLR 没生效这件事完全不可见。
    # 判据：函数里必须至少有一条 `str`（g_iwdg_sync_fail++ 在 -Og 下的落地形态）。
    objdump = tool_binary("objdump")
    if objdump is None:
        errors.append("找不到 arm-none-eabi-objdump，无法校验看门狗同步等待的失败分支")
    elif _skip_on_release_build(
        "iwdg_wait_sync 的独立符号与失败分支（-Os 会把它内联，符号不再存在）"
    ):
        pass
    else:
        dump = run_tool_args(objdump, "-d", "--disassemble=iwdg_wait_sync", elf.as_posix())
        if "iwdg_wait_sync" not in dump:
            errors.append(
                f"产物 {elf.name} 里找不到 iwdg_wait_sync：它被内联或改名，"
                "verify 无法证明 PR/RLR 的同步等待带失败分支（g_iwdg_sync_fail 不累加）"
            )
        elif not re.search(r"\bstr\b", dump):
            errors.append(
                f"产物 {elf.name} 的 iwdg_wait_sync 里没有任何 str：成功与超时共用一个出口，"
                "LSI 没起振时 PR/RLR 未生效不可见（N18 的回退）"
            )
    # 软件定时器是“开了但没人用”的典型：configUSE_TIMERS=1 会让 timers.c 整个编进来，
    # 即使自研代码一次都没调 xTimer* —— 一个 timer task（128 词栈 + TCB）、一个 8 项
    # 定时器队列，加上整条过期链表的维护逻辑全白占（实测约 510 B Flash + 约 0.8 KB RAM）。
    # 判据用符号在场而不是“有没有调用”：关掉后 timers.c 不参与编译，符号必然消失；
    # 哪天真需要软件定时器，回来删掉这条断言并写明理由 —— 不许静默重新打开。
    timer_symbols = [
        name
        for name in (
            "xTimerCreateTimerTask",
            "prvTimerTask",
            "prvProcessTimerOrBlockTask",
        )
        if re.search(rf"^[0-9a-fA-F]+\s+[A-Za-z]\s+{name}$", symbols, re.MULTILINE)
    ]
    if timer_symbols:
        errors.append(
            f"产物 {elf.name} 里编进了软件定时器（{', '.join(timer_symbols)}）："
            "configUSE_TIMERS 开着但工程里没有任何 xTimer* 使用方，"
            "timer task 的栈/TCB、8 项定时器队列与过期链表维护都在白占 RAM 和 Flash"
            "（要么在 FreeRTOSConfig.h 关掉它，要么真正用起来并说明理由）"
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
        if "-Wl,-u,_write" not in text and "-u,_write" not in text:
            errors.append(
                "toolchain.make 的 LDFLAGS 里没有 `-Wl,-u,_write`；"
                "osl 的 _write 无调用方，会被 --gc-sections 连同 g_last_write 一起丢掉"
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
        if not actual:
            # 链接脚本的 MEMORY 里没有这个区域：直接跳过等于这条容量断言被静默取消。
            errors.append(
                f"链接脚本的 MEMORY 里没有 {region} 区域，无法核对 cpu.{key}；"
                "要么把该区域补回链接脚本，要么从 json 里删掉这一项"
            )
            continue
        if isinstance(declared, bool) or not isinstance(declared, int):
            # 写成 null / "64" / true 时旧逻辑会 continue，这条断言等于不存在。
            # 注意 load_info() 会用 setdefault 把「缺失的键」补回默认值，所以能走到
            # 这里的一定是「键在、类型错」。
            errors.append(
                f"proj/project-info.json 的 cpu.{key} 不是整数（当前 {declared!r}）；"
                f"它必须等于链接脚本 {region} 的容量。写成字符串或 null 会让这条容量断言"
                "直接失效（历史上 flash_kb 曾写 2048 而 ROM 只有 1024 KB，构建完全不报错）"
            )
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
    archive = OUT_ARTIFACT_DIR / "lib" / "libosl.a"
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
    maps = sorted(OUT_ARTIFACT_DIR.glob("*.map"))
    for map_file in maps:
        if "libc_a-mlock.o" in map_file.read_text(encoding="utf-8", errors="replace"):
            errors.append(
                f"{map_file.name} 里链接了 libc_nano.a(libc_a-mlock.o)（newlib 的空锁）；"
                "本工程的 __malloc_lock 没有被采用，检查 LDFLAGS 是否缺少 -Wl,-u,__malloc_lock"
            )
    for elf in sorted(OUT_ARTIFACT_DIR.glob("*.elf")):
        elf_symbols = run_tool(nm, elf)
        if not elf_symbols or not re.search(r"^[0-9a-fA-F]+\s+T\s+_sbrk$", elf_symbols, re.MULTILINE):
            errors.append(f"产物 {elf.name} 里没有强符号 _sbrk（被 -lnosys 的空桩覆盖）")
    return errors
def collect_lib_member_errors() -> list[str]:
    """镜像里最新提取的库成员必须与 LIB_MEMBER_WHITELIST 一致。

    这是 `/DISCARD/` 那三条无效规则（见常量注释）的替代品：从"假装丢弃"改成
    "把实际提取到的成员逐个审计"。map 是机器事实，不依赖文本扫描，也不怕宏包装。
    """
    errors: list[str] = []
    maps = sorted(OUT_ARTIFACT_DIR.glob("*.map"))
    if not maps:
        return errors
    release_build = _artifact_is_release_build()
    for map_file in maps:
        text = map_file.read_text(encoding="utf-8", errors="replace")
        for archive, allowed in LIB_MEMBER_WHITELIST.items():
            tolerated = set(allowed)
            if archive in LIB_MEMBER_RELEASE_ONLY and release_build:
                tolerated |= set(LIB_MEMBER_RELEASE_ONLY[archive])
            members = {
                name
                for name in re.findall(rf"{re.escape(archive)}\(([^)\s]+)\)", text)
                if name != "*"
            }
            unexpected = sorted(members - tolerated)
            if unexpected:
                errors.append(
                    f"{map_file.name} 里 {archive} 被拉进了白名单外的成员 {', '.join(unexpected)}；"
                    "构建照样成功、只有体积在涨 —— 确认是有意的就把它加进 "
                    "LIB_MEMBER_WHITELIST 并在 .skills/工程规格.md 里写明用途"
                )
            missing = sorted(set(allowed) - members)
            if missing:
                errors.append(
                    f"{map_file.name} 里缺少 {archive} 的白名单成员 {', '.join(missing)}；"
                    "预期用到的库代码没有进镜像，检查 -Wl,-u 与链接顺序"
                )
    return errors
def collect_freertos_unit_errors() -> list[str]:
    """`configUSE_*` 与 FreeRTOS 编译单元必须同进同退（双向）。

    现有产物级断言只看镜像里有没有 timer 符号，看不见「开关关了、单元还在编」：
    timers.c 内部被 `#if` 掉后符号自然消失，`--gc-sections` 再丢一遍，产物一片干净。
    代价是每个 job 都在编一份永远进不了镜像的文件。

    反方向同样查：开关是 1 却没编出 .o，说明构建清单把它裁掉了，真用到时才在
    链接期报 undefined —— 那时已经离问题现场很远。
    """
    errors: list[str] = []
    config = ROOT / "osl" / "inc" / "FreeRTOSConfig.h"
    if not config.is_file():
        return ["缺少 osl/inc/FreeRTOSConfig.h，无法核对 FreeRTOS 编译单元"]
    text = _strip_comments_keep_offsets(config.read_text(encoding="utf-8"))
    obj_dir = out_object_dir(DEFAULT_BUILD_VARIANT) / "osl" / "src" / "freertos"
    if not obj_dir.is_dir():
        return errors
    for filename, macro in FREERTOS_CONFIG_GATED:
        match = re.search(rf"#define\s+{macro}\s+[\(\s]*(\d+)", text)
        if match is None:
            errors.append(
                f"FreeRTOSConfig.h 里找不到 {macro} 的数值定义，无法核对 {filename}"
            )
            continue
        value = match.group(1)
        obj = obj_dir / (filename[: -len(".c")] + ".o")
        if value == "0" and obj.is_file():
            errors.append(
                f"{macro} = 0，但 {obj.relative_to(ROOT).as_posix()} 仍被编译："
                "配置说不启用、构建却在编它（每个 job 白编一次，产物里也看不到）"
            )
        elif value != "0" and not obj.is_file():
            errors.append(
                f"{macro} = {value}，但 {obj.relative_to(ROOT).as_posix()} 不存在："
                "配置启用了该模块、构建却把它裁掉了（真用到时才在链接期报 undefined）"
            )
    return errors
def collect_unlinked_member_errors() -> list[str]:
    """自研库的每个成员都必须真的被链接，未链接的要在白名单里写明理由。"""
    errors: list[str] = []
    archive_tool = tool_binary("ar")
    if archive_tool is None:
        return ["找不到 arm-none-eabi-ar，无法核对归档成员是否真的被链接"]
    lib_dir = OUT_ARTIFACT_DIR / "lib"
    maps = sorted(OUT_ARTIFACT_DIR.glob("*.map"))
    if not lib_dir.is_dir() or not maps:
        return errors
    for archive in sorted(lib_dir.glob("lib*.a")):
        key = archive.name
        listing = run_tool_args(archive_tool, "t", archive.as_posix())
        members = {
            line.strip()
            for line in listing.splitlines()
            if line.strip() and line.strip() != "/"
        }
        layer = archive.stem[3:]
        text = maps[0].read_text(encoding="utf-8", errors="replace")
        linked = {
            name
            for name in re.findall(rf"{re.escape(key)}\(([^)\s]+)\)", text)
            if name != "*"
        }
        allowed = UNLINKED_MEMBER_WHITELIST.get(key, {})
        for member in sorted(members - linked):
            if member in allowed:
                continue
            errors.append(
                f"{key} 的成员 {member} 没有被链接（层 {layer}）："
                "新加一个没人调用的编译单元时，产物断言与镜像大小都不会变，"
                "只有归档成员表多一行 —— 确认是有意的就加进 "
                "UNLINKED_MEMBER_WHITELIST 并写明理由，否则删掉该源文件"
            )
    return errors
def collect_warning_gate_errors() -> list[str]:
    """编译警告门禁必须真的开着：-Wall -Wextra -Werror 与 -fstack-usage。

    与 FLAG_DEPS 是同类问题：门禁写在生成器里，但生成器本身可以被改回去，而代码
    干净时"有没有 -Werror"编出来的镜像完全一样——差别只有下一次新引入告警时才显现。
    所以这里直接对生成物 toolchain.make 断言这几个 flag 还在。
    """
    errors: list[str] = []
    path = ROOT / "toolchain.make"
    if not path.is_file():
        return [f"缺少 {path.relative_to(ROOT).as_posix()}，无法确认编译警告门禁"]
    text = read_text(path)
    for flag in ("-Wall", "-Wextra", "-Werror", "-fstack-usage"):
        if flag not in text:
            errors.append(
                f"{path.relative_to(ROOT).as_posix()} 里没有 {flag}；"
                "编译警告门禁被关掉了（干净代码看不出差别，新引入的告警会漏过去）"
            )
    return errors
def collect_periodic_task_errors() -> list[str]:
    """周期任务必须用 vTaskDelayUntil，而且基准变量必须在循环外只取一次。

    和 isr-prio 同类：纯静态、不编译、不要产物。改回 vTaskDelay 不会让任何产物断言
    变红（符号照样在、尺寸几乎不变），只能靠读源码守住。

    为什么不能只判"函数体里有没有 vTaskDelayUntil("：
    `vTaskDelayUntil(&last_wake, inc)` 的语义是 `xTimeToWake = *pxPreviousWakeTime + inc`，
    再把 `*pxPreviousWakeTime` 更新成 `xTimeToWake`——语义整个挂在这个外部变量上。
    基准每拍重取（把赋值挪进 for 里），唤醒时刻就从 t0+p、t0+2p… 退化成 now+p、now2+p…，
    与被替换掉的那个 vTaskDelay 在数学上等价，而调用名一个不少。
    这条退化实测过：只判调用名时，改完照样"自检通过"。
    所以这里查结构：基准变量在函数体内恰好被赋值一次，且位于首个循环之前。
    """
    errors: list[str] = []
    for relative, signature, name in PERIODIC_TASKS:
        text = read_text(ROOT / relative)
        if not text:
            errors.append(f"缺少 {relative}")
            continue
        body = _function_body(text, signature)
        if body is None:
            errors.append(f"{relative} 里找不到 {name}；无法确认周期基准是否固定")
            continue
        if "vTaskDelay(" in body:
            errors.append(
                f"{relative} 的 {name} 用了 vTaskDelay 而不是 vTaskDelayUntil："
                "循环体的执行时间会累加进周期"
            )
            continue
        call = re.search(r"vTaskDelayUntil\s*\(\s*&\s*([A-Za-z_][A-Za-z0-9_]*)", body)
        if call is None:
            errors.append(f"{relative} 的 {name} 没有把基准变量的地址传给 vTaskDelayUntil")
            continue
        baseline = call.group(1)
        assigns = [m.start() for m in re.finditer(rf"(?<![&\w]){baseline}\s*=(?!=)", body)]
        if len(assigns) != 1:
            errors.append(
                f"{relative} 的 {name} 里基准变量 {baseline} 被赋值 {len(assigns)} 次；"
                "必须只在循环外取一次（每拍重取等于退化成 vTaskDelay）"
            )
            continue
        loop = re.search(r"\b(?:for|while)\s*\(|\bdo\b", body)
        if loop is None:
            errors.append(f"{relative} 的 {name} 里找不到循环，无法确认基准赋值的位置")
            continue
        if assigns[0] >= loop.start():
            errors.append(
                f"{relative} 的 {name} 把基准变量 {baseline} 的赋值放在了循环内；"
                "vTaskDelayUntil 会退化成 vTaskDelay"
            )
    return errors

def _call_arguments(code: str, callee: str) -> list[tuple[int, list[str]]]:
    """按顶层逗号切出 callee 每次调用的实参，返回 (行号, 实参列表)。"""
    calls: list[tuple[int, list[str]]] = []
    for match in re.finditer(callee + r"\s*\(", code):
        depth = 1
        index = match.end()
        current = index
        args: list[str] = []
        if match.end() >= len(code):
            args = [""]
        else:
            for position in range(index, len(code)):
                char = code[position]
                if char in "([{":
                    depth += 1
                elif char in ")]}":
                    depth -= 1
                    if depth == 0:
                        args.append(code[current:position])
                        break
                elif char == "," and depth == 1:
                    args.append(code[current:position])
                    current = position + 1
        line = code.count("\n", 0, match.start()) + 1
        calls.append((line, [arg.strip() for arg in args]))
    return calls
def _priority_value(expression: str) -> int | None:
    """把 `tskIDLE_PRIORITY + 2U` / `3` 这类写法折成整数；静态算不出来就返回 None。"""
    expression = expression.strip()
    match = re.fullmatch(r"tskIDLE_PRIORITY\s*(?:\+\s*(\d+)\s*[Uu]?)?", expression)
    if match is not None:
        return int(match.group(1) or 0)
    match = re.fullmatch(r"(\d+)\s*[Uu]?", expression)
    if match is not None:
        return int(match.group(1))
    return None
def collect_task_priority_errors() -> list[str]:
    """任务优先级必须落在 configMAX_PRIORITIES 之内，且配置不许比实际用法更宽。

    后半句才是这条断言的目的：`configMAX_PRIORITIES` 直接决定 `pxReadyTasksLists[]`
    的元素数（`List_t` 20 B/项，实测 7 项 = 140 B）。自研层最高只用优先级 1 时，
    多出来的 5 项永远不会被任何任务挂上，是纯 RAM 白占——而它在编译、链接、尺寸统计
    里全都看不出来：`size` 只报 bss 总量，不会告诉你其中 100 B 是从没被用到的链表头。
    只能靠比"配置允许多少"和"实际用多少"才发现。反过来，把任务优先级提到配置之外
    则会让 `xTaskCreate` 直接失败（走 panic 码 2），这里一并守住。
    """
    errors: list[str] = []
    if not FREERTOS_CONFIG.is_file():
        return [f"缺少 {FREERTOS_CONFIG.relative_to(ROOT).as_posix()}，无法确认任务优先级上界"]
    config_text = read_text(FREERTOS_CONFIG)
    match = re.search(r"#define\s+configMAX_PRIORITIES\s+(\d+)", config_text)
    if match is None:
        return ["FreeRTOSConfig.h 里找不到 configMAX_PRIORITIES，无法确认任务优先级上界"]
    max_priorities = int(match.group(1))

    highest_used = 0
    for relative in TASK_SOURCES:
        path = ROOT / relative
        text = read_text(path)
        if not text:
            errors.append(f"缺少 {relative}，无法确认该文件里的任务优先级")
            continue
        code = _strip_comments_keep_offsets(text)
        calls = _call_arguments(code, "xTaskCreate")
        if not calls:
            errors.append(f"{relative} 里找不到 xTaskCreate，无法确认任务优先级")
            continue
        for line, args in calls:
            if len(args) < 5:
                errors.append(f"{relative}:{line} 的 xTaskCreate 实参少于 5 个，无法确认优先级")
                continue
            value = _priority_value(args[4])
            if value is None:
                errors.append(
                    f"{relative}:{line} 的 xTaskCreate 优先级写成 `{args[4]}`，"
                    "静态算不出数值；请写成 tskIDLE_PRIORITY + N 或整数字面量"
                )
                continue
            if value >= max_priorities:
                errors.append(
                    f"{relative}:{line} 的 xTaskCreate 优先级 {value} 超出上界 "
                    f"{max_priorities - 1}（configMAX_PRIORITIES = {max_priorities}）："
                    "xTaskCreate 会失败并走 panic 码 2"
                )
                continue
            highest_used = max(highest_used, value)

    if highest_used + 1 < max_priorities:
        spare = max_priorities - (highest_used + 1)
        errors.append(
            f"configMAX_PRIORITIES = {max_priorities}，但自研层用到的最高优先级是 {highest_used}；"
            f"pxReadyTasksLists[] 有 {spare} 项永远不会被挂上"
            f"（{spare} × {READY_LIST_ITEM_BYTES} B = {spare * READY_LIST_ITEM_BYTES} B 纯白占）"
        )
    return errors
def _panic_code_value(expression: str) -> int | None:
    """把 `OSL_PANIC_ASSERT` / `3U` 折成码值；静态算不出来返回 None。"""
    expression = expression.strip()
    match = re.fullmatch(r"OSL_PANIC_([A-Z_]+)", expression)
    if match is not None:
        return PANIC_CODE_BY_NAME.get(match.group(1))
    match = re.fullmatch(r"(\d+)\s*[Uu]?", expression)
    if match is not None:
        return int(match.group(1))
    return None
def collect_panic_code_errors() -> list[str]:
    """panic 码必须落在 0..7，而且播报前必须钳位。

    三灯只有 3 位，`osl_panic_report` 里的 `(raw >> bit) & 1` 只取低 3 位：传 9 会被
    播成 1，读灯的人会去查"调度器起不来"；传 8 会被播成 0，四轮全灭读起来像没故障。
    两条都要守：

      1) 自研层每一处 `osl_panic_report` / `fml_panic` 的实参，必须是 `OSL_PANIC_*`
         名字或 0..7 的字面量——这条防"新加一个码值忘了改三灯编码"；
      2) `osl_panic_report` 自己必须把**播报值**钳到上界，同时把**原始值**留在
         `g_panic_record.code`——这条防"有人直接拿 raw 去闪灯"。

    第 2 条是弱证据（它只证明钳位那一行还在），但它是这条缺陷唯一能静态回链的地方：
    运行时行为要真机才能验证，而本工程目前零硬件实测。
    """
    errors: list[str] = []
    for directory in ("apl", "fml", "bsl", "osl"):
        base = ROOT / directory
        if not base.is_dir():
            continue
        for source in iter_sources_under(base):
            if "freertos" in source.parts:
                continue
            relative = source.relative_to(ROOT).as_posix()
            code = _strip_comments_keep_offsets(read_text(source))
            for callee in PANIC_CALLERS:
                for line, args in _call_arguments(code, callee):
                    if len(args) != 1 or not args[0]:
                        continue
                    if PANIC_DECL_ARGUMENT.fullmatch(args[0]):
                        continue
                    value = _panic_code_value(args[0])
                    if value is None:
                        # 只有转发形参才允许"算不出"：OSL_PANIC_* 与字面量都在上一步
                        # 折过值，这里放行的必然是不带运算符的标识符。
                        if PANIC_FORWARD.fullmatch(args[0]):
                            continue
                        errors.append(
                            f"{relative}:{line} 的 {callee} 实参写成 `{args[0]}`，"
                            "静态算不出码值；请用 OSL_PANIC_* 名字或 0..7 的整数字面量"
                        )
                        continue
                    if not 0 <= value <= PANIC_CODE_MAX:
                        shown = value & PANIC_CODE_MAX
                        errors.append(
                            f"{relative}:{line} 的 {callee} 传了码 {value}，"
                            f"超出 0..{PANIC_CODE_MAX}；三灯只取低 3 位，会播成 {shown}，"
                            "与其他故障撞码"
                        )

    fault_rel = FAULT_SOURCE.relative_to(ROOT).as_posix()
    if FAULT_SOURCE.is_file():
        body = _function_body(read_text(FAULT_SOURCE), r"void\s+osl_panic_report\s*\(")
        if body is None:
            errors.append(f"{fault_rel} 里找不到 osl_panic_report 的函数体，无法确认码值钳位")
        else:
            # 钳位必须落在 shown 的赋值语句里。只查「函数体出现该宏」会被同函数里
            # 别的引用满足（例如按码清「别的族」时判断 raw != OSL_PANIC_STACK_OVERFLOW），
            # 于是把 shown = … 换成 shown = raw 之后判据一声不吭。
            clamp_start = body.find("shown =")
            clamp_end = body.find(";", clamp_start) if clamp_start >= 0 else -1
            if clamp_start < 0:
                clamp_stmt = ""
            elif clamp_end < 0:
                clamp_stmt = body[clamp_start:]
            else:
                clamp_stmt = body[clamp_start:clamp_end]
            if "OSL_PANIC_STACK_OVERFLOW" not in clamp_stmt:
                errors.append(
                    f"{fault_rel} 的 osl_panic_report 没有引用 OSL_PANIC_STACK_OVERFLOW"
                    " 做播报值钳位：上界判断必须出现在 shown 的赋值语句里，函数体别处"
                    "引用该宏不算钳位；越界码会被三灯取低 3 位播错，与其他故障撞码"
                )
            if "g_panic_record.code = raw" not in body:
                errors.append(
                    f"{fault_rel} 的 osl_panic_report 没有把原始码写进 g_panic_record.code；"
                    "现场就分不出'码真的越界'和'码本来就是这个值'"
                )
    return errors
def collect_panic_entry_ownership_errors() -> list[str]:
    """panic 语义入口的归属：它必须有自己的文件，不得寄生在 LED / 按键模块里。

    原状（A5）：`fml_led_panic()` 定义在 `fml/src/fml_led.c`、声明在
    `fml/inc/fml_led.h`，名字带 led，函数体却一行都不碰 LED —— 它只把 code 转给
    `osl_panic_report()`。两处副作用都是静态可查的，运行期看不见：

      1) `fml_button.c` 为了 `fml_led_panic(2U)` 这一行调用而 include `fml_led.h`，
         按键模块凭空依赖 LED 模块；
      2) 读到名字的人会以为播报里含 LED 逻辑，而「为什么在这里停机」这类设计判断
         被埋在 LED 文件的注释里，改名之后才有地方放。

    四条判据只看源码，所以 `verify` 在 `make` 之前（update 内部）也能跑：
    文件存在、函数体确实转调 `osl_panic_report`、LED 文件里没有它的定义、
    按键文件不再 include LED 头。
    """
    errors: list[str] = []
    rel = FML_PANIC_SOURCE.relative_to(ROOT).as_posix()

    if not FML_PANIC_SOURCE.is_file():
        errors.append(
            f"{rel} 不存在：panic 语义入口（fml_panic）应当有自己的文件。"
            "它原来叫 fml_led_panic、定义在 fml/src/fml_led.c —— 名字带 led，"
            "函数体却只转调 osl_panic_report，一行都不碰 LED；"
            "按键模块为了一行调用就得 include fml_led.h"
        )
        # 文件都不在，后面三条都失去范围；只报这一条更接近病灶。
        return errors

    body = _function_body(read_text(FML_PANIC_SOURCE), r"void\s+fml_panic\s*\(")
    if body is None:
        errors.append(f"{rel} 里找不到 fml_panic 的函数体，归属判据失去了范围")
    else:
        if "osl_panic_report(" not in body:
            errors.append(
                f"{rel} 的 fml_panic 没有调用 osl_panic_report："
                "本层只做语义映射，播报与停机都在 osl，另起一套播报就会撞码"
            )
        for driver in ("board_led_", "board_beep_"):
            if driver in body:
                errors.append(
                    f"{rel} 的 fml_panic 体内出现了 {driver}："
                    "panic 入口不负责执行器，三灯与蜂鸣的驱动都在 osl/bsl"
                )

    if FML_LED_SOURCE.is_file():
        code = _strip_comments_keep_offsets(read_text(FML_LED_SOURCE))
        if re.search(r"void\s+fml_panic\s*\(", code):
            errors.append(
                f"{FML_LED_SOURCE.relative_to(ROOT).as_posix()} 里又定义了 fml_panic："
                "panic 入口退回 LED 模块了 —— LED 模块只该管亮灭与闪烁周期，"
                "停机理由属于 panic 语义，不归它"
            )

    if FML_BUTTON_SOURCE.is_file():
        code = _strip_comments_keep_offsets(read_text(FML_BUTTON_SOURCE))
        if re.search(r'#include\s+"fml_led\.h"', code):
            errors.append(
                f"{FML_BUTTON_SOURCE.relative_to(ROOT).as_posix()} 又 include 了 fml_led.h："
                "按键模块只在任务创建失败时报码 2，应当 include fml_panic.h，"
                "不该为一次播报而依赖 LED 模块"
            )
    return errors
def collect_observability_errors() -> list[str]:
    """三条"故障可观测性"开关必须有门禁：它们各是一行宏，关掉后全流程都不会红。

    本工程零硬件实测，能拿到故障证据的通道只剩三条，而每一条都挂在一个宏上：

      * `configUSE_TICK_HOOK`：`vApplicationTickHook` 是喂狗与 MSP 栈守卫唯一的挂载点。
        置 0 后 `tasks.c` 不再调用它，符号被 `--gc-sections` 丢掉——看门狗永不启动、
        主栈越界再没人检查，而构建、链接、`size`、`verify` 全都照旧通过。
      * `configCHECK_FOR_STACK_OVERFLOW`：置 0 之后任务栈越界的最后一道检测消失，
        `vApplicationStackOverflowHook` 一起被 gc，码 7 永远不会出现。
      * `configUSE_MALLOC_FAILED_HOOK`：置 0 后堆不足从"码 2 + `.noinit` 现场"退化
        成 `pvPortMalloc` 返回 NULL，调用方若不检查就是静默失败。

    所以这里既守宏的取值，也守 tick hook 函数体里那两件事还在——宏对了、函数被搬走
    或调用被删掉，同样等于通道断了。
    """
    errors: list[str] = []
    config_rel = FREERTOS_CONFIG.relative_to(ROOT).as_posix()
    if not FREERTOS_CONFIG.is_file():
        return [f"缺少 {config_rel}，无法确认三条可观测性开关的取值"]
    code = _strip_comments_keep_offsets(read_text(FREERTOS_CONFIG))
    for name, expected in OBSERVABILITY_DEFINES:
        match = re.search(rf"#define\s+{name}\s+(\S+)", code)
        if match is None:
            errors.append(
                f"{config_rel} 里找不到 {name}；它决定一条故障可观测通道是否还在，不能默默消失"
            )
            continue
        value = match.group(1).rstrip("Uu")
        if value != expected:
            errors.append(
                f"{config_rel} 的 {name} = {value}，必须是 {expected}："
                "关掉它只会在故障时少一条证据，构建与尺寸全都看不出来"
            )

    hook_path = ROOT / TICK_HOOK_SOURCE
    if not hook_path.is_file():
        errors.append(f"缺少 {TICK_HOOK_SOURCE}，无法确认 vApplicationTickHook 还在喂狗与查栈")
        return errors
    body = _function_body(read_text(hook_path), r"void\s+vApplicationTickHook\s*\(")
    if body is None:
        errors.append(
            f"{TICK_HOOK_SOURCE} 里没有 vApplicationTickHook："
            "喂狗与 MSP 栈守卫都挂在它上面，符号被 gc 掉时没有任何检查会红"
        )
        return errors
    for call in TICK_HOOK_CALLS:
        if call not in body:
            calls_name = call.rstrip("(")
            errors.append(
                f"{TICK_HOOK_SOURCE} 的 vApplicationTickHook 里没有调用 {calls_name}："
                "每 tick 的这条通道少了它，故障时就少一份现场"
            )

    # 看门狗「只启动一次」的幂等守卫。
    # IWDG 一旦启动就不能关，重配它的代价是两次跨时钟域同步等待（写 PR / 写 RLR
    # 后各等约 5 个 LSI 周期 ≈ 156 µs）。每 tick 都跑一遍，tick 中断会被吃掉约
    # 三成算力，而这一步此前没有任何断言在场：
    #   collect_observ_artifact_errors 守的是 stack_guard_check / osl_watchdog_feed /
    #   xPortGetMinimumEverFreeHeapSize 三条；collect_boot_init_errors 守的是
    #   osl_watchdog_start 与 osl_watchdog_relax 的**函数体**档位 —— 都不看它的调用条件。
    # 所以把 if 拆掉直接调，源码里 osl_watchdog_start( 仍在、产物里 bl 仍在，全绿。
    if "osl_watchdog_start" not in body:
        errors.append(
            f"{TICK_HOOK_SOURCE} 的 vApplicationTickHook 没有启动看门狗："
            "IWDG 永不启用，卡死后板子只会一直停在那里，没有自恢复"
        )
    elif body.count("watchdog_started") < 2:
        errors.append(
            f"{TICK_HOOK_SOURCE} 的 vApplicationTickHook 没有「只启动一次」的守卫："
            "把 osl_watchdog_start() 的 if 拆掉后，每个 tick 都会重配 IWDG —— "
            "写 PR / 写 RLR 后各要等约 5 个 LSI 周期，两次跨域同步会吃掉 tick "
            "中断约三成算力；而产物断言只看有没有 bl，不看它的调用条件"
        )
    return errors
def collect_panic_task_name_errors() -> list[str]:
    """malloc 失败钩子必须写出"哪个任务"失败，不能只留一个码。

    `vApplicationMallocFailedHook` 是 `pvPortMalloc` 失败路径上最后的观察点。它只写
    `magic` 与 `code` 的话，调试器读到的是 `code = 2` + `heap_free_min`，而 `task_name`
    / `task` 仍是上一次留在 `.noinit` 里的旧值（首次上电甚至是随机内容）——码 2 恰好
    是最需要"哪个任务、哪个创建点"的场合，这一点只有这条钩子能补。

    另有一个易错点：`pcTaskGetName(NULL)` 取的是 `pxCurrentTCB`，调度器启动前
    `pxCurrentTCB` 还是 NULL，直接解引用会二次 fault。必须先经
    `xTaskGetCurrentTaskHandle()` 判空。所以这里既要求出现 `pcTaskGetName`，
    也要求写进 `g_panic_record.task_name`。
    """
    errors: list[str] = []
    relative = FAULT_SOURCE.relative_to(ROOT).as_posix()
    if not FAULT_SOURCE.is_file():
        return [f"缺少 {relative}，无法确认 malloc 失败钩子是否记录任务名"]
    body = _function_body(read_text(FAULT_SOURCE), MALLOC_HOOK_SIGNATURE)
    if body is None:
        return [
            f"{relative} 里找不到 vApplicationMallocFailedHook："
            "configUSE_MALLOC_FAILED_HOOK = 1 时它必须存在，否则堆不足从码 2 退化成静默 NULL"
        ]
    if "pcTaskGetName" not in body:
        errors.append(
            f"{relative} 的 vApplicationMallocFailedHook 没有取当前任务名（pcTaskGetName）："
            "现场只剩 code = 2 与堆水位，读不出是哪个任务/哪个创建点失败的"
        )
    if "task_name" not in body:
        errors.append(
            f"{relative} 的 vApplicationMallocFailedHook 没有把任务名写进 g_panic_record.task_name："
            "record 里留着上一次 panic 的旧名，比没有更误导"
        )
    return errors
def collect_panic_assert_task_errors() -> list[str]:
    """configASSERT 失败（码 3）也必须记下"哪个任务失败"。

    与码 2 同构：`vAssertFailed` 只写 magic / assert_file / assert_line，`task` 与
    `task_name` 仍是 `.noinit` 里的上一次值（首次上电是随机内容）。码 3 恰恰是最难
    复现的场合——它可能来自任意任务、任意断言点，读到一个旧任务名比读到空名更误导，
    会把人指向错误的创建点。

    和 malloc 钩子一样不能直接 `pcTaskGetName(NULL)`：调度器启动前 `pxCurrentTCB`
    是 NULL，解引用会二次 fault 并覆盖现场。必须先经 `xTaskGetCurrentTaskHandle()`
    判空，再走 `panic_copy_task_name()`（与码 2 共用同一份截断逻辑）。

    这条是纯文本检查（与 collect_panic_task_name_errors 同类）。它的作用是防"码 2 修了、
    码 3 忘了"这种半修——两条路径的失败后果一样，不能只守一条。
    """
    errors: list[str] = []
    relative = FAULT_SOURCE.relative_to(ROOT).as_posix()
    if not FAULT_SOURCE.is_file():
        return [f"缺少 {relative}，无法确认 configASSERT 是否记录任务名"]
    # `vAssertFailed` 与 HAL 的 `assert_failed` 共用 `panic_assert_from()`，判据盯实现体。
    text = FAULT_SOURCE.read_text(encoding="utf-8", errors="replace")
    body = _function_body(
        text,
        r"static\s+void\s+(?:__attribute__\s*\(\([^)]*\)\)\s+)?panic_assert_from\s*\(",
    )
    if body is None:
        return [f"{relative} 里找不到 panic_assert_from 的函数体"]
    vf = _function_body(text, r"void\s+vAssertFailed\s*\(")
    if vf is None:
        errors.append(f"{relative} 里找不到 vAssertFailed 的函数体")
    elif "panic_assert_from(" not in vf:
        errors.append(
            f"{relative} 的 vAssertFailed 没有调用 panic_assert_from()；"
            "configASSERT 与 HAL assert 会分叉"
        )
    label = "panic_assert_from"
    if "xTaskGetCurrentTaskHandle(" not in body:
        errors.append(
            f"{relative} 的 {label} 没有取当前任务句柄（缺 xTaskGetCurrentTaskHandle）；"
            "record 里的 task 会留上一次 panic 的旧值"
        )
    if "panic_copy_task_name(" not in body:
        errors.append(
            f"{relative} 的 {label} 没有调用 panic_copy_task_name()；"
            "任务名要么为空、要么是上次的旧名"
        )
    if not re.search(r"g_panic_record\.task\s*=", body):
        errors.append(
            f"{relative} 的 {label} 没有把任务句柄写进 g_panic_record.task"
        )
    return errors
def collect_panic_scene_errors() -> list[str]:
    """panic 现场必须是「这一次」的，不能是 .noinit 里上一次留下的旧值。

    magic 只证明「这里有过一条记录」，证明不了「这些字段属于本次故障」：
    g_panic_record 落在 .noinit，Reset_Handler 不清它，而 panic 之后板子必然复位，
    于是上一次的字段会原地冒充这一次的现场。三条都是可证的实际形态：

      1) fault_report() 在 SP 非法时跳过读帧（源码注释写的是「宁可留空」），但
         什么都不写等于保留上一次的 fault_pc —— 而同一段源码自己写过「错误的 PC
         比没有更糟」。所以 8 个帧字段必须在读帧之前显式清零。
      2) fault_kind 只有 fault_report() 写。UnhandledIRQ_Handler() 与三个钩子都不写，
         上一次 HardFault 的 fault_kind = 1 会留在这一次栈溢出/未实现中断的现场里，
         归因直接指向错误的方向。所以 UnhandledIRQ_Handler() 必须把它清成 0。
      3) malloc 钩子与 vAssertFailed 的注释都写「拿不到当前任务就留空名，绝不猜」，
         但实现只在 current != NULL 时写，NULL 时什么都不做 —— 留下的是上一次的
         任务名，正是注释说要避免的「指向错误的任务」。

    第 3 条只守 malloc / assert 两个钩子：vApplicationStackOverflowHook 的 pcTaskName
    由 FreeRTOS 从 TCB 传入，恒非 NULL，不存在空分支。
    """
    errors: list[str] = []
    relative = FAULT_SOURCE.relative_to(ROOT).as_posix()
    if not FAULT_SOURCE.is_file():
        return [f"缺少 {relative}，无法确认 panic 现场是否按代际清理"]

    source = read_text(FAULT_SOURCE)

    # ---- 1. 故障族必须由一个 helper 一次清整族 ----
    # 逐入口各清几条的下场是漏一条：漏掉的那条就是上一次的旧值。所以要求「一次
    # 清整族」的函数存在，且 17 个字段一个不少。
    clear_fn = "panic_clear_fault_scene"
    clear_body = _function_body(
        source, rf"static\s+void\s+{clear_fn}\s*\(\s*void\s*\)"
    )
    if clear_body is None:
        errors.append(
            f"{relative} 里找不到 {clear_fn}()：.noinit 里的 fault_* 与 unhandled_irq "
            "没有清零点，上一次故障的现场会冒充这一次"
        )
    else:
        missing = [
            name
            for name in PANIC_FAULT_FIELDS
            if f"g_panic_record.{name} = 0U;" not in clear_body
        ]
        if missing:
            errors.append(
                f"{relative} 的 {clear_fn}() 没有清 {'/'.join(missing)}："
                "这类字段只在 .noinit，不清就保留上一次的值 —— 上一次的 PC 被当成"
                "本次的现场，上一次的 fault_kind 把归因指向错误方向"
            )

    # ---- 2. 两个硬件异常入口都必须先清故障族，fault_report 的清必须在读帧之前 ----
    fault_body = _function_body(source, r"static\s+void\s+fault_report\s*\(")
    if fault_body is None:
        errors.append(
            f"{relative} 里找不到 fault_report 的函数体；"
            "SP 非法时帧字段是否留旧值无从确认"
        )
    elif f"{clear_fn}();" not in fault_body:
        errors.append(
            f"{relative} 的 fault_report 没有调用 {clear_fn}()："
            "SP 非法时它跳过读帧，8 个帧字段会保留上一次 panic 的值，"
            "把上一次的 PC 当成本次的现场（源码要求的是「宁可留空」）"
        )
    else:
        clear_at = fault_body.find(f"{clear_fn}();")
        frame_at = fault_body.find("frame = (uint32_t *)(sp + frame_offset);")
        if frame_at < 0:
            errors.append(
                f"{relative} 的 fault_report 里找不到读帧那一行；"
                "清零与读帧的先后无从确认"
            )
        elif clear_at > frame_at:
            errors.append(
                f"{relative} 的 fault_report 把 {clear_fn}() 放在读帧之后："
                "读到合法 SP 之后再清会把刚取到的帧冲掉，读到非法 SP 时又来不及"
            )

    irq_body = _function_body(source, r"void\s+UnhandledIRQ_Handler\s*\(")
    if irq_body is None:
        errors.append(
            f"{relative} 里找不到 UnhandledIRQ_Handler 的函数体；"
            "上一次 HardFault 的 fault_kind 会不会留在这里无从确认"
        )
    elif f"{clear_fn}();" not in irq_body:
        errors.append(
            f"{relative} 的 UnhandledIRQ_Handler 没有调用 {clear_fn}()："
            "fault_kind 属于故障族，这一次不是硬件框架帧时必须清掉，"
            "否则上一次 HardFault 的 fault_kind = 1 会留在这里，归因指向错误方向"
        )

    # ---- 3. current == NULL 时必须留空名，而不是留旧名 ----
    for pattern, label in (
        (MALLOC_HOOK_SIGNATURE, "vApplicationMallocFailedHook"),
        (
            r"static\s+void\s+(?:__attribute__\s*\(\([^)]*\)\)\s+)?panic_assert_from\s*\(",
            "panic_assert_from",
        ),
    ):
        body = _function_body(source, pattern)
        if body is None:
            errors.append(
                f"{relative} 里找不到 {label} 的函数体；"
                "拿不到当前任务时是否留旧名无从确认"
            )
            continue
        if "g_panic_record.task_name[0] = '\\0';" not in body:
            errors.append(
                f"{relative} 的 {label} 在拿不到当前任务时没有把任务名清空："
                "注释承诺的是「留空名，绝不猜」，不清就留下上一次 panic 的任务名，"
                "把人指向错误的创建点"
            )

    # ---- 4. 码 1 / 码 2 的堆读数：UNSET 时必须补采一次 ----
    report_body = _function_body(source, r"void\s+osl_panic_report\s*\(")
    if report_body is None:
        errors.append(
            f"{relative} 里找不到 osl_panic_report 的函数体；"
            "码 1 / 码 2 的 heap_free_min 是不是无意义的初值无从确认"
        )
    elif "if (g_heap_free_min == (uint32_t)OSL_HEALTH_UNSET)" not in report_body:
        errors.append(
            f"{relative} 的 osl_panic_report 没有识别 OSL_HEALTH_UNSET："
            "调度器启动前 panic（码 1 / 码 2）时 g_heap_free_min 还停在初值，"
            "落盘后读到的是无意义的 0xFFFFFFFF，而它正是码 2 的根因读数"
        )
    elif "xPortGetMinimumEverFreeHeapSize(" not in report_body:
        errors.append(
            f"{relative} 的 osl_panic_report 在初值时没有补采 "
            "xPortGetMinimumEverFreeHeapSize()：码 2 的现场只剩一个无意义的初值"
        )
    else:
        unset_at = report_body.find(
            "if (g_heap_free_min == (uint32_t)OSL_HEALTH_UNSET)"
        )
        ever_at = report_body.find("xPortGetMinimumEverFreeHeapSize(")
        store_at = report_body.find("g_panic_record.heap_free_min =")
        if store_at < 0:
            errors.append(
                f"{relative} 的 osl_panic_report 里找不到 g_panic_record.heap_free_min 的落盘"
            )
        elif not (unset_at < store_at and ever_at < store_at):
            errors.append(
                f"{relative} 的 osl_panic_report 把补采放在落盘之后："
                "落盘读到的仍是那个无意义的初值"
            )
    return errors
def collect_panic_generation_errors() -> list[str]:
    """panic 记录必须只带「这一次」的证据 —— 代际语义有三处残留。

    上一轮修的是「硬件入口的旧值冒充其它入口」（fault_report / UnhandledIRQ_Handler
    调 panic_clear_fault_scene）。这里补同一条线的另外三处，每处都能让现场读错方向：

      1) 故障族只在两个硬件入口里清。码 1/2/3/4/5/7 根本不经过它们，而
         osl_panic_report 只按码清了 assert 与 task 两族 —— 于是「上一次是
         HardFault、这一次是栈溢出」的记录里，code = 7 却带着上一次的
         fault_kind = 1 和上一次的 fault_pc。
      2) seq 靠 magic 判断「上一轮有没有留下记录」，但六个入口都在调用
         osl_panic_report **之前**先把 magic 写成 PANIC_MAGIC。判据因此恒真：
         首次上电（.noinit 是随机内容）读到的 seq 会是「随机值 + 1」，
         看起来像已经反复复位过几亿次。
      3) 码 1/2 的 heap_free_min 补采源在 heap_4 未初始化时返回 0（该变量初值
         就是 0，见 heap_4.c），落盘后读成「堆当时正好抽干」—— 比一个无意义的
         初值更像一条结论，也更误导。
    """
    errors: list[str] = []
    relative = FAULT_SOURCE.relative_to(ROOT).as_posix()
    if not FAULT_SOURCE.is_file():
        return [f"缺少 {relative}，无法确认 panic 记录是否按代际分族"]

    source = read_text(FAULT_SOURCE)
    report_body = _function_body(source, r"void\s+osl_panic_report\s*\(")
    if report_body is None:
        return [f"{relative} 里找不到 osl_panic_report 的函数体，代际语义无从确认"]

    # ---- 1. 不是硬件 fault 的码必须自己清故障族 ----
    clear_at = report_body.find("panic_clear_fault_scene();")
    if clear_at < 0:
        errors.append(
            f"{relative} 的 osl_panic_report 没有为非硬件码清故障族："
            "码 1/2/3/4/5/7 都不经过 fault_report，上一次 HardFault 的 "
            "fault_kind / fault_pc / fault_sp 会留在这一次的现场里，"
            "读到 code = 7 却看到 fault_kind = 1，归因方向直接错"
        )
    else:
        cond_at = report_body.rfind("if (", 0, clear_at)
        cond = report_body[cond_at:clear_at] if cond_at >= 0 else ""
        if "OSL_PANIC_HARDWARE_FAULT" not in cond:
            errors.append(
                f"{relative} 的 osl_panic_report 清故障族时没有排除 "
                "OSL_PANIC_HARDWARE_FAULT：硬件 fault 的现场由入口在调用前填好，"
                "无条件清会把刚取到的异常帧冲掉"
            )

    # ---- 2. 入口不得抢在 report 之前写 magic，否则 seq 的判据恒真 ----
    # 按符号名在两个候选源里找，不按 label 猜文件归属：osl 阶段 3 会把 _sbrk 搬进
    # 新文件，届时 label 与文件位置脱钩，`label == "_sbrk"` 这种硬编码会去错误
    # 文件里找函数体（找不到就报红、找到了更糟）。新增承载 panic 入口的文件时，
    # 把它加进这里的候选。
    for pattern, label in PANIC_ENTRY_PATTERNS:
        body = _function_body(source, pattern)
        if body is None:
            body = _function_body(read_text(OSL_PORT_ST_SOURCE), pattern)
        if body is None:
            where = " / ".join(
                p.relative_to(ROOT).as_posix()
                for p in (FAULT_SOURCE, OSL_PORT_ST_SOURCE)
            )
            errors.append(
                f"{where} 里都找不到 {label} 的函数体；"
                "它是否在 osl_panic_report 之前写 magic 无从确认"
            )
            continue
        entry_magic_at = body.find("g_panic_record.magic = PANIC_MAGIC;")
        report_at = body.find("osl_panic_report(")
        if entry_magic_at >= 0 and (report_at < 0 or entry_magic_at < report_at):
            errors.append(
                f"{relative} 的 {label} 在调用 osl_panic_report 之前就写了 "
                "g_panic_record.magic：seq 正是靠 magic 判断「上一轮有没有记录」，"
                "提前写让判据恒真，首次上电的 seq 会变成 .noinit 随机值 + 1"
            )

    seq_at = report_body.find("g_panic_record.seq =")
    report_magic_at = report_body.find("g_panic_record.magic = PANIC_MAGIC;")
    if seq_at < 0:
        errors.append(f"{relative} 的 osl_panic_report 里找不到 seq 的累加")
    elif report_magic_at >= 0 and report_magic_at < seq_at:
        errors.append(
            f"{relative} 的 osl_panic_report 先写 magic 再算 seq："
            "判据读到的是本次刚写的值，永远等于 PANIC_MAGIC"
        )

    # ---- 3. 堆补采源在「未初始化」时返回 0，不能当成「已耗尽」 ----
    ever_at = report_body.find("xPortGetMinimumEverFreeHeapSize(")
    store_at = report_body.find("g_panic_record.heap_free_min =")
    if ever_at >= 0 and store_at < 0:
        errors.append(f"{relative} 的 osl_panic_report 里找不到 heap_free_min 的落盘")
    elif ever_at >= 0:
        window = report_body[ever_at:store_at]
        if ("heap_min == 0U" not in window) or ("OSL_HEALTH_UNSET" not in window):
            errors.append(
                f"{relative} 的 osl_panic_report 没有区分「堆未初始化」与「堆已耗尽」："
                "heap_4 的 xMinimumEverFreeBytesRemaining 初值就是 0，从未分配过内存时"
                "补采得到 0，落盘后读起来像「堆当时正好抽干」"
            )

    # ---- 4. 码 7 有两个来源，只有钩子写任务族 ----
    # vApplicationStackOverflowHook 是码 7 的第一来源：它自己写 task / task_name，
    # 所以 osl_panic_report 把码 7 归进「入口已填好」那一类、不清任务族 —— 这个前提
    # 对它成立。第二个来源是 osl_boot.c 的 stack_guard_check（MSP 主栈越界），
    # 它拿不到任何任务身份，直接调 report 就会继承 .noinit 里上一代的名字：
    # 上一次 led_blink 栈溢出、这一次是主栈压穿，现场读出来是「led_blink 又溢出」，
    # task 还是一个可能已失效的 TCB 地址。判定域少的那一维就在这里：
    # 条件写的是「码 7 ⇒ 一定写过 task」，实际是「码 7 有两个来源，只有钩子写」。
    guard_body = _function_body(
        source, r"void\s+osl_panic_stack_guard_report\s*\(\s*void\s*\)"
    )
    if guard_body is None:
        errors.append(
            f"{relative} 里找不到 osl_panic_stack_guard_report；"
            "码 7 的第二个来源（MSP 越界）没有清任务族的地方，"
            "现场会继承 .noinit 里上一代的任务名"
        )
    else:
        task_at = guard_body.find("g_panic_record.task = NULL;")
        name_at = guard_body.find("g_panic_record.task_name[0] = '\\0';")
        report_at = guard_body.find("osl_panic_report(")
        if task_at < 0 or name_at < 0:
            errors.append(
                f"{relative} 的 osl_panic_stack_guard_report 没有同时清 task 与 task_name："
                "osl_panic_report 对码 7 不清任务族（它假定入口已把身份填好），"
                "少清一个就继承上一代的值"
            )
        elif report_at < 0 or task_at > report_at or name_at > report_at:
            errors.append(
                f"{relative} 的 osl_panic_stack_guard_report 清理晚于 osl_panic_report："
                "report 是 noreturn，写在它后面的清理永远执行不到"
            )

    boot_source = read_text(ROOT / "osl" / "src" / "osl_boot.c")
    guard_call = _function_body(boot_source, r"void\s+stack_guard_check\s*\(\s*void\s*\)")
    if guard_call is None:
        errors.append(
            "osl/src/osl_boot.c 里找不到 stack_guard_check；MSP 越界这条路径无从确认"
        )
    elif "osl_panic_stack_guard_report(" not in guard_call:
        errors.append(
            "osl/src/osl_boot.c 的 stack_guard_check 直接调 osl_panic_report(7)："
            "它写不出任务身份，而 report 对码 7 不清任务族 —— "
            "主栈越界的现场会带着上一代栈溢出任务的名字，归因到错误的栈"
        )
    elif re.search(r"osl_panic_report\s*\(", guard_call):
        errors.append(
            "osl/src/osl_boot.c 的 stack_guard_check 同时调了 osl_panic_report："
            "report 是 noreturn，两句并存时行为取决于谁在前，判据与现场都无法判定"
        )

    return errors
def collect_health_slot_errors() -> list[str]:
    """栈水位槽位表：越界必须可观测，槽数必须等于实际用法。

    现状（缺陷）是两件事同时不成立：

      * `osl_health_stack_mark(slot)` 在 `slot >= OSL_HEALTH_STACK_SLOTS` 时直接
        `return`。写错一个槽号，这个任务从此不再更新水位，而板上唯一的读数手段
        （调试器读 `g_stack_free_min[]`）看到的是"这个槽一直是初值"——和"任务栈
        富余"完全一样，没有任何东西会叫。
      * 表有 4 槽，自研层只用了 0 与 1（`fml_led` / `fml_button`），另外 8 B 永远
        写不进值，`size` 只报 bss 总量，看不出来。

    所以三条一起守：越界分支必须有可累加的计数、调用点槽号必须是可静态求值的
    小字面量、槽数必须正好等于用到的槽数。
    """
    errors: list[str] = []
    header_rel = HEALTH_HEADER.relative_to(ROOT).as_posix()
    source_rel = HEALTH_SOURCE.relative_to(ROOT).as_posix()
    if not HEALTH_HEADER.is_file() or not HEALTH_SOURCE.is_file():
        return [f"缺少 {header_rel} / {source_rel}，无法确认栈水位槽位表"]
    header = _strip_comments_keep_offsets(read_text(HEALTH_HEADER))
    match = re.search(r"#define\s+OSL_HEALTH_STACK_SLOTS\s+(\d+)", header)
    if match is None:
        return [f"{header_rel} 里找不到 OSL_HEALTH_STACK_SLOTS，无法确认槽位表尺寸"]
    slots = int(match.group(1))

    if HEALTH_SLOT_MARK not in header:
        errors.append(
            f"{header_rel} 没有声明 {HEALTH_SLOT_MARK}："
            "越界采样没有任何可读的证据，调试器无法区分'槽号写错'与'栈很富余'"
        )

    # osl_health_init 必须把两个「历史最低」复位成 UNSET。
    # 两个记录都落在 .bss，上电就是 0；而两处采样都是 `if (now < min) min = now`。
    # 一旦 init 不写 UNSET，min 从 0 起步就永远满足不了 `now < 0`：
    #   g_heap_free_min 永远是 0 → 「堆到底紧不紧张」这个读数失效；
    #   g_stack_free_min[slot] 每槽都停在 0 → 栈水位同样失效，而且读起来像「余量为 0」。
    # health 组映射的是 collect_artifact_errors（只查符号在不在镜像里），
    # health-slot 组查的是槽号与越界分支 —— 两处都不看这个复位值，所以这里单独守。
    init_body = _function_body(read_text(HEALTH_SOURCE), r"void\s+osl_health_init\s*\(")
    if init_body is None:
        errors.append(f"{source_rel} 里找不到 osl_health_init；历史最低的复位值无从确认")
    else:
        if re.search(r"#define\s+OSL_HEALTH_UNSET\s+0x", header) is None:
            errors.append(
                f"{header_rel} 里找不到 OSL_HEALTH_UNSET：历史最低没有「还没采过样」这个哨兵值"
            )
        elif init_body.count("OSL_HEALTH_UNSET") < 2:
            errors.append(
                f"{source_rel} 的 osl_health_init 没有把两组记录都复位成 OSL_HEALTH_UNSET："
                "g_heap_free_min 与 g_stack_free_min[] 都落在 .bss、上电为 0，"
                "漏复位的那一组会永远停在 0（采样判据 `now < min` 永不成立），"
                "板上读到的「历史最低」是假的"
            )

    body = _function_body(read_text(HEALTH_SOURCE), r"void\s+osl_health_stack_mark\s*\(")
    if body is None:
        return errors + [f"{source_rel} 里找不到 osl_health_stack_mark，无法确认越界分支"]
    if HEALTH_SLOT_MARK not in body:
        errors.append(
            f"{source_rel} 的 osl_health_stack_mark 越界时静默 return："
            "写错槽号等于该任务从此不再采样，板上没有任何读数能暴露它"
        )

    # 码 7 的根因读数必须进 .noinit 现场。
    # g_stack_free_min[] 与 g_stack_slot_err 都落在 .bss，Reset_Handler 会清零；
    # 而 panic 播报完是关中断自旋，板子必然被 IWDG 复位（32.7 s 后），
    # 所以这两个值如果不搬进 g_panic_record，复位后就只剩 0 —— 读起来像
    # 「栈余量充足」，码 7 会成为唯一一个「播报了、但根因永远查不到」的码。
    # 注意 build 的尺寸下限断言（g_panic_record ≥ 0x80）只查「够不够大」：
    # 往里加字段让它更大同样通过，所以那条永远发现不了「少了该有的字段」。
    fault_rel = FAULT_SOURCE.relative_to(ROOT).as_posix()
    record_h = ROOT / "osl" / "inc" / "osl_panic_record.h"
    fault_code = _strip_comments_keep_offsets(
        read_text(FAULT_SOURCE) + "\n" + read_text(record_h)
    )
    record_def = re.search(
        r"typedef\s+struct\s*\{.*?\}\s*panic_record_t\s*;", fault_code, re.S
    )
    if record_def is None:
        errors.append(
            f"{fault_rel} 里找不到 panic_record_t 的定义；"
            "栈水位与槽号计数能否跨复位无从确认"
        )
    else:
        record_text = record_def.group(0)
        for field_name, why in (
            ("stack_free_min", "码 7 的根因读数"),
            ("stack_slot_err", "「槽号写错」这件事本身"),
        ):
            if field_name not in record_text:
                errors.append(
                    f"{fault_rel} 的 panic_record_t 里没有 {field_name} 字段："
                    f"{why}在复位后就再也读不到 —— g_stack_free_min[] 与 "
                    "g_stack_slot_err 都在 .bss，Reset_Handler 会把它清零"
                )
    panic_text = _function_body(fault_code, r"void\s+osl_panic_report\s*\(")
    if panic_text is None:
        errors.append(
            f"{fault_rel} 里找不到 osl_panic_report；栈水位是否真搬进现场无从确认"
        )
    else:
        for assign in (
            "g_panic_record.stack_free_min",
            "g_panic_record.stack_slot_err",
        ):
            if assign not in panic_text:
                errors.append(
                    f"{fault_rel} 的 osl_panic_report 没有把 {assign} 落盘："
                    "字段声明了但没人往里写，复位后同样读到 0"
                )

    slot_constants: dict[str, int] = {}
    for name, value in re.findall(
        r"#define\s+(OSL_HEALTH_SLOT_\w+)\s+(\d+)U?\b", header
    ):
        slot_constants[name] = int(value)
    if not slot_constants:
        errors.append(
            f"{header_rel} 没有定义任何 OSL_HEALTH_SLOT_* 槽号常量："
            "槽号只能靠裸数字在调用点重复，改槽数时没有单一出处"
        )

    # 槽号与任务身份的绑定。
    # OSL_HEALTH_SLOT_* 所属的任务只写在行内注释里（`/* fml_led_blink_task */`），
    # 而下面的判据只比「槽号集合的基数」：把 fml_led.c 与 fml_button.c 的槽号对调，
    # used 仍是 {0, 1}、基数仍是 2，全绿 —— 而调试器读 g_stack_free_min[0] 会以为
    # 那是 LED 任务，实际是 button，整张表的下标与任务关系错位。
    # 所以把注释里的任务名当契约：调用某个槽号的文件必须真的含那个任务函数。
    # 覆盖不全（某槽号没写任务名）也报错，否则绑定表一删这条就自动失效。
    slot_owner: dict[str, str] = {}
    for comment_match in re.finditer(
        r"#define\s+(OSL_HEALTH_SLOT_\w+)\s+(\d+)U?\s+"
        r"(?:/\*\s*(\w+)\s*\*/|//\s*(\w+))",
        read_text(HEALTH_HEADER),
    ):
        owner = comment_match.group(3) or comment_match.group(4)
        if owner:
            slot_owner[comment_match.group(1)] = owner
    missing_owner = sorted(set(slot_constants) - set(slot_owner))
    if missing_owner:
        errors.append(
            f"{header_rel} 的 {', '.join(missing_owner)} 没有在行内注释里写明所属任务："
            "槽号与任务身份的对应只存在于人的记忆里，对调后没有任何判据看得出来"
        )

    used: set[int] = set()
    for relative in HEALTH_CALL_SOURCES:
        path = ROOT / relative
        text = read_text(path)
        if not text:
            errors.append(f"缺少 {relative}，无法确认该文件里的栈水位槽号")
            continue
        code = _strip_comments_keep_offsets(text)
        for line, args in _call_arguments(code, "osl_health_stack_mark"):
            if len(args) != 1 or not args[0]:
                errors.append(f"{relative}:{line} 的 osl_health_stack_mark 实参个数异常，无法确认槽号")
                continue
            raw = args[0].strip()
            if raw.isdigit() or raw.rstrip("Uu").isdigit():
                errors.append(
                    f"{relative}:{line} 的 osl_health_stack_mark 槽号是裸字面量 `{raw}`："
                    "同一个槽号在头文件、调用点和调试脚本里各写一遍，改槽数时必然漏改一处；"
                    "改用 OSL_HEALTH_SLOT_<用途> 常量"
                )
                slot = int(raw.rstrip("Uu"))
            else:
                slot = slot_constants.get(raw)
                if slot is None:
                    errors.append(
                        f"{relative}:{line} 的 osl_health_stack_mark 槽号写成 `{raw}`，"
                        "既不是数字也不是头文件里定义过的 OSL_HEALTH_SLOT_* 常量"
                    )
                    continue
            if not 0 <= slot < slots:
                errors.append(
                    f"{relative}:{line} 的 osl_health_stack_mark 槽号 {slot} 超出 0..{slots - 1}："
                    "采样会被丢掉，只剩越界计数"
                )
                continue
            owner = slot_owner.get(raw)
            if owner is not None and re.search(rf"\b{owner}\b", code) is None:
                errors.append(
                    f"{relative}:{line} 用 {raw} 采样，但该文件里没有 {owner}："
                    f"{raw} 的注释声明它属于 {owner}，这里的槽号与任务身份对不上 —— "
                    "g_stack_free_min 的下标会和任务错位，而基数判据"
                    "（len(used) < slots）永远看不出来"
                )
            used.add(slot)

    if used and len(used) < slots:
        spare = slots - len(used)
        errors.append(
            f"OSL_HEALTH_STACK_SLOTS = {slots}，但自研层只用了槽 {sorted(used)}；"
            f"多出的 {spare} 槽 × 4 B = {spare * 4} B 永远写不进值"
        )
    return errors
def collect_task_name_len_errors() -> list[str]:
    """configMAX_TASK_NAME_LEN 必须刚好装下实际任务名。

    它是 `TCB_t.pcTaskName[]` 的长度，每个任务一份（IDLE + 2 个应用任务 = 3 处），
    也决定了 `g_panic_record.task_name[]` 的拷贝上限。两个方向都会出问题：

      * 配小了：`prvInitialiseNewTask` 只是把名字截断，`g_panic_record.task_name`
        只剩前缀（`"led_blin"`），而 FreeRTOS 里唯一会 `configASSERT` 的
        `vTaskList` 路径当前根本没开——没有任何东西会报错；
      * 配大了：多出的字节摊到每个 TCB 上，`size` 只报 bss 总量，看不出来。

    "实际任务名"从 `xTaskCreate` 的第 2 个实参取，只接受字符串字面量：名字是运行期
    拼出来的就没法静态算长度，那时宁可报出来让人显式确认。
    """
    errors: list[str] = []
    config_rel = FREERTOS_CONFIG.relative_to(ROOT).as_posix()
    if not FREERTOS_CONFIG.is_file():
        return [f"缺少 {config_rel}，无法确认任务名长度上限"]
    code = _strip_comments_keep_offsets(read_text(FREERTOS_CONFIG))
    match = re.search(r"#define\s+configMAX_TASK_NAME_LEN\s+(\d+)", code)
    if match is None:
        return [f"{config_rel} 里找不到 configMAX_TASK_NAME_LEN，无法确认任务名长度上限"]
    limit = int(match.group(1))

    longest = 0
    longest_name = ""
    for relative in TASK_NAME_SOURCES:
        text = read_text(ROOT / relative)
        if not text:
            errors.append(f"缺少 {relative}，无法确认该文件里的任务名长度")
            continue
        calls = _call_arguments(_strip_comments_keep_offsets(text), "xTaskCreate")
        for line, args in calls:
            if len(args) < 2 or not args[1]:
                errors.append(f"{relative}:{line} 的 xTaskCreate 实参不足，无法确认任务名长度")
                continue
            literal = re.fullmatch(r'"((?:[^"\\]|\\.)*)"', args[1].strip())
            if literal is None:
                errors.append(
                    f"{relative}:{line} 的 xTaskCreate 任务名写成 `{args[1]}`，"
                    "静态算不出长度；用字符串字面量，长度上限才能被守住"
                )
                continue
            length = len(literal.group(1))
            if length > longest:
                longest = length
                longest_name = literal.group(1)

    if longest == 0:
        return errors
    required = longest + 1
    if limit < required:
        errors.append(
            f"{config_rel} 的 configMAX_TASK_NAME_LEN = {limit}，最长任务名有 {longest} 字符；"
            f"至少要 {required}，否则名字被截断，g_panic_record.task_name 只剩前缀"
        )
    elif limit > required + TASK_NAME_SLACK:
        spare = limit - required
        errors.append(
            f"{config_rel} 的 configMAX_TASK_NAME_LEN = {limit}，最长任务名只要 {required}；"
            f"多出的 {spare} B 摊到每个 TCB（IDLE + 2 个应用任务 = 3 处），是看不出来的 RAM 白占"
        )
    return errors
def collect_fault_range_errors() -> list[str]:
    """fault_report 的 SP 合法性范围必须取自链接脚本符号，不能写死地址。

    旧写法把 RAM 判据写死成 `0x20000000..0x20030000`（192 KB）。它与链接脚本是两份
    独立的真相：改 RAM 容量、换芯片、或将来启用 CCMRAM/挪堆以后，判据仍在原地——
    后果是两头的，而且都不报错：

      * 判据比实际 RAM 小：落在高位 RAM 的合法异常帧被判成"栈已崩"，现场直接留空；
      * 判据比实际 RAM 大：落在越界地址的 SP 被当成合法去读，读本身就是二次 fault。

    `_sdata` / `_estack` 由链接脚本给出（RAM 起点 / RAM 顶），判据跟着布局走。
    """
    errors: list[str] = []
    relative = FAULT_SOURCE.relative_to(ROOT).as_posix()
    if not FAULT_SOURCE.is_file():
        return [f"缺少 {relative}，无法确认异常帧的 SP 合法性判据"]
    body = _function_body(read_text(FAULT_SOURCE), r"void\s+fault_report\s*\(")
    if body is None:
        return [f"{relative} 里找不到 fault_report 的函数体，无法确认 SP 判据"]
    literals = sorted(set(FAULT_RANGE_LITERAL.findall(body)))
    if literals:
        errors.append(
            f"{relative} 的 fault_report 把 RAM 地址写死成 {', '.join(literals)}；"
            "应改用 _sdata / _estack，否则链接脚本一改判据就与布局脱钩"
        )
    for symbol in ("_estack", "_sdata"):
        if symbol not in body:
            errors.append(
                f"{relative} 的 fault_report 没有引用 {symbol}："
                "SP 合法性判据不是来自链接脚本，改内存布局后静默失效"
            )
    return errors
def collect_isr_priority_errors() -> list[str]:
    """中断里调用 FreeRTOS 的 ISR 变体时，NVIC 抢占优先级必须能进临界区。

    FreeRTOS 的临界区靠 `BASEPRI = configMAX_SYSCALL_INTERRUPT_PRIORITY` 屏蔽中断，
    抢占优先级数值**小于**它的中断根本不受屏蔽。在这样的中断里调 `*FromISR`，
    对内核链表/延迟列表的修改会和任务侧的临界区互相撕扯——而且功能上看不出任何异常，
    只在偶发的链表损坏或丢唤醒时暴露。

    现状（已核查，不是缺陷）：自研层一个 `*FromISR` 调用都没有，唯一的周期中断由
    `port.c` 的 `xPortStartScheduler()` 把 SysTick/PendSV 设成 0xFF（最低），
    SVCall 设成 0（正常）；`vPortValidateInterruptPriority()` 因此没有调用方，
    `--gc-sections` 把它丢掉是正确结果，不是缺口。

    这条断言的作用是防回归：一旦有人开始用，就必须在同一文件里给出该中断的
    `HAL_NVIC_SetPriority` 且数值 ≥ `configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY`。
    它先剥注释再匹配。此前只扫原文时实测出两种错：注释里写一句「不要在这里调
    `xQueueReceiveFromISR`」就会让 verify 误报「没有 SetPriority」（EXIT=1）；反过来，
    把越界调用整段注释掉、只在注释里留文本，又能让真实风险看不出。
    """
    errors: list[str] = []
    config = ROOT / "osl" / "inc" / "FreeRTOSConfig.h"
    if not config.is_file():
        return ["缺少 osl/inc/FreeRTOSConfig.h，无法确认中断优先级约定"]
    config_text = read_text(config)
    if not re.search(r"#define\s+configASSERT\s*\(", config_text):
        errors.append(
            "FreeRTOSConfig.h 没有定义 configASSERT：vPortValidateInterruptPriority() 不会被编进来，"
            "在过高的优先级上调 ISR 版 API 不会当场失败（只会偶发内核结构损坏）"
        )
    limit = 5
    syscall = re.search(
        r"#define\s+configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY\s+([0-9]+|0x[0-9A-Fa-f]+)",
        config_text,
    )
    if syscall is None:
        errors.append(
            "FreeRTOSConfig.h 里没有 configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY 的数值定义，"
            "无法判断哪个优先级的中断还能进临界区"
        )
    else:
        limit = int(syscall.group(1), 0)
    for layer in ("apl", "fml", "bsl", "osl", "hal"):
        for path in sorted((ROOT / layer).rglob("*.c")):
            if "freertos" in path.parts:
                continue  # 内核自身的 *FromISR 由 port.c 的校验负责
            body = _strip_comments_keep_offsets(read_text(path))
            hits = sorted(set(RTOS_ISR_API.findall(body)))
            if not hits:
                continue
            rel = path.relative_to(ROOT).as_posix()
            priorities = [int(value, 0) for value in NVIC_PRIORITY_CALL.findall(body)]
            if not priorities:
                errors.append(
                    f"{rel} 在中断里调用了 {', '.join(hits)}，但文件里没有 HAL_NVIC_SetPriority(...)："
                    f"无法证明该中断被临界区（BASEPRI = {limit}）屏蔽；"
                    "ISR 版 API 只能在抢占优先级数值 ≥ 该值的中断里使用"
                )
                continue
            if min(priorities) < limit:
                errors.append(
                    f"{rel} 在中断里调用了 {', '.join(hits)}，但该文件设置的最紧急优先级是 "
                    f"{min(priorities)}，高于 configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY（{limit}）："
                    "这样的中断不受 taskENTER_CRITICAL 屏蔽，与内核共享的链表会被并发修改"
                )
    return errors
def _function_body(text: str, signature: str) -> str | None:
    """取 C 函数体：从签名后的第一个 '{' 起按花括号配平到对应的 '}'。

    先剥注释再配平——注释里出现的花括号会让配平提前或延后结束。块注释和行注释都剥：
    只剥块注释时，函数体里写一行 `// g_panic_reporting` 就能让下面的断言假绿。

    签名可能先命中**前向声明**（`void f(void);`）。那种匹配后面没有 '{'，
    若直接 `find("{")` 就会落到下一个函数的函数体上 —— 断言于是检查了一个完全
    无关的函数，表现为「源码改了它不叫、没改倒叫」。所以逐个试匹配，
    紧跟的非空字符不是 '{' 就换下一个候选。实测 fault_monitor_init 有此声明，
    取到的曾是 hal_time_base_init 的函数体。
    """
    code = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
    code = re.sub(r"//.*", "", code)
    search_from = 0
    while True:
        match = re.search(signature, code[search_from:])
        if match is None:
            return None
        after = search_from + match.end()
        # 签名可能已经包含参数列表（`\(void\)`），也可能止于 '(' —— 正则原文以 ')'
        # 结尾说明参数已配平完毕，不要再往里扫一层括号。
        paren = 0 if signature.rstrip().endswith(")") else 1
        pos = after
        while pos < len(code) and paren > 0:
            if code[pos] == "(":
                paren += 1
            elif code[pos] == ")":
                paren -= 1
            pos += 1
        while pos < len(code) and code[pos].isspace():
            pos += 1
        if pos < len(code) and code[pos] == "{":
            start = pos
            break
        search_from = after
    depth = 0
    for index in range(start, len(code)):
        char = code[index]
        if char == "{":
            depth += 1
        elif char == "}":
            depth -= 1
            if depth == 0:
                return code[start : index + 1]
    return None
def collect_fault_errors() -> list[str]:
    """硬件异常必须由本工程接管；未实现的中断也必须留下证据。

    startup_stm32f429xx.s 把四个异常入口都 .weak 别名到 Default_Handler，同时还有
    90 个 .thumb_set …IRQHandler,Default_Handler。历史上 Default_Handler 是
    `Infinite_Loop: b Infinite_Loop`：真出现空指针 / 总线错误 / 除零、或某个没实现的
    中断被触发，板子只会安静停机，g_panic_record 里一个字都不会留下。

    静态部分断言三件事：
      1) 源码里四个入口是强定义；
      2) startup 仍保持弱别名（覆盖关系是有意设计，不是巧合）；
      3) startup 的 Default_Handler 跳到 C 侧的 UnhandledIRQ_Handler，且不再有
         `b Infinite_Loop`——这条改回去比硬件异常本身更容易发生，因为它是启动文件。
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
                "它会落到 startup 的 Default_Handler，故障现场不留任何证据"
            )
    unhandled_body = _function_body(text, r"void\s+UnhandledIRQ_Handler\s*\(void\)")
    if unhandled_body is None:
        errors.append(
            f"{FAULT_SOURCE.relative_to(ROOT).as_posix()} 没有定义 UnhandledIRQ_Handler；"
            "90 个未实现的中断会重新落回 Default_Handler 的自旋"
        )
    elif "NVIC->ICER" in unhandled_body or "NVIC->ICPR" in unhandled_body:
        # 「关掉引发本次异常的那个中断源」是这一步的唯一目的，而它的索引算错过一次：
        # ICSR.VECTACTIVE 是**异常号**（16..105 才是外部 IRQ），而 NVIC->ICER[n] 的 n
        # 是 IRQn >> 5（IRQn = 异常号 - 16）。少减 16 就整体偏了 16 位 —— 关掉的是另一个
        # 源的使能位，真正在拉线的那个仍然开着。它现在被 panic_mask_all_irqs() 的全屏蔽
        # 兜着，所以跑起来看不出问题；一旦有人把全屏蔽改成选择性屏蔽，这段就是唯一防线。
        # 参照 core_cm4.h 里 NVIC_DisableIRQ 的写法：索引取自 IRQn，不是异常号。
        if not re.search(r"irq\s*-\s*16", unhandled_body):
            errors.append(
                f"{FAULT_SOURCE.relative_to(ROOT).as_posix()} 的 UnhandledIRQ_Handler "
                "没有把异常号换算成 IRQn（irq - 16）就索引 NVIC->ICER/ICPR："
                "VECTACTIVE 是异常号，ICER 的索引是 IRQn，少减 16 会关掉另一个中断源的使能位"
            )
        if re.search(r"\[[^\]]*\birq\s*>>", unhandled_body):
            errors.append(
                f"{FAULT_SOURCE.relative_to(ROOT).as_posix()} 的 UnhandledIRQ_Handler "
                "用 `irq >> 5` 直接当 ICER/ICPR 的索引：索引必须由 IRQn 算出，不能直接用异常号"
            )
    startups = sorted((ROOT / "hal").rglob("startup_*.s"))
    if not startups:
        errors.append("未找到 hal 下的 startup_*.s，无法确认异常入口的弱别名")
    else:
        asm = read_text(startups[0])
        rel = startups[0].relative_to(ROOT).as_posix()
        # 只匹配真正的指令：注释里提到 "b Infinite_Loop / b UnhandledIRQ_Handler"
        # 是在解释历史，不能算数（否则文档被自己的说明绊倒）。
        code = re.sub(r"/\*.*?\*/", "", asm, flags=re.S)
        code = re.sub(r"@.*", "", code)
        for name in FAULT_HANDLER_NAMES:
            if not re.search(rf"\.thumb_set\s+{name}\s*,\s*Default_Handler", asm):
                errors.append(
                    f"{rel} 里 {name} 不再弱别名到 Default_Handler；"
                    "启动文件被改过，请重新确认异常覆盖关系"
                )
        if not re.search(r"^\s*b\s+UnhandledIRQ_Handler\b", code, re.M):
            errors.append(
                f"{rel} 的 Default_Handler 没有跳向 UnhandledIRQ_Handler；"
                "未实现的中断会静默自旋，和硬故障无法区分"
            )
        if re.search(r"^\s*b\s+Infinite_Loop\b", code, re.M):
            errors.append(
                f"{rel} 里仍有 `b Infinite_Loop` 自旋；Default_Handler 必须转发到 "
                "UnhandledIRQ_Handler（记录中断号并播报 code 6）"
            )

    # fault_monitor_init 必须打开三个可配置异常。
    # 复位后 SCB->SHCSR 里 MEMFAULTENA / BUSFAULTENA / USGFAULTENA 全是 0，
    # 所以 MemManage / BusFault / UsageFault 默认**全部升级为 HardFault**：
    # fault_report 里按「落在哪个入口」判定的 fault_kind 就恒为 1，
    # g_panic_record 承诺的「四类分开」完全不成立，CFSR/HFSR 里那些位也没人去读。
    # 删掉那两行照样编得过、链得过、尺寸不变，所以这里守三个使能位都在。
    monitor_body = _function_body(text, r"void\s+osl_fault_monitor_init\s*\(")
    if monitor_body is None:
        errors.append(
            f"{FAULT_SOURCE.relative_to(ROOT).as_posix()} 里找不到 osl_fault_monitor_init；"
            "三类可配置 fault 是否与 HardFault 分开无从确认"
        )
    else:
        for mask_name in (
            "SCB_SHCSR_MEMFAULTENA_Msk",
            "SCB_SHCSR_BUSFAULTENA_Msk",
            "SCB_SHCSR_USGFAULTENA_Msk",
        ):
            if mask_name not in monitor_body:
                errors.append(
                    f"{FAULT_SOURCE.relative_to(ROOT).as_posix()} 的 osl_fault_monitor_init "
                    f"没有置 {mask_name}：该位默认 0，对应的 fault 会升级成 HardFault，"
                    "g_panic_record.fault_kind 恒为 1，现场里「是哪种 fault」再也分不出来"
                )

    # panic 通道自己必须不可重入。写 record 与 0.5 s 三灯播报期间，0..4 优先级的中断
    # 不受 BASEPRI 约束（taskDISABLE_INTERRUPTS 只把 BASEPRI 提到 0x50，而且它在播报
    # 之后才调），第二路 panic 会与第一路交错写同一个 g_panic_record，把现场撕成
    # 一半旧一半新，栈也继续往 __heap_end__ 压。这里只断言"入口是否全局屏蔽 + 是否有
    # 重入闸"，不试图证明运行时行为——那需要真机。
    panic_rel = FAULT_SOURCE.relative_to(ROOT).as_posix()
    panic_body = _function_body(text, r"void\s+osl_panic_report\s*\(")
    mask_body = _function_body(text, r"void\s+panic_mask_all_irqs\s*\(")
    if panic_body is None:
        errors.append(f"{panic_rel} 里找不到 osl_panic_report 的函数体；无法确认 panic 通道是否防重入")
    else:
        if "g_panic_reporting" not in panic_body:
            errors.append(
                f"{panic_rel} 的 osl_panic_report 没有 g_panic_reporting 重入闸："
                "两路 panic 同时进来会交错写 g_panic_record，现场字段一半新一半旧"
            )
        if "panic_mask_all_irqs(" not in panic_body:
            errors.append(
                f"{panic_rel} 的 osl_panic_report 没有先调 panic_mask_all_irqs()："
                "写 record 与播报期间，优先级 0..4 的未实现中断仍可重入"
            )
    if mask_body is None:
        errors.append(f"{panic_rel} 里没有 panic_mask_all_irqs 函数；无法确认是否关掉全部外部中断")
    elif "NVIC->ICER" not in mask_body or "NVIC->ICPR" not in mask_body:
        errors.append(
            f"{panic_rel} 的 panic_mask_all_irqs 没有同时清 NVIC->ICER 与 ICPR："
            "只关使能会留下挂起位，重入路径仍未完全关闭"
        )
    if mask_body is not None:
        # 系统异常（SysTick / PendSV / SVC 都在 16 号以下）不在 NVIC->ICER 的管辖里：
        # 它们的使能位在 SCB 层。只清 ICER 时，从任务上下文进来的 panic（malloc 失败
        # 钩子、任务里的 configASSERT）在 1.2–1.5 s 的播报窗口里仍会被 SysTick 的
        # 1 kHz 时间片切走，被切到的任务继续写灯，把三灯播报的码覆盖掉——现场读到的
        # 闪烁次数与 code 不符，比不播报更误导。所以要再加一层 BASEPRI，并清掉
        # SysTick / PendSV 已经挂起的那次切换。
        baspri = re.search(r"__set_BASEPRI\s*\(([^)]*)\)", mask_body)
        if baspri is None:
            errors.append(
                f"{panic_rel} 的 panic_mask_all_irqs 没有设 BASEPRI："
                "SysTick/PendSV 不是外部 IRQ，ICER 关不掉它们，播报窗口里仍会切任务"
            )
        elif baspri.group(1).strip() != "configMAX_SYSCALL_INTERRUPT_PRIORITY":
            actual = baspri.group(1).strip()
            errors.append(
                f"{panic_rel} 的 panic_mask_all_irqs 把 BASEPRI 写成 `{actual}`；"
                "必须是 configMAX_SYSCALL_INTERRUPT_PRIORITY（0 等于不屏蔽，与没写一样）"
            )
        if "PENDSVCLR_Msk" not in mask_body or "PENDSTCLR_Msk" not in mask_body:
            errors.append(
                f"{panic_rel} 的 panic_mask_all_irqs 没有清 SysTick/PendSV 的挂起位："
                "已经挂起的那次切换会在播报过程中补上，同样把灯态打乱"
            )
        # ICER/ICPR 是 NVIC 的 8 个 32 位字，但 F429 只实现了 90 个外部 IRQ
        # （(90 + 31) / 32 = 3 个字）。系统异常不在 ICER 里，把它也算进上界会多写
        # 一个字，那是保留位：不报错、不生效，同时把"真实上限"藏在一个更大的数字里。
        if "PANIC_NVIC_WORDS" not in mask_body:
            errors.append(
                f"{panic_rel} 的 panic_mask_all_irqs 用裸数字当 ICER/ICPR 循环上界："
                "F429 只有 90 个外部 IRQ（3 个字），多写的保留字既不生效也掩盖了真实上限；"
                "改用 PANIC_NVIC_WORDS（= (90 + 31) / 32）"
            )

        # 引用存在还不够 —— 上界的「值」必须只按 90 个外部 IRQ 算出来。
        # 历史写法是 `(((16U) + (90U) + 31U) / 32U)`：把系统异常也加了进去，实算 4 个字，
        # 而注释与规格都写「3 个字」。多出来的那个字是保留位：不报错、不生效，
        # 同时让「真实上限」藏在一个更大的数字里 —— 只查符号名永远看不出来。
        words_line = re.search(r"^#define\s+PANIC_NVIC_WORDS.*$", text, re.M)
        exti_line = re.search(r"^#define\s+PANIC_EXTI_IRQ_COUNT\s+(\d+)U", text, re.M)
        if words_line is None:
            errors.append(
                f"{panic_rel} 里没有 #define PANIC_NVIC_WORDS；"
                "ICER/ICPR 的循环上界又退回裸数字"
            )
        elif exti_line is None:
            errors.append(
                f"{panic_rel} 里没有 #define PANIC_EXTI_IRQ_COUNT；上界算式的来源无从核对"
            )
        else:
            expr = words_line.group(0)
            count = int(exti_line.group(1))
            if "PANIC_EXTI_IRQ_COUNT" not in expr:
                errors.append(
                    f"{panic_rel} 的 PANIC_NVIC_WORDS 不是按 PANIC_EXTI_IRQ_COUNT 展开的："
                    f"`{expr.strip()}` —— 上界与 IRQ 数量脱钩"
                )
            if re.search(r"\b16(?:U|UL)?\b", expr):
                errors.append(
                    f"{panic_rel} 的 PANIC_NVIC_WORDS 把系统异常（16）也算进了上界："
                    f"`{expr.strip()}` —— 系统异常不在 NVIC->ICER 里，"
                    "多算出来的是保留字，既不生效又让实际值与注释不符"
                )
            if (count + 31) // 32 != 3:
                errors.append(
                    f"{panic_rel} 的 PANIC_EXTI_IRQ_COUNT = {count}，"
                    f"按 (count + 31) / 32 得 {(count + 31) // 32} 个字，"
                    "而 F429 只实现 90 个外部 IRQ（= 3 个字）："
                    "多写的保留字不生效，少写的会漏关中断源"
                )

    # panic 期间蜂鸣器必须静音。按键任务按住不放时 board_beep_set(1) 已经把 PI11 拉高，
    # panic 播报要持续到 IWDG 复位（relax 后 32.7 s），这期间板子会一直响：
    # 一个"故障信号"被蜂鸣器盖过去，而且没人关得掉它（中断已屏蔽、任务已停）。
    buzz_body = _function_body(text, r"static\s+void\s+panic_leds\s*\(")
    if buzz_body is None:
        errors.append(f"{panic_rel} 里找不到 panic_leds；无法确认 panic 期间是否让蜂鸣器静音")
    elif "GPIOI->BSRR" not in buzz_body:
        errors.append(
            f"{panic_rel} 的 panic_leds 没有把蜂鸣器（PI11）拉低："
            "panic 时要一直响到 IWDG 复位，故障信号被持续蜂鸣盖住"
        )

    # 关中断必须早于写 record，而且要落在**每个入口**里。只把屏蔽放进 osl_panic_report
    # 是不够的：各入口自己会先写 magic / fault_pc / task_name，那一段仍然在 0..4 优先级
    # 中断可打断的窗口里，两路 panic 交错写同一份现场，字段就会一半旧一半新。
    for entry, signature in PANIC_ENTRY_POINTS:
        entry_text = (
            read_text(OSL_PORT_ST_SOURCE) if entry == "_sbrk" else text
        )
        entry_rel = (
            OSL_PORT_ST_SOURCE.relative_to(ROOT).as_posix()
            if entry == "_sbrk"
            else panic_rel
        )
        body = _function_body(entry_text, signature)
        if body is None:
            errors.append(f"{entry_rel} 里找不到 {entry}；无法确认该 panic 入口是否先关中断")
            continue
        masked = body.find("panic_mask_all_irqs(")
        writes = [pos for pos in (body.find("g_panic_record"), body.find("g_last_write")) if pos >= 0]
        if writes and (masked < 0 or masked > min(writes)):
            errors.append(
                f"{entry_rel} 的 {entry} 在写 record 之前没有关中断（panic_mask_all_irqs 缺失或晚于首次写入）："
                "预写窗口里优先级 0..4 的中断重入会交错改写同一份现场"
            )
    return errors
def collect_fault_artifact_errors() -> list[str]:
    """链接之后：四个异常入口与 UnhandledIRQ_Handler 必须真的进镜像。

    与静态检查分开的原因同 collect_heap_artifact_errors()：update 里的自检发生在
    make 之前，产物还是上一轮的。这里能抓到"源码写了、但没被链接进来"的情形
    （例如将来把实现挪出 freertos_port_st.o，那个成员就不再被无条件拉入）。

    Default_Handler 额外用 objdump 直接看那条指令：它必须跳到 UnhandledIRQ_Handler。
    只看符号地址不够——符号在、但指令改回自旋，仍然是一次静默死机。
    """
    errors: list[str] = []
    nm = tool_binary("nm")
    elfs = sorted(OUT_ARTIFACT_DIR.glob("*.elf"))
    if nm is None or not elfs:
        return errors
    objdump = tool_binary("objdump")
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
                    "异常会落到 startup 的 Default_Handler"
                )
            elif default_handler is not None and entry[0] == default_handler[0]:
                errors.append(
                    f"产物 {elf.name} 里 {name} 与 Default_Handler 同地址 "
                    f"0x{int(entry[0], 16):08x}，说明强定义没生效"
                )
        unhandled = table.get("UnhandledIRQ_Handler")
        if unhandled is None or unhandled[1] != "T":
            errors.append(
                f"产物 {elf.name} 里没有 UnhandledIRQ_Handler；"
                "Default_Handler 的跳转目标没进镜像（会被 --gc-sections 收走）"
            )
        elif default_handler is not None and objdump is not None:
            start = int(default_handler[0], 16)
            dis = run_tool_args(
                objdump,
                "-d",
                f"--start-address=0x{start:08x}",
                f"--stop-address=0x{start + 4:08x}",
                str(elf),
            )
            if "UnhandledIRQ_Handler" not in dis:
                errors.append(
                    f"产物 {elf.name} 里 Default_Handler(0x{start:08x}) 的第一条指令"
                    "不是跳到 UnhandledIRQ_Handler"
                )
    return errors
def collect_observ_artifact_errors() -> list[str]:
    """tick hook 里的三件事必须在指令流里看得见，而不只是源码文本里出现。

    源码级断言（collect_observability_errors）只做子串包含。实测把
    `stack_guard_check();` 包进 `if (0U) { stack_guard_check(); }`——符号还在、调用
    永不执行——verify 仍打印"自检通过"。删掉整行会被抓到，但"保留符号、破坏语义"
    抓不到，而这正是行为被悄悄回退时最可能的形态（重构、条件编译、加调试开关）。

    所以这里改看反汇编：vApplicationTickHook 里必须真的有 bl 到
    stack_guard_check / osl_watchdog_feed / xPortGetMinimumEverFreeHeapSize。
    三条通道任一被包进永不执行的分支，对应的 bl 就从指令流里消失，这条立刻变红。

    实测 -O2 没有把 stack_guard_check 内联进 tick hook（它内部调 noreturn 的
    osl_panic_report，GCC 保留为独立函数），所以按符号名断言是稳定的。若将来它被
    内联，这条会报"缺少 bl"——那时把断言改成"体内必须有 bl osl_panic_report"
    （栈越界时唯一的下游），不要把这条删掉。
    """
    errors: list[str] = []
    objdump = tool_binary("objdump")
    if objdump is None:
        return ["找不到 arm-none-eabi-objdump，无法校验 tick hook 的三条通道"]
    elfs = sorted(OUT_ARTIFACT_DIR.glob("*.elf"))
    if not elfs:
        return errors
    required = (
        ("stack_guard_check", "主栈守卫（MSP 越界时没有人再报警）"),
        ("osl_watchdog_feed", "喂狗（看门狗会在 1 s 后复位一块运行正常的板子）"),
        (
            "xPortGetMinimumEverFreeHeapSize",
            "堆水位采样（g_heap_free_min 只在 panic 时才写，正常运行时读不到堆余量）",
        ),
    )
    for elf in elfs:
        dump = run_tool_args(
            objdump, "-d", "--disassemble=vApplicationTickHook", elf.as_posix()
        )
        if "vApplicationTickHook>:" not in dump:
            errors.append(
                f"产物 {elf.name} 里找不到 vApplicationTickHook 的函数体；"
                "tick hook 被 --gc-sections 丢掉了（configUSE_TICK_HOOK 是否为 1？）"
            )
            continue
        for symbol, reason in required:
            if f"<{symbol}>" not in dump:
                errors.append(
                    f"产物 {elf.name} 的 vApplicationTickHook 指令流里没有 bl {symbol}；{reason}"
                )
    return errors
def _disasm_lines(dump: str) -> list[tuple[int, str, str]]:
    """从 objdump -d 的文本里取出 (地址, 助记符, 操作数)。

    指令字节数不同会让 hex 字段宽度不齐（2 字节与 4 字节混排），所以按制表符切，
    不按列切。
    """
    out: list[tuple[int, str, str]] = []
    for line in dump.splitlines():
        parts = line.strip().split("\t")
        if len(parts) < 4 or not parts[0].endswith(":"):
            continue
        try:
            addr = int(parts[0].rstrip(":"), 16)
        except ValueError:
            continue
        out.append((addr, parts[2].strip(), parts[3].split("@")[0].strip()))
    return out
def _branch_target(operands: str) -> int | None:
    """取分支目标地址。

    `beq.n 8000a40 <sym>` 的目标是第一项，而 `cbnz r3, 8000a52 <sym>` 的目标在
    第二项 —— 所以按 token 逐个试，取第一个能解析成十六进制地址的。只对条件分支
    调用，操作数里不会出现 #4 这类立即数。
    """
    for token in operands.replace(",", " ").split():
        try:
            return int(token, 16)
        except ValueError:
            continue
    return None
def collect_boot_init_errors() -> list[str]:
    """挂在 HAL_InitTick 上的两件"只应做一次"的初始化，必须有幂等守卫。

    HAL 的契约是"每次改时钟都重装时基"：HAL_Init() 调一次 HAL_InitTick，
    HAL_RCC_ClockConfig() 在函数末尾再调一次。本工程用强定义覆盖 HAL_InitTick，
    里面做三件事，其中两件不能做第二次：

      1) osl_reset_cause_capture() 第一次读 RCC->CSR 后置 RMVF 清标志。第二次进来
         读到的 CSR 已是全零，g_reset_cause 被覆写成 OSL_RESET_UNKNOWN、g_reset_csr
         被覆写成 0 —— 而 osl_panic_report() 落盘的就是这两个值。后果：时钟切换
         之后的所有 panic（栈溢出/assert/硬件 fault/malloc 失败/未实现中断/堆耗尽）
         在 record 里的 reset_cause 全是假的。

      2) hal_time_base_init() 里的 DWT->CYCCNT = 0 会把毫秒原点往后推。时钟切换时
         再归零一次，HAL_GetTick() 就不再单调。

    两条都要有"已经做过就跳过"的出口。这里不查源码文本、不查符号，直接看指令流：
    源码写了 if 却被优化成无条件执行（或守卫漏了 return），反汇编里看得见。
    """
    errors: list[str] = []

    # ---- 0a. board_system_init 必须检查 HAL_Init 的返回值 ----
    # HAL_Init() 返回 HAL_StatusTypeDef，NVIC 优先级分组设置失败时是 HAL_ERROR。
    # 这不是理论问题：FreeRTOS 的优先级屏蔽全依赖 4 位分组（configPRIO_BITS），
    # 分组没设成 NVIC_PRIORITYGROUP_4 时 BASEPRI 比较的是错的位域，屏蔽范围整体错位。
    # 而同一次启动里 SystemClock_Config() 失败会 return 1 让 main 播报码 4 ——
    # 两种失败处理不一致，且 HAL_Init 这条是静默的（没有 panic、没有码）。
    system_rel = "bsl/src/board_system.c"
    system_body = _function_body(
        read_text(ROOT / "bsl" / "src" / "board_system.c"),
        r"int\s+board_system_init\s*\(",
    )
    if system_body is None:
        errors.append(
            f"{system_rel} 里找不到 board_system_init；启动失败是否可见无从确认"
        )
    elif re.search(r"HAL_Init\s*\(\s*\)\s*;", system_body):
        errors.append(
            f"{system_rel} 的 board_system_init 把 HAL_Init() 的返回值丢掉了："
            "NVIC 优先级分组设置失败时它返回 HAL_ERROR，而 FreeRTOS 的 BASEPRI "
            "屏蔽全靠 4 位分组 —— 分组错了整体错位且全程静默；"
            "同一函数里 SystemClock_Config 失败却会 return 1 播报码 4"
        )

    # ---- 0. IWDG 超时档位的取值（静态，不依赖产物） ----
    # iwdg-str 只看 iwdg_wait_sync 里有一条 str（失败分支存在），完全不看 PR/RLR
    # 被设成了多少。relax() 只有一个用途：把超时从 1.0 s 拉到 32.7 s，好让 panic 的
    # 4 轮三灯播报（≈1.2–1.5 s）跑完。把 IWDG_PR_DIV256 误写成 IWDG_PR_DIV16
    # （从 start() 复制那片代码时最容易犯）之后：名字没变、失败分支还在、iwdg-str
    # 照样绿，而超时仍是 1.0 s —— 播报被复位打断，三灯闪不完，record 也来不及写全。
    # 所以这里同时守「函数用了哪个常量」和「那个常量到底是多少」。
    boot_c = read_text(ROOT / "osl" / "src" / "osl_boot.c")

    def _ul_define(text: str, name: str) -> int | None:
        found = re.search(rf"#define\s+{name}\s+(\d+)UL", text)
        return int(found.group(1)) if found else None

    pr16 = _ul_define(boot_c, "IWDG_PR_DIV16")
    pr256 = _ul_define(boot_c, "IWDG_PR_DIV256")
    rlr_normal = _ul_define(boot_c, "IWDG_RLR_NORMAL")
    rlr_relaxed = _ul_define(boot_c, "IWDG_RLR_RELAXED")
    # IWDG PR 编码：0=/4, 1=/8, 2=/16, 3=/32, 4=/64, 5=/128, 6=/256, 7=/512
    for define_name, actual, want, meaning in (
        ("IWDG_PR_DIV16", pr16, 2, "PR=2 即 LSI/16（4 * 2^2 = 16）"),
        ("IWDG_PR_DIV256", pr256, 6, "PR=6 即 LSI/256（4 * 2^6 = 256）"),
        ("IWDG_RLR_RELAXED", rlr_relaxed, 4095, "12 位重装值取满"),
    ):
        if actual is None:
            errors.append(f"osl/src/osl_boot.c 里找不到 {define_name} 的十进制定义")
        elif actual != want:
            errors.append(
                f"osl/src/osl_boot.c 的 {define_name} = {actual}，但 {meaning} 要求 {want}："
                "改错这一位会让看门狗超时偏离设计值，而失败分支与命名检查都照样绿"
            )
    if rlr_normal is None:
        errors.append("osl/src/osl_boot.c 里找不到 IWDG_RLR_NORMAL 的十进制定义")
    elif rlr_relaxed is not None and rlr_normal >= rlr_relaxed:
        errors.append(
            f"osl/src/osl_boot.c 的 IWDG_RLR_NORMAL = {rlr_normal} 不小于 "
            f"IWDG_RLR_RELAXED = {rlr_relaxed}：正常档与播报档反了，"
            "panic 播报时反而更容易被复位"
        )
    # 超时 = 4 * 2^PR * (RLR + 1) / LSI(32000)。只比命名不看值的话，把 PR 编码写错
    # （例如 DIV256 写成 2）仍会通过，所以这里真算一遍秒数。
    if pr16 is not None and rlr_normal is not None:
        normal_s = 4 * (2 ** pr16) * (rlr_normal + 1) / 32000.0
        if normal_s > 2.0:
            errors.append(
                f"按 IWDG_PR_DIV16 + IWDG_RLR_NORMAL 算出的正常超时是 {normal_s:.2f} s，"
                "超过 2 s：tick hook 每 1 ms 喂一次狗，正常档留这么长余量没有意义，"
                "真死机时反而发现得更晚"
            )
    if pr256 is not None and rlr_relaxed is not None:
        relaxed_s = 4 * (2 ** pr256) * (rlr_relaxed + 1) / 32000.0
        if relaxed_s < 20.0:
            errors.append(
                f"按 IWDG_PR_DIV256 + IWDG_RLR_RELAXED 算出的播报超时只有 {relaxed_s:.2f} s："
                "panic 播报要 4 轮 × 3 位 ≈ 1.2–1.5 s，余量不足会在播报中途被 IWDG 复位"
            )
    # 函数体：start 用正常档，relax 必须用播报档，且两处不能互相串档。
    for label, ok_pr, ok_rlr, bad_pr, bad_rlr in (
        ("osl_watchdog_start", "IWDG_PR_DIV16", "IWDG_RLR_NORMAL",
         "IWDG_PR_DIV256", "IWDG_RLR_RELAXED"),
        ("osl_watchdog_relax", "IWDG_PR_DIV256", "IWDG_RLR_RELAXED",
         "IWDG_PR_DIV16", "IWDG_RLR_NORMAL"),
    ):
        body = _function_body(boot_c, rf"void\s+{label}\s*\(")
        if body is None:
            errors.append(f"osl/src/osl_boot.c 里找不到 {label}；无法核对看门狗超时档位")
            continue
        if ok_pr not in body or ok_rlr not in body:
            errors.append(
                f"osl/src/osl_boot.c 的 {label} 没有同时写 {ok_pr} 与 {ok_rlr}："
                "分频或重装值缺一个，超时就不是设计值"
            )
        if bad_pr in body or bad_rlr in body:
            errors.append(
                f"osl/src/osl_boot.c 的 {label} 里出现了另一档的 {bad_pr} / {bad_rlr}："
                "两档串了，正常档与播报档会退化成同一个超时"
            )

    # ---- 0b. relax 必须真的把 IWDG 的计数器打开 ----
    # 只写 PR / RLR / 0xAAAA 的 relax 对「运行期 panic」是对的：那时 tick hook 早已
    # 通过 osl_watchdog_start() 写过 0xCCCC。但启动期的 panic 根本不经过 tick hook
    # —— 码 1（vTaskStartScheduler 返回）、码 2（启动期 xTaskCreate 失败）、码 4
    # （时钟配置失败）发生时 IWDG 从未 ENABLE，PR / RLR 只是预置寄存器，计数器不跑。
    # 播报结束进入 taskDISABLE_INTERRUPTS() + 自旋之后，超时永远不会到 ——
    # 现场从此只能断电，跨复位的 g_panic_record 也不会产生第二代做对比。
    # 0xCCCC 是幂等的（已启动再写不重启计数器、不改超时），运行期重复调用无副作用。
    relax_body = _function_body(boot_c, r"void\s+osl_watchdog_relax\s*\(")
    if relax_body is None:
        errors.append(
            "osl/src/osl_boot.c 里找不到 osl_watchdog_relax；"
            "启动期 panic 之后能不能自己复位无从确认"
        )
    elif "IWDG_KEY_ENABLE" not in relax_body:
        errors.append(
            "osl/src/osl_boot.c 的 osl_watchdog_relax 没有写 IWDG_KEY_ENABLE（0xCCCC）："
            "看门狗只在 vApplicationTickHook 首次进入时启动，启动期 panic（码 1 调度器"
            "起不来 / 码 2 启动期建任务失败 / 码 4 时钟失败）发生时它从未 ENABLE，"
            "relax 只预置 PR/RLR 不启动计数 —— 播报完进关中断自旋，永不复位，只能断电"
        )

    objdump = tool_binary("objdump")
    if objdump is None:
        return ["找不到 arm-none-eabi-objdump，无法校验 HAL_InitTick 的幂等守卫"]
    elfs = sorted(OUT_ARTIFACT_DIR.glob("*.elf"))
    if not elfs:
        return errors

    for elf in elfs:
        # ---- A. osl_reset_cause_capture 必须有一条提前返回 ----
        dump = run_tool_args(
            objdump, "-d", "--disassemble=osl_reset_cause_capture", elf.as_posix()
        )
        if "osl_reset_cause_capture>:" not in dump:
            errors.append(
                f"产物 {elf.name} 里找不到 osl_reset_cause_capture 的函数体；"
                "复位原因采集没进镜像"
            )
        else:
            # 不能 `continue`：那会把同一个 elf 的 B 段（hal_time_base_init）与后续
            # 判据一起跳过，等于发布档位下整条 elf 免检。
            skipped = _skip_on_release_build(
                "osl_reset_cause_capture 的条件早退（-Os 会把它折成直线，早退分支消失）"
            )
            insns = [] if skipped else _disasm_lines(dump)
            lr_addrs = {a for (a, m, o) in insns if m == "bx" and o == "lr"}
            early_exit = any(
                COND_BRANCH_RE.match(m) and _branch_target(o) in lr_addrs
                for (_a, m, o) in insns
            )
            if not skipped and not early_exit:
                errors.append(
                    f"产物 {elf.name} 里 osl_reset_cause_capture 缺少条件早退："
                    "HAL_InitTick 会被调用两次以上，第二次读到的 RCC->CSR 已被 RMVF "
                    "清空，g_reset_cause 会被覆写成 OSL_RESET_UNKNOWN、g_reset_csr 变 0"
                )

        # ---- B. hal_time_base_init 只归零一次 ----
        dump = run_tool_args(
            objdump, "-d", "--disassemble=hal_time_base_init", elf.as_posix()
        )
        if "hal_time_base_init>:" not in dump:
            errors.append(
                f"产物 {elf.name} 里找不到 hal_time_base_init 的函数体；"
                "DWT 时基初始化没进镜像（HAL_GetTick 会退化成 0）"
            )
            continue
        insns = _disasm_lines(dump)
        writes = [
            i
            for i, (_a, m, o) in enumerate(insns)
            if m.startswith("str") and o.endswith("#4]")
        ]
        if not writes:
            errors.append(
                f"产物 {elf.name} 里 hal_time_base_init 没有写 DWT->CYCCNT 的 str；"
                "时间基准初始化被改写，无法确认只归零一次"
            )
        elif len(writes) > 1:
            errors.append(
                f"产物 {elf.name} 里 hal_time_base_init 有 {len(writes)} 处写 CYCCNT；"
                "每次 HAL_InitTick 都会归零，HAL_GetTick 不单调"
            )
        else:
            idx = writes[0]
            ctrl_bit0 = any(
                m.split(".")[0] in ("ands", "tst", "and") and "#1" in o
                for (_a, m, o) in insns[:idx]
            )
            skip = False
            for j in range(max(0, idx - 8), idx):
                _a, m, o = insns[j]
                if not COND_BRANCH_RE.match(m):
                    continue
                target = _branch_target(o)
                if target is not None and target >= insns[idx][0]:
                    skip = True
                    break
            if not (ctrl_bit0 and skip):
                errors.append(
                    f"产物 {elf.name} 里 hal_time_base_init 的 CYCCNT 归零前没有"
                    "对 DWT->CTRL bit0 的条件判断（或判断后没有能跳过归零的分支）："
                    "时钟切换时会被再归零一次，HAL_GetTick 不再单调"
                )

        # ---- C. osl_watchdog_relax 的指令流里必须有启动键 0xCCCC ----
        # 静态那边看的是源码里有没有那个常量；这里看它有没有真的变成一条 store。
        # 区别在「常量在、写入被优化掉」这种形态：`(void)IWDG_KEY_ENABLE;` 或把写入
        # 挪进永远不成立的分支，源码文本照样满足、反汇编里却没有 0xCCCC。
        dump = run_tool_args(
            objdump, "-d", "--disassemble=osl_watchdog_relax", elf.as_posix()
        )
        if "osl_watchdog_relax>:" not in dump:
            errors.append(
                f"产物 {elf.name} 里找不到 osl_watchdog_relax 的独立函数体；"
                "启动期 panic 走的正是它，找不到就无法确认 IWDG 会被真正启动"
            )
        elif not re.search(r"#52428", dump):
            errors.append(
                f"产物 {elf.name} 里 osl_watchdog_relax 的指令流没有 0xCCCC（52428）："
                "只预置 PR/RLR 不启动计数器，启动期 panic 播报完就永久自旋，"
                "记录也不会跨复位累积出第二代"
            )
    return errors
def collect_tick_source_errors() -> list[str]:
    """HAL 时基必须是「启动期折算 / 运行期累加」两条路径，且累加器在 .bss。

    改之前只有一条路径：HAL_GetTick() = DWT->CYCCNT / (SystemCoreClock / 1000)。
    反汇编实测每个调用点都带一次 udiv 和三次内存读（SystemCoreClock、DWT->CTRL、
    DWT->CYCCNT）。两个后果都不是理论上的：

      1. **时钟切换处 tick 倒退**。SystemClock_Config 把 HCLK 从 16 MHz 提到
         168 MHz，除数随之从 16000 变 168000，同一个 CYCCNT 读数折算出的毫秒数
         被压小 10.5 倍。切换前取到的 tickstart 在切换后"变小了"，无符号相减
         (`now - tickstart`) 回绕成约 2^32，判据立刻成立 —— 表现是这条超时
         一开始就超时。VOSRDY 那段等待恰好跨在这个窗口里。
      2. **CYCCNT 每 25.6 s 回绕**（2^32 / 168 MHz），HAL_GetTick 跟着归零，
         任何长于 25.6 s 的超时都会被截断。

    改成毫秒累加器后两条都不再有：tick hook 每 1 ms 加一，标度恒定，49.7 天才
    回绕。启动期（调度器还没起、SysTick 还没跳）仍靠 CYCCNT 折算，所以
    HAL_GetTick 必须分流；分流点用 xTaskGetSchedulerState()，不引入新全局。

    首拍对齐：调度器起来之前已经走过几十到几百毫秒。累加器若从 0 开始，tick
    会在那一刻**倒退**到 0。所以 osl_tick_ms_advance() 第一次进来先用当前
    CYCCNT 折算值填进去，之后才 +1 —— 那一次折算必然带一条 udiv。

    判据全部取产物（objdump 把全局符号渲染成 <g_tick_ms>，比查源码文本抗改）：
      A. HAL_GetTick 里有 bl xTaskGetSchedulerState（被内联成读 xSchedulerRunning
         时也认）—— 两条路径真的分开了；
      B. HAL_GetTick 引用 g_tick_ms —— 运行期直接返回，不再做除法；
      C. osl_tick_ms_advance 引用 g_tick_ms 且有一条 add #1 —— 每 tick 递增；
      D. osl_tick_ms_advance 里有 udiv —— 首拍做了折算对齐，不是从 0 开始；
      E. vApplicationTickHook 里有 bl osl_tick_ms_advance —— 真的挂在 tick 上；
      F. g_tick_ms 落在 .bss（nm 类型 b/B）—— 放 .noinit 会跨复位带旧值，
         放栈上则根本不算累加器。
    """
    errors: list[str] = []
    objdump = tool_binary("objdump")
    nm = tool_binary("nm")
    if objdump is None or nm is None:
        return ["找不到 arm-none-eabi-objdump / nm，无法校验 HAL 时基的两条路径"]
    elfs = sorted(OUT_ARTIFACT_DIR.glob("*.elf"))
    if not elfs:
        return errors

    for elf in elfs:
        symbols = run_tool(nm, elf)
        # nm 的类型字母 b/B 对 .noinit（NOBITS）同样成立，光看字母分不出
        # 「复位清零」和「跨复位存活」，所以必须按 _sbss.._ebss 区间核对地址。
        tick = re.search(r"^([0-9a-fA-F]+)\s+\S+\s+g_tick_ms\s*$", symbols, re.MULTILINE)
        sbss = re.search(r"^([0-9a-fA-F]+)\s+\S+\s+_sbss\s*$", symbols, re.MULTILINE)
        ebss = re.search(r"^([0-9a-fA-F]+)\s+\S+\s+_ebss\s*$", symbols, re.MULTILINE)
        if tick is None:
            errors.append(
                f"产物 {elf.name} 里没有 g_tick_ms：HAL 时基的毫秒累加器不存在，"
                "HAL_GetTick 只能回到 DWT->CYCCNT 折算，"
                "时钟切换处 tick 会倒退、25.6 s 会回绕"
            )
            continue
        if sbss is None or ebss is None:
            errors.append(
                f"产物 {elf.name} 里找不到 _sbss/_ebss，无法核对 g_tick_ms 的归属段"
            )
            continue
        tick_addr = int(tick.group(1), 16)
        if not int(sbss.group(1), 16) <= tick_addr < int(ebss.group(1), 16):
            errors.append(
                f"产物 {elf.name} 里 g_tick_ms 不在 .bss（0x{tick_addr:08x} 落在 "
                "_sbss.._ebss 之外）：放 .noinit 会跨复位带上一轮的毫秒数，"
                "所有以复位为原点的超时都会错"
            )
        # objdump 对 .word 字面量池只写裸地址、不渲染符号名（实测：
        # `80007cc: 20000018 .word 0x20000018`），所以引用判据按 nm 地址比对。
        tick_word = f"0x{tick_addr:08x}"

        dump = run_tool_args(objdump, "-d", "--disassemble=HAL_GetTick", elf.as_posix())
        if "HAL_GetTick>:" not in dump:
            errors.append(f"产物 {elf.name} 里找不到 HAL_GetTick 的函数体")
        else:
            if "<xTaskGetSchedulerState>" not in dump and "<xSchedulerRunning>" not in dump:
                errors.append(
                    f"产物 {elf.name} 的 HAL_GetTick 没有调度器状态判断："
                    "启动期与运行期走同一条路径，运行期仍在做 CYCCNT 除法"
                )
            if tick_word not in dump:
                errors.append(
                    f"产物 {elf.name} 的 HAL_GetTick 没有引用 g_tick_ms："
                    "调度器起来后仍按 CYCCNT 折算，时钟切换处 tick 会倒退"
                )

        dump = run_tool_args(
            objdump, "-d", "--disassemble=osl_tick_ms_advance", elf.as_posix()
        )
        if "osl_tick_ms_advance>:" not in dump:
            errors.append(
                f"产物 {elf.name} 里找不到 osl_tick_ms_advance 的函数体："
                "tick 累加器没有人推进，g_tick_ms 会永远停在首拍值"
            )
        else:
            insns = _disasm_lines(dump)
            if tick_word not in dump:
                errors.append(
                    f"产物 {elf.name} 的 osl_tick_ms_advance 没有引用 g_tick_ms"
                )
            elif not any(m.startswith("add") and "#1" in o for (_a, m, o) in insns):
                errors.append(
                    f"产物 {elf.name} 的 osl_tick_ms_advance 里没有 add #1："
                    "累加器没有递增，HAL_GetTick 会一直是同一个值"
                )
            if "<osl_hal_tick_ms_from_dwt>" not in dump and not any(
                m.startswith("udiv") for (_a, m, o) in insns
            ):
                errors.append(
                    f"产物 {elf.name} 的 osl_tick_ms_advance 里既没有 bl "
                    "osl_hal_tick_ms_from_dwt 也没有 udiv：首拍没有用 CYCCNT 折算值对齐，"
                    "调度器起来那一刻 tick 会从 0 重来"
                )

        dump = run_tool_args(
            objdump, "-d", "--disassemble=vApplicationTickHook", elf.as_posix()
        )
        if "vApplicationTickHook>:" not in dump:
            errors.append(
                f"产物 {elf.name} 里找不到 vApplicationTickHook 的函数体"
            )
        elif "<osl_tick_ms_advance>" not in dump:
            errors.append(
                f"产物 {elf.name} 的 vApplicationTickHook 没有调用 osl_tick_ms_advance："
                "毫秒累加器不在 tick 上推进，g_tick_ms 是个死值"
            )
    return errors
def collect_panic_order_errors() -> list[str]:
    """panic 重入闸的判定必须发生在关中断之后，不能有可抢占窗口。

    panic_mask_all_irqs() 用的是 __set_BASEPRI(configMAX_SYSCALL_INTERRUPT_PRIORITY)
    （0x50），只屏蔽数值 >= 5 的异常；HardFault / NMI 优先级是 -1 / -2，不受 BASEPRI
    约束。所以"先判重入、再关中断"这个顺序会留下一段中断未被屏蔽的窗口：判的瞬间
    g_panic_reporting 还是 0，而关中断还没执行，此时一个 HardFault 进来，它先
    panic_mask_all_irqs() 再进 osl_panic_report()，读到的仍是 0 —— 于是两层都通过
    重入闸、都去写 g_panic_record，.noinit 里的现场被撕成一半旧一半新。

    这不是纯理论：stack_guard_check() 在 vApplicationTickHook() 里跑（SysTick 上下文，
    BASEPRI 此时为 0），它检测的正是"MSP 已越过 _estack - _Min_Stack_Size" ——
    MSP 越界本身就是压栈失败，HardFault 多半已经挂起，会在下一条指令边界立刻进来。

    判据直接看指令流（不查源码文本、不查符号是否还在）：
      1) 函数体内必须有一个 bl 指向 panic_mask_all_irqs；
      2) 函数体内第一个条件分支（重入闸的 cbz/cbnz）不得早于该 bl；
      3) 置位对（movs #1 + str [rN, #0]）不得早于该 bl。
    源码写对了顺序、却被优化成别的形状时，(2)(3) 会一起失效并报出来。
    """
    errors: list[str] = []
    objdump = tool_binary("objdump")
    if objdump is None:
        return ["找不到 arm-none-eabi-objdump，无法校验 panic 重入闸与关中断的顺序"]
    elfs = sorted(OUT_ARTIFACT_DIR.glob("*.elf"))
    if not elfs:
        return errors

    for elf in elfs:
        dump = run_tool_args(
            objdump, "-d", "--disassemble=osl_panic_report", elf.as_posix()
        )
        if "osl_panic_report>:" not in dump:
            errors.append(
                f"产物 {elf.name} 里找不到 osl_panic_report 的函数体；"
                "panic 播报没进镜像"
            )
            continue
        insns = _disasm_lines(dump)
        if not insns:
            errors.append(f"产物 {elf.name} 里 osl_panic_report 的反汇编为空")
            continue

        mask_idx = next(
            (
                i
                for i, (_a, m, o) in enumerate(insns)
                if m == "bl" and "panic_mask_all_irqs>" in o
            ),
            None,
        )
        if mask_idx is None:
            errors.append(
                f"产物 {elf.name} 里 osl_panic_report 没有调用 panic_mask_all_irqs；"
                "panic 播报期间没有任何中断屏蔽"
            )
            continue

        cond_idx = next(
            (i for i, (_a, m, _o) in enumerate(insns) if COND_BRANCH_RE.match(m)),
            None,
        )
        if cond_idx is None:
            errors.append(
                f"产物 {elf.name} 里 osl_panic_report 没有任何条件分支；"
                "重入闸（g_panic_reporting 的判断）不见了"
            )
        elif cond_idx < mask_idx:
            errors.append(
                f"产物 {elf.name} 里 osl_panic_report 的重入闸判定排在关中断之前"
                f"（第一条条件分支在第 {cond_idx} 条指令，panic_mask_all_irqs 在第 "
                f"{mask_idx} 条）；判与关之间 BASEPRI 仍是 0，HardFault 不受 BASEPRI "
                "约束，可以从这里插进来再走一遍本函数，把 g_panic_record 撕成一半旧一半新"
            )

        set_idx = None
        # -Os 下 osl_panic_report 被调用者内联/折叠，"置位后关中断"这条指令序不再可读，
        # 按形态判必然假红。跳过只在发布档位发生，-Og 下仍逐条强制（panic-order 组覆盖）。
        if not _skip_on_release_build(
            "osl_panic_report 的重入闸置位与关中断顺序（-Os 下指令序不再可读）"
        ):
            for i in range(len(insns) - 1):
                _a, m, o = insns[i]
                if m != "movs" or "#1" not in o:
                    continue
                dest = o.replace(",", " ").split()[0] if o else ""
                _a2, m2, o2 = insns[i + 1]
                # str 的操作数是 `Rt, [Rn, #off]`：源寄存器是第一项，偏移为 #0 才写同一个字
                if (
                    m2.startswith("str")
                    and dest
                    and o2.startswith(dest + ",")
                    and "#0" in o2
                ):
                    set_idx = i
            if set_idx is None:
                errors.append(
                    f"产物 {elf.name} 里 osl_panic_report 找不到重入闸的置位"
                    "（movs #1 + str）；g_panic_reporting 可能已被删除"
                )
            elif set_idx < mask_idx:
                errors.append(
                    f"产物 {elf.name} 里 osl_panic_report 的重入闸置位排在关中断之前"
                    f"（置位在第 {set_idx} 条指令，panic_mask_all_irqs 在第 {mask_idx} 条）；"
                    "置位之后到关中断之间仍有可抢占窗口"
            )
    return errors
def _strip_ld_comments(text: str) -> str:
    """去掉链接脚本的 /* */ 与 // 注释。

    注释里写着「/DISCARD/ 里应保留 *(.ccmram*)」，不剔除就会把说明文字当成真规则，
    「规则被删掉」这种缺陷会被注释掩盖。
    """
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.DOTALL)
    return re.sub(r"//[^\n]*", "", text)
def collect_startup_guard_errors() -> list[str]:
    """启动早期的等待必须有上界，不能是裸空循环。

    SystemClock_Config 在把主频切到 168 MHz 之前要等 PWR_FLAG_VOSRDY。这条等待
    在 VDD 偏低、稳压器故障或供电爬升过慢时可以永远不满足，而这段代码跑在
    HAL_Init() 之后、应用初始化之前：三灯脚还没配置、osl_panic_report() 没有人调、
    IWDG 也还没启动。板上的表现是"上电后一动不动，也没有任何 fault"，比死机更难查。

    同仓的 osl_boot.c 已经用 IWDG_SYNC_GUARD 给 IWDG 的同步位等待加了上界（那里
    也写明"LSI 万一没起振，SR 永远不会清，这里不能变成又一个死等"），这条把同一
    条推理补到 VOSRDY 上。

    静态一条给出可读原因，产物一条防"源码写了上界却被优化掉/漏了 return"：
    修好之后 SystemClock_Config 的指令流里必须出现计数递减。
    """
    errors: list[str] = []
    if not SYSTEM_STM32F4XX_C.is_file():
        return ["找不到 hal/chip/startup/src/system_stm32f4xx.c，无法校验启动期等待"]
    text = SYSTEM_STM32F4XX_C.read_text(encoding="utf-8")
    if "VOS_READY_GUARD" not in text:
        errors.append(
            "system_stm32f4xx.c 里没有 VOS_READY_GUARD：VOSRDY 等待仍是裸空循环，"
            "VDD 偏低或稳压器故障时会静默死住（此时三灯未配置、panic 无人报、"
            "IWDG 未启动）"
        )

    objdump = tool_binary("objdump")
    if objdump is None:
        errors.append(
            "找不到 arm-none-eabi-objdump，无法校验 SystemClock_Config 的等待上界"
        )
        return errors
    elfs = sorted(OUT_ARTIFACT_DIR.glob("*.elf"))
    for elf in elfs:
        dump = run_tool_args(
            objdump, "-d", "--disassemble=SystemClock_Config", elf.as_posix()
        )
        if "SystemClock_Config>:" not in dump:
            errors.append(f"产物 {elf.name} 里找不到 SystemClock_Config 的函数体")
            continue
        insns = _disasm_lines(dump)
        has_countdown = any(
            m.split(".")[0] == "subs" and "#1" in o for (_a, m, o) in insns
        )
        if not has_countdown:
            errors.append(
                f"产物 {elf.name} 里 SystemClock_Config 的指令流没有计数递减"
                "（subs #1）：VOSRDY 等待没有超时上界，属于静默死等"
            )
    return errors
def collect_ccmram_state_errors() -> list[str]:
    """CCMRAM 的启用状态必须三处同态：输出段 / /DISCARD/ 规则 / startup 的拷贝清零符号。

    本工程不启用 CCMRAM，唯一的拦截面是 /DISCARD/ 里那条段名通配 `*(.ccmram*)`：
    真有人往 `.ccmram` 放变量时，链接期直接报 `defined in discarded section`。
    （这条与归档名规则不同：ld 对段名通配是真生效的，map 里能看到它。）

    两头的静默通道各一条：
      - 规则被删掉：没有任何构建或产物断言会报，verify 照旧通过；ld 会把 `.ccmram`
        当孤立段自行安置，变量读到 CCMRAM 上电随机值，退回成"链接不报错"的老毛病。
      - 半启用：恢复输出段却没补 startup 的 _siccmram/_sccmram/_eccmram，已初始化
        变量拿不到 .data 副本、未初始化变量不清零，链接同样成功。
    所以三处一起检查，且只接受"全关"与"全开"两种状态。

    `cpu.ccm_kb == MEMORY 的 CCMRAM` 那条只保证两个数字相等，与本机制没有因果关系，
    不能替代这里。
    """
    errors: list[str] = []
    info = load_info()
    try:
        script = linker_script_path(info)
    except (FileNotFoundError, ValueError) as error:
        return [f"无法定位链接脚本：{error}"]
    if not script.is_file():
        return ["找不到链接脚本，无法核对 CCMRAM 的启用状态"]
    body = _strip_ld_comments(script.read_text(encoding="utf-8", errors="replace"))

    section_on = CCMRAM_SECTION_RE.search(body) is not None
    discard = re.search(r"/DISCARD/\s*:\s*\{.*?\}", body, re.DOTALL)
    rule_on = discard is not None and CCMRAM_DISCARD_RE.search(discard.group(0)) is not None

    if not STARTUP_STM32F429XX_S.is_file():
        return [
            "找不到 hal/chip/startup/src/startup_stm32f429xx.s，"
            "无法核对 CCMRAM 的拷贝/清零循环"
        ]
    startup = STARTUP_STM32F429XX_S.read_text(encoding="utf-8", errors="replace")
    # 汇编的注释是 /* */ 与 @：注释里提到符号名不算"已有该符号"，
    # 否则把启用步骤写成注释就会把这条判据变成假绿。
    startup = re.sub(r"/\*.*?\*/", "", startup, flags=re.DOTALL)
    startup = re.sub(r"@[^\n]*", "", startup)
    present = [name for name in CCMRAM_STARTUP_SYMBOLS if name in startup]

    if not section_on and rule_on and not present:
        return errors  # 全关：本工程的既定状态
    if section_on and not rule_on and len(present) == len(CCMRAM_STARTUP_SYMBOLS):
        return errors  # 全开

    if not section_on:
        if not rule_on:
            errors.append(
                f"{script.name} 的 /DISCARD/ 里没有段名通配 *(.ccmram*)："
                "CCMRAM 的拷贝/清零循环从未实现，这条规则是唯一的拦截面。缺了它，"
                "ld 会把 .ccmram 当孤立段自行安置 —— 链接成功、不报 "
                "defined in discarded section，变量读到 CCMRAM 上电随机值"
            )
        if present:
            errors.append(
                "startup_stm32f429xx.s 里出现了 " + "、".join(present)
                + "，但链接脚本没有 .ccmram 输出段：拷贝/清零循环搬动的是一个不存在的段"
            )
    else:
        if rule_on:
            errors.append(
                f"{script.name} 恢复了 .ccmram 输出段，却还留着 /DISCARD/ 的 *(.ccmram*)："
                "刚恢复的段会被整条丢掉，链接报 defined in discarded section"
            )
        missing = [name for name in CCMRAM_STARTUP_SYMBOLS if name not in present]
        if missing:
            errors.append(
                ".ccmram 输出段已恢复，但 startup_stm32f429xx.s 里缺 "
                + "、".join(missing)
                + "：已初始化变量拿不到 .data 副本、未初始化变量不清零，链接照样成功，"
                "运行期读到 CCMRAM 上电随机值（半启用状态）"
            )
    return errors
def collect_last_write_errors() -> list[str]:
    """g_last_write 必须有真正的生产者，而不只是"符号还在镜像里"。

    这个 64 B 缓冲在 .noinit（跨复位存活），是本工程唯一"复位后能读到上一轮现场"
    的通道 —— 没有串口、没有 SEGGER RTT、printf 家族也不在镜像里（nm 实测
    printf/vprintf/iprintf/puts/fwrite 一个都没有）。

    但它唯一的写入方 _write 没有任何调用方：objdump 里 bl _write 零条，全靠
    toolchain.make 的 -Wl,-u,_write 把两者钉在镜像里。上一轮 N34 修的是
    "`.noinit` 段归属"，"谁往里写"从来没有 —— 原来的 write 组断言"镜像里有
    _write"，守的是符号存在而非有人调用。

    这里要求 osl_panic_report 自己写：panic 是不返回的（末尾关中断自旋），
    所以摘要必须落在三灯播报之前。判据用 strb —— g_last_write 是 volatile char[]，
    写入必然是字节写；同函数里 g_panic_record 的字段写入与 GPIO BSRR 都是
    32 位 str，不会混淆，所以 strb 在这里只可能来自这个缓冲。

    -Og 下摘要函数通常不会被内联（它有自己的循环与除法），所以两种形态都要认：
    ① 单独立成 static 函数 → 看"osl_panic_report 里有 bl 指向它" 且 "它体内真写"；
    ② 被内联 → 直接看 osl_panic_report 自己体内有没有字节写。
    """
    errors: list[str] = []
    elfs = sorted(OUT_ARTIFACT_DIR.glob("*.elf"))
    if not elfs:
        rel = OUT_ARTIFACT_DIR.relative_to(ROOT).as_posix()
        return [f"找不到 {rel}/*.elf，无法校验 g_last_write 的生产者"]

    nm_bin = tool_binary("nm")
    objdump = tool_binary("objdump")
    if nm_bin is None or objdump is None:
        return ["找不到 arm-none-eabi-nm/objdump，无法校验 g_last_write 的生产者"]

    for elf in elfs:
        syms = run_tool_args(nm_bin, "-S", "-t", "d", elf.as_posix())
        addr = None
        for line in syms.splitlines():
            parts = line.split()
            if len(parts) >= 3 and parts[-1] == "g_last_write":
                try:
                    addr = int(parts[0])
                except ValueError:
                    addr = None
                break
        if addr is None:
            errors.append(
                f"产物 {elf.name} 里没有 g_last_write：跨复位可读的现场通道不存在"
            )
            continue

        hex_addr = format(addr, "x")
        dump = run_tool_args(
            objdump, "-d", "--disassemble=osl_panic_report", elf.as_posix()
        )
        if "osl_panic_report>:" not in dump:
            errors.append(f"产物 {elf.name} 里找不到 osl_panic_report 的函数体")
            continue

        helper = run_tool_args(
            objdump, "-d", "--disassemble=panic_last_write", elf.as_posix()
        )
        called = ("bl" in dump) and ("panic_last_write" in dump)
        helper_writes = (helper.count("strb") >= 1) and (hex_addr in helper)
        inlined_writes = (dump.count("strb") >= 1) and (hex_addr in dump)

        if not ((called and helper_writes) or inlined_writes):
            errors.append(
                f"产物 {elf.name} 里 osl_panic_report 没有向 g_last_write"
                f"（0x{hex_addr}）写入：被调用={called}、辅助函数字节写={helper_writes}"
                f"（strb {helper.count('strb')} 条）、内联字节写={inlined_writes}。"
                "panic 是不返回的，摘要必须落在三灯播报之前，否则 .noinit 里的现场"
                "永远是上一次的遗留值（或初值 NUL）"
            )
    return errors
def collect_persist_record_errors() -> list[str]:
    """复位后要留下的证据变量，不能落在 Reset_Handler 清零的 [_sbss, _ebss) 里。

    nm 的段字母在这里靠不住：`.noinit` 是 NOLOAD 段，`g_panic_record` 与 `.bss`
    里的变量一样被标成 `B`。所以只能拿地址与 `_sbss`/`_ebss` 对拍——落在清零
    区间里就说明每次复位都会被抹掉。

    现状（缺陷）：`g_last_write` 在 `.bss`，`_write` 的输出撑不过一次复位；而它是
    panic 播报之外唯一的调试出口（`_write` 只把最近 64 B 抄进这里、既不打印也不
    走串口），跨复位保留才真正有用。IWDG 复位之后调试器连上来，看到的应该还是
    上一次运行最后写的那段，而不是一片零。
    """
    errors: list[str] = []
    nm = tool_binary("nm")
    elfs = sorted(OUT_ARTIFACT_DIR.glob("*.elf"))
    if nm is None or not elfs:
        return errors
    records = (
        ("g_panic_record", "panic 现场"),
        ("g_last_write", "_write 的最近一段输出"),
    )
    for elf in elfs:
        symbols = run_tool(nm, elf)
        if not symbols:
            continue
        table: dict[str, tuple[int, str]] = {}
        for line in symbols.splitlines():
            parts = line.split()
            if len(parts) == 3:
                try:
                    table[parts[2]] = (int(parts[0], 16), parts[1])
                except ValueError:
                    continue
        bounds: list[int] = []
        for name in ("_sbss", "_ebss"):
            entry = table.get(name)
            if entry is None:
                errors.append(f"产物 {elf.name} 里找不到 {name}，无法确认清零区间")
                break
            bounds.append(entry[0])
        if len(bounds) != 2:
            continue
        sbss, ebss = bounds
        if sbss >= ebss:
            errors.append(f"产物 {elf.name} 的 _sbss 不小于 _ebss，清零区间异常")
            continue
        for name, what in records:
            entry = table.get(name)
            if entry is None:
                errors.append(f"产物 {elf.name} 里没有 {name}；{what} 根本没进镜像")
                continue
            addr = entry[0]
            if sbss <= addr < ebss:
                errors.append(
                    f"产物 {elf.name} 的 {name} 落在清零区间 [_sbss, _ebss) 内"
                    f"（0x{addr:08x}，区间 0x{sbss:08x}..0x{ebss:08x}）："
                    f"Reset_Handler 每次复位都把它抹掉，{what} 撑不过一次复位"
                )
    return errors
def collect_bsp_artifact_errors() -> list[str]:
    """链接之后：按键/蜂鸣器实现必须真的进镜像。

    历史事实：bsl 的 board_key_init/pressed、board_beep_init/on/off 曾经全仓零引用，
    被 -Wl,--gc-sections 整段丢弃。源码、头文件、归档全都在，elf 里却查不到符号，
    板上按 KEY 毫无反应，也没有任何门禁发现——这正是"实现写完但没人接"最典型的假绿。
    """
    errors: list[str] = []
    nm = tool_binary("nm")
    elfs = sorted(OUT_ARTIFACT_DIR.glob("*.elf"))
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
    """文档里引用的路径必须真实存在。

    两种写法都要查：反引号包住的路径（`` `doc/x.md` ``）与 Markdown 链接
    （`` [说明](../doc/x.md) ``）。

    历史事实一：`.skills/工程规格.md` 同时写了 `doc/工具链安装.md`（不存在）和
    `doc/开发环境配置.md`（实际在 doc/design/ 下），两者都没被任何检查发现，
    因为 verify 只管"文档里的层名/链接顺序"，不管路径。
    历史事实二：第一版只查反引号，链接形式一条都不查，于是积累出 14 条点开即 404
    的相对链接，而 `doc-reference` 门禁组照样全绿。链接常写成 `../` 相对形式，
    所以这里按 `path.parent` 解析，不要求仓库相对前缀。
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

    def ref_missing(path: Path, ref: str) -> bool:
        """`ref` 解析到的目标不存在时返回 True。"""
        if ref.endswith("/"):
            target = path.parent / ref if ref.startswith((".", "..")) else ROOT / ref
            return not target.is_dir()
        if ref.startswith(("./", "../")):
            target = path.parent / ref
        elif ref.startswith(DOC_REF_PREFIXES):
            target = ROOT / ref
        else:
            return False  # 裸文件名/绝对路径不判：它们是把名字当术语用
        return not target.exists()

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
                # 计划文档里的未来路径：反引号后紧邻写「（待建）」即放过这一条。
                # 标记必须紧邻且同一行 —— 不认「本行任意位置出现『待建』」：
                # 一句话里同时提到「已存在的 A」与「待建的 B」时，那会把 A 一起漏检。
                # 词表固定，不引入文件级白名单：整个 doc/ 都仍然是默认要查的。
                tail = line[match.end():match.end() + 8]
                if re.match(r"[（(](待建|planned)", tail):
                    continue
                if (ROOT / ref).exists() or (path.parent / ref).exists():
                    continue
                errors.append(f"{relative}:{lineno} 引用的路径不存在：{ref}")
            for match in re.finditer(r"\[[^\]\n]*\]\(\s*([^)\s]+)\s*\)", line):
                ref = match.group(1).split("#", 1)[0].strip()
                if not ref or ref.startswith(("http://", "https://", "mailto:")):
                    continue
                if any(ref.startswith(prefix) for prefix in DOC_SCAN_SKIP_PREFIXES):
                    continue
                if not (ref.endswith("/") or ref.lower().endswith(DOC_REF_EXTENSIONS)):
                    continue
                if ref_missing(path, ref):
                    errors.append(f"{relative}:{lineno} 链接指向的路径不存在：{ref}")
    return errors
def _eol_is_binary(path: Path) -> bool:
    """按 git 的 text=auto 同款口径判断二进制：前 8 KB 有 NUL 或不是 UTF-8。

    不做这一步，docx / xlsx 这类 zip 容器会被当文本扫，里面恰好出现的 \\r\\n
    会让这条断言误报 —— 它们的字节和"行尾"毫无关系。

    解码走增量解码器的 final=False：8192 字节极可能正好切在 UTF-8 多字节序列
    中间，直接 head.decode("utf-8") 会抛 UnicodeDecodeError，把普通中文文档误判
    成二进制（实测 doc/hardware/datasheet/mcu/STM32F429-系统存储器自举模式.md
    就是这样漏过去的：它含 CRLF 却一直没被这条断言抓到）。
    """
    try:
        head = path.read_bytes()[:8192]
    except OSError:
        return True
    if b"\x00" in head:
        return True
    decoder = codecs.getincrementaldecoder("utf-8")()
    try:
        decoder.decode(head, final=False)
    except UnicodeDecodeError:
        return True
    return False
def _gitattributes_rules() -> list[tuple[str, list[str]]]:
    """读 .gitattributes 的非注释行，顺序即优先级（后匹配的覆盖先匹配的）。"""
    rules: list[tuple[str, list[str]]] = []
    path = ROOT / ".gitattributes"
    if not path.is_file():
        return rules
    for line in path.read_text(encoding="utf-8").splitlines():
        line = line.split("#", 1)[0].strip()
        if not line:
            continue
        parts = line.split()
        if len(parts) < 2 or parts[0].startswith("!"):
            continue
        rules.append((parts[0], parts[1:]))
    return rules
def _gitattributes_lines() -> list[tuple[int, str, list[str]]]:
    """逐行读 .gitattributes，返回 (行号, pattern, attrs)。行号供顺序类断言定位。"""
    entries: list[tuple[int, str, list[str]]] = []
    path = ROOT / ".gitattributes"
    if not path.is_file():
        return entries
    for lineno, raw in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        line = raw.split("#", 1)[0].strip()
        if not line:
            continue
        parts = line.split()
        if len(parts) < 2 or parts[0].startswith("!"):
            continue
        entries.append((lineno, parts[0], parts[1:]))
    return entries
def collect_eol_contract_errors() -> list[str]:
    """行尾"规则侧"必须成立的三件事，与"工作区文件真不真"分开检查。

    ① 必须存在兜底规则 `* text=auto eol=lf`。这一行一被注释掉，其余的规则只剩 4 条
       扩展名和 7 条 LFS，`_eol_resolved()` 对绝大多数据文件返回 (text=False, eol=None)，
       `collect_line_ending_errors()` 就一路 `continue` 跳过 —— 断言全绿，契约整个消失。
       这是最典型的"删掉被守护的东西，守护者也跟着哑掉"：所有检查都建立在"规则说
       eol=lf 才检查"之上，规则没了就没人可查。
    ② 兜底规则必须排在所有含 `-text` / `binary` 的规则之前。gitattributes 是后匹配覆盖
       先匹配，挪到后面会让 LFS 的 `-text` 被盖成 text=auto，PDF 与压缩包被当文本处理，
       而当前判据对二进制文件一律跳过，静态侧不会出现任何红。
    ③ 打包产物 .hex / .map 必须显式声明成文本 + LF。它们确实是纯 ASCII（Intel HEX 是
       可读的文本记录、map 是链接器打印的文本），但如果只靠 `*` 兜底，"这两个文件算
       文本还是二进制"这个口径就永远停留在读者脑子里；显式写下来，顺带让第 ② 条的
       顺序约束有更多 -text/text 对照可验证。
    """
    entries = _gitattributes_lines()
    if not entries:
        return [".gitattributes 不存在或没有一条可执行规则，行尾契约无从谈起"]

    fallback = [
        lineno
        for lineno, pattern, attrs in entries
        if pattern == "*" and "text=auto" in attrs and "eol=lf" in attrs
    ]
    if not fallback:
        return [
            ".gitattributes 缺少兜底规则 `*       text=auto eol=lf`："
            "它一被注释掉，其余规则只剩 4 条扩展名与 7 条 LFS，"
            "大多数文件被解析成 (text=False, eol=None) 后直接跳过 —— "
            "行尾断言全绿，契约却已整个消失"
        ]

    line = fallback[0]
    negations = [
        lineno
        for lineno, _, attrs in entries
        if "-text" in attrs or "binary" in attrs
    ]
    earlier = [n for n in negations if n < line]
    if earlier:
        return [
            f".gitattributes 的兜底规则在第 {line} 行，"
            f"但第 {min(earlier)} 行的 -text / binary 规则排在它之前："
            "gitattributes 后匹配覆盖先匹配，LFS 的 -text 会被盖成 text=auto，"
            "PDF 与压缩包会被当文本处理"
        ]

    errors: list[str] = []
    for suffix, kind in ((".hex", "Intel HEX"), (".map", "链接 map")):
        sample = f"out/userdebug/release/2026_11_06-12_00_00/sample{suffix}"
        explicit = [
            pattern
            for _, pattern, attrs in entries
            if pattern != "*"
            and "eol=lf" in attrs
            and _eol_pattern_matches(pattern, sample)
        ]
        if not explicit:
            errors.append(
                f".gitattributes 没有显式声明 `*{suffix}`（{kind}，纯 ASCII 文本）："
                "只靠 * 兜底等于把\"它是文本还是二进制\"留给下一个人推"
            )
    for lineno, pattern, attrs in entries:
        for attr in attrs:
            if attr.startswith("eol=") and attr not in ("eol=lf", "eol=crlf"):
                errors.append(
                    f".gitattributes 第 {lineno} 行（{pattern}）的 {attr} 不是合法 eol 值"
                )

    # ④ 生成端三处写盘必须在函数体内显式带 newline="\n"。
    # 这一条必须查源码形态，不能靠"删掉 newline 再跑 update 看有没有写 CRLF"：
    # Linux 上 os.linesep 就是 \n，删掉 newline 照样写 LF，破坏在 CI 上根本不成立
    # （实测本地 Windows PASS、CI 报"预期失败，但退出码是 0"）。而缺陷本身正是环境
    # 相关的：只有 Windows 上 newline=None 才会把 \n 写成 \r\n。
    # 按函数体切片而不是数全文出现次数 —— 全文计数会被 docstring 里的 write_text(
    # 字样干扰（实测数出 5 处而实际只有 4 处真实调用，断言自己先假红了一次）。
    write_disk_funcs: list[tuple[Path, str]] = [
        (ROOT / "build" / "generate.py", "load_info"),
        (ROOT / "build" / "generate.py", "write_generated_text"),
        (ROOT / "build" / "build.py", "fold_root_in_map"),
    ]
    for source, func in write_disk_funcs:
        if not source.is_file():
            errors.append(f"缺少 {source.relative_to(ROOT)}，无法确认 {func}() 写盘行尾")
            continue
        code = source.read_text(encoding="utf-8")
        start = code.find(f"def {func}(")
        if start < 0:
            errors.append(f"{source.relative_to(ROOT)} 找不到 def {func}(，无法确认它写盘的行尾")
            continue
        end = code.find("\ndef ", start + 1)
        body = code[start:end if end > 0 else len(code)]
        if "write_text(" not in body:
            errors.append(f"{func}() 里没有 write_text()，写盘路径变了需要重新确认")
        elif 'newline="\\n"' not in body:
            errors.append(
                f"{func}() 的 write_text() 没有显式 newline=\"\\n\"："
                "newline=None 在 Windows 上把 \\n 翻译成 \\r\\n，"
                "update 每轮都会把生成物写成 CRLF、行尾断言每轮都红"
            )
    return errors
def _eol_pattern_matches(pattern: str, relative: str) -> bool:
    """gitattributes 的匹配语义：含 / 的 pattern 相对仓库根锚定，否则匹配任意层级。"""
    pattern = pattern.rstrip("/")
    if not pattern:
        return False
    if "/" in pattern:
        return fnmatch.fnmatchcase(relative, pattern)
    return fnmatch.fnmatchcase(relative.rsplit("/", 1)[-1], pattern)
def _eol_resolved(relative: str, rules: list[tuple[str, list[str]]]) -> tuple[bool, str | None]:
    text = False
    eol = None
    for pattern, attrs in rules:
        if not _eol_pattern_matches(pattern, relative):
            continue
        for attr in attrs:
            if attr in ("-text", "binary"):
                text = False
                eol = None
            elif attr == "text" or attr.startswith("text="):
                text = True
            elif attr.startswith("eol="):
                eol = attr[4:]
    return text, eol
def collect_line_ending_errors() -> list[str]:
    """被 .gitattributes 钉成 eol=lf 的文件，工作区里必须真的只有 LF。

    历史事实：这条规则原来只列了 *.c / *.h / *.py / *.ld 四个扩展名。结果同一个
    `.skills` 目录下 SKILL.md、工程规格.md、架构缺陷.md 是 CRLF，文档维护.md 是 LF；
    startup 的 *.s 和 69 个 update 生成的 Kconfig / Makefile 的行尾全凭本机
    core.autocrlf 决定。行尾本该是"每个文件自己的事"，但只要有人按字节改锚点
    （gate-selftest 改源码、文档回填改 .skills），锚点就跟着换机器换行尾而静默失配
    —— 门禁报"锚点没匹配上"，与真实缺陷无关。

    判据取 .gitattributes 当唯一来源：规则说 eol=lf 的文件，工作区含 CRLF 就报错。
    刻意不调 git 可执行文件（本机 git 不在 PATH，`shutil.which` 拿不到就会让断言
    静默跳过），所以本地和 CI 判的是同一件事。
    """
    errors: list[str] = []
    rules = _gitattributes_rules()
    if not rules:
        return [".gitattributes 不存在或没有任何规则，行尾完全没有约束"]
    for dirpath, dirnames, filenames in os.walk(ROOT):
        relative_dir = Path(dirpath).relative_to(ROOT).as_posix()
        keep: list[str] = []
        for name in dirnames:
            if name in EOL_SKIP_DIRS:
                continue
            child = name if relative_dir == "." else f"{relative_dir}/{name}"
            if child in EOL_SKIP_SUBTREES:
                continue
            keep.append(name)
        dirnames[:] = sorted(keep)
        for name in sorted(filenames):
            path = Path(dirpath) / name
            relative = path.relative_to(ROOT).as_posix()
            text, eol = _eol_resolved(relative, rules)
            if not text or eol != "lf":
                continue
            if _eol_is_binary(path):
                continue
            try:
                data = path.read_bytes()
            except OSError:
                continue
            count = data.count(b"\r\n")
            if count:
                errors.append(
                    f"{relative}: 工作区有 {count} 处 CRLF，但 .gitattributes 钉了 eol=lf"
                )
    return errors
_ASSERT_HITS_PATH = os.environ.get("BUILD_ASSERT_HITS")
_ASSERT_TRACE: list[dict[str, object]] = []
def _instrument(func):
    def wrapper():
        result = func()
        if _ASSERT_HITS_PATH:
            _ASSERT_TRACE.append({"name": func.__name__, "errors": len(result or [])})
        return result

    wrapper.__name__ = func.__name__
    return wrapper
if _ASSERT_HITS_PATH:
    for _name, _func in list(globals().items()):
        if _name.startswith("collect_") and _name.endswith("_errors") and callable(_func):
            globals()[_name] = _instrument(_func)
    # `collect_verify_errors` 通过模块级元组按对象引用调子函数，替换 globals 管不到
    # 元组里已绑定的旧对象——不重建它，五类判据的命中会全被记到组合器头上（实测）。
    if "VERIFY_COLLECTORS" in globals():
        globals()["VERIFY_COLLECTORS"] = tuple(
            globals()[func.__name__] for func in VERIFY_COLLECTORS
        )

    def _dump_assert_trace() -> None:
        try:
            Path(_ASSERT_HITS_PATH).write_text(
                json.dumps(_ASSERT_TRACE, ensure_ascii=False), encoding="utf-8"
            )
        except OSError:
            pass

    atexit.register(_dump_assert_trace)


# --- gate-selftest ---
"""门禁自测：逐个破坏被测条件，确认对应断言真的会失败，再原样还原。

为什么需要这个：`verify` 的断言在"代码本来就正确"时永远是绿的，而**绿一次不能
证明它会在该报错时报错**（测试立刻通过等于没测到东西）。这里给每组断言造一个
最小破坏，要求它以非零退出并打出预期关键字，然后还原并复核。

组数由下面的 `CASES` 决定——加一条断言就该加一组；完整清单以
`python build/build.py gate-selftest --list` 为准（CI 的 job 矩阵也是它生成的）。
下表只列出最早的五组作示例：

| 组（示例） | 破坏方式 | 期望的失败信息 | 代价 |
|----|----------|----------------|------|
| flags | 不动文件，检查 `make -n -W <abs>/toolchain.make` 是否重编 | 必须打印 `CC` 行 | 秒级 |
| heap  | 从 `toolchain.make` 删 `-Wl,-u,_sbrk` | `_sbrk` 强制引用缺失 | 秒级（静态） |
| heap-bound | 把链接脚本的 `_Min_Heap_Size` 改大、不重链接 | 产物边界与脚本声明"已不一致" | 一次 build |
| bsp   | 注释掉 `main()` 里的 `fml_button_start()` | 产物里找不到 `board_key_init` | 一次 build + 一次恢复 build |
| fault | startup 的 `Default_Handler` 改回 `b Infinite_Loop` | 产物里的 `Default_Handler` 不跳 `UnhandledIRQ_Handler` | 一次 make + 一次恢复 make |

用法：

    python build/build.py gate-selftest                 # 全部组（数量见 --list）
    python build/build.py gate-selftest --only bsp      # 只跑一组
    python build/build.py gate-selftest --keep-going    # 某组失败也继续跑其余组

约定：
- 每组都先备份原始字节，`finally` 里写回并恢复 mtime，最后复核哈希与原文一致；
  即使中途异常（含 Ctrl+C）也不会把源码树留在破坏状态。
- 破坏后被改动过的文件 mtime 会被刷新，保证 make 真的重编，而不是拿旧 `.o` 假绿。
- 找不到 `make`（或 shell）时该组记为 SKIP，不算失败；其余组照常跑。
"""

BUILD_MAIN = ROOT / "build" / "build.py"
TOOLCHAIN = ROOT / "toolchain.make"
MAIN_C = ROOT / "apl" / "src" / "main.c"
BOARD_LED_C = ROOT / "bsl" / "src" / "board_led.c"
BOARD_BEEP_C = ROOT / "bsl" / "src" / "board_beep.c"
BOARD_KEY_C = ROOT / "bsl" / "src" / "board_key.c"
SYSTEM_STM32F4XX_C = ROOT / "hal" / "chip" / "startup" / "src" / "system_stm32f4xx.c"
STARTUP_S = ROOT / "hal" / "chip" / "startup" / "src" / "startup_stm32f429xx.s"
PANIC_STORE_C = ROOT / "osl" / "src" / "osl_panic.c"
PANIC_RECORD_H = ROOT / "osl" / "inc" / "osl_panic_record.h"
PORT_ST_C = ROOT / "osl" / "src" / "osl_boot.c"
BOARD_SYSTEM_C = ROOT / "bsl" / "src" / "board_system.c"
FML_BUTTON_C = ROOT / "fml" / "src" / "fml_button.c"
FML_LED_C = ROOT / "fml" / "src" / "fml_led.c"
FREERTOS_CONFIG_H = ROOT / "osl" / "inc" / "FreeRTOSConfig.h"
OSL_BOOT_C = ROOT / "osl" / "src" / "osl_boot.c"
OSL_HEALTH_H = ROOT / "osl" / "inc" / "osl_health.h"
OSL_HEALTH_C = ROOT / "osl" / "src" / "osl_health.c"


def run(args: list[str], *, env: dict[str, str] | None = None) -> subprocess.CompletedProcess:
    """跑子进程，合并 stderr 便于匹配失败信息。"""
    return subprocess.run(
        args,
        cwd=ROOT,
        capture_output=True,
        text=True,
        encoding="utf-8",
        errors="replace",
        env=env,
    )


ASSERT_HITS_FILE = Path(tempfile.gettempdir()) / "gate-selftest-assert-hits.json"
ASSERT_HITS: dict[str, int] = {}
"""本次 process 内所有 build/build.py 子进程调用累计到的「断言名 → 最多报了几条」。

跨调用取 max：`build` 内部会先跑一次 `update`（内含 verify），同一条断言可能出现
两次；条数只会因为破坏更彻底而变多，取 max 就是最坏情形。
"""


def create_project(*args: str) -> subprocess.CompletedProcess:
    """跑 build/build.py 子命令，并回收「这次到底哪几条断言真的报了错」。

    这是「断言 ↔ 门禁组」映射的证据来源：不靠读代码猜某组守哪条，而是让被测程序
    在破坏态下自己把报错的断言名交出来（见 verify 里 `_instrument` 插桩）。
    """
    env = dict(os.environ)
    env["BUILD_ASSERT_HITS"] = str(ASSERT_HITS_FILE)
    result = run([sys.executable, str(BUILD_MAIN), *args], env=env)
    try:
        records = json.loads(ASSERT_HITS_FILE.read_text(encoding="utf-8"))
    except (OSError, ValueError):
        records = []
    for record in records:
        name = record.get("name")
        count = record.get("errors", 0)
        if isinstance(name, str) and isinstance(count, int) and count > 0:
            ASSERT_HITS[name] = max(ASSERT_HITS.get(name, 0), count)
    return result


# 自带工具链是老式 MSYS（把 D: 挂成 /cygdrive/d），msys64 是 MSYS2（挂成 /d）。
# find_make() 依据实际选中的 make 设定它，msys_path() 据此拼 `-W` 的参数。
MSYS_MOUNT = "/"


def find_make() -> Path | None:
    """定位 make：Windows 上优先工程自带的 32 位 MSYS，其次 PATH，最后本机 MSYS 常见位置。

    自带 `tool/win_x86_64/bin/make.exe` 配对的 32 位 `sh.exe` 每次 fork 只复制几 MB
    的 cygheap；msys64 的 64 位 sh 在宿主机提交内存紧张时会直接
    `fork: Resource temporarily unavailable`（Error 127 / `Bad address`），让整组门禁
    假红——实测全量 6 组失败全部由此产生，同一时刻 `build/build.py build`
    用自带 sh 却一次通过。所以这里把自带工具链排在 PATH 前面。
    """
    if os.name == "nt":
        global MSYS_MOUNT
        bundled = ROOT / "tool" / "win_x86_64" / "bin" / "make.exe"
        if bundled.is_file():
            MSYS_MOUNT = "/cygdrive/"
            return bundled
    MSYS_MOUNT = "/"
    found = shutil.which("make")
    if found:
        return Path(found)
    if os.name == "nt":
        for candidate in (
            Path(r"D:\ExPrograms\DevelopTool\msys64\usr\bin\make.exe"),
            Path(r"C:\msys64\usr\bin\make.exe"),
        ):
            if candidate.is_file():
                return candidate
    return None


def make_env(make: Path) -> dict[str, str]:
    """make 规则里用了 mkdir/rm 之类，Windows 上必须把 MSYS 的 bin 放到 PATH 前面。"""
    env = dict(os.environ)
    msys_bin = make.parent
    env["PATH"] = str(msys_bin) + os.pathsep + env.get("PATH", "")
    return env


def msys_shell(make: Path) -> str | None:
    """MSYS 的 make 需要一个 POSIX shell 才能执行 mkdir/rm；Linux 上不需要指定。"""
    if os.name != "nt":
        return None
    shell = make.parent / "sh.exe"
    return str(shell) if shell.is_file() else None


def msys_path(path: Path) -> str:
    """把 D:/x/y 折成 `${MSYS_MOUNT}d/x/y`（自带 MSYS 是 /cygdrive/d，MSYS2 是 /d）。

    MSYS 版 make 会把依赖表里的 PROJECT_ROOT 归一成 POSIX 形式，所以 `-W` 的参数
    必须用同一形式；传 `D:/…` 时它不会匹配到任何目标，`-W` 就静默失效（本脚本
    第一次跑就是这样误报 FLAG_DEPS 失效的）。
    """
    as_posix = path.as_posix()
    if os.name != "nt":
        return as_posix
    drive, _, rest = as_posix.partition(":")
    return f"{MSYS_MOUNT}{drive.lower()}{rest}"


@contextlib.contextmanager
def patched(path: Path, transform):
    """临时改写一个文本文件，退出时恢复字节与 mtime。"""
    original = path.read_bytes()
    text = original.decode("utf-8")
    changed = transform(text)
    if changed == text:
        raise RuntimeError(f"{path.name} 的破坏没有生效（锚点没匹配上）")
    path.write_bytes(changed.encode("utf-8"))
    try:
        yield
    finally:
        path.write_bytes(original)
        # 还原后 mtime 必须刷到当下：内容虽然回到原样，但破坏期间的 `.o` 是拿坏源码
        # 编出来的，mtime 若回退到旧值，make 会认为目标 up-to-date 而不重编，产物就
        # 永远停在破坏版本上（历史上 Copy-Item 还原踩过一次同样的坑）。
        os.utime(path, None)


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()[:12]


def expect_failure(result: subprocess.CompletedProcess, needle: str) -> str | None:
    """断言：退出码非 0，且输出里出现预期关键字。返回 None 表示通过。"""
    if result.returncode == 0:
        return f"预期失败，但退出码是 0（关键字 {needle!r} 没触发）"
    blob = result.stdout + result.stderr
    if needle not in blob:
        return f"退出码 {result.returncode} 正确，但输出里没有 {needle!r}"
    return None


def expect_success(result: subprocess.CompletedProcess) -> str | None:
    if result.returncode != 0:
        blob = (result.stdout + result.stderr).strip().splitlines()
        tail = "\n".join(blob[-6:])
        return f"预期成功，实际退出码 {result.returncode}：\n{tail}"
    return None


def case_flags() -> tuple[str, str]:
    """flags 依赖：toolchain.make 变过一次，所有对象必须重编（FLAG_DEPS）。

    这里不改文件：`make -n -W <abs>/toolchain.make` 就是把依赖声明成已过期，
    只看它是否打印 `CC` 行。打印 = 会重编；不打印 = 改了编译参数却拿旧对象链接。
    """
    make = find_make()
    if make is None:
        return "SKIP", "找不到 make"
    env = make_env(make)
    args = [str(make), "-C", str(ROOT / "osl" / "src"), "-n",
            "-W", msys_path(ROOT / "toolchain.make"), "all"]
    shell = msys_shell(make)
    if shell:
        args.append(f"SHELL={shell}")
    result = run(args, env=env)
    blob = result.stdout + result.stderr
    if result.returncode != 0:
        return "FAIL", f"make 本身失败（{result.returncode}）：\n{blob[-400:]}"
    cc_lines = [line for line in blob.splitlines() if 'echo "CC ' in line]
    if not cc_lines:
        return "FAIL", "toolchain.make 被声明为已过期，但没有出现任何 CC 行（FLAG_DEPS 失效）"
    return "PASS", f"{len(cc_lines)} 个对象被要求重编"


def case_heap() -> tuple[str, str]:
    """堆边界：`-Wl,-u,_sbrk` 缺失时 verify 必须报错（否则 -lnosys 空桩会静默接管）。"""
    before = digest(TOOLCHAIN)
    with patched(TOOLCHAIN, lambda t: t.replace(" -Wl,-u,_sbrk", "")):
        result = create_project("verify")
        problem = expect_failure(result, "_sbrk")
    if before != digest(TOOLCHAIN):
        return "ERROR", "toolchain.make 没有还原到原始字节"
    if problem:
        return "FAIL", problem
    return "PASS", "删掉 -Wl,-u,_sbrk 后 verify 报错，并已还原"


def case_bsp() -> tuple[str, str]:
    """BSP 接入：注释掉 fml_button_start() 后，产物里必须找不到 board_key_init。"""
    before = digest(MAIN_C)
    with patched(MAIN_C, lambda t: t.replace(
        "    fml_button_start();",
        "    /* gate-selftest: fml_button_start(); */",
    )):
        result = create_project("build")
        problem = expect_failure(result, "board_key_init")
    if before != digest(MAIN_C):
        return "ERROR", "main.c 没有还原到原始字节"
    # 破坏期间的镜像已经是缺 BSP 的版本，必须重建回正确产物再收工。
    restore = create_project("build")
    if problem:
        return "FAIL", problem
    if restore.returncode != 0:
        return "FAIL", "破坏已还原，但恢复构建失败（产物可能仍缺 BSP）"
    return "PASS", "去掉 fml_button_start() 后产物断言报错，已还原并重建"


def case_board_polarity() -> tuple[str, str]:
    """bsl 物理极性：LED 低电平亮、蜂鸣器高电平响。

    这两条此前没有任何门禁组覆盖。极性写反时编译、链接、尺寸、产物符号全绿 ——
    只有板子上表现为「灯常亮/常灭」「蜂鸣器一直响」，而 verify 一片通过。纯静态。
    """
    before = digest(BOARD_LED_C)
    with patched(BOARD_LED_C, lambda t: t.replace(
        "on ? GPIO_PIN_RESET : GPIO_PIN_SET",
        "on ? GPIO_PIN_SET : GPIO_PIN_RESET",
    )):
        problem = expect_failure(create_project("verify"), "board_led_set 不是")
    if before != digest(BOARD_LED_C):
        return "ERROR", "board_led.c 没有还原到原始字节"
    if problem:
        return "FAIL", "LED 极性写反了但 verify 没报：" + problem

    before = digest(BOARD_BEEP_C)
    with patched(BOARD_BEEP_C, lambda t: t.replace(
        "on ? GPIO_PIN_SET : GPIO_PIN_RESET",
        "on ? GPIO_PIN_RESET : GPIO_PIN_SET",
    )):
        problem = expect_failure(create_project("verify"), "board_beep_set 不是")
    if before != digest(BOARD_BEEP_C):
        return "ERROR", "board_beep.c 没有还原到原始字节"
    if problem:
        return "FAIL", "蜂鸣器极性写反了但 verify 没报：" + problem

    # 第三段：只改 led_pin[] 的一个脚（PB2 → PB9），panic 播报的三个掩码不同步。
    # panic_leds 故意绕过 bsl 只写寄存器（栈可能已溢出），所以
    # PANIC_LED_MASK / PANIC_LED_MODE_MASK / PANIC_LED_MODE_OUT 是同一组脚的第三份
    # 独立定义。上面两段（数量、极性）都不会叫，panic-beep 组看的是 PI11，也不会叫 ——
    # 只有按 led_pin[] 反算掩码的交叉比对能叫。
    before = digest(BOARD_LED_C)

    def shift_one_led(text: str) -> str:
        anchor = "GPIO_PIN_2,"
        if anchor not in text:
            raise RuntimeError("board_led.c 里没有 GPIO_PIN_2 锚点")
        return text.replace(anchor, "GPIO_PIN_9,", 1)

    with patched(BOARD_LED_C, shift_one_led):
        problem = expect_failure(create_project("verify"), "PANIC_LED_MASK = ")
    if before != digest(BOARD_LED_C):
        return "ERROR", "board_led.c 没有还原到原始字节"
    if problem:
        return "FAIL", "改了 led_pin[] 的一个脚，但 panic 掩码没报不一致：" + problem
    return "PASS", "LED / 蜂鸣器极性各反一次、灯脚改一处漏改 panic 掩码，三处都报错，已还原"


def case_board_key() -> tuple[str, str]:
    """按键按下电平：把 KEY2 的 pressed_level 改成 RESET，verify 必须报。

    KEY2 历史上就写成过 RESET（与原理图相反），当时没有任何检查发现：读到的电平
    永远与按键相反，表现为「松开算按下」。
    """
    before = digest(BOARD_KEY_C)

    def flip_key2(text: str) -> str:
        anchor = "{GPIOC, GPIO_PIN_13, GPIO_PIN_SET}"
        if anchor not in text:
            raise RuntimeError("board_key.c 的 KEY2 初始化项没找到")
        return text.replace(anchor, "{GPIOC, GPIO_PIN_13, GPIO_PIN_RESET}", 1)

    with patched(BOARD_KEY_C, flip_key2):
        problem = expect_failure(create_project("verify"), "按下电平写成")
    if before != digest(BOARD_KEY_C):
        return "ERROR", "board_key.c 没有还原到原始字节"
    if problem:
        return "FAIL", problem
    return "PASS", "KEY2 按下电平改成 RESET 后 verify 报错，已还原"


def case_isr_priority() -> tuple[str, str]:
    """中断优先级约定：出现 *FromISR 却没给出该中断的 NVIC 优先级时，verify 必须报错。

    两个子判据，缺一个这组就是假的：
      1) 注释里的 API 名不算调用 —— 注释里提一句「不要在这里调 xQueueReceiveFromISR」
         不该让 verify 误报（此前实测 EXIT=1）；
      2) 真实代码里的调用必须报 —— 破坏点因此不能放注释。
    `collect_isr_priority_errors()` 自 N46 起先剥注释再匹配，所以探针必须写进代码。

    这一组是纯静态（不编译），但**仍要先建一次产物**：`verify` 默认还会跑
    `collect_last_write_errors()`，它要读 `out/<档位>/objects/target/*.elf`；CI 每个 job 都是干净
    检出、没有 elf，于是第一条「期望成功」会被「找不到 *.elf」顶成失败。此前这组
    只有「期望失败」一条，退出码恰好对上，在 CI 上一直是假绿 —— 加了这条才暴露。
    """
    blocked = ensure_artifacts()
    if blocked:
        return "SKIP", blocked
    before = digest(FML_BUTTON_C)
    with patched(
        FML_BUTTON_C,
        lambda t: t + '\n/* gate-selftest: 不要在这里调 xQueueReceiveFromISR(q, &item, 0U); */\n',
    ):
        problem = expect_success(create_project("verify"))
    if problem:
        return "FAIL", "注释里提及 ISR 版 API 后 verify 误报：" + problem
    with patched(
        FML_BUTTON_C,
        lambda t: t + "\nstatic void gate_probe_isr(void) { xQueueReceiveFromISR(0, 0, 0); }\n",
    ):
        problem = expect_failure(create_project("verify"), "ISR 版 API")
    if before != digest(FML_BUTTON_C):
        return "ERROR", "fml_button.c 没有还原到原始字节"
    if problem:
        return "FAIL", problem
    return "PASS", "注释里的 API 名不误报、真实调用仍报缺优先级，已还原"


def case_panic_mask() -> tuple[str, str]:
    """panic 通道重入：两处关中断各自都要在破坏态下变红。

    1) osl_panic_report 入口的全局屏蔽（删掉那次调用）；
    2) fault_report 在写 record 之前的屏蔽（删掉那一行）。
    纯静态（不编译、不要产物）。只删调用、保留函数定义——测的是"入口有没有真的
    屏蔽"，而不是"函数还在不在"。

    入口那处锚点必须跟着源码的实际顺序走：现在 `panic_mask_all_irqs()` 排在
    `if (g_panic_reporting != 0U)` 之前（N44 修的就是这个顺序）。如果还按旧形态
    （`g_panic_reporting = 1U;` 后面跟 `panic_mask_all_irqs();`）写锚点，替换会一次
    都匹配不上、破坏根本没发生，于是本组退化成"没破坏也就没失败预期"的假绿。
    """
    before = digest(PANIC_STORE_C)
    problems: list[str] = []

    with patched(
        PANIC_STORE_C,
        lambda t: t.replace(
            "    panic_mask_all_irqs();\n    if (g_panic_reporting != 0U) {\n",
            "    if (g_panic_reporting != 0U) {\n",
            1,
        ),
    ):
        result = create_project("verify")
        problems.append(expect_failure(result, "panic_mask_all_irqs()"))

    # ② fault_report 写 record 之前的那次屏蔽。第三轮起 panic_mask_all_irqs() 与
    #    g_panic_record.magic 之间还夹着 panic_clear_fault_scene() 和解释它的注释块，
    #    原来「连写三行」的固定锚点随即失配（破坏没发生 → patched() 抛 RuntimeError）。
    #    改成按结构定位：在 fault_report 定义之后删掉第一处 `    panic_mask_all_irqs();`。
    def drop_fault_mask(text: str) -> str:
        start = text.find("static void fault_report(")
        if start < 0:
            return text
        line = "    panic_mask_all_irqs();\n"
        index = text.find(line, start)
        if index < 0:
            return text
        return text[:index] + text[index + len(line):]

    with patched(PANIC_STORE_C, drop_fault_mask):
        result = create_project("verify")
        problems.append(expect_failure(result, "fault_report"))

    # ③ BASEPRI 那行还在、但实参改成 0：符号没变，语义全没了。
    #    只删调用能测到"有没有屏蔽"，测不到"屏蔽是否为空"——这一条专门守后者。
    with patched(
        PANIC_STORE_C,
        lambda t: t.replace(
            "__set_BASEPRI(configMAX_SYSCALL_INTERRUPT_PRIORITY);",
            "__set_BASEPRI(0U);",
            1,
        ),
    ):
        problems.append(expect_failure(create_project("verify"), "BASEPRI 写成"))

    # ④ 清 SysTick/PendSV 挂起位那一行删掉：已经 latched 的切换会在播报中补执行。
    with patched(
        PANIC_STORE_C,
        lambda t: t.replace(
            "    SCB->ICSR = SCB_ICSR_PENDSVCLR_Msk | SCB_ICSR_PENDSTCLR_Msk;\n",
            "",
            1,
        ),
    ):
        problems.append(expect_failure(create_project("verify"), "挂起位"))

    # ⑤ ICER/ICPR 循环上界退回裸数字 8：第 4..7 字是保留位，写了不生效，
    #    却把"真实上限是 3 个字"藏进一个更大的数字里。
    with patched(
        PANIC_STORE_C,
        lambda t: t.replace("index < PANIC_NVIC_WORDS", "index < 8U", 1),
    ):
        problems.append(
            expect_failure(create_project("verify"), "裸数字当 ICER/ICPR 循环上界")
        )

    if before != digest(PANIC_STORE_C):
        return "ERROR", "freertos_port_st.c 没有还原到原始字节"
    for problem in problems:
        if problem:
            return "FAIL", problem
    return "PASS", "删掉 report 入口 / fault_report 预写前的关中断 / BASEPRI 写成 0 / 删掉清 pending / ICER 上界退回 8 后，verify 都报错，已还原"


def case_panic_code() -> tuple[str, str]:
    """panic 码越界与 panic 入口归属：四处都要在破坏态下变红。

    1) 把钳位改成 `shown = raw`——符号（shown / raw）全在，只有语义没了。
       这条专门证明断言不是"看见某个标识符就放行"。
    2) 把 main.c 的 `fml_panic(4U)` 改成 `(9U)`——unsigned 传 enum 在 C 里静默截断，
       编译、链接、size 全都照旧通过，只有断言能拦。三灯会把 9 播成 1（撞"调度器起不来"）。
    3) 把 `fml_panic` 的定义塞回 `fml_led.c`——名字带 led 的停机入口又住进 LED 模块。
       这个定义本身合法、符号也在，构建与产物断言一声不吭，只有归属判据能红。
    4) 把 `fml_button.c` 的 `#include "fml_panic.h"` 改回 `fml_led.h`——按键模块
       为一行报码 2 而依赖 LED 模块，这正是 A5 的原状；同样不影响构建。
    三、四两段只跑 verify：源码改动不参与编译，产物断言读的是既有 elf。
    """
    before_store = digest(PANIC_STORE_C)
    before_main = digest(MAIN_C)
    before_led = digest(FML_LED_C)
    before_button = digest(FML_BUTTON_C)
    problems: list[str] = []

    with patched(
        PANIC_STORE_C,
        lambda t: t.replace(
            "    shown = (raw > (uint32_t)OSL_PANIC_STACK_OVERFLOW) ? (uint32_t)OSL_PANIC_STACK_OVERFLOW : raw;\n",
            "    shown = raw;\n",
            1,
        ),
    ):
        problems.append(expect_failure(create_project("verify"), "没有引用 OSL_PANIC_STACK_OVERFLOW"))

    with patched(MAIN_C, lambda t: t.replace("fml_panic(4U)", "fml_panic(9U)", 1)):
        problems.append(expect_failure(create_project("verify"), "超出 0..7"))

    def panic_back_to_led(text: str) -> str:
        # 锚点必须带函数签名的整两行：`void fml_led_blink_start(void)` 在文件里只出现一次，
        # 而 `void fml_panic(` 在 fml_panic.c 里也有一处——用短锚点会打错文件。
        anchor = "void fml_led_blink_start(void)\n{\n"
        if anchor not in text:
            raise RuntimeError("fml_led.c 里没有 fml_led_blink_start 的锚点，破坏前提不成立")
        body = (
            "void fml_panic(unsigned code)\n"
            "{\n"
            "    osl_panic_report((osl_panic_code_t)code);\n"
            "}\n"
            "\n"
        )
        return text.replace(anchor, body + anchor, 1)

    with patched(FML_LED_C, panic_back_to_led):
        problems.append(expect_failure(create_project("verify"), "里又定义了 fml_panic"))

    def button_back_to_led(text: str) -> str:
        if '#include "fml_panic.h"' not in text:
            raise RuntimeError("fml_button.c 里没有 fml_panic.h 的 include 锚点，破坏前提不成立")
        return text.replace('#include "fml_panic.h"', '#include "fml_led.h"', 1)

    with patched(FML_BUTTON_C, button_back_to_led):
        problems.append(expect_failure(create_project("verify"), "又 include 了 fml_led.h"))

    if (
        before_store != digest(PANIC_STORE_C)
        or before_main != digest(MAIN_C)
        or before_led != digest(FML_LED_C)
        or before_button != digest(FML_BUTTON_C)
    ):
        return "ERROR", "osl_panic.c / main.c / fml_led.c / fml_button.c 没有全部还原到原始字节"
    for problem in problems:
        if problem:
            return "FAIL", problem
    return "PASS", (
        "钳位改成 shown = raw / 调用点改成 9U / panic 定义塞回 fml_led.c / "
        "按键改回 include fml_led.h 四处都报错，已还原"
    )


def case_observ() -> tuple[str, str]:
    """三条可观测性开关：关掉任何一条、或 tick hook 少了守卫，verify 都必须报错。

    这十一个破坏都不影响构建成功与否：宏置 0 只是让 `vApplicationTickHook` /
    `vApplicationStackOverflowHook` / `vApplicationMallocFailedHook` 连同它们的
    调用方一起被 `--gc-sections` 丢掉，`size` 只会小几十字节。也就是说，
    "少了一条故障证据"在构建、链接、尺寸、产物断言里全是不可见的，只有静态断言能红。
    """
    before_config = digest(FREERTOS_CONFIG_H)
    before_boot = digest(OSL_BOOT_C)
    problems: list[str] = []

    def disable(name: str):
        def transform(text: str) -> str:
            return re.sub(rf"(#define\s+{name}\s+)\S+", r"\g<1>0", text, count=1)
        return transform

    for name in (
        "configUSE_TICK_HOOK",
        "configCHECK_FOR_STACK_OVERFLOW",
        "configUSE_MALLOC_FAILED_HOOK",
    ):
        with patched(FREERTOS_CONFIG_H, disable(name)):
            problems.append(expect_failure(create_project("verify"), name))

    # 宏还是 1，函数也还在，只删掉里面那一行守卫调用：等价于"通道建好了但没人查"。
    with patched(
        OSL_BOOT_C,
        lambda t: t.replace("    stack_guard_check();\n", "", 1),
    ):
        problems.append(expect_failure(create_project("verify"), "stack_guard_check"))

    # 宏还在、函数还在、产物里也有 bl —— 但把「只启动一次」的 if 拆掉，每个 tick
    # 都会重配 IWDG：写 PR / 写 RLR 后各要等约 5 个 LSI 周期，两次跨域同步会吃掉
    # tick 中断约三成算力。产物断言只看有没有 bl，完全不看它的调用条件。
    with patched(
        OSL_BOOT_C,
        lambda t: t.replace(
            "    if (watchdog_started == 0U) {\n"
            "        watchdog_started = 1U;\n"
            "        osl_watchdog_start();\n"
            "    }\n",
            "    osl_watchdog_start();\n",
            1,
        ),
    ):
        problems.append(expect_failure(create_project("verify"), "只启动一次"))

    # ---- 后六段：panic 现场的「代际清理」「代际判据」与「初值补采」 ----
    # g_panic_record 在 .noinit，Reset_Handler 不清它，而 panic 之后板子必然复位。
    # 所以「上一次的 PC / 上一次的任务名 / 上一次的 fault_kind 冒充这一次」这三条
    # 缺陷，在构建、链接、尺寸、产物断言（符号在不在镜像里）里全都不可见 ——
    # 只有下面这几条静态判据能红。
    before_port = digest(PANIC_STORE_C)

    # 第六段：整族清理少一个字段。之所以要逐字段守：漏掉的那条就是旧值，而「漏了
    # 哪条」无法从现场反推 —— 读到的是上一次故障的值，看起来完全正常。
    with patched(
        PANIC_STORE_C,
        lambda t: t.replace("    g_panic_record.fault_pc = 0U;\n", "", 1),
    ):
        problems.append(
            expect_failure(
                create_project("verify"), "panic_clear_fault_scene() 没有清"
            )
        )

    # 第七段：未实现中断的入口不再清整族。它自己不写 fault_kind，不清就继承上一次
    # HardFault 留下的 fault_kind = 1 —— 归因直接指向「硬件框架帧故障」。
    with patched(
        PANIC_STORE_C,
        lambda t: t.replace(
            "    panic_clear_fault_scene();\n"
            "    g_panic_record.unhandled_irq = irq;\n",
            "    g_panic_record.unhandled_irq = irq;\n",
            1,
        ),
    ):
        problems.append(
            expect_failure(
                create_project("verify"), "panic_clear_fault_scene"
            )
        )

    # 第九段：非硬件码不再清故障族。上一次 HardFault 的 fault_kind / fault_pc
    # 会留在这一次的栈溢出 / assert / malloc 失败现场里 —— 读到 code = 7
    # 却看到 fault_kind = 1，归因直接指向「硬件访存故障」。
    with patched(
        PANIC_STORE_C,
        lambda t: t.replace(
            "    if (raw != (uint32_t)OSL_PANIC_HARDWARE_FAULT) {\n"
            "        panic_clear_fault_scene();\n"
            "    }\n",
            "",
            1,
        ),
    ):
        problems.append(
            expect_failure(create_project("verify"), "没有为非硬件码清故障族")
        )

    # 第十段：入口抢在 report 之前写 magic。seq 正是靠 magic 判断「上一轮有没有
    # 记录」，提前写让判据恒真 —— 首次上电的 seq 变成 .noinit 随机值 + 1，
    # 看起来像已经反复复位过几亿次。
    with patched(
        PANIC_STORE_C,
        lambda t: t.replace(
            "    panic_clear_fault_scene();\n"
            "    g_panic_record.fault_kind = kind;\n",
            "    panic_clear_fault_scene();\n"
            "    g_panic_record.magic = PANIC_MAGIC;\n"
            "    g_panic_record.fault_kind = kind;\n",
            1,
        ),
    ):
        problems.append(
            expect_failure(
                create_project("verify"), "在调用 osl_panic_report 之前就写了"
            )
        )

    # 第十一段：去掉「0 改判 UNSET」。heap_4 的 xMinimumEverFreeBytesRemaining
    # 初值就是 0，从未分配过内存时补采得到 0 —— 落盘后读起来像「堆正好抽干」。
    with patched(
        PANIC_STORE_C,
        lambda t: t.replace(
            "    if (heap_min == 0U) {\n"
            "        heap_min = (uint32_t)OSL_HEALTH_UNSET;\n"
            "    }\n",
            "",
            1,
        ),
    ):
        problems.append(
            expect_failure(create_project("verify"), "没有区分「堆未初始化」与「堆已耗尽」")
        )

    # 第八段：堆初值不再补采。码 1（调度器起不来）与码 2（xTaskCreate 失败）都发生在
    # tick hook 与 LED 任务之前，g_heap_free_min 必然还是 osl_health_init() 置的初值；
    # 不补采就把 0xFFFFFFFF 落进 .noinit —— 而它正是文档写给码 2 的根因读数。
    with patched(
        PANIC_STORE_C,
        lambda t: t.replace(
            "    if (g_heap_free_min == (uint32_t)OSL_HEALTH_UNSET) {\n"
            "        heap_min = (uint32_t)xPortGetMinimumEverFreeHeapSize();\n"
            "    } else {\n"
            "        heap_min = g_heap_free_min;\n"
            "    }\n",
            "    heap_min = g_heap_free_min;\n",
            1,
        ),
    ):
        problems.append(
            expect_failure(create_project("verify"), "没有识别 OSL_HEALTH_UNSET")
        )

    # 第十二段：码 7 的第二个入口少清一个字段。report 对码 7 不清任务族（它假定
    # 入口已把身份填好），而这个入口任务名是空的 —— 少清一个就继承 .noinit 里
    # 上一代的值，读到的是「主栈压穿」却显示成上一次溢出的任务名。
    #
    # 锚点必须带 task_name 那一行：`    g_panic_record.task = NULL;` 单独拿出来是个
    # 子串——osl_panic_report 里那句是 8 空格缩进，尾部同样含 4 空格 + 该语句，
    # replace 会先命中它，删掉的是「码 1/2/4 清任务族」那一行，而新入口毫发无损。
    guard_clear = (
        "    g_panic_record.task = NULL;\n"
        "    g_panic_record.task_name[0] = '\\0';\n"
    )
    guard_clear_short = "    g_panic_record.task_name[0] = '\\0';\n"

    def drop_guard_task(text: str) -> str:
        if guard_clear not in text:
            raise RuntimeError("osl_panic.c 里找不到 MSP 守卫的 task 清理段")
        return text.replace(guard_clear, guard_clear_short, 1)

    with patched(PANIC_STORE_C, drop_guard_task):
        problems.append(
            expect_failure(create_project("verify"), "没有同时清 task 与 task_name")
        )

    # 第十三段：MSP 守卫改回裸调 report。这是修复前的原样：源码里没有第二个入口，
    # 清理条件把码 7 归进「入口已填好」那一类，于是中断上下文里的主栈越界会借用
    # 上一代任务的身份。破坏后新入口成了无人调用的外部函数，--gc-sections 会把它
    # 整块丢掉（构建照样成功，只有这条静态判据能红）。
    with patched(
        OSL_BOOT_C,
        lambda t: t.replace(
            "        osl_panic_stack_guard_report();\n",
            "        osl_panic_report(OSL_PANIC_STACK_OVERFLOW);\n",
            1,
        ),
    ):
        problems.append(
            expect_failure(create_project("verify"), "直接调 osl_panic_report(7)")
        )

    if before_config != digest(FREERTOS_CONFIG_H) or before_boot != digest(OSL_BOOT_C):
        return "ERROR", "FreeRTOSConfig.h / osl_boot.c 没有还原到原始字节"
    if before_port != digest(PANIC_STORE_C):
        return "ERROR", "freertos_port_st.c 没有还原到原始字节"
    for problem in problems:
        if problem:
            return "FAIL", problem
    return "PASS", "三条宏各关一次 / 删掉 tick hook 里的 stack_guard_check / 拆掉看门狗幂等守卫 / 少清一个故障字段 / 拆掉未实现中断的整族清理 / 去掉堆初值补采 / 非硬件码不清故障族 / 入口抢写 magic / 去掉 0 改判 UNSET / 码 7 第二入口少清任务族 / MSP 守卫改回裸调 report 共十三处都报错，已还原"


def case_fault() -> tuple[str, str]:
    """未实现中断：startup 改回自旋后，产物断言必须报 Default_Handler 不跳 handler。

    走 make 而不是 `build`：`build` 内部的 `update` 有静态检查，会在编译前就拦下，
    那样就测不到"产物级"那条断言了。这里先编出坏镜像，再让 verify 看产物。

    三段（前两段只跑 verify，几秒内结束；第三段要真编坏镜像）：
      0) 静态源码级 —— fault_monitor_init 少开一个可配置异常（SHCSR 里的 USGFAULTENA），
         MemManage / BusFault / UsageFault 就全部升级成 HardFault，fault_kind 恒为 1。
      1) 静态源码级 —— 删掉「异常号 → IRQn」的减法。ICSR.VECTACTIVE 是异常号
         （16..105 才是外部 IRQ），而 NVIC->ICER[n] 的 n 是 IRQn >> 5，IRQn = 异常号 - 16。
         少了这一步就整体偏 16 位，关掉的是另一个中断源 —— 而真正在拉线的那个仍开着，
         正是这段代码要防的重入没有被挡住。
      2) 产物级 —— startup 的 Default_Handler 改回自旋，要真编出坏镜像才测得到。
    """
    fault_src = PANIC_STORE_C
    before_src = digest(fault_src)
    static_problem = ""
    with patched(fault_src, lambda t: t.replace("irqn = irq - 16U;", "irqn = irq;")):
        static_problem = expect_failure(
            create_project("verify"), "没有把异常号换算成 IRQn"
        )
    if before_src != digest(fault_src):
        return "ERROR", "freertos_port_st.c 没有还原到原始字节"
    if static_problem:
        return "FAIL", f"静态段（IRQn 换算）：{static_problem}"

    # 第二段（静态）：fault_monitor_init 少开一个可配置异常。
    # SHCSR 的三位复位后都是 0，关着的那一刻 MemManage / BusFault / UsageFault 就全部
    # 升级成 HardFault，fault_report 判出来的 fault_kind 恒为 1 —— 而构建、链接、尺寸
    # 全都不变，产物断言（入口在不在镜像里）也全绿。
    def drop_shcsr(text: str) -> str:
        anchor = (
            "SCB->SHCSR |= SCB_SHCSR_MEMFAULTENA_Msk | SCB_SHCSR_BUSFAULTENA_Msk |\n"
            "                  SCB_SHCSR_USGFAULTENA_Msk;"
        )
        if anchor not in text:
            raise RuntimeError(f"{fault_src.name} 里找不到 SHCSR 三使能位锚点")
        return text.replace(
            anchor,
            "SCB->SHCSR |= SCB_SHCSR_MEMFAULTENA_Msk | SCB_SHCSR_BUSFAULTENA_Msk;",
            1,
        )

    with patched(fault_src, drop_shcsr):
        shcsr_problem = expect_failure(
            create_project("verify"), "没有置 SCB_SHCSR_USGFAULTENA_Msk"
        )
    if before_src != digest(fault_src):
        return "ERROR", "freertos_port_st.c 没有还原到原始字节"
    if shcsr_problem:
        return "FAIL", f"静态段（SHCSR 三使能位）：{shcsr_problem}"

    # 第三段（静态）：ICER/ICPR 的上界算式把系统异常也加了进去。
    # `(((16U) + (90U) + 31U) / 32U)` 实算 4 个字，而注释与规格都写「3 个字」。
    # 多出来那个字是保留位：不报错、不生效，只让「真实上限」藏在一个更大的数字里。
    # 旧断言只查 `PANIC_NVIC_WORDS` 这个名字在不在函数体 —— 算式改了它照样绿。
    def add_system_irqs(text: str) -> str:
        anchor = "#define PANIC_NVIC_WORDS (((PANIC_EXTI_IRQ_COUNT) + 31U) / 32U)"
        if anchor not in text:
            raise RuntimeError(f"{fault_src.name} 里找不到 PANIC_NVIC_WORDS 定义")
        return text.replace(
            anchor,
            "#define PANIC_NVIC_WORDS (((16U) + (PANIC_EXTI_IRQ_COUNT) + 31U) / 32U)",
            1,
        )

    with patched(fault_src, add_system_irqs):
        words_problem = expect_failure(
            create_project("verify"), "把系统异常（16）也算进了上界"
        )
    if before_src != digest(fault_src):
        return "ERROR", "freertos_port_st.c 没有还原到原始字节"
    if words_problem:
        return "FAIL", f"静态段（ICER 上界算式）：{words_problem}"

    make = find_make()
    if make is None:
        return "SKIP", "找不到 make"
    env = make_env(make)
    shell = msys_shell(make)
    before = digest(STARTUP_S)

    def make_all() -> subprocess.CompletedProcess:
        args = [str(make), "-C", str(ROOT), "-j2", "all"]
        if shell:
            args.append(f"SHELL={shell}")
        return run(args, env=env)

    with patched(STARTUP_S, lambda t: t.replace(
        "  b  UnhandledIRQ_Handler", "  b  Infinite_Loop"
    )):
        built = make_all()
        problem = ""
        if built.returncode != 0:
            # 不要在这里直接 return：那会把半成品镜像留在工作区，
            # 后续组（noinit / write / record-size / iwdg-str）全部连着挂。
            problem = f"破坏后 make 失败（{built.returncode}）：\n{(built.stdout + built.stderr)[-400:]}"
        else:
            checked = create_project("verify", "--artifacts")
            problem = expect_failure(checked, "Default_Handler")
    if before != digest(STARTUP_S):
        return "ERROR", "startup 没有还原到原始字节"
    restore = make_all()
    if problem:
        return "FAIL", problem
    if restore.returncode != 0:
        return "FAIL", "破坏已还原，但恢复 make 失败（镜像可能仍是自旋版本）"
    verify_again = create_project("verify", "--artifacts")
    problem_again = expect_success(verify_again)
    if problem_again:
        return "FAIL", f"恢复后自检仍不通过：{problem_again}"
    return "PASS", "SHCSR 少一位 / 异常号未减 16 / ICER 上界算式加进系统异常 / startup 改回自旋 四处都报错，已还原并重建"


def case_heap_bound() -> tuple[str, str]:
    """链接脚本与产物边界：改了 _Min_Heap_Size 却不重链接，断言必须报不一致。

    这条和 `-u _sbrk` 是两回事：前者管"谁提供 _sbrk"，这里管"堆到底多大"。
    `_Min_Heap_Size` 是布局里的魔数，只有拿 elf 里的 `__heap_start__`/`__heap_end__`
    跟它对比才能确认两者没跑偏；单看任何一方都是绿的。这里故意只改脚本文本、
    不重新链接，于是产物边界仍是旧值，断言必须看出来。
    """
    script = ROOT / "hal" / "chip" / "linker" / "stm32f429igx_flash.ld"
    if not script.is_file():
        return "SKIP", f"找不到 {script.name}"
    # 这一组必须“有产物、且产物是用旧脚本链出来的”。干净检出（CI 的每个 job 都是）
    # 没有 out/<档位>/objects/target/*.elf，若不先构建一次，verify --artifacts 只会因为“没产物”报错，
    # 测到的就不是“边界与脚本不一致”这条断言了。
    built = create_project("build")
    if built.returncode != 0:
        return "FAIL", f"破坏前的基础构建失败，无法测边界断言：\n{(built.stdout + built.stderr)[-400:]}"
    before = digest(script)

    def bump(text: str) -> str:
        m = re.search(r"_Min_Heap_Size\s*=\s*(0x[0-9a-fA-F]+)\s*;", text)
        if not m:
            raise RuntimeError("链接脚本里没有 _Min_Heap_Size 声明")
        value = int(m.group(1), 16)
        return text.replace(m.group(0), f"_Min_Heap_Size = 0x{value * 2:X};", 1)

    with patched(script, bump):
        result = create_project("verify", "--artifacts")
        problem = expect_failure(result, "已不一致")
    if before != digest(script):
        return "ERROR", f"{script.name} 没有还原到原始字节"
    if problem:
        return "FAIL", problem
    # 脚本 mtime 变了，make 会重链接一次；这里主动重建，免得留下"文本已改回、
    # 产物却按旧脚本链接"的中间状态。
    restore = create_project("build")
    if restore.returncode != 0:
        return "FAIL", "破坏已还原，但恢复构建失败（产物可能仍按旧布局链接）"
    return "PASS", "把 _Min_Heap_Size 改大而不重链接后边界断言报错，已还原并重建"


def case_noinit() -> tuple[str, str]:
    """跨复位记录：删掉 section(".noinit") 属性后，产物断言必须报“不在 .noinit 段内”。

    这是本工程最贵的静默失效：record 退回 .bss 后，Reset_Handler 每次复位都把它清零，
    “上一次为什么 panic”永远读不到，而代码看起来完全正常。源码里有属性不代表布局对，
    只有按 elf 里的 _snoinit / _enoinit 边界判才作数。
    """
    if not PANIC_STORE_C.is_file():
        return "SKIP", f"找不到 {PANIC_STORE_C.name}"
    before = digest(PANIC_STORE_C)
    needle = '__attribute__((section(".noinit")))'

    def drop(text: str) -> str:
        if needle not in text:
            raise RuntimeError('freertos_port_st.c 里找不到 section(".noinit") 属性')
        return text.replace(needle, "__attribute__((used))", 1)

    with patched(PANIC_STORE_C, drop):
        result = create_project("build")
        problem = expect_failure(result, "不在 .noinit 段")
    if before != digest(PANIC_STORE_C):
        return "ERROR", f"{PANIC_STORE_C.name} 没有还原到原始字节"
    if problem:
        return "FAIL", problem
    restore = create_project("build")
    if restore.returncode != 0:
        return "FAIL", "破坏已还原，但恢复构建失败（产物可能仍是 .bss 版本）"
    return "PASS", "去掉 .noinit 属性后产物断言报错，已还原并重建"


def case_write() -> tuple[str, str]:
    """_write 保留：从 LDFLAGS 去掉 `-Wl,-u,_write` 后，产物断言必须报“没有 _write”。

    走 make 而不是 build：`build` 内部的 `update` 会先跑 toolchain 静态检查
    （它也查 `-Wl,-u,_write`），那样就测不到产物级那条断言了 —— 与 case_fault 同理。
    改 toolchain.make 会让所有对象过期，所以这一步会全量重编，耗时明显长于其它组。
    """
    make = find_make()
    if make is None:
        return "SKIP", "找不到 make"
    text = TOOLCHAIN.read_text(encoding="utf-8", errors="replace")
    if "-Wl,-u,_write" not in text:
        return "SKIP", "toolchain.make 里本来就没有 -Wl,-u,_write"
    env = make_env(make)
    shell = msys_shell(make)
    before = digest(TOOLCHAIN)

    with patched(TOOLCHAIN, lambda t: t.replace(" -Wl,-u,_write", "", 1)):
        # 串行 make：改 toolchain.make 会让所有对象过期，这是一次必然的全量重编。
        args = [str(make), "-C", str(ROOT), "all"]
        if shell:
            args.append(f"SHELL={shell}")
        built = run(args, env=env)
        problem = ""
        if built.returncode != 0:
            problem = f"去掉 -u _write 后 make 失败：\n{(built.stdout + built.stderr)[-400:]}"
        else:
            checked = create_project("verify", "--artifacts")
            problem = expect_failure(checked, "没有 _write")
    if before != digest(TOOLCHAIN):
        return "ERROR", "toolchain.make 没有还原到原始字节"
    restore = create_project("build")
    if problem:
        return "FAIL", problem
    if restore.returncode != 0:
        return "FAIL", "破坏已还原，但恢复构建失败（产物可能仍缺 _write）"
    return "PASS", "去掉 -Wl,-u,_write 后产物断言报错，已还原并重建"


def case_record_size() -> tuple[str, str]:
    """panic 记录尺寸：把四个上下文字段（ipsr / reset_csr / wd_sync_fail / reset_cause）
    连同各自那行写入一起删掉后，产物断言必须报尺寸不足。

    尺寸是“三个上下文字段真的编进结构体”的唯一产物级证据。只删字段不改断言时，
    构建照样成功，调试器读 `g_panic_record.wd_sync_fail` 拿到的就是下一段内存。

    破坏幅度必须让尺寸真的跌破下限（`PANIC_RECORD_MIN_BYTES = 0x80`）：结构体现在是
    0x8C（140 B），删 1 个 4 B 字段只掉到 0x88（136 B），仍在 128 之上，断言一声不吭。
    这正是“下限判据”的脆弱面 —— 它只看“够不够大”，不看“字段在不在”，实现一涨就把
    余量吃掉、破坏点随即失效（本轮 CI 就是这样红的）。删满 4 个共 16 B → 124 B，
    才真正跌破 128。

    选字段的标准有三条：① 必须 4 B；② **不能被其它静态断言点名**；
    ③ **删掉后仍要能编译**。`wd_sync_fail` / `heap_free_min` 从那轮起被
    `collect_observability_errors` 与 `collect_panic_scene_errors` 盯着（后者要求堆的
    落盘行在场），拿它们当破坏点会让静态断言在 `build` 的 `update` 阶段先报红 ——
    那时还没编到链接，尺寸判据根本没机会跑，自检看到的是「退出码 1 但关键字不命中」，
    把一条真实缺陷报成假红。`assert_line` 也不行：删掉写入后 `vAssertFailed` 的
    `line` 形参变成未使用，`-Werror=unused-parameter` 直接把 `build` 打成退出码 2。
    `reset_cause` 可以：它唯一的消费者是非 static 全局变量 `g_reset_cause`，删掉
    消费点不产生任何告警。
    """
    if not PANIC_STORE_C.is_file() or not PANIC_RECORD_H.is_file():
        return "SKIP", f"找不到 {PANIC_STORE_C.name} 或 {PANIC_RECORD_H.name}"
    before_store = digest(PANIC_STORE_C)
    before_record = digest(PANIC_RECORD_H)
    fields = ("ipsr", "reset_csr", "wd_sync_fail", "reset_cause")

    def drop_fields(text: str) -> str:
        for name in fields:
            text, n_field = re.subn(
                rf"^[ \t]*uint32_t {name};.*\n", "", text, count=1, flags=re.MULTILINE
            )
            if n_field != 1:
                raise RuntimeError(
                    f"{PANIC_RECORD_H.name} 里找不到 {name} 字段（field={n_field}）"
                )
        return text

    def drop_writes(text: str) -> str:
        for name in fields:
            text, n_write = re.subn(
                rf"^[ \t]*g_panic_record\.{name} = .*\n", "", text, count=1, flags=re.MULTILINE
            )
            if n_write != 1:
                raise RuntimeError(
                    f"{PANIC_STORE_C.name} 里找不到 {name} 的写入（write={n_write}）"
                )
        return text

    with patched(PANIC_RECORD_H, drop_fields):
        with patched(PANIC_STORE_C, drop_writes):
            result = create_project("build")
            problem = expect_failure(result, "g_panic_record 只有")
    if before_store != digest(PANIC_STORE_C) or before_record != digest(PANIC_RECORD_H):
        return "ERROR", f"{PANIC_STORE_C.name} / {PANIC_RECORD_H.name} 没有还原到原始字节"
    if problem:
        return "FAIL", problem
    restore = create_project("build")
    if restore.returncode != 0:
        return "FAIL", "破坏已还原，但恢复构建失败（record 可能仍是 124 B）"
    return "PASS", "删掉四个上下文字段后产物断言报尺寸不足，已还原并重建"


def case_iwdg_str() -> tuple[str, str]:
    """看门狗同步等待：把失败计数删掉后，产物断言必须报“没有任何 str”。

    只写 while 循环时成功与超时共用一条 `bx lr`，LSI 没起振、PR/RLR 没生效这件事
    在镜像里完全不可见；这条断言就是盯它。
    """
    source = ROOT / "osl" / "src" / "osl_boot.c"
    if not source.is_file():
        return "SKIP", f"找不到 {source.name}"
    before = digest(source)

    def drop(text: str) -> str:
        return re.sub(r"g_iwdg_sync_fail\+\+;", "/* gate-selftest: removed */", text, count=1)

    with patched(source, drop):
        result = create_project("build")
        problem = expect_failure(result, "没有任何 str")
    if before != digest(source):
        return "ERROR", f"{source.name} 没有还原到原始字节"
    if problem:
        return "FAIL", problem
    restore = create_project("build")
    if restore.returncode != 0:
        return "FAIL", "破坏已还原，但恢复构建失败（iwdg_wait_sync 可能仍是单出口）"
    return "PASS", "删掉 g_iwdg_sync_fail++ 后产物断言报无 str，已还原并重建"


def case_health() -> tuple[str, str]:
    """堆/栈水位可观测：把栈采样入口的调用删掉后，产物断言必须报“不可观测”。

    这条盯的是“配置开着但镜像里什么都没有”这一类静默失效：
    `INCLUDE_uxTaskGetStackHighWaterMark=1` 只让函数可编，
    一旦没人引用，`--gc-sections` 就把 `uxTaskGetStackHighWaterMark` 连同
    记录变量一起丢掉，而构建、链接、产物尺寸都不会有任何异常。
    """
    # 子判据 1（静态）：上层不得直接引用采样变量。堆水位的唯一采样写者是
    # osl_boot.c 的 tick hook；fml 里再补一个采样点，两个写者就各自维护自己的下界
    # （g_panic_record 六入口各写各写那种 M3 形态）。纯静态，不依赖产物。
    led = ROOT / "fml" / "src" / "fml_led.c"
    if not led.is_file():
        return "SKIP", "找不到 fml_led.c"
    led_before = digest(led)
    with patched(led, lambda t: t.replace(
        "        osl_health_stack_mark(OSL_HEALTH_SLOT_LED);",
        "        g_heap_free_min = 0U;\n        osl_health_stack_mark(OSL_HEALTH_SLOT_LED);",
        1,
    )):
        problem = expect_failure(create_project("verify"), "直接引用了 g_heap_free_min")
    if led_before != digest(led):
        return "ERROR", "fml_led.c 没有还原到原始字节"
    if problem:
        return "FAIL", "fml 里直接写采样变量后没有报错：" + problem

    source = ROOT / "osl" / "src" / "osl_health.c"
    if not source.is_file():
        return "SKIP", f"找不到 {source.name}"
    before = digest(source)

    def drop(text: str) -> str:
        call = "    watermark = uxTaskGetStackHighWaterMark(NULL);"
        if call not in text:
            raise RuntimeError("osl_health.c 里找不到 uxTaskGetStackHighWaterMark 的调用")
        return text.replace(call, "    watermark = 0U;", 1)

    with patched(source, drop):
        result = create_project("build")
        problem = expect_failure(result, "没有 uxTaskGetStackHighWaterMark")
    if before != digest(source):
        return "ERROR", f"{source.name} 没有还原到原始字节"
    if problem:
        return "FAIL", problem
    restore = create_project("build")
    if restore.returncode != 0:
        return "FAIL", "破坏已还原，但恢复构建失败（栈水位可能仍是死的）"
    return "PASS", "删掉栈采样调用后产物断言报水位不可观测，已还原并重建"


def case_period() -> tuple[str, str]:
    """周期基准：三处破坏都必须让 verify 报错。

    ① 把 vTaskDelayUntil 换回 vTaskDelay；
    ② 保留调用名、只把基准赋值挪进循环——语义等价于 ①，正是这条断言的假绿通道
       （上一版只判调用名时，这样改完实测仍然"自检通过"）；
    ③ 第二个周期任务（fml_button_task）换回 vTaskDelay——断言以前只读 fml_led.c。

    纯静态（不编译、不要产物）：这组存在的理由正是"改回去不改变任何产物"，
    符号照样在、尺寸几乎不变，只有时间基准悄悄变差。
    """
    before_led = digest(FML_LED_C)
    before_button = digest(FML_BUTTON_C)
    problems: list[str] = []

    with patched(
        FML_LED_C,
        lambda t: t.replace("vTaskDelayUntil(&last_wake,", "vTaskDelay(", 1),
    ):
        problems.append(expect_failure(create_project("verify"), "vTaskDelayUntil"))

    with patched(
        FML_LED_C,
        lambda t: t.replace(
            "    last_wake = xTaskGetTickCount();\n    for (;;) {\n",
            "    for (;;) {\n        last_wake = xTaskGetTickCount();\n",
            1,
        ),
    ):
        problems.append(expect_failure(create_project("verify"), "放在了循环内"))

    with patched(
        FML_BUTTON_C,
        lambda t: t.replace("vTaskDelayUntil(&last_wake,", "vTaskDelay(", 1),
    ):
        problems.append(expect_failure(create_project("verify"), "fml_button_task"))

    if before_led != digest(FML_LED_C) or before_button != digest(FML_BUTTON_C):
        return "ERROR", "fml_led.c / fml_button.c 没有还原到原始字节"
    for problem in problems:
        if problem:
            return "FAIL", problem
    return "PASS", "换回 vTaskDelay / 基准挪进循环 / button 换回 vTaskDelay 三处都报错，已还原"


def case_task_priority() -> tuple[str, str]:
    """任务优先级：配置比用法宽、或用法越过配置，两个方向都必须报错。

    纯静态（不编译、不要产物）。这条盯的是 `pxReadyTasksLists[]` 的隐性白占：
    `configMAX_PRIORITIES` 7 → 2 实测省 100 B bss，而 `size` 只报 bss 总量，
    从数字上看不出哪 100 B 是永远挂不上任务的链表头。
    """
    before_config = digest(FREERTOS_CONFIG_H)
    before_button = digest(FML_BUTTON_C)
    problems: list[str] = []

    def widen(text: str) -> str:
        lines = text.split("\n")
        for index, line in enumerate(lines):
            if line.strip().startswith("#define") and "configMAX_PRIORITIES" in line:
                lines[index] = line.replace(line.split()[-1], "7")
        return "\n".join(lines)

    # ① 配置放宽到 7，用法仍是 1 → 必须报"纯白占"
    with patched(FREERTOS_CONFIG_H, widen):
        problems.append(expect_failure(create_project("verify"), "纯白占"))

    # ② 把 button 的优先级提到配置之外（2）→ 必须报"超出上界"
    with patched(
        FML_BUTTON_C,
        lambda t: t.replace("tskIDLE_PRIORITY + 1U", "tskIDLE_PRIORITY + 2U", 1),
    ):
        problems.append(expect_failure(create_project("verify"), "超出上界"))

    # ③ configMAX_TASK_NAME_LEN 放宽到 16 → 报"白占"。
    #    它决定 TCB_t.pcTaskName[] 的长度，每个任务一份；收窄之后省下的字节落在
    #    FreeRTOS 堆里（TCB 是 pvPortMalloc 的），`size` 的 bss 根本不反映它。
    with patched(FREERTOS_CONFIG_H, lambda t: re.sub(
        r"(#define\s+configMAX_TASK_NAME_LEN\s+)\d+", r"\g<1>16", t, count=1
    )):
        problems.append(expect_failure(create_project("verify"), "configMAX_TASK_NAME_LEN"))

    # ④ 收到 8（"led_blink" 是 9 字符）→ 名字被截断，报"被截断"。
    with patched(FREERTOS_CONFIG_H, lambda t: re.sub(
        r"(#define\s+configMAX_TASK_NAME_LEN\s+)\d+", r"\g<1>8", t, count=1
    )):
        problems.append(expect_failure(create_project("verify"), "被截断"))

    if before_config != digest(FREERTOS_CONFIG_H) or before_button != digest(FML_BUTTON_C):
        return "ERROR", "FreeRTOSConfig.h / fml_button.c 没有还原到原始字节"
    for problem in problems:
        if problem:
            return "FAIL", problem
    return "PASS", "优先级放宽到 7 / 越界到 2 / 任务名上限放宽到 16 / 收到 8 四处都报错，已还原"


def case_panic_ctx() -> tuple[str, str]:
    """panic 现场的两个"上下文"缺口：谁失败的、SP 判据从哪来。

    1) 删掉 `vApplicationMallocFailedHook` 里取任务名的整块（`pcTaskGetName` 在工程里
       只出现在那几行）。删掉之后构建、链接、`size` 全都不变，码 2 的现场又退回
       "只说堆不够、说不出哪个任务"。同时这条要证明断言不是"看见 helper 名就放行"：
       剩下的屏蔽、`g_panic_record.magic`、`code` 都还在。
    2) 把 `fault_report` 的 `_estack` 换回写死地址：判据与链接脚本脱钩。
       注意 `_sdata` 那半句仍然在，所以断言必须分别检查两个符号才抓得住。
    3) 删掉 `vAssertFailed`（码 3）里取任务名的整块：码 2 修了、码 3 忘了，是同一类
       半修。判据只认 `OSL_PANIC_ASSERT` 这一个出口，所以不会误伤码 2 那一段。
    纯静态（不编译、不要产物）。
    """
    before = digest(PANIC_STORE_C)
    problems: list[str] = []

    def drop_malloc_name(text: str) -> str:
        """删掉 malloc 钩子取任务名的整个 if/else（含本轮新增的「拿不到就清空」分支）。

        用正则而不是固定字符串：块尾不再是 `}` + 下一句 —— else 分支让同一段
        `panic_copy_task_name(...)` 在工程里出现两次，只给固定锚点会落错函数。
        `.*?` 配 `re.S` 非贪婪止于第一个「4 空格缩进的 `}` 加换行」；`} else {` 不
        满足该形态，所以会跨过它直到整块末尾。连带删掉整块才能保持花括号配平，
        否则 `_function_body()` 返回 None，报的是「找不到函数体」而不是真正的缺口。
        """
        pattern = (
            r"    current = xTaskGetCurrentTaskHandle\(\);\n"
            r"    if \(current != NULL\) \{\n"
            r".*?"
            r"    \}\n"
            r"(?:    else \{\n.*?    \}\n)?"
        )
        text, count = re.subn(pattern, "", text, count=1, flags=re.S)
        if count != 1:
            raise RuntimeError("freertos_port_st.c 里找不到 malloc 钩子的取名块")
        return text

    with patched(PANIC_STORE_C, drop_malloc_name):
        problems.append(expect_failure(create_project("verify"), "pcTaskGetName"))

    with patched(
        PANIC_STORE_C,
        lambda t: t.replace(
            "sp <= (uint32_t)(uintptr_t)_estack - PANIC_FRAME_MAX_BYTES",
            "sp <= 0x20030000U - PANIC_FRAME_MAX_BYTES",
            1,
        ),
    ):
        problems.append(expect_failure(create_project("verify"), "写死"))

    # 码 3 那一段：`OSL_PANIC_ASSERT` 是 vAssertFailed 唯一的出口，所以这个锚点不会
    # 误伤刚刚上面那段（那段以 OSL_PANIC_CREATE_FAILED 收尾）。
    # 两条约束（都是实测踩出来的）：
    # ① 锚点必须带 `OSL_PANIC_ASSERT` 上下文才唯一 —— 同一行 `panic_copy_task_name(...)`
    #    在 vApplicationMallocFailedHook 里也有一份，只给那一行会让 replace(..., 1)
    #    删掉 malloc 钩子那份，破坏落错函数，needle 自然不命中；
    # ② 替换后必须保持语法配平 —— 连带删掉 if 的 `}` 会让函数体花括号不归零，
    #    `_function_body()` 返回 None，断言报的是"找不到 vAssertFailed 的函数体"，
    #    而不是"没有调用 panic_copy_task_name()"。
    def drop_assert_name(text: str) -> str:
        """只删 vAssertFailed 里那一行取名，保留 if / else 骨架（花括号必须配平）。

        按 `OSL_PANIC_ASSERT` 这个出口定位，再往前找最近的一处取名行 —— 固定锚点
        会落到 malloc 钩子那份（两处文本完全相同），破坏落错函数、needle 不命中。
        """
        anchor = "    osl_panic_report(OSL_PANIC_ASSERT);\n"
        idx = text.find(anchor)
        if idx < 0:
            raise RuntimeError("freertos_port_st.c 里找不到 vAssertFailed 的出口")
        target = (
            "        panic_copy_task_name(pcTaskGetName(current), "
            "(uint32_t)configMAX_TASK_NAME_LEN);\n"
        )
        pos = text[:idx].rfind(target)
        if pos < 0:
            raise RuntimeError("freertos_port_st.c 里找不到 vAssertFailed 的取名行")
        return text[:pos] + text[pos + len(target) :]

    with patched(PANIC_STORE_C, drop_assert_name):
        problems.append(expect_failure(create_project("verify"), "panic_copy_task_name"))

    if before != digest(PANIC_STORE_C):
        return "ERROR", "freertos_port_st.c 没有还原到原始字节"
    for problem in problems:
        if problem:
            return "FAIL", problem
    return "PASS", "删掉 malloc 钩子的取名 / 删掉码 3 的取名 / 把 _estack 换回写死地址，三处都报错，已还原"


def case_panic_beep() -> tuple[str, str]:
    """panic 期间蜂鸣器静音：删掉 GPIOI->BSRR 那一行后，静态断言必须报红。

    PI11 经 1 kΩ 驱动 SS8050、高电平导通。按键任务按住不放时 board_beep_set(1)
    早已把它拉高，而 panic 播报要持续到 IWDG 复位（relax 后 32.7 s）。此时中断已屏蔽、
    任务已停，没有任何调用方还能把它关掉——这一行是唯一能让板子安静下来的动作，
    删掉它不产生任何编译、链接或尺寸告警。纯静态（不编译、不要产物）。
    """
    before = digest(PANIC_STORE_C)
    with patched(
        PANIC_STORE_C,
        lambda t: t.replace(
            "    GPIOI->BSRR = (uint32_t)GPIO_PIN_11 << 16U; /* 置复位位 = 输出低 = 静音 */\n",
            "",
            1,
        ),
    ):
        problem = expect_failure(create_project("verify"), "没有把蜂鸣器")
    if before != digest(PANIC_STORE_C):
        return "ERROR", "freertos_port_st.c 没有还原到原始字节"
    if problem:
        return "FAIL", problem
    return "PASS", "删掉 GPIOI->BSRR 静音后 verify 报错，已还原"


def case_persist_record() -> tuple[str, str]:
    """跨复位保留：去掉 g_last_write 的 .noinit 属性后，产物断言必须报“落在清零区间”。

    noinit 组守的是 g_panic_record，这一条守 _write 的 64 B 输出缓冲。两者静默失效的
    形态完全一样：段字母都是 B（NOLOAD 段也被标成 B），源码里属性写着也看不出来，
    只有按 elf 里的 _sbss/_ebss 地址区间判才作数。属性影响内存布局，所以必须重编。
    """
    if not PANIC_STORE_C.is_file():
        return "SKIP", f"找不到 {PANIC_STORE_C.name}"
    before = digest(PANIC_STORE_C)
    needle = 'volatile char g_last_write[64] __attribute__((section(".noinit")));'

    def drop(text: str) -> str:
        if needle not in text:
            raise RuntimeError("freertos_port_st.c 里找不到 g_last_write 的 .noinit 属性")
        return text.replace(needle, "volatile char g_last_write[64];", 1)

    with patched(PANIC_STORE_C, drop):
        result = create_project("build")
        problem = expect_failure(result, "落在清零区间")
    if before != digest(PANIC_STORE_C):
        return "ERROR", f"{PANIC_STORE_C.name} 没有还原到原始字节"
    if problem:
        return "FAIL", problem
    restore = create_project("build")
    if restore.returncode != 0:
        return "FAIL", "破坏已还原，但恢复构建失败（产物可能仍是 .bss 版本）"
    return "PASS", "去掉 g_last_write 的 .noinit 后产物断言报错，已还原并重建"


def case_health_slot() -> tuple[str, str]:
    """栈水位槽位表的九段破坏：从槽表本身一直守到记录落盘。

    这九段都只影响"故障读数可不可信"，构建、链接、`size`、产物断言一律照旧通过：

      ---- 槽表本身（4 段）----
      1) 越界分支的 `g_stack_slot_err++` 改成注释 → 回到静默 return，写错槽号等于
         那个任务从此不再采样，而数组里停在初值看起来和"栈很富余"一模一样；
      2) `fml_led.c` 的槽号 0 改成 9 → 采样丢弃（断言要求调用点槽号可静态求值且在界内）；
      3) 槽号退回裸字面量 `0U` → 同一個槽号在头文件、调用点、调试脚本里各写一遍，
         改槽数时必然漏改一处；
      4) `OSL_HEALTH_STACK_SLOTS` 改回 4 → 2 个槽 × 4 B 永远写不进值。

      ---- 采样复位（1 段）----
      5) `osl_health_init` 把 `g_heap_free_min` 的复位从 `OSL_HEALTH_UNSET` 改成 `0U`
         → 两组记录都在 .bss，采样判据是 `now < min`，min 从 0 起步就永远满足不了，
         板上读到的「历史最低」恒为 0，而上面四条全绿。

      ---- 归属（2 段）----
      6) 槽号与任务身份对调（`SLOT_LED` 换成 `SLOT_BUTTON`）→ 旧判据只比集合基数，
         对调后仍是 {0, 1}、基数仍是 2，全绿；调试器读 `[0]` 会认错任务；
      7) 删掉 `SLOT_LED` 的行内任务名注释 → 绑定表一删，第 6 段那条判据自动失效，
         所以连「必须写明所属任务」一起守。

      ---- 记录落盘（2 段）----
      8) `panic_record_t` 少了栈余量字段 → 尺寸下限断言只查「够不够大」，加字段让它
         更大同样通过，永远发现不了「少了该有的字段」；
      9) 字段声明了但没人写 → 复位后同样读到 0。

    纯静态（不编译、不要产物）。
    """
    before_h = digest(OSL_HEALTH_H)
    before_c = digest(OSL_HEALTH_C)
    before_led = digest(FML_LED_C)
    before_port = digest(PANIC_STORE_C)
    problems: list[str] = []

    with patched(
        OSL_HEALTH_C,
        lambda t: t.replace("        g_stack_slot_err++;\n", "        /* g_stack_slot_err++ */\n", 1),
    ):
        problems.append(expect_failure(create_project("verify"), "静默 return"))

    with patched(
        FML_LED_C,
        lambda t: t.replace(
            "osl_health_stack_mark(OSL_HEALTH_SLOT_LED)",
            "osl_health_stack_mark(9U)",
            1,
        ),
    ):
        problems.append(expect_failure(create_project("verify"), "槽号 9 超出"))

    # 槽号退回裸字面量：同一個槽号在头文件、调用点、调试脚本里各写一遍，
    # 改槽数时必然漏改一处。
    with patched(
        FML_LED_C,
        lambda t: t.replace(
            "osl_health_stack_mark(OSL_HEALTH_SLOT_LED)",
            "osl_health_stack_mark(0U)",
            1,
        ),
    ):
        problems.append(expect_failure(create_project("verify"), "裸字面量"))

    with patched(
        OSL_HEALTH_H,
        lambda t: re.sub(
            r"(#define\s+OSL_HEALTH_STACK_SLOTS\s+)\d+", r"\g<1>4", t, count=1
        ),
    ):
        problems.append(expect_failure(create_project("verify"), "永远写不进值"))

    # 第五段：osl_health_init 漏掉一组复位（堆那一路改成写 0，即"bss 本来就是 0，不用写"）。
    # 两个记录都在 .bss，采样判据是 `now < min`；min 从 0 起步就永远满足不了这个条件，
    # 于是板上读到的「历史最低」恒为 0 —— 而构建、size、产物符号、上面四条全绿。
    def zero_heap_min(text: str) -> str:
        anchor = "g_heap_free_min = OSL_HEALTH_UNSET;"
        if anchor not in text:
            raise RuntimeError("osl_health.c 里找不到 g_heap_free_min 的复位语句")
        return text.replace(anchor, "g_heap_free_min = 0U;", 1)

    with patched(OSL_HEALTH_C, zero_heap_min):
        problems.append(expect_failure(create_project("verify"), "没有把两组记录都复位"))

    # 第六段：槽号与任务身份对调。
    # 注释里写着「SLOT_LED 属于 fml_led_blink_task」，但旧判据只比槽号集合的基数：
    # 对调之后 used 仍是 {0, 1}、基数仍是 2，全绿；而调试器读 g_stack_free_min[0]
    # 会以为那是 LED 任务，实际是 button —— 整张表的下标与任务关系错位。
    with patched(
        FML_LED_C,
        lambda t: t.replace(
            "osl_health_stack_mark(OSL_HEALTH_SLOT_LED)",
            "osl_health_stack_mark(OSL_HEALTH_SLOT_BUTTON)",
            1,
        ),
    ):
        problems.append(
            expect_failure(create_project("verify"), "槽号与任务身份对不上")
        )

    # 第七段：把身份绑定从行内注释里删掉 —— 绑定表一删，第六段那条判据就自动失效，
    # 所以要连「必须写明所属任务」一起守。
    def drop_slot_owner(text: str) -> str:
        new, count = re.subn(
            r"(#define OSL_HEALTH_SLOT_LED 0U)\s+/\*\s*fml_led_blink_task\s*\*/",
            r"\1",
            text,
            count=1,
        )
        if count != 1:
            raise RuntimeError("osl_health.h 里找不到 SLOT_LED 的行内任务名")
        return new

    with patched(OSL_HEALTH_H, drop_slot_owner):
        problems.append(
            expect_failure(create_project("verify"), "没有在行内注释里写明所属任务")
        )

    # 第八段：panic_record_t 少了栈余量字段。
    # 尺寸下限断言（g_panic_record ≥ 0x80）只查「够不够大」：加字段让它更大同样通过，
    # 所以它永远发现不了「少了该有的字段」。这条从结构体定义里切出来单独查。
    def drop_stack_fields(text: str) -> str:
        pattern = (
            r"    uint32_t stack_free_min\[OSL_HEALTH_STACK_SLOTS\];[^\n]*\n"
            r"    uint32_t stack_slot_err;[^\n]*\n"
        )
        new, count = re.subn(pattern, "", text, count=1)
        if count != 1:
            raise RuntimeError(f"{PANIC_RECORD_H.name} 里找不到 panic_record_t 的栈水位字段")
        return new

    with patched(PANIC_RECORD_H, drop_stack_fields):
        problems.append(
            expect_failure(create_project("verify"), "没有 stack_free_min 字段")
        )

    # 第九段：字段声明了但没人往里写 —— 复位后同样读到 0。
    def drop_stack_copy(text: str) -> str:
        pattern = (
            r"    for \(slot = 0U; slot < \(uint32_t\)OSL_HEALTH_STACK_SLOTS; slot\+\+\) \{\n"
            r"        g_panic_record\.stack_free_min\[slot\] = g_stack_free_min\[slot\];\n"
            r"    \}\n"
            r"    g_panic_record\.stack_slot_err = g_stack_slot_err;\n"
        )
        new, count = re.subn(pattern, "", text, count=1)
        if count != 1:
            raise RuntimeError("freertos_port_st.c 里找不到栈水位的搬移段")
        return new

    with patched(PANIC_STORE_C, drop_stack_copy):
        problems.append(
            expect_failure(
                create_project("verify"), "没有把 g_panic_record.stack_free_min 落盘"
            )
        )

    if (
        before_h != digest(OSL_HEALTH_H)
        or before_c != digest(OSL_HEALTH_C)
        or before_led != digest(FML_LED_C)
        or before_port != digest(PANIC_STORE_C)
    ):
        return "ERROR", (
            "osl_health.h / osl_health.c / fml_led.c / freertos_port_st.c "
            "没有还原到原始字节"
        )
    for problem in problems:
        if problem:
            return "FAIL", problem
    return "PASS", "越界计数改注释 / 槽号改成 9 / 槽号退回裸字面量 / 槽数改回 4 / 漏一组 UNSET 复位 / 槽号对调 / 删掉身份注释 / 删掉栈余量字段 / 删掉栈余量搬移，九处都报错，已还原"


def case_layout() -> tuple[str, str]:
    """纯头目录不该有自己的构建元数据。

    生成器此前对 `walk_dirs()` 里每个目录都写 Makefile + Kconfig，纯头目录
    （`bsl/inc`、`hal/periph/inc`、`osl/inc/freertos/portable/GCC/ARM_CM4F` 等 8 个）
    拿到的是 `LOCAL_OBJS :=` 空 + 重列 include + `clean` 删一个不存在的目录，以及一条
    `comment "无子目录、无源文件"`。`bsl/` 15 个文件里 6 个是这种生成物。

    这组把 `bsl/inc/Makefile` 凭空造出来（带 `GEN_BANNER`，让「是否是生成物」的前提
    成立），要求 `verify` 报「纯头目录」。注意它不跑 build：判据在源码/目录层，
    与编译产物无关，跑 verify 即可。
    """
    target = ROOT / "bsl" / "inc" / "Makefile"
    if target.exists():
        return "ERROR", f"{target.name} 本来就在 bsl/inc/，破坏前提不成立"
    target.write_text(
        "# 自动生成，勿手改。刷新：python3 build/build.py update\n",
        encoding="utf-8",
        newline="",
    )
    try:
        problem = expect_failure(create_project("verify"), "纯头目录")
    finally:
        if target.exists():
            target.unlink()
    if target.exists():
        return "ERROR", "bsl/inc/Makefile 没有删掉，工作区停在破坏态"
    if problem:
        return "FAIL", problem
    return "PASS", "bsl/inc 凭空多出生成的 Makefile 后被 verify 拦下，文件已删除"


def case_layer_deps() -> tuple[str, str]:
    """跨层 include 越界：apl 直连 hal、bsl 反向 include fml，都必须报。

    这条守的是「apl 只依赖 fml/bsl/osl」在源码里是真话。此前 `main.c` 直接 include
    hal 的 stm32f4xx_hal.h 与 system_stm32f4xx.h 并调 HAL_Init()/SystemClock_Config()，
    而链接顺序（apl 排最前、hal 排最后、--start-group 包裹）让这个越级完全不显形：
    编译、链接、size、产物符号一个都不报。

    三段破坏分别覆盖「下行越级」（apl → hal）、「反向依赖」（bsl → 自己的消费者 fml）、
    「功能层越级」（fml → hal）。第三段是补的：`fml: hal` 这条边原先敞在
    `LAYER_DEPENDENCIES` 里却零使用，fml 可以顺手 include 一个 hal 头而不被拦。
    三段都只跑 verify：源码改动不参与编译，产物断言读的是既有 elf。
    """
    main_c = ROOT / "apl" / "src" / "main.c"
    board_led_c = ROOT / "bsl" / "src" / "board_led.c"
    if not main_c.is_file() or not board_led_c.is_file():
        return "SKIP", "找不到 apl/src/main.c 或 bsl/src/board_led.c"
    main_before = digest(main_c)
    led_before = digest(board_led_c)

    def add_hal_include(text: str) -> str:
        anchor = '#include "board_system.h"\n'
        if anchor not in text:
            raise RuntimeError("main.c 里没有 board_system.h 的 include 锚点，破坏前提不成立")
        return text.replace(anchor, anchor + '#include "stm32f4xx_hal.h"\n', 1)

    with patched(main_c, add_hal_include):
        problem = expect_failure(create_project("verify"), "越级 include")
    if main_before != digest(main_c):
        return "ERROR", "main.c 没有还原到原始字节"
    if problem:
        return "FAIL", problem

    def add_fml_include(text: str) -> str:
        anchor = '#include "board_led.h"\n'
        if anchor not in text:
            raise RuntimeError("board_led.c 里没有 board_led.h 的 include 锚点，破坏前提不成立")
        return text.replace(anchor, anchor + '#include "fml_led.h"\n', 1)

    with patched(board_led_c, add_fml_include):
        reverse = expect_failure(create_project("verify"), "越级 include")
    if led_before != digest(board_led_c):
        return "ERROR", "board_led.c 没有还原到原始字节"
    if reverse:
        return "FAIL", reverse

    fml_led_c = ROOT / "fml" / "src" / "fml_led.c"
    if not fml_led_c.is_file():
        return "SKIP", "找不到 fml/src/fml_led.c"
    fml_before = digest(fml_led_c)

    def add_hal_to_fml(text: str) -> str:
        anchor = '#include "board_led.h"\n'
        if anchor not in text:
            raise RuntimeError("fml_led.c 里没有 board_led.h 的 include 锚点，破坏前提不成立")
        return text.replace(anchor, anchor + '#include "stm32f4xx_hal.h"\n', 1)

    with patched(fml_led_c, add_hal_to_fml):
        fml_hal = expect_failure(create_project("verify"), "越级 include")
    if fml_before != digest(fml_led_c):
        return "ERROR", "fml_led.c 没有还原到原始字节"
    if fml_hal:
        return "FAIL", fml_hal

    # 第四段：尖括号形式。越级判定只认引号，而 `-I` 路径对两种写法都生效 ——
    # 用尖括号引本仓层头可以整段绕过越级判定。这里期望的不是「越级」文案，
    # 而是把它当**写法违规**报出来：尖括号的默认语义是系统头，工程里不会同时
    # 存在两个同名层头，所以引层头必是同一个目标，但判据看不见它。
    angle_before = digest(main_c)

    def add_angle_hal_fixed(text: str) -> str:
        anchor = '#include "board_system.h"\n'
        if anchor not in text:
            raise RuntimeError("main.c 里没有 board_system.h 的 include 锚点，破坏前提不成立")
        return text.replace(anchor, "#include <stm32f4xx_hal.h>\n" + anchor, 1)

    with patched(main_c, add_angle_hal_fixed):
        angle = expect_failure(
            create_project("verify"), "用尖括号引用本仓层头 <stm32f4xx_hal.h>"
        )
    if angle_before != digest(main_c):
        return "ERROR", "main.c 没有还原到原始字节（尖括号段）"
    if angle:
        return "FAIL", angle

    # 第五段：extern 符号级依赖。越级判定只遍历 INCLUDE_QUOTED / INCLUDE_ANGLE，
    # 而手写一句 `extern void fml_xxx(void);` 就能建立同一条依赖边 —— 编译期、
    # 链接期（--start-group 之内）、层判据三处都不响。bsl 的允许依赖只有 hal，
    # 所以 bsl 引 fml 的函数是确定的越级，期望文案要含「用 extern 声明引用」。
    led_extern_before = digest(board_led_c)

    def add_extern_bsl_to_fml(text: str) -> str:
        anchor = "void board_led_init(void)\n"
        if anchor not in text:
            raise RuntimeError("board_led.c 里没有 board_led_init 的锚点，破坏前提不成立")
        return text.replace(
            anchor, "extern void fml_button_start(void);\n\n" + anchor, 1
        )

    with patched(board_led_c, add_extern_bsl_to_fml):
        extern = expect_failure(
            create_project("verify"), "用 extern 声明引用 fml 层符号 fml_button_start"
        )
    if led_extern_before != digest(board_led_c):
        return "ERROR", "board_led.c 没有还原到原始字节（extern 段）"
    if extern:
        return "FAIL", extern
    return (
        "PASS",
        "apl 加回 hal 头、bsl 塞进 fml 头、fml 加 hal 头都报越级 include，"
        "apl 用尖括号引 hal 头报写法违规，bsl 用 extern 引 fml 符号报符号级越级，"
        "源码已按字节还原",
    )


def case_release_size() -> tuple[str, str]:
    """体积上限：把 .text / Flash 上限压到当前产物之下，verify 必须报「超出…上限」。

    下限（`ARTIFACT_TEXT_FLOOR`）守「镜像只剩启动代码」；上限守「悄悄长胖」——
    这块 Flash 有 8 MB，多长 10 KB 在容量比里毫无痛感，只有这条能拦住
    「整包拖进未裁剪的模块」。两段分别压 `.text` 与 Flash 两个上限，破坏点都在
    `build/contracts.py` 的常量上：产物分支读的是既有 elf，所以不必重编，
    但干净检出上必须先构建一次才有 elf。
    """
    ready = ensure_artifacts()
    if ready:
        return "SKIP", ready
    elf = primary_map_file().with_suffix(".elf")
    if not elf.is_file():
        rel = elf.relative_to(ROOT).as_posix()
        return "SKIP", f"缺少 {rel}，体积上限无从校验"
    gen = ROOT / "build" / "contracts.py"
    before = digest(gen)

    def shrink_text_ceil(text: str) -> str:
        anchor = "ARTIFACT_TEXT_CEIL = 16384"
        if anchor not in text:
            raise RuntimeError("找不到 ARTIFACT_TEXT_CEIL = 16384，破坏前提不成立")
        return text.replace(anchor, "ARTIFACT_TEXT_CEIL = 8192", 1)

    with patched(gen, shrink_text_ceil):
        problem = expect_failure(create_project("verify", "--artifacts"), "字节上限")
    if before != digest(gen):
        return "ERROR", f"{gen.name} 没有还原到原始字节"
    if problem:
        return "FAIL", problem

    def shrink_flash_ceil(text: str) -> str:
        anchor = "ARTIFACT_FLASH_CEIL = 65536"
        if anchor not in text:
            raise RuntimeError("找不到 ARTIFACT_FLASH_CEIL = 65536，破坏前提不成立")
        return text.replace(anchor, "ARTIFACT_FLASH_CEIL = 1024", 1)

    with patched(gen, shrink_flash_ceil):
        reverse = expect_failure(create_project("verify", "--artifacts"), "Flash 用量")
    if before != digest(gen):
        return "ERROR", f"{gen.name} 没有还原到原始字节"
    if reverse:
        return "FAIL", reverse
    return "PASS", ".text 上限压到 8192、Flash 上限压到 1024 都报「超出上限」，源码已按字节还原"


def case_line_ending() -> tuple[str, str]:
    """行尾契约：往一个 eol=lf 文件里注入一处 CRLF，verify 必须报出来。

    这条守的是「工作区行尾真的等于规则声明的行尾」，而不是「.gitattributes 里有
    一行 eol=lf」。历史事实：那条规则原来只列了 *.c / *.h / *.py / *.ld，结果同一个
    .skills 目录里 3 个 md 是 CRLF、1 个是 LF；*.s 与 69 个 update 生成的 Kconfig /
    Makefile 的行尾全凭本机 core.autocrlf 决定（生成物更糟：write_text 默认
    newline=None，在 Windows 上就是写 CRLF，光改 .gitattributes 每次 update 都会
    被自己打回去）。而 gate-selftest 改源码、文档回填改 .skills 都按字节锚点 ——
    锚点跟着换机器换行尾静默失配，门禁报「锚点没匹配上」，与真实缺陷无关。

    破坏选 doc/design/README.md：它被 `* text=auto eol=lf` 覆盖，又不参与任何
    编译或其他断言，注入一处 CRLF 只可能命中这一条。
    """
    target = ROOT / "doc" / "design" / "README.md"
    if not target.is_file():
        return "SKIP", "找不到 doc/design/README.md"
    before = digest(target)

    def inject(text: str) -> str:
        if "\r\n" in text:
            raise RuntimeError("README.md 已经不是纯 LF，破坏前提不成立")
        return text.replace("\n", "\r\n", 1)

    # 干净检出（CI 每个 job）没有 out/<档位>/objects/target/*.elf，verify 会先报「找不到 *.elf」。
    # 判定顺序因此固定为：先无条件重建一次拿到产物，再看断言 —— 否则破坏态下 verify
    # 无论断言有没有叫都会非 0，等于什么都没测（n29 组第一次跑就是这么 FAIL 的）。
    baseline = expect_success(create_project("build"))
    if baseline:
        return "FAIL", f"基线构建没有通过：{baseline}"

    with patched(target, inject):
        problem = expect_failure(
            create_project("verify"), "处 CRLF，但 .gitattributes 钉了 eol=lf"
        )
    if before != digest(target):
        return "ERROR", "README.md 没有还原到原始字节"
    if problem:
        return "FAIL", problem

    # 第二段：生成端。光钉 .gitattributes 管不住 write_text —— newline 默认 None，
    # 在 Windows 上写 \n 就是写 \r\n，每次 update 都会把 LF 打回 CRLF。这一段守的是
    # 源码形态（有没有显式 newline 行尾），不是"删掉之后 update 会不会写 CRLF"：
    # Linux 上 os.linesep 就是 \n，删掉 newline 照样写 LF，后者的破坏在 CI 上根本不
    # 成立（实测本地 Windows PASS、CI 报"预期失败，但退出码是 0"）。
    generator = ROOT / "build" / "generate.py"
    gen_before = digest(generator)
    guarded_call = 'path.write_text(text, encoding="utf-8", newline="\\n")'
    bare_call = 'path.write_text(text, encoding="utf-8")'

    def drop_newline(text: str) -> str:
        if guarded_call not in text:
            raise RuntimeError("找不到 write_generated_text 的 newline 锚点，破坏前提不成立")
        return text.replace(guarded_call, bare_call, 1)

    with patched(generator, drop_newline):
        gen_problem = expect_failure(create_project("verify"), "没有显式 newline")
    if gen_before != digest(generator):
        return "ERROR", f"{gen.name} 没有还原到原始字节"
    if gen_problem:
        return "FAIL", gen_problem

    restore_problem = expect_success(create_project("verify"))
    if restore_problem:
        return "FAIL", f"还原后 verify 仍不通过：{restore_problem}"
    return "PASS", "注入一处 CRLF 被行尾断言拦下，README.md 已按字节还原"


def case_hard_gate() -> tuple[str, str]:
    """产物级硬闸：库成员白名单 + tick hook 的调用必须真的在指令流里。

    这组专测"源码断言看不见"的那类破坏：

      1) `stack_guard_check();` 保留、但套进 `if (0U) { ... }`。子串还在，
         `collect_observability_errors` 照样绿 —— 只有反汇编看 bl 才能发现调用永不执行。
         这正是一次行为被悄悄回退时最可能的形态（重构、条件编译、临时调试开关）。

      2) `main()` 里多一次 int64 除法。链接把 `libgcc.a(_aeabi_ldivmod.o)` 拉进来，
         构建完全成功、尺寸只是涨了几十字节，只有 map 的成员白名单能拦下。

    两处破坏都必须先 make 才在产物里可见（verify 读的是 elf/map），所以合成一组、
    一次 make：分开跑要多一次编译，而它们失去的也不是"构建能不能过"这类共同信号。
    """
    make = find_make()
    if make is None:
        return "SKIP", "找不到 make"
    env = make_env(make)
    shell = msys_shell(make)
    before_boot = digest(OSL_BOOT_C)
    before_main = digest(MAIN_C)

    def make_all() -> subprocess.CompletedProcess:
        args = [str(make), "-C", str(ROOT), "-j2", "all"]
        if shell:
            args.append(f"SHELL={shell}")
        return run(args, env=env)

    probe = (
        "    osl_health_init();\n"
        "    {\n"
        "        volatile int64_t probe_a = 1;\n"
        "        volatile int64_t probe_b = 3;\n"
        "        volatile int64_t probe_c = probe_a / probe_b;\n"
        "        (void)probe_c;\n"
        "    }\n"
    )
    problems: list[str] = []
    built = subprocess.CompletedProcess(args=[], returncode=1, stdout="", stderr="")
    with patched(
        OSL_BOOT_C,
        lambda t: t.replace(
            "    stack_guard_check();\n",
            "    if (0U) {\n        stack_guard_check();\n    }\n",
            1,
        ),
    ), patched(MAIN_C, lambda t: t.replace("    osl_health_init();\n", probe, 1)):
        built = make_all()
        if built.returncode != 0:
            # 不要直接 return：半成品镜像会留在工作区，把后面的产物级组连着带坏。
            problems.append(
                f"破坏后 make 失败（{built.returncode}）：\n{(built.stdout + built.stderr)[-400:]}"
            )
        else:
            checked = create_project("verify")
            problems.append(expect_failure(checked, "白名单外的成员"))
            problems.append(expect_failure(checked, "bl stack_guard_check"))

    if before_boot != digest(OSL_BOOT_C) or before_main != digest(MAIN_C):
        return "ERROR", "osl_boot.c / main.c 没有还原到原始字节"
    restore = make_all()
    for problem in problems:
        if problem:
            return "FAIL", problem
    if restore.returncode != 0:
        return "FAIL", "破坏已还原，但恢复 make 失败（产物可能仍是破坏版本）"
    problem_again = expect_success(create_project("verify", "--artifacts"))
    if problem_again:
        return "FAIL", f"恢复后自检仍不通过：{problem_again}"
    return "PASS", "tick hook 套进 if (0U) / main 加 int64 除法两处都报错，已还原并重建"


def case_boot_init() -> tuple[str, str]:
    """HAL_InitTick 会被调多次，所以挂在它上面的初始化必须有幂等守卫。

    HAL_Init() 调一次 HAL_InitTick，HAL_RCC_ClockConfig() 每次改时钟还要在末尾
    再调一次。两处"只做一次"的初始化各有一个守卫，删掉任一个都不会让构建失败：
    只是 panic 记录的 reset_cause 全变成 UNKNOWN、HAL_GetTick 在启动期不再单调。
    正因为静默，必须由产物级断言（看指令流的控制流）抓出来，源码文本级断言抓不到。

    另外守 IWDG 播报档的两件事：PR 编码（重算超时秒数）与 relax() 末尾的启动键
    0xCCCC。后者决定启动期 panic（码 1/2/4）发生时看门狗到底跑不跑 —— 缺了它，
    播报完的关中断自旋永远等不到复位，板上只能断电，而构建、链接、尺寸全都正常。
    """
    problems: list[str] = []
    reset_guard = """    static uint8_t captured;
    uint32_t csr;

    if (captured != 0U) {
        return;
    }
    captured = 1U;
"""

    def drop_reset_guard(text: str) -> str:
        if reset_guard not in text:
            raise RuntimeError("osl_boot.c 里找不到 captured 守卫段")
        return text.replace(reset_guard, "    uint32_t csr;\n", 1)
    time_guard = """    if ((DWT->CTRL & DWT_CTRL_CYCCNTENA_Msk) != 0U) {
        return;
    }
"""
    before_boot = digest(OSL_BOOT_C)
    before_port = digest(PORT_ST_C)
    before_system = digest(BOARD_SYSTEM_C)

    # 破坏态下 build 本来就该非 0：编译照旧通过，是产物断言在拦。所以直接拿
    # build 的输出断言，既不额外跑一次 verify，也能区分"断言生效"与"源码被写坏"
    # （后者输出里不会有这条关键字）。
    with patched(OSL_BOOT_C, drop_reset_guard):
        problems.append(expect_failure(create_project("build"), "缺少条件早退"))

    def drop_time_guard(text: str) -> str:
        if time_guard not in text:
            raise RuntimeError(f"{PORT_ST_C.name} 里找不到 CYCCNTENA 幂等守卫")
        return text.replace(time_guard, "", 1)

    with patched(PORT_ST_C, drop_time_guard):
        problems.append(
            expect_failure(create_project("build"), "CYCCNT 归零前没有")
        )

    # 第三段：IWDG 播报档的 PR 编码写错（DIV256 退成 DIV16 的编码 2）。
    # 这一段不需要产物，跑 verify 的静态判据即可；它守的是"值"而不是"命名"——
    # 命名没改、失败分支还在、iwdg-str 组照样绿，只有真算超时秒数才看得出来。
    def wrong_pr(text: str) -> str:
        anchor = "#define IWDG_PR_DIV256 6UL"
        if anchor not in text:
            raise RuntimeError("osl_boot.c 里找不到 IWDG_PR_DIV256 定义")
        return text.replace(anchor, "#define IWDG_PR_DIV256 2UL", 1)

    with patched(OSL_BOOT_C, wrong_pr):
        problems.append(
            expect_failure(create_project("verify"), "IWDG_PR_DIV256 = 2")
        )

    # 第四段：board_system_init 把 HAL_Init() 的返回值丢掉。
    # HAL_Init 里设的正是 NVIC 优先级分组（NVIC_PRIORITYGROUP_4）；分组没设成时
    # FreeRTOS 的 BASEPRI 屏蔽会按错误的位域工作，整体错位且全程静默
    # （没有 panic、没有码）。而同一函数里 SystemClock_Config 失败会 return 1
    # 让 main 播报码 4 —— 两种失败处理不一致，这条是静默的那一半。
    def drop_hal_status(text: str) -> str:
        anchor = "    if (HAL_Init() != HAL_OK) {\n        return 1;\n    }\n"
        if anchor not in text:
            raise RuntimeError("board_system.c 里找不到 HAL_Init 的返回值检查")
        return text.replace(anchor, "    HAL_Init();\n", 1)

    with patched(BOARD_SYSTEM_C, drop_hal_status):
        problems.append(expect_failure(create_project("verify"), "返回值丢掉"))

    # 第五、六段：relax() 末尾的启动键 0xCCCC。
    #
    # 锚点必须带 IWDG_RLR_RELAXED 那几行：`IWDG->KR = IWDG_KEY_ENABLE;` 在整个文件里
    # 出现两次（start 与 relax 各一次，且两处前面都是 `IWDG_KEY_RELOAD`），
    # 只用那一行做替换会删掉 start 里的、relax 毫发无损 —— 判据照样绿，破坏段假过。
    relax_tail = (
        "    IWDG->RLR = IWDG_RLR_RELAXED;\n"
        "    (void)iwdg_wait_sync();\n"
        "    IWDG->KR = IWDG_KEY_RELOAD;\n"
        "    IWDG->KR = IWDG_KEY_ENABLE;\n"
    )
    relax_tail_short = (
        "    IWDG->RLR = IWDG_RLR_RELAXED;\n"
        "    (void)iwdg_wait_sync();\n"
        "    IWDG->KR = IWDG_KEY_RELOAD;\n"
    )

    # 第五段：干脆不写启动键。启动期 panic（码 1 调度器起不来 / 码 2 启动期建任务
    # 失败 / 码 4 时钟失败）发生时 tick hook 还没跑过，IWDG 从未 ENABLE ——
    # PR/RLR 只是预置寄存器，计数器不跑，播报完的关中断自旋永远等不到复位，
    # 板上表现就是只能断电。构建、链接、尺寸全部正常，只有静态判据能红。
    def drop_enable(text: str) -> str:
        if relax_tail not in text:
            raise RuntimeError("osl_boot.c 里找不到 relax() 末尾的启动键写入")
        return text.replace(relax_tail, relax_tail_short, 1)

    with patched(OSL_BOOT_C, drop_enable):
        problems.append(
            expect_failure(create_project("verify"), "没有写 IWDG_KEY_ENABLE")
        )

    # 第六段：常量还在、写入被优化掉。`(void)IWDG_KEY_ENABLE;` 让静态那条字符串
    # 判据照样通过（"IWDG_KEY_ENABLE" 仍在函数体里），反汇编里却没有 0xCCCC。
    # 这一段同时证明两件事：静态判据挡不住「保留符号、破坏语义」，产物判据不是
    # 静态判据的复读。
    def neutralize_enable(text: str) -> str:
        if relax_tail not in text:
            raise RuntimeError("osl_boot.c 里找不到 relax() 末尾的启动键写入")
        return text.replace(
            relax_tail,
            relax_tail_short + "    (void)IWDG_KEY_ENABLE;\n",
            1,
        )

    with patched(OSL_BOOT_C, neutralize_enable):
        problems.append(
            expect_failure(create_project("build"), "没有 0xCCCC（52428）")
        )

    if (
        before_boot != digest(OSL_BOOT_C)
        or before_port != digest(PORT_ST_C)
        or before_system != digest(BOARD_SYSTEM_C)
    ):
        return "ERROR", "osl_boot.c / freertos_port_st.c / board_system.c 没有还原到原始字节"
    restore = create_project("build")
    for problem in problems:
        if problem:
            return "FAIL", problem
    if restore.returncode != 0:
        return "FAIL", "破坏已还原，但恢复构建失败（产物可能仍是破坏版本）"
    problem_again = expect_success(create_project("verify", "--artifacts"))
    if problem_again:
        return "FAIL", f"恢复后自检仍不通过：{problem_again}"
    return "PASS", "两处幂等守卫各删一次、IWDG 播报档 PR 写错一次、HAL_Init 返回值丢弃一处、relax 启动键删一次 / 改成纯引用一次都报错，已还原并重建"


def case_last_write() -> tuple[str, str]:
    """g_last_write 必须有生产者：把 panic 摘要调用删掉，产物断言必须报"没有向它写入"。

    这条守的不是"符号还在镜像里"（那是 write 组的事），而是"真的有人往里写"。
    破坏时用 `(void)panic_last_write;` 把调用换成只引用符号，这样：
      - -Werror 不会因为 unused-function 把构建拦下来（否则掉进"编译失败"而不是"断言失败"）；
      - 函数体还在、strb 还在，但 osl_panic_report 里不再有 bl 指向它，
        判据的"被调用"那一半就断了 —— 正是要抓的形态。
    """
    if not PANIC_STORE_C.is_file():
        return "SKIP", f"找不到 {PANIC_STORE_C.name}"
    before = digest(PANIC_STORE_C)
    call_line = (
        "    /* 三灯播报之后就再没有机会了（末尾关中断自旋），摘要必须在播报之前落盘。 */\n"
        "    panic_last_write(raw, g_panic_record.seq, g_panic_record.task_name);\n"
    )
    silenced = "    (void)panic_last_write;\n"

    def drop_call(text: str) -> str:
        if call_line not in text:
            raise RuntimeError("freertos_port_st.c 里找不到 panic_last_write 的调用")
        return text.replace(call_line, silenced, 1)

    with patched(PANIC_STORE_C, drop_call):
        problem = expect_failure(create_project("build"), "没有向 g_last_write")
    if before != digest(PANIC_STORE_C):
        return "ERROR", f"{PANIC_STORE_C.name} 没有还原到原始字节"
    if problem:
        return "FAIL", problem
    restore = create_project("build")
    if restore.returncode != 0:
        return "FAIL", "破坏已还原，但恢复构建失败（产物可能仍缺 panic 摘要）"
    problem_again = expect_success(create_project("verify", "--artifacts"))
    if problem_again:
        return "FAIL", f"恢复后自检仍不通过：{problem_again}"
    return "PASS", "删掉 panic 摘要调用后产物断言报错，已还原并重建"


def case_last_write_size() -> tuple[str, str]:
    """g_last_write 的尺寸下限：把 [64] 缩到 [9]，编译期断言必须拦下。

    这条守的不是"有人往里写"（那是 last-write 组的事），而是**缓冲尺寸契约**：
    panic_last_write() 的 9 B 前缀是无条件写，之后还要写 1 B 终止符，所以少于
    10 B 就必然越过数组末尾。而 g_last_write 与 g_panic_record 同处 .noinit、
    紧邻排列 —— 尺寸是 4 的倍数时越界那一字节精确落在 g_panic_record.magic
    （64 B 配置下实测 +0x64，中间没有对齐填充），把这一轮的现场变成"从未发生"。

    破坏手法是"改小尺寸"，判据是**编译失败**而不是产物断言失败：
      - _Static_assert 不产生指令、也不进镜像，不存在"断言没编进 elf 却仍判绿"的假绿；
      - 编译失败本来就是预期结果，不会被 -Werror 混淆成"破坏写坏了源码"。
    """
    if not PANIC_STORE_C.is_file():
        return "SKIP", f"找不到 {PANIC_STORE_C.name}"
    before = digest(PANIC_STORE_C)
    full = 'volatile char g_last_write[64] __attribute__((section(".noinit")));'
    shrunk = 'volatile char g_last_write[9] __attribute__((section(".noinit")));'

    def shrink(text: str) -> str:
        if full not in text:
            raise RuntimeError("freertos_port_st.c 里找不到 g_last_write[64] 的定义")
        return text.replace(full, shrunk, 1)

    with patched(PANIC_STORE_C, shrink):
        problem = expect_failure(create_project("build"), "needs >= 10 bytes")
    if before != digest(PANIC_STORE_C):
        return "ERROR", f"{PANIC_STORE_C.name} 没有还原到原始字节"
    if problem:
        return "FAIL", problem
    restore = create_project("build")
    if restore.returncode != 0:
        return "FAIL", "破坏已还原，但恢复构建失败"
    problem_again = expect_success(create_project("verify", "--artifacts"))
    if problem_again:
        return "FAIL", f"恢复后自检仍不通过：{problem_again}"
    return "PASS", "尺寸缩到 9 B 被编译期断言拦下，已还原并重建"


def case_ccmram_state() -> tuple[str, str]:
    """CCMRAM 三态一致性：删掉 /DISCARD/ 的段名通配，三态断言必须报。

    当前无人往 `.ccmram` 放变量，所以删掉这条规则**不改变任何产物**：
    objdump -h 里本来就没有 .ccmram 段，nm 里也没有 _siccmram/_sccmram/_eccmram。
    正因为产物侧完全无感，这条只能由源码/链接脚本层的静态断言来守 —— 这也正是
    它与 `cpu.ccm_kb == CCMRAM` 那条数字对拍的分工：后者只保证两个数字相等，
    与本机制没有因果关系。

    破坏只删 `*(.ccmram*)` 这条通配，它是 `*(.ccmram)` 的超集：只删精确名那条
    不会带来任何行为变化（被打包丢弃的仍是同一个段），那样的"破坏"什么也测不出来。

    判据关键字取 `*(.ccmram*)` 本身 —— 纯 ASCII，不依赖 locale 下中文输出的编码。
    """
    script = ROOT / "hal" / "chip" / "linker" / "stm32f429igx_flash.ld"
    if not script.is_file():
        return "SKIP", f"找不到 {script.name}"
    # 干净检出（CI 的每个 job 都是）没有 out/<档位>/objects/target/*.elf，而还原后那条 `verify`
    # 仍会去校验产物里 g_last_write 的生产者 —— 没有产物它只会报「找不到 *.elf」，
    # 测到的就不是「还原后自检通过」。所以先构建一次，与 heap-bound 组同理。
    built = create_project("build")
    if built.returncode != 0:
        return "FAIL", (
            "破坏前的基础构建失败，无法测还原后的自检：\n"
            f"{(built.stdout + built.stderr)[-400:]}"
        )
    before = digest(script)
    rule = "    *(.ccmram*)\n"

    def drop(text: str) -> str:
        if rule not in text:
            raise RuntimeError("链接脚本里找不到 *(.ccmram*) 行")
        return text.replace(rule, "", 1)

    with patched(script, drop):
        problem = expect_failure(create_project("verify"), "*(.ccmram*)")
    if before != digest(script):
        return "ERROR", f"{script.name} 没有还原到原始字节"
    if problem:
        return "FAIL", problem
    problem_again = expect_success(create_project("verify"))
    if problem_again:
        return "FAIL", f"还原后自检仍不通过：{problem_again}"
    return "PASS", "删掉 *(.ccmram*) 被三态断言拦下，链接脚本已按字节还原"


def case_startup_guard() -> tuple[str, str]:
    """VOSRDY 等待必须有上界：把超时判断删掉，产物断言必须报"没有计数递减"。

    这条静默失效的形态是"源码里还留着 VOS_READY_GUARD 宏、只是没人用它"，所以
    静态断言（宏存在）照样通过 —— 只有看指令流里有没有 subs 才抓得到。

    破坏时保留 `(void)guard;` 让 -Werror 不因为 unused-but-set 把构建拦下来，
    这样才能真正走到产物断言那一步。
    """
    if not SYSTEM_STM32F4XX_C.is_file():
        return "SKIP", f"找不到 {SYSTEM_STM32F4XX_C.name}"
    before = digest(SYSTEM_STM32F4XX_C)
    guarded = """  guard = VOS_READY_GUARD;
  while (__HAL_PWR_GET_FLAG(PWR_FLAG_VOSRDY) == 0U)
  {
    if (guard == 0U)
    {
      return 1;
    }
    guard--;
  }
"""
    bare = """  guard = VOS_READY_GUARD;
  while (__HAL_PWR_GET_FLAG(PWR_FLAG_VOSRDY) == 0U)
  {
  }
  (void)guard;
"""

    def drop_guard(text: str) -> str:
        if guarded not in text:
            raise RuntimeError("system_stm32f4xx.c 里找不到 VOSRDY 超时守卫")
        return text.replace(guarded, bare, 1)

    with patched(SYSTEM_STM32F4XX_C, drop_guard):
        problem = expect_failure(create_project("build"), "没有计数递减")
    if before != digest(SYSTEM_STM32F4XX_C):
        return "ERROR", f"{SYSTEM_STM32F4XX_C.name} 没有还原到原始字节"
    if problem:
        return "FAIL", problem
    restore = create_project("build")
    if restore.returncode != 0:
        return "FAIL", "破坏已还原，但恢复构建失败（产物可能仍是裸循环版本）"
    problem_again = expect_success(create_project("verify", "--artifacts"))
    if problem_again:
        return "FAIL", f"恢复后自检仍不通过：{problem_again}"
    return "PASS", "去掉 VOSRDY 超时上界后产物断言报错，已还原并重建"


def case_tick_source() -> tuple[str, str]:
    """HAL 时基的两条路径：把累加器的 +1 改成 +0，产物断言必须报"没有 add #1"。

    这条守的是「调度器起来之后 HAL_GetTick 真的由毫秒累加器推进」，而不是
    「g_tick_ms 这个符号存在」—— 累加器停摆时符号、调用关系、链接全都不变，
    只有指令流里的 add #1 消失，静态文本判据一律看不出来。

    破坏用 += 0 而不是删掉整行：源码形状保持、编译照常通过（+0 不触发任何警告，
    编译器把整条读-改-写消掉），不会退化成"构建失败"掩盖本该由产物断言发出的红。

    破坏态同时验两件事：① build 自己就要打出「没有 add #1」（第 4 条把这条断言
    接进了 build 的产物段，build 绿必须等于产物断言全绿）；② verify --artifacts
    仍以同一句文案报红。两条都命中才算判据被检验过。

    判据关键字 `add #1` 取反汇编助记符，纯 ASCII，不依赖 locale 下中文输出的编码。
    """
    if not PORT_ST_C.is_file():
        return "SKIP", f"找不到 {PORT_ST_C.name}"
    before = digest(PORT_ST_C)
    good = "    g_tick_ms += 1U;\n"
    bad = "    g_tick_ms += 0U;\n"

    def stall(text: str) -> str:
        if good not in text:
            raise RuntimeError(f"{PORT_ST_C.name} 里找不到 g_tick_ms += 1U")
        return text.replace(good, bad, 1)

    with patched(PORT_ST_C, stall):
        # 判据在**产物**分支里，破坏后必须先重建一次，否则 verify --artifacts 看到的
        # 还是上一轮的 elf，判的是没破坏的产物（假绿）。
        # 第 4 条之后 build 末尾也跑 collect_tick_source_errors，所以破坏态的 build
        # 本身就会非 0 并打出这句判据文案 —— 那不是「破坏变成编译错误」，反而是
        # 「build 绿 = 产物断言全绿」的证据；真正的编译错误不会带这句文案。
        built = create_project("build")
        blob = built.stdout + built.stderr
        if "没有 add #1" not in blob:
            if built.returncode == 0:
                return "FAIL", (
                    "破坏态 build 通过：build 末尾没有跑 collect_tick_source_errors，"
                    "「build 绿 = 产物断言全绿」不成立"
                )
            return "FAIL", (
                "破坏态构建失败，但不是判据在叫（破坏变成编译错误，判据没被检验）：\n"
                f"{blob[-400:]}"
            )
        problem = expect_failure(
            create_project("verify", "--artifacts"), "没有 add #1"
        )
    if before != digest(PORT_ST_C):
        return "ERROR", f"{PORT_ST_C.name} 没有还原到原始字节"
    if problem:
        return "FAIL", problem
    restore = create_project("build")
    if restore.returncode != 0:
        return "FAIL", "破坏已还原，但恢复构建失败（产物可能仍是停摆版本）"
    problem_again = expect_success(create_project("verify", "--artifacts"))
    if problem_again:
        return "FAIL", f"恢复后自检仍不通过：{problem_again}"
    return "PASS", "累加器停摆（+= 0）被反汇编判据拦下，源码已按字节还原"


def case_panic_order() -> tuple[str, str]:
    """panic 重入闸必须排在关中断之后：把 mask 挪回判断之后，产物断言必须报。

    这条守的不是"函数里有没有 panic_mask_all_irqs 的调用"—— 那种"符号还在"的判据在
    旧顺序下同样成立，也正是缺陷能长期留下来的原因。破坏时只调换两行的位置：
    构建照常通过（mask 仍是外部链接可见的函数、调用次数不变），只有指令序变了，
    所以这条必须靠反汇编判据抓，静态文本检查抓不到。

    .c 里不能整段删掉 mask 调用再补一句 —— 那会让"是否被调用"这一半失效，
    退化成抓错东西；只挪位置才能保持"调用存在、顺序错"这个原始形态。
    """
    if not PANIC_STORE_C.is_file():
        return "SKIP", f"找不到 {PANIC_STORE_C.name}"
    before = digest(PANIC_STORE_C)
    good = (
        "    panic_mask_all_irqs();\n"
        "    if (g_panic_reporting != 0U) {\n"
        "        for (;;) {\n"
        '            __asm volatile("nop");\n'
        "        }\n"
        "    }\n"
        "    g_panic_reporting = 1U;\n"
    )
    bad = (
        "    if (g_panic_reporting != 0U) {\n"
        "        for (;;) {\n"
        '            __asm volatile("nop");\n'
        "        }\n"
        "    }\n"
        "    g_panic_reporting = 1U;\n"
        "    panic_mask_all_irqs();\n"
    )

    def demote(text: str) -> str:
        if good not in text:
            raise RuntimeError("freertos_port_st.c 里找不到 panic 入口的三步顺序")
        return text.replace(good, bad, 1)

    with patched(PANIC_STORE_C, demote):
        problem = expect_failure(create_project("build"), "重入闸判定排在关中断之前")
    if before != digest(PANIC_STORE_C):
        return "ERROR", f"{PANIC_STORE_C.name} 没有还原到原始字节"
    if problem:
        return "FAIL", problem
    restore = create_project("build")
    if restore.returncode != 0:
        return "FAIL", "破坏已还原，但恢复构建失败（产物可能仍是旧顺序）"
    problem_again = expect_success(create_project("verify", "--artifacts"))
    if problem_again:
        return "FAIL", f"恢复后自检仍不通过：{problem_again}"
    return "PASS", "把 panic_mask_all_irqs() 挪回重入闸之后，产物断言报错，已还原并重建"


HAL_CONF_H = ROOT / "hal" / "periph" / "inc" / "stm32f4xx_hal_conf.h"
PROJECT_INFO_JSON = ROOT / "proj" / "project-info.json"
def primary_map_file() -> Path:
    name = load_info().get("project_name", ROOT.name)
    return OUT_ARTIFACT_DIR / f"{name}.map"


SPEC_MARKDOWN = ROOT / ".skills" / "工程规格.md"
DEP_SAMPLE = out_object_dir(DEFAULT_BUILD_VARIANT) / "apl" / "src" / "main.d"


def ensure_artifacts() -> str | None:
    """这几组读 map / .d，干净检出上必须先构建一次；失败返回原因。"""
    if primary_map_file().is_file() and DEP_SAMPLE.is_file():
        return None
    build = create_project("build")
    if build.returncode != 0:
        tail = "\n".join((build.stdout + build.stderr).strip().splitlines()[-4:])
        return f"缺少产物且重建失败：{tail}"
    return None


def case_hal_module() -> tuple[str, str]:
    """HAL 模块的三处影子：conf 里把 `#include` 行删掉，verify 必须报"缺块"。

    这条断言此前没有任何破坏态覆盖。它守的是 `#ifdef HAL_X_MODULE_ENABLED` /
    `#include` 块 / `hal_x.c` / `hal_x.o` 四处自洽：删掉 include 行不会让任何编译
    或链接报错（该模块的 .c 仍在编译），只是顶层头不再引入它，用到的地方退化成
    隐式声明。纯静态（不编译）。
    """
    before = digest(HAL_CONF_H)
    with patched(
        HAL_CONF_H,
        lambda t: t.replace('  #include "stm32f4xx_hal_gpio.h"\n', "", 1),
    ):
        problem = expect_failure(create_project("verify"), "HAL_GPIO_MODULE_ENABLED")
    if problem:
        return "FAIL", problem

    # 子判据 2：整块被一个 `/* … */` 包裹。块内文本保持原样，所以「只看原文」的实现
    # 仍能匹配到 `#ifdef … #include`（假绿）；剥注释后整段变空白才看得出来。
    # 逐行注释不行：`#ifdef X */` 中间夹了 `*/`，旧实现也匹配不上，区分力为零（实测过）。
    def comment_out_gpio_block(text: str) -> str:
        block = (
            '#ifdef HAL_GPIO_MODULE_ENABLED\n'
            '  #include "stm32f4xx_hal_gpio.h"\n'
            '#endif /* HAL_GPIO_MODULE_ENABLED */\n'
        )
        if block not in text:
            raise RuntimeError("hal_conf.h 里 GPIO 的 #ifdef/#include/#endif 块没找到")
        replacement = (
            '/* #ifdef HAL_GPIO_MODULE_ENABLED\n'
            '  #include "stm32f4xx_hal_gpio.h"\n'
            '#endif */\n'
        )
        return text.replace(block, replacement, 1)

    with patched(HAL_CONF_H, comment_out_gpio_block):
        problem = expect_failure(create_project("verify"), "HAL_GPIO_MODULE_ENABLED")
    if before != digest(HAL_CONF_H):
        return "ERROR", f"{HAL_CONF_H.name} 没有还原到原始字节"
    if problem:
        return "FAIL", "注释包裹整块后没有报缺块：" + problem

    # 子判据 3（反方向）：归档目录里凭空多一个「没启用的模块」的 .o。
    # 这时开关关着、该模块的 .c 也不在 Makefile 里，构建完全正常 —— 只有归档成员
    # 表里多一行，产物断言与镜像大小都不会变。旧实现只查 enabled → .o 在不在，
    # 反方向一直没人守。
    blocked = ensure_artifacts()
    if blocked:
        return "SKIP", blocked
    adc_obj = (
        out_object_dir(DEFAULT_BUILD_VARIANT) / "hal" / "periph" / "src" / "stm32f4xx_hal_adc.o"
    )
    adc_obj.write_bytes(b"")
    try:
        problem = expect_failure(
            create_project("verify"), "HAL_ADC_MODULE_ENABLED 没有启用"
        )
    finally:
        if adc_obj.exists():
            adc_obj.unlink()
    if before != digest(HAL_CONF_H):
        return "ERROR", f"{HAL_CONF_H.name} 没有还原到原始字节"
    if problem:
        return "FAIL", "禁用模块的 .o 混进归档后没有报错：" + problem
    return "PASS", "include 行删除 / 整块注释 / 禁用模块 .o 三种形态都报错，已还原"


def case_hal_assert() -> tuple[str, str]:
    """HAL 全量断言开关：把它注释掉后 verify 必须报错。

    `USE_FULL_ASSERT` 打开后 `assert_param` 才真正检查参数，代价是 `text` 约 +880 B。
    关掉它不会让任何编译或链接失败，镜像里的 `assert_failed` 强符号也照旧存在
    （它定义在 `osl_panic.c`，与这个宏无关）——所以这处只能由配置本身来守，
    "产物里有没有 assert_failed"在这件事上恒真。

    破坏用「整行注释」而不是「删行」：注释后原文里那几个字符还在，只有剥注释的
    判据才看得出来（与 `hal-module` 段第二子判据是同一个陷阱）。
    """
    before = digest(HAL_CONF_H)
    with patched(
        HAL_CONF_H,
        lambda t: t.replace(
            "#define USE_FULL_ASSERT    1U",
            "/* #define USE_FULL_ASSERT    1U */",
            1,
        ),
    ):
        problem = expect_failure(create_project("verify"), "没有启用 USE_FULL_ASSERT")
    if before != digest(HAL_CONF_H):
        return "ERROR", f"{HAL_CONF_H.name} 没有还原到原始字节"
    if problem:
        return "FAIL", problem
    return "PASS", "注释掉 USE_FULL_ASSERT 后 verify 报错，并已还原"


def case_freertos_unit() -> tuple[str, str]:
    """`configUSE_*` 与编译单元同进同退：开关改成 0 但 .o 还在，verify 必须报。

    产物级断言看不见这个状态：event_groups.c / timers.c 内部被 `#if` 掉之后符号
    自然消失，`--gc-sections` 再丢一遍，镜像一片干净 —— 只有 objects/target 里那份
    永远进不了镜像的对象文件还在，每个 job 都在白编它。纯静态（不编译）。
    """
    blocked = ensure_artifacts()
    if blocked:
        return "SKIP", blocked
    before = digest(FREERTOS_CONFIG_H)
    with patched(FREERTOS_CONFIG_H, lambda t: re.sub(
        r"(#define configUSE_EVENT_GROUPS\s+)1", r"\g<1>0", t
    )):
        problem = expect_failure(create_project("verify"), "仍被编译")
    if before != digest(FREERTOS_CONFIG_H):
        return "ERROR", "FreeRTOSConfig.h 没有还原到原始字节"
    if problem:
        return "FAIL", "configUSE_EVENT_GROUPS 改成 0 后没有报多余的编译单元：" + problem
    return "PASS", "开关改成 0 而 event_groups.o 仍在，verify 报出多余的编译单元，已还原"


def case_unlinked_member() -> tuple[str, str]:
    """自研库的未链接成员必须登记：把白名单里的一行删掉，verify 必须报。

    守的是「层里悄悄长出死代码」：新加一个没人调用的模块时，产物断言（查符号
    在场）与镜像大小都不变，只有归档成员表多一行、`--gc-sections` 顺手丢掉。
    破坏点在审计清单本身而不是源码 —— 正因如此才要用一次实跑证明它真会叫。
    """
    blocked = ensure_artifacts()
    if blocked:
        return "SKIP", blocked
    script = ROOT / "build" / "contracts.py"
    before = digest(script)

    def drop_queue(text: str) -> str:
        anchor = (
            '        "queue.o": "configUSE_MUTEXES=1 但全仓没有 xSemaphoreCreate*'
            '（台账「五项 RTOS 配置零使用」已登记）",\n'
        )
        if anchor not in text:
            raise RuntimeError("UNLINKED_MEMBER_WHITELIST 里的 queue.o 条目没找到")
        return text.replace(anchor, "", 1)

    with patched(script, drop_queue):
        problem = expect_failure(create_project("verify"), "queue.o 没有被链接")
    if before != digest(script):
        return "ERROR", f"{gen.name} 没有还原到原始字节"
    if problem:
        return "FAIL", problem
    return "PASS", "白名单删掉 queue.o 后 verify 报未链接成员，已还原"


def case_stale_dependency() -> tuple[str, str]:
    """`.d` 里的绝对路径失效：追加一条不存在的路径，verify 必须报"路径已不存在"。

    守的是"工程被整目录搬动后 make 静默漏编头文件"：`.d` 记的是绝对路径，旧路径
    不存在时 make 看不见头文件变化，tasks.o 会一直是几天前的产物。
    """
    blocked = ensure_artifacts()
    if blocked:
        return "FAIL", blocked
    before = digest(DEP_SAMPLE)
    marker = " /tmp/gate-probe-absent-header.h\n"
    with patched(DEP_SAMPLE, lambda t: t + marker):
        problem = expect_failure(create_project("verify"), "记录的路径已不存在")
    if before != digest(DEP_SAMPLE):
        return "ERROR", f"{DEP_SAMPLE.name} 没有还原到原始字节"
    if problem:
        return "FAIL", problem
    return "PASS", ".d 里追加一条不存在的头文件路径后 verify 报依赖失效，已还原"


def case_warning_gate() -> tuple[str, str]:
    """编译警告门禁：把 `-Werror` 从 toolchain.make 里删掉，verify 必须报。

    代码干净时有没有 -Werror 编出来的镜像一模一样，差别只有下一次引入告警才显现，
    所以只能靠这条文本断言守住。toolchain.make 是生成物，patched() 只管字节。
    """
    before = digest(TOOLCHAIN)
    with patched(TOOLCHAIN, lambda t: t.replace(" -Wextra -Werror ", " -Wextra ")):
        problem = expect_failure(create_project("verify"), "里没有 -Werror")
    if before != digest(TOOLCHAIN):
        return "ERROR", "toolchain.make 没有还原到原始字节"
    if problem:
        return "FAIL", problem
    return "PASS", "删掉 -Werror 后 verify 报警告门禁被关，已还原"


def case_memory_metadata() -> tuple[str, str]:
    """容量元数据：把 json 的 flash_kb 改成 2048，verify 必须以链接脚本为准报错。

    json 只被生成器读取，写错不会有任何构建报错（历史上就写成了 2048 而 ROM 只有
    1024 KB），所以这条断言是唯一的防线。
    """
    before = digest(PROJECT_INFO_JSON)
    with patched(
        PROJECT_INFO_JSON,
        lambda t: t.replace('"flash_kb": 1024', '"flash_kb": 2048', 1),
    ):
        problem = expect_failure(create_project("verify"), "cpu.flash_kb = 2048")
    if before != digest(PROJECT_INFO_JSON):
        return "ERROR", "project-info.json 没有还原到原始字节"
    if problem:
        return "FAIL", problem
    return "PASS", "flash_kb 改成 2048 后 verify 报与链接脚本不符，已还原"


def case_topology() -> tuple[str, str]:
    """顶层目录：造一个未识别且非空的顶层目录，verify 必须报。

    `collect_topology_errors` 只有 1 个 errors.append 点，此前一直登记在 UNCOVERED 的
    「已知缺口」里（零门禁覆盖）。它守的是「有人新增一个顶层目录却没写进 LAYER_DIRS /
    STATIC_LIB_DIRS」—— 这种目录不会被任何构建规则覆盖、也不会有编译报错，只会静静躺着。
    既有行为会跳过空目录（git 不跟踪空目录，checkout 之后不会出现），所以破坏点必须放文件进去。
    """
    probe = ROOT / "gate-probe-topo"
    if probe.exists():
        return "FAIL", f"{probe.name} 已存在，先人工确认再跑本组"
    problem: str | None = "探针目录没有被创建"
    try:
        probe.mkdir()
        (probe / "probe.txt").write_text("probe\n", encoding="utf-8", newline="\n")
        problem = expect_failure(
            create_project("verify"), "未识别的顶层目录 gate-probe-topo/"
        )
    finally:
        shutil.rmtree(probe, ignore_errors=True)
    if probe.exists():
        return "ERROR", "探针目录没有删干净"
    if problem:
        return "FAIL", problem
    return "PASS", "造一个未识别且非空的顶层目录，verify 报「未识别的顶层目录 gate-probe-topo/」，已删除"


def case_makefile() -> tuple[str, str]:
    """构建描述：三处破坏，验证 collect_makefile_errors 的三类判据都会叫。

    `collect_makefile_errors` 有 21 个 errors.append 点，此前零门禁覆盖（登记在
    UNCOVERED 的「已知缺口」里）。这里取三个互不依赖的点各造一次最小破坏：
      ① 造 proj/project-target.ld —— 「旧链接脚本应删除」只判文件存在，不受
         Makefile 模板变动影响，也不会被 update 自愈；
      ② 删根 Makefile 的分档 Kconfig include —— 「根 Makefile 缺少 …」；
      ③ 把 apl/Makefile 里的 libapl.a 全部改名 —— 「未产出 libapl.a」。
    `verify` 不生成 Makefile（生成在 `update` 里跑），所以三处破坏都不会自愈。
    """
    root_makefile = ROOT / "Makefile"
    apl_makefile = ROOT / "apl" / "Makefile"
    probe = ROOT / "proj" / "project-target.ld"
    for path in (root_makefile, apl_makefile):
        if not path.is_file():
            return "ERROR", f"{path.relative_to(ROOT)} 不存在，先跑一次 update"
    if probe.exists():
        return "FAIL", "proj/project-target.ld 已存在，先人工确认再跑本组"

    try:
        probe.write_text("/* gate probe */\n", encoding="utf-8", newline="\n")
        one = expect_failure(
            create_project("verify"), "旧链接脚本 proj/project-target.ld 应删除"
        )
    finally:
        if probe.exists():
            probe.unlink()
    if one:
        return "FAIL", f"① 旧链接脚本：{one}"

    root_before = digest(root_makefile)
    with patched(
        root_makefile,
        lambda text: text.replace(KCONFIG_INCLUDE_MAKE_EXPR, "", 1),
    ):
        two = expect_failure(create_project("verify"), "根 Makefile 缺少")
    if digest(root_makefile) != root_before:
        return "ERROR", "根 Makefile 没有按字节还原"
    if two:
        return "FAIL", f"② 根 Makefile 分档 .config include：{two}"

    apl_before = digest(apl_makefile)
    with patched(apl_makefile, lambda text: text.replace("libapl.a", "libapl_probe.a")):
        three = expect_failure(create_project("verify"), "apl/Makefile 未产出 libapl.a")
    if digest(apl_makefile) != apl_before:
        return "ERROR", "apl/Makefile 没有按字节还原"
    if three:
        return "FAIL", f"③ 层 Makefile 未产出 lib：{three}"

    return "PASS", (
        "三处破坏各自报出预期文案：①造旧链接脚本 → 应删除；"
        "②根 Makefile 去掉分档 Kconfig include；③apl/Makefile 把 libapl.a 全部改名 —— "
        "全部按字节还原"
    )


def case_doc_contract() -> tuple[str, str]:
    """文档契约：两处破坏，验证 collect_doc_contract_errors 的两类判据都会叫。

    `collect_doc_contract_errors` 多处 errors.append（守工程规格 / SKILL
    是否覆盖当前链接顺序、层目录与排除目录）。取两个互不
    依赖的点各造一次最小破坏：
      ① 往工程规格末尾追加一个已移除的层名 bll —— 判据按词边界匹配，追加即报；
      ② 把 SKILL.md 里的 verify 全部换成 check —— 「未说明 verify 子命令」。
    两份被改的文档都用 digest 做按字节还原校验。
    """
    spec = ROOT / ".skills" / "工程规格.md"
    skill = ROOT / ".skills" / "SKILL.md"
    for path in (spec, skill):
        if not path.is_file():
            return "ERROR", f"{path.name} 不存在"

    spec_before = digest(spec)
    with patched(spec, lambda text: text + "\nbll\n"):
        one = expect_failure(create_project("verify"), "仍引用已移除的层 bll")
    if digest(spec) != spec_before:
        return "ERROR", "工程规格.md 没有按字节还原"
    if one:
        return "FAIL", f"① 工程规格仍引用已移除层：{one}"

    skill_before = digest(skill)
    with patched(skill, lambda text: re.sub(r"\bverify\b", "check", text)):
        two = expect_failure(create_project("verify"), "SKILL.md 未说明 verify 子命令")
    if digest(skill) != skill_before:
        return "ERROR", "SKILL.md 没有按字节还原"
    if two:
        return "FAIL", f"② SKILL.md 未说明 verify：{two}"

    return "PASS", (
        "两处破坏各自报出预期文案：①工程规格引用已移除层 bll；"
        "②SKILL.md 去掉 verify —— 两份文档都按字节还原"
    )


def case_config() -> tuple[str, str]:
    """配置一致性：两处破坏，验证 collect_config_errors 的两类判据都会叫。

    `collect_config_errors` 守分档 defconfig / out/<variant>/.config /
    project-info.json 交叉，此前零门禁覆盖。
    这里取两个互不依赖的点：
      ① 从 project_defconfig_userdebug 删掉分类注释 `# apl` —— 「缺少分类注释」；
      ② 把 project-info.json 的 project_name 改成 gate-probe —— 「与工程根目录名不一致」。
    两份被改的文件都用 digest 做按字节还原校验。
    """
    defconfig = project_defconfig_path(DEFAULT_BUILD_VARIANT)
    info = ROOT / "proj" / "project-info.json"
    for path in (defconfig, info):
        if not path.is_file():
            return "ERROR", f"{path.relative_to(ROOT)} 不存在，先跑一次 update"

    defconfig_before = digest(defconfig)
    rel = defconfig.relative_to(ROOT).as_posix()
    with patched(defconfig, lambda text: text.replace("\n# apl\n", "\n", 1)):
        one = expect_failure(create_project("verify"), f"{rel} 缺少分类注释 # apl")
    if digest(defconfig) != defconfig_before:
        return "ERROR", f"{rel} 没有按字节还原"
    if one:
        return "FAIL", f"① 缺少分类注释：{one}"

    info_before = digest(info)
    info_text = info.read_text(encoding="utf-8")
    found = re.search(r'"project_name"\s*:\s*"([^"]+)"', info_text)
    if found is None:
        return "ERROR", "project-info.json 里找不到 project_name 字段"
    # 不能拿 ROOT.name 拼锚点：CI 的检出目录名是仓库名 stm32_freertos（下划线），
    # 文件里却是 stm32-freertos（连字符）。判据比较时忽略 -/_，所以 verify 不会
    # 因此报错，但字符串替换会失配 —— 直接从文件里取当前值当锚点。
    current = found.group(1)
    with patched(
        info,
        lambda text: text.replace(
            f'"project_name": "{current}"', '"project_name": "gate-probe"'
        ),
    ):
        two = expect_failure(
            create_project("verify"), f"与工程根目录名 {ROOT.name!r} 不一致"
        )
    if digest(info) != info_before:
        return "ERROR", "project-info.json 没有按字节还原"
    if two:
        return "FAIL", f"② project_name 不一致：{two}"

    return "PASS", (
        f"两处破坏各自报出预期文案：①{rel} 删掉分类注释 # apl；"
        "②project-info.json 的 project_name 改成 gate-probe —— 两份文件都按字节还原"
    )


def case_newlib_preboot() -> tuple[str, str]:
    """启动期不许调 newlib：main() 的调度器之前、board_system_init() 整函数体。

    `__malloc_lock` 走 taskENTER_CRITICAL，而 port.c 的 `uxCriticalNesting` 初值是
    0xaaaaaaaa（:161），只在 `xPortStartScheduler()` 里置 0（:443）。启动前调进去，
    ENTER 变 0xaaaaaaab、EXIT 退回 0xaaaaaaaa，`if (== 0) portENABLE_INTERRUPTS()`
    永不成立 —— 中断静默保持关闭，且没有任何断言会响。原先 freertos_port_st.c 的
    注释还断言「调度器启动前也能用」，与 port.c 的哨兵初值直接矛盾。

    `_sbrk` / `__malloc_lock` 被 `-Wl,-u` 强制拉进镜像，所以「符号在」不等于「路径
    不可达」；这条判据守的是「别在启动期加第一个调用方」。

    两段破坏分别压两个判定范围，证明判据不是只扫 main.c：
      ① main() 里 vTaskStartScheduler() 之前插一句 malloc()
      ② board_system_init() 函数体里插一句 printf()
    """
    main_c = ROOT / "apl" / "src" / "main.c"
    system_c = ROOT / "bsl" / "src" / "board_system.c"
    if not main_c.is_file() or not system_c.is_file():
        return "SKIP", "找不到 apl/src/main.c 或 bsl/src/board_system.c"

    main_before = digest(main_c)

    def add_malloc_in_main(text: str) -> str:
        anchor = "    vTaskStartScheduler();\n"
        if anchor not in text:
            raise RuntimeError("main.c 里没有 vTaskStartScheduler() 锚点，破坏前提不成立")
        return text.replace(anchor, "    (void)malloc(4);\n" + anchor, 1)

    with patched(main_c, add_malloc_in_main):
        one = expect_failure(create_project("verify"), "在 main() 里调用了 malloc()")
    if digest(main_c) != main_before:
        return "ERROR", "main.c 没有按字节还原"
    if one:
        return "FAIL", f"① main() 里 malloc：{one}"

    system_before = digest(system_c)

    def add_printf_in_init(text: str) -> str:
        anchor = "    if (HAL_Init() != HAL_OK) {"
        if anchor not in text:
            raise RuntimeError("board_system.c 里没有 HAL_Init 判定锚点，破坏前提不成立")
        return text.replace(anchor, '    (void)printf("probe");\n' + anchor, 1)

    with patched(system_c, add_printf_in_init):
        two = expect_failure(
            create_project("verify"), "在 board_system_init() 里调用了 printf()"
        )
    if digest(system_c) != system_before:
        return "ERROR", "board_system.c 没有按字节还原"
    if two:
        return "FAIL", f"② board_system_init() 里 printf：{two}"

    return "PASS", (
        "main() 调度器前插 malloc、board_system_init() 里插 printf 都报"
        "「在 …() 里调用了 …()」，两份文件都按字节还原"
    )


def case_doc_reference() -> tuple[str, str]:
    """文档路径：反引号路径与 Markdown 链接两种写法都必须报。

    历史事实一：`.skills/工程规格.md` 同时提到过不存在和不符实际位置的路径，没有任何
    检查发现，因为 verify 原先只管层名与链接顺序。
    历史事实二：第一版断言只抓反引号，于是 `[说明](../doc/x.md)` 这种写法一条都不查，
    攒出 14 条点开即 404 的链接而门禁全绿。所以破坏点也要按两种写法各注入一次。
    第三/四段守「计划文档的未来路径」这一合法例外：`（待建）` 标记必须能放过紧邻的
    那条路径（否则计划文档写不了还没建的文件），又只能放过紧邻的那一条（否则一句
    话里同时提到已存在与待建文件时，前者会被一起漏检）。
    """
    before = digest(SPEC_MARKDOWN)

    def inject_backtick(text: str) -> str:
        return text + ("" if text.endswith("\n") else "\n") + "`doc/gate-probe-absent.md`\n"

    def inject_link(text: str) -> str:
        return text + ("" if text.endswith("\n") else "\n") + "[探针](../doc/gate-link-probe-absent.md)\n"

    def inject_planned(text: str) -> str:
        return text + ("" if text.endswith("\n") else "\n") + "`doc/gate-probe-planned.md`（待建）\n"

    def inject_mixed(text: str) -> str:
        return text + ("" if text.endswith("\n") else "\n") + (
            "`doc/gate-probe-absent.md` 与 `doc/gate-probe-planned.md`（待建）\n"
        )

    with patched(SPEC_MARKDOWN, inject_backtick):
        problem = expect_failure(create_project("verify"), "引用的路径不存在")
    if before != digest(SPEC_MARKDOWN):
        return "ERROR", "工程规格.md 没有还原到原始字节"
    if problem:
        return "FAIL", problem

    with patched(SPEC_MARKDOWN, inject_link):
        problem = expect_failure(create_project("verify"), "链接指向的路径不存在")
    if before != digest(SPEC_MARKDOWN):
        return "ERROR", "工程规格.md 没有还原到原始字节"
    if problem:
        return "FAIL", problem

    # 干净检出（CI 每个 job 都是一份）没有 out/<档位>/objects/target/*.elf，`verify` 会先报「找不到
    # out/<档位>/objects/target/*.elf，无法校验 g_last_write 的生产者」—— 与「待建」标记无关，但会把
    # 下面的 expect_success 判成失败。本地有残留产物时看不到这条，所以必须先 build 一次。
    blocked = ensure_artifacts()
    if blocked:
        return "FAIL", blocked

    with patched(SPEC_MARKDOWN, inject_planned):
        problem = expect_success(create_project("verify"))
    if before != digest(SPEC_MARKDOWN):
        return "ERROR", "工程规格.md 没有还原到原始字节"
    if problem:
        return "FAIL", f"带「（待建）」标记的未来路径没有被放过：{problem}"

    with patched(SPEC_MARKDOWN, inject_mixed):
        result = create_project("verify")
        blob = result.stdout + result.stderr
    if before != digest(SPEC_MARKDOWN):
        return "ERROR", "工程规格.md 没有还原到原始字节"
    if result.returncode == 0:
        return "FAIL", "同行混写时悬空路径没报 —— 标记把整行都放过了"
    if "gate-probe-absent.md" not in blob:
        return "FAIL", "悬空路径没有出现在报错里"
    if "gate-probe-planned.md" in blob:
        return "FAIL", "「（待建）」标记没有放过紧邻的那条未来路径"

    return (
        "PASS",
        "反引号路径与 Markdown 链接各注入一条悬空引用都报错；"
        "「（待建）」标记放过紧邻的未来路径、同行另一条悬空路径仍报；四处均按字节还原",
    )

def case_lib_member() -> tuple[str, str]:
    """库成员白名单：往 map 里插一个白名单外的成员，verify 必须报。

    这是 /DISCARD/ 那三条无效规则的替代判据：GNU ld 不认"按归档名+成员通配"的丢弃
    表达式，规则静默无效、text 一字未减；只能从 map 里逐个数实际被提取的成员。
    """
    blocked = ensure_artifacts()
    if blocked:
        return "FAIL", blocked
    map_file = primary_map_file()
    before = digest(map_file)
    marker = "\nlibgcc.a(_gate_probe_outside.o)\n"
    with patched(map_file, lambda t: t + marker):
        problem = expect_failure(
            create_project("verify"), "白名单外的成员 _gate_probe_outside.o"
        )
    if before != digest(map_file):
        return "ERROR", "map 没有还原到原始字节"
    if problem:
        return "FAIL", problem
    return "PASS", "map 里插入白名单外成员后 verify 报库成员越界，已还原"


def case_fault_range() -> tuple[str, str]:
    """SP 合法性判据：把 `_estack` 换成写死地址，verify 必须报"写死"。

    写死地址与链接脚本是两份独立的真相：判据偏小会把高位 RAM 的合法异常帧判成栈崩，
    判据偏大会去读越界地址（读本身就是二次 fault），两种都不报错。
    """
    before = digest(PANIC_STORE_C)
    with patched(
        PANIC_STORE_C,
        lambda t: t.replace(
            "sp <= (uint32_t)(uintptr_t)_estack - PANIC_FRAME_MAX_BYTES",
            "sp <= 0x20030000U - PANIC_FRAME_MAX_BYTES",
            1,
        ),
    ):
        problem = expect_failure(create_project("verify"), "把 RAM 地址写死成")
    if before != digest(PANIC_STORE_C):
        return "ERROR", f"{PANIC_STORE_C.name} 没有还原到原始字节"
    if problem:
        return "FAIL", problem
    return "PASS", "把 _estack 换成写死地址后 verify 报 SP 判据脱钩，已还原"


ASSERTION_GROUPS: dict[str, tuple[str, ...]] = {
    # 这张表由 `--assert-map` 的实测输出固化，不要手填：它记的是「这一组破坏之后，
# 究竟是哪几条断言真的报了错」，不是「名字看起来像」。破坏点改过就重跑一次
# `--only <组> --assert-map` 并更新这里。
    "heap": ("collect_heap_errors",),
    "heap-bound": ("collect_artifact_errors",),
    "bsp": ("collect_bsp_artifact_errors",),
    "fault": ("collect_fault_artifact_errors", "collect_fault_errors"),
    "panic-mask": ("collect_fault_errors",),
    "panic-beep": ("collect_fault_errors",),
    "noinit": ("collect_artifact_errors", "collect_persist_record_errors"),
    "persist-record": ("collect_persist_record_errors",),
    "boot-init": ("collect_boot_init_errors",),
    "tick-source": ("collect_tick_source_errors",),
    "startup-guard": ("collect_startup_guard_errors",),
    "ccmram-state": ("collect_ccmram_state_errors",),
    "last-write": ("collect_last_write_errors",),
    "panic-order": ("collect_panic_order_errors",),
    "write": ("collect_artifact_errors", "collect_heap_errors"),
    "record-size": ("collect_artifact_errors",),
    "iwdg-str": ("collect_artifact_errors",),
    "isr-prio": ("collect_isr_priority_errors",),
    "task-priority": ("collect_task_name_len_errors", "collect_task_priority_errors"),
    "panic-code": ("collect_panic_code_errors", "collect_panic_entry_ownership_errors"),
    "observ": ("collect_observability_errors", "collect_panic_scene_errors", "collect_panic_generation_errors"),
    "panic-ctx": ("collect_fault_range_errors", "collect_panic_assert_task_errors", "collect_panic_task_name_errors"),
    "health-slot": ("collect_health_slot_errors",),
    "period": ("collect_periodic_task_errors",),
    "health": ("collect_artifact_errors", "collect_health_writer_errors"),
    "hard-gate": ("collect_lib_member_errors", "collect_observ_artifact_errors"),
    "line-ending": ("collect_eol_contract_errors", "collect_line_ending_errors"),
    "hal-module": ("collect_hal_module_errors",),
    "hal-assert": ("collect_hal_assert_errors",),
    "stale-dependency": ("collect_stale_dependency_errors",),
    "warning-gate": ("collect_warning_gate_errors",),
    "memory-metadata": ("collect_memory_metadata_errors",),
    "doc-reference": ("collect_doc_reference_errors",),
    "makefile": ("collect_makefile_errors",),
    "doc-contract": ("collect_doc_contract_errors",),
    "config": ("collect_config_errors",),
    "topology": ("collect_topology_errors",),
    "newlib-preboot": ("collect_newlib_preboot_errors",),
    "lib-member": ("collect_lib_member_errors",),
    "fault-range": ("collect_fault_range_errors",),
    "board-polarity": ("collect_board_polarity_errors",),
    "board-key": ("collect_board_polarity_errors",),
    "layer-deps": ("collect_layer_dependency_errors",),
    # 组合器 `collect_verify_errors` 会跟着一起命中：布局判据在 VERIFY_COLLECTORS
    # 七类里，插桩如实记下「它返回非空」，这不代表它自己有判据（见 UNCOVERED）。
    "layout": ("collect_layout_errors", "collect_verify_errors"),
    "release-size": ("collect_artifact_errors",),
    "freertos-unit": ("collect_freertos_unit_errors",),
    "unlinked-member": ("collect_unlinked_member_errors",),
    # 下面两组不进 collect_*_errors，用空元组登记：flags 守的是「make 会不会重编」，
    # last-write-size 守的是编译期 `_Static_assert`（破坏后 build 直接失败）。
    # 登记空元组是为了让 `--assert-map` 的漂移比对也覆盖它们——未登记的组会被
    # `ASSERTION_GROUPS.get(name, ())` 当成空元组，恰好与「实测一条都不触发」相等，
    # 于是新增一组门禁而忘了登记时，漂移比对会静默放过（`assertion_coverage()` 现在的
    # 反向检查专门拦这个）。
    "flags": (),
    "last-write-size": (),
}
"""组名 → 该组破坏时真的报错的断言。两组空元组（flags / last-write-size）守的是
「make 会不会重编」与「编译期 _Static_assert」这类非 collect_*_errors 条件，
走 case 内部的 `expect_*` 与 size 判定；登记空元组使漂移比对仍能覆盖它们。"""

UNCOVERED: dict[str, str] = {
    "collect_verify_errors": (
        "组合器：本身不含 append，判据都在 VERIFY_COLLECTORS 七类里；"
        "破坏落在这七类时它会随子函数一起被插桩记到，不代表它有判据"
    ),
    "collect_doc_count_errors": (
        "层边处数的文本自洽：破坏点只是把 doc/design/系统设计.md 里某一格改成错值，"
        "与 doc-contract 组同一形态；doc-contract 只锚定 .skills/ 两份文档。"
        "要覆盖它就得新增第 50 组（组数以 gate-selftest --list 为准）"
    ),
    "collect_heap_artifact_errors": "heap-bound 实际只触发 collect_artifact_errors（实测）",
}
"""确实没有门禁组覆盖的断言，每条都要写清为什么。这个白名单是静态的：
新增一条断言而忘了加组时，它不在 `ASSERTION_GROUPS` 里也不在这里，自检会立刻叫。
"""

NEEDS_ARTIFACTS: frozenset[str] = frozenset(
    {
        "isr-prio",
        "release-size",
        "hal-module",
        "freertos-unit",
        "unlinked-member",
        "stale-dependency",
        "doc-reference",
        "lib-member",
    }
)
"""干净检出上必须先 `build` 一次、有了 elf/map 才能跑的组。

其余 40 组是纯静态的（只读源码与 Makefile）。这个区别此前**只靠人记**：
CI 每组各自重编，本地 `--only` 跑某组时忘了先 build 就会得到「找不到 elf」
的假红（`doc-reference` 就这么栽过一次）。登记成常量后由
`assertion_coverage()` 与实测交叉核对，漏登记或登记错都会拦下。
"""


def _case_needs_artifacts() -> set[str]:
    """实测哪些组真的调了 `ensure_artifacts()` —— 按 AST 取函数体，空格换行都吃。"""
    tree = ast.parse(Path(__file__).read_text(encoding="utf-8"))
    funcs = {node.name: node for node in tree.body if isinstance(node, ast.FunctionDef)}
    needs: set[str] = set()
    for group, func in CASES.items():
        node = funcs.get(getattr(func, "__name__", ""))
        if node is None:
            continue
        if any(
            isinstance(sub, ast.Call)
            and isinstance(sub.func, ast.Name)
            and sub.func.id == "ensure_artifacts"
            for sub in ast.walk(node)
        ):
            needs.add(group)
    return needs


def _assertion_names() -> set[str]:
    """从 verify.py 的语法树取全部断言函数名（不执行 `grep collect_`）。"""
    verify_py = ROOT / "build" / "verify.py"
    tree = ast.parse(verify_py.read_text(encoding="utf-8"))
    return {
        node.name
        for node in tree.body
        if isinstance(node, ast.FunctionDef)
        and node.name.startswith("collect_")
        and node.name.endswith("_errors")
    }


def assertion_coverage() -> str | None:
    """每组都声明了守哪几条断言、每条断言都有去处吗？返回问题描述，None 表示通过。"""
    actual = _assertion_names()
    declared = {name for names in ASSERTION_GROUPS.values() for name in names}
    problems: list[str] = []
    for name in sorted(actual - declared - set(UNCOVERED)):
        problems.append(
            f"断言 {name} 既没有门禁组，也没登记在 UNCOVERED —— 它坏了没有任何东西会叫"
        )
    for name in sorted((declared | set(UNCOVERED)) - actual):
        problems.append(f"ASSERTION_GROUPS / UNCOVERED 里写了不存在的断言 {name}")
    for group in sorted(set(ASSERTION_GROUPS) - set(CASES)):
        problems.append(f"ASSERTION_GROUPS 里的组 {group} 不在 CASES 中")
    # 反方向也要查：新增一组门禁却没在映射表登记时，--assert-map 的漂移比对会静默跳过它
    # （`ASSERTION_GROUPS.get(name, ())` 对未登记的组返回空元组，恰好等于「该组一条断言都不触发」）。
    for group in sorted(set(CASES) - set(ASSERTION_GROUPS)):
        problems.append(
            f"CASE 组 {group} 没登记在 ASSERTION_GROUPS —— --assert-map 的漂移比对会静默放过它"
        )
    # 前置条件也要守：哪组需要先 build 此前只靠人记，忘 build 就得到「找不到 elf」假红。
    actual_needs = _case_needs_artifacts()
    for group in sorted(actual_needs - set(NEEDS_ARTIFACTS)):
        problems.append(
            f"组 {group} 调了 ensure_artifacts() 却没登记在 NEEDS_ARTIFACTS —— "
            "别人照「纯静态组」的用法跑它会拿到「找不到 elf」假红"
        )
    for group in sorted(set(NEEDS_ARTIFACTS) - actual_needs):
        problems.append(
            f"NEEDS_ARTIFACTS 登记了 {group}，但它并不调 ensure_artifacts() —— 登记已失真"
        )
    for group in sorted(set(NEEDS_ARTIFACTS) - set(CASES)):
        problems.append(f"NEEDS_ARTIFACTS 里的组 {group} 不在 CASES 中")
    return "；".join(problems) if problems else None


CASES = {
    "flags": case_flags,
    "heap": case_heap,
    "heap-bound": case_heap_bound,
    "bsp": case_bsp,
    "board-polarity": case_board_polarity,
    "board-key": case_board_key,
    "layer-deps": case_layer_deps,
    "layout": case_layout,
    "release-size": case_release_size,
    "fault": case_fault,
    "panic-mask": case_panic_mask,
    "panic-beep": case_panic_beep,
    "noinit": case_noinit,
    "persist-record": case_persist_record,
    "boot-init": case_boot_init,
    "tick-source": case_tick_source,
    "startup-guard": case_startup_guard,
    "ccmram-state": case_ccmram_state,
    "last-write": case_last_write,
    "last-write-size": case_last_write_size,
    "panic-order": case_panic_order,
    "write": case_write,
    "record-size": case_record_size,
    "iwdg-str": case_iwdg_str,
    "isr-prio": case_isr_priority,
    "task-priority": case_task_priority,
    "panic-code": case_panic_code,
    "observ": case_observ,
    "panic-ctx": case_panic_ctx,
    "health-slot": case_health_slot,
    "period": case_period,
    "health": case_health,
    "hard-gate": case_hard_gate,
    "line-ending": case_line_ending,
    "hal-module": case_hal_module,
    "hal-assert": case_hal_assert,
    "freertos-unit": case_freertos_unit,
    "unlinked-member": case_unlinked_member,
    "stale-dependency": case_stale_dependency,
    "warning-gate": case_warning_gate,
    "memory-metadata": case_memory_metadata,
    "doc-reference": case_doc_reference,
    "makefile": case_makefile,
    "doc-contract": case_doc_contract,
    "config": case_config,
    "topology": case_topology,
    "newlib-preboot": case_newlib_preboot,
    "lib-member": case_lib_member,
    "fault-range": case_fault_range,
}


def restore_artifacts() -> str | None:
    """收尾：按当前源码重建一次产物，并做产物级校验。

    patched() 只还原源码字节与 mtime，管不到 out/。任何一组删过链接参数、改过段边界
    再重建之后，产物就停在破坏版本上：跑完整套时后一组会顺带重建掩盖掉，但单跑一组
    （--only）就会留下一个坏 elf，此后任何产物级断言都可能假红或假绿。
    进程被 SIGKILL（任务被打断、设备掉线）时本函数同样不会执行——那种情况只能人工重建。
    """
    build = create_project("build")
    if build.returncode != 0:
        tail = "\n".join((build.stdout + build.stderr).strip().splitlines()[-4:])
        return f"收尾重建失败：{tail}"
    check = create_project("verify", "--artifacts")
    if check.returncode != 0:
        tail = "\n".join((check.stdout + check.stderr).strip().splitlines()[-4:])
        return f"收尾产物校验失败（产物可能仍是破坏态）：{tail}"
    return None


def gate_selftest_main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="给 verify 的产物断言做红测")
    parser.add_argument("--only", choices=sorted(CASES), help="只跑指定的一组")
    parser.add_argument("--keep-going", action="store_true", help="某组失败后继续跑其余组")
    # CI 的矩阵靠它取组名：组名只有 CASES 这一份来源，workflow 不硬编码列表，
    # 否则新增一组忘了同步 workflow 就永远漏跑（而且没人会发现）。
    parser.add_argument("--list", action="store_true", help="只打印组名的 JSON 数组（给 CI 矩阵用）")
    # 打印「组 → 破坏时真的报错的断言」映射。常量 ASSERTION_GROUPS 就是用它的输出
    # 固化下来的，不要手填：破坏点改过之后旧表会静默失真，所以要能随时重生成并比对。
    parser.add_argument("--assert-map", action="store_true", help="打印本次实际观测到的断言命中映射")
    args = parser.parse_args(argv)

    # 先查「每条断言都有去处吗」，再决定跑不跑：新增一条断言却忘了加门禁组时，
    # 全套照样 36 PASS，缺口只会在下一次真出问题时才暴露——所以这里直接挡在前面。
    problem = assertion_coverage()
    if problem:
        print(f"断言覆盖自检失败：{problem}", file=sys.stderr, flush=True)
        return 1

    if args.list:
        print(json.dumps(list(CASES), ensure_ascii=False))
        return 0

    selected = [args.only] if args.only else list(CASES)
    results: list[tuple[str, str, str]] = []
    observed: dict[str, list[str]] = {}
    started = time.time()

    for name in selected:
        print(f"== {name} ==", flush=True)
        ASSERT_HITS.clear()
        try:
            status, detail = CASES[name]()
        except Exception as exc:  # 备份/还原以外的意外：也当作失败，但先让 finally 完成还原
            status, detail = "ERROR", f"{type(exc).__name__}: {exc}"
        observed[name] = sorted(ASSERT_HITS)
        results.append((name, status, detail))
        print(f"{status}: {detail}", flush=True)

    if args.assert_map:
        print("== assert-map ==", flush=True)
        print(json.dumps(observed, ensure_ascii=False, indent=2), flush=True)
        # 固化的 ASSERTION_GROUPS 与这次实测比对：破坏点或判据一改，旧表就静默失真，
        # 只有真正跑一次才看得出来。只比本次跑过的组。
        drift = []
        for name, hits in observed.items():
            want = set(ASSERTION_GROUPS.get(name, ()))
            got = set(hits)
            if want != got:
                drift.append(f"{name}: 声明 {sorted(want)} / 实测 {sorted(got)}")
        if drift:
            print("== assert-map 漂移 ==", flush=True)
            for line in drift:
                print(f"  - {line}", flush=True)

    # 不管上面各组结果如何都重建一次：否则破坏态产物会留给下一次运行（见 restore_artifacts）。
    print("== restore ==", flush=True)
    problem = restore_artifacts()
    if problem:
        results.append(("restore", "ERROR", problem))
        print(f"ERROR: {problem}", flush=True)
    else:
        print("PASS: 产物已按当前源码重建，verify --artifacts 通过", flush=True)

    failed = [r for r in results if r[1] in ("FAIL", "ERROR")]
    elapsed = time.time() - started
    print(f"\n{len(results)} 组，{elapsed:.1f} s："
          f"{sum(1 for r in results if r[1] == 'PASS')} PASS / "
          f"{sum(1 for r in results if r[1] == 'SKIP')} SKIP / "
          f"{len(failed)} FAIL", flush=True)
    for name, status, detail in failed:
        print(f"  - {name}: {status} {detail}", flush=True)
    return 0 if not failed else 1


