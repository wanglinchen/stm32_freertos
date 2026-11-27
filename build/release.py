"""把最新一次构建的四件套收进仓库根 `release/<日期>/<档位>/`。

用法：`python build/release.py [--date YYYY-MM-DD]`

行为：
  1. 对 userdebug / user 两档，各取 `out/<variant>/release/<时间戳>/` 里**最新的那个目录**；
  2. 把 `stm32-freertos.elf/.hex/.bin/.map` 拷到 `release/<日期>/<variant>/`；
  3. 在 `release/<日期>/RELEASE.txt` 记录来源目录、大小与拷贝时间。

`release/` 是本地存档（已在 `.gitignore`），不入库；判据是四件套齐全且非空，
任一档缺文件就返回 1 并打印缺了哪个，方便定时任务里直接看退出码。
"""
from __future__ import annotations

import datetime as _dt
import shutil
import sys
from pathlib import Path

_ROOT = Path(__file__).resolve().parent.parent
_VARIANTS = ("userdebug", "user")
_FILES = (
    "stm32-freertos.elf",
    "stm32-freertos.hex",
    "stm32-freertos.bin",
    "stm32-freertos.map",
)


def _latest_release_dir(variant: str) -> Path | None:
    """`out/<variant>/release/` 下按名字（时间戳）排序取最后一个。"""
    root = _ROOT / "out" / variant / "release"
    if not root.is_dir():
        return None
    dirs = sorted(p for p in root.iterdir() if p.is_dir())
    return dirs[-1] if dirs else None


def main(argv: list[str]) -> int:
    date = _dt.date.today().isoformat()
    if len(argv) >= 2 and argv[0] == "--date":
        date = argv[1]

    lines: list[str] = []
    problems: list[str] = []

    for variant in _VARIANTS:
        src = _latest_release_dir(variant)
        if src is None:
            problems.append(f"{variant}: 没有 out/{variant}/release/<时间戳>/，先跑 build")
            continue
        missing = [n for n in _FILES if not (src / n).is_file() or (src / n).stat().st_size == 0]
        if missing:
            problems.append(f"{variant}: {src.name} 里缺 {', '.join(missing)}")
            continue

        dst = _ROOT / "release" / date / variant
        dst.mkdir(parents=True, exist_ok=True)
        for name in _FILES:
            shutil.copy2(src / name, dst / name)
            size = (dst / name).stat().st_size
            print(f"[release] {variant}/{name} -> release/{date}/{variant}/{name} ({size} B)")
            lines.append(f"{variant}/{name}\t{size}\t{src.name}")

    if problems:
        for p in problems:
            print(f"[release] FAIL {p}", file=sys.stderr)
        return 1

    note = _ROOT / "release" / date / "RELEASE.txt"
    note.write_text(
        f"date: {date}\ncopied_at: {_dt.datetime.now().isoformat(timespec='seconds')}\n"
        f"source: out/<variant>/release/<时间戳>/\n\n" + "\n".join(lines) + "\n",
        encoding="utf-8",
    )
    print(f"[release] copy done -> release/{date}/（RELEASE.txt 已写）")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
