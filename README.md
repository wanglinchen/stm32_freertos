# stm32-freertos

STM32F429IGT6（正点原子 Apollo）+ FreeRTOS 的五层固件模板：`apl` / `fml` / `bsl` / `osl` / `hal`。

## 克隆与参考资料

大量 PDF 走 Git LFS；未装 `git lfs` 时 checkout 只有约 132 B 指针且 `git status` 仍可能显示干净。**完整说明见 [doc/design/README.md](doc/design/README.md)（参考资料 · Git LFS）。**

```text
git lfs install
git clone <repo>
```

## 构建（三条）

```text
python build/build.py build     # update + userdebug/user 双档编译 → out/<档位>/objects/target 与 release/
python build/build.py build --fast --variant userdebug   # 日常增量：跳过 update，只编调试档
python build/build.py verify    # 静态自检
python build/build.py clean
```

编译、刷机、调试环境：**[doc/design/搭建环境.md](doc/design/搭建环境.md)**。刷机：`python flash/flash.py`（须先 `build` 与 `install-openocd`）。

增量构建出现 `Nothing to be done` 不能当验证；要确认编译过，须 `clean` 后全量 `build`。

门禁：`python build/build.py gate-selftest --list`（现 **49 组**）；全量约 40+ 分钟，勿与 `verify`/`build` 并行。细则见 [.skills/工程规格.md](.skills/工程规格.md)「自检」。

## 文档入口

| 文档 | 看什么 |
|---|---|
| [doc/design/README.md](doc/design/README.md) | 设计文档索引（**先读**） |
| [doc/design/系统设计.md](doc/design/系统设计.md) | 工程搭建/生成链、五层、构建/门禁、运行期、故障码、**现态 size** |
| [doc/design/搭建环境.md](doc/design/搭建环境.md) | 编译 / 刷机 / 调试环境；板上验收 |
| `.skills/工程规格.md` | 与代码一致的规格（**冲突时以此为准**） |
