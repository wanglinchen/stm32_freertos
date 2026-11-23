#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
gen_dts.py - 从 pinmap xlsx 生成 STM32F429 的 GPIO 设备树描述。

输入（同目录两张表，列序对称，表头第 1 行、口径说明第 2 行、数据第 3 行起）：

  stm32f429_default_gpio_pinmap.xlsx
      芯片复位默认态：C 列 mode/pull，E 列可选 AF/ANALOG 全集，F 列复位功能。
      生成 stm32f429_gpio_default.dtsi —— 每个端口一个 pinctrl 节点，逐脚记录
      复位模式、上拉、AF 号与可选复用全集。

  apollo_alternate_gpio_pinmap.xlsx
      阿波罗 V2 板级占用：F 列板上网络，G 列中文连接说明。
      生成 apollo_gpio_overlay.dtsi —— 板上实际走复用的脚写 AF，纯 GPIO/按键/LED/
      晶振/模拟/软件模拟 SPI 脚保持默认输入态，网络名与备注写进属性留档。

说明：
  * 本工程 dtc 单文件编译（Makefile: dtc -I dts -O dtb -o $@ $<），没有 C 预处理、
    也没有 stm32-pinctrl.h 宏头文件，因此 pinmux 一律写裸数值，不使用 STM32_PINMUX
    宏；数值口径与 RM0090 GPIO 寄存器一致。
  * 当前内核 gpio 驱动刻意未实现 pinctrl 解析，这两份 dtsi 是描述性/约定式数据，
    供后续 pinctrl 子系统或人工对照使用。

用法：
  python gen_dts.py                 # 生成两份 dtsi 到 project/dts/（入库的源）
  python gen_dts.py --out <dir>     # 指定输出目录
  python gen_dts.py --check         # 只做映射自检，不写文件
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

import openpyxl

PINMAP_DIR = Path(__file__).resolve().parent
ROOT_DIR = PINMAP_DIR.parents[2]
DEFAULT_XLSX = PINMAP_DIR / "stm32f429_default_gpio_pinmap.xlsx"
APOLLO_XLSX = PINMAP_DIR / "apollo_alternate_gpio_pinmap.xlsx"
DEFAULT_DTSI_NAME = "stm32f429_gpio_default.dtsi"
OVERLAY_DTSI_NAME = "apollo_gpio_overlay.dtsi"
# 生成的 dtsi 是工程「源」的一部分（入库、可 diff），默认落到 project/dts/；--out 可覆盖。
DEFAULT_OUT_DIR = ROOT_DIR / "project" / "dts"

# ---- RM0090 编码（与 include/soc/stm32f4/regs.h 对齐）----
# MODER: 0 input, 1 output, 2 alternate, 3 analog
MODE_CODE = {"input": 0, "output": 1, "alternate": 2, "analog": 3}
# PUPDR: 0 none, 1 up, 2 down
PULL_CODE = {"none": 0, "up": 1, "down": 2}
# ST pinctrl 习惯：ANALOG 复用号记 15（AF15 在 F429 上是 EVENTOUT，模拟脚不占 AF）
ANALOG_AF = 15

PORT_BASE = {
    "A": 0x40020000, "B": 0x40020400, "C": 0x40020800, "D": 0x40020C00,
    "E": 0x40021000, "F": 0x40021400, "G": 0x40021800, "H": 0x40021C00,
    "I": 0x40022000,
}

# 表内模式/上拉 token -> 标准 pinctrl 标志
PULL_PROP = {"up": "bias-pull-up", "down": "bias-pull-down"}


# ---------------------------------------------------------------- 读取 xlsx

def _clean(v) -> str:
    return "" if v is None else str(v).strip()


def read_rows(xlsx: Path) -> list[dict[str, str]]:
    """读 7 列 pinmap 表，跳过表头与口径说明两行。"""
    wb = openpyxl.load_workbook(xlsx, read_only=True, data_only=True)
    ws = wb.active
    rows: list[dict[str, str]] = []
    for r in list(ws.iter_rows(values_only=True))[2:]:
        if not r[1]:
            continue
        rows.append({
            "pin_no": _clean(r[0]),
            "gpio": _clean(r[1]),
            "config": _clean(r[2]),
            "independent": _clean(r[3]),
            "optional": _clean(r[4]),
            "function": _clean(r[5]),
            "notes": _clean(r[6]),
        })
    wb.close()
    return rows


def parse_config(cfg: str) -> tuple[str, str]:
    """C 列 'mode=input; pull=none' -> (mode, pull)。"""
    mode = re.search(r"mode=([^;]+)", cfg)
    pull = re.search(r"pull=([^;]+)", cfg)
    return (
        mode.group(1).strip() if mode else "input",
        pull.group(1).strip() if pull else "none",
    )


def parse_optional(text: str) -> tuple[list[str], dict[int, list[str]]]:
    """E 列 -> (analog 信号列表, {af 号: [信号名,...]})。"""
    analog: list[str] = []
    afs: dict[int, list[str]] = {}
    for seg in text.split(";"):
        seg = seg.strip()
        if not seg:
            continue
        if seg.upper().startswith("ANALOG"):
            body = seg[len("ANALOG:"):].strip()
            analog = [t.strip() for t in body.split(",") if t.strip()]
            continue
        m = re.match(r"AF(\d+):\s*(.*)", seg)
        if m:
            afs[int(m.group(1))] = [t.strip() for t in m.group(2).split(",") if t.strip()]
    return analog, afs


def parse_reset_af(function: str) -> int | None:
    """默认表 F 列 'AF0 / SYS_JTMS_SWDIO' -> 0；'GPIO'/'RCC_OSC_*' -> None。"""
    m = re.match(r"AF(\d+)", function.strip())
    return int(m.group(1)) if m else None


def split_gpio(gpio: str) -> tuple[str, int]:
    m = re.match(r"P([A-I])(\d+)", gpio)
    return m.group(1), int(m.group(2))


def gpio_sort_key(gpio: str) -> tuple[str, int]:
    port, pin = split_gpio(gpio)
    return (port, pin)


# ------------------------------------------------- 板级网络 -> 实际复用 AF

# 网络名关键词 -> E 列信号子串；按顺序匹配，先命中的外设优先。
# 注意前缀包含关系：USART2 不能被 UART2 抢，SPI5 不能被 SPI2 抢，ETH 要在 FMC 前。
_NET_RULES: list[tuple[str, str]] = [
    ("USART1", "USART1"),
    ("USART2", "USART2"),
    ("USART3", "USART3"),
    ("USART6", "USART6"),
    ("UART4", "UART4"),
    ("UART5", "UART5"),
    ("UART7", "UART7"),
    ("UART8", "UART8"),
    ("CAN", "CAN"),
    ("SPI2", "SPI2"),
    ("SPI5", "SPI5"),
    ("I2C2", "I2C2"),
    ("SDIO", "SDIO"),
    ("SAI1", "SAI1"),
    ("DCMI", "DCMI"),
    ("LTDC", "LTDC"),
    ("ETH", "ETH"),
    ("USB", "USB"),
    ("FMC", "FMC"),
]


def net_to_af(net: str, afs: dict[int, list[str]]) -> tuple[int | None, str | None]:
    """板上网络名反查实际生效 AF。

    返回 (af 号, 命中的外设关键词)；查不到复用（GPIO/模拟/晶振/调试/软件模拟）
    返回 (None, None)。
    """
    n = net.upper()

    # RMII / ETH 前缀别名
    if "RMII" in n or "ETH" in n:
        hits = [af for af, toks in afs.items() if any("ETH" in t for t in toks)]
        return (hits[0], "ETH") if len(hits) == 1 else (None, None)

    # LCD 数据线/时序 -> LTDC
    if n.startswith("LCD"):
        hits = [af for af, toks in afs.items() if any("LTDC" in t for t in toks)]
        return (hits[0], "LTDC") if len(hits) == 1 else (None, None)

    # 板上 I2C2 只走 PH4/PH5（IIC_SCL/IIC_SDA）；IIC_INT、1WIRE 这类是中断/单总线 GPIO
    if n in ("IIC_SCL", "IIC_SDA"):
        hits = [af for af, toks in afs.items() if any("I2C2" in t for t in toks)]
        return (hits[0], "I2C2") if len(hits) == 1 else (None, None)

    for kw, token in _NET_RULES:
        if kw in n:
            hits = [af for af, toks in afs.items() if any(token in t for t in toks)]
            if len(hits) == 1:
                return hits[0], kw
    return None, None


# ---------------------------------------------------------------- DTS 生成

def _esc(text: str) -> str:
    """DTS 字符串转义：反斜杠与双引号。"""
    return text.replace("\\", "\\\\").replace('"', '\\"')


def _node_name(gpio: str, net: str) -> str:
    """生成合法的小写 dts 节点名：<net>_<gpio 小写>。"""
    base = re.sub(r"[^A-Za-z0-9]+", "_", net).strip("_").lower() or "gpio"
    return f"{base}_{gpio.lower()}"


def gen_default_dtsi(rows: list[dict[str, str]]) -> str:
    by_port: dict[str, list[dict[str, str]]] = {p: [] for p in PORT_BASE}
    for row in rows:
        port, _ = split_gpio(row["gpio"])
        by_port[port].append(row)

    lines: list[str] = []
    lines.append("// SPDX-License-Identifier: GPL-2.0")
    lines.append("/*")
    lines.append(" * stm32f429_gpio_default.dtsi - STM32F429IG 复位默认 GPIO 配置（自动生成，勿手改）")
    lines.append(" *")
    lines.append(" * 数据源：stm32f429_default_gpio_pinmap.xlsx（DS9405 Table 12 + RM0090 复位态）。")
    lines.append(" * 本工程 dtc 单文件编译、无 C 预处理与 pinctrl 宏头文件，pinmux 写裸数值：")
    lines.append(" *   pinmux = <port pin mode af>;")
    lines.append(" *   mode: 0 input, 1 output, 2 alternate, 3 analog；af: 0..15，模拟脚记 15。")
    lines.append(" */")
    lines.append("/dts-v1/;")
    lines.append("")
    lines.append("/ {")
    lines.append("\t#address-cells = <1>;")
    lines.append("\t#size-cells = <1>;")
    lines.append("\tsoc {")
    lines.append("\t\tcompatible = \"simple-bus\";")
    lines.append("\t\t#address-cells = <1>;")
    lines.append("\t\t#size-cells = <1>;")
    lines.append("\t\tranges;")
    lines.append("")

    for port in sorted(by_port):
        pins = sorted(by_port[port], key=lambda r: gpio_sort_key(r["gpio"]))
        if not pins:
            continue
        base = PORT_BASE[port]
        lines.append(f"\t\tgpio{port.lower()}: gpio@{base:08x} {{")
        lines.append("\t\t\tcompatible = \"st,stm32-gpio\";")
        lines.append(f"\t\t\treg = <0x{base:08x} 0x400>;")
        lines.append("\t\t\tgpio-controller;")
        lines.append("\t\t\t#gpio-cells = <2>;")
        lines.append("\t\t\tngpios = <16>;")
        lines.append("")

        for row in pins:
            gpio = row["gpio"]
            _, pin = split_gpio(gpio)
            mode, pull = parse_config(row["config"])
            analog, afs = parse_optional(row["optional"])
            reset_af = parse_reset_af(row["function"])
            mode_code = MODE_CODE.get(mode, 0)
            pull_code = PULL_CODE.get(pull, 0)

            # 复位态 AF：alternate 脚取 F 列 AF 号；模拟脚记 15；其余输入脚 AF0 占位。
            if mode == "alternate":
                af_code = reset_af if reset_af is not None else 0
            elif mode == "analog":
                af_code = ANALOG_AF
            else:
                af_code = 0

            node = f"pinctrl_{gpio.lower()}"
            lines.append(f"\t\t\t{node}: {node} {{")
            lines.append(f"\t\t\t\tpinmux = <{ord(port) - 64} {pin} {mode_code} {af_code}>;")
            lines.append(f"\t\t\t\tbrd,mode = \"{mode}\";")
            lines.append(f"\t\t\t\tbrd,pull = \"{pull}\";")
            if row["independent"]:
                lines.append(f"\t\t\t\tbrd,independent = \"{row['independent']}\";")
            if row["function"]:
                lines.append(f"\t\t\t\tbrd,reset-function = \"{_esc(row['function'])}\";")
            if analog:
                lines.append(f"\t\t\t\tbrd,analog = \"{_esc(', '.join(analog))}\";")
            if afs:
                opt = "; ".join(
                    f"AF{af}: {', '.join(toks)}" for af, toks in sorted(afs.items())
                )
                lines.append(f"\t\t\t\tbrd,optional-function-modes = \"{_esc(opt)}\";")
            if row["notes"]:
                lines.append(f"\t\t\t\tbrd,notes = \"{_esc(row['notes'])}\";")
            lines.append("\t\t\t};")
        lines.append("\t\t};")
        lines.append("")

    lines.append("\t};")
    lines.append("};")
    lines.append("")
    return "\n".join(lines)


def gen_overlay_dtsi(rows: list[dict[str, str]]) -> tuple[str, list[str]]:
    """生成板级 overlay dtsi；同时返回未能反查到 AF 的脚清单（自检用）。"""
    lines: list[str] = []
    unmatched: list[str] = []
    af_pins: list[dict[str, str]] = []
    gpio_pins: list[dict[str, str]] = []

    for row in sorted(rows, key=lambda r: gpio_sort_key(r["gpio"])):
        gpio = row["gpio"]
        net = row["function"]
        analog, afs = parse_optional(row["optional"])
        if not net or net.lower() == "none":
            gpio_pins.append(row)
            continue

        nets = [x.strip() for x in net.split("/") if x.strip()]
        resolved: list[tuple[str, int, str]] = []
        for n in nets:
            af, kw = net_to_af(n, afs)
            if af is not None:
                resolved.append((n, af, kw))
            else:
                unmatched.append(f"{gpio} {n}")

        if resolved:
            row["_resolved"] = resolved
            af_pins.append(row)
        else:
            gpio_pins.append(row)

    lines.append("// SPDX-License-Identifier: GPL-2.0")
    lines.append("/*")
    lines.append(" * apollo_gpio_overlay.dtsi - 正点原子阿波罗 V2 板级 GPIO 占用（自动生成，勿手改）")
    lines.append(" *")
    lines.append(" * 数据源：apollo_alternate_gpio_pinmap.xlsx（手册 V1.1 表 1.5.1）。")
    lines.append(" * 走片上复用的脚在 pinmux 里写实际 AF；纯 GPIO/按键/LED/晶振/模拟/软件模拟")
    lines.append(" * SPI 脚保持默认输入态，网络名与连接说明写进 brd 属性留档。")
    lines.append(" * pinmux = <port pin mode af>; mode: 0 input, 2 alternate；af: 0..15。")
    lines.append(" */")
    lines.append("/dts-v1/;")
    lines.append("")
    lines.append("/ {")
    lines.append("\t#address-cells = <1>;")
    lines.append("\t#size-cells = <1>;")
    lines.append("\tsoc {")
    lines.append("")

    # ---- 复用脚：按外设分组写节点 ----
    groups: dict[str, list[dict[str, str]]] = {}
    for row in af_pins:
        kw = row["_resolved"][0][2]
        groups.setdefault(kw, []).append(row)

    for kw in sorted(groups):
        lines.append(f"\t\t/* ---- {kw} ---- */")
        for row in groups[kw]:
            gpio = row["gpio"]
            port, pin = split_gpio(gpio)
            net, af, _ = row["_resolved"][0]
            node = _node_name(gpio, net)
            lines.append(f"\t\t{node}: {node} {{")
            lines.append(f"\t\t\tpinmux = <{ord(port) - 64} {pin} 2 {af}>;")
            lines.append(f"\t\t\tbrd,gpio = \"{gpio}\";")
            lines.append(f"\t\t\tbrd,board-net = \"{_esc(row['function'])}\";")
            if row["independent"]:
                lines.append(f"\t\t\tbrd,independent = \"{row['independent']}\";")
            if row["notes"]:
                lines.append(f"\t\t\tbrd,notes = \"{_esc(row['notes'])}\";")
            lines.append("\t\t};")
        lines.append("")

    # ---- 普通 GPIO / 未复用脚：只留档，不配 AF ----
    lines.append("\t\t/* ---- 纯 GPIO / 按键 / LED / 晶振 / 模拟 / 软件模拟 SPI（保持输入态）---- */")
    for row in gpio_pins:
        gpio = row["gpio"]
        port, pin = split_gpio(gpio)
        net = row["function"] or "(未连接)"
        node = _node_name(gpio, net)
        lines.append(f"\t\t{node}: {node} {{")
        lines.append(f"\t\t\tpinmux = <{ord(port) - 64} {pin} 0 0>;")
        lines.append(f"\t\t\tbrd,gpio = \"{gpio}\";")
        lines.append(f"\t\t\tbrd,board-net = \"{_esc(net)}\";")
        if row["independent"]:
            lines.append(f"\t\t\tbrd,independent = \"{row['independent']}\";")
        if row["notes"]:
            lines.append(f"\t\t\tbrd,notes = \"{_esc(row['notes'])}\";")
        lines.append("\t\t};")
    lines.append("")

    lines.append("\t};")
    lines.append("};")
    lines.append("")
    return "\n".join(lines), unmatched


# ------------------------------------------------------------------- 主流程

def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--out", type=Path, default=DEFAULT_OUT_DIR, help="输出目录（默认 project/dts）")
    ap.add_argument("--check", action="store_true", help="只做映射自检，不写文件")
    args = ap.parse_args()

    if not DEFAULT_XLSX.is_file():
        raise SystemExit(f"缺少默认表：{DEFAULT_XLSX}")
    if not APOLLO_XLSX.is_file():
        raise SystemExit(f"缺少板级表：{APOLLO_XLSX}")

    default_rows = read_rows(DEFAULT_XLSX)
    apollo_rows = read_rows(APOLLO_XLSX)

    default_dtsi = gen_default_dtsi(default_rows)
    overlay_dtsi, unmatched = gen_overlay_dtsi(apollo_rows)

    # 自检：板级表每个网络都应能归类（复用脚命中 AF，其余归 GPIO 留档）；
    # unmatched 只用于提示“未反查到片上复用”，这些脚按 GPIO 处理，不算错误。
    print(f"default pins: {len(default_rows)} | apollo pins: {len(apollo_rows)}")
    if unmatched:
        print(f"[info] {len(unmatched)} 个网络未命中片上复用，按 GPIO/模拟/晶振/软件模拟处理：")
        for u in unmatched:
            print(f"       {u}")

    if args.check:
        return 0

    args.out.mkdir(parents=True, exist_ok=True)
    default_path = args.out / DEFAULT_DTSI_NAME
    overlay_path = args.out / OVERLAY_DTSI_NAME
    # 本工程 .gitattributes 钉 * text=auto eol=lf；Windows 上 write_text 默认把
    # "\n" 写成 CRLF，会让 build.py update 的 EOL 自检报红，这里显式钉 LF。
    default_path.write_text(default_dtsi, encoding="utf-8", newline="\n")
    overlay_path.write_text(overlay_dtsi, encoding="utf-8", newline="\n")
    print(f"Wrote {default_path}")
    print(f"Wrote {overlay_path}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
