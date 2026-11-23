# stm32-freertos

STM32F429IGT6（正点原子 Apollo）+ FreeRTOS 的五层固件模板：`apl` / `fml` / `bsl` / `osl` / `hal`。

## 克隆与参考资料

大量 PDF 走 Git LFS；未装 `git lfs` 时 checkout 只有约 132 B 指针且 `git status` 仍可能显示干净。**完整说明见 [docs/design/README.md](docs/design/README.md)（参考资料 · Git LFS）。**

```text
git lfs install
git clone <repo>
```

## 构建（四条）

```text
python build/build.py build     # update + userdebug/user 双档编译 → out/<档位>/objects/target 与 release/
python build/build.py build --fast --variant userdebug   # 日常增量：跳过 update，只编调试档
python build/build.py update    # 只刷新生成物：Makefile/Kconfig + pinmap 脚本产 DTS/DTB
python build/build.py clean
```

编译、刷机、调试环境：**[docs/design/搭建环境.md](docs/design/搭建环境.md)**。刷机：`python flash/flash.py`（须先 `build` 与 `install-openocd`）。

增量构建出现 `Nothing to be done` 不能当验证；要确认编译过，须 `clean` 后全量 `build`。

板级事实唯一来源是 `docs/hardware/pinmap/*.xlsx`：`update` 跑该目录的 `gen_dts.py` 产 default/overlay 两份 dtsi，合成 `out/dts/apollo.dts` 并用 dtc 编成 `apollo.dtb`（内嵌进固件 `.dtb` 段，运行期由 `bsl_fdt` + `bsl/board_rt.c` 读回）。C 里不再有第二份引脚常量。

门禁（`build/checks/`、`verify`、`gate-selftest`）已于 2026-11-22 整体删除，待重新构建；现阶段自检口径就是 `update` 零诊断 + `build` 零告警。

## 文档入口

| 文档 | 看什么 |
|---|---|
| [docs/design/README.md](docs/design/README.md) | 设计文档索引（**先读**） |
| [docs/design/系统设计.md](docs/design/系统设计.md) | 工程搭建/生成链、五层、构建与 DTS 链、运行期、故障码、**现态 size** |
| [docs/design/搭建环境.md](docs/design/搭建环境.md) | 编译 / 刷机 / 调试环境；板上验收 |
| `.skills/工程规格.md` | 与代码一致的规格（**冲突时以此为准**） |
