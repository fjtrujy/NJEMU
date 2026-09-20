#!/usr/bin/env python3

from __future__ import annotations

import struct
import sys
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


if __name__ == "__main__":
    unittest.main()
