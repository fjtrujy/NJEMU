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

## Milestone C4b — Desktop runtime video backend selection

Desktop currently treats `USE_DESKTOP_GL` as a mutually exclusive build-time source selection: the normal SDL renderer and the OpenGL 3.3 renderer are not present in the same binary. Convert this into a compiled-capability plus runtime-policy model, consistent with the rest of this plan.

- [ ] Refactor the Desktop video backends behind a common interface so SDL and OpenGL can coexist in one binary without duplicate public symbols or global ownership.
- [ ] Compile both Desktop backends by default when their dependencies are available; retain a CMake capability switch only where it is useful for dependency/minimal-build validation, not as the user's renderer preference.
- [ ] Remove the current `USE_DESKTOP_GL`/`GUI` mutual exclusion by making GUI presentation work with either selected backend, without silently falling back to SDL.
- [ ] Add a global Desktop `Video backend` setting with `Auto`, `SDL`, and `OpenGL` choices. Keep this emulator-wide rather than per-game.
- [ ] Define deterministic `Auto` policy and startup fallback/error semantics. An explicitly requested unavailable backend must be reported rather than silently changing the user's choice.
- [ ] Persist the backend setting in the global configuration and expose it only on Desktop; keep translations/UI IDs deterministic across other platforms.
- [ ] Make backend changes restart the emulator/video subsystem at a safe boundary; do not switch live while backend-owned GPU resources exist unless lifecycle correctness is demonstrated.
- [ ] Extend Desktop CI to build/test the dual-backend binary and, where practical, retain focused single-capability builds to catch accidental coupling.
- [ ] Add runtime smoke/regression coverage for SDL and OpenGL on representative CPS1/CPS2/MVS/NCDZ content, including GUI navigation and save-state thumbnail paths.
- [ ] Document which renderer is active in diagnostics so performance/correctness comparisons are unambiguous.

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
- Desktop can compile SDL and OpenGL together and select the video backend at runtime through a persisted global setting.
- C/C++ consumers do not redundantly re-check platform/core constraints already guaranteed by CMake.
