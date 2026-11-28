# 架构对比 Zephyr 与优化建议（第二轮）

- 快照：`D:\Users\Local\stm32-freertos`，分支 `main`，HEAD `91cef65`，**全部改动未提交**
- 上一轮对比：`debug/arch_vs_zephyr_2026_11_15-20_06_04/analyze.md`（结论 A/B/C 已落地，4.2/4.3/4.4 未动）
- 本轮的差别：A（init 返回码 + 设备就绪态）、B（清 compat 死代码）、C（驱动 API 类型化）已全部落地；另修 7 条机制级缺陷（单点外设失败放大、未编译分支 `.api` 残留、日志出口不判 UART、UART 时钟伪通用、I2C ACK 紧轮询、GPIO 端口表缺 J/K、banner 硬编码）
- 本轮只读：`bsl_device.h` / `bsl_init.h` / `bsl_init.c` / `bsl_device.c` / `bsl_device_api.h` / `board_device_registry.h`（生成物）/ `bsl_log.h` / `fml_button.c` / `fml_led.c` / `apl/src/main.c` / `FreeRTOSConfig.h` / `project/dts/bindings/*.yaml` / `generate.py` 的 binding→registry 链

---

## 1. 当前架构的机制事实

| 机制 | 实现位置 | 观测到的形态 | 证据等级 |
|---|---|---|---|
| 设备注册 | `bsl/src/bsl_device.c:14-16` | `BOARD_DEVICE_REGISTRY_APPLY(M)` 宏展开成 `static const bsl_device_t s_devices[]`，落 `.rodata` | confirmed |
| 注册表条数 | `bsl_device.c:28` | `sizeof(s_devices)/sizeof(s_devices[0])`，不用 `BSL_DEV_COUNT`；`:36` 有 `_Static_assert` 钉「条数 == 枚举项数」 | confirmed |
| **初始化顺序** | `generate.py:2455` 的 `rows: list[tuple[int, int, Entry]]` + binding 的 `init_level` / `order` | 生成期按 (level, order) 排序，运行时只有一个 for 循环。实测 registry 顺序 = `usart 10 → leds 20 → i2c 30 → keys 40 → pcf8574 50 → beep 60`，与 binding 的 order 完全一致 | confirmed |
| 初始化分级 | `bsl_init.h:6-9` | 两级：`POST_CLOCK` / `BOARD`。**没有** Zephyr 的 4 级与同级 priority 之外的层级 | confirmed |
| 单级内的失败策略 | `bsl_init.c:12-49` | 逐个跑；失败只记 state + 首错，**不中断同级其余设备** | confirmed |
| 跨级策略 | `bsl_init.c:52-63` | 两级都跑，返回首错（`rc_post != OK ? rc_post : rc_board`） | confirmed |
| 设备运行态 | `bsl_device.c:43` | `static uint8_t s_state[]`，与 const 表分离；初值 0 == `UNINIT` | confirmed |
| 就绪查询 | `bsl_device.h:46-48` | `bsl_device_state()` / `bsl_device_is_ready()`；查不到 id 一律按 `FAILED`（不让「没有条目」被读成「可选设备」） | confirmed |
| driver 面 | `bsl_device.h:30-32` | 只剩 `bsl_err_t (*init)(void)`，带返回码 | confirmed |
| api 类型 | `bsl_device_api.h:24-40` / `board_device_registry.h:39-54` | `bsl_gpio_leds_api_t` 等三张 ops 表；`bsl_devapi_leds()/keys()/beep()` 是生成期 `static inline`，类型在编译期确定 | confirmed |
| 设备事实来源 | `board_device_registry.h:1` | EDT（`apollo.dts` + `bindings/*.yaml`）→ 4 个生成头：`board_dts.h` / `board_device_registry.h` / `board_debug_uart_dt.h` / `board_panic_hw.h` | confirmed |
| 日志 | `bsl_log.h:6-40` / `bsl_log.c:23-29` | 编译期三级（ERR/INF/DBG，默认 INF）；出口先判 `bsl_device_is_ready(BSL_DEV_DEBUG_UART)`；同步 `HAL_UART_Transmit(…, 100U)` | confirmed |
| 启动 banner | `bsl_log.c` 的 `bsl_log_startup_banner` | 文案来自 `BOARD_DEBUG_UART_BANNER`（EDT 生成），只打一次 | confirmed |
| 故障取证 | `osl_panic.c` | 关中断 → 重入闸 → IWDG relax → 写 `.noinit` record（140 B，含 fault 帧 17 字段 / seq / reset_cause / ipsr / 堆栈水位）→ 三灯二进制 4 轮 | confirmed |
| 健康水位 | `osl_health.h:19/32` | 2 个栈槽（LED / BUTTON），单位 **word**；堆最低剩余按字节 | confirmed |
| 并发 | `FreeRTOSConfig.h:18/38/43/53/73/76` | `configMAX_PRIORITIES=2`、`configUSE_TIMERS=0`、`configSUPPORT_STATIC_ALLOCATION=0`、heap 16 KB、`configCHECK_FOR_STACK_OVERFLOW=2`、`configUSE_MUTEXES=1`；只有 2 个任务（led_blink / button），无 timer / work / 队列消费者 | confirmed |
| 门禁 | `build/checks/_gate.py` + 12 模块 | 51 组破坏自证、`ASSERTION_GROUPS` 双向比对、`--assert-map` 实测集合；`bsp` 十段 356.6 s | confirmed |

---

## 2. 与 Zephyr 的逐维对比

| 维度 | Zephyr 的做法 | 本工程现状 | 裁决 |
|---|---|---|---|
| 设备注册 | `DEVICE_DT_DEFINE` 落链接段，运行时遍历 | 生成期宏表 + 单一 for 循环 | **等价**：两者都是"编译期固定表 + 运行期遍历"，本工程还少一段链接段解析 |
| 初始化分级 | `SYS_INIT(fn, level, prio)`，4 级 | `init_level` 两级 + `order` 数值 | **已覆盖顺序表达**（`order` 已在用、有实测证据），缺的是 `APPLICATION` 级（内核起来之后的驱动） |
| 失败语义 | `device_init_status` 可查，失败不阻断 | 失败记 `FAILED`、返回首错、不阻断 | **等价且更严**：多了一条"跨级也跑完"的契约 |
| 运行态查询 | `device_is_ready()` | `bsl_device_is_ready()` | **等价** |
| api 类型安全 | `DEVICE_API` + `DEVICE_API_GET` 编译期类型 | binding `api_type/api_sym` → `bsl_devapi_*()` | **等价**：都是编译期类型，且本工程把"类型与符号的对应"挪进了 binding |
| 延后初始化 | `DEVICE_DEFERRED_INIT`（内核起来后再 init） | 无：所有设备串行同步 init | **缺**（见 P2-1，当前触发条件未到） |
| 电源管理 | `CONFIG_PM` + device runtime PM + tickless | `configUSE_TICKLESS_IDLE=0`，无常开以外的功耗策略 | **缺**（明确不做，理由见 §4） |
| 异步执行体 | `k_work` / `k_work_delayable` / `k_timer` | `configUSE_TIMERS=0`，无 work 队列 | **缺**（见 P2-2） |
| 日志 | 模块级 level（编译期可调 + 运行期 `LOG_LEVEL_SET`）、多后端、deferred 模式、`LOG_PANIC` 刷缓冲 | 编译期 3 级、单后端 UART、同步阻塞、无 ring | **缺 ring 与"故障前 N 条"**（见 P1-1） |
| 栈可见性 | `K_THREAD_STACK_DEFINE` 静态栈，map 可算 + `CONFIG_STACK_SENTINEL` | 动态栈在 `ucHeap`，只能上板读 `uxTaskGetStackHighWaterMark`；`configSUPPORT_STATIC_ALLOCATION=0` | **缺编译期可见性**（见 P1-2） |
| 内存保护 | `CONFIG_USERSPACE` + MPU 线程域 / `CONFIG_HW_STACK_PROTECTION` | 单特权级，canary 事后发现 | **缺**（见 P2-3） |
| 存储与升级 | NVS / LittleFS / MCUboot / mcumgr | 无文件系统、无 NVS、无 bootloader 分区 | **缺**（明确不做，理由见 §4） |
| 测试栈 | `ztest` + `twister` 跨板矩阵 + 覆盖率 | 无 host 单测；门禁是"源码破坏自证" | **两者强项不重叠**：本工程有"断言会不会漏红"的自证，Zephyr 有"判据函数本身被测" |
| binding 校验 | dt-schema（YAML schema + dtc 全量校验） | 自定义 YAML 子集；`collect_device_binding_errors` **只查 5 个写死项**（leds / keys / i2c / pcf8574 / beep） | **部分**：可升级为全量双向覆盖（见 P0-2） |
| 文档一致性 | 无强制门禁 | `doc-contract` / `doc-count` / `doc-reference` 三道 | **本工程更强** |

**一句话结论**：设备模型这一块已经不落后 Zephyr 了——注册表、类型化 api、init 分级 + 顺序、失败语义、就绪查询这五件对应物都齐了，其中"失败不放大"和"跨级都跑完"比 Zephyr 默认行为更严。真正落后的是**外围子系统**：日志、栈可见性、异步执行体、内存保护、升级能力。

---

## 3. 优化建议（按"收益 / 代价"排序）

### P0 — 纯判据，各半天，立刻能加

**P0-1 初始化顺序的依赖判据（有牙齿，零运行时成本）**

现状：`order` 数值是"谁先谁后"的唯一真源，靠人维护。`beep`(60) 依赖 `pcf8574`(50) 依赖 `i2c_gpio`(30)。把 beep 的 `order` 改成 25，编译、链接、体积、`verify` 全绿，板子上蜂鸣器在所有 I2C 设备之前 init —— `pcf8574_write` 拿到未初始化的总线，`BSL_ERR_IO` 被 `beep_apply_shadow()` 丢弃，现场只是"蜂鸣器不响"。

建议判据（加在 `collect_device_binding_errors` 或 `collect_board_polarity_errors`）：

- 读 `bindings/*.yaml` 的 `order` / `init_level`，建立 `node → order` 表；
- 断言 `pcf8574.order > i2c_gpio.order`、`beep.order > pcf8574.order`（前两条是总线依赖，不是审美）；
- 断言同级内不存在重复 `order`（现在 10/20/30/40/50/60 全不同，重复会让生成期排序退化成文件名序，静默改顺序）。

代价：一个判据函数 + `bsp`/`board-polarity` 组一段破坏（改 beep 的 order 为 25 → 期望报红）。收益：把"隐式契约"变成"编译期契约"。

**P0-2 binding ↔ DTS 的全量双向覆盖**

现状：`generate.py:3564 collect_device_binding_errors` 逐条写死检查 5 个 compatible（`brd,gpio-leds` / `brd,gpio-keys` / `brd,i2c-gpio` / `nxp,pcf8574` / `brd,pcf8574-beep`）。新增第 7 个设备时，写漏一条不会被发现；反过来，某个 binding 的 `compatible` 拼错、DTS 里没人用，也只在"设备少了"时表现为功能缺失。

建议：改成从 `bindings/*.yaml` 的 `compatible:` 字段自动收集集合，与 DTS 树里出现的 `compatible` 做**双向差集**：

- binding 有、DTS 无 → 报「该 binding 无消费者（拼错？或设备忘了加）」
- DTS 有、binding 无 → 报「该节点没有 bsl 段，不会进注册表」

代价：一个集合差集（bindings 已在 `load_bsl_bindings` 里解析）。收益：新增设备时"忘了两边对齐"这条静默路径被堵死。

**P0-3 设备的"必需 / 可选"显式化**

现状：`fml_button.c:67` 把 `keys == NULL || beep == NULL` 判成"降级、整段跳过"——语义上是"这两个可选"；而 `main.c:25` 对 `bsl_board_periph_init()` 的返回值 `(void)` 丢弃。也就是说"哪个设备缺失算正常、哪个算故障"只写在 if 里，`bsl_init_run` 的 `first_err` 没人消费。

建议：binding 加 `optional: true|false`（默认 true），生成期产出 `bsl_device_is_optional(id)`，`bsl_board_periph_init()` 只把**必需**设备的失败合成 `first_err`；同时 `main.c` 消费它（`osl_boot_stage_note` 或打一条 ERR）。

代价：binding 字段 + 生成器 + `bsl_init.c` 一处判断。收益：把"可选"从注释/if 里搬到数据里，且顺带让 `first_err` 有消费者（现在它是纯死值）。

### P1 — 改代码 + 判据，各 1~2 天

**P1-1 日志 ring + panic 快照（对应上一轮 4.3）**

现状：日志是"写过就没了"。一次 IWDG 复位后，`g_panic_record`（140 B）只说明"哪条路炸的"，说明不了"炸之前发生了什么"。串口在故障瞬间也可能已经不可用。

建议：RAM 里 64 B ~ 128 B 的 ring（每条只存 `模块 id(uint8) + 事件码(uint8) + tick 低 16 位`），`bsl_log_puts` 每写一次推进；`osl_panic_report` 把 ring 头 + 最近 N 条的索引写进 record 或独立 `.noinit` 段（当前 `.noinit` 用了 0x20004190~0x20004270，还有空间但要实测）。

代价：`bsl_log_puts` 多一条写 ring；record 扩字段（要同步 `PANIC_RECORD_MIN_BYTES` 与 `_Static_assert`）。收益：现场从"只有一个码"变成"码 + 最后几次动作"。

**P1-2 任务栈静态化（从"上板才知道"变成"编译期可见"）**

现状：`configSUPPORT_STATIC_ALLOCATION=0`，栈在 `ucHeap` 里动态分配。栈余量只能跑起来后读 `uxTaskGetStackHighWaterMark`，门禁无法断言"总栈量上限"。

建议：开 `configSUPPORT_STATIC_ALLOCATION=1`，`osl_thread_create` 改成 `xTaskCreateStatic` + 每任务 `static StackType_t stack[WORDS]`（落 `.bss`）。收益：map 里能算总栈量、可加"栈总量 ≤ N"的产物判据、`high_watermark` 仍是运行期补充。

代价：`osl_thread.c`/`osl_thread.h` 改造 + 2 个调用点 + `bsl` 层不感知；判据要同步（`artifact` 组的符号检查里 `uxTaskGetStackHighWaterMark` 仍在）。

**P1-3 初始化耗时观测**

现状：`bsl_init_run` 只记成功/失败，不记耗时。软件 I2C 的 `iic_wait_ack` 在本轮才把窗口从 30 µs 修到 400 µs —— 这类"init 里等多久"没有任何读数。

建议：每次 `driver->init()` 前后取 `osl_tick_now()`，差值超过阈值（如 5 ms）打一条 ERR 并记进 health。收益：慢设备立刻可见；也能量化"启动到调度器"的总时长。

代价：`bsl_init.c` +~40 B，`osl_health` 加 1 个槽或复用 `first_err` 旁边。

### P2 — 条件触发，现在不建议动

| 建议 | 触发条件 | 为什么现在不做 |
|---|---|---|
| P2-1 延后初始化（`DEVICE_DEFERRED_INIT` 等价物） | 出现单设备 init > 10 ms，或需要"内核起来后再探总线" | 当前 6 个设备都是寄存器写，全部 init 在几百 µs 级；加了只是多一份状态机 |
| P2-2 `k_work` / 软件定时器等价物 | 出现事件驱动需求（长按、双击、蜂鸣器节奏、迟滞重试） | `configUSE_TIMERS=0` 是主动选择；现在只有 2 个周期任务，加 timer 会带进 `prvTimerTask` 的栈与优先级 |
| P2-3 MPU / 硬件栈保护 | 产品化交付、多人并行改代码 | 当前单特权级 + canary(2)；MPU 分区要先算 RAM 布局，收益在"隔离野指针"而非当前的单线程风险 |
| P2-4 host 单元测试（判据函数本身） | 判据逻辑再复杂化（如出现二进制/数值解析类判据） | 现有 51 组"破坏自证"已覆盖"判据会不会漏红"，缺的是"判据算得对不对"；当前判据多是字符串匹配，误算风险低 |
| P2-5 升级/存储（NVS、MCUboot） | 交付客户、需要现场升级 | 当前是实验室板：改固件插调试器即可；引入 bootloader 会动链接脚本与启动流程 |

---

## 4. 明确不做（含理由）

- **PM / tickless idle**：当前板 USB 常供电，"省电"没有收益；`configUSE_TICKLESS_IDLE=0` 也让 `osl_tick_ms_advance` 的 CYCCNT 折算逻辑保持简单（那条逻辑本身有 gate 守着）。
- **统一 `compatible` 命名**：用户已明确拒绝（`nxp,pcf8574` 是 upstream 风格，`brd,*` 是板级私有），混用是刻意保留的。
- **删 `generate.py` 的 `default_gpio_config_and_function` / `format_optional_function_modes`**：`docs/hardware/pinmap/pinmap_tool.py:161/171` 在用。
- **把 vendor 的 redundant-decls 当自研缺陷改**：vendored HAL/CMSIS 的告警不在本项目责任范围。

---

## 5. 若要落地，第一步最小动作

按 P0 顺序做，每条都能独立验收：

1. **P0-1**：写 `collect_binding_order_errors()`（`build/checks/dts.py`）+ 注册进 `ASSERTION_GROUPS["board-polarity"]`（或新建组）+ 一段破坏（beep 改 25 → 期望报红）→ `gate-selftest --only <组> --assert-map`。
2. **P0-2**：把 `collect_device_binding_errors` 里 5 条写死检查换成集合双向差集；破坏段改成"删掉一个 DTS 节点的 compatible"→ 期望报红。
3. **P0-3**：binding 加 `optional` 字段 → 生成 `bsl_device_is_optional()` → `bsl_board_periph_init` 只聚合必需设备失败 → `main.c` 消费返回值；破坏段"把 beep 标成必需后把 beep init 打回失败"→ 期望 `first_err` 非零被记录。

每条都要同步：`.skills/工程规格.md` 的判据描述、`docs/design/系统设计.md` 的机制表、`.skills/架构缺陷.md` 的敞口表。

---

## 6. 证据边界

- 本报告的"当前架构"结论全部来自本轮实际读取（§1 列出的路径），未读的：`hal/` 内部（ST HAL + CMSIS，vendored）、`osl/src/freertos/*`（vendored 内核）、`project/dts/soc/stm32f429-gpio-default.dtsi` 全文（只确认了生成事实）。
- `order` 排序的实现位置是 `generate.py:2455` 的 `rows` 结构与 `collect_board_registry_entries`；registry 的实际顺序是用生成物 `board_device_registry.h:56-62` 反查验证的，不是从代码推断的。
- Zephyr 侧的机制描述基于其公开设计（device model / SYS_INIT / LOG / PM / ztest），未在本环境核对具体版本源码；凡涉及"Zephyr 也有"的判断都按机制层面表述，不引用具体行号。
