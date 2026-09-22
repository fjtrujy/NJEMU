# Exhaustive loader / decrypt / init / cache audit

Status: 2026-09-20

This document records the exhaustive loader/decrypt/init/cache phase that follows
`docs/RUNTIME_COVERAGE_AUDIT.md`. The earlier runtime audit remains a separate
game-dependent branch-coverage phase.

## Scope

The exhaustive pass covers:

- CPS1 parent/clone loader aliases and custom init selection;
- CPS2 raw/ZIP/folder cache loading, Phoenix/decrypted sets, USER1/key paths and
  converter output;
- MVS P/C/S/M/V transforms, cache formats, parent/clone cache policy, program and
  sound decryption, PCB paths and protection initialization;
- NCDZ initial IPL loading, file-type loaders and the stateful later-load handlers
  used by AOF3, Last Blade and Last Blade 2.

All generated caches, runtime aliases and test logs used by this audit live under
`build_audit_*` directories. The user-owned `resources/` tree is read-only for
this work and is never staged or committed.

## CPS1

The exhaustive structural pass crossed every `FILENAME`/driver alias and custom
init selector with the loader implementation.

Fixes found:

- `3a9071a Fix CPS1 legacy driver aliases` restores legacy names that are valid
  in the local ROM corpus but were absent from the driver alias table.
- `d5f2203 Restore CPS1 custom bootleg init selection` restores the missing
  custom bootleg init dispatch discovered while enumerating the complete init
  matrix.

Representative runtime sweeps were run against the local CPS1 corpus after the
structural fixes. The exhaustive loader/init matrix has no remaining known CPS1
gap.

## CPS2

The exhaustive CPS2 pass validated all distinct cache/decryption families rather
than treating ordinary runtime game coverage as sufficient.

Validated paths include:

- raw cache;
- ZIP cache;
- folder cache;
- ordinary encrypted sets;
- Phoenix/decrypted sets;
- USER1/key-backed decryption coverage;
- cache converter output and runtime consumption.

The converter/runtime regression pass found a 64-bit ZIP handle truncation that
could create incomplete ZIP caches (for example a ZIP containing only an empty
`000` entry). It was fixed by:

- `e0efee8 Fix 64-bit ROM converter ZIP handles`.

Post-fix converter validation included repeated complete MVS ZIP conversions and
a CPS2 `avsp -zip` regression. No remaining CPS2 loader/decrypt/cache branch is
known to be uncovered.

## MVS

### Corpus and matrix

`rominfo.mvs` defines 305 sets and all 72 init IDs (0 through 71) occur in the
database.

The local ROM corpus contains 24 sets and dynamically represents these init IDs:

- 0: ordinary Neo Geo path (16 local sets);
- 8: `mslug3`;
- 16: `mslug5`;
- 31: `ms5pcb`;
- 33: `kf2k3pcb`;
- 34: `jockeygp`;
- 35: `vliner` / `vlinero`;
- 49: `kog`.

Init IDs not represented by local ROMs were checked statically against the
region flags in `rominfo.mvs` and every loader/decrypt switch that consumes
those flags.

### Program decryption and protection

The static CPU1 matrix exposed two independent KOF2003H omissions:

- `50b7c36 Fix KOF2003H program decryption` adds
  `INIT_kof2003h -> kof2003h_decrypt_68k()`;
- `2e16908 Install KOF2003H PVC protection` installs the same PVC
  read/write handlers used by KOF2003.

After those fixes, every init ID attached to an `ENCRYPTED` CPU1 region has a
matching program-decryption path. The CPU2/M1 matrix likewise has no missing
handler for an `ENCRYPTED` CPU2 region.

The protection/fix-bank audit covers the dedicated Fat Fury 2, KOF98, Metal Slug
X, KOF99/Garou/Metal Slug 3, AES, PVC, PCB, BrezzaSoft and bootleg paths. No
additional mismatch was found after adding KOF2003H PVC.

### Cache formats and clone fallback

Folder and ZIP caches were validated separately.

A ZIP cache could contain valid `srom`, `vrom` and `cache_info`, but runtime
attempted to read encrypted S/V regions before `cache_start()` had opened the
ZIP. The fix:

- `c22c1f6 Fix MVS ZIP cache loading`

adds on-demand ZIP opening for S/V and makes the C-ROM ZIP selection respect
`use_parent_crom`.

Dynamic validation includes:

- `mslug3`: folder cache;
- `mslug5`: ZIP cache with decrypted GFX2, VROM and C-ROM cache;
- `kof97` + `kog`: mixed parent/clone cache fallback, where `kog` owns C/S
  while V is inherited from `kof97`;
- newly generated raw/folder and ZIP `kog` caches consumed successfully by the
  runtime.

The mixed `kog` case validates that C/S/V parent fallback is decided per
region, not globally.

### Converter/runtime cache-policy parity

The runtime and converter had separate `MVS_cacheinfo` tables. The converter
was missing `kof98a` and `kof98evo`; they were synchronized in:

- `782964d Sync MVS converter cache policy table`.

A deeper audit found that runtime treats an unlisted clone as inheriting all
cache regions from its parent, whereas single-set `romcnv_mvs` previously
forced C/S/V conversion. There are 34 clone definitions for which that
difference matters structurally.

The fix:

- `becb311 Align MVS cache conversion with runtime`

makes single-set and `-all` conversion use the same per-region policy as
runtime, skips redundant inherited V conversion, and avoids creating unnecessary
clone caches.

With runtime-equivalent policy:

- 128 / 305 MVS sets require at least one cache region;
- 84 large clone sets inherit all cache regions and require no clone cache;
- all SOUND1 transformations that are actually required by the cache policy have
  a converter handler;
- all GFX2/GFX3 transformations required to generate S/C data or `cache_info`
  have a converter handler.

The same pass found that `kf2k3pl` / `kf2k3upl` need the KOF2003 CMC50
`0x9d` transform before producing cache metadata. That transform is included
in `becb311` alongside their bootleg S transformations.

### Local MVS blockers

The local sweep distinguishes loader success from the non-deterministic Desktop
teardown SIGSEGV. The SIGSEGV moved between otherwise successful sets on
repeated runs (for example `fatfury1`, `aof2`, `fatfursp`, `mosyougi`,
`irrmaze`, `pbobbl2n`, `mslug5`), so it is not classified as a per-ROM core
loader failure.

Known corpus blockers:

- `pbobblen`: incomplete local set; shared 068 V/C ROMs are missing;
- `ms5pcb`: local P-ROMs are invalid/zero-filled, so init ID 31 cannot be
  dynamically validated from this corpus.

Revalidated on 2026-09-20 without modifying `resources/`:

- `pbobblen.zip` still lacks `068-v1`, `068-v2`, and `068-c1..c4`;
  a CRC scan across every local MVS ZIP found no copy of any of those six ROMs,
  so a temporary complete set cannot be assembled from the current corpus;
- `ms5pcb.zip` still contains 4 MiB `268-p1r.bin` and `268-p2r.bin`
  payloads with zero non-zero bytes in either file.

These remain corpus blockers, not reasons to relax ROM validation or add source
compatibility paths.

All other locally represented special MVS init IDs reach successful
initialization.

## NCDZ

### Initial IPL loader sweep

The local NCDZ corpus contains 15 CD directories. Because the Desktop no-GUI
frontend stores the selected directory in the short `game_name` buffer, long
disc-directory names were exposed to the test binary through short symlinks
inside `build_audit_ncdz_later/roms`; the original `resources/` names were not
changed.

All 15 local discs reached completion of a real `cdrom_process_ipl()` pass:

- ADK World;
- Art of Fighting 2;
- Art of Fighting 3;
- Crossed Swords II;
- Last Blade;
- Last Blade 2;
- Master of Syougi;
- Metal Slug;
- Metal Slug 2;
- OverTop;
- Real Bout Fatal Fury 2;
- Samurai Shodown RPG (English patch and Japanese);
- Super Sidekicks 3;
- Windjammers.

The largest local IPL contains 31 entries (Real Bout Fatal Fury 2), within the
32-entry `filelist` capacity.

Across that sweep, real IPL data dynamically exercised all six distinct CD file
upload branches produced by ordinary game metadata:

- type 0: PRG;
- type 1: FIX;
- type 2: SPR;
- type 3: Z80;
- type 4: PCM;
- type 5: PAT.

`AXX_TYPE` (type 8) is selected for extensions beginning with `A`. The local
corpus contains `AAA.AAA`, but no local IPL references it. In `upload_file()`
AXX and PRG intentionally share the exact same switch body, so its data-upload
semantics are covered by the dynamic PRG branch; extension classification was
verified structurally.

`PAL_TYPE` and `OBJ_TYPE` belong to the hardware-upload interface in
`driver.c`, not the CD file-extension loader.

### Stateful later-load handlers

The previous runtime audit deliberately stopped short of claiming full AOF3
later-load coverage because synthetic handler execution lacked state established
by a real game-initiated CD load.

The expanded local corpus now permits all three special loadscreen handlers to
be reached through normal emulation. A temporary audit harness only supplied
ordinary Start/A button presses; it did not alter handler addresses or game
memory.

Observed sequences:

- AOF3 (`NGH 0x0096`): after two ordinary later loads using BIOS handler
  `0xc0c814`, the game installed `0x00124400`; the subsequent real load
  processed 19 files and completed with the handler state at `0x001245ce`.
- Last Blade (`NGH 0x0234`): the game installed `0x00124300`; the real
  special load processed 7 files and completed with `0x00124550`.
- Last Blade 2 (`NGH 0x0243`): after two ordinary loads the game installed
  `0x00124400`; the special load processed 9 files and returned successfully,
  with the progress pointer subsequently at `0x00124a7c`.

This closes the stateful later-load item that remained intentionally open in
`RUNTIME_COVERAGE_AUDIT.md`.

The previously fixed Super Sidekicks 3 loading path remains covered by
`cfc1b6d Initialize NCDZ timers before IPL loading`.

### Save-state CD reload path

When `SAVE_STATE` is enabled, the CD subsystem tracks the mutable FIX, SPR and
PCM ranges and reconstructs them through `cdrom_state_load_file()`. These three
types map back through the same `upload_file()` implementations validated by
the IPL sweep. The SAVE_STATE configurations are included in the final build
matrix; no separate game-specific reload handler exists.

The final SAVE_STATE build pass also exposed previously hidden Desktop
portability issues outside the NCDZ loader itself. They were fixed in:

- `4e06845 Fix desktop save-state portability`.

That fix removes an unnecessary target `timer.h` dependency from the Desktop
ticker, uses the cross-platform UI texture driver instead of the PSP-only
`UI_TEXTURE` address when serializing thumbnails, preserves the fixed thumbnail
payload with zeroes when no GUI texture exists, replaces 32-bit pointer casts
with pointer subtraction, and supplies the missing no-GUI progress initializer.

## Final validation and closure

All temporary `NJEMU_EXHAUSTIVE_AUDIT` instrumentation was removed before the
final build matrix. Validation then passed for:

- Desktop CPS1/CPS2/MVS/NCDZ with GUI disabled;
- Desktop CPS1/CPS2/MVS/NCDZ with GUI enabled;
- Desktop CPS1/CPS2/MVS/NCDZ with `SAVE_STATE=ON` and GUI disabled;
- Desktop NCDZ with both GUI and `SAVE_STATE=ON`;
- PS2 CPS1/CPS2/MVS/NCDZ with GUI, `SAVE_STATE` and `COMMAND_LIST` enabled;
- `romcnv_cps2`;
- `romcnv_mvs`.

The final source/document staging is explicitly checked to contain no path under
`resources/`. Therefore the exhaustive loader/decrypt/init/cache phase is
closed.

The only remaining dynamic gaps are caused by unavailable or invalid local ROM
data and are recorded above; they are not unhandled loader/decrypt/init
branches.
