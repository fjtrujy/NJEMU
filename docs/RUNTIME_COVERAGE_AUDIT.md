# Runtime / Game-Dependent Coverage Audit

Status: 2026-09-20

This document records the runtime coverage pass for CPS1, CPS2, MVS, and NCDZ.
It deliberately separates game-dependent runtime branch coverage from a future
exhaustive loader/decrypt/init matrix.

## Scope and acceptance criteria

The current phase answers a narrower question than "has every ROM variant and
every loader/decrypt/init path been executed?":

1. identify meaningful game-dependent runtime branches;
2. map those branches to representative games;
3. execute the branches with real ROM data where practical;
4. fix product bugs exposed by the runtime pass;
5. record ROM/cache/data blockers rather than weakening validation to get past
   bad inputs.

An exhaustive pass over every clone, ROM layout, cache format, decryption
routine, init function, and later stateful load sequence is useful future work,
but it does **not** block completion of this runtime-coverage phase.

All destructive/runtime test state was kept outside `resources/`. Temporary
instrumentation and forced test modes were removed before product commits.

## CPS1

Representative runtime coverage is broad and includes:

- `ghouls`
- `3wonders`
- `pang3`
- `wof`
- `dino`
- `punisher`
- `varth`
- `cworld2j`
- `qad`
- `qadj`
- `qtono2`
- `megaman`
- `pnickj`
- `sfzch`
- `sf2m2`
- `sf2rb`
- `sf2rb2`
- `dinoh`
- `wofh`

The multi-controller pass additionally exercised distinct CPS1 input layouts:
`captcomm` 4P, `mercs` 3P, `slammast` 4P, `forgottn` independent dial
state, `1941` rotated routing, and `sf2` split six-button routing.

The remaining game-specific runtime patches were also exercised directly:

- `sf2rb`: bootleg runtime patch reached;
- `sf2rb2`: distinct bootleg runtime patch reached;
- `sf2m2`: previously covered runtime patch;
- `dinoh`: Q-Sound runtime patch reached.

`dinoha` is not available in the local ROM set, but it selects the exact same
runtime patch and address as `dinoh`. It is therefore a clone-equivalent path,
not a semantically distinct runtime branch for this phase.

### Fixes found

- `78305a8 Fix CPS1 quiz driver names`: fixes the `qadj` / `qtono2`
  identification mismatch found while selecting representative quiz branches.

No remaining CPS1 runtime branch is known to block this phase. A future
exhaustive pass can still enumerate every parent/clone loader and init
combination.

## CPS2

The earlier pass covered the main `CPS2_KLUDGE_*` families, ordinary 2P
layouts, vertical/rotated input, MMATRIX coin lockout, and the legacy
`P2_START` compatibility behavior.

The final multi-pad pass used PCSX2 with an isolated datapath and four virtual
PS2 pads in the same connector order used by NJEMU:

- controller 0: P1/S1
- controller 1: P2/S1
- controller 2: P1/S2
- controller 3: P1/S3

For 3P/4P tests the harness selected a valid arcade coin/player mode only in
temporary source instrumentation. It did not modify ROMs, caches, saved config,
or `resources/`.

### `avsp` — 3P

Covered:

- P3 directional routing to the P3 byte;
- P3 Start;
- P3 Coin in a 3P/3-chute mode;
- no P1/P2 cross-talk.

Observed representative transitions included P3 input changing only the P3
portion of CPS2 port 1, P3 Start clearing the P3 Start bit in port 2, and P3 Coin
clearing the third coin-chute bit.

### `ddtod` — 4P / 4-button layout

Covered independently for P3 and P4:

- directional input;
- Start;
- Coin in 4P/4-chute mode.

The runtime pass confirmed that P3 and P4 occupy separate bytes of port 1 and
separate Start/Coin bits of port 2.

### `batcir` — 4P / 2-button layout

Covered the same P3/P4 movement, Start, and Coin cases through the distinct
`INPTYPE_batcir` routing table. This avoids treating `ddtod` as sufficient
coverage for every four-player CPS2 layout.

### `pzloop2` — independent paddle accumulators

The paddle transition was explicitly observed for both physical players.

Starting from zero:

- P1 paddle-left changed accumulator 0 to 246 while accumulator 1 stayed 0;
- P2 paddle-left then changed accumulator 1 to 246 while accumulator 0 stayed
  246;
- the packed analog port moved from `0x00f6` to `0xf6f6`.
- the real game was also observed changing `readpaddle` from `0` to `2`,
  covering the runtime stick/paddle mode switch rather than only the underlying
  accumulator updates.

This confirms that the two `input_analog_value[]` accumulators advance
independently with no cross-talk.

### Fixes found

- `f6de133 Prevent CPS2 cross-player start input in multi-pad mode`: suppresses
  the legacy one-pad `P2_START` binding while true multi-pad routing is active.
- `50485d9 Ignore invalid CPS2 EEPROM input modes`: an erased CPS2 EEPROM is
  initialized to `0xff`. The old code used that byte directly as an index into
  the 16-entry `inp_eeprom_value[]` table. Runtime testing on `avsp`
  confirmed `0xff` on a clean launch. Invalid values are now ignored until the
  game writes a valid mode, avoiding an out-of-bounds read and preserving the
  driver's initial player-count semantics.

The game may subsequently write a valid EEPROM cabinet mode (for example,
`avsp` factory initialization selected a 2-player mode during the audit).
That is game configuration behavior and is separate from physical-controller
routing.

## MVS

Runtime-validated representative cases:

- `fatfursp`
- `fatfursa`
- `popbounc`
- `irrmaze`
- `vliner`
- `jockeygp`
- `tpgolf`
- `mosyougi`
- `mahretsu`
- `kf2k3pcb`
- `kog`

This covers ordinary routing plus the main special input / analog / hardware
cases used by the MVS driver.

### Fixes found

- `d773d19 Select Fat Fury Special input by NGH id`: parent and clone sets share
  the Fat Fury Special poller.
- `b2d1d80 Support legacy MVS ROM set variants`: supports the
  `fatfursa` / `fatfurspa` naming variant and the legacy `irrmaze` P-ROM
  while preserving `irrmaze` NGH `0x0236` and its analog poller.
- The multi-controller audit also fixed secondary service/test ownership in
  `f176a5a`.

`kog` cache generation was validated in a temporary build only; no generated
cache was written into `resources/`.

### ROM blocker: `ms5pcb`

`ms5pcb` is not an emulator-coverage failure. The supplied
`268-p1r.bin` and `268-p2r.bin` files are each 4 MiB and entirely zero-filled.
They are invalid input data. Do not add an emulator workaround to accept them.

The runtime phase records this as a ROM blocker and moves on.

## NCDZ

Runtime-confirmed game-dependent paths:

- `wjammers`: generic path plus NGH identification;
- `aof2`: automatic raster enable plus `RASTER_AOF2`;
- `lastbld2`: `hack_irq=1`;
- `ssrpg`: program patch plus sprite-priority rendering path;
- `crsword2`: VRAM patch;
- `adkworld`: VRAM patch;
- `rbff2`: VRAM patch;
- `mosyougi`: raster path plus `busy=1`;
- `overtop`: special loading start and stop paths;
- `ssideki3`: loading-time `timer_update_subcpu()` path.

### Later-load branches

The `aof3`, `lastblad`, and `lastbld2` special loading branches depend on
handlers installed during later game-initiated loads. During the initial IPL,
`0x11c80c` still points to the BIOS default handler `0xc0c814`.

A controlled runtime harness, after real game data had been loaded, confirmed
selection of all three game-specific branches. Synthetic execution of the
`aof3` handler was intentionally not counted as complete runtime coverage:
the handler expects state established by a real later CD load and remained
inside `m68000_execute2()` when that state was invented.

Therefore:

- game-dependent branch selection is covered for this phase;
- full stateful execution of every later-load handler remains a valid future
  deep-runtime test;
- it does not block the current audit.

### Fix found

Enabling the real NCDZ loadscreen exposed a clean-start crash in
`ssideki3`. `cdrom_process_ipl()` can call `timer_update_subcpu()` before
the first normal reset, while the Z80 CPU timer callback had not yet been
initialized.

- `cfc1b6d Initialize NCDZ timers before IPL loading` initializes the timer CPU
  descriptors before IPL processing.
- With the fix, `ssideki3` reached `timer_update_subcpu()` and continued into
  subsequent loading instead of jumping through a null callback.

## Validation after removing instrumentation

Product changes were rebuilt after all temporary runtime hooks were removed.

- NCDZ Desktop Release: pass.
- CPS2 Desktop Release: pass.
- CPS2 PS2 Release with `GUI=ON`, `SAVE_STATE=ON`, and
  `COMMAND_LIST=ON`: pass.
- Source grep confirmed no `RUNTIME_AUDIT` / audit helper remained in the
  affected NCDZ or CPS2 source before committing fixes.

## Future exhaustive loader/decrypt/init coverage

The following work is intentionally separate from this phase:

- enumerate every CPS1 parent/clone loader and init combination;
- enumerate every CPS2 decryption key / Phoenix/decrypted set / cache-loader
  combination;
- enumerate every MVS P/C/S/M/V ROM transform, protection/decrypt path, cache
  format, parent/clone fallback, and PCB-special init path;
- execute every NCDZ later-load handler from naturally reached game state rather
  than branch-selection harnesses;
- add any additional malformed-ROM corpus tests that validate rejection behavior
  without weakening ROM validation.

Those tasks can be pursued as a dedicated exhaustive matrix. They should not
retroactively turn already-covered runtime game branches into blockers.
