# NJEMU translation sources

`messages.def` is the authoritative, build-independent numeric ID namespace.
The five `.lang` files contain one value for every stable ID in exactly that
order.

The `.lang` files are UTF-8 source files and the generated `.lng` V2 packs
store UTF-8 directly. There is no source-to-GBK transcoding in the translation
pipeline.

## Syntax

Each non-comment line is:

```text
KEY=value
```

Blank lines and lines whose first non-space character is `#` are ignored.
Duplicate, unknown, missing or reordered keys are rejected.

The value syntax supports:

- `\n`, `\r` and `\t`;
- `\\` for a literal backslash;
- `\=`, `\#`, `\<` and `\>` for literal syntax characters when needed;
- named graphic tokens such as `<CIRCLE>`, `<CROSS>`, `<SQUARE>`,
  `<TRIANGLE>`, `<UPARROW>` and `<DOWNARROW>`;
- `<NULL>` only as the complete value of the reserved `END_OF_TEXT` entry.

An unescaped `<...>` sequence is treated as a graphic token. Write `\<` and
`\>` when literal angle brackets are required. Arbitrary hexadecimal byte
escapes and embedded-NUL escapes are intentionally unsupported: textual
content must be valid UTF-8 and should use the intended Unicode character
directly.

Graphic tokens are encoded in V2 packs as Unicode Private Use Area code points
(`U+E000..`) rather than raw control bytes. The renderer maps those code points
back to the existing graphic glyphs.

## Font repertoire

The renderer keeps NJEMU's existing `gbk_s14` bitmap font asset. During the
build, the generator derives a compact Unicode-to-glyph lookup containing only
the non-ASCII characters required by the shipped translation catalogs. A
translation character that has no glyph in the current font is rejected at
validation time instead of silently rendering garbage.

The normal UI renderer also retains a legacy GBK fallback for non-translation
strings such as old resource metadata and filenames.

## Validation

Run:

```sh
python3 tools/build_translations.py
```

The validator checks all five catalogs for valid UTF-8, completeness, exact
manifest order, known escapes/tokens, supported font glyphs and the same
`printf` conversion contract as English.

Generated `.lng` runtime packs must not be hand-edited. Their exact binary
layout is documented in `docs/TRANSLATION_BINARY_FORMAT.md`.
