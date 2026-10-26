# PS2 internal HDD boot (issue #23)

## Scope and launch selection

The canonical PS2 CPS2/MVS release uses an extensionless core executable
(`CPS2` or `MVS`) and the external `ps2_drivers.irximg`. It now packages
two entry points:

| Launcher | Storage | Driver handoff |
| --- | --- | --- |
| `BOOT.ELF` | USB / MX4SIO | Installed PS2 drivers image bootstrap (unchanged) |
| `BOOT_HDD.ELF` | Internal APA/PFS HDD | NJEMU PFS-aware bootstrap |

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

The HDD-specific bootstrap captures the original partition from the
launcher's argv, restores PFS, loads the external core without another IOP
reset, and passes that APA partition path as the engine's launch argument.
The engine captures that argument before its own IOP reset and remounts PFS.
Both MVS and CPS2 can then load runtime files and initialize GUI, audio,
video and pad controls.

The external image's generic HDD requirements include two BDM entries
(BDM and BDMFS_FATFS) which APA/PFS does not consume. On the HDD path only,
NJEMU explicitly releases this known unused pair after initializing the pad
driver. The existing strict staging check remains intact for all other
boot devices.

## Test evidence

Validated on PCSX2 2.9.142, macOS, using a separate sparse **8 GiB**
APA/PFS HDD image with an `__common` partition. The owner's previously
configured **40 GiB** `DEV9hdd.raw` image was not used or modified.

- Original `BOOT.ELF` from PFS: reproduces failed removable-storage
  fallback and returns to the PS2 browser.
- Embedded MVS: mounts HDD when APA partition metadata is preserved, then
  reaches the GUI when its runtime `lang/en.lng` is present.
- External-image MVS via `BOOT_HDD.ELF`: reaches the NJEMU GUI splash;
  video/audio/pad initialization succeeds without staged-driver errors.
- External-image CPS2 via `BOOT_HDD.ELF`: reaches the CPS2 GUI splash;
  video/audio/pad initialization succeeds without staged-driver errors.
- External-image MVS with `GUI=OFF`: with `neogeo.zip`,
  `wjammers.zip`, `rominfo.mvs` and `game_name.ini` on the test PFS
  partition, Windjammers loaded all 065 program/sound/sprite ROM members,
  initialized audsrv audio, and advanced into the running attract-mode
  game. A PCSX2 screenshot showed the playfield at **60 FPS**.
- Neither test used the USB/MX4SIO FAST CACHE driver on the HDD path.

Further verification remains valuable on **physical PS2 internal HDD**
hardware, especially alternate launchers and APA partitions. The MVS
Windjammers test establishes full runtime I/O and game execution; cached
MVS/CPS2 gameplay and physical-console validation remain outstanding.

## Troubleshooting

If the HDD launcher reports missing partition information, the program
that launched it did not provide the original `hdd0:...:pfs:/...` path.
Use a launcher that preserves that argument. If NJEMU starts but cannot
open its runtime files, confirm that the release package's `lang/`,
`font/`, metadata, `roms/` and `cache/` files/directories are beside
the executable, not placed underneath `resources/` on the PS2.

For reproducibility, local PCSX2 diagnostic files are generated under
ignored `build_issue23_hdd_probe/`; they are not part of release packaging.
