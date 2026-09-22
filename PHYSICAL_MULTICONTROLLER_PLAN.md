# Physical Multi-Controller Plan

## Implementation status (2026-09-20)

- ✅ Phase 0 complete: clean non-resource baseline established and preserved.
- ✅ Phase 1 complete: common input API supports physical-controller count and
  indexed polling while legacy `poll_gamepad()` remains controller 0.
- ✅ Phase 2 complete: PS2 enumerates direct pads/multitap slots in stable
  connector order and periodically refreshes late multitap attachment.
- ✅ Phase 3 complete: NCDZ and MVS route simultaneous physical pads directly to
  emulated players. MVS `popbounc` keeps per-player analog routing; hardware
  special cases that are not independent multiplayer controllers retain the
  legacy path.
- ✅ Phase 4 complete: CPS1/CPS2 support 2/3/4 simultaneous physical pads using
  the existing per-game mappings and active-low port aggregation. Forgotten
  Worlds and `pzloop2` analog accumulators remain per emulated player.
- ✅ Phase 5 complete for routing semantics: `option_controller` and Switch
  Player remain compatible for one-pad operation and are ignored for gameplay
  routing while 2+ physical pads are active. Controller 0 owns emulator/UI
  hotkeys in multi mode.
- ✅ Phase 6 build validation: 12/12 PS2 builds pass (baseline, GUI, and GUI +
  SAVE_STATE + COMMAND_LIST for CPS1/CPS2/MVS/NCDZ).
- ✅ PCSX2 2.9.70 runtime validation now confirms one-pad fallback, two direct
  pads, multitap enumeration, MVS two-player routing, CPS1 four-player routing,
  CPS2 two-player routing, and NCDZ two-player routing.
- ✅ Additional CPS1 runtime coverage confirms `mercs` 3P, `slammast` 4P,
  `forgottn` independent P1/P2 dial accumulators, `1941` rotated input routing,
  and `sf2` six-button split-port routing.
- ✅ The one-pad legacy Switch Player path was explicitly exercised in PCSX2;
  `option_controller` still cycles as expected while only one physical pad is
  present.
- ✅ With both PS2 multitaps enabled, NJEMU enumerates all eight active endpoints
  in the intended stable order. `captcomm` was exercised with P3/P4 mapped to
  `(0,1)` / `(1,1)` and routed to the correct CPS1 ports.
- ✅ MVS secondary controllers can no longer trigger remappable service/test
  system inputs (`f176a5a`); a temporary runtime mapping confirmed P2's test flag
  is cleared while the same input from P1 remains active.
- ✅ CPS2 multi-pad routing now suppresses the legacy `P2_START` compatibility
  binding, preventing a physical pad from starting the opposite emulated player
  while true multi-controller routing is active.
- ✅ Fat Fury Special special-poller selection now uses `NGH_fatfursp` rather
  than the literal parent set name, so clones such as `fatfursa` use the same
  input semantics (`d773d19`).
- ✅ MVS `fatfursp` / `fatfursa` and `popbounc` now have runtime coverage,
  including the special poller and independent analog routing.
- ✅ CPS2 `avsp`, `ddtod`, and `batcir` were exercised with three/four
  PCSX2 pads, including P3/P4 movement, Start, and Coin routing.
- ✅ CPS2 `pzloop2` was observed advancing P1/P2 paddle accumulators
  independently with no cross-talk.
- ⚠️ Remaining multi-controller runtime work is primarily real-hardware
  hotplug/late-multitap and explicit global-hotkey/UI smoke testing.

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

- ✅ one PS2 pad: NJEMU reports one active controller, uses the legacy one-pad
  path, and Switch Player was runtime-validated by cycling `option_controller`;
- ✅ two direct PS2 pads: indexed polling and independent P1/P2 routing verified;
- ✅ multitap: eight endpoints detected; CPS1 `captcomm` P3/P4, `mercs` P3, and
  `slammast` P4 routing verified;
- ✅ MVS normal game with two pads;
- ✅ MVS Fat Fury Special: `fatfursp` and `fatfursa` special-poller runtime
  behavior validated;
- ✅ MVS Irritating Maze / Pop '\''n Bounce: `irrmaze` intentionally remains on
  the legacy special-hardware path; `popbounc` per-player analog routing has
  runtime validation;
- ✅ NCDZ two-player input (`Windjammers`);
- ✅ CPS2 `avsp` P3 and `ddtod`/`batcir` P3/P4 routing validated through
  multitap-style PCSX2 endpoints, including Start and per-player Coin;
- ✅ CPS2 `pzloop2` P1/P2 paddle accumulators validated independently;
- ✅ CPS2 legacy `Start2` cross-player binding was explicitly exercised with a
  temporary runtime mapping and verified not to leak between physical players in
  multi-controller mode; the temporary instrumentation was removed afterwards;
- ✅ primary-only MVS test/service ownership is covered structurally and the
  `TEST_SWITCH` path has runtime validation;
- ⚠️ menu/screenshot/save-state/command-list interaction and live pad/multitap
  hotplug still need an explicit real-hardware smoke test.

## Non-goals

- Per-player remappable control profiles.
- Desktop SDL gamepad multi-controller support in the first PS2-focused pass.
- PSP ADHOC redesign.
- More than four emulated local players.
- Changing save-state/config formats solely for controller routing.

