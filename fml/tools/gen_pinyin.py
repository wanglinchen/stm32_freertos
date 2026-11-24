#!/usr/bin/env python3
"""生成 fml_t9_table.c：九宫格拼音输入法（T9）的音节 + 汉字表。

为什么要生成而不是手写：3755 个一级汉字的拼音归属靠人排必然出错，而这个表的
正确性直接决定"按键 94664 出不出得了 zhong 组"。

数据来源：GB2312 **一级汉字**（16..55 区，3755 字）。二级汉字（56..87 区）收
进来会让表翻倍（+3008 字 × 2 B），而记事本里几乎用不到生僻字。多音字只取
`pypinyin` 的默认读音（`heteronym=False`）—— 这是"够用"与"表大小"的取舍，
不是疏漏；要展开多音字，把 heteronym 打开并去重即可，接口不用改。

用法：
    python fml/tools/gen_pinyin.py
输出：
    fml/src/fml_t9_table.c（覆盖写）

表结构（fml_t9.c 里以 extern 引用）：
    fml_t9_syl[]        音节字符串（字母序，"a".."zuo"）
    fml_t9_syl_first[]  该音节第一个汉字在 fml_t9_han[] 里的下标
    fml_t9_syl_count[]  该音节有多少个汉字
    fml_t9_han[]        汉字 GBK 码（0xD6D0 = "中"），组内按 GBK 升序
这样"音节 → 汉字"是两次数组读，"GBK 升序"让候选列表的第一项稳定（同一张表
重新生成，候选首页不变）。
"""

import sys
from pathlib import Path

from pypinyin import Style, pinyin

QU_FIRST = 16
QU_LAST = 55
WEI_COUNT = 94

OUT = Path(__file__).resolve().parents[1] / "src" / "fml_t9_table.c"


def han_iter():
    for qu in range(QU_FIRST, QU_LAST + 1):
        for wei in range(1, WEI_COUNT + 1):
            raw = bytes([0xA0 + qu, 0xA0 + wei])
            try:
                ch = raw.decode("gb2312")
            except UnicodeDecodeError:
                continue
            if len(ch) != 1:
                continue
            yield raw[0], raw[1], ch


def han_pinyin(ch: str) -> str:
    got = pinyin(ch, style=Style.NORMAL, heteronym=False, errors="ignore")
    if not got or not got[0]:
        return ""
    syl = got[0][0].strip().lower()
    # NORMAL 下 ü 写作 "lv"/"nv"；只保留 a-z，其余（含声调数字、标点）一律丢掉。
    syl = "".join(c for c in syl if "a" <= c <= "z")
    return syl


def main() -> int:
    groups: dict[str, list[int]] = {}
    skipped = 0

    for hb, lb, ch in han_iter():
        syl = han_pinyin(ch)
        if not syl:
            skipped += 1
            continue
        code = (hb << 8) | lb
        groups.setdefault(syl, []).append(code)

    if not groups:
        print("没有生成任何分组：pypinyin 不可用？", file=sys.stderr)
        return 1

    names = sorted(groups.keys())
    first = []
    count = []
    hans = []
    for name in names:
        codes = sorted(groups[name])
        first.append(len(hans))
        count.append(len(codes))
        hans.extend(codes)

    lines = []
    lines.append("/* 由 fml/tools/gen_pinyin.py 生成，勿手改。")
    lines.append(f" * GB2312 一级汉字（{QU_FIRST}..{QU_LAST} 区）按默认读音分组：")
    lines.append(f" * {len(names)} 个音节 / {len(hans)} 个汉字；组内按 GBK 码升序。")
    lines.append(" * 重新生成：python fml/tools/gen_pinyin.py */")
    lines.append("")
    lines.append("#include <stdint.h>")
    lines.append("")
    lines.append(f"#define T9_SYL_COUNT {len(names)}U")
    lines.append(f"#define T9_HAN_COUNT {len(hans)}U")
    lines.append("")
    lines.append("const char *const fml_t9_syl[T9_SYL_COUNT] = {")
    for name in names:
        lines.append(f'    "{name}",')
    lines.append("};")
    lines.append("")
    lines.append("const uint16_t fml_t9_syl_first[T9_SYL_COUNT] = {")
    for i in range(0, len(first), 12):
        lines.append("    " + " ".join(f"{v}U," for v in first[i:i + 12]))
    lines.append("};")
    lines.append("")
    lines.append("const uint8_t fml_t9_syl_count[T9_SYL_COUNT] = {")
    for i in range(0, len(count), 16):
        lines.append("    " + " ".join(f"{v}U," for v in count[i:i + 16]))
    lines.append("};")
    lines.append("")
    lines.append("const uint16_t fml_t9_han[T9_HAN_COUNT] = {")
    for i in range(0, len(hans), 10):
        lines.append("    " + " ".join(f"0x{v:04X}U," for v in hans[i:i + 10]))
    lines.append("};")
    lines.append("")

    OUT.write_text("\n".join(lines), encoding="utf-8", newline="\n")
    print(f"写出 {OUT}")
    print(f"  音节 {len(names)} 个，汉字 {len(hans)} 个（{len(hans) * 2} B），"
          f"跳过多音/无拼音 {skipped} 个")
    for probe in ("zhong", "zh", "ni", "hao"):
        if probe in groups:
            codes = sorted(groups[probe])
            print(f"  {probe}: {len(codes)} 字，首字 GBK=0x{codes[0]:04X}"
                  f"（{bytes([codes[0] >> 8, codes[0] & 0xFF]).decode('gb2312')}）")
    return 0


if __name__ == "__main__":
    sys.exit(main())
