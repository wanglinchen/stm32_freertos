#!/usr/bin/env python3
"""host 侧 DTS 解析与 pinmap 索引单测（不经过 verify 全量）。"""
from __future__ import annotations

import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "build"))

import dts as dts_mod  # noqa: E402
import dts_board_gpio as gpio_mod  # noqa: E402
import dts_parse_util as dpu  # noqa: E402


class DtsParserTests(unittest.TestCase):
    def test_apollo_board_parse(self) -> None:
        path = ROOT / "device/dts/boards/apollo-f429.dts"
        board = dts_mod.parse_board_dts(path)
        self.assertEqual(board.led_port, "B")
        self.assertEqual(board.led_pins, (1, 0, 2))
        self.assertEqual(board.led_names, ("R", "G", "B"))
        self.assertEqual(board.hse_hz, 25_000_000)
        self.assertEqual(board.lse_hz, 32768)
        self.assertEqual(len(board.keys), 4)
        self.assertEqual(board.key_names, ("WK_UP", "KEY0", "KEY1", "KEY2"))
        self.assertEqual(board.keys[0][:3], ("A", 0, True))
        self.assertEqual(board.keys[3][:2], ("C", 13))
        self.assertEqual(board.beep_via, "pcf8574")
        self.assertEqual(board.pcf8574_addr7, 0x20)
        self.assertEqual(board.pcf8574_bit, 0)
        self.assertEqual(board.i2c_scl_port, "H")
        self.assertEqual(board.i2c_scl_pin, 4)

    def test_balanced_block_nested(self) -> None:
        body = 'foo { bar { nested = <1>; }; tail = <2>; }'
        inner = dpu.extract_named_block(body, "foo")
        self.assertIsNotNone(inner)
        assert inner is not None
        self.assertIn("nested", inner)
        self.assertIn("tail", inner)


class GpioBindTests(unittest.TestCase):
    def test_gpio_dtsi_from_bind(self) -> None:
        pinmap = ROOT / "device/dts/generated/apollo-f429-pinmap.dtsi"
        info_path = ROOT / "proj/project-info.json"
        if not pinmap.is_file() or not info_path.is_file():
            self.skipTest("缺少 pinmap 或 project-info")
        import json

        bind = json.loads(info_path.read_text(encoding="utf-8"))["board"]["gpio_bind"]
        index = dts_mod.parse_pinmap_index(pinmap.read_text(encoding="utf-8"))
        text = gpio_mod.generate_gpio_dtsi(bind, index)
        self.assertIn('led-names = "R", "G", "B"', text)
        self.assertIn("pins = <1 0 2>", text)
        self.assertIn('compatible = "brd,pcf8574-beep"', text)
        self.assertIn("pcf8574-addr = <32>", text)
        self.assertIn('i2c-scl-port = "H"', text)


class PinmapIndexTests(unittest.TestCase):
    def test_pinmap_lookup_led0(self) -> None:
        path = ROOT / "device/dts/generated/apollo-f429-pinmap.dtsi"
        if not path.is_file():
            self.skipTest("缺少 pinmap，请先 build.py update")
        index = dts_mod.parse_pinmap_index(path.read_text(encoding="utf-8"))
        self.assertIn(("B", 1), index)
        self.assertEqual(index[("B", 1)].label, "LED0")


if __name__ == "__main__":
    unittest.main()