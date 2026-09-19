# Physical Multi-Controller — Next Session Handoff

## Repository

Work directly on:

```text
/Users/fjtrujy/Projects/NJEMU
```

Current branch:

```text
gui
```

Do **not** create a new worktree.

## Critical safety rule

There are many user-owned runtime files under `resources/` (ROMs, NVRAM, caches,
configs, etc.).

**Never modify, delete, revert, stage, or commit anything under `resources/`.**

Do not use broad staging commands such as `git add -A` or `git commit -a`. Stage
only explicit source/document paths.

## Authoritative documents

Read these first:

1. `PHYSICAL_MULTICONTROLLER_PLAN.md` — authoritative multi-controller design.
2. `PORTING_PLAN.md` — overall port status and PS2 validation context.

The implementation is **not starting from zero**. The common/core routing is
already implemented and must be preserved while finishing runtime support.

## Current implementation state

The following work is already committed:

- `305e190` — indexed physical-controller polling API.
- `0df015f` — simultaneous NCDZ controller routing.
- `b5ae13b` — simultaneous MVS controller routing.
- `5379e23` — simultaneous CPS1 controller routing.
- `b127e1c` — simultaneous CPS2 controller routing.
- `567b07e` — PS2 multitap rediscovery.
- `0e15f91` — preserve legacy controller selection in multi-pad mode.
- `ecf0460` — document multi-controller support.
- `a647d0c` — fix Desktop/ROMCNV/Web CI builds.
- `f176a5a` — restrict MVS service/test system inputs to the primary controller.
- `f6de133` — prevent CPS2 legacy `Start2` from crossing physical players in
  true multi-pad mode.
- `d773d19` — select the Fat Fury Special poller by NGH id so parent and clone
  sets share the same special input semantics.

Current semantics:

- physical controller 0 -> emulated P1
- physical controller 1 -> emulated P2
- physical controller 2 -> emulated P3
- physical controller 3 -> emulated P4
- true simultaneous routing activates with 2+ physical pads.
- one-pad behavior keeps the legacy `option_controller` / Switch Player semantics.
- controller 0 owns emulator/UI-global hotkeys while multi-pad routing is active.
- autofire state is independent per player.
- CPS1 Forgotten Worlds, CPS2 `pzloop2`, and MVS `popbounc` keep per-player
  analog state.
- MVS hardware-special cases that do not model independent local players remain
  intentionally on the legacy path unless runtime investigation proves a better
  mapping.

PS2 enumerates direct pads and multitap slots in stable connector order:

```text
(0,0), (1,0), (0,1), (1,1), ...
```

The common API is in `src/common/input_driver.h`. PSP/Desktop currently expose a
single physical controller; PS2 exposes indexed pads/multitap slots.

## Validation already completed

- PS2 build matrix: CPS1/CPS2/MVS/NCDZ baseline, GUI, and GUI + SAVE_STATE +
  COMMAND_LIST all pass.
- PR #10 CI for the current branch is green after `a647d0c`:
  - Desktop
  - ROMCNV
  - Web ROM Converter
  - PS2 CMAKE
  - PSP CMAKE
- PCSX2 2.9.70 was exercised with isolated `-datapath` configurations so the
  user's normal PCSX2 settings were not modified.
- One-pad regression: NJEMU reports exactly one active controller with only
  `P1/S1` configured, preserving the legacy one-pad routing branch.
- Two direct pads: NJEMU reports two active controllers and independently polls
  `(port 0, slot 0)` and `(port 1, slot 0)`.
- MVS normal 2P runtime routing verified: P1 and P2 modify independent MVS input
  ports. During this validation a real bug was found and fixed: secondary pads
  could propagate remapped `TEST_SWITCH`; `f176a5a` now clears secondary
  service/test system flags.
- The MVS system-flag fix was explicitly runtime-tested with a temporary mapping:
  P2 kept `TEST_SWITCH=0`, while the same input from P1 produced
  `TEST_SWITCH=1`. Temporary mapping/logging was removed afterwards.
- Multitap runtime: with both multitaps enabled PCSX2 configured all eight
  endpoints and NJEMU reported eight active controllers in stable order:
  `(0,0), (1,0), (0,1), (1,1), ... (0,3), (1,3)`.
- CPS1 `captcomm`: P3 (`controller 2`, `(0,1)`) and P4 (`controller 3`, `(1,1)`)
  were exercised and routed exclusively to the P3/P4 arcade ports.
- CPS1 `mercs`: P3 (`controller 2`) was exercised through multitap and modified
  only the P3 arcade port.
- CPS1 `slammast`: P4 (`controller 3`) was exercised through multitap and
  modified only the P4 arcade port.
- CPS1 `forgottn`: P1 and P2 dial inputs were held independently; only the
  corresponding `input_analog_value[]` accumulator advanced for each pad.
- CPS1 `1941`: rotated input adjustment was exercised with both direct pads and
  retained independent P1/P2 routing after rotation.
- CPS1 `sf2`: the six-button split-port path was exercised from both pads; the
  same extra button set distinct P1/P2 bits without cross-talk.
- One-pad Switch Player was explicitly runtime-tested with a temporary binding:
  `option_controller` cycled `0 -> 1` (and back after the normal debounce period).
  Temporary mapping/logging was removed afterwards.
- CPS2 `ssf2`: direct P1/P2 runtime routing verified on independent port bits.
- CPS2 legacy `P2_START` / `Start2` compatibility routing was found to be
  inappropriate in true multi-pad mode because it can intentionally start the
  opposite emulated player from one physical pad. The multi-controller path now
  suppresses `P2_START`; a temporary `Start2=L` runtime test confirmed both pads
  receive L independently without cross-player Start leakage. Test logging and
  forced mappings were removed afterwards.
- NCDZ `Windjammers`: direct P1/P2 runtime routing verified on independent NCDZ
  ports.
- A fresh PS2 `GUI + SAVE_STATE + COMMAND_LIST` build matrix passes for all four
  cores after removing all test instrumentation.
- Desktop MVS `GUI + COMMAND_LIST` (SAVE_STATE off) also passes. Desktop
  `SAVE_STATE=ON` currently fails in pre-existing `src/common/state.c` code
  (`UI_TEXTURE` plus pointer-to-`uint32_t` casts), unrelated to multi-controller
  input.
- No local PSP toolchain is installed under `/Users/fjtrujy/toolchains`; the most
  recent PR #10 PSP CI supplied by the user is green.

All temporary runtime logging/instrumentation used for these checks was removed
before committing source changes.

## Recommended ROM validation corpus

Use parent sets unless noted. This is a coverage-oriented corpus: each entry
exercises a distinct input-routing path rather than duplicating equivalent
clones.

### CPS1

- `captcomm` — canonical 4-player routing; P3/P4 already validated in PCSX2.
- `slammast` — 4-player path with the three-button mapping.
- `mercs` — canonical 3-player CPS1 routing.
- `wofch3p` — alternate 3-player port layout (`INPTYPE_wofch3p`).
- `forgottn` — per-player dial/analog accumulator path.
- `sf2` — normal two-player six-button split-port path.
- `1941` — rotated/vertical input-adjustment path.

### CPS2

- `ssf2` — normal two-player six-button path; already validated in PCSX2.
- `avsp` — three-player routing and P3 start/coin handling.
- `ddtod` — four-player, four-button routing and 4-player coin/start handling.
- `batcir` — four-player, two-button routing with a different coin-chute table.
- `pzloop2` — per-player dial/analog path.
- `progear` — real legacy `Start2` binding; validates that it remains a one-pad
  compatibility feature and cannot cross players in multi-pad mode.
- `19xx` — rotated two-button CPS2 mapping.

### MVS

- `pbobbl2n` (or another ordinary two-player MVS title) — normal P1/P2 routing,
  MVS/AES start/coin and global-hotkey baseline.
- `fatfursp` — exclusive digital/analog special poller.
- `fatfursa` — clone regression for the NGH-based Fat Fury Special poller.
- `popbounc` — independent per-player analog/paddle accumulators.
- `irrmaze` — special analog hardware; intentionally remains on legacy routing.
- `vliner` — special non-standard controller/coin path; intentionally legacy.
- `jockeygp` — special non-standard controller path; intentionally legacy.

### NCDZ

- `Windjammers` — representative simultaneous two-player NCDZ path; already
  validated in PCSX2. Additional ordinary NCDZ two-player titles are redundant
  for input routing unless a game-specific issue appears.

## Next-session objective

Finish physical simultaneous multi-controller support for **every emulator core
where the arcade hardware/game actually supports independent local players**.
Do not redesign unrelated input/config systems.

Start by auditing the existing implementation rather than rewriting it. Then
perform runtime tests and fix every issue found.

Remaining validation/fix order:

1. One PS2 pad:
   - routing and Switch Player are runtime-validated; retain a final visual smoke
     test on real hardware if convenient.
2. CPS2 3/4-player games through multitap when legal local ROMs are available:
   - `avsp`, `ddtod`, `batcir`;
   - verify P3/P4 coin/start routing in addition to the CPS1 `captcomm` validation
     already completed.
3. MVS special cases when legal local ROMs are available:
   - `fatfursp` exclusive digital/analog poller;
   - `popbounc` per-player analog routing;
   - keep `irrmaze` on the legacy special-hardware path unless runtime evidence
     shows an independent-player model is correct.
4. While several pads are attached:
   - menu;
   - screenshot;
   - save/load state;
   - command list.
5. Hotplug/multitap on real hardware:
   - second direct pad attach/remove where supported;
   - multitap present at boot;
   - late multitap discovery where supported.

Use PCSX2 when it can reliably emulate multiple controllers. If real-hardware
validation is required, prepare a suitable PS2 ELF/package and state exactly what
must be tested; do not claim a runtime test that was not actually performed.

If runtime investigation reveals missing support in common/core code, implement
it for all applicable cores rather than adding a game-specific workaround unless
the original arcade hardware genuinely requires one.

## Build environment for PS2

```sh
export PS2DEV=/Users/fjtrujy/toolchains/ps2/ps2dev
export PS2SDK=$PS2DEV/ps2sdk
export GSKIT=$PS2DEV/gsKit
export PATH="$PATH:$PS2DEV/bin:$PS2DEV/ee/bin:$PS2DEV/iop/bin:$PS2DEV/dvp/bin:$PS2SDK/bin"
```

CMake PS2 builds use:

```text
-DCMAKE_TOOLCHAIN_FILE=$PS2DEV/share/ps2dev.cmake
```

## Working style

- Inspect repository instructions, `git status`, and recent `git log` first.
- Preserve all user changes.
- Work autonomously through stable chunks without repeatedly asking permission.
- Run the relevant build matrix after meaningful changes.
- Commit stable validated source changes in small logical commits.
- Explicitly verify that no `resources/` path is staged before every commit.
- Keep `PHYSICAL_MULTICONTROLLER_PLAN.md` updated if findings change the design
  or completion status.

## Prompt to paste into a new ChatGPT session

```text
Continúa el soporte de multi-mando físico de NJEMU trabajando directamente en:

/Users/fjtrujy/Projects/NJEMU

Rama: gui

NO crees otro worktree.

Antes de modificar nada, lee las instrucciones del repositorio, revisa `git status`
y `git log`, y después lee:

- PHYSICAL_MULTICONTROLLER_NEXT_SESSION.md
- PHYSICAL_MULTICONTROLLER_PLAN.md
- PORTING_PLAN.md

Regla crítica: NO tocar, revertir, stagear ni comitear nada dentro de `resources/`.
Usa staging explícito; nunca `git add -A` ni `git commit -a`.

El routing multi-mando ya está implementado para CPS1, CPS2, MVS y NCDZ, junto
con el input indexado y soporte de multitap PS2. No lo rehagas desde cero.
Continúa desde el estado documentado y completa/valida el multi-mando físico en
todos los cores donde el hardware/juego soporte jugadores locales independientes.

Prioriza pruebas reales/runtime con 1 y 2 mandos, multitap 3/4 jugadores,
CPS1/CPS2, MVS (incluyendo fatfursp/popbounc y casos especiales) y NCDZ. Usa
PCSX2 cuando sea fiable; si hace falta hardware real, genera el ELF/package y
dime exactamente qué debo probar. Corrige cualquier problema encontrado,
valida las matrices de build relevantes y haz commits pequeños y estables.

La CI de la PR #10 está actualmente verde (Desktop, ROMCNV, Web, PS2 y PSP).
Trabaja de forma autónoma y avanza todo lo posible sin pedirme confirmación para
cada iteración.
```
