#!/usr/bin/env python3
"""Build NJEMU's bounded-access unified runtime game database."""

from __future__ import annotations

import argparse
import struct
import sys
import zlib
from dataclasses import dataclass
from pathlib import Path

import game_metadata
import rominfo

MAGIC = b"NJGD"
VERSION = 1
CORE_CPS2 = game_metadata.CORE_IDS["cps2"]
ROOT_PARENT = "cps2"

NAME_BYTES = game_metadata.NAME_BYTES
TITLE_BYTES = game_metadata.TITLE_BYTES
ROM_NAME_BYTES = 32
TITLE_COUNT = len(game_metadata.LANGUAGES)
PARENT_NONE = 0xFFFF

REGION_IDS = {
    "CPU1": 1,
    "CPU2": 2,
    "GFX1": 3,
    "SOUND1": 4,
    "USER1": 5,
}
REGION_NAMES = {value: key for key, value in REGION_IDS.items()}
ROM_FLAG_ROMX = 0x01

# V1 is explicitly serialized little-endian. No native C layout is written.
HEADER = struct.Struct("<4s8H11I")
GAME_RECORD = struct.Struct("<IHBBHHHHI4IBBH")
REGION_RECORD = struct.Struct("<HBBIIHH")
ROM_RECORD = struct.Struct("<IIIIBBBB")
CPS2_RECORD = struct.Struct("<4I")


class GameDatabaseError(RuntimeError):
    pass


@dataclass(frozen=True)
class UnifiedGame:
    metadata: game_metadata.SourceRecord
    topology: rominfo.GameRecord


@dataclass(frozen=True)
class DecodedGame:
    name: str
    parent: str
    titles: tuple[str, str, str, str]
    display_flags: int
    core_flags: int
    aux_name: str
    data: tuple[int, int, int]
    machine: int
    input: int
    init: int
    rotation: int
    regions: tuple[rominfo.RegionRecord, ...]


def fail(message: str) -> None:
    raise GameDatabaseError(message)


def _check_unsigned(value: int, maximum: int, field: str, name: str) -> None:
    if value < 0 or value > maximum:
        fail(f"{name}: {field} value {value} exceeds format V1 range 0..{maximum}")


def merge_sources(
    metadata_rows: list[game_metadata.SourceRecord],
    topology_rows: list[rominfo.GameRecord],
) -> list[UnifiedGame]:
    metadata_by_name = {row.name: row for row in metadata_rows}
    topology_by_name = {row.name: row for row in topology_rows}
    metadata_names = set(metadata_by_name)
    topology_names = set(topology_by_name)
    if metadata_names != topology_names:
        missing_metadata = sorted(topology_names - metadata_names)
        missing_topology = sorted(metadata_names - topology_names)
        details: list[str] = []
        if missing_metadata:
            details.append(f"missing metadata: {', '.join(missing_metadata)}")
        if missing_topology:
            details.append(f"missing topology: {', '.join(missing_topology)}")
        fail(f"CPS2 identity divergence: {'; '.join(details)}")

    return [
        UnifiedGame(metadata_by_name[name], topology_by_name[name])
        for name in sorted(metadata_names)
    ]


def validate_v1(games: list[UnifiedGame]) -> None:
    if not games:
        fail("CPS2 database has no games")
    if len(games) >= PARENT_NONE:
        fail("CPS2 game count exceeds uint16 parent/index capacity")

    total_regions = 0
    total_roms = 0
    for index, game in enumerate(games):
        metadata = game.metadata
        topology = game.topology
        if metadata.name != topology.name:
            fail(f"internal identity mismatch at sorted game {index}")
        if len(metadata.name.encode("ascii")) >= NAME_BYTES:
            fail(f"{metadata.name}: canonical name exceeds V1 name capacity")
        for selector_name, selector in (
            ("machine", topology.machine),
            ("input", topology.input),
            ("init", topology.init),
            ("rotation", topology.rotation),
        ):
            _check_unsigned(selector, 0xFFFF, selector_name, metadata.name)
        _check_unsigned(len(topology.regions), 0xFF, "region count", metadata.name)

        total_regions += len(topology.regions)
        for region in topology.regions:
            if region.name not in REGION_IDS:
                fail(f"{metadata.name}: unsupported CPS2 region {region.name}")
            _check_unsigned(region.flags, 0xFFFF, f"{region.name} flags", metadata.name)
            _check_unsigned(len(region.roms), 0xFF, f"{region.name} ROM count", metadata.name)
            total_roms += len(region.roms)
            for record in region.roms:
                _check_unsigned(record.load_type, 0xFF, "ROM load type", metadata.name)
                _check_unsigned(record.group, 0xFF, "ROMX group", metadata.name)
                _check_unsigned(record.skip, 0xFF, "ROMX skip", metadata.name)
                if record.name and len(record.name.encode("ascii")) >= ROM_NAME_BYTES:
                    fail(
                        f"{metadata.name}: ROM filename {record.name!r} exceeds "
                        f"{ROM_NAME_BYTES - 1} characters"
                    )

    if total_regions > 0xFFFFFFFF or total_roms > 0xFFFFFFFF:
        fail("CPS2 topology exceeds uint32 section count capacity")


def load_sources(metadata_path: Path, rominfo_path: Path) -> list[UnifiedGame]:
    metadata_rows = game_metadata.read_source(metadata_path, "cps2")
    game_metadata.validate(metadata_rows, "cps2", rominfo_path)
    topology_rows = rominfo.parse(rominfo_path)
    rominfo.validate_parent_graph(topology_rows, ROOT_PARENT)
    games = merge_sources(metadata_rows, topology_rows)
    validate_v1(games)
    return games


def add_string(pool: bytearray, offsets: dict[str, int], value: str) -> int:
    if not value:
        return 0
    previous = offsets.get(value)
    if previous is not None:
        return previous
    encoded = value.encode("utf-8") + b"\0"
    offset = len(pool)
    if offset > 0xFFFFFFFF or len(encoded) > 0xFFFFFFFF - offset:
        fail("string pool exceeds uint32 format capacity")
    pool.extend(encoded)
    offsets[value] = offset
    return offset


def build_blob(games: list[UnifiedGame]) -> bytes:
    validate_v1(games)
    index_by_name = {game.metadata.name: index for index, game in enumerate(games)}
    pool = bytearray(b"\0")
    string_offsets = {"": 0}
    game_bytes = bytearray()
    region_bytes = bytearray()
    rom_bytes = bytearray()
    core_bytes = bytearray()

    region_index = 0
    rom_index = 0
    for game_index, game in enumerate(games):
        metadata = game.metadata
        topology = game.topology
        parent_index = PARENT_NONE
        if topology.parent != ROOT_PARENT:
            try:
                parent_index = index_by_name[topology.parent]
            except KeyError as exc:
                raise GameDatabaseError(
                    f"{metadata.name}: unresolved parent {topology.parent}"
                ) from exc

        name_offset = add_string(pool, string_offsets, metadata.name)
        title_offsets = [add_string(pool, string_offsets, title) for title in metadata.titles]
        aux_offset = add_string(pool, string_offsets, metadata.aux_name)
        first_region = region_index

        for region in topology.regions:
            first_rom = rom_index
            for record in region.roms:
                flags = ROM_FLAG_ROMX if record.is_romx else 0
                rom_bytes.extend(
                    ROM_RECORD.pack(
                        add_string(pool, string_offsets, record.name),
                        record.offset,
                        record.length,
                        record.crc,
                        record.load_type,
                        record.group,
                        record.skip,
                        flags,
                    )
                )
                rom_index += 1
            region_bytes.extend(
                REGION_RECORD.pack(
                    game_index,
                    REGION_IDS[region.name],
                    len(region.roms),
                    region.size,
                    first_rom,
                    region.flags,
                    0,
                )
            )
            region_index += 1

        core_index = game_index
        core_bytes.extend(
            CPS2_RECORD.pack(
                metadata.data0,
                metadata.data1,
                metadata.data2,
                aux_offset,
            )
        )
        game_bytes.extend(
            GAME_RECORD.pack(
                name_offset,
                parent_index,
                len(topology.regions),
                metadata.display_flags,
                topology.machine,
                topology.input,
                topology.init,
                topology.rotation,
                first_region,
                *title_offsets,
                metadata.core_flags,
                0,
                core_index,
            )
        )

    games_offset = HEADER.size
    regions_offset = games_offset + len(game_bytes)
    roms_offset = regions_offset + len(region_bytes)
    core_offset = roms_offset + len(rom_bytes)
    strings_offset = core_offset + len(core_bytes)
    body = bytes(game_bytes + region_bytes + rom_bytes + core_bytes + pool)
    file_size = HEADER.size + len(body)
    if file_size > 0xFFFFFFFF:
        fail("database exceeds uint32 file size capacity")

    header = HEADER.pack(
        MAGIC,
        VERSION,
        CORE_CPS2,
        HEADER.size,
        GAME_RECORD.size,
        REGION_RECORD.size,
        ROM_RECORD.size,
        CPS2_RECORD.size,
        0,
        len(games),
        region_index,
        rom_index,
        games_offset,
        regions_offset,
        roms_offset,
        core_offset,
        strings_offset,
        len(pool),
        zlib.crc32(body) & 0xFFFFFFFF,
        file_size,
    )
    return header + body


def _read_string(blob: bytes, strings_offset: int, strings_size: int, offset: int) -> str:
    if offset == 0:
        return ""
    if offset >= strings_size:
        fail(f"invalid string offset {offset}")
    start = strings_offset + offset
    end_limit = strings_offset + strings_size
    end = blob.find(b"\0", start, end_limit)
    if end < 0:
        fail(f"unterminated string at offset {offset}")
    try:
        return blob[start:end].decode("utf-8")
    except UnicodeDecodeError as exc:
        raise GameDatabaseError(f"invalid UTF-8 string at offset {offset}") from exc


def decode_blob(blob: bytes) -> list[DecodedGame]:
    if len(blob) < HEADER.size:
        fail("database is truncated before header")
    unpacked = HEADER.unpack_from(blob)
    (
        magic,
        version,
        core,
        header_size,
        game_record_size,
        region_record_size,
        rom_record_size,
        core_record_size,
        reserved,
        game_count,
        region_count,
        rom_count,
        games_offset,
        regions_offset,
        roms_offset,
        core_offset,
        strings_offset,
        strings_size,
        checksum,
        file_size,
    ) = unpacked
    if magic != MAGIC or version != VERSION or core != CORE_CPS2 or reserved != 0:
        fail("invalid database identity/header")
    if (
        header_size != HEADER.size
        or game_record_size != GAME_RECORD.size
        or region_record_size != REGION_RECORD.size
        or rom_record_size != ROM_RECORD.size
        or core_record_size != CPS2_RECORD.size
        or file_size != len(blob)
    ):
        fail("invalid database record sizes or file size")
    expected_regions = games_offset + game_count * GAME_RECORD.size
    expected_roms = regions_offset + region_count * REGION_RECORD.size
    expected_core = roms_offset + rom_count * ROM_RECORD.size
    expected_strings = core_offset + game_count * CPS2_RECORD.size
    if (
        games_offset != HEADER.size
        or regions_offset != expected_regions
        or roms_offset != expected_roms
        or core_offset != expected_core
        or strings_offset != expected_strings
        or strings_size != len(blob) - strings_offset
        or strings_size == 0
        or blob[strings_offset] != 0
    ):
        fail("invalid database section layout")
    if zlib.crc32(blob[HEADER.size :]) & 0xFFFFFFFF != checksum:
        fail("database checksum mismatch")

    raw_games = [
        GAME_RECORD.unpack_from(blob, games_offset + index * GAME_RECORD.size)
        for index in range(game_count)
    ]
    names = [
        _read_string(blob, strings_offset, strings_size, record[0])
        for record in raw_games
    ]
    if names != sorted(names) or len(names) != len(set(names)):
        fail("game index is not strictly sorted")

    result: list[DecodedGame] = []
    for game_index, record in enumerate(raw_games):
        (
            name_offset,
            parent_index,
            game_region_count,
            display_flags,
            machine,
            input_type,
            init,
            rotation,
            first_region,
            title0,
            title1,
            title2,
            title3,
            core_flags,
            game_reserved,
            core_index,
        ) = record
        if game_reserved != 0 or core_index != game_index:
            fail(f"{names[game_index]}: invalid game/core record linkage")
        if first_region > region_count or game_region_count > region_count - first_region:
            fail(f"{names[game_index]}: invalid region range")
        if parent_index == PARENT_NONE:
            parent = ROOT_PARENT
        elif parent_index < game_count:
            parent = names[parent_index]
        else:
            fail(f"{names[game_index]}: invalid parent index")

        data0, data1, data2, aux_offset = CPS2_RECORD.unpack_from(
            blob, core_offset + core_index * CPS2_RECORD.size
        )
        decoded_regions: list[rominfo.RegionRecord] = []
        for relative_region in range(game_region_count):
            region_offset = regions_offset + (first_region + relative_region) * REGION_RECORD.size
            (
                record_game_index,
                region_type,
                region_rom_count,
                region_size,
                first_rom,
                region_flags,
                region_reserved,
            ) = REGION_RECORD.unpack_from(blob, region_offset)
            if record_game_index != game_index or region_reserved != 0:
                fail(f"{names[game_index]}: invalid region linkage")
            if region_type not in REGION_NAMES:
                fail(f"{names[game_index]}: invalid region type {region_type}")
            if first_rom > rom_count or region_rom_count > rom_count - first_rom:
                fail(f"{names[game_index]}: invalid ROM range")
            decoded_roms: list[rominfo.RomRecord] = []
            for relative_rom in range(region_rom_count):
                rom_offset = roms_offset + (first_rom + relative_rom) * ROM_RECORD.size
                (
                    rom_name_offset,
                    load_offset,
                    length,
                    crc,
                    load_type,
                    group,
                    skip,
                    flags,
                ) = ROM_RECORD.unpack_from(blob, rom_offset)
                if flags & ~ROM_FLAG_ROMX:
                    fail(f"{names[game_index]}: invalid ROM flags {flags}")
                decoded_roms.append(
                    rominfo.RomRecord(
                        load_type,
                        _read_string(blob, strings_offset, strings_size, rom_name_offset),
                        load_offset,
                        length,
                        crc,
                        group,
                        skip,
                        bool(flags & ROM_FLAG_ROMX),
                    )
                )
            decoded_regions.append(
                rominfo.RegionRecord(
                    REGION_NAMES[region_type],
                    region_size,
                    region_flags,
                    tuple(decoded_roms),
                )
            )

        result.append(
            DecodedGame(
                _read_string(blob, strings_offset, strings_size, name_offset),
                parent,
                tuple(
                    _read_string(blob, strings_offset, strings_size, title_offset)
                    for title_offset in (title0, title1, title2, title3)
                ),
                display_flags,
                core_flags,
                _read_string(blob, strings_offset, strings_size, aux_offset),
                (data0, data1, data2),
                machine,
                input_type,
                init,
                rotation,
                tuple(decoded_regions),
            )
        )
    return result


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--core", choices=("cps2",), required=True)
    parser.add_argument("--metadata", type=Path, required=True)
    parser.add_argument("--rominfo", type=Path, required=True)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--gamelist-output", type=Path)
    parser.add_argument("--validate-only", action="store_true")
    args = parser.parse_args()

    try:
        games = load_sources(args.metadata, args.rominfo)
        blob = build_blob(games)
        if args.validate_only:
            if args.output is not None or args.gamelist_output is not None:
                fail("--validate-only cannot be combined with output options")
        else:
            if args.output is None:
                fail("--output is required unless --validate-only is used")
            args.output.parent.mkdir(parents=True, exist_ok=True)
            args.output.write_bytes(blob)
            print(
                f"generated {args.output}: core=cps2 games={len(games)} "
                f"regions={sum(len(game.topology.regions) for game in games)} "
                f"roms={sum(len(region.roms) for game in games for region in game.topology.regions)} "
                f"bytes={len(blob)}"
            )
            if args.gamelist_output is not None:
                gamelist = game_metadata.build_gamelist(
                    [game.metadata for game in games], "cps2"
                )
                args.gamelist_output.parent.mkdir(parents=True, exist_ok=True)
                args.gamelist_output.write_text(gamelist, encoding="utf-8")
                print(
                    f"generated {args.gamelist_output}: core=cps2 games={len(games)} "
                    f"bytes={len(gamelist.encode('utf-8'))}"
                )
    except (
        GameDatabaseError,
        game_metadata.MetadataError,
        rominfo.RomInfoError,
        OSError,
        UnicodeError,
        struct.error,
    ) as exc:
        print(f"game_database: {exc}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
