# 刷机验证中发现的两条 panic 现场缺陷（MSP 帧 / MMFAR 有效性）

- 时间：2026-11-16 21:30 左右
- 环境：Windows 本机，OpenOCD 0.12.0 (xPack) + ST-LINK V2J29S7，`VID:PID 0483:3748`，目标电压 3.20 V
- 固件：`out/userdebug/objects/target/stm32-freertos.elf`
- 触发前提：**本机没有串口**（`[System.IO.Ports.SerialPort]::GetPortNames()` 返回空、`Get-PnpDevice -Class Ports` 无设备），所以验证走 SWD 读内存，不读 UART

## 现象

刷机本身成功（`** Verified OK **`，`FLASH_EXIT=0`）。随后用调试器构造硬件 fault 来验证落盘链路，两次结果不一致：

| 构造 | `EXC_RETURN` | `fault_pc` | 结论 |
|------|--------------|-----------|------|
| 调度器起来后在**任务上下文**（idle 用 PSP）写 `pc=0xfffffffe` | `0xFFFFFFFD`（PSP） | `0xFFFFFFFE` ✓ | PSP 路径正确 |
| `reset halt` 后在**MSP 上下文**做同一件事 | `0xFFFFFFF9`（MSP） | `0x000DAC00`（无关内存）✗ | **MSP 路径整组读错** |

同时 record 里 `fault_mmfar = 0xE000ED34`、`fault_bfar = 0xE000ED38` —— 这两个值正好是 MMFAR / BFAR **寄存器自己的地址**。

## 命令与退出码

```text
python flash/flash.py flash                                   # FLASH_EXIT=0，** Verified OK **
openocd -c "init" -c "reset halt" -c "reg pc 0xfffffffe" \
        -c "resume" -c "sleep 600" -c "halt" \
        -c "mdw 0x20004e48 30" -c "reset run" -c "shutdown"    # 读到 fault_pc = 0x000DAC00
openocd -c "init" -c "halt" -c "mdw 0xE000ED28 6"             # CFSR/HFSR/DFSR/MMFAR/BFAR/AFSR
```

寄存器实测（fault 之后、halt 状态下）：`CFSR = 0x00000001`（IACCVIOL）、`HFSR = 0x40000000`（FORCED）、
`MMFAR = 0x20004DD0`、`BFAR = 0x20004DD0`。**与 record 里记的 `0xE000ED34 / 0xE000ED38` 完全不同。**

## 已排除

- **不是 ABI 传参错位**：`fault_kind`、`fault_cfsr`、`fault_hfsr` 三个字段与寄存器值一一对上（`kind=2`/`cfsr=1`/`hfsr=0`），
  说明 r0–r3 的映射是对的。
- **不是帧偏移（`frame_offset`）错**：PSP 那次 `fault_pc` 一字不差。
- **不是我读错列**：CFSR/HFSR 两个值一开始被我误判成"写反了"，重新按 `mdw` 的行列逐字对齐后，
  `w10@40 = fault_cfsr = 1`、`w11@44 = fault_hfsr = 0x40000000`，**代码是对的**，是解析看错了列，已排除。
- **不是 `_estack` / `_sdata` 判据的问题**：MSP 边界的 `fault_sp = 0x2002FFE0` 落在
  `_estack - 0x20` 上，判据放行，读帧那一步确实执行了 —— 读到的是错误**位置**的内容。

## 根因

### ① MSP 路径：栈指针取在被 prologue 压栈之后

`osl/src/osl_panic.c`（修复前行号）`sp = ((exc_return & 0x4U) != 0U) ? __get_PSP() : __get_MSP();`

`fault_report` 是普通 C 函数，进入时 prologue 先把自己要用的寄存器压到 **MSP** 上；此后 `__get_MSP()`
返回的是"压过之后"的值，而真正的异常帧在更高地址。**PSP 路径不受影响**——handler 模式永远用 MSP，
任务帧在 PSP，两者不重叠，所以 PSP 现场一直正确，这个错误只在 **中断 / 启动早期** 的 fault 上暴露。

差分即证据：同一份固件，PSP 上下文 `fault_pc=0xFFFFFFFE`，MSP 上下文 `fault_pc=0x000DAC00`。

### ② MMFAR / BFAR：不看 VALID 位

`g_panic_record.fault_mmfar = SCB->MMFAR;` —— `MMFSR.MMARVALID`(bit7) 为 0 时该寄存器内容**未定义**。
IACCVIOL / INVSTATE / UNDEFINSTR 不产生访问地址，此时抄下来的没有意义；F429 实测读到的是寄存器自身地址，
读的人会拿它去查 SCB。与本工程反复引用的「错误的地址比没有更糟」直接冲突。

## 修复

| 文件 | 位置 | 改动 |
|------|------|------|
| `osl/src/osl_panic.c` | `:477` / `:478` | 新增 `fault_entry_stub`（`naked`）：`mov r0, lr` / `mrs r1, msp` / `mrs r2, psp` / `b fault_report`，**在任何 push 之前**取齐 |
| `osl/src/osl_panic.c` | `:523` | `sp = ((exc_return & 0x4U) != 0U) ? psp : msp;`（不再调 `__get_MSP/__get_PSP`） |
| `osl/src/osl_panic.c` | `:581` / `:584` | 四个 handler 改 `naked`：`movs r3, #<kind>` 后跳 stub；`FAULT_KIND_*` 去掉 `U` 后缀以便字符串化 |
| `osl/src/osl_panic.c` | `:517` | `fault_mmfar = ((cfsr & (1U << 7)) != 0U) ? SCB->MMFAR : 0U;`（BFAR 用 bit15） |
| `osl/src/osl_panic.c` | `:448` / `:550` | `_Static_assert(PANIC_FRAME_MAX_BYTES == 0x48 + 8*4)`；`frame_need = frame_offset + (8U*4U)`（上界按帧类型算） |
| `build/checks/panic.py` | `collect_fault_range_errors()` | 新增四条：上界必须分帧类型 / `fault_mmfar` 必须判 bit7 / 体内不得出现 `__get_MSP()`·`__get_PSP()` / 源码必须有 `mrs r1, msp` |
| `build/checks/_gate.py` | `case_fault_range()` | 破坏段从 1 段扩到 4 段 |

## 验证证据

```text
python build/build.py build                      # BUILD_EXIT=0，userdebug text=17104 / user text=15072
python build/build.py verify                     # VERIFY_EXIT=0，自检通过
python build/build.py gate-selftest --only fault-range --assert-map
                                                 # PASS 135.2 s，4 段都报红
                                                 # {"fault-range": ["collect_fault_range_errors"]} 无漂移
python flash/flash.py flash                      # ** Verified OK **，FLASH_EXIT=0
```

硬件实测（修复后同一份固件，两个上下文各触发一次）：

| 上下文 | `fault_kind` | `fault_cfsr` | `fault_hfsr` | `fault_sp` | `fault_pc` |
|--------|--------------|--------------|--------------|------------|------------|
| A：`reset halt` 后立刻（MSP、栈深恰 0x20） | `1`（HARD） | `1`（IACCVIOL） | `0x40000000`（FORCED） | `0x2002FFE0` | **`0xFFFFFFFE`** ✓ |
| B：调度器起来 2.5 s 后（idle 任务 PSP） | `2`（MEM） | `1`（IACCVIOL） | `0` | `0x200003E8` | **`0xFFFFFFFE`** ✓ |

- A 组 `fault_sp` 距 `_estack = 0x20030000` **恰好 0x20** —— 旧判据（0x68）会把这一帧整段判成"栈已崩"、
  8 个字段留空；新判据（`frame_need = 0x20`）刚好放行。**这是"上界必须分帧类型"那条修复的硬件实证。**
- A/B 两组 `fault_kind` 不同（1 vs 2）、`fault_hfsr` 不同（FORCED vs 0），说明
  `osl_fault_monitor_init()` 的 SHCSR 三使能位在启动流程里真的生效了 —— D4 的硬件侧证据。
- `fault_mmfar` / `fault_bfar` 两侧都是 `0`；修复前同一路径记的是 `0xE000ED34` / `0xE000ED38`。

其它链路同时得到验证：

| 项 | 实测 |
|------|------|
| `.noinit` 跨复位 | 写 `0x50414E43` / `0x11223344` → `reset run` → 原样读回 ✓ |
| `.bss` 复位清零（与 `.noinit` 行为相反） | `g_panic_reporting` 写 1 → reset → 读回 0 ✓ |
| 调度器在跑 | `g_tick_ms` 两次 halt 间隔 1 s：`0` → `0x3453`（13395）✓ |
| 日志环形缓冲真的在工作 | `s_ring`（`0x2000005c`，320 B）按小端转 ASCII 读出 `stm32-freertos: USART1 115200 debug rea…` ✓ |
| `_write` 从未被调用（日志不走 newlib） | `g_last_write` 64 B 全 `0xAA`，与 `.noinit` 初值一致 ✓ |

elf SHA256：

- userdebug `8E8B81D728D1D6E7089A835A89C251842E6734A31E8EC886A32B09F06576D65E`
- user `36CD7569FA9D4A97E7A9F6261B3B1FC3A5F4D49C33E230F7F1ABF7B64DA3B7CD`

## 遗留

1. **`fault_entry_stub` 用 `b` 尾跳到 `static` 的 `fault_report`**：已给 `fault_report` 加
   `noinline, used` 保证符号实体存在；若将来有人把它挪成 `static inline` 或改名，**链接期**会报
   undefined（可见失败，不会静默）。
2. **`reset halt` 后立刻触发的那一帧，`CFSR`/`HFSR` 语义依赖 SHCSR 是否已使能**：
   启动早期 kind 恒为 HARD（FORCED），调度器起来后才会细分到 MEM/BUS/USAGE。这是设计如此，
   读现场时要结合 `fault_hfsr` 的 FORCED 位判断"是不是被升级的"。
3. 本机没有串口，所有板上结论都走 SWD 读内存；**UART 实际输出波形未验**（与既有「零硬件实测」口径一致）。
