# NJEMU translation sources

`messages.def` is the authoritative, build-independent numeric ID namespace.
The five `.lang` files contain one value for every stable ID in exactly that
order.

During the byte-preserving migration phase the `.lang` files are intentionally
ASCII even for Japanese and Chinese. This avoids editor/source-encoding changes
while NJEMU still uses its legacy byte-oriented font decoder.

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
- `\xNN` for an exact byte;
- named graphic bytes such as `<CIRCLE>`, `<CROSS>`, `<SQUARE>`,
  `<TRIANGLE>`, `<UPARROW>` and `<DOWNARROW>`;
- `<NULL>` only as the complete value of the reserved `END_OF_TEXT` entry.

Literal `<` bytes are emitted as `\x3c`, so an unescaped `<...>` sequence is
always a graphic token. Non-ASCII text must remain `\xNN`-escaped until the
separate UTF-8 migration phase.

## Validation

Run:

```sh
python3 tools/build_translations.py
```

The validator checks all five catalogs for completeness, exact manifest order,
known escapes/tokens and the same `printf` conversion contract as English. While
the embedded legacy tables still exist, it also compares every decoded value
byte-for-byte with those tables across the complete core/feature matrix.

The initial sources were extracted with:

```sh
python3 tools/build_translations.py --extract-legacy
```

Re-extraction refuses to overwrite edited sources unless `--force` is supplied.
Generated `.lng` runtime packs are introduced in T3 and must not be hand-edited.
Their exact binary layout is documented in `docs/TRANSLATION_BINARY_FORMAT.md`.
