#!/usr/bin/env python3
"""Validate converter-only CPS2 graphics cache layout metadata."""

from __future__ import annotations

import argparse
import csv
import sys
from pathlib import Path

FIELDS = (
    "name",
    "object_start",
    "object_end",
    "scroll1_start",
    "scroll1_end",
    "scroll2_start",
    "scroll2_end",
    "scroll3_start",
    "scroll3_end",
    "object2_start",
    "object2_end",
)
RANGE_PAIRS = (
    ("object_start", "object_end"),
    ("scroll1_start", "scroll1_end"),
    ("scroll2_start", "scroll2_end"),
    ("scroll3_start", "scroll3_end"),
    ("object2_start", "object2_end"),
)


def fail(message: str) -> None:
    raise ValueError(message)


def read_cps2_names(path: Path) -> set[str]:
    with path.open(newline="", encoding="utf-8") as source:
        reader = csv.DictReader(
            (line for line in source if line.strip() and not line.startswith("#")),
            delimiter="\t",
        )
        if reader.fieldnames is None or "name" not in reader.fieldnames:
            fail(f"{path}: missing name column")
        return {row["name"] for row in reader if row.get("name")}


def validate(layout_path: Path, metadata_path: Path) -> int:
    cps2_names = read_cps2_names(metadata_path)
    seen: set[str] = set()
    count = 0

    with layout_path.open(newline="", encoding="ascii") as source:
        reader = csv.DictReader(
            (line for line in source if line.strip() and not line.startswith("#")),
            delimiter="\t",
        )
        if tuple(reader.fieldnames or ()) != FIELDS:
            fail(f"{layout_path}: expected columns: {', '.join(FIELDS)}")

        for line_number, row in enumerate(reader, 2):
            name = row["name"]
            if not name:
                fail(f"{layout_path}:{line_number}: empty name")
            if len(name) > 15 or not name.isascii():
                fail(f"{layout_path}:{line_number}: invalid name {name!r}")
            if name in seen:
                fail(f"{layout_path}:{line_number}: duplicate name {name}")
            if name not in cps2_names:
                fail(f"{layout_path}:{line_number}: unknown CPS2 game {name}")
            seen.add(name)

            values: dict[str, int] = {}
            for field in FIELDS[1:]:
                text = row[field]
                try:
                    value = int(text, 0)
                except ValueError as exc:
                    raise ValueError(
                        f"{layout_path}:{line_number}: invalid {field} value {text!r}"
                    ) from exc
                if value < 0 or value > 0xFFFFFFFF:
                    fail(f"{layout_path}:{line_number}: {field} exceeds uint32 range")
                values[field] = value

            for start_field, end_field in RANGE_PAIRS:
                start = values[start_field]
                end = values[end_field]
                if start == 0 and end == 0:
                    continue
                if start > end:
                    fail(
                        f"{layout_path}:{line_number}: {start_field} exceeds {end_field}"
                    )
            count += 1

    if count == 0:
        fail(f"{layout_path}: no cache layouts")
    return count


def main() -> int:
    parser = argparse.ArgumentParser()
    root = Path(__file__).resolve().parents[1]
    parser.add_argument(
        "--layouts",
        type=Path,
        default=root / "romcnv" / "data" / "cps2_cache_layouts.tsv",
    )
    parser.add_argument(
        "--metadata",
        type=Path,
        default=root / "metadata" / "cps2.tsv",
    )
    args = parser.parse_args()

    try:
        count = validate(args.layouts, args.metadata)
    except (OSError, ValueError) as exc:
        print(f"cps2_cache_layouts: {exc}", file=sys.stderr)
        return 1

    print(f"validated {count} CPS2 ROMCNV cache layouts")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
