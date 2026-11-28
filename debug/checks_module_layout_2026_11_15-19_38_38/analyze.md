# `build/checks` 模块划分不系统：按门禁组分而非按被检查对象

时间：2026-11-15 19:38
范围：`build/checks/*.py`（判据实现）、`build/verify.py`（编排）、`.skills` / `docs/design` 口径。

## 1. 现象（用户诉求）

> `build/checks` 目录下的文件划分不够系统，为什么有的模块没有呢？系统性整理一下这些文件。

「有的模块没有」= 仓库有 5 个代码层目录（`apl` `fml` `bsl` `osl` `hal`），checks 里却没有 `fml.py` / `apl.py`；同时 `osl.py` 一个文件 1604 行、19 条判据。

## 2. 盘点（AST 提取，未改动前）

52 条 `collect_*`（不含 `verify.py` 里的组合器 `collect_verify_errors`，含它共 53）分布：

| 模块 | 行数 | 判据 |
| --- | --- | --- |
| `_gate.py` | 2834 | （门禁自测，非判据） |
| `osl.py` | 1604 | 19 |
| `hal.py` | 576 | 7 |
| `_support.py` | 517 | （公共底座） |
| `artifact.py` | 435 | 6 |
| `makefile.py` | 302 | 5 |
| `dts.py` | 254 | 2 |
| `layout.py` | 229 | 4 |
| `docs.py` | 214 | 3 |
| `bsl.py` | 177 | 2 |
| `config.py` | 170 | 4 |
| `__init__.py` | 8 | — |

## 3. 根因（三处错位，均有代码坐标）

### 3.1 缺 `fml` / `apl` 两个模块

- `collect_periodic_task_errors` / `collect_task_priority_errors` / `collect_task_name_len_errors` 的检查对象是
  `TASK_SOURCES = ("fml/src/fml_led.c", "fml/src/fml_button.c")`（`project/generate.py:359`，另见 `:388` `HEALTH_CALL_SOURCES`、`:392` `TASK_NAME_SOURCES` 同指这两个文件），
  却全部塞在 `osl.py` —— 因为拆分时它们和 `configMAX_PRIORITIES` / `configMAX_TASK_NAME_LEN` 一起被归到「osl 门禁组」。
  判据是**跨层的**（配置在 osl、任务在 fml），但被检查对象明确是 fml。
- `apl/` 目录只有 5 个文件（`Kconfig` / `Makefile` / `src/{Kconfig,main.c,Makefile}`），没有任何 checks 模块对应它，
  于是 apl 侧的启动顺序判据没地方放。

### 3.2 `collect_newlib_preboot_errors` 主体是 apl，却放在 `makefile.py`

`build/checks/makefile.py:240` 的 `main_rel = "apl/src/main.c"`：这条判据读 `main.c` 的启动顺序，
判「调度器起来之前不许调 newlib 的分配/格式化」，与 Makefile 无关。

### 3.3 `collect_layer_dependency_errors` 是层次契约，却放在 `makefile.py`

它按 `LAYER_DEPENDENCIES` 查**跨层 include 边**，和「构建描述」是两类东西：
`makefile` 组查的是「构建有没有漏挂 lib、链接顺序对不对」，这条查的是「源码有没有越界包含」。

### 3.4 `osl.py` 混了四类主题

RTOS 运行时（任务优先级/任务名/周期任务）、panic 族（9 条，横跨 `osl_panic.c` 与 `fml_panic.c`）、
启动/时基（`boot_init` / `tick_source` / `last_write` / `persist_record`）、健康槽位（`health_slot` / `health_writer`）。
1604 行一个文件，读一条判据要滚动整个文件。

## 4. 处置：按「层 + 契约 + 产物」重整为 12 个模块

原则：**一个模块 = 一类被检查对象**；只搬位置，判据文本与函数名一个字都不改
（因此 `ASSERTION_GROUPS`（按函数名映射）、`install_assert_trace()` 插桩、`build.py` 的 `verify.<判据名>` 访问路径零影响）。

| 类 | 模块 | 判据 | 说明 |
| --- | --- | --- | --- |
| 骨架与契约 | `layout.py` | 5 | layout / topology / **layer_dependency** / eol_contract / line_ending |
| | `makefile.py` | 3 | makefile / stale_dependency / warning_gate |
| | `config.py` | 4 | config / defconfig_user_custom_sync / build_variant_buildflags / memory_metadata |
| | `docs.py` | 3 | doc_contract / doc_count / doc_reference |
| | `dts.py` | 2 | device_dts / host_dts |
| 代码层 | `hal.py` | 7 | 不变 |
| | `bsl.py` | 2 | 不变 |
| | `osl.py` | 7 | health_writer / health_slot / observ_artifact / boot_init / tick_source / last_write / persist_record |
| | `fml.py` | 3 | periodic_task / task_priority / task_name_len（**新建**） |
| | `apl.py` | 1 | newlib_preboot（**新建**，自 `makefile.py` 移入） |
| 横切主题 | `panic.py` | 9 | panic_code / entry_ownership / observability / task_name / assert_task / scene / generation / order / fault_range（**新建**） |
| 产物 | `artifact.py` | 6 | 不变 |

`panic` 单列的理由：它的对象横跨 `osl/src/osl_panic.c` 与 `fml/src/fml_panic.c`，
「panic 入口该归哪一层」「哪一层允许写记录」本身就是跨层契约，塞进任一单层模块都讲不清；
`artifact` 同理，是唯一按「构建输出」而非「源码层」归拢的模块。

搬后行数（实测）：`osl.py` 1604 → **733**；新建 `panic.py` 696、`fml.py` 185、`apl.py` 49；
`layout.py` 229 → 297、`makefile.py` 302 → 190；`__init__.py` 8 → 30（12 模块 docstring + `MODULES`）。

同步改动：
- `build/checks/__init__.py`：`MODULES = (layout, makefile, config, docs, dts, hal, bsl, osl, fml, apl, panic, artifact)`（顺序即层序）。
- `build/verify.py`：导入块按新模块重排（`VERIFY_COLLECTORS` 与 `verify()` 调用顺序未动，只按名字引用）。
- 文档口径：`.skills/工程规格.md:14`/`:208`（「9 个模块」→ 12）、`.skills/SKILL.md:40`、`.skills/架构缺陷.md:150` 后新增台账、`docs/design/系统设计.md:160`。

## 5. 一个踩到的坑（已修）

搬运脚本用 `Path.write_text()`，在 Windows 上默认写 CRLF，`update` 直接报
`build/checks/osl.py: 工作区有 789 处 CRLF，但 .gitattributes 钉了 eol=lf`（exit 1）。
按字节 `replace(b"\r\n", b"\n")` 归一化 6 个文件（apl/fml/layout/makefile/osl/panic）后转绿。

## 6. 验证

| 动作 | 结果 |
| --- | --- |
| `python -m py_compile build/checks/*.py build/verify.py` | exit 0 |
| `python build\build.py update` | exit 0，「自检通过」 |
| `python build\build.py verify --artifacts` | exit 0，elf text=14420 B、Flash 1.38%、RAM 9.43% |
| `python project\generate.py check` | 10 项 0 失败，exit 0 |
| `gate-selftest --only layer-deps` | 1 组 180.0 s：**1 PASS / 0 FAIL**（「apl 加回 hal 头、bsl 塞进 fml 头、fml 加 hal 头都报越级 include……源码已按字节还原」） |
| `gate-selftest --only period` | 1 组 116.1 s：**1 PASS / 0 FAIL** |
| `gate-selftest --only newlib-preboot` | 1 组 115.3 s：**1 PASS / 0 FAIL** |
| `gate-selftest --only panic-code` | 1 组 207.5 s：**1 PASS / 0 FAIL** |
| `gate-selftest --only doc-contract` | 1 组 110.5 s：**1 PASS / 0 FAIL** |
| `gate-selftest --list` | exit 0（51 组枚举正常，`assertion_coverage()` 双向比对通过） |
| `python build\build.py build --no-update` | exit 0；elf 41144 B、hex 40696 B、bin 14444 B、map 126650 B；**SHA256 前 16 位 `E5D44A4D166D7D19`，与改动前一致** → 判据搬家不动固件 |

这五组正好覆盖被搬动的判据：`layer-deps`（→ layout）、`period`（→ fml）、`newlib-preboot`（→ apl）、`panic-code`（→ panic），
四组都仍按原样「破坏 → 报红 → 按字节还原」，证明插桩（`install_assert_trace()` 按 `checks.MODULES` 枚举）在 12 模块下依然命中。
（判据一个都没有被削弱：搬运不改函数体。）
