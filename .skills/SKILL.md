---
name: test-stm32f429
description: >-
  STM32F429IGT6 分层固件工程（Cortex-M4、Kconfig/Makefile 生成链、工程内工具链）。
  修改 apl/fml/bsl/hal/osl、build/create-project.py、构建/链接行为或目录结构时使用；
  任何实现变更须同步更新 .skills 规格与 doc/，并在 update/verify 自检通过后才算完成。
---

# 本工程 Agent Skill

## 必读顺序

1. [工程规格.md](./工程规格.md) — 目录、构建、链接、Kconfig、脚本行为的**现行规格**（与代码一致）
2. [文档维护.md](./文档维护.md) — 改代码后必须更新哪些文档
3. [设计手稿.md](./设计手稿.md) — 搭建与设计 checklist（含工程框架图；细节冲突时以工程规格为准）

用户可见说明在仓库 **`doc/`**（当前为 `doc/design/开发环境配置.md`，索引见 `doc/design/README.md`）。

上文的 `python3` 在 Windows 本机请改用 `python`；`make` 不在 PATH，`create-project.py build` 内部会用 `tool/win_x86_64/bin/make.exe` 并设置 `SHELL`/`TMP`。

## 快速命令

```text
python3 build/create-project.py --help
python3 build/install-toolchain.py --help
python3 build/create-project.py update    # 刷新生成物 + 自动 verify
python3 build/create-project.py verify    # 仅自检（改 .skills 后也可单独跑）
python3 build/create-project.py build
python3 build/install-toolchain.py
```

## 更新后自检（必做）

凡改动 **源码树、层目录、链接顺序、`build/create-project.py` / `build/install-toolchain.py` / `build/set-buildenv.py`、CMSIS 启动约定**，Agent 须按顺序完成，**不得跳过 verify**：

1. **同步规格文档**：按 [文档维护.md](./文档维护.md) 更新 `.skills`（必要时 `doc/`）。
2. **刷新生成物**：`python3 build/create-project.py update`（末尾会自动执行 `verify`）。
3. **处理失败项**：若 verify 报错，根据终端列出的项修正 **代码 / 生成逻辑 / `.skills`**，再跑 `update` 或 `verify`，直到输出「自检通过」。
4. **编译并校验产物**：`python3 build/create-project.py build`。链接后会自动断言：镜像里确有强 `main` 与 `vTaskStartScheduler`、`.text` ≥ 2048 字节、`text + data` 不超 `ROM`、`data + bss` 不超 `RAM`（容量实时取自链接脚本 `MEMORY`）；此外还断言堆/锁/写符号（`_sbrk`、`__malloc_lock`、`_write`）、**panic 记录的 `.noinit` 落位**（`g_panic_record` 必须落在 `[_snoinit, _enoinit)` 内）、四个硬件异常入口，以及 BSP 接入（`board_key_init` / `board_key_pressed` / `board_beep_init` / `board_beep_set` 必须出现在镜像里）。断言失败说明应用层没进镜像或资源越界，先修链接顺序 / 弱符号 / 内存布局，不要只看 make 退出码。

仅改 `.skills` 文案、未改 `create-project.py` 与目录时，可只跑 `python3 build/create-project.py verify` 确认与仓库仍一致。

### verify 检查什么

| 类别 | 内容 |
|------|------|
| 生成物 | 根与各层 `Makefile`、`toolchain.make`、`proj/project_defconfig`、解析后的 `out/.config`；根链接行含各 `lib*.a`（`apl → fml → bsl → osl → hal`，`--start-group` 包裹） |
| 产物（仅 `build` / `verify --artifacts`） | `arm-none-eabi-nm` 中 `main`、`vTaskStartScheduler` 为强符号 `T`；`size` 的 `.text` ≥ 2048 字节；`text + data` ≤ `ROM` 容量、`data + bss` ≤ `RAM` 容量（从链接脚本 `MEMORY` 解析） |
| 工程元数据 | `project-info.json` 的 `project_name` 与根目录名、`CONFIG_PROJECT_NAME` 一致 |
| 中间产物 | `out/object/**/*.d` 里记录的依赖路径必须仍然存在；工程被整目录搬动后这些绝对路径会失效，make 将漏编改动过的头文件（实测 77 个 `.d` 指向另一台机器的 `/mnt/d/...`，导致改了 `FreeRTOSConfig.h` 而 `tasks.o` 三天没重编） |
| HAL 模块开关 | `hal_conf.h` 里每个启用的 `HAL_*_MODULE_ENABLED` 必须同时具备：同文件底部的 `#ifdef … / #include "stm32f4xx_hal_x.h"` 块、`hal/periph/src/stm32f4xx_hal_x.c`、以及已生成的 `stm32f4xx_hal_x.o`；底部出现"既没启用也没注释"的守卫即判为开关名拼错 |
| 堆边界 | 链接脚本必须输出 `__heap_start__` / `__heap_end__`；`toolchain.make` 的 LDFLAGS 必须含 `-Wl,-u,_sbrk`、`-Wl,-u,__malloc_lock` 与 `-Wl,-u,_write`（否则 `-lnosys` 的空桩和 `libc_nano.a(mlock.o)` 的空锁抢先，`malloc` 静默失败且不防并发；`_write` 无调用方，不加 `-u` 会被 `--gc-sections` 连同 `g_last_write` 一起丢掉）；`libosl.a` 与产物里的 `_sbrk`、`__malloc_lock`、`_write` 必须是强符号 `T`；map 里出现 `libc_a-mlock.o` 即判失败 |
| panic 现场跨复位 | 从 `nm` 同时取 `_snoinit` / `_enoinit` / `g_panic_record`，要求 `_snoinit <= g_panic_record < _enoinit`，缺一符号即失败。`Reset_Handler` 只清 `_sbss.._ebss`，`.noinit` 必须落在它之外，否则复位后现场静默归零（把 `section(".noinit")` 属性删掉时构建照样成功，只有这条能拦住） |
| newlib 分配器 | 产物里出现 `_malloc_r` / `_calloc_r` / `_realloc_r` / `_memalign_r`（按需提取，出现即有人真在用 newlib 堆）时，`_Min_Heap_Size` 必须 ≥ 2048 B；产物 `__heap_end__ - __heap_start__` 必须等于链接脚本声明值。`CFLAGS` / `CXXFLAGS` 必须带 `-include …/osl/inc/osl_alloc_guard.h`，该头把 `malloc` 一族编译期 poison 掉 |
| 元数据容量 | `proj/project-info.json` 的 `flash_kb` / `sram_kb` / `ccm_kb` 必须等于链接脚本 `MEMORY` 的 `ROM` / `RAM` / `CCMRAM`（历史上 json 写 2048 KB 而 `ROM` 只有 1024 KB，构建完全不会报错） |
| 硬件异常与未实现中断 | `osl/src/freertos_port_st.c` 必须定义 `HardFault_Handler` / `MemManage_Handler` / `BusFault_Handler` / `UsageFault_Handler`，以及 `UnhandledIRQ_Handler`；`startup_stm32f429xx.s` 必须保留 `.thumb_set …,Default_Handler` 弱别名，且 `Default_Handler` 必须 `b UnhandledIRQ_Handler`——不得再出现 `b Infinite_Loop`（检测先剥掉 `/* */` 与 `@` 注释，注释里的历史说明不算）；产物里四个入口与 `UnhandledIRQ_Handler` 必须是强符号 `T`、异常入口地址不等于 `Default_Handler`，并且 `objdump` 看 `Default_Handler` 第一条指令确实跳到 `UnhandledIRQ_Handler`。缺一条就意味着 90 个未实现中断和硬故障一样静默停机 |
| 编译警告门禁 | `toolchain.make` 的 `CFLAGS` / `CXXFLAGS` 必须同时含 `-Wall`、`-Wextra`、`-Werror`、`-fstack-usage`（由 `write_toolchain()` 写入，`verify` 直接断言这 4 个字符串还在）。实测三档全开 0 条真实告警，不需要 `-Wno-*` 白名单；`.su` 与 `.o` 同目录，用来查单个函数的帧大小 |
| 看门狗与复位原因 | `osl/inc/osl_boot.h` + `osl/src/osl_boot.c` 必须提供 `osl_reset_cause_capture` / `osl_watchdog_start` / `osl_watchdog_feed` / `osl_watchdog_relax` / `vApplicationTickHook`；`FreeRTOSConfig.h` 的 `configUSE_TICK_HOOK` 必须为 1；`HAL_InitTick()` 第一条语句必须是 `osl_reset_cause_capture()`；`osl_panic_report()` 的第一条 `bl` 必须是 `osl_watchdog_relax`。反汇编核对：`osl_watchdog_start` 为 `0x5555→KR`、`PR=2`、`RLR=2000`、`0xaaaa→KR`、`0xcccc→KR` 且基址 `0x40003000`，**每步之间必须有 `bl iwdg_wait_sync`**，而 `iwdg_wait_sync` 自身必须有 `ldr [0x4000300c]`(SR) + `tst #3`(PVU\|RVU) + 回跳分支（旧实现是 5 条连续 store、一次 SR 都没读，RLR 可能没生效就重载）；`osl_reset_cause_capture` 出现 `tst` 位 27/25/26/28/29/30 与 `orr #0x1000000`（RMVF） |
| 文档路径 | `.skills/` 与 `doc/` 下 Markdown 中反引号包住的**仓库相对路径**（`doc/…`、`.skills/…`、`build/…`、`proj/…`、`hal/…`）必须真实存在；裸文件名（`FreeRTOSConfig.h`、`Depend.make` 这类正文提及）与 `tool/`、`out/`、`examples/`、`doc/reference/` 前缀不作断言 |
| BSP 接入 | 产物里必须能找到 `board_key_init`、`board_key_pressed`、`board_beep_init`、`board_beep_set`。bsl 的按键/蜂鸣器曾经零引用、被 `-Wl,--gc-sections` 整段丢弃：源码、头文件、`libbsl.a` 全在，elf 里却没有符号，板上按 KEY 毫无反应 |
| 目录 | 无排除目录内误生成的 `Makefile`/`Kconfig`；无未在 `LAYER_DIRS` 声明的顶层目录。**空目录不判**——git 不跟踪空目录，一个空壳不该把门禁卡死；`release/` 这类打包输出已在 `EXCLUDE_DIRS` 里 |
| 文档 | [工程规格.md](./工程规格.md) 含当前层名、扫描排除项、链接顺序；[SKILL.md](./SKILL.md) 含 `verify` 与各层名；[设计手稿.md](./设计手稿.md) 中 lib 链接链与现网一致 |

实现细节与链接顺序以 [工程规格.md](./工程规格.md) 及 `build/create-project.py` 中 `STATIC_LIB_DIRS` / `NORMAL_LINK_LIBS` 为准。

**改过门禁就跑门禁自测**：`python build/gate-selftest.py` 会逐组破坏被测条件（flags 依赖 / `-Wl,-u,_sbrk` / `_Min_Heap_Size` 与产物边界 / BSP 接入 / `Default_Handler` 跳转），要求 `verify` 报错后再还原。断言在代码正确时永远是绿的，绿一次证明不了它会拦人。全量约 160 s，`--only <组>` 可单跑。

## 故障排查

| 现象 | 原因 | 处理 |
|------|------|------|
| 只有本次改过的文件报 `Error 49`，其余显示 `Nothing to be done` | 编译规则末尾的时间戳回写在 MSYS `sh` 里调用了失效的 `python3` | 确认 `toolchain.make` 有 `PYTHON := python`（Windows）且各层规则用 `$(PYTHON)`，再 `update` |
| `make` 退出 0，但 `.bin` 只有几百字节、`nm` 里 `main` 是 `W` | 静态库顺序反了，或库里有 `main` 的弱定义抢占了启动对象的引用 | 顺序改 `apl → fml → bsl → osl → hal`；删除 `system_stm32f4xx.c` 中的弱 `main`；靠 `build` 的产物断言把关 |
| `undefined reference to vApplicationStackOverflowHook` | `FreeRTOSConfig.h` 开了 `configCHECK_FOR_STACK_OVERFLOW = 2` 但没提供钩子 | 由 `osl/src/freertos_port_st.c` 提供该钩子；**不要把它挪到别的层**，`libhal.a` 的弱 `HAL_GetTick` 正是靠这个钩子把 `freertos_port_st.o` 拉进场后才被强定义压过的 |
| 板子上电后“什么都不动”、没有日志 | 栈溢出 / 堆耗尽 / `configASSERT` 失败 / **硬件异常**都曾表现为静默停机 | 看三灯故障码（码表只有一份，见 `osl/inc/osl_panic.h`）：`1` 调度器起不来、`2` 任务创建失败或堆不足、`3` 断言失败、`4` 时钟配置失败、`5` newlib 堆耗尽、`6` 硬件异常、`7` 任务栈溢出；细节在 `g_panic_record`（含 `pcTaskName`、`assert_file`、`assert_line`；硬件异常另含 `fault_kind`、`fault_frame_type`、`fault_cfsr`、`fault_pc`、`fault_lr`，未实现中断另含 `unhandled_irq`；另有 `seq` = 第几次 panic、`reset_cause` = 进 panic 前那次复位原因），用调试器直接读 `g_panic_record` / `g_last_write`。**`g_panic_record` 在 `.noinit` 段、复位不清**，所以 IWDG 把板子重启后上一次的现场还在（首次上电内容随机，判据是它自己的 `magic`） |
| 板子莫名其妙自己重启，且不是上电 | 看门狗兜住了：某处卡死超过 1 s 没喂狗 | 调试器读 `g_reset_cause`：`3` = IWDG（有人卡死过）、`4` = 软件复位、`5` = NRST 按键、`6` = 欠压、`7` = 上电；`g_reset_csr` 是 `RCC->CSR` 原始值。若重启后立刻又是三灯 panic，说明 panic 本身在重启循环（看下一条） |
| 三灯播报完仍停机，但约 32.7 s 后板子自己重启并重复播报 | 这是设计行为，不是故障：`osl_panic_report()` 先把 IWDG 超时拉到最大（PR=/256 + RLR=4095 = 32.7 s）再播报，F4 的 IWDG 一旦启动无法关闭。重启后现场仍在 `.noinit` 的 `g_panic_record` 里：`seq` 记着第几次、`reset_cause` 记着上次是 IWDG 复位（3） | 想永久停机只能不启动看门狗（去掉 `vApplicationTickHook` 里的 `osl_watchdog_start()`），代价是失去“卡死自恢复”；调试时可在 32.7 s 内连上读 `g_panic_record` |
| 三灯播报 `6`，但没跑过任何会出故障的代码 | 某个**未实现的中断**被触发了：`startup` 的 90 个 `.thumb_set …IRQHandler,Default_Handler` 全转发到 `UnhandledIRQ_Handler`，与硬故障同码 6 | 读 `g_panic_record.unhandled_irq`（`SCB->ICSR.VECTACTIVE` = 16 + 中断号）：对照 `startup_stm32f429xx.s` 向量表定位是哪个外设；确认该中断是否该开、或实现对应 `Xxx_IRQHandler` |
| 编译因为一条 `warning:` 直接失败 | `-Wall -Wextra -Werror` 已开（`toolchain.make` 由生成器写死），告警即错误 | 修代码本身；不要加 `-Wno-*` 或删掉 `-Werror`——`verify` 会断言这 4 个 flag 仍在 `toolchain.make` 里，删了会被拦下 |
| 初始化阶段 `HAL_Delay` 卡死 | 旧实现里 `HAL_GetTick` 只在调度器运行时返回 tick，启动阶段恒返回 0，`while((0-0) < N)` 死等 | 现在 HAL 时基是 DWT 周期计数器（`freertos_port_st.c`），调度器前后都可用；若被改回 SysTick 方案必须重读「工程规格」的时基约定 |
| 链接报 `defined in discarded section '.ccmram'` | `.ccmram` 段被 `/DISCARD/` 丢弃（CCMRAM 的拷贝/清零循环从未实现） | 要么把变量移出 `.ccmram`，要么按「工程规格」补 startup 的拷贝与清零循环后移除 `/DISCARD/` 条目 |
| 改了头文件却什么都没重编，`make` 一直 `Nothing to be done` | `out/object/**/*.d` 里的绝对路径已失效（工程被搬动过），make 看不见头文件变化 | `python3 build/create-project.py verify` 会报出失效的 `.d` 数量；`clean` 后重新 `build` |
| 三灯闪 4 次后常亮停机 | `SystemClock_Config` 失败：HSE 没起振或 PLL 没锁，芯片仍停在 HSI 16 MHz | 查 25 MHz 晶振与负载电容（核心板 Y1、C27/C28 22 pF），示波器看 `OSC_IN`；确认 `HAL` 的 `HSE_VALUE` 与实际晶振一致 |
| 板子能跑但明显慢约 10.5 倍 | 时钟没切到 168 MHz（`SystemCoreClock` 仍是 16 MHz），FreeRTOS 与 DWT 时基全按 16 MHz 算 | 反汇编确认 `main` 调用了 `SystemClock_Config` 且返回 0；同时检查 PWR Scale1 与 `FLASH_LATENCY_5` |
| 按键读数总是反的 | `board_key.c` 的 `pressed_level` 与原理图极性不一致（两块板级按键都是按下接 3V3 = 高电平） | 对照「工程规格」的「板级事实」表；板上已有 4.7K 外部下拉，`GPIO_NOPULL` 是正确用法 |
| 板上有按键/蜂鸣器，按下去毫无反应也不报错 | `bsl` 的实现零引用，被 `-Wl,--gc-sections` 整段丢弃（源码和 `libbsl.a` 都在，elf 里却没有符号） | `nm` 查 `board_key_init` / `board_beep_set`；`verify` 的「BSP 接入」断言会直接报出来，把 `fml_button_start()` 接进 `main()` 即可 |
| 新用某个外设，链接报 `undefined reference to HAL_xxx` | 该驱动已被裁掉：`hal_conf.h` 的开关注释着，`project_defconfig` 的 `USER_CUSTOM` 区里也写死 `=n` | 两处同时放开（放开 `#define …_MODULE_ENABLED`、删掉对应的 `CONFIG_HAL_PERIPH_SRC_…=n`），再 `update`；只改一处的表现是"编了但头文件不引入"（隐式声明）或"头文件引入了却没有 `.o`"，`verify` 都会拦 |
| 三灯播报 `5` 后停机，或某处 `malloc` 之后莫名崩 | newlib 的 `_sbrk` 越界：单次 `malloc` 超过 512 B（`_Min_Heap_Size`），或链路退回到 `-lnosys` 的空桩 | 中等/长期分配改 `pvPortMalloc`；确认 `toolchain.make` 的 LDFLAGS 含 `-Wl,-u,_sbrk`（详见「工程规格」的「两套堆的边界」） |
| 编译报 `implicit declaration of function 'HAL_XXX_...'` | `hal_conf.h` 底部少了该模块的 `#ifdef … / #include …` 块（模块开关还开着，但顶层头没引入） | `verify` 会直接报"缺 `#ifdef … / #include` 块"并列出模块名；按「工程规格」HAL 配置一节补齐四处对应关系 |
| 编译报 `attempt to use poisoned "malloc"` | `osl_alloc_guard.h` 用 `#pragma GCC poison` 禁掉了 newlib 的分配入口（newlib 堆只有 512 B，而它到 `_estack` 之间是约 159 KB 无保护真空，越界既不撞栈也不报错） | 改成 `pvPortMalloc` / `vPortFree`；确实需要 newlib 堆时不要拆守卫，而是把 `_Min_Heap_Size` 提到 2048 B 以上并重算 RAM 预算 |
| 产物断言报"链接了 newlib 分配器（`_malloc_r`），但 newlib 堆只有 512 字节" | 有人用 `_malloc_r` 等内部入口绕过了 poison，把 newlib 分配器拉进镜像 | 同上：改用 `pvPortMalloc`，或按上面的门禁把堆提到下限以上 |
| 同一提交在两台机器/两个目录编出的镜像大小不同、`.bin` 哈希不同 | 路径被编进镜像：`configASSERT` 把 `__FILE__` 传给 `vAssertFailed`，`-g3` 又把源码绝对路径写进调试段 | `objdump -s -j .rodata` 看有没有 `D:/.../osl/src/freertos/tasks.c`；`toolchain.make` 必须有 `-ffile-prefix-map=$(PROJECT_ROOT)=.`（详见「工程规格」的「可复现构建」）。注意跨 OS 工具链包自带库不同，字节一致只在同包内成立 |

## 硬规则

- **不要手改**自动生成的根/层 `Makefile`、`Kconfig`、根目录 `toolchain.make`（除非用户明确要求且随后会再跑 `update`）。
- 增删 `apl` / `fml` / `bsl` / `hal` / `osl` 下参与编译的源文件或子目录后，执行 **`python3 build/create-project.py update`**（含自检），再视需要 `build`。
- `build/` 存放生成器和工具链辅助脚本，不参与 Kconfig/Makefile 扫描；解析后的 `.config` 位于 `out/`。
- **实现类修改未完成的标准**：`.skills` / `doc/` 与代码不一致，或 **`verify` 未通过**。
