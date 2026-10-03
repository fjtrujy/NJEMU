# Game Metadata Externalization Plan

## Status

Branch: `externalize_game_metadata`

This document is the authoritative plan and progress log for the game metadata
cleanup. It covers CPS1, CPS2, MVS, and NCDZ. It must be kept current as each
migration lands.

The work has two related goals:

1. make game identity and display-name metadata deterministic and validated;
2. move data-heavy per-game metadata out of the executable where doing so is
   safe and reduces binary size without changing emulator behavior.

The runtime resource trees under `resources/` are validation trees and may
contain local ROMs, caches, configuration, NVRAM, and other private runtime
files. This work must never modify or stage anything under `resources/`.

## Baseline and constraints

The branch was created from the actual repository state at `e397ff3`
(`Document PSP ME runtime validation`). The older requested reference
`c9ecb0b` remains in history but was no longer HEAD when this work started.

The existing untracked ROM/cache/BIOS data under `resources/` and
`tools/__pycache__/` are pre-existing local data and are not part of this work.

Desktop GUI baseline builds succeed for all four cores before any metadata
change:

- CPS1
- CPS2
- MVS
- NCDZ

The baseline Mach-O segment sizes are useful only as coarse whole-program
references because Apple's `size` and `nm -S` do not report precise per-symbol
sizes for these binaries. Object-level measurements are more useful for the
first prioritization pass:

- CPS2 `cps2crpt.c.o`: 12,255 B `__TEXT`, 5,880 B `__DATA`;
- CPS2 `memintrf.c.o`: 11,159 B `__TEXT`, 3,376 B `__DATA`;
- MVS `driver.c.o`: 11,526 B `__TEXT`, 11,826 B `__DATA`;
- MVS `memintrf.c.o`: 28,589 B `__TEXT`, 3,280 B `__DATA`;
- NCDZ `driver.c.o`: 9,270 B `__TEXT`, 1,376 B `__DATA`;
- CPS1 `dipsw.c.o`: 40,391 B `__TEXT`, 516,800 B `__DATA`;
- CPS1 `driver.c.o`: 6,478 B `__TEXT`, 11,316 B `__DATA`.

Those figures include all data in each object rather than only the candidate
table, so final before/after decisions still require delta measurements. They
do establish that CPS1 localized DIP metadata is a much larger size candidate
than the initial game-name examples.

## Current game-name pipeline

### CPS1, CPS2, and MVS GUI

The GUI browser currently loads one of these text files from `launchDir`:

- `zipname.<core>`
- `zipnamej.<core>`
- `zipnamech1.<core>`
- `zipnamech2.<core>`

`src/common/filer.c:load_zipname()` selects the localized file when present and
falls back to the base file. It loads up to `MAX_GAMES` records into a temporary
heap array. `get_zipname()` maps a ZIP basename to a friendly title and display
flags.

A ZIP that is absent from the selected `zipname` catalog is marked
`GAME_BADROM` and displayed using its raw ROM-set filename. Therefore a stale
name database can make a supported game look unsupported even though
`rominfo.<core>` can boot it correctly.

The selected game identity used by emulation is not the friendly title. The
browser strips the `.zip` extension, lowercases the ROM-set basename, and
stores it in the global `game_name` buffer.

### CPS1, CPS2, and MVS no-GUI

The no-GUI path does not use `zipname*`. `src/common/no_gui.c:file_browser()`
reads the first line of `launchDir/game_name.ini` directly into `game_name`.
The selector is mutable configuration rather than a game database.

### NCDZ

NCDZ does not use `zipname*` at all. The browser displays the storage directory
or ZIP name. Once a game is opened, `neogeo_check_game()` reads the game NGH
identifier and maps it through the compiled `games[]` table to NJEMU's
canonical internal `game_name`.

That means NCDZ has a game-identity metadata table, but not the same pre-launch
friendly-name database used by the cartridge cores. The migration must preserve
that distinction rather than forcing NCDZ into a misleading ZIP-name model.

### Platform behavior

Desktop, PSP, and PS2 use the same `launchDir` metadata contract. PS Vita
packages read-only assets in `app0:` and copies missing files recursively to
`ux0:data/<target>/` before switching `launchDir` there. Any new generated
metadata file included in the VPK therefore follows the same common runtime
contract automatically.

## Root cause of unreliable display names

The existing `zipname*` files are manually tracked runtime resources. No build
step generates them from the actual supported-game database, and no test checks
that they still describe the same set of games as `rominfo.<core>`.

The divergence is already observable in the current tree:

- CPS1 `rominfo.cps1` and the base/Japanese catalogs each describe 137 sets,
  but each Chinese catalog contains 217 entries, omits 15 current supported
  set names, and contains 95 names absent from the current ROM database.
- CPS2 is currently consistent: all four catalogs and `rominfo.cps2` describe
  the same 286 set names.
- MVS `rominfo.mvs` describes 305 sets while the English catalog has only 300.
  The supported sets `kof2001d`, `kof2k1hd`, `kof2kd`, `roboarma`, and
  `samsho2k2` are missing from the English browser catalog. The Japanese
  catalog also contains missing, extra, and case-drifted identities.

The localized legacy files also use mixed encodings rather than one text
contract: current files include UTF-8, CP932, and GBK content depending on core
and locale. The canonical metadata source normalizes strings to UTF-8.

The executable metadata had stale entries too. The CPS2 Phoenix list contains
the unsupported alias `gigaman2`. MVS `MVS_cacheinfo[]` contains eight names
absent from current `rominfo.mvs` (`fatfursa`, `kf2k2ur`, `kof96pm`, `kof97c`,
`kof97prc`, `kof97xt`, `kof98a`, `kof98evo`) plus a duplicate `shocktroa`
record. These are omitted from the canonical source rather than preserved as
dead runtime metadata.

This is not a loader bug that should be hidden with a broader fallback. It is a
data-ownership problem: supported game identities, parent relationships,
friendly titles, localized titles, and per-game runtime flags are maintained in
independent lists with no deterministic cross-check.

## Authority model

There should be one authoritative owner for each kind of information, with a
build-time validator enforcing every intentional relationship between them.

### ROM topology authority

`rominfo.cps1`, `rominfo.cps2`, and `rominfo.mvs` remain authoritative for ROM
set identity, parent relationships, ROM regions, CRCs, machine/input/init
selection, and ROM layout. They are already runtime metadata and moving that
large parser/database is outside this task.

### Human-facing and compact per-game metadata authority

Add source-controlled game metadata outside `resources/`, with one canonical
record per supported set. It owns:

- English display title;
- optional localized display titles;
- browser display flags such as bootleg/hack/not-working;
- compact per-game fields that are currently encoded as large static lookup
  tables or long name-comparison chains and are safe to data-drive.

For CPS1/CPS2/MVS, generation must validate the canonical record set against
`rominfo.<core>` exactly. Parent relationships used by emulation remain sourced
from `rominfo`; the metadata generator may read them for validation but should
not create another independent parent authority unless a field explicitly
represents a different semantic relationship.

For NCDZ, the canonical metadata source owns the NGH-to-internal-name mapping
currently compiled as `games[]`.

### Generated runtime artifact

A deterministic generator produces a compact versioned binary metadata file for
each core. Build staging, install packaging, PSP/PS2 deployment layouts, and
Vita VPK packaging consume the generated artifact rather than manually copied
`zipname*` catalogs.

The generated format must:

- have a magic value, format version, core identifier, record count, record
  size, and validated bounds;
- use fixed-width little-endian fields rather than host ABI structs;
- store canonical set names in bounded fields;
- store display strings in a deduplicated string pool referenced by offsets;
- support language fallback to English without duplicating English text;
- support core-specific compact fields without making the common browser know
  core behavior;
- reject malformed/truncated files safely;
- be deterministic byte-for-byte for identical source inputs.

The exact record layout should remain deliberately small and versioned so it can
be changed later without silently interpreting incompatible files.

## Legacy `resources/zipname*` policy

The tracked `resources/zipname*` files are historical inputs today, but this
branch must not edit anything under `resources/`.

The migration therefore has two stages:

1. seed the new canonical source from the existing catalogs plus the known
   corrections, then validate the new source independently;
2. switch build/runtime packaging to generated metadata so the old files are no
   longer runtime authorities.

The legacy files may remain in the repository for compatibility/reference in
this branch, but CMake and tests must no longer depend on their correctness.
Documentation must make that status explicit. A future repository-cleanup
change can remove them separately if desired.

## Static executable metadata audit

The following candidates are data rather than executable algorithms and should
be evaluated for externalization.

### CPS2 encryption keys — high priority

`src/cps2/cps2crpt.c` contains `static const struct game_keys keys_table[]`, a
large per-set table of:

- game name;
- two 32-bit decryption keys;
- upper encrypted-range limit.

`cps2_init_68k()` linearly scans it by `game_name`. The table is required only
when initializing the selected game and is a strong candidate for generated
runtime metadata. The decryption algorithm itself remains compiled code.

### CPS2 Phoenix/decrypted-set classification — high priority

`src/cps2/memintrf.c` has a long `!strcmp(game_name, ...)` chain used to set the
Phoenix/decrypted edition flag. That list is pure game classification data and
should become a metadata flag.

The nearby CPS2 cache-parent special cases are also metadata. They should be
represented explicitly only where their semantic cache parent differs from the
normal ROM parent. The special `mpangj` independent-cache rule must remain
preserved.

### MVS processed/cache inheritance — high priority

`src/mvs/driver.c` embeds `MVS_cacheinfo[]`, which records per-clone ownership
of C-ROM, S-ROM, and V-ROM processed assets. Runtime code scans the table only
to decide which regions inherit parent processed assets. This is pure per-game
metadata and should move to the generated runtime artifact.

The converter has a related table and must consume the same canonical metadata
or generated representation so converter/runtime policy cannot drift.

### NCDZ NGH identity map — medium priority

`src/ncdz/driver.c` embeds `games[100]`, mapping NGH identifiers to canonical
internal game names. Runtime lookup and command-list reduction consume it. This
is pure identity data and should be loaded/enumerated from the generated NCDZ
metadata file.

The special BIOS/default sentinels should become explicit logic or metadata
records; code must not retain brittle assumptions such as `games[99]` or
"first 97 entries".

### CPS1 localized DIP-switch tables — separate high-value size candidate

`src/cps1/dipsw.c` contains many large localized static tables. The baseline
object contains approximately 516,800 B of `__DATA`, making this a major
binary-size target. The tables have a broader schema and runtime ownership than
game identity/name metadata, so they should be migrated only with a small,
robust loader that preserves all DIP semantics. They must not delay the
higher-confidence game-metadata wins above, but the measured size justifies a
dedicated milestone rather than merely documenting them.

### Tables that should remain compiled unless measurement justifies otherwise

Small fixed algorithm tables, hardware constants, input masks, cryptographic
permutation tables, and game-specific code paths that encode actual behavior
rather than declarative metadata should remain in source. The purpose is not to
turn every constant into file I/O.

## Runtime API direction

Introduce a small common metadata reader with bounded APIs rather than exposing
the on-disk layout throughout the emulator. The useful operations are:

- open/validate the current core metadata file;
- lookup a record by canonical set name;
- enumerate records when the browser or command-list reducer needs all games;
- return a language-aware title with English fallback;
- return browser display flags;
- expose typed core-specific accessors for CPS2 decryption/cache flags, MVS
  processed-asset ownership, and NCDZ NGH identity;
- close/free all temporary metadata before emulation when only browser data was
  needed.

The reader must not require the GUI. Core-specific metadata needed during game
initialization must work in both GUI and no-GUI builds.

Memory should stay proportional to the selected operation. In particular, the
browser may keep a compact index/string blob while browsing, but game startup
should not retain a large title database merely to access one CPS2 key or one
MVS ownership mask.

## Generator and validation

Add a host-side generator/validator under `tools/` and focused tests under
`tests/`.

Required validation:

1. every CPS1/CPS2/MVS `FILENAME(...)` identity appears exactly once in the
   canonical metadata source;
2. no canonical CPS1/CPS2/MVS identity exists outside the corresponding
   `rominfo` database;
3. names are lowercase, unique, non-empty, and fit the runtime canonical-name
   bound;
4. English titles are present for every browser-visible cartridge set;
5. localized titles are either present or deterministically fall back to
   English;
6. browser flags use only known values;
7. CPS2 encrypted sets that need a key have exactly one valid key record, while
   Phoenix/decrypted sets are classified explicitly;
8. CPS2 cache-parent overrides reference valid game identities;
9. MVS processed-asset ownership metadata references valid clones and agrees
   with `rominfo` parent relationships where applicable;
10. NCDZ NGH identifiers are unique except for intentionally documented
    sentinel/special cases;
11. generated files round-trip through a host-side parser and are identical on
    repeated generation;
12. malformed/truncated runtime fixtures are rejected by the C reader.

Generation should happen from CMake with explicit input dependencies. CI should
fail on stale or invalid canonical metadata rather than silently using a stale
checked-in generated artifact.

## Migration milestones

### M0 — audit and baseline

- [x] Branch from actual current validated HEAD.
- [x] Read repository instructions and existing runtime/size plans.
- [x] Map GUI, no-GUI, and NCDZ identity/name paths.
- [x] Confirm platform packaging behavior, including Vita copy-on-first-run.
- [x] Prove current catalog divergence with exact set comparisons.
- [x] Build Desktop GUI baselines for all four cores.
- [x] Record candidate object/section sizes for the main initial targets.

### M1 — canonical metadata source and deterministic generator

- [x] Define the source schema and versioned binary format.
- [x] Seed CPS1/CPS2/MVS titles and display flags without modifying
  `resources/`.
- [x] Correct known stale/missing identities in the new canonical source.
- [x] Add NCDZ NGH identity records.
- [x] Add generator validation against `rominfo` and NCDZ constraints.
- [x] Add deterministic-generation tests.

### M2 — replace GUI `zipname*` runtime dependency

- [x] Add the bounded common C metadata reader.
- [x] Switch CPS1/CPS2/MVS browser title/flag lookup to generated metadata.
- [x] Preserve language fallback and release bootleg filtering.
- [x] Ensure every supported set receives a friendly title.
- [x] Switch command-list reduction enumeration away from `zipname*`.
- [x] Make CMake build staging/install/Vita package the generated file.
- [x] Remove `zipname*` from the distributed runtime manifest without touching
  the legacy files under `resources/`.
- [x] Update README/runtime-file documentation.

The runtime reader validates magic/version/core, exact file bounds, sorted
unique records, string-pool offsets and NUL termination, reserved fields, and a
CRC32 over the generated body before exposing any record. Desktop tests cover
valid loads, wrong-core rejection, checksum corruption, and truncation.

The browser title path is now UTF-8 end-to-end. CMake includes the selected
core's canonical metadata source when generating the Unicode-to-font-glyph
lookup, so localized game names are covered by the same renderer as translated
UI text. CPS1's legacy Japanese middle-dot U+30FB is normalized to the visually
equivalent U+00B7 because the bundled GBK-derived font has no U+30FB glyph while
the Latin-1 renderer has U+00B7.

### M3 — CPS2 executable metadata

- [x] Move `keys_table[]` data into canonical/generated CPS2 metadata.
- [x] Replace the Phoenix/decrypted name chain with a metadata flag.
- [x] Replace cache-parent exception lists with explicit metadata.
- [x] Validate encrypted/Phoenix coverage against supported CPS2 sets.
- [x] Measure executable-size delta and startup I/O cost.

`memory_init()` now validates and looks up the selected CPS2 record once after
`rominfo` has established the normal parent. It copies only the selected
decryption key/range, Phoenix bit, and cache-parent policy, then immediately
unloads the 68,883-byte metadata file. The metadata allocation is therefore
transient and is gone before the large emulation-region allocations. Both GUI
and no-GUI startup use the same path.

`cps2_init_68k()` no longer scans a compiled game-name/key table. Encrypted
sets consume the selected key copied during `memory_init()`; Phoenix/decrypted
sets clear the key state. Driver initialization now fails rather than silently
starting an encrypted CPU region when no key was configured.

Representative metadata tests pin the historical `ssf2` key, `jyangoku`'s
zero/default upper range, a Phoenix set, the `ssf2ta -> ssf2t` streaming-cache
override, and `mpangj`'s independent-cache rule. The exhaustive generator
invariant additionally requires every supported CPS2 set to be exactly one of
keyed or Phoenix/decrypted.

Desktop object-size measurements against the pre-migration baseline:

| Object | Baseline | M3 | Delta |
| --- | ---: | ---: | ---: |
| `cps2crpt.c.o` | 18,231 B | 10,608 B | **-7,623 B** |
| `memintrf.c.o` | 14,855 B | 13,261 B | **-1,594 B** |
| Combined | 33,086 B | 23,869 B | **-9,217 B** |

Within `cps2crpt.c.o`, `__DATA` falls from 5,880 B to 16 B because the compiled
key table is gone. Whole Mach-O segment totals are unchanged at their coarse
page-rounded granularity, so the object delta is the useful Desktop size
measurement. Debug GUI, release GUI with streaming cache, and no-GUI with
streaming cache builds all succeed, and the metadata generator/reader tests
pass in the migrated build.

### M4 — MVS executable metadata

- [x] Move runtime `MVS_cacheinfo[]` ownership bits into generated metadata.
- [x] Make `romcnv_mvs` consume the same canonical source/generated policy.
- [x] Preserve mixed parent/clone C/S/V fallback behavior.
- [x] Measure executable-size delta.

The MVS emulator now loads the selected record after `rominfo` establishes the
normal parent and derives C/S/V processed-asset inheritance from the
`owns_crom`, `owns_srom`, and `owns_vrom` bits. The metadata allocation is
released immediately after those three booleans are copied, before large ROM
regions are allocated.

`romcnv_mvs` no longer embeds its own duplicate `MVS_cacheinfo[]`. Its CMake
build generates `game_metadata.mvs` from the same canonical TSV plus
`rominfo.mvs`, compiles the shared metadata reader, and loads that file once for
the conversion process. Native converter builds stage the generated file beside
the executable; the Emscripten configuration preloads it beside `rominfo.mvs`.
The emulator and converter generated blobs were compared byte-for-byte and are
identical.

Representative tests pin full C/S/V ownership, C-only ownership, V-only
ownership, and ordinary parent inheritance. The generator still requires exact
MVS identity parity with `rominfo.mvs`, so stale ownership aliases cannot be
reintroduced silently.

Desktop object-size measurements against the pre-migration baseline:

| Object(s) | Baseline | M4 | Delta |
| --- | ---: | ---: | ---: |
| emulator `driver.c.o` | 25,304 B | 20,762 B | **-4,542 B** |
| emulator `memintrf.c.o` | 32,477 B | 33,151 B | +674 B |
| emulator combined | 57,781 B | 53,913 B | **-3,868 B** |
| converter `romcnv.c.o` + metadata reader | 29,973 B | 27,239 B | **-2,734 B** |

The emulator `driver.c.o` data contribution falls from 11,826 B to 8,434 B.
The converter's combined data contribution falls from 13,172 B to 9,952 B.
The full 26-test MVS Desktop suite passes, along with release GUI and no-GUI
streaming-cache builds and a clean `romcnv_mvs` build.

### M5 — NCDZ executable identity metadata

- [x] Replace compiled `games[]` lookup with generated NGH identity metadata.
- [x] Remove index magic from BIOS and command-list paths.
- [x] Preserve all existing NGH-dependent driver behavior.
- [x] Measure executable-size delta.

Normal NCDZ game identification now reads the NGH value exactly as before and
resolves it through `game_metadata.ncdz`. The metadata blob is loaded only for
the lookup and released immediately afterward. Unknown NGH values preserve the
historical `default` identity; a missing/corrupt metadata file is reported as a
metadata error instead of being misreported as a missing `IPL.TXT`.

The compiled 100-slot `games[]` table, `GAMES` type, `game_index`, and the
`game_index == 99` BIOS sentinel are gone. BIOS reset behavior now checks the
existing `neogeo_boot_bios` state directly. Command-list reduction uses the
common generated-metadata enumeration path for NCDZ as well, eliminating the
hard-coded 97-game count.

Generator tests pin representative NGH values (`lastbld2` `0x0243` and
`fatfury3` `0x069c`) in addition to the exhaustive non-zero/unique NGH
invariant. The C reader test now exercises `game_metadata_find_ngh()` for NCDZ.

Desktop `driver.c.o` falls from 11,638 B to 10,291 B, a **1,347 B** executable
reduction. `ncdz.c.o` is unchanged. The generated metadata file is 5,077 B, but
that file was already part of the generated/distributed metadata set from M1,
so M5 adds no new packaged metadata relative to the post-M1 baseline. GUI with
command-list support and no-GUI builds pass, as does the full 22-test NCDZ
Desktop suite.

### M6 — remaining data audit

- [ ] Measure CPS1 localized DIP tables and other large static candidates.
- [ ] Classify each remaining table/list as behavior, small fixed data, or
  worthwhile external metadata.
- [ ] Externalize only candidates whose size/complexity tradeoff is positive.
- [ ] Record intentionally retained tables and why.

### M7 — final validation

- [ ] Run focused metadata generator/parser tests.
- [ ] Build GUI and no-GUI variants for all relevant cores on Desktop.
- [ ] Build PSP, PS2, and Vita matrices available in the local toolchains/CI
  configuration.
- [ ] Validate representative parent/clone browsing and boot paths.
- [ ] Validate CPS2 encrypted and Phoenix sets.
- [ ] Validate MVS mixed parent/clone processed-asset cases.
- [ ] Validate NCDZ NGH identification.
- [ ] Record before/after executable and packaged-metadata sizes.
- [ ] Update `RUNTIME_FILES_AUDIT.md`, `BINARY_SIZE_AUDIT.md`, README, and this
  plan with the final ownership model.

## Safety and compatibility rules

- Never modify, stage, or commit anything under `resources/`.
- Never stage generated ROM/cache/config/NVRAM/runtime files.
- Use explicit Git staging only.
- Do not change ROM CRC/layout semantics while migrating metadata.
- Do not make friendly titles part of save/config/NVRAM identity; canonical
  `game_name` remains the stable key.
- Missing or corrupt mandatory core metadata must fail with a clear error rather
  than continue with wrong decryption/cache behavior.
- Optional localized text may fall back to English.
- Keep runtime parsing endian- and ABI-independent for PSP, PS2, Desktop, and
  Vita.
- Prefer one small metadata read at game startup over permanently resident
  tables when the data is only needed for the selected game.
