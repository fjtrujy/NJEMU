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
SOURCE = ROOT / "metadata/mvs_dips.json"

spec = importlib.util.spec_from_file_location("dip_metadata", TOOL)
assert spec is not None and spec.loader is not None
dip_metadata = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = dip_metadata
spec.loader.exec_module(dip_metadata)


class MvsDipMetadataGeneratorTests(unittest.TestCase):
    def profiles(self):
        return {profile.name: profile for profile in dip_metadata.load_source(SOURCE)}

    def test_source_has_expected_profiles(self) -> None:
        profiles = self.profiles()
        self.assertEqual(set(profiles), {"default", "pcb", "mjneogeo", "kog"})
        for profile in profiles.values():
            self.assertEqual(tuple(profile.localized), dip_metadata.LANGUAGES)

    def test_mahjong_control_panel_is_disabled_in_every_language(self) -> None:
        profile = self.profiles()["mjneogeo"]
        for language in dip_metadata.LANGUAGES:
            row = profile.localized[language][2]
            self.assertEqual(row.mask, 0x04)
            self.assertEqual(row.enable, 0)

    def test_kog_japanese_autofire_matches_behavior(self) -> None:
        row = self.profiles()["kog"].localized["ja"][2]
        self.assertEqual(row.label, "Autofire (in some games)")
        self.assertEqual(row.enable, 1)
        self.assertEqual(row.mask, 0x04)

    def test_runtime_blob_is_deterministic_and_small(self) -> None:
        profiles = list(self.profiles().values())
        first = dip_metadata.build_blob(profiles)
        second = dip_metadata.build_blob(profiles)
        self.assertEqual(first, second)
        self.assertEqual(first[:4], dip_metadata.MAGIC)
        self.assertLess(len(first), 8 * 1024)

    def test_structural_locale_drift_is_rejected(self) -> None:
        document = json.loads(SOURCE.read_text(encoding="utf-8"))
        broken = copy.deepcopy(document)
        broken["profiles"]["default"]["ja"][0]["mask"] ^= 1
        with tempfile.TemporaryDirectory() as temp_dir:
            path = Path(temp_dir) / "broken.json"
            path.write_text(json.dumps(broken, ensure_ascii=False), encoding="utf-8")
            with self.assertRaises(dip_metadata.DipMetadataError):
                dip_metadata.load_source(path)


if __name__ == "__main__":
    unittest.main()
