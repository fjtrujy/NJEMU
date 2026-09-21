# NJEMU translation sources

`messages.def` is the authoritative, build-independent numeric ID namespace.
The five `.lang` files contain one value for every stable ID in exactly that
order.

The `.lang` files are UTF-8 source files. The runtime renderer still consumes
its legacy byte-oriented encoding, so the pack generator transcodes literal
Unicode text to GBK before writing `.lng` V1. This keeps the runtime pack
format and renderer unchanged while making translation sources human-readable.

## Syntax

Each non-comment line is:

```text
KEY=value
```

Blank lines and lines whose first non-space character is `#` are ignored.
Duplicate, unknown, missing or reordered keys are rejected.

The value syntax supports:

- `\n`, `\r`, `\t` and `\0`;
- `\\` for a literal backslash;
- `\xNN` for an exact legacy runtime byte when a value cannot yet be
  represented safely as readable Unicode;
- named graphic bytes such as `<CIRCLE>`, `<CROSS>`, `<SQUARE>`,
  `<TRIANGLE>`, `<UPARROW>` and `<DOWNARROW>`;
- `<NULL>` only as the complete value of the reserved `END_OF_TEXT` entry.

Literal `<` bytes are emitted as `\x3c`, so an unescaped `<...>` sequence is
always a graphic token.

Literal Unicode characters must be representable in the language pack's legacy
runtime encoding. All five current catalogs use the same GBK byte decoder in
the UI renderer, so the generator rejects source characters that Python cannot
encode as GBK. Exact `\xNN` escapes bypass transcoding and are intentionally
kept for exceptional byte sequences whose runtime behaviour must remain
unchanged during Encoding Phase 2.

## Validation

Run:

```sh
python3 tools/build_translations.py
```

The validator checks all five catalogs for valid UTF-8, completeness, exact
manifest order, known escapes/tokens, representability in the legacy runtime
encoding and the same `printf` conversion contract as English.
`messages.def` plus these five `.lang` files are now the authoritative source;
the old platform-embedded C tables were removed after byte-equivalence was
established during T0-T5.

Generated `.lng` runtime packs must not be hand-edited. Their exact binary
layout is documented in `docs/TRANSLATION_BINARY_FORMAT.md`.
