# Build Capability Cleanup Plan

## Goal

Make CMake the single source of truth for build capabilities and valid platform/core combinations. C code should normally consume one capability macro (`#if FEATURE`) instead of re-validating the platform and emulator core at every use site.

This cleanup must preserve real core-specific behavior. `EMU_SYSTEM`, platform macros, and target checks remain appropriate when they select genuinely different implementations rather than merely restating whether a feature is available.

## Rules

1. CMake owns feature availability, defaults, dependencies, conflicts, and invalid combinations.
2. A requested unsupported combination must fail at configure time instead of being silently ignored.
3. A feature macro means that the feature is compiled into the binary; it must not be confused with whether runtime policy actually chooses to use it for a particular game.
4. Platform/core-specific defaults are policy, not capability. Supported options remain overrideable unless there is a real technical requirement.
5. C sources should prefer `#if FEATURE` / `#ifdef FEATURE` once CMake has established the invariant.
6. Keep core checks when they represent actual behavioral differences.
7. Every cleanup slice must validate both accepted and rejected CMake combinations and the affected build/test matrix.

## Milestone C1 — Restore optional streaming cache

The reactive-memory refactor currently leaves MVS and CPS2 dependent on cache symbols even when `USE_CACHE=OFF`. Restore the historical contract that cache support is optional.

- [x] Make MVS compile and operate full-resident with `USE_CACHE=OFF` (build/test path restored; runtime game validation remains part of the matrix).
- [x] Make CPS2 compile and operate full-resident with `USE_CACHE=OFF` (build/test path restored; runtime game validation remains part of the matrix).
- [x] Keep the runtime memory planner/full-resident path when `USE_CACHE=ON`; compiling the fallback does not mean every game must use it.
- [x] Keep `Cache read size` configuration/UI strictly behind `USE_CACHE`.
- [x] Keep CPS1 and NCDZ invalid with `USE_CACHE=ON`.
- [x] Validate Desktop MVS/CPS2 with cache both ON and OFF; hardware runtime validation remains pending.
- [x] Separate MVS offline-processed assets from the runtime streaming-cache capability: converted C/S/V-ROM data now lives under `processed/` for both resident and streaming modes. `USE_CACHE=OFF` neither creates nor reads `cache/`; `USE_CACHE=ON` may still read the legacy `cache/` layout as a compatibility fallback.

## Milestone C2 — Cache defaults and platform matrix

After C1 is functional, define defaults deliberately rather than deriving capability in C headers.

- [x] Audit platform memory policy for cache defaults: PSP/PS2 retain streaming-cache capability by default because constrained runs require fallback; Vita/Desktop default full-resident, with cache still available as an explicit fallback-capability build. Runtime hardware validation remains a separate completion gate below.
- [x] Select platform defaults without removing the supported override: PSP/PS2 cached, Vita/Desktop full-resident.
- [x] Keep Vita/Desktop full-resident by default while retaining optional MVS/CPS2 cache fallback builds.
- [x] Validate PSP/PS2/Vita/Desktop × MVS/CPS2 configurations that toolchains permit. Local PSP/PS2 cache ON/OFF builds pass and CI run for `f6f65d4` passed Desktop, PSP, PS2, and all 16 Vita jobs, including the opposite cache overrides.
- [x] Keep demand-read-size `Auto` policy independent from whether cache support is compiled.

## Runtime validation gate

Build capability cleanup is compile-time complete only when runtime policy is also exercised on representative content. This gate is intentionally separate from CMake capability correctness.

- [ ] PSP: validate representative MVS (`mslug3`) and CPS2 games with the default cached build on real hardware.
- [ ] PS2: validate representative MVS (`mslug3`) and CPS2 games with the default cached build on real hardware.
- [ ] Vita: validate representative MVS/CPS2 games with the default full-resident build on hardware or a trusted runtime environment.
- [ ] Desktop: validate representative MVS/CPS2 games with the default full-resident build.
- [ ] Confirm `USE_CACHE=OFF` fails cleanly with an out-of-memory message when a full-resident allocation cannot be satisfied; it must never silently enter streaming paths.

## Milestone C3 — Cache-related capability cleanup

- [x] `PS2_FAST_CACHE`: require PS2 and `USE_CACHE`; C consumers should not repeat core checks.
- [x] `PS2_DIRTY_SPRITE_UPLOADS`: encode its PS2/MVS contract in CMake instead of silently ignoring invalid requests.
- [x] `CACHE_IO_PROFILE`: require MVS streaming cache support.
- [x] `CACHE_IO_FORCE_SEEK`: require MVS streaming cache support.
- [x] `CACHE_IO_VALIDATE_ACCELERATED`: require PS2/MVS streaming cache and the accelerated backend.
- [x] Exercise configure-time rejection for representative invalid cache-feature combinations during this audit.

## Milestone C4 — General feature/capability audit

Audit every public CMake option and classify it as platform-independent, platform-specific, core-specific, or dependent on another capability.

Initial candidates:

- `KERNEL_MODE`
- `ADHOC`
- `USE_VITAGL`
- `PSVITA_VIDEO_STATS`
- `USE_DESKTOP_GL`
- `PS2_EXTERNAL_IRX_IMAGE`
- `PS2_VIDEO_MODE`
- `PS2_CACHE_RESERVE_KB`
- sanitizer/profiling options where platform support differs

The PS2 string options now reject non-default requests outside PS2, and ASan/UBSan/gprof are explicitly desktop-only diagnostics.

For each option:

- [x] Define valid platforms/cores for the audited boolean capabilities.
- [x] Define dependencies/conflicts for the audited boolean capabilities.
- [x] Reject unsupported explicit requests rather than silently ignoring them.
- [x] Remove redundant platform/core preprocessor checks from audited consumers after establishing CMake invariants.

## Milestone C4b — Runtime video backend selection

Desktop now compiles SDL and OpenGL 3.3 together by default and selects the active renderer at runtime. `USE_DESKTOP_GL` remains only as a focused build-capability switch for dependency/minimal-build validation. The UI/configuration model is platform-independent even when a platform currently has only one backend.

- [x] Refactor the Desktop video backends behind a common interface so SDL and OpenGL can coexist in one binary without duplicate public symbols or global ownership.
- [x] Compile both Desktop backends by default when their dependencies are available; retain a CMake capability switch only where it is useful for dependency/minimal-build validation, not as the user's renderer preference.
- [x] Remove the current `USE_DESKTOP_GL`/`GUI` mutual exclusion by making GUI presentation work with either selected backend, without silently falling back to SDL.
- [x] Add a global `Video backend` setting to the emulator menu on every platform. Keep this emulator-wide rather than per-game.
- [x] Populate the setting from the backends compiled for the current platform instead of hardcoding a Desktop-only list. Desktop initially exposes `Auto`, `SDL`, and `OpenGL`; single-backend platforms expose their effective backend.
- [x] Make the setting editable only when more than one selectable backend is compiled. On single-backend platforms keep the row visible but disabled/read-only so the active backend remains discoverable.
- [x] Define deterministic `Auto` policy and startup fallback/error semantics. Desktop `Auto` currently resolves to SDL; single-backend platforms resolve to their native backend. An explicitly configured unavailable backend is reported at startup and falls back to `Auto`.
- [x] Persist the backend setting in the global configuration using stable backend IDs. Keep translations/UI IDs deterministic across all platforms and gracefully resolve a configured backend that is unavailable in the current build.
- [x] Make backend changes restart the emulator/video subsystem at a safe boundary; do not switch live while backend-owned GPU resources exist unless lifecycle correctness is demonstrated.
- [x] Extend Desktop CI to build/test the dual-backend binary and retain a focused SDL-only Desktop build to catch accidental coupling.
- [ ] Add runtime smoke/regression coverage for SDL and OpenGL on representative CPS1/CPS2/MVS/NCDZ content, including GUI navigation and save-state thumbnail paths.
  - MVS/`mslug3` OpenGL validation covers GUI splash/browser, gameplay, main menu, save-state menu, save-state creation/thumbnail reload, and the `GUI=OFF` frame path. Its no-GUI frame-120 regression remains pixel-identical (0/68,096 differing pixels) after the GUI work. OpenGL UI uploads reuse a fixed RGBA8 atlas region so RGBA4444 glyph/shadow alpha precision is preserved while each sprite rectangle still uses one pitched transfer. CPS1/`ffight` and CPS2/`avsp` now boot through the OpenGL no-GUI path, reach frame 120, and match SDL exactly (0/86,016 differing pixels for each); their OpenGL GUI browsers also render successfully. NCDZ OpenGL GUI/browser rendering is exercised, but gameplay validation remains blocked until `neocd.bin` is available.
- [x] Document which renderer is active in diagnostics so performance/correctness comparisons are unambiguous.

## Milestone C5 — Preprocessor audit

Search compound conditions such as `FEATURE && PLATFORM`, `FEATURE && EMU_SYSTEM`, and `FEATURE && BUILD_*`.

- [x] Remove audited conditions that only repeat a CMake invariant.
- [x] Preserve conditions that select genuinely different core/platform behavior.
- [x] Prefer positive capability names over indirect platform inference where a reusable capability exists.
- [x] Run strict builds/tests after each focused cleanup rather than performing a repository-wide mechanical rewrite.

## Completion criteria

- Supported feature combinations are explicit and configure successfully.
- Unsupported requested combinations fail during CMake configuration.
- MVS/CPS2 can be built with or without streaming cache support.
- Feature macros describe compiled capabilities rather than platform identity.
- Runtime policy remains responsible for choosing full-resident versus fallback behavior when both are compiled.
- Every platform exposes the active video backend through the global emulator menu; builds with multiple backends can select it at runtime through a persisted global setting. Desktop can compile SDL and OpenGL together as the first multi-backend implementation.
- C/C++ consumers do not redundantly re-check platform/core constraints already guaranteed by CMake.
