# tests/test_dts.py 在工作区消失：单测整组失效

- 问题目录：`debug/tests_test_dts_missing_2026_11_15-16_37_25/`
- 首次观察时间：2026-11-15 16:37:25（Asia/Shanghai）
- 状态：**已确认为用户手动删除，并按用户要求重写恢复**（`tests/test_dts.py` 199 行、19 条；`verify` 的 host 单测门禁重新生效）

## 1. 现象

DTS 改名流程照旧跑单测，突然整组不可导入（不是断言失败，是模块找不到）：

```text
PS> python -m unittest tests.test_dts
E
ERROR: test_dts (unittest.loader._FailedTest.test_dts)
ImportError: Failed to import test module: test_dts
  File "...\unittest\loader.py", line 141, in loadTestsFromName
    module = __import__(module_name)
ModuleNotFoundError: No module named 'tests.test_dts'
Ran 1 test in 0.000s
FAILED (errors=1)
```

关键点：同一命令在本轮早先（改 `stm32f429-gpio-default.dtsi` 之后）还是 `Ran 19 tests … OK`，
所以不是解释器/路径问题，而是**文件本身没了**。

## 2. 复现与证据

```text
PS> Test-Path tests\test_dts.py          → False
PS> Get-ChildItem tests                  → 只剩 test_case.md（无 __init__.py，靠 PEP 420 命名空间包导入）
PS> git status --porcelain -- tests
 D tests/test_dts.py                     → 工作区删除、未 staged
```

`git log -1 -- tests/test_dts.py` 仍是最后一次提交：

```text
91cef65 Sun Nov 15 00:07:35 2026 +0800 工程布局与构建链收口：project 目录、DTS 并入 generate、BSL 分层与门禁补强。
```

`git show HEAD:tests/test_dts.py | Measure-Object -Line` → 60 行，说明库里版本完好。

## 3. 已排除项

| 假设 | 排除依据 |
|------|----------|
| 解释器 / 工作目录不对 | 同一 `python`、同一 `Set-Location 'D:\Users\Local\stm32-freertos'`；报错是导入不到模块名，不是路径拼接错误 |
| `tests/__init__.py` 缺失导致 | 本轮早先同样没有 `__init__.py` 却能跑通（PEP 420 命名空间包），且此时 `tests` 目录本身存在 |
| 我把文件名改错（改名脚本替换路径时写坏） | 本轮对 `tests/test_dts.py` 只做**字符串替换**（`…/apollo-stm32f429.dts` → `…/apollo.dts`、`…-pinmap.dtsi` → `…-default.dtsi`），`[IO.File]::WriteAllText` 只覆盖内容不改名；替换前后文件均存在且可读 |
| `py_compile` 或 `verify` 清理了它 | `python -m py_compile debug\capture.py` 只写 `debug/__pycache__/`；`build.py verify` 只读源码、不删 `tests/`（该目录不在 Kconfig/Makefile 扫描范围） |

## 4. 根因

**已确认（2026-11-15 用户答复）：文件由用户手动删除**，不是工具链或本次改动引入。

排查过程中排除的对象：

| 候选 | 排除方式 |
|---|---|
| `build.py update/verify` 清理目录 | 在 `tests/` 放 `probe_keep.py` / `probe_keep.md` 后跑 `verify`，两个文件均保留（只报 CRLF 与 `.gitattributes eol=lf` 冲突，exit 1） |
| `generate.remove_stale_build_files()` | 只 unlink `Makefile` / `Kconfig` / `.config`（`generate.py:4843-4846`），不含 `.py` |
| `generate.is_empty_source_dir()` + `rmdir` | 仅对名为 `src` 的目录生效（`generate.py:4141-4147`） |
| `git stash` / `reset` | `git stash list` 为空；reflog 最后一条是 00:07 的正常 commit |
| 我自己的改名脚本 | 本轮只做内容字符串替换（`WriteAllText`），不改文件名、不删除文件 |

另一个附带风险（本次发现，未修）：`collect_host_dts_unittest_errors()`（当时在 `generate.py:3462`，
2026-11-15 已搬进 `build/checks/dts.py`）
在 `tests/test_dts.py` 不存在时直接 `return []`——**文件被删后 `verify` 仍 exit 0**，
DTS 单测整段静默跳过。这正是「零用例也算成功」的窗口（docstring 里只兜住了 discover 跑 0 条的情形）。

## 5. 修复

第一步取回库里版本：

```powershell
git checkout HEAD -- tests/test_dts.py     # exit 0，60 行
```

**关键发现**：HEAD 版本是 DTS 搬迁**之前**的旧形态（`apollo-f429.dts` 路径、`parse_board_dts`
之外的旧 API 组合），本会话一直在用的 19 条版本是**未提交的工作区改动**，随本次删除一起丢失，
git 无法恢复。因此不能「恢复 + 改名」，只能**按当前生成链重写**。

重写后的 `tests/test_dts.py`（196 行、19 条用例）用当前 `project/generate.py` 的真实 API，
覆盖四个层次：

| 组 | 用例 | 依赖的真实 API |
|---|---|---|
| `DtsParserTests` | 板级事实、嵌套块提取、拓扑单根、同目录 include、phandle gpios、SoC 片段 9 控制器 | `parse_board_dts` / `extract_named_block` / `collect_board_dts_topology_errors` / `strip_c_comments` |
| `PinmapGeneratedTests` | 生成路径在 `soc/`、140 条、B1 可查、pin label 与索引一致、片段无根 | `platform_pinmap_dtsi_path` / `parse_pinmap_index` |
| `OverlayGeneratedTests` | 生成路径在 `boards/`、140 个 `&gpio_p*` 覆盖 A–I、片段无根 | `board_pinmap_overlay_dtsi_path` / `board_pinmap_overlay_has_entries` |
| `EdtConsistencyTests` | bundle 与板级 DTS 同步、EDT 事实与 `parse_board_dts` 逐项一致、`chosen.stdout-path` 解析到 `serial@40011000` | `collect_edt_bundle_dts_errors` / `load_board_edt_session` / `run_edt` |
| `BoardRegistryTests` | 6 条 bsl 契约行、10 份 binding 均含 `compatible`/`properties` | `collect_board_registry_entries` |

EDT 用例在 `devicetree` 包缺失时用 `skipTest` 跳过（`gen._edt_unavailable_reason()`），
保持旧文件的「无 EDT 环境也能跑其余项」行为。

影响面：只改测试文件；未改生成器、门禁与固件代码。

## 6. 修复后验证

| 命令 | 期望 | 结果 |
|------|------|------|
| `Test-Path tests\test_dts.py` | True | **True** |
| `python -m unittest tests.test_dts` | 19 条 OK | **Ran 19 tests … OK** |
| `git status --porcelain -- tests` | ` M tests/test_dts.py`（不再是 `D`） | **` M tests/test_dts.py`** |
| `python build\build.py update` | exit 0 + 自检通过 | **exit 0，`自检通过`** |
| `python build\build.py verify --artifacts` | exit 0 | **exit 0** |
| `python build\build.py build --no-update` | exit 0，产出 elf | **exit 0** |

产物：`out/user/release/2026_11_15-00_06_07/stm32-freertos.elf` 41216 B，
SHA256 前 16 位 `299E4FDDE46BB94F`（与删除前的产出同哈希，说明固件内容未受影响）。

## 7. 遗留

- 工作区里仍有若干与本轮无关的 `D` 项（`build/contracts.py`、`build/environment.py`、
  `docs/hardware/datasheet/**`、`project/config/project_defconfig_*` 等），来源不明，
  未在本轮处理；如需清理/恢复请单独确认，避免误判为本轮产物。
- 建议后续在 `update` / `verify` 里加一条「`tests/test_dts.py` 存在且可导入」的存在性断言，
  让这类缺失在门禁里直接暴露，而不是等人工跑单测才发现。此条**未实施**（属新增门禁，需你确认）。
- **后续处置（2026-11-15，已按用户指示落地）**：不再给独立测试文件加存在性断言，而是把 19 条用例
  整体移植为 `build/checks/dts.py` 的 `collect_host_dts_errors()`（进程内直调 `generate` 的解析/EDT
  helper），`tests/test_dts.py` 已删除、`ASSERTION_GROUPS["device-dts"]` 改为该函数名。这样「文件缺失
  即静默放行」的口子从根上消失——不再有独立文件可缺。定向破坏复核：把 `apollo.dts` 的 `reg = <32>;`
  改成 `<33>`，`verify` exit 1 并打出 `board.pcf8574_addr7 期望 32，实际 33`（同时旧判据也报
  `parse_board_dts 与 EDT 不一致`），还原后 exit 0。
- 本次只重建了 `tests/test_dts.py`。若原先还有其它 `tests/test_*.py` 也属未提交改动，
  本次删除同样不可恢复；当前 `tests/` 下只剩 `test_dts.py` 与 `test_case.md`。

## 复跑记录

```text
PS> python -m unittest tests.test_dts
...................
Ran 19 tests in 0.414s
OK

PS> python build\build.py update
已生成 project/dts/soc/stm32f429-gpio-default.dtsi（140 脚，平台默认）
已生成 project/dts/boards/apollo-gpio-overlay.dtsi（板级 &gpio_p* 覆盖）
已根据 DTS 生成 bsl/inc/board_dts.h、board_device_registry.h、board_debug_uart_dt.h、
  osl/inc/board_panic_hw.h（EDT，project/dts/boards/apollo.dts）
自检：代码生成物与 .skills 规格
自检通过                                    → update_exit=0

PS> python build\build.py verify --artifacts  → verify_exit=0

PS> python build\build.py build --no-update
复制 …stm32-freertos.bin → 14428 字节
复制 …stm32-freertos.elf → 41216 字节
复制 …stm32-freertos.hex → 40651 字节
复制 …stm32-freertos.map → 104385 字节  → build_exit=0
```

改名后重写测试时踩到 3 处自造错误（已修，记录备查）：
`/dts-v1/` 命中注释文字（须先 `strip_c_comments`）、overlay `&gpio_p*` 端口字母是小写、
pin label 需去掉 `gpio_p` 前缀才能与索引键 `(端口, 编号)` 对齐。
