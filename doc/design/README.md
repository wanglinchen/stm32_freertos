# 文档索引

## 读者文档（`doc/design/`）

| 文档 | 说明 |
|------|------|
| [系统设计.md](./系统设计.md) | **工程搭建与生成链** + 五层、7 条边、构建/门禁、启动时序、运行期稳态、**故障码表**、现态 size |
| [搭建环境.md](./搭建环境.md) | 工具链/OpenOCD/ST-Link、刷机（含 **刷机连接示意**）；`build/build.py`（编译+工具链）/ `flash/flash.py` |

架构机制与历史守门：[.skills/架构缺陷.md](../../.skills/架构缺陷.md)（**勿**在 `doc/archive/` 下新建归档文档；`verify` 会拒收该目录）。

`USE_FULL_ASSERT`：见 [.skills/工程规格.md](../../.skills/工程规格.md)「HAL 配置」· **USE_FULL_ASSERT**。

## 参考资料（Git LFS）

| 目录 | 内容 |
|------|------|
| `doc/reference/` | 器件/库手册 PDF |
| `doc/hardware/` | 原理图与芯片手册 PDF |

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
| [工程规格.md](../../.skills/工程规格.md) | 与代码一致（**冲突时以此为准**；含搭建手稿、框架图、verify / 门禁） |
| [架构缺陷.md](../../.skills/架构缺陷.md) | 架构机制与守门 |
| [SKILL.md](../../.skills/SKILL.md) | Agent 流程 |
