# Physical Multi-Controller Plan

## Goal

Add simultaneous physical-controller support, starting with PS2, while preserving the
current single-controller behavior on PSP, Desktop, and PS2 when only one controller
is connected.

The intended PS2 mapping is deterministic:

- physical controller 0 -> emulated Player 1
- physical controller 1 -> emulated Player 2
- physical controller 2 -> emulated Player 3
- physical controller 3 -> emulated Player 4

Only as many physical controllers as the current arcade game supports are consumed.
PS2 may discover more pads through multitaps, but NJEMU currently has no >4-player
core.

## Compatibility rules

1. Do not change existing config or save-state formats.
2. Keep `option_controller` semantics for a single physical controller. The
   existing "Switch Player" feature must keep working unchanged.
3. Multi-controller routing activates only when 2+ physical controllers are
   available.
4. PSP and Desktop initially continue to expose one physical controller through the
   new API, so their behavior remains unchanged.
5. UI/menu navigation and emulator-level hotkeys use the primary physical
   controller (controller 0) unless a later phase explicitly broadens this.
6. Gameplay mappings continue to use the existing `input_map[]`; there is no
   duplicate per-player button configuration in this migration.
7. ADHOC behavior must remain unchanged. Multi-controller local routing is not
   mixed into the PSP network path.

## Phase 0 - Baseline and safety

- Start from a clean non-`resources/` working tree.
- Keep all user-owned `resources/` changes untouched.
- Validate the four PS2 GUI builds before API migration.

## Phase 1 - Indexed common input API

Extend `input_driver_t` so a backend can:

- report the number of currently usable physical controllers;
- poll a specific physical controller by logical index;
- for MVS, poll Fat Fury Special / analog variants by logical index.

Keep compatibility wrappers:

- `poll_gamepad()` means controller 0;
- new indexed helpers expose controller N;
- a backend that only supports one controller reports count 1 and returns 0 for
  indices > 0.

Update PSP/Desktop as one-controller implementations.

## Phase 2 - PS2 controller enumeration

Refactor `src/ps2/ps2_input.c` so:

- active pads are enumerated in stable connector order:
  `(0,0), (1,0), (0,1), (1,1), ...`;
- indexed polling reads the requested active pad rather than always the first one;
- direct ports and multitap slots can be used simultaneously;
- hotplug/multitap discovery is refreshed periodically without doing expensive
  device setup RPCs every frame;
- analog/dead-zone/Fat Fury semantics remain identical per controller.

Validation:
- all four PS2 GUI builds;
- MVS analog/Fat Fury build paths.

## Phase 3 - Two-player cores first: NCDZ and MVS

When 2+ physical pads are present:

- route physical controller N directly to emulated player N;
- retain active-low port semantics by combining each player'\''s contribution;
- preserve controller 0 as the source of menu/snapshot/command-list actions;
- preserve the legacy single-controller path byte-for-byte where practical.

For MVS also validate:
- AES/MVS coin/start behavior;
- Fat Fury Special exclusive digital/analog rules;
- Irritating Maze / Pop '\''n Bounce analog routing.

Autofire timing/state must be independent per physical player in multi mode.

## Phase 4 - CPS1 and CPS2

Use the same direct physical-to-emulated-player model, respecting
`input_max_players` (2/3/4 depending on the game).

Reuse the current per-game port mapping logic rather than creating a second mapping
table. Multi-controller contributions should be accumulated into the active-low
arcade port values.

Special cases to verify:

- CPS1 3/4-player games;
- CPS1 Forgotten Worlds dial state;
- CPS1/CPS2 service/start combinations;
- CPS2 coin-chute routing;
- rotated/flipped input adjustment;
- per-player autofire.

## Phase 5 - UX and compatibility cleanup

- Hide or disable "Switch Player" only while true multi-controller routing is
  active if that avoids ambiguous behavior; keep it available in single-pad mode.
- Keep saved `option_controller` values compatible with old states/configs.
- Document primary-controller ownership of emulator/UI hotkeys.
- Do not rename legacy `PSPClock`/config keys as part of this work.

## Phase 6 - Validation

Build matrix for CPS1/CPS2/MVS/NCDZ:

1. baseline (GUI off);
2. GUI;
3. GUI + SAVE_STATE + COMMAND_LIST.

Runtime checks where possible:

- one PS2 pad: old behavior and Switch Player;
- two direct PS2 pads: P1 + P2 simultaneous;
- multitap: 3/4-player CPS titles;
- MVS normal game with two pads;
- MVS Fat Fury Special;
- MVS Irritating Maze / Pop '\''n Bounce;
- NCDZ two-player input;
- menu/screenshot/save-state while multiple pads are attached.

## Non-goals

- Per-player remappable control profiles.
- Desktop SDL gamepad multi-controller support in the first PS2-focused pass.
- PSP ADHOC redesign.
- More than four emulated local players.
- Changing save-state/config formats solely for controller routing.

