import io, re, sys, pathlib

SRC = r"C:\Users\Cwl\AppData\Local\Temp\p6_lcd_ex.utf8.txt"
DST = r"D:\Users\Local\stm32-freertos\bsl\src\board_nt35510.c"

text = io.open(SRC, encoding="utf-8", errors="replace").read()
i = text.find("void lcd_ex_nt35510_reginit")
if i < 0:
    sys.exit("找不到 lcd_ex_nt35510_reginit")
j = text.find("\n}", i)
if j < 0:
    sys.exit("函数体没有收尾")
body = text[i:j]

pairs = re.findall(r"lcd_write_reg\(\s*0[xX]([0-9A-Fa-f]+)\s*,\s*0[xX]([0-9A-Fa-f]+)\s*\)", body)

others = []
for line in body.splitlines():
    s = line.strip()
    if not s or s.startswith("//") or s.startswith("/*") or s.startswith("void lcd_ex"):
        continue
    if "lcd_write_reg(" in s:
        continue
    others.append(s)

print("抽取条目数:", len(pairs))
print("非 write_reg 语句:", others)

lines = []
for idx, (a, b) in enumerate(pairs):
    lines.append("    {0x%04XU, 0x%04XU}, /* %03d */" % (int(a, 16), int(b, 16), idx + 1))

tbl = "\n".join(lines)

c = io.open(DST, encoding="utf-8").read()
if "/* @@TABLE@@ */" not in c:
    sys.exit("目标文件里没有占位符")
c = c.replace("    /* @@TABLE@@ */", tbl)
io.open(DST, "w", encoding="utf-8", newline="\n").write(c)
print("已写入:", DST, "字节数:", len(c))
