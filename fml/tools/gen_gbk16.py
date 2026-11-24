#!/usr/bin/env python3
"""生成 fml_font_gbk16.c：16×16 GB2312 全字库点阵表。

为什么要生成而不是从参考工程搬：参考工程把字库烧在 SPI Flash 上、按"裸地址 +
偏移表"读；本工程 P3 勘探已证实这块板的 NOR **卷里没有任何字库文件**（/SYSTEM
下是别的工程的素材目录，三个 44~51 KB 的 BIN 装不下 766 KB 的 GBK16 全集）。
所以字库自带，以 const 表进 flash —— 不依赖 `fml_fatfs`，也不碰板上已有数据。

用法：
    python fml/tools/gen_gbk16.py
输出：
    fml/src/fml_font_gbk16.c（覆盖写）

约束（改参数前先读）：
  - 表覆盖 GB2312 的 1..87 区 × 94 位 = 8178 格，**空区也占格子**（全 0 字形）。
    这样下标 = (qu-1)*94 + (wei-1) 不需要第二张偏移表；空区取字返回全 0，
    画出来就是空白，渲染侧不用特判。
  - 10..15 区在 GB2312 里未定义，符号区 1..9、一级汉字 16..55、二级汉字 56..87。
  - 每字 16 行 × 2 字节，bit7 = 最左像素（与 ASCII 表、LTDC 从左到右一致）。
  - 二值化阈值 110 而不是 128：宋体 16px 的细笔画在 128 下断笔（"永"只剩 47 点），
    110 能保住横细竖粗的关系，同时不至于把小字糊成一团。
"""

import sys
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

CELL_W = 16
CELL_H = 16
QU_COUNT = 87
WEI_COUNT = 94
FONT_PATH = r"C:\Windows\Fonts\simsun.ttc"
FONT_SIZE = 16
THRESHOLD = 110

OUT = Path(__file__).resolve().parents[1] / "src" / "fml_font_gbk16.c"


def render_glyph(font, ch: str) -> list[list[int]]:
    img = Image.new("L", (CELL_W, CELL_H), 0)
    draw = ImageDraw.Draw(img)
    bb = draw.textbbox((0, 0), ch, font=font)
    w = bb[2] - bb[0]
    h = bb[3] - bb[1]
    ox = (CELL_W - w) // 2 - bb[0]
    oy = (CELL_H - h) // 2 - bb[1]
    draw.text((ox, oy), ch, font=font, fill=255)

    px = img.load()
    rows = []
    for y in range(CELL_H):
        hi = 0
        lo = 0
        for x in range(CELL_W):
            if px[x, y] >= THRESHOLD:
                if x < 8:
                    hi |= 0x80 >> x
                else:
                    lo |= 0x80 >> (x - 8)
        rows.append([hi, lo])
    return rows


def main() -> int:
    if not Path(FONT_PATH).exists():
        print(f"字体不存在：{FONT_PATH}", file=sys.stderr)
        return 1
    font = ImageFont.truetype(FONT_PATH, FONT_SIZE)

    glyphs = []
    mapped = 0
    for qu in range(1, QU_COUNT + 1):
        for wei in range(1, WEI_COUNT + 1):
            raw = bytes([0xA0 + qu, 0xA0 + wei])
            try:
                ch = raw.decode("gb2312")
            except UnicodeDecodeError:
                ch = None
            if ch is None or len(ch) != 1:
                glyphs.append([[0, 0] for _ in range(CELL_H)])
                continue
            rows = render_glyph(font, ch)
            if any(hi or lo for hi, lo in rows):
                mapped += 1
            glyphs.append(rows)

    total_bytes = len(glyphs) * CELL_H * 2
    lines = []
    lines.append("/* 由 fml/tools/gen_gbk16.py 生成，勿手改。")
    lines.append(" * 16×16 GB2312 全表（1..87 区 × 94 位 = 8178 格，32 B/字），")
    lines.append(f" * bit7 = 该行最左像素；阈值 {THRESHOLD}/255；空区与未映射字为全 0。")
    lines.append(" * 重新生成：python fml/tools/gen_gbk16.py */")
    lines.append("")
    lines.append("#include <stdint.h>")
    lines.append("")
    lines.append('#include "fml_font.h"')
    lines.append("")
    lines.append("#define GBK16_COUNT 8178U")
    lines.append("")
    lines.append("static const uint8_t gbk16_glyphs[GBK16_COUNT][16][2] = {")
    for rows in glyphs:
        # 每一行都写内层花括号：扁平写法（{ 0x00,0x00, ... }）会触发
        # -Wmissing-braces，在本工程 -Werror 下直接编译失败。
        text = ", ".join(f"{{0x{hi:02X},0x{lo:02X}}}" for hi, lo in rows)
        lines.append(f"    {{ {text} }},")
    lines.append("};")
    lines.append("")
    lines.append("const fml_font_t fml_font_gbk16 = {")
    lines.append("    .width = 16U,")
    lines.append("    .height = 16U,")
    lines.append("    .glyphs = &gbk16_glyphs[0][0][0],")
    lines.append("    .first = 0xA1A1U,")
    lines.append("    .count = GBK16_COUNT,")
    lines.append("    .enc = FML_FONT_ENC_GB2312,")
    lines.append("};")
    lines.append("")

    OUT.write_text("\n".join(lines), encoding="utf-8", newline="\n")
    print(f"写出 {OUT}（{len(glyphs)} 格 / {total_bytes} B，实际有字形 {mapped} 个）")
    return 0


if __name__ == "__main__":
    sys.exit(main())
