# PS2 internal HDD boot (issue #23)

## Current universal launch (ps2_drivers PR #29)

Canonical PS2 CPS2/MVS releases use **one universal `BOOT.ELF`** copied from
the installed `ps2_drivers_irximg_bootstrap.elf`. It is the **same byte-for-byte
bootstrap** on USB FAT32, MX4SIO and internal APA/PFS HDD; ATA-backed BDM
FAT/exFAT is also handled by the generic driver bootstrap. NJEMU no longer
installs or packages `BOOT_HDD.ELF` or `BOOT_ATA.ELF`.

Each target directory must contain the universal `BOOT.ELF`, the extensionless
core executable (`MVS` or `CPS2`), `elf_path.ini` naming that executable,
`ps2_drivers.irximg`, and the core's installed `lang/`, `font/`, databases,
and user-provided ROM/cache files. Run the `BOOT.ELF` *in that directory*.

On APA/PFS, wLaunchELF must pass a partition-qualified launch argument such
as `hdd0:__common:pfs:/NJEMU/MVS/BOOT.ELF`. The generic bootstrap accepts
wLaunchELF `-r`/`-nr` options, recovers an initially malformed cwd, remounts
the correct PFS partition and passes launch context to NJEMU. NJEMU still
captures the partition before its own IOP reset and restores it afterward via
the public `ps2_hdd_launch_*()` APIs. FAST CACHE remains disabled on HDD.

**Real-hardware status:** the standalone universal-bootstrap `HDD_SMOKE.zip`
from `ps2_drivers` PR #29 passed on a physical PS2 with wLaunchELF, including
IRX staging, a further IOP reset and PFS re-read. This proves the generic
bootstrap, **not** NJEMU gameplay. NJEMU MVS/CPS2 need their own real-hardware
validation.

### NJEMU PCSX2 validation with the universal bootstrap

Against merged `ps2_drivers/njemu` commit `069f6cbd`, all four canonical GCC
PS2 cores built and a complete canonical ZIP was generated. The MVS and CPS2
`BOOT.ELF` copies match the installed universal bootstrap byte-for-byte
(SHA-256 `dc25723bceebd9271f66a7bbfaec0cde588a80eb1305f1fab6fa4536bdde042e`).
The release tests reject missing, mismatched or obsolete bootstraps.

The following checks used PCSX2 2.9.142 and **clones** of existing isolated
test storage images and profiles. Original test images, normal emulator
settings, ROM sources and all `resources/` files were left untouched:

- **APA/PFS HDD, MVS:** A PFS-resident newer wLaunchELF was started using a
  host-loaded diagnostic handoff, then its auto-launch opened
  `pfs0:/NJEMU/MVS/BOOT.ELF` on the cloned 8 GiB HDD. The universal
  bootstrap recovered a malformed startup cwd and loaded the core; NJEMU
  captured and restored `hdd0:__common` after its own IOP reset. A GUI-off
  Windjammers build loaded its Neo Geo BIOS and all ROM members from PFS,
  initialized audio, and visibly continued to attract mode at 60 FPS.
- **APA/PFS HDD, CPS2:** The same wLaunchELF method loaded
  `pfs0:/NJEMU/CPS2/BOOT.ELF`. An Alien vs. Predator GUI-off build loaded
  the ROM members, initialized the 16 MiB graphics cache and PS2 audio.
  The standard GUI-on MVS and CPS2 builds also reached their video backends.
- **USB FAT32, MVS:** A cloned 256 MiB virtual USB image received the same
  universal bootstrap SHA, external driver image, core and game files.
  A host-loaded USB launch helper loaded `mass0:/UNIVERSAL_TEST/BOOT.ELF`
  **from FAT32**, not a host-side NJEMU binary. The bootstrap identified and
  reused live USB storage; NJEMU completed its IOP reset, loaded Windjammers
  ROM members from USB and initialized audio. PCSX2 printed an `ohci_die:
  DMA error` warning during USB startup, but subsequent game-file reads and
  audio initialization succeeded. This warning warrants separate follow-up
  if it occurs on hardware.

The initial attempt with an **older** wLaunchELF binary failed inside that
loader (`memory allocation failed`) before the bootstrap executed. Replacing
only the wLaunchELF test binary with the newer validated one allowed the
identical universal `BOOT.ELF` to boot. This is not evidence of a NJEMU
bootstrap regression.

These tests establish functional HDD/USB bootstrap and game-loading paths,
not physical NJEMU compatibility, MX4SIO hardware operation, long-duration
stability or quantitative audio/frame pacing accuracy. Evidence and logs are
in the ignored `build_universal_ps2/` test directory.

### Historical two-bootstrap investigation (prior to PR #29)

The remainder of this document records the previous HDD-only bootstrap
investigation for comparison. Names such as `BOOT_HDD.ELF` below describe
**old test artifacts**, not current NJEMU releases.

## Scope and launch selection (historical)

The canonical PS2 CPS2/MVS release uses an extensionless core executable
(`CPS2` or `MVS`) and the external `ps2_drivers.irximg`. It now packages
two entry points:

| Launcher | Storage | Driver handoff |
| --- | --- | --- |
| `BOOT.ELF` | USB / MX4SIO | Installed PS2 drivers image bootstrap (unchanged) |
| `BOOT_HDD.ELF` | Internal APA/PFS HDD | Installed `ps2_drivers_irximg_hdd_bootstrap.elf` |

Copy **all** files from the corresponding release package into the same
directory on the target storage. Launch `BOOT_HDD.ELF` from a PS2 HDD file
browser that preserves the original partition path in an argument such as
`hdd0:__common:pfs:/NJEMU/MVS/BOOT_HDD.ELF`. The HDD launcher expects a
current directory of `pfs:/...` and the APA partition argument; it reports
a clear error when the launcher does not supply either.

There is no need to rename the core executable, and the normal USB
`BOOT.ELF` remains available. CPS1/NCDZ canonical releases use their
existing embedded-driver ELF rather than this dual-launcher layout.

`PS2_FAST_CACHE=ON` is a compiled capability; runtime cacheio acceleration
is only requested for USB/MX4SIO. Internal HDD uses the regular filesystem
backend, including the standard `USE_CACHE` ROM streaming functionality
for MVS/CPS2 when a game needs it.

## Root cause

The installed external `BOOT.ELF` bootstrap used by NJEMU restores a
removable-storage filesystem after an IOP reset. When started from a PFS
partition, it attempted USB recovery instead:

```text
[irximg-bootstrap] restoring launch filesystem at pfs:/NJEMU/MVS
[irximg-bootstrap] launch filesystem unavailable; restoring mass:
[irximg-bootstrap] failed to restore mass filesystem
```

The embedded-driver control build could start NJEMU, but after its own IOP
reset the generic filesystem detector did not identify `pfs:/...` as an HDD
boot device. The launch path's original APA partition name is lost if only
the PFS working directory is used.

The reusable `ps2_drivers` HDD bootstrap captures the original partition
from the launcher's argv, restores PFS, loads the external core without
another IOP reset, and passes the APA-qualified path as its launch argument.
NJEMU calls `ps2_hdd_launch_capture_current()` before its own IOP reset,
selects `BOOT_DEVICE_HDD0` for external IRX staging, and calls
`ps2_hdd_launch_restore()` after IOP synchronization and SBV patches.
The application still owns audio, GUI, input, ROM loading, FAST CACHE policy,
and orderly shutdown. `getBootDeviceID("pfs:/...")` is unchanged.

`ps2_drivers` PR #26 also corrected the HDD dependency closure: APA/PFS
requires six IRXs (IOMANX, FILEXIO, PS2DEV9, PS2ATAD, PS2HDD, PS2FS),
not eight. In particular, BDM and BDMFS_FATFS are no longer staged for HDD.
NJEMU consequently no longer discards an unused BDM pair. The input
initialization still rejects nonzero `ps2_drivers_img_staged_modules()`
or `ps2_drivers_img_staged_bytes()` after initialization.

Missing or malformed APA metadata is a launch error on PFS: the application
does not try a generic current-device fallback and does not infer an arbitrary
partition from the PFS path. Non-HDD launch and storage paths are unchanged.

## Test evidence

Validated on PCSX2 2.9.142, macOS, using a separate sparse **8 GiB**
APA/PFS HDD image with an `__common` partition. The owner's previously
configured **40 GiB** `DEV9hdd.raw` image was not used or modified.

- Original `BOOT.ELF` from PFS: reproduces failed removable-storage
  fallback and returns to the PS2 browser.
- Embedded MVS: mounts HDD when APA partition metadata is preserved, then
  reaches the GUI when its runtime `lang/en.lng` is present.
- Original external-image MVS via `BOOT_HDD.ELF`: reaches the NJEMU GUI splash;
  video/audio/pad initialization succeeds without staged-driver errors.
- Original external-image CPS2 via `BOOT_HDD.ELF`: reaches the CPS2 GUI splash;
  video/audio/pad initialization succeeds without staged-driver errors.
- External-image MVS with `GUI=OFF`: with `neogeo.zip`,
  `wjammers.zip`, `rominfo.mvs` and `game_name.ini` on the test PFS
  partition, Windjammers loaded all 065 program/sound/sprite ROM members,
  initialized audsrv audio, and advanced into the running attract-mode
  game. A PCSX2 screenshot showed the playfield at **60 FPS**.
- Neither test used the USB/MX4SIO FAST CACHE driver on the HDD path.

### Generic drivers migration (PR #25 with ps2_drivers PR #26)

Validated against `ps2_drivers` commit `2949ebbfe765dca960d639f9d88a31b37ec25f7d`
using the same isolated PCSX2 8 GiB APA/PFS image:

- Installed the generic driver build, its public HDD launch header, both
  bootstrap ELFs, and external IRX archive. All four driver checks completed:
  deterministic image, staged dependencies, HDD launch parsing, and
  external-image payload.
- Rebuilt both external MVS and CPS2 cores and their canonical packages.
  Both launch through the installed generic HDD bootstrap, preserve
  `hdd0:__common`, remount `pfs:/NJEMU/<target>/` after the application's
  IOP reset, and initialize audio and PS2 controller input.
- MVS `GUI=OFF` loaded Windjammers and its Neo Geo BIOS and ROM members,
  initialized audio, completed the ROM-loading sequence, and kept running.
- CPS2 `GUI=OFF` loaded Alien vs. Predator (`avsp`) from PFS, decoded
  graphics, reported a 16 MiB graphics cache, completed decryption to 100%,
  initialized audio and controllers, and kept running.
- No `startup left staged modules` or filesystem-restore errors appeared
  in either PCSX2 log. The emulator did not invoke FAST CACHE on the HDD path.
  These are functional ROM-loading checks, not quantitative frame-rate
  measurements or physical-console validation.
- A negative PCSX2 launch probe with an unqualified `pfs:/...` argument
  reached the generic HDD bootstrap but did not load NJEMU; execution
  returned to the PS2 browser. The `ps2_drivers_hdd_launch_check` host
  target also covers malformed or missing partitions.
- After testing, the original MVS/CPS2 executables, bootstraps and IRX
  images were restored in the isolated test partition. The temporary CPS2
  `avsp` ROM, cache and test `game_name.ini` were removed.

The generic USB bootstrap remains 300,116 bytes; the generic HDD bootstrap
is 322,516 bytes, replacing NJEMU's roughly 1.7 MiB local HDD bootstrap.
The external IRX image is 557,440 bytes for this tested driver revision.
HDD staging drops from 8 IRXs / 152,004 bytes (HDD + explicit BDM in the
same driver revision) to 6 IRXs / 104,206 bytes; including NJEMU application
drivers, the dependency closure drops from 14 to 12 IRXs.

The original PR #25 build outputs and the new generic-driver release builds
had these binary sizes (bytes; identical GUI and external-IRX options):

| Target and artifact | Original PR #25 | Generic drivers | Difference |
| --- | ---: | ---: | ---: |
| MVS `BOOT.ELF` | 300,116 | 300,116 | 0 |
| MVS `BOOT_HDD.ELF` | 1,704,616 | 322,516 | -1,382,100 |
| MVS core | 3,651,124 | 3,649,872 | -1,252 |
| MVS IRX image | 558,640 | 557,440 | -1,200 |
| MVS four-file total | 6,214,496 | 4,829,944 | -1,384,552 |
| CPS2 `BOOT.ELF` | 300,116 | 300,116 | 0 |
| CPS2 `BOOT_HDD.ELF` | 1,704,612 | 322,516 | -1,382,096 |
| CPS2 core | 3,290,228 | 3,290,796 | +568 |
| CPS2 IRX image | 558,640 | 557,440 | -1,200 |
| CPS2 four-file total | 5,853,596 | 4,470,868 | -1,382,728 |

The rebuilt USB bootstrap's SHA-256 matches the byte-for-byte backup of
the previously installed USB bootstrap. The canonical four-core PS2 ZIP
packages successfully with `BOOT_HDD.ELF` in both MVS and CPS2; release
fixture tests now reject a missing HDD bootstrap.

Regression compilation succeeded for all four embedded-driver PS2 targets
(CPS1, CPS2, MVS and NCDZ), all four PSP targets, and all four Desktop Debug
targets. The Desktop test suites passed 54/54 (MVS), 47/47 (CPS2),
45/45 (NCDZ), and 45/45 (CPS1). A Desktop Release-mode test build exposes
an unrelated existing issue: `desktop_gamepad_tests.c` relies on
`assert()` to use test variables, and `NDEBUG` makes its strict-warning
compilation fail. No unrelated code was changed to hide this failure.

USB/MX4SIO startup and physical PS2 HDD operation were not exercised
on hardware in this migration. The USB bootstrap binary and NJEMU's
non-HDD boot logic are unchanged; this is not a substitute for a real
USB/MX4SIO runtime regression test.

`ps2_drivers` PR #26 merged into its `njemu` branch on October 26, 2026
(merge commit `d891c7bdd6b1f7e3474390996f990511c7fd8a1a`).
NJEMU's PS2 CI and release workflow now both consume that shared branch
without the temporary review-commit pin. Both workflows validate the HDD
launch helper and require the installed generic HDD bootstrap before
building or packaging NJEMU. The workflow log prints the resolved driver
commit so that each CI build can be reproduced.

The merge includes an additional, independently selected ATA-backed BDM
FAT/exFAT HDD mode. After the merge, the external IRX image is 569,728
bytes (12,288 more than the earlier tested revision); the USB and APA/PFS
bootstraps retain their original 300,116-byte and 322,516-byte sizes and
identical SHA-256 digests. Rebuilding against the merged driver tree passed
the four driver checks, external MVS and CPS2 builds, and embedded CPS1/NCDZ
builds. Their canonical four-core PS2 ZIP also packaged successfully with
both bootstraps.

### Post-merge PCSX2 runtime verification

After rebuilding against the merged `ps2_drivers/njemu` source tree, both
external-core configurations were also exercised in PCSX2 2.9.142 using a
copy-on-write clone of the isolated 8 GiB APA/PFS test image and a copied
PCSX2 profile. Neither the normal PCSX2 configuration nor the original
isolated test image was modified:

- **MVS / Windjammers** (`GUI=OFF`): launched `BOOT_HDD.ELF` from
  `pfs:/NJEMU/MVS`, loaded the MVS core without an extra IOP reset,
  preserved `hdd0:__common`, loaded all Windjammers ROM components,
  initialized audio and controller input, and ran without a reported
  crash or HDD staging error for approximately 25 seconds of PCSX2 play.
- **CPS2 / Alien vs. Predator** (`GUI=OFF`): launched `BOOT_HDD.ELF`
  from `pfs:/NJEMU/CPS2`, preserved the same APA partition, loaded the
  ROM and 16 MiB graphics cache, completed 100% decryption, initialized
  audio/controller input, and ran without a reported crash or HDD staging
  error for approximately 29 seconds of PCSX2 play.

Logs: `build_issue23_hdd_probe/generic_merged_validation/`
(`mvs_merged_stdout.log` and `cps2_merged_stdout.log`). These results
validate the merged driver revision on PCSX2; physical PS2 internal HDD,
USB/MX4SIO runtime boot, and quantitative FPS regression testing still
require separate validation.

Further verification remains valuable on **physical PS2 internal HDD**
hardware, especially alternate launchers and APA partitions, and for
quantitative performance or frame-rate comparison.

## Troubleshooting

If the HDD launcher reports missing partition information, the program
that launched it did not provide the original `hdd0:...:pfs:/...` path.
Use a launcher that preserves that argument. If NJEMU starts but cannot
open its runtime files, confirm that the release package's `lang/`,
`font/`, metadata, `roms/` and `cache/` files/directories are beside
the executable, not placed underneath `resources/` on the PS2.

For reproducibility, local PCSX2 diagnostic files are generated under
ignored `build_issue23_hdd_probe/`; they are not part of release packaging.
