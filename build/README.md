# build/ —— 构建脚本使用说明

这个目录只放**构建链的脚本**，不放源码、不放产物。产物一律落在 `out/`（不入库），
归档落在仓库根 `release/`（入库，见下）。

| 文件 | 作用 | 怎么用 |
|---|---|---|
| `build.py` | 生成构建描述（Kconfig / Makefile / defconfig / .config / dtsi / dtb）并驱动编译，同时持有工具链环境逻辑 | `python build/build.py <子命令>` |
| `refcheck.py` | 只读盘查：层边、环、产物一致性、DTS 派生链、头守卫、忽略规则 | 只能经 `python build/build.py check` 调用 |
| `release.py` | 把最新一次构建的四件套收进 `release/<日期>/<档位>/` | `python build/release.py [--date YYYY-MM-DD]` |

`build.py` 会自行把 `build/` 与 `project/` 放进 `sys.path`，所以**必须从仓库根目录调用**（或给足相对路径），
不要 `cd build` 后再跑。

## 一、build.py

```
python build/build.py {build|update|clean|check} [--fast|--no-update] [--variant userdebug|user]
python build/build.py install-toolchain [--version 13.3.rel1] [--make-only] [--from "<Git>\usr\bin"]
python build/build.py set-buildenv [--print bash|powershell|json] [--check]
```

### update —— 只刷新构建描述，不编译

扫描源码树，生成各层的 `Kconfig` / `Makefile`、`project/config/stm32f429_defconfig_common`、
`out/<variant>/.config`；再由 `docs/hardware/pinmap/gen_dts.py` 生成 `default` / `overlay` 两份 dtsi，
用 `dtc` 编成 `out/dts/apollo.dtb`。

**判据**：零 dtc 诊断才叫通过。改了 pinmap 表、改了 `gen_dts.py`、加了新目录或新 `.c`，都必须先跑它。

### build —— 编译，默认先 update 再双档

默认依次编译 `userdebug`（`OPT=-Og DEBUG=-g3`）与 `user`（`OPT=-Os DEBUG=-g0`），每档编完把
`.elf/.hex/.bin/.map` 拷到 `out/<档位>/release/<时间戳>/`：

- `--fast`（别名 `--no-update`）：跳过 update 直接增量编译。**只改源码时可用**；
  改了 Kconfig / defconfig / Makefile / pinmap 一定不要用，否则编出的东西用的是旧描述。
  跳过 update 时如果 `out/dts/apollo.dtb` 不存在会直接报错退出（避免失败拖到类似 `.incbin` 的位置）。
- `--variant userdebug|user`：只编一档。

`out/<档位>/release/` 的目录名是时间戳，**同一个桶 `RELEASE_BUCKET_DAYS` 天内会复用**（不每编一次就新开一个目录）。
`.map` 拷出时会把工程根绝对路径折成 `.`，保证不同机器/目录下产物可复现。

### clean

删 `out/`，并顺手清掉 MSYS 在 `tool/**/tmp` 留下的空目录（那是 make 的副作用，正式临时目录是 `out/tmp`）。

### check —— 只读盘查

转调 `refcheck.py`，六块判据（**不是门禁，不改任何文件、不设基线数字**）：

- **A 层边**：跨层 `#include` 处数，越级（引用方不允许的层）直接失败；
- **B 环**：文件级 `#include` 图跑 Tarjan SCC，再把边折到层查层级环；`osl/**/freertos/**` 内部引用不计入；
- **C 产物**：DTB 的 FDT magic、大小、elf 里 `.dtb` 段的 4 字节对齐，以及 6 个必须存在的公开符号
  （`bsl_fdt_init`、`bsl_fdt_prop_flag`、`board_rt_led_count`、`board_system_init`、`g_panic_record`、
  `osl_panic_report`）——少一个就说明某次修复被回退了；
- **D DTS 派生链**：把 `gen_dts.py` 重跑到临时目录，与 `project/dts/` 两份 dtsi 逐字节比，再复算
  `out/dts/apollo.dts` 全文。抓「改了 pinmap 却没跑 update」——此时 dtsi/dts/dtb/elf 一起停在旧坐标，编译不报错；
- **E 头守卫**：自研头（vendored 除外）必须成对 `#ifndef/#define` + 末尾 `#endif`，且守卫名不重名；
- **F 忽略规则**：用 `git check-ignore` 逐条问 `out/` 的 9 条产物路径是否真被忽略，不读 `.gitignore` 的字面形状；
- **G API 面**：自研头里声明的函数必须能在 userdebug 镜像里用 `nm` 查到符号。`--gc-sections` 会把零调用的
  函数丢掉、头文件却仍在声明，编译/链接/`-Wmissing-prototypes` 全都不报，只有 `nm` 查得出这种“假 API”。

退出码 0 = 全过，1 = 有越级边 / 有环 / 产物不一致 / 派生链过期 / 守卫缺陷 / 忽略缺失 / 头里有声明镜像里没符号。
拿不到 `arm-none-eabi-nm` 或还没编过 userdebug 时，G 段（以及 C 的符号部分）会跳过。

### install-toolchain / set-buildenv

`install-toolchain` 把 Arm GNU 工具链装进 `tool/<host>/`（包缓存放 `tool/.cache/`），Windows 下可
`--make-only --from "<Git>\usr\bin"` 只补 `make.exe` 和 `sh.exe`（make 需要 sh，缺了就报
“could not find /tmp” 之类的警告刷屏）。`set-buildenv --check` 体检；`--print powershell|bash|json`
输出可直接 eval 的环境变量。**编译时 `build.py` 会自己 apply 同一套变量，不必手动设。**

## 二、refcheck.py

六个判据 A–G 的实现都在这个文件里，正常入口是 `python build/build.py check`（由 `build.py` 先 ensure
工具链 PATH）。也带 `if __name__ == "__main__"`，可以直接 `python build/refcheck.py` 跑，但那时 PATH 里
没有 `tool/win_x86_64/bin`，`nm` 找不到会静默少检 C/G 两段——要单独跑就先
`python build/build.py set-buildenv --print powershell | Invoke-Expression`。

## 三、release.py —— 归档四件套

```
python build/release.py                 # 用今天日期
python build/release.py --date 2026-11-28
```

对 `userdebug` / `user` 各取 `out/<档位>/release/<时间戳>/` 里**最新**的那个目录，把
`stm32-freertos.elf/.hex/.bin/.map` 拷到 `release/<日期>/<档位>/`，并写 `release/<日期>/RELEASE.txt`
（来源目录、每个文件字节数、拷贝时间）。

- 判据是四件套齐全且非空，任一档缺文件就 `exit 1` 并打印缺了哪个（方便直接看退出码）；
- 拷入后 `.hex`/`.map` 统一转 LF —— `.gitattributes` 里它们是 `text eol=lf`，而 objcopy 与链接器在
  Windows 上写 CRLF，不转的话每周归档都会显示“改了行尾”；
- `release/` **是入库的**（`.gitignore` 只挡 `out/`、`logs/`、`tool/.cache/`、`.vscode/`），归档后随代码
  一起提交并双远端推送。静态库不进 `release/`，那是构建中间物。

之前还有一个 `weekly_release.ps1`（update → 双档 build → release.py 的定时编排脚本），已删除；
现在归档三步自己串：

```
python build/build.py update
python build/build.py build
python build/release.py
```

## 四、约定与坑

- **拿退出码要走 `Start-Process`**：`python build/build.py build | ...` 这种管道形式在 PowerShell 里
  拿不到 `exit code`，用
  `Start-Process -FilePath <python> -ArgumentList 'build/build.py','build' -Wait -PassThru -NoNewWindow -RedirectStandardOutput <log> -RedirectStandardError <err>`。
- **`-Werror` 环境下双档都必须过**；`user` 档是量产出货档，不看它等于没编。
- 过程日志一律写仓库根 `logs/`（不入库），临时脚本、commit message 不落仓库。
- 不要手工改 `Kconfig` / `Makefile`：都是 `build.py update` 的生成物，改了下一次 update 就覆盖；
  要改规则请改 `project/generate.py`。
- 新增顶层目录默认会被当“层”扫描。`release/`、`debug/`、`out/` 等已在 `generate.py` 的 `EXCLUDE_DIRS`
  里；`debug/` 有子目录时若不排除，`update` 会在里面生成 `Kconfig`/`Makefile`。
