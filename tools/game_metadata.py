#!/usr/bin/env python3
"""Validate NJEMU's canonical game metadata and build compact runtime files."""

from __future__ import annotations

import argparse
import csv
import re
import struct
import sys
import zlib
from dataclasses import dataclass
from pathlib import Path

MAGIC = b"NJGM"
VERSION = 1
NAME_BYTES = 16
LANGUAGES = ("title_en", "title_ja", "title_zh_hans", "title_zh_hant")
HEADER = struct.Struct("<4sHHIIIIII")
RECORD = struct.Struct("<16s8IBBH")

CORE_IDS = {"cps1": 1, "cps2": 2, "mvs": 3, "ncdz": 4}
DISPLAY_FLAGS = {"not_work": 0x01, "bootleg": 0x02, "hack": 0x04}
CORE_FLAGS = {
    "cps1": {},
    "cps2": {
        "phoenix": 0x01,
        "cache_parent_override": 0x02,
        "cache_independent": 0x04,
    },
    "mvs": {
        "owns_crom": 0x01,
        "owns_srom": 0x02,
        "owns_vrom": 0x04,
    },
    "ncdz": {},
}
FIELDS = (
    "name",
    *LANGUAGES,
    "display_flags",
    "core_flags",
    "aux_name",
    "data0",
    "data1",
    "data2",
)
NAME_RE = re.compile(r"^[a-z0-9_]+$")
ROMINFO_RE = re.compile(r"^FILENAME\(\s*([^,\s]+)\s*,\s*([^,\s]+)", re.MULTILINE)


class MetadataError(RuntimeError):
    pass


@dataclass(frozen=True)
class SourceRecord:
    name: str
    titles: tuple[str, str, str, str]
    display_flags: int
    core_flags: int
    aux_name: str
    data0: int
    data1: int
    data2: int


def fail(message: str) -> None:
    raise MetadataError(message)


def parse_flags(raw: str, known: dict[str, int], field: str, name: str) -> int:
    value = 0
    if not raw:
        return value
    for token in raw.split("|"):
        token = token.strip()
        if token not in known:
            fail(f"{name}: unknown {field} token {token!r}")
        value |= known[token]
    return value


def parse_u32(raw: str, field: str, name: str) -> int:
    if not raw:
        return 0
    try:
        value = int(raw, 0)
    except ValueError as exc:
        raise MetadataError(f"{name}: invalid {field} value {raw!r}") from exc
    if value < 0 or value > 0xFFFFFFFF:
        fail(f"{name}: {field} is outside uint32 range")
    return value


def read_source(path: Path, core: str) -> list[SourceRecord]:
    with path.open("r", encoding="utf-8", newline="") as handle:
        reader = csv.DictReader(
            (line for line in handle if not line.startswith("#")), delimiter="\t"
        )
        if tuple(reader.fieldnames or ()) != FIELDS:
            fail(
                f"{path}: expected TSV columns {FIELDS!r}, got "
                f"{tuple(reader.fieldnames or ())!r}"
            )
        rows: list[SourceRecord] = []
        seen: set[str] = set()
        for line_number, row in enumerate(reader, 2):
            name = (row["name"] or "").strip()
            if not name:
                fail(f"{path}:{line_number}: empty game name")
            if name in seen:
                fail(f"{path}:{line_number}: duplicate game name {name}")
            seen.add(name)
            if not NAME_RE.fullmatch(name) or name != name.lower():
                fail(f"{path}:{line_number}: invalid canonical game name {name!r}")
            if len(name.encode("ascii")) >= NAME_BYTES:
                fail(f"{path}:{line_number}: game name {name!r} does not fit {NAME_BYTES} bytes")

            titles = tuple((row[field] or "").strip() for field in LANGUAGES)
            if core != "ncdz" and not titles[0]:
                fail(f"{path}:{line_number}: {name} has no English display title")
            for title in titles:
                if "\t" in title or "\n" in title or "\r" in title:
                    fail(f"{path}:{line_number}: title contains a control separator")

            aux_name = (row["aux_name"] or "").strip()
            if aux_name:
                if not NAME_RE.fullmatch(aux_name) or len(aux_name.encode("ascii")) >= NAME_BYTES:
                    fail(f"{path}:{line_number}: invalid auxiliary game name {aux_name!r}")

            rows.append(
                SourceRecord(
                    name=name,
                    titles=titles,  # type: ignore[arg-type]
                    display_flags=parse_flags(
                        (row["display_flags"] or "").strip(), DISPLAY_FLAGS, "display_flags", name
                    ),
                    core_flags=parse_flags(
                        (row["core_flags"] or "").strip(), CORE_FLAGS[core], "core_flags", name
                    ),
                    aux_name=aux_name,
                    data0=parse_u32((row["data0"] or "").strip(), "data0", name),
                    data1=parse_u32((row["data1"] or "").strip(), "data1", name),
                    data2=parse_u32((row["data2"] or "").strip(), "data2", name),
                )
            )
    return rows


def read_rominfo(path: Path) -> dict[str, str]:
    text = path.read_text(encoding="utf-8", errors="strict")
    pairs = ROMINFO_RE.findall(text)
    if not pairs:
        fail(f"{path}: no FILENAME records found")
    result: dict[str, str] = {}
    for name, parent in pairs:
        if name in result:
            fail(f"{path}: duplicate FILENAME record {name}")
        result[name] = parent
    return result


def validate(rows: list[SourceRecord], core: str, rominfo: Path | None) -> None:
    by_name = {row.name: row for row in rows}

    if core != "ncdz":
        if rominfo is None:
            fail(f"{core}: --rominfo is required")
        topology = read_rominfo(rominfo)
        source_names = set(by_name)
        topology_names = set(topology)
        missing = sorted(topology_names - source_names)
        extra = sorted(source_names - topology_names)
        if missing or extra:
            details = []
            if missing:
                details.append(f"missing from metadata: {', '.join(missing)}")
            if extra:
                details.append(f"not present in rominfo: {', '.join(extra)}")
            fail(f"{core}: identity divergence: {'; '.join(details)}")

    if core == "cps1":
        for row in rows:
            if row.core_flags or row.aux_name or row.data0 or row.data1 or row.data2:
                fail(f"{row.name}: CPS1 source has unexpected core-specific metadata")

    elif core == "cps2":
        phoenix = CORE_FLAGS[core]["phoenix"]
        parent_override = CORE_FLAGS[core]["cache_parent_override"]
        independent = CORE_FLAGS[core]["cache_independent"]
        for row in rows:
            has_key = row.data0 != 0 or row.data1 != 0 or row.data2 != 0
            is_phoenix = bool(row.core_flags & phoenix)
            if has_key == is_phoenix:
                fail(f"{row.name}: CPS2 record must have exactly one of decryption key or phoenix flag")
            if row.core_flags & parent_override:
                if not row.aux_name or row.aux_name not in by_name:
                    fail(f"{row.name}: CPS2 cache parent override is missing or invalid")
            elif row.aux_name:
                fail(f"{row.name}: CPS2 aux_name requires cache_parent_override")
            if row.core_flags & independent and row.core_flags & parent_override:
                fail(f"{row.name}: CPS2 cache metadata cannot be both independent and overridden")

    elif core == "mvs":
        for row in rows:
            if row.aux_name or row.data0 or row.data1 or row.data2:
                fail(f"{row.name}: MVS source has unexpected numeric/auxiliary metadata")

    elif core == "ncdz":
        seen_ngh: dict[int, str] = {}
        for row in rows:
            if row.display_flags or row.core_flags or row.aux_name or row.data1 or row.data2:
                fail(f"{row.name}: NCDZ source has unexpected metadata fields")
            if row.data0 == 0 or row.data0 > 0xFFFF:
                fail(f"{row.name}: NCDZ NGH must be a non-zero uint16")
            previous = seen_ngh.get(row.data0)
            if previous is not None:
                fail(f"{row.name}: NCDZ NGH 0x{row.data0:04x} duplicates {previous}")
            seen_ngh[row.data0] = row.name


def add_string(pool: bytearray, offsets: dict[str, int], value: str) -> int:
    if not value:
        return 0
    if value in offsets:
        return offsets[value]
    encoded = value.encode("utf-8") + b"\0"
    offset = len(pool)
    pool.extend(encoded)
    offsets[value] = offset
    return offset


def build_blob(rows: list[SourceRecord], core: str) -> bytes:
    pool = bytearray(b"\0")
    offsets = {"": 0}
    records = bytearray()

    for row in sorted(rows, key=lambda item: item.name):
        title_offsets = [add_string(pool, offsets, title) for title in row.titles]
        aux_offset = add_string(pool, offsets, row.aux_name)
        name = row.name.encode("ascii") + b"\0"
        name = name.ljust(NAME_BYTES, b"\0")
        records.extend(
            RECORD.pack(
                name,
                *title_offsets,
                aux_offset,
                row.data0,
                row.data1,
                row.data2,
                row.display_flags,
                row.core_flags,
                0,
            )
        )

    records_offset = HEADER.size
    strings_offset = records_offset + len(records)
    body = bytes(records + pool)
    header = HEADER.pack(
        MAGIC,
        VERSION,
        CORE_IDS[core],
        len(rows),
        RECORD.size,
        records_offset,
        strings_offset,
        len(pool),
        zlib.crc32(body) & 0xFFFFFFFF,
    )
    return header + body


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--core", choices=tuple(CORE_IDS), required=True)
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--rominfo", type=Path)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--validate-only", action="store_true")
    args = parser.parse_args()

    try:
        rows = read_source(args.source, args.core)
        validate(rows, args.core, args.rominfo)
        blob = build_blob(rows, args.core)
        if args.validate_only:
            if args.output is not None:
                fail("--validate-only cannot be combined with --output")
        else:
            if args.output is None:
                fail("--output is required unless --validate-only is used")
            args.output.parent.mkdir(parents=True, exist_ok=True)
            args.output.write_bytes(blob)
            print(
                f"generated {args.output}: core={args.core} records={len(rows)} "
                f"record_size={RECORD.size} bytes={len(blob)}"
            )
    except (MetadataError, OSError, UnicodeError) as exc:
        print(f"game_metadata: {exc}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
