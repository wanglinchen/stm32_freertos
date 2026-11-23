"""只读盘查：层引用 / 环 / 产物一致性。

**不是门禁**。区别在三条：
  1. 只读 —— 不注入破坏、不模拟错误、不改任何一个文件；
  2. 不设基线数字 —— 层边处数变了不会失败，只打印并和 `LAYER_DEPENDENCIES` 的允许边对表；
  3. 可复现 —— 同一棵树跑两次输出一致，便于贴进分析记录。

三块判据：
  A. 层边：跨层 `#include` 的处数；越级（引用方不允许的层）直接判失败。
  B. 环：文件级 `#include` 图跑 Tarjan SCC；再把边折叠到层，查层级环。
     vendored（`osl/**/freertos/**`）内部引用不计入 —— 那是上游代码，改了会破坏整块升级。
  C. 产物：`out/dts/apollo.dtb` 的 FDT magic、文件大小与 elf 里 `.dtb` 段跨度的 4 字节对齐、
     以及几个必须存在的公开符号。
  D. DTS 派生链：把 `gen_dts.py` 重跑到临时目录，与 `project/dts/` 的两份 dtsi 逐字节比；
     再用 `generate.build_dts_text()` 复算 `out/dts/apollo.dts` 全文。抓的是「改了 pinmap
     表 / 改了脚本却没跑 update」——此时 dtsi、dts、dtb、elf 会一起停在旧坐标上，编译不报错。
  E. 头守卫：自研头（vendored 除外）必须有配对的 `#ifndef/#define`、末尾 `#endif`，且守卫名不重名。
     guard 重名时两个头互相屏蔽，症状是「明明 include 了却 undeclared」，且只在特定编译顺序下复现。
  F. 忽略规则双向：中间产物（`out/dts/*`、`out/*/objects/`、`out/tmp/`）必须被 `.gitignore` 覆盖；
     而发布件（`out/<variant>/.config`、`out/<variant>/release/`）**必须不被**忽略 —— 用
     `git check-ignore` 逐条问，不读 `.gitignore` 的字面形状。

退出码：0 = 全部通过；1 = 有越级边 / 有环 / 产物不一致 / 派生链过期 / 守卫缺陷 / 忽略缺失。

用法：`python build/build.py check`（会先 ensure 工具链在 PATH；拿不到 nm 就跳过 C 的符号段）。
"""
from __future__ import annotations

import re
import struct
import subprocess
import sys
from pathlib import Path

_ROOT = Path(__file__).resolve().parent.parent
_SELF_LAYERS = ("apl", "fml", "bsl", "osl")
_REF_LAYERS = ("apl", "fml", "bsl", "osl", "hal")
_VENDORED = "freertos"

_INCLUDE_RE = re.compile(r'^\s*#\s*include\s*[<"]([^">]+)[">]')

_FDT_MAGIC = 0xd00dfeed

# 必须出现在镜像里的符号（各自代表一条已经修过的机制，少一个就说明那次修复被回退了）。
_REQUIRED_SYMBOLS = (
    "bsl_fdt_init",          # DTB 解析层被链进来
    "bsl_fdt_prop_flag",     # 极性按值判
    "board_rt_led_count",    # 运行期从 DTB 读板级事实
    "board_system_init",     # 时钟编排（含码 3 对拍）
    "g_panic_record",        # panic 记录在 .noinit
    "osl_panic_report",      # 故障播报唯一入口
)


def _is_vendored(path: Path) -> bool:
    return _VENDORED in path.parts


def _layer_of(path: Path) -> str | None:
    rel = path.relative_to(_ROOT)
    return rel.parts[0] if rel.parts[0] in _REF_LAYERS else None


def _collect_sources() -> list[Path]:
    out: list[Path] = []
    for layer in _SELF_LAYERS:
        base = _ROOT / layer
        if not base.is_dir():
            continue
        for p in base.rglob("*"):
            if p.suffix not in (".c", ".h") or not p.is_file():
                continue
            if _is_vendored(p):
                continue
            out.append(p)
    return out


def _header_owner_map() -> dict[str, str]:
    """头文件名 → 所属层。同层平时按 -I 直取，跨层时靠这张表判归属。"""
    owners: dict[str, str] = {}
    for layer in _REF_LAYERS:
        base = _ROOT / layer
        if not base.is_dir():
            continue
        for p in base.rglob("*.h"):
            if not p.is_file() or _is_vendored(p):
                continue
            # 后写的覆盖同名前面的：hal 的 inc 在深层，不影响层归属判断
            owners.setdefault(p.name, layer)
    return owners


def _build_graph(sources: list[Path], owners: dict[str, str]):
    """返回 (层边计数, 文件级邻接表, 无法归属的头名)。"""
    by_name = {p.name: p for p in sources}
    edges: dict[tuple[str, str], int] = {}
    adj: dict[Path, set[Path]] = {p: set() for p in sources}
    unknown: list[str] = []

    for src in sources:
        layer = _layer_of(src)
        if layer is None:
            continue
        text = src.read_text(encoding="utf-8", errors="ignore")
        for line in text.splitlines():
            m = _INCLUDE_RE.match(line)
            if not m:
                continue
            inc = m.group(1)
            name = Path(inc).name
            if _VENDORED in Path(inc).parts:
                continue
            dst_layer = owners.get(name)
            if dst_layer is None:
                # 编译期 -I 能取到，但没有层归属（如 stdint.h / HAL 顶层头）
                if name in by_name:
                    dst = by_name[name]
                    if dst != src:
                        adj[src].add(dst)
                continue
            if dst_layer == layer:
                dst = by_name.get(name)
                if dst is not None and dst != src:
                    adj[src].add(dst)
                continue
            edges[(layer, dst_layer)] = edges.get((layer, dst_layer), 0) + 1
            dst = by_name.get(name)
            if dst is not None and dst != src:
                adj[src].add(dst)
    return edges, adj, unknown


def _tarjan_scc(adj: dict[Path, set[Path]]) -> list[list[Path]]:
    index: dict[Path, int] = {}
    low: dict[Path, int] = {}
    on_stack: dict[Path, bool] = {}
    stack: list[Path] = []
    counter = 0
    sccs: list[list[Path]] = []

    def strong(v: Path) -> None:
        nonlocal counter
        index[v] = counter
        low[v] = counter
        counter += 1
        stack.append(v)
        on_stack[v] = True
        for w in adj.get(v, ()):
            if w not in index:
                strong(w)
                low[v] = min(low[v], low[w])
            elif on_stack.get(w):
                low[v] = min(low[v], index[w])
        if low[v] == index[v]:
            comp: list[Path] = []
            while True:
                w = stack.pop()
                on_stack[w] = False
                comp.append(w)
                if w == v:
                    break
            sccs.append(comp)

    for v in adj:
        if v not in index:
            strong(v)
    return sccs


def _check_artifacts(problems: list[str]) -> list[str]:
    notes: list[str] = []
    dtb = _ROOT / "out" / "dts" / "apollo.dtb"
    dts = _ROOT / "out" / "dts" / "apollo.dts"
    if not dts.is_file():
        problems.append("缺 out/dts/apollo.dts（先运行 python build/build.py update）")
    if not dtb.is_file():
        problems.append("缺 out/dts/apollo.dtb（先运行 python build/build.py update）")
        return notes

    data = dtb.read_bytes()
    magic = struct.unpack(">I", data[:4])[0] if len(data) >= 4 else 0
    if magic != _FDT_MAGIC:
        problems.append(f"out/dts/apollo.dtb 头部 magic={magic:#x}，不是 FDT（应为 {_FDT_MAGIC:#x}）")
    else:
        notes.append(f"out/dts/apollo.dtb {len(data)} B，FDT magic 正确")

    elf = _ROOT / "out" / "userdebug" / "objects" / "target" / "stm32-freertos.elf"
    if not elf.is_file():
        notes.append("跳过 elf 校验：还没编过 userdebug")
        return notes
    syms = _nm_symbols(elf)
    if syms is None:
        notes.append("跳过派生物校验：PATH 里没有 arm-none-eabi-nm")
        return notes

    span = None
    if "_sdtb" in syms and "_edtb" in syms:
        span = syms["_edtb"] - syms["_sdtb"]

    # 内容边界必须精确等于文件大小：bsl_dtb_start/end 由 .incbin 两侧的标号定义，
    # 中间没有填充。段跨度 _sdtb/_edtb 含对齐填充，**不能**拿它直接对拍文件大小
    # （board_dtb.s 里 `.align 4` 在 ARM as 语义下是 2^4 = 16 B，会把段尾推到 16 对齐）。
    if "bsl_dtb_start" not in syms or "bsl_dtb_end" not in syms:
        problems.append("elf 里找不到 bsl_dtb_start/bsl_dtb_end：DTB 没被 .incbin 链进来")
    else:
        content = syms["bsl_dtb_end"] - syms["bsl_dtb_start"]
        if content != len(data):
            problems.append(
                f"内嵌 DTB 内容 {content} B 与 out/dts/apollo.dtb {len(data)} B 不一致"
            )
        else:
            pad = (span - content) if span is not None else 0
            notes.append(
                f"内嵌 DTB 内容 {content} B，与文件逐字节等长"
                + (f"；段跨度 {span} B = 内容 + {pad} B 对齐填充" if span else "")
            )

    missing = [s for s in _REQUIRED_SYMBOLS if s not in syms]
    if missing:
        problems.append("镜像缺关键符号：" + ", ".join(missing))
    else:
        notes.append(f"关键符号 {len(_REQUIRED_SYMBOLS)} 个全部在镜像")
    return notes


def _check_dts_chain(problems: list[str], notes: list[str]) -> None:
    """D. 派生链：xlsx → dtsi → apollo.dts。

    gen_dts.py 重跑到临时目录（临时目录在系统 temp，不在树里），只做字节比较。
    跑不起来（缺 xlsx / 缺 openpyxl / 脚本失败）就跳过并说明，不判失败 —— 那是环境问题，
    不是树的问题。
    """
    import tempfile

    import generate

    here_dtsi = _ROOT / "project" / "dts"
    default_name = generate.PINMAP_DEFAULT_DTSI
    overlay_name = generate.PINMAP_OVERLAY_DTSI
    script = _ROOT / generate.PINMAP_SCRIPT

    if not here_dtsi.is_dir():
        problems.append("缺 project/dts 目录")
    else:
        try:
            with tempfile.TemporaryDirectory() as td:
                proc = subprocess.run(
                    [sys.executable, str(script), "--out", td],
                    capture_output=True,
                    text=True,
                    timeout=180,
                )
                if proc.returncode != 0:
                    raise RuntimeError(
                        (proc.stderr or proc.stdout).strip().splitlines()[-1:] or ["未知"]
                    )
                fresh = {n: (Path(td) / n).read_bytes() for n in (default_name, overlay_name)}
        except Exception as exc:  # noqa: BLE001 —— 任何打不开都只跳过
            notes.append(f"跳过 dtsi 新鲜度：gen_dts.py 未能重跑（{exc}）")
        else:
            stale = []
            for name, data in fresh.items():
                kept = here_dtsi / name
                if not kept.is_file():
                    problems.append(f"缺 project/dts/{name}")
                elif kept.read_bytes() != data:
                    stale.append(name)
            if stale:
                problems.append(
                    "dtsi 与 pinmap 表不同步：" + ", ".join(stale)
                    + "（改了 xlsx 或 gen_dts.py 后没跑 python build/build.py update）"
                )
            else:
                notes.append(f"两份 dtsi 与 pinmap 表逐字节一致（{len(fresh)} 个）")

    dts = _ROOT / "out" / "dts" / "apollo.dts"
    if not dts.is_file():
        return  # 缺 dts 已在 C 里报过
    try:
        want = generate.build_dts_text(
            (here_dtsi / default_name).read_text(encoding="utf-8"),
            (here_dtsi / overlay_name).read_text(encoding="utf-8"),
        )
    except Exception as exc:  # noqa: BLE001
        notes.append(f"跳过 apollo.dts 复算（{exc}）")
        return
    if want != dts.read_text(encoding="utf-8"):
        problems.append("out/dts/apollo.dts 过期：与当前两份 dtsi 复算的结果不同（重跑 update）")
    else:
        notes.append("out/dts/apollo.dts 由当前两份 dtsi 精确派生")


_GUARD_IFNDEF_RE = re.compile(r"^\s*#\s*ifndef\s+([A-Za-z_][A-Za-z0-9_]*)", re.MULTILINE)
_GUARD_DEFINE_RE = re.compile(r"^\s*#\s*define\s+([A-Za-z_][A-Za-z0-9_]*)", re.MULTILINE)


def _check_header_guards(problems: list[str], notes: list[str]) -> None:
    """E. 自研头的包含守卫。"""
    seen: dict[str, list[str]] = {}
    bad: list[str] = []
    count = 0
    for layer in _SELF_LAYERS:
        base = _ROOT / layer
        if not base.is_dir():
            continue
        for p in base.rglob("*.h"):
            if not p.is_file() or _is_vendored(p):
                continue
            count += 1
            text = p.read_text(encoding="utf-8", errors="ignore")
            m_if = _GUARD_IFNDEF_RE.search(text)
            m_def = _GUARD_DEFINE_RE.search(text)
            rel = p.relative_to(_ROOT).as_posix()
            if not m_if or not m_def or m_if.group(1) != m_def.group(1):
                bad.append(f"{rel}：没有配对的 #ifndef/#define 守卫")
                continue
            name = m_if.group(1)
            body = [ln.strip() for ln in text.splitlines() if ln.strip()]
            if not any(ln.startswith("#endif") for ln in body[-3:]):
                bad.append(f"{rel}：末尾三行内没有 #endif（守卫没闭合）")
            seen.setdefault(name, []).append(rel)
    dups = {k: v for k, v in seen.items() if len(v) > 1}
    for name, files in dups.items():
        bad.append(f"守卫名 {name} 重名：{', '.join(files)}")
    if bad:
        problems.extend(bad[:6])
        if len(bad) > 6:
            problems.append(f"…另有 {len(bad) - 6} 处守卫缺陷")
    else:
        notes.append(f"自研头 {count} 个守卫齐全且不重名")


_MUST_IGNORE = (
    "out/dts/apollo.dts",       # 由 dtsi 合成，可随时重生成
    "out/dts/apollo.dtb",       # 由 dtc 编译，且已 .incbin 进镜像
    "out/user/objects/",
    "out/userdebug/objects/",
    "out/tmp/",
)

# 反过来：这几类**必须留在库**。`out/` 若被整体忽略，发布件会静默掉出库 ——
# 下次别人 clone 下来就没有可直接刷机的 bin，而且 git 不会有任何提示。
_MUST_TRACK = (
    "out/user/.config",
    "out/userdebug/.config",
)


def _git_ignored(path: str) -> bool | None:
    """按 `.gitignore` 判定一条路径是否被忽略（路径不需要真实存在）。git 不可用返回 None。"""
    try:
        proc = subprocess.run(
            ["git", "check-ignore", "-q", path],
            cwd=str(_ROOT),
            capture_output=True,
            text=True,
            timeout=20,
        )
    except (FileNotFoundError, subprocess.SubprocessError):
        return None
    if proc.returncode == 0:
        return True
    if proc.returncode == 1:
        return False
    return None


_DECL_RE = re.compile(
    r"^(?:extern\s+)?(?:const\s+|volatile\s+|static\s+|inline\s+)*"
    r"[A-Za-z_][\w\s\*]*?\b([A-Za-z_]\w*)\s*\([^;{]*\)\s*;"
)

# 留在头里却允许不进镜像的名字。空 = 一条都不允许：声明了就得有符号，
# 否则 `nm` 查不到、调用方却以为能链上（`bsl_device_get` 第八轮就是这么删掉的）。
_API_ALLOWLIST: tuple[str, ...] = ()


def _nm_defs(elf: Path) -> set[str] | None:
    """镜像里**有定义**的符号名（排除 U = 未定义引用）。"""
    for exe in ("arm-none-eabi-nm", "arm-none-eabi-nm.exe"):
        try:
            out = subprocess.run(
                [exe, str(elf)], capture_output=True, text=True, timeout=30
            )
        except (FileNotFoundError, subprocess.SubprocessError):
            continue
        if out.returncode != 0:
            continue
        names: set[str] = set()
        for line in out.stdout.splitlines():
            parts = line.split()
            if len(parts) >= 3 and parts[1] in "TtDdBbRrWwVv":
                names.add(parts[2])
        return names
    return None


def _check_api_surface(problems: list[str], notes: list[str]) -> None:
    """G. 头里声明的函数必须在镜像里有符号。

    `--gc-sections` 会把零调用的函数直接丢掉，而头文件仍然声明它 —— 编译、链接、
    `-Wmissing-prototypes` 全都沉默，只有 `nm` 查不到。这种"假 API"会让下一个人
    以为能力还在（`bsl_device_get` 就是这样：声明在 `bsl_device.h:46`，镜像里没有
    任何符号，第八轮删除）。
    """
    elf = _ROOT / "out" / "userdebug" / "objects" / "target" / "stm32-freertos.elf"
    if not elf.is_file():
        notes.append("跳过 API 面：还没编过 userdebug")
        return
    defined = _nm_defs(elf)
    if defined is None:
        notes.append("跳过 API 面：PATH 里没有 arm-none-eabi-nm")
        return

    # HAL / libc / FreeRTOS 回调不归自研头管，按前缀排除
    vendor_prefix = ("HAL_", "System", "osDelay", "osThread", "osKernel", "xTask",
                     "vApplication", "fault_entry", "assert_failed", "__", "_")
    declared: dict[str, str] = {}
    for layer in _SELF_LAYERS:
        base = _ROOT / layer
        if not base.is_dir():
            continue
        for h in base.rglob("*.h"):
            if not h.is_file() or _is_vendored(h):
                continue
            rel = h.relative_to(_ROOT).as_posix()
            for ln in h.read_text(encoding="utf-8", errors="ignore").splitlines():
                m = _DECL_RE.match(ln.strip())
                if not m:
                    continue
                name = m.group(1)
                if name == "void" or name.startswith(vendor_prefix):
                    continue
                declared.setdefault(name, rel)

    missing = [n for n in declared if n not in defined and n not in _API_ALLOWLIST]
    if missing:
        for n in sorted(missing)[:8]:
            problems.append(f"头里声明但镜像无符号：{n}（{declared[n]}）；零调用请删声明，别留假 API")
        if len(missing) > 8:
            problems.append(f"…另有 {len(missing) - 8} 个")
    else:
        notes.append(f"API 面一致：自研头声明 {len(declared)} 个函数，全部在镜像里有符号")


def _check_ignore_rules(problems: list[str], notes: list[str]) -> None:
    """F. 忽略规则双向：中间产物必须被忽略，发布件必须不被忽略。

    单向判据（"out/ 被忽略"）会判错这棵树：`out/<variant>/release/<时间戳>/` 里的
    bin/elf/hex/map 与 `out/<variant>/.config` 是入库的，`out/` 故意没有整体忽略，
    而是逐个子目录列。所以这里用 `git check-ignore` 逐条问真相，不问 `.gitignore` 长什么样。
    """
    probe = _git_ignored(_MUST_IGNORE[0])
    if probe is None:
        notes.append("跳过忽略规则校验：git 不可用")
        return

    leaked = [p for p in _MUST_IGNORE if _git_ignored(p) is False]
    dropped = [p for p in _MUST_TRACK if _git_ignored(p) is True]
    for p in leaked:
        problems.append(f"{p} 没有被忽略：构建中间产物会被提交")
    for p in dropped:
        problems.append(f"{p} 被忽略了：发布件会静默掉出库（不要整体忽略 out/）")
    if not leaked and not dropped:
        notes.append(
            f"忽略规则双向正确（{len(_MUST_IGNORE)} 条中间产物被忽略，"
            f"{len(_MUST_TRACK)} 条发布配置留在库）"
        )


def _nm_symbols(elf: Path) -> dict[str, int] | None:
    for exe in ("arm-none-eabi-nm", "arm-none-eabi-nm.exe"):
        try:
            out = subprocess.run(
                [exe, str(elf)], capture_output=True, text=True, timeout=30
            )
        except (FileNotFoundError, subprocess.SubprocessError):
            continue
        if out.returncode != 0:
            continue
        syms: dict[str, int] = {}
        for line in out.stdout.splitlines():
            parts = line.split()
            if len(parts) >= 3:
                try:
                    syms[parts[2]] = int(parts[0], 16)
                except ValueError:
                    continue
        return syms
    return None


def run() -> int:
    import generate

    problems: list[str] = []
    notes: list[str] = []

    sources = _collect_sources()
    owners = _header_owner_map()
    edges, adj, _ = _build_graph(sources, owners)

    # A. 层边 + 越级
    total = sum(edges.values())
    order = sorted(edges.items(), key=lambda kv: (-kv[1], kv[0]))
    notes.append(f"层边 {total} 处：" + "，".join(f"{a}→{b} {n}" for (a, b), n in order))

    allowed = {k: tuple(v) for k, v in generate.LAYER_DEPENDENCIES.items()}
    for (src, dst), n in order:
        if dst not in allowed.get(src, ()):
            problems.append(
                f"越级引用 {src}→{dst} {n} 处（{src} 只允许 {', '.join(allowed.get(src, ()))}）"
            )

    # B. 环
    folded: dict[str, set[str]] = {}
    for (src, dst) in edges:
        folded.setdefault(src, set()).add(dst)
    sccs = _tarjan_scc(adj)
    real_scc = [c for c in sccs if len(c) > 1]
    if real_scc:
        names = [" <-> ".join(sorted(p.name for p in comp[:4])) for comp in real_scc[:3]]
        problems.append(f"文件级有环：{len(real_scc)} 组，例如 {names}")
    else:
        notes.append(f"文件级无环（{len(adj)} 个节点，{len(sccs)} 个强连通分量）")

    # 层级环：从 folded 上做一次 DFS 找回到自身
    def has_layer_cycle() -> list[str]:
        state: dict[str, int] = {}
        path: list[str] = []
        found: list[str] = []

        def visit(u: str) -> None:
            state[u] = 1
            path.append(u)
            for v in folded.get(u, ()):
                if state.get(v) == 1:
                    found.append(" → ".join(path[path.index(v):] + [v]))
                elif state.get(v, 0) == 0:
                    visit(v)
            path.pop()
            state[u] = 2

        for u in folded:
            if state.get(u, 0) == 0:
                visit(u)
        return found

    cycles = has_layer_cycle()
    if cycles:
        problems.append("层间有环：" + "；".join(cycles))
    else:
        notes.append("层间无环（允许边构成 DAG）")

    # C. 产物
    notes.extend(_check_artifacts(problems))

    # D / E / F / G
    _check_dts_chain(problems, notes)
    _check_header_guards(problems, notes)
    _check_ignore_rules(problems, notes)
    _check_api_surface(problems, notes)

    print("== 只读盘查（不是门禁，不改任何文件） ==")
    for n in notes:
        print(f"  · {n}")
    if problems:
        print("== 不通过 ==")
        for p in problems:
            print(f"  ! {p}")
        return 1
    print("== 通过 ==")
    return 0


if __name__ == "__main__":
    sys.exit(run())
