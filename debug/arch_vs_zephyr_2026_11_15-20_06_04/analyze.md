# 架构对比：stm32-freertos 五层自研栈 vs Zephyr

时间：2026-11-15 20:06
性质：架构分析（只读）。本轮不改代码。
证据：`docs/design/系统设计.md`（7 条层边 / 36 处实测）、`do` 侧源码行号、`Makefile` / `toolchain.make` / `project/config/*defconfig`（131 行）、`project/dts/bindings/`（10 份 YAML）、`build/checks`（52 条判据 + 51 组破坏自测）、CI workflow（`build` + `gate-list` + `gate-selftest` 矩阵）、`out/user` 实测（text 14420 B / Flash 1.38% / RAM 9.43%）。

## 0. 结论先行

两者不是"同一个东西的新旧版本"，而是两条不同的取舍：

- **Zephyr = 通用平台**：几百块板、几十个 `arch`/`soc`/`drivers`/`subsystem`，靠 CMake+Kconfig+west 把"配置空间爆炸"管起来，代价是最小镜像也在十几~几十 KB Flash、构建链依赖 Python/CMake/dtc 一整套。
- **本工程 = 极小可验证的专用栈**：单板（apollo / STM32F429IG）、14.4 KB Flash、五层目录强制分层、**生成物入库**与**判据自证门禁**（51 组"破坏→报红→按字节还原"），代价是每加一个能力都要自己写判据，没有任何生态可复用。

**重叠最深的其实是三处**：DTS→C 代码生成、设备注册与初始化顺序、生成物一致性校验。本工程在这三处已经用上了 Zephyr 生态的 `edtlib`（`project/dts/generated/edt-bindings/` 就是为了让 edtlib 认 binding 而落盘的副本），只是**预处理与驱动模型是自己写的最小实现**。

所以改进方向不是"迁 Zephyr"，而是**挑 4 个机制补进来**（驱动 API 类型安全、init 顺序显式化、日志后端化、binding schema 校验），并把"明确不追"的能力写进台账。§5 给方案，§6 给"什么条件下才值得真迁移"的判据。

## 1. 能力域逐项对比

| 能力域 | 本工程（现状，带证据） | Zephyr | 差距的实际影响 |
|---|---|---|---|
| 构建驱动 | GNU Make + 自写 `project/generate.py`（约 4900 行）生成 Kconfig/Makefile/头文件；`python build/build.py update` 一条命令贯通 | CMake + Kconfig(kconfiglib) + west（多仓库 manifest） | 本工程无"配置求解"，加配置项要人肉改三处（Kconfig/defconfig/Makefile）；Zephyr 有 `menuconfig` 与 `depends on` 求值 |
| 配置系统 | `out/<variant>/.config` 被根 `Makefile:8` `-include`，用 `ifeq ($(CONFIG_X),y)` 做**源文件级裁剪**；只有 `CONFIG_STM32%` 经 `toolchain.make:41` 转 `-DSTM32F429xx` | Kconfig → `autoconf.h` + `IS_ENABLED(CONFIG_X)`，C 代码里可对任意配置分支 | **本工程的配置几乎不进入 C**：`#if CONFIG_*` 在自研源里几乎不存在，所以"同一份源码两种行为"的能力缺失，只能新增/删除源文件 |
| 设备树预处理 | 自写 parser（`generate.py:793 parse_board_dts`，232 行）+ `edtlib` 构建 EDT 对拍 + 快照 `project/dts/generated/apollo-edt.dts` | 预处理器 → `dtc` 展开成完整 `zephyr.dts` → `edtlib` → `devicetree_generated.h` + `DT_*` 巨量宏 | 本工程没有通用 overlay 合并/phandle 全语义（只有 DTS 子集），但**生成物入库 + 「生成物必须与提交一致」CI 步骤**比 Zephyr 更严（Zephyr 的 generated 头不入库） |
| binding 使用 | 10 份 YAML（9–29 行）在 `project/dts/bindings/`，复制到 `generated/edt-bindings/` 供 edtlib 用；判据 `_registry_errors` 检查 10 份 binding 含 `compatible:`/`properties:` | binding YAML 是**驱动契约的唯一真源**：`DT_DRV_COMPAT` ↔ binding 的 `compatible`、`properties` 带类型/默认/`required`，`edtlib` 做类型校验 | 本工程 binding 只被用来"让 EDT 认得节点"，**属性类型/必填没有 schema 校验**，全靠 19 条手工判据逐字段对拍（`dts.py` 的 `_board_parse_errors` 等） |
| 驱动模型 | `bsl_device_t { id, compat_id, name, init_level, driver }` + `bsl_device_driver_t { void(*init)(void); const void *api; }`（`bsl/inc/bsl_device.h:9-20`）；API 靠强转 `(const bsl_gpio_leds_api_t *)dev->driver->api`（`fml/src/fml_led.c:21`） | `DEVICE_DT_DEFINE` / `DEVICE_DECLARE` + `DEVICE_API`（api 结构类型参与编译期检查）+ `device_is_ready()` + iterable sections（链接器排序） | **本工程 typo 一个 api 结构名不会报错**，只会在运行期按错布局取函数指针；设备表是静态数组（`bsl_device.c:14-16`），新增设备要重生成 |
| 初始化顺序 | 两级枚举 `BSL_INIT_LEVEL_POST_CLOCK` / `BOARD`（`bsl/inc/bsl_init.h:4-7`），`bsl_board_periph_init()` 串跑两轮；`main.c:14-25` 手工排"health→system→periph→任务→调度器" | 四级 `PRE_KERNEL_1 < PRE_KERNEL_2 < POST_KERNEL < APPLICATION`，同级按链接顺序，`SYS_INIT(fn, level, prio)` | 本工程"谁先谁后"靠人写在 `main` 里 + 注册表项顺序；跨级依赖（UART 必须在 LED 之前）没有机制保证，只有 `collect_layer_dependency_errors` 管了 **头文件** 依赖，管不了**调用时序** |
| RTOS 能力面 | FreeRTOS 最小集：2 个优先级（`configMAX_PRIORITIES=2`）、task/queue/semphr 编进来但自研只用 task；`osl_thread.h` 只暴露 create/delay/tick/watermark 5 个 API | `k_thread`/`k_sem`/`k_mutex`/`k_msgq`/`k_pipe`/`k_fifo`/`k_work`(workqueue)/`k_timer`/`k_poll`/`sys_heap`（多后端堆） | 本工程没有 work/soft-timer（`configUSE_TIMERS=0` 明确关掉）、没有多堆分配器（只有 heap_4 16 KB + newlib 512 B `_sbrk`）；功能扩展（异步事务、周期回调）无现成承载体 |
| 日志 | `bsl_log.h` 三级**编译期**宏（ERR/INF/DBG，默认 INF）；输出同步 `HAL_UART_Transmit(..., 100U)`（`board_debug_uart.c:56`），无时间戳、无模块过滤、无 ISR 保护 | `LOG_MODULE_REGISTER` + 每模块 `LOG_LEVEL`（编译期+运行期可调）+ 多后端（UART/RAM ring/shell/net）+ deferred 模式 + `LOG_PANIC`（panic 时刷缓冲） | 本工程日志是"阻塞打印"：**ISR 里绝对不能打**（100 ms 超时即卡死 tick hook）；现场复盘只有 `g_panic_record` 140 B，没有"故障前 N 条日志" |
| 故障取证 | `osl_panic.c:241 osl_panic_report()`：关中断→重入闸→看门狗 relax→写 `.noinit` record（magic/code/task_name/assert/fault 帧(17 字段)/seq/reset_cause/ipsr/堆栈水位）→三灯二进制 4 轮 | `k_sys_fatal_error_handler` + fatal reason；coredump 子系统（`coredump flash/uart`，dump 到 Flash 分区或串口）；`CONFIG_STACK_SENTINEL` | **本工程在"无调试器、跨复位取证"上更强**（`.noinit` + `seq` 代际，一次 IWDG 复位后仍能读上一代现场）；Zephyr 的 coredump 更完整（全寄存器/栈回溯），但依赖存储与格式化工具链 |
| 测试与门禁 | 52 条 `collect_*` 判据 + 51 组 `gate-selftest`（**破坏态自证**：改源码→期望报红→按字节还原，GitHub Actions 一组一个 job）+ 文档契约/引用/计数判据 + "生成物必须与提交一致" | `ztest`（单元/集成）+ `twister`（跨板矩阵）+ gcov 覆盖率 + `checkpatch` + 多种 sanitizer 配置 | **两种强项不重叠**：本工程没有 host 单元测试框架与覆盖率（判据函数本身不被测）；Zephyr 没有"断言强度自证"（它的门禁靠"测试够不够多"，不验证"断言会不会漏红"） |
| 静态分析 | 只有 `-Wall -Wextra -Werror -fstack-usage`（`makefile.py` 判据守这三项）；本轮审计实测 `-Wmissing-prototypes` 能抓出 13 处 | `-Wall -Wextra` + 可选 `CONFIG_COMPILER_WARNINGS_AS_ERRORS`、`checkpatch`、`clang-tidy`/`sparse` 类工具链、twister 带 sanitizer | 本工程把"可见性缺陷"整类留给了人工审计（本轮 13 处即证据） |
| 内存/栈 | 任务栈**动态**分配在 FreeRTOS `ucHeap`（16 KB，`configTOTAL_HEAP_SIZE`），栈余量只能靠运行期 `uxTaskGetStackHighWaterMark` 采到 `g_stack_free_min[]`；MSP 有 tick 级守卫（`osl_boot.c:218`） | 任务栈**静态** `K_THREAD_STACK_DEFINE` 落 `.bss`（map 里可算），`CONFIG_STACK_SENTINEL` canary，可选 `CONFIG_HW_STACK_PROTECTION`（MPU 守栈） | 本工程"栈用了多少"在编译期不可见，只能上板跑；门禁无法断言"栈总量上限" |
| 电源/低功耗 | `configUSE_TICKLESS_IDLE=0`（`FreeRTOSConfig.h:13`），无 PM 概念 | `CONFIG_PM` + 设备 runtime PM + tickless idle + `pm_device` 状态机 | 本工程功耗能力接近"常开"，当前场景（USB 供电的调试板）无影响；**若将来做电池产品，这是硬缺口** |
| 存储/OTA | 无文件系统、无 NVS、无 bootloader 分区；`flash/flash.py` 只是一次性烧写（OpenOCD/pyocd 路径） | NVS、LittleFS、DT flash `partitions`、`settings`、MCUboot + `mcumgr`（差分/回滚/双 slot） | 本工程没有"升级"能力：改固件必须插调试器。对"实验室/自用板"够用，对"交付客户"是 P0 缺口 |
| 安全隔离 | 全部代码同一特权级；MPU 零引用（自研源里 `MPU`/`mpu_armv7` 命中 0 处，`hal/arch/inc/mpu_armv7.h` 是 vendored 但没人用）；buffer 溢出只能靠 canary（`configCHECK_FOR_STACK_OVERFLOW=2`） | `CONFIG_USERSPACE` + 线程权限/内存域 + syscall 跳板（ARMv7-M 用 MPU） | 本工程"一个野指针写坏 `ucHeap` 就全崩且难定位"；Zephyr 能把故障限制在一个线程 |
| 多板/可移植 | 单板；`project/dts/{soc,boards}` + 生成链已具备"换板只改 DTS"的形状，但 `hal/` 是 ST HAL、启动文件是 ST 模板改造 | `boards/<arch>/<board>/`（`.dts`+`.conf`+`board.yaml`）+ `arch`/`soc` 抽象，twister 可跨板上跑 | 本工程换 MCU 厂商 ≈ 重写 `hal` + `bsl`；Zephyr 换板是"加一个目录" |
| 文档与规格一致 | **代码-文档一致性是门禁**：`doc-contract`（规格必须覆盖链接序/层目录）、`doc-count`（层边处数四处互证）、`doc-reference`（文档引用的路径必须存在），`.skills/工程规格.md` 是权威手稿 | Doxygen + Sphinx 文档站；无"文档必须与代码数字一致"的强制门禁 | **本工程更严**：改了层边不改文档会报红；Zephyr 文档滞后不会失败构建 |
| 工具链 | `toolchain.make` 由 `build.py` 自生成（优先 `STM32_TOOLCHAIN_ROOT`，否则 `tool/<平台>`）+ `install-toolchain` 下载/校验 | CMake toolchain file + Zephyr SDK（多编译器/多版本） | 两者都做到"一条命令装好"；本工程不做多编译器矩阵 |

## 2. 三处"本工程已经走在 Zephyr 路上"

1. **DTS→C 生成是刚需而非玩具**：`board_dts.h` / `board_device_registry.h` / `board_panic_hw.h` / `board_debug_uart_dt.h` 全部由 DTS 生成，`bsl` 里没有一个手写引脚常量。Zephyr 的价值主张（"板级事实只写一次"）在本工程已成立，且**生成物入库**让 review 能看见"这次 DTS 改动到底改了哪些 C 常量"——Zephyr 做不到（generated 文件不入库）。
2. **`edtlib` 已经在用**：`project/dts/generated/edt-bindings/*.yaml` 是给 `from devicetree import edtlib` 准备的 binding 副本，`apollo-edt.dts` 是 EDT 快照，门禁 `_edt_errors` 做"逐字段对拍"。也就是说**本工程信任的是 Zephyr 生态的解析器**，只是自己写了 DTS 预处理与代码发射。
3. **破坏式门禁自证（51 组）**：这是 Zephyr 都没有的机制。Zephyr 的门禁是"测试数量与覆盖率"，而本工程要求**每条断言证明它会红**（改一处→报红→按字节还原→跑 `build`+`verify` 复原），并让 `ASSERTION_GROUPS` 与实测触发集合双向比对。对"断言被悄悄改弱"这类腐败，这套机制比覆盖率有效。

## 3. 六处结构性落后（机制级，带代码坐标）

### 3.1 驱动 API 无类型检查 → 运行期按错布局取函数指针

```c
/* bsl/inc/bsl_device.h:9-12 */
typedef struct bsl_device_driver {
    void (*init)(void);
    const void *api;          /* ← 类型信息在这里断掉 */
} bsl_device_driver_t;

/* fml/src/fml_led.c:21 */
return (const bsl_gpio_leds_api_t *)dev->driver->api;   /* 强转，编译器不校验 */
```
Zephyr 的 `DEVICE_API` 让 api 结构类型进入编译期检查（`DEVICE_DT_DEFINE` 时校验 `api` 的实际类型），且 `device_is_ready()` 是统一入口。本工程把 api 变成 `void*` 之后，"注册表里的 api 指向哪个结构"完全靠人记。

### 3.2 初始化顺序只有两级且无跨级依赖保证

`bsl_init.h:4-7` 两个 level（POST_CLOCK / BOARD），`bsl_board_periph_init()` 先跑一遍 POST_CLOCK 再跑 BOARD。但"BOARD 级的 `beep` 依赖 POST_CLOCK 级的 `i2c_gpio` 已经把总线拉起来"这件事，只由 **注册表项顺序**（`board_device_registry.h:44-49`）保证——顺序错了没有任何门禁会红（`_registry_errors` 只查 6 条 tuple 的字段内容，不查顺序语义）。
Zephyr 用 level + prio 显式声明，且同 level 内链接顺序可解释；跨级依赖由"level 更低者先跑"天然保证。

### 3.3 配置系统没有进入 C 代码

`Makefile:8` `-include out/$(BUILD_VARIANT)/.config`，`:26-28` 用 `ifeq ($(CONFIG_APL_SRC_MAIN),y)` 决定**要不要编这个 .o**。这套只支持"裁剪源文件"，不支持"同一文件里两种实现"。Zephyr 的 `autoconf.h` + `IS_ENABLED()` 支持后者。
后果：本工程做"可选功能"（例如可选 shell、可选低功耗模式）时，只能拆成独立源文件，或者干脆不做——这也是它功能面窄的结构性原因。

### 3.4 日志同步阻塞，且 ISR 不可用

`board_debug_uart.c:56` 是 `HAL_UART_Transmit(&s_huart_debug, ..., 100U)`：阻塞式轮询发送，最长 100 ms 超时。于是：
- **不能在 tick hook / 任何 ISR 里打日志**（115200 下发 40 字节 ≈ 3.5 ms，直接拖垮 1 kHz tick）；
- 没有"故障前 N 条日志"：`g_panic_record` 只有 140 B 结构化字段，没有消息历史；
- 没有时间戳，多任务日志交错时无法排序。

### 3.5 没有 work 队列 / 软件定时器 / 多堆

`configUSE_TIMERS=0`（明确关掉，省 ~510 B Flash + ~0.8 KB RAM），`configMAX_PRIORITIES=2`，堆只有 `heap_4` 16 KB + newlib 512 B。自研层现在只有两个周期任务。
这不是缺陷——是**刻意的极简**。但它定义了扩展边界：任何"延迟执行/超时回调/异步事务"的需求，要么自己开一个新任务（每任务 ≥ 256 词栈 + TCB），要么加软件定时器（把 RAM 从 9.43% 推上去）。在本工程语境下，这是取舍不是落后，列在这里是为了"要扩功能时先算这笔账"。

### 3.6 无存储、无 OTA、无 MPU

三项都是"当前不需要、但一旦产品化就是硬缺口"：改固件要插调试器（`flash/flash.py` 是烧写不是升级）；野指针写坏堆无隔离；EEPROM/配置持久化无处可放（`.noinit` 只跨复位、不掉电）。

## 4. 改进建议

### P1（建议做，投入产出比最高）

#### 4.1 驱动 API 类型安全化（吸收 `DEVICE_API` 思想，不引入 iterable sections）

目标：让"api 指向哪个结构"在编译期确定，杀掉 `const void *api` 与强转。

做法（三选一，按成本从低到高）：
- **A. 生成期类型化访问器**：`generate.py` 生成 `bsl/inc/board_device_ops.h`，为每类 api 生成 `static inline const bsl_gpio_leds_api_t *bsl_leds_ops(const bsl_device_t *dev)`，内部断言 `compat_id` 匹配（`_Static_assert` 无法查运行期值，但可用"开关 + 默认分支"），fml 只调访问器、不再强转。
- **B. 把 `void *api` 换成带 tag 的 union**：`bsl_device_ops_t { bsl_compat_id_t tag; union { const bsl_gpio_leds_api_t *leds; ... }; }`，访问器 `switch(tag)` + `default: panic`。
- **C. 学 Zephyr 用链接器排序 + `DEVICE_DT_DEFINE` 宏**：需要 section 属性与链接脚本配合，对当前 8 个设备是过度设计。

**建议 A**（改动最小、与生成链同源、门禁友好）。守门：`bsl.py` 加一条"`fml`/`apl` 里不得出现 `(const bsl_*_api_t *)` 强转"，否则报红。

**已落地（本轮，选 A，但机制从"运行期断言"加强到"编译期定型"）**：原 A 草案打算靠 `compat_id` 做运行期断言，而 §8.4 已把 `compat_id` 整套删掉，于是改成让**类型与符号都进编译期**：

- binding 新增 `api_type` / `api_sym`（leds / keys / pcf8574-beep 三份；两条只写一条时 `_parse_binding_bsl_block` 抛 `ValueError`）；
- `generate.py` 产出 `extern const <api_type> <api_sym>;` + `static inline const <api_type> *bsl_devapi_<node>(void)`，落在 `board_device_registry.h`（该头已被 fml 与 `bsl_device.h` include，不新增生成物）；
- `bsl_device_driver_t` 只剩 `init`（`api` 成员删除），4 个 driver 定义去掉 `.api = ...`；
- fml 三处（`fml_leds_ops` / `fml_keys_ops` / `fml_beep_ops`）改成 `return bsl_devapi_<node>();`；`dev->driver->api == NULL` 的判空随之删除（`bsl_device_is_ready()` 已覆盖"id 不在表里 / init 失败"两种情形）；
- 顺带删掉零消费的 `bsl_debug_uart_api_t` / `bsl_api_debug_uart`（`puts` 全仓无调用，`bsl_log` 直连 `board_debug_uart_puts`）。

实测：user `text` 14716 → **14560**（-156 B），userdebug 16736 → **16644**（-92 B），RAM 18544 不变。守门：新增 `collect_device_api_errors()`（5 条）+ `bsp` 组第四段（把 `const void *api` 塞回 driver 结构 → 期望报红），整组 4 段 **247.0 s PASS**，`--assert-map` 实测 4 条断言且无漂移。

#### 4.2 初始化顺序显式化 + 跨级依赖可判

目标：把"注册表顺序 == 执行顺序"这个隐式契约变成显式声明。

做法：
- `bsl_init.h` 的 level 从 2 个扩到 4 个：`PRE_CLOCK` / `POST_CLOCK` / `BOARD` / `APP`（对应 Zephyr 的 PRE_KERNEL_1/2、POST_KERNEL、APPLICATION 的最小版）；
- 生成器为每个设备产出 `BSL_DEV_LEVEL_<id>` 与 `BSL_DEV_DEPENDS(<id>, <dep_id>)`（依赖写在 binding 里，例如 `brd,pcf8574-beep.yaml` 里加 `depends-on: [i2c]`）；
- 新增判据 `collect_init_order_errors()`：从生成表算拓扑序，若"依赖设备的 level 不小于被依赖者"或拓扑有环，报红。

成本：生成器 ~150 行 + 1 条判据；验证用 `gate-selftest` 新增 `init-order` 组（把 `apollo.dts` 里 `beep` 的 `depends-on` 写错 → 期望报红）。

#### 4.3 日志后端化 + panic 前 N 条留存

目标：ISR 可打（非阻塞）、panic 现场能带"故障前日志"。

做法：
- `bsl_log` 增加一个 RAM ring（例如 16 条 × 64 B = 1 KB，放 `.noinit`，跨复位保留），写入是"拷结构体 + 推进 head"，不做格式化、不阻塞；
- UART 输出改成"后端"：`BSL_LOG_BACKEND_UART`（现有同步路径，仅任务上下文）与 `BSL_LOG_BACKEND_RAM`（ISR 可用）；
- `osl_panic_report()` 落盘时把 ring 的 head/count 与 ring 首地址写进 `g_panic_record`（新增 2 个 `uint32_t`，注意 `record-size` 门禁的 0x80 下限，加 8 B 到 148 B 仍安全）；
- 给 `bsl_log_*` 加"是否在 ISR"判据（`__get_IPSR()`），ISR 上下文自动只走 RAM 后端。

成本：~200 行 + 1 条判据 + `panic-generation` / `record-size` 组要复跑（因为 record 结构变了）。

#### 4.4 binding schema 校验（把手工判据收敛）

目标：`dts.py` 现在 19 条判据里，`_board_parse_errors` 等一大半在逐字段对拍"这个属性在不在、值是多少"。这些事 binding YAML 已经声明了。

做法：
- 给 10 份 binding 补 `properties: <name>: {type: int|string|boolean|int-array, required: true}`（大多数 YAML 已有 `properties:`，见 9–29 行规模）；
- 自写 parser 读到节点后，按 binding 的 schema 做通用校验（类型/必填/枚举），把"字段存在性"判据合并成 1 条 `collect_dt_binding_errors()`；
- 保留语义判据（例如"pinmap 必须 140 条""overlay 端口集合 = abcdefghi"），这些 schema 表达不了。

收益：19 条手工判据压到 ~8 条 + 1 条通用；新增一个 DTS 属性只需改 binding，不必再写一条判据。
成本：parser 加 ~120 行；binding 补字段；`device-dts` 组要重跑（这条组耗时 101 s，可接受）。

### P2（有明确触发条件再做）

| 建议 | 触发条件 | 做法要点 |
|---|---|---|
| 4.5 `-Wmissing-prototypes` 等下沉进构建 | 立刻（成本 1 行 + 判据 1 条） | 见上一轮审计 §1.5；`CFLAGS` 加 `-Wmissing-prototypes -Wstrict-prototypes -Wredundant-decls -Wundef -Wshadow`，vendor 单元豁免 `-Wno-redundant-decls` |
| 4.6 host 单元测试（pytest） | 当 `generate.py` 的 parser/EDT 对拍继续增长（已 4900 行） | 只测纯函数（DTS 解析、binding schema、判据里的文本抽取 helper），不测"会不会报红"（那是 gate-selftest 的职责） |
| 4.7 任务栈静态化 | 当需要"编译期断言栈总量上限"或 RAM 逼近上限 | 开 `configSUPPORT_STATIC_ALLOCATION`，栈落 `.bss`，map 里可算；`artifact` 判据加"栈总量 ≤ N B" |
| 4.8 tickless / PM | 出现电池供电需求 | 不要半吊子：要么做 `FreeRTOS` 的 `configUSE_TICKLESS_IDLE=1` + `vPortSuppressTicksAndSleep`，要么明确记录"不支持" |

### 明确不追（写进台账，避免反复讨论）

| 能力 | 为什么不追 |
|---|---|
| 迁 CMake + west | 现有 Make+Python 生成链已经把"配置/生成/门禁"打通，迁移收益是"生态便利"，成本是重写 4900 行生成器 + 51 组门禁全部重标定 |
| Kconfig 全求解（kconfiglib） | 当前配置项本质是"源文件裁剪表"（131 行 defconfig），没有 `depends on`/`choice` 语义需求；引入求解器只会让 `.config` 与 Make 层裁剪的关系更难解释 |
| userspace + MPU 隔离 | 单板、无第三方代码、无不受信输入；MPU 收益低于"给每个线程配域"的调参成本。若要防栈溢出，`CONFIG_STACK_SENTINEL` 式 canary 更便宜 |
| 多 arch / 多厂商 HAL 抽象 | 项目明确单芯片（`system_stm32f4xx.c:10 #error "for STM32F429xx only"` 就是这个立场）。抽象层等于把 Zephyr 的 `arch`/`soc` 抄一遍，没有第二个受益者 |
| 引入 Zephyr 的日志框架 | 需要 Kconfig + 多后端 + deferred 的完整地基，与本工程"Make 层裁剪"的配置模型冲突；4.3 的轻量 ring 已覆盖实际需求 |

## 5. 改进优先级总表

| 序 | 项 | 收益 | 成本 | 破坏面 | 验证 |
|---|---|---|---|---|---|
| 1 | 4.5 告警下沉 | 关门"可见性缺陷"整类 | 1 行 + 1 判据 | 可能冒出 vendor 告警，需豁免 | `update`/`verify`/`makefile` 组 |
| 2 | 4.1 驱动 API 类型化 | 消除强转与错布局风险 | ~80 行生成 + 1 判据 | fml 三处调用点改写 | `bsp`/`device-dts` 组 |
| 3 | 4.2 init 顺序显式化 | 把隐式顺序契约变机制 | ~150 行 + 1 判据 + 1 组 | 注册表生成格式变化（`_registry_errors` 要改） | 新增 `init-order` 组 |
| 4 | 4.4 binding schema | 判据从 19 条压到 9 条 | ~120 行 + binding 补字段 | `dts.py` 判据重排 | `device-dts` 组（101 s） |
| 5 | 4.3 日志 ring | panic 现场带上下文 | ~200 行 + record 扩 8 B | `osl_panic` 落盘、record-size 组 | `panic-code` / `panic-generation` 组 |

顺序原则：**先做"关门型"（4.5），再做"减码型"（4.1/4.4），最后做"加字段型"（4.2/4.3）**——后两者都会动生成格式或跨复位结构，破坏面大，放到门禁已经加固之后。

## 6. 什么条件下才值得真迁 Zephyr

给三条硬判据，全部满足才考虑：

1. **出现第二个以上硬件目标**（第二块板 / 第二个 MCU 厂商）——否则 Zephyr 的 boards/soc 复用率用不上；
2. **需要 OTA / 文件系统 / 安全隔离中的至少两项**——这些自己写的成本远超迁移成本（MCUboot + NVS + userspace ≈ 数千行 + 长期维护）；
3. **镜像预算能接受 ≥ 24 KB Flash / ≥ 8 KB RAM**（Zephyr 最小配置在本板上的经验量级），当前本工程是 14.4 KB / 9.4% RAM，迁移等于放弃"极小"这个最大优势。

反过来说：**当前三条全不满足**，所以最优解是"留在自研栈，按 §5 补 4 个机制"。这也是这份对比的实质结论——不是"Zephyr 更好"，而是"Zephyr 用通用性换体积与复杂度，本工程用体积换自建成本，现在该把自建成本里最贵的四块（类型安全、顺序契约、日志上下文、schema 校验）补上"。

## 7. 复核方式

- 层边/结构数据：`python build\build.py verify --artifacts`（系统设计.md 的 7 条边/36 处由 `collect_doc_count_errors` 对拍）；
- 门禁规模：`python build\build.py gate-selftest --list`；
- 资源占用：`build --no-update` 打印的 `text/Flash/RAM` 行（当前 14420 B / 1.38% / 9.43%）；
- 本文引用的每个本工程行号，均可用 `Read` 复核；Zephyr 侧机制名（`DEVICE_API`、iterable sections、`SYS_INIT`、`LOG_MODULE_REGISTER`、coredump、`CONFIG_USERSPACE`、`NVS`、`MCUboot`）为其公开文档中的概念名，未引用具体版本行为。

---

## 8. 复核后追加：上轮清单之外的三条（2026-11-15 晚）

§4 的四条 P1 仍然成立（除 4.5 已完成）。本节记录**在那之后**又查出的三点，都是本轮实测确认、且与前四条不重叠。

### 8.1 状态刷新

| 项 | 上轮状态 | 现态 |
|---|---|---|
| 4.5 告警下沉 | 建议做 | **已完成**：`CFLAGS` 含 `-Wmissing-prototypes -Wstrict-prototypes`，`collect_warning_gate_errors()` 守着，`warning-gate` 组以"删 `-Werror` 必红"自证 |
| 4.1 驱动 API 类型化 | 建议做 | 未做（见 8.2，这条优先级应上调，因为发现了更硬的问题） |
| 4.2 init 顺序显式化 | 建议做 | 未做 |
| 4.3 日志 ring | 建议做 | 未做 |
| 4.4 binding schema | 建议做 | 未做 |
| （上轮未列）设备表条数 | — | **已关闭**：`bsl_device_at/get/count` 改用 `sizeof`，`bsl_device_get` 按 `.id` 线性查找，并加 `_Static_assert(BSL_DEV_COUNT == 条目数)`；`bsp` 组第二段以"手改生成物删一条 `M(...)` 必须编译失败"自证 |
| （上轮未列）DTS 极性双写 | — | **已关闭**：生成器对"`gpios` flags 与 `pressed-*` 矛盾"抛 `ValueError`；`board-key` 第三、四段自证 |

### 8.2 缺陷：干净 api 结构里躺着一个从不被调用的 `init`

```c
/* bsl/inc/bsl_device_api.h:16-22 —— 每类 api 都有 init */
typedef struct {
    void (*init)(void);            /* ← 全仓没有任何调用点 */
    void (*set)(board_led_id_t id, int on);
    ...
} bsl_gpio_leds_api_t;

/* bsl/src/board_led.c:74-85 —— 同一个函数被写进两处 */
const bsl_gpio_leds_api_t bsl_api_brd_gpio_leds = { .init = board_led_init, ... };
const bsl_device_driver_t bsl_driver_brd_gpio_leds = { .init = board_led_init,
                                                       .api = &bsl_api_brd_gpio_leds };
```

唯一的初始化调用点是 `bsl/src/bsl_init.c:19-21` 的 `dev->driver->init()`；`fml` 三个消费点（`fml_led.c` / `fml_button.c` 两处）只用 `set/on/off/toggle/pressed/pin_high`，从不碰 `api->init`。实测 `.init = ` 的 11 处赋值里，有 5 处是 api 结构（`board_led.c:75`、`board_key.c:95`、`board_debug_uart.c:81`、`board_beep.c:139`、`bsl_i2c_gpio.c:172` 之外的同名对）。

**为什么这是缺陷而不是冗余**：它和本轮刚修掉的"DTS 极性写两遍"是同一类 —— 改 `api->init` 会静默不生效，而两处恰好都叫 `init`、都指向同一函数，肉眼极难分辨哪个是活的。`-Wredundant-decls` 抓不到（不是重复声明），`-Wmissing-prototypes` 也抓不到（签名正确且有原型）。

**修法（零风险纯删除）**：从四个 `*_api_t` 里删掉 `init` 成员，driver 的 `init` 保持唯一入口。收益：`.rodata` 省 4 个函数指针（每结构 4 B），并消除"两套 init 入口"的误读。成本：4 处定义 + 相应 `.init =` 赋值行；验证 `build` + `verify --artifacts` + `board-key`/`bsp` 组。

**已落地**（本轮）：删掉 4 个成员（`bsl_device_api.h` + `board_led.c` / `board_key.c` / `board_debug_uart.c` / `board_beep.c` 各自的 api 表赋值行）。实测两档 `text` 各 -16 B（user 14420→14404、userdebug 16500→16484），`update` / `build --no-update` / `verify --artifacts` 全 EXIT=0，`bsp` 组 PASS 205.3 s（两段破坏仍红）。

对照 Zephyr：`DEVICE_DT_DEFINE` 只有一个 `init_fn`，api 结构里**不含** init —— 本工程多出来的那个是历史遗留。

### 8.3 契约缺口：init 无返回值，设备"就绪"不可判

- `bsl/inc/bsl_device.h:10`：`void (*init)(void);` —— 没有返回值通道；
- `bsl/src/board_beep.c:63-68`：`board_beep_init()` 里 `beep_set_pcf8574_logical(0)` → `beep_apply_shadow()`（`:40-43`）**丢弃** `pcf8574_write()` 的 `bsl_err_t`（该函数 `:25-27` 会在总线未就绪时返回 `BSL_ERR_IO`）；
- `bsl/src/board_beep.c:31-37`：写失败只有一条 `BSL_LOG_MOD_ERR`（`s_i2c_fail_logged` 单次门控），**卡上没有任何状态位**；
- `bsl/src/bsl_init.c:19-21`：调用处结构上没有返回值可看；
- `fml/src/fml_button.c:29-34`：消费方只能判 `dev` / `driver` / `api` 非空，判不了"这个设备初始化成功了"。

后果：I2C 总线没拉起来时，`board_beep` 会一路"初始化成功"地进入运行态，`fml_button_beep()` 调 `api->set(1)` 静默无效——现场表现是"蜂鸣器坏了"，而日志里那一条 ERR 若被 UART 时序或 `INF` 级过滤吃掉，就完全没有线索。

对照 Zephyr：`int (*init)(const struct device *dev)` + `device_is_ready()` + `DEVICE_INIT_STATUS_{UNINITIALIZED,INITIALIZING,READY,FAILED}`；初始化失败可由调用方显式检查，也可由 `device_is_ready()` 拦住。

**修法（中等，建议与 8.2 一起做，因为都改同一组结构）**：
1. `init` 改成 `bsl_err_t (*init)(void)`；`bsl_init_run()` 返回首个失败码并记进 `g_boot_stage`（复用现有 boot-init 通道，不新增 `.noinit` 字段）；
2. `bsl_device_t` 加 `bsl_dev_state_t state`（`READY` / `FAILED` / `UNINIT`），由 `bsl_init_run()` 写；
3. 新增 `bsl_device_is_ready(id)`，`fml_*` 的 accessor 改为先判 ready 再取 api；
4. 守门：`bsp` 组加一段——把 `board_beep_init` 改成返回 `BSL_ERR_IO` → 期望 `verify` 或 `boot-init` 报红；`panic`/`boot-init` 组复跑。
成本：~60 行 + 1 条判据；破坏面横跨 6 个 driver 的 `init` 签名与 3 个 fml 消费点。

**已落地（本轮）**，与上面的草案有两处偏差，都是 `s_devices[]` 是 `static const` 逼出来的：

- **`bsl_device_t` 不加字段** —— 它在 `.rodata` 里，写不了。改成 `bsl_device.c` 另起一张 `static uint8_t s_state[BSL_DEVICE_TABLE_COUNT]`（实测 `nm`：`s_state` 落 bss，大小 **6 B**，正好 6 个设备 × 1 B；用 `bsl_dev_state_t` 数组会按 4 B 对齐白占 24 B）。
- **不进 `g_boot_stage`** —— 那是 `board_system_init` 专用的通道，复用会让 `boot-init` 组的判据分不清"哪一级失败"。失败改成：写设备状态 + 一条 `BSL_LOG_MOD_ERR`，且**只在 `bsl_device_is_ready(BSL_DEV_DEBUG_UART)` 为真时打**（debug_uart 自己初始化失败时写 UART，会把 `Instance=NULL` 的 huart 交给 `HAL_UART_Transmit`，初始化失败变成 HardFault）。这一层"打印前先问 UART 就绪"也写进了判据。
- `bsl_board_periph_init()` 的返回值在 `main()` 里被 `(void)` 丢弃 —— 保持"外设失败不 panic"的既有容错语义，可见性由状态位与日志承担。

实测：user `text` 14404→**14716**（+312 B）、userdebug 16484→**16736**（+252 B），RAM 校验值 18544 B 不变；`bsl_device_is_ready` / `bsl_device_mark` / `bsl_device_state` 三个符号进镜像（0x0e / 0x14 / 0x18 字节），`board_beep_init` 0x10 字节。守门：新判据 `collect_device_state_errors()`（53 个 `collect_*` 之一），`bsp` 组加第三段——把失败分支的 `FAILED` 改成 `READY` → 期望判据报 `mark FAILED`。

### 8.4 无效代码：`find_by_compat` 与 `compat_id` 字段没有消费者

```c
/* bsl/inc/bsl_device.h:16,24 + bsl/src/bsl_device.c:55-65 */
const bsl_device_t *bsl_device_find_by_compat(bsl_compat_id_t compat_id);
```

全仓 grep：除定义与声明外**零调用**；`bsl_device_t.compat_id` 的唯一读点就在这个函数自身，即"字段只被一个没人调的函数读"。

它存在的理由是 Zephyr 式的"运行期按 compatible 查设备"，而本工程的绑定已经在**生成期**完成（binding YAML → `board_device_registry.h` 的 `M(...)` 静态表），运行期没有这个需求。留着它的实际风险是误导：后来者会以为"设备查找走 compat"，而去改 `BSL_COMPAT_*` 枚举——但它不影响任何行为。

**修法（需拍板，因为要动生成格式）**：删 `find_by_compat` + `bsl_device_t.compat_id` + `BSL_COMPAT_*` 全套枚举与 binding 里的 `compat_id:` 字段（7 份 YAML）。收益：去掉一套"看起来能用"的面子 API。成本：生成器 `write_board_device_registry()` 少产一段 + `_registry_errors` 判据要改（它现在查 6 条 tuple 的字段内容）+ `device-dts` 组复跑。
**保守替代**：保留枚举，但在 `bsl_device.h` 上标注"当前无调用方，按 compatible 查找已被生成期绑定取代"，并将其列入台账「已知敞口」。

**已落地（本轮，彻底删）**：`bsl_device_find_by_compat()` + `bsl_device_t.compat_id` + `bsl_compat_id_t` 枚举段 + 6 份 binding 的 `compat_id:`（**实测 6 份，不是 7 份**——`brd,usart-ch340` 是 `NONE` 也一并删）+ 生成器 `compat_enums`/`BoardRegistryEntry.compat_enum`，`M(...)` 由 5 参变 4 参。实测：`s_devices` = **96 B**（6 × 16 B 四字段行；删前 120 B），`find_by_compat` 已不在符号表，`bsl_compat_id_t` 在生成头里整段消失。判据侧：`dts.py` 的 `_EXPECT_REGISTRY` 4 元组→3 元组，`bsp` 组第二段的 `M(...)` 锚点同步改（否则该段会 RuntimeError）。`update` / `build --no-update` / `verify --artifacts` 全 EXIT=0。

### 8.5 这三条怎么排

| 序 | 项 | 收益 | 成本 | 破坏面 | 验证 |
|---|---|---|---|---|---|
| 1 | 8.2 删 api 里的死 `init` | 消除"改了一处不生效"陷阱；省 4 个指针 | 4 处定义 + 赋值行 | 无（唯一调用点是 driver->init） | `build` / `verify --artifacts` / `bsp` |
| 2 | 8.3 init 返回码 + ready 状态 | 初始化失败可见、消费方可拦 | ~60 行 + 1 判据 | 6 个 driver + 3 个 fml 点 | 新增 `bsp` 破坏段 |
| 3 | 8.4 清 compat 查找 | 去掉面子 API | 生成器 + 7 份 YAML + 1 判据 | 生成格式变化 | `device-dts` 组 |

顺序理由：8.2 是**零风险纯删除**，任何时刻可做；8.3 与 8.4 都要动生成/结构，做完 8.2 再动它们，寄存器与产物判据的基线更干净。

### 8.6 「明确不追」补一条

| 能力 | 为什么不追 |
|---|---|
| 驱动 api 带 `const struct device *dev` 首参（Zephyr 的多实例前提） | 本工程 6 个设备都是单实例（`bsl_device_api.h` 里四类 api 的签名都不带 dev）。多实例（例如两组 LED）需要"每实例一份私有数据 + api 只存函数指针"的拆分，属真正的重构；当前没有第二个同类实例，做了只会增加间接层 |
