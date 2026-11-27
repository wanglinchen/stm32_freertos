---
name: test-stm32f429
description: >-
  STM32F429IGT6 分层固件工程（Cortex-M4、Kconfig/Makefile 生成链、工程内工具链）。
  修改 apl/fml/bsl/hal/osl、build/build.py、构建/链接行为或目录结构时使用；
  任何实现变更须同步更新 .skills 规格与 docs/，并在 update + build 通过后才算完成。
---

# 本工程 Agent Skill

## 必读顺序

1. [工程规格.md](./工程规格.md) — 目录、构建、链接、Kconfig、搭建手稿与框架图
2. [文档维护.md](./文档维护.md) — 改代码后必须更新哪些文档
3. [架构缺陷.md](./架构缺陷.md) — 架构机制（按机制查，不按编号猜）

用户可见说明：`docs/design/`（索引 [README.md](../docs/design/README.md)，搭建环境 [搭建环境.md](../docs/design/搭建环境.md)，上手全流程 [用户指南.md](../docs/design/用户指南.md)）。

Windows：`python3` → `python`；`make` 由 `build` 内部调用 `tool/win_x86_64/bin/make.exe`，临时目录 `out/tmp/`。

## 快速命令

```text
python build/build.py --help
python build/build.py update    # 刷新 Makefile/Kconfig + pinmap 脚本产 dtsi(→project/dts) + 合成并编译 dtb(→out/dts)
python build/build.py build     # 两档编译（userdebug / user）
python build/build.py clean
python flash/flash.py          # 刷机（默认无子命令即烧录）
python build/build.py install-toolchain  # 工具链
```

## 开工前核对（必做，先于写代码）

**每次动代码之前**（新批次、批次里夹的单点改动都算），先打开
[docs/design/功能实现对照表.md](../docs/design/功能实现对照表.md) §0.1，逐条核四件事，
**把核对结果写进这一轮的回复**（用户要能当场纠正），再动手：

1. **条目在不在**：本批在 §4 批次路线里有行，或能归到已有某一行。查不到的落点先改表并提交，
   不许"先做完再补表"。
2. **路径逐字对齐**：§4 的「新增文件（精确）」列就是层 + 目录 + 文件名，不临时改名、不加层、
   不把文件塞进表里没写的目录。
3. **命名规约**：`board_<板级>` / `bsl_<设备>` / `fml_<中间件>` / `app_<应用>`、
   `bsl_api_*` / `bsl_devapi_*`、`BSL_DEV_*` 枚举名，与本批涉及的对象一一对上（§2 六步 + §3）。
4. **验收口径**：本批 POST 项名与 §1.1 登记的项名一致，判据写得出"什么数字算过"；
   新增项要同时改 §1.1 项数与 §8 的证据文件命名。

收工回填同一张表四处（§4 状态列、§1.3/§1.4 对应行、§1.1 项数、§9 变更记录），
一致性判据：任挑一处读，得到的"做到哪一步、靠什么证明"与另外三处相同。
**任务完成标准因此是"代码 / 对照表 / 证据三者一致"，不是"代码能跑"。**

## 更新后自检（必做）

1. 按 [文档维护.md](./文档维护.md) 同步 `.skills` / `docs/`。
2. `python build/build.py update`：必须零诊断（dtc 的 Warning 也算失败），并打印
   `已生成 out/dts/apollo.dts → …apollo.dtb（N 字节）`。
3. 改源码或链接后：`python build/build.py build`，退出码 0 且 `-Werror` 全过；产物断言
   失败先查链接顺序 / 弱符号 / 内存，勿只看 make 退出码。
4. 动过 DTS / pinmap：连跑两次 `update`，`project/dts` 与 `out/dts` 的文件 SHA256 必须不变（幂等），
   否则说明生成链里混进了时间戳或未初始化顺序。
5. 动过度层边界（新增 include / 跨层调用）：按 [架构缺陷.md](./架构缺陷.md) 第三轮第 6 节的方法重扫
   层边与 SCC，层边数变化要同步 `docs/design/系统设计.md` 与 `工程规格.md`。
6. 收尾跑一次 `python build/build.py check`：七块判据 —— A 层边/越级、B 文件级与层间环、C DTB 与 elf 内容等长 + 关键符号、
   D DTS 派生链（`gen_dts.py` 重跑到系统 temp 目录，与 `project/dts` 两份 dtsi 逐字节比，再用
   `generate.build_dts_text()` 复算 `out/dts/apollo.dts`）、E 自研头守卫、F 忽略规则（`out/` 整目录都必须被 `.gitignore` 覆盖，
   含 `out/<档位>/.config`）、G API 面（头里声明的函数必须在 `nm` 里有符号 —— `--gc-sections` 会把零调用函数丢掉，
   只剩一条悬空声明）。它只读盘查、幂等、不设基线数字（不是门禁）；层边处数变了只打印不判失败，
   越级、成环、产物不一致、派生链过期、守卫缺陷、假 API 才失败。
7. 增删公开 API：`arm-none-eabi-nm out/<variant>/objects/target/stm32-freertos.elf | findstr <符号>`
   确认它到底进没进镜像。**判死代码不能只看 `name(` 出现次数** —— `board_led_*` / `board_key_*` /
   `board_beep_*` 只通过 `bsl_devapi_*()` 的函数指针表被调，文本计数是 1，但不是死代码。
8. 改实现顺带改同文件的头注释（`bsl_fdt_prop_flag` 那种"实现已按值判、注释还写存在即真"的
   漂移，会让下一轮照旧注释再犯一次）。
9. **层内共享 static 被多任务访问时必须先有机制**：bsl 不能引内核头（`LAYER_DEPENDENCIES["bsl"]` 只允许 hal），
   所以用 `__get_PRIMASK()` → `__disable_irq()` → 退出 `__set_PRIMASK()` 的保存-恢复式临界区；
   临界区里**只**做内存操作，任何 UART/延时放到区外。`bsl_log` 的 ring / `s_line` / `s_head` 就是这么补上的
   （第七轮 P1：两个任务同时写，日志行会拼接、槽位会丢，比丢行更误导）。
10. **占用的标志必须能还回去**：`s_uart_busy` 这类"占住防重入"的位，要配超时自愈（`HAL_GetTick` 差值 ≥ 50 ms 强制收回）
    与丢弃计数，并把计数打在 banner 与 panic dump 上（第九轮）。否则卡一次就永久静默——
    现场分不清"日志本来没打"与"串口一直在丢"，而这正是排查最怕的形态。

**门禁已整体删除（2026-11-22）**：`build/checks/`、`build/verify.py`、`verify` 子命令、
`gate-selftest` 子命令与 CI 红测矩阵都不再存在，待重新设计。此前 54 组门禁、产物断言、
`--assert-map` 的描述仅作历史参考，见 [工程规格.md](./工程规格.md) 顶部横幅。

## 故障排查

编译 / 刷机 / 调试环境：[docs/design/搭建环境.md](../docs/design/搭建环境.md)。

**遇到任何非预期现象**（编译失败、`update` 报错、dtc 诊断、行为回归）：先在仓库根建
`debug/<问题>_YYYY_MM_DD-HH_MM_SS/`，在其中写 `analyze.md`（现象 + 命令与 EXIT 码 + 复现 +
已排除 + 根因 `path:line` + 修复与影响面 + 验证证据 + 遗留），边查边写，再动代码。

## 硬规则

- **蜂鸣器永久禁用（2026-11-25 用户明确要求：「关闭蜂鸣器，以后都不要打开」）**。
  这一条不随批次变化，也不要为任何演示/自检/告警"临时"打开：
  - 实现上断在**唯一执行器** `bsl/src/board_beep.c` 的 `BEEP_ALLOW_ON = 0` ——
    `board_beep_set(1)` / `bsl_devapi_beep()->on()` 一律被吞掉成 0，并打一行
    `[beep] on request ignored: buzzer disabled by policy`（一家人不见响的时候，
    这行是唯一能说明"被策略拦下"而不是"驱动坏了"的证据）。
  - **不要**把 `BEEP_ALLOW_ON` 改成 1；**不要**绕过 `board_beep_set` 直接写
    PCF8574 的 P0（BEEP）；**不要**新增任何会响的命令或应用 —— `fml_shell` 的
    `beep` 命令因此已删除，不要加回来。
  - `app_beep` 保留（对照表里它是参考工程的 24 个应用之一），但标题写
    `BEEP - disabled`、日志写 `req on (muted)`：请求到执行器、声音不发出。
    它现在只用于验证"按压 → 请求"这条链路，不是发声功能。
  - 需要提示人的地方用**屏幕**或**串口日志**，不要用声音；panic 播报也不借蜂鸣器
    （`board_panic_beep_silence()` 仍然每轮调用，保证静音状态是显式设置过的）。
- **不要手改**自动生成的 `Makefile` / `Kconfig` / `toolchain.make`（除非随后 `update`）；
  `project/config/stm32f429_defconfig_common`（公共项，两档共用）与 `stm32f429_defconfig_<variant>`（档位差异层）里的 `# >>> USER_CUSTOM` 区可长期手改；`build` 时按「common → 档位」合并，同名符号档位优先。
- 增删参与编译的层内源文件或子目录后：**`update`**，再视需要 `build`。
- `build/` 不参与 Kconfig 扫描；`.config` 在 `out/`（不入库，`update` 生成）。
- **板级事实只写一次**：引脚 / 极性 / 波特率写在 `docs/hardware/pinmap/*.xlsx`，由
  `docs/hardware/pinmap/gen_dts.py` 产 dtsi，`bsl/src/board_rt.c` 在运行期从内嵌 DTB 读回。
  C 里不得再出现第二份引脚常量；`osl/inc/board_panic_hw.h` 是唯一例外（panic 路径不能遍历 FDT）。
- **目录落点按「源 / 产物」分**：入库的源只有 `project/dts/*.dtsi`（由 xlsx 唯一决定）；
  `apollo.dts` / `apollo.dtb` 是构建中间产物，落 `out/dts/`（已在 `.gitignore`，`clean` 清干净）。
  同一个 `gen_dts.py` 不许在别处再产副本——曾经 `docs/hardware/pinmap/generated/` 的双份副本
  已删除，因为两条更新路径必然漂移。改插件脚点改 xlsx，改节点结构改 `generate.py` 的 `_BOARD_NODES`。
- **"可优化"不等于"该改"**：动手前先判代价。反例（第五轮否掉）：把 vendored FreeRTOS 迁出
  `osl/` 看着"干净"，但会破坏 upstream 目录形状（升级时无法整块替换）并重做目录扫描与 30 个头
  的 `-I`；`fml` 只剩 54 行也不是空壳，它是"倒日志 + 转码表"的单一门面，删了会让 apl 多依赖一层。
  台账里这类条目要写成**取舍**（决定 + 理由），而不是永远挂着的"未决"。
- **解析了的事实必须有人消费**：DTB 属性被读进 `s_rt` 之后，要么有运行期消费者，要么在
  `board_system_init()` 这类入口做对拍断言；只解析不校验等于给后来的人一个"已经检查过"的错觉
  （`hse-frequency` 第四轮就是这种形态，已改为与 `HSE_VALUE` 对拍、码 3）。
- **`out/` 整目录不入库**：`objects/`、`release/<时间戳>/`、`tmp/`、`dts/`、`tests/`、
  `<档位>/.config`、临时脚本与采集日志全部是本地产物，`.gitignore` 用一条 `out/` 覆盖；
  `check` 的 F 段逐条核对有没有漏网的。clone 后先 `python build/build.py update` 再 `build`。
  构建产物永远不当证据提交。
- **一次性脚本与提交信息不落仓库目录**：临时 python / ps1 补丁脚本、commit message
  一律写系统 temp（`$env:TEMP\…`）或 `logs/`；`out/` 只有构建产物（`objects/`、`release/`、
  `dts/`、`<档位>/`），仓库根或 `out/` 里出现 `tmp_*.py` / `tmp_*.ps1` / `tmp_*.txt`
  就是落点错了，收尾清掉。日志照旧落 `logs/`。
- **所有过程日志只落 `logs/`（2026-11-27 用户要求）**：编译、调试、刷写、openocd、gdb、串口采集
  产生的 `*.log` / `*.err` 一律写仓库根 `logs/`（`.gitignore` 已忽略），包括脚本默认落点与一次性
  PowerShell 重定向；仓库根和 `out/` 下不留 `*.log` / `*.err`。要留证据请整理成
  `docs/design/串口自检_<n>项_<date>.txt`，**不要拿 logs 里的过程日志当证据**。文档里引用过程日志
  时写 `logs/<文件>`，不要写 `out/<文件>`。
- 任务完成标准：文档与代码一致，且 **`update` + `build` 均通过**。
