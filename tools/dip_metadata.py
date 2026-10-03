#!/usr/bin/env python3
"""Validate localized DIP menu metadata and build its compact runtime pack."""

from __future__ import annotations

import argparse
import json
import struct
import sys
import zlib
from dataclasses import dataclass
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

MAGIC = b"NJDP"
VERSION = 1
LANGUAGES = ("en", "ja", "zh-Hans", "zh-Hant")
LANGUAGE_IDS = {language: index for index, language in enumerate(LANGUAGES)}
MAX_PROFILE_NAME = 15
MAX_DIP_ROWS = 32
MAX_VALUE_LABELS = 33

# magic, version, language count, profile count, directory count,
# directory offset, row offset, choice offset, string offset, string size, crc32
HEADER = struct.Struct("<4sHH8I")
# profile[16], language, reserved, row count, first row, reserved
DIRECTORY = struct.Struct("<16sBBHII")
# label string offset, enable, mask, value max, choice count, first choice, reserved
ROW = struct.Struct("<IBBBBII")
CHOICE = struct.Struct("<I")


class DipMetadataError(RuntimeError):
    pass


@dataclass(frozen=True)
class DipRow:
    label: str
    enable: int
    mask: int
    value_max: int
    values: tuple[str, ...]


@dataclass(frozen=True)
class DipProfile:
    name: str
    localized: dict[str, tuple[DipRow, ...]]


def _row_from_json(profile: str, language: str, index: int, value: object) -> DipRow:
    context = f"{profile}/{language}/row {index}"
    if not isinstance(value, dict):
        raise DipMetadataError(f"{context}: row must be an object")
    expected = {"label", "enable", "mask", "value_max", "values"}
    if set(value) != expected:
        raise DipMetadataError(f"{context}: fields must be {sorted(expected)}")
    label = value["label"]
    values = value["values"]
    if not isinstance(label, str):
        raise DipMetadataError(f"{context}: label must be a string")
    if not isinstance(values, list) or not all(isinstance(item, str) for item in values):
        raise DipMetadataError(f"{context}: values must be a string list")
    try:
        enable = int(value["enable"])
        mask = int(value["mask"])
        value_max = int(value["value_max"])
    except (TypeError, ValueError) as exc:
        raise DipMetadataError(f"{context}: numeric fields must be integers") from exc
    if enable not in (0, 1):
        raise DipMetadataError(f"{context}: enable must be 0 or 1")
    if not 0 <= mask <= 0xFF:
        raise DipMetadataError(f"{context}: mask is out of range")
    if not 0 <= value_max <= 0xFF:
        raise DipMetadataError(f"{context}: value_max is out of range")
    if len(values) > MAX_VALUE_LABELS:
        raise DipMetadataError(f"{context}: too many value labels")
    if values and value_max != len(values) - 1:
        raise DipMetadataError(
            f"{context}: value_max {value_max} does not match {len(values)} labels"
        )
    if not values and value_max != 0:
        raise DipMetadataError(f"{context}: no labels for non-zero value_max")
    return DipRow(label, enable, mask, value_max, tuple(values))


def load_source(path: Path) -> list[DipProfile]:
    try:
        document = json.loads(path.read_text(encoding="utf-8"))
    except FileNotFoundError as exc:
        raise DipMetadataError(f"missing DIP metadata source: {path}") from exc
    except UnicodeDecodeError as exc:
        raise DipMetadataError(f"{path}: source must be UTF-8") from exc
    except json.JSONDecodeError as exc:
        raise DipMetadataError(f"{path}:{exc.lineno}: invalid JSON: {exc.msg}") from exc

    if not isinstance(document, dict) or set(document) != {"version", "languages", "profiles"}:
        raise DipMetadataError("top-level fields must be version, languages and profiles")
    if document["version"] != VERSION:
        raise DipMetadataError(f"source version must be {VERSION}")
    if tuple(document["languages"]) != LANGUAGES:
        raise DipMetadataError(f"languages must be {list(LANGUAGES)}")
    raw_profiles = document["profiles"]
    if not isinstance(raw_profiles, dict) or not raw_profiles:
        raise DipMetadataError("profiles must be a non-empty object")

    profiles: list[DipProfile] = []
    for name in sorted(raw_profiles):
        if not isinstance(name, str) or not name or len(name.encode("ascii", errors="ignore")) != len(name):
            raise DipMetadataError(f"invalid profile name {name!r}")
        if len(name.encode("ascii")) > MAX_PROFILE_NAME:
            raise DipMetadataError(f"profile name {name!r} is too long")
        raw_localized = raw_profiles[name]
        if not isinstance(raw_localized, dict) or set(raw_localized) != set(LANGUAGES):
            raise DipMetadataError(f"{name}: must define exactly {list(LANGUAGES)}")

        localized: dict[str, tuple[DipRow, ...]] = {}
        for language in LANGUAGES:
            raw_rows = raw_localized[language]
            if not isinstance(raw_rows, list) or not raw_rows:
                raise DipMetadataError(f"{name}/{language}: rows must be non-empty")
            if len(raw_rows) > MAX_DIP_ROWS:
                raise DipMetadataError(f"{name}/{language}: too many DIP rows")
            rows = tuple(
                _row_from_json(name, language, index, row)
                for index, row in enumerate(raw_rows)
            )
            if rows[-1].label != "\0":
                raise DipMetadataError(f"{name}/{language}: final row must be MENU_END")
            localized[language] = rows

        english = localized["en"]
        for language in LANGUAGES[1:]:
            rows = localized[language]
            if len(rows) != len(english):
                raise DipMetadataError(f"{name}/{language}: row count differs from English")
            for index, (base, row) in enumerate(zip(english, rows)):
                if (row.enable, row.mask, row.value_max, len(row.values)) != (
                    base.enable,
                    base.mask,
                    base.value_max,
                    len(base.values),
                ):
                    raise DipMetadataError(
                        f"{name}/{language}/row {index}: structural fields differ from English"
                    )
        profiles.append(DipProfile(name, localized))
    return profiles


def build_blob(profiles: list[DipProfile]) -> bytes:
    strings = bytearray(b"\0")
    string_offsets: dict[str, int] = {"": 0, "\0": 0}

    def add_string(text: str) -> int:
        if text in string_offsets:
            return string_offsets[text]
        encoded = text.encode("utf-8")
        if b"\0" in encoded:
            raise DipMetadataError("embedded NUL is only allowed for MENU_END")
        offset = len(strings)
        strings.extend(encoded)
        strings.append(0)
        string_offsets[text] = offset
        return offset

    directories: list[tuple[bytes, int, int, int]] = []
    rows: list[tuple[int, int, int, int, int, int]] = []
    choices: list[int] = []

    for profile in profiles:
        encoded_name = profile.name.encode("ascii")
        name_field = encoded_name + bytes(16 - len(encoded_name))
        for language in LANGUAGES:
            localized_rows = profile.localized[language]
            first_row = len(rows)
            for row in localized_rows:
                first_choice = len(choices)
                choices.extend(add_string(value) for value in row.values)
                rows.append(
                    (
                        add_string(row.label),
                        row.enable,
                        row.mask,
                        row.value_max,
                        len(row.values),
                        first_choice,
                    )
                )
            directories.append(
                (name_field, LANGUAGE_IDS[language], len(localized_rows), first_row)
            )

    directory_offset = HEADER.size
    rows_offset = directory_offset + len(directories) * DIRECTORY.size
    choices_offset = rows_offset + len(rows) * ROW.size
    strings_offset = choices_offset + len(choices) * CHOICE.size

    payload = bytearray()
    for name_field, language_id, row_count, first_row in directories:
        payload.extend(DIRECTORY.pack(name_field, language_id, 0, row_count, first_row, 0))
    for label_offset, enable, mask, value_max, choice_count, first_choice in rows:
        payload.extend(
            ROW.pack(
                label_offset,
                enable,
                mask,
                value_max,
                choice_count,
                first_choice,
                0,
            )
        )
    for choice_offset in choices:
        payload.extend(CHOICE.pack(choice_offset))
    payload.extend(strings)

    crc = zlib.crc32(payload) & 0xFFFFFFFF
    header = HEADER.pack(
        MAGIC,
        VERSION,
        len(LANGUAGES),
        len(profiles),
        len(directories),
        directory_offset,
        rows_offset,
        choices_offset,
        strings_offset,
        len(strings),
        crc,
    )
    return header + payload


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--validate-only", action="store_true")
    args = parser.parse_args()

    try:
        profiles = load_source(args.source)
        blob = build_blob(profiles)
        if not args.validate_only:
            if args.output is None:
                raise DipMetadataError("--output is required unless --validate-only is used")
            args.output.parent.mkdir(parents=True, exist_ok=True)
            if not args.output.exists() or args.output.read_bytes() != blob:
                args.output.write_bytes(blob)
    except DipMetadataError as exc:
        print(f"DIP metadata validation failed: {exc}", file=sys.stderr)
        return 1

    rows = sum(len(profile.localized[language]) for profile in profiles for language in LANGUAGES)
    print(
        f"validated DIP metadata: profiles={len(profiles)} localized_rows={rows} "
        f"runtime_bytes={len(blob)}"
    )
    if args.output is not None and not args.validate_only:
        print(f"generated {args.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
