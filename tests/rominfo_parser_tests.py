#!/usr/bin/env python3

from __future__ import annotations

import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))

import rominfo  # noqa: E402


class RomInfoParserTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.games = rominfo.parse(ROOT / "resources" / "cps2" / "rominfo.cps2")
        rominfo.validate_parent_graph(cls.games, "cps2")
        cls.by_name = {game.name: game for game in cls.games}

    def test_real_cps2_topology_shape(self):
        self.assertEqual(len(self.games), 286)
        self.assertEqual(sum(len(game.regions) for game in self.games), 1387)
        self.assertEqual(
            sum(len(region.roms) for game in self.games for region in game.regions),
            5382,
        )
        self.assertEqual(
            {region.name for game in self.games for region in game.regions},
            {"CPU1", "CPU2", "GFX1", "SOUND1", "USER1"},
        )

    def test_representative_parent_and_selector_semantics(self):
        root = self.by_name["ssf2"]
        clone = self.by_name["ssf2ta"]
        self.assertEqual(root.parent, "cps2")
        self.assertEqual(clone.parent, "ssf2t")
        self.assertEqual((root.machine, root.input, root.init, root.rotation), (0, 1, 0, 0))

    def test_rom_and_romx_records_round_trip_source_semantics(self):
        game = self.by_name["1944"]
        regions = {region.name: region for region in game.regions}
        self.assertEqual(regions["CPU1"].size, 0x180000)
        self.assertEqual(regions["USER1"].size, 0x080000)

        cpu2 = regions["CPU2"].roms
        self.assertEqual(cpu2[0].name, "nff.01")
        self.assertEqual(cpu2[0].load_type, 0)
        self.assertEqual(cpu2[1].load_type, 1)
        self.assertEqual(cpu2[1].name, "")
        self.assertEqual((cpu2[1].offset, cpu2[1].length, cpu2[1].crc), (0x10000, 0x18000, 0))

        gfx = regions["GFX1"].roms[0]
        self.assertTrue(gfx.is_romx)
        self.assertEqual(gfx.name, "nff.13m")
        self.assertEqual((gfx.group, gfx.skip), (2, 6))
        self.assertEqual(gfx.crc, 0xC9FCA741)

    def test_duplicate_names_and_parent_cycles_are_rejected(self):
        duplicate = """\
FILENAME( a, cps2, 0, 0, 0, 0 )
END
FILENAME( a, cps2, 0, 0, 0, 0 )
END
"""
        with self.assertRaisesRegex(rominfo.RomInfoError, "duplicate FILENAME"):
            rominfo.parse_text(duplicate)

        cycle = """\
FILENAME( a, b, 0, 0, 0, 0 )
END
FILENAME( b, a, 0, 0, 0, 0 )
END
"""
        games = rominfo.parse_text(cycle)
        with self.assertRaisesRegex(rominfo.RomInfoError, "parent cycle"):
            rominfo.validate_parent_graph(games, "cps2")

    def test_unresolved_parent_and_malformed_records_are_rejected(self):
        games = rominfo.parse_text("FILENAME( a, missing, 0, 0, 0, 0 )\nEND\n")
        with self.assertRaisesRegex(rominfo.RomInfoError, "unresolved parent"):
            rominfo.validate_parent_graph(games, "cps2")

        malformed = """\
FILENAME( a, cps2, 0, 0, 0, 0 )
\tREGION( 0x100, CPU1, 0 )
\tROM( 0, foo.bin, 0, 0x10 )
END
"""
        with self.assertRaisesRegex(rominfo.RomInfoError, "ROM requires 5 fields"):
            rominfo.parse_text(malformed)


if __name__ == "__main__":
    unittest.main()
