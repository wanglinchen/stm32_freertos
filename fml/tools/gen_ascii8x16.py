#!/usr/bin/env python3
"""生成 fml_font_ascii8x16.c：8×16 单色 ASCII 字形表。

为什么要生成而不是从参考工程搬：参考工程 lcd_ex.c 里的 asc2_* 表是半角点阵，
与本工程 P3 要接的 GBK 全角字库重复（[对照表 §7 不照搬清单]）。这里用本机等宽
字体渲染一份最小 ASCII 表（95 个码位、16 B/字、共 1520 B），只服务 P1 的
"屏上真的显示出文字"这一条验收；P3 接上 NOR 字库后，fml_font 的接口不变，
换的是 fml_font_t 里的数据指针。

用法：
    python fml/tools/gen_ascii8x16.py
输出：
    fml/src/fml_font_ascii8x16.c（覆盖写）

约束（改参数前先读）：
  - 位序 bit7 = 最左像素，与 LTDC 帧缓冲的从左到右一致；
  - 空格字形必须全 0：POST 的 font 项拿"空格渲染出的像素数必须为 0"当反证，
    表一旦给空格留下脏点，那条判据就失去意义；
  - 行高固定 16、字宽固定 8：LTDC 是 RGB565 逐行缓冲，字形高度不整除会在
    行间留下半像素撕裂。
"""

import sys
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

FIRST = 0x20
LAST = 0x7E
CELL_W = 8
CELL_H = 16
FONT_PATH = r"C:\Windows\Fonts\consola.ttf"
FONT_SIZE = 12

OUT = Path(__file__).resolve().parents[1] / "src" / "fml_font_ascii8x16.c"


def render_glyph(font, ch: str) -> list[int]:
    img = Image.new("1", (CELL_W, CELL_H), 0)
    draw = ImageDraw.Draw(img)
    bb = draw.textbbox((0, 0), ch, font=font)
    w = bb[2] - bb[0]
    h = bb[3] - bb[1]
    ox = (CELL_W - w) // 2 - bb[0]
    oy = (CELL_H - h) // 2 - bb[1]
    draw.text((ox, oy), ch, font=font, fill=1)

    rows = []
    for y in range(CELL_H):
        bits = 0
        for x in range(CELL_W):
            if img.getpixel((x, y)):
                bits |= 0x80 >> x
        rows.append(bits)
    return rows


def main() -> int:
    if not Path(FONT_PATH).exists():
        print(f"字体不存在：{FONT_PATH}", file=sys.stderr)
        return 1
    font = ImageFont.truetype(FONT_PATH, FONT_SIZE)

    glyphs = []
    for cp in range(FIRST, LAST + 1):
        rows = render_glyph(font, chr(cp))
        if cp == 0x20 and any(rows):
            print("空格字形非空：表会污染 POST 的 blank 判据", file=sys.stderr)
            return 1
        glyphs.append((cp, rows))

    lines = []
    lines.append("/* 由 fml/tools/gen_ascii8x16.py 生成，勿手改。")
    lines.append(" * 8×16 单色 ASCII（0x20..0x7E，95 字，16 B/字），bit7 = 最左像素。")
    lines.append(" * 重新生成：python fml/tools/gen_ascii8x16.py */")
    lines.append("")
    lines.append("#include <stdint.h>")
    lines.append("")
    lines.append('#include "fml_font.h"')
    lines.append("")
    lines.append(f"#define ASCII8X16_COUNT {len(glyphs)}U")
    lines.append("")
    lines.append("static const uint8_t ascii8x16_glyphs[ASCII8X16_COUNT][16] = {")
    for cp, rows in glyphs:
        text = ", ".join(f"0x{b:02X}" for b in rows)
        lines.append(f"    {{ {text} }}, /* 0x{cp:02X} */")
    lines.append("};")
    lines.append("")
    lines.append("const fml_font_t fml_font_ascii8x16 = {")
    lines.append("    .width = 8U,")
    lines.append("    .height = 16U,")
    lines.append("    .glyphs = &ascii8x16_glyphs[0][0],")
    lines.append("    .first = 0x20U,")
    lines.append("    .count = ASCII8X16_COUNT,")
    lines.append("    .enc = FML_FONT_ENC_U8, ")
    lines.append("};")
    lines.append("")

    OUT.write_text("\n".join(lines), encoding="utf-8", newline="\n")
    print(f"写出 {OUT}（{len(glyphs)} 字 / {len(glyphs) * 16} B）")
    return 0


if __name__ == "__main__":
    sys.exit(main())
