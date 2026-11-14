# 设备描述（Device DTS）

板级硬件事实的单处描述，与 `bsl/` 里的运行时驱动分离。

## 目录

| 路径 | 作用 |
|------|------|
| `device/dts/include/` | SoC `.dtsi`、`*-peripherals.dtsi`（外设资源摘要） |
| `device/dts/boards/` | 整板 `.dts`（已驱动 leds/keys/beep + include） |
| `device/dts/generated/` | `<板名>-pinmap.dtsi`（xlsx）、`<板名>-gpio.dtsi`（gpio_bind+pinmap），勿手改 |
| `device/gen/` | 外设 codegen 占位（见 `device/gen/README.md`） |

## 与构建的关系

- `python build/build.py update` 会解析当前板级 `.dts`，生成 `bsl/inc/board_dts.h` 与 `osl/inc/board_panic_hw.h`（勿手改）。
- `verify` 用 `collect_device_dts_errors()` 核对 `.dts`、`pinmap`、外设摘要与 `board_dts.h`。
- 可选单测：`python tests/test_dts.py`（解析器与 pinmap 索引）。
- `bsl/src/board_led.c`、`board_key.c`、`board_beep.c` 通过 `#include "board_dts.h"` 取引脚与 RCC 宏，不再手写 `GPIO_PIN_*` / 端口。

## 扩展新板

1. 在 `device/dts/boards/` 增加 `<板名>.dts`（`#include` 生成的 pinmap / gpio 片段）。
2. 修改 `proj/project-info.json`：`board.dts`、`board.compatible`、`pinmap_xlsx`、`gpio_bind`。
3. `update` 后实现或复用 `bsl` 驱动；灯/键数量变化时同步改 `board_led.h` / `board_key.h` 枚举。

`gpio_bind` 用 pinmap 的 `label` 解析脚位；`led_names` / 每项 `key_id` 分别对齐 `board_led.h`、`board_key.h` 枚举后缀。

语法说明见 `build/dts.py` 模块文档字符串。