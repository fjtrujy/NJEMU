#!/usr/bin/env python3
"""Build-time parser and validator for NJEMU translation sources.

T2 keeps the legacy runtime tables in place.  The editable ``translations/*.lang``
files are nevertheless byte-exact: printable ASCII stays readable, legacy
non-ASCII bytes use ``\\xNN`` escapes, and NJEMU's graphic glyph bytes use named
tokens such as ``<CIRCLE>``.  T3 extends this tool with deterministic ``.lng``
pack generation.
"""

from __future__ import annotations

import argparse
import re
import struct
import sys
from pathlib import Path

import capture_translation_contract as legacy


ROOT = Path(__file__).resolve().parents[1]
TRANSLATIONS_DIR = ROOT / "translations"
LANGUAGE_FILES = {
    "en": "en.lang",
    "ja": "ja.lang",
    "es": "es.lang",
    "zh-Hans": "zh-Hans.lang",
    "zh-Hant": "zh-Hant.lang",
}
NULL_MARKER = "<NULL>"

PACK_MAGIC = b"NJTL"
PACK_VERSION = 1
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
    "<UPARROW>": b"\x10",
    "<DOWNARROW>": b"\x11",
    "<LEFTARROW>": b"\x12",
    "<RIGHTARROW>": b"\x13",
    "<CIRCLE>": b"\x14",
    "<CROSS>": b"\x15",
    "<SQUARE>": b"\x16",
    "<TRIANGLE>": b"\x17",
    "<LTRIGGER>": b"\x18",
    "<RTRIGGER>": b"\x19",
    "<UPTRIANGLE>": b"\x1b",
    "<DOWNTRIANGLE>": b"\x1c",
    "<LEFTTRIANGLE>": b"\x1d",
    "<RIGHTTRIANGLE>": b"\x1e",
}
BYTE_TO_GRAPHIC = {value[0]: token for token, value in GRAPHIC_TOKENS.items()}

PRINTF_RE = re.compile(
    rb"%(?:[-+ #0]*)(?:\*|\d+)?(?:\.(?:\*|\d+))?"
    rb"(?:hh|h|ll|l|j|z|t|L)?[diuoxXfFeEgGaAcspn%]"
)


class TranslationError(RuntimeError):
    pass


def runtime_bytes(value: bytes | None) -> bytes | None:
    """Return the bytes visible through the legacy C-string API."""
    if value is None:
        return None
    nul = value.find(b"\0")
    return value if nul < 0 else value[:nul]


def encode_source_value(value: bytes | None) -> str:
    if value is None:
        return NULL_MARKER

    parts: list[str] = []
    last_index = len(value) - 1
    for index, byte in enumerate(value):
        if byte in BYTE_TO_GRAPHIC:
            parts.append(BYTE_TO_GRAPHIC[byte])
        elif byte == 0x0A:
            parts.append(r"\n")
        elif byte == 0x0D:
            parts.append(r"\r")
        elif byte == 0x09:
            parts.append(r"\t")
        elif byte == 0x00:
            parts.append(r"\0")
        elif byte == 0x5C:
            parts.append(r"\\")
        elif byte == 0x3C:
            # Literal '<' is escaped so every unescaped <...> is a named token.
            parts.append(r"\x3c")
        elif byte == 0x20 and index == last_index:
            # Avoid source-file trailing whitespace while preserving the byte.
            parts.append(r"\x20")
        elif 0x20 <= byte <= 0x7E:
            parts.append(chr(byte))
        else:
            parts.append(f"\\x{byte:02x}")
    return "".join(parts)


def decode_source_value(text: str, context: str) -> bytes | None:
    if text == NULL_MARKER:
        return None

    try:
        text.encode("ascii")
    except UnicodeEncodeError as exc:
        raise TranslationError(
            f"{context}: source values must stay ASCII during the byte-preserving phase; "
            "use \\xNN for legacy bytes"
        ) from exc

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
                out.extend(GRAPHIC_TOKENS[token])
            except KeyError as exc:
                raise TranslationError(f"{context}: unknown graphic token {token!r}") from exc
            index = end + 1
            continue

        if char != "\\":
            out.append(ord(char))
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
            out.append(int(digits, 16))
            index += 4
            continue
        raise TranslationError(f"{context}: unsupported escape \\{escape}")

    return bytes(out)


def source_header(language: str) -> list[str]:
    tokens = " ".join(GRAPHIC_TOKENS)
    return [
        f"# NJEMU translation source: {language}",
        "# Byte-exact legacy phase: keep this file ASCII; use \\xNN for non-ASCII bytes.",
        "# Escapes: \\n \\r \\t \\0 \\\\ \\xNN. END_OF_TEXT uses <NULL>.",
        f"# Graphic tokens: {tokens}",
        "",
    ]


def render_language_source(language: str, names: list[str], catalog: dict[str, bytes | None]) -> str:
    lines = source_header(language)
    for name in names:
        lines.append(f"{name}={encode_source_value(catalog[name])}")
    lines.append("")
    return "\n".join(lines)


def parse_language_source(path: Path) -> tuple[list[str], dict[str, bytes | None]]:
    try:
        text = path.read_text(encoding="ascii")
    except FileNotFoundError as exc:
        raise TranslationError(f"missing translation source: {path}") from exc
    except UnicodeDecodeError as exc:
        raise TranslationError(f"{path}: file must remain ASCII during the byte-preserving phase") from exc

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


def extract_legacy_catalogs() -> tuple[list[str], dict[str, dict[str, bytes | None]]]:
    names, values, platform_errors, alignment_errors, _counts = legacy.capture_current()
    structural_errors = platform_errors + alignment_errors
    if structural_errors:
        raise TranslationError("legacy contract is structurally inconsistent: " + "; ".join(structural_errors))
    legacy.validate_current_contract(names, values)

    catalogs: dict[str, dict[str, bytes | None]] = {}
    for language in LANGUAGE_FILES:
        catalogs[language] = {}
        for name in names:
            candidates = list(values[name][language])
            if len(candidates) != 1:
                raise TranslationError(
                    f"legacy {language}:{name} resolves to {len(candidates)} byte sequences"
                )
            catalogs[language][name] = runtime_bytes(candidates[0])
    return names, catalogs


def extract_sources(directory: Path, force: bool) -> None:
    names, catalogs = extract_legacy_catalogs()
    directory.mkdir(parents=True, exist_ok=True)
    for language, filename in LANGUAGE_FILES.items():
        path = directory / filename
        content = render_language_source(language, names, catalogs[language])
        encoded = content.encode("ascii")
        if path.exists() and path.read_bytes() != encoded and not force:
            raise TranslationError(
                f"refusing to overwrite edited source {path}; use --force only for deliberate re-extraction"
            )
        path.write_bytes(encoded)


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
    names = legacy.parse_stable_manifest(manifest_text)
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

    return names, catalogs


def verify_against_legacy(names: list[str], catalogs: dict[str, dict[str, bytes | None]]) -> None:
    legacy_names, legacy_catalogs = extract_legacy_catalogs()
    if names != legacy_names:
        raise TranslationError("source manifest order differs from the embedded legacy contract")
    for language in LANGUAGE_FILES:
        for name in names:
            actual = catalogs[language][name]
            expected = legacy_catalogs[language][name]
            if actual != expected:
                raise TranslationError(
                    f"{language}:{name}: source bytes differ from embedded legacy bytes: "
                    f"{actual!r} != {expected!r}"
                )


def schema_hash(names: list[str]) -> int:
    """32-bit FNV-1a over the canonical stable-ID schema."""
    value = 0x811C9DC5
    for message_id, name in enumerate(names):
        canonical = f"{message_id}:{name}\n".encode("ascii")
        for byte in canonical:
            value ^= byte
            value = (value * 0x01000193) & 0xFFFFFFFF
    return value


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
            raise TranslationError(f"{language}:{name}: embedded NUL cannot be represented in .lng V1")
        if len(blob) > PACK_MAX_BLOB_SIZE:
            raise TranslationError(f"{language}: string blob exceeds .lng V1 16-bit offset limit")
        offsets.append(len(blob))
        blob.extend(value)
        blob.append(0)

    if len(blob) > PACK_MAX_BLOB_SIZE:
        raise TranslationError(
            f"{language}: string blob is {len(blob)} bytes; .lng V1 allows at most {PACK_MAX_BLOB_SIZE}"
        )
    if len(names) > 0xFFFF:
        raise TranslationError(".lng V1 allows at most 65535 message IDs")

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
        raise TranslationError(".lng file is smaller than the V1 header")

    magic, version, language_id, count, reserved, blob_size, pack_schema = PACK_HEADER.unpack_from(data)
    if magic != PACK_MAGIC:
        raise TranslationError(f"invalid .lng magic {magic!r}")
    if version != PACK_VERSION:
        raise TranslationError(f"unsupported .lng version {version}")
    if language_id not in LANGUAGE_IDS.values():
        raise TranslationError(f"invalid .lng language id {language_id}")
    if reserved != 0:
        raise TranslationError(".lng V1 reserved header field must be zero")
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
        values.append(blob[offset:terminator])
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
        "--extract-legacy",
        action="store_true",
        help="create the initial .lang sources from the verified embedded catalogs",
    )
    parser.add_argument(
        "--force",
        action="store_true",
        help="allow --extract-legacy to overwrite edited .lang files",
    )
    parser.add_argument(
        "--no-verify-legacy",
        action="store_true",
        help="skip byte-for-byte comparison with embedded catalogs (needed only after T6)",
    )
    parser.add_argument(
        "--translations-dir",
        type=Path,
        default=TRANSLATIONS_DIR,
        help="directory containing messages.def and the editable .lang files",
    )
    parser.add_argument(
        "--build",
        action="store_true",
        help="emit deterministic .lng V1 runtime packs after validation",
    )
    parser.add_argument(
        "--output-dir",
        type=Path,
        default=ROOT / "build/translations/lang",
        help="output directory for generated .lng files",
    )
    args = parser.parse_args()

    try:
        if args.extract_legacy:
            extract_sources(args.translations_dir, args.force)
        names, catalogs = load_and_validate_sources(args.translations_dir)
        if not args.no_verify_legacy:
            verify_against_legacy(names, catalogs)
        pack_sizes = write_packs(args.output_dir, names, catalogs) if args.build else None
    except (TranslationError, legacy.ContractError) as exc:
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
        print(f"generated .lng V1 packs in {args.output_dir}: {rendered_pack_sizes}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
