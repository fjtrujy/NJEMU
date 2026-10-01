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

- [ ] Make MVS compile and operate full-resident with `USE_CACHE=OFF`.
- [x] Make CPS2 compile and operate full-resident with `USE_CACHE=OFF` (build/test path restored; runtime game validation remains part of the matrix).
- [ ] Keep the runtime memory planner/full-resident path when `USE_CACHE=ON`; compiling the fallback does not mean every game must use it.
- [ ] Keep `Cache read size` configuration/UI strictly behind `USE_CACHE`.
- [ ] Keep CPS1 and NCDZ invalid with `USE_CACHE=ON`.
- [ ] Validate MVS/CPS2 with cache both ON and OFF.

## Milestone C2 — Cache defaults and platform matrix

After C1 is functional, define defaults deliberately rather than deriving capability in C headers.

- [ ] Audit actual RAM/runtime requirements for PSP, PS2, PS Vita, and Desktop.
- [ ] Select platform defaults without removing the supported override.
- [ ] Verify whether Vita/Desktop should default to full-resident while retaining optional cache fallback builds.
- [ ] Validate PSP/PS2/Vita/Desktop × MVS/CPS2 configurations that toolchains permit.
- [ ] Keep demand-read-size `Auto` policy independent from whether cache support is compiled.

## Milestone C3 — Cache-related capability cleanup

- [ ] `PS2_FAST_CACHE`: require PS2 and `USE_CACHE`; C consumers should not repeat core checks.
- [ ] `PS2_DIRTY_SPRITE_UPLOADS`: encode its PS2/MVS contract in CMake instead of silently ignoring invalid requests.
- [ ] `CACHE_IO_PROFILE`: document and validate its exact dependencies.
- [ ] `CACHE_IO_FORCE_SEEK`: document and validate its exact dependencies.
- [ ] `CACHE_IO_VALIDATE_ACCELERATED`: require the accelerated backend it validates.
- [ ] Add configure-time negative tests for invalid combinations where practical.

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

For each option:

- [ ] Define valid platforms/cores.
- [ ] Define dependencies/conflicts.
- [ ] Reject unsupported explicit requests rather than silently ignoring them.
- [ ] Remove redundant platform/core preprocessor checks from consumers only after the CMake invariant exists.

## Milestone C5 — Preprocessor audit

Search compound conditions such as `FEATURE && PLATFORM`, `FEATURE && EMU_SYSTEM`, and `FEATURE && BUILD_*`.

- [ ] Remove conditions that only repeat a CMake invariant.
- [ ] Preserve conditions that select genuinely different core/platform behavior.
- [ ] Prefer positive capability names over indirect platform inference where a reusable capability exists.
- [ ] Run strict builds/tests after each focused cleanup rather than performing a repository-wide mechanical rewrite.

## Completion criteria

- Supported feature combinations are explicit and configure successfully.
- Unsupported requested combinations fail during CMake configuration.
- MVS/CPS2 can be built with or without streaming cache support.
- Feature macros describe compiled capabilities rather than platform identity.
- Runtime policy remains responsible for choosing full-resident versus fallback behavior when both are compiled.
- C/C++ consumers do not redundantly re-check platform/core constraints already guaranteed by CMake.
