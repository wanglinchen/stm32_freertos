# pinmap 目录说明

## 板级表格（本工程真源）

| 文件 | 来源 |
|------|------|
| `apollo_alternate_gpio_pinmap.xlsx` | **正点原子 Apollo V2** 板级 AF-GPIO 映射（非 ST 官方） |

板级数据源为正点原子《阿波罗 V2 硬件参考手册 V1.1》**表 1.5.1 阿波罗 V2 STM32F429 IO 资源分配总表**（第 18–22 页，140 条）：

- `docs/hardware/datasheet/board/apollo_mainboard_reference_manual_v1.1.pdf`
- 同一信息的图形版：`docs/hardware/sch/DNF429P_F767P_H743P V2.6.pdf`

两张 pinmap xlsx 是本目录的真源；`gen_dts.py` 直接读取它们生成两份 dtsi：

- `stm32f429_gpio_default.dtsi`：芯片复位默认 GPIO 配置（由官方表生成）。
- `apollo_gpio_overlay.dtsi`：阿波罗 V2 板级 GPIO 占用 overlay（由板级表生成）。

**在本工程里它们是构建输入**：`python build/build.py update` 会调用 `gen_dts.py` 把两份 dtsi 写到
`project/dts/`，再合成单树 `apollo.dts` 并用 `dtc` 编译成 `apollo.dtb`（随固件烧进
`.dtb` 段，运行期由 `bsl_fdt` + `bsl/src/board_rt.c` 读回端口/脚/极性/波特率）。改引脚只改这里，
C 里没有第二份常量。`gen_dts.py --out <dir>` 单独跑时默认写 `docs/hardware/pinmap/`，
那份只是参考产物，不参与构建。

原 `阿波罗V2 STM32F429开发板IO引脚分配表.xlsx`（7 列，A/B/C/D/E/F/G 语义为 pin_no/gpio/连接资源/次要资源/完全独立/说明/提示）已从本目录移除，由本表取代。

## ST 官方（芯片 pin / 复用功能）

| 文件 | 说明 |
|------|------|
| `stm32f429_default_gpio_pinmap.xlsx` | **英文表头/正文**（default-gpio-config + optional-function-modes + 复位功能）；脚本生成 `stm32f429_gpio_default.dtsi` |
| `gen_dts.py` | 读取两张 xlsx 生成默认/板级两份 dtsi，可 `--check` 自检、`--out <dir>` 指定输出目录 |

```text
pip install openpyxl
python docs/hardware/pinmap/gen_dts.py            # 生成两份 dtsi 到 project/dts/（入库的源）
python docs/hardware/pinmap/gen_dts.py --check   # 只自检（打印归类与未命中清单），不写文件
python docs/hardware/pinmap/gen_dts.py --out <dir>
```

两张表的列布局**不同**，不要按一套列序去读另一张。

### 板级表 `apollo_alternate_gpio_pinmap.xlsx`（7 列，本工程真源）

| 列 | 含义 |
|----|------|
| A `pin_no` | LQFP176 package pin（1–176，140 个可用引脚） |
| B `gpio` | `PA0`..`PI11`（e.g. `PA9`） |
| C `default-gpio-config` | `mode=` + `pull=`（RM0090 复位电气态） |
| D `independent` | **板级**判定：该脚在 Apollo 板上可完全独立使用记 `Y`；已接外设或上下拉记 `N`（Y=55 / N=85） |
| E `optional-function-modes` | AF/ANALOG 列表（pinctrl / DS9405 Table 12） |
| F `af-function-mode` | 板上**实际接入的网络**（手册表 1.5.1「连接资源」；双路用 ` / ` 分隔，如 `SDIO_D0 / DCMI_D2`） |
| G `notes` | **中文**连接关系说明（手册表 1.5.1「连接关系说明」） |

D 列用的是板级判据，与官方表 D 列的芯片判据**不是同一件事**（官方表只看 AF0 复位态与 JTAG/SWD/BOOT/OSC 关键字）。实测两套判据在 140 个脚中有 68 个结论不同，本表一律取板级结论。

### 官方表 `stm32f429_default_gpio_pinmap.xlsx`（7 列，脚本输入）

| 列 | 含义 |
|----|------|
| A `pin_no` | LQFP176 package pin |
| B `gpio` | `PA0`..`PI15` |
| C `default-gpio-config` | `mode=` + `pull=` only (input/analog/alternate, pull none/up/down) |
| D `independent` | 芯片判据 `Y` / `N` |
| E `optional-function-modes` | AF/ANALOG list → `brd,optional-function-modes` |
| F `default-function-mode` | Reset function (e.g. `GPIO`, `AF0 / SYS_JTMS_SWDIO`, `RCC_OSC_IN`) |
| G `notes` | **中文**（板级表 G 列） |

官方表**不再生成** `apollo-function-mode-mux` 列：板级网名只由板级表 F 列持有，写进官方表等于同一份数据存两处。板级网名只出现在 `apollo_gpio_overlay.dtsi` 的 `brd,board-net` 属性里。

`gen_dts.py` 生成 dtsi 时按板级 F 列网络名反推实际生效 AF：片上复用脚在 pinmux 里写 `mode=2` + 实际 AF；纯 GPIO/按键/LED/晶振/模拟/软件模拟 SPI 脚保持输入态，网络名与连接说明写进 `brd` 属性留档。

ST **没有**与板级 xlsx 同内容的「阿波罗官方表」；原始 AF 定义仍以 **数据手册 + 参考手册** 为准：

| 本地文件 | 官方文档 | 查 pin 看什么 |
|----------|----------|----------------|
| `ST-STM32F427xx-STM32F429xx-datasheet-DS9405-en.pdf` | **DS9405**（原 DM00071990）数据手册 | **§4** 封装引脚图；**Table 12** *Alternate function mapping*（各 GPIO 的 AF0–AF15） |
| `ST-RM0090-reference-manual-en.pdf` | **RM0090** 参考手册 | **§9 GPIO**、外设章节寄存器与 AF 配置细节 |
| `ST-ES0206-errata-sheet-en.pdf` | **ES0206** 勘误表 | 与 pin/外设相关的器件限制 |

本板 MCU：**STM32F429IGT6**（LQFP176）。数据手册中对应 **STM32F429IG** / Figure *STM32F429IGT6 LQFP176 pinout*。

中文版参考手册：`docs/hardware/datasheet/mcu/stm32f4xx_reference_manual_v4.pdf`（正文 + 扩展章节合并；含 PDF 书签目录）。合并后若导航侧栏无目录，可运行 `python docs/hardware/datasheet/mcu/add_stm32f4xx_ref_bookmarks.py --in-place`。

## 下载来源（可自行更新）

- 数据手册：<https://www.st.com/resource/en/datasheet/stm32f429ig.pdf>
- RM0090：<https://www.st.com/resource/en/reference_manual/rm0090-stm32f405415-stm32f407417-stm32f427437-and-stm32f429439-advanced-armbased-32bit-mcus-stmicroelectronics.pdf>
- 勘误：<https://www.st.com/resource/en/errata_sheet/es0206-stm32f427437xx-and-stm32f429439xx-device-limitations-stmicroelectronics.pdf>