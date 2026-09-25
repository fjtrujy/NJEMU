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

The legacy wrapper also no longer needs its pointer/integer warning
suppression. `zip_open()` and archive-mode `zopen()` historically returned the
`unzFile` pointer cast to `int`/`long`, even though every caller only tests the
result against `-1` and the archive read/close paths ignore that pseudo-handle.
They now return `0` as the success sentinel instead. Desktop, PSP and PS2 all
compile that legacy path with the project's `-Werror` settings without
`-Wpointer-to-int-cast`/`-Wvoid-pointer-to-int-cast` pragmas.

`mz_zip_reader_locate_file(..., 0)` was verified to retain the case-insensitive
lookup behavior expected by the previous `unzLocateFile()` path.

## Platforms

- PS2: miniz 3.1.2 is installed under `$PS2SDK/ports` and builds with the EE
  GCC 15.2.0 toolchain.
- Desktop/macOS: Homebrew miniz 3.1.2 is available and the backend builds and
  runs against it.
- PSP: `psp-packages` now contains a miniz 3.1.2 package built from the same
  upstream release, with no `MINIZ_NO_*` configuration. The local PSPDEV
  installation exposes that package through its normal CMake config and NJEMU
  builds against it using the same `src/zip/zfile_miniz.c` backend as PS2 and
  Desktop.

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

The same `src/zip/zfile_miniz.c` reader implementation is suitable for Desktop,
PS2 and PSP; there is no reason to keep platform-specific ZIP code. The package
and linker strategy does need to differ between the consoles, however.

On PS2, full upstream miniz 3.1.2 is suitable when the port is built with
`-ffunction-sections -fdata-sections` and the final application uses
`--gc-sections`: the linker then removes the writer/deflate path and the final
ELF is slightly smaller than the legacy MiniZip build.

On PSP, the installed full upstream archive is appreciably larger than the
legacy backend and the PRX linker cannot currently use `--gc-sections` safely.
The measured reader-only build using miniz's official `MINIZ_NO_*` switches is
smaller than the legacy backend, so a separate read-only package/target is
justified there.

NJEMU therefore keeps the miniz backend behind `USE_MINIZ=ON` and uses
`--gc-sections` only for PS2 miniz builds. PSP and PS2 otherwise share the same
reader implementation. The option remains disabled by default for now. Before
making it the default backend:

1. update/reinstall the `ps2sdk-ports` miniz package with function/data sections;
2. add a PSP reader-only miniz package/target configured through the official
   `MINIZ_NO_*` switches above; the measured result is smaller than the legacy
   backend and avoids relying on unsafe PRX section GC. Fixing the PSP SDK
   linker script can remain a separate toolchain improvement;
3. decide how Desktop CI should obtain miniz on Linux before making Desktop
   builds depend on it by default.

The old `src/zip/unzip.c` backend should remain in-tree until those packaging
steps are complete and the miniz path is the normal validated build on all
supported platforms.
