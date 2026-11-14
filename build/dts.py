"""板级 Device DTS 解析、头文件生成与一致性校验。

本工程不运行 Linux dtc；`.dts` 使用约定子集，由本模块解析。
"""
from __future__ import annotations

import re
from dataclasses import dataclass
from pathlib import Path

from contracts import BOARD_DTS_GEN_BANNER, BOARD_PANIC_GEN_BANNER, ROOT

import dts_parse_util as dpu

_HSE_RE = re.compile(r"hse-frequency\s*=\s*<\s*(\d+)\s*>")
_LSE_RE = re.compile(r"lse-frequency\s*=\s*<\s*(\d+)\s*>")
_LED_PORT_RE = re.compile(r"gpio-port\s*=\s*\"([A-I])\"")
_LED_PINS_RE = re.compile(r"pins\s*=\s*<\s*([^>]+)\s*>")
_KEY_PORT_RE = re.compile(r"gpio-port\s*=\s*\"([A-I])\"")
_KEY_PIN_RE = re.compile(r"gpio-pin\s*=\s*<\s*(\d+)\s*>")
_KEY_LABEL_RE = re.compile(r'label\s*=\s*"([^"]*)"')
_PORT_PIN_PROP_PORT = re.compile(r"(\w+-port)\s*=\s*\"([A-I])\"")
_PORT_PIN_PROP_PIN = re.compile(r"(\w+-pin)\s*=\s*<\s*(\d+)\s*>")
@dataclass(frozen=True)
class PinmapEntry:
    label: str
    alt_label: str


@dataclass(frozen=True)
class BoardDts:
    dts_path: Path
    compatible: str
    hse_hz: int
    lse_hz: int | None
    led_port: str
    led_pins: tuple[int, ...]
    led_names: tuple[str, ...]
    led_active_low: bool
    key_names: tuple[str, ...]
    keys: tuple[tuple[str, int, bool, str], ...]  # port, pin, pressed_high, label
    beep_via: str  # "gpio" | "pcf8574"
    beep_port: str
    beep_pin: int
    beep_active_high: bool
    pcf8574_addr7: int | None
    pcf8574_bit: int | None
    i2c_scl_port: str | None
    i2c_scl_pin: int | None
    i2c_sda_port: str | None
    i2c_sda_pin: int | None


def _strip_c_comments(text: str) -> str:
    return dpu.strip_c_comments(text)


def _parse_pin_list(raw: str) -> tuple[int, ...]:
    parts = [p.strip() for p in raw.split() if p.strip()]
    return tuple(int(p) for p in parts)


def parse_board_dts(path: Path) -> BoardDts:
    raw = path.read_text(encoding="utf-8")
    body = dpu.merge_dts_includes(raw, path.parent)
    body = _strip_c_comments(body)

    clocks_inner = dpu.extract_named_block(body, "clocks")
    if clocks_inner is None:
        raise ValueError(f"{path.relative_to(ROOT)} 缺少 clocks 节点")
    hse_m = _HSE_RE.search(clocks_inner)
    if hse_m is None:
        raise ValueError(f"{path.relative_to(ROOT)} 缺少 clocks.hse-frequency")
    hse_hz = int(hse_m.group(1))
    lse_m = _LSE_RE.search(clocks_inner)
    lse_hz = int(lse_m.group(1)) if lse_m else None

    led_inner = dpu.extract_named_block(body, "leds")
    if led_inner is None:
        raise ValueError(f"{path.relative_to(ROOT)} 缺少 leds 节点")
    port_m = _LED_PORT_RE.search(led_inner)
    pins_m = _LED_PINS_RE.search(led_inner)
    if port_m is None or pins_m is None:
        raise ValueError(f"{path.relative_to(ROOT)} leds 须含 gpio-port 与 pins")
    led_port = port_m.group(1)
    led_pins = _parse_pin_list(pins_m.group(1))
    led_names = dpu.extract_quoted_string_list(led_inner, "led-names")
    if len(led_names) != len(led_pins):
        raise ValueError(
            f"{path.relative_to(ROOT)} leds 的 led-names 数量 "
            f"({len(led_names)}) 须与 pins ({len(led_pins)}) 一致"
        )
    led_active_low = "active-low" in led_inner and "active-high" not in led_inner

    keys_inner = dpu.extract_named_block(body, "keys")
    if keys_inner is None:
        raise ValueError(f"{path.relative_to(ROOT)} 缺少 keys 节点")
    key_names = dpu.extract_quoted_string_list(keys_inner, "key-names")
    keys: list[tuple[str, int, bool, str]] = []
    for inner in dpu.iter_gpio_child_inners(keys_inner):
        kp = _KEY_PORT_RE.search(inner)
        kn = _KEY_PIN_RE.search(inner)
        if kp is None or kn is None:
            continue
        pressed_high = "pressed-high" in inner
        label_m = _KEY_LABEL_RE.search(inner)
        label = label_m.group(1) if label_m else ""
        keys.append((kp.group(1), int(kn.group(1)), pressed_high, label))
    if not keys:
        raise ValueError(f"{path.relative_to(ROOT)} keys 节点为空或格式不符")
    if len(key_names) != len(keys):
        raise ValueError(
            f"{path.relative_to(ROOT)} keys 的 key-names 数量 "
            f"({len(key_names)}) 须与按键节点数 ({len(keys)}) 一致"
        )

    beep_inner = dpu.extract_named_block(body, "beep")
    if beep_inner is None:
        raise ValueError(f"{path.relative_to(ROOT)} 缺少 beep 节点")
    beep_via = "gpio"
    pcf8574_addr7 = None
    pcf8574_bit = None
    i2c_scl_port = None
    i2c_scl_pin = None
    i2c_sda_port = None
    i2c_sda_pin = None
    if "brd,pcf8574-beep" in beep_inner:
        beep_via = "pcf8574"
        addr_m = re.search(r"pcf8574-addr\s*=\s*<\s*(\d+)\s*>", beep_inner)
        bit_m = re.search(r"pcf8574-pin\s*=\s*<\s*(\d+)\s*>", beep_inner)
        scl_port_m = re.search(r"i2c-scl-port\s*=\s*\"([A-I])\"", beep_inner)
        scl_pin_m = re.search(r"i2c-scl-pin\s*=\s*<\s*(\d+)\s*>", beep_inner)
        sda_port_m = re.search(r"i2c-sda-port\s*=\s*\"([A-I])\"", beep_inner)
        sda_pin_m = re.search(r"i2c-sda-pin\s*=\s*<\s*(\d+)\s*>", beep_inner)
        if (
            addr_m is None
            or bit_m is None
            or scl_port_m is None
            or scl_pin_m is None
            or sda_port_m is None
            or sda_pin_m is None
        ):
            raise ValueError(
                f"{path.relative_to(ROOT)} pcf8574 beep 须含 "
                "pcf8574-addr/pin 与 i2c-scl/i2c-sda 脚"
            )
        pcf8574_addr7 = int(addr_m.group(1))
        pcf8574_bit = int(bit_m.group(1))
        i2c_scl_port = scl_port_m.group(1)
        i2c_scl_pin = int(scl_pin_m.group(1))
        i2c_sda_port = sda_port_m.group(1)
        i2c_sda_pin = int(sda_pin_m.group(1))
        beep_port = i2c_scl_port
        beep_pin = i2c_scl_pin
    else:
        beep_port_m = _KEY_PORT_RE.search(beep_inner)
        beep_pin_m = _KEY_PIN_RE.search(beep_inner)
        if beep_port_m is None or beep_pin_m is None:
            raise ValueError(f"{path.relative_to(ROOT)} beep 须含 gpio-port / gpio-pin")
        beep_port = beep_port_m.group(1)
        beep_pin = int(beep_pin_m.group(1))
    beep_active_high = "active-high" in beep_inner

    board_compat_m = re.search(
        r'model\s*=.*?\n\s*compatible\s*=\s*"([^"]+)"',
        body,
        re.S,
    )
    compatible = (
        board_compat_m.group(1).split(",")[0].strip()
        if board_compat_m
        else "unknown"
    )

    return BoardDts(
        dts_path=path,
        compatible=compatible,
        hse_hz=hse_hz,
        lse_hz=lse_hz,
        led_port=led_port,
        led_pins=led_pins,
        led_names=led_names,
        led_active_low=led_active_low,
        key_names=tuple(key_names),
        keys=tuple(keys),
        beep_via=beep_via,
        beep_port=beep_port,
        beep_pin=beep_pin,
        beep_active_high=beep_active_high,
        pcf8574_addr7=pcf8574_addr7,
        pcf8574_bit=pcf8574_bit,
        i2c_scl_port=i2c_scl_port,
        i2c_scl_pin=i2c_scl_pin,
        i2c_sda_port=i2c_sda_port,
        i2c_sda_pin=i2c_sda_pin,
    )


def _led_panic_masks(led_pins: tuple[int, ...]) -> tuple[int, int, int]:
    mask = sum(1 << pin for pin in led_pins)
    mode_mask = sum(0b11 << (2 * pin) for pin in led_pins)
    mode_out = sum(0b01 << (2 * pin) for pin in led_pins)
    return mask, mode_mask, mode_out


def board_dts_path(info: dict) -> Path:
    board = info.get("board") or {}
    rel = board.get("dts") or "device/dts/boards/apollo-f429.dts"
    return ROOT / rel


def pinmap_dtsi_path(info: dict) -> Path:
    board = info.get("board") or {}
    custom = board.get("pinmap_dtsi")
    if custom:
        return ROOT / custom
    stem = board_dts_path(info).stem
    return ROOT / "device" / "dts" / "generated" / f"{stem}-pinmap.dtsi"


def board_peripherals_dtsi_path(info: dict) -> Path:
    board = info.get("board") or {}
    custom = board.get("peripherals_dtsi")
    if custom:
        return ROOT / custom
    stem = board_dts_path(info).stem
    return ROOT / "device" / "dts" / "include" / f"{stem}-peripherals.dtsi"


def parse_pinmap_index(text: str) -> dict[tuple[str, int], PinmapEntry]:
    body = _strip_c_comments(text)
    index: dict[tuple[str, int], PinmapEntry] = {}
    for block in re.finditer(r"\bpin-\d+\s*\{([^}]*)\}", body, re.S):
        inner = block.group(1)
        port_m = _KEY_PORT_RE.search(inner)
        pin_m = _KEY_PIN_RE.search(inner)
        label_m = re.search(r'label\s*=\s*"([^"]*)"', inner)
        alt_m = re.search(r'alt-label\s*=\s*"([^"]*)"', inner)
        if port_m is None or pin_m is None or label_m is None:
            continue
        key = (port_m.group(1), int(pin_m.group(1)))
        index[key] = PinmapEntry(
            label=label_m.group(1),
            alt_label=alt_m.group(1) if alt_m else "",
        )
    return index


def extract_port_pin_pairs(block: str) -> list[tuple[str, str, int]]:
    pairs: list[tuple[str, str, int]] = []
    for port_m in _PORT_PIN_PROP_PORT.finditer(block):
        prop, port = port_m.group(1), port_m.group(2)
        if not prop.endswith("-port"):
            continue
        pin_prop = prop[: -len("-port")] + "-pin"
        pin_m = re.search(rf"{re.escape(pin_prop)}\s*=\s*<\s*(\d+)\s*>", block)
        if pin_m is None:
            continue
        pairs.append((prop, port, int(pin_m.group(1))))
    return pairs


def _label_matches_pinmap(expect: str, entry: PinmapEntry) -> bool:
    expect_u = expect.upper()
    hay = f"{entry.label} {entry.alt_label}".upper()
    tokens = re.split(r"[\s_/]+", expect_u)
    for tok in tokens:
        if len(tok) >= 2 and tok in hay:
            return True
    return expect_u in hay


def board_key_enum_suffixes() -> tuple[str, ...]:
    path = ROOT / "bsl" / "inc" / "board_key.h"
    text = _strip_c_comments(path.read_text(encoding="utf-8"))
    for block in re.findall(r"typedef\s+enum\s*\{(.*?)\}", text, re.S):
        if "BOARD_KEY_COUNT" not in block:
            continue
        members = [item.strip() for item in block.split(",") if item.strip()]
        suffixes: list[str] = []
        for member in members:
            if "BOARD_KEY_COUNT" in member:
                break
            name_m = re.search(r"BOARD_(\w+)", member)
            if name_m and name_m.group(1) != "KEY_COUNT":
                suffixes.append(name_m.group(1))
        return tuple(suffixes)
    return ()


def board_led_enum_suffixes() -> tuple[str, ...]:
    path = ROOT / "bsl" / "inc" / "board_led.h"
    text = _strip_c_comments(path.read_text(encoding="utf-8"))
    for block in re.findall(r"typedef\s+enum\s*\{(.*?)\}", text, re.S):
        if "BOARD_LED_COUNT" not in block:
            continue
        members = [item.strip() for item in block.split(",") if item.strip()]
        suffixes: list[str] = []
        for member in members:
            if "BOARD_LED_COUNT" in member:
                break
            name_m = re.search(r"BOARD_LED_(\w+)", member)
            if name_m:
                suffixes.append(name_m.group(1))
        return tuple(suffixes)
    return ()


def _enum_member_count_header(path: Path, count_name: str) -> int | None:
    header = _strip_c_comments(path.read_text(encoding="utf-8"))
    for block in re.findall(r"typedef\s+enum\s*\{(.*?)\}", header, re.S):
        if count_name not in block:
            continue
        members = [item.strip() for item in block.split(",") if item.strip()]
        for index, member in enumerate(members):
            if count_name in member:
                return index
    return None


def write_board_dts_header(board: BoardDts, *, compatible: str | None = None) -> None:
    gpio_port_define = {
        "A": "GPIOA",
        "B": "GPIOB",
        "C": "GPIOC",
        "H": "GPIOH",
        "I": "GPIOI",
    }
    led_gpio = gpio_port_define.get(board.led_port, f"GPIO{board.led_port}")
    lines = [
        BOARD_DTS_GEN_BANNER,
        f"/* 源：{board.dts_path.relative_to(ROOT).as_posix()} */",
        "#ifndef BOARD_DTS_H",
        "#define BOARD_DTS_H",
        "",
        f'#define BOARD_DTS_COMPATIBLE "{compatible or board.compatible}"',
        f"#define BOARD_DTS_HSE_HZ {board.hse_hz}U",
        "",
        f"#define BOARD_DTS_LED_GPIO_PORT {led_gpio}",
        f"#define BOARD_DTS_LED_ACTIVE_LOW {1 if board.led_active_low else 0}",
        f"#define BOARD_DTS_LED_PIN_COUNT {len(board.led_pins)}",
    ]
    pin_inits = ", ".join(f"BOARD_DTS_LED_PIN_{idx}" for idx in range(len(board.led_pins)))
    for idx, pin in enumerate(board.led_pins):
        lines.append(f"#define BOARD_DTS_LED_PIN_{idx} GPIO_PIN_{pin}")
    lines.append(f"#define BOARD_DTS_LED_PINS_INITIALIZER {pin_inits}")
    lines.append("")
    if board.led_active_low:
        lines.extend(
            [
                "#define BOARD_DTS_LED_LEVEL_ON GPIO_PIN_RESET",
                "#define BOARD_DTS_LED_LEVEL_OFF GPIO_PIN_SET",
                "#define BOARD_DTS_LED_LEVEL_IDLE GPIO_PIN_SET",
            ]
        )
    else:
        lines.extend(
            [
                "#define BOARD_DTS_LED_LEVEL_ON GPIO_PIN_SET",
                "#define BOARD_DTS_LED_LEVEL_OFF GPIO_PIN_RESET",
                "#define BOARD_DTS_LED_LEVEL_IDLE GPIO_PIN_RESET",
            ]
        )
    lines.append("")
    if board.beep_via == "pcf8574":
        scl_gpio = gpio_port_define.get(
            board.i2c_scl_port or "H", f"GPIO{board.i2c_scl_port}"
        )
        sda_gpio = gpio_port_define.get(
            board.i2c_sda_port or "H", f"GPIO{board.i2c_sda_port}"
        )
        lines.extend(
            [
                "#define BOARD_DTS_BEEP_VIA_PCF8574 1",
                f"#define BOARD_DTS_PCF8574_I2C_ADDR7 0x{board.pcf8574_addr7:02X}U",
                f"#define BOARD_DTS_PCF8574_BEEP_BIT {board.pcf8574_bit}U",
                f"#define BOARD_DTS_BEEP_ACTIVE_HIGH "
                f"{1 if board.beep_active_high else 0}",
                f"#define BOARD_DTS_I2C_SCL_PORT {scl_gpio}",
                f"#define BOARD_DTS_I2C_SCL_PIN GPIO_PIN_{board.i2c_scl_pin}",
                f"#define BOARD_DTS_I2C_SDA_PORT {sda_gpio}",
                f"#define BOARD_DTS_I2C_SDA_PIN GPIO_PIN_{board.i2c_sda_pin}",
                "#define BOARD_DTS_I2C_RCC_CLK_ENABLE() \\",
                "    do { \\",
                f"        __HAL_RCC_GPIO{board.i2c_scl_port}_CLK_ENABLE(); \\",
                f"        __HAL_RCC_GPIO{board.i2c_sda_port}_CLK_ENABLE(); \\",
                "    } while (0)",
            ]
        )
    else:
        beep_gpio = gpio_port_define.get(board.beep_port, "GPIO" + board.beep_port)
        lines.extend(
            [
                "#define BOARD_DTS_BEEP_VIA_PCF8574 0",
                f"#define BOARD_DTS_BEEP_GPIO_PORT {beep_gpio}",
                f"#define BOARD_DTS_BEEP_PIN GPIO_PIN_{board.beep_pin}",
                f"#define BOARD_DTS_BEEP_ACTIVE_HIGH "
                f"{1 if board.beep_active_high else 0}",
            ]
        )
    if board.beep_active_high:
        lines.extend(
            [
                "#define BOARD_DTS_BEEP_LEVEL_ON GPIO_PIN_SET",
                "#define BOARD_DTS_BEEP_LEVEL_OFF GPIO_PIN_RESET",
                "#define BOARD_DTS_BEEP_LEVEL_IDLE GPIO_PIN_RESET",
            ]
        )
    else:
        lines.extend(
            [
                "#define BOARD_DTS_BEEP_LEVEL_ON GPIO_PIN_RESET",
                "#define BOARD_DTS_BEEP_LEVEL_OFF GPIO_PIN_SET",
                "#define BOARD_DTS_BEEP_LEVEL_IDLE GPIO_PIN_SET",
            ]
        )
    lines.extend(
        [
            "",
            f"#define BOARD_DTS_KEY_COUNT {len(board.keys)}",
        ]
    )
    for idx, (port, pin, pressed_high, _label) in enumerate(board.keys):
        g = gpio_port_define.get(port, f"GPIO{port}")
        pl = "GPIO_PIN_SET" if pressed_high else "GPIO_PIN_RESET"
        lines.append(f"#define BOARD_DTS_KEY_{idx}_PORT {g}")
        lines.append(f"#define BOARD_DTS_KEY_{idx}_PIN GPIO_PIN_{pin}")
        lines.append(f"#define BOARD_DTS_KEY_{idx}_PRESSED {pl}")
        lines.append(
            f"#define BOARD_DTS_KEY_{idx}_RCC_CLK_ENABLE() "
            f"__HAL_RCC_GPIO{port}_CLK_ENABLE()"
        )
    if board.keys:
        lines.append("")
        lines.append("#define BOARD_DTS_KEYS_RCC_ENABLE_ALL() \\")
        lines.append("    do { \\")
        for idx in range(len(board.keys)):
            lines.append(f"        BOARD_DTS_KEY_{idx}_RCC_CLK_ENABLE(); \\")
        lines.append("    } while (0)")
    lines.extend(
        [
            "",
            f"#define BOARD_DTS_LED_RCC_CLK_ENABLE() "
            f"__HAL_RCC_GPIO{board.led_port}_CLK_ENABLE()",
        ]
    )
    if board.beep_via != "pcf8574":
        lines.append(
            f"#define BOARD_DTS_BEEP_RCC_CLK_ENABLE() "
            f"__HAL_RCC_GPIO{board.beep_port}_CLK_ENABLE()"
        )
    lines.extend(
        [
            "",
            "#endif /* BOARD_DTS_H */",
            "",
        ]
    )
    from generate import write_generated_text

    write_generated_text(ROOT / "bsl" / "inc" / "board_dts.h", "\n".join(lines))


def write_board_panic_hw_header(board: BoardDts) -> None:
    gpio_port_define = {
        "A": "GPIOA",
        "B": "GPIOB",
        "C": "GPIOC",
        "I": "GPIOI",
    }
    led_gpio = gpio_port_define.get(board.led_port, f"GPIO{board.led_port}")
    mask, mode_mask, mode_out = _led_panic_masks(board.led_pins)
    pin_bytes = ", ".join(f"{pin}U" for pin in board.led_pins)
    lines = [
        BOARD_PANIC_GEN_BANNER,
        f"/* 源：{board.dts_path.relative_to(ROOT).as_posix()} */",
        "#ifndef BOARD_PANIC_HW_H",
        "#define BOARD_PANIC_HW_H",
        "",
        "/* 须在包含本头之前已 include stm32f4xx_hal.h（层级上 panic 路径不入 bsl）。 */",
        "",
        f"#define PANIC_LED_GPIO {led_gpio}",
        f"#define PANIC_LED_MASK 0x{mask:04X}U",
        f"#define PANIC_LED_MODE_MASK 0x{mode_mask:08X}U",
        f"#define PANIC_LED_MODE_OUT 0x{mode_out:08X}U",
        f"#define PANIC_LED_PIN_COUNT {len(board.led_pins)}",
        f"static const uint8_t panic_led_pin[PANIC_LED_PIN_COUNT] = {{{pin_bytes}}};",
        "",
        f"#define PANIC_LED_RCC_CLK_ENABLE() "
        f"__HAL_RCC_GPIO{board.led_port}_CLK_ENABLE()",
        "",
    ]
    lines.extend(
        [
            "void board_panic_beep_silence(void);",
            "",
            "#endif /* BOARD_PANIC_HW_H */",
            "",
        ]
    )
    from generate import write_generated_text

    write_generated_text(ROOT / "osl" / "inc" / "board_panic_hw.h", "\n".join(lines))


def sync_board_dts(info: dict) -> None:
    path = board_dts_path(info)
    if not path.is_file():
        raise SystemExit(f"板级 DTS 不存在：{path.relative_to(ROOT)}")
    board_cfg = info.get("board") or {}
    pinmap_rel = board_cfg.get("pinmap_xlsx")
    pinmap_out: Path | None = None
    if pinmap_rel:
        import dts_pinmap as pinmap_mod

        xlsx = ROOT / pinmap_rel
        pinmap_out = pinmap_dtsi_path(info)
        try:
            count = pinmap_mod.sync_pinmap_dtsi(xlsx, pinmap_out)
        except (OSError, ValueError) as exc:
            raise SystemExit(f"引脚表生成 pinmap dtsi 失败：{exc}") from exc
        print(
            f"已生成 {pinmap_out.relative_to(ROOT).as_posix()}（{count} 脚，源 {pinmap_rel}）"
        )
    if pinmap_out is not None and board_cfg.get("gpio_bind"):
        import dts_board_gpio as gpio_mod

        try:
            gpio_path = gpio_mod.sync_board_gpio_dtsi(info, pinmap_out)
        except (OSError, ValueError) as exc:
            raise SystemExit(f"生成 *-gpio.dtsi 失败：{exc}") from exc
        print(
            f"已生成 {gpio_path.relative_to(ROOT).as_posix()}（gpio_bind + pinmap）"
        )
    board = parse_board_dts(path)
    compat = board_cfg.get("compatible")
    write_board_dts_header(board, compatible=compat)
    write_board_panic_hw_header(board)
    rel = path.relative_to(ROOT).as_posix()
    print(f"已根据 DTS 生成 bsl/inc/board_dts.h、osl/inc/board_panic_hw.h（{rel}）")


def _gpio_letter_from_hal_name(port_token: str) -> str | None:
    m = re.search(r"GPIO([A-I])", port_token)
    return m.group(1) if m else None


def read_board_dts_header() -> str:
    path = ROOT / "bsl" / "inc" / "board_dts.h"
    if not path.is_file():
        raise FileNotFoundError("bsl/inc/board_dts.h")
    return _strip_c_comments(path.read_text(encoding="utf-8"))


def header_led_hw() -> tuple[str, tuple[int, ...]]:
    text = read_board_dts_header()
    port_m = re.search(r"#define\s+BOARD_DTS_LED_GPIO_PORT\s+(GPIO([A-I]))", text)
    if port_m is None:
        raise ValueError("board_dts.h 缺少 BOARD_DTS_LED_GPIO_PORT")
    pins: list[tuple[int, int]] = []
    for m in re.finditer(
        r"#define\s+BOARD_DTS_LED_PIN_(\d+)\s+GPIO_PIN_(\d+)", text
    ):
        pins.append((int(m.group(1)), int(m.group(2))))
    pins.sort(key=lambda item: item[0])
    return port_m.group(2), tuple(num for _, num in pins)


def header_keys_hw() -> tuple[tuple[str, int, bool], ...]:
    text = read_board_dts_header()
    count_m = re.search(r"#define\s+BOARD_DTS_KEY_COUNT\s+(\d+)", text)
    count = int(count_m.group(1)) if count_m else 0
    rows: list[tuple[int, str, int, bool]] = []
    for idx in range(count):
        port_m = re.search(
            rf"#define\s+BOARD_DTS_KEY_{idx}_PORT\s+(GPIO([A-I]))", text
        )
        pin_m = re.search(rf"#define\s+BOARD_DTS_KEY_{idx}_PIN\s+GPIO_PIN_(\d+)", text)
        press_m = re.search(
            rf"#define\s+BOARD_DTS_KEY_{idx}_PRESSED\s+(GPIO_PIN_(?:SET|RESET))",
            text,
        )
        if port_m and pin_m and press_m:
            pressed_high = press_m.group(1) == "GPIO_PIN_SET"
            rows.append(
                (
                    idx,
                    port_m.group(2),
                    int(pin_m.group(1)),
                    pressed_high,
                )
            )
    rows.sort(key=lambda item: item[0])
    return tuple((port, pin, pressed) for _, port, pin, pressed in rows)


def header_beep_hw() -> tuple[str, int, int]:
    text = read_board_dts_header()
    if re.search(r"#define\s+BOARD_DTS_BEEP_VIA_PCF8574\s+1", text):
        addr_m = re.search(
            r"#define\s+BOARD_DTS_PCF8574_I2C_ADDR7\s+0x([0-9A-Fa-f]+)U", text
        )
        bit_m = re.search(r"#define\s+BOARD_DTS_PCF8574_BEEP_BIT\s+(\d+)U", text)
        if addr_m is None or bit_m is None:
            raise ValueError("board_dts.h 缺少 PCF8574 蜂鸣器定义")
        return (
            "pcf8574",
            int(addr_m.group(1), 16),
            int(bit_m.group(1)),
        )
    port_m = re.search(r"#define\s+BOARD_DTS_BEEP_GPIO_PORT\s+(GPIO([A-I]))", text)
    pin_m = re.search(r"#define\s+BOARD_DTS_BEEP_PIN\s+GPIO_PIN_(\d+)", text)
    if port_m is None or pin_m is None:
        raise ValueError("board_dts.h 缺少蜂鸣器引脚定义")
    return ("gpio", port_m.group(2), int(pin_m.group(1)))


def effective_led_pins_from_c() -> tuple[int, ...]:
    _, hdr_pins = header_led_hw()
    text = _strip_c_comments(
        (ROOT / "bsl" / "src" / "board_led.c").read_text(encoding="utf-8")
    )
    block = re.search(
        r"led_pin\s*\[\s*BOARD_(?:LED_COUNT|DTS_LED_PIN_COUNT)\s*\]\s*=\s*\{(.*?)\}",
        text,
        re.S,
    )
    if block is None:
        return hdr_pins
    pins: list[int] = []
    for item in block.group(1).split(","):
        token = item.strip()
        if not token:
            continue
        macro = re.match(r"BOARD_DTS_LED_PIN_(\d+)", token)
        if macro:
            idx = int(macro.group(1))
            if idx >= len(hdr_pins):
                raise ValueError(f"board_led.c 引用无效的 BOARD_DTS_LED_PIN_{idx}")
            pins.append(hdr_pins[idx])
            continue
        literal = re.match(r"GPIO_PIN_(\d+)", token)
        if literal:
            pins.append(int(literal.group(1)))
    return tuple(pins) if pins else hdr_pins


def bsl_uses_board_dts(rel: str) -> bool:
    text = (ROOT / rel).read_text(encoding="utf-8")
    return '#include "board_dts.h"' in text


def collect_host_dts_unittest_errors() -> list[str]:
    import subprocess
    import sys

    script = ROOT / "tests" / "test_dts.py"
    if not script.is_file():
        return []
    proc = subprocess.run(
        [sys.executable, str(script)],
        cwd=ROOT,
        capture_output=True,
        text=True,
        check=False,
    )
    if proc.returncode == 0:
        return []
    detail = (proc.stderr or proc.stdout or "").strip()
    if len(detail) > 800:
        detail = detail[:800] + "…"
    return [f"tests/test_dts.py 未通过（returncode={proc.returncode}）：{detail}"]


def collect_device_dts_errors() -> list[str]:
    """DTS 与 bsl 板级源文件的事实须一致。"""
    errors: list[str] = []
    info_path = ROOT / "proj" / "project-info.json"
    if not info_path.is_file():
        errors.append("缺少 proj/project-info.json，无法定位板级 DTS")
        return errors
    import json

    info = json.loads(info_path.read_text(encoding="utf-8"))
    board = info.get("board") or {}
    if not board.get("dts"):
        errors.append("project-info.json 缺少 board.dts（设备 DTS 架构未绑定当前板）")
        return errors
    dts_path = board_dts_path(info)
    if not dts_path.is_file():
        errors.append(f"board.dts 指向的文件不存在：{dts_path.relative_to(ROOT)}")
        return errors
    header = ROOT / "bsl" / "inc" / "board_dts.h"
    if not header.is_file():
        errors.append("缺少 bsl/inc/board_dts.h（请先运行 build/build.py update）")
    elif BOARD_DTS_GEN_BANNER not in header.read_text(encoding="utf-8"):
        errors.append("bsl/inc/board_dts.h 不是由 update 生成的（缺少生成标记）")

    try:
        parsed = parse_board_dts(dts_path)
    except (OSError, ValueError, FileNotFoundError) as exc:
        errors.append(f"解析板级 DTS 失败：{exc}")
        return errors

    for rel in (
        "bsl/src/board_led.c",
        "bsl/src/board_key.c",
        "bsl/src/board_beep.c",
    ):
        if not bsl_uses_board_dts(rel):
            errors.append(f"{rel} 必须 #include \"board_dts.h\"（引脚真源在 device DTS）")

    try:
        led_port, hdr_led_pins = header_led_hw()
        eff_led_pins = effective_led_pins_from_c()
    except (OSError, ValueError) as exc:
        errors.append(str(exc))
        led_port, hdr_led_pins, eff_led_pins = "?", (), ()

    if hdr_led_pins != parsed.led_pins or eff_led_pins != parsed.led_pins:
        errors.append(
            f"DTS leds pins={parsed.led_pins} 与 board_dts.h {hdr_led_pins} "
            f"/ board_led.c 生效表 {eff_led_pins} 不一致"
        )
    if led_port != parsed.led_port:
        errors.append(
            f"DTS led 口 {parsed.led_port} 与 board_dts.h GPIO{led_port} 不一致"
        )
    if not parsed.led_active_low:
        errors.append("DTS leds 须为 active-low（与板载 LED 共阳接法一致）")

    try:
        keys_hdr = header_keys_hw()
    except (OSError, ValueError) as exc:
        errors.append(str(exc))
        keys_hdr = ()

    parsed_keys_hw = tuple((p, n, ph) for p, n, ph, _ in parsed.keys)
    if keys_hdr != parsed_keys_hw:
        errors.append(f"DTS keys {parsed_keys_hw} 与 board_dts.h {keys_hdr} 不一致")

    try:
        beep_hdr = header_beep_hw()
    except (OSError, ValueError) as exc:
        errors.append(str(exc))
        beep_hdr = ("?", -1)

    if parsed.beep_via == "pcf8574":
        if beep_hdr[0] != "pcf8574" or beep_hdr[1:] != (
            parsed.pcf8574_addr7,
            parsed.pcf8574_bit,
        ):
            errors.append(
                f"DTS pcf8574 beep addr/bit="
                f"{parsed.pcf8574_addr7}/{parsed.pcf8574_bit} 与 "
                f"board_dts.h {beep_hdr} 不一致"
            )
    elif beep_hdr[0] != "gpio" or (parsed.beep_port, parsed.beep_pin) != (
        beep_hdr[1],
        beep_hdr[2],
    ):
        errors.append(
            f"DTS gpio beep {parsed.beep_port}{parsed.beep_pin} 与 "
            f"board_dts.h {beep_hdr} 不一致"
        )
    hal_conf = _strip_c_comments(
        (ROOT / "hal/periph/inc/stm32f4xx_hal_conf.h").read_text(encoding="utf-8")
    )
    hse_m = re.search(r"#define\s+HSE_VALUE\s+(\d+)U", hal_conf)
    if hse_m is None:
        errors.append("stm32f4xx_hal_conf.h 找不到 HSE_VALUE，无法与 DTS 对拍")
    elif int(hse_m.group(1)) != parsed.hse_hz:
        errors.append(
            f"DTS hse-frequency={parsed.hse_hz} 与 HAL HSE_VALUE={hse_m.group(1)} 不一致"
        )

    if parsed.lse_hz is not None:
        lse_m = re.search(r"#define\s+LSE_VALUE\s+(\d+)U", hal_conf)
        if lse_m is None:
            errors.append("stm32f4xx_hal_conf.h 找不到 LSE_VALUE，无法与 DTS 对拍")
        elif int(lse_m.group(1)) != parsed.lse_hz:
            errors.append(
                f"DTS lse-frequency={parsed.lse_hz} 与 HAL LSE_VALUE={lse_m.group(1)} 不一致"
            )

    led_enum = board_led_enum_suffixes()
    if led_enum and parsed.led_names != led_enum:
        errors.append(
            f"DTS leds led-names={parsed.led_names} 与 "
            f"board_led.h 枚举后缀 {led_enum} 不一致"
        )

    key_enum = board_key_enum_suffixes()
    if key_enum and parsed.key_names != key_enum:
        errors.append(
            f"DTS keys key-names={parsed.key_names} 与 "
            f"board_key.h 枚举后缀 {key_enum} 不一致"
        )

    led_count = _enum_member_count_header(ROOT / "bsl" / "inc" / "board_led.h", "BOARD_LED_COUNT")
    if led_count is not None and led_count != len(parsed.led_pins):
        errors.append(
            f"BOARD_LED_COUNT={led_count} 与 DTS pins 数量 {len(parsed.led_pins)} 不一致"
        )
    key_count = _enum_member_count_header(ROOT / "bsl" / "inc" / "board_key.h", "BOARD_KEY_COUNT")
    if key_count is not None and key_count != len(parsed.keys):
        errors.append(
            f"BOARD_KEY_COUNT={key_count} 与 DTS keys 数量 {len(parsed.keys)} 不一致"
        )

    panic_path = ROOT / "osl" / "inc" / "board_panic_hw.h"
    if not panic_path.is_file():
        errors.append("缺少 osl/inc/board_panic_hw.h（请先运行 build/build.py update）")
    elif BOARD_PANIC_GEN_BANNER not in panic_path.read_text(encoding="utf-8"):
        errors.append("osl/inc/board_panic_hw.h 不是由 update 生成的（缺少生成标记）")
    else:
        panic_body = _strip_c_comments(panic_path.read_text(encoding="utf-8"))
        pm, pmm, pmo = _led_panic_masks(parsed.led_pins)
        for name, want in (
            ("PANIC_LED_MASK", pm),
            ("PANIC_LED_MODE_MASK", pmm),
            ("PANIC_LED_MODE_OUT", pmo),
        ):
            found = re.search(rf"#define\s+{name}\s+0x([0-9A-Fa-f]+)U", panic_body)
            if found is None:
                errors.append(f"board_panic_hw.h 缺少 {name}")
            elif int(found.group(1), 16) != want:
                errors.append(
                    f"board_panic_hw.h 的 {name}=0x{found.group(1)} "
                    f"与 DTS 灯脚算得的 0x{want:X} 不一致"
                )
        pin_m = re.search(
            r"panic_led_pin\[PANIC_LED_PIN_COUNT\]\s*=\s*\{([^}]+)\}", panic_body
        )
        if pin_m is None:
            errors.append("board_panic_hw.h 缺少 panic_led_pin[]")
        else:
            got = tuple(
                int(x.rstrip("U").strip())
                for x in pin_m.group(1).split(",")
                if x.strip()
            )
            if got != parsed.led_pins:
                errors.append(
                    f"board_panic_hw.h panic_led_pin {got} 与 DTS {parsed.led_pins} 不一致"
                )

    osl_panic = (ROOT / "osl/src/osl_panic.c").read_text(encoding="utf-8")
    if '#include "board_panic_hw.h"' not in osl_panic:
        errors.append("osl/src/osl_panic.c 必须 #include \"board_panic_hw.h\"")

    gpio_bind = board.get("gpio_bind")
    if gpio_bind:
        import dts_board_gpio as gpio_mod

        gpio_path = gpio_mod.board_gpio_dtsi_path(info)
        want_include = f'generated/{gpio_path.name}'
        dts_raw = dts_path.read_text(encoding="utf-8")
        if want_include not in dts_raw.replace("\\", "/"):
            errors.append(
                f"{dts_path.relative_to(ROOT)} 须 #include 生成的 {gpio_path.name}"
            )
        if pinmap_rel := board.get("pinmap_xlsx"):
            pinmap_path = pinmap_dtsi_path(info)
            if pinmap_path.is_file() and gpio_path.is_file():
                try:
                    index = parse_pinmap_index(
                        _strip_c_comments(pinmap_path.read_text(encoding="utf-8"))
                    )
                    want = gpio_mod.generate_gpio_dtsi(gpio_bind, index)
                    got = gpio_path.read_text(encoding="utf-8")
                    if _strip_c_comments(got) != _strip_c_comments(want):
                        errors.append(
                            f"{gpio_path.relative_to(ROOT)} 与 gpio_bind+pinmap "
                            "不同步，请运行 update"
                        )
                except ValueError as exc:
                    errors.append(f"gpio_bind 校验失败：{exc}")
            elif not gpio_path.is_file():
                errors.append(
                    f"缺少 {gpio_path.relative_to(ROOT)}（请先 update）"
                )

    pinmap_rel = board.get("pinmap_xlsx")
    if pinmap_rel:
        pinmap_path = pinmap_dtsi_path(info)
        periph_path = board_peripherals_dtsi_path(info)
        if not pinmap_path.is_file():
            errors.append(
                f"缺少 {pinmap_path.relative_to(ROOT)}（请先 update）"
            )
        else:
            pinmap_body = _strip_c_comments(pinmap_path.read_text(encoding="utf-8"))
            count_m = re.search(r"pin-count\s*=\s*<\s*(\d+)\s*>", pinmap_body)
            entries = len(re.findall(r"\bpin-\d+\s*\{", pinmap_body))
            if count_m is None:
                errors.append("pinmap dtsi 缺少 pin-count")
            else:
                declared = int(count_m.group(1))
                if entries != declared or declared < 100:
                    errors.append(
                        f"pinmap dtsi 引脚条数异常：声明 {declared}，"
                        f"解析到 {entries} 条"
                    )
            pinmap_index = parse_pinmap_index(pinmap_body)
            for pin in parsed.led_pins:
                coord = (parsed.led_port, pin)
                if coord not in pinmap_index:
                    errors.append(
                        f"DTS LED P{parsed.led_port}{pin} 在 pinmap 中不存在"
                    )
                elif pin in (0, 1):
                    label = pinmap_index[coord].label.upper()
                    if "LED" not in label:
                        errors.append(
                            f"pinmap P{parsed.led_port}{pin} label={label!r} "
                            f"不含 LED，与 DTS leds 不一致"
                        )
            for port, pin, _pressed, label in parsed.keys:
                coord = (port, pin)
                if coord not in pinmap_index:
                    errors.append(f"DTS 按键 P{port}{pin} 在 pinmap 中不存在")
                elif label and not _label_matches_pinmap(label, pinmap_index[coord]):
                    entry = pinmap_index[coord]
                    errors.append(
                        f"DTS 按键 label={label!r} 与 pinmap "
                        f"P{port}{pin} label={entry.label!r} alt={entry.alt_label!r} 不匹配"
                    )
            beep_coord = (parsed.beep_port, parsed.beep_pin)
            if beep_coord not in pinmap_index:
                errors.append(
                    f"DTS 蜂鸣器 P{parsed.beep_port}{parsed.beep_pin} 在 pinmap 中不存在"
                )
        if not periph_path.is_file():
            errors.append(f"缺少外设摘要 {periph_path.relative_to(ROOT)}")
        elif "board-resources" not in _strip_c_comments(
            periph_path.read_text(encoding="utf-8")
        ):
            errors.append(f"{periph_path.name} 缺少 board-resources 节点")
        elif pinmap_path.is_file():
            pinmap_index = parse_pinmap_index(
                _strip_c_comments(pinmap_path.read_text(encoding="utf-8"))
            )
            periph_body = _strip_c_comments(periph_path.read_text(encoding="utf-8"))
            resources_inner = dpu.extract_named_block(periph_body, "board-resources")
            if resources_inner:
                for block in re.finditer(r"[\w-]+:\s*[\w@.-]+\s*\{([^}]*)\}", resources_inner, re.S):
                    for prop, port, pin in extract_port_pin_pairs(block.group(1)):
                        if (port, pin) not in pinmap_index:
                            errors.append(
                                f"board-resources {prop} P{port}{pin} "
                                f"在 pinmap 中不存在"
                            )
    return errors