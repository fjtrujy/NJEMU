#!/usr/bin/env python3

from __future__ import annotations

import sys
import unittest
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))

import game_metadata  # noqa: E402


class GameMetadataGeneratorTests(unittest.TestCase):
    def load(self, core: str):
        source = ROOT / "metadata" / f"{core}.tsv"
        rominfo = None if core == "ncdz" else ROOT / "resources" / core / f"rominfo.{core}"
        rows = game_metadata.read_source(source, core)
        game_metadata.validate(rows, core, rominfo)
        return rows

    def test_all_core_sources_validate_and_generate_deterministically(self):
        expected_counts = {"cps1": 137, "cps2": 286, "mvs": 305, "ncdz": 97}
        for core, expected_count in expected_counts.items():
            with self.subTest(core=core):
                rows = self.load(core)
                self.assertEqual(len(rows), expected_count)
                first = game_metadata.build_blob(rows, core)
                second = game_metadata.build_blob(rows, core)
                self.assertEqual(first, second)

                header = game_metadata.HEADER.unpack_from(first)
                magic, version, core_id, count, record_size, records_offset, strings_offset, strings_size, crc = header
                self.assertEqual(magic, game_metadata.MAGIC)
                self.assertEqual(version, game_metadata.VERSION)
                self.assertEqual(core_id, game_metadata.CORE_IDS[core])
                self.assertEqual(count, expected_count)
                self.assertEqual(record_size, game_metadata.RECORD.size)
                self.assertEqual(records_offset, game_metadata.HEADER.size)
                self.assertEqual(strings_offset + strings_size, len(first))
                self.assertEqual(zlib.crc32(first[game_metadata.HEADER.size :]) & 0xFFFFFFFF, crc)

    def test_mvs_browser_metadata_covers_previously_missing_supported_sets(self):
        rows = {row.name: row for row in self.load("mvs")}
        for name in ("kof2001d", "kof2k1hd", "kof2kd", "roboarma", "samsho2k2"):
            self.assertIn(name, rows)
            self.assertTrue(rows[name].titles[0])

    def test_cps2_every_supported_set_is_keyed_or_phoenix(self):
        phoenix = game_metadata.CORE_FLAGS["cps2"]["phoenix"]
        for row in self.load("cps2"):
            has_key = row.data0 != 0 or row.data1 != 0 or row.data2 != 0
            self.assertNotEqual(has_key, bool(row.core_flags & phoenix), row.name)

    def test_cps2_representative_runtime_metadata(self):
        rows = {row.name: row for row in self.load("cps2")}
        phoenix = game_metadata.CORE_FLAGS["cps2"]["phoenix"]
        override = game_metadata.CORE_FLAGS["cps2"]["cache_parent_override"]
        independent = game_metadata.CORE_FLAGS["cps2"]["cache_independent"]

        self.assertEqual(
            (rows["ssf2"].data0, rows["ssf2"].data1, rows["ssf2"].data2),
            (0x23456789, 0xABCDEF01, 0x400000),
        )
        self.assertEqual(rows["jyangoku"].data2, 0)
        self.assertTrue(rows["ddtodd"].core_flags & phoenix)
        self.assertEqual(rows["ddtodd"].data0, 0)
        self.assertTrue(rows["ssf2ta"].core_flags & override)
        self.assertEqual(rows["ssf2ta"].aux_name, "ssf2t")
        self.assertTrue(rows["mpangj"].core_flags & independent)

    def test_mvs_representative_processed_asset_ownership(self):
        rows = {row.name: row for row in self.load("mvs")}
        owns_crom = game_metadata.CORE_FLAGS["mvs"]["owns_crom"]
        owns_srom = game_metadata.CORE_FLAGS["mvs"]["owns_srom"]
        owns_vrom = game_metadata.CORE_FLAGS["mvs"]["owns_vrom"]

        self.assertEqual(
            rows["kof96ae"].core_flags,
            owns_crom | owns_srom | owns_vrom,
        )
        self.assertEqual(rows["kof97ps"].core_flags, owns_crom)
        self.assertEqual(rows["matrimbl"].core_flags, owns_vrom)
        self.assertEqual(rows["mslug"].core_flags, 0)

    def test_stale_legacy_aliases_are_not_canonical_records(self):
        cps2 = {row.name for row in self.load("cps2")}
        mvs = {row.name for row in self.load("mvs")}
        self.assertNotIn("gigaman2", cps2)
        for name in ("fatfursa", "kf2k2ur", "kof96pm", "kof97c", "kof97prc", "kof97xt", "kof98a", "kof98evo"):
            self.assertNotIn(name, mvs)

    def test_rominfo_divergence_is_rejected(self):
        rows = self.load("cps1")
        with self.assertRaisesRegex(game_metadata.MetadataError, "identity divergence"):
            game_metadata.validate(rows[:-1], "cps1", ROOT / "resources/cps1/rominfo.cps1")

    def test_ncdz_ngh_values_are_unique(self):
        rows = self.load("ncdz")
        ngh = [row.data0 for row in rows]
        self.assertEqual(len(ngh), len(set(ngh)))


if __name__ == "__main__":
    unittest.main()
