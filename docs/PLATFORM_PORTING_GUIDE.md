# NJEMU Platform Backend Porting Guide

This document describes the current extension boundary for adding a new host
platform to NJEMU. It reflects the post-`PLATFORM_DRIVER_REFACTOR_PLAN` design;
older PSP-first porting notes should not be used as an architectural template.

A new backend such as PS Vita should reuse the emulator cores, target renderers,
GUI logic, configuration, file browser, memory policy, and input policy from
common/target code. Platform code should contain only OS, SDK, audio, input,
threading, timing, power/capability, video, PNG/readback, and presentation
mechanics.

## 1. Source layout

Add a directory `src/<platform>/` containing the backend implementations used by
CMake. The normal backend set is:

- `<platform>_drivers.c` - link-time binding of the common driver globals;
- `<platform>_platform.c` - startup, launch path, main loop and system language;
- `<platform>_video.c` - native GPU/display implementation;
- `<platform>_audio.c` - native audio output;
- `<platform>_input.c` - raw physical controller sampling;
- `<platform>_thread.c` - threads/synchronization primitives;
- `<platform>_ticker.c` - monotonic microsecond ticker;
- `<platform>_ui_draw.c` - UI texture storage/lifecycle adapter when `GUI=ON`;
- `png.c` - platform image load/save/readback glue when `GUI=ON`.

A platform may add a narrow private header such as `<platform>_video.h` when two
files in the same backend must share an opaque accessor. Do not add a broad
`<platform>.h` umbrella header.

## 2. Link-time driver binding

`<platform>_drivers.c` selects concrete implementations by assigning the common
globals declared by the driver contracts. Backend selection is intentionally a
link/build concern rather than a host-platform `#ifdef` inside `src/common/`.

The currently bound services are:

- `audio_driver_t`;
- `input_driver_t`;
- `platform_driver_t`;
- `power_driver_t`;
- `thread_driver_t`;
- `ticker_driver_t`;
- `video_driver_t`;
- `ui_draw_driver_t`.

Use `power_unsupported` when the platform has no meaningful battery/performance
control. Use `null_ui_draw_driver` in GUI-disabled builds.

## 3. Logical versus physical geometry

`src/common/video_geometry.h` owns NJEMU's logical presentation geometry. Do not
copy logical `SCR_WIDTH`, `SCR_HEIGHT`, texture pitch, or refresh constants into a
new platform header.

The backend reports its real output size through `video_driver_t::getOutputSize`.
Common UI and target renderers perform presentation/layout decisions in logical
coordinates and scale to the backend's physical surface where required.

This separation is important for backends whose native display does not match the
original 480x272 PSP geometry.

## 4. Video backend contract

Target renderers are platform-neutral. Each target has a single `sprite.c`; it
must not include PSP/PS2/SDL/Vita GPU types or branch on the host platform.

The video backend owns:

- frame lifecycle and presentation;
- CPU-addressable frame access/readback capabilities;
- native render targets and scissoring;
- texture/CLUT upload and cache coherency;
- indexed/direct-color atlas layout;
- conversion/submission of `video_sprite_vertex_t` and
  `video_point_vertex_t` batches;
- CPS2 depth/priority support;
- low-level UI sprite/line/rectangle/fill primitives.

`writeIndexedTextureRect()` and `writeDirectTextureRect()` deliberately pass
logical rectangular texels. Native swizzling/tiled layout remains backend-local.
Likewise, `blitSpriteVertices()` receives the compact common vertex format and the
backend chooses the fastest native execution path.

Do not expose native texture handles, command-buffer pointers, GPU contexts, or
native vertex structures back through the common driver merely to make a target
renderer work. If a platform requires a special fast path, implement it inside
its backend while preserving the common semantic contract.

## 5. Renderer performance rule

Portability must not require an avoidable per-frame materialization pass.

The PSP backend currently consumes the compact common sprite vertex layout
directly. The PS2 backend converts portable vertices once into their final gsKit
queue location rather than building a second retained/native staging array. A new
backend should follow the same principle: common semantic data is stable, but
backend execution is free to be native and optimized.

Texture-update staging is acceptable when it is a cold cache-miss operation and
measurements show it is not significant. Add a more complex writable-atlas API
only after profiling demonstrates a real need.

## 6. Input contract

`input_driver_t::sample()` returns stable physical input state. The backend should
only translate native controller data into that representation and report the
number of physical controllers.

Target-specific interpretation belongs outside the platform backend. Examples
include player routing, analog game semantics, autofire, menu combinations, and
special MVS input modes. Do not change the input-driver ABI based on `EMU_SYSTEM`.

## 7. Platform and power capabilities

`platform_driver_t` is intentionally small. A backend provides lifecycle/main-loop
services, memory telemetry, and system-language mapping. Memory telemetry is
informational; cache sizing remains governed by the common runtime allocation
policy.

Power support is capability based. Do not fabricate battery values or fake
performance levels on platforms that do not expose those features.

## 8. UI architecture

The complete menu/file-browser/configuration UI lives in `src/common/`.
`ui_draw_driver_t` is not a second renderer: it owns only UI texture
storage/lifetime details that genuinely differ by platform. Actual primitive
submission belongs to `video_driver_t`.

A GUI-capable backend therefore needs both:

1. the video driver's low-level UI drawing operations; and
2. a small UI texture adapter for allocation/upload/prepare/finish semantics.

The common UI must remain free of native SDK includes and host-platform branches.

## 9. Includes and private APIs

Include the narrow header that declares what a translation unit consumes.
Platform SDK headers stay in `src/<platform>/` implementation files or narrow
backend-private headers. Common and target code must not gain native SDK types
through transitive includes.

Rules for a new backend:

- no umbrella `<platform>.h`;
- no common-to-platform include;
- no target renderer including platform code;
- no directory-wide project include-path workaround;
- prefer an opaque accessor when two backend files need one piece of private
  state rather than publishing the backend state structure;
- keep public headers self-contained.

## 10. CMake integration

Add the new `PLATFORM` value and its required external SDK/library setup while
keeping the existing target/platform axes independent. The application should
continue to build with the single project include root (`src/`) plus external SDK
include directories.

Do not add platform-specific source selection inside target directories. CMake
selects the backend sources; the four target renderers remain shared.

## 11. Validation gate

Before calling a backend usable, validate at least:

- CPS1, CPS2, MVS and NCDZ builds;
- GUI OFF and GUI ON where supported;
- save-state and command-list configurations where supported;
- common CTest suites on a host where they can run;
- a runtime smoke for at least MVS plus a representative CPS target;
- indexed and direct-color atlas updates;
- CPS2 priority/depth masking;
- physical input release/menu behavior;
- output scaling and UI layout at the platform's native resolution;
- binary/runtime-memory impact;
- `git diff --check` and no accidental resource changes.

Emulator output and timing should be checked on real hardware for console
backends. Emulator-only timing is useful for development but is not the final
performance authority.

## 12. Deferred PS2 VU1 experiment

VU1/VIF1 is a possible future PS2 backend optimization, not part of the portable
contract. If prototyped, it should consume the existing compact common batches and
remain entirely inside the PS2 video backend. It must beat the current direct-
queue EE path on real hardware before being retained.
