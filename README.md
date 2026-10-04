# NJEMU - Multi-Platform Arcade Emulator

[![License: GPL v3](https://img.shields.io/badge/License-GPLv3-blue.svg)](https://www.gnu.org/licenses/gpl-3.0)

## Table of Contents

- [Overview](#overview)
  - [Supported Arcade Systems](#supported-arcade-systems)
  - [Supported Platforms](#supported-platforms)
- [How to Use](#how-to-use)
  - [Menu Controls](#menu-controls)
  - [In-Game Controls](#in-game-controls)
  - [Directory Structure](#directory-structure)
- [Features](#features)
- [Building](#building)
  - [Build Commands](#build-commands)
  - [Build Options](#build-options)
- [Platform-Specific Build Instructions](#platform-specific-build-instructions)
  - [PSP (PlayStation Portable)](#psp-playstation-portable)
  - [PS2 (PlayStation 2)](#ps2-playstation-2)
  - [Desktop (PC/SDL2)](#desktop-pcsdl2)
  - [PS Vita](#ps-vita)
- [ROM Compatibility](#rom-compatibility)
  - [MVS-Specific ROM Notes](#mvs-specific-rom-notes)
  - [CPS2-Specific Cache Notes](#cps2-specific-cache-notes)
  - [NCDZ-Specific Setup](#ncdz-specific-setup)
- [ROM Conversion Tool (romcnv)](#rom-conversion-tool-romcnv)
- [Memory Requirements](#memory-requirements)
  - [PSP Runtime Memory Policy](#psp-runtime-memory-policy)
- [Project Structure](#project-structure)
- [Technical Architecture](#technical-architecture---emulator-targets)
  - [MVS (Neo-Geo) Target](#mvs-neo-geo-target)
  - [CPS1 Target](#cps1-capcom-play-system-1-target)
  - [CPS2 Target](#cps2-capcom-play-system-2-target)
  - [NCDZ (Neo-Geo CD) Target](#ncdz-neo-geo-cd-target)
  - [Porting Guide](#porting-guide)
- [Internal Systems Documentation](#internal-systems-documentation)
  - [Sound System](#sound-system-architecture)
  - [State Save/Load](#state-saveload-system)
  - [Cache System](#cache-system-details)
  - [Input System](#input-system)
  - [ROM Loading](#rom-loading-system)
- [Changelog](#changelog)
- [Credits](#credits)
- [License](#license)
- [Contributing](#contributing)

---

## Overview

**NJEMU** is an open-source arcade emulator for classic Capcom and SNK hardware. It originated as a **PSP (PlayStation Portable)** project and now supports PSP, **PlayStation 2**, **PS Vita**, and **Desktop/SDL2** from the same C codebase.

**Current Version:** 2.4.0  
**Based on:** NJEmu 2.3.5

### Multi-Platform Status

The PSP-first codebase has completed its platform-driver refactor. All four emulator cores and the shared GUI/menu frontend now run through common contracts on all four supported hosts:

- **PSP** - Original platform with native GU/audio/input backends
- **PS2** - Native gsKit/PS2SDK backend for all four cores and the common GUI
- **DESKTOP** - SDL2 backend for all four cores and the common GUI, also used for tests and debugging
- **PS VITA** - Native Vita backend with both GXM/vita2d and vitaGL available in every build

MVS, CPS1, CPS2, and NCDZ each have a single platform-neutral `sprite.c`. Target code owns emulation and rendering semantics; the selected host backend owns native texture layout, GPU submission, audio, physical input, threading, timing, lifecycle, and optional power capabilities.

### Architecture

Platform selection is a build/link-time concern rather than a set of host `#ifdef`s spread through common code. Each backend binds the shared driver contracts from its `<platform>_drivers.c`, while common and target code remain independent of PSP/PS2/SDL SDK types.

The current architecture follows these rules:

- `src/common/` contains no PSP/PS2/Desktop conditionals and no native platform SDK includes
- Target sprite renderers emit compact portable texture updates and vertex/point batches through `video_driver_t`
- PSP consumes the common sprite vertex layout directly; PS2 converts it once into its final gsKit queue location; Desktop consumes it through SDL
- Input backends report stable physical state; player routing, menu combinations, autofire, and target-specific interpretation stay in common/target code
- Power, frame readback, UI texture storage, and similar differences are expressed as capabilities instead of PSP-shaped assumptions
- Platform SDK headers and private backend state stay inside `src/<platform>/`

### Current Porting Status

| Emulator | PSP | PS2 | PC | PS Vita |
|----------|-----|-----|-----|---------|
| **MVS** | ✅ Full | ✅ Full | ✅ Full | ✅ Full |
| **CPS1** | ✅ Full | ✅ Full | ✅ Full | ✅ Full |
| **CPS2** | ✅ Full | ✅ Full | ✅ Full | ✅ Full |
| **NCDZ** | ✅ Full | ✅ Full | ✅ Full | ✅ Full |

> **Note:** "Full" here means the emulator core and common GUI/menu frontend are available on the platform. Platform-specific features such as PSP Ad Hoc remain capability-dependent.

📋 See [PORTING_PLAN.md](PORTING_PLAN.md) for current platform status and follow-up work, and [docs/PLATFORM_PORTING_GUIDE.md](docs/PLATFORM_PORTING_GUIDE.md) for the backend extension contract.

---

## Supported Arcade Systems

| Target | Name | Description | Setup Guide |
|--------|------|-------------|-------------|
| **CPS1** | CPS1PSP | Capcom Play System 1 Emulator | [README](resources/cps1/README.md) |
| **CPS2** | CPS2PSP | Capcom Play System 2 Emulator | [README](resources/cps2/README.md) |
| **MVS** | MVSPSP | Neo-Geo MVS/AES Emulator | [README](resources/mvs/README.md) |
| **NCDZ** | NCDZPSP | Neo-Geo CD Emulator | [README](resources/ncdz/README.md) |

The authoritative external-file contract is
[docs/RUNTIME_FILES_AUDIT.md](docs/RUNTIME_FILES_AUDIT.md). The per-target
resource READMEs remain useful background material, but runtime requirements
and canonical lookup paths should be checked against that audit.

## Supported Platforms

| Platform | Description | Status |
|----------|-------------|--------|
| **PSP** | Sony PlayStation Portable | ✅ Original platform |
| **PS2** | Sony PlayStation 2 | ✅ Full |
| **DESKTOP** | PC/Desktop (SDL2) | ✅ Full |
| **PS VITA** | Sony PlayStation Vita | ✅ Full |

### PSP Runtime and Packaging

The maintained PSP build uses the current CMake/PSPSDK PRX + `EBOOT.PBP` packaging path. By default NJEMU runs as a user-mode module and explicitly requests the largest PSP user-memory partition; runtime memory sizing is then determined by the common allocator policy.

`KERNEL_MODE=ON` remains available for the PSP-specific code paths that require a kernel module, but it is not a separate legacy 1.50 packaging system. Historical firmware-specific build layouts from the original PSP-only project are no longer the maintained build path.

---

## How to Use

### Menu Controls

| Button | Action |
|--------|--------|
| O (Circle) | OK / Confirm |
| X (Cross) | Cancel |
| SELECT | Help (press in any menu except game screen) |
| SELECT + START | Emulator menu (during gameplay) |
| R Trigger | BIOS menu (MVS file browser) |

> **Menu shortcut:** press START+SELECT during gameplay to open the emulator menu on every platform.

### In-Game Controls

Button layouts automatically flip/rotate when:
- DIP Switch "Cabinet" is set to Cocktail (2P mode)
- DIP Switch "Flip Screen" is enabled
- Vertical games with "Rotate Screen" set to Yes

#### Common Controls (All Emulators)

| Input | Button |
|-------|--------|
| Up | D-Pad Up / Analog Up |
| Down | D-Pad Down / Analog Down |
| Left | D-Pad Left / Analog Left |
| Right | D-Pad Right / Analog Right |
| Start | Start |
| Coin | Select |

#### CPS1PSP Button Layouts

**2-Button Games:**
| Button | PSP |
|--------|-----|
| Button 1 | Square |
| Button 2 | Triangle |

**3-Button Games:**
| Button | PSP |
|--------|-----|
| Button 1 | Square |
| Button 2 | Triangle |
| Button 3 | Cross |

**Street Fighter II Series (6-Button):**
| Button | PSP |
|--------|-----|
| Light Punch | Square |
| Medium Punch | Triangle |
| Heavy Punch | L Trigger |
| Light Kick | Cross |
| Medium Kick | Circle |
| Heavy Kick | R Trigger |

**Quiz Games (No directional controls):**
| Button | PSP |
|--------|-----|
| Button 1 | Square |
| Button 2 | Triangle |
| Button 3 | Cross |
| Button 4 | Circle |
| Switch Player | L Trigger |

**Forgotten Worlds / Lost Worlds:**
| Action | PSP |
|--------|-----|
| Fire | Square |
| Rotate Left | L Trigger |
| Rotate Right | R Trigger |

#### CPS2PSP Button Layouts

**2-Button Games:**
| Button | PSP |
|--------|-----|
| Button 1 | Square |
| Button 2 | Triangle |

**3-Button Games:**
| Button | PSP |
|--------|-----|
| Button 1 | Square |
| Button 2 | Triangle |
| Button 3 | Cross |

**6-Button Games (Fighting Games):**
| Button | PSP |
|--------|-----|
| Light Punch | Square |
| Medium Punch | Triangle |
| Heavy Punch | L Trigger |
| Light Kick | Cross |
| Medium Kick | Circle |
| Heavy Kick | R Trigger |

**Quiz Nanairo Dreams (No directional controls):**
| Button | PSP |
|--------|-----|
| Button 1 | Square |
| Button 2 | Cross |
| Button 3 | Triangle |
| Button 4 | Circle |
| Switch Player | L Trigger |

#### MVSPSP / NCDZPSP Button Layout

| Button | PSP |
|--------|-----|
| A | Cross |
| B | Circle |
| C | Square |
| D | Triangle |

> **Note:** NCDZPSP uses the same layout as NeoGeo CD / AES controllers.

#### Special Controls

| Action | Buttons |
|--------|---------|
| Open Menu | START + SELECT |
| PSP system exit dialog | HOME |
| Service Switch | L + R + SELECT |
| 1P & 2P Start | L + R + START |

NJEMU does not intercept PSP system buttons. HOME is left to the firmware and
uses the standard PSP exit callback; VOL +/- are likewise left entirely to the
firmware so the normal system volume behavior/overlay can be used.

#### AdHoc Mode

- Press **Square** in the file browser to start a game in AdHoc mode
- Press **START + SELECT** during AdHoc play to pause and show the disconnect dialog

### Directory Structure

The examples below show the runtime layout. Generated translation packs and the
external GBK font are installed by CMake. ROMs, BIOS files, converter output,
configuration, saves, and NVRAM are user/runtime data and are never part of the
NJEMU release manifest.

For the complete per-core/per-platform classification and lookup order, see
[docs/RUNTIME_FILES_AUDIT.md](docs/RUNTIME_FILES_AUDIT.md).

#### CPS1PSP / CPS2PSP

```
/PSP/GAME/CPS1PSP/              (or CPS2PSP/)
├── EBOOT.PBP                   # Main executable
├── lang/                       # Generated UI translation packs
├── font/gbk_s14.bin            # Generated external UI font
├── njemu.ini                    # Settings (auto-created)
├── rominfo.cps1                # CPS1 ROM topology database
├── game_metadata.cps1          # CPS1 names/metadata
├── game_database.cps2          # CPS2 unified topology/names/policy database
├── dip_metadata.cps1           # CPS1 DIP menu metadata (CPS1 only)
├── command.dat                 # MAME Plus! command list (optional)
├── roms/                       # ROM files (ZIP format)
├── cache/                      # CPS2 streaming cache, only when needed
├── config/                     # Per-game settings
├── nvram/                      # EEPROM saves
└── state/                      # Save states
```

CPS1 does not use `cache/`. CPS2 first tries to keep decoded graphics fully
resident; cache files are only required when a cache-enabled build falls back
to streaming.

#### MVSPSP

```
/PSP/GAME/MVSPSP/
├── EBOOT.PBP                   # Main executable
├── lang/                       # Generated UI translation packs
├── font/gbk_s14.bin            # Generated external UI font
├── njemu.ini                  # Settings (auto-created)
├── rominfo.mvs                 # ROM database (REQUIRED)
├── game_metadata.mvs           # Generated names/game metadata (REQUIRED)
├── dip_metadata.mvs            # Generated DIP menu metadata
├── command.dat                 # MAME Plus! command list (optional)
├── roms/                       # ROM files (ZIP format)
│   └── neogeo.zip              # BIOS file (REQUIRED)
├── processed/                  # Canonical romcnv_mvs processed assets
├── config/                     # Per-game settings
├── memcard/                    # Memory card saves
├── nvram/                      # SRAM saves
└── state/                      # Save states
```

Cache-enabled builds can still read historical MVS processed assets from
`cache/` as a migration fallback, but new converter output belongs under
`processed/`.

#### NCDZPSP

```
/PSP/GAME/NCDZPSP/
├── EBOOT.PBP                   # Main executable
├── lang/                       # Generated UI translation packs
├── font/gbk_s14.bin            # Generated external UI font
├── njemu.ini                    # Settings (auto-created)
├── game_metadata.ncdz          # Generated NGH/game metadata (REQUIRED)
├── neocd.bin                   # Neo Geo CD BIOS (user supplied, REQUIRED)
├── 000-lo.lo                   # Neo Geo low ROM (user supplied, REQUIRED)
├── command.dat                 # MAME Plus! command list (optional)
├── roms/                       # Extracted game folders or ZIP archives
│   └── [Game Name]/            # Must contain IPL.TXT
│       ├── *.PRG, *.SPR, etc.  # Neo Geo CD game files
│       └── mp3/                # MP3 audio tracks
├── data/loading.png            # Optional loading image
├── config/                     # Per-game settings
├── backup.bin                  # Backup RAM (auto-created)
└── state/                      # Save states
```

NJEMU currently opens NCDZ games as extracted directories or ZIP archives; it
does not mount ISO/BIN/CUE disc images directly. PSP screenshots are written to
`ms0:/PICTURE/<CORE>/`, outside the application directory.

---

## Features

- High-quality arcade emulation
- ROM caching system for improved performance
- Save state support
- Cheat support with extensive cheat databases
- Multiple language support (platform system language on PSP/PS2; English fallback on Desktop)
- DIP switch configuration (CPS1, MVS)
- BIOS menu with UniBIOS 1.0-3.0 support (MVS)
- Ad Hoc multiplayer (PSP, except NCDZPSP)
- Command list display (MAME Plus! format)

### Language Support

The UI language is selected through the platform driver. PSP and PS2 map their system language to Japanese, Spanish, Simplified Chinese, Traditional Chinese, or English; Desktop currently uses English. If a requested catalog is unavailable, NJEMU falls back to English.

Game display names are generated into `game_metadata.<core>` from the tracked
UTF-8 sources under `metadata/`. Localized names fall back to English when a
core-specific translation is absent.

CPS1 and MVS DIP-menu labels/schema are likewise generated from the tracked
UTF-8 `metadata/*_dips.json` sources into `dip_metadata.<core>`. The emulator
loads only the selected DIP profile while that menu is open; DIP bit behavior
remains core code.

The build generates `lang/*.lng` and `font/gbk_s14.bin` from the tracked
translation/font sources. Both GUI and no-GUI binaries initialize the common UI
text/draw services, so the English catalog and GBK font are runtime assets in
both configurations.

---

## Building

### Prerequisites

- CMake 3.12 or higher
- Platform-specific toolchain:
  - **PSP**: PSPSDK
  - **PS2**: PS2DEV toolchain
  - **PC**: GCC/Clang with SDL2

### Build Commands

Building requires specifying both `TARGET` and `PLATFORM`:

```bash
# Build MVS for PSP
cmake -DTARGET=MVS -DPLATFORM=PSP -B build_psp
cmake --build build_psp

# Build MVS for PS2
cmake -DTARGET=MVS -DPLATFORM=PS2 -B build_ps2
cmake --build build_ps2

# Build MVS for PC
cmake -DTARGET=MVS -DPLATFORM=DESKTOP -B build_pc
cmake --build build_pc

# Build CPS1 for PSP
cmake -DTARGET=CPS1 -DPLATFORM=PSP -B build_psp_cps1
cmake --build build_psp_cps1
```

### Build Options

| Option | Description | Default |
|--------|-------------|---------|
| `KERNEL_MODE` | Enable kernel mode (PSP) | OFF |
| `COMMAND_LIST` | Enable command list display | OFF |
| `ADHOC` | Enable Ad Hoc multiplayer | OFF |
| `GUI` | Enable GUI menu system | OFF |
| `SAVE_STATE` | Enable save state support | OFF |
| `RELEASE` | Release build | OFF |

### Build Directory Convention

Build directories follow the naming pattern: `build_{platform}_{target}`

Examples:
- `build_psp_cps1` - PSP build for CPS1
- `build_psp_mvs` - PSP build for MVS
- `build_ps2_mvs` - PS2 build for MVS
- `build_desktop_ncdz` - Desktop build for NCDZ

### Resource Folders

Each target has a corresponding resource folder under `resources/` containing files required for the emulator:

| Target | Resource Folder | Setup Guide |
|--------|-----------------|-------------|
| CPS1 | `resources/cps1/` | [README](resources/cps1/README.md) |
| CPS2 | `resources/cps2/` | [README](resources/cps2/README.md) |
| MVS | `resources/mvs/` | [README](resources/mvs/README.md) |
| NCDZ | `resources/ncdz/` | [README](resources/ncdz/README.md) |

For local development, CMake stages the target resource tree into the build
root, normally using symlinks for read-only entries and private copies for
mutable entries. This deliberately allows local ROM/BIOS/cache data under
`resources/<target>/` to be used for validation.

Release/install output is different: `cmake --install` and Vita VPK packaging
use an explicit distribution manifest, so local ROMs, BIOS files, converter
output, caches, saves, NVRAM, and configuration are not copied into release
artifacts merely because they exist under `resources/`.

---

## Platform-Specific Build Instructions

### PSP (PlayStation Portable)

#### Prerequisites

- [PSPDEV Toolchain](https://github.com/pspdev/pspdev) installed

#### Environment Setup

Set up the required environment variables:

```bash
export PSPDEV=/path/to/pspdev
export PATH=$PSPDEV/bin:$PATH
```

> **Note:** Replace `/path/to/pspdev` with your actual PSPDEV installation path (e.g., `/usr/local/pspdev` or `$HOME/pspdev`).

#### Building

1. Create the build directory and navigate to it:

```bash
mkdir build_psp_{target}
cd build_psp_{target}
```

2. Run CMake with the PSP toolchain:

```bash
cmake -DPLATFORM="PSP" \
      -DCMAKE_TOOLCHAIN_FILE=$PSPDEV/psp/share/pspdev.cmake \
      -DCMAKE_BUILD_TYPE=RelWithDebInfo \
      -DTARGET={TARGET} \
      ..
```

Replace `{TARGET}` with one of: `CPS1`, `CPS2`, `MVS`, or `NCDZ`.

3. Build the project:

```bash
cmake --build . --parallel
```

#### Example: Building CPS1 for PSP

```bash
export PSPDEV=/path/to/pspdev
export PATH=$PSPDEV/bin:$PATH

mkdir build_psp_cps1
cd build_psp_cps1
cmake -DPLATFORM="PSP" \
      -DCMAKE_TOOLCHAIN_FILE=$PSPDEV/psp/share/pspdev.cmake \
      -DCMAKE_BUILD_TYPE=RelWithDebInfo \
      -DTARGET=CPS1 \
      ..
cmake --build . --parallel
```

#### Output

After a successful build, you'll find the following files in the build directory:
- `EBOOT.PBP` - The main executable for PSP
  - The PBP embeds the target-specific XMB icon from `data/{target}.png`.
  - Its XMB title includes the target and NJEMU version (for example, `MVS 2.4 for PSP`).
- The build root stages an explicit per-target runtime set. Known read-only
  entries are **linked** back to `resources/{target}/` by default, while
  writable entries such as `config/`, `nvram/`, `memcard/`, `state/`,
  and `game_name.ini` are private build copies. `-DCOPY_RESOURCES=ON` copies
  that same explicit set instead of linking it.

#### Configuring the Game (without GUI)

For builds without GUI (default), the emulator reads the game to boot from the `game_name.ini` file in the build directory. Edit this file and set it to the ROM name (without extension):

```bash
echo "sf2" > game_name.ini
```

#### Running on PSP

**On Real Hardware:**

1. Copy the entire build directory contents to your PSP's `PSP/GAME/` folder
2. Rename the folder appropriately (e.g., `CPS1PSP`)
3. Launch from the PSP XMB menu

**On PPSSPP Emulator:**

From the build directory, run:

```bash
/Applications/PPSSPPSDL.app/Contents/MacOS/PPSSPPSDL $(pwd)/EBOOT.PBP
```

#### Debugging

For debugging on PSP, use `pspsh` and `psplink`:

```bash
# Start pspsh to connect to PSP running psplink
pspsh

# Load and run the ELF file
host0:/> ./EBOOT.ELF
```

---

### PS2 (PlayStation 2)

#### Prerequisites

- [PS2DEV Toolchain](https://github.com/ps2dev/ps2dev) installed

#### Environment Setup

Set up the required environment variables:

```bash
export PS2DEV=/path/to/ps2dev
export PS2SDK=$PS2DEV/ps2sdk
export PATH=$PATH:$PS2DEV/bin:$PS2DEV/ee/bin:$PS2DEV/iop/bin:$PS2DEV/dvp/bin:$PS2SDK/bin
```

> **Note:** Replace `/path/to/ps2dev` with your actual PS2DEV installation path (e.g., `/usr/local/ps2dev` or `$HOME/ps2dev`).

#### Building

1. Create the build directory and navigate to it:

```bash
mkdir build_ps2_{target}
cd build_ps2_{target}
```

2. Run CMake with the PS2 toolchain:

```bash
cmake -DCMAKE_TOOLCHAIN_FILE=${PS2DEV}/share/ps2dev.cmake \
      -DCMAKE_BUILD_TYPE=RelWithDebInfo \
      -DTARGET={TARGET} \
      -DPLATFORM=PS2 \
      ..
```

Replace `{TARGET}` with one of: `MVS`, `NCDZ`, `CPS1`, or `CPS2`.

3. Build the project:

```bash
cmake --build . --parallel
```

#### Example: Building MVS for PS2

```bash
export PS2DEV=/path/to/ps2dev
export PS2SDK=$PS2DEV/ps2sdk
export PATH=$PATH:$PS2DEV/bin:$PS2DEV/ee/bin:$PS2DEV/iop/bin:$PS2DEV/dvp/bin:$PS2SDK/bin

mkdir build_ps2_mvs
cd build_ps2_mvs
cmake -DCMAKE_TOOLCHAIN_FILE=${PS2DEV}/share/ps2dev.cmake \
      -DCMAKE_BUILD_TYPE=RelWithDebInfo \
      -DTARGET=MVS \
      -DPLATFORM=PS2 \
      ..
cmake --build . --parallel
```

#### Output

After a successful build, you'll find the following in the build directory:
- `{TARGET}` - The main executable for PS2
- Resource entries are staged directly in the build root from an explicit
  per-target set. Read-only entries such as applicable `roms/`, `data/`,
  `cache/`, `processed/`, and `rominfo.*` are **linked** from
  `resources/{target}/`; `game_metadata.<core>` is generated in the build root;
  writable entries such as `config/`, `nvram/`,
  `memcard/`, `state/`, and `game_name.ini` are private build copies. This
  matches the runtime `launchDir` layout and PCSX2's `host:` root without
  letting runtime writes modify `resources/`. `-DCOPY_RESOURCES=ON` copies
  that same explicit set instead of linking it.

Use `cmake --install .` for a distributable tree. The install step uses the
explicit runtime manifest and intentionally excludes local ROMs, BIOS files,
processed assets, caches, saves, NVRAM, and configuration.

With `-DPS2_EXTERNAL_IRX_IMAGE=ON`, the install tree instead contains
`BOOT.ELF`, the real `{TARGET}` engine executable, `elf_path.ini`, and
`ps2_drivers.irximg`. The default embedded-driver build does not require those
external support files.

#### Configuring the Game (without GUI)

For builds without GUI (default), the emulator reads the game to boot from the `game_name.ini` file in the build directory. Edit this file and set it to the ROM name (without extension):

```bash
echo "mslug" > game_name.ini
```

#### Running on PS2

**On Real Hardware:**

1. Copy the ELF file and resource files to your PS2 (via USB, network, or memory card)
2. Launch using a homebrew loader (e.g., uLaunchELF, OPL, or ps2link)

**On PCSX2 Emulator:**

From the build directory, run:

```bash
/Applications/PCSX2.app/Contents/MacOS/PCSX2 -elf $(pwd)/{TARGET}
```

Replace `{TARGET}` with the target name (e.g., `MVS`, `NCDZ`, `CPS1`, `CPS2`). PCSX2 exposes the executable's directory as the PS2 `host:` root, so the resource links/copies must remain beside the executable.

#### Debugging

For debugging on PS2, use `ps2client` with ps2link:

```bash
# Send and run the ELF file on PS2 running ps2link
ps2client -h <PS2_IP_ADDRESS> execee host:{TARGET}.elf
```

---

### Desktop (PC/SDL2)

The Desktop build is primarily intended for development and debugging purposes.

#### Prerequisites

- CMake 3.12 or higher
- GCC or Clang compiler
- SDL2 development libraries

On macOS (using Homebrew):

```bash
brew install sdl2
```

On Ubuntu/Debian:

```bash
sudo apt install libsdl2-dev
```

#### Building

1. Create the build directory and navigate to it:

```bash
mkdir build_desktop_{target}
cd build_desktop_{target}
```

2. Run CMake:

```bash
cmake -DPLATFORM="Desktop" \
      -DCMAKE_BUILD_TYPE=RelWithDebInfo \
      -DTARGET={TARGET} \
      ..
```

Replace `{TARGET}` with one of: `CPS1`, `CPS2`, `MVS`, or `NCDZ`.

3. Build the project:

```bash
cmake --build . --parallel
```

#### Example: Building MVS for Desktop

```bash
mkdir build_desktop_mvs
cd build_desktop_mvs
cmake -DPLATFORM="Desktop" \
      -DCMAKE_BUILD_TYPE=RelWithDebInfo \
      -DTARGET=MVS \
      ..
cmake --build . --parallel
```

#### Output

After a successful build, you'll find the following files in the build directory:
- `{TARGET}` - The main executable
- The build root stages an explicit per-target runtime set. Known read-only
  entries are linked back to `resources/{target}/`; writable entries such as
  `config/`, `nvram/`, `memcard/`, `state/`, and `game_name.ini` are
  private build copies. `-DCOPY_RESOURCES=ON` copies that same explicit set
  instead of linking it.

Use `cmake --install .` when you want a distributable tree without local
ROM/BIOS/cache/save data from the development resource directory.

#### Configuring the Game (without GUI)

For builds without GUI (default), the emulator reads the game to boot from the `game_name.ini` file in the build directory. Edit this file and set it to the ROM name (without extension):

```bash
echo "mslug" > game_name.ini
```

#### Running

From the build directory, simply run the executable:

```bash
./{TARGET}
```

For example:

```bash
./MVS
```

#### Debugging

Use your preferred debugger (GDB, LLDB) for debugging:

```bash
# Using GDB
gdb ./{TARGET}

# Using LLDB (macOS)
lldb ./{TARGET}
```

---

### PS Vita

#### Prerequisites

- [VitaSDK](https://vitasdk.org/) with `VITASDK` configured
- vita2d
- vitaGL and vitashark
- miniz built for Vita

#### Building

    mkdir build_vita_{target}
    cd build_vita_{target}
    cmake -DPLATFORM="PSVITA" -DCMAKE_TOOLCHAIN_FILE=${VITASDK}/share/vita.toolchain.cmake -DTARGET={TARGET} ..
    cmake --build . --parallel
    cmake --install .

Every Vita build contains both graphics backends. Runtime selection is
`Auto / GXM / VitaGL`; there is no `USE_VITAGL` build option.

The generated `{TARGET}.vpk` contains only NJEMU-distributed metadata,
generated translations/font data, and the empty runtime layout. On first use,
packaged files are copied from `app0:` to the writable runtime root
`ux0:data/<target>/`. User ROMs, BIOS files, processed assets, caches, saves,
and configuration belong in that writable data tree and are never packaged
from local `resources/` contents.

---

## ROM Compatibility

- ROMs must be in **MAME 0.152** compatible format
- Place ROMs in ZIP format in the `roms/` directory
- Clone sets require the parent ROM in the same directory
- Some games require ROM conversion using the `romcnv` tools
- Games shown in **white** in the file browser are fully supported
- Games shown in **gray** cannot run (usually due to memory limitations)

### ROM Verification

If a game doesn't work, verify your ROM set using tools like:
- **ClrMame Pro**
- **RomCenter**

ROM file names inside the ZIP can be anything, but **CRC values must match** MAME 0.152.

### Supported Games

| System | Supported Games | Notes |
|--------|-----------------|-------|
| **CPS1** | 137 sets | Street Fighter II, Final Fight, Ghouls'n Ghosts, etc. |
| **CPS2** | 286 sets | Including Phoenix Edition decrypted sets |
| **MVS** | 305 sets | Including bootlegs and homebrew |
| **NCDZ** | All official releases | All officially released Neo-Geo CD games |

Supported-game lists are generated from the canonical `metadata/<core>.tsv`
sources and included in cartridge-core build/install packages as:

- `gamelist_cps1.txt`
- `gamelist_cps2.txt`
- `gamelist_mvs.txt`

### MVS-Specific ROM Notes

#### BIOS Setup

Place the BIOS file as `neogeo.zip` in the `roms/` folder. Supported BIOS options include:
- Standard MVS/AES BIOS
- UniBIOS 1.0 - 3.0
- NeoGit BIOS

> **Important:** Configure BIOS settings in the file browser (press START) **before** launching a game.

#### Region/Machine Mode

You can change Region and Machine Mode in the game settings menu, but:
- Some later games have protection that prevents this from working
- Running MVS games with AES BIOS may trigger protection
- For reliable region changes, use **UniBIOS**

#### Memory Card

- Memory card files are created per-game
- Memory card is always recognized (no need to insert)

#### Unsupported Games

The following games work in MAME but cannot run due to memory constraints:

| ROM Set | Game | Reason |
|---------|------|--------|
| svcpcb | SvC Chaos - SNK vs Capcom (JAMMA PCB) | Insufficient memory |

#### Clone Sets with Different Parent Relationships

These ROM sets have different parent/clone relationships than MAME (bootleg-only):

| Parent | Clones |
|--------|--------|
| garoup | garoubl |
| svcboot | svcplus, svcplusa, svcsplus |
| kof2k4se | kf2k4pls |
| kof10th | kf10thep, kf2k5uni |
| kf2k3bl | kf2k3bla, kf2k3pl, kf2k3upl |

### CPS2-Specific Cache Notes

CPS2 cache files are a **conditional streaming fallback**, not an unconditional
requirement. The runtime first probes whether the decoded GFX region can stay
fully resident. If it fits, the original ROM ZIPs are loaded and decoded
directly.

`USE_CACHE` defaults to ON for CPS2 on PSP and PS2, where a game that does not
fit fully in RAM can fall back to converter output from `romcnv_cps2`. Desktop
and PS Vita default to `USE_CACHE=OFF` and therefore require a successful
full-resident allocation.

Supported cache representations are:

- `cache/<game>.cache`
- `cache/<game>_cache.zip`

The runtime also accepts the older `cache/<game>_cache/` folder representation
for backwards compatibility, but current ROMCNV output is raw or ZIP.

#### Special Cases

| Game | Notes |
|------|-------|
| **Super Street Fighter II Turbo** (ssf2t) | Parent is ssf2, but has additional graphics. Create cache for ssf2t, not ssf2 |
| **Mighty! Pang** (mpang/mpangj) | USA and Japan versions have different graphics ROMs - create separate caches |

### NCDZ-Specific Setup

#### BIOS

Two user-supplied top-level files are required:

- `neocd.bin`
- `000-lo.lo`

They are validated by size/CRC by the core and are not distributed with NJEMU.

#### Game Files

Neo-Geo CD games can be stored in two ways:

1. **Extracted folder:** Create a folder containing `IPL.TXT` and the game files
2. **ZIP archive:** Put `IPL.TXT` and the game files into a single ZIP

The current runtime does not mount ISO/BIN/CUE disc images directly.

#### MP3 Audio Setup

CDDA audio must be converted to MP3 format. For a directory-backed game, place
MP3 files in its `mp3/` subfolder. For a ZIP-backed game, the runtime uses an
`mp3/` directory beside the ZIP.

**File Naming Rules:**

MP3 files must end with the track number: `xx.mp3` (where xx = 02-99)

| ✅ Valid Names | ❌ Invalid Names |
|----------------|------------------|
| `mslug-02.mp3` | `02-mslug.mp3` |
| `track02.mp3` | `track_2.mp3` |
| `02.mp3` | `mslug_track2.mp3` |

**Recommended encoding:** 96 Kbps (good balance of quality and size)

#### Example Directory Structure

```
roms/
├── Metal Slug/
│   ├── IPL.TXT
│   ├── *.PRG
│   ├── *.SPR
│   └── mp3/
│       ├── track02.mp3
│       ├── track03.mp3
│       └── ...
├── samsho.zip            # ZIP contains IPL.TXT + game files
└── mp3/                  # Audio directory used by ZIP-backed games
    ├── track02.mp3
    └── ...
```

---

## ROM Conversion Tool (romcnv)

The `romcnv` tools create derived assets for the runtime paths that need them.
`romcnv_cps2` can generate streaming cache data when decoded CPS2 graphics do
not fit fully in RAM. `romcnv_mvs` generates canonical `processed/` assets for
sets that require offline C/S/V-ROM processing and for MVS streaming paths.

### Web Interface

A web-based ROM converter is available at: **[https://fjtrujy.github.io/NJEMU/](https://fjtrujy.github.io/NJEMU/)**

This allows you to convert ROMs directly in your browser without installing any software.

### Why is it needed?

MVS and CPS2 can use processed ROM data through either resident or streaming
runtime paths. The `romcnv` tool prepares that data independently of the target
platform or memory tier:

1. Extracts and processes sprite data from ROM files
2. Creates reusable processed/cache assets for resident or streaming access
3. Decrypts encrypted ROMs (for newer Neo-Geo games)

### Building romcnv

```bash
cd romcnv
mkdir build && cd build
cmake -DTARGET=MVS ..    # For MVS ROM conversion
cmake --build .

# Or for CPS2:
cmake -DTARGET=CPS2 ..
cmake --build .
```

### Usage

```bash
# Convert a single ROM
./romcnv_mvs /path/to/rom.zip

# Convert all ROMs in a directory
./romcnv_mvs /path/to/roms -all

```

### Output

MVS writes canonical processed assets under `processed/`; CPS2 writes cache data
under `cache/`. The generated files are shared across NJEMU platforms rather
than having separate PSP/PS2/Desktop variants.

See [romcnv/README_MVS.md](romcnv/README_MVS.md) and [romcnv/README_CPS2.md](romcnv/README_CPS2.md) for detailed instructions.

---

## Memory Requirements

Understanding memory allocation is crucial for PSP and PS2 platforms where RAM is limited.

### Platform Memory Constraints

| Platform | Available RAM | Notes |
|----------|--------------|-------|
| PSP (Fat) | ~24 MB | User memory only |
| PSP (Slim/2000+) | ~64 MB | Same EBOOT requests the expanded user-memory partition |
| PS2 | ~32 MB | Main RAM |
| Desktop | Unlimited | System dependent |

### Total Memory Requirements by System

| System | CPU/ROM | GFX ROM | Sound ROM | Cache | Static RAM | Estimated Total |
|--------|---------|---------|-----------|-------|------------|-----------------|
| CPS1   | 1-4 MB  | 2-8 MB  | 0.5-2 MB  | -     | ~128 KB    | 4-15 MB         |
| CPS2   | 2-8 MB  | 4-16 MB | 1-4 MB    | 0-20 MB | ~128 KB  | 7-48 MB         |
| MVS    | 1-4 MB  | 8-64 MB | 1-8 MB    | 0-32 MB + 3 MB PCM | ~98 KB | 13-111 MB |
| NCDZ   | 1-2 MB  | 16-128 MB | 0-2 MB  | -     | ~512 B     | 17-132 MB       |

### Why Cache is Required

Many arcade games have graphics data larger than available RAM:
- **MVS games** can have 64+ MB of sprite data
- **CPS2 games** can have 16+ MB of sprite data
- **PSP/PS2** only have 24-64 MB available

The cache system streams graphics from storage in 64 KB blocks, allowing large games to run on memory-constrained platforms.

### PSP Runtime Memory Policy

NJEMU ships a single PSP binary. Its PARAM.SFO explicitly requests the largest
user-memory partition with `MEMSIZE=1`; the same EBOOT therefore runs on
PSP-1000 and PSP-2000/3000-class hardware without a model-specific build.

At startup NJEMU measures the memory actually available to the process with
`pspSdkTotalFreeUserMemSize()` and the largest contiguous allocation with
`sceKernelMaxFreeMemSize()`. The game-specific memory planner then chooses the
cache/residency targets from those runtime measurements.

The important consequences are:

- PSP-1000 naturally receives a smaller cache budget;
- PSP-2000/3000 can use the expanded user heap exposed by the same EBOOT;
- GFX/C-ROM gets allocation priority, with MVS PCM using the remaining planned
  share;
- cache targets are dynamic and aligned to the 64 KB streaming block size;
- CPS2 uses full GFX residency only when the complete region fits the selected
  plan, otherwise it uses the streaming cache;
- MVS applies the same runtime policy to C-ROM and PCM/V-ROM;
- allocation retry-down handles fragmentation without a second build mode;
- the loading log reports the effective allocation, for example
  `C-ROM cache: 15360KB / 65536KB`;
- all PSP allocations use normal heap ownership; there is no raw model-specific
  memory allocator or suspend/resume memory-copy workaround.

### Static RAM Allocations (per system)

**CPS1/CPS2:**
- Main RAM: 64 KB
- Graphics RAM: 48 KB
- Object RAM (CPS2): 8 KB

**MVS:**
- Main RAM: 64 KB
- SRAM: 32 KB
- Memory Card: 2 KB

### GPU Command List Size

| System | GULIST_SIZE |
|--------|-------------|
| CPS1   | 48 KB |
| CPS2   | 48 KB |
| MVS    | 300 KB |
| NCDZ   | 300 KB |

### Sound Buffer Sizes

| System | Buffer Size | Total (stereo) |
|--------|-------------|----------------|
| CPS2 | 2,944 samples | ~12 KB |
| MVS/Others | 1,600 samples | ~6 KB |

---

## Project Structure

```
NJEMU/
├── CMakeLists.txt          # Main CMake build configuration
├── src/
│   ├── common/             # Platform-agnostic code & driver interfaces
│   │   ├── audio_driver.c/h    # Audio abstraction
│   │   ├── video_driver.c/h    # Video abstraction
│   │   ├── input_driver.c/h    # Input abstraction
│   │   ├── thread_driver.c/h   # Threading abstraction
│   │   └── platform_driver.c/h # Platform abstraction
│   │
│   ├── cpu/                # CPU emulation cores
│   │   ├── m68000/         # Motorola 68000 (C68K)
│   │   └── z80/            # Zilog Z80 (CZ80)
│   │
│   ├── sound/              # Sound chip emulation
│   │   ├── ym2151.c/h      # Yamaha YM2151
│   │   ├── ym2610.c/h      # Yamaha YM2610
│   │   └── qsound.c/h      # QSound DSP
│   │
│   ├── mvs/                # MVS/Neo-Geo emulation
│   ├── cps1/               # CPS1 emulation
│   ├── cps2/               # CPS2 emulation
│   ├── ncdz/               # Neo-Geo CD emulation
│   │
│   ├── psp/                # PSP platform drivers
│   ├── ps2/                # PS2 platform drivers
│   └── desktop/            # PC/SDL platform drivers
│
├── romcnv/                 # ROM conversion tools
│   ├── CMakeLists.txt      # romcnv build configuration
│   └── src/                # romcnv source code
│       ├── mvs/            # MVS ROM conversion
│       └── cps2/           # CPS2 ROM conversion
└── docs/                   # Documentation and game lists
```

### Driver Architecture

A platform backend is selected at link time and implements the shared contracts declared in `src/common/`. The normal backend layout is:

| Backend file | Purpose |
|--------------|---------|
| `*_drivers.c` | Bind the common driver globals to this platform's implementations |
| `*_platform.c` | Startup, launch path, main loop, language and memory telemetry |
| `*_video.c` | Native GPU/display, texture layout, sprite submission and readback |
| `*_audio.c` | Native audio output |
| `*_input.c` | Raw physical controller/keyboard sampling |
| `*_thread.c` | Threading and synchronization |
| `*_ticker.c` | Monotonic timing/frame pacing support |
| `*_power.c` | Optional battery/performance capabilities |
| `*_ui_draw.c` | GUI texture storage/lifecycle adapter when `GUI=ON` |
| `png.c` | Platform image load/save/readback glue when `GUI=ON` |

The bound common services are `audio_driver_t`, `input_driver_t`, `platform_driver_t`, `power_driver_t`, `thread_driver_t`, `ticker_driver_t`, `video_driver_t`, and `ui_draw_driver_t`. `ui_draw_driver_t` is deliberately not a second renderer: low-level drawing belongs to `video_driver_t`, while the UI adapter only handles texture storage/lifetime details that genuinely differ by host.

All four target renderers are shared across platforms. A backend receives logical indexed/direct-color atlas updates plus compact `video_sprite_vertex_t`/`video_point_vertex_t` batches and chooses the fastest native execution path without exposing native GPU objects back to target code.

### Target Configuration

Each emulator target (MVS, NCDZ, CPS1, CPS2) defines configuration globals in its core file (e.g., `src/mvs/mvs.c`):

| Global | Type | Purpose |
|--------|------|---------|
| `emu_layer_textures` | `layer_texture_info_t[]` | Texture atlas dimensions per layer |
| `emu_layer_textures_count` | `uint8_t` | Number of texture layers |
| `emu_clut_info` | `clut_info_t` | CLUT configuration (base, entries, banks) |

**CLUT Configuration Example (MVS):**
```c
clut_info_t emu_clut_info = {
    .base = (uint16_t *)video_palettebank,
    .entries_per_bank = PALETTE_BANK_SIZE,  // 4096
    .bank_count = PALETTE_BANKS             // 2
};
```

**CLUT Configuration Example (CPS1):**
```c
clut_info_t emu_clut_info = {
    .base = (uint16_t *)video_palette,
    .entries_per_bank = CPS1_PALETTE_ENTRIES,  // 3072
    .bank_count = 1
};
```

These are passed to the video driver during initialization in `src/emumain.c`.

---

## Technical Architecture - Emulator Targets

This section documents the internal architecture of each emulator target, focusing on the sprite rendering systems. This information is essential for understanding the codebase and for porting to new platforms.

### Common Concepts

#### Texture Caching System

All targets use a hash-table based texture caching system to avoid re-decoding sprites every frame:

```
┌─────────────────────────────────────────────────────────────┐
│                    Texture Cache Flow                        │
├─────────────────────────────────────────────────────────────┤
│  1. Generate key from (code, attributes)                    │
│  2. Look up key in hash table                               │
│  3. If found: return cached texture index                   │
│  4. If not found:                                           │
│     a. Allocate slot in texture atlas                       │
│     b. Decode tile from ROM to texture memory               │
│     c. Insert into hash table                               │
│     d. Return new texture index                             │
│  5. Mark sprite as "used" this frame                        │
│  6. At frame end: evict sprites not used this frame         │
└─────────────────────────────────────────────────────────────┘
```

#### PSP Texture Swizzling (PSP-Specific)

The PSP GPU benefits from swizzled texture storage, but swizzling is no longer part of any target renderer. Common MVS/CPS/NCDZ code always describes atlas updates in logical rectangular coordinates through `video_driver_t::writeIndexedTextureRect()` / `writeDirectTextureRect()`.

The PSP backend translates those logical coordinates into its native swizzled T8 layout; PS2 and Desktop use their own backend-native layouts. This keeps cache/decode policy shared while preventing PSP memory-addressing rules from leaking back into target code. A new platform should implement the same logical texture-update contract rather than copying PSP swizzle helpers.

#### Color Table (CLUT) System

All targets use 4-bit indexed color (16 colors per palette). The `color_table` embeds palette indices into 8-bit texture pixels:

```c
static const uint32_t color_table[16] = {
    0x00000000, 0x10101010, 0x20202020, 0x30303030,
    0x40404040, 0x50505050, 0x60606060, 0x70707070,
    0x80808080, 0x90909090, 0xa0a0a0a0, 0xb0b0b0b0,
    0xc0c0c0c0, 0xd0d0d0d0, 0xe0e0e0e0, 0xf0f0f0f0
};
```

This allows storing a 4-bit palette index in the upper nibble of each 8-bit texture pixel, which is then used with CLUT (Color Look-Up Table) rendering.

---

### MVS (Neo-Geo) Target

**Files:** `src/mvs/sprite.c`, `src/mvs/sprite_common.c`, `src/mvs/sprite_common.h`

**Hardware Reference:** https://wiki.neogeodev.org/

#### Hardware Specifications

| Feature | Specification |
|---------|---------------|
| Max sprites per frame | 381 |
| Max sprites per scanline | 96 |
| Sprite width | Fixed 16 pixels |
| Sprite height | Up to 512 pixels (32 tiles) |
| Sprite scaling | Shrink only (no magnification) |
| Fix layer tiles | 4,096 (12-bit addressing) |
| Palettes | 2 banks × 256 palettes × 16 colors |
| Fix layer palettes | First 16 only |

#### VRAM Layout

The Neo Geo VRAM is organized into Sprite Control Blocks (SCB):

```
VRAM Address Map (word addresses):
─────────────────────────────────────────
$0000-$6FFF  SCB1 - Sprite tilemaps (28KB)
             - 64 words per sprite × 448 sprites
             - Even words: tile number (bits 0-15)
             - Odd words: palette, tile MSB, flip, auto-anim

$7000-$74FF  FIX Layer - 40×32 tilemap
             - Each word: palette (4 bits) | tile (12 bits)

$7500-$7FFF  Extension area (bankswitching)

$8000-$81FF  SCB2 - Shrink coefficients
             - Lower byte: Y shrink ($FF=full, $00=min)
             - Upper nibble: X shrink ($F=full, $0=min)

$8200-$83FF  SCB3 - Y position and size
             - Bits 7-15: Y position (496 - actual)
             - Bit 6: Sticky bit (chain to previous)
             - Bits 0-5: Height in tiles

$8400-$85FF  SCB4 - X position
             - Bits 7-15: X position
```

#### Code Organization

MVS uses one platform-neutral renderer:

| File | Purpose |
|------|---------|
| `sprite_common.h` | Target sprite/cache declarations and constants |
| `sprite_common.c` | Hash/cache management and shared target data |
| `sprite.c` | MVS decoding, batching, atlas/cache policy and portable draw submission |

#### Graphics Layers

| Layer | Name | Tile Size | Purpose |
|-------|------|-----------|---------|
| FIX | Fixed Layer | 8×8 | Text, HUD, static elements |
| SPR | Sprites | 16×16 | Characters, objects, effects |

#### Texture Cache Configuration

| Layer | Hash Size | Texture Size | Max Sprites/Frame |
|-------|-----------|--------------|-------------------|
| FIX | 0x200 | 512×512 / 8×8 tiles | 1,200 |
| SPR | 0x200 | 512×1536 / 16×16 tiles | 12,288 |

#### Texture Atlas Architecture

The emulator uses texture atlases to batch sprite rendering. Decoded tiles are stored in large textures and referenced by UV coordinates.

**FIX Layer Atlas (TEX_FIX):**
```
┌─────────────────────────────────────────────────────┐
│ Dimensions: 512×512 pixels (8-bit indexed)          │
│ Tile size: 8×8 pixels                               │
│ Tiles per row: 64 (512/8)                           │
│ Total rows: 64 (512/8)                              │
│ Max tiles: 4,096                                    │
├─────────────────────────────────────────────────────┤
│  Tile Layout (idx = tile index):                    │
│  ┌────┬────┬────┬────┬─────┬─────┐                  │
│  │ 0  │ 1  │ 2  │ 3  │ ... │ 63  │  row 0          │
│  ├────┼────┼────┼────┼─────┼─────┤                  │
│  │ 64 │ 65 │ 66 │ 67 │ ... │ 127 │  row 1          │
│  ├────┼────┼────┼────┼─────┼─────┤                  │
│  │... │... │... │... │ ... │ ... │                  │
│  └────┴────┴────┴────┴─────┴─────┘                  │
│                                                     │
│  UV Calculation:                                    │
│  u0 = (idx % 64) * 8 = (idx & 0x3f) << 3            │
│  v0 = (idx / 64) * 8 = (idx & 0xfc0) >> 3           │
└─────────────────────────────────────────────────────┘
```

**SPR Layer Atlas (TEX_SPR0/1/2):**
```
┌─────────────────────────────────────────────────────┐
│ Dimensions: 512×1536 pixels (3 banks × 512)         │
│ Tile size: 16×16 pixels                             │
│ Tiles per row: 32 (512/16)                          │
│ Total rows: 96 (1536/16)                            │
│ Max tiles: 3,072                                    │
├─────────────────────────────────────────────────────┤
│  Memory Organization (3 contiguous buffers):        │
│  ┌─────────────────────┐ ─┐                         │
│  │     TEX_SPR0        │  │                         │
│  │   512×512 (bank 0)  │  │                         │
│  ├─────────────────────┤  │                         │
│  │     TEX_SPR1        │  ├─ 512×1536 total         │
│  │   512×512 (bank 1)  │  │                         │
│  ├─────────────────────┤  │                         │
│  │     TEX_SPR2        │  │                         │
│  │   512×512 (bank 2)  │  │                         │
│  └─────────────────────┘ ─┘                         │
│                                                     │
│  UV Calculation:                                    │
│  u0 = (idx % 32) * 16 = (idx & 0x1f) << 4           │
│  v0 = (idx / 32) * 16 = (idx & 0x3e0) >> 1          │
│  bank = idx >> 10  (which 512×512 section)          │
└─────────────────────────────────────────────────────┘
```

#### CLUT (Color Look-Up Table) System

MVS uses a hardware CLUT for palette-based rendering:

```
┌─────────────────────────────────────────────────────┐
│              CLUT Organization                      │
├─────────────────────────────────────────────────────┤
│ Palette Banks: 2 (for raster effects)               │
│ Palettes per Bank: 256                              │
│ Colors per Palette: 16 (4-bit index)                │
│ Color Format: 15-bit RGB (5-5-5)                    │
│ Total Colors: 2 × 256 × 16 = 8,192                  │
├─────────────────────────────────────────────────────┤
│  Bank Layout (256×16 colors each):                  │
│  ┌────────────────────────────────────┐             │
│  │ Bank 0: Palettes 0-255             │             │
│  │   Palette 0:  colors 0-15          │ ← FIX uses  │
│  │   Palette 1:  colors 16-31         │   palettes  │
│  │   ...                              │   0-15 only │
│  │   Palette 15: colors 240-255       │             │
│  │   Palette 16: colors 256-271       │ ← SPR uses  │
│  │   ...                              │   all 256   │
│  │   Palette 255: colors 4080-4095    │             │
│  ├────────────────────────────────────┤             │
│  │ Bank 1: Palettes 0-255             │             │
│  │   (identical structure)            │             │
│  └────────────────────────────────────┘             │
│                                                     │
│  Texture Pixel Format (8-bit):                      │
│  ┌─────────────────────────────────┐                │
│  │ Bits 7-4: Palette offset (0-15) │                │
│  │ Bits 3-0: Color index (0-15)    │                │
│  └─────────────────────────────────┘                │
│                                                     │
│  color_table[] embeds palette offset:               │
│  0x00000000 = palette offset 0                      │
│  0x10101010 = palette offset 1                      │
│  ...                                                │
│  0xf0f0f0f0 = palette offset 15                     │
└─────────────────────────────────────────────────────┘
```

#### Sprite Cache Key Generation

Sprites are cached using unique keys to avoid re-decoding:

```c
// FIX tiles: code (12-bit) + palette (4-bit)
#define MAKE_FIX_KEY(code, attr)  (code | (attr << 28))

// SPR tiles: code (20-bit) + palette high nibble
#define MAKE_SPR_KEY(code, attr)  (code | ((attr & 0x0f00) << 20))
```

The hash table uses open addressing with linked lists for collision resolution.

#### Work Buffer System

The rendering pipeline uses multiple work buffers:

| Buffer | Type | Size | Purpose |
|--------|------|------|---------|
| SCRBITMAP | 16-bit RGB | 384×264 | Software rendering target |
| TEX_SPR0 | 8-bit indexed | 512×512 | Sprite atlas bank 0 |
| TEX_SPR1 | 8-bit indexed | 512×512 | Sprite atlas bank 1 |
| TEX_SPR2 | 8-bit indexed | 512×512 | Sprite atlas bank 2 |
| TEX_FIX | 8-bit indexed | 512×512 | FIX layer atlas |

#### Graphics Data Format

MVS graphics are stored with a simple nibble-packed format:
- Each 32-bit word contains 8 pixels (4 bits per pixel)
- Decoding extracts odd/even nibbles separately:

```c
tile = *(uint32_t *)(src + 0);
*(uint32_t *)(dst +  0) = ((tile >> 0) & 0x0f0f0f0f) | col;  // pixels 0,2,4,6
*(uint32_t *)(dst +  4) = ((tile >> 4) & 0x0f0f0f0f) | col;  // pixels 1,3,5,7
```

#### Sprite Shrinking (NOT Zooming)

**Important:** Neo Geo sprites can only SHRINK, not magnify. The hardware uses pixel-skipping with no interpolation:

```
Horizontal shrink (4-bit value in SCB2):
  $F = Full size (16 pixels)
  $7 = Half size (8 pixels, alternating)
  $0 = Minimum (1 pixel at center)

Vertical shrink (8-bit value in SCB2):
  $FF = Full size
  $00 = Minimum
```

The emulator uses lookup tables (`zoom_x_tables[]`) to determine which pixels to display for each shrink level. Full-size sprites use an optimized `drawgfxline_fixed()` path.

#### Sprite Rendering Features

- **Hardware path:** Used for full-screen updates (>15 scanlines)
- **Software path:** Scanline-by-scanline for partial updates and shrunk sprites
- **Palette banking:** Two palette banks for raster effects
- **ROM caching:** Large sprite ROMs can be cached to storage
- **Sprite chaining:** Horizontal chaining via "sticky bit" for wide objects

#### Screen Resolution

- **Native:** 304×224
- **With borders:** 320×224 (visible area starts at x=24, y=16)

---

### CPS1 (Capcom Play System 1) Target

**Files:** `src/cps1/sprite.c`, `src/cps1/sprite_common.c`, `src/cps1/sprite_common.h`

**Hardware Reference:**
- [Fabien Sanglard's CPS-1 Graphics Study](https://fabiensanglard.net/cps1_gfx/index.html)
- [Arcade Hacker CPS1 Technical Analysis](https://arcadehacker.blogspot.com/2015/04/capcom-cps1-part-1.html)
- [System16 Hardware Database](https://www.system16.com/hardware.php?id=793)

#### Hardware Specifications

| Component | Specification |
|-----------|---------------|
| **CPU** | Motorola 68000 @ 10MHz (primary), Zilog Z80 @ 3.579MHz (sound) |
| **Sound** | Yamaha YM2151 @ 3.579MHz + OKI6295 @ 7.576kHz |
| **Resolution** | 384×224 pixels @ 59.6294Hz |
| **Colors** | 65,536 available, 4,096 on-screen (192 palettes × 16 colors) |
| **Sprites** | 256 per scanline, 16×16 pixels, 16 colors each |
| **Tilemaps** | 3 layers: 512×512, 1024×1024, 2048×2048 pixels |
| **Memory** | 64KB work RAM + 192KB VRAM |

**History:** Released 1988 with *Forgotten Worlds*. In production for 12 years (1988-2000), hosting 32 game titles (~137 with revisions). Notable games include *Street Fighter II*, *Final Fight*, *Ghouls'n Ghosts*, and *Strider*.

#### File Organization

| File | Purpose |
|------|---------|
| `sprite_common.h` | CPS1 cache/decode declarations and constants |
| `sprite_common.c` | Shared sprite-cache management and target data |
| `sprite.c` | CPS1 object/scroll/stars/high-priority rendering and portable draw submission |

#### Graphics Layers

The CPS-1 composites six layers that can be stacked in any order:

| Layer | Name | Tile Size | Tilemap Size | Purpose |
|-------|------|-----------|--------------|---------|
| OBJECT | Sprites | 16×16 | N/A | Characters, projectiles (max 256 per scanline) |
| SCROLL1 | Text Layer | 8×8 | 512×512 | GUI, text, score (finest granularity) |
| SCROLL2 | Main BG | 16×16 | 1024×1024 | Primary scrolling, **supports per-line parallax** |
| SCROLL3 | Background | 32×32 | 2048×2048 | Large background tiles |
| SCROLLH | High Priority | varies | varies | Overlay effects (uses 16-bit direct color) |
| STAR1/STAR2 | Star Field | 1×1 | N/A | Background stars (Forgotten Worlds, etc.) |

**Layer Notes:**
- **SCROLL1** is typically used for GUI elements due to its 8×8 tile size offering the finest granularity
- **SCROLL2** has a special per-line horizontal scrolling feature used for parallax effects (e.g., Street Fighter II stages)
- **Priority masks** allow specific colors to appear over the OBJ layer, enabling effects like staircases in front of characters (Final Fight)
- GFX ROM is divided into four areas at hardware level (one per SCROLL layer + OBJ), sizes fixed at manufacturing

#### Texture Cache Configuration

| Layer | Hash Size | Texture Size | Max Sprites/Frame |
|-------|-----------|--------------|-------------------|
| OBJECT | 0x200 | 512×512 / 16×16 | 4,096 |
| SCROLL1 | 0x200 | 512×512 / 8×8 | ~1,500 |
| SCROLL2 | 0x100 | 512×512 / 16×16 | ~450 |
| SCROLL3 | 0x40 | 512×512 / 32×32 | ~150 |
| SCROLLH | 0x200 | 512×192 / varies | ~1,500 |

#### Texture Atlas Architecture

CPS1 uses **5 separate texture atlases** (vs MVS/NCDZ's 2), each optimized for different tile sizes:

**OBJECT Atlas (tex_object) - 16×16 sprites:**
```
┌─────────────────────────────────────────────────────┐
│ Dimensions: 512×512 pixels (8-bit indexed)          │
│ Tile size: 16×16 pixels                             │
│ Tiles per row: 32 (512/16)                          │
│ Max tiles: 1,024                                    │
├─────────────────────────────────────────────────────┤
│  UV Calculation:                                    │
│  u0 = (idx & 0x001f) << 4                           │
│  v0 = (idx & 0x03e0) >> 1                           │
│                                                     │
│  Flip handling via attribute bits 5-6:             │
│  attr ^= 0x60;                                      │
│  vertices[(attr & 0x20) >> 5].u += 16;  // X flip   │
│  vertices[(attr & 0x40) >> 6].v += 16;  // Y flip   │
└─────────────────────────────────────────────────────┘
```

**SCROLL1 Atlas (tex_scroll1) - 8×8 tiles:**
```
┌─────────────────────────────────────────────────────┐
│ Dimensions: 512×512 pixels (8-bit indexed)          │
│ Tile size: 8×8 pixels                               │
│ Tiles per row: 64 (512/8)                           │
│ Max tiles: 4,096                                    │
├─────────────────────────────────────────────────────┤
│  UV Calculation:                                    │
│  u0 = (idx & 0x003f) << 3                           │
│  v0 = (idx & 0x0fc0) >> 3                           │
└─────────────────────────────────────────────────────┘
```

**SCROLL2 Atlas (tex_scroll2) - 16×16 tiles:**
```
┌─────────────────────────────────────────────────────┐
│ Dimensions: 512×512 pixels (8-bit indexed)          │
│ Tile size: 16×16 pixels                             │
│ Tiles per row: 32 (512/16)                          │
│ Max tiles: 1,024                                    │
├─────────────────────────────────────────────────────┤
│  UV Calculation: Same as OBJECT                     │
│                                                     │
│  Special Feature: Per-line parallax scrolling       │
│  - When clip region < 16 lines: software rendering  │
│  - When clip region >= 16 lines: hardware rendering │
└─────────────────────────────────────────────────────┘
```

**SCROLL3 Atlas (tex_scroll3) - 32×32 tiles:**
```
┌─────────────────────────────────────────────────────┐
│ Dimensions: 512×512 pixels (8-bit indexed)          │
│ Tile size: 32×32 pixels                             │
│ Tiles per row: 16 (512/32)                          │
│ Max tiles: 256                                      │
├─────────────────────────────────────────────────────┤
│  UV Calculation:                                    │
│  u0 = (idx & 0x000f) << 5                           │
│  v0 = (idx & 0x00f0) << 1                           │
└─────────────────────────────────────────────────────┘
```

**SCROLLH Atlas (tex_scrollh) - High Priority Layer:**
```
┌─────────────────────────────────────────────────────┐
│ Dimensions: 512×192 pixels (16-bit DIRECT color)    │
│ Tile sizes: 8×8, 16×16, or 32×32 (varies by layer)  │
│ DIFFERENT from other layers: NOT indexed!           │
├─────────────────────────────────────────────────────┤
│  Key difference: Pre-rendered to 16-bit color       │
│  - No CLUT lookup at draw time                      │
│  - tpens bitmask controls which colors are visible  │
│  - Used for priority mask effects (e.g., staircases │
│    appearing in front of characters in Final Fight) │
└─────────────────────────────────────────────────────┘
```

#### CLUT (Color Look-Up Table) System

CPS1 uses a different palette organization than MVS/NCDZ:

```
┌─────────────────────────────────────────────────────┐
│              CPS1 CLUT Organization                 │
├─────────────────────────────────────────────────────┤
│ Total Palettes: 192 (vs MVS's 256)                  │
│ Colors per Palette: 16 (4-bit index)                │
│ Color Format: 15-bit RGB (5-5-5)                    │
│ On-screen colors: 192 × 16 = 3,072                  │
├─────────────────────────────────────────────────────┤
│  Palette Assignment by Layer:                       │
│  ┌────────────────────────────────────┐             │
│  │ Palettes 0-31:   OBJECT sprites    │             │
│  │ Palettes 32-63:  SCROLL1 tiles     │             │
│  │ Palettes 64-95:  SCROLL2 tiles     │             │
│  │ Palettes 96-127: SCROLL3 tiles     │             │
│  │ Palettes 128-191: Extended/unused  │             │
│  └────────────────────────────────────┘             │
├─────────────────────────────────────────────────────┤
│  Two CLUT Banks (bit 4 of attr):                    │
│  attr & 0x10 == 0: Bank 0 (lower 16 palettes)       │
│  attr & 0x10 != 0: Bank 1 (upper 16 palettes)       │
│                                                     │
│  PSP CLUT Loading:                                  │
│  OBJECT:  clut[0<<4] or clut[16<<4]                 │
│  SCROLL1: clut[32<<4] or clut[48<<4]                │
│  SCROLL2: clut[64<<4] or clut[80<<4]                │
│  SCROLL3: clut[96<<4] or clut[112<<4]               │
└─────────────────────────────────────────────────────┘
```

#### Sprite Cache Key Generation

CPS1 uses different key formats for normal and high-priority layers:

```c
// Normal layers: tile code + palette (4 bits)
#define MAKE_KEY(code, attr)      (code | ((attr & 0x0f) << 28))

// High priority layer: code + attr including tpens mask
#define MAKE_HIGH_KEY(code, attr) (code | ((attr & 0x19f) << 16))
```

#### Work Buffer System

CPS1 requires more memory due to multiple texture atlases:

| Buffer | Type | Size | Purpose |
|--------|------|------|---------|
| scrbitmap | 16-bit RGB | 512×272 | Software rendering target |
| tex_scrollh | 16-bit RGB | 512×192 | High priority layer (direct color) |
| tex_object | 8-bit indexed | 512×512 | OBJECT sprite atlas |
| tex_scroll1 | 8-bit indexed | 512×512 | SCROLL1 tile atlas |
| tex_scroll2 | 8-bit indexed | 512×512 | SCROLL2 tile atlas |
| tex_scroll3 | 8-bit indexed | 512×512 | SCROLL3 tile atlas |

**Memory Layout (PSP):**
```
work_frame ─┬─ scrbitmap    (512 × 272 × 2 bytes)
            ├─ tex_scrollh  (512 × 192 × 2 bytes)
            ├─ tex_object   (512 × 512 × 1 byte)
            ├─ tex_scroll1  (512 × 512 × 1 byte)
            ├─ tex_scroll2  (512 × 512 × 1 byte)
            └─ tex_scroll3  (512 × 512 × 1 byte)
```

#### CPS1 Graphics Data Format (Interleaved Planar)

**Important:** CPS1 graphics ROMs use an interleaved planar format. When decoding a 32-bit word, pixels are NOT sequential:

```c
// 16-bit direct color decoding (SCROLLH layers)
// Notice the interleaved pattern: 0, 4, 1, 5, 2, 6, 3, 7
dst[ 0] = pal[tile & 0x0f]; tile >>= 4;
dst[ 4] = pal[tile & 0x0f]; tile >>= 4;
dst[ 1] = pal[tile & 0x0f]; tile >>= 4;
dst[ 5] = pal[tile & 0x0f]; tile >>= 4;
dst[ 2] = pal[tile & 0x0f]; tile >>= 4;
dst[ 6] = pal[tile & 0x0f]; tile >>= 4;
dst[ 3] = pal[tile & 0x0f]; tile >>= 4;
dst[ 7] = pal[tile & 0x0f];
```

This pattern corresponds to CPS1's hardware graphics layout where bits are organized as:
- Bits 0-3: Pixel 0
- Bits 4-7: Pixel 4
- Bits 8-11: Pixel 1
- Bits 12-15: Pixel 5
- etc.

**This is NOT a PSP optimization - it's inherent to CPS1's graphics format and must be preserved on all platforms.**

#### Rendering Modes

1. **Hardware Rendering:** Uses GPU texture mapping for most layers
2. **Software Rendering:** Direct pixel writing for SCROLL2 when clipping is complex

```c
// Selection based on clip region size
if (scroll2_max_y - scroll2_min_y >= 16) {
    blit_draw_scroll2 = blit_draw_scroll2_hardware;
} else {
    blit_draw_scroll2 = blit_draw_scroll2_software;
}
```

#### Layer Priority System

CPS1 has a flexible layer priority system controlled by hardware registers:
- All layers can have their priority set freely (any stacking order)
- The SCROLLH (high-priority) layer handles tiles that need to appear above sprites
- **Priority masks** can be assigned to tiles, allowing specific colors (pen values) to appear in front of the OBJ layer instead of behind it. This creates effects like characters appearing "inside" background elements (e.g., staircases in *Final Fight*)

#### Screen Resolution

- **Native:** 384×224
- **With borders:** 512×256 work area (visible at x=64, y=16)

#### Rendering Pipeline (CPS1)

```
┌─────────────────────────────────────────────────────────────────┐
│                    CPS1 Frame Rendering Flow                     │
├─────────────────────────────────────────────────────────────────┤
│                                                                  │
│  1. blit_start(high_layer)                                      │
│     └─ Reset scrollh if layer changed                           │
│     └─ Delete dirty palette entries from scrollh cache          │
│     └─ Clear palette dirty marks                                │
│     └─ Clear work frame                                         │
│                                                                  │
│  2. For each layer (priority order varies per game):            │
│                                                                  │
│     SCROLL3: blit_draw_scroll3() → blit_finish_scroll3()        │
│     └─ Cache miss: decode 32×32 tile to tex_scroll3             │
│     └─ Add vertices, batch by CLUT bank (0 or 1)                │
│     └─ Draw with CLUT at palette 96 or 112                      │
│                                                                  │
│     SCROLL2: blit_set_clip_scroll2() sets render mode           │
│     └─ Clip >= 16 lines: hardware path                          │
│     └─ Clip < 16 lines: software path (for parallax)            │
│     └─ blit_draw_scroll2() → blit_finish_scroll2()              │
│     └─ Draw with CLUT at palette 64 or 80                       │
│                                                                  │
│     SCROLL1: blit_draw_scroll1() → blit_finish_scroll1()        │
│     └─ Cache miss: decode 8×8 tile to tex_scroll1               │
│     └─ gfxset parameter for different character sets            │
│     └─ Draw with CLUT at palette 32 or 48                       │
│                                                                  │
│     OBJECT: blit_draw_object() → blit_finish_object()           │
│     └─ Cache miss: decode 16×16 sprite to tex_object            │
│     └─ Track CLUT changes, batch draw when CLUT switches        │
│     └─ Draw with CLUT at palette 0 or 16                        │
│                                                                  │
│     SCROLLH (high priority): varies by scrollh_layer_number     │
│     └─ blit_draw_scroll1h/2h/3h() → blit_finish_scrollh()       │
│     └─ Uses 16-bit direct color (no CLUT at draw time)          │
│     └─ tpens bitmask controls visible colors (priority mask)    │
│                                                                  │
│     STARS: blit_draw_stars() (point rendering)                  │
│     └─ Draws 1×1 pixel stars as GPU points                      │
│     └─ Used in Forgotten Worlds, etc.                           │
│                                                                  │
│  3. blit_finish()                                               │
│     └─ Handle screen rotation/flip if enabled                   │
│     └─ Transfer work frame to display with scaling              │
│                                                                  │
└─────────────────────────────────────────────────────────────────┘
```

---

### CPS2 (Capcom Play System 2) Target

**File:** `src/cps2/sprite.c` (shared by PSP, PS2 and Desktop)

#### Graphics Layers

| Layer | Name | Tile Size | Purpose |
|-------|------|-----------|---------|
| OBJECT | Sprites | 16×16 | Characters, effects (with priority) |
| SCROLL1 | Text Layer | 8×8 | Text, HUD |
| SCROLL2 | Main Background | 16×16 | Primary scrolling |
| SCROLL3 | Background | 32×32 | Large background tiles |

#### Texture Cache Configuration

| Layer | Hash Size | Texture Size | Max Sprites/Frame |
|-------|-----------|--------------|-------------------|
| OBJECT | 0x200 | 512×512 / 16×16 | 5,120 |
| SCROLL1 | 0x200 | 512×512 / 8×8 | ~1,500 |
| SCROLL2 | 0x100 | 512×512 / 16×16 | ~450 |
| SCROLL3 | 0x40 | 512×512 / 32×32 | ~150 |

#### CPS2-Specific Features

**Object Priority System:**
CPS2 has 8 priority levels for sprites. Objects are sorted into priority buckets:

```c
static OBJECT *vertices_object_head[8];  // 8 priority levels
static OBJECT *vertices_object_tail[8];
static uint16_t object_num[8];
```

**Z-Buffer Rendering:**
Optional Z-buffer based rendering for complex priority scenes:

```c
void (*blit_finish_object)(int start_pri, int end_pri);
// Can be either:
// - blit_render_object()     - Standard rendering
// - blit_render_object_zb()  - Z-buffer based
```

#### Graphics Data Format

CPS2 uses the same interleaved planar format as CPS1 (see CPS1 section above).

#### Screen Resolution

- **Native:** 384×224
- **With borders:** 512×256 work area

---

### NCDZ (Neo-Geo CD) Target

**Files:** `src/ncdz/sprite.c`, `src/ncdz/sprite_common.c`, `src/ncdz/sprite_common.h`

**Hardware Reference:** https://wiki.neogeodev.org/

#### Hardware Specifications

| Feature | Specification |
|---------|---------------|
| Max sprites per frame | 381 |
| Max sprites per scanline | 96 |
| Sprite width | Fixed 16 pixels |
| Sprite height | Up to 512 pixels (32 tiles) |
| Sprite scaling | Shrink only (no magnification) |
| Fix layer tiles | 4,096 (12-bit addressing) |
| Palettes | 2 banks × 256 palettes × 16 colors |
| Fix layer palettes | First 16 only |
| Tile addressing | 15-bit (vs MVS 20-bit) |

#### VRAM Layout

The Neo Geo CD uses the same VRAM layout as MVS:

```
VRAM Address Map (word addresses):
─────────────────────────────────────────
$0000-$6FFF  SCB1 - Sprite tilemaps (28KB)
             - 64 words per sprite × 448 sprites
             - Even words: tile number (bits 0-14 for CD)
             - Odd words: palette, tile MSB, flip, auto-anim

$7000-$74FF  FIX Layer - 40×32 tilemap
             - Each word: palette (4 bits) | tile (12 bits)

$7500-$7FFF  Extension area (bankswitching)

$8000-$81FF  SCB2 - Shrink coefficients
             - Lower byte: Y shrink ($FF=full, $00=min)
             - Upper nibble: X shrink ($F=full, $0=min)

$8200-$83FF  SCB3 - Y position and size
             - Bits 7-15: Y position (496 - actual)
             - Bit 6: Sticky bit (chain to previous)
             - Bits 0-5: Height in tiles

$8400-$85FF  SCB4 - X position
             - Bits 7-15: X position
```

#### Code Organization

NCDZ uses one platform-neutral renderer similar to MVS:

| File | Purpose |
|------|---------|
| `sprite_common.h` | Neo Geo CD sprite/cache declarations and constants |
| `sprite_common.c` | Shared target cache management and data |
| `sprite.c` | NCDZ decoding, batching, atlas policy and portable draw submission |

#### Graphics Layers

| Layer | Name | Tile Size | Purpose |
|-------|------|-----------|---------|
| FIX | Fixed Layer | 8×8 | Text, HUD |
| SPR | Sprites | 16×16 | All game graphics |

#### Texture Cache Configuration

| Layer | Hash Size | Texture Size | Max Sprites/Frame |
|-------|-----------|--------------|-------------------|
| FIX | 0x200 | 512×512 / 8×8 | 1,200 |
| SPR | 0x200 | 512×1536 / 16×16 | 12,288 |

#### Texture Atlas Architecture

NCDZ uses the same texture atlas architecture as MVS. Since both systems share the Neo Geo hardware base, the rendering implementation is nearly identical.

**FIX Layer Atlas (TEX_FIX):**
```
┌─────────────────────────────────────────────────────┐
│ Dimensions: 512×512 pixels (8-bit indexed)          │
│ Tile size: 8×8 pixels                               │
│ Tiles per row: 64 (512/8)                           │
│ Max tiles: 4,096                                    │
├─────────────────────────────────────────────────────┤
│  UV Calculation (same as MVS):                      │
│  u0 = (idx & 0x3f) << 3                             │
│  v0 = (idx & 0xfc0) >> 3                            │
│                                                     │
│  Key difference from MVS:                           │
│  - Graphics from memory_region_gfx1 (loaded from CD)│
│  - Different tile decoding (bit expansion):         │
│    datal = ((tile & 0x0f) >> 0) | ...               │
│    (expands 4-bit to 8-bit per pixel)               │
└─────────────────────────────────────────────────────┘
```

**SPR Layer Atlas (TEX_SPR0/1/2):**
```
┌─────────────────────────────────────────────────────┐
│ Dimensions: 512×1536 pixels (3 banks × 512)         │
│ Tile size: 16×16 pixels                             │
│ Tiles per row: 32 (512/16)                          │
│ Max tiles: 3,072                                    │
├─────────────────────────────────────────────────────┤
│  Memory Organization:                               │
│  ┌─────────────────────┐                            │
│  │ TEX_SPR0 (bank 0)   │ ← tiles 0-1023             │
│  ├─────────────────────┤                            │
│  │ TEX_SPR1 (bank 1)   │ ← tiles 1024-2047          │
│  ├─────────────────────┤                            │
│  │ TEX_SPR2 (bank 2)   │ ← tiles 2048-3071          │
│  └─────────────────────┘                            │
│                                                     │
│  UV Calculation:                                    │
│  u0 = (idx & 0x1f) << 4                             │
│  v0 = (idx & 0x3e0) >> 1                            │
│  bank = idx >> 10                                   │
│                                                     │
│  Key difference from MVS:                           │
│  - Graphics from memory_region_gfx2 (loaded from CD)│
│  - No ROM caching needed (all data in RAM)          │
└─────────────────────────────────────────────────────┘
```

#### CLUT (Color Look-Up Table) System

NCDZ uses the same CLUT system as MVS:

```
┌─────────────────────────────────────────────────────┐
│              CLUT Organization                      │
├─────────────────────────────────────────────────────┤
│ Palette Banks: 2                                    │
│ Palettes per Bank: 256                              │
│ Colors per Palette: 16                              │
│ Color Format: 15-bit RGB                            │
├─────────────────────────────────────────────────────┤
│  Palette Selection in Attributes:                   │
│  ┌────────────────────────────────────┐             │
│  │ FIX: attr bits 0-3 = palette 0-15  │             │
│  │ SPR: attr bits 8-11 = palette 0-255│             │
│  └────────────────────────────────────┘             │
│                                                     │
│  color_table[] lookup (same as MVS):                │
│  col = color_table[(attr >> 8) & 0x0f]              │
│  Embeds palette offset into 8-bit texture pixels   │
└─────────────────────────────────────────────────────┘
```

#### Sprite Cache Key Generation

Same key generation as MVS:

```c
// FIX: 12-bit tile code + 4-bit palette
#define MAKE_FIX_KEY(code, attr)  (code | (attr << 28))

// SPR: 15-bit tile code + palette offset
#define MAKE_SPR_KEY(code, attr)  (code | ((attr & 0x0f00) << 20))
```

**Note:** NCDZ uses 15-bit tile codes (`code & 0x7fff`) vs MVS's 20-bit addressing.

#### Work Buffer System

| Buffer | Type | Size | Purpose |
|--------|------|------|---------|
| SCRBITMAP | 16-bit RGB | 384×264 | Software rendering target |
| TEX_SPR0 | 8-bit indexed | 512×512 | Sprite atlas bank 0 |
| TEX_SPR1 | 8-bit indexed | 512×512 | Sprite atlas bank 1 |
| TEX_SPR2 | 8-bit indexed | 512×512 | Sprite atlas bank 2 |
| TEX_FIX | 8-bit indexed | 512×512 | FIX layer atlas |

#### Graphics Data Format

NCDZ uses the same nibble-packed format as MVS:
- Each 32-bit word contains 8 pixels (4 bits per pixel)
- Decoding extracts odd/even nibbles separately:

```c
tile = *(uint32_t *)(src + 0);
*(uint32_t *)(dst +  0) = ((tile >> 0) & 0x0f0f0f0f) | col;  // pixels 0,2,4,6
*(uint32_t *)(dst +  4) = ((tile >> 4) & 0x0f0f0f0f) | col;  // pixels 1,3,5,7
```

#### Sprite Shrinking (NOT Zooming)

**Important:** Like MVS, Neo Geo CD sprites can only SHRINK, not magnify. The hardware uses pixel-skipping with no interpolation:

```
Horizontal shrink (4-bit value in SCB2):
  $F = Full size (16 pixels)
  $7 = Half size (8 pixels, alternating)
  $0 = Minimum (1 pixel at center)

Vertical shrink (8-bit value in SCB2):
  $FF = Full size
  $00 = Minimum
```

The emulator uses lookup tables (`zoom_x_tables[]`) to determine which pixels to display for each shrink level. Full-size sprites use an optimized `drawgfxline_fixed()` path.

#### Sprite Rendering Features

- **Hardware path:** Used for full-screen updates (>15 scanlines)
- **Software path:** Scanline-by-scanline for partial updates and shrunk sprites
- **Palette banking:** Two palette banks for raster effects
- **Sprite chaining:** Horizontal chaining via "sticky bit" for wide objects

#### NCDZ vs MVS Differences

NCDZ is similar to MVS but with key differences:
- **No ROM caching:** Graphics loaded from CD to RAM
- **Larger RAM:** Can hold more graphics data
- **Same graphics format:** Uses MVS-compatible tile format
- **Audio from CD:** MP3/CDDA instead of ROM-based audio
- **15-bit tile addressing:** Uses `code & 0x7fff` vs MVS's 20-bit addressing

#### Screen Resolution

- **Native:** 304×224 (same as MVS)
- **With borders:** 320×224 (visible area starts at x=24, y=16)

---

### Porting Guide

Target renderers are no longer ported separately per host. They emit portable texture updates and compact sprite/point batches through `video_driver_t`; PSP, PS2 and Desktop keep native texture layout, CLUT handling and GPU submission inside their backends.

For the current extension recipe, including logical-vs-physical geometry, driver binding, input capabilities, UI ownership, renderer performance requirements and the validation gate for a new host such as PS Vita, see [docs/PLATFORM_PORTING_GUIDE.md](docs/PLATFORM_PORTING_GUIDE.md). The completed refactor and its measurements are recorded in [docs/PLATFORM_DRIVER_REFACTOR_PLAN.md](docs/PLATFORM_DRIVER_REFACTOR_PLAN.md).

## Internal Systems Documentation

This section provides detailed documentation of the emulator's internal systems, useful for developers and advanced users.

### Sound System Architecture

The sound system uses a multi-threaded architecture to ensure smooth audio output without blocking the main emulation loop.

#### Sound Thread (`src/common/sound.c`)

```
┌─────────────────────────────────────────────────────────────┐
│                    Sound Thread Flow                         │
├─────────────────────────────────────────────────────────────┤
│  Main Thread                    Sound Thread                 │
│  ───────────                    ────────────                 │
│  1. Initialize sound info       1. Wait for enable           │
│  2. Start sound thread          2. Call sound->update()      │
│  3. Enable sound output   ───►  3. Fill buffer with samples  │
│  4. Run emulation               4. Output via audio driver   │
│  5. Disable on pause      ───►  5. Loop or sleep             │
│  6. Stop thread on exit         6. Clean exit                │
└─────────────────────────────────────────────────────────────┘
```

| Configuration | CPS2 | MVS/NCDZ/CPS1 |
|--------------|------|---------------|
| Sample Rate | 24 KHz | 44.1 KHz |
| Buffer Size | 1,600 samples | 2,944 samples |
| Channels | 2 (stereo) | 2 (stereo) |

#### MP3 Thread (`src/common/mp3.c`) - NCDZ Only

The Neo-Geo CD emulator uses a separate thread for MP3 decoding (using libmad):

| Feature | Description |
|---------|-------------|
| Decoder | libmad (MPEG Audio Decoder) |
| Buffer | Double-buffered (736×2 samples each) |
| Auto-loop | Configurable track looping |
| Seek Support | Frame-based seeking for state load |
| Sleep Handling | File re-open after PSP sleep mode |

### State Save/Load System

The state system (`src/common/state.c`) provides save state functionality with thumbnails.

#### State File Format

```
┌─────────────────────────────────────────┐
│ Offset    │ Size      │ Content         │
├───────────┼───────────┼─────────────────┤
│ 0x00      │ 8 bytes   │ Version string  │
│ 0x08      │ 16 bytes  │ Timestamp       │
│ 0x18      │ 34,048 B  │ Thumbnail       │
│ Variable  │ Variable  │ Emulator state  │
└─────────────────────────────────────────┘
```

| System | Buffer Size | Compression |
|--------|-------------|-------------|
| CPS1 | 320 KB | None |
| CPS2 | 336 KB | None |
| MVS | 320 KB | None |
| NCDZ | 3 MB | zlib |

#### Thumbnail Dimensions

| Screen Type | Width | Height |
|-------------|-------|--------|
| Horizontal | 152 | 112 |
| Vertical (CPS1/CPS2) | 112 | 152 |

#### AdHoc State Synchronization

For multiplayer, states are synchronized between PSPs:

```
Server                           Client
──────                           ──────
1. Serialize state          
2. Send state (0x400 chunks) ──► 3. Receive state
4. Wait for ACK             ◄── 5. Send ACK
                                 6. Deserialize state
```

### Command List System

The command list viewer (`src/common/cmdlist.c`) displays move lists from MAME Plus! format `command.dat` files.

#### Supported Character Encodings

| Encoding | Charset Tag | Use Case |
|----------|-------------|----------|
| GBK | `$charset=gbk` | Chinese |
| Shift_JIS | `$charset=shift_jis` | Japanese |
| ISO-8859-1 | `$charset=latin1` | Western European |

The system auto-detects encoding if not specified by analyzing byte patterns.

#### Command.dat Size Reduction

The emulator includes a utility to reduce `command.dat` size by extracting only the games supported by each emulator:

1. Navigate to File Browser
2. Access the command list reduction option
3. Creates backup as `command.org`
4. Outputs optimized `command.dat`

### Cache System Details

The cache system (`src/common/cache.c`) enables running games with graphics larger than available RAM.

#### Cache Types

| Type | Description | Use Case |
|------|-------------|----------|
| `CACHE_RAWFILE` | Uncompressed cache file | Faster loading |
| `CACHE_ZIPFILE` | ZIP compressed cache | Saves storage space |

#### Cache Block Management

```
┌─────────────────────────────────────────────────────────────┐
│                    LRU Cache Algorithm                       │
├─────────────────────────────────────────────────────────────┤
│  Block Request:                                              │
│  1. Check if block in cache (blocks[] array)                 │
│  2. If cached: move to tail (most recently used)             │
│  3. If not cached:                                           │
│     a. Evict head block (least recently used)                │
│     b. Load new block from storage                           │
│     c. Insert at tail                                        │
│  4. Return memory address                                    │
└─────────────────────────────────────────────────────────────┘
```

| Parameter | Value | Notes |
|-----------|-------|-------|
| Block Size | 64 KB | Single cache unit |
| Active cache size | Runtime-selected | Chosen from the retained allocation probe and game requirements |
| Addressability limit | Target/core-specific | Format limit, not a PSP model or memory tier |

#### PCM Cache (MVS Only)

For MVS games with large ADPCM sound data, the runtime planner can reserve a
separate PCM/V-ROM cache. Its active size is selected from the same runtime
memory plan instead of a PSP model-specific build mode.

### Input System

The input system (`src/common/input_driver.c`) provides unified controller handling across platforms.

#### Key Repeat Handling

```c
Initial Delay: 8 frames (~133ms at 60fps)
Repeat Acceleration: Decreases by 2 frames each repeat
Minimum Delay: 2 frames (~33ms)
```

#### Special Input Modes (MVS)

| Game | Mode | Function |
|------|------|----------|
| irrmaze | Analog | Trackball emulation via analog stick |
| popbounc | Analog | Paddle control via analog stick |
| fatfursp | Special | Modified input polling |

### ROM Loading System

The ROM loader (`src/common/loadrom.c`) handles ZIP file extraction and ROM verification.

#### ROM Search Order

```
1. {game_dir}/{game_name}.zip
2. {game_dir}/{parent_name}.zip
3. {launchDir}/roms/{parent_name}.zip
```

#### ROM Types

| Type | Description |
|------|-------------|
| `ROM_LOAD` | Standard sequential load |
| `ROM_CONTINUE` | Continue from previous file |
| `ROM_WORDSWAP` | Byte-swap during load |

#### Interleaved Loading

For ROMs with interleaved data:
```c
group: Number of bytes to read consecutively
skip: Bytes to skip between groups
```

Example: `group=2, skip=2` reads 2 bytes, skips 2, repeating.

### Coin Counter System

The coin counter (`src/common/coin.c`) tracks coin insertions for arcade authenticity:

- Supports up to 4 coin counters
- Lockout support (prevents coin insertion)
- State is saved/loaded with save states

---

## Changelog

### Version 2.4.0 (Cross-Platform Port)
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

## Credits

### Original NJEMU
- **NJ's Emulator** - Original PSP development
- **Cheat codes:** davex
- **HBL/Multi-language support:** 173210
- **MAME Team** for reference implementations

### Cross-Platform Port
- **fjtrujy** - For Multi-platform porting, adding the CMake build system, creating a WASM-based ROM converter, and porting the emulator to PS2 and PC plus some other improvements.

---

## License

This project is licensed under the **GNU General Public License v3.0** - see the [Licence.txt](Licence.txt) file for details.

**Important:** This emulator does not include any copyrighted ROM files. Users must provide their own legally obtained ROM files.

---

## Contributing

Contributions are welcome! Please ensure your code follows the existing style and includes appropriate documentation.

1. Fork the repository
2. Create a feature branch
3. Make your changes
4. Submit a pull request
