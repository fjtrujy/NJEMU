#!/usr/bin/env python3
"""Parse NJEMU textual rominfo databases into a deterministic semantic model."""

from __future__ import annotations

import re
from dataclasses import dataclass
from pathlib import Path


class RomInfoError(RuntimeError):
    pass


@dataclass(frozen=True)
class RomRecord:
    load_type: int
    name: str
    offset: int
    length: int
    crc: int
    group: int
    skip: int
    is_romx: bool


@dataclass(frozen=True)
class RegionRecord:
    name: str
    size: int
    flags: int
    roms: tuple[RomRecord, ...]


@dataclass(frozen=True)
class GameRecord:
    name: str
    parent: str
    machine: int
    input: int
    init: int
    rotation: int
    regions: tuple[RegionRecord, ...]


_MACRO_RE = re.compile(r"^([A-Z]+)\s*\((.*)\)\s*(?://.*)?$")


def _fail(path: Path, line_number: int, message: str) -> None:
    raise RomInfoError(f"{path}:{line_number}: {message}")


def _parse_u32(path: Path, line_number: int, token: str, field: str) -> int:
    try:
        value = int(token, 0)
    except ValueError as exc:
        raise RomInfoError(
            f"{path}:{line_number}: invalid {field} value {token!r}"
        ) from exc
    if value < 0 or value > 0xFFFFFFFF:
        _fail(path, line_number, f"{field} is outside uint32 range")
    return value


def _split_fields(path: Path, line_number: int, body: str) -> list[str]:
    fields = [field.strip() for field in body.split(",")]
    if any(not field for field in fields):
        _fail(path, line_number, "empty macro field")
    return fields


def parse_text(text: str, path: Path | None = None) -> list[GameRecord]:
    source = path or Path("<rominfo>")
    games: list[GameRecord] = []
    seen_names: set[str] = set()
    current_header: tuple[str, str, int, int, int, int] | None = None
    current_regions: list[RegionRecord] = []
    current_region_header: tuple[str, int, int] | None = None
    current_roms: list[RomRecord] = []

    def finish_region(line_number: int) -> None:
        nonlocal current_region_header, current_roms
        if current_region_header is None:
            return
        name, size, flags = current_region_header
        current_regions.append(RegionRecord(name, size, flags, tuple(current_roms)))
        current_region_header = None
        current_roms = []

    def finish_game(line_number: int) -> None:
        nonlocal current_header, current_regions
        if current_header is None:
            _fail(source, line_number, "END outside FILENAME record")
        finish_region(line_number)
        name, parent, machine, input_type, init, rotation = current_header
        games.append(
            GameRecord(
                name,
                parent,
                machine,
                input_type,
                init,
                rotation,
                tuple(current_regions),
            )
        )
        current_header = None
        current_regions = []

    for line_number, raw_line in enumerate(text.splitlines(), 1):
        stripped = raw_line.strip()
        if not stripped or stripped.startswith("//"):
            continue
        if stripped == "END":
            finish_game(line_number)
            continue

        match = _MACRO_RE.fullmatch(stripped)
        if match is None:
            _fail(source, line_number, f"unrecognized rominfo syntax {stripped!r}")
        macro, body = match.groups()
        fields = _split_fields(source, line_number, body)

        if macro == "FILENAME":
            if current_header is not None:
                _fail(source, line_number, "FILENAME before previous END")
            if len(fields) != 6:
                _fail(source, line_number, "FILENAME requires 6 fields")
            name, parent = fields[:2]
            if name in seen_names:
                _fail(source, line_number, f"duplicate FILENAME record {name}")
            seen_names.add(name)
            selectors = tuple(
                _parse_u32(source, line_number, token, field)
                for token, field in zip(
                    fields[2:], ("machine", "input", "init", "rotation")
                )
            )
            current_header = (name, parent, *selectors)
            continue

        if current_header is None:
            _fail(source, line_number, f"{macro} outside FILENAME record")

        if macro == "REGION":
            if len(fields) != 3:
                _fail(source, line_number, "REGION requires 3 fields")
            finish_region(line_number)
            current_region_header = (
                fields[1],
                _parse_u32(source, line_number, fields[0], "region size"),
                _parse_u32(source, line_number, fields[2], "region flags"),
            )
            continue

        if macro not in ("ROM", "ROMX"):
            _fail(source, line_number, f"unsupported macro {macro}")
        if current_region_header is None:
            _fail(source, line_number, f"{macro} outside REGION record")

        load_type = _parse_u32(source, line_number, fields[0], "ROM load type")
        continuation = load_type == 1
        expected = (6 if continuation else 7) if macro == "ROMX" else (4 if continuation else 5)
        if len(fields) != expected:
            _fail(source, line_number, f"{macro} requires {expected} fields for load type {load_type}")

        cursor = 1
        name = ""
        if not continuation:
            name = fields[cursor]
            cursor += 1
        offset = _parse_u32(source, line_number, fields[cursor], "ROM offset")
        length = _parse_u32(source, line_number, fields[cursor + 1], "ROM length")
        crc = _parse_u32(source, line_number, fields[cursor + 2], "ROM CRC")
        cursor += 3
        group = 0
        skip = 0
        if macro == "ROMX":
            group = _parse_u32(source, line_number, fields[cursor], "ROMX group")
            skip = _parse_u32(source, line_number, fields[cursor + 1], "ROMX skip")

        current_roms.append(
            RomRecord(load_type, name, offset, length, crc, group, skip, macro == "ROMX")
        )

    if current_header is not None:
        _fail(source, len(text.splitlines()) or 1, "unterminated FILENAME record")
    if not games:
        raise RomInfoError(f"{source}: no FILENAME records found")
    return games


def parse(path: Path) -> list[GameRecord]:
    return parse_text(path.read_text(encoding="utf-8", errors="strict"), path)


def validate_parent_graph(games: list[GameRecord], root_parent: str) -> None:
    by_name = {game.name: game for game in games}
    if len(by_name) != len(games):
        raise RomInfoError("duplicate game names in topology")

    for game in games:
        if game.parent != root_parent and game.parent not in by_name:
            raise RomInfoError(f"{game.name}: unresolved parent {game.parent}")

    visiting: set[str] = set()
    visited: set[str] = set()

    def visit(name: str) -> None:
        if name in visited:
            return
        if name in visiting:
            raise RomInfoError(f"parent cycle detected at {name}")
        visiting.add(name)
        parent = by_name[name].parent
        if parent != root_parent:
            visit(parent)
        visiting.remove(name)
        visited.add(name)

    for game in games:
        visit(game.name)
