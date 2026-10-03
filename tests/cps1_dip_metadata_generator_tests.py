#!/usr/bin/env python3
from __future__ import annotations

import copy
import importlib.util
import json
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
TOOL = ROOT / "tools/dip_metadata.py"
SOURCE = ROOT / "metadata/cps1_dips.json"

spec = importlib.util.spec_from_file_location("cps1_dip_metadata", TOOL)
assert spec is not None and spec.loader is not None
cps1_dip_metadata = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = cps1_dip_metadata
spec.loader.exec_module(cps1_dip_metadata)


class Cps1DipMetadataGeneratorTests(unittest.TestCase):
    def test_source_has_expected_profiles_and_languages(self) -> None:
        profiles = cps1_dip_metadata.load_source(SOURCE)
        self.assertEqual(len(profiles), 33)
        self.assertEqual(
            tuple(profiles[0].localized), cps1_dip_metadata.LANGUAGES
        )
        names = {profile.name for profile in profiles}
        self.assertIn("forgottn", names)
        self.assertIn("msword", names)
        self.assertIn("punisherbz", names)
        self.assertIn("wofhfh", names)

    def test_msword_vitality_pack_range_matches_four_choices(self) -> None:
        profiles = {profile.name: profile for profile in cps1_dip_metadata.load_source(SOURCE)}
        for language in cps1_dip_metadata.LANGUAGES:
            rows = profiles["msword"].localized[language]
            row = next(item for item in rows if item.mask == 0x03 and len(item.values) == 4)
            self.assertEqual(row.value_max, 3)

    def test_runtime_blob_is_deterministic(self) -> None:
        profiles = cps1_dip_metadata.load_source(SOURCE)
        first = cps1_dip_metadata.build_blob(profiles)
        second = cps1_dip_metadata.build_blob(profiles)
        self.assertEqual(first, second)
        self.assertEqual(first[:4], cps1_dip_metadata.MAGIC)
        self.assertLess(len(first), 64 * 1024)

    def test_structural_locale_drift_is_rejected(self) -> None:
        document = json.loads(SOURCE.read_text(encoding="utf-8"))
        broken = copy.deepcopy(document)
        broken["profiles"]["forgottn"]["ja"][0]["mask"] ^= 1
        with tempfile.TemporaryDirectory() as temp_dir:
            path = Path(temp_dir) / "broken.json"
            path.write_text(json.dumps(broken, ensure_ascii=False), encoding="utf-8")
            with self.assertRaises(cps1_dip_metadata.DipMetadataError):
                cps1_dip_metadata.load_source(path)

    def test_invalid_value_range_is_rejected(self) -> None:
        document = json.loads(SOURCE.read_text(encoding="utf-8"))
        broken = copy.deepcopy(document)
        broken["profiles"]["msword"]["en"][6]["value_max"] = 1
        with tempfile.TemporaryDirectory() as temp_dir:
            path = Path(temp_dir) / "broken.json"
            path.write_text(json.dumps(broken, ensure_ascii=False), encoding="utf-8")
            with self.assertRaises(cps1_dip_metadata.DipMetadataError):
                cps1_dip_metadata.load_source(path)


if __name__ == "__main__":
    unittest.main()
