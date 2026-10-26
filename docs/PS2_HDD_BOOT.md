# PS2 internal HDD boot (issue #23)

## Scope and launch selection

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
