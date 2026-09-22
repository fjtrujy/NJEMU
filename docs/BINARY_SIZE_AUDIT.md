# NJEMU Binary and Static-RAM Size Audit

Date: 2026-09-22

This audit was performed while closing R10 of the reactive-memory migration.
It is intentionally observational: no selective `-Os`, font externalization,
or buffer-size optimization is applied here.

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

## 2. Largest GUI/binary-size opportunity: embedded CJK font data

The GUI build adds about 2.69 MiB of `.rodata`. Almost all of that increase is
explained by two translation units:

| Object | PSP contribution |
| --- | ---: |
| `src/common/font/gbk_s14.c` | 2,588,772 B |
| `src/common/font/gbk_tbl.c` | 64,408 B |

`gbk_s14.c` embeds the complete glyph bitmap plus position/width/height/pitch
tables. Together these two objects account for roughly 2.65 MiB of immutable
data in every GUI binary, independently of which language is actually selected.

This is a much larger opportunity than compiler size tuning. A future phase
should investigate one or more of:

- moving the full CJK glyph payload to an external resource loaded only when
  required;
- generating per-language or translation-corpus glyph subsets at build time;
- keeping only a small built-in fallback font and loading additional glyph
  packs on demand;
- sharing/compressing lookup metadata if random glyph access remains cheap.

Any such change must preserve the existing Unicode/translation behavior and
must account for I/O latency on PSP/PS2 before replacing resident tables.

## 3. Static RAM (`.bss`) is the next memory target

Representative large PSP symbols include:

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

## 4. Selective `-Os`: useful, but secondary

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

## 5. PS2 observations

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

## 6. Recommended order for future size work

1. Externalize or subset the ~2.65 MiB embedded CJK font payload.
2. Audit large `.bss` buffers for mutually-exclusive or lifecycle-scoped use.
3. Audit embedded PS2 IRX payloads and their post-load lifetime.
4. Apply selective `-Os` to measured cold translation units.
5. Re-measure performance and memory after every step; keep emulation/rendering
   hot paths optimized for speed.

R10 itself deliberately stops at measurement. Its runtime-memory improvement
comes from empirical allocation-shape probing, not from changing optimization
flags or removing assets.
