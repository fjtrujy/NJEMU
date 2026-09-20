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
            if percent == len(value) - 1:
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
    args = parser.parse_args()

    try:
        if args.extract_legacy:
            extract_sources(args.translations_dir, args.force)
        names, catalogs = load_and_validate_sources(args.translations_dir)
        if not args.no_verify_legacy:
            verify_against_legacy(names, catalogs)
    except (TranslationError, legacy.ContractError) as exc:
        print(f"translation validation failed: {exc}", file=sys.stderr)
        return 1

    total_bytes = {
        language: sum(len(value) for value in catalog.values() if value is not None)
        for language, catalog in catalogs.items()
    }
    sizes = ", ".join(f"{language}={size} B" for language, size in total_bytes.items())
    print(f"validated {len(names)} keys in {len(catalogs)} languages; {sizes}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
