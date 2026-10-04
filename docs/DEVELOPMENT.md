# Development Guide

This document is the contributor-facing entry point for NJEMU development. Repository-specific engineering rules in `CLAUDE.md` remain authoritative for implementation work.

## Repository structure

- `src/common/` - shared runtime services, GUI, configuration, drivers, cache/ZIP policy, save states, metadata readers
- `src/cps1/`, `src/cps2/`, `src/mvs/`, `src/ncdz/` - emulator-core and target-renderer code
- `src/psp/`, `src/ps2/`, `src/psvita/`, `src/desktop/` - native host backends
- `metadata/` - tracked source metadata used to generate runtime databases
- `translations/` - tracked localization sources
- `tools/` - metadata/translation/font/build helpers
- `romcnv/` - MVS/CPS2 ROM conversion tools
- `web/` - static GitHub Pages site and browser converter
- `tests/` - Desktop-hosted unit/integration tests
- `docs/` - architecture, audits, plans, and user/developer documentation

## Architecture rules

Platform selection is a build/link concern. Shared/core code should not acquire host SDK types or repeat platform checks when a capability is already expressed by the build system/driver contracts.

The normal backend contract covers:

- lifecycle and system information;
- video/presentation;
- audio;
- input;
- threading;
- timing/frame pacing;
- optional power/device capabilities;
- UI texture storage and image/readback support.

All four emulator targets have one portable sprite renderer. Do not fork target renderers per platform.

For the detailed contract, see [PLATFORM_PORTING_GUIDE.md](PLATFORM_PORTING_GUIDE.md). For the completed refactor history and measurements, see [PLATFORM_DRIVER_REFACTOR_PLAN.md](PLATFORM_DRIVER_REFACTOR_PLAN.md).

## Building and tests

See [BUILDING.md](BUILDING.md) for toolchains and build options.

Prefer the smallest focused validation first, then expand when shared code changes. Typical expectations include:

- the affected Desktop target build and CTests;
- all four Desktop targets for common/core-interface changes;
- PSP/PS2/Vita build coverage when native contracts or packaging change;
- GUI and no-GUI variants when frontend/runtime initialization changes;
- save-state/command-list/cache variants when their dependencies change;
- `git diff --check`.

Do not weaken the existing CI matrix just to make a change pass.

## Runtime data and source control

Never treat the local contents of `resources/` as distributable source data. The tree can contain private/local ROMs, BIOS files, generated caches, processed assets, save data, and configuration.

The authoritative runtime/packaging contract is [RUNTIME_FILES_AUDIT.md](RUNTIME_FILES_AUDIT.md). Build/install/release code must continue to use explicit manifests and must not recursively package `resources/`.

## Pull requests

Keep changes focused and use explicit staging. A pull request should explain:

- what changed and why;
- affected emulator cores/platforms;
- important implementation/compatibility decisions;
- tests and hardware/emulator validation performed;
- visual evidence when UI/rendering output changes.

The repository PR template provides a compact checklist for this information.

## Bug and performance reports

When a bug or slowdown occurs after a game has started, attach a save state captured as close as possible to the problematic scene. This is the fastest way to give maintainers the same emulated machine state and reproduce scene-specific problems.

Reports should also include the exact NJEMU build version shown by the application/logs, emulator core, ROM/set name, host platform, relevant backend/cache settings, and reproduction steps.

## Release work

NJEMU uses Semantic Versioning for release tags and derives development build identity from Git. Do not add new independent version constants to platform code or workflows.

See [RELEASING.md](RELEASING.md) for tagging, canonical build configurations, release artifacts, and Pages/download publication.
