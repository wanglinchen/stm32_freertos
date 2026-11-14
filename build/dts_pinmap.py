"""从阿波罗 V2 引脚分配表 xlsx 生成 device/dts/generated/*-pinmap.dtsi。"""
from __future__ import annotations

import re
import xml.etree.ElementTree as ET
import zipfile
from pathlib import Path

from contracts import ROOT

_PINMAP_GEN_BANNER = (
    "/* 自动生成，勿手改。来源：doc/hardware/pinmap/*.xlsx；刷新：build/build.py update */"
)
_NS = {"m": "http://schemas.openxmlformats.org/spreadsheetml/2006/main"}


def _col_letter(cell_ref: str) -> str:
    return re.sub(r"[0-9]", "", cell_ref)


def _read_xlsx_rows(path: Path) -> list[dict[str, str]]:
    with zipfile.ZipFile(path) as zf:
        ss: list[str] = []
        ss_root = ET.fromstring(zf.read("xl/sharedStrings.xml"))
        for si in ss_root.findall(".//m:si", _NS):
            texts = [t.text or "" for t in si.findall(".//m:t", _NS)]
            ss.append("".join(texts))

        sheet = ET.fromstring(zf.read("xl/worksheets/sheet1.xml"))
        rows: list[dict[str, str]] = []
        for row in sheet.findall(".//m:row", _NS):
            by_col: dict[str, str] = {}
            for cell in row.findall("m:c", _NS):
                ref = cell.get("r", "")
                col = _col_letter(ref)
                v_el = cell.find("m:v", _NS)
                if v_el is None or v_el.text is None:
                    continue
                val = v_el.text
                if cell.get("t") == "s":
                    val = ss[int(val)]
                by_col[col] = val.strip().replace("\n", " ")
            if "B" not in by_col or not re.fullmatch(r"P[A-I]\d+", by_col["B"]):
                continue
            rows.append(
                {
                    "pin_no": by_col.get("A", ""),
                    "gpio": by_col["B"],
                    "resource": by_col.get("C", ""),
                    "resource2": by_col.get("D", ""),
                    "independent": by_col.get("E", ""),
                    "note": by_col.get("F", ""),
                    "hint": by_col.get("G", ""),
                }
            )
    return rows


def _gpio_port_pin(gpio: str) -> tuple[str, int]:
    m = re.fullmatch(r"P([A-I])(\d+)", gpio)
    if m is None:
        raise ValueError(f"非法 GPIO 名：{gpio}")
    return m.group(1), int(m.group(2))


def _dts_escape(s: str) -> str:
    return s.replace("\\", "\\\\").replace('"', '\\"')


def generate_pinmap_dtsi(rows: list[dict], *, source_rel: str) -> str:
    lines = [
        _PINMAP_GEN_BANNER,
        f"/* 源：{source_rel} */",
        "",
        "pinmap {",
        '    compatible = "brd,apollo-f429-pinmap";',
        f"    pin-count = <{len(rows)}>;",
        "",
    ]
    for row in rows:
        gpio = row["gpio"]
        port, pin = _gpio_port_pin(gpio)
        pin_no = row["pin_no"] or "0"
        label = _dts_escape(row["resource"] or "NC")
        alt = _dts_escape(row["resource2"]) if row["resource2"] else ""
        indep = "independent;" if row["independent"] == "Y" else "shared;"
        lines.append(f"    pin-{pin_no} {{")
        lines.append(f'        gpio-port = "{port}";')
        lines.append(f"        gpio-pin = <{pin}>;")
        lines.append(f'        label = "{label}";')
        if alt:
            lines.append(f'        alt-label = "{alt}";')
        lines.append(f"        {indep}")
        if row["note"]:
            lines.append(f'        /* {_dts_escape(row["note"][:120])} */')
        lines.append("    };")
        lines.append("")
    lines.extend(["};", ""])
    return "\n".join(lines)


def default_pinmap_xlsx() -> Path:
    return (
        ROOT
        / "doc"
        / "hardware"
        / "pinmap"
        / "阿波罗V2 STM32F429开发板IO引脚分配表.xlsx"
    )


def sync_pinmap_dtsi(xlsx: Path, out: Path) -> int:
    if not xlsx.is_file():
        raise FileNotFoundError(xlsx)
    rows = _read_xlsx_rows(xlsx)
    if len(rows) < 100:
        raise ValueError(f"引脚表行数异常：{len(rows)}（期望 ≥ 100）")
    text = generate_pinmap_dtsi(rows, source_rel=xlsx.relative_to(ROOT).as_posix())
    out.parent.mkdir(parents=True, exist_ok=True)
    from generate import write_generated_text

    write_generated_text(out, text)
    return len(rows)