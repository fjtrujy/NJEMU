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

## 4. Static RAM (`.bss`) remains the next memory target

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

## 5. Selective `-Os`: useful, but secondary

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

## 6. PS2 observations

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

## 7. Recommended order for future size work

1. **Completed in R11:** externalize the ~2.65 MiB embedded CJK font/lookup
   payload without reducing the glyph repertoire.
2. **In progress:** audit large `.bss` buffers for platform ownership,
   mutually-exclusive use or lifecycle-scoped use. R12 already removes the
   unused 300 KiB PSP GU list from PS2/Desktop.
3. Audit embedded PS2 IRX payloads and their post-load lifetime.
4. Apply selective `-Os` to measured cold translation units.
5. Re-measure performance and memory after every step; keep emulation/rendering
   hot paths optimized for speed.

R10 itself deliberately stops at measurement. Its runtime-memory improvement
comes from empirical allocation-shape probing, not from changing optimization
flags or removing assets.
