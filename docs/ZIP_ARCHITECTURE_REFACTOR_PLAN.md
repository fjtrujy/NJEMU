# ZIP and Resource I/O Architecture Refactor Plan

Date: 2026-09-26
Status: Z0-Z1 complete; Z2 next

## Purpose

NJEMU now uses external upstream miniz 3.1.2 for ZIP support on Desktop, PS2 and PSP, and the old embedded MiniZip sources have been removed. The remaining technical debt is no longer the ZIP library itself: it is the historical `zfile` abstraction built around it.

The current API intentionally makes ZIP entries and normal files look alike through `zip_open()`, `zopen()`, `zread()`, `zclose()`, `zsize()` and `zlength()`. This works, but it encodes backend selection in global state and gives integer pseudo-handles semantics that differ depending on whether the active source is a ZIP archive or a directory.

This plan replaces that implicit model with small explicit C abstractions while preserving all current supported input formats and runtime behavior.

`docs/ZIP_MINIZ_EVALUATION.md` remains the authoritative record for the miniz migration, binary-size measurements and package/toolchain conclusions. This document is authoritative for the architectural cleanup that follows it.

## Goals

1. Make ZIP archives explicit objects rather than process-global hidden state.
2. Make ZIP entries explicit objects rather than fake `int64_t` file descriptors.
3. Keep normal filesystem I/O as normal filesystem I/O where no abstraction is needed.
4. Introduce a ZIP-or-directory abstraction only where the domain genuinely needs it, principally NCDZ.
5. Remove the legacy `zopen` / `zread` / `zgetc` / `zclose` / `zsize` / `zlength` API.
6. Remove `zip_findfirst` / `zip_findnext` and their global iterator state.
7. Avoid constructing miniz extractors when only metadata such as size or CRC is required.
8. Remove unnecessary global mutable ZIP state and make ownership/lifetime explicit.
9. Simplify `romcnv` now that it no longer has to emulate the old MiniZip API.
10. Preserve streaming behavior and low peak-memory usage on PSP and PS2.
11. Preserve all currently supported ROM/cache/CD layouts and filename lookup behavior.
12. Keep the code C-only and suitable for the current portability targets.

## Non-goals

- Replacing miniz with another ZIP library.
- Changing ROM, cache or NCDZ on-disk formats.
- Removing folder/raw cache formats.
- Introducing a generic virtual filesystem framework for the whole emulator.
- Adding C++ abstractions or opaque heap-owned object hierarchies.
- Solving the PS2/PSP miniz package-size optimization in this refactor. That remains a toolchain/package concern documented in `ZIP_MINIZ_EVALUATION.md`.
- Touching anything under `resources/`.

## Current architecture and problems

### Global archive state

`src/zip/zfile.c` currently owns one global `mz_zip_archive`, one global extraction iterator, one current `mz_zip_archive_file_stat`, one enumeration index, one 4 KiB byte cache and one streamed-length counter.

Consequences:

- only one archive can be active;
- only one ZIP entry can be open;
- opening another entry implicitly closes the previous iterator;
- `zclose(fd)` and `zsize(fd)` ignore the supplied handle in ZIP mode;
- an archive entry is represented externally by the integer value `0`, not by an object that identifies that entry;
- correctness depends on call ordering and hidden process-global state.

### ZIP-open failure doubles as directory initialization

`zip_open(path)` first attempts `mz_zip_reader_init_file()`. If that fails it stores `path` as `basedir` and returns `-1`. Calls to `zopen(name)` then use POSIX `open()` below that directory.

Therefore `zip_open() == -1` can mean either:

- a genuine failure to open a ZIP; or
- a deliberately initialized directory backend that is expected to work through later `zopen()` calls.

NCDZ relies on this behavior. It is semantically ambiguous and should be removed.

### Fake descriptor type

`int64_t` was historically introduced so the same API could carry either a native descriptor or a pointer-derived ZIP pseudo-handle. Miniz removed the pointer cast, but the public type remains. In archive mode the value is just a success sentinel.

### Metadata performs unnecessary extraction setup

NCDZ `zlength()` currently calls `zopen()` + `zsize()` + `zclose()`. For ZIP entries this creates an extraction iterator even though only central-directory metadata is required.

### Archive enumeration exists for one domain operation

`zip_findfirst()` / `zip_findnext()` are effectively only used by the ROM loader to locate an entry by CRC. The generic global enumeration API is larger and more stateful than the real operation requires.

### Cache code already knows its backend

MVS/CPS2 cache handling explicitly distinguishes `CACHE_RAWFILE`, `CACHE_ZIPFILE` and `CACHE_FOLDER`, yet ZIP/folder unification still leaks through the historical `zfile` API in some paths. No generic source abstraction is required there.

### ROM converter still emulates old writer semantics

`romcnv` currently preserves `zopen()` / `zwrite()` / `zclose()` for ZIP output by staging each entry in `tmpfile()` and feeding it to `mz_zip_writer_add_cfile()`. This is correct and memory-safe, but is now compatibility machinery for an API that no longer has another implementation behind it.

## Architectural rules

The refactor must follow these rules:

- Prefer explicit value/state ownership over hidden globals.
- Do not build a generic VFS layer unless a concrete caller needs multiple backends.
- A function named `zip_*` must operate on a ZIP archive or entry, never silently fall back to a directory.
- Filesystem-only paths should keep using direct POSIX/file APIs unless a domain abstraction materially improves the caller.
- Metadata queries must not allocate or initialize an extraction stream.
- Opening an archive entry must not implicitly close another unrelated entry.
- Functions should return explicit success/failure and use output parameters for objects or metadata.
- Keep structs concrete where practical; avoid heap allocation for small owner objects when stack/static ownership suffices.
- Preserve case-insensitive ZIP filename lookup currently provided by `mz_zip_reader_locate_file(..., 0)`.
- Preserve CRC validation behavior for fully consumed entries.
- Preserve streaming extraction: do not inflate whole ROMs or CD files into temporary buffers solely for API convenience.

## Target ZIP API

The exact names may change during implementation, but the intended ownership model is:

```c
typedef struct zip_archive_t {
    mz_zip_archive archive;
    int is_open;
} zip_archive_t;

typedef struct zip_entry_t {
    mz_zip_reader_extract_iter_state *reader;
    uint64_t size;
    uint64_t bytes_read;
    uint32_t crc32;
    unsigned char byte_cache[4096];
    size_t byte_cache_pos;
    size_t byte_cache_len;
} zip_entry_t;

typedef struct zip_entry_info_t {
    char name[PATH_MAX];
    uint64_t size;
    uint32_t crc32;
} zip_entry_info_t;
```

Representative operations:

```c
bool zip_archive_open(zip_archive_t *archive, const char *path);
void zip_archive_close(zip_archive_t *archive);

bool zip_archive_stat(zip_archive_t *archive,
                      const char *name,
                      zip_entry_info_t *info);

bool zip_archive_find_crc(zip_archive_t *archive,
                          uint32_t crc32,
                          zip_entry_info_t *info);

bool zip_entry_open(zip_archive_t *archive,
                    const char *name,
                    zip_entry_t *entry);

size_t zip_entry_read(zip_entry_t *entry, void *dst, size_t size);
int zip_entry_getc(zip_entry_t *entry);
bool zip_entry_close(zip_entry_t *entry);
```

Requirements:

- `zip_archive_t` owns the miniz archive lifetime.
- `zip_entry_t` owns the extraction iterator lifetime.
- `zip_entry_info_t` is metadata only and must not require opening an extractor.
- no integer pseudo-handle crosses this API;
- archive enumeration remains private unless another real caller needs it.

If miniz imposes a limitation on simultaneous iterators from one archive, model that limitation explicitly rather than reproducing silent implicit closing behavior.

## Target NCDZ resource-source API

NCDZ legitimately supports the same logical CD files from either a directory or a ZIP. That is the appropriate place for a small backend abstraction.

Representative model:

```c
typedef enum resource_source_type_t {
    RESOURCE_SOURCE_DIRECTORY,
    RESOURCE_SOURCE_ZIP
} resource_source_type_t;

typedef struct resource_source_t {
    resource_source_type_t type;
    union {
        char directory[PATH_MAX];
        zip_archive_t zip;
    } backend;
} resource_source_t;
```

Representative operations:

```c
bool resource_source_open_directory(resource_source_t *source,
                                    const char *path);

bool resource_source_open_zip(resource_source_t *source,
                              const char *path);

void resource_source_close(resource_source_t *source);

bool resource_source_stat(resource_source_t *source,
                          const char *name,
                          resource_file_info_t *info);

bool resource_file_open(resource_source_t *source,
                        const char *name,
                        resource_file_t *file);

size_t resource_file_read(resource_file_t *file,
                          void *dst,
                          size_t size);

void resource_file_close(resource_file_t *file);
```

Important: source type must be selected explicitly. There must be no "try ZIP and treat failure as a directory" behavior.

## Phases

### Z0 — Baseline and invariants

Before structural changes:

- record the current callers of every `zfile` API;
- record current Desktop smoke ROMs for CPS1, CPS2, MVS and NCDZ;
- keep the existing `romcnv` producer/consumer tests established during the miniz migration;
- record PS2 and PSP MVS build success as cross-build gates;
- record current ELF/PBP sizes so the abstraction cleanup does not accidentally introduce material growth;
- add focused unit tests where practical for ZIP metadata, CRC lookup and directory-vs-ZIP source semantics.

No `resources/` files are to be modified by tests. Temporary generated archives must live under build directories or `/tmp`.

Exit criteria:

- baseline documented;
- tests can distinguish metadata lookup, stream reading and source selection.

#### Z0 recorded baseline

Baseline commit: `0ff1712` (`Plan ZIP architecture refactor`), rebuilt on
2026-09-26 before the Z1 source changes. All builds below use Release,
`GUI=OFF`, `COMMAND_LIST=OFF`, `SAVE_STATE=ON` and `ADHOC=OFF`.

Runtime legacy API ownership at this point is:

- `src/common/loadrom.c`: archive open/close, global enumeration,
  entry open/read/getc/close; this is the only runtime consumer of
  `zip_findfirst()` / `zip_findnext()`;
- `src/common/cache.c`: ZIP-cache archive and entry operations;
- `src/common/filer.c`: NCDZ/title ZIP probing and entry reads;
- `src/ncdz/cdrom.c` and `src/ncdz/driver.c`: mixed ZIP/directory resource
  reads and `zlength()`;
- `src/mvs/memintrf.c` and `src/mvs/biosmenu.c`: residual ZIP/cache entry
  reads and cleanup;
- `romcnv/src/zfile.c`: a separate converter compatibility API, intentionally
  deferred to Z6/Z7.

Cross-build gates pass for Desktop CPS1/CPS2/MVS/NCDZ, PS2 MVS and PSP MVS.
The established real-data smoke set remains CPS1 `ghoulsu`, CPS2 `mpangu`,
MVS `pbobbl2n` and NCDZ `Windjammers` (directory source). The miniz
producer/consumer baseline remains the `pbobbl2n` and `mpangu` conversions
recorded in `docs/ZIP_MINIZ_EVALUATION.md`.

Pre-Z1 binary measurements:

| Build | Baseline size |
| --- | ---: |
| Desktop CPS1 executable | 990,008 B |
| Desktop CPS2 executable | 367,656 B |
| Desktop MVS executable | 474,768 B |
| Desktop NCDZ executable | 402,856 B |
| PS2 MVS `.text` | 891,040 B |
| PS2 MVS `.data` | 391,060 B |
| PS2 MVS `.bss` | 2,251,848 B |
| PS2 MVS `text+data+bss` | 3,533,948 B |
| PS2 MVS ELF file | 3,277,680 B |
| PSP MVS `.text` | 768,904 B |
| PSP MVS `.data` | 13,204 B |
| PSP MVS `.bss` | 1,944,080 B |
| PSP MVS `text+data+bss` | 2,726,188 B |
| PSP MVS ELF file | 2,444,212 B |
| PSP MVS PRX | 914,874 B |
| PSP MVS EBOOT.PBP | 915,250 B |

The Release CTest baseline has the pre-existing `memory_plan_tests`
NDEBUG abort documented by the miniz/memory work; it is unrelated to ZIP.
Z1 adds a focused ZIP test using a generated archive under the build
directory, never under `resources/`.

### Z1 — Explicit ZIP archive and entry objects

Introduce the new ZIP-only API around miniz.

Tasks:

- move global `mz_zip_archive` state into `zip_archive_t`;
- move extraction iterator, file stat, streamed length and byte cache into `zip_entry_t`;
- implement explicit archive open/close;
- implement entry open/read/getc/close;
- implement metadata-only stat by filename;
- implement CRC lookup without exposing a generic global iterator;
- preserve case-insensitive filename lookup;
- preserve close-time CRC behavior for fully consumed streams;
- add tests for opening/closing multiple sequential entries without hidden state leakage.

Do not migrate all callers in the same commit if it obscures review. A temporary compatibility adapter is acceptable within this phase only if it is small and immediately removed in later phases.

Exit criteria:

- ZIP implementation itself contains no process-global archive/entry/iterator state;
- metadata lookup does not create an extraction iterator;
- ZIP code has no directory fallback.

#### Z1 implementation result

Z1 introduces `src/zip/zip_archive.h/.c` with explicit `zip_archive_t`,
`zip_entry_t` and metadata-only `zip_entry_info_t` values. The public ZIP-only
operations are `zip_archive_open()`, `zip_archive_close()`,
`zip_archive_stat()`, `zip_archive_find_crc()`, `zip_entry_open()`,
`zip_entry_read()`, `zip_entry_getc()` and `zip_entry_close()`.

The ZIP core now has no process-global archive, entry or enumeration state.
The former global `mz_zip_archive`, extraction iterator,
`mz_zip_archive_file_stat`, streamed-length counter and ZIP byte-cache state
have moved into explicit owner objects. CRC lookup enumerates the central
directory privately, filename lookup remains case-insensitive through miniz
flags `0`, stream reads remain incremental, and fully consumed entries retain
the miniz close-time CRC result.

`src/zip/zfile.c` remains as an explicitly transitional Z2-Z5 adapter. It owns
one `legacy_archive`, one `legacy_entry`, one `legacy_find_index` and the
legacy `basedir` state so existing callers keep their observable behavior.
Only this adapter still implements the ambiguous ZIP-or-directory fallback,
the integer success pseudo-handle and `zip_findfirst()` / `zip_findnext()`.
For NCDZ ZIP sources, `zlength()` now uses `zip_archive_stat()` and therefore
does not create an extraction iterator just to obtain the entry size.

`zip_archive_tests` generates its archive in the build directory and verifies
case-insensitive metadata lookup, CRC lookup, rejection of directory paths,
two simultaneous entry iterators, sequential entry reuse, streaming reads and
the per-entry `getc` cache. The final validation matrix is:

- Desktop Release builds: CPS1, CPS2, MVS and NCDZ pass;
- Desktop CTest: 10/10 pass when excluding only the previously documented
  Release/NDEBUG `memory_plan_tests` abort; the ZIP test itself passes;
- real 30-frame smokes: `ghoulsu`, `mpangu`, `pbobbl2n` and NCDZ
  `Windjammers` directory source all exit successfully;
- PS2 MVS Release cross-build passes;
- PSP MVS Release cross-build passes and produces `EBOOT.PBP`.

Final size deltas versus the Z0 baseline are small and explained by the extra
explicit API code. The adapter reuses `zip_entry_t`'s 4 KiB byte cache for the
legacy directory path instead of retaining a second buffer, so console BSS is
slightly smaller than before Z1.

| Build | Z0 | Z1 | Delta |
| --- | ---: | ---: | ---: |
| Desktop CPS1 executable | 990,008 B | 990,552 B | +544 B |
| Desktop CPS2 executable | 367,656 B | 368,200 B | +544 B |
| Desktop MVS executable | 474,768 B | 475,344 B | +576 B |
| Desktop NCDZ executable | 402,856 B | 403,400 B | +544 B |
| PS2 MVS `.text` | 891,040 B | 892,672 B | +1,632 B |
| PS2 MVS `.bss` | 2,251,848 B | 2,250,760 B | -1,088 B |
| PS2 MVS `text+data+bss` | 3,533,948 B | 3,534,492 B | +544 B |
| PS2 MVS ELF file | 3,277,680 B | 3,279,432 B | +1,752 B |
| PSP MVS `.text` | 768,904 B | 770,640 B | +1,736 B |
| PSP MVS `.bss` | 1,944,080 B | 1,943,040 B | -1,040 B |
| PSP MVS `text+data+bss` | 2,726,188 B | 2,726,884 B | +696 B |
| PSP MVS ELF file | 2,444,212 B | 2,446,068 B | +1,856 B |
| PSP MVS PRX | 914,874 B | 916,938 B | +2,064 B |
| PSP MVS EBOOT.PBP | 915,250 B | 917,314 B | +2,064 B |

### Z2 — ROM loader migration

Migrate `src/common/loadrom.c` to the explicit ZIP API.

Tasks:

- make the currently active ROM archive an explicit owner;
- replace `zip_findfirst()` / `zip_findnext()` with direct CRC lookup;
- preserve parent/clone fallback order;
- preserve fallback-by-name used to distinguish CRC mismatch (`-2`) from not found (`-1`), while replacing magic return values with named result values if practical;
- remove `rom_fd` pseudo-descriptor semantics and use a real ZIP entry object;
- keep `file_read()` / `file_getc()` only if they remain useful semantic loader helpers; otherwise reduce them to direct entry operations.

Potential result enum:

```c
typedef enum rom_file_open_result_t {
    ROM_FILE_OPEN_OK,
    ROM_FILE_OPEN_NOT_FOUND,
    ROM_FILE_OPEN_CRC_MISMATCH
} rom_file_open_result_t;
```

Exit criteria:

- ROM loading never uses `zopen`/`zread`/`zgetc`/`zclose`;
- `zip_findfirst`/`zip_findnext` no longer have a consumer;
- CPS1/CPS2/MVS real-ROM smokes pass.

### Z3 — MVS/CPS2 cache migration

Make the existing cache backend distinctions explicit.

Tasks:

- `CACHE_RAWFILE`: keep direct filesystem descriptor logic;
- `CACHE_FOLDER`: keep direct filesystem paths;
- `CACHE_ZIPFILE`: own a `zip_archive_t` and open `zip_entry_t` objects explicitly;
- replace `cache_fd` overload where it currently means either POSIX fd or ZIP pseudo-handle;
- replace ZIP `cache_info` reads with explicit entry objects;
- preserve suspend/resume behavior by explicitly closing and reopening the ZIP archive;
- ensure per-block ZIP reads do not leak entry iterator state;
- preserve MVS parent-cache fallback behavior.

Exit criteria:

- cache code never relies on directory fallback inside ZIP code;
- backend type determines the operations directly;
- MVS/CPS2 raw, folder and ZIP cache formats remain supported;
- caches generated by `romcnv` still load correctly.

### Z4 — NCDZ resource source

Introduce the small explicit ZIP-or-directory abstraction for NCDZ.

Tasks:

- identify source type in `filer.c` when the user selects the game;
- construct `game_dir`/source state without requiring later ZIP probing;
- migrate `cdrom.c`, `driver.c` and title/`IPL.TXT` paths to `resource_source_t`;
- replace `zlength()` calls with `resource_source_stat()`;
- for directory sources use `stat()`/filesystem metadata rather than opening and seeking where possible;
- for ZIP sources use `mz_zip_reader_file_stat()` without creating an extraction iterator;
- preserve ZIP-packaged NCDZ and directory-based NCDZ behavior;
- preserve case-insensitive ZIP entry lookup where required by existing CD images.

Exit criteria:

- no caller ignores a failed `zip_open()` as a way of initializing a directory;
- `zlength()` is gone;
- NCDZ `Windjammers` directory smoke passes;
- at least one ZIP-packaged NCDZ path is validated if a suitable test asset is available without modifying resources.

### Z5 — Remaining runtime callers and API deletion

Migrate `filer.c`, MVS memory/cache helper paths, BIOS/menu cleanup and any residual callers.

Then remove:

- `zip_open()` legacy signature;
- `zip_close()` global form;
- `zip_findfirst()`;
- `zip_findnext()`;
- `zopen()`;
- `zread()`;
- `zgetc()`;
- `zclose()`;
- `zsize()`;
- `zlength()`;
- `struct zip_find_t`;
- historical `int64_t` pseudo-handle plumbing.

Rename/move `src/zip/zfile.c/.h` if the remaining responsibilities are better represented by e.g. `src/common/zip_archive.c/.h`.

Exit criteria:

```text
$ git grep -E '\b(zopen|zread|zgetc|zclose|zsize|zlength|zip_findfirst|zip_findnext)\b'
```

returns no active runtime code references.

### Z6 — ROM converter reader cleanup

Migrate the input side of `romcnv` to the same conceptual ZIP archive/entry model where practical.

The runtime and converter do not have to share a source file if doing so would couple their build environments unnecessarily, but they should share the same ownership principles and naming.

Tasks:

- remove converter reader pseudo-handles;
- replace global entry state;
- replace generic enumeration with the exact metadata/lookup operations converter callers need;
- retain streaming extraction and CRC behavior.

Exit criteria:

- converter read side contains no `zopen`/`zread` fake descriptor API.

### Z7 — ROM converter writer cleanup

Remove the compatibility writer based on `tmpfile()` where the source buffers are already directly available.

Tasks:

- introduce an explicit `zip_writer_t` owner around `mz_zip_archive`;
- use `mz_zip_writer_add_mem()` / `mz_zip_writer_add_mem_ex*()` for blocks and already-materialized buffers such as GFX/cache data;
- design `cache_info` output without introducing a large aggregate buffer. A small fixed metadata buffer or a bounded segmented callback is acceptable;
- use the current compression level unless measurement shows a reason to change it;
- ensure writer failure is propagated instead of silently ignored by existing `zwrite()` callers;
- finalize/end the archive explicitly and report failure.

Important: do not replace `tmpfile()` with a new large heap buffer simply to preserve old call structure.

Exit criteria:

- `romcnv` no longer uses `zopen`/`zwrite`/`zclose`;
- `tmpfile()` is gone from ZIP generation unless a specific large-data case proves it is still the lowest-memory solution;
- real MVS and CPS2 cache ZIP generation passes `unzip -t`;
- NJEMU consumes those generated ZIPs successfully.

### Z8 — Naming and source layout cleanup

After all callers are migrated:

- remove historical `zfile` terminology;
- choose source locations based on actual scope, e.g. `src/common/zip_archive.*` and `src/ncdz/resource_source.*`;
- keep miniz include/build requirements local to ZIP implementation translation units;
- update comments and documentation that still describe the old combined ZIP/file abstraction;
- ensure headers expose only domain-level types required by callers.

Exit criteria:

- filenames and APIs describe actual semantics;
- no comments refer to MiniZip-era pseudo-handles or transparent ZIP/directory fallback.

### Z9 — Full validation and measurements

Run the complete validation matrix after the old API is removed.

Required Desktop builds:

- CPS1;
- CPS2;
- MVS;
- NCDZ;
- `romcnv_mvs`;
- `romcnv_cps2`.

Required real-data smokes:

- CPS1: `ghoulsu` or current equivalent baseline;
- CPS2: `mpangu`;
- MVS: `pbobbl2n` or another established cache test;
- NCDZ: `Windjammers` directory source;
- ZIP-packaged NCDZ where practical;
- `romcnv_mvs` creates a ZIP cache which passes `unzip -t` and is consumed by NJEMU;
- `romcnv_cps2` does the same.

Required console builds:

- PS2 MVS with the established release flags;
- PSP MVS with the established release flags;
- broader targets if changed headers/source layout affect them in a platform-specific way.

Run CTest using the repository's known Release/NDEBUG exclusions only where still required. Do not hide new failures behind the existing `memory_plan_tests` issue.

Measure before/after:

- Desktop executable sizes for representative targets;
- PS2 MVS ELF `text/data/bss`;
- PSP MVS ELF/PBP size;
- ZIP-related symbols retained in PS2/PSP;
- optional runtime memory measurements if the explicit object model changes iterator lifetime materially.

Exit criteria:

- no functional regressions;
- no material unexplained binary-size increase;
- no legacy ZIP/file API remains.

## Expected end state

Runtime ROM loading:

```text
ROM loader
  -> zip_archive_t
      -> zip_entry_t
      -> miniz
```

MVS/CPS2 caches:

```text
cache backend
  -> RAW/FOLDER -> normal filesystem I/O
  -> ZIP        -> zip_archive_t / zip_entry_t -> miniz
```

NCDZ:

```text
NCDZ resource_source_t
  -> DIRECTORY -> filesystem
  -> ZIP       -> zip_archive_t / zip_entry_t -> miniz
```

ROM converter:

```text
source ROM ZIP -> explicit miniz reader
cache ZIP      -> explicit miniz writer
```

There should be no generic layer in which an integer sometimes means a native file descriptor and sometimes means an archive entry, and no API where failing to open a ZIP implicitly selects a filesystem backend.

## Commit strategy

Use small verified milestone commits. Suggested boundaries:

1. `Introduce explicit ZIP archive objects`
2. `Migrate ROM loading to explicit ZIP entries`
3. `Separate cache ZIP and filesystem backends`
4. `Add explicit NCDZ resource sources`
5. `Remove legacy zfile API`
6. `Simplify ROM converter ZIP reader`
7. `Simplify ROM converter ZIP writer`
8. `Document ZIP architecture validation`

Do not force these exact commit titles if implementation boundaries differ, but every commit must build and preserve observable behavior.

## Repository rules for this work

- Work on the currently active branch unless instructed otherwise.
- Do not create a worktree unless explicitly requested.
- Preserve existing valid uncommitted work.
- Never touch, stage or commit anything under `resources/`.
- Use explicit staging only.
- Never use `git add -A` or `git commit -a`.
- Do not use generated ROM/cache output under `resources/` for tests; use build directories or `/tmp`.
- Treat current source and this plan as authoritative over old conversation SHAs/status descriptions.
