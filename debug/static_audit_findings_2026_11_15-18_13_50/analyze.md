# 全仓静态审计：死代码与可见性缺陷

- 时间：2026-11-15 18:13
- 范围：`D:\Users\Local\stm32-freertos`（main @ `91cef65` 工作区）
- 手段：自建 Python AST 审计 + 用真实构建口径对自研 C 源加跑更严告警（环境无 pyflakes / pylint / flake8 / vulture / cppcheck）

## 现象

`python build/build.py update` 与 `verify --artifacts` 全绿（说明现有门禁能覆盖的规则都成立），但仓库里存在门禁抓不到的四类残留：

1. Python 侧有零引用的薄封装函数与一条未使用的 re-export 导入。
2. C 侧有定义处看不到原型的函数、头文件重复声明、只在本 TU 使用的非 static 函数。
3. 一处 include 顺序使头文件的**条件声明整段失效**（编译仍过，但类型检查退化为隐式）。
4. 审计工具链缺失，仓库没有任何“静态扫描”入口，只能人工看。

## 命令与 EXIT

| 命令 | 结果 |
|---|---|
| `python -m pip list` 过滤 `pyflakes/pylint/flake8/vulture/cppcheck` | 无输出（工具均未安装） |
| `Get-Command cppcheck` / `Get-Command arm-none-eabi-gcc` | 均未找到（gcc 只在 `tool/win_x86_64/bin` 下） |
| `python audit_py.py`（自建 AST 审计，19 个 py 文件） | exit 0；命中 `generate.py` 五处零引用定义、`verify.py` 一处未用导入 |
| `python audit_c.py`（宏 / include / TODO / typedef） | exit 0；自研 46 个 C/H：零引用 typedef 0、TODO 0、重复 include 1 |
| `python audit_gcc_extra.py`（真实 include/宏 + `-Wshadow -Wformat=2 -Wundef -Wnull-dereference -Wdouble-promotion -Wswitch-default -Wfloat-equal -Wcast-qual -Wwrite-strings -Wmissing-declarations -Wmissing-prototypes ...`） | exit 0；20 个自研 C 源，初轮 17 条、全部在 `-Wmissing-prototypes`/`-Wredundant-decls` |
| 修复后同一条命令 | 仅余 13 条“设计上无原型”（newlib 钩子 / ISR 入口 / CMSIS 启动函数） |

## 复现

```powershell
Set-Location D:\Users\Local\stm32-freertos
# 1) 死代码：五处只有定义、全仓零调用
python -c "import pathlib,re; c='\n'.join(p.read_text(encoding='utf-8') for p in pathlib.Path('.').rglob('*.py')); [print(n, len(re.findall(r'\b'+n+r'\b', c))) for n in ('header_keys_hw','header_hse_hz','header_board_compatible','header_led_active_low','header_beep_hw')]"
# 2) 可见性：编译期告警
python audit_gcc_extra.py     # 见上文表格
```

## 已排除

- `-Wall -Wextra -Werror` 已开的告警：构建全绿 ⇒ 未使用的 static 函数、未使用局部变量、未使用参数、隐式函数声明都不存在。
- `-Wshadow`（变量遮蔽）、`-Wformat=2`（格式串）、`-Wundef`（`#if` 引用未定义宏）、`-Wnull-dereference`、`-Wdouble-promotion`、`-Wswitch-default`、`-Wfloat-equal`、`-Wcast-qual`、`-Wwrite-strings`、`-Wmissing-declarations`：自研 20 个 C 源**零命中**。
- 自研 46 个 C/H：无 `TODO/FIXME/XXX/HACK`，无零引用 `typedef`，无同一 TU 重复 `#include`（修完后）。
- Python：无同名顶层重复定义，`_support.__all__` 无缺失项；`codecs/fnmatch/atexit` 仅在 `_support.py` 自身使用（不是死导入）。
- 39 个“零引用宏”里 34 个是 `FreeRTOSConfig.h` 的 `config*`/`INCLUDE_*` 宏——初版语料排除了 `osl/inc/freertos` 才误报，被 vendor 源消费，属正常配置面。
- 生成头里 `BOARD_DTS_COMPATIBLE` / `BOARD_DTS_LED_ACTIVE_LOW` / `BOARD_DEBUG_UART_REG` / `BOARD_DEBUG_UART_RX_GPIO_PORT` 无 C 消费方，但它们是 DTS→C 事实的契约产物（`BOARD_DTS_HSE_HZ` 还被 `_gate.py:2857` 当锚点），保留不删。

## 根因（path:line）

1. `project/generate.py:3399/3402/3405/3408/3425` 五个 `header_*()` 是 `parse_board_dts_header(read_board_dts_header()).X` 的一行转调封装，随着调用方改用 `BoardDtsHeaderFacts` 字段后已无人引用（`header_led_hw()` 是例外，`:3432` 仍在用）。
2. `build/verify.py:35` 导入 `collect_defconfig_user_custom_sync_errors` 只为早年的 `verify` 内联调用；拆分后真实调用点在 `build/checks/config.py` 的 `collect_config_errors()` 内，`verify.py` 从未按名引用。
3. `bsl/inc/bsl_i2c_gpio.h:20` 的 `bsl_board_i2c_gpio_bus()/ready()` 声明被 `#if defined(BOARD_DTS_BEEP_VIA_PCF8574) && BOARD_DTS_BEEP_VIA_PCF8574` 包住，而该宏来自 `board_dts.h`；`bsl/src/bsl_i2c_gpio.c` 原先第 1 行就 include 自己的头、第 3 行才 include `board_dts.h` ⇒ 解析头文件时宏未定义，声明整段被跳过，`:147/:152` 的两个定义在定义处无原型（GCC `-Wmissing-prototypes` 实测两条）。同类位还有一个 `#if` 块内重复的 `#include "board_dts.h"`（原 `:141`）。
4. `bsl/inc/bsl_i2c_gpio.h:23` 的 `extern const struct bsl_device_driver bsl_driver_brd_i2c_gpio;` 与生成的 `bsl/inc/board_device_registry.h:38` 重复声明同一个对象（`-Wredundant-decls`）。
5. `osl/src/osl_boot.c:335` 的 `osl_hal_tick_ms_from_dwt()` 只被同文件 `:361/:402` 调用，却是外部链接（`-Wmissing-prototypes`）。
6. `bsl/src/board_beep.c:75/108` 定义的 `board_panic_beep_silence()` 在 bsl 侧没有任何可见声明：唯一声明在生成的 `osl/inc/board_panic_hw.h:19`，而 `osl/inc` 不在 bsl 的 include 路径里（层依赖不允许 bsl→osl）。

## 修复与影响面

| 改动 | 文件 | 影响面 |
|---|---|---|
| 删五个零引用薄封装 | `project/generate.py`（-12 行） | 无调用方；`header_led_hw` 保留 |
| 删未使用 re-export 导入 | `build/verify.py:37` | `collect_config_errors` 仍从 `checks.config` 导出，`build.py` 照旧可用 |
| `board_dts.h` 提前到自身头之前，并删 `#if` 块内重复 include | `bsl/src/bsl_i2c_gpio.c` | 头文件条件声明恢复可见；无逻辑改动 |
| 删重复 extern | `bsl/inc/bsl_i2c_gpio.h` | 唯一来源改为生成的 `board_device_registry.h` |
| `osl_hal_tick_ms_from_dwt` 改 `static` | `osl/src/osl_boot.c:335` | 无行为改动；`-Os` 下被内联，`tick-source` 组按 `udiv` 兜底判据仍 PASS |
| 补 bsl 侧声明 | `bsl/inc/board_beep.h` | 定义处可按原型检查；调用方契约仍在生成的 osl 头 |

## 验证证据

| 项 | 结果 |
|---|---|
| `python build\build.py update` | exit 0，`自检通过` |
| `python build\build.py verify --artifacts` | exit 0，两个档位产物均校验（userdebug text=16484 / user text=14404） |
| `python -m unittest tests.test_dts` | `Ran 19 tests … OK` |
| `python build\build.py build --no-update` | exit 0；elf 41144 B（原 41216 B）、hex 40696 B、map 104197 B；elf SHA256 前 16 位 `E5D44A4D166D7D19`（原 `299E4FDDE46BB94F`，差异来自静态化后调试符号） |
| `gate-selftest --only tick-source` | 1 PASS / 0 FAIL（236.5 s，破坏→报红→还原后 `verify --artifacts` 通过） |
| `gate-selftest --only panic-beep` | 1 PASS / 0 FAIL（98.8 s） |
| 修复后 `audit_gcc_extra.py` | 自研源仅剩 13 条设计性无原型告警 |
| 修复后 `audit_py.py` | 死代码 0；仅剩 `_support` 的共享导入（供 `checks` 各模块 `import *` 用）和无占位 f-string（lint 级） |

## 追加发现：`generate.py check` 恒红

移动单测门禁时顺手跑了 `python project\generate.py check`，得到 **exit 1 / 10 项 1 项失败**：

```
[FAIL] bindings 与 gpio dtsi 一致
    - gpio dtsi 缺少 compatible brd,gpio-leds
    - gpio dtsi 缺少 compatible brd,gpio-keys
    - pcf8574 蜂鸣须描述 I2C 总线 compatible brd,i2c-gpio
    … 另有 3 条
```

根因（`project/generate.py` 原 `:3943-3944`）：`cmd_check` 用 `board_gpio_dtsi_path(info)` = `project/dts/generated/<stem>-gpio.dtsi` 当比对源；该文件只在 `project-info.json` 配了 `board.gpio_bind` 时才生成，而 DTS 重写后 LED/KEY/I2C/BEEP 都写进了整板 `boards/apollo.dts`（`board_static_gpio_dtsi_path()` 的注释已写明）。文件不存在时 `gpio_text = ""`，7 条 token 断言逐条失败——**命令本身永远红，跟代码对不对无关**。

修复：`cmd_check` 改为读板级 `.dts` 文本作比对源，条目改名「bindings 与板级 DTS 一致」；`collect_device_binding_errors()` 的 docstring 说明两种输入形态（整板 `.dts` / 配了 `gpio_bind` 时的生成物）。实测 `10 项检查，0 项失败`，exit 0。

## 追加改动：单测门禁实现搬进 `build/checks/dts.py`

- 原：实现在 `project/generate.py` 的 `collect_host_dts_unittest_errors()`，`build/checks/dts.py` 只有两行转调。
- 现：实现整体落在 `build/checks/dts.py`（docstring、`Ran N tests` 判据、800 字符截断逐字保留），`generate.py` 里同名函数删除。
- `cmd_check` 原本也把这条挂在自检清单里，但它属 build 层（直接跑 `project/generate.py` 时 `checks` 不在 `sys.path`），且 `update` 收尾会跑 `verify()` 已覆盖，故从 `cmd_check` 移除并把执行路径写进注释。

## 遗留

- `board_panic_beep_silence` 现在有两处声明：`bsl/inc/board_beep.h`（定义方）与生成的 `osl/inc/board_panic_hw.h`（调用方）。因层依赖禁止 bsl 包含 osl 头，这是结构性重复；两处签名若漂移，链接期不会报错。未加断言（新增断言会改动 `ASSERTION_GROUPS` 覆盖自检），登记在 `.skills/架构缺陷.md`「已知敞口」。
- `osl_debug_uart_write` 同类问题：osl 弱符号定义 + bsl 强符号覆盖，bsl 侧是 `board_debug_uart.c:13` 手写原型（不能包含 osl 头）。
- 未处理无占位 f-string（`ruff F541` 级，非缺陷，涉及 40+ 行）。
- `gate-selftest` 51 组只跑过 12 组，其余未跑（全量约 40+ 分钟，中断会留破坏态）。
