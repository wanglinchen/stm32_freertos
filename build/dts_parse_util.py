"""DTS 子集解析小工具（括号匹配、include 展开）。"""
from __future__ import annotations

import re
from pathlib import Path

from contracts import ROOT

_DTS_INCLUDE_RE = re.compile(r'#include\s+"([^"]+)"')


def strip_c_comments(text: str) -> str:
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
    text = re.sub(r"//[^\n]*", "", text)
    return text


def merge_dts_includes(text: str, base_dir: Path, seen: set[Path] | None = None) -> str:
    if seen is None:
        seen = set()
    out: list[str] = []
    pos = 0
    for match in _DTS_INCLUDE_RE.finditer(text):
        out.append(text[pos : match.start()])
        inc_path = (base_dir / match.group(1)).resolve()
        if not inc_path.is_file():
            raise FileNotFoundError(f"DTS #include 找不到：{inc_path.relative_to(ROOT)}")
        if inc_path in seen:
            raise ValueError(f"DTS #include 循环：{inc_path.relative_to(ROOT)}")
        seen.add(inc_path)
        nested = strip_c_comments(inc_path.read_text(encoding="utf-8"))
        out.append(merge_dts_includes(nested, inc_path.parent, seen))
        pos = match.end()
    out.append(text[pos:])
    return "".join(out)


def extract_named_block(body: str, name: str) -> str | None:
    m = re.search(rf"(?<![\w-]){re.escape(name)}\s*\{{", body)
    if m is None:
        return None
    open_brace = m.end() - 1
    depth = 0
    for i in range(open_brace, len(body)):
        ch = body[i]
        if ch == "{":
            depth += 1
        elif ch == "}":
            depth -= 1
            if depth == 0:
                return body[open_brace + 1 : i]
    return None


def iter_gpio_child_inners(parent_inner: str) -> list[str]:
    """在父节点内按出现顺序收集含 gpio-port/gpio-pin 的直接子块内容。"""
    inners: list[str] = []
    i = 0
    n = len(parent_inner)
    while i < n:
        if parent_inner[i] != "{":
            i += 1
            continue
        depth = 1
        j = i + 1
        while j < n and depth:
            if parent_inner[j] == "{":
                depth += 1
            elif parent_inner[j] == "}":
                depth -= 1
            j += 1
        block = parent_inner[i + 1 : j - 1]
        if "gpio-port" in block and "gpio-pin" in block:
            inners.append(block)
        i = j
    return inners


def extract_quoted_string_list(inner: str, prop: str) -> tuple[str, ...]:
    m = re.search(rf"{re.escape(prop)}\s*=\s*((?:\"[^\"]+\"\s*,?\s*)+)", inner)
    if m is None:
        return ()
    return tuple(re.findall(r"\"([^\"]+)\"", m.group(1)))