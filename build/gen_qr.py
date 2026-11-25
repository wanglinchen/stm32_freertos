#!/usr/bin/env python3
"""把参考工程的 QR_ENCODE（正点原子 Middlewares/QR_ENCODE）生成 fml/src/fml_qr.c。

为什么要生成而不是手抄：那份源码 45 KB / 1200 行，全是位流与掩码运算，
手抄一遍等于给"抄错一位"开门。脚本只做**机械改写**，不做算法改动：

  * 三个文件拼成一个编译单元（qr_encode.h 的宏与结构 + qr_data.h 的表 +
    qr_encode.c 的函数体），这样表与函数同属一个 TU，不新增表外头文件；
  * 去掉参考工程的 `#include "./SYSTEM/sys/sys.h"`，换成 <stdint.h>/<string.h>；
  * 所有编码函数加 `static`：本工程不导出 `EncodeData` 这类通用名字，
    对外只有 fml_qr_* 四个接口；
  * `_qr_encode *qrx` 的堆分配改为**文件内 static 结构**（参考工程用
    `gui_memin_malloc`，本工程没有能放 40 KB 的通用堆；bss 里放着更确定）。

用法：
    python build/gen_qr.py [源目录]
源目录默认取 C:\\Users\\Cwl\\AppData\\Local\\Temp\\p6e2_qr（UTF-8 副本）。
"""
import pathlib
import re
import sys

SRC = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else
                   r"C:\Users\Cwl\AppData\Local\Temp\p6e2_qr")
ROOT = pathlib.Path(__file__).resolve().parents[1]

FUNCS = [
    "EncodeData", "GetEncodeVersion", "EncodeSourceData", "GetBitLength",
    "SetBitStream", "IsNumeralData", "IsAlphabetData", "IsKanjiData",
    "IsChineseData", "AlphabetToBinaly", "KanjiToBinaly", "ChineseToBinaly",
    "GetRSCodeu32", "FormatModule", "SetFunctionModule", "SetFinderPattern",
    "SetAlignmentPattern", "SetVersionPattern", "SetCodeu32Pattern",
    "SetMaskingPattern", "SetFormatInfoPattern", "CountPenalty",
]


def strip_includes(text):
    return "\n".join(l for l in text.splitlines() if not l.lstrip().startswith("#include"))


def patch_port(text):
    """参考工程与本工程的编译档位差异，只改这三处（都不触及算法）。

    1. `ncSource > 0 ? ncSource : strlen(...)`：?: 两侧一个 int 一个 size_t，
       本工程 -Wsign-compare 当错误，补 (int) 转换。
    2. IsChineseData / ChineseToBinaly：参考工程的中文模式在 EncodeSourceData
       里没有调用点（只认数字/字母/8bit/汉字 Kanji），两个函数编进来就是
       unused-function；算法保留、加 unused 属性，不删。
    """
    text = text.replace("ncSource > 0 ? ncSource : strlen(lpsSource)",
                        "ncSource > 0 ? ncSource : (int)strlen(lpsSource)")
    return text


def header_macros(text):
    """取 qr_encode.h 里 guard 之间、但去掉 extern 函数声明那一段。"""
    body = text.split("#ifndef __QR_ENCODE_H", 1)[1]
    body = body.split("#endif", 1)[0]
    lines = body.splitlines()
    out = []
    skipping = False
    for line in lines:
        s = line.strip()
        if s.startswith("uint8_t EncodeData"):
            skipping = True
        if skipping:
            # extern 声明段以 CountPenalty 的声明收尾
            if s.startswith("int CountPenalty"):
                skipping = False
            continue
        if s.startswith('extern _qr_encode *qrx'):
            continue
        if s.startswith('#define __QR_ENCODE_H'):
            continue
        out.append(line)
    return "\n".join(out)


def data_tables(text):
    """取 qr_data.h 的表体，砍掉自己的 guard 与 NULL 宏块。"""
    text = strip_includes(text)
    text = text.replace("#ifndef __QR_DATA_H", "").replace("#define __QR_DATA_H", "")
    # 末尾那个 #endif 是 guard 的，直接去掉；文件里没有别的预处理分支
    text = text.splitlines()
    while text and text[-1].strip() == "":
        text.pop()
    if text and text[-1].strip() == "#endif":
        text.pop()
    return "\n".join(text)


def make_static(text):
    for name in FUNCS:
        text = re.sub(rf"^(?!(?:static|extern)\b)([A-Za-z_][A-Za-z0-9_ \t\*]*?\b{name}\s*\()",
                      r"static \1", text, flags=re.M)
    return text


def main():
    h = strip_includes((SRC / "qr_encode.h.utf8.txt").read_text(encoding="utf-8"))
    c = strip_includes((SRC / "qr_encode.c.utf8.txt").read_text(encoding="utf-8"))
    d = data_tables((SRC / "qr_data.h.utf8.txt").read_text(encoding="utf-8"))

    # qrx 由堆改为文件内 static（40 KB 的结构，堆上放不下也不该放）
    c = c.replace("_qr_encode *qrx;        /* 二维码编码,各种参数结构体 */",                  "/* 参考工程用 gui_memin_malloc 申请这块 40 KB 的结构；本工程没有能放它的\n"
                  " * 通用堆，改为文件内 static（bss），生命周期与固件相同。 */\n"
                  "static _qr_encode s_qr;\n"
                  "static _qr_encode *const qrx = &s_qr;")
    c = patch_port(c)
    c = make_static(c)
    # 这两个函数在参考工程里就没有调用点（中文模式只有 Kanji 分支被用）。
    # 算法照抄、不删；加 unused 属性让 -Werror=unused-function 闭口。
    c = c.replace("static uint8_t IsChineseData(",
                  "static __attribute__((unused)) uint8_t IsChineseData(")
    c = c.replace("static uint32_t ChineseToBinaly(",
                  "static __attribute__((unused)) uint32_t ChineseToBinaly(")

    protos = "\n".join(
        f"static {ret} {name}({args});"
        for ret, name, args in [
            ("uint8_t", "EncodeData", "char *lpsSource"),
            ("int", "GetEncodeVersion", "int nVersion, char *lpsSource, int ncLength"),
            ("int", "EncodeSourceData", "char *lpsSource, int ncLength, int nVerGroup"),
            ("int", "GetBitLength", "uint8_t nMode, int ncData, int nVerGroup"),
            ("int", "SetBitStream", "int nIndex, uint32_t wData, int ncData"),
            ("uint8_t", "IsNumeralData", "unsigned char c"),
            ("uint8_t", "IsAlphabetData", "unsigned char c"),
            ("uint8_t", "IsKanjiData", "unsigned char c1, unsigned char c2"),
            ("uint8_t", "IsChineseData", "unsigned char c1, unsigned char c2"),
            ("uint8_t", "AlphabetToBinaly", "unsigned char c"),
            ("uint32_t", "KanjiToBinaly", "uint32_t wc"),
            ("uint32_t", "ChineseToBinaly", "uint32_t wc"),
            ("void", "GetRSCodeu32", "uint8_t *lpbyRSWork, int ncDataCodeu32, int ncRSCodeu32"),
            ("void", "FormatModule", "void"),
            ("void", "SetFunctionModule", "void"),
            ("void", "SetFinderPattern", "int x, int y"),
            ("void", "SetAlignmentPattern", "int x, int y"),
            ("void", "SetVersionPattern", "void"),
            ("void", "SetCodeu32Pattern", "void"),
            ("void", "SetMaskingPattern", "int nPatternNo"),
            ("void", "SetFormatInfoPattern", "int nPatternNo"),
            ("int", "CountPenalty", "void"),
        ])

    api = """
/*
 * 对外接口（唯一导出的四个）：编码、取边长、取版本、取模块。
 * 参考工程把 `qrx` 直接给应用读（qrplay.c 里 `qrx->m_byModuleData[i][j]`），
 * 本工程把结构留在编译单元里 —— 应用拿不到 40 KB 的中间态，也就不会
 * 在编码失败时读到上一轮的残矩阵。
 */
int fml_qr_encode(const char *text)
{
    if (text == NULL) {
        return -1;
    }
    /* 每次全清：EncodeData 只写"这次用到的"字段，残留的 m_nVersion /
     * m_ncDataCodeu32Bit 会让第二次编码的版本与位流和第一次不同。 */
    (void)memset(&s_qr, 0, sizeof(s_qr));
    if (EncodeData((char *)text) != QR_TRUE) {
        return -2;
    }
    return 0;
}

unsigned fml_qr_size(void)
{
    return (unsigned)s_qr.m_nSymbleSize;
}

unsigned fml_qr_version(void)
{
    return (unsigned)s_qr.m_nVersion;
}

int fml_qr_module(unsigned x, unsigned y)
{
    unsigned size = (unsigned)s_qr.m_nSymbleSize;

    if ((x >= size) || (y >= size)) {
        return 0;
    }
    return (s_qr.m_byModuleData[y][x] != 0U) ? 1 : 0;
}

unsigned fml_qr_dark(void)
{
    unsigned x;
    unsigned y;
    unsigned n = 0U;
    unsigned size = (unsigned)s_qr.m_nSymbleSize;

    for (y = 0U; y < size; y++) {
        for (x = 0U; x < size; x++) {
            if (fml_qr_module(x, y) != 0) {
                n++;
            }
        }
    }
    return n;
}
"""

    out = []
    out.append("""/*
 * fml_qr.c —— 二维码编码（P6e-2）。
 *
 * **本文件由 build/gen_qr.py 生成，不要手改**：源是参考工程（正点原子 实验63）的
 * Middlewares/QR_ENCODE（qr_encode.h / qr_encode.c / qr_data.h，V1.1 2022-06-02，
 * 其自身移植自网友"旮旯旭"的二维码生成工程）。生成器只做机械改写：
 *   - 三个文件拼成一个编译单元，去掉 "./SYSTEM/sys/sys.h"；
 *   - 22 个编码函数全部 static，对外只留 fml_qr_encode/size/version/module/dark；
 *   - `_qr_encode *qrx` 的堆分配改为文件内 static（40 KB 结构的 bss 比堆确定）。
 * 算法一行未改：改算法请改源仓再重跑脚本，否则下次生成就把手改覆盖了。
 *
 * 编码参数与参考工程一致：纠错等级 L（7%）、掩码 0、版本按内容自动选。
 */
#include <stdint.h>
#include <string.h>

#include "fml_qr.h"

""")
    out.append(header_macros(h))
    out.append("\n/* ---- 静态原型（参考工程里在头文件里 extern，这里改 static） ---- */\n")
    out.append(protos + "\n\n")
    out.append("/* ---- 数据表（qr_data.h 的表体） ---- */\n")
    # 参考工程的版本表用"扁平初始化"（嵌套结构数组写成一条数字串），
    # GCC 会逐层报 -Wmissing-braces 而本工程把告警当错误。表是逐字节照抄的，
    # 与其把 500 行拆成层套括号（拆错一处就是表错），不如在表这一段局部关掉
    # 这条告警 —— 作用域仅限表体，函数体仍然按全档告警编译。
    out.append("#pragma GCC diagnostic push\n"
               "#pragma GCC diagnostic ignored \"-Wmissing-braces\"\n")
    out.append(d + "\n")
    out.append("#pragma GCC diagnostic pop\n\n")
    out.append("/* ---- 编码函数体（qr_encode.c，全部 static） ---- */\n")
    out.append(c + "\n")
    out.append(api)

    dst_c = ROOT / "fml" / "src" / "fml_qr.c"
    dst_c.write_text("\n".join(out), encoding="utf-8", newline="\n")
    print(f"wrote {dst_c} ({dst_c.stat().st_size} B)")


if __name__ == "__main__":
    main()
