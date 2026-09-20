# NJEMU remaining work plan

Status: 2026-09-20

This document records the work that remains after completion of:

- `docs/RUNTIME_COVERAGE_AUDIT.md`;
- `docs/EXHAUSTIVE_LOADER_AUDIT.md`.

Those audits are closed and should not be reopened merely to accumulate more
equivalent ROM coverage. The work below is deliberately separated into product
bugs, performance work, corpus-dependent validation and optional hardening.

## Priority overview

Recommended order:

1. investigate and fix the non-deterministic Desktop teardown SIGSEGV;
2. implement and benchmark the PS2 MVS cache-I/O improvements already designed
   in `docs/MVS_CACHE_IO_INVESTIGATION.md`;
3. make the GUI resolution-independent, with PS2 as the first non-PSP layout;
4. extend the optimized cache path to CPS2 if MVS measurements justify it;
5. dynamically close the remaining MVS corpus gaps when valid source ROMs are
   available;
6. run longer functional/soak validation as a final hardening pass.

The first three items are the highest-value engineering work. Item 5 is blocked
by input data rather than missing emulator logic.

---

## Phase A - Desktop teardown SIGSEGV

### Why this remains

The exhaustive MVS sweep repeatedly observed exit status 139 after otherwise
successful game initialization. The failing title changed between runs:

- `fatfury1`;
- `aof2`;
- `fatfursp`;
- `mosyougi`;
- `irrmaze`;
- `popbounc`;
- `pbobbl2n`;
- `mslug5`;
- and other successful loader cases depending on the run.

The failure moving between games is strong evidence that this is a Desktop
lifecycle/teardown defect rather than a game-specific MVS loader failure.

### A0 - Reproduce under diagnostics

- build the Desktop MVS target with debug symbols;
- make the automated exit path deterministic;
- run repeated short launch/exit loops over several small and large MVS sets;
- collect the first useful native backtrace;
- repeat with AddressSanitizer where the current dependencies permit it;
- use UBSan as a secondary signal if ASan changes timing too much.

Do not classify a ROM as failing unless the crash happens before successful
loader/init completion or can be reproduced specifically with that ROM.

### A1 - Determine ownership/lifetime failure

Audit shutdown ordering around:

- emulation thread / ticker shutdown;
- audio shutdown;
- renderer/UI destruction;
- cache/file handles;
- CPU/timer state;
- save-state/progress helpers;
- globals freed by both platform and core cleanup paths.

Specifically look for:

- use-after-free;
- double free;
- callbacks firing after their owner has been destroyed;
- races between Desktop ticker/audio threads and core teardown;
- shutdown functions that are not idempotent;
- pointers retained across a core reset or driver exit.

### A2 - Fix and regression-test

The fix should be minimal and preserve platform ownership boundaries.

Acceptance criteria:

- no sanitizer finding in the reproduced path;
- at least 100 automated launch/exit iterations without SIGSEGV;
- representative CPS1, CPS2, MVS and NCDZ Desktop launch/exit smoke tests pass;
- GUI and no-GUI Desktop builds still pass;
- no audit-only instrumentation remains in production code.

Deliverable: one focused bug-fix commit plus any reusable regression harness that
is suitable for the repository.

---

## Phase B - PS2 MVS cache-I/O performance

The detailed investigation and design already exist in:

- `docs/MVS_CACHE_IO_INVESTIGATION.md`.

That document should remain the technical authority for this phase. The outline
below is a project roadmap, not a replacement for its measurements and design.

### B0 - Baseline and instrumentation

Instrument MVS cache misses without changing behavior.

At minimum measure:

- cache hits and misses;
- C-ROM and PCM reads;
- requested block index and file offset;
- sequential vs non-sequential requests;
- number of `lseek()` calls;
- bytes read;
- time spent in cache miss handling.

On PS2, add enough I/O-side counters to correlate NJEMU misses with filesystem
and block-device operations.

Primary representative title: `mslug3`.

Acceptance criteria:

- repeatable baseline captured in PCSX2;
- repeatable baseline captured on real PS2;
- instrumentation can be disabled without changing normal behavior.

### B1 - Low-risk NJEMU optimizations

Implement the cheap optimizations identified by the investigation:

- avoid redundant seeks during sequential `fill_cache()`;
- track the expected raw-cache file position;
- skip `lseek()` when the descriptor is already at the requested block;
- apply the same reasoning to PCM cache reads where appropriate.

Measure each change independently before combining them.

Acceptance criteria:

- cache contents remain bit-identical;
- no regression in MVS loader/cache tests;
- fewer seek/RPC operations are observed;
- startup and gameplay timings are recorded on real PS2.

### B2 - FatFs FastSeek experiment

Evaluate `FF_USE_FASTSEEK` as an isolated PS2SDK experiment.

This is a measurement phase, not an assumed final solution. Record:

- random/backward seek cost;
- memory overhead;
- effect on a 64 MiB contiguous `crom`;
- whether 64 KiB logical reads are still split by filesystem cluster boundaries.

Do not merge a global PS2SDK configuration change unless the measured tradeoff
is clearly acceptable for other users of the filesystem.

### B3 - Extent-aware cache reader prototype

Prototype the direct large-file path described by
`MVS_CACHE_IO_INVESTIGATION.md`:

- obtain and retain the physical fragment/extents of the cache file;
- read an arbitrary logical 64 KiB cache block through the extent map;
- bypass per-cluster `f_read()` splitting for aligned full-block reads;
- transfer data safely from IOP to EE;
- support reads crossing extent boundaries;
- retain the normal filesystem implementation as fallback.

The prototype must not assume that every cache file is contiguous.

Acceptance criteria:

- byte-for-byte equality with the existing raw cache reader;
- fragmented-file correctness;
- safe failure/fallback if the optimized backend cannot be opened;
- no dependency on the particular PCSX2 USB image layout.

### B4 - Integrate through ps2_drivers

Once the prototype proves useful, expose the optimized reader through a clean
PS2 storage abstraction rather than embedding PS2SDK internals throughout
NJEMU.

Expected properties:

- explicit open/read-at/close lifecycle;
- normal POSIX/fileXio backend remains available;
- the core cache code does not need to know FAT32/SCSI details;
- backend selection is compile-time or capability-driven, not game-specific.

### B5 - Benchmark and decide

Compare at least:

- current baseline;
- NJEMU seek-elision only;
- FastSeek experiment;
- extent-aware reader;
- optional prefetch only after the above are understood.

Measure:

- average miss latency;
- p95/p99 miss latency where practical;
- startup time;
- in-game stutter;
- I/O command count;
- CPU overhead;
- memory overhead.

Use PCSX2 for repeatable counters/correctness and real PS2 for the performance
decision.

### B6 - Extend to CPS2

Only after the MVS path is stable, audit CPS2 cache access against the same
storage abstraction.

Do not blindly share assumptions: first verify CPS2 block sizes, access pattern,
compression/raw-cache behavior and lifetime.

Acceptance criteria:

- existing CPS2 raw/folder/ZIP behavior remains correct;
- optimized path is used only where its storage semantics match;
- representative large CPS2 titles show measured benefit on PS2.

### B7 - PSP follow-up

PSP is explicitly secondary to PS2.

Reuse the storage abstraction if profiling shows an equivalent bottleneck, but
do not port the PS2 extent implementation merely for architectural symmetry.

---

## Phase C - Resolution-independent / responsive GUI

### Why this remains

The GUI still inherits the PSP's fixed 480x272 coordinate system even on
platforms with a different physical output resolution.

The current PS2 implementation makes that mismatch explicit:

- `src/ps2/ps2.h` defines `SCR_WIDTH=480` and `SCR_HEIGHT=272`;
- `src/emumain.c` builds `full_rect` directly from those constants;
- the PS2 GS output is configured to 448 lines and uses the gsKit screen width,
  so the physical output and the common GUI coordinate space are different;
- `src/common/ui.c`, `ui_menu.c`, `filer.c`, and `cmdlist.c` contain many
  positions derived directly from PSP-era constants such as 240/136,
  469/479/270 and fixed row pitches;
- `ps2_ui_draw.c` also clips UI primitives against 480x272.

This means the current PS2 GUI is effectively a PSP-sized layout rendered on a
larger output rather than a layout that understands the target display.

The goal is not to special-case a larger PS2 menu. The goal is to make the
common GUI independent of the physical resolution while keeping the existing
PSP appearance unchanged.

### Implementation status

The first responsive-GUI implementation pass is now in place:

- `fd9fb36 Introduce resolution-independent UI viewport`
- `66a7f19 Reflow GUI to native output size`
- `9e283b0 Reflow secondary GUI screens`
- `8e4d3ac Finish native-aware GUI chrome`
- `f7df684 Simplify responsive UI layout state`
- `f9fdf29 Add live Desktop GUI resize validation`
- `311d74c Wire PSP GUI matrix into CI`
- `0301587 Add responsive UI layout regression coverage`
- `8109848 Fix PNG state buffer pointer alignment`
- `ca78316 Add feature-on GUI CI coverage`

Completed in that pass:

- a common `ui_layout` layer owns logical/output dimensions and layout helpers;
- PSP remains naturally 480x272 because its physical output is still 480x272;
- PS2 now lays out GUI content against the real 640x448 NTSC framebuffer instead
  of treating 480x272 as the screen;
- Desktop lays out against its physical output dimensions;
- game/render targets and save-state thumbnail payloads remain at their existing
  sizes rather than being enlarged with the GUI;
- common chrome, dialogs, popups, scrollbars, main menu, file selector, command
  list, help and save-state panels have been migrated away from PSP screen-edge
  and center constants;
- NCDZ `title_x.sys` previews and save-state previews are anchored relative to
  the current output;
- the legacy 480x272 background cache is used only as a compact backing surface;
  resolution-dependent chrome is drawn after presentation at native coordinates;
- non-legacy output sizes use complete GUI redraws instead of the old partial
  copy path, preventing logical/physical rectangle mixing;
- live Desktop resize is supported, and output-size changes force a common
  `UI_FULL_REFRESH`; the legacy partial-refresh copy path is now PSP-only.

Validation completed so far:

- Desktop GUI builds pass for CPS1, CPS2, MVS and NCDZ;
- PS2 GUI builds pass for CPS1, CPS2, MVS and NCDZ;
- PS2 `GUI+SAVE_STATE+COMMAND_LIST` builds pass;
- Desktop and PS2 no-GUI smoke builds remain valid;
- PCSX2 directly booted the PS2 ELF and visually confirmed the native 640x448
  layout: full-width header and correctly centered native-coordinate dialog;
- a temporary pre-refactor Desktop comparison confirmed that the existing
  Desktop shadow-atlas artifacts predate this responsive-layout work;
- live Desktop validation passed at 480x272, 800x600 (4:3) and 960x540 (16:9);
  the file browser reflows to use the additional vertical space and keeps its
  scrollbar anchored to the current right edge;
- Desktop, PS2 and PSP workflows now pass their `gui` matrix value to CMake, so
  `GUI=OFF` and `GUI=ON` are distinct builds instead of duplicate defaults;
- `ui_layout_tests` is part of Desktop CTest and passes in both GUI and no-GUI
  configurations for the 480x272, 640x448, 800x600 and 960x540 layout cases plus
  the aspect-preserving logical/output transform;
- all four Desktop cores and all four PS2 cores compile with
  `GUI=ON + SAVE_STATE=ON + COMMAND_LIST=ON`; targeted feature-on jobs now cover
  the same combination in Desktop, PS2 and PSP CI without exploding the normal
  Cartesian matrix;
- enabling those feature combinations exposed a 64-bit PNG scratch-buffer pointer
  truncation; it is fixed portably with `uintptr_t` in Desktop/PS2/PSP.

The local environment does not currently contain a PSP toolchain, so the final
PSP compile/runtime confirmation remains a CI/hardware validation item. The CI
configuration now contains both normal GUI-on jobs and feature-on GUI jobs, and
the PSP layout path itself remains the identity 480x272 case.

Phase C implementation is complete and its policy is settled:

- common GUI layout always derives from the active platform output dimensions;
- PS2 continues to use its current autodetected GS mode and the GUI consumes the
  resulting `gsGlobal->Width/Height`;
- adding user-selectable PS2 480p/widescreen modes is deliberately **not** part
  of this phase because those modes also change GS timing, interlace/field mode,
  DW/DH and framebuffer requirements; that work should be treated as a separate
  presentation feature, not as a GUI-layout change;
- the only remaining validation item is execution of the corrected PSP GUI CI
  matrix and, ideally, a quick real-PSP visual smoke test.

### C0 - Inventory fixed layout assumptions

Audit all GUI code for hard-coded screen geometry and classify each use as:

- viewport edge;
- horizontal/vertical center;
- safe margin;
- list/content bounds;
- row/column spacing;
- dialog size;
- animation origin;
- source texture/buffer geometry;
- game-render geometry that must **not** be changed with the GUI.

At minimum include:

- `src/common/ui.c`;
- `src/common/ui_draw.c`;
- `src/common/ui_menu.c`;
- `src/common/filer.c`;
- `src/common/cmdlist.c`;
- save-state UI;
- per-core menu helpers;
- PSP, PS2 and Desktop UI backends.

Do not mechanically replace every occurrence of 480 or 272. Some values belong
to emulated video modes, textures or source clips and are unrelated to layout.

Deliverable: a short matrix of fixed-layout sites and their semantic replacement.

### C1 - Introduce common display/layout metrics

Add a small common UI metrics abstraction instead of exposing platform
`SCR_WIDTH` / `SCR_HEIGHT` throughout layout code.

It should provide at least:

- physical/output width and height;
- logical UI viewport width and height;
- center coordinates;
- configurable/safe margins;
- content rectangle;
- scale factor(s) when a logical design coordinate is transformed;
- helpers for right/bottom anchoring and centering.

The platform/video layer should supply the actual output metrics. PSP remains
480x272 and therefore becomes the compatibility baseline with no visual change.

Avoid a design where common UI code directly reaches into gsKit/SDL/PSP state.

### C2 - Define scaling policy

Separate two concepts explicitly:

1. **layout reflow**: using additional width/height for lists, dialogs and
   margins;
2. **visual scaling**: scaling fonts, icons, borders and other UI artwork.

A pure 480x272 texture stretched to every output would technically fill the
screen but would not be a responsive GUI and would blur non-integer scales.

Preferred policy:

- preserve aspect ratio by default;
- support platform safe areas / overscan margins;
- anchor title bars, scrollbars and status elements to viewport edges;
- derive list row count from available vertical space;
- center dialogs from current viewport dimensions;
- keep font/icon scaling discrete or otherwise quality-preserving where
  possible;
- allow unused extra space rather than distorting UI elements.

PS2 should be the first validation target because its 640-ish x 448 output makes
the PSP assumptions visible. Desktop should then be used as an easy way to test
multiple arbitrary resolutions.

### C3 - Decouple GUI framebuffer geometry from emulator render geometry

Today `full_rect` and `SCREEN_BITMAP` are intertwined with the 480x272 GUI
assumption. Before increasing GUI dimensions, define which buffers represent:

- emulated game render targets;
- GUI/background snapshots;
- physical presentation buffers.

Do not enlarge every emulator render texture simply because the UI is larger.
That would waste scarce PS2 VRAM/EE RAM and could change core rendering.

Where possible:

- keep game source/render textures at their existing required sizes;
- let UI primitives render in output/layout coordinates;
- allocate GUI snapshot/scratch buffers according to their actual role;
- keep copy/restore operations explicit about source and destination rectangles.

Acceptance criteria:

- PSP retains its existing memory footprint unless a change is justified;
- PS2 does not gain an unnecessary full-resolution copy of every legacy buffer;
- save-state thumbnails and frame-copy paths remain correct.

### C4 - Convert common screens to anchors and derived dimensions

Migrate one screen family at a time.

Suggested order:

1. common background/title bar and popup/dialog helpers;
2. main menu;
3. file selector;
4. option/configuration menus;
5. command list;
6. save/load-state UI;
7. cheat/DIP/input configuration screens;
8. less frequently used dialogs.

Replace magic geometry with semantic layout values, for example:

- `viewport.right - scrollbar_width` instead of `469`;
- `viewport.center_x` instead of `240`;
- `viewport.center_y` instead of `136`;
- calculated visible rows instead of PSP-fixed row counts where practical.

Keep migration commits screen-focused so visual regressions are easy to bisect.

### C5 - Font, icon and asset strategy

The existing fonts/icons were designed around the PSP-scale UI. Determine which
assets can remain at native pixel size and which need scalable presentation.

Requirements:

- do not scale source texture atlases unnecessarily;
- allow destination-size scaling through `ui_draw_driver` where quality is
  acceptable;
- preserve crisp text at common target scales;
- avoid a PS2-only duplicate of the complete UI asset set unless measurements
  or visual quality require it;
- keep CJK font paths in the validation matrix.

If a larger font tier is required, introduce it as a common UI capability rather
than hard-coding a PS2 font.

### C6 - Make clipping/input follow the same viewport

Scissor/clipping must use current UI/output metrics rather than 480x272. The PS2
backend currently clips against `SCR_WIDTH` / `SCR_HEIGHT`, so this must be
updated together with the layout abstraction.

If pointer/touch/mouse input is present or added on a platform, screen-to-UI
coordinate conversion must use the exact same viewport transform as rendering.
Pad navigation itself should remain layout-independent.

### C7 - Multi-resolution validation

Use Desktop as the fast visual/debug target and PS2/PCSX2 as the primary console
target.

At minimum validate:

- 480x272 compatibility baseline;
- current Desktop output;
- PS2 NTSC output;
- at least one 4:3 mode;
- at least one 16:9 mode;
- a larger arbitrary Desktop window to expose remaining absolute coordinates.

For every mode inspect:

- title/header alignment;
- menus and highlighted rows;
- scrollbar anchoring;
- long localized strings;
- dialogs/popups;
- command list;
- file browser;
- save-state UI;
- no clipping outside the viewport;
- no distortion caused by independent X/Y scaling.

Where practical, add screenshot/reference tests for the common layout transform
and a few representative screens so future PSP-era constants cannot silently
return.

### C8 - PS2 presentation and configuration follow-up

After the layout is resolution-independent, decide whether PS2 should expose
selectable presentation modes (for example 4:3 vs widescreen/safe-area policy)
or simply derive layout from the active video mode.

This is intentionally last: first make layout independent of resolution, then
add user-facing mode selection if it is still useful.

Acceptance criteria for Phase C:

- PSP GUI is visually equivalent to the current 480x272 baseline;
- PS2 GUI uses the available display area intentionally instead of appearing as
  a PSP-sized layout;
- common screens contain no screen-edge/center magic numbers that depend on
  480x272;
- resizing/changing the Desktop target exercises the same common layout code;
- no core game-render dimensions are coupled to the new GUI dimensions;
- GUI-enabled builds pass for PSP, PS2 and Desktop.

---

## Phase D - MVS corpus-dependent dynamic closure

These are not known emulator defects and must not block other work.

### D1 - `pbobblen`

Current local set is incomplete. Shared ROM data is missing:

- `068-v1`;
- `068-v2`;
- `068-c1`;
- `068-c2`;
- `068-c3`;
- `068-c4`.

When a valid complete set is available:

1. validate it with the existing loader;
2. generate any required cache using the repository converter;
3. run raw/folder/ZIP cases only if the set actually exercises distinct policy;
4. record the result in the exhaustive audit addendum only if it changes a
   previously static-only conclusion.

Do not weaken ROM validation to accept an incomplete set.

### D2 - `ms5pcb`

The current `268-p1r.bin` and `268-p2r.bin` inputs are zero-filled and
invalid.

When valid P-ROMs are available:

- validate PCB CPU1/USER1/decrypt/init execution dynamically;
- confirm the already-audited PCB-specific protection/fix/cache behavior;
- compare against the static matrix recorded in the exhaustive audit.

Do not add compatibility code for the invalid zero-filled files.

Acceptance criteria for Phase D:

- both blockers are either dynamically validated with valid data or continue to
  be documented explicitly as corpus blockers;
- no source workaround is introduced solely to make bad ROM data pass.

---

## Phase E - Functional and soak hardening

This is optional quality work beyond branch coverage.

The completed audits prove loader/init/cache and semantically distinct runtime
branches. They do not prove that every title can run for hours without a later
state/lifecycle issue.

### E0 - Representative play/soak matrix

Choose a small set per core that stresses different subsystems:

- CPS1: ordinary, QSound, bootleg/conversion, multiplayer;
- CPS2: large cache user, multiplayer, paddle/analog, Phoenix/decrypted;
- MVS: raw cache, ZIP/folder cache, protected/encrypted, analog/special input;
- NCDZ: raster title, later-load title, patched title, large/multi-stage CD load.

For each representative case test:

- boot to gameplay;
- several scene/stage transitions;
- audio continuity;
- pause/menu transitions where supported;
- save/load state where supported;
- memcard/NVRAM persistence where relevant;
- repeated exit/relaunch.

### E1 - Long-running stress

After the Desktop teardown issue is fixed, run repeated automated or semi-
automated sessions looking for:

- leaks;
- resource-handle growth;
- stale cache state;
- timer drift;
- audio starvation;
- renderer corruption after repeated resets/state loads.

This phase should produce bugs only when there is a concrete reproduction. It
should not become another requirement to execute every clone.

---

## Work explicitly considered complete

Do not schedule these again unless a regression or new feature changes the
implementation:

- runtime/game-dependent branch coverage for CPS1/CPS2/MVS/NCDZ;
- exhaustive loader/decrypt/init/cache matrix;
- CPS2 raw/ZIP/folder cache validation;
- CPS2 Phoenix/decrypted and USER1/key coverage;
- MVS runtime/converter cache-policy parity;
- MVS KOF2003H program decrypt and PVC protection;
- MVS mixed parent/clone cache fallback;
- NCDZ initial IPL file-type coverage;
- NCDZ AOF3 / Last Blade / Last Blade 2 stateful later-load handlers;
- final Desktop/PS2 build matrix recorded in
  `docs/EXHAUSTIVE_LOADER_AUDIT.md`.

---

## Definition of overall completion

The remaining roadmap can be considered closed when:

1. the Desktop teardown crash has a root cause, fix and repeated-exit regression
   coverage;
2. the PS2 MVS cache path has been measured and either:
   - materially improved and integrated, or
   - demonstrated not to justify further complexity with measurements recorded;
3. the common GUI is resolution-independent, preserving PSP while using PS2 and
   Desktop output dimensions intentionally;
4. CPS2 has been evaluated against the resulting storage solution;
5. corpus blockers remain accurately documented or are dynamically closed once
   valid ROM data becomes available;
6. a representative functional/soak pass shows no new reproducible core issue.

The priority is measurable product correctness and PS2 performance, not
increasing ROM-count statistics after semantic coverage is already complete.
