#!/usr/bin/env python3

from __future__ import annotations

import importlib.util
import json
import sys
import tempfile
import unittest
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("package_release", ROOT / "tools" / "package_release.py")
assert SPEC and SPEC.loader
MODULE = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = MODULE
SPEC.loader.exec_module(MODULE)


class ReleasePackagingTests(unittest.TestCase):
    def setUp(self) -> None:
        tmp = tempfile.TemporaryDirectory()
        self.addCleanup(tmp.cleanup)
        self.root = Path(tmp.name)
        self.source = self.root / "source"
        self.output = self.root / "output"
        self.source.mkdir()
        (self.source / "README.md").write_text("NJEMU\n", encoding="utf-8")
        (self.source / "Licence.txt").write_text("GPL\n", encoding="utf-8")

    def make_installs(self, platform: str, version: str = "1.2.3") -> dict[str, Path]:
        installs: dict[str, Path] = {}
        for core in MODULE.CORES:
            directory = self.root / "installs" / core
            directory.mkdir(parents=True)
            (directory / "version.txt").write_text(version + "\n", encoding="utf-8")
            if platform == "psp":
                binary = directory / "EBOOT.PBP"
            elif platform == "psvita":
                binary = directory / f"{core}.vpk"
            else:
                binary = directory / core
            binary.write_bytes((core + platform).encode("ascii"))
            if platform != "psvita":
                (directory / "lang").mkdir()
                (directory / "lang" / "en.lng").write_bytes(b"catalog")
                (directory / "roms").mkdir()
                (directory / "roms" / "_placeholder").write_bytes(b"")
            installs[core] = directory
        return installs

    def test_psp_archive_contains_all_core_install_trees(self) -> None:
        result = MODULE.build_package(
            source_dir=self.source,
            platform="psp",
            version="1.2.3",
            installs=self.make_installs("psp"),
            output_dir=self.output,
        )
        self.assertEqual(result.archive.name, "njemu-1.2.3-psp.zip")
        with zipfile.ZipFile(result.archive) as archive:
            names = set(archive.namelist())
            self.assertIn("NJEMU/CPS1/EBOOT.PBP", names)
            self.assertIn("NJEMU/NCDZ/lang/en.lng", names)
            self.assertIn("NJEMU/release-manifest.json", names)
            manifest = json.loads(archive.read("NJEMU/release-manifest.json"))
            self.assertEqual(manifest["version"], "1.2.3")
            self.assertEqual(manifest["platform"], "psp")
        sidecar = json.loads(result.metadata.read_text(encoding="utf-8"))
        self.assertEqual(sidecar["sha256"], result.sha256)
        self.assertEqual(sidecar["size"], result.size)

    def test_vita_archive_contains_only_ready_to_install_vpks_for_cores(self) -> None:
        result = MODULE.build_package(
            source_dir=self.source,
            platform="psvita",
            version="1.2.3",
            installs=self.make_installs("psvita"),
            output_dir=self.output,
        )
        with zipfile.ZipFile(result.archive) as archive:
            names = set(archive.namelist())
            for core in MODULE.CORES:
                self.assertIn(f"NJEMU/{core}.vpk", names)
                self.assertNotIn(f"NJEMU/{core}/version.txt", names)
            self.assertIn("NJEMU/version.txt", names)

    def test_version_mismatch_is_rejected(self) -> None:
        installs = self.make_installs("desktop-linux", version="1.2.2")
        with self.assertRaisesRegex(ValueError, "version mismatch"):
            MODULE.build_package(
                source_dir=self.source,
                platform="desktop-linux",
                version="1.2.3",
                installs=installs,
                output_dir=self.output,
            )

    def test_user_runtime_payload_is_rejected(self) -> None:
        installs = self.make_installs("psp")
        (installs["MVS"] / "roms" / "neogeo.zip").write_bytes(b"bios")
        with self.assertRaisesRegex(ValueError, "runtime data|ROM/cache"):
            MODULE.build_package(
                source_dir=self.source,
                platform="psp",
                version="1.2.3",
                installs=installs,
                output_dir=self.output,
            )


if __name__ == "__main__":
    unittest.main()
