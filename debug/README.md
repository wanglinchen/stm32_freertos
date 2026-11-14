# 板上现场快照（`debug/`）

用 ST-Link + OpenOCD 读 MCU 寄存器与 `.noinit` 变量，写成 Markdown 报告，便于对照 `g_boot_stage`、panic、时钟、用户灯等问题。

## 用法

```powershell
# 先编译出 elf（符号地址从产物解析）
python build/build.py build

# 问题名自定，文件名：<问题>_YYYY_MM_DD-HH_MM_SS.md
python debug/capture.py red-led-constant
python debug/capture.py clock-hse-fail
```

可选参数：

| 参数 | 含义 |
|------|------|
| `--elf PATH` | 默认 `out/userdebug/objects/target/stm32-freertos.elf` |
| `--sample-gpio N` | 采样 `GPIOB->ODR` 间隔毫秒（默认 1000，0=不采样） |
| `--no-halt` | 只连接读现场，不 `halt`（读值可能抖动） |

## 报告里有什么

- PC / LR 与 `addr2line` 符号
- `g_boot_stage`、`g_clock_fail_sub`、`g_panic_record`、SysTick、`RCC`/`PWR`/`GPIOB`
- 若干次 `GPIOB->ODR` 采样（判断红绿是否在 1 s 交替）
- 指向 `doc/design/搭建环境.md` 的排查表

需要 OpenOCD：未安装时 `python flash/flash.py install-openocd`。