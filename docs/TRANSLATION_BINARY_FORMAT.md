# NJEMU translation pack format (`.lng` V1)

The runtime translation pack is deliberately small and dependency-free. All
multi-byte integers are **little-endian** and the file has no implicit padding.

## Header

The V1 header is 20 bytes:

| Offset | Size | Field | V1 value / meaning |
| ---: | ---: | --- | --- |
| 0 | 4 | `magic` | ASCII `NJTL` |
| 4 | 2 | `version` | `1` |
| 6 | 2 | `language_id` | `0=en`, `1=ja`, `2=es`, `3=zh-Hans`, `4=zh-Hant` |
| 8 | 2 | `message_count` | Number of stable IDs; currently 377 |
| 10 | 2 | `reserved` | Must be zero |
| 12 | 4 | `string_blob_size` | Bytes in the final string blob |
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

Because `0xffff` is reserved, V1 limits the string blob to **65534 bytes**.

## String blob

The offset table is followed immediately by `string_blob_size` bytes. Every
non-NULL entry points at a NUL-terminated byte string. There is no encoding
conversion in V1: the bytes are exactly those represented by the validated
`.lang` source.

Embedded NUL bytes inside a message are not representable in V1 and are
rejected by the generator. Empty strings are represented normally by a single
NUL byte in the blob.

## Validation requirements

A loader must reject at least:

- wrong magic or unsupported version;
- unknown `language_id`;
- non-zero reserved field;
- a message count or schema hash that does not match the executable;
- a blob larger than 65534 bytes;
- a file whose actual size differs from the size derived from the header;
- non-NULL offsets outside the blob;
- referenced strings without a terminating NUL.

Generated packs are deterministic build artifacts. They are produced with:

```sh
python3 tools/build_translations.py --build
```

The default output is `build/translations/lang/`; generated `.lng` files are
not source files and should not be edited or committed.
