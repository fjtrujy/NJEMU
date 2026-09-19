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
- Multitap runtime: with both multitaps enabled PCSX2 configured all eight
  endpoints and NJEMU reported eight active controllers in stable order:
  `(0,0), (1,0), (0,1), (1,1), ... (0,3), (1,3)`.
- CPS1 `captcomm`: P3 (`controller 2`, `(0,1)`) and P4 (`controller 3`, `(1,1)`)
  were exercised and routed exclusively to the P3/P4 arcade ports.
- CPS2 `ssf2`: direct P1/P2 runtime routing verified on independent port bits.
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

## Next-session objective

Finish physical simultaneous multi-controller support for **every emulator core
where the arcade hardware/game actually supports independent local players**.
Do not redesign unrelated input/config systems.

Start by auditing the existing implementation rather than rewriting it. Then
perform runtime tests and fix every issue found.

Remaining validation/fix order:

1. One PS2 pad:
   - explicitly exercise Switch Player in the GUI (the one-pad branch itself is
     already runtime-validated).
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
