"""`build` / `clean`：驱动 make，产物断言与 out/release 拷贝。"""
from __future__ import annotations

import argparse
import os
import shutil
import subprocess
import sys
from datetime import datetime, timedelta
from pathlib import Path

import contracts
from contracts import *  # noqa: F403
import generate
import environment as te
import verify

def select_release_bucket(now: datetime | None = None) -> Path:
    """选择本次 build 写入的 `out/release/` 子目录。

    目录名形如 `2026_11_12-22_50_30`。距当前最新桶已满 RELEASE_BUCKET_DAYS 天则新建，
    否则覆写写入该桶（同 5 日窗口内不堆新目录）。
    """
    now = now or datetime.now()
    OUT_RELEASE_DIR.mkdir(parents=True, exist_ok=True)
    buckets: list[tuple[datetime, Path]] = []
    for child in OUT_RELEASE_DIR.iterdir():
        if not child.is_dir() or not RELEASE_DIR_NAME_RE.match(child.name):
            continue
        try:
            started = datetime.strptime(child.name, "%Y_%m_%d-%H_%M_%S")
        except ValueError:
            continue
        buckets.append((started, child))
    if buckets:
        buckets.sort(key=lambda item: item[0], reverse=True)
        latest_start, latest_dir = buckets[0]
        if now - latest_start < timedelta(days=RELEASE_BUCKET_DAYS):
            return latest_dir
    dest = OUT_RELEASE_DIR / now.strftime("%Y_%m_%d-%H_%M_%S")
    dest.mkdir(parents=True)
    return dest
def copy_outputs() -> None:
    """只复制发布产物到 `out/release/<时间戳>/`。

    静态库（尤其带 -g3 的 libhal.a / libosl.a）属于构建中间物，体积近百 MB，
    不进 release/，避免发布目录变成工程垃圾场。
    """
    release = select_release_bucket()
    artifacts = [
        path
        for path in OUT_ARTIFACT_DIR.iterdir()
        if path.is_file() and path.suffix in {".bin", ".hex", ".elf", ".map"}
    ]
    if not artifacts:
        raise SystemExit("编译结束，但 out/object/target/ 下没有 .elf/.hex/.bin/.map 可复制")
    for path in sorted(artifacts):
        dest = release / path.name
        shutil.copy2(path, dest)
        if dest.suffix == ".map":
            fold_root_in_map(dest)
        print(f"复制 {path.relative_to(ROOT)} -> {dest.relative_to(ROOT)} → {dest.stat().st_size} 字节")
def fold_root_in_map(dest: Path) -> None:
    """把 .map 里的工程根绝对路径折成 "."，让产物在别的目录/机器上也一致。

    链接器只记录它收到的路径，-ffile-prefix-map 管不到 map 文件：实测同一提交
    Windows 266831 B、Linux 325007 B、GitHub runner 304955 B，差异全是根路径长度。
    map 不参与镜像，这里只做文本归一化，保证 artifact 里四个文件都可复现。
    """
    text = dest.read_text(encoding="utf-8", errors="replace")
    for prefix in (
        ROOT.as_posix(),
        str(ROOT),
        ROOT.as_posix().upper(),
        str(ROOT).upper(),
    ):
        text = text.replace(prefix, ".")
    dest.write_text(text, encoding="utf-8", newline="\n")
def build_env() -> dict[str, str]:
    return te.apply(os.environ)
def resolve_compiler() -> str | None:
    gcc = te.resolve_gcc()
    if gcc is not None:
        return str(gcc)
    return shutil.which("arm-none-eabi-gcc")
def build() -> None:
    generate.update()
    compiler = resolve_compiler()
    if compiler is None:
        raise SystemExit(
            "未找到 arm-none-eabi-gcc。请先运行: python3 build/build.py install-toolchain\n"
            "或安装系统包: sudo apt install gcc-arm-none-eabi binutils-arm-none-eabi"
        )
    make_cmd = "make"
    make_argv: list[str] = []
    if te.host_os() == "win32":
        make_path = te.resolve_make()
        if make_path is None:
            raise SystemExit(
                "未找到 tool/win_x86_64/bin/make.exe。"
                "请运行: python build/build.py install-toolchain --make-only --from \"<Git>\\usr\\bin\""
            )
        make_cmd = str(make_path)
        tool_bin = te.find_toolchain_bin()
        make_argv.append(f"PROJECT_ROOT={ROOT.resolve().as_posix()}")
        if tool_bin is not None:
            sh = tool_bin / "sh.exe"
            if sh.is_file():
                make_argv.append(f"SHELL={te._path_for_env(sh)}")
        make_argv.append(f"PYTHON={Path(sys.executable).as_posix()}")
    print("编译工程")
    result = subprocess.run(
        [make_cmd, "-C", str(ROOT), *make_argv, "all"],
        check=False,
        env=build_env(),
    )
    if result.returncode != 0:
        raise SystemExit(result.returncode)
    tool_errors, tools_ok = verify.artifact_tool_errors()
    errors = list(tool_errors)
    if tools_ok:
        # 与 verify() 的 check_symbols 分支逐条对齐：`build` 绿就必须等于"全部产物
        # 断言绿"，否则本地只跑 build 会漏掉只在 verify 里跑的那几条
        # （lib_member / observ_artifact / tick_source，都是反汇编或 map 级判据）。
        errors.extend(verify.collect_artifact_errors())
        errors.extend(verify.collect_heap_artifact_errors())
        errors.extend(verify.collect_lib_member_errors())
        errors.extend(verify.collect_freertos_unit_errors())
        errors.extend(verify.collect_unlinked_member_errors())
        errors.extend(verify.collect_fault_artifact_errors())
        errors.extend(verify.collect_observ_artifact_errors())
        errors.extend(verify.collect_bsp_artifact_errors())
        errors.extend(verify.collect_persist_record_errors())
        errors.extend(verify.collect_boot_init_errors())
        errors.extend(verify.collect_tick_source_errors())
        errors.extend(verify.collect_startup_guard_errors())
        errors.extend(verify.collect_last_write_errors())
        errors.extend(verify.collect_panic_order_errors())
    if errors:
        print("产物断言失败：", file=sys.stderr)
        for item in errors:
            print(f"  - {item}", file=sys.stderr)
        raise SystemExit(
            "产物断言失败：编译通过不等于镜像里有对应实现，逐条看上面列出的项"
            "（应用代码未进镜像 / 弱符号被抢占 / 实现被 --gc-sections 丢弃 / 资源越界）。"
        )
    copy_outputs()
    te.remove_msys_tmp()
    te.remove_stray_tool_tmp_dirs()
def clean() -> None:
    print("清理编译产物")
    makefile = ROOT / "Makefile"
    make_cmd = te.resolve_make() if te.host_os() == "win32" else shutil.which("make")
    if makefile.is_file() and make_cmd is not None:
        # 直接写 "make" 在 Windows 会抛 FileNotFoundError 并让 clean 半途而废（out/ 反而没删）。
        subprocess.run([str(make_cmd), "-C", str(ROOT), "clean"], check=False, env=build_env())
    out = ROOT / "out"
    if out.exists():
        shutil.rmtree(out)
    te.remove_stray_tool_tmp_dirs()
    print("已删除 out/")


def main(argv: list[str] | None = None) -> None:
    parser = argparse.ArgumentParser(
        description="生成 STM32 工程描述并编译。",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""\
子命令说明:
  update  扫描源码树，生成 Kconfig、Makefile、toolchain.make、project_defconfig、out/.config，并自检
  verify  检查生成物、层目录、链接顺序与 .skills/工程规格.md、SKILL.md 是否一致
          （加 --artifacts 时额外校验现有 elf 里的应用代码）
  build   先 update（含自检），再 make 全量编译，产物复制到 out/release/<时间戳>/
  clean   删除 out/；并移除空的 tool/**/tmp（MSYS 副作用，正式临时目录为 out/tmp）

示例:
  python3 build/build.py update
  python3 build/build.py verify
  python3 build/build.py build
  python3 build/build.py clean

工具链环境: python3 build/build.py install-toolchain / set-buildenv
刷机环境见: python3 flash/flash.py
""",
    )
    parser.add_argument(
        "option",
        choices=(
            "build",
            "update",
            "verify",
            "clean",
            "gate-selftest",
        ),
        metavar="{build|update|verify|clean|gate-selftest}",
        help="build 会先 update；update 刷新描述并自检；verify 仅自检",
    )
    parser.add_argument(
        "--artifacts",
        action="store_true",
        help="verify 时额外校验现有 elf 产物（强 main、vTaskStartScheduler、.text 下限）",
    )
    if argv is None:
        argv = sys.argv[1:]
    if not argv or argv[0] in ("-h", "--help"):
        parser.print_help()
        raise SystemExit(0 if argv and argv[0] in ("-h", "--help") else 2)
    env_dispatch = {
        "install-toolchain": te.install_toolchain_main,
        "set-buildenv": te.set_buildenv_main,
    }
    if argv[0] in env_dispatch:
        env_dispatch[argv[0]](argv[1:])
        return
    option = argv[0]
    rest = argv[1:]
    class Args:
        pass
    args = Args()
    args.option = option
    args.artifacts = "--artifacts" in rest
    if args.option == "build":
        build()
    elif args.option == "update":
        generate.update()
    elif args.option == "verify":
        verify.verify(artifacts=args.artifacts)
    elif args.option == "gate-selftest":
        raise SystemExit(verify.gate_selftest_main(rest))
    elif args.option == "clean":
        clean()
    else:
        parser.error(f"未知子命令: {args.option}")


if __name__ == "__main__":
    main()
