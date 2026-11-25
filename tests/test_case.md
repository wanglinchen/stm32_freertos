# tests/ 功能测试用例

可选的 host 侧案例与说明目录；**不参与**交叉编译与 Kconfig/Makefile 扫描。门禁（`verify` / `gate-selftest`）已于 2026-11-22 删除，本文件是验收条目清单，能自动判定的稳定性部分落到同目录的 `stability.py`，其余 B/M 类按表人工打勾。

- DTS 链判据：`python build\build.py update` 必须零 dtc 诊断，并产出 `out/dts/apollo.dtb`；`update` 本身就是 dts 结构的自检，不再有 `build/checks/dts.py`
- 构建判据：`python build\build.py build` 两档 exit 0、`-Werror` 零告警；本目录 = 本文件 + `stability.py`（host 侧脚本，只读 ELF / 串口 / OpenOCD，不参与交叉编译）

## 判据分级

| 标记 | 含义 | 判定方式 |
|------|------|----------|
| **H** | host 自动 | 命令 exit code / 断言文本，无需硬件 |
| **B** | 板上自动 | ST-Link + OpenOCD（`python debug/capture.py <问题>`）读寄存器与 `.noinit`，或串口抓日志 |
| **M** | 人工目视/听感 | 灯态、蜂鸣、按键手感，需操作员按表判定 |

「现状」列：`已覆盖` = 已有自动断言；`手工` = 目前只能按本文步骤人工验证；`待补` = 建议补自动断言但尚未实现。

## A. 构建与生成链

| ID | 目标功能 | 前置 | 步骤 | 期望 | 判据 | 现状 |
|----|----------|------|------|------|------|------|
| TC-B01 | 生成链幂等 | 干净工作区 | `python build\build.py update` 连跑两次 | 两次均 exit 0，末行为 `已生成 out/dts/apollo.dts → apollo.dtb（N 字节）`；三份 defconfig 与 `out/<档>/.config` 的哈希与 mtime 都不变（内容未变即不重写） | H | 已覆盖（人工比对哈希） |
| TC-B02 | 未知参数拒绝 | — | `build.py update --artifact`（少一个 s） | exit **2** 并打印 usage，不执行生成 | H | 已覆盖（argparse） |
| TC-B03 | 子命令 flag 白名单 | — | `build.py update --fast`、`build.py clean --variant user` | 均 exit **2** | H | 已覆盖 |
| TC-B04 | 源文件增删后重生成 | — | 在 `bsl/src/` 加一个 `.c` 并写进 Kconfig → `update` | `Makefile` 出现该目标，`out/<档>/.config` 含对应 `=y` | H | 已覆盖 |
| TC-B05 | DTS 生成物落位 | — | `update` 后列 `project/dts/` 与 `out/dts/` | `project/dts/` 只有两份入库的 dtsi（`stm32f429_gpio_default.dtsi` 平台、`apollo_gpio_overlay.dtsi` 板级 overlay）；`out/dts/` 有合成单树 `apollo.dts`（81165 B 量级）与 `apollo.dtb`（63875 B），两者都不入库。旧的 `apollo-edt.dts` / `edt-bindings/` 已随生成头一起删除 | H | 已覆盖（`sync_dts` 的落地检查） |
| TC-B06 | 空 pinmap 表兜底 | 故意清空 `docs/hardware/pinmap/*.xlsx` 数据区 | `update` | 给出明确报错而非静默生成 0 脚 dtsi | H | 待补 |

## B. 启动、时钟、复位原因、看门狗

| ID | 目标功能 | 前置 | 步骤 | 期望 | 判据 | 现状 |
|----|----------|------|------|------|------|------|
| TC-S01 | 正常上电启动 | 板子 + 电源 | 上电，串口 115200 8N1 | POST 21 项跑完后（数秒内）打印 `stm32-freertos: USART1 115200 debug ready`（banner 在 POST 之后，由 `app_sample` 的按键任务触发）；LED 运行期不再 1 Hz 翻转（P5a 起归 `app_led` 独占）（banner 由 `app_sample` 的 led_blink 任务调 `bsl_log_startup_banner()`，文案与实例名/波特率在 `bsl_log.c` 运行期从 DTB 读） | B/M | 手工 |
| TC-S02 | 启动阶段标记 | 同上 | `capture.py boot-stage` 读 `g_boot_stage` | `g_boot_stage == 0`（`board_system_init` 全程成功）；`1` = HAL_Init 失败，`2` = 时钟失败，`3` = DTB 的 `hse-frequency` 与 `HSE_VALUE` 不一致（`bsl/src/board_system.c:15/33/38`） | B | 手工 |
| TC-S03 | 复位原因判定 | 分别制造：按 NRST、`NVIC_SystemReset`、喂狗停摆 | 每次复位后 `capture.py` 看 `g_reset_cause`、`g_reset_csr` | 引脚复位 → 5；软件复位 → 4；IWDG 超时 → 3；上电 → 7（优先级：低功耗 > WWDG > IWDG > 软件 > 引脚 > BOR > 上电，`osl/inc/osl_boot.h:16-24`） | B | 手工 |
| TC-S04 | 系统时钟 168 MHz | — | `capture.py` 读 `RCC->PLLCFGR`/`SystemCoreClock` | 168 MHz、PLLQ = 7（48 MHz 给 USB/SDIO，`board_system.c:17-20`）；`g_clock_fail_sub == 0` | B | 手工 |
| TC-S05 | IWDG 生效与放行 | — | 观察 `capture.py` 两次采样 | 正常运行不触发 IWDG 复位；panic 后 `osl_watchdog_relax()` 让复位延后 ≈32.7 s；`g_iwdg_sync_fail` 保持 0（PR/RLR 被 `iwdg_wait_sync()` 确认写入） | B | 手工 |

## C. LED 指示

| ID | 目标功能 | 前置 | 步骤 | 期望 | 判据 | 现状 |
|----|----------|------|------|------|------|------|
| TC-L01 | 红绿 1 s 交替 | 板子运行 | 目视 ≥5 s | 周期 **1000 ms**（`SAMPLE_LED_PERIOD_MS`），红/绿互补、同一时刻只有一只亮 | M | 手工 |
| TC-L02 | 蓝灯常灭 | 同上 | 目视 | `BOARD_LED_B` 始终灭（每轮 `leds->off(BOARD_LED_B)`） | M | 手工 |
| TC-L03 | LEDS 未就绪只降级 | 人为让 `board_led_init` 返回非 0（如把 DTS 里一个 led 的 gpios 指向不存在的端口） | `update` + `build` + 上板 | **不 panic**：打一行 `[led] LEDS 未就绪（init 失败），闪灯降级`，任务继续喂健康心跳；`g_panic_record` 无记录 | M/B | 手工（语义已于本轮改为降级，原“panic 2”写法作废） |

## D. 按键与蜂鸣器

| ID | 目标功能 | 前置 | 步骤 | 期望 | 判据 | 现状 |
|----|----------|------|------|------|------|------|
| TC-K01 | 四键识别 | 板子运行 | 依次按住 WK_UP/KEY0/KEY1/KEY2 | 串口打印 `stable=1 raw WKU=… K0=… K1=… K2=…`；WK_UP 按下 raw=1，KEY0/1/2 按下 raw=0（`sample_log_keys()`） | B | 手工 |
| TC-K02 | 去抖门限 | 同上 | 快速抖动按键（<30 ms） | 不产生 `stable` 变化：轮询 10 ms × 3 次一致才切换（`SAMPLE_POLL_PERIOD_MS` / `SAMPLE_DEBOUNCE_SAMPLES`） | B/M | 手工 |
| TC-K03 | 蜂鸣随按键 | 同上 | 按住任一键 | 按下时响、松开即停（`beep_try(beep, stable)`） | M | 手工 |
| TC-K04 | 上电短鸣 | 每次复位 | 上电瞬间听/看 | 80 ms 单声（任务开头 `beep_try(beep, 1)` → 延时 80 ms → `beep_try(beep, 0)`）；BEEP 未就绪时这一步空转，按键照常工作 | M | 手工 |

## E. I2C（GPIO 模拟）与 PCF8574

| ID | 目标功能 | 前置 | 步骤 | 期望 | 判据 | 现状 |
|----|----------|------|------|------|------|------|
| TC-I01 | I2C 引脚映射 | — | 读 `out/dts/apollo.dts` 的 i2c 节点 | SCL = PH4、SDA = PH5（`scl-gpios` / `sda-gpios`）；运行期由 `board_rt_i2c_scl_pin()` / `board_rt_i2c_sda_pin()` 读回，断言随 `collect_host_dts_errors` 删除 | H | 手工核对 |
| TC-I02 | PCF8574 地址 | 板子 + 逻辑分析仪/示波器 | 探 SCL/SDA | 7 位地址 **0x20**（`parse_board_dts` 实测 `pcf8574_addr7=0x20`），起始位后地址帧 `0x40` 写 | B | 手工 |
| TC-I03 | 蜂鸣经 PCF8574 | — | 按键触发蜂鸣 | 蜂鸣由 PCF8574 bit0 驱动（DTS `beep gpios=<&pcf8574 0 1>`，active-low），非 MCU 直连 | M/B | 手工 |

## F. 调试串口与日志

| ID | 目标功能 | 前置 | 步骤 | 期望 | 判据 | 现状 |
|----|----------|------|------|------|------|------|
| TC-U01 | 串口参数 | USB-TTL 接 PA9/PA10 | 串口助手 115200 8N1 | 收到 banner；实例/波特率/TX/RX 全部来自 DTB 的 `debug_serial` 节点（`reg=<0x40011000 …>`、`baud=<115200>`、`tx-port="A"/tx-pin=<9>`、`rx-port="A"/rx-pin=<10>`），AF7 由 `uart_af()` 按实例推出 | B | 手工（生成头 `board_debug_uart_dt.h` 已删除） |
| TC-U02 | 日志等级 | — | 临时把 `BSL_LOG_LEVEL` 改 `ERR` 后编译 | `BSL_LOG_INF` 编译成 `(void)0`，不再打印 banner（`bsl/inc/bsl_log.h` 宏分支） | H+M | 手工 |
| TC-U03 | 模块前缀 | — | 触发按键 | 输出带 `[btn]` 前缀（`BSL_LOG_MOD_INF`），banner 带 `[apl]` | M | 手工 |

## G. FreeRTOS 线程与健康统计

| ID | 目标功能 | 前置 | 步骤 | 期望 | 判据 | 现状 |
|----|----------|------|------|------|------|------|
| TC-T01 | 两任务创建 | — | `capture.py` 读任务列表 / 看串口 | `led_blink` 与 `button` 均存在，优先级 1（由 `apl/src/app_sample.c` 的 `sample_task_create()` 创建） | B | 手工 |
| TC-T02 | 栈水位统计 | 运行 ≥10 s | 读 `g_stack_free_min[0..1]` | 两个槽位都从 `0xFFFFFFFF`（`OSL_HEALTH_UNSET`）降为实际水位（`osl_health.c:13-30`），且 `g_stack_slot_err == 0` | B | 手工 |
| TC-T03 | 堆最小剩余 | 同上 | 读 `g_heap_free_min` | 有值（非 `UNSET`）；`build` 报告 newlib 堆 512 B 未被占用 | B | 手工 |
| TC-T04 | 任务栈溢出钩子 | 把任务栈调小到溢出 | 编译上板 | 走码 **7**（`OSL_PANIC_STACK_OVERFLOW`），`g_panic_record.task_name` 为溢出任务名（任务栈）/ 或 NULL（MSP 守卫） | B | 手工 |

## H. panic 播报与记录

| ID | 目标功能 | 前置 | 步骤 | 期望 | 判据 | 现状 |
|----|----------|------|------|------|------|------|
| TC-P01 | 码 1 调度器失败 | 人为让 IDLE/Timer 创建失败 | 上板 | 三灯二进制播报 **001** ×4 轮 → 全亮 → 停机 | M | 手工 |
| TC-P02 | 码 4 时钟失败 | 拔掉/短接 HSE 使起振失败 | 上板 | 播报 **100**；`g_boot_stage == 2`；`g_clock_fail_sub` 指明 HSE 还是 PLL | M/B | 手工 |
| TC-P03 | 播报轮次与收尾 | 任意 panic | 计时 | 4 轮逐位（`panic_blink_code()` 的循环）+ 末段全亮（同一函数的收尾分支），此后再无灯变化（关中断自旋） | M | 手工 |
| TC-P04 | 记录可读 | panic 后不复位（halt） | `capture.py` 读 `g_panic_record` | `fault_kind`/`assert_file`/`task_name` 三项与触发原因一致；记录在 `.noinit` 跨复位保留 | B | 手工 |
| TC-P05 | 码值域与钳位 | — | 人工核对（门禁已删） | 自研层每处 `osl_panic_report`/`fml_panic` 实参都是 `OSL_PANIC_*` 名或 0..7 字面量（三灯只有 3 位，9 会播成 1）；`osl_panic_report` 播报值钳到上界、原始值留在 `g_panic_record.code` | H | 待重建判据 |

## I. 设备树 / 运行期事实 / bsl 设备契约

| ID | 目标功能 | 前置 | 步骤 | 期望 | 判据 | 现状 |
|----|----------|------|------|------|------|------|
| TC-D01 | 单根 dts + dtc 零诊断 | — | `python build\build.py update` | 合成 `apollo.dts` 只有一个 `/ { }`；dtc 无任何 Warning/Error，退出码 0 | H | 已覆盖（`sync_dts` 的 dtc 口径） |
| TC-D02 | 9 个 GPIO 控制器 | — | 同上 | `gpioa..gpioi` 各带 `gpio-controller`、`#gpio-cells=<2>`、`brd,port`、`reg` | H | 已覆盖 |
| TC-D03 | pinmap 140 脚 | — | 同上 | `parse_pinmap_index` 140 条，`gpio_p*` label 与索引键一一对应 | H | 已覆盖 |
| TC-D04 | 板级 overlay 覆盖 | — | 同上 | 140 个 `&gpio_p*` 覆盖 A–I 九端口，文件无根 | H | 已覆盖 |
| TC-D05 | DTS 与运行期解析一致 | — | `update` 后 `grep` 合成树，再对照 `board_rt_*` 返回值 | `leds` 组 `active-low` 与每个子节点 `gpios` 第三 cell（flags=1）同向；`keys` 的 `pressed-high/低` 与 flags 同向（不一致时 `parse_leds`/`parse_keys` 整组拒绝，`board_led_init` 返回 `BSL_ERR_INVAL`）；`debug_serial` 节点被 `board_rt_uart()` 读回 | H | 已覆盖（双源校验，2026-11-22 新增） |
| TC-D06 | bsl 设备契约 6 行 | — | 同上 | registry 顺序与 `bsl/inc/board_device_registry.h` 的 6 设备（DEBUG_UART/LEDS/I2C_GPIO/KEYS/PCF8574/BEEP）一致；10 份 binding 均含 `compatible`+`properties` | H | 已覆盖 |

## J. 双档构建、体积与发布快照

| ID | 目标功能 | 前置 | 步骤 | 期望 | 判据 | 现状 |
|----|----------|------|------|------|------|------|
| TC-R01 | user 档可编译 | — | `build.py build --variant user` | exit 0；`out/user/objects/target/` 出 elf/hex/bin/map | H | 已覆盖 |
| TC-R02 | userdebug 档可编译 | — | `build.py build --variant userdebug` | exit 0；含断言与日志（`USE_FULL_ASSERT`） | H | 已覆盖 |
| TC-R03 | 体积记录 | — | `arm-none-eabi-size out/<档>/objects/target/stm32-freertos.elf` | **无上限判据**（原四项产物断言已随门禁删除）；基线：userdebug `text` 86104 / `data` 16 / `bss` 21984（Flash 86120 = 8.21%，RAM 22000 = 11.19%），user `text` 83248（Flash 7.94%），newlib 堆 512 B 未用 | H | 手工 |
| TC-R04 | 发布快照四件套 | 构建成功 | 列 `out/<档>/release/<时间戳>/` | elf/hex/bin/map 四份齐全且大小非零 | H | 已覆盖 |
| TC-R05 | 产物强符号 | — | `arm-none-eabi-nm <elf>` | `main`、`vTaskStartScheduler` 均为强符号；四个 fault handler（HardFault/MemManage/BusFault/UsageFault）为 `T` 且不等于 `Default_Handler`（原 `build` 内断言已随门禁删除，改人工/nm） | H | 手工 |

## K. 刷机与调试工具

| ID | 目标功能 | 前置 | 步骤 | 期望 | 判据 | 现状 |
|----|----------|------|------|------|------|------|
| TC-F01 | OpenOCD 刷写 | OpenOCD 已装、ST-Link 连接 | `python flash\flash.py` | 默认取 `out/userdebug/objects/target/stm32-freertos.elf`，`verify reset exit` 后板子重启运行 | B | 手工 |
| TC-F02 | 现场快照可生成 | 同上 | `python debug\capture.py <问题>` | 生成 `debug/<问题>_YYYY_MM_DD-HH_MM_SS/snapshot.md`，含 PC/LR 符号、`g_boot_stage`、panic、RCC/PWR/GPIOB | B | 手工 |

## L. 稳定性回归（`tests/stability.py`）

一条命令把"上电一次能跑"升级成"重复上电一致 + 长时间运行不变形"。`--only dtb|reset|soak` 可单跑；报告落 `out/tests/stability_<时间戳>.json`；退出码 0 = 无 FAIL。

| ID | 目标功能 | 前置 | 步骤 | 期望 | 判据 | 现状 |
|----|----------|------|------|------|------|------|
| TC-S01 | 镜像 DTB 不陈旧 | 已 `update` + `build` | `python tests\stability.py --only dtb` | ELF `.dtb` 段与 `out/dts/apollo.dtb` 逐字节一致（尾部对齐填充全零、`totalsize` 与文件等长） | H | 已覆盖（反证：改 dtb 一字节 → 偏移 1479 报红） |
| TC-S02 | 启动串口逐字节稳定 | 串口在位 | `--only reset --cycles N` | N 轮 banner 均为 45 字节同内容；`s_tx_fail/s_tx_remain/s_uart_drops` 全 0 | B | 已覆盖 |
| TC-S03 | 运行态轮间一致 | 同上 | 同上 | 除 `g_tick_ms` 外 10 个字段（影子/极性/位号/6 设备状态/banner/启动阶段/看门狗计数）逐轮相同 | B | 已覆盖 |
| TC-S04 | 蜂鸣器不上电长鸣 | 同上 | 同上 | `s_pcf8574_shadow` 的 BEEP 位停在**关电平**（`active_high=0` 时应为 1）；设备全 READY | B | 已覆盖 |
| TC-S05 | tick 不漂移 | 同上 | `--only soak --soak T` | `Δg_tick_ms / Δ墙钟` ∈ [0.97, 1.03]；tick 不回落 | B | 已覆盖（30 s 实测 0.999） |
| TC-S06 | 长跑无意外重启 | 同上 | 同上 | 期间串口不出现 banner 片段（出现即视为 IWDG 咬/panic 复位） | B | 已覆盖 |
| TC-S07 | LED 仍在翻转 | 同上 | 同上 | `GPIOB ODR` 三次采样出现 ≥2 种取值 | B | 已覆盖（0x05/0x06/0x05） |
| TC-S08 | IWDG 同步无失败 | 同上 | `--only reset` | `g_iwdg_sync_fail = 0` 且 `IWDG PR=2 / RLR=2000`（1.0 s 档真的落地） | B | 已覆盖 |

## 使用约定

1. 每次改代码 → `update` → `build`，再按本表 H 类逐条对表；B/M 类在板子上按需抽测。
2. 跑失败或出现非预期现象时，按 `.skills/工程规格.md`「问题分析记录（debug/）」在 `debug/<问题>_时间/analyze.md` 留痕，再改代码。
3. 新增用例：在对应小节追加 `TC-*` 行，写清「可判定期望 + 判据来源」；能脚本化的优先落到 `update` 的 dtc 零诊断或 `build` 的退出码，或追加进 `tests/stability.py` 的检查项（新增符号只改 `SYMS` 与一条不变量即可）。
4. 本文件只描述**验收条目**，不重复静态判据细节（见 `.skills/工程规格.md`）。
