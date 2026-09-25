# ZIP backend evaluation: miniz 3.1.2

Date: 2026-09-25

## Scope

NJEMU historically embeds the 1998 MiniZip 0.15 implementation in
`src/zip/unzip.c`, with `src/zip/zfile.c` providing the application-facing API
and a POSIX directory fallback. This pass evaluates an alternative backend
using unmodified upstream miniz 3.1.2 while preserving that public API and the
directory behavior.

The legacy backend remains available through `USE_MINIZ=OFF`. The miniz backend
is intentionally opt-in until the console packages have the linker-friendly
build described below.

## API actually used by NJEMU

The shared ROM/cache/CD code uses the following API on every core:

| NJEMU API | miniz implementation |
| --- | --- |
| `zip_open()` | `mz_zip_reader_init_file()` |
| `zip_close()` | `mz_zip_reader_end()` |
| `zip_findfirst()` / `zip_findnext()` | `mz_zip_reader_get_num_files()` + `mz_zip_reader_file_stat()` |
| `zopen()` | `mz_zip_reader_locate_file()` + `mz_zip_reader_file_stat()` + `mz_zip_reader_extract_iter_new()` |
| `zread()` | `mz_zip_reader_extract_iter_read()` |
| `zgetc()` | existing 4 KiB NJEMU byte cache over `zread()` |
| `zclose()` | `mz_zip_reader_extract_iter_free()` |
| `zsize()` | cached `m_uncomp_size` from `mz_zip_archive_file_stat` |
| `zlength()` (NCDZ) | unchanged `zopen()` / `zsize()` / `zclose()` composition |

If `zip_open()` cannot open the path as a ZIP, NJEMU keeps the existing
directory backend based on `open()`, `read()`, `lseek()` and `close()`. This is
required by NCDZ and cache/resource paths.

`mz_zip_reader_locate_file(..., 0)` was verified to retain the case-insensitive
lookup behavior expected by the previous `unzLocateFile()` path.

## Platforms

- PS2: miniz 3.1.2 is installed under `$PS2SDK/ports` and builds with the EE
  GCC 15.2.0 toolchain.
- Desktop/macOS: Homebrew miniz 3.1.2 is available and the backend builds and
  runs against it.
- PSP: `psp-packages` now contains a miniz 3.1.2 package built from the same
  upstream release, with no `MINIZ_NO_*` configuration. The PSP compiler is not
  exposed in the current NJEMU shell, so the package recipe was inspected but
  the NJEMU PSP build/runtime path could not be exercised here.

The CMake integration keeps miniz's include directories and compile definitions
local to `src/zip/zfile_miniz.c`; they are not propagated to the CPU cores or
other NJEMU translation units.

## Functional validation

Desktop builds with `USE_MINIZ=ON` succeeded for CPS1, CPS2, MVS and NCDZ.
The Release CTest runs also pass the ZIP-independent tests except for the
existing `memory_plan_tests` Release/NDEBUG abort; that test does not exercise
the archive backend.

Representative real-ROM 30-frame smokes also exited successfully:

| Core | Game | Result |
| --- | --- | --- |
| CPS1 | `ghoulsu` | ROM enumeration/load completed; 30-frame run exited 0 |
| CPS2 | `mpangu` | ROM enumeration/load/decode/decrypt completed; exited 0 |
| MVS | `pbobbl2n` | BIOS + ROM + C-ROM cache load completed; exited 0 |
| NCDZ | `Windjammers` | directory/CD resource path loaded and ran; exited 0 |

These paths exercise entry enumeration, filename lookup, size/CRC metadata,
streaming decompression/read, and the NCDZ directory fallback.

The PS2 MVS `Release`, GUI, `COMMAND_LIST=ON`, `SAVE_STATE=ON`, `ADHOC=OFF`
cross-build also succeeds with miniz. A separate no-GUI/COMMAND_LIST=OFF ELF
was booted directly in PCSX2 using the build directory as `host:`. It reached
normal video/audio/controller initialization and remained running without a
fatal error or exception during the smoke window. PCSX2's normal log does not
capture NJEMU EE stdout, so this is a PS2 boot/runtime smoke rather than a full
ROM-load trace on real hardware.

## PS2 size measurements

All rows use the same MVS configuration and GCC 15.2.0 toolchain.

| Backend/build | `.text` | `.data` | `.bss` | `text+data+bss` |
| --- | ---: | ---: | ---: | ---: |
| Legacy MiniZip, current flags | 1,023,828 B | 406,472 B | 2,270,920 B | 3,701,220 B |
| Installed miniz 3.1.2, current flags | 1,098,536 B | 406,468 B | 2,272,008 B | 3,777,012 B |
| Legacy MiniZip + `--gc-sections` | 1,023,488 B | 406,468 B | 2,270,920 B | 3,700,876 B |
| Full miniz 3.1.2 built with function/data sections + `--gc-sections` | 1,019,696 B | 406,468 B | 2,272,008 B | 3,698,172 B |

The currently installed `libminiz.a` is not compiled with
`-ffunction-sections -fdata-sections`. Its archive members contain monolithic
`.text` sections. In particular, `miniz_zip.c.obj` is about 54.6 KiB of text;
once that object is selected, its internal writer references also cause deflate
code to be linked. The unoptimized miniz link therefore contains
`mz_zip_writer_*`, `mz_deflate*` and related functions even though NJEMU never
calls them.

A temporary build of the exact same upstream 3.1.2 sources, with no
`MINIZ_NO_*` definitions and only `-ffunction-sections -fdata-sections`, changes
that result. With `--gc-sections`, no writer/deflate symbols remain in the ELF.
Against the equivalently GC-linked legacy build, full miniz saves 3,792 bytes of
text, adds 1,088 bytes of BSS, and reduces the combined loaded sections by 2,704
bytes.

The optimized final ELF contains the reader path (`mz_zip_reader_*`),
`mz_crc32`, and `tinfl_decompress`. It contains no `mz_zip_writer_*` or
`tdefl_*` symbols.

## Runtime memory

The reader remains streaming and never allocates an entire ROM entry. On the
PS2 ABI, the relevant upstream structures measure:

- `mz_zip_archive`: 80 bytes;
- `mz_zip_archive_file_stat`: 1,112 bytes;
- `mz_zip_reader_extract_iter_state`: 9,560 bytes;
- `tinfl_decompressor`: 8,364 bytes (contained in the iterator state).

For a Deflate entry, the iterator may additionally allocate up to 64 KiB for
compressed input and a 32 KiB inflate dictionary while the entry is open. The
legacy reader has a 16 KiB explicit compressed-data buffer and also relies on
zlib's internal inflate allocations, so an exact peak-memory delta would still
need an on-target measurement if this becomes material to the PSP/PS2 memory
budget.

## Decision

There is no size justification for a separate read-only `MINIZ_NO_*` variant at
this point. Full upstream miniz 3.1.2 is suitable for NJEMU when the PS2 port is
built with `-ffunction-sections -fdata-sections` and the final application uses
`--gc-sections`.

NJEMU therefore keeps the miniz backend behind `USE_MINIZ=ON` and adds
`--gc-sections` for PS2 and PSP miniz builds, but leaves the option disabled by default for
now. Before making it the default backend:

1. update/reinstall the `ps2sdk-ports` miniz package with function/data sections;
2. build/verify the existing `psp-packages` miniz package with the same section
   granularity and validate NJEMU on PSP/PPSSPP;
3. decide how Desktop CI should obtain miniz on Linux before making Desktop
   builds depend on it by default.

The old `src/zip/unzip.c` backend should remain in-tree until those packaging
steps are complete and the miniz path is the normal validated build on all
supported platforms.
