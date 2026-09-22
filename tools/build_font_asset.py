#!/usr/bin/env python3
"""Build and validate the external fixed-size GBK UI font asset."""

from __future__ import annotations

import argparse
import re
from pathlib import Path

GLYPH_COUNT = 0x5E80
GLYPH_WIDTH = 14
GLYPH_HEIGHT = 14
GLYPH_BYTES = GLYPH_WIDTH * GLYPH_HEIGHT // 2
GBK_TABLE_COUNT = 0x7DC0


def _array_values(source: str, name: str) -> list[int]:
    match = re.search(
        rf"\b{re.escape(name)}\s*\[[^\]]*\]\s*=\s*\{{(.*?)\}};",
        source,
        re.DOTALL,
    )
    if match is None:
        raise ValueError(f"missing C array {name}")
    body = re.sub(r"/\*.*?\*/", "", match.group(1), flags=re.DOTALL)
    body = re.sub(r"//.*", "", body)
    return [int(token, 0) for token in re.findall(r"0[xX][0-9A-Fa-f]+|\d+", body)]


def build_bitmap(source_path: Path) -> bytes:
    source = source_path.read_text(encoding="utf-8", errors="replace")
    bitmap = _array_values(source, "gbk_s14")
    positions = _array_values(source, "gbk_s14_pos")
    constants = {
        "gbk_s14_width": GLYPH_WIDTH,
        "gbk_s14_height": GLYPH_HEIGHT,
        "gbk_s14f_skipx": 0,
        "gbk_s14p_skipx": 0,
        "gbk_s14_skipy": 0,
        "gbk_s14_pitch": GLYPH_WIDTH,
    }

    expected_bitmap_size = GLYPH_COUNT * GLYPH_BYTES
    if len(bitmap) != expected_bitmap_size:
        raise ValueError(
            f"gbk_s14 has {len(bitmap)} bytes, expected {expected_bitmap_size}"
        )
    if len(positions) != GLYPH_COUNT:
        raise ValueError(
            f"gbk_s14_pos has {len(positions)} entries, expected {GLYPH_COUNT}"
        )
    for index, offset in enumerate(positions):
        expected = index * GLYPH_BYTES
        if offset != expected:
            raise ValueError(
                f"gbk_s14_pos[{index}]={offset}, expected fixed offset {expected}"
            )

    for name, expected in constants.items():
        values = _array_values(source, name)
        if len(values) != GLYPH_COUNT:
            raise ValueError(
                f"{name} has {len(values)} entries, expected {GLYPH_COUNT}"
            )
        if any(value != expected for value in values):
            raise ValueError(f"{name} is not uniformly {expected}")

    return bytes(bitmap)


def validate_gbk_table(table_path: Path) -> None:
    source = table_path.read_text(encoding="utf-8", errors="replace")
    table = _array_values(source, "gbk_table")
    if len(table) != GBK_TABLE_COUNT:
        raise ValueError(
            f"gbk_table has {len(table)} entries, expected {GBK_TABLE_COUNT}"
        )

    for lead in range(0x81, 0xFF):
        for trail in range(0x40, 0xFF):
            if trail in (0x7F, 0xFF):
                continue
            table_index = ((lead << 8) | trail) - 0x8140
            expected = (lead - 0x81) * 0xC0 + (trail - 0x40)
            actual = table[table_index]
            if actual != expected:
                raise ValueError(
                    f"gbk_table[{table_index:#x}]={actual:#x}, "
                    f"expected arithmetic glyph {expected:#x}"
                )


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--table-source", type=Path, required=True)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()

    bitmap = build_bitmap(args.source)
    validate_gbk_table(args.table_source)

    if args.output is not None:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        if not args.output.exists() or args.output.read_bytes() != bitmap:
            args.output.write_bytes(bitmap)

    print(
        f"validated {GLYPH_COUNT} fixed 14x14 glyphs "
        f"({len(bitmap)} bytes) and arithmetic GBK mapping"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
