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

## 验收证据（上板串口原文）

每条证据都是当轮刷机后复位抓的**串口原文**（不加工），落在 `docs/design/`；
判据与数字回填在 [`docs/design/功能实现对照表.md`](docs/design/功能实现对照表.md) 的 §4 / §9。
读证据的顺序：先看最后一行 `[post] result N pass N fail 0`，再回表里对 `§4` 那一行的实测字段。

| 批次 | 证据文件 | POST 项数 |
|---|---|---|
| P6b（shell + 任务缺陷修复） | `串口自检_P6b_13项_2026-11-24.txt` | 13 |
| P6c（音频链） | `串口自检_P6c_15项_2026-11-25.txt` | 15 |
| P6d（传感器四件） | `串口自检_P6d_19项_2026-11-25.txt` | 19 |
| P6e-1（CAN / USART3） | `串口自检_P6e1_21项_2026-11-25.txt` | 21 |
| P7（NT35510 MCU 屏） | `串口自检_P7_21项_2026-11-25.txt` | 21 |
| P8a（竖屏适配） | `串口自检_P8a_21项_2026-11-25.txt` | 21 |
| P8b（绘画） | `串口自检_P8b_21项_2026-11-25.txt` | 21 |
| P8c（IMU / 水平仪） | `串口自检_P8c_21项_2026-11-25.txt` | 21 |
| P9a（NAND 只读通道） | `串口自检_P9a_22项_2026-11-25.txt` | 22 |
| P9c-1（DMA2 内存→内存） | `串口自检_P9c1_23项_2026-11-25.txt` | 23 |
| P9c-2（PWM + EXTI） | `串口自检_P9c2_25项_2026-11-25.txt` | 25 |
| P9d（DAC1_OUT1 + ADC 闭环） | `串口自检_P9d_26项_2026-11-25.txt` | 26 |
| P9e-1（电子书 app_ebook） | `串口自检_P9e1_26项_2026-11-25.txt` | 26 |
| P6e-2（二维码） | `串口自检_P6e2_21项_2026-11-25.txt` | 21 |

更早的 1~8 项证据（P0~P5c）在同目录 `串口自检_<n>项_*.txt`；P6c0 的型号排查是
`串口自检_P6c0_13项_2026-11-25.txt`（`i2c scan` 逐字再现 `1a 1e 20 50 68`）。
