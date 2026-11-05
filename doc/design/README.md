# 文档索引

## 用户文档（`doc/`）

| 文档 | 说明 |
|------|------|
| [开发环境配置.md](./开发环境配置.md) | 工具链、`create-project` / `install-toolchain` / `set-buildenv`、Windows 本机构建 |
| [调试环境配置.md](./调试环境配置.md) | GDB 客户端、连接方式、读 `g_panic_record` / 硬件异常现场 |
| [工程设计手稿.md](./工程设计手稿.md) | 搭建与生成链设计的**读者版**（权威副本是 `.skills/设计手稿.md`） |

## 参考资料（`doc/reference/`，走 Git LFS）

`doc/reference/` 下的图片与 PDF 由 `.gitattributes` 交给 Git LFS 管理（仓库里存 131 B 指针，
checkout 时还原真文件）。**克隆前请先装 `git lfs` 并在仓库内执行一次 `git lfs install`**，
否则拿到的是一堆指针文件：图片打不开、PDF 0 字节，而 `git status` 却显示干净。

```text
git lfs install
git clone <repo>            # 已装 lfs 的机器上 clone 会自动拉取真实文件
git lfs pull                # 若 clone 时没装 lfs，事后补拉
```

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
