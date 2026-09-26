# ZIP backend evaluation: miniz 3.1.2

Date: 2026-09-25

## Scope

NJEMU historically embedded the 1998 MiniZip 0.15 implementation in
`src/zip/unzip.c`, with `src/zip/zfile.c` providing the application-facing API
and a POSIX directory fallback. That embedded implementation and the later
compatibility `zfile` layer have now been removed. Runtime ZIP access lives in
`src/common/zip_archive.c`; NCDZ owns its intentional ZIP-or-directory policy
separately in `src/ncdz/resource_source.c`.

The ROM converter had separate copies of the same legacy reader plus the old
MiniZip writer. Those copies and their temporary `zfile` compatibility layer
have also been removed. `romcnv/src/zip_reader.c` and `zip_writer.c` now expose
explicit reader/writer owners backed by miniz.

## API actually used by NJEMU

The final runtime abstraction uses the following operations:

| NJEMU API | miniz implementation |
| --- | --- |
| `zip_archive_open()` | `mz_zip_reader_init_file()` |
| `zip_archive_close()` | `mz_zip_reader_end()` |
| `zip_archive_stat()` | `mz_zip_reader_locate_file()` + `mz_zip_reader_file_stat()` |
| `zip_archive_find_crc()` | private central-directory scan with `mz_zip_reader_file_stat()` |
| `zip_entry_open()` | `mz_zip_reader_locate_file()` + `mz_zip_reader_extract_iter_new()` |
| `zip_entry_read()` | `mz_zip_reader_extract_iter_read()` |
| `zip_entry_getc()` | lazy 4 KiB NJEMU byte cache over `zip_entry_read()` |
| `zip_entry_close()` | `mz_zip_reader_extract_iter_free()` |

The ZIP layer is ZIP-only. Cache code chooses raw/folder/ZIP backends
explicitly, while NCDZ's `resource_source_t` is the only domain abstraction
that deliberately unifies directory and ZIP resources.

The old wrapper's pointer/integer warning suppression and pseudo-handle model
are no longer present. Public runtime ZIP types contain only opaque domain
state; miniz types do not cross the public header boundary.

`mz_zip_reader_locate_file(..., 0)` was verified to retain the case-insensitive
lookup behavior expected by the previous `unzLocateFile()` path.

## Platforms

- PS2: miniz 3.1.2 is installed under `$PS2SDK/ports` and builds with the EE
  GCC 15.2.0 toolchain.
- Desktop/macOS: an installed miniz 3.1.2 package is used when available. CMake
  falls back to fetching the pinned upstream 3.1.2 tag when it is absent.
- PSP: `psp-packages` now contains a miniz 3.1.2 package built from the same
  upstream release, with no `MINIZ_NO_*` configuration. The local PSPDEV
  installation exposes that package through its normal CMake config and NJEMU
  builds against it using the same `src/common/zip_archive.c` backend as PS2 and
  Desktop.

The CMake integration keeps miniz's include directories and compile definitions
local to the ZIP implementation translation unit. Runtime and `romcnv` public
headers do not include miniz, so its compile requirements are not propagated to
their callers.

## Functional validation

Desktop miniz builds succeeded for CPS1, CPS2, MVS and NCDZ.
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

These paths exercise filename and CRC lookup, metadata-only queries, streaming
decompression/read, explicit cache backends, and the NCDZ resource-source
directory path.

The PS2 MVS `Release`, GUI, `COMMAND_LIST=ON`, `SAVE_STATE=ON`, `ADHOC=OFF`
cross-build also succeeds with miniz. A separate no-GUI/COMMAND_LIST=OFF ELF
was booted directly in PCSX2 using the build directory as `host:`. It reached
normal video/audio/controller initialization and remained running without a
fatal error or exception during the smoke window. PCSX2's normal log does not
capture NJEMU EE stdout, so this is a PS2 boot/runtime smoke rather than a full
ROM-load trace on real hardware.

The PSP MVS `Release`, `GUI=OFF`, `COMMAND_LIST=OFF`, `SAVE_STATE=ON`,
`ADHOC=OFF` cross-build also succeeds with the installed miniz package. This
configuration uses the same miniz reader implementation as PS2; there is no
PSP-specific ZIP backend.

The resulting `EBOOT.PBP` was also booted with PPSSPP 1.20.2 using the build
directory as the application directory. PPSSPP loaded the relocatable MVS
module, its import tables and module metadata, reached the NJEMU module entry
and reported the application as booted without a fatal error during the smoke
window. As with the PS2 smoke, PPSSPP's normal log does not capture NJEMU's
ROM-loader stdout, so the real-ROM equivalence evidence still comes primarily
from the Desktop smokes until a PSP stdout/psplink run is recorded.

The final removal pass rebuilt MVS against the miniz-only tree on Desktop, PS2
and PSP, and rebuilt CPS2 on Desktop. No embedded MiniZip source is required by
any of those builds.

`romcnv_mvs` and `romcnv_cps2` also build against miniz. Real conversions of
`pbobbl2n` and `mpangu` using ZIP cache output produced archives that pass a
full `unzip -t` integrity check. NJEMU then booted both games for 30 frames
using those newly generated cache ZIPs, validating the converter writer and the
runtime reader together. The final converter writer uses an explicit
`zip_writer_t`: already-materialized cache blocks are passed directly to miniz,
while `cache_info` is written through a small segmented callback without a
temporary aggregate buffer or the removed `zwrite()`/`tmpfile()` compatibility
path.

The converter's CMake build uses an installed miniz package when available and
otherwise fetches the pinned upstream 3.1.2 tag, which also covers its
Emscripten configuration. Emscripten is not installed on the local validation
host, so the WASM target was not rebuilt in this pass.

## Final post-refactor measurements (Z9)

The full architecture refactor was revalidated on 2026-09-26 after Z8 made the
runtime and converter miniz state private to their implementation translation
units. The final Desktop executable sizes are:

| Target | Z0 baseline | Z9 final | Delta |
| --- | ---: | ---: | ---: |
| CPS1 | 990,008 B | 990,056 B | +48 B |
| CPS2 | 367,656 B | 367,720 B | +64 B |
| MVS | 474,768 B | 474,784 B | +16 B |
| NCDZ | 402,856 B | 403,384 B | +528 B |

The final converter files are 90,392 bytes for MVS and 87,752 bytes for CPS2.
Their Mach-O `__text` / `__bss` sections are 28,976 / 9,928 bytes and
26,592 / 2,384 bytes respectively. File-size changes between converter phases
can include 16 KiB Mach-O segment-alignment steps, so section sizes remain the
more useful code/data comparison.

For the same MVS Release/no-GUI configuration used by the Z0 architecture
baseline, the console results are:

| Build | Z0 | Z9 | Delta |
| --- | ---: | ---: | ---: |
| PS2 `.text` | 891,040 B | 892,208 B | +1,168 B |
| PS2 `.data` | 391,060 B | 391,060 B | 0 B |
| PS2 `.bss` | 2,251,848 B | 2,245,512 B | -6,336 B |
| PS2 `text+data+bss` | 3,533,948 B | 3,528,780 B | -5,168 B |
| PS2 ELF file | 3,277,680 B | 3,278,688 B | +1,008 B |
| PSP `.text` | 768,904 B | 769,956 B | +1,052 B |
| PSP `.data` | 13,204 B | 13,188 B | -16 B |
| PSP `.bss` | 1,944,080 B | 1,937,792 B | -6,288 B |
| PSP `text+data+bss` | 2,726,188 B | 2,720,936 B | -5,252 B |
| PSP ELF file | 2,444,212 B | 2,444,968 B | +756 B |
| PSP PRX | 914,874 B | 916,050 B | +1,176 B |
| PSP `EBOOT.PBP` | 915,250 B | 916,426 B | +1,176 B |

The loaded-section totals therefore shrink slightly despite the explicit owner
API. Z8's opaque public owners allocate their small archive/entry state on open
instead of reserving it in static BSS. Entry extraction remains streaming and
the optional 4 KiB byte cache is still allocated lazily only for byte-oriented
reads, so there is no new whole-entry buffering behavior.

Symbol inspection of the final installed-package links finds 34
`mz_zip_reader_*`, 24 `mz_zip_writer_*`, 6 `tinfl_*` and 24 `tdefl_*` symbols in
both the PS2 and PSP MVS ELFs. NJEMU runtime code calls only reader operations;
the writer/deflate retention is the same full-package/link-granularity issue
described below. It remains packaging work rather than an architectural reason
to expose miniz or restore a second ZIP backend.

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

## PSP size and linker behavior

For an apples-to-apples MVS `Release`, `GUI=OFF`, `COMMAND_LIST=OFF`,
`SAVE_STATE=ON`, `ADHOC=OFF` build, the legacy ZIP backend produces 711,468
bytes of text, 13,188 bytes of data and 1,942,976 bytes of BSS. The installed
full miniz package produces 768,904 bytes of text, 13,204 bytes of data and
1,944,080 bytes of BSS before any linker garbage collection, an increase of
57,436 bytes of text and 1,104 bytes of BSS.

The installed PSP `libminiz.a` is also built as four monolithic text objects
(`miniz.c`, `miniz_zip.c`, `miniz_tinfl.c`, `miniz_tdef.c`), so unused writer
and deflate functions are retained once `miniz_zip.c` is selected. Their text
sizes are approximately 7.2 KiB, 54.6 KiB, 10.8 KiB and 24.6 KiB respectively;
the final NJEMU ELF confirms that `mz_zip_writer_*`, `mz_deflate*` and `tdefl_*`
symbols are present even though NJEMU only calls the reader API.

A temporary evaluation build of the same upstream 3.1.2 sources using only
official feature switches (`MINIZ_NO_DEFLATE_APIS`, `MINIZ_NO_ZLIB_APIS` and
`MINIZ_NO_TIME`) reduces that same NJEMU ELF to 708,964 bytes of text, 13,196
bytes of data and 1,944,080 bytes of BSS. No `mz_zip_writer_*`, `mz_deflate*` or
`tdefl_*` symbols remain. Compared with the equivalent legacy build, the
reader-only miniz ELF uses 2,504 fewer bytes of text and 1,392 fewer bytes in
the combined loaded sections (the miniz reader still accounts for 1,104 bytes
more BSS). This makes a separate reader-only package/target worthwhile on PSP;
the general-purpose full miniz package does not need to be weakened for other
users.

Unlike PS2, enabling `-Wl,--gc-sections` globally is not currently safe for a
PSP PRX. The PSP SDK `linkfile.prx` does not `KEEP` the module-info/import-stub
sections that must survive section GC. A normal GC link loses
`.rodata.sceModuleInfo` and `psp-fixup-imports` reports `no sceModuleInfo section
found`. Forcing `module_info` back into the link is not sufficient when miniz
itself is built with function/data sections: `psp-fixup-imports` then reports a
stub/NID size mismatch because additional PSP import metadata has been
discarded. NJEMU therefore does not enable `--gc-sections` for PSP.

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

The same `src/common/zip_archive.c` reader implementation is used by Desktop,
PS2 and PSP; there is no platform-specific ZIP code. The package and linker
strategy does need to differ between the consoles, however.

On PS2, full upstream miniz 3.1.2 is suitable when the port is built with
`-ffunction-sections -fdata-sections` and the final application uses
`--gc-sections`: the linker then removes the writer/deflate path and the final
ELF is slightly smaller than the legacy MiniZip build.

On PSP, the installed full upstream archive is appreciably larger than the
legacy backend and the PRX linker cannot currently use `--gc-sections` safely.
The measured reader-only build using miniz's official `MINIZ_NO_*` switches is
smaller than the legacy backend, so a separate read-only package/target is
justified there.

NJEMU now requires miniz and uses `--gc-sections` only for PS2. Desktop can
fetch the pinned upstream release when no package is installed; PS2 and PSP use
their native toolchain packages. The old `src/zip/unzip.c/.h` implementation,
the optional `USE_MINIZ` switch, and the duplicate MiniZip sources in `romcnv`
have been removed.

The size work is still relevant after the migration. The next packaging
optimizations are:

1. build the `ps2sdk-ports` miniz package with `-ffunction-sections` and
   `-fdata-sections`, allowing NJEMU's PS2 `--gc-sections` link to discard the
   unused writer/deflate implementation;
2. provide a PSP reader-only miniz target configured with official `MINIZ_NO_*`
   options, because global section GC is not currently safe for PSP PRX files.

Until those package changes land, the installed full miniz archives retain
unused code on the consoles and therefore produce larger binaries than the
historical embedded MiniZip baseline. This is a packaging/link-granularity
issue, not a reason to keep a second ZIP implementation in NJEMU.
