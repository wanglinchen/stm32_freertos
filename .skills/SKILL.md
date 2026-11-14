---
name: test-stm32f429
description: >-
  STM32F429IGT6 分层固件工程（Cortex-M4、Kconfig/Makefile 生成链、工程内工具链）。
  修改 apl/fml/bsl/hal/osl、build/build.py、构建/链接行为或目录结构时使用；
  任何实现变更须同步更新 .skills 规格与 doc/，并在 update/verify 自检通过后才算完成。
---

# 本工程 Agent Skill

## 必读顺序

1. [工程规格.md](./工程规格.md) — 目录、构建、链接、Kconfig、搭建手稿与框架图、**完整 verify / 门禁说明**
2. [文档维护.md](./文档维护.md) — 改代码后必须更新哪些文档
3. [架构缺陷.md](./架构缺陷.md) — 架构机制与守门（按机制查，不按编号猜）

用户可见说明：`doc/design/`（索引 [README.md](../doc/design/README.md)，搭建环境 [搭建环境.md](../doc/design/搭建环境.md)）。

Windows：`python3` → `python`；`make` 由 `build` 内部调用 `tool/win_x86_64/bin/make.exe`，临时目录 `out/tmp/`。

## 快速命令

```text
python build/build.py --help
python build/build.py update    # 刷新生成物 + 自动 verify
python build/build.py verify
python build/build.py build
python build/build.py gate-selftest --list
python build/build.py gate-selftest --only <组名>
python flash/flash.py          # 刷机（默认无子命令即烧录）
python build/build.py install-toolchain  # 工具链
```

## 更新后自检（必做）

1. 按 [文档维护.md](./文档维护.md) 同步 `.skills` / `doc/`。
2. `python build/build.py update`（或改文案后单独 `verify`）直到「自检通过」。
3. 改源码或链接后：`python build/build.py build`；产物断言失败先查链接顺序 / 弱符号 / 内存，勿只看 make 退出码。

**verify / 产物断言 / 49 组 gate-selftest 的完整条款**见 [工程规格.md](./工程规格.md)「自检（verify）」「发布构建」；改断言或新增 `collect_*` 后须跑对应 `gate-selftest` 组（`ASSERTION_GROUPS` 勿手填，用 `--assert-map`）。

## 故障排查

编译 / 刷机 / 调试环境：[doc/design/搭建环境.md](../doc/design/搭建环境.md)。

## 硬规则

- **不要手改**自动生成的 `Makefile` / `Kconfig` / `toolchain.make`（除非随后 `update`）。
- 增删参与编译的层内源文件或子目录后：**`update`**，再视需要 `build`。
- `build/` 不参与 Kconfig 扫描；`.config` 在 `out/`。
- 任务完成标准：文档与代码一致，且 **`verify` 通过**。
