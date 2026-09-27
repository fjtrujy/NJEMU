# Platform Driver and Include Architecture Refactor Plan

## 1. Purpose

NJEMU started as a PSP-only codebase and was progressively adapted to PSP, PS2,
and Desktop. The current driver layer successfully made all four emulator targets
build on all three platforms, but part of the original PSP structure still leaks
through the supposedly portable layers.

This plan makes the platform boundary explicit and smaller. The objective is not
to add abstractions for their own sake. The objective is to move semantic and
algorithmic behaviour into `src/common/` and target code, leave only hardware/OS
mechanics in `src/<platform>/`, and make dependencies visible through narrow
headers rather than umbrella includes and transitive platform headers.

The current source tree is authoritative. `PORTING_PLAN.md` is useful historical
context but contains stale intermediate states and must not override this plan.

## 2. Goals

1. Common code must not select PSP/PS2/Desktop implementations with platform
   preprocessor branches.
2. Platform vtables must describe genuinely platform-dependent operations, not
   PSP-specific historical concepts exposed to every backend.
3. Target-specific behaviour must not change the shape of a generic platform
   driver interface.
4. Hardware-independent input, rendering, timing, menu, and lifecycle logic
   should live in common code where practical.
5. Target renderers must not reach through `video_driver_t` to native SDK objects.
6. `#include` dependencies should be direct, minimal, and acyclic.
7. Platform SDK headers must remain inside platform implementation directories,
   except for intentionally platform-specific target adapters that are scheduled
   for removal.
8. Every phase must preserve PSP, PS2, and Desktop behaviour and must be validated
   before the next phase begins.

## 3. Non-goals

- This plan does not try to remove legitimate target conditionals such as CPS1 vs
  CPS2 vs MVS vs NCDZ behaviour from emulator-domain code in one sweep.
- It does not replace CMake or change the external resource format.
- It does not redesign CPU cores, sound-chip emulation, ROM loading, or memory
  policy unless a narrow interface cleanup requires it.
- It does not force one rendering implementation when hardware semantics truly
  differ. Common command generation is preferred; backend execution remains
  platform-specific.

## 4. Baseline audit (2026-09-26)

### 4.1 Driver families

The runtime currently has these abstraction families:

- `video_driver_t`
- `audio_driver_t`
- `input_driver_t`
- `platform_driver_t`
- `thread_driver_t`
- `ticker_driver_t`
- `power_driver_t`
- `ui_draw_driver_t`
- `ui_text_driver_t` (already a single common implementation)

The basic architecture is sound: opaque backend data plus function tables is an
appropriate C design for this project. The main problems are contract shape,
binding, duplicated policy, and header ownership rather than the existence of
vtable-based drivers itself.

### 4.2 Backend selection is still preprocessor-driven in common code

`audio_driver.c`, `input_driver.c`, `platform_driver.c`, `power_driver.c`,
`thread_driver.c`, `ticker_driver.c`, and `video_driver.c` each build an array
whose element zero depends on `#ifdef PSP`, `#ifdef PS2`, or `#ifdef DESKTOP`.
Call sites then use macros such as:

```c
#define video_driver video_drivers[0]
```

The arrays are not runtime registries: no caller iterates or changes them. Since
CMake already selects exactly one `PLATFORM`, this is unnecessary indirection and
puts platform selection in the wrong layer.

### 4.3 `platform_driver_t` contains PSP legacy concepts

The interface currently exposes:

- `getDevkitVersion()` -- only used by PSP AdHoc code;
- `getWlanSwitchState()` -- only used for the PSP AdHoc launch path;
- `getHardwareModel()` -- currently unused by production code.

Desktop and PS2 therefore implement meaningless stubs. These functions should
leave the generic platform contract; PSP networking code should own PSP firmware
and WLAN details.

### 4.4 `power_driver_t` is semantically PSP-shaped

Desktop and PS2 allocate empty heap objects and report fake values such as 100%
battery, charging=true, and highest clock=1 solely to satisfy the PSP-era API.
Common config/menu code still uses names such as `PSPClock` and PSP clock enum
values. The capability should be expressed as optional battery/performance
features rather than pretending all platforms are PSPs.

### 4.5 `input_driver_t` changes ABI by emulator target

`pollFatfursp` and `pollAnalog` only exist when `EMU_SYSTEM == MVS`, including in
the driver struct itself. Platform backends therefore contain target conditionals.
This crosses the platform/target boundary in the wrong direction.

The intended replacement is a stable common physical-input state (buttons plus
optional axes/capabilities). MVS-specific interpretation of that state belongs in
common/MVS code, not in PSP/PS2/Desktop drivers.

### 4.6 UI rendering has two overlapping abstraction layers

`ui_draw_driver_t` and `video_driver_t` both expose drawing primitives. On PSP and
PS2, much of `*_ui_draw.c` simply forwards calls back to `video_driver_t`; Desktop
implements them separately because its video backend currently leaves the UI
slots NULL.

The final design should have one owner for low-level drawing. `ui_draw_driver`
should either become a small UI texture-storage adapter over `video_driver_t`, or
be folded into the video backend after the required texture lifecycle semantics
are explicit. Keeping two parallel primitive APIs is not desirable.

### 4.7 Sprite rendering is the largest remaining platform duplication

Each emulator target still has PSP, PS2, and Desktop sprite files. Approximate
exact-line sequence similarity at this baseline:

| Target | PSP vs PS2 | PSP vs Desktop | PS2 vs Desktop |
|---|---:|---:|---:|
| CPS1 | 74.8% | 90.5% | 80.8% |
| CPS2 | 73.5% | 86.2% | 83.0% |
| MVS | 75.5% | 90.2% | 77.8% |
| NCDZ | 74.3% | 87.3% | 77.4% |

PS2 target renderers include `gsKit.h`, allocate native `GSPRIM*` arrays, and use
`video_driver->getNativeObjects()` to recover `GSGLOBAL`/`GSTEXTURE`. This is an
abstraction escape hatch. The target renderer should emit platform-neutral draw
commands/vertices and the video backend should translate them to native objects.

### 4.8 Geometry/constants are owned by platform headers despite being common

`SCR_WIDTH=480`, `SCR_HEIGHT=272`, `BUF_WIDTH=512`, and the nominal refresh rate
are duplicated among PSP/PS2/Desktop headers, while common and target code depends
on them. They describe NJEMU's logical rendering model, not a specific platform.
They should move to a narrow common geometry/config header. Physical output size
must remain backend-provided (important for PS2 and future Vita support).

### 4.9 Include topology is too broad and sometimes circular

`emumain.h` currently:

- includes standard I/O/filesystem headers unrelated to its public declarations;
- conditionally includes `psp/psp.h`, `ps2/ps2.h`, or `desktop/desktop.h`;
- includes most driver headers and target headers;
- is itself included by dozens of unrelated translation units.

`psp/psp.h` in turn includes `emumain.h`, creating a guarded circular dependency.
It also includes many PSP SDK headers, so a source can acquire platform APIs
transitively without declaring the dependency it actually uses.

This is the main reason imports currently feel chaotic.

## 5. Target architecture

### 5.1 Build-time binding, runtime-neutral common code

CMake continues selecting one platform. A tiny source in each platform directory
binds generic pointers to that platform's descriptors:

```text
CMake PLATFORM=PSP
        |
        +--> src/psp/psp_drivers.c
                 video_driver    -> video_psp
                 audio_driver    -> audio_psp
                 input_driver    -> input_psp
                 ...
```

Common driver code no longer contains PSP/PS2/Desktop selection branches or fake
runtime registries.

### 5.2 Stable contracts

Every generic interface has one shape for every target. Optional functionality is
represented by capabilities or nullable operations with common wrapper semantics,
not by changing struct layout with `EMU_SYSTEM`.

### 5.3 Dependency direction

The intended dependency direction is:

```text
emulator target logic
        |
        v
common services / common driver contracts
        |
        v
platform backend implementation
        |
        v
platform SDK / SDL / gsKit / PSP SDK / PS2 SDK
```

No common header should include a platform umbrella header. No target renderer
should require native GPU SDK types once the renderer-unification phase is done.

## 6. Execution phases

### D0 - Baseline and plan [COMPLETE]

- Audit current driver contracts, selection, platform conditionals, includes, and
  sprite backend duplication.
- Record the findings in this document.
- Preserve the current build/test matrix as the regression baseline.

Acceptance:
- authoritative plan exists;
- tracked tree was clean before implementation;
- no resource changes.

### D1 - Replace driver registries with explicit platform bindings [COMPLETE]

Create one `*_drivers.c` binding unit per platform and expose direct generic
pointers from driver headers. Remove `*_drivers[]` arrays and the platform
`#ifdef` selection blocks from common driver sources.

Also:
- remove platform-instance declarations from common public headers;
- keep `null_ui_draw_driver` only for `GUI=OFF` binding;
- delete common driver `.c` files that become truly empty;
- make no behavioural/interface change beyond binding.

Acceptance:
- no PSP/PS2/Desktop preprocessor selection remains in common driver registry
  code;
- GUI and non-GUI builds still select the same implementation;
- all three platforms compile.

Result (2026-09-26):
- added `psp_drivers.c`, `ps2_drivers.c`, and `desktop_drivers.c` as the only
  build-time binding units;
- replaced the `*_drivers[]` registries and `[0]` macros with direct const driver
  pointers;
- removed the now-empty common audio/platform/thread registry sources and all
  platform-selection branches from the remaining common registry code;
- kept the pre-existing PS2-only `pad_wait_clear()` behaviour explicitly out of
  scope for D1 because it is behavioural rather than backend binding;
- validated MVS GUI ON/OFF on Desktop, PS2, and PSP;
- validated Desktop CPS1/CPS2/MVS/NCDZ builds, MVS 10/10 focused CTests, and a
  30-frame MVS runtime smoke.

### D2 - Establish include ownership and common geometry [COMPLETE]

Introduce narrow common headers for:

- logical presentation geometry (`480x272`, common texture pitch where genuinely
  invariant, nominal refresh constants);
- runtime paths/state that platform startup code needs;
- public emulator lifecycle/state declarations currently buried in `emumain.h`.

Then:
- remove platform-header includes from `emumain.h`;
- remove `emumain.h` from files that only need one or two declarations;
- eliminate the `emumain.h` <-> `psp.h` include cycle;
- make each `.c` include its own public header first where practical;
- stop relying on transitive SDK includes;
- replace global `include_directories()` growth with target-scoped include paths.

#### D2a - Common geometry and first transitive-dependency break [COMPLETE]

- added `common/video_geometry.h` as the single owner of the 480x272 logical
  presentation space, 512-pixel logical texture pitch, and nominal refresh rate;
- moved the output-fit scaling helper from `ps2.h` to the common geometry layer;
- removed PSP/PS2/Desktop umbrella includes from `emumain.h`;
- broke the `emumain.h` <-> `psp.h` include cycle;
- made `psp_video.h` depend directly on the video contract/PSP GU API rather than
  importing all of `psp.h`;
- made `video_driver.h` self-contained for `size_t`;
- exposed optional `video_driver_t::readFrame` capability so `common/state.c` no
  longer calls the PS2 readback function directly;
- repaired newly exposed direct dependencies in PSP video and PS2 PNG code.

Validation (2026-09-26):
- MVS GUI ON/OFF builds on Desktop, PS2, and PSP;
- Desktop focused CTests 10/10;
- 30-frame Desktop MVS smoke passes.

#### D2b - Decompose `emumain.h` and remove transitive platform/SDK imports [COMPLETE]

Next, introduce narrow runtime/path/state headers and replace `emumain.h` includes
where a translation unit only needs a small contract. Platform implementation
files must include the SDK headers they actually use. Keep this mechanical and
behaviour-preserving.

Result (2026-09-26):
- deleted the legacy `emumain.h` umbrella and replaced it with narrow common
  contracts for runtime state, options, paths, video descriptors, UI definitions,
  AdHoc transport, and target-specific sound/driver glue;
- removed all production `#include "emumain.h"` dependencies and the remaining
  common-to-platform include leakage;
- made platform implementation dependencies explicit, including PSP video/UI
  SDK dependencies that had previously arrived transitively;
- replaced three platform-identical `*_no_gui.c` files with one
  `common/no_gui.c` implementation;
- made many target/sound headers self-contained instead of relying on umbrella
  ordering, while preserving target conditionals where they express emulator
  semantics rather than host-platform selection.

Validation (2026-09-26):
- Desktop CPS1/CPS2/MVS/NCDZ GUI-OFF builds pass and Desktop MVS GUI-ON passes;
- PSP MVS GUI ON/OFF builds pass and still generate `EBOOT.PBP`;
- PS2 MVS GUI ON/OFF builds pass;
- Desktop MVS focused CTests pass 10/10 and the 30-frame runtime smoke passes;
- `src/common/` contains no PSP/PS2/Desktop SDK or umbrella includes;
- `git diff --check` is clean and no resource file is part of the change.

#### D2c - CMake include-scope cleanup [COMPLETE]

After source/header ownership is explicit, replace directory-wide
`include_directories()` calls with target-scoped include directories and remove
platform-directory search paths that are no longer needed.

Result (2026-09-26):
- removed all directory-wide `include_directories()` calls from the application
  build;
- the NJEMU executable now receives one private project include root (`src/`),
  relying on normal quoted-header same-directory lookup for target/CPU/platform
  local headers;
- SDL2 and libmad include paths are target-private instead of directory-global;
- test executables retain their already-explicit private include scopes.

Validation (2026-09-26):
- Desktop CPS1/CPS2/MVS/NCDZ builds pass, including MVS GUI ON/OFF;
- PSP MVS GUI ON/OFF and PS2 MVS GUI ON/OFF builds pass;
- Desktop MVS focused CTests pass 10/10 and the 30-frame smoke passes;
- generated Desktop MVS compile flags contain only the `src/` project include
  root plus external dependency includes; no target/platform/CPU directory-wide
  paths remain;
- `git diff --check` is clean and no resource file is changed.

Acceptance:
- common headers include no PSP/PS2/Desktop umbrella header;
- platform SDK includes are explicit in platform implementation files;
- `SCR_WIDTH`/`SCR_HEIGHT`/logical pitch have one common definition;
- include-what-you-use cleanup does not change behaviour.

### D3 - Shrink `platform_driver_t` to real platform services [COMPLETE]

Remove `getHardwareModel()` (unused). Move firmware/WLAN logic out of the generic
platform vtable and into the PSP AdHoc backend/service. Keep generic operations
that really differ by host, currently:

- lifecycle/init/main/free;
- memory telemetry;
- system language.

Re-evaluate whether path discovery belongs in the platform vtable or a smaller
startup service.

Result (2026-09-26):
- removed `getHardwareModel()`, `getDevkitVersion()`, and
  `getWlanSwitchState()` from `platform_driver_t` and deleted the fake
  Desktop/PS2 implementations;
- moved PSP firmware-version gating for network-module loading directly into
  the PSP AdHoc transport, where `sceKernelDevkitVersion()` is a real backend
  implementation detail;
- moved the PSP WLAN switch query behind the common AdHoc transport contract
  (`adhocNetworkAvailable()`), so common file-browser code no longer knows
  about PSP platform services;
- removed cached firmware state and the WLAN SDK dependency from
  `psp_platform.c`; the generic platform contract now contains only lifecycle,
  startup, memory telemetry, and system-language services;
- audited the remaining startup/path callback and kept it in the platform
  contract for now because launch-path discovery, screenshot roots, and PSP
  callback setup genuinely differ by host; this can be split later if those
  responsibilities diverge further;
- while exercising the previously under-tested AdHoc configurations, made the
  remaining AdHoc/UI/sound dependencies explicit in CPS1/CPS2/MVS and the PSP
  transport instead of relying on the deleted umbrella header.

Validation (2026-09-26):
- PSP CPS1, CPS2, and MVS builds with `ADHOC=ON`, `GUI=ON`, and
  `SAVE_STATE=ON` all pass and generate EBOOT.PBP;
- normal PSP MVS and PS2 MVS builds pass;
- Desktop MVS build passes, focused CTests pass 10/10, and the 30-frame runtime
  smoke passes;
- no production reference to `getHardwareModel`, `getDevkitVersion`, or
  `getWlanSwitchState` remains; PSP WLAN ownership is confined to
  `src/psp/adhoc.c`.

Acceptance:
- Desktop/PS2 no longer implement PSP-specific stubs;
- common AdHoc code uses a networking/AdHoc capability rather than PSP firmware
  concepts;
- no behavioural regression in PSP AdHoc builds.

### D4 - Redesign physical input around a stable state object [COMPLETE]

Replace target-conditioned `pollFatfursp` / `pollAnalog` callbacks with a stable
physical input sample, for example:

```c
typedef struct input_state {
    uint32_t buttons;
    uint8_t lx;
    uint8_t ly;
    uint8_t axis_flags;
} input_state_t;
```

Platform drivers only report physical state. Common/MVS code owns:

- analog dead-zone to digital conversion;
- the FatFury special direction rule;
- raw analog packing needed by Irritating Maze / Pop 'n Bounce;
- controller-index routing.

Acceptance:
- no `EMU_SYSTEM` appears in `input_driver.h` or platform input backends;
- PSP, PS2 multitap, and Desktop input retain current semantics;
- menu hotkey remains common (`START+SELECT`).

Result (2026-09-26):
- added `common/input_state.h/.c` as the platform-neutral owner of physical
  buttons, optional left-stick axes, neutral/dead-zone constants, reset, and
  analog-to-digital conversion;
- replaced the target-shaped `poll`, `pollFatfursp`, and `pollAnalog` input
  vtable entries with one stable `sample()` callback on PSP, PS2, and Desktop;
- moved normal analog-direction conversion and the Fatal Fury Special opposite
  direction rule out of platform backends; MVS now owns its special polling
  modes while Irritating Maze / Pop 'n Bounce consume raw axes directly;
- removed all `EMU_SYSTEM` conditionals from `input_driver.h` and the three
  platform input backends, so target selection no longer changes the platform
  driver ABI;
- preserved PS2 controller-index/multitap routing through the common sample
  contract and kept `START+SELECT` in the common menu-combo path;
- eliminated the old PSP analog-mode mismatch where portable button bits were
  overwritten with raw `PSP_CTRL_*` bits before MVS consumed the packed axes;
- added focused `input_state_tests` for neutral reset, digital preservation,
  dead-zone thresholds, and absent-axis behaviour.

Validation (2026-09-26):
- Desktop CPS1/CPS2/MVS/NCDZ builds pass; focused non-memory-plan suites pass
  11/11, 11/11, 11/11, and 12/12 respectively;
- Desktop MVS GUI build passes its full 13/13 CTest suite and the 30-frame MVS
  runtime smoke passes;
- PSP CPS1/CPS2/MVS/NCDZ Release builds pass and generate EBOOT.PBP;
- PSP MVS with GUI, AdHoc, save states, and command list enabled passes;
- PS2 CPS1/CPS2/MVS/NCDZ Release builds pass and PS2 MVS GUI build passes;
- `git diff --check` is clean and no resource file is part of the change.

### D5 - Replace PSP-shaped power semantics with capabilities [COMPLETE]

Separate battery telemetry from performance control. Add explicit capability
semantics instead of fake Desktop/PS2 implementations.

Candidate model:

- battery status: supported/unsupported + percentage/charging;
- performance profiles: optional list/level selected by common UI/config;
- common `platform_performance_level` naming instead of `PSPClock` internally;
- migration compatibility for existing INI key `PSPClock` if required.

Desktop and PS2 should use a common no-op backend or unsupported capability rather
than allocate empty objects and return invented values.

Acceptance:
- no fake 100% charging battery on non-battery platforms;
- PSP clock behaviour remains identical;
- platform-specific menu visibility is capability-driven, not `#if PSP`.

Result (2026-09-26):
- replaced the stateful PSP-shaped power vtable with two explicit optional
  capabilities: `POWER_CAP_BATTERY` and `POWER_CAP_PERFORMANCE`;
- removed power-driver init/free state entirely: the service is stateless, PSP
  exposes the real implementation, and Desktop/PS2 bind the common
  `power_unsupported` descriptor instead of allocating empty objects or returning
  invented battery/clock values;
- introduced common battery/performance helpers and renamed the shared setting to
  `platform_performance_level`; common call sites no longer know PSP clock enum
  names or invoke platform callbacks directly;
- retained `PSPClock` only as a legacy on-disk INI key. PSP accepts both the
  historical level values (`0..3`) and MHz-style values (`222/266/300/333`),
  while platforms without `POWER_CAP_PERFORMANCE` neither consume nor serialize
  the setting;
- removed `#if PSP` from the game-configuration menu. Capability-tagged menu
  entries are compacted by the common menu layer, so the CPU-clock option and its
  spacer appear only when the selected backend supports performance control;
- common menu code now uses the generic `CPU_CLOCK` alias while the stable V2
  translation schema deliberately retains historical key `PSP_CLOCK` at text ID
  117, avoiding an unnecessary language-pack compatibility break;
- removed `desktop_power.c`, `ps2_power.c`, and `psp_power.h`; PSP's backend now
  directly owns the only remaining `<psppower.h>` dependency needed for clock and
  battery operations (the platform callback code keeps its own direct SDK include);
- added `power_driver_tests` covering capability queries, battery telemetry,
  performance-level clamping, lowest-level selection, and highest-level reporting.

Validation (2026-09-26):
- Desktop CPS1/CPS2/MVS/NCDZ builds pass; CPS1 passes the 12-test non-memory-plan
  suite including translation/font validation, and focused portable suites pass
  9/9 for CPS2/MVS and 10/10 for NCDZ;
- Desktop MVS 30-frame runtime smoke passes;
- PSP MVS GUI OFF and GUI ON builds pass and generate EBOOT.PBP;
- PS2 MVS GUI OFF and GUI ON builds pass;
- translation source/pack validation passes with the existing V2 schema hash;
- `git diff --check` is clean and no resource file is part of the change.

### D6 - Collapse overlapping UI drawing abstractions [COMPLETE]

Define a single ownership model for low-level 2D primitives. Preferred direction:

- `video_driver_t` owns actual draw primitives and GPU submission;
- common `ui_draw.c` owns UI semantics/layout;
- a much smaller UI texture-storage adapter remains only if PSP/PS2/Desktop need
  genuinely different staging/upload lifetimes.

Remove PSP/PS2 forwarding wrappers that only call the corresponding
`video_driver` method. Move Desktop primitives into the same low-level owner.

Acceptance:
- one primitive API, not two parallel ones;
- GUI builds pass on PSP/PS2/Desktop;
- no loss of PS2 full-refresh/sharpness behaviour or PSP batching behaviour.

Result (2026-09-26):
- made `video_driver_t` the single owner of low-level UI sprite/line/rectangle,
  gradient, fill, and clipping primitives; common `ui_draw.c` now performs
  layout/semantic work and submits those primitives directly to the video
  backend;
- reduced `ui_draw_driver_t` to UI presentation policy/capabilities plus texture
  storage, upload, native-texture resolution, and draw-lifetime synchronization;
  the former parallel primitive API and all PSP/PS2 forwarding wrappers are
  gone;
- moved Desktop's SDL primitive implementation into `desktop_video.c`, matching
  the same ownership model as PSP and PS2 instead of keeping Desktop as a special
  second renderer;
- preserved backend-specific texture lifetime behaviour behind
  `prepareTextureDraw()` / `finishTextureDraw()`: Desktop lazily refreshes SDL
  textures, PSP flushes/synchronizes its mutable font scratch, and PS2 retains
  its batched glyph ring plus oversized-scratch synchronization path;
- moved UI clipping to the video backend with explicit x/y/width/height
  semantics, while retaining the emulator renderer's existing edge-coordinate
  scissor callback separately;
- removed the unused historical UI sprite tint parameter and clarified that the
  remaining UI driver is a texture/presentation adapter rather than a second
  low-level renderer;
- verified a fresh PSP MVS `GUI=OFF` Release build against pre-D6 `HEAD`: PRX
  size changes from 891242 to 891162 bytes and PBP from 900511 to 900431 bytes,
  so consolidating primitive ownership does not impose a no-GUI size penalty.

Validation (2026-09-26):
- Desktop CPS1/CPS2/MVS/NCDZ GUI-OFF builds pass and Desktop MVS GUI-ON passes;
- Desktop MVS GUI build passes its full 14/14 CTest suite and the 30-frame MVS
  runtime smoke passes;
- PSP MVS GUI ON/OFF builds pass and generate EBOOT.PBP;
- PS2 MVS GUI ON/OFF builds pass;
- no draw primitive remains in `ui_draw_driver_t` or the three platform UI
  texture adapters, and `src/common/` remains free of platform SDK includes;
- `git diff --check` is clean and no resource file is part of the change.

### D7 - Unify sprite renderers behind portable draw data

This is the largest phase and must be done target-by-target, not as one rewrite.
Recommended order: NCDZ -> MVS -> CPS1 -> CPS2.

For each target:

1. define platform-neutral sprite/point vertex structures and draw batches;
2. keep sprite decoding, clipping, priority grouping, atlas placement, palette
   selection, and batching in target-common code;
3. make the video backend translate common batches to sceGu, gsKit, or SDL;
4. remove `getNativeObjects()` use for that target;
5. delete the three platform-specific target sprite files once equivalent.

CPS2 comes last because its depth/priority masking is the most specialized.

Performance rule for this phase: unification must share emulator/render semantics,
but it must not require an avoidable extra materialization pass merely to make the
code look uniform.  The preferred end state is **common semantics + backend-native
execution**.  A portable intermediate representation is appropriate where it is
either consumed directly (as on PSP GU today), materially reduces retained memory,
or has measured negligible cost.  Backend-specific materialization/command
submission remains valid when it is the faster representation, provided target
renderers do not regain platform SDK dependencies or platform `#if`s.

Acceptance per target:
- one target sprite implementation instead of PSP/PS2/Desktop copies;
- no platform SDK include in that target's renderer;
- visual/screenshot and runtime tests preserved;
- binary/memory impact measured on PSP and PS2;
- no unexplained measurable renderer regression versus the previous platform-native
  implementation.  CPU/render submission cost must be measured whenever a portable
  representation adds staging, conversion, or copying.

#### D7a - NCDZ portable sprite renderer [COMPLETE]

Result (2026-09-26):
- replaced `ncdz/{psp,ps2,desktop}_sprite.c` with one platform-neutral
  `ncdz/sprite.c` that owns tile decoding, atlas placement, clipping, palette
  grouping, sprite batching, flip semantics, and presentation rectangles;
- added portable indexed-atlas updates and sprite-vertex batches to
  `video_driver_t`. PSP owns swizzled T8 writes and GU submission, PS2 owns
  linear T8 writes and conversion to gsKit vertices, and Desktop owns indexed
  expansion plus SDL submission;
- removed all NCDZ access to `getNativeObjects()` and all PSP/PS2/SDL SDK types
  from the target renderer; the only remaining `getNativeObjects()` consumers
  are the not-yet-migrated MVS/CPS1/CPS2 PS2 renderers;
- moved physical output-size reporting to `video_driver_t`, where it belongs,
  and removed the duplicate output-size callback from the UI texture adapter;
- unified `StretchScreen` indices with the common NCDZ menu. This also repairs
  Desktop's historical extra leading clip entry, which shifted every configured
  preset and made the final 16:9 preset unreachable with the configured `0..5`
  range;
- while exercising NCDZ GUI configurations, made `common/filer.c` and
  `common/config.c` explicitly include the geometry/NCDZ/CDDA declarations they
  consume instead of inheriting them transitively.

Validation (2026-09-26):
- NCDZ GUI OFF and GUI ON builds pass on Desktop, PSP, and PS2;
- Desktop NCDZ passes 14/14 CTests and a 30-frame Windjammers runtime smoke;
- PSP Release GUI-OFF baseline vs D7a: `.text` +1,400 B, `.data` -32 B,
  `.bss` unchanged, ELF +1,416 B, PRX +1,384 B;
- PS2 Release GUI-OFF baseline vs D7a: `.text` +1,768 B, `.data` +8 B,
  `.bss` -539,536 B, total runtime image -537,760 B, ELF +1,624 B. The large
  BSS reduction comes from replacing target-owned `GSPRIMUVPOINTFLAT` vertex
  arrays with the compact portable vertex representation;
- `git diff --check` is clean and no resource file is part of the change.

#### D7b - MVS portable sprite renderer [COMPLETE]

Result (2026-09-26):
- replaced `mvs/{psp,ps2,desktop}_sprite.c` with one platform-neutral
  `mvs/sprite.c` built on the indexed-atlas and portable sprite-batch contract
  introduced by D7a;
- kept MVS-specific C-ROM cache behaviour in target-common code: atlas keys,
  `read_cache()` source translation, `update_cache()` reuse notifications, and
  the per-frame sprite-disable fallback when no atlas entry can be reclaimed;
- moved draw-list counters, palette binding and the sprite-disable state out of
  `sprite_common` and into the renderer that owns them. Removed dead clip state
  that was written by all three legacy renderers but never read;
- removed the MVS PS2 dependency on `getNativeObjects()`, gsKit vertex types and
  native texture objects. MVS now contains no PSP/PS2/SDL renderer API types;
- unified `StretchScreen` indices with `common/menu/mvs.c`, fixing the same
  historical Desktop extra-entry/index shift found during D7a;
- reused backend-owned swizzled T8 writes on PSP, linear indexed writes on PS2,
  indexed expansion on Desktop, and portable physical-output geometry without
  adding target-side platform conditionals.

Validation (2026-09-26):
- MVS GUI OFF and GUI ON builds pass on Desktop, PSP, and PS2;
- Desktop MVS passes 13/13 CTests and a 30-frame `pbobbl2n` runtime smoke;
- PSP Release GUI-OFF, SAVE_STATE=OFF baseline vs D7b: `.text` +572 B,
  `.data` -48 B, `.bss` -72 B, total runtime image +452 B, ELF +336 B,
  PRX +472 B;
- PS2 Release GUI-OFF, SAVE_STATE=OFF baseline vs D7b: `.text` -728 B,
  `.data` -16 B, `.bss` -539,520 B, total runtime image -540,264 B,
  ELF -888 B;
- no MVS sprite source references `getNativeObjects()` or native PSP/PS2/SDL
  APIs after the migration.

#### D7c - CPS1 portable sprite renderer [COMPLETE]

Result (2026-09-27):
- replaced `cps1/{psp,ps2,desktop}_sprite.c` with one platform-neutral
  `cps1/sprite.c` that owns object/scroll decoding, atlas placement, CLUT
  batching, high-priority direct-color layers, scroll clipping, stars,
  flip/rotation and final presentation geometry;
- extended `video_driver_t` only for CPS1 capabilities that were genuinely
  missing from the portable contract: 16-bit direct-color atlas rectangle
  updates and typed colored-point batches. PSP, PS2 and Desktop now own the
  native memory layout/submission details for both operations;
- kept PSP T8 swizzling and CLUT cache coherency inside `psp_video.c`, PS2
  converts compact portable vertices/points to gsKit command data inside the
  backend, and Desktop expands/draws the same portable representation through
  SDL;
- removed all CPS1 uses of `getNativeObjects()`, all target-side GU/gsKit/SDL
  types, and the five direct texture-buffer pointers that previously leaked
  backend storage into `sprite_common`;
- unified CPS1 `StretchScreen` indices with `common/menu/cps.c`, removing the
  stale Desktop-only leading 640x480 entry/index shift while preserving PSP and
  PS2 logical presentation sizes;
- while exercising GUI/save-state/command-list configurations, made the NCDZ
  CD-ROM save-state dependency and all remaining CPS1/CPS2/NCDZ command-list
  dependencies explicit instead of relying on transitive include ordering.

Validation (2026-09-27):
- Desktop CPS1/CPS2/MVS/NCDZ GUI-OFF builds pass and CPS1 GUI-ON passes;
- CPS1 passes the 12-test non-memory-plan Desktop suite and a 30-frame
  `ghoulsu` runtime smoke; MVS also passes its 12-test non-memory-plan suite and
  the 30-frame `pbobbl2n` smoke after the backend contract extension;
- PSP CPS1 GUI ON/OFF, CPS2, MVS and NCDZ builds pass and generate EBOOT.PBP;
- PS2 CPS1 GUI ON/OFF, CPS2, MVS and NCDZ builds pass;
- PSP Release GUI-OFF, SAVE_STATE=OFF baseline vs D7c: `.text` -460 B,
  `.data` -48 B, `.bss` -20 B, total runtime image -528 B, ELF -10,468 B,
  PRX/PBP -544 B;
- PS2 Release GUI-OFF, SAVE_STATE=OFF baseline vs D7c: `.text` -3,416 B,
  `.data` -8 B, `.bss` -386,992 B, total runtime image -390,416 B,
  ELF -3,328 B. The BSS reduction comes from replacing target-owned native
  gsKit vertex arrays with the compact portable vertex representation;
- after CPS1 migration, CPS2 PS2 is the only target renderer that still consumes
  `getNativeObjects()`; CPS1 contains no native PSP/PS2/SDL renderer API types.

#### D7P - Portable-renderer performance checkpoint [COMPLETE]

Do not start the CPS2 migration until the already-unified NCDZ/MVS/CPS1 path has
been audited for CPU cost as well as code/data/BSS size.  D7a-D7c deliberately
proved the ownership model first; this checkpoint verifies that the abstraction
does not buy cleanliness by adding avoidable work on low-end hardware.

Primary questions:

1. **PSP vertex submission** - keep the current zero-conversion fast path.
   `video_sprite_vertex_t` intentionally matches the GU sprite vertex layout, so
   `sceGuDrawArray()` consumes the common array directly.  Do not introduce a
   second PSP-native vertex array.
2. **PS2 vertex submission** - quantify the cost of reading compact 12-byte
   portable vertices, converting them to 32-byte `GSPRIMUVPOINTFLAT`/point data,
   and then letting gsKit copy that native data to its command queue.  Preserve
   the very large BSS saving from compact retained arrays, but investigate a
   cheaper backend path (integer/fixed conversion, direct queue construction, or
   another backend-local fast path) if the extra pass is measurable.
3. **Tile-cache staging** - quantify the extra cache-miss path introduced by
   decoding a tile into a temporary linear buffer and then asking the backend to
   copy/swizzle it into the atlas.  This is not a per-drawn-sprite cost, but CPS1
   high-priority/palette invalidation can make it hot enough to matter.  Prefer a
   backend-owned writable-atlas/row-writer contract or equivalent fast path when
   it can remove a copy without exposing PSP swizzle/PS2 texture details to target
   code.
4. **Desktop** - keep correctness as the priority; avoid optimizing SDL-specific
   paths at the expense of PSP/PS2 simplicity unless profiling shows a real issue.

Measurement policy:

- compare the portable renderer with the immediately preceding platform-native
  implementation using identical target/options/ROM/workload;
- collect at minimum: vertices submitted, sprite batches, texture-cache misses,
  bytes staged/copied for atlas updates, time spent building common draw data,
  time spent in backend vertex conversion/submission, and total frame time;
- use real PSP/PS2 hardware as the authoritative performance result when
  available.  PCSX2/PPSSPP are useful for functional validation and relative
  instrumentation but are not substitutes for hardware timing;
- profiling instrumentation should be build-time/temporary or compiled out by
  default and must not alter the normal renderer contract;
- retain the D7 binary/BSS comparisons alongside timing results: a small CPU cost
  may be acceptable only when the memory win is material and the measured frame
  budget remains unaffected; unexplained regressions are not accepted.

Optimization order:

1. remove obviously redundant work without changing the portable contract;
2. specialize backend materialization internally (no target-side platform types);
3. eliminate temporary tile copies for linear backends where a clean generic
   contract permits it, retaining PSP swizzle ownership in the PSP backend;
4. only after measurements are satisfactory, use the refined path as the basis
   for CPS2.

Result (2026-09-27):
- confirmed the PSP sprite path is already the desired zero-conversion case:
  `video_sprite_vertex_t` has the GU-compatible 12-byte layout and is submitted
  directly to `sceGuDrawArray()`; no PSP native staging array was added;
- replaced PS2's per-vertex `int -> float -> fixed` gsKit helper path with an
  exactly equivalent integer/fixed conversion. Exhaustive host-side comparison
  covered every signed 16-bit XY value and every unsigned 16-bit UV value at the
  texture extents used by the renderer;
- with the exact R5900 Release flags used by NJEMU, the original
  `ps2_blitSpriteVertices()` compiled to 392 static instructions including 28
  FP-related instructions. Integer conversion alone reduced that to 360 / 0;
- refactored NJEMU's local gsKit sprite-list helper so the PS2 backend reserves
  the final render-queue storage first and materializes portable vertices directly
  into it. The final sprite path is 304 static instructions, contains no FP
  conversion, uses a 64-byte stack frame instead of the former approximately
  8 KiB `GSPRIMUVPOINTFLAT[256]` staging buffer, and has no second vertex `memcpy`;
- applied the same direct-queue approach to portable colored points (CPS1 stars):
  the approximately 8 KiB `GSPRIMPOINT[256]` staging buffer and following gsKit
  copy are gone, and the function stack frame is 48 bytes;
- for CPS1 PS2 Release (`GUI=OFF`, `SAVE_STATE=OFF`), the D7c float/staging
  baseline changes from `.text` 791,348 B to 790,860 B after the optimized direct
  queue path (-488 B); `.data` and `.bss` are unchanged, so the large compact-
  vertex BSS saving from D7c is fully preserved;
- temporary CPS1 instrumentation measured atlas staging independently from drawn
  vertices, then was removed completely. Representative samples were:
  - `ghoulsu`, 600 frames: 14,068 sprite vertices / 151 batches versus 18 indexed
    8x8 atlas updates (1,152 staged bytes);
  - `sf2`, 600 frames: 47,926 vertices / 552 batches versus 55 indexed 8x8
    updates (3,520 staged bytes);
  - `sf2`, 1,800 frames: 298,424 vertices / 1,718 batches versus 89 indexed 8x8
    updates (5,696 staged bytes).
  These samples did not exercise every high-priority/direct-color invalidation
  case, so they do not prove all workloads are cold; however they show that the
  extra tile copy is orders of magnitude less frequent than vertex submission in
  the exercised workloads. No new writable-atlas/row-writer API is justified at
  this point; retain the simple backend-owned texture-update contract and revisit
  it only if profiling a representative hot case shows otherwise;
- completing the cross-platform validation exposed two remaining transitive-
  include assumptions rather than renderer defects: NCDZ command-list reduction
  now directly includes `ncdz/driver.h` for `games[]`, and `power_driver_tests`
  directly includes `<stddef.h>` for `NULL`;
- final validation: CPS1/CPS2/MVS/NCDZ PS2 builds pass with the optimized backend;
  NCDZ PSP builds; NCDZ Desktop builds and passes 15/15 CTests; CPS1 Desktop runs
  a clean 30-frame `ghoulsu` smoke with all temporary profiling code absent;
  `git diff --check` is clean and no resource file is modified.

Real-PS2 timing remains a useful release-level confirmation, especially for cache
behaviour, but D7P no longer has an avoidable EE materialization pass to optimize:
portable sprite/point data is converted exactly once into its final gsKit queue
location. The deferred VU1 experiment should therefore compare against this
direct-queue EE path, not the former staging implementation.

##### Deferred PS2 VU1/VIF1 backend experiment

A future PS2 optimization may replace the EE-side portable-to-gsKit conversion
with a VIF1/VU1 submission path.  This is intentionally **not** part of the current
D7P implementation work and must remain optional until a measured prototype wins.

Candidate architecture:

```text
common target renderer
        |
        v
compact video_sprite_vertex_t batches
        |
        +-- PSP: direct sceGuDrawArray (existing zero-conversion path)
        |
        +-- PS2: DMA -> VIF1 UNPACK -> VU1 -> GIF/XGKICK -> GS
        |
        `-- Desktop: SDL backend
```

The VU program should remain backend infrastructure, not emulator logic.  Sprite
decoding, clipping, cache replacement, priority, batching, atlas selection and
palette decisions stay in common/target code.  VU1 would only materialize GS
coordinates/UVs and primitive packets.  The prototype should investigate:

- feeding the existing compact 16-bit vertex fields through VIF1 without first
  expanding them into a second EE buffer;
- VU1 double buffering so EE batch construction overlaps VIF/VU/GIF work rather
  than synchronously waiting for each batch;
- direct GIF/XGKICK output to remove both the temporary
  `GSPRIMUVPOINTFLAT[256]` conversion buffer and the subsequent gsKit queue copy;
- coexistence/order with gsKit-managed texture, CLUT, scissor, framebuffer,
  alpha/depth and CPS2 priority state;
- a batch-size threshold or EE fallback if VIF/VU launch overhead makes small
  batches slower;
- optional evaluation with the modern VU toolchain/OpenVCL once the basic PS2SDK
  microprogram is correct; NJEMU must not depend on that toolchain merely to keep
  the portable renderer architecture.

Acceptance for any future VU1 path: identical output/state ordering, no common or
target-side PS2 conditional, and a clear real-hardware performance win over the
best EE backend.  Otherwise retain the EE path.

### D8 - Remove remaining platform conditionals from common behaviour

Audit the remaining platform `#if`s in `src/common/` (config defaults, filer,
state, UI, menu). Replace them only where a capability, geometry, filesystem, or
backend contract expresses the real semantic difference more clearly.

Do not replace a simple compile-time semantic fact with an over-engineered vtable.
The goal is meaningful ownership, not a zero-preprocessor vanity metric.

### D9 - Final include/API audit and documentation

- run an include-fanout audit again;
- remove stale umbrella headers and duplicate declarations;
- update `CLAUDE.md` architecture text and retire stale sections of
  `PORTING_PLAN.md`;
- document the final extension recipe for a future platform such as PS Vita;
- capture final counts for platform conditionals, duplicated renderer files,
  binary sizes, and test matrix.

## 7. Validation matrix

At each milestone, use the smallest matrix that exercises the changed layer,
then run the full matrix before declaring a phase complete.

Minimum cross-platform gate:

- Desktop: CPS1, CPS2, MVS, NCDZ build;
- Desktop: applicable CTest suites plus MVS runtime smoke;
- PSP: MVS GUI ON and OFF; expand to all four cores after input/render phases;
- PS2: MVS GUI ON and OFF; expand to all four cores after input/render phases;
- translation/font validation remains green;
- `git diff --check` clean;
- no file below `resources/` touched/staged.

Renderer phases additionally require screenshot/runtime visual validation where
available.

## 8. Commit policy

One coherent, verified commit per milestone. Never mix speculative later-phase
cleanup into an earlier phase merely because a file is already open. Update this
plan after every completed milestone with observed results and any design change.

## 9. Immediate next step

Continue D7 with CPS2, now using the D7P-refined contract: compact common draw
data, direct backend-native execution, and no avoidable PS2 materialization pass.
Model CPS2's depth/priority masking explicitly in the video backend rather than
leaking native GS state into target code. Keep the VU1/VIF1 experiment deferred.
Once CPS2 no longer needs native backend objects, remove the legacy
`getNativeObjects()` / native-vertex escape hatches from `video_driver_t` before
starting the broader D8 common-conditional audit.
