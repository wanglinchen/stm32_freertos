#!/usr/bin/env python3
"""门禁自测：逐个破坏被测条件，确认对应断言真的会失败，再原样还原。

为什么需要这个：`verify` 的断言在"代码本来就正确"时永远是绿的，而**绿一次不能
证明它会在该报错时报错**（测试立刻通过等于没测到东西）。这里给每组断言造一个
最小破坏，要求它以非零退出并打出预期关键字，然后还原并复核。

五组（对应 `verify` 里能静默失败的静态/产物门禁）：

| 组 | 破坏方式 | 期望的失败信息 | 代价 |
|----|----------|----------------|------|
| flags | 不动文件，检查 `make -n -W <abs>/toolchain.make` 是否重编 | 必须打印 `CC` 行 | 秒级 |
| heap  | 从 `toolchain.make` 删 `-Wl,-u,_sbrk` | `_sbrk` 强制引用缺失 | 秒级（静态） |
| heap-bound | 把链接脚本的 `_Min_Heap_Size` 改大、不重链接 | 产物边界与脚本声明"已不一致" | 一次 build |
| bsp   | 注释掉 `main()` 里的 `fml_button_start()` | 产物里找不到 `board_key_init` | 一次 build + 一次恢复 build |
| fault | startup 的 `Default_Handler` 改回 `b Infinite_Loop` | 产物里的 `Default_Handler` 不跳 `UnhandledIRQ_Handler` | 一次 make + 一次恢复 make |

用法：

    python build/gate-selftest.py                 # 全部 5 组
    python build/gate-selftest.py --only bsp      # 只跑一组
    python build/gate-selftest.py --keep-going    # 某组失败也继续跑其余组

约定：
- 每组都先备份原始字节，`finally` 里写回并恢复 mtime，最后复核哈希与原文一致；
  即使中途异常（含 Ctrl+C）也不会把源码树留在破坏状态。
- 破坏后被改动过的文件 mtime 会被刷新，保证 make 真的重编，而不是拿旧 `.o` 假绿。
- 找不到 `make`（或 shell）时该组记为 SKIP，不算失败；其余组照常跑。
"""

from __future__ import annotations

import argparse
import contextlib
import hashlib
import os
import re
import shutil
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
CREATE_PROJECT = ROOT / "build" / "create-project.py"
TOOLCHAIN = ROOT / "toolchain.make"
MAIN_C = ROOT / "apl" / "src" / "main.c"
STARTUP_S = ROOT / "hal" / "chip" / "startup" / "src" / "startup_stm32f429xx.s"


def run(args: list[str], *, env: dict[str, str] | None = None) -> subprocess.CompletedProcess:
    """跑子进程，合并 stderr 便于匹配失败信息。"""
    return subprocess.run(
        args,
        cwd=ROOT,
        capture_output=True,
        text=True,
        encoding="utf-8",
        errors="replace",
        env=env,
    )


def create_project(*args: str) -> subprocess.CompletedProcess:
    return run([sys.executable, str(CREATE_PROJECT), *args])


def find_make() -> Path | None:
    """定位 make：优先 PATH，其次本机 MSYS 的常见位置（Windows 上 make 往往不在 PATH）。"""
    found = shutil.which("make")
    if found:
        return Path(found)
    if os.name == "nt":
        for candidate in (
            Path(r"D:\ExPrograms\DevelopTool\msys64\usr\bin\make.exe"),
            Path(r"C:\msys64\usr\bin\make.exe"),
        ):
            if candidate.is_file():
                return candidate
    return None


def make_env(make: Path) -> dict[str, str]:
    """make 规则里用了 mkdir/rm 之类，Windows 上必须把 MSYS 的 bin 放到 PATH 前面。"""
    env = dict(os.environ)
    msys_bin = make.parent
    env["PATH"] = str(msys_bin) + os.pathsep + env.get("PATH", "")
    return env


def msys_shell(make: Path) -> str | None:
    """MSYS 的 make 需要一个 POSIX shell 才能执行 mkdir/rm；Linux 上不需要指定。"""
    if os.name != "nt":
        return None
    shell = make.parent / "sh.exe"
    return str(shell) if shell.is_file() else None


def msys_path(path: Path) -> str:
    """把 D:/x/y 折成 /d/x/y。

    MSYS 版 make 会把依赖表里的 PROJECT_ROOT 归一成 POSIX 形式，所以 `-W` 的参数
    必须用同一形式；传 `D:/…` 时它不会匹配到任何目标，`-W` 就静默失效（本脚本
    第一次跑就是这样误报 FLAG_DEPS 失效的）。
    """
    as_posix = path.as_posix()
    if os.name != "nt":
        return as_posix
    drive, _, rest = as_posix.partition(":")
    return f"/{drive.lower()}{rest}"


@contextlib.contextmanager
def patched(path: Path, transform):
    """临时改写一个文本文件，退出时恢复字节与 mtime。"""
    original = path.read_bytes()
    text = original.decode("utf-8")
    changed = transform(text)
    if changed == text:
        raise RuntimeError(f"{path.name} 的破坏没有生效（锚点没匹配上）")
    path.write_bytes(changed.encode("utf-8"))
    try:
        yield
    finally:
        path.write_bytes(original)
        # 还原后 mtime 必须刷到当下：内容虽然回到原样，但破坏期间的 `.o` 是拿坏源码
        # 编出来的，mtime 若回退到旧值，make 会认为目标 up-to-date 而不重编，产物就
        # 永远停在破坏版本上（历史上 Copy-Item 还原踩过一次同样的坑）。
        os.utime(path, None)


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()[:12]


def expect_failure(result: subprocess.CompletedProcess, needle: str) -> str | None:
    """断言：退出码非 0，且输出里出现预期关键字。返回 None 表示通过。"""
    if result.returncode == 0:
        return f"预期失败，但退出码是 0（关键字 {needle!r} 没触发）"
    blob = result.stdout + result.stderr
    if needle not in blob:
        return f"退出码 {result.returncode} 正确，但输出里没有 {needle!r}"
    return None


def expect_success(result: subprocess.CompletedProcess) -> str | None:
    if result.returncode != 0:
        blob = (result.stdout + result.stderr).strip().splitlines()
        tail = "\n".join(blob[-6:])
        return f"预期成功，实际退出码 {result.returncode}：\n{tail}"
    return None


def case_flags() -> tuple[str, str]:
    """flags 依赖：toolchain.make 变过一次，所有对象必须重编（FLAG_DEPS）。

    这里不改文件：`make -n -W <abs>/toolchain.make` 就是把依赖声明成已过期，
    只看它是否打印 `CC` 行。打印 = 会重编；不打印 = 改了编译参数却拿旧对象链接。
    """
    make = find_make()
    if make is None:
        return "SKIP", "找不到 make"
    env = make_env(make)
    args = [str(make), "-C", str(ROOT / "osl" / "src"), "-n",
            "-W", msys_path(ROOT / "toolchain.make"), "all"]
    shell = msys_shell(make)
    if shell:
        args.append(f"SHELL={shell}")
    result = run(args, env=env)
    blob = result.stdout + result.stderr
    if result.returncode != 0:
        return "FAIL", f"make 本身失败（{result.returncode}）：\n{blob[-400:]}"
    cc_lines = [line for line in blob.splitlines() if 'echo "CC ' in line]
    if not cc_lines:
        return "FAIL", "toolchain.make 被声明为已过期，但没有出现任何 CC 行（FLAG_DEPS 失效）"
    return "PASS", f"{len(cc_lines)} 个对象被要求重编"


def case_heap() -> tuple[str, str]:
    """堆边界：`-Wl,-u,_sbrk` 缺失时 verify 必须报错（否则 -lnosys 空桩会静默接管）。"""
    before = digest(TOOLCHAIN)
    with patched(TOOLCHAIN, lambda t: t.replace(" -Wl,-u,_sbrk", "")):
        result = create_project("verify")
        problem = expect_failure(result, "_sbrk")
    if before != digest(TOOLCHAIN):
        return "ERROR", "toolchain.make 没有还原到原始字节"
    if problem:
        return "FAIL", problem
    return "PASS", "删掉 -Wl,-u,_sbrk 后 verify 报错，并已还原"


def case_bsp() -> tuple[str, str]:
    """BSP 接入：注释掉 fml_button_start() 后，产物里必须找不到 board_key_init。"""
    before = digest(MAIN_C)
    with patched(MAIN_C, lambda t: t.replace(
        "    fml_button_start();",
        "    /* gate-selftest: fml_button_start(); */",
    )):
        result = create_project("build")
        problem = expect_failure(result, "board_key_init")
    if before != digest(MAIN_C):
        return "ERROR", "main.c 没有还原到原始字节"
    # 破坏期间的镜像已经是缺 BSP 的版本，必须重建回正确产物再收工。
    restore = create_project("build")
    if problem:
        return "FAIL", problem
    if restore.returncode != 0:
        return "FAIL", "破坏已还原，但恢复构建失败（产物可能仍缺 BSP）"
    return "PASS", "去掉 fml_button_start() 后产物断言报错，已还原并重建"


def case_fault() -> tuple[str, str]:
    """未实现中断：startup 改回自旋后，产物断言必须报 Default_Handler 不跳 handler。

    走 make 而不是 `build`：`build` 内部的 `update` 有静态检查，会在编译前就拦下，
    那样就测不到"产物级"那条断言了。这里先编出坏镜像，再让 verify 看产物。
    """
    make = find_make()
    if make is None:
        return "SKIP", "找不到 make"
    env = make_env(make)
    shell = msys_shell(make)
    before = digest(STARTUP_S)

    def make_all() -> subprocess.CompletedProcess:
        args = [str(make), "-C", str(ROOT), "-j4", "all"]
        if shell:
            args.append(f"SHELL={shell}")
        return run(args, env=env)

    with patched(STARTUP_S, lambda t: t.replace(
        "  b  UnhandledIRQ_Handler", "  b  Infinite_Loop"
    )):
        built = make_all()
        if built.returncode != 0:
            return "FAIL", f"破坏后 make 失败（{built.returncode}）：\n{(built.stdout + built.stderr)[-400:]}"
        checked = create_project("verify", "--artifacts")
        problem = expect_failure(checked, "Default_Handler")
    if before != digest(STARTUP_S):
        return "ERROR", "startup 没有还原到原始字节"
    restore = make_all()
    if problem:
        return "FAIL", problem
    if restore.returncode != 0:
        return "FAIL", "破坏已还原，但恢复 make 失败（镜像可能仍是自旋版本）"
    verify_again = create_project("verify", "--artifacts")
    problem_again = expect_success(verify_again)
    if problem_again:
        return "FAIL", f"恢复后自检仍不通过：{problem_again}"
    return "PASS", "startup 改回自旋后产物断言报错，已还原并重建"


def case_heap_bound() -> tuple[str, str]:
    """链接脚本与产物边界：改了 _Min_Heap_Size 却不重链接，断言必须报不一致。

    这条和 `-u _sbrk` 是两回事：前者管"谁提供 _sbrk"，这里管"堆到底多大"。
    `_Min_Heap_Size` 是布局里的魔数，只有拿 elf 里的 `__heap_start__`/`__heap_end__`
    跟它对比才能确认两者没跑偏；单看任何一方都是绿的。这里故意只改脚本文本、
    不重新链接，于是产物边界仍是旧值，断言必须看出来。
    """
    script = ROOT / "hal" / "chip" / "linker" / "stm32f429igx_flash.ld"
    if not script.is_file():
        return "SKIP", f"找不到 {script.name}"
    before = digest(script)

    def bump(text: str) -> str:
        m = re.search(r"_Min_Heap_Size\s*=\s*(0x[0-9a-fA-F]+)\s*;", text)
        if not m:
            raise RuntimeError("链接脚本里没有 _Min_Heap_Size 声明")
        value = int(m.group(1), 16)
        return text.replace(m.group(0), f"_Min_Heap_Size = 0x{value * 2:X};", 1)

    with patched(script, bump):
        result = create_project("verify", "--artifacts")
        problem = expect_failure(result, "已不一致")
    if before != digest(script):
        return "ERROR", f"{script.name} 没有还原到原始字节"
    if problem:
        return "FAIL", problem
    # 脚本 mtime 变了，make 会重链接一次；这里主动重建，免得留下"文本已改回、
    # 产物却按旧脚本链接"的中间状态。
    restore = create_project("build")
    if restore.returncode != 0:
        return "FAIL", "破坏已还原，但恢复构建失败（产物可能仍按旧布局链接）"
    return "PASS", "把 _Min_Heap_Size 改大而不重链接后边界断言报错，已还原并重建"


CASES = {
    "flags": case_flags,
    "heap": case_heap,
    "heap-bound": case_heap_bound,
    "bsp": case_bsp,
    "fault": case_fault,
}


def main() -> int:
    parser = argparse.ArgumentParser(description="给 verify 的四组断言做红测")
    parser.add_argument("--only", choices=sorted(CASES), help="只跑指定的一组")
    parser.add_argument("--keep-going", action="store_true", help="某组失败后继续跑其余组")
    args = parser.parse_args()

    selected = [args.only] if args.only else list(CASES)
    results: list[tuple[str, str, str]] = []
    started = time.time()

    for name in selected:
        print(f"== {name} ==", flush=True)
        try:
            status, detail = CASES[name]()
        except Exception as exc:  # 备份/还原以外的意外：也当作失败，但先让 finally 完成还原
            status, detail = "ERROR", f"{type(exc).__name__}: {exc}"
        results.append((name, status, detail))
        print(f"{status}: {detail}", flush=True)

    failed = [r for r in results if r[1] in ("FAIL", "ERROR")]
    elapsed = time.time() - started
    print(f"\n{len(results)} 组，{elapsed:.1f} s："
          f"{sum(1 for r in results if r[1] == 'PASS')} PASS / "
          f"{sum(1 for r in results if r[1] == 'SKIP')} SKIP / "
          f"{len(failed)} FAIL", flush=True)
    for name, status, detail in failed:
        print(f"  - {name}: {status} {detail}", flush=True)
    return 0 if not failed else 1


if __name__ == "__main__":
    sys.exit(main())
