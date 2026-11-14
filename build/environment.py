"""工程内 Arm GNU 工具链：解析、PATH 注入、安装与 set-buildenv。

库模块 `build/environment.py`：由 `build/build.py`、`build/generate.py`、`build/verify.py` 与 `flash/flash.py` 导入。
CLI：`python build/build.py install-toolchain` / `set-buildenv`。
"""
from __future__ import annotations

import argparse
import io
import json
import os
import platform
import shutil
import subprocess
import sys
import tarfile
import tempfile
import urllib.error
import urllib.request
import zipfile
from pathlib import Path

from contracts import ROOT

TOOL = ROOT / "tool"

ENV_TOOLCHAIN_ROOT = "STM32_TOOLCHAIN_ROOT"
ENV_TOOLCHAIN_PREFIX = "STM32_TOOLCHAIN_PREFIX"
ENV_GIT_USR_BIN = "STM32_GIT_USR_BIN"

DEFAULT_TOOLCHAIN_VERSION = "13.3.rel1"
DOWNLOAD_RETRIES = 3

MSYS2_MAKE_PKG = (
    "https://mirror.msys2.org/msys/x86_64/make-4.4.1-2-x86_64.pkg.tar.zst"
)

GIT_USR_BIN_COPY = (
    "sh.exe",
    "mkdir.exe",
    "rm.exe",
    "msys-2.0.dll",
    "msys-intl-8.dll",
    "msys-iconv-2.dll",
)

_HOST_DIR = {
    "win32": "win_x86_64",
    "linux": "linux_x86_64",
}

def host_os() -> str:
    return "win32" if sys.platform == "win32" else "linux"


def host_toolchain_dirname() -> str:
    return _HOST_DIR[host_os()]


def host_arch() -> str:
    machine = platform.machine().lower()
    if machine in ("x86_64", "amd64"):
        return "x86_64"
    if machine in ("aarch64", "arm64"):
        return "aarch64"
    raise SystemExit(f"不支持的 CPU 架构: {platform.machine()}")


def gcc_candidates(bin_dir: Path) -> tuple[Path, ...]:
    if host_os() == "win32":
        return (bin_dir / "arm-none-eabi-gcc.exe",)
    return (bin_dir / "arm-none-eabi-gcc",)


def _has_gcc(bin_dir: Path) -> bool:
    for gcc in gcc_candidates(bin_dir):
        if gcc.is_file():
            if host_os() == "win32":
                return True
            return os.access(gcc, os.X_OK)
    return False


def find_toolchain_root() -> Path | None:
    if not TOOL.is_dir():
        return None
    candidates: list[Path] = [TOOL / host_toolchain_dirname()]
    if host_os() == "win32":
        extras = sorted(
            (p for p in TOOL.glob("arm-gnu-toolchain-*") if "mingw" in p.name),
            reverse=True,
        )
    else:
        extras = sorted(TOOL.glob("arm-gnu-toolchain-*"), reverse=True)
    candidates.extend(extras)
    for root in candidates:
        if _has_gcc(root / "bin"):
            return root
    return None


def find_toolchain_bin() -> Path | None:
    root = find_toolchain_root()
    if root is None:
        return None
    return root / "bin"


def resolve_gcc() -> Path | None:
    bin_dir = find_toolchain_bin()
    if bin_dir is None:
        return None
    for gcc in gcc_candidates(bin_dir):
        if gcc.is_file():
            return gcc
    return None


def resolve_make() -> Path | None:
    if host_os() == "win32":
        bin_dir = find_toolchain_bin()
        if bin_dir is None:
            return None
        make = bin_dir / "make.exe"
        return make if make.is_file() else None
    which = shutil.which("make")
    return Path(which) if which else None


def path_for_env(path: Path) -> str:
    return path.resolve().as_posix()


_path_for_env = path_for_env


def msys_tmp_dir() -> Path:
    return ROOT / "out" / "tmp"


def ensure_msys_tmp() -> Path | None:
    if host_os() != "win32":
        return None
    tmp = msys_tmp_dir()
    tmp.mkdir(parents=True, exist_ok=True)
    return tmp


def remove_msys_tmp() -> None:
    if host_os() != "win32":
        return
    tmp = msys_tmp_dir()
    if tmp.is_dir():
        shutil.rmtree(tmp, ignore_errors=True)


def remove_stray_tool_tmp_dirs() -> None:
    """MSYS 在未注入 TMP=out/tmp 时可能在 tool/ 下留下空 tmp/，构建收尾可安全删掉。"""
    for rel in (
        ("tool", "tmp"),
        ("tool", "win_x86_64", "tmp"),
        ("tool", "linux_x86_64", "tmp"),
    ):
        path = ROOT.joinpath(*rel)
        if not path.is_dir():
            continue
        try:
            if any(path.iterdir()):
                continue
            path.rmdir()
        except OSError:
            pass


def _prepend_path(merged: dict[str, str], bin_dir: Path) -> None:
    path_key = "Path" if host_os() == "win32" and "Path" in merged else "PATH"
    if path_key not in merged and host_os() == "win32":
        path_key = "PATH"
    merged[path_key] = path_for_env(bin_dir) + os.pathsep + merged.get(path_key, "")


def apply(env: dict[str, str] | None = None) -> dict[str, str]:
    """在 env 上写入 STM32_TOOLCHAIN_* 并将工具链 bin 置于 PATH 最前。"""
    merged = dict(os.environ if env is None else env)
    root = find_toolchain_root()
    if root is None:
        return merged
    bin_dir = root / "bin"
    merged[ENV_TOOLCHAIN_ROOT] = path_for_env(root)
    merged[ENV_TOOLCHAIN_PREFIX] = path_for_env(bin_dir / "arm-none-eabi-")
    _prepend_path(merged, bin_dir)
    if host_os() == "win32":
        sh = bin_dir / "sh.exe"
        if sh.is_file():
            merged["SHELL"] = path_for_env(sh)
        merged.setdefault("MSYS2_ARG_CONV_EXCL", "*")
        merged.setdefault("MSYS2_ENV_CONV_EXCL", "*")
        tmp = ensure_msys_tmp()
        if tmp is not None:
            tmp_s = path_for_env(tmp)
            merged["TMP"] = tmp_s
            merged["TEMP"] = tmp_s
    return merged


def resolve_gdb() -> Path | None:
    bin_dir = find_toolchain_bin()
    if bin_dir is None:
        return None
    name = "arm-none-eabi-gdb.exe" if host_os() == "win32" else "arm-none-eabi-gdb"
    candidate = bin_dir / name
    return candidate if candidate.is_file() else None


def _print_bash(env: dict[str, str]) -> None:
    root = env.get(ENV_TOOLCHAIN_ROOT)
    prefix = env.get(ENV_TOOLCHAIN_PREFIX)
    path = env.get("PATH", "")
    if root:
        print(f'export {ENV_TOOLCHAIN_ROOT}="{root}"')
        print(f'export {ENV_TOOLCHAIN_PREFIX}="{prefix}"')
    if path:
        print(f'export PATH="{path}"')


def _print_powershell(env: dict[str, str]) -> None:
    root = env.get(ENV_TOOLCHAIN_ROOT)
    prefix = env.get(ENV_TOOLCHAIN_PREFIX)
    path = env.get("PATH", "")
    if root:
        print(f'$env:{ENV_TOOLCHAIN_ROOT}="{root}"')
        print(f'$env:{ENV_TOOLCHAIN_PREFIX}="{prefix}"')
    if path:
        print(f'$env:PATH="{path}"')


def download(url: str, dest: Path) -> None:
    dest.parent.mkdir(parents=True, exist_ok=True)
    tmp = dest.with_suffix(dest.suffix + ".part")
    req = urllib.request.Request(
        url,
        headers={"User-Agent": "stm32-freertos-build/1.0"},
    )
    with urllib.request.urlopen(req, timeout=300) as response:
        total = int(response.headers.get("Content-Length", 0))
        done = 0
        chunk = 1024 * 1024
        with tmp.open("wb") as out:
            while True:
                block = response.read(chunk)
                if not block:
                    break
                out.write(block)
                done += len(block)
                if total:
                    pct = done * 100 // total
                    print(
                        f"\r  已下载 {done // (1024 * 1024)} / {total // (1024 * 1024)} MiB ({pct}%)",
                        end="",
                        flush=True,
                    )
        print()
    tmp.replace(dest)


def download_with_retries(urls: tuple[str, ...], dest: Path) -> None:
    last_error: Exception | None = None
    for url in urls:
        for attempt in range(1, DOWNLOAD_RETRIES + 1):
            try:
                print(f"下载 {url}" + (f"（第 {attempt} 次）" if attempt > 1 else ""))
                download(url, dest)
                return
            except (urllib.error.URLError, TimeoutError, OSError) as exc:
                last_error = exc
    raise SystemExit(f"下载失败: {last_error}") from last_error


def extract_xz_tar(archive: Path, dest_dir: Path) -> None:
    dest_dir.mkdir(parents=True, exist_ok=True)
    with tarfile.open(archive, "r:xz") as tar:
        tar.extractall(path=dest_dir)


def extract_zip(archive: Path, dest_dir: Path) -> None:
    dest_dir.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(archive) as zf:
        zf.extractall(path=dest_dir)


def clear_canonical_except_gitkeep(canonical: Path) -> None:
    if not canonical.is_dir():
        canonical.mkdir(parents=True)
        return
    for child in canonical.iterdir():
        if child.name == ".gitkeep":
            continue
        if child.is_dir():
            shutil.rmtree(child)
        else:
            child.unlink()


def populate_tool_tree(extracted: Path, canonical: Path) -> None:
    clear_canonical_except_gitkeep(canonical)
    for child in extracted.iterdir():
        shutil.move(str(child), str(canonical / child.name))
    if extracted != canonical and extracted.is_dir():
        shutil.rmtree(extracted, ignore_errors=True)


def merge_tool_tree(extracted: Path, canonical: Path) -> None:
    """将解压结果并入 canonical（保留已有工具链等目录，合并 bin 等）。"""
    canonical.mkdir(parents=True, exist_ok=True)
    for child in extracted.iterdir():
        dest = canonical / child.name
        if child.is_dir():
            shutil.copytree(child, dest, dirs_exist_ok=True)
        else:
            shutil.copy2(child, dest)


def package_name(version: str) -> str:
    if host_os() == "win32":
        return f"arm-gnu-toolchain-{version}-mingw-w64-i686-arm-none-eabi"
    return f"arm-gnu-toolchain-{version}-{host_arch()}-arm-none-eabi"


def canonical_install_dir() -> Path:
    return ROOT / "tool" / host_toolchain_dirname()


def resolve_gcc_in_tree(root: Path) -> Path | None:
    bin_dir = root / "bin"
    for gcc in gcc_candidates(bin_dir):
        if gcc.is_file():
            return gcc
    return None


def download_urls(version: str, pkg: str) -> tuple[str, ...]:
    ext = ".zip" if host_os() == "win32" else ".tar.xz"
    rel = f"gnu/{version}/binrel/{pkg}{ext}"
    mirror = f"https://armkeil.blob.core.windows.net/developer/Files/downloads/{rel}"
    primary = f"https://developer.arm.com/-/media/Files/downloads/{rel}"
    return (mirror, primary)


def install_archive(version: str) -> tuple[tuple[str, ...], Path]:
    pkg = package_name(version)
    cache = ROOT / "tool" / ".cache"
    if host_os() == "win32":
        return download_urls(version, pkg), cache / f"{pkg}.zip"
    return download_urls(version, pkg), cache / f"{pkg}.tar.xz"


def populate_canonical(extracted: Path, canonical: Path) -> None:
    if resolve_gcc_in_tree(extracted) is None:
        raise SystemExit(f"解压后未找到 arm-none-eabi-gcc: {extracted / 'bin'}")
    populate_tool_tree(extracted, canonical)


def gcc_version_line(gcc: Path) -> str:
    result = subprocess.run(
        [str(gcc), "--version"],
        check=True,
        capture_output=True,
        text=True,
    )
    return result.stdout.splitlines()[0]


def win_tool_bin() -> Path:
    return ROOT / "tool" / "win_x86_64" / "bin"


def git_usr_bin_candidates() -> list[Path]:
    roots: list[Path] = []
    env = os.environ.get(ENV_GIT_USR_BIN)
    if env:
        roots.append(Path(env))
    for portable in (
        Path(r"D:/ExPrograms/DevelopTool/Git/usr/bin"),
        Path(r"D:/ExPrograms/DevelopTool/msys64/usr/bin"),
    ):
        if portable.is_dir():
            roots.append(portable)
    for key in ("ProgramFiles", "ProgramFiles(x86)"):
        base = os.environ.get(key)
        if base:
            roots.append(Path(base) / "Git" / "usr" / "bin")
    local = os.environ.get("LOCALAPPDATA")
    if local:
        roots.append(Path(local) / "Programs" / "Git" / "usr" / "bin")
    return roots


def find_git_usr_bin(explicit: Path | None) -> Path | None:
    if explicit is not None:
        explicit = explicit.resolve()
        if (explicit / "sh.exe").is_file():
            return explicit
        if explicit.name.lower() == "sh.exe" and explicit.is_file():
            return explicit.parent
        return None
    for directory in git_usr_bin_candidates():
        if (directory / "sh.exe").is_file():
            return directory
    return None


def fetch_msys_make(dest_bin: Path) -> None:
    try:
        import zstandard
    except ImportError:
        subprocess.run(
            [sys.executable, "-m", "pip", "install", "zstandard", "-q"],
            check=True,
        )
        import zstandard

    cache = ROOT / "tool" / ".cache" / "make-msys.pkg.tar.zst"
    cache.parent.mkdir(parents=True, exist_ok=True)
    if not cache.is_file() or cache.stat().st_size < 100_000:
        print("下载 MSYS make 包 …")
        download_with_retries((MSYS2_MAKE_PKG,), cache)
    with cache.open("rb") as handle:
        raw = zstandard.ZstdDecompressor().stream_reader(handle).read()
    tmpdir = Path(tempfile.mkdtemp())
    try:
        with tarfile.open(fileobj=io.BytesIO(raw), mode="r:") as tar:
            tar.extractall(tmpdir)
        shutil.copy2(tmpdir / "usr" / "bin" / "make.exe", dest_bin / "make.exe")
    finally:
        shutil.rmtree(tmpdir, ignore_errors=True)


def copy_from_git_usr_bin(git_bin: Path, dest_bin: Path) -> list[str]:
    copied: list[str] = []
    for name in GIT_USR_BIN_COPY:
        src = git_bin / name
        if src.is_file():
            shutil.copy2(src, dest_bin / name)
            copied.append(name)
    return copied


def install_win_host_tools(from_dir: Path | None) -> None:
    if host_os() != "win32":
        raise SystemExit("install_win_host_tools 仅适用于 Windows")
    dest_bin = win_tool_bin()
    if resolve_gcc_in_tree(canonical_install_dir()) is None:
        print(
            "提示: 尚未检测到 arm-none-eabi-gcc，建议先完成: python build/build.py install-toolchain",
            file=sys.stderr,
        )
    dest_bin.mkdir(parents=True, exist_ok=True)
    git_bin = find_git_usr_bin(from_dir)
    if git_bin is None:
        raise SystemExit(
            f"未找到 Git usr\\bin（需 sh.exe）。设置 {ENV_GIT_USR_BIN} 或使用:\n"
            '  python build/build.py install-toolchain --make-only --from "D:\\ExPrograms\\DevelopTool\\Git\\usr\\bin"'
        )
    if not (dest_bin / "make.exe").is_file():
        print("安装 make.exe 到 tool/win_x86_64/bin/ …")
        fetch_msys_make(dest_bin)
    copied = copy_from_git_usr_bin(git_bin, dest_bin)
    print(f"已从 {git_bin} 复制到 {dest_bin}（未修改 Git 目录）:")
    for name in copied:
        print(f"  {name}")
    print("  make.exe")
    if not (dest_bin / "sh.exe").is_file():
        raise SystemExit(f"缺少 sh.exe，请检查 Git usr\\bin: {git_bin}")


def install_make_only(from_dir: Path | None) -> None:
    install_win_host_tools(from_dir)


def try_install_host_make(from_dir: Path | None) -> None:
    dest_bin = win_tool_bin()
    if (dest_bin / "make.exe").is_file() and (dest_bin / "sh.exe").is_file():
        return
    git_bin = find_git_usr_bin(from_dir)
    if git_bin is None:
        print(
            f'提示: 运行 python build/build.py install-toolchain --make-only --from "<Git>\\usr\\bin"\n'
            f"  或设置 {ENV_GIT_USR_BIN}",
            file=sys.stderr,
        )
        return
    print("安装 Windows 本机构建工具到 tool/win_x86_64/bin/ …")
    install_win_host_tools(from_dir)


def install_toolchain(version: str) -> None:
    canonical = canonical_install_dir()
    existing = resolve_gcc_in_tree(canonical)
    if existing is not None:
        if host_os() != "win32" and not os.access(existing, os.X_OK):
            pass
        else:
            print(f"工具链已存在: {canonical}")
            print(gcc_version_line(existing))
            if host_os() == "win32":
                try_install_host_make(None)
            return

    pkg = package_name(version)
    install_dir = ROOT / "tool" / pkg
    urls, archive = install_archive(version)

    if (gcc := resolve_gcc_in_tree(install_dir)) is not None:
        populate_canonical(install_dir, canonical)
        gcc = resolve_gcc_in_tree(canonical)
        assert gcc is not None
        print("安装完成:")
        print(gcc_version_line(gcc))
        print(f"编译: python build/build.py build（使用 tool/{canonical.name}/）")
        if host_os() == "win32":
            try_install_host_make(None)
        return

    if not archive.is_file():
        download_with_retries(urls, archive)

    print(f"解压到 {ROOT / 'tool'}/")
    if archive.suffix == ".zip":
        extract_zip(archive, ROOT / "tool")
    else:
        extract_xz_tar(archive, ROOT / "tool")

    if not install_dir.is_dir():
        raise SystemExit(f"解压后未找到 {install_dir}")

    populate_canonical(install_dir, canonical)
    gcc = resolve_gcc_in_tree(canonical)
    if gcc is None:
        raise SystemExit(f"安装后未找到 {canonical / 'bin'}")

    print("安装完成:")
    print(gcc_version_line(gcc))
    print(f"编译: python build/build.py build（使用 tool/{canonical.name}/）")
    if host_os() == "win32":
        try_install_host_make(None)


def install_toolchain_main(argv: list[str] | None = None) -> None:
    default_version = os.environ.get("TOOLCHAIN_VERSION", DEFAULT_TOOLCHAIN_VERSION)
    host_dir = host_toolchain_dirname()
    parser = argparse.ArgumentParser(
        description="安装 Arm GNU 交叉工具链；Windows 可将 make.exe 装入 tool/win_x86_64/bin/。",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=f"""\
说明:
  安装包缓存: tool/.cache/
  安装目录:   tool/{host_dir}/（bin/arm-none-eabi-gcc[.exe]）
  Linux 包:   .tar.xz（x86_64 / aarch64 等）
  Windows 包: .zip（mingw-w64-i686）
  Windows make: MSYS 包 + 自 Git usr\\bin 只读复制 sh/mkdir 等（--make-only --from）
  默认版本:   {DEFAULT_TOOLCHAIN_VERSION}（环境变量 TOOLCHAIN_VERSION 或 --version）

示例:
  python build/build.py install-toolchain
  python build/build.py install-toolchain --version 13.3.rel1
  python build/build.py install-toolchain --make-only
  python build/build.py install-toolchain --make-only --from "D:\\ExPrograms\\DevelopTool\\Git\\usr\\bin"

安装完成后: python build/build.py build
""",
    )
    parser.add_argument(
        "--version",
        default=default_version,
        help=f"Arm GNU 工具链版本（默认: {default_version}）",
    )
    parser.add_argument(
        "--make-only",
        action="store_true",
        help="仅安装 make.exe 到 tool/win_x86_64/bin/（Windows）",
    )
    parser.add_argument(
        "--from",
        dest="from_dir",
        type=Path,
        help=f"Git usr\\bin（含 sh.exe，只读复制；默认查 {ENV_GIT_USR_BIN} 与常见路径）",
    )
    args = parser.parse_args([] if argv is None else argv)
    if args.make_only:
        install_make_only(args.from_dir)
        return
    if args.from_dir is not None and host_os() == "win32":
        install_toolchain(args.version)
        try_install_host_make(args.from_dir)
        return
    install_toolchain(args.version)


def set_buildenv_main(argv: list[str] | None = None) -> None:
    parser = argparse.ArgumentParser(
        description="检测并导出工程内 Arm GNU 工具链环境变量。",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""\
目录约定:
  Linux x86_64:  tool/linux_x86_64/bin/arm-none-eabi-gcc
  Windows x86_64: tool/win_x86_64/bin/arm-none-eabi-gcc(.exe)
  Windows 本机 make: tool/win_x86_64/bin/make.exe（python build/build.py install-toolchain --make-only）

环境变量:
  STM32_TOOLCHAIN_ROOT    工具链根目录
  STM32_TOOLCHAIN_PREFIX  bin/arm-none-eabi- 前缀（供脚本使用）
  PATH                    前置工具链 bin

示例:
  eval "$(python3 build/build.py set-buildenv --print bash)"
  python3 build/build.py set-buildenv --print powershell | Invoke-Expression
  python3 build/build.py build            # 编译时会自动 apply 相同变量

含 OpenOCD 的环境见: python flash/flash.py set-buildenv --flash
""",
    )
    parser.add_argument(
        "--print",
        choices=("bash", "powershell", "json"),
        help="输出可 eval 的 shell 设置或 JSON",
    )
    parser.add_argument(
        "--check",
        action="store_true",
        help="检查工具链；Windows 另需 tool/win_x86_64/bin/make.exe",
    )
    args = parser.parse_args([] if argv is None else argv)

    root = find_toolchain_root()
    if args.check:
        if root is None:
            print(
                f"未找到 {host_toolchain_dirname()} 下的 arm-none-eabi-gcc",
                file=sys.stderr,
            )
            raise SystemExit(1)
        if host_os() == "win32":
            win_bin = (root / "bin") if root else None
            missing = []
            if resolve_make() is None:
                missing.append("make.exe")
            if win_bin is not None and not (win_bin / "sh.exe").is_file():
                missing.append("sh.exe")
            if missing:
                print(
                    f"缺少 tool/win_x86_64/bin/{'、'.join(missing)}。"
                    '请运行: python build/build.py install-toolchain --make-only --from "<Git>\\usr\\bin"',
                    file=sys.stderr,
                )
                raise SystemExit(1)
        print(root)
        return

    env = apply()
    if args.print == "json":
        payload = {
            "host": host_os(),
            "toolchain_dir": host_toolchain_dirname(),
            ENV_TOOLCHAIN_ROOT: env.get(ENV_TOOLCHAIN_ROOT),
            ENV_TOOLCHAIN_PREFIX: env.get(ENV_TOOLCHAIN_PREFIX),
        }
        print(json.dumps(payload, indent=2))
        return
    if args.print == "powershell":
        _print_powershell(env)
        return
    if args.print == "bash":
        _print_bash(env)
        return

    if root is None:
        print(
            f"当前为 {host_os()}，未在 tool/{host_toolchain_dirname()}/ 找到工具链。",
            file=sys.stderr,
        )
        raise SystemExit(1)
    gcc = resolve_gcc()
    print(f"host={host_os()} toolchain={root}")
    if gcc is not None:
        print(f"gcc={gcc}")
