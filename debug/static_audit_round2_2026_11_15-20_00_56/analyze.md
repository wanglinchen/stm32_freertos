# 第二轮全仓审计：缺陷与修改建议（只分析、未改码）

时间：2026-11-15 20:00
范围：自研 20 个 C 源（`apl/src/main.c`、`bsl/src/*`、`fml/src/*`、`osl/src/{osl_boot,osl_health,osl_panic,osl_thread}.c`、`hal/chip/startup/src/system_stm32f4xx.c`）+ 20 个 py 文件。
方法：gcc 13.3 按真实构建口径（`-DSTM32F429xx`、9 个 `INCLUDE_DIRS`、`-include osl/inc/osl_alloc_guard.h`、`-mcpu=cortex-m4 -mfloat-abi=hard`）`-fsyntax-only` 加严格告警集；Python AST 审计；逐文件读语义。
本轮**只出建议，不改代码**。HEAD 仍 `91cef65`，工作区是上一轮 checks 重排的产物。

## 0. 审计口径与噪音过滤

严格告警集（在既有 `-Wshadow/-Wformat=2/-Wundef/-Wcast-qual/...` 基础上加）：
`-Wunused-function -Wunused-but-set-variable -Wstack-usage=512 -Wframe-larger-than=1024 -Wmissing-prototypes -Wmissing-declarations -Wredundant-decls`。

结果：286 条告警行里，**自研文件 40 条**，且只有两类：

| 类别 | 条数 | 归属 |
| --- | --- | --- |
| `-Wmissing-prototypes` | 13 | 见 §1.3 / §1.4 / §1.5 |
| `-Wredundant-decls` | 9 | vendor 头（`stm32f429xx.h:176-178` vs `system_stm32f4xx.h:57-60`），同一变量两处 extern |
| `-Wstack-usage` / `-Wframe-larger-than` | **0** | 没有超过 512 B 的栈帧 |

**误报更正**（记录在此，避免下轮重复踩）：
- `_support.py` 的 `argparse/ast/hashlib/tempfile/time/generate` 「未使用 import」**不是缺陷**：它是 re-export hub，各模块靠 `from ._support import *` 拿这些名字。
- `generate.py:2631 default_gpio_config_and_function` / `:2775 format_optional_function_modes` 看似零引用，实际被 `docs/hardware/pinmap/pinmap_tool.py:161/171` 用（审计脚本跳过 `docs/` 才误判）。**不要删**。

## 1. 缺陷清单（按严重度）

### P0 — 当前活跃改动面一旦变化就出错

#### 1.1 `bsl/src/board_key.c:43-48` 按键初始化器硬编码 4 条，数量由 DTS 决定

```c
static const board_key_hw_t key_hw[BOARD_DTS_KEY_COUNT] = {
    {BOARD_DTS_KEY_0_PORT, ...}, {BOARD_DTS_KEY_1_PORT, ...},
    {BOARD_DTS_KEY_2_PORT, ...}, {BOARD_DTS_KEY_3_PORT, ...},
};
```

`BOARD_DTS_KEY_COUNT` 是生成值（当前 `board_dts.h:38` = 4）。DTS 增到 5 个键时：

- 数组长度 5、初始化器 4 条 → 第 5 项全零（`port=NULL, pin=0`）；
- `board_key_init()`（`:56`）照样遍历 5 项 → `bsl_gpio_init_input(NULL, 0, …)`；
- `bsl_gpio.c:3 enable_port_clock(NULL)` 九个 `if` 全不匹配、静默返回；
- `HAL_GPIO_Init(NULL, &gpio)` 内部写 `GPIOx->MODER` → **空指针解引用，HardFault（码 6）**。

减到 3 个键时反而是编译错误（too many initializers），所以只有"变多"这一侧是运行期崩。
对照：`board_led.c:12` 已用 `BOARD_DTS_LED_PINS_INITIALIZER` 宏生成，是正确做法。

**建议**：生成器为 keys 也产出 `BOARD_DTS_KEY_HW_INITIALIZERS` 宏（`project/generate.py` 里 led 的 `*_INITIALIZER` 已有一套），`board_key.c` 改成
```c
static const board_key_hw_t key_hw[] = { BOARD_DTS_KEY_HW_INITIALIZERS };
_Static_assert(sizeof(key_hw)/sizeof(key_hw[0]) == BOARD_DTS_KEY_COUNT, "keys count");
```
守门：`build/checks/bsl.py` 加一条「key_hw 不得出现逐项字面初始化器」。

### P1 — 契约/可见性缺陷（当前能编过，但类型不一致无人发现）

#### 1.2 `system_stm32f4xx.c` 不 include 自己的头（**同类问题第二次出现**）

`hal/chip/startup/src/system_stm32f4xx.c:8` 只有 `#include "stm32f4xx.h"`，没有 `#include "system_stm32f4xx.h"`。于是：

- `system_stm32f4xx.h:86-89` 声明 `SystemInit` / `SystemCoreClockUpdate` / `int SystemClock_Config(void)`；
- 定义处 `.c:75 / :95 / :146` **看不到声明**，实测三条 `-Wmissing-prototypes`；
- 定义与声明漂移（例如把 `int` 改成 `void`、参数变化）时不会报错，链接期才可能炸。

上一轮在 `bsl/src/bsl_i2c_gpio.c` 修过完全同型的问题（include 顺序导致声明被 `#if` 跳过）。这类缺陷的共同根源是**构建没有开 `-Wmissing-prototypes`**。

**建议**：`system_stm32f4xx.c` 第一行 `#include "system_stm32f4xx.h"`（头里已 include 依赖链）。

#### 1.3 `osl_debug_uart_write` 跨层手写 extern + 定义侧无原型

- 定义：`osl/src/osl_boot.c:512`，`__attribute__((weak)) int osl_debug_uart_write(const char *buf, int len)`；
- 消费：`bsl/src/board_debug_uart.c:13` 手写 `int osl_debug_uart_write(const char *buf, int len);`（为了不 include osl 头，分层约束）；
- `osl/inc/osl_boot.h` 里**没有**这条声明 → 实测 `-Wmissing-prototypes`。

两侧各自持有一份签名，谁改了都没人发现；weak 符号还能被静默覆盖成不匹配的实现。

**建议**：`osl/inc/osl_boot.h` 加正式原型（weak 属性留在定义处）；`board_debug_uart.c:13` 的注释改成「签名以 `osl/inc/osl_boot.h` 为准」；再加一条判据对拍两处文本签名（或直接让 bsl 通过 `bsl_device_api.h` 的 `puts` 间接调用，消掉这个跨层符号）。

#### 1.4 五个异常入口无原型（向量表靠名字链接，拼错只静默）

`osl/src/osl_panic.c:518-529` 的 `DEFINE_FAULT_HANDLER` 宏展开出 `HardFault_Handler` / `MemManage_Handler` / `BusFault_Handler` / `UsageFault_Handler`，`:537` 的 `UnhandledIRQ_Handler`，全部实测 `-Wmissing-prototypes`。

向量表在 `startup_stm32f429xx.s` 里按名字引用；名字拼错时落到 `Default_Handler`（本工程改成 `UnhandledIRQ_Handler`），现场只剩「码 6」，说不清是哪个入口。

**建议**：`osl/inc/osl_panic_internal.h` 里统一声明这五个（`void X(void) __attribute__((noreturn))`），宏定义处加 `-Wmissing-prototypes` 清白 + 类型检查。

#### 1.5 构建未开 `-Wmissing-prototypes`（机制级：上面 1.2–1.4 能潜伏的真正原因）

`build/checks/makefile.py` 的 `collect_warning_gate_errors` 只守 `-Wall -Wextra -Werror -fstack-usage`。本轮自建的审计集里，`-Wmissing-prototypes` 一条就抓出 13 处可见性缺陷。

**建议**：`Makefile` 的 `CFLAGS` 增 `-Wmissing-prototypes -Wstrict-prototypes -Wredundant-decls -Wundef -Wshadow`；vendor 单元（`hal/periph`、`hal/cmsis`）用 `-Wno-redundant-decls` 局部豁免（当前 9 条 redundant 全在 `stm32f4xx.h`/`system_stm32f4xx.h` 的 `SystemCoreClock` 双声明，属 ST 官方包重复）。同步把这几项写进 `collect_warning_gate_errors` 的判据，否则下一个人删掉又静默。

#### 1.6 `bsl/src/board_beep.c:75-79` panic 静音只对 active-low 蜂鸣器成立

```c
void board_panic_beep_silence(void)   /* PCF8574 分支 */
{
    s_pcf8574_shadow |= (uint8_t)BEEP_BIT_MASK;   /* 硬编码「置 1」 */
    beep_apply_shadow();
}
```

同一文件的 GPIO 分支（`:108-117`）是按极性算的：
```c
bsrr = BOARD_DTS_BEEP_ACTIVE_HIGH ? (PIN << 16) : PIN;   /* 置 16 位 = 清引脚 */
```
两者语义不一致：

- 当前 `BOARD_DTS_BEEP_ACTIVE_HIGH = 0`（`board_dts.h:24`）→ PCF 置 1 = 高 = 静音 ✓ 恰好正确；
- 把 DTS 改成 active-high 蜂鸣器（一个字段），GPIO 分支仍正确，**PCF 分支会在 panic 时把蜂鸣器打开**，且是在关中断之后的播报路径上。

**建议**：改成极性无关写法 —— 直接复用已有的正确路径 `beep_set_pcf8574_logical(0)`（它按 `BOARD_DTS_BEEP_ACTIVE_HIGH` 分支），或用 `BOARD_DTS_BEEP_LEVEL_IDLE` 决定置/清。守门：`bsl.py` 的极性判据加一条「`*_silence` 必须走 `LEVEL_IDLE` 或极性分支，不得裸 `|=`」。

## 2. P2 — 机制级敞口（当前正确，改配置即失效）

### 2.1 `bsl_device_get()` 依赖生成顺序 == 枚举顺序（无编译期断言）

`bsl/src/bsl_device.c:18-24`：
```c
return &s_devices[id];            /* id 直接当数组下标 */
```
`s_devices[]` 由 `BOARD_DEVICE_REGISTRY_APPLY` 顺序展开（`board_device_registry.h:44-49`）。当前枚举 `:8-16` 与展开顺序一致（DEBUG_UART=0 … BEEP=5），但这层一致性**只存在于生成器的书写习惯里**：生成器一改序（例如按 DTS 节点顺序输出），`bsl_device_get(BSL_DEV_LEDS)` 会返回另一个设备，而 `(unsigned)id >= BSL_DEV_COUNT` 只能挡越界、挡不住错配。

**建议**：生成器改成指定初始化器，顺序无关：
```c
static const bsl_device_t s_devices[] = {
    [BSL_DEV_DEBUG_UART] = { ... },
    [BSL_DEV_LEDS]       = { ... },
};
```
或 `bsl_device_get()` 改为按 `.id` 线性查找（6 项，代价可忽略）。守门：`dts.py` 的注册表判据加「每个 entry 的 dev_enum 与其下标一致」——即 `_Static_assert` 风格的行内断言。

### 2.2 `bsl_gpio.c:3-24 enable_port_clock()` 未知端口静默跳过

九个 `else if` 覆盖 A–I，其它端口（F429 部分封装有 GPIOJ/K）不匹配时**什么都不做**，随后 `HAL_GPIO_Init` 往未开时钟的端口写寄存器：值写不进去、也不报错，引脚保持复位态。

当前 DTS 端口集合是 `abcdefghi`（`dts.py` 的 `_PORTS`），所以不触发。
**建议**：加 `else { /* 断言：端口必须在 A..I */ }`（走 `osl_panic_report` 或直接返回错误码让调用方 panic），并在 `bsl_gpio_init_*` 返回 `bsl_err_t`。

### 2.3 `board_debug_uart.c:39-48` MspInit 用 TX 端口配两个引脚

```c
gpio.Pin = BOARD_DEBUG_UART_TX_PIN | BOARD_DEBUG_UART_RX_PIN;
HAL_GPIO_Init(BOARD_DEBUG_UART_TX_GPIO_PORT, &gpio);   /* 只给了 TX 端口 */
```
`board_dts.h` 有独立的 `BOARD_DEBUG_UART_RX_GPIO_PORT`，但这里只用了 TX 端口。当前 TX/RX 同在 GPIOA（PA9/PA10）✓，一旦 RX 换到别的端口，RX 引脚会被配到错误端口上（该端口同编号引脚被误配成 AF）。另外 `:36` 只对 `USART1` 使能时钟，换实例即静默不工作。

**建议**：按端口分组两次初始化（或生成器直接产出「端口 → 引脚掩码」表）；实例时钟用生成宏（`BOARD_DTS` 已有 `*_RCC_CLK_ENABLE()` 的写法可循）。

### 2.4 `bsl_i2c_gpio.c:8-14 iic_delay()` 无时序保证

```c
static void iic_delay(void) { volatile uint32_t n = 80U; while (n-- > 0U) { (void)0; } }
```
80 次空循环在 168 MHz 下约 0.5 µs 量级，随编译优化与主频变化，**不承诺 SCL 高低电平宽度**（标准模式 ≥4.7/4.0 µs，快速模式 ≥1.3/0.6 µs）。PCF8574 宽容所以能通，但换从机或提频就可能失败。

**建议**：用 DWT CYCCNT（`osl_boot.c` 已启用）做 µs 级延时，或按 `SystemCoreClock` 算循环次数并注明目标频率；注释写明"当前实测 SCL ≈ xxx kHz"。

### 2.5 `osl_boot.c:386 HAL_GetTick()` 在 ISR 里调任务 API

```c
if (xTaskGetSchedulerState() == taskSCHEDULER_RUNNING) { return g_tick_ms; }
```
`HAL_GetTick()` 会被 HAL 驱动在中断上下文用于超时判断（`HAL_xxx` 的 `while ((HAL_GetTick()-tickstart) < Timeout)`）。`xTaskGetSchedulerState()` 不是 ISR-safe API（实现上只读全局，暂不致错，但违背 FreeRTOS 契约；将来内核对 ISR 加断言就会炸）。

**建议**：改用 tick hook 维护的「是否已有 tick」标志（`osl_tick_ms_advance()` 的 `synced` 已是现成判据），把 flag 提成文件级 `volatile uint8_t g_tick_live`。

### 2.6 `osl_health.c:25` 越界计数是 `volatile` 自增（非原子）

```c
if (slot >= (uint32_t)OSL_HEALTH_STACK_SLOTS) { g_stack_slot_err++; return; }
```
两个任务同时越界会丢计数。虽然越界本身已是"不该发生"，但这条计数的用途正是**证明没发生**，丢计数等于观测弱化。

**建议**：`g_stack_slot_err` 改成"置位式"（`g_stack_slot_err = 1`）或进临界区（`taskENTER_CRITICAL`）——后者在任务上下文可用。

## 3. P3 — 整洁/诊断性

| 位置 | 问题 | 建议 |
| --- | --- | --- |
| `bsl/src/bsl_i2c_gpio.c:129-131` | 地址 NACK（`iic_wait_ack` 返回 `BSL_ERR_TIMEOUT`）被统一映射成 `BSL_ERR_IO`，日志 `rc=` 分不清「从机不在」与「总线异常」 | 保留原始码返回（`BSL_ERR_TIMEOUT`），或加 `BSL_ERR_NACK` |
| `osl/src/osl_boot.c:99` | `RCC->CSR \|= RCC_CSR_RMVF` 是对整个 CSR 的读-改-写（CSR 含 `LSION` 等控制位） | 用 `__HAL_RCC_CLEAR_RESET_FLAGS()`（只写 RMVF），语义更窄 |
| `build/checks/_support.py:5,7` | `from __future__ import annotations` 写了两遍 | 删一行 |
| `build/checks/_support.py:48` | 原 `verify` 的模块 docstring 掉到 import 之后，成了孤立字符串表达式（拆分残留） | 移到 `:1` 与现有 docstring 合并 |
| 全仓 py 40+ 处 | 无占位 f-string（ruff F541）：`f"..."` 里没有任何 `{}` | 统一改普通字符串；churn 大，可只在下次改动该文件的顺手清 |
| `build/checks/*.py` | 6 个判据函数 196–245 行（`collect_boot_init_errors` 245、`collect_artifact_errors` 243、`collect_fault_errors` 237、`collect_health_slot_errors` 196）；`generate.py:793 parse_board_dts` 232 行 | 判据本身是"一条断言一大段"，拆分收益低；若要动，按「探测 / 判定 / 报错文案」三段抽 helper |
| `fml/src/fml_led.c:37` | 日志模块名写成 `"apl"`（调用在 fml 层） | 改 `"fml"` 或 `"led"` |
| `fml/src/fml_led.c:29` | `static int uart_banner_done` 只为「打印一次」，任务只创建一次 | 改普通局部即可（去掉 .bss 占用） |
| `bsl/src/bsl_pcf8574.c:7-10` | `.init` 是空实现 + 注释 | 保留（注册表要求每个设备有 init），但把注释写进函数体一句 |
| `bsl/src/bsl_init.c:19-21` | 所有 `driver->init` 返回 `void`，`HAL_UART_Init` 的返回值也在 `board_debug_uart.c:25` 被 `(void)` 丢掉 → 设备初始化失败**永远不可见** | 设计敞口：`init` 改 `bsl_err_t`，或至少让 `board_debug_uart_init` 失败走 panic |
| `bsl/src/board_debug_uart.c:75-76` | `for (len = 0; s[len] != '\0'; len++)` 手写 strlen | 用 `strlen`（newlib 已在链接里）或保留（无符号依赖更轻） |
| `apl/src/main.c:17` | `osl_boot_stage_note((uint32_t)rc)` 把 `int` 原值转 `uint32_t`（当前 `board_system_init` 只返回 0/1/2，安全；接口一旦返回负数，stage 会变成 0xFFFFFFFF） | 用显式映射，或把 `board_system_init` 返回类型改 `enum` |

## 4. 未发现问题的部分（避免下轮重复投入）

- **栈深度**：`-Wstack-usage=512` 零告警，`-Wframe-larger-than=1024` 零告警（`osl_panic.c` 的 record 写入全走全局结构体，不压局部缓冲）。
- **panic 重入/顺序**：`g_panic_reporting` 是 `static volatile uint32_t`（`osl_panic.c:61`），`panic_mask_all_irqs()` 早于重入判断（`:270-276`），与 `panic-code` 门禁组一致。
- **I2C 失败路径**：`iic_wait_ack()` 超时分支（`bsl_i2c_gpio.c:87-92`）已 `scl_low → sda_out → iic_stop`，`write_byte` 的 `:130` 早退不会把总线留在中间态（**本轮曾误判为缺陷，已核实不成立**）。
- **`fml_led` / `fml_button` 的 NULL 分支**：`fml_panic` 在 `fml/inc/fml_panic.h:24` 标了 `__attribute__((noreturn))`，故 `if (leds == NULL) fml_panic(2)` 之后的解引用不可达（**同样是误判更正**）。
- **未用过 FreeRTOS 定时器**：`configUSE_TIMERS=0` 且 `ASSERTION_GROUPS` 有产物断言守「软件定时器不得进镜像」，一致。

## 5. 建议的落地顺序

1. **P0（1.1）**：`board_key` 初始化器改宏生成 + `_Static_assert`，配一条 `bsl` 判据。改动小、风险低、直接消除"改 DTS 就崩"。
2. **P1（1.2–1.5）**：三个 `#include` 修复 + 五个 handler 声明 + 把 `-Wmissing-prototypes` 等下沉进 `CFLAGS` 与 `warning-gate` 判据。这批做完，"可见性缺陷"这一类就永久关门。
3. **P1（1.6）**：`board_panic_beep_silence` 改极性无关 + 判据加固。
4. **P2**：按 2.1 → 2.5 逐条，每条都能独立验证（`2.1` 可用"生成器故意错序"做破坏态自测）。
5. **P3**：与其它改动同文件时顺手清，不单独开一轮。

每一条都要按仓库既有纪律走：改码 → `python build\build.py update` → `verify --artifacts` → 对应 `gate-selftest --only <组>` → `build --no-update` 比对 `elf` SHA（判据/文档类改动应保持 `E5D44A4D166D7D19` 不变）。

## 6. 落地记录（第一批，2026-11-15 20:00 之后）

### 6.1 落地的项

| # | 改动 | 落点 | 守门 |
|---|------|------|------|
| 1 | `key_hw[]` 改宏生成形态 + 尺寸 `_Static_assert` | `bsl/src/board_key.c:43-55` | `board-key` 两段破坏 |
| 2 | 生成器产出 `BOARD_DTS_KEY_HW_INITIALIZERS` | `project/generate.py`（`write_board_dts_h` keys 段）→ `bsl/inc/board_dts.h:55` | `generate.py check` 的「board_dts.h 事实」项 |
| 3 | `board_panic_beep_silence` 的 PCF8574 分支改调 `beep_set_pcf8574_logical(0)` | `bsl/src/board_beep.c` | `board-polarity` 第三段（panic LED 掩码/极性） |
| 4 | `system_stm32f4xx.c` 加自 include | `hal/chip/startup/src/system_stm32f4xx.c` | `-Wmissing-prototypes` + `warning-gate` |
| 5 | libc 四钩子与 `osl_debug_uart_write` 原型进 `osl_boot.h` | `osl/inc/osl_boot.h` 尾部 | 同上 |
| 6 | 五个异常入口原型进 `osl_panic_internal.h` | `osl/inc/osl_panic_internal.h` | 同上 |
| 7 | `CFLAGS` 增 `-Wmissing-prototypes -Wstrict-prototypes` | `project/generate.py:4226` → `toolchain.make:42` | `collect_warning_gate_errors()` flag 元组 4→6 项 |

要点：
- 第 1 项**不是 P0**。`board_key.c` 原本已有 `_Static_assert(BOARD_KEY_COUNT == BOARD_DTS_KEY_COUNT)`，只改 DTS 时编译期就会拦；真正敞口是"同时改 `board_key.h` 枚举、却漏改手写初始化器"——此时数组长度跟着枚举变，第 5 项起零初始化（`port=NULL`）→ `board_key_init` → `HAL_GPIO_Init(NULL, …)` → `GPIOx->MODER` 写空指针 → HardFault（码 6）。定为 P1 加固项。
- 第 5 项**没有新建头文件**：`bsl` 层不能 include `osl` 头（层边契约），`board_debug_uart.c` 继续保留同签名的本地 `extern`，两处必须同步改——这是有意的、被注释写明的重复。
- 第 7 项只加 `CFLAGS`，`CXXFLAGS` 不动；**刻意不加** `-Wredundant-decls`（vendor `SystemCoreClock` / `AHBPrescTable` / `APBPrescTable` 重复声明实测 9 条）、不加 `-Wundef` / `-Wshadow`（未评估 vendor 侧告警量）。

### 6.2 判据侧的两处重构（含一次被自己否证的假绿）

`collect_board_polarity_errors()` 原用正则匹配 `key_hw[BOARD_DTS_KEY_COUNT] = {…}` 逐项三元组。宏化后第一次 `update` 直接报红「找不到 key_hw 的初始化块」——判据守的是旧代码形态。

第一版改法是"`block is None` 走新分支、否则保留旧的字面量校验"。`gate-selftest --only board-key` **第一段破坏 EXIT=0 不报**：破坏后正好落进 `else`，4 项、电平全合法 → 假绿。**结论：保留一条旧的通过路径，等于判据形同虚设。** 第二版删掉 `else` 与 `triples` 全部逻辑，改成三条只用宏形态的检查：

1. `static const board_key_hw_t key_hw[] = { BOARD_DTS_KEY_HW_INITIALIZERS }` 必须逐字在场；
2. 尺寸 `_Static_assert` 必须在场；
3. **极性期望表**：从 `board_dts.h` 读 `BOARD_DTS_KEY_n_{PORT,PIN,PRESSED}`，对照写死的原理图事实（`GPIOA/GPIO_PIN_0/SET`、`GPIOH/GPIO_PIN_3/RESET`、`GPIOH/GPIO_PIN_2/RESET`、`GPIOC/GPIO_PIN_13/RESET`），并校验 `key_count == 4`。强度高于旧判据（旧判据只查电平、不查脚）。

`_gate.py` 的 `case_board_key()` 同步改成两段纯静态破坏（换回手写字面量 / 删尺寸断言），都用 `patched()` + `digest()` 字节还原校验。

### 6.3 验证证据（均为实测）

| 动作 | 结果 |
|------|------|
| `python build\build.py update` | 首次 EXIT=1（判据守旧形态）；改判据后 EXIT=0 |
| `python build\build.py build` | EXIT=0，userdebug（`-Og -g3`）与 user（`-Os -g0`）双档均过；加两个新 flag 后**零告警** |
| `verify --artifacts` | EXIT=0 |
| `python project\generate.py check` | 10 项全 PASS |
| `gate-selftest --only board-key` | PASS 165.5 s（两段破坏都红、按字节还原、restore 后 `verify --artifacts` 通过） |
| `gate-selftest --only warning-gate` | PASS 178.4 s（删 `-Werror` 后报红、已还原重建） |
| user 档体积 | `text=14388`（原 14420，−32 B）、Flash `14408/1048576`（1.37%）、RAM `18544/196608`（9.43%） |
| elf | `out\user\objects\target\stm32-freertos.elf` 41056 B，SHA256 前 16 `FEC002DF57D15D98`（改动前 `E5D44A4D166D7D19`） |

### 6.4 未做（明确缺口，不当已闭合）

1. **极性期望表缺破坏自证**：真源是 `project/dts/boards/apollo.dts`，改它会连带把 `board_dts.h` 重写成错误值，需要在组内加"恢复 + 重生成"步骤，本轮未加。已登记进 `.skills/架构缺陷.md` 的「已知敞口」。
2. **`-Wredundant-decls` 不加**：见 6.1 第 7 项，属 vendor 噪音，非自研缺陷。
3. **架构对比 P1 其余三项未动**：驱动 API 类型化（`bsl_device_driver_t.api` 仍是 `const void *`）、init 顺序显式化（仍是 `POST_CLOCK`/`BOARD` 两级）、binding schema 校验（10 份 YAML 仍只被 edtlib 用于 `compatible`）；日志 RAM ring 未做。
4. **`bsl_device_get()` 的修法未定**（生成器给指定初始化器 vs 运行时按 `.id` 线性查找）。
