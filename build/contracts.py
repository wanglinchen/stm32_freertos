"""build 三件套共享常量与路径约定。"""
from __future__ import annotations

import re
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
_BUILD = Path(__file__).resolve().parent

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


def project_defconfig_basename(variant: str) -> str:
    if variant not in BUILD_VARIANTS:
        raise ValueError(f"未知 BUILD_VARIANT: {variant!r}")
    return f"project_defconfig_{variant}"


def project_defconfig_path(variant: str = DEFAULT_BUILD_VARIANT) -> Path:
    return ROOT / "proj" / f"{project_defconfig_basename(variant)}"


def out_config_path(variant: str = DEFAULT_BUILD_VARIANT) -> Path:
    return out_profile_root(variant) / ".config"
# userdebug：可调试优化 + 完整 DWARF（宏、内联展开）；user：体积优先、不带调试节。
VARIANT_MAKE_OPTS: dict[str, dict[str, str]] = {
    BUILD_VARIANT_USERDEBUG: {"OPT": "-Og", "DEBUG": "-g3"},
    BUILD_VARIANT_USER: {"OPT": "-Os", "DEBUG": "-g0"},
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


def _sync_out_dir_exports() -> None:
    """`from contracts import *` 会把路径绑死在各模块命名空间里，切换档位后须写回。"""
    import sys

    names = ("OUT_OBJECT_DIR", "OUT_ARTIFACT_DIR", "OUT_RELEASE_DIR")
    values = {n: globals()[n] for n in names}
    for mod in sys.modules.values():
        mod_dict = getattr(mod, "__dict__", None)
        if mod_dict is None or "OUT_ARTIFACT_DIR" not in mod_dict:
            continue
        for name in names:
            if name in mod_dict:
                mod_dict[name] = values[name]


def set_build_variant(variant: str) -> str:
    """切换当前逻辑使用的 object/target/release 目录（verify / copy 共用）。"""
    global OUT_OBJECT_DIR, OUT_ARTIFACT_DIR, OUT_RELEASE_DIR, _active_build_variant
    if variant not in BUILD_VARIANTS:
        raise ValueError(f"未知 BUILD_VARIANT: {variant!r}")
    _active_build_variant = variant
    OUT_OBJECT_DIR = out_object_dir(variant)
    OUT_ARTIFACT_DIR = out_artifact_dir(variant)
    OUT_RELEASE_DIR = out_release_dir(variant)
    _sync_out_dir_exports()
    return variant


def active_build_variant() -> str:
    return _active_build_variant


OUT_OBJECT_DIR = out_object_dir(DEFAULT_BUILD_VARIANT)
OUT_ARTIFACT_DIR = out_artifact_dir(DEFAULT_BUILD_VARIANT)
OUT_RELEASE_DIR = out_release_dir(DEFAULT_BUILD_VARIANT)
RELEASE_BUCKET_DAYS = 5
RELEASE_DIR_NAME_RE = re.compile(r"^\d{4}_\d{2}_\d{2}-\d{2}_\d{2}_\d{2}$")
EXCLUDE_DIRS = {
    "debug",
    "doc",
    "flash",
    "proj",
    "tool",
    "build",
    "workspace",
    "out",
    # tests 若存在则只放 host 侧辅助文件；必须让 update 不要往里生成 Makefile/Kconfig，
    # 否则 verify 会把 tests/Makefile 当成「排除目录里的生成物」报错。
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
    "doc",
    "proj",
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
NAME_SEP_RE = re.compile(r"[^a-z0-9]+")
# FreeRTOSConfig.h 里 `configUSE_*` 直接决定编译单元要不要存在的三个文件。
#
# `configUSE_TIMERS = 0` 时 timers.c 内部几乎被 `#if` 掉，产物里没有任何符号
# （`--gc-sections` 也把它丢光），但**编译单元照样参与编译**：每个 job 都在编一份
# 永远进不了镜像的文件，而产物级断言因为符号不在而一直绿 —— 这就是「编了没链」的
# 典型形态，只能在编译单元层看得见。
FREERTOS_CONFIG_GATED = (
    ("timers.c", "configUSE_TIMERS"),
    ("event_groups.c", "configUSE_EVENT_GROUPS"),
    ("stream_buffer.c", "configUSE_STREAM_BUFFERS"),
)
RESTAMP_DIRTY: list[Path] = []
SKILLS_DIR = ROOT / ".skills"
SPEC_DOC = SKILLS_DIR / "工程规格.md"
SKILL_DOC = SKILLS_DIR / "SKILL.md"
DOC_SKILL_PATHS = (SPEC_DOC, SKILL_DOC)
INCLUDE_QUOTED = re.compile(r'(?m)^[ \t]*#[ \t]*include[ \t]+"([^"]+)"')
INCLUDE_ANGLE = re.compile(r'(?m)^[ \t]*#[ \t]*include[ \t]+<([^>]+)>')
# 自研符号的层前缀 → 层。用于识别 `extern` 声明引的是哪一层的符号。
#
# `board_` 归 bsl 而不是单独一个 "board 层"：bsl 就是板级支持，它的门面就叫
# `board_*`（board_led / board_key / board_beep / board_system）。
# `hal` 用 `HAL_` —— ST 的 HAL API 全部以它开头。
LAYER_SYMBOL_PREFIXES = {
    "apl": "apl_",
    "bsl": "board_",
    "fml": "fml_",
    "osl": "osl_",
    "hal": "HAL_",
}
# `.c` 里手写的 `extern` 函数声明。只取带 `(` 的形态 —— 变量声明不在层依赖范畴。
EXTERN_DECL = re.compile(
    r"(?m)^[ \t]*extern[ \t]+(?:[A-Za-z_][A-Za-z0-9_]*[ \t\*]+)*"
    r"([A-Za-z_][A-Za-z0-9_]*)[ \t]*\("
)
NEWLIB_PREBOOT_CALLS = re.compile(
    r"\b(malloc|calloc|realloc|printf|sprintf|snprintf|vprintf|puts|fwrite)\s*\("
)
# 应用代码参与链接后 .text 至少应有这么多字节；明显低于此值说明镜像里只有启动代码。
ARTIFACT_TEXT_FLOOR = 2048
# 发布构建的 .text 上限（16 KB）。
#
# 下限守「镜像只剩启动代码」，上限守「悄悄长胖」：现态 `.text` 以 `verify --artifacts` 为准，
# `-Os` 发布构建更小，16 KB 留约 49% 余量。超限通常意味着有人整包拖进一个没裁剪的
# 模块（例如把 hal 的 68 个 .c 全开、或把 newlib 的 printf 家族拉进镜像），
# 这类变化在 Flash 8192 KB 里毫无痛感，只能靠这条钉住。
ARTIFACT_TEXT_CEIL = 16384
# 整镜像 Flash 用量（text + data）上限 64 KB。
ARTIFACT_FLASH_CEIL = 65536
# panic 记录结构体的最小尺寸下限（0x80 = 128 B；现态结构约 0x8C / 140 B）。
#
# 它是 ipsr / reset_csr / wd_sync_fail 三个"上下文"字段是否真的编进去的唯一产物级证据：
# 少任何一个，尺寸就回落到 0x70。字段的偏移顺序在 struct 里是固定的，所以尺寸下限
# 等价于"新字段都在"。用 DWARF 解析字段名能更精确，但代价是几百毫秒和脆弱的调试信息依赖。
PANIC_RECORD_MIN_BYTES = 0x80
# newlib 的分配入口。它们按需提取：只有真有代码引用 malloc 才会进镜像。
#
# 因此"符号在不在 elf 里"就是"有没有人用 newlib 堆"的机器事实，不依赖文本扫描，
# 也不怕宏包装或函数指针。
NEWLIB_ALLOC_SYMBOLS = ("_malloc_r", "_calloc_r", "_realloc_r", "_memalign_r")
# newlib 堆的最小可用尺寸（2048 B）。
#
# 触发条件只有一种：镜像里出现了 newlib 的分配入口。此时若堆仍是链接脚本里那个
# 512 B 的默认值，一次中等分配就会越过 __heap_end__ 写进紧跟其后的栈区 ——
# 链接不报错、运行时崩在别处。
NEWLIB_HEAP_FLOOR = 0x800
# 三家编译器运行库当前被提取的成员（实测值，不是猜测）。
#
# 链接脚本 /DISCARD/ 里曾写 `libc.a ( * )` / `libm.a ( * )` / `libgcc.a ( * )`，本意是
# 整包丢弃；但 GNU ld 只认段名通配（`*(.ccmram)`），"按归档名 + 成员通配"的表达式
# 静默无效 —— 把 `libc.a` 改成真库名 `libc_nano.a` 之后链接照样成功、text 一字未减
# （实测 10520 → 10520）。那三行什么也没挡住，只留下"已挡住"的假印象。
#
# 所以这条意图只能落到产物上：map 里每家库被提取了哪些成员一清二楚。出现白名单外的
# 成员，就是有代码把新的 libc/libgcc 功能拉了进来（典型是 printf 家族、浮点除法的
# 软件实现、动态分配），体积会涨而构建完全不会报错。
#
# 新增成员确实是有意的时，两处一起改：把它加进本表，并在 `.skills/工程规格.md`
# 里写明用途与大致字节数；只改表而不写理由，等于把审计改成放行。
LIB_MEMBER_WHITELIST = {
    "libc_nano.a": ("libc_a-init.o", "libc_a-memset.o"),
    # _aeabi_ldivmod.o 只在发布构建（-Os）里被拉进来，见 LIB_MEMBER_OPTIONAL。
    "libgcc.a": ("_aeabi_uldivmod.o", "_dvmd_tls.o", "_udivmoddi4.o"),
    "libm.a": (),
}
# 只在发布构建（-Os）里允许出现的库成员 —— 默认（-Og）构建出现它仍是缺陷。
#
# `_aeabi_ldivmod.o` 在 -Os 下由 HAL_RCC 的 64 位频率计算拉进来（-Og 折叠成常数，
# -Os 不折，落到 libgcc 软件除法，.text 0xc9 B）。但 -Og 下它出现恰恰说明有人新写了
# 64 位除法 —— `hard-gate` 组就是靠"main 加 int64 除法"来探测这条的，所以不能一律
# 放行。判据按经济档位决定容忍与否，见 `_artifact_is_release_build()`。
LIB_MEMBER_RELEASE_ONLY = {
    "libgcc.a": ("_aeabi_ldivmod.o",),
}
HEALTH_SAMPLE_LAYERS = ("apl", "bsl", "fml")
ARTIFACT_TOOL_NAMES = ("nm", "objdump")
# 自研库里「编了但没被链接」的成员，每条都要写明为什么可以存在。
#
# 这是「编译单元层」的唯一守卫：层里新加一个没人调用的模块时，产物断言（查符号
# 在场）与镜像大小都不会变 —— 只有归档成员表里多一行，`--gc-sections` 顺手丢掉。
# 不登记就报错，避免层内悄悄长出死代码。
UNLINKED_MEMBER_WHITELIST = {
    "libhal.a": {
        "stm32f4xx_hal_flash.o": "时钟树用 __HAL_FLASH_SET_LATENCY 宏直接写 ACR，不引用 flash.c 的函数",
        "stm32f4xx_hal_flash_ex.o": "同上；flash_ex 只提供函数级 API，本工程不用",
        "stm32f4xx_hal_pwr.o": "RCC/PWR 的时钟序列走 __HAL_RCC_* 宏，不调 PWR 函数",
        "stm32f4xx_hal_pwr_ex.o": "同上",
        "stm32f4xx_hal_rcc_ex.o": "RCC 主体被 HAL_InitTick 用到，_ex 的函数无人调用",
    },
    "libosl.a": {
        "queue.o": "configUSE_MUTEXES=1 但全仓没有 xSemaphoreCreate*（台账「五项 RTOS 配置零使用」已登记）",
        "event_groups.o": "configUSE_EVENT_GROUPS=1 但无 xEventGroup* 调用（同上）",
        "stream_buffer.o": "configUSE_STREAM_BUFFERS=1 但无 xStreamBuffer* 调用（同上）",
    },
}
# Cortex-M4 的四个异常入口；startup 里全部是 .weak 别名到 Default_Handler。
FAULT_HANDLER_NAMES = (
    "HardFault_Handler",
    "MemManage_Handler",
    "BusFault_Handler",
    "UsageFault_Handler",
)
FAULT_SOURCE = ROOT / "osl" / "src" / "osl_panic.c"
OSL_PORT_ST_SOURCE = ROOT / "osl" / "src" / "osl_boot.c"
PANIC_ENTRY_POINTS = (
    ("fault_report", r"void\s+fault_report\s*\("),
    ("UnhandledIRQ_Handler", r"void\s+UnhandledIRQ_Handler\s*\("),
    ("vApplicationStackOverflowHook", r"void\s+vApplicationStackOverflowHook\s*\("),
    ("vApplicationMallocFailedHook", r"void\s+vApplicationMallocFailedHook\s*\("),
    ("vAssertFailed", r"void\s+vAssertFailed\s*\("),
    ("_sbrk", r"void\s*\*\s*_sbrk\s*\("),
)
# 文档里这些路径属于工具链目录、上游示例或未跟踪的大体积参考资料，不做存在性断言。
DOC_SCAN_SKIP_PREFIXES = ("tool/", "out/", "examples/", "doc/reference/")
# 只有"仓库相对路径"才断言存在。裸文件名（`FreeRTOSConfig.h`、`Depend.make` 这类
# 正文提及）不在此列——它们不是在说"去看这个文件"，而是把文件名当术语用。
DOC_REF_PREFIXES = (".skills/", "build/", "proj/", "doc/", "apl/", "fml/", "bsl/", "osl/", "hal/")
DOC_REF_EXTENSIONS = (
    ".md", ".c", ".h", ".py", ".ld", ".s", ".json", ".make", ".mk", ".txt",
    ".cfg", ".xlsx", ".pdf", ".png", ".bin", ".hex", ".elf", ".map",
)
RTOS_ISR_API = re.compile(r"\b([A-Za-z_][A-Za-z0-9_]*FromISR|portYIELD_FROM_ISR)\s*\(")
NVIC_PRIORITY_CALL = re.compile(
    r"HAL_NVIC_SetPriority\s*\(\s*[A-Za-z0-9_]+(?:_IRQn|[0-9]+)\s*,\s*([0-9]+|0x[0-9A-Fa-f]+)"
)
PERIODIC_TASKS = (
    ("fml/src/fml_led.c", r"static\s+void\s+fml_led_blink_task\s*\(", "fml_led_blink_task"),
    ("fml/src/fml_button.c", r"static\s+void\s+fml_button_task\s*\(", "fml_button_task"),
)
FREERTOS_CONFIG = ROOT / "osl" / "inc" / "FreeRTOSConfig.h"
TASK_SOURCES = ("fml/src/fml_led.c", "fml/src/fml_button.c")
READY_LIST_ITEM_BYTES = 20
TICK_HOOK_SOURCE = "osl/src/osl_boot.c"
OBSERVABILITY_DEFINES = (
    ("configUSE_TICK_HOOK", "1"),
    ("configCHECK_FOR_STACK_OVERFLOW", "2"),
    ("configUSE_MALLOC_FAILED_HOOK", "1"),
)
TICK_HOOK_CALLS = ("stack_guard_check(", "osl_watchdog_feed(")
PANIC_CODE_MAX = 7
PANIC_CALLERS = ("osl_panic_report", "fml_panic")
PANIC_DECL_ARGUMENT = re.compile(
    r"(?:osl_panic_code_t|unsigned(?:\s+int)?|uint\d+_t|int)\s*(?:code|value|raw|kind)"
)
PANIC_FORWARD = re.compile(r"(?:\(\s*osl_panic_code_t\s*\))?\s*[A-Za-z_][A-Za-z0-9_]*")
PANIC_CODE_BY_NAME = {
    "SCHEDULER_FAILED": 1,
    "CREATE_FAILED": 2,
    "ASSERT": 3,
    "CLOCK_FAILED": 4,
    "HEAP_EXHAUSTED": 5,
    "HARDWARE_FAULT": 6,
    "STACK_OVERFLOW": 7,
}
FML_PANIC_SOURCE = ROOT / "fml" / "src" / "fml_panic.c"
FML_LED_SOURCE = ROOT / "fml" / "src" / "fml_led.c"
FML_BUTTON_SOURCE = ROOT / "fml" / "src" / "fml_button.c"
HEALTH_HEADER = ROOT / "osl" / "inc" / "osl_health.h"
HEALTH_SOURCE = ROOT / "osl" / "src" / "osl_health.c"
HEALTH_CALL_SOURCES = TASK_SOURCES
# 健康采样量的写入口只在 osl；上层出现这些名字即为职责外溢。
HEALTH_SLOT_MARK = "g_stack_slot_err"
MALLOC_HOOK_SIGNATURE = r"void\s+vApplicationMallocFailedHook\s*\("
TASK_NAME_SOURCES = TASK_SOURCES
TASK_NAME_SLACK = 2
FAULT_RANGE_LITERAL = re.compile(r"0x200[0-9a-fA-F]{5}")
PANIC_FAULT_FIELDS = (
    "fault_kind",
    "fault_cfsr",
    "fault_hfsr",
    "fault_mmfar",
    "fault_bfar",
    "fault_exc_return",
    "fault_frame_type",
    "fault_sp",
    "fault_r0",
    "fault_r1",
    "fault_r2",
    "fault_r3",
    "fault_r12",
    "fault_lr",
    "fault_pc",
    "fault_xpsr",
    "unhandled_irq",
)
PANIC_ENTRY_PATTERNS = (
    (r"static\s+void\s+fault_report\s*\(", "fault_report"),
    (r"void\s+UnhandledIRQ_Handler\s*\(", "UnhandledIRQ_Handler"),
    (r"void\s+vApplicationStackOverflowHook\s*\(", "vApplicationStackOverflowHook"),
    (MALLOC_HOOK_SIGNATURE, "vApplicationMallocFailedHook"),
    (r"void\s+vAssertFailed\s*\(", "vAssertFailed"),
    (r"void\s+\*_sbrk\s*\(", "_sbrk"),
)
BSP_ARTIFACT_SYMBOLS = (
    "board_key_init",
    "board_key_pressed",
    "board_beep_init",
    "board_beep_set",
)
COND_BRANCH_RE = re.compile(
    r"^(beq|bne|bcs|bhs|bcc|blo|bmi|bpl|bvs|bvc|bhi|bls|bge|blt|bgt|ble|cbz|cbnz)"
    r"(\.n|\.w)?$"
)
SYSTEM_STM32F4XX_C = ROOT / "hal" / "chip" / "startup" / "src" / "system_stm32f4xx.c"
STARTUP_STM32F429XX_S = ROOT / "hal" / "chip" / "startup" / "src" / "startup_stm32f429xx.s"
CCMRAM_SECTION_RE = re.compile(r"\.\s*ccmram[^\s:]*\s*:\s*\{.*?\}\s*>\s*CCMRAM", re.DOTALL)
CCMRAM_DISCARD_RE = re.compile(r"\*\(\s*\.\s*ccmram\s*\*\s*\)")
CCMRAM_STARTUP_SYMBOLS = ("_siccmram", "_sccmram", "_eccmram")
EOL_SKIP_DIRS = {".git", "tool", "__pycache__", ".vscode"}
EOL_SKIP_SUBTREES = (
    "out/user/objects",
    "out/user/release",
    "out/userdebug/objects",
    "out/userdebug/release",
    # 迁桶前的旧布局
    "out/user/object",
    "out/userdebug/object",
    "out/object",
    "out/release",
)
