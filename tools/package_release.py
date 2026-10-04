#!/usr/bin/env python3
"""Package canonical NJEMU install trees into a user-facing release archive."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import stat
import zipfile
from dataclasses import dataclass
from pathlib import Path, PurePosixPath
from typing import Iterable

CORES = ("CPS1", "CPS2", "MVS", "NCDZ")
PLATFORM_SLUG_RE = re.compile(r"^[a-z0-9][a-z0-9-]*$")
VERSION_FILENAME_RE = re.compile(r"^[0-9A-Za-z.+-]+$")
RUNTIME_DATA_DIRS = {"roms", "cache", "processed", "state", "nvram", "memcard", "config", "cheats", "data"}
FORBIDDEN_RUNTIME_FILES = {"neocd.bin", "000-lo.lo", "backup.bin", "njemu.ini", "command.dat"}


@dataclass(frozen=True)
class PackageResult:
    archive: Path
    metadata: Path
    sha256: str
    size: int


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def _zip_info(path: Path, arcname: PurePosixPath) -> zipfile.ZipInfo:
    info = zipfile.ZipInfo.from_file(path, str(arcname))
    # ZipInfo.from_file preserves executable mode bits. Normalize timestamps to
    # the ZIP epoch so identical staged inputs produce identical archives.
    info.date_time = (1980, 1, 1, 0, 0, 0)
    info.compress_type = zipfile.ZIP_DEFLATED
    return info


def _write_file(archive: zipfile.ZipFile, source: Path, arcname: PurePosixPath) -> None:
    info = _zip_info(source, arcname)
    archive.writestr(info, source.read_bytes())


def _validate_install_tree(core: str, install_dir: Path, platform: str, version: str) -> None:
    if not install_dir.is_dir():
        raise ValueError(f"{core}: install tree does not exist: {install_dir}")

    version_file = install_dir / "version.txt"
    if not version_file.is_file():
        raise ValueError(f"{core}: install tree is missing version.txt")
    installed_version = version_file.read_text(encoding="utf-8").strip()
    if installed_version != version:
        raise ValueError(
            f"{core}: version mismatch: install has {installed_version!r}, expected {version!r}"
        )

    if platform == "psp":
        required = install_dir / "EBOOT.PBP"
    elif platform == "psvita":
        required = install_dir / f"{core}.vpk"
    else:
        required = install_dir / core
    if not required.is_file():
        raise ValueError(f"{core}: canonical executable/package is missing: {required.name}")

    for path in install_dir.rglob("*"):
        if not path.is_file():
            continue
        rel = path.relative_to(install_dir)
        lower_parts = [part.lower() for part in rel.parts]
        if path.name.lower() in FORBIDDEN_RUNTIME_FILES:
            raise ValueError(f"{core}: user/runtime file must not be released: {rel}")
        if any(part in RUNTIME_DATA_DIRS for part in lower_parts[:-1]) and path.name != "_placeholder":
            raise ValueError(f"{core}: non-placeholder runtime data must not be released: {rel}")
        if path.suffix.lower() in {".zip", ".cache"}:
            raise ValueError(f"{core}: ROM/cache-like file must not be released: {rel}")
        if re.fullmatch(r".+\.sv[0-9]", path.name, re.IGNORECASE):
            raise ValueError(f"{core}: save state must not be released: {rel}")


def _iter_tree_files(root: Path) -> Iterable[tuple[Path, PurePosixPath]]:
    for path in sorted(root.rglob("*"), key=lambda item: item.as_posix()):
        if path.is_file():
            yield path, PurePosixPath(*path.relative_to(root).parts)


def build_package(
    *,
    source_dir: Path,
    platform: str,
    version: str,
    installs: dict[str, Path],
    output_dir: Path,
) -> PackageResult:
    if not PLATFORM_SLUG_RE.fullmatch(platform):
        raise ValueError(f"invalid platform slug: {platform!r}")
    if not VERSION_FILENAME_RE.fullmatch(version):
        raise ValueError(f"version is not safe for an archive filename: {version!r}")
    if tuple(sorted(installs)) != tuple(sorted(CORES)):
        raise ValueError(f"install trees must be provided for exactly: {', '.join(CORES)}")

    source_dir = source_dir.resolve()
    output_dir.mkdir(parents=True, exist_ok=True)
    for core in CORES:
        _validate_install_tree(core, installs[core], platform, version)

    archive_name = f"njemu-{version}-{platform}.zip"
    archive_path = output_dir / archive_name
    metadata_path = output_dir / f"{archive_name}.json"
    root = PurePosixPath("NJEMU")

    package_manifest: dict[str, object] = {
        "schema": 1,
        "name": "NJEMU",
        "version": version,
        "platform": platform,
        "cores": list(CORES),
        "files": [],
    }
    file_manifest: list[dict[str, object]] = []

    with zipfile.ZipFile(archive_path, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
        for project_file in ("README.md", "Licence.txt"):
            source = source_dir / project_file
            if not source.is_file():
                raise ValueError(f"project file is missing: {source}")
            arcname = root / project_file
            _write_file(archive, source, arcname)
            file_manifest.append({
                "path": str(arcname),
                "size": source.stat().st_size,
                "sha256": sha256_file(source),
            })

        version_bytes = (version + "\n").encode("utf-8")
        version_info = zipfile.ZipInfo(str(root / "version.txt"), (1980, 1, 1, 0, 0, 0))
        version_info.compress_type = zipfile.ZIP_DEFLATED
        version_info.external_attr = (stat.S_IFREG | 0o644) << 16
        archive.writestr(version_info, version_bytes)
        file_manifest.append({
            "path": str(root / "version.txt"),
            "size": len(version_bytes),
            "sha256": hashlib.sha256(version_bytes).hexdigest(),
        })

        for core in CORES:
            install_dir = installs[core]
            if platform == "psvita":
                selected = [install_dir / f"{core}.vpk"]
                for source in selected:
                    arcname = root / source.name
                    _write_file(archive, source, arcname)
                    file_manifest.append({
                        "path": str(arcname),
                        "size": source.stat().st_size,
                        "sha256": sha256_file(source),
                    })
                continue

            for source, relative in _iter_tree_files(install_dir):
                arcname = root / core / relative
                _write_file(archive, source, arcname)
                file_manifest.append({
                    "path": str(arcname),
                    "size": source.stat().st_size,
                    "sha256": sha256_file(source),
                })

        package_manifest["files"] = file_manifest
        manifest_bytes = (json.dumps(package_manifest, indent=2, sort_keys=True) + "\n").encode("utf-8")
        manifest_info = zipfile.ZipInfo(str(root / "release-manifest.json"), (1980, 1, 1, 0, 0, 0))
        manifest_info.compress_type = zipfile.ZIP_DEFLATED
        manifest_info.external_attr = (stat.S_IFREG | 0o644) << 16
        archive.writestr(manifest_info, manifest_bytes)

    archive_sha = sha256_file(archive_path)
    sidecar = {
        "schema": 1,
        "name": archive_name,
        "version": version,
        "platform": platform,
        "cores": list(CORES),
        "size": archive_path.stat().st_size,
        "sha256": archive_sha,
    }
    metadata_path.write_text(json.dumps(sidecar, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    return PackageResult(
        archive=archive_path,
        metadata=metadata_path,
        sha256=archive_sha,
        size=archive_path.stat().st_size,
    )


def parse_core_install(value: str) -> tuple[str, Path]:
    try:
        core, raw_path = value.split("=", 1)
    except ValueError as exc:
        raise argparse.ArgumentTypeError("expected CORE=PATH") from exc
    core = core.upper()
    if core not in CORES:
        raise argparse.ArgumentTypeError(f"unknown core {core!r}")
    return core, Path(raw_path)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--source-dir", type=Path, default=Path.cwd())
    parser.add_argument("--platform", required=True)
    parser.add_argument("--version", required=True)
    parser.add_argument("--core-install", action="append", type=parse_core_install, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    args = parser.parse_args()

    installs: dict[str, Path] = {}
    for core, path in args.core_install:
        if core in installs:
            parser.error(f"duplicate install tree for {core}")
        installs[core] = path

    try:
        result = build_package(
            source_dir=args.source_dir,
            platform=args.platform,
            version=args.version,
            installs=installs,
            output_dir=args.output_dir,
        )
    except ValueError as exc:
        parser.error(str(exc))

    print(result.archive)
    print(f"sha256={result.sha256}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
