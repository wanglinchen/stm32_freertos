#!/usr/bin/env python3
"""按主机 OS 解析工程内 Arm GNU 工具链，并设置编译相关环境变量。"""
from __future__ import annotations

import argparse
import json
import os
import shutil
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
TOOL = ROOT / "tool"

ENV_TOOLCHAIN_ROOT = "STM32_TOOLCHAIN_ROOT"
ENV_TOOLCHAIN_PREFIX = "STM32_TOOLCHAIN_PREFIX"
ENV_GIT_USR_BIN = "STM32_GIT_USR_BIN"

_HOST_DIR = {
    "win32": "win_x86_64",
    "linux": "linux_x86_64",
}


def host_os() -> str:
    return "win32" if sys.platform == "win32" else "linux"


def host_toolchain_dirname() -> str:
    return _HOST_DIR[host_os()]


def _gcc_candidates(bin_dir: Path) -> tuple[Path, ...]:
    if host_os() == "win32":
        return (bin_dir / "arm-none-eabi-gcc.exe",)
    return (bin_dir / "arm-none-eabi-gcc",)


def _has_gcc(bin_dir: Path) -> bool:
    for gcc in _gcc_candidates(bin_dir):
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
    for gcc in _gcc_candidates(bin_dir):
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


def _path_for_env(path: Path) -> str:
    return path.resolve().as_posix()


def msys_tmp_dir() -> Path:
    return ROOT / "out" / "msys-tmp"


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


def apply(env: dict[str, str] | None = None) -> dict[str, str]:
    """在 env 上写入 STM32_TOOLCHAIN_* 并将工具链 bin 置于 PATH 最前。"""
    merged = dict(os.environ if env is None else env)
    root = find_toolchain_root()
    if root is None:
        return merged
    bin_dir = root / "bin"
    merged[ENV_TOOLCHAIN_ROOT] = _path_for_env(root)
    merged[ENV_TOOLCHAIN_PREFIX] = _path_for_env(bin_dir / "arm-none-eabi-")
    path_key = "Path" if host_os() == "win32" and "Path" in merged else "PATH"
    if path_key not in merged and host_os() == "win32":
        path_key = "PATH"
    merged[path_key] = _path_for_env(bin_dir) + os.pathsep + merged.get(path_key, "")
    if host_os() == "win32":
        sh = bin_dir / "sh.exe"
        if sh.is_file():
            merged["SHELL"] = _path_for_env(sh)
        merged.setdefault("MSYS2_ARG_CONV_EXCL", "*")
        merged.setdefault("MSYS2_ENV_CONV_EXCL", "*")
        tmp = ensure_msys_tmp()
        if tmp is not None:
            tmp_s = _path_for_env(tmp)
            merged["TMP"] = tmp_s
            merged["TEMP"] = tmp_s
    return merged


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


def main(argv: list[str] | None = None) -> None:
    parser = argparse.ArgumentParser(
        description="检测 Linux/Windows 并设置工程内 Arm GNU 工具链环境变量。",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""\
目录约定:
  Linux x86_64:  tool/linux_x86_64/bin/arm-none-eabi-gcc
  Windows x86_64: tool/win_x86_64/bin/arm-none-eabi-gcc(.exe)
  Windows 本机 make: tool/win_x86_64/bin/make.exe（python build/install-toolchain.py --make-only）

环境变量:
  STM32_TOOLCHAIN_ROOT    工具链根目录
  STM32_TOOLCHAIN_PREFIX  bin/arm-none-eabi- 前缀（供脚本使用）
  PATH                    前置工具链 bin

示例:
  eval "$(python3 build/set-buildenv.py --print bash)"
  python3 build/set-buildenv.py --print powershell | Invoke-Expression   # PowerShell
  python3 build/create-project.py build   # 内部会自动 apply 相同变量
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
    args = parser.parse_args(argv)

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
                    "请运行: python build/install-toolchain.py --make-only --from \"<Git>\\usr\\bin\"",
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


if __name__ == "__main__":
    main()
