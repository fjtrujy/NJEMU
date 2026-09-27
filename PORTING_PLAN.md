# NJEMU Cross-Platform Status and Remaining Porting Work

This file is the current high-level platform roadmap. The original PSP-to-PS2/
Desktop migration checklist has been retired because its intermediate file names,
platform-specific sprite renderers, PSP-only GUI assumptions, and many unchecked
steps no longer describe the source tree.

For the driver-refactor history and measurements, see
`docs/PLATFORM_DRIVER_REFACTOR_PLAN.md`. For the contract required by a future
backend such as PS Vita, see `docs/PLATFORM_PORTING_GUIDE.md`.

## Current platform status

NJEMU supports four emulator targets on three host backends:

| Target | PSP | PS2 | Desktop |
|---|---|---|---|
| CPS1 | Core + GUI | Core + GUI | Core + GUI |
| CPS2 | Core + GUI | Core + GUI | Core + GUI |
| MVS | Core + GUI | Core + GUI | Core + GUI |
| NCDZ | Core + GUI | Core + GUI | Core + GUI |

The common GUI includes the file browser, menus, configuration, translations,
state UI, command-list UI and layout logic. PSP, PS2 and Desktop provide only the
backend mechanics needed to present it.

## Current architecture

Platform selection is performed at build/link time. Common code does not select
PSP, PS2 or Desktop drivers with host-platform preprocessor branches.

Each backend supplies implementations for:

- platform lifecycle/system language/memory telemetry;
- video and presentation;
- audio;
- raw physical input;
- threads;
- monotonic timing;
- optional power capabilities;
- GUI texture storage and PNG/readback support.

All four emulator targets now have one portable sprite renderer each:

- `src/cps1/sprite.c`;
- `src/cps2/sprite.c`;
- `src/mvs/sprite.c`;
- `src/ncdz/sprite.c`.

Target code produces compact portable draw data. PSP submits the compatible
vertex representation directly to GU. PS2 converts portable vertices directly
into their final gsKit queue location. Desktop consumes the same representation
through SDL. Native SDK types do not leak back into target renderers.

## Completed platform work

The following PSP-first assumptions have been removed from the shared
architecture:

- platform umbrella headers and the old `emumain.h` dependency hub;
- target-dependent input-driver ABI;
- fake power/battery implementations on unsupported hosts;
- duplicated platform-specific target sprite renderers;
- duplicated low-level UI primitive APIs;
- host-platform branches in `src/common/`;
- direct target access to native GPU objects;
- platform-owned copies of common logical geometry.

The PS2 renderer performance checkpoint also removed the avoidable native vertex
staging/copy pass while preserving the large BSS reduction obtained from compact
portable retained vertices. VU1/VIF1 remains only a deferred measured experiment.

## Remaining platform work

The core cross-platform migration is complete. Remaining items are follow-up
validation or optional new-platform work rather than blockers for PSP/PS2/Desktop:

1. Perform periodic real-hardware PSP/PS2 regression passes after substantial
   renderer, input, filesystem or audio changes.
2. Re-test PS2 multitap hotplug/late connection and representative GUI/global
   hotkeys on real hardware.
3. Keep binary size and runtime-memory measurements as regression gates for
   low-memory console builds.
4. Add another host backend, such as PS Vita, only through the contracts in
   `docs/PLATFORM_PORTING_GUIDE.md`; do not fork target renderers again.
5. Revisit PS2 VU1/VIF1 only as a separate optimization project with a real-
   hardware benchmark against the current direct-queue EE implementation.

## Validation expectations

A substantial platform/backend change should normally cover:

- Desktop CPS1/CPS2/MVS/NCDZ builds and applicable CTests;
- a Desktop runtime smoke;
- PSP and PS2 GUI OFF/ON builds, expanded to all targets for renderer/input work;
- save-state/command-list variants when their dependencies change;
- translation/font validation;
- real-hardware checks when timing or native-device behavior matters;
- `git diff --check`;
- no accidental changes under `resources/`.

## Historical note

The pre-refactor plan described files such as `psp_sprite.c`, `ps2_sprite.c`,
`desktop_sprite.c`, PSP-only UI implementations, and a future GUI port. Those
steps were useful during the migration but have all been superseded by the
current common renderer/UI architecture. The Git history preserves that plan if
an intermediate migration state ever needs to be studied.
