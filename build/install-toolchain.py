#!/usr/bin/env python3
"""安装工程 tool/ 下的交叉工具链；Windows 另可将 GNU Make 装入 tool/win_x86_64/bin/。"""
from __future__ import annotations

import argparse
import io
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

ROOT = Path(__file__).resolve().parent.parent
_BUILD = Path(__file__).resolve().parent


def _load_setenv():
    import importlib.util

    path = _BUILD / "set-buildenv.py"
    spec = importlib.util.spec_from_file_location("setenv", path)
    if spec is None or spec.loader is None:
        raise ImportError(f"无法加载 {path}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


setenv = _load_setenv()
DEFAULT_VERSION = "13.3.rel1"
DOWNLOAD_RETRIES = 3
ENV_GIT_USR_BIN = "STM32_GIT_USR_BIN"
MSYS2_MAKE_PKG = (
    "https://mirror.msys2.org/msys/x86_64/make-4.4.1-2-x86_64.pkg.tar.zst"
)

# 从 Git usr\bin 只读复制到 tool/win_x86_64/bin（不修改 Git 安装目录）
GIT_USR_BIN_COPY = (
    "sh.exe",
    "mkdir.exe",
    "rm.exe",
    "msys-2.0.dll",
    "msys-intl-8.dll",
    "msys-iconv-2.dll",
)


def host_arch() -> str:
    machine = platform.machine().lower()
    if machine in ("x86_64", "amd64"):
        return "x86_64"
    if machine in ("aarch64", "arm64"):
        return "aarch64"
    raise SystemExit(f"不支持的 CPU 架构: {platform.machine()}")


def package_name(version: str) -> str:
    if setenv.host_os() == "win32":
        return f"arm-gnu-toolchain-{version}-mingw-w64-i686-arm-none-eabi"
    return f"arm-gnu-toolchain-{version}-{host_arch()}-arm-none-eabi"


def canonical_install_dir() -> Path:
    return ROOT / "tool" / setenv.host_toolchain_dirname()


def resolve_gcc_in_tree(root: Path) -> Path | None:
    bin_dir = root / "bin"
    for gcc in setenv._gcc_candidates(bin_dir):
        if gcc.is_file():
            return gcc
    return None


def download_urls(version: str, pkg: str) -> tuple[str, ...]:
    ext = ".zip" if setenv.host_os() == "win32" else ".tar.xz"
    rel = f"gnu/{version}/binrel/{pkg}{ext}"
    mirror = f"https://armkeil.blob.core.windows.net/developer/Files/downloads/{rel}"
    primary = f"https://developer.arm.com/-/media/Files/downloads/{rel}"
    return (mirror, primary)


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
                    print(f"\r  已下载 {done // (1024 * 1024)} / {total // (1024 * 1024)} MiB ({pct}%)", end="", flush=True)
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


def install_archive(version: str) -> tuple[tuple[str, ...], Path]:
    pkg = package_name(version)
    cache = ROOT / "tool" / ".cache"
    if setenv.host_os() == "win32":
        return download_urls(version, pkg), cache / f"{pkg}.zip"
    return download_urls(version, pkg), cache / f"{pkg}.tar.xz"


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


def populate_canonical(extracted: Path, canonical: Path) -> None:
    if resolve_gcc_in_tree(extracted) is None:
        raise SystemExit(f"解压后未找到 arm-none-eabi-gcc: {extracted / 'bin'}")
    clear_canonical_except_gitkeep(canonical)
    for child in extracted.iterdir():
        shutil.move(str(child), str(canonical / child.name))
    if extracted != canonical and extracted.is_dir():
        shutil.rmtree(extracted, ignore_errors=True)


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
        print(f"下载 MSYS make 包 …")
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
    if setenv.host_os() != "win32":
        raise SystemExit("install_win_host_tools 仅适用于 Windows")
    dest_bin = win_tool_bin()
    if resolve_gcc_in_tree(canonical_install_dir()) is None:
        print(
            "提示: 尚未检测到 arm-none-eabi-gcc，建议先完成: python build/install-toolchain.py",
            file=sys.stderr,
        )
    dest_bin.mkdir(parents=True, exist_ok=True)
    git_bin = find_git_usr_bin(from_dir)
    if git_bin is None:
        raise SystemExit(
            f"未找到 Git usr\\bin（需 sh.exe）。设置 {ENV_GIT_USR_BIN} 或使用:\n"
            '  python build/install-toolchain.py --make-only --from "D:\\ExPrograms\\DevelopTool\\Git\\usr\\bin"'
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
            f"提示: 运行 python build/install-toolchain.py --make-only --from \"<Git>\\usr\\bin\"\n"
            f"  或设置 {ENV_GIT_USR_BIN}",
            file=sys.stderr,
        )
        return
    print("安装 Windows 本机构建工具到 tool/win_x86_64/bin/ …")
    install_win_host_tools(from_dir)


def install(version: str) -> None:
    canonical = canonical_install_dir()
    existing = resolve_gcc_in_tree(canonical)
    if existing is not None:
        if setenv.host_os() != "win32" and not os.access(existing, os.X_OK):
            pass
        else:
            print(f"工具链已存在: {canonical}")
            print(gcc_version_line(existing))
            if setenv.host_os() == "win32":
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
        print(f"编译: python build/create-project.py build（使用 tool/{canonical.name}/）")
        if setenv.host_os() == "win32":
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
    print(f"编译: python build/create-project.py build（使用 tool/{canonical.name}/）")
    if setenv.host_os() == "win32":
        try_install_host_make(None)


def main(argv: list[str] | None = None) -> None:
    default_version = os.environ.get("TOOLCHAIN_VERSION", DEFAULT_VERSION)
    host_dir = setenv.host_toolchain_dirname()
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
  默认版本:   {DEFAULT_VERSION}（环境变量 TOOLCHAIN_VERSION 或 --version）

示例:
  python build/install-toolchain.py
  python build/install-toolchain.py --version 13.3.rel1
  python build/install-toolchain.py --make-only
  python build/install-toolchain.py --make-only --from "D:\\ExPrograms\\DevelopTool\\Git\\usr\\bin"

安装完成后: python build/create-project.py build
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
    args = parser.parse_args(argv)
    if args.make_only:
        install_make_only(args.from_dir)
        return
    if args.from_dir is not None and setenv.host_os() == "win32":
        install(args.version)
        try_install_host_make(args.from_dir)
        return
    install(args.version)


if __name__ == "__main__":
    main()
