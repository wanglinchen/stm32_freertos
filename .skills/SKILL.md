---
name: test-stm32f429
description: >-
  STM32F429IGT6 分层固件工程（Cortex-M4、Kconfig/Makefile 生成链、工程内工具链）。
  修改 apl/fml/bsl/hal/osl、build/create-project.py、构建/链接行为或目录结构时使用；
  任何实现变更须同步更新 .skills 规格与 doc/，并在 update/verify 自检通过后才算完成。
---

# 本工程 Agent Skill

## 必读顺序

1. [工程规格.md](./工程规格.md) — 目录、构建、链接、Kconfig、脚本行为的**现行规格**（与代码一致）
2. [文档维护.md](./文档维护.md) — 改代码后必须更新哪些文档
3. [设计手稿.md](./设计手稿.md) — 搭建与设计 checklist（含工程框架图；细节冲突时以工程规格为准）

用户可见说明在仓库 **`doc/`**（`使用说明.md`、`工具链安装.md`）。

## 快速命令

```text
python3 build/create-project.py --help
python3 build/install-toolchain.py --help
python3 build/create-project.py update    # 刷新生成物 + 自动 verify
python3 build/create-project.py verify    # 仅自检（改 .skills 后也可单独跑）
python3 build/create-project.py build
python3 build/install-toolchain.py
```

## 更新后自检（必做）

凡改动 **源码树、层目录、链接顺序、`build/create-project.py` / `build/install-toolchain.py` / `build/set-buildenv.py`、CMSIS 启动约定**，Agent 须按顺序完成，**不得跳过 verify**：

1. **同步规格文档**：按 [文档维护.md](./文档维护.md) 更新 `.skills`（必要时 `doc/`）。
2. **刷新生成物**：`python3 build/create-project.py update`（末尾会自动执行 `verify`）。
3. **处理失败项**：若 verify 报错，根据终端列出的项修正 **代码 / 生成逻辑 / `.skills`**，再跑 `update` 或 `verify`，直到输出「自检通过」。
4. **编译（可选）**：需要确认能链接时再 `python3 build/create-project.py build`。

仅改 `.skills` 文案、未改 `create-project.py` 与目录时，可只跑 `python3 build/create-project.py verify` 确认与仓库仍一致。

### verify 检查什么

| 类别 | 内容 |
|------|------|
| 生成物 | 根与各层 `Makefile`、`toolchain.make`、`proj/project_defconfig`、解析后的 `out/.config`；根链接行含各 `lib*.a` |
| 工程元数据 | `project-info.json` 的 `project_name` 与根目录名、`CONFIG_PROJECT_NAME` 一致 |
| 目录 | 无排除目录内误生成的 `Makefile`/`Kconfig`；无未在 `LAYER_DIRS` 声明的顶层目录 |
| 文档 | [工程规格.md](./工程规格.md) 含当前层名、扫描排除项、链接顺序；[SKILL.md](./SKILL.md) 含 `verify` 与各层名；[设计手稿.md](./设计手稿.md) 中 lib 链接链与现网一致 |

实现细节与链接顺序以 [工程规格.md](./工程规格.md) 及 `build/create-project.py` 中 `STATIC_LIB_DIRS` / `NORMAL_LINK_LIBS` 为准。

## 硬规则

- **不要手改**自动生成的根/层 `Makefile`、`Kconfig`、根目录 `toolchain.make`（除非用户明确要求且随后会再跑 `update`）。
- 增删 `apl` / `fml` / `bsl` / `hal` / `osl` 下参与编译的源文件或子目录后，执行 **`python3 build/create-project.py update`**（含自检），再视需要 `build`。
- `build/` 存放生成器和工具链辅助脚本，不参与 Kconfig/Makefile 扫描；解析后的 `.config` 位于 `out/`。
- **实现类修改未完成的标准**：`.skills` / `doc/` 与代码不一致，或 **`verify` 未通过**。
