#!/usr/bin/env python3

from __future__ import annotations

import importlib.util
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("njemu_version", ROOT / "tools" / "njemu_version.py")
assert SPEC and SPEC.loader
MODULE = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = MODULE
SPEC.loader.exec_module(MODULE)


def git(repo: Path, *args: str) -> str:
    result = subprocess.run(
        ["git", "-C", str(repo), *args],
        check=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
    )
    return result.stdout.strip()


def commit(repo: Path, name: str) -> None:
    path = repo / "content.txt"
    previous = path.read_text(encoding="utf-8") if path.exists() else ""
    path.write_text(previous + name + "\n", encoding="utf-8")
    git(repo, "add", "content.txt")
    git(repo, "commit", "-m", name)


class VersionDerivationTests(unittest.TestCase):
    def make_repo(self) -> Path:
        tmp = tempfile.TemporaryDirectory()
        self.addCleanup(tmp.cleanup)
        repo = Path(tmp.name)
        git(repo, "init")
        git(repo, "config", "user.name", "NJEMU Test")
        git(repo, "config", "user.email", "njemu@example.invalid")
        return repo

    def test_source_archive_fallback(self) -> None:
        tmp = tempfile.TemporaryDirectory()
        self.addCleanup(tmp.cleanup)
        info = MODULE.derive_version(Path(tmp.name))
        self.assertEqual(info.version, "2.4.0+source")
        self.assertEqual(info.release_version, "2.4.0")
        self.assertEqual(info.git_sha, "unknown")
        self.assertEqual(info.source, "archive")

    def test_repository_without_semver_tag_is_marked_prerelease(self) -> None:
        repo = self.make_repo()
        commit(repo, "initial")
        info = MODULE.derive_version(repo)
        self.assertRegex(info.version, r"^2\.4\.0-pre\.1\+g[0-9a-f]{8}$")
        self.assertEqual(info.commits_since_tag, 1)
        self.assertFalse(info.exact_tag)
        self.assertEqual(info.source, "git-no-semver-tag")

    def test_exact_semver_tag_uses_release_version(self) -> None:
        repo = self.make_repo()
        commit(repo, "release")
        git(repo, "tag", "v1.2.3")
        info = MODULE.derive_version(repo)
        self.assertEqual(info.version, "1.2.3")
        self.assertEqual(info.release_version, "1.2.3")
        self.assertEqual((info.major, info.minor, info.patch), (1, 2, 3))
        self.assertEqual(info.commits_since_tag, 0)
        self.assertTrue(info.exact_tag)

    def test_development_version_counts_commits_since_tag(self) -> None:
        repo = self.make_repo()
        commit(repo, "release")
        git(repo, "tag", "v1.2.3")
        commit(repo, "one")
        commit(repo, "two")
        info = MODULE.derive_version(repo)
        self.assertRegex(info.version, r"^1\.2\.3\+2\.g[0-9a-f]{8}$")
        self.assertEqual(info.commits_since_tag, 2)
        self.assertFalse(info.exact_tag)

    def test_dirty_build_is_identified_without_counting_untracked_files(self) -> None:
        repo = self.make_repo()
        commit(repo, "release")
        git(repo, "tag", "v1.2.3")
        (repo / "content.txt").write_text("dirty\n", encoding="utf-8")
        (repo / "untracked.bin").write_bytes(b"ignored for dirty identity")
        info = MODULE.derive_version(repo)
        self.assertRegex(info.version, r"^1\.2\.3\+0\.g[0-9a-f]{8}\.dirty$")
        self.assertTrue(info.dirty)

    def test_untracked_files_alone_do_not_mark_build_dirty(self) -> None:
        repo = self.make_repo()
        commit(repo, "release")
        git(repo, "tag", "v1.2.3")
        (repo / "runtime.zip").write_bytes(b"local runtime data")
        info = MODULE.derive_version(repo)
        self.assertEqual(info.version, "1.2.3")
        self.assertFalse(info.dirty)

    def test_non_semver_tag_is_ignored(self) -> None:
        repo = self.make_repo()
        commit(repo, "release")
        git(repo, "tag", "v1.2.3")
        commit(repo, "development")
        git(repo, "tag", "latest-test")
        info = MODULE.derive_version(repo)
        self.assertRegex(info.version, r"^1\.2\.3\+1\.g[0-9a-f]{8}$")

    def test_override_is_validated_and_centralized(self) -> None:
        tmp = tempfile.TemporaryDirectory()
        self.addCleanup(tmp.cleanup)
        info = MODULE.derive_version(Path(tmp.name), override="3.1.4-rc.2+ci.7")
        self.assertEqual(info.version, "3.1.4-rc.2+ci.7")
        self.assertEqual(info.release_version, "3.1.4")
        self.assertEqual((info.major, info.minor, info.patch), (3, 1, 4))
        with self.assertRaises(ValueError):
            MODULE.derive_version(Path(tmp.name), override="3.1")


if __name__ == "__main__":
    unittest.main()
