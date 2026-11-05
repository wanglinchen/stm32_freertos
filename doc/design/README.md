# 文档索引

## 用户文档（`doc/`）

| 文档 | 说明 |
|------|------|
| [开发环境配置.md](./开发环境配置.md) | 工具链、`create-project` / `install-toolchain` / `set-buildenv`、Windows 本机构建 |
| [调试环境配置.md](./调试环境配置.md) | GDB 客户端、连接方式、读 `g_panic_record` / 硬件异常现场 |
| [工程设计手稿.md](./工程设计手稿.md) | 搭建与生成链设计的**读者版**（权威副本是 `.skills/设计手稿.md`） |

## 参考资料（走 Git LFS）

有两个目录交给 Git LFS 管理，仓库里只存 131 B 指针，checkout 时才还原真文件：

| 目录 | 内容 | 规模 |
|------|------|------|
| `doc/reference/` | 器件/库手册（`*.pdf`、历史遗留 `*.png` 规则仍在） | 56.2 MB |
| `doc/hardware/` | 底板/核心板原理图、芯片数据手册（`*.pdf`） | 43 个文件 66.5 MB |

**克隆前请先装 `git lfs` 并在仓库内执行一次 `git lfs install`**，否则拿到的是指针文件：
PDF 只有 132 B、打不开，而 `git status` 却显示干净（LFS 的 clean filter 对“本身已是指针”
的文件原样透传）。已经克隆过但忘了装 lfs 的，补 `git lfs pull` 再 `git lfs checkout`。

```text
git lfs install
git clone <repo>            # 已装 lfs 的机器上 clone 会自动拉取真实文件
git lfs pull                # 若 clone 时没装 lfs，事后补拉
git lfs checkout            # 工作树里仍是 132 B 指针时，用它还原
```

`doc/hardware/` 的 PDF 是连同历史版本一起迁入 LFS 的，因此**旧提交也需要 LFS**；
查某份文件在历史里是否已完成迁移，用 `git lfs ls-files -l`（`*` = 对象在本地，`-` = 缺失）。

## build 脚本速查

| 脚本 | 常用命令 |
|------|----------|
| `build/create-project.py` | `update` · `verify` · `build` · `clean` |
| `build/install-toolchain.py` | 安装交叉工具链；Windows `--make-only` |
| `build/set-buildenv.py` | `--check` · `--print powershell` |

## Agent / 维护（`.skills/`）

| 文档 | 说明 |
|------|------|
| [.skills/设计手稿.md](../.skills/设计手稿.md) | 搭建与生成链设计（`verify` 校验之一） |
| [.skills/工程规格.md](../.skills/工程规格.md) | 与代码一致的规格（**冲突时以此为准**） |
| [.skills/SKILL.md](../.skills/SKILL.md) | 维护流程与 `verify` |

设计手稿与工程规格冲突时，以 **工程规格** 为准。
