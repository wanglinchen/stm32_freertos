# osl 层 FreeRTOS 相对官方的改动清单

> 生成日期：2026-11-27（同日补 `LICENSE.md`、清掉 timers 假开关，见 §9）
> 比对对象：`osl/inc/freertos/` + `osl/src/freertos/` 与官方 **FreeRTOS-Kernel V11.1.0**
> 基线来源：GitHub `FreeRTOS/FreeRTOS-Kernel` tag `V11.1.0` 的 archive zip（3,537,691 B）
> 基线指纹：zip SHA-256 `D5875ACAD2479817B35B2031DC1C7C22D100F45C7F3C9F6039CB217437A298A2`
> 版本自证：`osl/inc/freertos/task.h:56` 为 `#define tskKERNEL_VERSION_NUMBER "V11.1.0"`

## 0. 结论

**内核源码零改动。** 本地 32 个上游文件与官方同名文件逐字节一致（按 LF 归一后逐行 difflib：新增 0 行、删除 0 行、替换 0 行）。

本工程对 FreeRTOS 的全部“改动”集中在四个层面，都不碰内核 `.c/.h` 的正文：

1. **收录范围**：只取 V11.1.0 的 21 个头文件、`CMakeLists.txt` 与 `stdint.readme`、6 个核心 `.c`、`heap_4.c`、`port.c`/`portmacro.h`（合计 32 个上游文件），其余（`croutine.c`、其他移植层、`heap_1/2/3/5`、文档类文件）不收；
2. **目录重排**：上游 `include/` 平铺进 `osl/inc/freertos/`，上游根目录 `.c` 进 `osl/src/freertos/`，`port.c` 改名 `port_m4.c`；
3. **构建胶水**：`Kconfig` + `Makefile` 由 `project/generate.py` 生成（带“自动生成，勿手改”横幅），另有一处“官方源因 `configUSE_TIMERS=0` 不进编译”的条件裁剪；
4. **配置与集成**：`osl/inc/FreeRTOSConfig.h`（工程自有，不是官方文件）决定内核行为，`osl/src` 侧实现 4 个 hook，并用 `-include osl_alloc_guard.h` 从编译期封掉 newlib 的 `malloc/free`。

## 1. 文件映射（32 个文件：逐字节一致）

### 1.1 头文件（22 个）

| 本地路径 | 官方路径 | 行数 | 结论 |
|---|---|---|---|
| `osl/inc/freertos/FreeRTOS.h` | `include/FreeRTOS.h` | 3335 | 一致 |
| `osl/inc/freertos/task.h` | `include/task.h` | 3775 | 一致 |
| `osl/inc/freertos/queue.h` | `include/queue.h` | 1811 | 一致 |
| `osl/inc/freertos/semphr.h` | `include/semphr.h` | 1216 | 一致 |
| `osl/inc/freertos/list.h` | `include/list.h` | 512 | 一致 |
| `osl/inc/freertos/timers.h` | `include/timers.h` | 1432 | 一致 |
| `osl/inc/freertos/event_groups.h` | `include/event_groups.h` | 858 | 一致 |
| `osl/inc/freertos/stream_buffer.h` | `include/stream_buffer.h` | 1281 | 一致 |
| `osl/inc/freertos/message_buffer.h` | `include/message_buffer.h` | 968 | 一致 |
| `osl/inc/freertos/croutine.h` | `include/croutine.h` | 763 | 一致 |
| `osl/inc/freertos/projdefs.h` | `include/projdefs.h` | 139 | 一致 |
| `osl/inc/freertos/portable.h` | `include/portable.h` | 282 | 一致 |
| `osl/inc/freertos/atomic.h` | `include/atomic.h` | 428 | 一致 |
| `osl/inc/freertos/stack_macros.h` | `include/stack_macros.h` | 142 | 一致 |
| `osl/inc/freertos/StackMacros.h` | `include/StackMacros.h` | 35 | 一致 |
| `osl/inc/freertos/deprecated_definitions.h` | `include/deprecated_definitions.h` | 282 | 一致 |
| `osl/inc/freertos/mpu_prototypes.h` | `include/mpu_prototypes.h` | 390 | 一致 |
| `osl/inc/freertos/mpu_wrappers.h` | `include/mpu_wrappers.h` | 277 | 一致 |
| `osl/inc/freertos/mpu_syscall_numbers.h` | `include/mpu_syscall_numbers.h` | 106 | 一致 |
| `osl/inc/freertos/newlib-freertos.h` | `include/newlib-freertos.h` | 63 | 一致 |
| `osl/inc/freertos/picolibc-freertos.h` | `include/picolibc-freertos.h` | 92 | 一致 |
| `osl/inc/freertos/portable/GCC/ARM_CM4F/portmacro.h` | `portable/GCC/ARM_CM4F/portmacro.h` | 267 | 一致 |

附带两个非源码文件：

| 本地 | 官方 | 说明 |
|---|---|---|
| `osl/inc/freertos/CMakeLists.txt` | `include/CMakeLists.txt` | 16 行，原样保留；本工程走 Makefile，CMake 不参与构建 |
| `osl/inc/freertos/stdint.readme` | `include/stdint.readme` | 59 行，官方说明文件，原样保留 |

### 1.2 源文件（8 个）

| 本地路径 | 官方路径 | 行数 | 字节 | 结论 |
|---|---|---|---|---|
| `osl/src/freertos/tasks.c` | `tasks.c` | 8697 | 352352 | 一致 |
| `osl/src/freertos/queue.c` | `queue.c` | 3365 | 128165 | 一致 |
| `osl/src/freertos/stream_buffer.c` | `stream_buffer.c` | 1719 | 75282 | 一致 |
| `osl/src/freertos/timers.c` | `timers.c` | 1341 | 56556 | 一致（但不进编译，见 §3） |
| `osl/src/freertos/event_groups.c` | `event_groups.c` | 885 | 35927 | 一致 |
| `osl/src/freertos/list.c` | `list.c` | 247 | 10049 | 一致 |
| `osl/src/freertos/heap_4.c` | `portable/MemMang/heap_4.c` | 627 | 24078 | 一致 |
| `osl/src/freertos/port_m4.c` | `portable/GCC/ARM_CM4F/port.c` | 910 | 41903 | **一致（仅改名）** |

## 2. 没有收进来的官方文件（裁剪清单）

| 未收录 | 体积/规模 | 不收录的理由 |
|---|---|---|
| `croutine.c` | 17,389 B | co-routine 未启用（`configUSE_CO_ROUTINES` 走默认 0），且无任何 `croutine.h` 使用点 |
| `portable/` 除 `GCC/ARM_CM4F` 外的全部移植层 | 数十个架构/编译器目录 | 只用 Cortex-M4F + GCC |
| `portable/MemMang/heap_1.c` / `heap_2.c` / `heap_3.c` / `heap_5.c` | — | 固定用 `heap_4.c`（唯一堆） |
| 根目录 `CMakeLists.txt`、`History.txt`、`README.md`、`MISRA.md`、`sbom.spdx`、`manifest.yml`、`.gitattributes`、`.gitmodules` 等 | — | 非运行源码 |
| `examples/`（`template_configuration/FreeRTOSConfig.h`、`cmake_example`、`coverity`） | — | 仅作比对基线用，不随仓库分发 |
| `LICENSE.md` | 1,036 B | **已补齐**：原样复制到 `osl/inc/freertos/LICENSE.md`（SHA-256 与官方一致）。注意它只是本组件的第三方 MIT 声明，**不是本工程的许可**——工程根目录没有 LICENSE，README 也没有许可声明 |

## 3. 目录与命名改动（仅路径，不含内容）

1. `include/*.h` → `osl/inc/freertos/*.h`，平铺；唯独 `portable/GCC/ARM_CM4F/portmacro.h` 保留三级相对路径，因为 `Makefile` 的 `-I` 直接指向该目录（`osl/inc/freertos/portable/GCC/ARM_CM4F`），`portable.h` 按官方写法 `#include "portmacro.h"` 才能命中。
2. 上游根目录 6 个核心 `.c` → `osl/src/freertos/`，与层内其他源文件同级，由层 Makefile 统一编。
3. `portable/MemMang/heap_4.c` → `osl/src/freertos/heap_4.c`；`portable/GCC/ARM_CM4F/port.c` → `osl/src/freertos/port_m4.c`。改名意图是让“M4 移植”在文件名上自明，同时避免同一目录同时存在多个 `port.c` 时靠目录名区分。

## 4. 本地新增的构建胶水（4 个文件，全部为生成物）

| 文件 | 字节 | 生成者 | 作用 |
|---|---|---|---|
| `osl/inc/freertos/Kconfig` | 233 | `project/generate.py` | 给 `osl/inc` 下这一级挂菜单项 |
| `osl/inc/freertos/Makefile` | 1094 | 同上 | 只做 `-I` 与递归 `portable/`，本身无编译单元 |
| `osl/inc/freertos/portable/{Kconfig,Makefile}`、`portable/GCC/{Kconfig,Makefile}` | 240/1110、127/1025 | 同上 | 逐级下发到 ARM_CM4F |
| `osl/src/freertos/Kconfig` | 593 | 同上 | 8 个源文件开关（`OSL_SRC_FREERTOS_*`），默认全 `y` |
| `osl/src/freertos/Makefile` | 5312 | 同上 | 每源一条编译规则 + `-MMD` 依赖，条件为对应 `CONFIG_*` |

四个 Makefile/Kconfig 的文件头均为 `# 自动生成，勿手改。刷新：python3 build/build.py update`，属工程生成物，不是对内核的改写。

## 5. 唯一一处“官方文件不进编译”：`timers.c`

- 机制：`project/generate.py:187` 的 `FREERTOS_CONFIG_GATED` 表把 `timers.c → configUSE_TIMERS`、`event_groups.c → configUSE_EVENT_GROUPS`、`stream_buffer.c → configUSE_STREAM_BUFFERS` 三对映射登记起来；`config_gated_sources()`（`generate.py:696`）读 `osl/inc/FreeRTOSConfig.h` 的宏值，为 0 就把该源从 Makefile 生成列表里剔除。
- 本工程 `configUSE_TIMERS = 0`（`osl/inc/FreeRTOSConfig.h:39`），于是 `timers.c` 既不编译也不链接。
- 证据链：
  - `out/userdebug/objects/osl/src/freertos/` 只有 7 个 `.o`（`event_groups/heap_4/list/port_m4/queue/stream_buffer/tasks`），**无 `timers.o`**；
  - `arm-none-eabi-nm out/userdebug/objects/target/stm32-freertos.elf` 里 `xTimer*` 零符号，`vTaskStartScheduler`、`uxTaskGetStackHighWaterMark` 在场。
- `event_groups.c` / `stream_buffer.c` 走同一张表，但两个开关为 1，照常编译。

> 这解释了“`timers.c` 在仓库里但镜像里没有定时器”的现象：控制点不在 Kconfig，而在 `FreeRTOSConfig.h` 的 `#define`。

**已修（2026-11-27）**：原先 `osl/src/freertos/Kconfig` 仍生成 `config OSL_SRC_FREERTOS_TIMERS`（默认 `y`），`out/<档位>/.config` 也写着 `y`，却没有任何 Makefile 规则消费它 —— 名存实亡。修法不是在生成物上手工删（一跑 `update` 就回来），而是让 `project/generate.py` 的 `source_symbols()` 也走 `FREERTOS_CONFIG_GATED` 同一张表：被 `configUSE_*` 裁掉的官方源不再产生 `CONFIG_*` 符号，Makefile 规则与 Kconfig 开关从此只有一个事实来源。复跑 `update` 后 `osl/src/freertos/Kconfig`、`project/config/stm32f429_defconfig_common`、`out/<档位>/.config` 里 `FREERTOS_TIMERS` 命中数均为 **0**，`timers.o` 仍不产生。

## 6. `FreeRTOSConfig.h`：真正改变内核行为的配置

该文件是工程自有文件（官方模板为 `examples/template_configuration/FreeRTOSConfig.h`，95 个宏；本地显式定义 43 个，其余走内核默认值）。

### 6.1 与官方模板取值不同（13 项）

| 宏 | 本地 | 官方模板 | 影响 |
|---|---|---|---|
| `configCPU_CLOCK_HZ` | `SystemCoreClock` | `20000000UL` | 与 HAL 时钟树同源，改主频不用改两处 |
| `configTICK_RATE_HZ` | 1000 | 100 | 1 ms 节拍；`vTaskDelay` 的毫秒直接对应 tick |
| `configMAX_PRIORITIES` | 2 | 5 | `pxReadyTasksLists[]` 只 2 项，比模板 5 项少 60 B（3×`List_t` 20 B）；实际只用 idle=0、应用=1 两档 |
| `configTOTAL_HEAP_SIZE` | 16 KB | 4 KB | 唯一堆（heap_4）；32 KB 是官方常用默认，这里收到 16 KB 仍留约 6 倍余量 |
| `configUSE_TIMERS` | 0 | 1 | 不建 timer task / 定时器队列，`timers.c` 整单元不进编译 |
| `configUSE_TIME_SLICING` | 1 | 0 | 同优先级轮转（本工程应用任务同档时才生效） |
| `configUSE_PORT_OPTIMISED_TASK_SELECTION` | 1 | 0 | 用 `CLZ` 选下一个任务，O(1) |
| `configUSE_TICK_HOOK` | 1 | 0 | tick hook 里喂 IWDG + 栈哨兵检查 |
| `configUSE_MALLOC_FAILED_HOOK` | 1 | 0 | 堆耗尽可观测（panic 码 2） |
| `INCLUDE_uxTaskGetStackHighWaterMark` | 1 | 0 | 允许编入栈水位查询，fml 周期采样 |
| `configTICK_TYPE_WIDTH_IN_BITS` | 32 位 | 64 位 | 32 位 tick 在 1 kHz 下约 49.7 天回绕，本工程可接受 |
| `configKERNEL_INTERRUPT_PRIORITY` | 由 library 15 左移算出 | 0（模板占位） | 与 CMSIS 优先级口径统一 |
| `configMAX_SYSCALL_INTERRUPT_PRIORITY` | 由 library 5 左移算出 | 0（模板占位） | 同上 |

### 6.2 本地显式设置、模板同值（节选，均未改动语义）

`configMAX_TASK_NAME_LEN=16`、`configMINIMAL_STACK_SIZE=128`、`configCHECK_FOR_STACK_OVERFLOW=2`、`configSUPPORT_STATIC_ALLOCATION=1`、`configSUPPORT_DYNAMIC_ALLOCATION=1`、`configUSE_PREEMPTION=1`、`configIDLE_SHOULD_YIELD=1`、`configQUEUE_REGISTRY_SIZE=0`、`configENABLE_BACKWARD_COMPATIBILITY=0`、`configUSE_MUTEXES/RECURSIVE_MUTEXES/COUNTING_SEMAPHORES/TASK_NOTIFICATIONS=1`、`configUSE_EVENT_GROUPS=1`、`configUSE_STREAM_BUFFERS=1`（后两项即内核默认值 1，显式写出来只为口径可见）。

### 6.3 模板里没有的三处（真正的行为分叉）

| 项 | 位置 | 含义 |
|---|---|---|
| `configASSERT(x)` → `vAssertFailed(__FILE__, __LINE__)` | `FreeRTOSConfig.h:112` | 断言失败不再原地死循环：记录文件/行号进 `.noinit` 并用 LED 播报（`osl/src/osl_panic.c:738`） |
| `vPortSVCHandler/xPortPendSVHandler/xPortSysTickHandler` 映射到 `SVC_Handler/PendSV_Handler/SysTick_Handler` | `FreeRTOSConfig.h:121-123` | 让 FreeRTOS 的三个异常入口直接占用启动文件的向量槽 |
| `configCHECK_HANDLER_INSTALLATION=1` | `FreeRTOSConfig.h:119` | 启动调度器时自检三个 handler 是否装到向量表，防止“换了启动文件后静默跑不起来” |

## 7. 内核之外的集成点（会改变内核运行时行为）

| 集成点 | 位置 | 作用 |
|---|---|---|
| `vApplicationGetIdleTaskMemory` | `osl/src/osl_thread.c:40-48` | idle 任务 TCB/栈取自静态池（`s_idle_tcb`、`s_idle_stack[configMINIMAL_STACK_SIZE]`） |
| `osl_thread_create` | `osl/src/osl_thread.c:50` | 只走 `xTaskCreateStatic`；静态池 `OSL_THREAD_MAX=2`、每任务 256 词，超限直接拒绝 |
| `vApplicationTickHook` | `osl/src/osl_boot.c:270` | 每 tick 推进 HAL 毫秒时基、跑栈哨兵检查、采样堆历史最低；第一次进来才启动 IWDG，之后每 tick 喂狗（tick 停 → 1 s 复位） |
| `vApplicationStackOverflowHook` | `osl/src/osl_panic.c:678` | panic 码 7，记录任务身份 |
| `vApplicationMallocFailedHook` | `osl/src/osl_panic.c:687` | panic 码 2，把堆现场写 `.noinit` |
| `vAssertFailed` | `osl/src/osl_panic.c:738` | 断言证据落盘 + LED 播报 |
| `-include osl_alloc_guard.h` | `toolchain.make` 的 `CFLAGS/CXXFLAGS` | `#pragma GCC poison malloc calloc realloc free strdup`，编译期禁止绕过 `pvPortMalloc`（对内核单元同样生效，内核本身不用这些名字） |

任务创建点当前只有 `apl/src/app_sample.c:195` 一处；`configUSE_TIMERS=0` 也意味着不需要 `vApplicationGetTimerTaskMemory`。

## 8. 这样做的收益与升级流程

收益：**官方文件零改动**意味着升级是“替换文件 + 重新生成胶水”，不需要三方合并内核补丁。

建议流程（与现有构建链一致）：

1. 取新版 tag zip，替换 `osl/inc/freertos/**`、`osl/src/freertos/*.c`（保持文件名映射，`port.c → port_m4.c`）；
2. `python build/build.py update` —— 重新生成四类 Kconfig/Makefile，并按 `FreeRTOSConfig.h` 重算“哪些官方源不进编译”；
3. `python build/build.py build`（双档）+ `check`；
4. 若新版改了配置宏名（如 V11 起 `configTICK_TYPE_WIDTH_IN_BITS` 取代旧的 `configUSE_16_BIT_TICKS`），编译会立刻失败而不是静默改行为。

复现比对的命令（脚本放系统 temp，不落仓库）：

```powershell
$zip='%TEMP%\FreeRTOS-Kernel-11.1.0.zip'
Invoke-WebRequest 'https://github.com/FreeRTOS/FreeRTOS-Kernel/archive/refs/tags/V11.1.0.zip' -OutFile $zip
Expand-Archive $zip "$env:TEMP\fr"
# 之后对 §1 的每对路径做 LF 归一 + difflib.unified_diff，期望 0 行差异
```

## 9. 处理状态

1. **`LICENSE.md` —— 已处理（2026-11-27）**：官方 MIT 全文（1,036 B）原样复制到 `osl/inc/freertos/LICENSE.md`，SHA-256 与官方一致。
   范围要说清：**它只覆盖本目录下的 FreeRTOS 官方文件，不是本工程的许可**。该文本本身不带版权行（官方如此），权利人靠每个源文件头的 `Copyright (C) 2021 Amazon.com, Inc. or its affiliates` 体现（`tasks.c:3`、`queue.h:3`、`heap_4.c:3`、`port_m4.c:3`）。工程自有代码（`apl/`、`fml/`、`bsl/`、`hal/`、`osl/` 的 `osl_*.c`）没有版权头，仓库根目录也没有 LICENSE；将来若要对外发布整包，需要单独定一份工程许可，不能用这份 MIT 顶替（它覆盖不到自研代码）。
2. **`CONFIG_OSL_SRC_FREERTOS_TIMERS` 假开关 —— 已处理（2026-11-27）**：`project/generate.py` 的 `source_symbols()` 改为与 Makefile 共用 `config_gated_sources()`，被 `configUSE_TIMERS=0` 裁掉的 `timers.c` 不再产生 Kconfig 开关，`defconfig` 与 `.config` 同步消失；`update` exit=0，三处 `FREERTOS_TIMERS` 命中数为 0。
3. **`CMakeLists.txt` 冗余 —— 待定**：官方 `include/CMakeLists.txt` 原样保留但不参与本工程构建，容易让后来人误以为可以 `cmake` 构建。可删，或在文件头加一行“非本工程构建路径”；因属官方文件，未擅自改动。

（本清单不含对内核代码的任何修改；若后续要打补丁，请在本文件追加“补丁登记”一节，逐条记录文件、行号、原因与回退方式。）
