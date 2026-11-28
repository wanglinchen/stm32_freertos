# 问题目录（`debug/`）

`debug/<问题>_YYYY_MM_DD-HH_MM_SS/` 是**一次问题一个目录**：现场快照、分析过程都落在同一次问题的目录里，便于对照回溯。

| 文件 | 谁写 | 内容 |
|------|------|------|
| `analyze.md` | 人 / Agent 手写 | 问题分析过程：现象（命令 + EXIT 码 + 原始报错）、复现步骤、已排除项、根因（定位到 `path:line`）、修复方案与影响面、修复后的验证证据、遗留项。**边查边写**，同一问题多次尝试追加在本目录内，不另开目录 |
| `snapshot.md` | `capture.py` 生成 | 板上现场：PC/LR 与 `addr2line` 符号、`g_boot_stage`、`g_clock_fail_sub`、`g_panic_record`、SysTick、`RCC`/`PWR`/`GPIOB`、`GPIOB->ODR` 采样 |

- `<问题>` 用短名概括现象（如 `red-led-constant`、`edt_reg_length`）；时间取**首次观察到现象**的本地时间，格式固定 `YYYY_MM_DD-HH_MM_SS`（`2026_11_15-00_06_07`）。
- `debug/` 只放过程记录，不放产物；跨轮次仍成立的结构性缺陷在关闭后另摘一条进 [`.skills/架构缺陷.md`](../.skills/架构缺陷.md)。

## 现场快照：`capture.py`

用 ST-Link + OpenOCD 读 MCU 寄存器与 `.noinit` 变量，写成 Markdown 报告。

```powershell
# 先编译出 elf（符号地址从产物解析）
python build/build.py build

# 生成 debug/<问题>_YYYY_MM_DD-HH_MM_SS/snapshot.md
python debug/capture.py red-led-constant
python debug/capture.py clock-hse-fail
```

可选参数：

| 参数 | 含义 |
|------|------|
| `--elf PATH` | 默认 `out/userdebug/objects/target/stm32-freertos.elf` |
| `--sample-gpio N` | 采样 `GPIOB->ODR` 间隔毫秒（默认 1000，0=不采样） |
| `--no-halt` | 只连接读现场，不 `halt`（读值可能抖动） |

需要 OpenOCD：未安装时 `python flash/flash.py install-openocd`。
