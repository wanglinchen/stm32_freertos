# project/dts

本目录**没有**手写 DTS、bindings、EDT 或 dtsi 模板——那套自研生成链已于 2026-11-22 整体删除。
现在这里只有两份**由构建链生成的 dtsi**，它们是「源」（入库、可 diff）；合成树与 `dtb` 属构建中间产物，落 `out/dts/`。

## 谁生成

```text
docs/hardware/pinmap/stm32f429_default_gpio_pinmap.xlsx   ─┐
docs/hardware/pinmap/apollo_alternate_gpio_pinmap.xlsx    ─┴─ gen_dts.py
        ├─→ project/dts/stm32f429_gpio_default.dtsi    （芯片默认 GPIO 控制器 + 复用）
        └─→ project/dts/apollo_gpio_overlay.dtsi       （板级占用：pinctrl / 网络名）
                    │
        project/generate.py 的 sync_dts()
                    ├─ 按花括号配对抽出两份 dtsi 的 soc / pinctrl 内容
                    ├─ 与根属性、板级设备节点（clocks / leds / keys / i2c / beep / debug_serial）拼成
                    │   out/dts/apollo.dts
                    └─ 调 dtc 编译 → out/dts/apollo.dtb
```

`update` 对 dtc 的诊断零容忍：**任何 Warning 都判失败**，成功时打印
`已生成 out/dts/apollo.dts → out/dts/apollo.dtb（N 字节）`。

## 为什么分成两处

- `project/dts/*.dtsi` 是**源**：内容由 xlsx 唯一决定，入库后能 review「这次引脚改了什么」。
- `out/dts/` 是**构建中间产物**：`apollo.dts` 只是把两份 dtsi 拼起来的结果，`apollo.dtb` 是 dtc
  的二进制输出。它们和 `elf` / `map` 同性质，归到 `out/` 下，`clean` 一次清干净，不必入库。

## 谁消费

`bsl/src/board_dtb.s` 用 `.incbin "../../out/dts/apollo.dtb"` 把它塞进 `.dtb` 段
（链接脚本 `hal/chip/linker/stm32f429igx_flash.ld` 排在 ROM，`_sdtb` / `_edtb` 标记）。
运行期 `bsl/src/bsl_fdt.c` 解析 FDT，`bsl/src/board_rt.c` 把节点翻译成驱动要的端口/脚/极性/波特率。
单镜像保证「固件与 DTB 一定成对」（本机只有 SWD 一条通道，双镜像会有「刷了固件忘刷 DTB」的整机故障）。

`--fast`（`--no-update`）不会重生成 DTB：缺 `out/dts/apollo.dtb` 时 `build` 在编译前就报错并提示
跑 `update`，而不是让失败出现在汇编期的 `.incbin`。

## 规矩

- **不要手改 `project/dts/*.dtsi`**：下次 `update` 会覆盖。要改引脚就改上层 xlsx，要改节点结构就改
  `project/generate.py` 里的 `_BOARD_NODES`。
- `out/dts/` 已在 `.gitignore`（整目录是构建产物）；`*.dtsi` 入库，便于 review。
- 同一个 `gen_dts.py` 不要在别处再产一份副本：`docs/hardware/pinmap/` 只放 xlsx + 脚本 + README，
  生成物只有 `project/dts/` 一个落点（曾经的 `docs/hardware/pinmap/generated/` 双份副本已删除）。
- panic 播报不走这条链：栈可能已崩、不能再遍历 FDT，`osl/inc/board_panic_hw.h` 是手写常量，与 DTS 人工同步。
