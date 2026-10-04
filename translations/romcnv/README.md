# ROMCNV translations

This catalog owns user-visible messages that historically lived behind the
ROM converter's `CHINESE` preprocessor flag.

The editable sources are UTF-8 `KEY=value` files. `messages.def` is the numeric
key list, and `tools/build_romcnv_translations.py` validates key order,
completeness and `printf` format compatibility before generating the C string
tables used by ROMCNV.

The legacy converter only shipped English and Simplified Chinese text, so those
are the two catalogs currently provided. Unsupported languages fall back to
English rather than carrying duplicate placeholder translations.

At runtime ROMCNV selects the language in this order:

1. `-lang <tag>` / `--lang <tag>` (or `-lang=<tag>` / `--lang=<tag>`);
2. `NJEMU_LANG`;
3. `LC_ALL`;
4. `LC_MESSAGES`;
5. `LANG`;
6. English fallback.

Accepted Simplified Chinese forms include `zh-Hans`, `zh_CN`, `zh-CN`,
`zh_SG`, `zh-SG`, and plain `zh`. Other locale tags currently use English.
