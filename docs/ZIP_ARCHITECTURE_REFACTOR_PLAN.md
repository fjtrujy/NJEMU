# ZIP and Resource I/O Architecture Refactor Plan

Date: 2026-09-26
Status: Z0-Z7 complete; Z8 next

## Purpose

NJEMU now uses external upstream miniz 3.1.2 for ZIP support on Desktop, PS2 and PSP, and the old embedded MiniZip sources have been removed. The remaining technical debt is no longer the ZIP library itself: it is the historical `zfile` abstraction built around it.

At the start of this plan, the runtime API intentionally made ZIP entries and normal files look alike through `zip_open()`, `zopen()`, `zread()`, `zclose()`, `zsize()` and `zlength()`. Z5 removes that runtime compatibility layer. Z6 also removes the converter read-side compatibility API; only the temporary converter writer names remain until Z7.

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

#### Z2 implementation result

`src/common/loadrom.c` now owns a dedicated `zip_archive_t` and `zip_entry_t`
for ROM loading. `file_open()` preserves the historical search order (current
set, parent beside the current set, then the launch `roms/` parent path), but
uses `zip_archive_find_crc()` directly instead of generic archive enumeration.
If no CRC match exists, `zip_archive_stat()` performs the metadata-only
filename check that distinguishes a CRC mismatch from a missing ROM.

`file_open()` now returns `rom_file_open_result_t` with the existing numeric
semantics preserved as named values: `ROM_FILE_OPEN_OK` (0),
`ROM_FILE_OPEN_NOT_FOUND` (-1) and `ROM_FILE_OPEN_CRC_MISMATCH` (-2). CPS1,
CPS2 and MVS callers use those names where they distinguish the error cases.
`file_read()` and `file_getc()` are thin semantic helpers over the owned
`zip_entry_t`, and `file_close()` explicitly closes the entry and archive.

The runtime `zip_findfirst()` / `zip_findnext()` API, `struct zip_find_t`,
`legacy_find_index`, and the private indexed-stat/count helpers were deleted
because they have no remaining runtime consumer. The similarly named
converter API under `romcnv/` is separate and remains deferred to Z6.

Validation after Z2:

- Desktop Release builds pass for CPS1, CPS2 and MVS;
- real 30-frame smokes pass for `ghoulsu`, `mpangu` and `pbobbl2n`;
- PS2 MVS and PSP MVS Release cross-builds pass;
- Desktop CTest is 10/10 when excluding only the previously documented
  Release/NDEBUG `memory_plan_tests` abort;
- `git diff --check` passes and no runtime `zip_findfirst`/`zip_findnext`
  references remain.

Z2 temporarily added roughly 4 KiB of static entry state while the explicit
ROM entry and the legacy cache/NCDZ adapter coexisted. Compared with the Z1
build, PS2 MVS `text+data+bss` grew from 3,534,492 B to 3,538,444 B
(+3,952 B), and PSP MVS grew from 2,726,884 B to 2,730,824 B (+3,940 B).
Z3 removes that cost by making the per-entry `getc` cache lazy.

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

#### Z3 implementation result

`src/common/cache.c` now owns the cache backend resources directly.
`cache_fd` is again an ordinary POSIX descriptor used only by raw/folder
filesystem paths, while `CACHE_ZIPFILE` owns a dedicated
`cache_zip_archive` and `cache_zip_entry`. ZIP `cache_info` and 64 KiB
block reads use the explicit ZIP API, validate short reads, and propagate the
close-time CRC result instead of relying on the legacy pseudo-handle adapter.

MVS cache path ownership also moved back into the cache module:
`cachefile_open()` handles filesystem cache files and
`cachefile_zip_read()` handles the special encrypted `srom`/`vrom`
entries with short-lived explicit archive/entry owners. The previous
`cachefile_zopen()` pseudo-handle API is gone. Parent C-ROM selection keeps
the historical parent-then-game ZIP fallback order.

`cache_sleep()` now closes and reopens `cache_zip_archive` explicitly for
ZIP caches. Raw caches continue to close/reopen their filesystem descriptor,
and folder caches continue to require no persistent resource. The normal
suspend/resume callers therefore no longer depend on global `zfile` state.

The Z3 pass also reduced the size of every `zip_entry_t`: its 4 KiB
`getc` buffer is now allocated lazily on the first `zip_entry_getc()` and
freed by `zip_entry_close()`. Bulk-read-only ROM/cache/NCDZ entries therefore
do not reserve an unused 4 KiB buffer. The now-dead legacy `zgetc()` adapter
was removed.

Validation after Z3:

- Desktop Release builds pass for CPS1, CPS2, MVS and NCDZ;
- PS2 MVS and PSP MVS Release cross-builds pass;
- Desktop CTest is 10/10 when excluding only the previously documented
  Release/NDEBUG `memory_plan_tests` abort;
- `ghoulsu` passes a 30-frame smoke after the lazy `getc` change;
- CPS2 `mpangu` passes 30-frame smokes with ZIP, raw and folder caches;
- MVS `pbobbl2n` passes with both ZIP and folder caches;
- a freshly generated MVS `mslug3_cache.zip` passes `unzip -t` and is
  consumed successfully by NJEMU, including the encrypted `srom` path;
- a freshly generated MVS `mslug5_cache.zip` is consumed successfully and
  exercises the encrypted `vrom` path (`Loading decrypted SOUND1 ROM...`);
- generated test caches lived under `/tmp` or build directories only;
- `git diff --check` passes and cache/MVS runtime code contains no legacy
  `zopen`/`zread`/`zclose`/global-`zip_open` calls.

The lazy byte cache more than offsets the new explicit cache owner. Relative
to Z2, PS2 MVS `text+data+bss` falls from 3,538,444 B to 3,530,604 B
(-7,840 B), with BSS down 8,064 B. PSP MVS falls from 2,730,824 B to
2,722,868 B (-7,956 B), also with BSS down 8,064 B. The final Z3 loaded
section totals are therefore also below the original Z0 baseline by 3,344 B
on PS2 and 3,320 B on PSP. File/PRX sizes move by a few hundred bytes because
of code/alignment and are not representative of resident RAM:

| Build | Z2 | Z3 | Delta |
| --- | ---: | ---: | ---: |
| Desktop CPS1 executable | 990,360 B | 990,264 B | -96 B |
| Desktop CPS2 executable | 368,008 B | 367,928 B | -80 B |
| Desktop MVS executable | 475,152 B | 475,040 B | -112 B |
| PS2 MVS `.text` | 892,400 B | 892,624 B | +224 B |
| PS2 MVS `.bss` | 2,254,984 B | 2,246,920 B | -8,064 B |
| PS2 MVS `text+data+bss` | 3,538,444 B | 3,530,604 B | -7,840 B |
| PS2 MVS ELF file | 3,279,004 B | 3,279,312 B | +308 B |
| PSP MVS `.text` | 770,388 B | 770,496 B | +108 B |
| PSP MVS `.bss` | 1,947,248 B | 1,939,184 B | -8,064 B |
| PSP MVS `text+data+bss` | 2,730,824 B | 2,722,868 B | -7,956 B |
| PSP MVS ELF file | 2,445,988 B | 2,446,252 B | +264 B |
| PSP MVS PRX | 917,210 B | 917,602 B | +392 B |
| PSP MVS EBOOT.PBP | 917,586 B | 917,978 B | +392 B |

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

#### Z4 implementation result

NCDZ now owns an explicit `resource_source_t` in
`src/ncdz/resource_source.h/.c`. A source is opened explicitly as either
`RESOURCE_SOURCE_DIRECTORY` or `RESOURCE_SOURCE_ZIP`; a failed ZIP open never
selects the directory backend. Directory metadata uses `stat()`, ZIP metadata
uses `zip_archive_stat()`, and `resource_file_t` owns either an ordinary POSIX
descriptor or an explicit `zip_entry_t`.

`src/ncdz/cdrom.c` and `src/ncdz/driver.c` no longer use the legacy `zopen`,
`zread`, `zclose` or `zlength` paths. `src/common/filer.c` now probes NCDZ ZIP
contents with `resource_source_stat()` and selects the source type explicitly
when launching. The Desktop, PS2 and PSP no-GUI launchers do the same from the
selected `game_name.ini` path. Title-system reads also use a local explicit
resource source.

The GUI build exposed one miniz/zlib compatibility-header collision because
`filer.c` included system `zlib.h` while the NCDZ resource source exposes
`zip_archive_t`. The only zlib use there was the Neo Geo CD BIOS CRC, so it now
uses `mz_crc32()` from the already-required miniz API instead of mixing the two
zlib-compatible headers.

`resource_source_tests` generates a directory and ZIP under the build
directory and verifies directory stat/read, case-insensitive ZIP stat/read,
and that neither explicit open operation falls back to the other backend.

Validation after Z4:

- Desktop NCDZ Release/no-GUI builds and CTest passes 11/11 when excluding only
  the pre-existing Release/NDEBUG `memory_plan_tests` abort;
- Desktop NCDZ GUI + `COMMAND_LIST=ON` + `SAVE_STATE=ON` builds successfully;
- PS2 NCDZ Release/no-GUI cross-build passes;
- PSP NCDZ Release/no-GUI cross-build passes and produces `EBOOT.PBP`;
- `Windjammers` directory source completes a 30-frame Desktop smoke;
- a temporary `Windjammers.zip` generated only inside the Desktop build
  directory passes `unzip -t` and also completes a 30-frame smoke.

For an exact size comparison, `89dd903` (Z3) was exported to `/tmp` and built
with matching NCDZ Release/no-GUI options. Z4 adds a small amount of code and
one 1 KiB-class global source owner while the legacy `zfile` globals still
coexist. Z5 will remove that duplicate legacy state.

| Build | Z3 | Z4 | Delta |
| --- | ---: | ---: | ---: |
| Desktop NCDZ executable | 403,112 B | 403,640 B | +528 B |
| PS2 NCDZ `.text` | 859,040 B | 860,552 B | +1,512 B |
| PS2 NCDZ `.bss` | 2,168,840 B | 2,169,864 B | +1,024 B |
| PS2 NCDZ `text+data+bss` | 3,419,916 B | 3,422,452 B | +2,536 B |
| PS2 NCDZ ELF file | 3,365,004 B | 3,366,788 B | +1,784 B |
| PSP NCDZ `.text` | 779,156 B | 780,100 B | +944 B |
| PSP NCDZ `.bss` | 1,861,148 B | 1,862,172 B | +1,024 B |
| PSP NCDZ `text+data+bss` | 2,647,848 B | 2,649,816 B | +1,968 B |
| PSP NCDZ ELF file | 2,413,636 B | 2,415,380 B | +1,744 B |
| PSP NCDZ PRX | 906,414 B | 907,774 B | +1,360 B |
| PSP NCDZ EBOOT.PBP | 906,794 B | 908,154 B | +1,360 B |

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

#### Z5 implementation result

The runtime compatibility layer is now deleted completely. `src/zip/zfile.c`
and `src/zip/zfile.h` are gone, they are no longer part of `COMMON_SRC`, and
`emumain.h` no longer exposes the historical header transitively. The last
process-global adapter state (`legacy_archive`, `legacy_entry`, `basedir` and
`basedirend`) therefore no longer exists.

Residual cleanup callers now use their real owner directly. ROM-loader error
paths call `file_close()` instead of the removed global `zip_close()`, and the
MVS BIOS menu uses `rom_file_open_result_t` rather than the old `int64_t`
pseudo-handle-derived result type. The unrelated PNG member previously named
`zlength` was renamed to `compressed_length` on Desktop, PS2 and PSP so the
runtime legacy-API grep has no false-positive identifier matches.

During the Z5 GUI audit, the MVS file-browser branch was also found to contain
an accidental Z4 `resource_source_close(&ncdz_game_source)` call. That symbol
is NCDZ-only and would break an MVS GUI build; Z5 removes the stray call and
the MVS GUI + command-list configuration builds successfully.

Runtime validation after Z5:

- `git grep` over `src/` finds no `zopen`, `zread`, `zgetc`, `zclose`,
  `zsize`, `zlength`, legacy `zip_open`/`zip_close`, or
  `zip_findfirst`/`zip_findnext` references;
- Desktop Release/no-GUI builds pass for CPS1, CPS2, MVS and NCDZ;
- Desktop MVS GUI + `COMMAND_LIST=ON` + `SAVE_STATE=ON` builds successfully;
- MVS CTest passes 10/10 and NCDZ CTest passes 11/11 when excluding only the
  pre-existing Release/NDEBUG `memory_plan_tests` abort;
- real 30-frame smokes pass for CPS1 `ghoulsu`, CPS2 `mpangu`, MVS
  `pbobbl2n`, NCDZ `Windjammers.zip`, and NCDZ `Windjammers` directory source;
- PS2 and PSP Release/no-GUI cross-builds pass for both MVS and NCDZ, with
  PSP producing `EBOOT.PBP` for both targets;
- `git diff --check` passes.

Removing the final adapter recovers the duplicated global archive/entry state.
For MVS, Z4 was rebuilt from commit `8a18c03` in `/tmp` with matching options
to provide an exact comparison. NCDZ uses the Z4 measurements already recorded
above.

| Build | Z4 | Z5 | Delta |
| --- | ---: | ---: | ---: |
| Desktop MVS executable | 475,040 B | 474,736 B | -304 B |
| PS2 MVS `.text` | 892,632 B | 891,936 B | -696 B |
| PS2 MVS `.bss` | 2,246,920 B | 2,245,768 B | -1,152 B |
| PS2 MVS `text+data+bss` | 3,530,612 B | 3,528,764 B | -1,848 B |
| PS2 MVS ELF file | 3,279,576 B | 3,278,400 B | -1,176 B |
| PSP MVS `.text` | 770,512 B | 769,824 B | -688 B |
| PSP MVS `.bss` | 1,939,184 B | 1,938,032 B | -1,152 B |
| PSP MVS `text+data+bss` | 2,722,884 B | 2,721,044 B | -1,840 B |
| PSP MVS ELF file | 2,446,268 B | 2,444,928 B | -1,340 B |
| PSP MVS PRX | 917,618 B | 916,610 B | -1,008 B |
| PSP MVS EBOOT.PBP | 917,994 B | 916,986 B | -1,008 B |
| Desktop NCDZ executable | 403,640 B | 403,336 B | -304 B |
| PS2 NCDZ `.text` | 860,552 B | 859,408 B | -1,144 B |
| PS2 NCDZ `.bss` | 2,169,864 B | 2,168,712 B | -1,152 B |
| PS2 NCDZ `text+data+bss` | 3,422,452 B | 3,420,156 B | -2,296 B |
| PS2 NCDZ ELF file | 3,366,788 B | 3,365,208 B | -1,580 B |
| PSP NCDZ `.text` | 780,100 B | 779,012 B | -1,088 B |
| PSP NCDZ `.bss` | 1,862,172 B | 1,861,020 B | -1,152 B |
| PSP NCDZ `text+data+bss` | 2,649,816 B | 2,647,576 B | -2,240 B |
| PSP NCDZ ELF file | 2,415,380 B | 2,413,304 B | -2,076 B |
| PSP NCDZ PRX | 907,774 B | 906,062 B | -1,712 B |
| PSP NCDZ EBOOT.PBP | 908,154 B | 906,442 B | -1,712 B |

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

#### Z6 implementation result

`romcnv` now has a dedicated explicit reader in
`romcnv/src/zip_reader.c/.h`. `zip_reader_archive_t` owns the miniz reader
archive and `zip_reader_entry_t` owns the extraction iterator, size/CRC,
streamed byte count and byte-oriented read cache. The converter reader is
ZIP-only: the undocumented historical directory fallback has been removed.

`romcnv/src/common.c` now owns one explicit ROM archive/entry pair. CRC lookup
uses `zip_reader_archive_find_crc()` directly, filename metadata lookup is
used only to preserve the existing CRC-mismatch result, and the set search
order remains current set followed by parent set. Case-insensitive filename
lookup, streaming extraction and close-time CRC validation are preserved.
`rom_fd`, `zip_findfirst()`, `zip_findnext()`, `zread()`, `zgetc()`,
`zsize()` and `zcrc()` are gone from the converter reader.

The reader result is now the explicit `rom_file_open_result_t`, and the MVS
and CPS2 callers use `ROM_FILE_OPEN_NOT_FOUND` rather than magic
pseudo-handle-era values. Error cleanup closes the reader owner directly and
does not interact with the output ZIP writer.

`romcnv/src/zfile.c/.h` is deliberately retained only as the temporary Z7
writer compatibility layer. It now contains only writer archive state,
`tmpfile()` staging and the writer-facing `zip_open()`, `zopen()`,
`zwrite()`, `zclose()` and `zip_close()` names. No reader state or reader
operation remains there.

Validation after Z6:

- Release builds pass for `romcnv_mvs` and `romcnv_cps2` using the installed
  miniz 3.1.2 package;
- the MVS Release build also passes with `find_package(miniz)` disabled,
  exercising the pinned FetchContent fallback;
- a real `pbobbl2n` conversion generates `pbobbl2n_cache.zip`, which passes
  a complete `unzip -t` and is consumed successfully by NJEMU for a 30-frame
  MVS smoke;
- a real `mpangu` conversion generates `mpangu_cache.zip`, which likewise
  passes `unzip -t` and is consumed successfully by a 30-frame CPS2 smoke;
- all generated caches and temporary binaries used for this validation live
  under `/tmp` or build directories; `resources/` is not modified;
- `git diff --check` passes.

For size comparison, pre-Z6 commit `a1ef499` and the Z6 tree were both built
fresh in `/tmp` with Release and the same installed miniz package. Both
targets reduce real code and zero-fill state. CPS2 also crosses a 16 KiB Mach-O
`__TEXT` alignment boundary, so its file-size drop is much larger than the
actual code reduction.

| Build | Z5 | Z6 | Delta |
| --- | ---: | ---: | ---: |
| MVS converter `__text` | 29,268 B | 28,888 B | -380 B |
| MVS converter `__bss` | 17,344 B | 15,328 B | -2,016 B |
| MVS converter file | 90,424 B | 90,616 B | +192 B |
| CPS2 converter `__text` | 26,796 B | 26,416 B | -380 B |
| CPS2 converter `__bss` | 9,808 B | 7,792 B | -2,016 B |
| CPS2 converter file | 87,784 B | 71,400 B | -16,384 B |

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

#### Z7 implementation result

Z7 removes the converter writer compatibility layer completely. The historical
`romcnv/src/zfile.c/.h` files are deleted and `romcnv` now owns ZIP output
through `zip_writer_t`, a concrete owner around `mz_zip_archive`.

MVS and CPS2 cache generation now write already-materialized cache blocks
directly with `mz_zip_writer_add_mem()`. `cache_info` is emitted through a
small segmented read callback so the existing pieces can be compressed as one
entry without recreating the old `tmpfile()` staging step or allocating a new
aggregate buffer. Writer failures are propagated to the converter, and archive
finalization/end failures cause the partial output ZIP to be removed.

Validation after Z7:

- Release `romcnv_mvs` and `romcnv_cps2` builds pass with miniz 3.1.2;
- no `zopen()`, `zwrite()`, `zclose()`, converter `zip_open()/zip_close()`,
  `tmpfile()` or `zfile` source remains under `romcnv/src`;
- a real `pbobbl2n` MVS conversion produces `pbobbl2n_cache.zip`; a complete
  `unzip -t` passes and NJEMU consumes that exact generated cache successfully
  for a 30-frame Desktop smoke;
- a real `mpangu` CPS2 conversion produces `mpangu_cache.zip`; a complete
  `unzip -t` passes and NJEMU consumes that exact generated cache successfully
  for a 30-frame Desktop smoke;
- the non-`memory_plan_tests` Desktop CTest set passes 10/10 for both MVS and
  CPS2 validation builds;
- all generated caches and smoke artifacts remain in build directories;
  `resources/` is not modified;
- `git diff --check` passes.

Using the same Release configuration and installed miniz package as the Z6
measurements above, Z7 also reduces the converter binaries slightly:

| Build | Z6 | Z7 | Delta |
| --- | ---: | ---: | ---: |
| MVS converter `__text` | 28,888 B | 28,760 B | -128 B |
| MVS converter `__bss` | 15,328 B | 14,176 B | -1,152 B |
| MVS converter file | 90,616 B | 90,408 B | -208 B |
| CPS2 converter `__text` | 26,416 B | 26,376 B | -40 B |
| CPS2 converter `__bss` | 7,792 B | 6,640 B | -1,152 B |
| CPS2 converter file | 71,400 B | 71,208 B | -192 B |

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
