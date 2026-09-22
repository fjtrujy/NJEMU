# NJEMU Binary and Static-RAM Size Audit

Date: 2026-09-22

This audit was started while closing R10 of the reactive-memory migration and
is now updated through R12. R11 implemented the largest measured resident-RAM
opportunity; R12 begins the static-buffer audit with a zero-cost platform
ownership fix. Selective `-Os` remains intentionally deferred.

## 1. PSP section footprint

Representative current PSPSDK builds:

| Core/configuration | `.text` | `.rodata` | `.data` | `.bss` | PRX |
| --- | ---: | ---: | ---: | ---: | ---: |
| CPS2, GUI off | 559,504 B | 48,660 B | 5,152 B | 1,901,980 B | ~702 KiB |
| MVS, GUI off | 646,144 B | 47,732 B | 12,784 B | 2,023,804 B | ~812 KiB |
| CPS2, GUI on | 668,396 B | 2,736,732 B | 17,520 B | 1,989,100 B | ~3.39 MiB |
| MVS, GUI on | 756,464 B | 2,735,732 B | 21,536 B | 2,110,908 B | ~3.49 MiB |

The important distinction is that file size and runtime RAM pressure are not
the same problem. `.bss` does not materially inflate the PRX on disk, but it is
resident RAM that directly reduces the heap available to ROM/cache data.

## 2. R11 result: external CJK font data

The GUI build adds about 2.69 MiB of `.rodata`. Almost all of that increase is
explained by two translation units:

| Object | PSP contribution |
| --- | ---: |
| `src/common/font/gbk_s14.c` | 2,588,772 B |
| `src/common/font/gbk_tbl.c` | 64,408 B |

`gbk_s14.c` embeds the complete glyph bitmap plus position/width/height/pitch
tables. Together these two objects account for roughly 2.65 MiB of immutable
data in every GUI binary, independently of which language is actually selected.

R11 audited the data rather than subsetting it:

- `gbk_s14` contains 24,192 fixed 14x14 4-bpp glyphs, exactly 98 bytes each;
- all position entries equal `glyph * 98`;
- width, height and pitch are uniformly 14; skip values are uniformly zero;
- all runtime-valid entries in `gbk_tbl` equal the arithmetic mapping
  `(lead - 0x81) * 0xc0 + (trail - 0x40)`.

The complete 2,370,816-byte bitmap is therefore generated as
`font/gbk_s14.bin` and read through a 64-entry LRU glyph cache. No glyphs are
removed, so translated UI, legacy GBK names/metadata and command-list text keep
the original repertoire. The redundant metadata and GBK lookup arrays are no
longer linked into GUI binaries.

Measured PSP GUI result, using configurations comparable to the R10 rows:

| Core | `.text` | `.rodata` | `.data` | `.bss` | Total section delta |
| --- | ---: | ---: | ---: | ---: | ---: |
| CPS2 R11 | 669,644 B | 83,900 B | 17,520 B | 1,996,012 B | -2,644,656 B |
| MVS R11 | 757,712 B | 82,884 B | 21,536 B | 2,117,820 B | -2,644,656 B |

For CPS2, the PRX falls from 3,553,466 B to 901,986 B. The runtime code costs
only +1,248 B of `.text` and the cache +6,912 B of `.bss` versus the comparable
R10 build, while `.rodata` falls by 2,652,832 B.

On PS2, matching the R10 CPS2 GUI `SAVE_STATE=ON` + `COMMAND_LIST=ON`
configuration, total sections fall from 8,654,170 B to 5,985,474 B, a
2,668,696-byte reduction. The generated font asset is installed with the
external translation packs rather than being resident in the ELF.

Storage-I/O latency for cold CJK cache misses should still be profiled on
physical PSP/PS2 hardware. The cache makes repeated UI glyphs resident, but the
cross-build/unit validation does not substitute for device timing.

## 3. R12 result: remove the PSP GU list from non-PSP builds

The first `.bss` audit found a platform-ownership bug rather than a buffer that
needed a more complicated lifetime change. `gulist` is the 300 KiB command-list
buffer passed to `sceGuStart()` by `src/psp/psp_video.c`; there are no PS2 or
Desktop users. It was nevertheless defined unconditionally in `emumain.c`, so
every platform paid for it for the entire process lifetime.

R12 keeps the exact same static buffer on PSP and compiles it out everywhere
else. This does not replace static memory with heap memory: PS2/Desktop simply
stop reserving the unused 307,200 bytes.

Measured on the MVS PS2 GUI build with `SAVE_STATE=ON` and
`COMMAND_LIST=ON`:

| Section | Before R12 | R12 | Delta |
| --- | ---: | ---: | ---: |
| `.text` | 913,416 B | 913,416 B | 0 B |
| `.rodata` | 101,992 B | 101,992 B | 0 B |
| `.data` | 406,432 B | 406,432 B | 0 B |
| `.bss` | 2,735,944 B | 2,428,744 B | **-307,200 B** |
| total sections | 6,205,501 B | 5,898,301 B | **-307,200 B** |

Validation performed:

- Desktop MVS GUI build succeeds and all 10 CTest tests pass;
- PS2 MVS GUI cross-build succeeds and the linked ELF no longer contains a
  `gulist` symbol;
- the `PSP` preprocessor path retains the original declaration unchanged. The
  local PSP compiler/toolchain installation is not currently available on this
  shell's configured paths, so this small platform-guard change was not
  re-cross-built for PSP in this R12 pass.

## 4. R13 result: replace derived full-palette LUTs

The next large `.bss` candidates were `video_clut16` tables. They do not hold
emulated state: they cache deterministic conversions from each machine's native
palette word to the renderer's 15-bit color format.

For MVS/NCDZ the 32,768-entry table is unnecessary. Neo Geo's palette encoding
is only a permutation of the five red/green/blue bits, so R13 converts it with
bit operations and removes the 65,536-byte table completely.

CPS1/CPS2 include a four-bit brightness term, but each output component depends
only on `(brightness, component)`. The old 65,536-entry `uint16_t` table is now
a 16x16 `uint8_t` component table (256 bytes). Palette conversion performs three
lookups in this cache-hot table and combines the resulting five-bit channels.
The net static-RAM saving is **130,816 bytes per CPS core**.

`palette_convert_tests` exhaustively compares the replacement against the old
algorithms for all 32,768 Neo Geo colors and all 65,536 CPS palette words. This
proves exact 15-bit output equivalence rather than relying on visual inspection.

Measured MVS PS2 GUI result on the same R12 configuration:

| Section | R12 | R13 | Delta |
| --- | ---: | ---: | ---: |
| `.text` | 913,416 B | 913,112 B | -304 B |
| `.rodata` | 101,992 B | 101,992 B | 0 B |
| `.data` | 406,432 B | 406,432 B | 0 B |
| `.bss` | 2,428,744 B | 2,363,208 B | **-65,536 B** |
| total sections | 5,898,301 B | 5,832,461 B | **-65,840 B** |

Representative PS2 GUI section sizes after R13 are:

| Core | `.text` | `.rodata` | `.data` | `.bss` |
| --- | ---: | ---: | ---: | ---: |
| CPS1 | 899,256 B | 110,120 B | 673,632 B | 2,693,448 B |
| CPS2 | 812,104 B | 103,024 B | 402,256 B | 2,235,336 B |
| MVS | 913,112 B | 101,992 B | 406,432 B | 2,363,208 B |
| NCDZ | 868,408 B | 141,544 B | 395,200 B | 2,244,552 B |

Validation performed:

- Desktop CPS1/CPS2/MVS/NCDZ builds succeed;
- the exhaustive palette-conversion test passes in all Desktop core builds;
- MVS additionally passes the complete 11-test Desktop CTest suite;
- PS2 CPS1/CPS2/MVS/NCDZ GUI cross-builds succeed;
- the local PSP compiler installation remains unavailable in this shell, so
  PSP cross-build validation is still pending. The replacement code is shared
  C with no platform-specific API dependency.

The complete CPS1 Desktop CTest suite still has the existing target-specific
`memory_plan_tests` assertion failure; the CPS1 application build and the new
palette test both pass, and this failure is unrelated to the palette changes.

## 5. Static RAM (`.bss`) remains the next memory target

Representative large PSP symbols include. `gulist` remains in this list because
it is genuinely required by PSP; R12 only removes its accidental cost on the
other platforms.

### CPS2

- `gulist`: 307,200 B
- `JumpTable`: 262,144 B
- `cps1_gfxram`: 196,608 B
- `vertices_object`: 163,840 B
- `video_clut16`: 131,072 B
- `SZHVC_add`: 131,072 B
- `SZHVC_sub`: 131,072 B
- `vertices_object_flat`: 122,880 B
- `vertices_scroll`: 72,000 B
- `cps1_ram`: 65,536 B
- `scroll1_data`: 65,536 B

### MVS

- `gulist`: 307,200 B
- `vertices_spr`: 294,912 B
- `JumpTable`: 262,144 B
- `lfo_pm_table`: 131,072 B
- `neogeo_videoram`: 131,072 B
- `SZHVC_add`: 131,072 B
- `SZHVC_sub`: 131,072 B
- several 64 KiB video/RAM/fix buffers.

These should be audited separately from executable-size work. Candidates that
are not required simultaneously could potentially become lifecycle-scoped heap
allocations or share storage, but only after proving their ownership and hot-
path requirements.

## 6. Selective `-Os`: useful, but secondary

The user's proposed split between `-O3` hot paths and `-Os` cold/menu code is
technically reasonable. The measurements show, however, that it is not the
first-order win.

Representative PSP GUI object code sizes are:

- `common/ui_draw.c`: ~22.2 KiB `.text`
- `common/ui_menu.c`: ~19.3 KiB
- `common/ui.c`: ~10 KiB
- `common/config.c`: ~3.8 KiB `.text`
- `common/ui_text_catalog.c`: ~2.0 KiB
- `common/ui_layout.c`: ~1.3 KiB
- `common/ui_utf8.c`: <1 KiB
- R10 `common/memory_plan.c`: ~15.3 KiB including its small read-only tables,
  and almost entirely ROM-load/startup-only.

Even an unusually strong percentage reduction across the menu/UI code would
save tens of KiB, not the multi-MiB available by addressing the embedded font.

If selective optimization is pursued later, good cold/startup candidates are:

- menu/UI/configuration and translation plumbing;
- `memory_plan.c`, because allocation probing happens only during ROM loading;
- crypto/decode setup such as MVS `neocrypt.c` or CPS2 decrypt code, subject to
  measuring acceptable ROM-load latency.

The following should remain `-O3` unless profiling proves otherwise:

- C68K/CZ80 execution cores;
- renderer/video/sprite hot loops;
- mixer/audio synthesis loops;
- per-frame cache/address translation paths.

## 7. PS2 observations

Representative no-GUI PS2 builds currently contain approximately:

| Core | `.text` | `.data` | `.rodata` | `.bss` |
| --- | ---: | ---: | ---: | ---: |
| CPS2 | 658,160 B | 390,064 B | 54,168 B | 2,570,456 B |
| MVS | 755,952 B | 397,760 B | 53,352 B | 2,639,944 B |

The on-disk ELF is larger because these development builds contain debug
information. A significant fraction of PS2 `.data` is embedded IRX payloads
(USB/filesystem/pad/audio/memory-card modules). A future PS2-specific audit
should determine which modules are actually required for each storage/runtime
configuration and whether their embedded images remain resident after module
startup. That work is independent from compiler `-Os` tuning.

## 8. Recommended order for future size work

1. **Completed in R11:** externalize the ~2.65 MiB embedded CJK font/lookup
   payload without reducing the glyph repertoire.
2. **In progress:** audit large `.bss` buffers for platform ownership,
   derivable data, mutually-exclusive use or lifecycle-scoped use. R12 removes
   the unused 300 KiB PSP GU list from PS2/Desktop; R13 removes another 64 KiB
   from MVS/NCDZ and 127.75 KiB from CPS1/CPS2 by replacing full color LUTs.
3. Audit embedded PS2 IRX payloads and their post-load lifetime.
4. Apply selective `-Os` to measured cold translation units.
5. Re-measure performance and memory after every step; keep emulation/rendering
   hot paths optimized for speed.

R10 itself deliberately stops at measurement. Its runtime-memory improvement
comes from empirical allocation-shape probing, not from changing optimization
flags or removing assets.
