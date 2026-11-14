#!/usr/bin/env python3
"""板上现场快照 → debug/<问题>_YYYY_MM_DD-HH_MM_SS.md"""
from __future__ import annotations

import argparse
import re
import subprocess
import sys
from datetime import datetime
from pathlib import Path

_DEBUG = Path(__file__).resolve().parent
ROOT = _DEBUG.parent
_BUILD = ROOT / "build"
_FLASH = ROOT / "flash"

if str(_BUILD) not in sys.path:
    sys.path.insert(0, str(_BUILD))
if str(_FLASH) not in sys.path:
    sys.path.insert(0, str(_FLASH))

from contracts import OUT_ARTIFACT_DIR, ROOT as CONTRACT_ROOT  # noqa: E402

import environment as te  # noqa: E402

# 与 flash/flash.py 同目录加载
import importlib.util

_flash_py = _FLASH / "flash.py"
_spec = importlib.util.spec_from_file_location("project_flash", _flash_py)
if _spec is None or _spec.loader is None:
    raise SystemExit(f"无法加载 {_flash_py}")
flash_mod = importlib.util.module_from_spec(_spec)
sys.modules["project_flash"] = flash_mod
_spec.loader.exec_module(flash_mod)

PANIC_MAGIC = 0x50414E43
BOOT_STAGE_MEANING = {
    0: "board_system_init 成功",
    1: "HAL_Init 失败",
    2: "SystemClock_Config 失败（再看 g_clock_fail_sub）",
}
CLOCK_FAIL_SUB = {
    0: "（未记录 / 已清零）",
    1: "VOSRDY 等待超时",
    2: "HAL_RCC_OscConfig（HSE/PLL）",
    3: "HAL_RCC_ClockConfig（切 SYSCLK）",
}


def slug_problem(name: str) -> str:
    s = name.strip().lower()
    s = re.sub(r"[^\w\-]+", "-", s, flags=re.UNICODE)
    s = re.sub(r"-+", "-", s).strip("-")
    if not s:
        raise SystemExit("问题名不能为空")
    return s[:80]


def timestamp_tag() -> str:
    return datetime.now().strftime("%Y_%m_%d-%H_%M_%S")


def resolve_toolchain_bin() -> Path | None:
    env = te.apply()
    prefix = env.get(te.ENV_TOOLCHAIN_PREFIX, "arm-none-eabi-")
    root = env.get(te.ENV_TOOLCHAIN_ROOT)
    if root:
        bindir = Path(root) / "bin"
        for suffix in (".exe", ""):
            p = bindir / f"{prefix}nm{suffix}"
            if p.is_file():
                return bindir
    found = te.find_toolchain_bin()
    return found.parent if found else None


def tool_path(bindir: Path | None, name: str) -> str | None:
    if bindir is None:
        return None
    for suffix in (".exe", ""):
        p = bindir / f"{name}{suffix}"
        if p.is_file():
            return str(p)
    return None


def nm_symbol(elf: Path, symbol: str, nm_exe: str) -> int | None:
    out = subprocess.run(
        [nm_exe, "-n", str(elf)],
        check=False,
        capture_output=True,
        text=True,
        encoding="utf-8",
        errors="replace",
    )
    for line in out.stdout.splitlines():
        parts = line.split()
        if len(parts) >= 3 and parts[2] == symbol:
            try:
                return int(parts[0], 16)
            except ValueError:
                continue
    return None


def addr2line(elf: Path, addr: int, exe: str) -> str:
    proc = subprocess.run(
        [exe, "-e", str(elf), "-f", f"0x{addr:08x}"],
        check=False,
        capture_output=True,
        text=True,
        encoding="utf-8",
        errors="replace",
    )
    lines = [ln.strip() for ln in proc.stdout.splitlines() if ln.strip()]
    return " ".join(lines) if lines else "?"


def run_openocd(commands: list[str]) -> str:
    argv = flash_mod.openocd_server_argv()
    for cmd in commands:
        argv.extend(["-c", cmd])
    argv.extend(["-c", "exit"])
    env = flash_mod.apply_flash()
    proc = subprocess.run(
        argv,
        check=False,
        capture_output=True,
        text=True,
        encoding="utf-8",
        errors="replace",
        env=env,
    )
    return proc.stdout + proc.stderr


def parse_mdw_words(text: str, addr: int) -> list[int]:
    pat = re.compile(rf"0x{addr:08x}:\s*([0-9a-fA-F]+(?:\s+[0-9a-fA-F]+)*)", re.I)
    out: list[int] = []
    for m in pat.finditer(text):
        for w in m.group(1).split():
            out.append(int(w, 16))
    return out


def parse_mdw_u32(text: str, addr: int, index: int = 0) -> int | None:
    words = parse_mdw_words(text, addr)
    if index >= len(words):
        return None
    return words[index]


def parse_reg(text: str, name: str) -> int | None:
    m = re.search(rf"{name}\s*\(/32\):\s*0x([0-9a-fA-F]+)", text, re.I)
    return int(m.group(1), 16) if m else None


def decode_hse_rcc(cr: int) -> dict[str, bool]:
    return {
        "HSION": bool(cr & (1 << 0)),
        "HSIRDY": bool(cr & (1 << 1)),
        "HSEON": bool(cr & (1 << 16)),
        "HSERDY": bool(cr & (1 << 17)),
        "PLLON": bool(cr & (1 << 24)),
        "PLLRDY": bool(cr & (1 << 25)),
    }


def gpio_odr_led_hint(odr: int) -> str:
    """低电平点亮：PB0 绿、PB1 红。"""
    pb0 = (odr >> 0) & 1
    pb1 = (odr >> 1) & 1
    red = pb1 == 0
    green = pb0 == 0
    if red and not green:
        return "红亮 / 绿灭"
    if green and not red:
        return "红灭 / 绿亮"
    if red and green:
        return "红绿都亮（异常或 panic 末段）"
    return "红绿都灭"


def build_report(
    problem: str,
    elf: Path,
    *,
    do_halt: bool,
    sample_gpio_ms: int,
) -> str:
    bindir = resolve_toolchain_bin()
    nm_exe = tool_path(bindir, "arm-none-eabi-nm")
    a2l_exe = tool_path(bindir, "arm-none-eabi-addr2line")
    if nm_exe is None:
        raise SystemExit("未找到 arm-none-eabi-nm，请先 python build/build.py install-toolchain")

    sym_boot = nm_symbol(elf, "g_boot_stage", nm_exe)
    sym_panic = nm_symbol(elf, "g_panic_record", nm_exe)
    sym_sub = nm_symbol(elf, "g_clock_fail_sub", nm_exe)
    sym_rcc_snap = nm_symbol(elf, "g_clock_fail_rcc_cr", nm_exe)
    sym_tick = nm_symbol(elf, "g_tick_ms", nm_exe)

    cmds: list[str] = ["init"]
    if do_halt:
        cmds.append("halt")
    cmds.append("reg pc")
    cmds.append("reg lr")
    cmds.extend(["mdw 0x40023800 2", "mdw 0x40007000 2", "mdw 0xE000E010 3"])
    if sym_boot is not None:
        cmds.append(f"mdw 0x{sym_boot:08x} 1")
    if sym_rcc_snap is not None:
        cmds.append(f"mdw 0x{sym_rcc_snap:08x} 1")
    if sym_sub is not None:
        cmds.append(f"mdw 0x{sym_sub:08x} 1")
    if sym_panic is not None:
        cmds.append(f"mdw 0x{sym_panic:08x} 4")
    if sym_tick is not None:
        cmds.append(f"mdw 0x{sym_tick:08x} 1")
    cmds.append("mdw 0x40020414 1")
    if sample_gpio_ms > 0 and do_halt:
        for _ in range(2):
            cmds.append("resume")
            cmds.append(f"sleep {sample_gpio_ms}")
            cmds.append("halt")
            cmds.append("mdw 0x40020414 1")

    gpio_samples: list[tuple[int, str]] = []
    raw = run_openocd(cmds)
    for odr in parse_mdw_words(raw, 0x40020414):
        gpio_samples.append((odr, gpio_odr_led_hint(odr)))

    pc = parse_reg(raw, "pc")
    lr = parse_reg(raw, "lr")
    rcc_cr = parse_mdw_u32(raw, 0x40023800)
    rcc_cfgr = parse_mdw_u32(raw, 0x40023800, 1)
    pwr_cr = parse_mdw_u32(raw, 0x40007000)
    pwr_csr = parse_mdw_u32(raw, 0x40007000, 1)
    systick_ctrl = parse_mdw_u32(raw, 0xE000E010, 0)
    systick_load = parse_mdw_u32(raw, 0xE000E010, 1)

    boot_stage = parse_mdw_u32(raw, sym_boot) if sym_boot is not None else None
    fail_sub = parse_mdw_u32(raw, sym_sub) if sym_sub is not None else None
    fail_rcc = None
    if sym_rcc_snap is not None:
        fail_rcc = parse_mdw_u32(raw, sym_rcc_snap)
    panic_magic = None
    panic_code = None
    if sym_panic is not None:
        prec = parse_mdw_words(raw, sym_panic)
        if prec:
            panic_magic = prec[0]
            if len(prec) > 1:
                panic_code = prec[1]
    tick_ms = parse_mdw_u32(raw, sym_tick) if sym_tick is not None else None

    pc_info = addr2line(elf, pc, a2l_exe) if pc is not None and a2l_exe else "—"
    lr_info = addr2line(elf, lr, a2l_exe) if lr is not None and a2l_exe else "—"

    lines: list[str] = []
    now = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
    lines.append(f"# 现场快照：{problem}")
    lines.append("")
    lines.append(f"- 采集时间：{now}")
    lines.append(f"- 问题标签：`{slug_problem(problem)}`")
    lines.append(f"- 产物 ELF：`{elf.relative_to(CONTRACT_ROOT)}`")
    lines.append("")
    lines.append("## 执行上下文")
    lines.append("")
    lines.append(f"| 项 | 值 |")
    lines.append(f"|----|-----|")
    lines.append(f"| PC | `0x{pc:08x}` — {pc_info}" if pc is not None else "| PC | （未读到） |")
    lines.append(f"| LR | `0x{lr:08x}` — {lr_info}" if lr is not None else "| LR | （未读到） |")
    lines.append(f"| `g_tick_ms` | {tick_ms if tick_ms is not None else '—'} |")
    lines.append("")
    lines.append("## 启动 / Panic")
    lines.append("")
    if boot_stage is not None:
        hint = BOOT_STAGE_MEANING.get(boot_stage, "未知")
        lines.append(f"- `g_boot_stage` = **{boot_stage}** — {hint}")
    else:
        lines.append("- `g_boot_stage` — 符号未解析")
    if fail_sub is not None:
        lines.append(
            f"- `g_clock_fail_sub` = **{fail_sub}** — {CLOCK_FAIL_SUB.get(fail_sub, '未知')}"
        )
        if fail_rcc is not None:
            lines.append(f"- `g_clock_fail_rcc_cr` = `0x{fail_rcc:08x}`（失败时 RCC->CR 快照）")
    if panic_magic is not None:
        if panic_magic == PANIC_MAGIC:
            lines.append(
                f"- `g_panic_record` 有效，**code = {panic_code}**（`.noinit` 多为上一轮遗留；本次是否失败以 `g_boot_stage` 为准）"
            )
        else:
            lines.append(
                f"- `g_panic_record.magic` = `0x{panic_magic:08x}`（非 PANC，多数表示本次尚未 panic）"
            )
    lines.append("")
    lines.append("## 时钟 / 电源")
    lines.append("")
    if rcc_cr is not None:
        flags = decode_hse_rcc(rcc_cr)
        lines.append(f"- `RCC->CR` = `0x{rcc_cr:08x}`")
        for k, v in flags.items():
            lines.append(f"  - {k}: **{'是' if v else '否'}**")
    if rcc_cfgr is not None:
        sws = (rcc_cfgr >> 2) & 3
        sws_name = {0: "HSI", 1: "HSE", 2: "PLL", 3: "PLL"}.get(sws, "?")
        lines.append(f"- `RCC->CFGR` SWS = {sws}（{sws_name}）")
    if pwr_cr is not None:
        lines.append(f"- `PWR->CR` = `0x{pwr_cr:08x}`")
    if pwr_csr is not None:
        vos = bool(pwr_csr & (1 << 14))
        lines.append(f"- `PWR->CSR` = `0x{pwr_csr:08x}`，VOSRDY = **{'是' if vos else '否'}**")
    if systick_ctrl is not None:
        load_s = f"`0x{systick_load:08x}`" if systick_load is not None else "—"
        lines.append(f"- SysTick CTRL/LOAD = `0x{systick_ctrl:08x}` / {load_s}")
    lines.append("")
    lines.append("## 用户灯 GPIOB")
    lines.append("")
    lines.append("DS0 红 PB1、DS1 绿 PB0，**低电平点亮**。")
    lines.append("")
    if gpio_samples:
        lines.append("| 采样序 | `GPIOB->ODR` | 解读 |")
        lines.append("|--------|-------------|------|")
        for i, (val, hint) in enumerate(gpio_samples, start=1):
            lines.append(f"| {i} | `0x{val:08x}` | {hint} |")
        if len(gpio_samples) >= 2:
            vals = {s[0] for s in gpio_samples}
            if len(vals) > 1:
                lines.append("")
                lines.append("**GPIO 在采样间隔内变化** → `fml_led_blink_task` 互斥 1 s 应在工作。")
            else:
                lines.append("")
                lines.append("**GPIO 不变** → 可能停在 panic、灯任务未跑、或采样时刻相同相位。")
    lines.append("")
    lines.append("## 原始 OpenOCD 输出（摘录）")
    lines.append("")
    lines.append("```text")
    for line in raw.splitlines():
        if re.search(r"^(0x[0-9a-fA-F]+:|pc |lr )", line, re.I) or "Error:" in line:
            lines.append(line[:200])
    lines.append("```")
    lines.append("")
    lines.append("## 下一步")
    lines.append("")
    lines.append("- `g_boot_stage==2` → 查 HSE 25 MHz、看重载 `g_clock_fail_sub`")
    lines.append("- `g_boot_stage==0` 且 GPIO 交替 → 软件正常，查 DS1 绿灯或肉眼观察 ≥3 s")
    lines.append("- 详见 `doc/design/搭建环境.md`")
    lines.append("")
    return "\n".join(lines)


def main(argv: list[str] | None = None) -> None:
    parser = argparse.ArgumentParser(description="采集板上现场 → debug/<问题>_时间.md")
    parser.add_argument("problem", help="问题短名，如 clock-hse-fail、red-led-constant")
    parser.add_argument("--elf", type=Path, default=None)
    parser.add_argument(
        "--sample-gpio",
        type=int,
        default=1000,
        metavar="MS",
        help="GPIO 额外采样间隔 ms（0 关闭）",
    )
    parser.add_argument("--no-halt", action="store_true", help="不 halt，直接读（一般不建议）")
    args = parser.parse_args(argv)

    if flash_mod.resolve_openocd() is None:
        raise SystemExit("未找到 OpenOCD：python flash/flash.py install-openocd")

    elf = args.elf or flash_mod.default_firmware_elf()
    if not elf.is_file():
        raise SystemExit(f"ELF 不存在: {elf}，请先 python build/build.py build")

    slug = slug_problem(args.problem)
    out = _DEBUG / f"{slug}_{timestamp_tag()}.md"
    body = build_report(
        args.problem,
        elf,
        do_halt=not args.no_halt,
        sample_gpio_ms=max(0, args.sample_gpio),
    )
    out.write_text(body, encoding="utf-8", newline="\n")
    print(out)


if __name__ == "__main__":
    main()