# 文档索引

## 读者文档（`docs/design/`）

| 文档 | 说明 |
|------|------|
| [系统设计.md](./系统设计.md) | **工程搭建与生成链** + 五层、7 条边、自检现状（门禁已删）、启动时序、运行期稳态、**故障码表**、现态 size |
| [用户指南.md](./用户指南.md) | **按顺序上手**：系统构建（真源/生成物）→ 编译（两档与告警即错误）→ 刷机 → 上板首次验证 → 调试（串口/GDB/`capture.py`/panic 现场）→ 排障与提交前检查 |
| [搭建环境.md](./搭建环境.md) | 工具链/OpenOCD/ST-Link、刷机（含 **刷机连接示意**）；`build/build.py`（编译+工具链）/ `flash/flash.py` |
| [功能实现对照表.md](./功能实现对照表.md) | 参考工程（实验63 V2.6）**24 应用 / 18 中间件 / 38 BSP** → `bsl`/`fml`/`apl` 落点、命名、DTB 节点与 P0~P6 批次（分支 `feature/full-demo`） |

架构机制与历史记录：[.skills/架构缺陷.md](../../.skills/架构缺陷.md)（**勿**在 `docs/archive/` 下新建归档文档；历史用 git）。

`USE_FULL_ASSERT`：见 [.skills/工程规格.md](../../.skills/工程规格.md)「HAL 配置」· **USE_FULL_ASSERT**。

## 参考资料（Git LFS）

| 目录 | 内容 |
|------|------|
| `docs/reference/` | 器件/库手册 PDF |
| `docs/hardware/` | 原理图与芯片手册 PDF |

克隆前执行 `git lfs install`；指针文件约 132 B。补拉：`git lfs pull` / `git lfs checkout`。详见下文命令块。

```text
git lfs install
git clone <repo>
git lfs pull
git lfs checkout
```

## Agent / 维护（`.skills/`）

| 文档 | 说明 |
|------|------|
| [工程规格.md](../../.skills/工程规格.md) | 与代码一致（**冲突时以此为准**；含搭建手稿、框架图；门禁章节已标注为历史） |
| [架构缺陷.md](../../.skills/架构缺陷.md) | 架构机制与历史记录 |
| [SKILL.md](../../.skills/SKILL.md) | Agent 流程 |
