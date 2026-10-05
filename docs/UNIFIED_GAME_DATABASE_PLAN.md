# Unified Game Database Plan

## Goal

Replace the current split runtime metadata model (`rominfo.<core>` plus
`game_metadata.<core>`) with one generated, indexed, versioned game database per
cartridge core. The first migration target is CPS2; CPS1 and MVS follow once the
format and runtime reader are proven.

The main motivations are:

- remove duplicated game identity across runtime files;
- eliminate runtime parsing of the large textual `rominfo.*` databases;
- reduce startup peak RAM and allocator pressure, especially on PSP;
- make ROM topology, titles and core-specific policy impossible to drift apart;
- keep the generated runtime representation compact and deterministic;
- give the emulator and ROM converters one authoritative runtime reader.

This work is intentionally deferred until the save/load UI regressions are fixed.

## Current State

For CPS2 today:

- `rominfo.cps2` is the runtime authority for parent relationships, machine/input/
  init/rotation selection, region sizes, ROM filenames, offsets, lengths, CRCs,
  ROM load type, and ROMX group/skip information;
- `metadata/cps2.tsv` is the canonical source for localized titles, display flags,
  CPS2 decryption keys/ranges, Phoenix status, and cache-parent policy;
- `game_metadata.cps2` is generated from `metadata/cps2.tsv` and consumed by the
  GUI, emulator and converter;
- both files duplicate the game/set identity and both are required at runtime;
- the current `rominfo` loader allocates and reads the complete text database before
  extracting the selected game.

The same architectural split exists for CPS1 and MVS, although their core-specific
metadata differs. NCDZ is already closer to the desired single-database model.

### CPS2 consumer inventory (audited 2026-10-03)

The pre-migration CPS2 runtime and tooling consumers are:

- `src/cps2/memintrf.c`:
  - `load_rom_info()` loads the complete `rominfo.cps2` text file and extracts
    parent identity, machine/input/init/rotation selectors, region sizes and ROM
    descriptors into the fixed target runtime arrays;
  - `configure_game_metadata()` separately loads `game_metadata.cps2` to resolve
    CPS2 decryption key/range, Phoenix status and cache-parent policy;
- `src/common/filer.c`: the GUI browser keeps the complete generated game metadata
  blob resident while resolving ROM filenames to localized titles/display flags;
- `src/common/cmdlist.c`: command-list size reduction loads the complete metadata
  blob and enumerates every canonical set name;
- `romcnv/src/cps2/romcnv.c`: the converter line-parses `rominfo.cps2` for parent and
  GFX1 topology, while separately loading `game_metadata.cps2` for cache-parent
  override/independent policy;
- top-level `CMakeLists.txt`: generates/packages `game_metadata.cps2`, separately
  distributes `rominfo.cps2`, and wires the metadata generator/reader tests;
- `romcnv/CMakeLists.txt`: separately stages/preloads `rominfo.cps2`, generates
  `game_metadata.cps2`, and compiles the shared metadata reader;
- the native host-tool metadata suite and `tests/game_metadata_reader_tests.c`:
  validate the current metadata format and representative CPS2 policy records;
- `docs/RUNTIME_FILES_AUDIT.md`, `resources/cps2/README.md`, the top-level README
  and converter README currently describe the two-file runtime contract.

No CPS2-specific CI workflow contains an independent hardcoded reference to either
filename; package validation is driven through the CMake install/package outputs.

## Non-Negotiable Design Requirements

1. **One packaged runtime database per migrated core.**
   `rominfo.<core>` and `game_metadata.<core>` must not both remain runtime
   requirements after migration.
2. **No whole-database allocation on game boot.**
   The reader must support bounded random access to one selected game.
3. **The source representation stays reviewable.**
   Do not replace maintainable source metadata with opaque generated binary data.
4. **ROM layout semantics must be lossless.**
   Parent inheritance, region sizes, ROM type, offsets, lengths, CRCs, ROMX group/
   skip and machine/input/init/rotation behavior must remain byte-for-byte semantic
   equivalents of the current loader.
5. **Core-specific metadata stays extensible without platform conditionals.**
   Common database/IO code belongs in `src/common`; target interpretation belongs
   in the target or generator.
6. **The emulator and `romcnv` use the same generated data and reader contract.**
7. **The format is explicitly versioned and checksummed.**
8. **No binary-size or RAM regression is accepted without measurement and a clear
   justification.**

## Proposed Source Model

Initially preserve the existing source authorities and merge them only at build
time:

- `rominfo.cps2`: ROM topology source;
- `metadata/cps2.tsv`: canonical title/display/crypto/cache-policy source.

Extend the generator to parse and validate both sources, then emit one runtime
database. This keeps the first migration focused on runtime architecture and avoids
mixing it with a large source-data rewrite.

After the runtime migration is stable, decide separately whether `rominfo.*` should
remain a build-time source or be converted to a more structured canonical source
format. That later source migration must preserve human reviewability and produce
the exact same generated database.

## Proposed Runtime Format

Use a sectioned binary format rather than expanding the current fixed-size game
metadata record. A conceptual layout is:

```text
header
  magic/version/core/checksum
  game_count
  offsets/counts for each section

game index (sorted by canonical set name)
  name/string offset
  parent game index or sentinel
  title offsets
  display/core flags
  machine/input/init/rotation values
  first region + region count
  core-specific payload reference

region records
  game index
  region type
  declared size
  first ROM + ROM count

ROM records
  filename/string offset or continuation sentinel
  load type
  offset
  length
  CRC
  group
  skip

core-specific records
  CPS2 key/range/Phoenix/cache-parent data
  future CPS1/MVS extensions

string pool
  set names, ROM filenames, localized titles and auxiliary names
```

Exact field widths must be selected from measured maxima rather than convenience.
Use indices/offsets instead of native pointers and encode all multibyte values with
an explicit endianness.

### CPS2 V1 measured schema (implementation start: 2026-10-03)

The first implementation audit measured the current CPS2 source as:

- 286 games;
- 1,387 region records;
- 5,382 ROM records;
- at most 5 regions per game and 50 ROM records per game;
- per-region maxima: CPU1 8, CPU2 3, GFX1 32, SOUND1 8, USER1 0 ROMs;
- region types: CPU1, CPU2, GFX1, SOUND1 and USER1 only;
- all current region flags are zero;
- ROM load types are 0, 1 and 2;
- ROMX groups are 1 or 2 and skips are 1, 6 or 7;
- the longest canonical game/parent name is 10 characters;
- the longest ROM filename is 13 characters;
- selector maxima are machine 0, input 10, init 1 and rotation 1;
- largest region size is `0x02000000`, largest ROM offset is `0x01000006`,
  and largest ROM length is `0x00800000`.

The V1 CPS2 generator also treats region uniqueness and the existing runtime ROM
array capacities as format-generation invariants: CPU1 <= 8, CPU2 <= 3,
GFX1 <= 32, SOUND1 <= 8 and USER1 == 0 ROM records. A future source change that
would exceed those runtime structures is rejected during generation rather than
producing a database that fails only when a game is launched.

Format V1 therefore uses explicit little-endian fixed records rather than native C
struct serialization:

- 64-byte header with magic/version/core, record sizes, section counts/offsets,
  total file size and a CRC32 of the complete body;
- 40-byte sorted game records with uint32 string offsets, uint16 parent/core
  indices, uint8 region count/flags, uint16 machine/input/init/rotation selectors,
  first-region index and four localized title offsets;
- 16-byte region records with uint16 game index, uint8 type/ROM count, uint32 size
  and first-ROM index, and uint16 source flags;
- 20-byte ROM records with uint32 filename/offset/length/CRC plus uint8 load type,
  group, skip and an explicit ROMX flag;
- 16-byte CPS2-specific records containing the two key words, range and auxiliary
  cache-parent string offset;
- one deduplicated UTF-8 NUL-terminated string pool.

The shared C host parser in `tools/host/rominfo.c` now normalizes the complete textual CPS2
topology and rejects malformed records, duplicate sets, unresolved parents and
parent cycles. `njemu-tool game-database` merges that topology with
`metadata/cps2.tsv`, enforces V1 field-width limits and existing CPS2 metadata
invariants, and emits deterministic `NJGD` V1 data. The generator parity test
decodes every generated game/region/ROM record and compares it against both source
models, including continuation records and ROMX semantics.

## Runtime Access Model

The runtime API must be designed around bounded IO:

1. open database and read/validate the small header;
2. locate a game through a compact sorted index;
3. read only that game's fixed record;
4. read only its region and ROM records;
5. copy the descriptors into the existing target runtime structures;
6. read only the strings/core payload actually needed;
7. close the database unless a measured use case justifies keeping a small handle
   or index resident.

Do not implement the new format by loading the whole generated file into memory;
that would preserve most of the current peak-RAM problem.

`src/common/game_database.c` now implements this access model for CPS2 V1. It
keeps only the open file handle plus section/count metadata. Normal runtime open
validates the header, record sizes and section arithmetic, then binary-searches the
sorted game records and reads only the selected game's strings/regions/ROMs. It
does not scan or allocate a copy of the database. Exhaustive body CRC32 plus sorted
index/linkage validation is available through `game_database_validate()` for tests
and diagnostics; its largest scratch buffer is a 4 KiB CRC chunk on the stack.
The open handle supplies its own fixed 512-byte stdio buffer, so normal database
I/O does not depend on an implementation-selected full-file or large stdio buffer.
Selected-game titles and names live in the caller-provided fixed-size game record.

The C reader test opens the real generated CPS2 database, walks all 286 games,
1,387 regions and 5,382 ROM records, checks representative keyed/Phoenix/cache and
ROMX/continuation cases, and verifies rejection of wrong-core, checksum-corrupt,
truncated, invalid-section and invalid-record-range files.

## Memory and Performance Targets

Before implementation, measure the current CPS2 baseline on Desktop and PSP-style
builds:

- `rominfo.cps2` file size;
- `game_metadata.cps2` file size;
- largest temporary allocation during metadata/ROM-info loading;
- total transient bytes allocated during the path;
- time from selected-game launch to completion of ROM-info/metadata parsing.

Initial file/allocation baseline from the validated pre-migration implementation:

- `rominfo.cps2`: **344,076 B**;
- generated `game_metadata.cps2`: **68,883 B**;
- combined packaged metadata: **412,959 B**;
- `load_rom_info()` allocates one buffer equal to the complete 344,076-byte
  `rominfo.cps2`, reads the file into it, extracts one game and frees it;
- `configure_game_metadata()` subsequently allocates the complete 68,883-byte
  metadata file through `game_metadata_load()` before looking up one game;
- the first generated unified CPS2 V1 database is **215,482 B**, 197,477 B smaller
  than the two current runtime files combined (about 47.8% smaller) before runtime
  cutover or further format optimization.

Wall-clock parsing/startup timing and real PSP allocator telemetry still need to be
captured during the runtime-reader migration; the figures above are structural file
and allocation measurements only.

Same-configuration Desktop no-GUI size comparison against validated `380b6a4`
(CPS2, GUI OFF, SAVE_STATE ON, COMMAND_LIST OFF) after the runtime cutover:

- executable file: 493,864 B -> 493,944 B (**+80 B**); Apple's Mach-O `size`
  reports identical aggregate segment sizes for both binaries;
- old `memintrf.c.o` + `game_metadata.c.o`: 17,160 decoded object bytes,
  including 12,912 B of text;
- new `memintrf.c.o` + `game_database.c.o`: 16,485 decoded object bytes,
  including 12,173 B of text;
- the migrated topology/metadata path therefore reduces those directly related
  object totals by **675 B** overall and **739 B** of text, while the linked file
  size remains effectively neutral.

This confirms the migration does not trade the runtime-file/RAM savings for a
meaningful executable-size increase.

Acceptance targets for CPS2:

- eliminate the current whole-`rominfo.cps2` allocation (currently hundreds of
  KiB);
- eliminate whole-`game_metadata.cps2` loading on the game-boot path;
- keep metadata lookup/read working set to a small bounded amount independent of
  total supported game count;
- materially reduce startup peak RAM and largest contiguous temporary allocation;
- do not increase steady-state emulation RAM except for unavoidable selected-game
  descriptors;
- do not make startup slower than the current implementation.

Record exact before/after measurements in `docs/BINARY_SIZE_AUDIT.md` or a dedicated
measurement section in this document when implementation begins.

## Validation Invariants

The generator must fail if any of these invariants break:

- every supported CPS2 `rominfo` set has exactly one canonical metadata record;
- every canonical CPS2 metadata record maps to exactly one ROM topology record;
- parent names resolve and parent graphs are acyclic;
- region/ROM ranges are representable in the selected binary field widths;
- all required ROM names and CRCs round-trip exactly;
- ROMX group/skip semantics round-trip exactly;
- every CPS2 set is either keyed or Phoenix according to current policy;
- cache-parent override/independent references resolve;
- game index sorting and offsets are deterministic;
- generated output is byte-for-byte reproducible from the same inputs.

Add parity tests that parse the legacy source and the generated database and compare
the complete selected-game semantic model for every CPS2 set, not just a sample.

## Migration Phases

### U1 - Baseline and schema inventory

**Status: COMPLETE (2026-10-03).**

- measure current CPS2 file sizes, transient allocations and parsing time;
- enumerate every field used from `rominfo.cps2` by emulator and converter;
- document maxima/counts needed to choose compact field widths;
- add tests for legacy parsing semantics before changing the runtime.

### U2 - Build-time ROM-info parser

**Status: COMPLETE (2026-10-03).**

- implement a deterministic host-side parser for `rominfo.cps2`;
- normalize it into an intermediate model;
- validate parent/region/ROM relationships and field ranges;
- compare the model against current runtime behavior.

### U3 - Unified binary generator

**Status: COMPLETE (2026-10-03).**

- define format V1 and common serialization helpers;
- merge the ROM topology model with `metadata/cps2.tsv`;
- emit one CPS2 database plus optional human-readable diagnostics/gamelist;
- add corruption, truncation, version, checksum and deterministic-output tests.

### U4 - Random-access common reader

**Status: COMPLETE (2026-10-03).**

- implement the common header/index reader without whole-file allocation;
- expose lookup/read APIs that return one selected game's metadata and variable ROM
  records;
- add bounded-memory tests and malformed-offset/range rejection.

### U5 - CPS2 emulator migration

**Status: COMPLETE (2026-10-03) for code migration and Desktop validation.**

- replace `load_rom_info()` text parsing with the new reader;
- replace separate CPS2 `game_metadata` game-boot loading with the unified record;
- preserve existing target runtime arrays and ROM-loading behavior initially;
- validate representative parent/clone, ROMX, Phoenix, keyed, cache-parent and
  independent-cache sets.

### U6 - CPS2 converter migration

**Status: COMPLETE (2026-10-03) for code migration and Desktop cache parity.**

- make `romcnv_cps2` consume the same unified database;
- remove its textual `rominfo.cps2` parser and separate game-metadata dependency;
- compare generated cache outputs against the current implementation.

### U7 - Packaging cutover

**Status: COMPLETE (2026-10-03) for build/install logic and CI package guards.**

- package only the unified CPS2 database;
- remove runtime/install dependency on `rominfo.cps2` and `game_metadata.cps2`;
- update READMEs, runtime-file audit and CI package guards;
- keep source `rominfo.cps2` only if it remains a generator input.

**Implemented for CPS2 on 2026-10-03.** The emulator startup path, GUI browser,
command-list reduction, and `romcnv_cps2` now consume `game_database.cps2` through
the bounded reader. The converter no longer contains its textual CPS2 ROM-info
parser or standalone metadata dependency. Top-level and converter CMake generate
the unified database directly from `metadata/cps2.tsv` + tracked
`resources/cps2/rominfo.cps2`; CPS2 install/package manifests no longer distribute
either `rominfo.cps2` or `game_metadata.cps2`. Desktop install validation produced
only `game_database.cps2` for CPS2 metadata, and the Desktop/PSP/PS2/Vita CI
package guards now explicitly enforce that contract.

Current bounded-memory impact on these migrated paths:

- the 344,076-byte whole-`rominfo.cps2` heap allocation is eliminated;
- the 68,883-byte whole-`game_metadata.cps2` heap allocation is eliminated from
  CPS2 game boot, GUI browsing and command-list reduction;
- the runtime open path performs no full-body scan and no database-sized heap
  allocation; its explicit persistent I/O buffer is 512 B, while exhaustive
  optional validation uses only a 4 KiB stack CRC buffer;
- the fixed selected-game payload is 592 B; region and ROM records materialize as
  16 B and 52 B respectively, one at a time, rather than as database-sized arrays;
- browser persistence is an open `FILE *` plus compact section/count state rather
  than a resident metadata blob;
- packaged CPS2 metadata falls from 412,959 B to 215,482 B, a reduction of
  197,477 B (47.8%).

Still outstanding before U8 can be declared complete: real PSP allocator/startup
telemetry, PSP/PPSSPP smoke coverage, PS2/PCSX2 build/runtime smoke coverage, and
Vita build/package validation.

Desktop runtime/converter validation completed after that cutover:

- a clean GUI/command-list CPS2 build passes all 24 configured Desktop tests;
- a separate no-GUI install containing `game_database.cps2` and no legacy CPS2
  metadata files booted the local `ssf2` ROM through ROM loading, full GFX decode,
  keyed decryption to 100%, and entry into the emulation loop;
- the migrated converter regenerated the local `ssf2.cache` as exactly
  12,058,624 B with SHA-256
  `47e12d3de06ad7d84b7ac7f3730276c559ea04eb0d08f1a531df1caf9ec03b39`,
  byte-for-byte identical to the existing validated cache;
- clone/parent coverage with local `ssf2tu.zip` + `ssf2t.zip` regenerated
  `ssf2tu.cache` with SHA-256
  `690e614190de7e7e939eb7afc9abaf110247daab3c83f8b5a748f359a47b1055`, also
  byte-for-byte identical to the existing validated cache.

The current local shell does not expose PSPDEV/PS2SDK toolchains, so PSP and PS2
compile/install/runtime validation remains for CI or an SDK-enabled environment;
the package guards for those matrices have nevertheless been updated to require
the new CPS2 database and reject both legacy runtime files.

### U8 - Cross-platform CPS2 validation

**Status: IN PROGRESS.** Desktop is complete; PSP/PS2/Vita validation requires
toolchains/runtimes that are not available in the current local shell.

- Desktop: full tests plus real ROM boot coverage;
- PSP: PPSSPP smoke tests plus real-hardware memory/startup measurements;
- PS2: PCSX2 boot/cache coverage;
- Vita: CI build/package validation and hardware/emulator validation when available;
- verify GUI browser, command list, save states and no-GUI paths are unchanged.

### U9 - CPS1 migration

- generalize only proven CPS2 abstractions;
- migrate CPS1 ROM topology and canonical metadata into the same format;
- preserve CPS1-specific machine/init behavior and DIP metadata as a separate asset
  unless there is a demonstrated reason to fold it in.

### U10 - MVS migration

- migrate MVS ROM topology plus existing ownership/cache metadata;
- preserve high-memory/no-cache and parent ownership behavior;
- compare converter cache output and runtime inheritance for all sets.

### U11 - Source-format decision

- once all three cartridge cores use the unified runtime database, evaluate whether
  legacy `rominfo.*` should remain as generator inputs;
- if replaced, migrate to a structured canonical source with exact semantic parity;
- remove legacy parsers only after exhaustive generated-model comparison succeeds.

## Completion Criteria

The project is complete when:

- CPS1, CPS2 and MVS each package one unified game database rather than separate
  `rominfo.*` and `game_metadata.*` runtime files;
- neither emulator nor converter contains a runtime textual `rominfo` parser;
- game lookup and selected-game ROM topology are random-access and bounded-memory;
- exhaustive parity tests cover every supported set;
- PSP peak startup RAM/largest temporary allocation are measured and improved;
- Desktop/PSP/PS2/Vita CI is green;
- representative PPSSPP, PCSX2 and real PSP runtime tests pass;
- documentation and package guards describe the final ownership accurately.
