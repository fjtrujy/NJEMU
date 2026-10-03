# NJEMU canonical game metadata

These TSV files are the source-controlled authority for human-facing game
metadata and compact per-game data that does not belong in executable code.
Runtime binaries and the cartridge-core `gamelist_<core>.txt` files are generated
by `tools/game_metadata.py`.

Do not edit generated `game_metadata.<core>` files. Edit the corresponding TSV
and regenerate/validate it instead.

For CPS1, CPS2, and MVS, `rominfo.<core>` remains authoritative for ROM set
identity, ROM parent relationships, regions, CRCs, and machine/input/init
selection. The generator requires the TSV identity set to match `rominfo`
exactly, preventing browser metadata from silently becoming stale.

NCDZ does not use `rominfo`; `metadata/ncdz.tsv` owns the NGH-to-canonical-name
mapping that was historically compiled into `driver.c`.

## Columns

The schema is shared so the runtime reader can use one versioned binary format:

- `name`: canonical lowercase NJEMU game/set name;
- `title_en`: English browser title (mandatory for CPS1/CPS2/MVS);
- `title_ja`: Japanese title, empty to fall back to English;
- `title_zh_hans`: Simplified Chinese title, empty to fall back to English;
- `title_zh_hant`: Traditional Chinese title, empty to fall back to English;
- `display_flags`: `|`-separated `not_work`, `bootleg`, and/or `hack`;
- `core_flags`: core-specific `|`-separated flags described below;
- `aux_name`: core-specific canonical game-name reference;
- `data0`, `data1`, `data2`: core-specific unsigned 32-bit values, accepted in
  decimal or `0x` notation.

## Core-specific fields

CPS1 currently has no core-specific fields.

CPS2 uses:

- `phoenix`: decrypted/Phoenix-style set; mutually exclusive with key data;
- `cache_parent_override`: `aux_name` is the streaming-cache parent rather than
  the normal `rominfo` parent;
- `cache_independent`: do not inherit a streaming cache from the ROM parent;
- `data0`: CPS2 decryption key word 0;
- `data1`: CPS2 decryption key word 1;
- `data2`: encrypted upper range, where zero retains the historical 4 MiB
  default.

Every CPS2 set must have exactly one of a decryption key or the `phoenix` flag.
The emulator and `romcnv_cps2` both use the cache-parent flags from this same
generated record set; converter-specific graphics cache geometry remains a
separate concern.

MVS uses:

- `owns_crom`: the clone has its own processed C-ROM asset;
- `owns_srom`: the clone has its own processed S-ROM asset;
- `owns_vrom`: the clone has its own processed V-ROM asset.

An MVS record with none of those bits follows the normal parent-inheritance
behavior. Both the emulator and `romcnv_mvs` consume these same generated bits;
there is no separate converter ownership table.

NCDZ uses `data0` for the 16-bit NGH identifier. NGH values must be non-zero and
unique. The NCDZ browser does not currently use friendly titles before opening
a disc/directory, so title columns may be empty.

## Encoding

The canonical TSV files and generated string pools are UTF-8. This deliberately
replaces the mixed historical encodings used by legacy `resources/zipname*`
files (UTF-8, CP932, and GBK depending on core/language).

The old `resources/zipname*` catalogs and hand-maintained resource-tree
`gamelist_<core>.txt` files were removed after the migration completed. Browser
metadata and supported-game lists must be derived from these canonical TSV
sources instead of introducing another manually maintained catalog.

## DIP menu metadata

CPS1 and MVS also keep localized DIP-menu schema/text under `metadata/`:

- `cps1_dips.json`
- `mvs_dips.json`

These files are UTF-8 source authority for menu labels, option labels, enabled
state, masks, and value ranges. The actual DIP bit manipulation remains in the
core C code; the JSON is deliberately data-only.

`tools/dip_metadata.py` validates that English, Japanese, Simplified Chinese,
and Traditional Chinese have identical row structure and generates the compact
versioned `dip_metadata.<core>` runtime file. The runtime reader validates the
header, bounds, version and CRC before materializing only the selected profile
and language. Do not edit generated `dip_metadata.*` files.

The build also feeds these JSON sources into Unicode-glyph generation so all
non-ASCII menu text must be representable by the shipped UI font.
