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

## Memory and Performance Targets

Before implementation, measure the current CPS2 baseline on Desktop and PSP-style
builds:

- `rominfo.cps2` file size;
- `game_metadata.cps2` file size;
- largest temporary allocation during metadata/ROM-info loading;
- total transient bytes allocated during the path;
- time from selected-game launch to completion of ROM-info/metadata parsing.

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

- measure current CPS2 file sizes, transient allocations and parsing time;
- enumerate every field used from `rominfo.cps2` by emulator and converter;
- document maxima/counts needed to choose compact field widths;
- add tests for legacy parsing semantics before changing the runtime.

### U2 - Build-time ROM-info parser

- implement a deterministic host-side parser for `rominfo.cps2`;
- normalize it into an intermediate model;
- validate parent/region/ROM relationships and field ranges;
- compare the model against current runtime behavior.

### U3 - Unified binary generator

- define format V1 and common serialization helpers;
- merge the ROM topology model with `metadata/cps2.tsv`;
- emit one CPS2 database plus optional human-readable diagnostics/gamelist;
- add corruption, truncation, version, checksum and deterministic-output tests.

### U4 - Random-access common reader

- implement the common header/index reader without whole-file allocation;
- expose lookup/read APIs that return one selected game's metadata and variable ROM
  records;
- add bounded-memory tests and malformed-offset/range rejection.

### U5 - CPS2 emulator migration

- replace `load_rom_info()` text parsing with the new reader;
- replace separate CPS2 `game_metadata` game-boot loading with the unified record;
- preserve existing target runtime arrays and ROM-loading behavior initially;
- validate representative parent/clone, ROMX, Phoenix, keyed, cache-parent and
  independent-cache sets.

### U6 - CPS2 converter migration

- make `romcnv_cps2` consume the same unified database;
- remove its textual `rominfo.cps2` parser and separate game-metadata dependency;
- compare generated cache outputs against the current implementation.

### U7 - Packaging cutover

- package only the unified CPS2 database;
- remove runtime/install dependency on `rominfo.cps2` and `game_metadata.cps2`;
- update READMEs, runtime-file audit and CI package guards;
- keep source `rominfo.cps2` only if it remains a generator input.

### U8 - Cross-platform CPS2 validation

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
