# Changelog

NJEMU follows Semantic Versioning for releases from the 2.4.0 line onward. Historical notes below were migrated from the previous README; the first strict SemVer Git tag has not been retroactively created for older history.

## Unreleased

- Restructured project/user/developer documentation and introduced structured contribution templates.
- Added Git-derived Semantic Versioning/build identity with exact source revision reporting.
- Added canonical release configurations, packaging validation, and automated release publishing.
- Expanded GitHub Pages into the NJEMU landing/download site while preserving the browser ROM converter.

## 2.4.0 baseline (Cross-Platform Port)
- Refactored the codebase around platform driver contracts and common emulator policy
- Ported **CPS1, CPS2, MVS and NCDZ** to PS2 and Desktop/SDL2
- Unified each target's sprite renderer across PSP, PS2 and Desktop
- Ported the common menu/GUI frontend to PS2 and Desktop
- Introduced CMake builds for all supported hosts while maintaining PSP compatibility

### Version 2.3.x (Development Version)

> **Note:** Version 2.3.x was a development version containing experimental code.

**Differences from 2.2.x:**

- **AdHoc Support:** Built-in support for AdHoc multiplayer (except NCDZPSP)
- **SystemButtons.prx (historical):** 2.3.x used an extended SystemButtons.prx
  (based on homehook.prx), even for 1.5 Kernel builds. Current builds no longer
  ship or load this module.
  - On CFW 3.52+, supports volume display when pressing VOL +/- buttons
- **Sound Emulation:** Different sound emulation processing
- **Video Emulation:** Different video emulation processing for MVS and NCDZ
- **Memory Usage:** Due to added features, free memory is reduced - more games require cache files, and some games may not boot
- **VBLANK Sync:** Screen update interval matches PSP's refresh rate for easier VBLANK synchronization
  - Note: MVS runs slightly faster than real hardware (barely noticeable)

### Version 2.3.5

**General:**
- ROM set updated to MAME 0.152
- Font uses simhei (CHARSET: GBK)
- Japanese command list must use GBK charset
- Fixed PNG format bug
- Changed help button to SELECT
- Changed BIOS menu to R trigger
- Multi-language support
- Added command hotkey
- Game list expanded to 512
- Cheat support
- Added hack/bootleg ROM sets

**CPS1PSP:**
- Fixed DIP switch
- Fixed Mercs player 3 support
- Added hack ROMs button 3
- Fixed Warriors of Fate (bootleg)
- Fixed Huo Feng Huang (Chinese bootleg of Sangokushi II) sound

**MVSPSP:**
- Fixed DIP menu
- Fixed Jockey Grand Prix
- Fixed King of Gladiator (KOF'97 bootleg)
- Support 128MB CROM cache
- Support UniBIOS 1.0-3.0 and NeoGit BIOS
- Support M1 decrypt
- Fixed 000-lo.lo length

**NCDZPSP:**
- Fixed 000-lo.lo length (fix sleep mode)

---
