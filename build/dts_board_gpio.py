"""由 project-info 的 gpio_bind + pinmap 生成 bsl 已接 GPIO 的 .dtsi 片段。"""
from __future__ import annotations

from pathlib import Path

from contracts import ROOT

_GPIO_DTSI_BANNER = (
    "/* 自动生成，勿手改。来源：proj/project-info.json board.gpio_bind + pinmap；"
    "刷新：python build/build.py update */"
)


def board_gpio_dtsi_path(info: dict) -> Path:
    from dts import board_dts_path

    board = info.get("board") or {}
    custom = board.get("gpio_dtsi")
    if custom:
        return ROOT / custom
    stem = board_dts_path(info).stem
    return ROOT / "device" / "dts" / "generated" / f"{stem}-gpio.dtsi"


def _resolve_pinmap_label(index: dict, label: str) -> tuple[str, int]:
    from dts import PinmapEntry

    for (port, pin), entry in index.items():
        assert isinstance(entry, PinmapEntry)
        if entry.label == label or entry.alt_label == label:
            return port, pin
    raise ValueError(f"pinmap 中找不到 label={label!r}")


def generate_gpio_dtsi(bind: dict, index: dict[tuple[str, int], object]) -> str:
    lines = [
        _GPIO_DTSI_BANNER,
        "/* 绑定：proj/project-info.json → board.gpio_bind */",
        "",
    ]

    leds = bind.get("leds")
    if not isinstance(leds, dict):
        raise ValueError("gpio_bind 缺少 leds 对象")
    led_names = leds.get("led_names")
    pinmap_labels = leds.get("pinmap_labels")
    if not led_names or not pinmap_labels:
        raise ValueError("gpio_bind.leds 须含 led_names 与 pinmap_labels")
    if len(led_names) != len(pinmap_labels):
        raise ValueError("led_names 与 pinmap_labels 长度须一致")
    coords = [_resolve_pinmap_label(index, lb) for lb in pinmap_labels]
    ports = {port for port, _ in coords}
    if len(ports) != 1:
        raise ValueError(f"LED 引脚须在同一 GPIO 口，当前={sorted(ports)}")
    led_port = next(iter(ports))
    pin_list = " ".join(str(pin) for _, pin in coords)
    name_list = ", ".join(f'"{n}"' for n in led_names)

    lines.extend(
        [
            "leds {",
            '    compatible = "brd,gpio-leds";',
            f'    gpio-port = "{led_port}";',
        ]
    )
    if leds.get("active_low", True):
        lines.append("    active-low;")
    else:
        lines.append("    active-high;")
    lines.extend(
        [
            "    /* 顺序须与 bsl/inc/board_led.h 中 BOARD_LED_* 枚举一致 */",
            f"    led-names = {name_list};",
            f"    pins = <{pin_list}>;",
            "};",
            "",
        ]
    )

    keys = bind.get("keys")
    if not isinstance(keys, list) or not keys:
        raise ValueError("gpio_bind 缺少 keys 数组")
    key_ids: list[str] = []
    for idx, key in enumerate(keys):
        if not isinstance(key, dict):
            raise ValueError(f"gpio_bind.keys[{idx}] 须为对象")
        key_id = key.get("key_id")
        if not key_id:
            raise ValueError(
                f"gpio_bind.keys[{idx}] 缺少 key_id（须与 board_key.h 中 BOARD_* 枚举后缀一致，如 KEY1）"
            )
        key_ids.append(str(key_id))

    lines.append("keys {")
    lines.append('    compatible = "brd,gpio-keys";')
    lines.append("    /* 顺序须与 bsl/inc/board_key.h 中 BOARD_KEY* 枚举一致 */")
    key_name_list = ", ".join(f'"{kid}"' for kid in key_ids)
    lines.append(f"    key-names = {key_name_list};")
    lines.append("")
    for idx, key in enumerate(keys):
        pinmap_label = key.get("pinmap_label")
        if not pinmap_label:
            raise ValueError(f"gpio_bind.keys[{idx}] 缺少 pinmap_label")
        port, pin = _resolve_pinmap_label(index, pinmap_label)
        node = key.get("node") or f"key{idx}"
        label = key.get("label") or pinmap_label
        lines.append(f"    key{idx}: {node} {{")
        lines.append(f'        gpio-port = "{port}";')
        lines.append(f"        gpio-pin = <{pin}>;")
        if key.get("pressed_high", True):
            lines.append("        pressed-high;")
        else:
            lines.append("        pressed-low;")
        lines.append(f'        label = "{label}";')
        lines.append(f'        key-id = "{key_ids[idx]}";')
        lines.append("    };")
    lines.extend(["};", ""])

    beep = bind.get("beep")
    if not isinstance(beep, dict):
        raise ValueError("gpio_bind 缺少 beep 对象")
    lines.append("beep {")
    if beep.get("via") == "pcf8574":
        addr = beep.get("pcf8574_addr_7bit")
        pbit = beep.get("pcf8574_pin")
        scl = beep.get("i2c_scl") or {}
        sda = beep.get("i2c_sda") or {}
        if addr is None or pbit is None or not scl.get("port") or scl.get("pin") is None:
            raise ValueError(
                "gpio_bind.beep（pcf8574）须含 pcf8574_addr_7bit、pcf8574_pin、i2c_scl/i2c_sda"
            )
        lines.extend(
            [
                '    compatible = "brd,pcf8574-beep";',
                f"    pcf8574-addr = <{int(addr)}>;",
                f"    pcf8574-pin = <{int(pbit)}>;",
                f'    i2c-scl-port = "{scl["port"]}";',
                f'    i2c-scl-pin = <{int(scl["pin"])}>;',
                f'    i2c-sda-port = "{sda.get("port", scl["port"])}";',
                f'    i2c-sda-pin = <{int(sda.get("pin", scl["pin"]))}>;',
            ]
        )
        if beep.get("active_low", True):
            lines.append("    active-low;")
        else:
            lines.append("    active-high;")
    else:
        if beep.get("pinmap_label"):
            beep_port, beep_pin = _resolve_pinmap_label(index, beep["pinmap_label"])
        else:
            beep_port = beep.get("port")
            beep_pin = beep.get("pin")
            if not beep_port or beep_pin is None:
                raise ValueError("gpio_bind.beep 须含 pinmap_label 或 port+pin")
            beep_pin = int(beep_pin)
        lines.extend(
            [
                '    compatible = "brd,gpio-beep";',
                f'    gpio-port = "{beep_port}";',
                f"    gpio-pin = <{beep_pin}>;",
            ]
        )
        if beep.get("active_high", True):
            lines.append("    active-high;")
        else:
            lines.append("    active-low;")
    lines.append("};")
    lines.append("")
    return "\n".join(lines)


def sync_board_gpio_dtsi(info: dict, pinmap_path: Path) -> Path:
    bind = (info.get("board") or {}).get("gpio_bind")
    if not bind:
        raise ValueError("board.gpio_bind 未配置，无法生成 *-gpio.dtsi")
    if not pinmap_path.is_file():
        raise FileNotFoundError(pinmap_path)
    from dts import parse_pinmap_index

    index = parse_pinmap_index(pinmap_path.read_text(encoding="utf-8"))
    text = generate_gpio_dtsi(bind, index)
    out = board_gpio_dtsi_path(info)
    from generate import write_generated_text

    write_generated_text(out, text)
    return out