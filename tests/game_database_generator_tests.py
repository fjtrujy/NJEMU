#!/usr/bin/env python3

from __future__ import annotations

import sys
import unittest
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))

import game_database  # noqa: E402
import game_metadata  # noqa: E402


class GameDatabaseGeneratorTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.metadata_path = ROOT / "metadata" / "cps2.tsv"
        cls.rominfo_path = ROOT / "resources" / "cps2" / "rominfo.cps2"
        cls.games = game_database.load_sources(cls.metadata_path, cls.rominfo_path)
        cls.blob = game_database.build_blob(cls.games)
        cls.decoded = game_database.decode_blob(cls.blob)

    def test_generated_database_is_deterministic_and_compact(self):
        second = game_database.build_blob(self.games)
        self.assertEqual(self.blob, second)
        self.assertLess(len(self.blob), self.rominfo_path.stat().st_size)

        header = game_database.HEADER.unpack_from(self.blob)
        self.assertEqual(header[0], game_database.MAGIC)
        self.assertEqual(header[1], game_database.VERSION)
        self.assertEqual(header[2], game_database.CORE_CPS2)
        self.assertEqual(header[3], game_database.HEADER.size)
        self.assertEqual(header[4], game_database.GAME_RECORD.size)
        self.assertEqual(header[5], game_database.REGION_RECORD.size)
        self.assertEqual(header[6], game_database.ROM_RECORD.size)
        self.assertEqual(header[7], game_database.CPS2_RECORD.size)
        self.assertEqual(header[9:12], (286, 1387, 5382))
        self.assertEqual(header[-1], len(self.blob))
        self.assertEqual(
            zlib.crc32(self.blob[game_database.HEADER.size :]) & 0xFFFFFFFF,
            header[-2],
        )

    def test_every_generated_record_has_exact_source_parity(self):
        self.assertEqual(len(self.decoded), len(self.games))
        for source, decoded in zip(self.games, self.decoded):
            with self.subTest(game=source.metadata.name):
                self.assertEqual(decoded.name, source.metadata.name)
                self.assertEqual(decoded.parent, source.topology.parent)
                self.assertEqual(decoded.titles, source.metadata.titles)
                self.assertEqual(decoded.display_flags, source.metadata.display_flags)
                self.assertEqual(decoded.core_flags, source.metadata.core_flags)
                self.assertEqual(decoded.aux_name, source.metadata.aux_name)
                self.assertEqual(
                    decoded.data,
                    (source.metadata.data0, source.metadata.data1, source.metadata.data2),
                )
                self.assertEqual(decoded.machine, source.topology.machine)
                self.assertEqual(decoded.input, source.topology.input)
                self.assertEqual(decoded.init, source.topology.init)
                self.assertEqual(decoded.rotation, source.topology.rotation)
                self.assertEqual(decoded.regions, source.topology.regions)

    def test_representative_cps2_policy_and_topology_records(self):
        decoded = {game.name: game for game in self.decoded}
        phoenix = game_metadata.CORE_FLAGS["cps2"]["phoenix"]
        override = game_metadata.CORE_FLAGS["cps2"]["cache_parent_override"]
        independent = game_metadata.CORE_FLAGS["cps2"]["cache_independent"]

        self.assertEqual(decoded["ssf2"].data, (0x23456789, 0xABCDEF01, 0x400000))
        self.assertTrue(decoded["ddtodd"].core_flags & phoenix)
        self.assertEqual(decoded["ddtodd"].data, (0, 0, 0))
        self.assertTrue(decoded["ssf2ta"].core_flags & override)
        self.assertEqual(decoded["ssf2ta"].aux_name, "ssf2t")
        self.assertEqual(decoded["ssf2ta"].parent, "ssf2t")
        self.assertTrue(decoded["mpangj"].core_flags & independent)
        self.assertEqual(decoded["jyangoku"].data[2], 0)

        regions = {region.name: region for region in decoded["1944"].regions}
        self.assertEqual(regions["GFX1"].roms[0].group, 2)
        self.assertEqual(regions["GFX1"].roms[0].skip, 6)
        self.assertTrue(regions["GFX1"].roms[0].is_romx)
        self.assertEqual(regions["CPU2"].roms[1].load_type, 1)
        self.assertEqual(regions["CPU2"].roms[1].name, "")

    def test_corruption_truncation_and_section_bounds_are_rejected(self):
        truncated = self.blob[: game_database.HEADER.size - 1]
        with self.assertRaisesRegex(game_database.GameDatabaseError, "truncated"):
            game_database.decode_blob(truncated)

        corrupt = bytearray(self.blob)
        corrupt[game_database.HEADER.size + 7] ^= 0x01
        with self.assertRaisesRegex(game_database.GameDatabaseError, "checksum"):
            game_database.decode_blob(bytes(corrupt))

        invalid_layout = bytearray(self.blob)
        header = list(game_database.HEADER.unpack_from(invalid_layout))
        header[13] += 1  # regions_offset
        game_database.HEADER.pack_into(invalid_layout, 0, *header)
        with self.assertRaisesRegex(game_database.GameDatabaseError, "section layout"):
            game_database.decode_blob(bytes(invalid_layout))

    def test_metadata_topology_identity_disagreement_is_rejected(self):
        metadata = [game.metadata for game in self.games]
        topology = [game.topology for game in self.games]
        with self.assertRaisesRegex(game_database.GameDatabaseError, "identity divergence"):
            game_database.merge_sources(metadata[:-1], topology)


if __name__ == "__main__":
    unittest.main()
