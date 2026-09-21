#!/usr/bin/env python3

from __future__ import annotations

import struct
import sys
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))

import build_translations as translations  # noqa: E402


class TranslationPackTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.names, cls.catalogs = translations.load_and_validate_sources(
            ROOT / "translations"
        )

    def make_pack(self, language: str = "en") -> bytes:
        return translations.build_pack(language, self.names, self.catalogs[language])

    def assert_rejected(self, data: bytes, message: str) -> None:
        with self.assertRaisesRegex(translations.TranslationError, message):
            translations.parse_pack(data, expected_names=self.names, expected_language="en")

    def test_all_languages_round_trip(self) -> None:
        for language, catalog in self.catalogs.items():
            with self.subTest(language=language):
                first = translations.build_pack(language, self.names, catalog)
                second = translations.build_pack(language, self.names, catalog)
                self.assertEqual(first, second)
                translations.verify_pack_round_trip(language, self.names, catalog, first)

    def test_schema_hash_is_stable(self) -> None:
        self.assertEqual(translations.schema_hash(self.names), 0x1ED49DE8)

    def test_rejects_bad_magic(self) -> None:
        data = bytearray(self.make_pack())
        data[0:4] = b"BAD!"
        self.assert_rejected(bytes(data), "magic")

    def test_rejects_bad_version(self) -> None:
        data = bytearray(self.make_pack())
        struct.pack_into("<H", data, 4, translations.PACK_VERSION + 1)
        self.assert_rejected(bytes(data), "version")

    def test_rejects_bad_language(self) -> None:
        data = bytearray(self.make_pack())
        struct.pack_into("<H", data, 6, 99)
        self.assert_rejected(bytes(data), "language")

    def test_rejects_bad_message_count(self) -> None:
        data = bytearray(self.make_pack())
        struct.pack_into("<H", data, 8, len(self.names) - 1)
        self.assert_rejected(bytes(data), "message count")

    def test_rejects_nonzero_reserved_field(self) -> None:
        data = bytearray(self.make_pack())
        struct.pack_into("<H", data, 10, 1)
        self.assert_rejected(bytes(data), "reserved")

    def test_rejects_bad_blob_size(self) -> None:
        data = bytearray(self.make_pack())
        blob_size = struct.unpack_from("<I", data, 12)[0]
        struct.pack_into("<I", data, 12, blob_size + 1)
        self.assert_rejected(bytes(data), "size")

    def test_rejects_bad_schema(self) -> None:
        data = bytearray(self.make_pack())
        schema = struct.unpack_from("<I", data, 16)[0]
        struct.pack_into("<I", data, 16, schema ^ 1)
        self.assert_rejected(bytes(data), "schema hash")

    def test_rejects_out_of_range_offset(self) -> None:
        data = bytearray(self.make_pack())
        blob_size = struct.unpack_from("<I", data, 12)[0]
        struct.pack_into("<H", data, translations.PACK_HEADER.size, blob_size)
        self.assert_rejected(bytes(data), "outside")

    def test_rejects_truncated_file(self) -> None:
        self.assert_rejected(self.make_pack()[:-1], "size")

    def test_rejects_missing_string_terminator(self) -> None:
        data = bytearray(self.make_pack())
        self.assertEqual(data[-1], 0)
        data[-1] = ord("X")
        self.assert_rejected(bytes(data), "NUL-terminated")

    def test_rejects_invalid_utf8_payload(self) -> None:
        data = bytearray(self.make_pack())
        count = len(self.names)
        blob_start = translations.PACK_HEADER.size + count * 2
        data[blob_start] = 0xFF
        self.assert_rejected(bytes(data), "valid UTF-8")

    def test_rejects_v2_blob_overflow(self) -> None:
        names = ["ONLY"]
        catalog = {"ONLY": b"x" * translations.PACK_MAX_BLOB_SIZE}
        with self.assertRaisesRegex(translations.TranslationError, "allows at most"):
            translations.build_pack("en", names, catalog)


class TranslationSourceValidationTests(unittest.TestCase):
    def make_sources(
        self,
        root: Path,
        manifest_names: list[str],
        values: dict[str, dict[str, str]] | None = None,
    ) -> None:
        manifest = "".join(
            f"UI_TEXT_ID({name}, {index})\n" for index, name in enumerate(manifest_names)
        )
        (root / "messages.def").write_text(manifest, encoding="ascii")

        values = values or {}
        for language, filename in translations.LANGUAGE_FILES.items():
            language_values = values.get(language, {})
            lines = [
                f"{name}={language_values.get(name, name)}" for name in manifest_names
            ]
            (root / filename).write_text("\n".join(lines) + "\n", encoding="utf-8")

    def test_complete_catalog_succeeds(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            self.make_sources(root, ["A", "B"])
            names, catalogs = translations.load_and_validate_sources(root)
            self.assertEqual(names, ["A", "B"])
            self.assertEqual(set(catalogs), set(translations.LANGUAGE_FILES))

    def test_missing_key_fails(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            self.make_sources(root, ["A", "B"])
            en = root / translations.LANGUAGE_FILES["en"]
            en.write_text("A=A\n", encoding="ascii")
            with self.assertRaisesRegex(translations.TranslationError, "missing keys: B"):
                translations.load_and_validate_sources(root)

    def test_extra_key_fails(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            self.make_sources(root, ["A"])
            en = root / translations.LANGUAGE_FILES["en"]
            en.write_text("A=A\nEXTRA=value\n", encoding="ascii")
            with self.assertRaisesRegex(translations.TranslationError, "unknown keys: EXTRA"):
                translations.load_and_validate_sources(root)

    def test_duplicate_key_fails(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            self.make_sources(root, ["A"])
            en = root / translations.LANGUAGE_FILES["en"]
            en.write_text("A=A\nA=duplicate\n", encoding="ascii")
            with self.assertRaisesRegex(translations.TranslationError, "duplicate key A"):
                translations.load_and_validate_sources(root)

    def test_bad_escape_fails(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            self.make_sources(root, ["A"])
            en = root / translations.LANGUAGE_FILES["en"]
            en.write_text(r"A=bad\q" + "\n", encoding="ascii")
            with self.assertRaisesRegex(translations.TranslationError, "unsupported escape"):
                translations.load_and_validate_sources(root)

    def test_utf8_source_is_preserved_in_runtime_pack(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            values = {"ja": {"A": "しばらくお待ちください。"}}
            self.make_sources(root, ["A"], values)
            _names, catalogs = translations.load_and_validate_sources(root)
            self.assertEqual(
                catalogs["ja"]["A"],
                "しばらくお待ちください。".encode("utf-8"),
            )

    def test_hex_byte_escape_is_rejected(self) -> None:
        with self.assertRaisesRegex(translations.TranslationError, "unsupported escape"):
            translations.decode_source_value(r"NEO\xc2\xb7GEO", "test")

    def test_embedded_nul_escape_is_rejected(self) -> None:
        with self.assertRaisesRegex(translations.TranslationError, "unsupported escape"):
            translations.decode_source_value(r"bad\0value", "test")

    def test_character_missing_from_font_fails(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            values = {"ja": {"A": "emoji 😀"}}
            self.make_sources(root, ["A"], values)
            with self.assertRaisesRegex(translations.TranslationError, "font is missing"):
                translations.load_and_validate_sources(root)

    def test_graphic_token_uses_private_use_codepoint(self) -> None:
        encoded = translations.decode_source_value("<CIRCLE> OK", "test")
        self.assertEqual(encoded, "\ue004 OK".encode("utf-8"))

    def test_unicode_glyph_map_covers_catalogs(self) -> None:
        _names, catalogs = translations.load_and_validate_sources(ROOT / "translations")
        entries = translations.required_unicode_glyphs(catalogs)
        codepoints = {codepoint for codepoint, _glyph in entries}
        self.assertIn(ord("し"), codepoints)
        self.assertIn(ord("请"), codepoints)
        self.assertNotIn(ord("·"), codepoints)

    def test_latin1_translation_characters_use_builtin_font(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            values = {language: {"A": "Español · acción"} for language in translations.LANGUAGE_FILES}
            self.make_sources(root, ["A"], values)
            _names, catalogs = translations.load_and_validate_sources(root)
            self.assertEqual(catalogs["es"]["A"], "Español · acción".encode("utf-8"))

    def test_invalid_utf8_source_fails(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            self.make_sources(root, ["A"])
            ja = root / translations.LANGUAGE_FILES["ja"]
            ja.write_bytes(b"A=bad\xff\n")
            with self.assertRaisesRegex(translations.TranslationError, "valid UTF-8"):
                translations.load_and_validate_sources(root)

    def test_printf_contract_mismatch_fails(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            values = {
                language: {"A": "value=%s"} for language in translations.LANGUAGE_FILES
            }
            values["es"]["A"] = "value=%d"
            self.make_sources(root, ["A"], values)
            with self.assertRaisesRegex(translations.TranslationError, "does not match English"):
                translations.load_and_validate_sources(root)


if __name__ == "__main__":
    unittest.main()
