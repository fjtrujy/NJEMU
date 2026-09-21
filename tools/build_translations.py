#!/usr/bin/env python3
"""Build-time parser, validator and .lng generator for NJEMU translations."""

from __future__ import annotations

import argparse
import re
import struct
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
TRANSLATIONS_DIR = ROOT / "translations"
GBK_TABLE_SOURCE = ROOT / "src/common/font/gbk_tbl.c"
LANGUAGE_FILES = {
    "en": "en.lang",
    "ja": "ja.lang",
    "es": "es.lang",
    "zh-Hans": "zh-Hans.lang",
    "zh-Hant": "zh-Hant.lang",
}
NULL_MARKER = "<NULL>"

PACK_MAGIC = b"NJTL"
PACK_VERSION = 2
PACK_NULL_OFFSET = 0xFFFF
PACK_MAX_BLOB_SIZE = PACK_NULL_OFFSET - 1
PACK_HEADER = struct.Struct("<4sHHHHII")
LANGUAGE_IDS = {
    "en": 0,
    "ja": 1,
    "es": 2,
    "zh-Hans": 3,
    "zh-Hant": 4,
}

GRAPHIC_TOKENS = {
    "<UPARROW>": 0xE000,
    "<DOWNARROW>": 0xE001,
    "<LEFTARROW>": 0xE002,
    "<RIGHTARROW>": 0xE003,
    "<CIRCLE>": 0xE004,
    "<CROSS>": 0xE005,
    "<SQUARE>": 0xE006,
    "<TRIANGLE>": 0xE007,
    "<LTRIGGER>": 0xE008,
    "<RTRIGGER>": 0xE009,
    "<UPTRIANGLE>": 0xE00B,
    "<DOWNTRIANGLE>": 0xE00C,
    "<LEFTTRIANGLE>": 0xE00D,
    "<RIGHTTRIANGLE>": 0xE00E,
}
PRINTF_RE = re.compile(
    rb"%(?:[-+ #0]*)(?:\*|\d+)?(?:\.(?:\*|\d+))?"
    rb"(?:hh|h|ll|l|j|z|t|L)?[diuoxXfFeEgGaAcspn%]"
)


class TranslationError(RuntimeError):
    pass


def parse_stable_manifest(text: str) -> list[str]:
    entries: list[tuple[int, str]] = []
    for line_no, line in enumerate(text.splitlines(), 1):
        stripped = line.strip()
        if not stripped or stripped.startswith("/*") or stripped.startswith("*"):
            continue
        match = re.fullmatch(r"UI_TEXT_ID\(\s*([A-Za-z_]\w*)\s*,\s*(\d+)\s*\)", stripped)
        if not match:
            raise TranslationError(
                f"messages.def:{line_no}: unsupported manifest line: {stripped!r}"
            )
        entries.append((int(match.group(2)), match.group(1)))

    if not entries:
        raise TranslationError("stable manifest is empty")
    entries.sort()
    ids = [entry_id for entry_id, _name in entries]
    if ids != list(range(len(entries))):
        raise TranslationError("stable manifest IDs are not contiguous from zero")
    names = [name for _entry_id, name in entries]
    if len(names) != len(set(names)):
        raise TranslationError("stable manifest contains duplicate names")
    return names


def decode_source_value(text: str, context: str) -> bytes | None:
    if text == NULL_MARKER:
        return None

    out = bytearray()
    index = 0
    while index < len(text):
        char = text[index]
        if char == "<":
            end = text.find(">", index + 1)
            if end < 0:
                raise TranslationError(f"{context}: unterminated graphic token")
            token = text[index : end + 1]
            try:
                out.extend(chr(GRAPHIC_TOKENS[token]).encode("utf-8"))
            except KeyError as exc:
                raise TranslationError(f"{context}: unknown graphic token {token!r}") from exc
            index = end + 1
            continue

        if char != "\\":
            out.extend(char.encode("utf-8"))
            index += 1
            continue

        if index + 1 >= len(text):
            raise TranslationError(f"{context}: trailing backslash")
        escape = text[index + 1]
        simple = {
            "n": 0x0A,
            "r": 0x0D,
            "t": 0x09,
            "0": 0x00,
            "\\": 0x5C,
            "=": 0x3D,
            "#": 0x23,
            "<": 0x3C,
            ">": 0x3E,
        }
        if escape in simple:
            out.append(simple[escape])
            index += 2
            continue
        if escape == "x":
            digits = text[index + 2 : index + 4]
            if len(digits) != 2 or not re.fullmatch(r"[0-9a-fA-F]{2}", digits):
                raise TranslationError(f"{context}: \\x escape must contain exactly two hex digits")
            byte = int(digits, 16)
            if byte >= 0x80:
                raise TranslationError(
                    f"{context}: high-byte \\x{digits} escape is not valid UTF-8 source; "
                    "use the literal Unicode character"
                )
            out.append(byte)
            index += 4
            continue
        raise TranslationError(f"{context}: unsupported escape \\{escape}")

    return bytes(out)


def parse_language_source(path: Path) -> tuple[list[str], dict[str, bytes | None]]:
    try:
        text = path.read_text(encoding="utf-8")
    except FileNotFoundError as exc:
        raise TranslationError(f"missing translation source: {path}") from exc
    except UnicodeDecodeError as exc:
        raise TranslationError(f"{path}: file must be valid UTF-8") from exc

    order: list[str] = []
    values: dict[str, bytes | None] = {}
    for line_no, line in enumerate(text.splitlines(), 1):
        if not line or line.lstrip().startswith("#"):
            continue
        if "=" not in line:
            raise TranslationError(f"{path}:{line_no}: expected KEY=value")
        key, encoded = line.split("=", 1)
        if key != key.strip() or not re.fullmatch(r"[A-Za-z_]\w*", key):
            raise TranslationError(f"{path}:{line_no}: invalid key {key!r}")
        if key in values:
            raise TranslationError(f"{path}:{line_no}: duplicate key {key}")
        values[key] = decode_source_value(encoded, f"{path}:{line_no} ({key})")
        order.append(key)
    return order, values


def printf_contract(value: bytes | None, context: str) -> tuple[str, ...] | None:
    if value is None:
        return None
    conversions: list[str] = []
    index = 0
    while True:
        percent = value.find(b"%", index)
        if percent < 0:
            break
        match = PRINTF_RE.match(value, percent)
        if not match:
            next_byte = value[percent + 1] if percent + 1 < len(value) else None
            if next_byte is None or not chr(next_byte).isalpha():
                conversions.append("%literal")
                index = percent + 1
                continue
            preview = value[percent : percent + 16]
            raise TranslationError(f"{context}: unsupported printf conversion near {preview!r}")
        conversions.append(match.group(0).decode("ascii"))
        index = match.end()
    return tuple(conversions)


def load_and_validate_sources(
    directory: Path,
) -> tuple[list[str], dict[str, dict[str, bytes | None]]]:
    manifest = directory / "messages.def"
    try:
        manifest_text = manifest.read_text(encoding="utf-8")
    except FileNotFoundError as exc:
        raise TranslationError(f"missing stable manifest: {manifest}") from exc
    names = parse_stable_manifest(manifest_text)
    expected = set(names)
    catalogs: dict[str, dict[str, bytes | None]] = {}

    for language, filename in LANGUAGE_FILES.items():
        order, catalog = parse_language_source(directory / filename)
        actual = set(catalog)
        missing = [name for name in names if name not in actual]
        unknown = sorted(actual - expected)
        if missing:
            raise TranslationError(f"{filename}: missing keys: {', '.join(missing)}")
        if unknown:
            raise TranslationError(f"{filename}: unknown keys: {', '.join(unknown)}")
        if order != names:
            raise TranslationError(f"{filename}: keys must follow translations/messages.def order")
        catalogs[language] = catalog

    english = catalogs["en"]
    for name in names:
        expected_contract = printf_contract(english[name], f"en:{name}")
        for language in LANGUAGE_FILES:
            actual_contract = printf_contract(catalogs[language][name], f"{language}:{name}")
            if actual_contract != expected_contract:
                raise TranslationError(
                    f"{language}:{name}: printf contract {actual_contract} does not match "
                    f"English {expected_contract}"
                )

    required_unicode_glyphs(catalogs)
    return names, catalogs


def schema_hash(names: list[str]) -> int:
    """32-bit FNV-1a over the canonical stable-ID schema."""
    value = 0x811C9DC5
    for message_id, name in enumerate(names):
        canonical = f"{message_id}:{name}\n".encode("ascii")
        for byte in canonical:
            value ^= byte
            value = (value * 0x01000193) & 0xFFFFFFFF
    return value


def load_gbk_unicode_glyph_map(path: Path = GBK_TABLE_SOURCE) -> dict[int, int]:
    try:
        text = path.read_text(encoding="ascii")
    except FileNotFoundError as exc:
        raise TranslationError(f"missing GBK glyph table: {path}") from exc
    try:
        body = text.split("{", 1)[1].rsplit("}", 1)[0]
    except IndexError as exc:
        raise TranslationError(f"{path}: could not locate GBK glyph table initializer") from exc
    values = [int(value, 16) for value in re.findall(r"0x([0-9A-Fa-f]{4})", body)]
    if len(values) != 0x7DC0:
        raise TranslationError(
            f"{path}: expected 0x7dc0 GBK glyph entries, found {len(values)}"
        )

    mapping: dict[int, int] = {}
    for index, glyph in enumerate(values):
        if glyph == 0xFFFF:
            continue
        encoded = 0x8140 + index
        pair = bytes(((encoded >> 8) & 0xFF, encoded & 0xFF))
        try:
            char = pair.decode("gbk")
        except UnicodeDecodeError:
            continue
        if len(char) != 1 or char.encode("gbk") != pair:
            continue
        mapping[ord(char)] = glyph
    return mapping


def required_unicode_glyphs(
    catalogs: dict[str, dict[str, bytes | None]],
) -> list[tuple[int, int]]:
    required: set[int] = set()
    graphic_codepoints = set(GRAPHIC_TOKENS.values())
    for language, catalog in catalogs.items():
        for name, value in catalog.items():
            if value is None:
                continue
            try:
                text = value.decode("utf-8")
            except UnicodeDecodeError as exc:
                raise TranslationError(f"{language}:{name}: value is not valid UTF-8") from exc
            required.update(
                ord(char)
                for char in text
                if ord(char) >= 0x80 and ord(char) not in graphic_codepoints
            )

    if not required:
        return []
    available = load_gbk_unicode_glyph_map()
    missing = sorted(codepoint for codepoint in required if codepoint not in available)
    if missing:
        rendered = ", ".join(f"U+{codepoint:04X}" for codepoint in missing[:12])
        if len(missing) > 12:
            rendered += ", ..."
        raise TranslationError(
            f"translation font is missing {len(missing)} Unicode glyph(s): {rendered}"
        )
    return [(codepoint, available[codepoint]) for codepoint in sorted(required)]


def render_unicode_glyph_source(entries: list[tuple[int, int]]) -> str:
    rows = "\n".join(
        f"\t{{ 0x{codepoint:04x}u, 0x{glyph:04x}u }}," for codepoint, glyph in entries
    )
    return f"""/* Generated by tools/build_translations.py; do not edit. */
#include <stddef.h>
#include <stdint.h>

#include "common/ui_unicode_glyph.h"

static const ui_unicode_glyph_entry_t ui_unicode_glyphs[] = {{
{rows}
}};

int ui_unicode_glyph_lookup(uint32_t codepoint, uint16_t *glyph)
{{
\tsize_t lo = 0;
\tsize_t hi = sizeof(ui_unicode_glyphs) / sizeof(ui_unicode_glyphs[0]);

\twhile (lo < hi) {{
\t\tsize_t mid = lo + (hi - lo) / 2;
\t\tuint32_t candidate = ui_unicode_glyphs[mid].codepoint;
\t\tif (candidate < codepoint)
\t\t\tlo = mid + 1;
\t\telse
\t\t\thi = mid;
\t}}
\tif (lo >= sizeof(ui_unicode_glyphs) / sizeof(ui_unicode_glyphs[0])
\t\t|| ui_unicode_glyphs[lo].codepoint != codepoint)
\t\treturn 0;
\tif (glyph != NULL)
\t\t*glyph = ui_unicode_glyphs[lo].glyph;
\treturn 1;
}}
"""


def write_unicode_glyph_source(
    output: Path,
    catalogs: dict[str, dict[str, bytes | None]],
) -> int:
    entries = required_unicode_glyphs(catalogs)
    data = render_unicode_glyph_source(entries)
    output.parent.mkdir(parents=True, exist_ok=True)
    if not output.exists() or output.read_text(encoding="utf-8") != data:
        output.write_text(data, encoding="utf-8")
    return len(entries)


def build_pack(language: str, names: list[str], catalog: dict[str, bytes | None]) -> bytes:
    try:
        language_id = LANGUAGE_IDS[language]
    except KeyError as exc:
        raise TranslationError(f"unknown language {language!r}") from exc

    offsets: list[int] = []
    blob = bytearray()
    for name in names:
        value = catalog[name]
        if value is None:
            offsets.append(PACK_NULL_OFFSET)
            continue
        if b"\0" in value:
            raise TranslationError(f"{language}:{name}: embedded NUL cannot be represented in .lng V2")
        if len(blob) > PACK_MAX_BLOB_SIZE:
            raise TranslationError(f"{language}: string blob exceeds .lng V2 16-bit offset limit")
        offsets.append(len(blob))
        blob.extend(value)
        blob.append(0)

    if len(blob) > PACK_MAX_BLOB_SIZE:
        raise TranslationError(
            f"{language}: string blob is {len(blob)} bytes; .lng V2 allows at most {PACK_MAX_BLOB_SIZE}"
        )
    if len(names) > 0xFFFF:
        raise TranslationError(".lng V2 allows at most 65535 message IDs")

    header = PACK_HEADER.pack(
        PACK_MAGIC,
        PACK_VERSION,
        language_id,
        len(names),
        0,
        len(blob),
        schema_hash(names),
    )
    offset_table = struct.pack(f"<{len(offsets)}H", *offsets)
    return header + offset_table + bytes(blob)


def parse_pack(
    data: bytes,
    *,
    expected_names: list[str] | None = None,
    expected_language: str | None = None,
) -> tuple[int, list[bytes | None]]:
    if len(data) < PACK_HEADER.size:
        raise TranslationError(".lng file is smaller than the V2 header")

    magic, version, language_id, count, reserved, blob_size, pack_schema = PACK_HEADER.unpack_from(data)
    if magic != PACK_MAGIC:
        raise TranslationError(f"invalid .lng magic {magic!r}")
    if version != PACK_VERSION:
        raise TranslationError(f"unsupported .lng version {version}")
    if language_id not in LANGUAGE_IDS.values():
        raise TranslationError(f"invalid .lng language id {language_id}")
    if reserved != 0:
        raise TranslationError(".lng V2 reserved header field must be zero")
    if blob_size > PACK_MAX_BLOB_SIZE:
        raise TranslationError(f".lng string blob exceeds {PACK_MAX_BLOB_SIZE} bytes")

    if expected_names is not None:
        if count != len(expected_names):
            raise TranslationError(
                f".lng message count {count} does not match schema count {len(expected_names)}"
            )
        expected_schema = schema_hash(expected_names)
        if pack_schema != expected_schema:
            raise TranslationError(
                f".lng schema hash 0x{pack_schema:08x} does not match 0x{expected_schema:08x}"
            )
    if expected_language is not None:
        try:
            expected_language_id = LANGUAGE_IDS[expected_language]
        except KeyError as exc:
            raise TranslationError(f"unknown expected language {expected_language!r}") from exc
        if language_id != expected_language_id:
            raise TranslationError(
                f".lng language id {language_id} does not match {expected_language} ({expected_language_id})"
            )

    offset_table_size = count * 2
    blob_start = PACK_HEADER.size + offset_table_size
    expected_file_size = blob_start + blob_size
    if len(data) != expected_file_size:
        raise TranslationError(
            f".lng size {len(data)} does not match header-derived size {expected_file_size}"
        )

    offsets = struct.unpack_from(f"<{count}H", data, PACK_HEADER.size) if count else ()
    blob = data[blob_start:]
    values: list[bytes | None] = []
    for message_id, offset in enumerate(offsets):
        if offset == PACK_NULL_OFFSET:
            values.append(None)
            continue
        if offset >= blob_size:
            raise TranslationError(
                f".lng message {message_id} offset {offset} is outside {blob_size}-byte string blob"
            )
        terminator = blob.find(b"\0", offset)
        if terminator < 0:
            raise TranslationError(f".lng message {message_id} is not NUL-terminated")
        value = blob[offset:terminator]
        try:
            value.decode("utf-8")
        except UnicodeDecodeError as exc:
            raise TranslationError(f".lng message {message_id} is not valid UTF-8") from exc
        values.append(value)
    return language_id, values


def verify_pack_round_trip(
    language: str,
    names: list[str],
    catalog: dict[str, bytes | None],
    data: bytes,
) -> None:
    _language_id, decoded = parse_pack(
        data, expected_names=names, expected_language=language
    )
    for name, actual in zip(names, decoded):
        expected = catalog[name]
        if actual != expected:
            raise TranslationError(
                f"{language}:{name}: generated .lng round-trip differs: {actual!r} != {expected!r}"
            )


def write_packs(
    output_dir: Path,
    names: list[str],
    catalogs: dict[str, dict[str, bytes | None]],
) -> dict[str, int]:
    output_dir.mkdir(parents=True, exist_ok=True)
    sizes: dict[str, int] = {}
    for language in LANGUAGE_FILES:
        data = build_pack(language, names, catalogs[language])
        verify_pack_round_trip(language, names, catalogs[language], data)
        path = output_dir / f"{language}.lng"
        if not path.exists() or path.read_bytes() != data:
            path.write_bytes(data)
        sizes[language] = len(data)
    return sizes


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--translations-dir",
        type=Path,
        default=TRANSLATIONS_DIR,
        help="directory containing messages.def and the editable .lang files",
    )
    parser.add_argument(
        "--build",
        action="store_true",
        help="emit deterministic .lng V2 runtime packs after validation",
    )
    parser.add_argument(
        "--output-dir",
        type=Path,
        default=ROOT / "build/translations/lang",
        help="output directory for generated .lng files",
    )
    parser.add_argument(
        "--unicode-map-output",
        type=Path,
        help="emit the compact generated Unicode-to-glyph C lookup",
    )
    args = parser.parse_args()

    try:
        names, catalogs = load_and_validate_sources(args.translations_dir)
        pack_sizes = write_packs(args.output_dir, names, catalogs) if args.build else None
        glyph_count = (
            write_unicode_glyph_source(args.unicode_map_output, catalogs)
            if args.unicode_map_output is not None
            else None
        )
    except TranslationError as exc:
        print(f"translation validation failed: {exc}", file=sys.stderr)
        return 1

    total_bytes = {
        language: sum(len(value) for value in catalog.values() if value is not None)
        for language, catalog in catalogs.items()
    }
    sizes = ", ".join(f"{language}={size} B" for language, size in total_bytes.items())
    print(f"validated {len(names)} keys in {len(catalogs)} languages; {sizes}")
    if pack_sizes is not None:
        rendered_pack_sizes = ", ".join(
            f"{language}={size} B" for language, size in pack_sizes.items()
        )
        print(f"generated .lng V2 packs in {args.output_dir}: {rendered_pack_sizes}")
    if glyph_count is not None:
        print(
            f"generated Unicode glyph lookup with {glyph_count} entries "
            f"at {args.unicode_map_output}"
        )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
