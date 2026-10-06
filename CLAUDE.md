# CLAUDE.md

This file provides repository-specific guidance for work on NJEMU.

## Project Overview

NJEMU is a multi-platform arcade emulator written in C. It emulates:

- CPS1 (Capcom Play System 1)
- CPS2 (Capcom Play System 2)
- MVS/AES (Neo Geo)
- NCDZ (Neo Geo CD)

Builds are selected by two independent CMake axes: `TARGET` (arcade system) and
`PLATFORM` (PSP, PS2, DESKTOP).

## Build Commands

CMake requires both axes:

```bash
cmake -S . -B build_desktop_mvs -DTARGET=MVS -DPLATFORM=DESKTOP
cmake --build build_desktop_mvs -j4

cmake -S . -B build_desktop_cps1 -DTARGET=CPS1 -DPLATFORM=DESKTOP
cmake --build build_desktop_cps1 -j4

cmake -S . -B build_ps2_mvs \
  -DCMAKE_TOOLCHAIN_FILE="$PS2DEV/share/ps2dev.cmake" \
  -DTARGET=MVS -DPLATFORM=PS2
cmake --build build_ps2_mvs -j4
```

For PSP use the PSPSDK CMake toolchain (`$PSPDEV/psp/share/pspdev.cmake`). Run
executables from their build/resource context when relative resource paths are
needed.

Useful options include:

- `GUI=ON/OFF`;
- `SAVE_STATE=ON/OFF`;
- `COMMAND_LIST=ON/OFF`;
- `ADHOC=ON/OFF` (PSP where supported);
- `PSP_ME_AUDIO=ON/OFF` (optional PSP Media Engine audio producer; default OFF;
  ON exposes persistent Auto/Main CPU/Media Engine runtime selection);
- `PSP_AUDIO_PROFILE=ON/OFF` (PSP-only audio timing log; default OFF);
- `PSP_ME_SOUND_PROFILE=ON/OFF` (PSP MVS-only 68000/Z80/scheduler and sound
  protocol timing log for the full ME sound-coprocessor work; default OFF;
  pair with `PSP_AUDIO_PROFILE` for YM2610/producer/ME-wait timing);
- `PSP_ME_RING_SELFTEST=ON/OFF` (PSP MVS + `PSP_ME_AUDIO=ON` only; default OFF;
  runs the synthetic Allegrex/ME shared-ring transport oracle at startup and
  builds the standalone `psp_me_ring_hardware_test` PRX for psplink validation);
- `PSP_ME_SOUND_COPROCESSOR=ON/OFF` (PSP MVS/NCDZ + `PSP_ME_AUDIO=ON` only;
  default OFF; builds the persistent ME sound worker and the target-specific
  Z80/YM2610 ownership path while preserving CPU recovery/fallback support);
- `USE_ASAN=ON` (Desktop development).

PSP packages request the large user-memory partition (`MEMSIZE=1`). Platform
`queryMemoryInfo()` values are telemetry; CPS2/MVS cache capacity is established
at ROM-load time by retained allocator probes after mandatory regions are
resident. `memory_plan_t` owns reserve/floor policy.

## Architecture

### Platform drivers

Common contracts live in `src/common/`:

- `video_driver_t` - presentation, texture/CLUT operations, portable sprite
  submission and low-level UI drawing;
- `audio_driver_t` - audio output;
- `input_driver_t` - raw physical input sampling;
- `platform_driver_t` - lifecycle/main loop, memory telemetry and system language;
- `thread_driver_t` - threading primitives;
- `ticker_driver_t` - monotonic timing;
- `power_driver_t` - optional battery/performance capabilities;
- `ui_draw_driver_t` - UI texture storage/lifecycle only.

Each platform binds these globals in `src/<platform>/<platform>_drivers.c`.
Backend selection is a build/link concern; shared code should not add host-
platform `#ifdef`s.

### Rendering

Every emulator target has one platform-neutral renderer:

- `src/cps1/sprite.c`
- `src/cps2/sprite.c`
- `src/mvs/sprite.c`
- `src/ncdz/sprite.c`

Target renderers own emulator semantics such as sprite/tile decoding, clipping,
priority, atlas/cache policy, palette selection and batching. They emit portable
`video_sprite_vertex_t` / `video_point_vertex_t` data and texture updates through
`video_driver_t`.

Native execution stays in the backend. PSP can submit the compact sprite layout
directly to GU. PS2 materializes it directly into the final gsKit queue rather
than retaining native target-side vertex arrays. Desktop maps the same data to
SDL. Do not reintroduce PSP/PS2/SDL SDK types or `getNativeObjects()` escape
hatches into target code.

Logical 480x272 presentation geometry lives in `common/video_geometry.h`.
Physical output dimensions come from `video_driver_t::getOutputSize()`.

### GUI

The GUI/menu/file-browser/configuration logic is common and works on PSP, PS2 and
Desktop. `common/ui_draw.c` owns UI semantics; `video_driver_t` owns primitive
rendering. Platform `*_ui_draw.c` files are small texture-storage/lifecycle
adapters, not parallel renderers.

### Includes

Use narrow, direct headers. Common/target code must not include platform umbrella
headers or acquire native SDK declarations transitively. Platform SDK includes
belong in platform implementation files or narrowly-scoped backend-private
headers. The application uses one project include root (`src/`) rather than a
collection of directory-wide include paths.

## Source Layout

```text
src/
├── common/          shared drivers, UI, runtime services, ZIP/cache policy
├── cpu/             CPU cores
├── sound/           sound chip emulation
├── cps1/            CPS1 target
├── cps2/            CPS2 target
├── mvs/             MVS/AES target
├── ncdz/            Neo Geo CD target
├── psp/             PSP backend
├── ps2/             PS2 backend
└── desktop/         SDL2 backend
```

## Key Entry Points

- `src/emumain.c` - common emulator startup and driver initialization;
- `src/<target>/driver.c` - target/game driver wiring;
- `src/<target>/memintrf.c` - CPU/memory interface;
- `src/<target>/vidhrdw.c` - video-hardware emulation;
- `src/<target>/sprite.c` - portable target renderer.

Each target defines `emu_layer_textures`, `emu_layer_textures_count`, and
`emu_clut_info`; these describe the target's atlas/CLUT requirements to the video
backend.

## Validation

For driver/render/common changes, prefer the smallest focused matrix first, then
cross-platform validation before committing. Renderer/input changes should expand
to all four targets. Keep `git diff --check` clean and never include runtime
resources/caches/ROMs in source commits.

The current driver-refactor history and its performance/size measurements are in
`docs/PLATFORM_DRIVER_REFACTOR_PLAN.md`. The extension recipe for a future backend
such as Vita is `docs/PLATFORM_PORTING_GUIDE.md`. `PORTING_PLAN.md` now records
only current platform status and remaining follow-up work.
