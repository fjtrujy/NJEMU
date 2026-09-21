# NJEMU translation system redesign plan

Status: 2026-09-21

## Goal

Replace the current platform-specific, compile-time translation tables with a
single portable translation subsystem that:

- keeps translations out of the emulator executable;
- uses materially less RAM on memory-constrained platforms;
- gives every text key a stable identity that does not depend on build flags;
- removes the duplicated Desktop/PS2/PSP translation tables;
- validates translation completeness and printf-style format compatibility
  before packaging;
- keeps runtime lookup cheap enough for PSP and PS2;
- preserves current rendering/encoding behaviour during the first migration.

This work is intentionally separate from a future font/Unicode redesign.
Storage, IDs and validation should be made robust first.

## Current implementation

The public API is centered on:

- `src/common/ui_text_driver.h`;
- `src/common/ui_text_driver.c`;
- `src/desktop/desktop_ui_text.c`;
- `src/ps2/ps2_ui_text.c`;
- `src/psp/psp_ui_text.c`.

Call sites use:

```c
TEXT(PLEASE_WAIT)
```

which expands to:

```c
ui_text_driver->getText(ui_text_data, PLEASE_WAIT)
```

The lookup shape is reasonable. The storage and ID generation are not.

### Compile-flag-dependent IDs

The text ID enum in `ui_text_driver.h` contains conditionals for, among other
things:

- `ADHOC`;
- `SAVE_STATE`;
- `COMMAND_LIST`;
- `LARGE_MEMORY`;
- `USE_CACHE`;
- `EMU_SYSTEM`.

There are currently 46 conditional-preprocessor blocks in the enum. The same
conditional structure is then repeated through each language table.

This means that an ID is not a stable property of a message. Its numeric value
depends on the exact target and feature flags used to compile the executable.

That is the main structural fragility in the current design. A missing or
different conditional in one translation array can shift every following
message while still producing valid C.

### Duplicated platform sources

Each platform embeds nearly the same five complete translation tables:

- English;
- Japanese;
- Spanish;
- Simplified Chinese;
- Traditional Chinese.

Current source sizes are approximately:

| File | Source size |
| --- | ---: |
| `src/desktop/desktop_ui_text.c` | 57.8 KiB |
| `src/ps2/ps2_ui_text.c` | 57.5 KiB |
| `src/psp/psp_ui_text.c` | 57.9 KiB |

The three files total about 184 KiB of duplicated source.

The translation data itself is effectively platform-independent. The
meaningful platform difference is how the preferred system language is
selected.

### Measured PS2 cost

With the existing feature-on builds (`GUI + SAVE_STATE + COMMAND_LIST`), the
PS2 translation object currently measures:

| Core | Current text IDs | `ps2_ui_text.c` object text+data |
| --- | ---: | ---: |
| CPS1 | 247 | 27.1 KiB |
| CPS2 | 259 | 29.6 KiB |
| MVS | 283 | 31.5 KiB |
| NCDZ | 254 | 28.4 KiB |

The exact linked/resident cost must be measured again at migration time, but
these object measurements are a useful baseline.

At runtime the current implementation also allocates a
`const char *ui_text[UI_TEXT_MAX]` array and copies every pointer from the
selected embedded language table. On 32-bit PSP/PS2 this costs roughly another
1 KiB for the larger current configurations.

### Language identity problems

The language constants are not a proper enum today. Several unsupported
language names alias `LANG_ENGLISH` with value zero, including Spanish.
That makes language identity unsuitable as a stable subsystem contract.

### Encoding is legacy and source-file-dependent

The three translation C files are detected as `unknown-8bit`, and Desktop
already suppresses invalid source-encoding warnings. The UI renderer currently
contains its own byte-oriented ASCII/GBK/graphic-token decoding.

Changing the translation storage format and changing the renderer to UTF-8 in
the same milestone would make regressions much harder to isolate.

## Target architecture

Use one common runtime translation catalog implementation and external language
packs.

The emulator should no longer compile translation strings into
`desktop_ui_text.c`, `ps2_ui_text.c`, or `psp_ui_text.c`.

Proposed source layout:

```text
src/common/
    ui_text.c
    ui_text.h
    ui_text_ids.h

translations/
    messages.def
    en.lang
    es.lang
    ja.lang
    zh-Hans.lang
    zh-Hant.lang

tools/
    build_translations.py
```

Packaged runtime files:

```text
lang/
    en.lng
    es.lng
    ja.lng
    zh-Hans.lng
    zh-Hant.lng
```

The exact directory name can be adjusted to the existing packaging conventions,
but all platforms should consume the same catalog files.

## Stable message IDs

### Rule

Message IDs must never depend on `EMU_SYSTEM` or optional compilation flags.

A key that exists keeps the same numeric ID across:

- CPS1;
- CPS2;
- MVS;
- NCDZ;
- PSP;
- PS2;
- Desktop;
- Debug/Release;
- every feature-flag combination.

### Manifest

Use a single authoritative manifest, for example:

```text
0   EOM
1   LF
2   PLEASE_WAIT
3   COULD_NOT_OPEN_ZIPNAME_DAT
...
```

or an equivalent X-macro form:

```c
UI_TEXT_ID(EOM,                         0)
UI_TEXT_ID(LF,                          1)
UI_TEXT_ID(PLEASE_WAIT,                 2)
UI_TEXT_ID(COULD_NOT_OPEN_ZIPNAME_DAT,  3)
```

The important property is that numeric values are explicit and stable.

Removing a message must not silently renumber later entries. Deleted IDs should
remain reserved until a deliberate catalog-format version break.

### No feature conditionals in the ID list

A feature flag decides whether code referring to a message is compiled. It must
not decide whether that message exists in the ID namespace.

All language packs contain the complete stable catalog, even if a particular
core uses only a subset. The catalog is small enough that this is preferable to
reintroducing build-configuration coupling.

## Source translation format

The editable files should be human-oriented rather than generated C.

A simple key/value form is sufficient:

```text
PLEASE_WAIT=Please wait...
COULD_NOT_OPEN_ZIPNAME_DAT=Could not open zipname.%s
START_EMULATION=Start emulation.
```

Requirements for the parser:

- comments and blank lines;
- deterministic escaping for `\n`, `\\` and literal separators;
- named tokens for NJEMU's graphic characters, e.g. `<CIRCLE>`,
  `<CROSS>`, `<LTRIGGER>`;
- byte-exact output support during the legacy-encoding migration;
- duplicate-key rejection;
- unknown-key rejection;
- missing-key rejection.

The source format can later become UTF-8 without changing the runtime pack
format or call-site API.

## Runtime binary format

Use a deliberately small, dependency-free format.

Version 1 proposal:

```text
header:
    magic[4]          = "NJTL"
    version           = 1
    language_id
    message_count
    string_blob_size
    schema_id/hash

offsets:
    uint16_t offsets[message_count]

strings:
    NUL-terminated byte strings
```

All multi-byte integer fields should have a defined byte order. Current
supported targets are little-endian, but the format should still specify it.

### Why 16-bit offsets

The current UI text corpus is far below 64 KiB per language. A 16-bit offset
table keeps the index around 0.5-0.7 KiB for the expected stable union of
messages.

The generator must reject a V1 catalog whose string blob exceeds 65534 bytes.
A future V2 can use 32-bit offsets if ever needed.

### Lookup

The loaded catalog can be represented as approximately:

```c
typedef struct ui_text_catalog {
    uint32_t language;
    uint16_t message_count;
    uint16_t *offsets;
    char *strings;
} ui_text_catalog_t;
```

The offsets and string blob should preferably live in one allocation so free
and error handling remain trivial.

Lookup becomes:

```c
const char *ui_text_get(ui_text_id_t id)
{
    if ((unsigned)id >= catalog.message_count)
        return missing_text;
    return catalog.strings + catalog.offsets[id];
}
```

There is:

- no hash table;
- no per-string allocation;
- no runtime string comparison;
- no disk I/O after initial loading.

## RAM strategy

Only one language pack should be resident.

Startup policy:

1. determine requested language;
2. try that catalog;
3. validate magic, version, schema, bounds and offsets;
4. if loading fails, free partial state and try `en.lng`;
5. if English also fails, report a bootstrap error and abort GUI startup
   cleanly.

Do not keep English loaded as a per-string fallback catalog. Translation packs
should be complete, and completeness is enforced offline.

This should reduce the translation subsystem from the current roughly
28-32 KiB PS2 object contribution plus runtime pointer table to one small
catalog allocation. The expected active catalog is only a few KiB, but the
actual before/after resident and ELF measurements must be recorded during the
implementation rather than treating the estimate as a guarantee.

## Platform responsibilities

Translation storage and lookup become common code.

Platform code should expose only the preferred system language.

A clean interface would be one platform-driver method such as:

```c
ui_language_t (*getSystemLanguage)(void *data);
```

Expected implementations:

- PSP: map `sceUtilityGetSystemParamInt(PSP_SYSTEMPARAM_ID_INT_LANGUAGE)`;
- PS2: map the existing `configGetLanguage()`;
- Desktop: use configured language first, then host locale if desired, with
  English fallback.

The translation subsystem should not need separate platform driver instances.

After migration, remove:

- `src/desktop/desktop_ui_text.c`;
- `src/ps2/ps2_ui_text.c`;
- `src/psp/psp_ui_text.c`.

## Language enum

Introduce real unique language values, for example:

```c
typedef enum ui_language {
    UI_LANG_ENGLISH = 0,
    UI_LANG_JAPANESE,
    UI_LANG_SPANISH,
    UI_LANG_CHINESE_SIMPLIFIED,
    UI_LANG_CHINESE_TRADITIONAL,
} ui_language_t;
```

Do not keep unsupported languages aliased to English. Unsupported platform
locale values should be mapped to `UI_LANG_ENGLISH` explicitly.

Existing language-dependent behaviour in file-browser naming and DIP-switch
tables must be validated against the new enum.

## Offline generator and validation

`tools/build_translations.py` should be the strict part of the system.

It should:

- parse the stable ID manifest;
- parse every language source;
- reject missing, duplicate or unknown keys;
- verify every language has exactly the same key set;
- verify printf-style conversion sequences against English;
- verify NJEMU graphic tokens;
- emit deterministic binary packs;
- optionally emit a human-readable report with byte sizes and hashes;
- support a verification-only mode for CI.

### Format-string validation

For every translated message, compare the formatting contract to English.

Examples that must fail generation:

- English has `%s`, translation omits it;
- English has `%d%%`, translation changes it to `%s`;
- translation changes argument order without positional support;
- an unsupported conversion specifier is introduced.

This prevents a translation edit from becoming a runtime varargs bug.

## Encoding migration policy

### Phase 1: preserve bytes

The first implementation must preserve the exact byte sequences expected by
the current UI font decoder.

Do not convert Japanese/Chinese strings to UTF-8 as part of the catalog-storage
migration.

The extraction/migration tooling should compare old and new strings byte for
byte for every reachable message.

### Phase 2: normalize sources

Once external packs are proven equivalent, the editable source files can be
normalized to a documented encoding, preferably UTF-8.

The pack generator can then encode into the legacy runtime form if necessary.

Encoding Phase 2 infrastructure status (2026-09-21): the generator now reads
`.lang` sources as UTF-8 and transcodes literal Unicode text to the legacy GBK
byte stream consumed by the current UI renderer. Exact `\xNN` escapes remain
available as an explicit byte-preservation mechanism. An audit of all five
current catalogs proved that every runtime string round-trips through GBK
without byte changes. The pack tests now lock the SHA-256 digest of every
language pack, not only English, and add UTF-8 transcoding, invalid UTF-8,
unencodable-character and exact-byte-escape coverage (24/24 tests passing).
No catalog source has been normalized yet in this infrastructure milestone.

Four pre-existing entries (two English, two Spanish) contain the byte sequence
`c2 b7`. Under the current GBK renderer those bytes decode as `路`, despite
apparently originating from a UTF-8 middle dot. Encoding Phase 2 deliberately
keeps those bytes escaped rather than silently changing runtime output; any
semantic correction belongs to the later renderer/content migration.

Japanese source normalization status (2026-09-21): `ja.lang` is now readable
UTF-8. All 5776 hexadecimal byte escapes were removed in favor of literal
Unicode text while preserving graphic/control tokens. The regenerated
`ja.lng` is byte-identical to the pre-normalization baseline and retains
SHA-256 `9a39ba63d7962e515480fec74179f2461d8fcf9edc409fb5681874e719c9ee06`.
The 24 translation tests remain green.

Simplified Chinese source normalization status (2026-09-21): `zh-Hans.lang`
is now readable UTF-8. All 2892 hexadecimal byte escapes were removed while
preserving the runtime byte stream. The regenerated `zh-Hans.lng` is
byte-identical to the pre-normalization baseline and retains SHA-256
`89d6a5682f35c4c308ed5fd63c6229d10642212df65c0e2ca9efb33fe15ca9a0`.
The 24 translation tests remain green.

Traditional Chinese source normalization status (2026-09-21): `zh-Hant.lang`
is now readable UTF-8. All 2489 hexadecimal byte escapes were removed while
preserving the runtime byte stream. The regenerated `zh-Hant.lng` is
byte-identical to the pre-normalization baseline and retains SHA-256
`700ed7b48f33225abd672935e56cd72a6019978851e5f0ad90e06b7523d9ed2f`.
The 24 translation tests remain green.

English source normalization status (2026-09-21): the three valid GBK
`a1 a4` middle-dot sequences are now literal UTF-8 `·` in `en.lang`. The
two legacy `c2 b7` sequences remain explicit hexadecimal escapes so their
current runtime bytes are not reinterpreted or changed. The regenerated
`en.lng` is byte-identical to the pre-normalization baseline and retains
SHA-256 `e287d6a283585cfdd41664c8e1ea57ec5fc8adeb6595edec227447fff5c4c415`.
The 24 translation tests remain green.

Spanish source normalization status (2026-09-21): the same three valid GBK
`a1 a4` middle-dot sequences are now literal UTF-8 `·` in `es.lang`, while
the two legacy `c2 b7` sequences remain explicit hexadecimal escapes. The
regenerated `es.lng` is byte-identical to the pre-normalization baseline and
retains SHA-256
`79c351b857be24740976851ee9e22888e1766e9d78fdd0cf7b2531c1d12aeadc`.
The 24 translation tests remain green.

At this point all five editable catalogs are UTF-8 source files. Japanese and
both Chinese catalogs contain no hexadecimal byte escapes. English and Spanish
retain only the four deliberately preserved `c2 b7` sequences described
above (two entries per language).

Encoding Phase 2 validation status (2026-09-21): all five regenerated packs are
byte-identical to the pre-Phase-2 baseline. The Python translation suite passes
24/24 tests. A feature-rich MVS configuration (`COMMAND_LIST=ON`,
`SAVE_STATE=ON`, `GUI=ON`, `ADHOC=OFF`) builds successfully on Desktop,
PS2 and PSP; Desktop also passes 5/5 CTest targets, PS2 produces `MVS.elf`,
and PSP produces `EBOOT.PBP`. The five installed packs from every platform
retain the locked SHA-256 hashes. No runtime renderer or binary pack-format
change was required.

### Phase 3: UTF-8 end-to-end

Encoding Phase 2 established a byte-exact baseline first. That intermediate
state is intentionally temporary: the editable sources are UTF-8 but the
generator still transcodes them back to GBK for the V1 runtime packs.

Phase 3 removes that last encoding dependency:

- `.lang` remains readable UTF-8;
- `.lng` V2 stores validated UTF-8 strings directly;
- graphic tokens are encoded as Unicode Private Use Area code points rather
  than raw `0x10..0x1e` control bytes;
- the UI renderer decodes UTF-8 and maps Unicode code points onto the existing
  GBK-backed glyph bitmap data, so the large font asset itself does not need to
  be replaced;
- Latin-1 Unicode characters use the existing `latin1_14` font directly;
- a compact generated Unicode -> glyph lookup contains only the remaining
  characters required by the shipped translation catalogs;
- legacy GBK decoding remains as a compatibility fallback for non-translation
  UI strings such as old resource metadata or filenames;
- malformed UTF-8 packs are rejected by the runtime loader;
- arbitrary `\\xNN` and embedded-NUL escapes are removed from editable
  translation content.

This design keeps the UTF-8 migration cheap in RAM: it reuses `gbk_s14` and the
existing legacy GBK table for compatibility rather than adding a second
full-Unicode font table. The generated Unicode lookup for translated strings is
expected to be only a few KiB.

The four deliberately preserved `c2 b7` sequences from Phase 2 can now be
normalized to the intended middle dot `·`, because V2 no longer promises
byte-level compatibility with the V1 string payload.

Phase 3 UTF-8 runtime milestone status (2026-09-21): implemented `.lng` V2
with validated UTF-8 payloads, PUA-encoded graphic tokens, strict runtime
UTF-8 validation and a compact generated Unicode-to-glyph lookup. The shipped
catalogs initially required 558 non-ASCII glyph mappings. `uifont_*` decodes
UTF-8 directly and retains the old GBK path only as a compatibility fallback
for non-translation strings. The four Phase 2 `c2 b7` escapes were normalized
to literal `·`. Validation passes 27/27 Python translation tests and 6/6
Desktop CTest targets; feature-rich MVS builds also succeed on PS2 and PSP.
Relative to the Phase 2 MVS baseline, the UTF-8 decoder and compact lookup add
about 3.6 KiB to the PS2 executable and 2.9 KiB to the PSP EBOOT.

Phase 3 source/font cleanup status (2026-09-21): arbitrary hexadecimal byte
escapes and embedded-NUL escapes are no longer accepted by `.lang` sources.
Latin-1 characters are rendered by the existing `latin1_14` asset regardless
of `COMMAND_LIST`, so Spanish can use correct diacritics such as `ñ`, `ó` and
`ú` without extending the GBK lookup. The generated CJK/non-Latin-1 lookup is
now 557 entries. Obvious spelling/diacritic corruption in `es.lang` was
corrected without changing message contracts. Translation tests pass 28/28,
and Desktop GUI builds pass 6/6 CTest with `COMMAND_LIST` both ON and OFF.

## Migration plan

### T0 - Capture the current contract

Before changing runtime code:

- enumerate all four cores and relevant feature combinations;
- preprocess the current ID enum and translation tables;
- build the union of message names used by all configurations;
- verify that a given symbolic key has the same translated bytes wherever it
  appears;
- record current PS2/PSP/Desktop binary and runtime memory baselines.

Important: because no single `EMU_SYSTEM` configuration exposes all
core-specific IDs, the migration tool must build the union across CPS1, CPS2,
MVS and NCDZ rather than treating one build as authoritative.

Deliverable: a machine-readable stable key manifest plus a baseline report.

T0 status (2026-09-21): implemented in `tools/capture_translation_contract.py`
and `docs/TRANSLATION_T0_BASELINE.md`. The exhaustive source-level matrix covers
all 16 combinations of `ADHOC`, `SAVE_STATE`, `COMMAND_LIST` and
`LARGE_MEMORY` for each core and compares Desktop/PS2/PSP tables byte-for-byte.
It found and fixed one pre-existing MVS positional-table bug in the four
non-English catalogs (`e56539d`). The legacy union contains 362 symbolic names;
14 of those were core-dependent, so `translations/messages.def` normalizes them
before assigning IDs and exposes 377 unambiguous stable IDs. Feature-on Desktop
and PS2 object/executable baselines plus the exact per-driver pointer-copy heap
cost are recorded in the T0 report. A native PSP binary-size measurement is not
available on the current workstation because no PSP toolchain is installed;
the 32-bit PSP pointer-copy heap cost is exact, and commit `e56539d` plus the
recorded source hashes preserve the pre-T1 input so that binary figure can be
backfilled later from CI/toolchain without depending on migrated sources.

### T1 - Introduce stable IDs

- create the explicit stable ID manifest/header;
- replace the conditional enum with the stable namespace;
- keep the existing embedded arrays temporarily;
- adapt the legacy tables so lookup still returns byte-identical strings;
- add compile-time/runtime tests asserting representative IDs do not change
  across target/feature matrices.

This milestone changes identity but not storage.

T1 status (2026-09-21): implemented with `src/common/ui_text_ids.h` generated
from the explicit 377-entry `translations/messages.def` namespace. The old
conditional public enum is gone. Embedded catalogs remain temporarily
positional behind `translations/legacy_layout.def` and
`ui_text_copy_legacy_catalog()`, which maps each active legacy slot into its
stable ID while preserving the original bytes. Core-dependent legacy names for
stretch modes, numbered-vs-lettered buttons/autofire, reset help and
`ROMINFO_NOT_FOUND` have been replaced at call sites by unambiguous stable IDs.
`ui_text_id_tests` locks representative numeric IDs and
`ui_text_legacy_map_tests` checks the complete active mapping for duplicates,
holes and wrong destinations. Validation covers all four Desktop feature-on
builds (3/3 tests each), Desktop CPS2/MVS without `SAVE_STATE`, all four PS2
feature-on builds, all four PS2 base builds, and the exhaustive 192-table
source matrix. Native PSP compilation remains unavailable locally, but PSP is
included in the byte-level matrix and shares the same adapter source.

### T2 - Extract language sources

- generate initial `translations/*.lang` from the verified legacy tables;
- preserve exact bytes/escapes;
- replace raw graphic bytes with documented named tokens only where the
  conversion is provably reversible;
- run completeness and format-string validation.

No embedded tables are removed yet.

T2 status (2026-09-21): implemented with five complete ASCII source catalogs
(`en.lang`, `ja.lang`, `es.lang`, `zh-Hans.lang`, `zh-Hant.lang`) and
`tools/build_translations.py`. The source format preserves runtime-visible
legacy bytes using deterministic escapes, represents NJEMU graphic bytes with
named tokens, and keeps the reserved `END_OF_TEXT` NULL value explicit. The
validator rejects duplicate/unknown/missing/reordered keys, invalid escapes or
graphic tokens, and `printf` conversion contracts that differ from English.
All 377 entries in all five languages compare byte-for-byte with the embedded
catalogs across the same exhaustive 192-table matrix. Negative validation was
also exercised for duplicate, unknown and missing keys, an invalid graphic
token, and a `%s` -> `%d` format mismatch; all were rejected as expected.

### T3 - Implement the pack generator

- define and document `.lng` V1;
- implement deterministic generation;
- add malformed-pack tests;
- add round-trip tests proving every generated entry matches the extracted
  source bytes;
- generate all five language packs.

Generated runtime packs should be build/package artifacts, not hand-edited
files.

T3 status (2026-09-21): implemented `.lng` V1 and documented it in
`docs/TRANSLATION_BINARY_FORMAT.md`. V1 uses a 20-byte little-endian header,
377 direct `uint16_t` offsets, a reserved `0xffff` NULL offset, a <=65534-byte
NUL-terminated string blob, and a 32-bit FNV-1a schema hash over the explicit
ID/name manifest (`0x1ed49de8` for the current schema).
`tools/build_translations.py --build` deterministically emits and immediately
round-trips all five packs
under `build/translations/lang/`. Current complete pack sizes are 7452 B (en),
8446 B (ja), 8598 B (es), 5298 B (zh-Hans) and 5294 B (zh-Hant). Twelve pack
tests cover all-language round trips plus bad magic/version/language/count,
reserved bits, size/schema corruption, bad offsets, truncation and missing NUL
termination. Source validation and pack tests are registered with Desktop
CTest; all four feature-on cores pass 5/5 tests.

### T4 - Add the common runtime loader

- implement one-allocation catalog loading;
- validate all file sizes/offsets before exposing strings;
- implement requested-language -> English fallback;
- keep the public `TEXT(KEY)` call sites working;
- add tests for valid packs, missing packs, corrupt headers, bad offsets and
  schema mismatch.

At this point Desktop should be the first interactive runtime validation
platform.

T4 status (2026-09-21): implemented the common `.lng` V1 loader in
`ui_text_catalog.c/.h`. It validates header/schema/count/size/offsets and NUL
termination before exposing strings, stores the offset table plus string blob
in one resident allocation, performs requested-language -> English fallback,
and preserves the existing `TEXT(KEY)` API through the text-driver interface.
Desktop is the first migrated runtime: it now links
`desktop_ui_text_catalog.c` instead of the embedded `desktop_ui_text.c` tables,
and CMake generates the five packs under each Desktop build's `lang/` directory
without writing to `resources/`. A C loader test covers a real valid pack,
missing/corrupt requested-language fallback, missing English, bad magic/schema,
out-of-range offsets and truncation; the Python pack tests cover the remaining
malformed-header/NUL cases. All four Desktop feature-on cores pass 6/6 CTest,
and all four PS2 feature-on cores compile the common loader successfully while
continuing to use their embedded adapter until language selection is separated
in T5. Link-command auditing confirms Desktop no longer links the legacy text
object. PSP native compilation remains unavailable locally.

### T5 - Move language selection to platform drivers

- add a unique `ui_language_t`;
- add platform language selection;
- preserve PSP system-language behaviour;
- preserve PS2 OSD-language behaviour;
- define Desktop behaviour explicitly;
- audit all current direct `getLanguage()` users.

T5 status (2026-09-21): implemented a unique five-value `ui_language_t`
(`ENGLISH`, `JAPANESE`, `SPANISH`, simplified Chinese, traditional Chinese)
whose numeric values match the `.lng` V1 language IDs. The previous aliases
where unsupported languages (including Spanish) collapsed to English have been
removed. `platform_driver_t` now exposes `getSystemLanguage()`: PS2 maps
`configGetLanguage()`, PSP maps `sceUtilityGetSystemParamInt()`, and Desktop
keeps its historical English-only behaviour explicitly. Both the common pack
driver and the temporary PS2/PSP legacy adapters consume that platform result;
no translation-storage file reads an OS language API directly anymore. All
current `getLanguage()` consumers (file-browser naming, DIP switches, NCDZ
defaults and UI layout behaviour) use the new `UI_LANG_*` identities. The four
Desktop feature-on builds continue to pass 6/6 tests and all four PS2
feature-on builds compile successfully. Native PSP compilation remains
unavailable locally.

### T6 - Remove embedded/platform translation sources

After byte-equivalence and runtime tests pass on all platforms:

- remove `desktop_ui_text.c`;
- remove `ps2_ui_text.c`;
- remove `psp_ui_text.c`;
- remove the platform-specific text-driver registry if no longer useful;
- simplify CMake so translation storage does not vary with platform or feature
  flags.

T6 status (2026-09-21): implemented. The three platform-embedded translation
sources and the temporary Desktop catalog driver are deleted, along with
`ui_text_legacy.h`, `legacy_layout.def`, the legacy mapping test and the T0
capture tool that depended on those sources. `ui_text_driver.c` is now the
single runtime text driver on Desktop, PS2 and PSP; it asks the platform driver
for `ui_language_t` and loads the corresponding common `.lng` catalog. The old
platform-specific text-driver registry is gone. `messages.def` plus the five
`.lang` files are now the authoritative source, and `build_translations.py` is
self-contained instead of reading deleted C tables. CMake generates the same
five packs into every build directory regardless of platform/core/feature
flags. Byte equivalence was established and committed before deletion in T0-T5.
After removal, all four Desktop feature-on builds pass 5/5 tests and all four
PS2 feature-on builds compile successfully while generating identical pack
sizes. Native PSP compilation remains unavailable locally.

### T7 - Packaging

Ensure every GUI package contains the required catalogs.

At minimum:

```text
lang/en.lng
lang/es.lng
lang/ja.lng
lang/zh-Hans.lng
lang/zh-Hant.lng
```

Validate relative paths on:

- native PSP;
- PPSSPP;
- native PS2;
- PCSX2;
- Desktop.

Do not place generated/test catalogs under the user's `resources/` tree
during development. Use isolated build/runtime directories for smoke tests.

T7 packaging status (2026-09-21): implemented. CMake installs the five
generated packs as a dedicated `translations` component under `lang/` for
Desktop, PS2 and PSP, while the legacy PSP Makefile now builds the same packs
and the legacy PSP artifact workflow uploads `lang/` alongside `EBOOT.PBP`.
The CMake PSP/PS2 container workflows and Ubuntu Desktop workflow explicitly
provide Python 3 for build-time pack generation. Isolated component installs
for Desktop and PS2 produced exactly the required five files with byte-identical
hashes and without copying/touching `resources/`. A Desktop runtime smoke passed
text initialization from a build root containing `lang/`. A PCSX2 smoke mapped
`host:` to the PS2 build root, executed the new ELF for ~7 seconds and remained
alive through initialization with the adjacent `lang/` directory present. The
legacy PSP Makefile pack target was syntax/generation-smoke-tested locally using
a stub SDK include. The local PSPSDK was subsequently located and used to build
all four PSP cores. A PPSSPP runtime matrix forced all five supported PSP system
languages and confirmed each run opens the expected adjacent `lang/*.lng`
catalog without fallback/error. A missing Spanish pack correctly falls back to
English. Native PS2 hardware remains optional because PCSX2 already validates
the relative package path used by the loader.

### T8 - Memory/performance verification

For PS2 and PSP, record:

- executable size before/after;
- free RAM immediately before text initialization;
- free RAM immediately after catalog load;
- catalog allocation size;
- startup I/O time;
- lookup cost in a tight synthetic loop if needed.

Acceptance target:

- one small catalog allocation;
- no permanent per-message pointer copy;
- no embedded full-language tables;
- no recurring file I/O during GUI rendering;
- clear net RAM reduction over the current implementation.

T8 status (2026-09-21): PS2 measurement completed and documented in
`docs/TRANSLATION_T8_MEASUREMENTS.md`. A clean pre-migration snapshot at
`e56539d` was rebuilt with the current PS2 toolchain and compared against the
post-migration builds. The actual `text+data+bss` image shrinks by 19,640 B
(CPS1), 22,224 B (CPS2), 24,328 B (MVS) and 21,208 B (NCDZ). The English
catalog is one 7456-byte PS2 allocation; after accounting for the old ~1512-byte
pointer table this still leaves a conservative net RAM saving of about 13.7-18.4
KiB depending on core. A temporary PCSX2 probe measured English pack loading at
~2.05 ms with an 8192-byte heap-break increment (allocator granularity), and
was removed after measurement. Runtime lookup is pure in-memory O(1) with no
recurring I/O. PSP cross-compilation and PPSSPP runtime loading are now also
validated; English remains one 7456-byte catalog allocation on the 32-bit PSP
ABI. Real-PSP free-RAM/timing measurement is optional performance follow-up,
not a blocker for the translation migration.

### T9 - Cross-build fragility matrix

Build at least:

- CPS1/CPS2/MVS/NCDZ;
- GUI on;
- SAVE_STATE on/off;
- COMMAND_LIST on/off;
- USE_CACHE where applicable;
- ADHOC on PSP where buildable.

For each build, assert representative stable IDs have identical numeric values.

The same prebuilt language pack must work across all four cores and all tested
feature combinations.

T9 status (2026-09-21): locally completed and documented in
`docs/TRANSLATION_T9_MATRIX.md`. Desktop validated all 16 combinations of four
cores x `SAVE_STATE={ON,OFF}` x `COMMAND_LIST={ON,OFF}` with the four
translation-specific tests passing in every build. The same 16 combinations
cross-compiled successfully for PS2. CPS2/MVS cover the cache-enabled cores.
Every Desktop and PS2 build generated a byte-identical five-pack set with the
same aggregate SHA-256 digest. The Python translation suite now has 20 tests and
explicitly covers the generator failure modes listed below, including V1 blob
overflow and deterministic output. The PSP CMake workflow now includes explicit
`ADHOC=ON` builds for CPS1/CPS2/MVS, removes the stale duplicate `LARGE_MEMORY`
axis, and validates that every installed PSP artifact contains exactly the five
required `.lng` catalogs. Local PSPSDK builds then executed that missing PSP
coverage directly: CPS1/CPS2/MVS all link with `ADHOC=ON + SAVE_STATE=ON`, and
NCDZ builds with `SAVE_STATE=ON + COMMAND_LIST=ON`. This exposed and fixed a
pre-existing CMake omission of `psp/adhoc.c` and the PSP adhoc networking
libraries. All four PSP builds generate byte-identical packs.

## Closure status

T0-T9 are complete for the translation storage/identity migration.

Validated platforms now include:

- Desktop runtime + exhaustive translation tests;
- PS2 cross-build matrix + PCSX2 runtime smoke;
- PSP cross-builds for all four cores, including ADHOC on CPS1/CPS2/MVS;
- PPSSPP runtime selection for all five supported languages;
- PSP requested-language -> English fallback with a deliberately missing pack.

No known translation-system implementation or required validation work remains.
Real PSP free-RAM/timing measurements and real PS2 storage timing can still be
collected as performance data, but they are not blockers for this migration.

The encoding work described earlier under **Encoding migration policy, Phase 2**
is a separate follow-up project. It can now start because the external-pack
runtime checks are complete. Phase 2 intentionally changes the
editable source representation from byte-exact ASCII escapes toward UTF-8 while
keeping legacy runtime bytes stable.

## Tests to add

### Generator tests

- complete catalog succeeds;
- missing key fails;
- extra key fails;
- duplicate key fails;
- bad escape fails;
- format-argument mismatch fails;
- string blob over V1 limit fails;
- deterministic output hash.

### Loader tests

- valid pack;
- truncated header;
- wrong magic;
- wrong version;
- wrong schema;
- truncated offset table;
- out-of-range offset;
- missing NUL terminator;
- missing selected language -> English fallback;
- missing English -> clean initialization failure.

### Compatibility tests

For each legacy language and the union of keys:

```text
legacy_bytes(KEY) == new_catalog_bytes(KEY)
```

This byte-equivalence test is the gate for deleting the embedded arrays.

## Non-goals for this migration

Do not combine this work with:

- redesigning the UI font renderer;
- replacing all legacy fonts;
- changing Japanese/Chinese glyph coverage;
- translating DIP-switch source tables that are independent of the UI text
  catalog;
- adding new languages before the five existing catalogs are migrated and
  validated.

Those can follow once storage and identity are stable.

## Expected end state

When this plan is complete:

- `TEXT(KEY)` remains simple at call sites;
- every `KEY` has a stable build-independent ID;
- translations are external files rather than executable data;
- exactly one compact catalog is resident;
- Desktop, PS2 and PSP share one loader;
- platform code only selects a language;
- adding or editing a translation requires no C recompilation;
- translation mistakes are caught by the generator/CI rather than by fragile
  positional alignment;
- feature flags cannot renumber or desynchronize translations.
