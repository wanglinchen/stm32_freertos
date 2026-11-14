# device/gen（规划）

本目录预留给 **外设启用后的代码生成**（例如 `board-resources` 里 `status = "okay"` 的 USART / SPI 等）。

当前子工程仍由 `bsl` 手写驱动；整板脚位与外设摘要见 `device/dts/`。GPIO 类（灯/键/蜂鸣器）已由 `build/dts_board_gpio.py` 根据 `board.gpio_bind` + pinmap 生成 `device/dts/generated/*-gpio.dtsi`。

后续若在此生成 HAL 初始化片段或 `bsl` 桩代码，应：

1. 在 `proj/project-info.json` 增加生成开关与外设列表；
2. 在 `verify` 增加与 DTS / pinmap 的对拍；
3. 生成物带与 `board_dts.h` 相同的 update 横幅。