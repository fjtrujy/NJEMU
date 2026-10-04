#!/usr/bin/env python3
"""Derive NJEMU's build version from semantic Git tags and source identity."""

from __future__ import annotations

import argparse
import json
import os
import re
import subprocess
from dataclasses import asdict, dataclass
from pathlib import Path

FALLBACK_VERSION = "2.4.0"
SEMVER_RE = re.compile(
    r"^(?P<major>0|[1-9][0-9]*)\."
    r"(?P<minor>0|[1-9][0-9]*)\."
    r"(?P<patch>0|[1-9][0-9]*)"
    r"(?:-(?:[0-9A-Za-z-]+(?:\.[0-9A-Za-z-]+)*))?"
    r"(?:\+(?:[0-9A-Za-z-]+(?:\.[0-9A-Za-z-]+)*))?$"
)
TAG_RE = re.compile(r"^v(?P<major>0|[1-9][0-9]*)\.(?P<minor>0|[1-9][0-9]*)\.(?P<patch>0|[1-9][0-9]*)$")


@dataclass(frozen=True)
class VersionInfo:
    version: str
    release_version: str
    major: int
    minor: int
    patch: int
    git_sha: str
    commits_since_tag: int
    dirty: bool
    exact_tag: bool
    source: str


def _git(source_dir: Path, *args: str, check: bool = True) -> str | None:
    try:
        result = subprocess.run(
            ["git", "-C", str(source_dir), *args],
            check=check,
            stdout=subprocess.PIPE,
            stderr=subprocess.DEVNULL,
            text=True,
        )
    except (FileNotFoundError, subprocess.CalledProcessError):
        return None
    if result.returncode != 0:
        return None
    return result.stdout.strip()


def _parse_base(version: str) -> tuple[int, int, int]:
    match = SEMVER_RE.fullmatch(version)
    if not match:
        raise ValueError(f"not a Semantic Version: {version!r}")
    return tuple(int(match.group(name)) for name in ("major", "minor", "patch"))


def _nearest_semver_tag(source_dir: Path) -> tuple[str, int] | None:
    output = _git(
        source_dir,
        "for-each-ref",
        "--merged=HEAD",
        "--format=%(refname:short)",
        "refs/tags",
        check=False,
    )
    if not output:
        return None

    candidates: list[tuple[int, int, str]] = []
    for tag in output.splitlines():
        if not TAG_RE.fullmatch(tag):
            continue
        count_text = _git(source_dir, "rev-list", "--count", f"{tag}..HEAD", check=False)
        timestamp_text = _git(source_dir, "log", "-1", "--format=%ct", tag, check=False)
        if count_text is None or timestamp_text is None:
            continue
        candidates.append((int(count_text), -int(timestamp_text), tag))

    if not candidates:
        return None
    count, _, tag = min(candidates)
    return tag, count


def derive_version(source_dir: Path, override: str | None = None) -> VersionInfo:
    source_dir = source_dir.resolve()
    override = override or os.environ.get("NJEMU_VERSION_OVERRIDE")
    if override:
        major, minor, patch = _parse_base(override)
        return VersionInfo(
            version=override,
            release_version=f"{major}.{minor}.{patch}",
            major=major,
            minor=minor,
            patch=patch,
            git_sha="override",
            commits_since_tag=0,
            dirty=False,
            exact_tag=False,
            source="override",
        )

    fallback_major, fallback_minor, fallback_patch = _parse_base(FALLBACK_VERSION)
    inside = _git(source_dir, "rev-parse", "--is-inside-work-tree", check=False)
    if inside != "true":
        return VersionInfo(
            version=f"{FALLBACK_VERSION}+source",
            release_version=FALLBACK_VERSION,
            major=fallback_major,
            minor=fallback_minor,
            patch=fallback_patch,
            git_sha="unknown",
            commits_since_tag=0,
            dirty=False,
            exact_tag=False,
            source="archive",
        )

    sha = _git(source_dir, "rev-parse", "--short=8", "HEAD") or "unknown"
    dirty = bool(_git(source_dir, "status", "--porcelain", "--untracked-files=no", check=False))
    nearest = _nearest_semver_tag(source_dir)

    if nearest is None:
        total_text = _git(source_dir, "rev-list", "--count", "HEAD", check=False) or "0"
        commit_count = int(total_text)
        metadata = f"g{sha}" + (".dirty" if dirty else "")
        version = f"{FALLBACK_VERSION}-pre.{commit_count}+{metadata}"
        return VersionInfo(
            version=version,
            release_version=FALLBACK_VERSION,
            major=fallback_major,
            minor=fallback_minor,
            patch=fallback_patch,
            git_sha=sha,
            commits_since_tag=commit_count,
            dirty=dirty,
            exact_tag=False,
            source="git-no-semver-tag",
        )

    tag, commit_count = nearest
    tag_match = TAG_RE.fullmatch(tag)
    assert tag_match is not None
    major, minor, patch = (int(tag_match.group(name)) for name in ("major", "minor", "patch"))
    release_version = f"{major}.{minor}.{patch}"
    exact_tag = commit_count == 0 and not dirty

    if exact_tag:
        version = release_version
    else:
        metadata = f"{commit_count}.g{sha}" + (".dirty" if dirty else "")
        version = f"{release_version}+{metadata}"

    return VersionInfo(
        version=version,
        release_version=release_version,
        major=major,
        minor=minor,
        patch=patch,
        git_sha=sha,
        commits_since_tag=commit_count,
        dirty=dirty,
        exact_tag=exact_tag,
        source="git-tag",
    )


def _cmake_escape(value: str) -> str:
    return value.replace("\\", "\\\\").replace('"', '\\"').replace(";", "\\;")


def format_cmake(info: VersionInfo) -> str:
    values = {
        "NJEMU_VERSION": info.version,
        "NJEMU_RELEASE_VERSION": info.release_version,
        "NJEMU_VERSION_MAJOR": str(info.major),
        "NJEMU_VERSION_MINOR": str(info.minor),
        "NJEMU_VERSION_PATCH": str(info.patch),
        "NJEMU_GIT_SHA": info.git_sha,
        "NJEMU_COMMITS_SINCE_TAG": str(info.commits_since_tag),
        "NJEMU_VERSION_DIRTY": "TRUE" if info.dirty else "FALSE",
        "NJEMU_VERSION_EXACT_TAG": "TRUE" if info.exact_tag else "FALSE",
        "NJEMU_VERSION_SOURCE": info.source,
    }
    return "".join(f'set({key} "{_cmake_escape(value)}")\n' for key, value in values.items())


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--source-dir", type=Path, default=Path.cwd())
    parser.add_argument("--override")
    parser.add_argument("--format", choices=("version", "json", "cmake"), default="version")
    args = parser.parse_args()

    try:
        info = derive_version(args.source_dir, args.override)
    except ValueError as exc:
        parser.error(str(exc))

    if args.format == "version":
        print(info.version)
    elif args.format == "json":
        print(json.dumps(asdict(info), sort_keys=True))
    else:
        print(format_cmake(info), end="")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
