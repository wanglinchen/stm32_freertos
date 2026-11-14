"""OpenOCD / ST-Link 刷机与调试环境。CLI：`python flash/flash.py <子命令>`。"""
from __future__ import annotations

import argparse
import json
import os
import shutil
import subprocess
import sys
import tarfile
import tempfile
from pathlib import Path

_ROOT = Path(__file__).resolve().parent.parent
_BUILD = _ROOT / "build"
if str(_BUILD) not in sys.path:
    sys.path.insert(0, str(_BUILD))

from contracts import OUT_ARTIFACT_DIR, ROOT

import environment as te

TOOL = ROOT / "tool"
ENV_OPENOCD_ROOT = "STM32_OPENOCD_ROOT"
OPENOCD_XPACK_VERSION = "0.12.0-7"
OPENOCD_TOOL_PLATFORMS = frozenset({"win_x86_64", "linux_x86_64"})

host_os = te.host_os
host_toolchain_dirname = te.host_toolchain_dirname
path_for_env = te.path_for_env
apply = te.apply
_prepend_path = te._prepend_path
merge_tool_tree = te.merge_tool_tree
download_with_retries = te.download_with_retries
extract_zip = te.extract_zip
find_toolchain_bin = te.find_toolchain_bin
resolve_gdb = te.resolve_gdb
canonical_install_dir = te.canonical_install_dir
_print_bash = te._print_bash
_print_powershell = te._print_powershell

def _print_bash_flash(env: dict[str, str]) -> None:
    _print_bash(env)
    ocd = env.get(ENV_OPENOCD_ROOT)
    if ocd:
        print(f'export {ENV_OPENOCD_ROOT}="{ocd}"')

def _print_powershell_flash(env: dict[str, str]) -> None:
    _print_powershell(env)
    ocd = env.get(ENV_OPENOCD_ROOT)
    if ocd:
        print(f'$env:{ENV_OPENOCD_ROOT}="{ocd}"')

ENV_TOOLCHAIN_ROOT = te.ENV_TOOLCHAIN_ROOT
ENV_TOOLCHAIN_PREFIX = te.ENV_TOOLCHAIN_PREFIX

def openocd_canonical_dir() -> Path:
    return canonical_install_dir()

def openocd_platform_os(platform_dir: str) -> str:
    if platform_dir == "win_x86_64":
        return "win32"
    if platform_dir == "linux_x86_64":
        return "linux"
    raise SystemExit(f"未知 OpenOCD 平台目录: {platform_dir}")

def tool_dir_for_platform(platform_dir: str) -> Path:
    if platform_dir not in OPENOCD_TOOL_PLATFORMS:
        raise SystemExit(f"不支持的平台: {platform_dir}（可选: {', '.join(sorted(OPENOCD_TOOL_PLATFORMS))}）")
    return TOOL / platform_dir

def resolve_openocd_in(root: Path) -> Path | None:
    for name in ("openocd.exe", "openocd"):
        candidate = root / "bin" / name
        if candidate.is_file():
            return candidate
    return None

def resolve_openocd() -> Path | None:
    return resolve_openocd_in(openocd_canonical_dir())

def openocd_scripts_dir() -> Path:
    return openocd_canonical_dir() / "openocd" / "scripts"

def find_openocd_root() -> Path | None:
    if resolve_openocd() is None:
        return None
    return openocd_canonical_dir()

def apply_flash(env: dict[str, str] | None = None) -> dict[str, str]:
    """在 apply 基础上追加 OpenOCD 根目录与 bin 到 PATH（刷机/GDB 用）。"""
    merged = apply(env)
    root = find_openocd_root()
    if root is None:
        return merged
    merged[ENV_OPENOCD_ROOT] = path_for_env(root)
    _prepend_path(merged, root / "bin")
    return merged

def default_firmware_elf() -> Path:
    from generate import load_info

    name = load_info().get("project_name", ROOT.name)
    return OUT_ARTIFACT_DIR / f"{name}.elf"

def openocd_program_argv(
    elf: Path,
    *,
    interface_cfg: str = "interface/stlink.cfg",
    target_cfg: str = "target/stm32f4x.cfg",
) -> list[str]:
    exe = resolve_openocd()
    if exe is None:
        raise SystemExit(
            "未找到 OpenOCD。请先运行: python flash/flash.py install-openocd"
        )
    scripts = openocd_scripts_dir()
    if not scripts.is_dir():
        raise SystemExit(f"OpenOCD 脚本目录不存在: {scripts}")

    elf_posix = path_for_env(elf.resolve())
    cmd = f'program "{elf_posix}" verify reset exit'

    return [
        str(exe),
        "-s",
        path_for_env(scripts),
        "-f",
        interface_cfg,
        "-f",
        target_cfg,
        "-c",
        cmd,
    ]

def openocd_server_argv(
    *,
    interface_cfg: str = "interface/stlink.cfg",
    target_cfg: str = "target/stm32f4x.cfg",
) -> list[str]:
    exe = resolve_openocd()
    if exe is None:
        raise SystemExit(
            "未找到 OpenOCD。请先运行: python flash/flash.py install-openocd"
        )
    scripts = openocd_scripts_dir()
    if not scripts.is_dir():
        raise SystemExit(f"OpenOCD 脚本目录不存在: {scripts}")
    return [
        str(exe),
        "-s",
        path_for_env(scripts),
        "-f",
        interface_cfg,
        "-f",
        target_cfg,
    ]

def stlink_driver_pack() -> Path:
    return openocd_canonical_dir() / "openocd" / "Drivers" / "stsw-link009_v3"

def stlink_linux_udev_pack() -> Path:
    return openocd_canonical_dir() / "openocd" / "Drivers" / "stlink_udev"

def openocd_udev_rules_src() -> Path:
    rules = openocd_canonical_dir() / "openocd" / "contrib" / "60-openocd.rules"
    if not rules.is_file():
        raise SystemExit(
            f"未找到 udev 规则: {rules}\n请先运行: python flash/flash.py install-openocd"
        )
    return rules

def ensure_linux_stlink_udev_layout(tool_root: Path) -> None:
    """Linux OpenOCD 旁补齐 Drivers/stlink_udev（与 Windows stsw-link009_v3 对称）。"""
    src_pack = ROOT / "tool" / "linux_x86_64" / "openocd" / "Drivers" / "stlink_udev"
    if not src_pack.is_dir():
        return
    dst_pack = tool_root / "openocd" / "Drivers" / "stlink_udev"
    dst_pack.mkdir(parents=True, exist_ok=True)
    for name in ("README.md", "install_udev.sh"):
        src = src_pack / name
        dst = dst_pack / name
        if not src.is_file():
            continue
        if not dst.is_file() or dst.read_bytes() != src.read_bytes():
            dst.write_bytes(src.read_bytes())
            if name.endswith(".sh"):
                dst.chmod(0o755)

def program_firmware(
    elf: Path | None = None,
    *,
    interface_cfg: str = "interface/stlink.cfg",
    target_cfg: str = "target/stm32f4x.cfg",
) -> None:
    target = default_firmware_elf() if elf is None else elf
    if not target.is_file():
        raise SystemExit(f"固件不存在: {target}（请先 python build/build.py build）")
    argv = openocd_program_argv(
        target, interface_cfg=interface_cfg, target_cfg=target_cfg
    )
    print("刷写:", " ".join(argv))
    result = subprocess.run(argv, check=False, env=apply_flash())
    if result.returncode != 0:
        raise SystemExit(result.returncode)

def run_openocd_server(
    *,
    interface_cfg: str = "interface/stlink.cfg",
    target_cfg: str = "target/stm32f4x.cfg",
) -> None:
    argv = openocd_server_argv(interface_cfg=interface_cfg, target_cfg=target_cfg)
    gdb = resolve_gdb()
    elf = default_firmware_elf()
    print("GDB 服务器:", " ".join(argv))
    print("默认端口: GDB 3333，telnet 4444")
    if gdb is not None and elf.is_file():
        print(f"另一终端: {path_for_env(gdb)} {path_for_env(elf)}")
        print("  (gdb) target extended-remote :3333")
        print("  (gdb) monitor reset halt")
    result = subprocess.run(argv, check=False, env=apply_flash())
    if result.returncode != 0:
        raise SystemExit(result.returncode)

def win_disable_case_sensitive_tree(root: Path) -> None:
    """将 root 及子目录的「目录区分大小写」设为关闭（dpinst 会小写化路径段）。"""
    if host_os() != "win32" or not root.is_dir():
        return
    dirs = sorted(
        (p for p in root.rglob("*") if p.is_dir()),
        key=lambda p: len(p.parts),
        reverse=True,
    )
    targets = [root, *dirs]
    fsutil = shutil.which("fsutil") or "fsutil"
    for directory in targets:
        subprocess.run(
            [fsutil, "file", "setCaseSensitiveInfo", path_for_env(directory), "disable"],
            check=False,
            capture_output=True,
        )

def win_run_elevated_dpinst(pack: Path) -> int:
    pack_win = path_for_env(pack)
    dpinst_win = path_for_env(pack / "dpinst_amd64.exe")
    with tempfile.NamedTemporaryFile(
        mode="w",
        suffix=".bat",
        delete=False,
        encoding="utf-8",
        newline="\r\n",
    ) as bat:
        bat.write(f'@echo off\r\ncd /d "{pack_win}"\r\n')
        bat.write(f'"{dpinst_win}" /SW /PATH "{pack_win}" /LM\r\n')
        bat.write("exit /b %ERRORLEVEL%\r\n")
        bat_path = bat.name
    try:
        ps = (
            f"$p = Start-Process -FilePath '{bat_path}' -Wait -PassThru -Verb RunAs; "
            f"exit $p.ExitCode"
        )
        result = subprocess.run(
            ["powershell", "-NoProfile", "-Command", ps],
            check=False,
        )
        return int(result.returncode)
    finally:
        Path(bat_path).unlink(missing_ok=True)

def install_stlink_driver_linux() -> None:
    root = openocd_canonical_dir()
    ensure_linux_stlink_udev_layout(root)
    openocd_udev_rules_src()
    pack = stlink_linux_udev_pack()
    script = pack / "install_udev.sh"
    if not script.is_file():
        raise SystemExit(
            f"未找到 Linux ST-Link 安装脚本: {script}\n"
            "请确认仓库含 tool/linux_x86_64/openocd/Drivers/stlink_udev/"
        )
    print(f"安装 ST-Link / OpenOCD udev 规则（需 sudo）: {script}")
    result = subprocess.run(["bash", str(script)], check=False)
    if result.returncode != 0:
        raise SystemExit(
            f"udev 安装返回 {result.returncode}；可手动: sudo bash {script}"
        )

def install_stlink_driver() -> None:
    if host_os() == "linux":
        install_stlink_driver_linux()
        return
    if host_os() != "win32":
        raise SystemExit("install-stlink-driver 仅支持 Windows 与 Linux")
    host_root = openocd_canonical_dir()
    win_disable_case_sensitive_tree(host_root)
    pack = stlink_driver_pack()
    if not (pack / "dpinst_amd64.exe").is_file():
        raise SystemExit(
            f"未找到 ST-Link 驱动包: {pack}\n"
            "请将 Drivers/stsw-link009_v3 放到 tool/<平台>/openocd/Drivers/ 下"
        )
    print(f"安装 ST-Link 驱动（WinUSB）: {pack}")
    print("若弹出 UAC，请确认管理员安装。")
    code = win_run_elevated_dpinst(pack)
    if code != 0:
        raise SystemExit(
            f"驱动安装返回 {code}；请在 {pack} 以管理员运行 "
            f"dpinst_amd64.exe /SW /PATH . /LM，或查看 C:\\Windows\\DPINST.LOG"
        )

def openocd_package_name_for(platform_dir: str) -> str:
    if openocd_platform_os(platform_dir) == "win32":
        return f"xpack-openocd-{OPENOCD_XPACK_VERSION}-win32-x64"
    arch = "arm64" if platform_dir == "linux_aarch64" else "x64"
    return f"xpack-openocd-{OPENOCD_XPACK_VERSION}-linux-{arch}"

def openocd_archive_path_for(platform_dir: str) -> Path:
    cache = ROOT / "tool" / ".cache"
    pkg = openocd_package_name_for(platform_dir)
    ext = ".zip" if openocd_platform_os(platform_dir) == "win32" else ".tar.gz"
    return cache / f"{pkg}{ext}"

def openocd_download_url_for(platform_dir: str) -> str:
    pkg = openocd_package_name_for(platform_dir)
    ext = ".zip" if openocd_platform_os(platform_dir) == "win32" else ".tar.gz"
    return (
        f"https://github.com/xpack-dev-tools/openocd-xpack/releases/download/"
        f"v{OPENOCD_XPACK_VERSION}/{pkg}{ext}"
    )

def extract_openocd_archive(archive: Path, dest_dir: Path) -> None:
    dest_dir.mkdir(parents=True, exist_ok=True)
    if archive.suffix.lower() == ".zip":
        extract_zip(archive, dest_dir)
        return
    with tarfile.open(archive, "r:gz") as tar:
        tar.extractall(path=dest_dir)

def openocd_extracted_root(staging: Path) -> Path:
    children = [p for p in staging.iterdir() if p.name != ".gitkeep"]
    if len(children) == 1 and children[0].is_dir():
        return children[0]
    if (staging / "bin").is_dir():
        return staging
    raise SystemExit(f"无法识别 OpenOCD 解压布局: {staging}")

def install_openocd(platform_dir: str | None = None) -> None:
    target = platform_dir or host_toolchain_dirname()
    canonical = tool_dir_for_platform(target)
    existing = resolve_openocd_in(canonical)
    if existing is not None:
        print(f"OpenOCD 已存在 ({target}): {existing}")
        if openocd_platform_os(target) == host_os():
            result = subprocess.run(
                [str(existing), "--version"],
                check=True,
                capture_output=True,
                text=True,
            )
            print(result.stdout.splitlines()[0] if result.stdout else result.stderr.splitlines()[0])
        return

    archive = openocd_archive_path_for(target)
    if not archive.is_file() or archive.stat().st_size < 100_000:
        print(f"下载 OpenOCD xPack {OPENOCD_XPACK_VERSION} ({target}) …")
        download_with_retries((openocd_download_url_for(target),), archive)

    staging = ROOT / "tool" / ".cache" / f"_openocd_extract_{target}"
    if staging.exists():
        shutil.rmtree(staging)
    staging.mkdir(parents=True)
    print(f"解压到 {staging} …")
    extract_openocd_archive(archive, staging)
    extracted = openocd_extracted_root(staging)
    bin_name = "openocd.exe" if openocd_platform_os(target) == "win32" else "openocd"
    if (extracted / "bin" / bin_name).is_file() is False:
        raise SystemExit(f"解压后未找到 openocd 可执行文件: {extracted / 'bin'}")
    merge_tool_tree(extracted, canonical)
    if openocd_platform_os(target) == "linux":
        ensure_linux_stlink_udev_layout(canonical)
    shutil.rmtree(staging, ignore_errors=True)
    exe = resolve_openocd_in(canonical)
    if exe is None:
        raise SystemExit(f"安装失败: {canonical / 'bin'}")
    print(f"OpenOCD 已安装到 {canonical}")
    if openocd_platform_os(target) == host_os():
        subprocess.run([str(exe), "--version"], check=True)
    else:
        print(f"（{target} 为 {openocd_platform_os(target)} 包，请在对应系统/WSL 下使用 {exe.name}）")

def set_buildenv_main(argv: list[str] | None = None) -> None:
    parser = argparse.ArgumentParser(
        description="导出刷机/GDB 用环境（工具链 + OpenOCD）。",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""\
目录约定:
  Linux x86_64:  tool/linux_x86_64/bin/arm-none-eabi-gcc
  Windows x86_64: tool/win_x86_64/bin/arm-none-eabi-gcc(.exe)
  Windows 本机 make: tool/win_x86_64/bin/make.exe（python flash/flash.py install-toolchain --make-only）
  OpenOCD: tool/<host>/bin/openocd[.exe]

环境变量:
  STM32_TOOLCHAIN_ROOT    工具链根目录
  STM32_TOOLCHAIN_PREFIX  bin/arm-none-eabi- 前缀（供脚本使用）
  STM32_OPENOCD_ROOT      OpenOCD 安装根（已 install-openocd 时）
  PATH                    前置工具链与 OpenOCD bin

示例:
  eval "$(python3 flash/flash.py set-buildenv --print bash)"
  python3 flash/flash.py set-buildenv --print powershell | Invoke-Expression
  python3 build/build.py build            # 编译时会自动 apply 相同变量
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

    env = apply_flash()
    if args.print == "json":
        payload = {
            "host": host_os(),
            "toolchain_dir": host_toolchain_dirname(),
            ENV_TOOLCHAIN_ROOT: env.get(ENV_TOOLCHAIN_ROOT),
            ENV_TOOLCHAIN_PREFIX: env.get(ENV_TOOLCHAIN_PREFIX),
            ENV_OPENOCD_ROOT: env.get(ENV_OPENOCD_ROOT),
        }
        print(json.dumps(payload, indent=2))
        return
    if args.print == "powershell":
        _print_powershell_flash(env)
        return
    if args.print == "bash":
        _print_bash_flash(env)
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
    ocd = resolve_openocd()
    if ocd is not None:
        print(f"openocd={ocd}")

def _interface_target_args(parser: argparse.ArgumentParser) -> None:
    parser.add_argument(
        "--interface",
        default="interface/stlink.cfg",
        help="OpenOCD 接口脚本（相对 scripts 目录）",
    )
    parser.add_argument(
        "--target",
        default="target/stm32f4x.cfg",
        help="OpenOCD 目标脚本",
    )

def install_openocd_main(argv: list[str] | None = None) -> None:
    parser = argparse.ArgumentParser(
        description="下载并解压 xPack OpenOCD 到 tool/<host>/（与工具链同目录）。",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""\
目录约定:
  Windows: tool/win_x86_64/bin/openocd.exe
  Linux:   tool/linux_x86_64/bin/openocd

示例:
  python flash/flash.py install-openocd
  python flash/flash.py install-openocd --platform linux_x86_64
  python flash/flash.py install-openocd --all-platforms
""",
    )
    parser.add_argument(
        "--platform",
        choices=sorted(OPENOCD_TOOL_PLATFORMS),
        action="append",
        dest="platforms",
        metavar="DIR",
        help="安装到 tool/<DIR>/（可多次指定；默认仅当前系统目录）",
    )
    parser.add_argument(
        "--all-platforms",
        action="store_true",
        help="安装 win_x86_64 与 linux_x86_64 两套 xPack",
    )
    args = parser.parse_args([] if argv is None else argv)
    if host_os() not in ("win32", "linux"):
        raise SystemExit("install-openocd 仅支持 Windows 与 Linux")
    if args.all_platforms:
        targets = sorted(OPENOCD_TOOL_PLATFORMS)
    elif args.platforms:
        targets = args.platforms
    else:
        targets = [host_toolchain_dirname()]
    for name in targets:
        install_openocd(name)

def flash_main(argv: list[str] | None = None) -> None:
    parser = argparse.ArgumentParser(
        description="用工程内 OpenOCD + ST-Link 刷写 ELF（verify reset exit）。",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""\
示例:
  python flash/flash.py flash
  python flash/flash.py flash out/object/target/stm32-freertos.elf
  python flash/flash.py flash --interface interface/stlink-v2-1.cfg
""",
    )
    parser.add_argument(
        "elf",
        nargs="?",
        type=Path,
        help="固件 ELF（默认 out/object/target/<CONFIG_PROJECT_NAME>.elf）",
    )
    _interface_target_args(parser)
    args = parser.parse_args([] if argv is None else argv)
    program_firmware(
        args.elf,
        interface_cfg=args.interface,
        target_cfg=args.target,
    )

def debug_server_main(argv: list[str] | None = None) -> None:
    parser = argparse.ArgumentParser(
        description="前台启动 OpenOCD GDB 服务器（端口 3333）。",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""\
示例:
  python flash/flash.py debug-server
  python flash/flash.py debug-server --interface interface/stlink-v2-1.cfg
""",
    )
    _interface_target_args(parser)
    args = parser.parse_args([] if argv is None else argv)
    run_openocd_server(interface_cfg=args.interface, target_cfg=args.target)

def install_stlink_driver_main(argv: list[str] | None = None) -> None:
    parser = argparse.ArgumentParser(
        description=(
            "ST-Link 设备访问：Windows 安装 WinUSB（Drivers/stsw-link009_v3）；"
            "Linux 安装 udev（Drivers/stlink_udev + contrib/60-openocd.rules）。"
        ),
    )
    parser.parse_args([] if argv is None else argv)
    install_stlink_driver()


def main(argv: list[str] | None = None) -> None:
    if argv is None:
        argv = sys.argv[1:]
    if not argv:
        flash_main([])
        return
    cmd = argv[0]
    rest = argv[1:]
    dispatch = {
        "install-openocd": install_openocd_main,
        "install-stlink-driver": install_stlink_driver_main,
        "set-buildenv": set_buildenv_main,
        "flash": flash_main,
        "program": flash_main,
        "debug-server": debug_server_main,
        "debug": debug_server_main,
    }
    handler = dispatch.get(cmd)
    if handler is None:
        flash_main(argv)
        return
    handler(rest)


if __name__ == "__main__":
    main()
