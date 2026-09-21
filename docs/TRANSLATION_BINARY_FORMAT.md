# NJEMU translation pack format (`.lng` V2)

The runtime translation pack is deliberately small and dependency-free. All
multi-byte integers are **little-endian** and the file has no implicit padding.

V2 keeps the compact V1 container layout but changes the string payload contract
to validated UTF-8.

## Header

The V2 header is 20 bytes:

| Offset | Size | Field | V2 value / meaning |
| ---: | ---: | --- | --- |
| 0 | 4 | `magic` | ASCII `NJTL` |
| 4 | 2 | `version` | `2` |
| 6 | 2 | `language_id` | `0=en`, `1=ja`, `2=es`, `3=zh-Hans`, `4=zh-Hant` |
| 8 | 2 | `message_count` | Number of stable IDs; currently 377 |
| 10 | 2 | `reserved` | Must be zero |
| 12 | 4 | `string_blob_size` | Bytes in the final UTF-8 string blob |
| 16 | 4 | `schema_hash` | FNV-1a 32-bit hash of the stable ID schema |

The schema hash is computed over the ASCII concatenation:

```text
0:EOM\n
1:LF\n
2:PLEASE_WAIT\n
...
```

using standard FNV-1a 32-bit parameters (offset basis `0x811c9dc5`, prime
`0x01000193`). For the current `messages.def`, the hash is `0x1ed49de8`.

## Offset table

Immediately after the header are `message_count` little-endian `uint16_t`
offsets into the string blob. The table is indexed directly by `ui_text_id_t`.

`0xffff` is reserved as the NULL sentinel and is currently used by
`END_OF_TEXT`. Any other offset must be strictly less than
`string_blob_size`.

Because `0xffff` is reserved, V2 limits the string blob to **65534 bytes**.

## UTF-8 string blob

The offset table is followed immediately by `string_blob_size` bytes. Every
non-NULL entry points at a NUL-terminated **valid UTF-8** string. The runtime
loader validates UTF-8 before accepting the catalog.

Editable Unicode text is copied directly from the UTF-8 `.lang` source.
Named controller/graphic tokens are compiled into Unicode Private Use Area code
points. The current assignments are derived from their historical graphic-byte
slots: for example `<CIRCLE>` is `U+E004` and `<CROSS>` is `U+E005`.

Embedded NUL bytes inside a message are not representable and are rejected by
the generator. Empty strings are represented normally by a single NUL byte in
the blob.

## Font mapping

The pack contains Unicode text, not GBK codes or glyph indices. U+00A0 through
U+00FF are rendered through NJEMU's existing Latin-1 font, which covers
Spanish diacritics without involving GBK. At build time a separate generated
C lookup maps the remaining non-ASCII code points required by the shipped
catalogs to the existing `gbk_s14` bitmap glyph indices. This keeps the pack
format independent from the font implementation and avoids adding a full
Unicode font table.

## Validation requirements

A loader must reject at least:

- wrong magic or unsupported version;
- unknown `language_id`;
- non-zero reserved field;
- a message count or schema hash that does not match the executable;
- a blob larger than 65534 bytes;
- a file whose actual size differs from the size derived from the header;
- non-NULL offsets outside the blob;
- referenced strings without a terminating NUL;
- referenced strings that are not valid UTF-8.

Generated packs are deterministic build artifacts. They are produced with:

```sh
python3 tools/build_translations.py --build
```

The default output is `build/translations/lang/`; generated `.lng` files are
not source files and should not be edited or committed.

## V1 compatibility

V1 used the same container layout but stored NJEMU's legacy byte-oriented/GBK
runtime representation. V2 deliberately does not preserve those payload bytes;
the Phase 2 migration established and tested byte equivalence first, then V2
moved the runtime contract to UTF-8.
