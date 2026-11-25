#!/usr/bin/env python3
"""tests/stability.py —— 功能稳定性回归（host 侧，一条命令跑完）

与 tests/test_case.md 的分工：test_case.md 是逐条验收清单（含人工 M 类项），
本脚本只承载**可自动判定**的稳定性部分，把"上电一次能跑"升级成
"重复上电一致 + 长时间运行不变形"。

四个检查（--only 可选其一）：

  dtb    : 镜像里 .dtb 段与 out/dts/apollo.dtb 逐字节一致。
           防的是"改了 DTS 但 board_dtb.o 没重编"，镜像里继续嵌旧整树
           ——2026-11-22 蜂鸣器极性改高有效后刷进去仍长鸣，就是这一类。
  reset  : N 轮复位，每轮抓启动串口 banner 逐字节与黄金串比对，
           再读一组运行态寄存器；轮与轮之间要求除 g_tick_ms 外完全一致。
           防的是"偶发丢字符 / 偶发设备初始化失败 / 极性偶尔反"。
  soak   : 长跑 T 秒，全程盯着串口（期间只要冒出 banner 就是意外重启），
           在首/中/尾各采一次 g_tick_ms 与墙钟对算 tick 漂移，
           并看 LED 是否仍在翻转、健康计数是否保持归零。
           防的是"跑一会就复位、tick 走偏、日志任务卡死、蜂鸣器抖回长鸣"。
  all    : dtb → reset → soak 顺序跑（默认）。

用法（在仓库根目录执行）:

    python tests\\stability.py                                  # 默认 userdebug，3 轮 + 30 s 长跑
    python tests\\stability.py --variant user --cycles 10 --soak 300
    python tests\\stability.py --only soak --soak 120 --port COM3
    python tests\\stability.py --only dtb --elf out/userdebug/objects/target/stm32-freertos.elf

退出码：0 全通过（含 SKIP）；1 有 FAIL。报告落 out/tests/stability_<时间戳>.json。

串口独占：脚本自己开端口，别同时挂着别的串口助手，否则打不开端口直接报错。
"""

from __future__ import annotations

import argparse
import json
import re
import subprocess
import sys
import tempfile
import time
from dataclasses import dataclass
from datetime import datetime
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
if str(ROOT / "debug") not in sys.path:
    sys.path.insert(0, str(ROOT / "debug"))

import capture as cap  # noqa: E402  （复用 nm / openocd / mdw 解析）

BAUD = 115200

# 启动 banner 黄金串：board_debug_uart.c 的 greeting + bsl_log_startup_banner 的分段拼接，
# 实测 45 字节。改了文案就要改这里，否则断言失败是"提醒"，不是"缺陷"。
BANNER = b"\r\nstm32-freertos: USART1 115200 debug ready\r\n"

# 读的运行态符号：(名字, 32 位字数)。地址在每次运行时用 nm 现取，不写死——
# 重编后符号位移会让写死的地址读错地方，那是脚本自己在制造假阳性。
SYMS: dict[str, int] = {
    "s_pcf8574_shadow": 1,   # PC0..P7 影子，bit0 = BEEP
    "s_active_high": 1,      # 1 = 高有效（BEEP 亮电平为 1）
    "s_beep_bit": 1,         # BEEP 落在 PCF8574 的哪一位（板上是 0）
    "s_state": 2,            # 6 个设备状态字节：0=UNINIT 1=READY 2=FAILED
    "s_tx_fail": 1,          # HAL_UART_Transmit 非 OK 次数
    "s_tx_remain": 1,        # 上次失败剩下的待发字节数
    "s_uart_drops": 1,       # 串口写超时导致丢掉的行数
    "s_banner_done": 1,      # banner 是否已发完
    "g_tick_ms": 1,          # osl 侧毫秒累加器（tick 漂移的被测量）
    "g_boot_stage": 1,       # 0=board_system_init 全成功；1/2=启动阶段失败点
    "g_iwdg_sync_fail": 1,   # 看门狗喂狗超时/同步失败计数
}

# 轮与轮之间允许不同的量（时间是单调走的，本来就不该相等）
VOLATILE = {"tick_ms"}

SYS_DEV_READY = 1
GPIOB_ODR = 0x40020414  # LED1/LED2 在 PB0/PB1


# --------------------------------------------------------------------------- 结果

@dataclass
class Result:
    name: str
    status: str          # PASS / FAIL / SKIP
    detail: str
    data: dict | None = None


RESULTS: list[Result] = []


def add(name: str, status: str, detail: str, data: dict | None = None) -> None:
    RESULTS.append(Result(name, status, detail, data))
    mark = {"PASS": "[PASS]", "FAIL": "[FAIL]", "SKIP": "[SKIP]"}[status]
    print(f"  {mark} {name}: {detail}", flush=True)


# --------------------------------------------------------------------------- 板级通道

def resolve_syms(elf: Path) -> dict[str, int]:
    """用 nm 现取符号地址；缺符号直接报出来（不猜地址）。"""
    bindir = cap.resolve_toolchain_bin()
    nm = cap.tool_path(bindir, "arm-none-eabi-nm") if bindir else None
    if nm is None:
        raise SystemExit("找不到 arm-none-eabi-nm：先确认 toolchain_env 能被 apply()")
    addrs: dict[str, int] = {}
    missing: list[str] = []
    for name in SYMS:
        addr = cap.nm_symbol(elf, name, nm)
        if addr is None:
            missing.append(name)
        else:
            addrs[name] = addr
    if missing:
        raise SystemExit(
            "ELF 里没有这些符号（是不是档位/优化把静态变量优化掉了？）："
            + ", ".join(missing)
        )
    return addrs


def mdw_words(raw: str, addr: int) -> list[int]:
    """从 openocd 输出里取某个地址那一段。只认"行首就是该地址"的打印，
    并且取**最后一次**出现：halt 请求遇到 target 未就绪时 openocd 会重试，
    同一条 mdw 可能打印多遍，按 cap.parse_mdw_words 的宽松正则会把多遍
    的词拼在一起（现场报过"期望 1 字解析到 2 字"）。"""
    groups = mdw_groups(raw, addr)
    return groups[-1] if groups else []


def mdw_groups(raw: str, addr: int) -> list[list[int]]:
    """同一地址的**每一次**打印各算一组。采样类检查要多点取值，不能只留最后一次。"""
    # 只认"行首就是该地址"的打印，且空白限定为同一行内（[^\S\n]）：
    # 用 \s 会跨行吃到下一条 `0x...` 的地址数字，把 1 字读成 2 字。
    pat = re.compile(rf"^0x{addr:08x}:((?:[^\S\n]+[0-9a-fA-F]+)+)", re.I | re.M)
    return [[int(w, 16) for w in m.group(1).split()] for m in pat.finditer(raw)]


def read_syms(addrs: dict[str, int], names: list[str]) -> dict[str, list[int]]:
    """一次 openocd 会话读完全部字；halt 窗口只有几个 mdw 的时间（远小于 IWDG 1 s）。"""
    cmds = ["init", "halt"]
    for name in names:
        cmds.append(f"mdw 0x{addrs[name]:08x} {SYMS[name]}")
    cmds.append("resume")           # 读完必须放行，否则 CPU 冻在 halt，IWDG 会咬
    raw = cap.run_openocd(cmds)
    vals: dict[str, list[int]] = {}
    for name in names:
        words = mdw_words(raw, addrs[name])
        if len(words) != SYMS[name]:
            raise SystemExit(
                f"读 {name} 失败：期望 {SYMS[name]} 字，解析到 {len(words)} 字\n{raw[-400:]}"
            )
        vals[name] = words
    return vals


def snapshot(syms: dict[str, list[int]]) -> dict[str, object]:
    """把寄存器值摊平成便于比对/落报告的形式。"""
    st = syms["s_state"]
    state_bytes = []
    for w in st:
        for i in range(4):
            state_bytes.append((w >> (8 * i)) & 0xFF)
    return {
        "shadow": syms["s_pcf8574_shadow"][0],
        "active_high": syms["s_active_high"][0],
        "beep_bit": syms["s_beep_bit"][0],
        "dev_state": state_bytes[:6],
        "tx_fail": syms["s_tx_fail"][0],
        "tx_remain": syms["s_tx_remain"][0],
        "uart_drops": syms["s_uart_drops"][0],
        "banner_done": syms["s_banner_done"][0],
        "boot_stage": syms["g_boot_stage"][0],
        "iwdg_sync_fail": syms["g_iwdg_sync_fail"][0],
        "tick_ms": syms["g_tick_ms"][0],
    }


def invariants(snap: dict[str, object]) -> list[str]:
    """单次快照必须满足的不变量；返回违反项清单（空 = 通过）。"""
    bad: list[str] = []
    bit = int(snap["beep_bit"])
    if not 0 <= bit <= 7:
        bad.append(f"beep_bit={bit} 越界（PCF8574 只有 P0..P7）")
    shadow = int(snap["shadow"])
    if int(snap["active_high"]) == 1:
        on_level = 1
    else:
        on_level = 0
    if ((shadow >> bit) & 1) == on_level:
        bad.append(
            f"蜂鸣器停在响电平：shadow=0x{shadow:02X} bit{bit}="
            f"{((shadow >> bit) & 1)} active_high={snap['active_high']}"
        )
    # 极性本身来自 DTB（本板实测低有效 → active_high=0，关电平=1），
    # 这里不二次硬编码，只校验"当前输出 ≠ 响电平"。
    lost = [i for i, s in enumerate(snap["dev_state"]) if s != SYS_DEV_READY]
    if lost:
        bad.append(f"设备未 READY：下标 {lost} 的状态={[snap['dev_state'][i] for i in lost]}")
    if int(snap["tx_fail"]) != 0:
        bad.append(f"s_tx_fail={snap['tx_fail']}，串口发送失败过")
    if int(snap["tx_remain"]) != 0:
        bad.append(f"s_tx_remain={snap['tx_remain']}，有没发完的字节")
    if int(snap["uart_drops"]) != 0:
        bad.append(f"s_uart_drops={snap['uart_drops']}，超时丢了行")
    if int(snap["banner_done"]) != 1:
        bad.append(f"s_banner_done={snap['banner_done']}，banner 没发完")
    if int(snap["iwdg_sync_fail"]) != 0:
        bad.append(f"g_iwdg_sync_fail={snap['iwdg_sync_fail']}，喂狗异常")
    return bad


def pick_port(explicit: str | None) -> str | None:
    if explicit:
        return explicit
    try:
        from serial.tools import list_ports
    except Exception:
        return None
    # 刷机/复位后 USB 重新枚举，CH340 会短暂从设备列表消失；重试几次再判"没有串口"，
    # 免得把 USB 抖动误报成"线没插"。
    ports = []
    for _ in range(6):
        ports = list(list_ports.comports())
        if ports:
            break
        time.sleep(0.5)
    if not ports:
        return None
    for p in ports:
        text = f"{p.description} {p.hwid}".upper()
        if "CH340" in text or "STLINK" in text or "ST-LINK" in text or "USB-SERIAL" in text:
            return p.device
    return ports[0].device


def open_port(port: str):
    import serial

    ser = serial.Serial()
    ser.port = port
    ser.baudrate = BAUD
    ser.timeout = 0.05
    # 必须在 open 之前置低：CH340 默认拉高 DTR/RTS，板上是复位/BOOT0 的握手线，
    # 一开会把板子按在复位里，抓到的永远是 0 字节（11-22 现场踩过）。
    ser.dtr = False
    ser.rts = False
    ser.open()
    return ser


def capture_boot(port: str, window: float) -> bytes:
    """复位一次并抓启动串口。

    **判据起点已改（2026-11-25）**：P5a 起 banner 由 `app_sample` 的按键任务在
    调度器启动后打印，POST 的 21 项全排在它前面。所以不能再"读到 0.8 s 静默就
    收工"——POST 项之间本就有停顿，那样会在中途返回，banner 永远抓不到
    （stability 首跑 3 轮全 FAIL 就是这么来的）。
    现在：一直读到 **BANNER 完整出现**，再等 0.8 s 静默收工；超时才算失败。
    """
    ser = open_port(port)
    try:
        ser.reset_input_buffer()                       # 清掉复位前残留
        # reset 之后必须 resume：openocd 的 reset 会把核停在复位向量上，
        # 不 resume 就没有任何 POST/banner 输出（首跑抓到的 512 B 是上一轮
        # 残在下行缓冲里的 [sai] 行，s_banner_done 自然还是 0）。
        cap.run_openocd(["init", "reset", "resume", "shutdown"])  # 复位后放行
        buf = bytearray()
        t0 = last = time.monotonic()
        seen_banner = False
        while time.monotonic() - t0 < window:
            chunk = ser.read(64)
            if chunk:
                buf.extend(chunk)
                last = time.monotonic()
                if BANNER in buf:
                    seen_banner = True
            elif seen_banner and time.monotonic() - last > 0.8:
                break          # banner 已发完且静了 0.8 s
        return bytes(buf)
    finally:
        ser.close()


def hexdump(b: bytes) -> str:
    return b.hex(" ")


# --------------------------------------------------------------------------- 检查项

def check_dtb(elf: Path) -> None:
    """镜像 .dtb 段 vs out/dts/apollo.dtb 逐字节一致（含 FDT 头自检）。"""
    dtb_file = ROOT / "out" / "dts" / "apollo.dtb"
    if not dtb_file.is_file():
        add("dtb-存在", "FAIL", f"{dtb_file} 不存在，先 python build\\build.py update")
        return
    bindir = cap.resolve_toolchain_bin()
    objcopy = cap.tool_path(bindir, "arm-none-eabi-objcopy") if bindir else None
    if objcopy is None:
        add("dtb-抽取", "FAIL", "找不到 arm-none-eabi-objcopy")
        return
    expect = dtb_file.read_bytes()
    with tempfile.TemporaryDirectory() as td:
        out = Path(td) / "sec.dtb"
        proc = subprocess.run(
            [objcopy, "-O", "binary", "--only-section=.dtb", str(elf), str(out)],
            check=False, capture_output=True, text=True,
        )
        if proc.returncode != 0 or not out.is_file():
            add("dtb-抽取", "FAIL", f"objcopy 失败：{proc.stderr.strip()[:200]}")
            return
        got = out.read_bytes()

    if expect[:4] != b"\xd0\x0d\xfe\xed":
        add("dtb-魔数", "FAIL", f"文件首 4 字节 {expect[:4].hex()} 不是 d00dfeed")
        return
    total = int.from_bytes(expect[4:8], "big")
    if total != len(expect):
        add("dtb-长度自检", "FAIL", f"头里 totalsize={total}，文件实际 {len(expect)} 字节")
        return
    if got[:4] != b"\xd0\x0d\xfe\xed":
        add("dtb-魔数", "FAIL", f"镜像段首 4 字节 {got[:4].hex()} 不是 d00dfeed")
        return
    # `.dtb` 段在 board_dtb.s 里有 .align 4 收尾，段长会比 DTB 本身多几个字节；
    # 多出来的必须是纯 0 对齐填充，非零就是嵌进了别的东西。
    if len(got) < len(expect):
        add("dtb-一致", "FAIL",
            f"段长 {len(got)} < 文件 {len(expect)}，镜像里的 DTB 被截断了")
        return
    pad = got[len(expect):]
    if any(pad):
        add("dtb-一致", "FAIL",
            f"DTB 之后还有 {len(pad)} 字节非零内容：{pad.hex(' ')[:60]}")
        return
    body = got[: len(expect)]
    if body != expect:
        first = next(i for i, (a, b) in enumerate(zip(body, expect)) if a != b)
        add("dtb-一致", "FAIL",
            f"首个差异在偏移 {first}：镜像={body[first]:02x} 文件={expect[first]:02x}；"
            "说明 bsl/src/board_dtb.o 未随 apollo.dtb 重编")
        return
    add("dtb-一致", "PASS",
        f"{len(expect)} 字节逐字节一致（totalsize={total}，尾部对齐填充 {len(pad)} 字节全零）",
        {"bytes": len(expect), "pad": len(pad)})


def check_reset(port: str, elf: Path, cycles: int, window: float) -> None:
    """N 轮复位：banner 逐字节稳定 + 运行态快照轮间一致 + 不变量成立。"""
    addrs = resolve_syms(elf)
    names = list(SYMS)

    baselines: list[bytes] = []
    post_bits: list[bytes] = []
    snaps: list[dict[str, object]] = []
    for i in range(1, cycles + 1):
        boot = capture_boot(port, window)
        time.sleep(1.2)                      # 让设备初始化和 LED 首拍跑完再读
        snap = snapshot(read_syms(addrs, names))

        tag = f"第 {i}/{cycles} 轮"
        # banner 现在排在 POST 之后（见 capture_boot 的注释），判据改成：
        #   1) 完整 banner 必须出现；
        #   2) banner 之前必须有一行 `[post] result N pass N fail 0`（N>0）；
        #   3) banner 之后的运行日志每行必须是 `[模块] ASCII` 且 \r\n 收尾。
        idx = boot.find(BANNER)
        post = re.search(rb"\[post\] result (\d+) pass \1 fail 0", boot)
        if idx < 0:
            n = min(len(boot), len(BANNER))
            first = next((k for k in range(n) if boot[k] != BANNER[k]), n)
            add(f"reset-banner {tag}", "FAIL",
                f"抓不到完整 banner（收到 {len(boot)} 字节，首 {n} 字节与黄金串首个差异在 "
                f"偏移 {first}）；实收={hexdump(boot)[:160]}")
        elif post is None:
            add(f"reset-banner {tag}", "FAIL",
                "banner 之前没有 `[post] result N pass N fail 0`（POST 未跑完或全部失败）")
        else:
            tail = boot[idx + len(BANNER):]
            bad_lines = [
                ln for ln in tail.split(b"\r\n")
                if ln and not re.match(rb"^\[[a-z][a-z0-9_]{0,7}\] [\x20-\x7e]*$", ln)
            ]
            post_bits.append(post.group(0))
            if bad_lines:
                add(f"reset-banner {tag}", "FAIL",
                    "banner 之后的运行日志不合格式：" + " / ".join(
                        ln.decode("ascii", "replace")[:40] for ln in bad_lines[:3]))
            else:
                add(f"reset-banner {tag}", "PASS",
                    f"POST `{post.group(1).decode()} 项` 全过，banner 出现且其后日志格式合规")
        baselines.append(BANNER if idx >= 0 else boot)

        bad = invariants(snap)
        if bad:
            add(f"reset-不变量 {tag}", "FAIL", "；".join(bad))
        else:
            # tick 的**绝对值**不代表"上电以来"：osl_tick_ms_align() 把它对齐到
            # DWT->CYCCNT 的折算值，而 CYCCNT 属调试域、系统复位不清零、25.6 s
            # 回绕一次（接调试器时必然如此）。所有判据都只用差分，别按绝对值下结论。
            add(f"reset-不变量 {tag}", "PASS",
                f"shadow=0x{int(snap['shadow']):02X} 设备全 READY "
                f"tick(绝对值,受DWT跨复位影响)={snap['tick_ms']}")
        snaps.append(snap)

    # 轮间一致：除 g_tick_ms 外逐字段比
    if snaps:
        keys = [k for k in snaps[0] if k not in VOLATILE]
        diffs: list[str] = []
        ref = snaps[0]
        for idx, s in enumerate(snaps[1:], start=2):
            for k in keys:
                if s[k] != ref[k]:
                    diffs.append(f"第{idx}轮 {k}: {s[k]} ≠ {ref[k]}")
        if diffs:
            add("reset-轮间一致", "FAIL", "；".join(diffs[:6]))
        else:
            add("reset-轮间一致", "PASS", f"{cycles} 轮 {len(keys)} 个字段完全一致")
    if baselines:
        if len(set(baselines)) == 1 and baselines[0] == BANNER:
            if post_bits and len(set(post_bits)) == 1:
                add("reset-banner稳定", "PASS",
                    f"{cycles} 轮 banner 均为 {len(BANNER)} 字节同内容，"
                    f"且每轮 POST 同一条 `{post_bits[0].decode()}`")
            else:
                add("reset-banner稳定", "FAIL",
                    f"{cycles} 轮里 POST 结果不一致：{[b.decode() for b in post_bits]}")
        else:
            add("reset-banner稳定", "FAIL",
                f"{cycles} 轮里 banner 出现 {len(set(baselines))} 种内容/长度，不满足逐字节稳定")


def check_soak(port: str, elf: Path, seconds: float) -> None:
    """长跑：串口全程盯重启 + tick 漂移 + 健康计数归零。

    **起点改（2026-11-25）**：先完整启动一次（读到 banner + 1 s），再开长跑窗口。
    首跑时 soak 紧接 reset 的最后一次"半截启动"开始，于是 POST 的 `[app_qr]` 行
    被当成"意外重启"，tick 起点也早于调度器 —— 比值与重启两项同时误判。
    """
    addrs = resolve_syms(elf)
    names = list(SYMS)
    _boot = capture_boot(port, 20.0)   # 完整启动一次（端口由它自己开关）
    time.sleep(1.0)                    # 等 banner 之后的运行期日志落定
    ser = open_port(port)
    stray = bytearray()
    try:
        ser.reset_input_buffer()
        t_start = time.monotonic()
        snap0 = snapshot(read_syms(addrs, names))
        bad = invariants(snap0)
        if bad:
            add("soak-起点", "FAIL", "；".join(bad))
        else:
            add("soak-起点", "PASS", f"tick={snap0['tick_ms']} ms，健康计数归零")

        mid_done = False
        snap_mid = snap_last = None
        t_mid = t_last = t_start
        while True:
            now = time.monotonic()
            elapsed = now - t_start
            if elapsed >= seconds and snap_last is not None:
                break
            chunk = ser.read(64)
            if chunk:
                stray.extend(chunk)
            if not mid_done and elapsed >= seconds / 2 and seconds >= 6:
                # 中点样本：openocd 会话期间板子在跑（只在 mdw 那几毫秒 halt），
                # 串口由驱动缓冲兜住，回来继续排空。
                t_mid = time.monotonic()
                snap_mid = snapshot(read_syms(addrs, names))
                mid_done = True
            elif elapsed >= seconds and snap_last is None:
                t_last = time.monotonic()
                snap_last = snapshot(read_syms(addrs, names))
        snap_last = snap_last or snapshot(read_syms(addrs, names))
        wall_ms = (t_last - t_start) * 1000.0
        tick_ms = float(int(snap_last["tick_ms"]) - int(snap0["tick_ms"]))
        # 下界放宽到 0.97：采样时 halt 的几毫秒不计入 tick，只会让比值偏小。
        ratio = tick_ms / wall_ms if wall_ms > 0 else 0.0
        if int(snap_last["tick_ms"]) < int(snap0["tick_ms"]):
            add("soak-tick漂移", "FAIL",
                f"tick 回落：{snap0['tick_ms']} → {snap_last['tick_ms']}，期间复位过")
        elif not 0.97 <= ratio <= 1.03:
            add("soak-tick漂移", "FAIL",
                f"{seconds:.0f} s 墙钟走 tick {tick_ms:.0f} ms，比值 {ratio:.3f}（允差 0.97~1.03）")
        else:
            add("soak-tick漂移", "PASS",
                f"{seconds:.0f} s 墙钟 / tick {tick_ms:.0f} ms，比值 {ratio:.3f}")

        bad = invariants(snap_last)
        if bad:
            add("soak-终点", "FAIL", "；".join(bad))
        else:
            add("soak-终点", "PASS",
                f"shadow=0x{int(snap_last['shadow']):02X} 设备全 READY "
                f"tick(绝对值)={snap_last['tick_ms']}")

        if snap_mid is not None:
            bad_mid = invariants(snap_mid)
            if bad_mid:
                add("soak-中点", "FAIL", "；".join(bad_mid))

        # 期间串口只要冒出**完整 banner**，就是意外重启（IWDG 咬 / panic 复位）。
        # 用完整串而不是片段：POST 的项名里没有 "stm32-freertos"，但 `[app_qr]`
        # 这类运行日志不该被判成 banner（首跑就误判在这）。
        if BANNER in stray or b"stm32-freertos" in stray:
            add("soak-无意外重启", "FAIL",
                f"长跑中收到 {len(stray)} 字节串口输出，含 banner（说明复位过）：{hexdump(stray)[:120]}")
        else:
            add("soak-无意外重启", "PASS",
                f"期间无 banner（收到 {len(stray)} 字节运行日志）")

        # LED 心跳：P5a 起 app_sample 的 1 Hz 翻转已删（LED 归 app_led 独占，
        # 否则"屏上状态"与"灯的实际状态"会在 1 s 内对不上，见 app_sample.c 第 15-20 行）。
        # 这一项不再适用；活性由 tick 漂移 + 不变量判。
        add("soak-LED翻转", "SKIP",
            "P5a 起运行期无 1 Hz LED 心跳（app_sample.c 注释），该项已不适用")
    finally:
        ser.close()


def sample_odr(count: int, gap_ms: int) -> list[int]:
    cmds = ["init", "halt"]
    for _ in range(count):
        cmds += [f"mdw 0x{GPIOB_ODR:08x} 1", "resume", f"sleep {gap_ms}", "halt"]
    cmds.append("resume")
    raw = cap.run_openocd(cmds)
    # 取每一次采样（不是最后一次），三次采样才能看出 LED 在不在翻转
    return [g[0] for g in mdw_groups(raw, GPIOB_ODR) if g]


# --------------------------------------------------------------------------- 入口

def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(
        description="功能稳定性回归（dtb / reset / soak）",
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    ap.add_argument("--variant", default="userdebug", choices=["userdebug", "user"])
    ap.add_argument("--elf", default=None, help="直接指定 ELF（覆盖 --variant）")
    ap.add_argument("--port", default=None, help="串口设备，默认自动按 CH340/STLink 挑")
    ap.add_argument("--cycles", type=int, default=3, help="复位轮数（默认 3）")
    ap.add_argument("--window", type=float, default=20.0,
                    help="每轮抓启动（POST + banner）的窗口秒数；POST 21 项在 8080 屏上要数秒")
    ap.add_argument("--soak", type=float, default=30.0, help="长跑秒数（默认 30）")
    ap.add_argument("--only", default="all", choices=["all", "dtb", "reset", "soak"])
    args = ap.parse_args(argv)

    elf = Path(args.elf) if args.elf else (
        ROOT / "out" / args.variant / "objects" / "target" / "stm32-freertos.elf"
    )
    if not elf.is_file():
        print(f"[FAIL] 找不到 ELF：{elf}，先 python build\\build.py build --variant {args.variant}")
        return 1

    print(f"ELF: {elf}")
    print(f"检查: {args.only}  复位轮数={args.cycles}  长跑={args.soak:.0f}s", flush=True)

    need_port = args.only in ("all", "reset", "soak")
    port = pick_port(args.port) if need_port else None
    if need_port and port is None:
        add("串口", "SKIP", "没找到可用串口：接上板子的 USB-UART，或用 --port COMx 指定；"
                          "只跑 dtb 可用 --only dtb")
        need_port = False

    if args.only in ("all", "dtb"):
        check_dtb(elf)
    if args.only in ("all", "reset") and need_port:
        check_reset(port, elf, args.cycles, args.window)
    if args.only in ("all", "soak") and need_port:
        check_soak(port, elf, args.soak)

    fails = [r for r in RESULTS if r.status == "FAIL"]
    skips = [r for r in RESULTS if r.status == "SKIP"]
    print()
    print(f"汇总: {len(RESULTS)} 项 → PASS {len(RESULTS) - len(fails) - len(skips)}"
          f" / FAIL {len(fails)} / SKIP {len(skips)}")

    report_dir = ROOT / "out" / "tests"
    report_dir.mkdir(parents=True, exist_ok=True)
    tag = datetime.now().strftime("%Y_%m_%d-%H_%M_%S")
    path = report_dir / f"stability_{tag}.json"
    path.write_text(json.dumps({
        "when": tag,
        "elf": str(elf),
        "variant": args.variant,
        "port": port,
        "cycles": args.cycles,
        "soak_s": args.soak,
        "results": [{"name": r.name, "status": r.status, "detail": r.detail, "data": r.data}
                    for r in RESULTS],
    }, ensure_ascii=False, indent=2), encoding="utf-8")
    print(f"报告: {path}")
    return 1 if fails else 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except KeyboardInterrupt:
        print("\n中断（可能停在 halt，补一次 resume 让板子继续跑）")
        cap.run_openocd(["init", "resume", "shutdown"])
        sys.exit(130)
