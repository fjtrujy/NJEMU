# NJEMU - Multi-Platform Arcade Emulator

[![License: GPL v3](https://img.shields.io/badge/License-GPLv3-blue.svg)](https://www.gnu.org/licenses/gpl-3.0)

## Overview

**NJEMU** is an open-source arcade emulator that provides emulation for classic arcade systems from Capcom and SNK. Originally developed exclusively for the **PSP (PlayStation Portable)**, this project is now being ported to additional platforms.

**Current Version:** 2.4.0  
**Based on:** NJEmu 2.3.5

### Porting Project

This repository contains the ongoing effort to bring NJEMU to multiple platforms:

- **PSP** - Original platform, fully supported
- **PS2** - Primary porting target, actively developed
- **DESKTOP** - Development platform for easier debugging and testing

The PC port using SDL serves primarily as a development and debugging tool, making it easier to test changes before deploying to the target console platforms (PSP and PS2).

### Architecture

The porting effort involved encapsulating platform-agnostic code and creating specific **drivers** for each platform. This abstraction layer allows the emulation core to run on different hardware while platform-specific code handles:

- Video rendering
- Audio output
- Input handling
- Threading
- File I/O

### Current Porting Status

| Emulator | PSP | PS2 | PC |
|----------|-----|-----|-----|
| **MVS** | ✅ Full | ✅ Core | ✅ Core |
| **CPS1** | ✅ Full | ❌ | ❌ |
| **CPS2** | ✅ Full | ❌ | ❌ |
| **NCDZ** | ✅ Full | ✅ Core | ✅ Core |

> **Note:** Currently MVS and NCDZ cores have been ported to PS2 and PC. The menu/GUI system has not been ported yet - only the emulation core runs on the new platforms.

📋 See [PORTING_PLAN.md](PORTING_PLAN.md) for detailed roadmap and remaining work.

---

## Supported Arcade Systems

| Target | Name | Description |
|--------|------|-------------|
| **CPS1** | CPS1PSP | Capcom Play System 1 Emulator |
| **CPS2** | CPS2PSP | Capcom Play System 2 Emulator |
| **MVS** | MVSPSP | Neo-Geo MVS/AES Emulator |
| **NCDZ** | NCDZPSP | Neo-Geo CD Emulator |

## Supported Platforms

| Platform | Description | Status |
|----------|-------------|--------|
| **PSP** | Sony PlayStation Portable | ✅ Original platform |
| **PS2** | Sony PlayStation 2 | 🔄 Active development |
| **DESKTOP** | PC/Desktop (SDL2) | 🛠️ Debug/Development |

---

## How to Use

### Controls

| Button | Action |
|--------|--------|
| O (Circle) | OK / Confirm |
| X (Cross) | Cancel |
| SELECT | Help |
| HOME / PS | Emulator menu |
| SELECT + START | Emulator menu (alternative) |
| R Trigger | BIOS menu (MVS) |

> **Note:** On PS Vita or PPSSPP, the HOME/PS button may not work. You can delete `SystemButtons.prx` and use SELECT+START instead.

### Directory Structure

Place your files in the following structure:

```
[EMULATOR]/
├── roms/           # ROM files (ZIP format)
├── cache/          # ROM cache files
├── cheats/         # Cheat files (.ini)
├── config/         # Configuration files
├── memcard/        # Memory card saves
├── nvram/          # NVRAM saves
├── state/          # Save states
└── PICTURE/        # Screenshots and backgrounds
```

---

## Features

- High-quality arcade emulation
- ROM caching system for improved performance
- Save state support
- Cheat support with extensive cheat databases
- Multiple language support
- DIP switch configuration (CPS1, MVS)
- BIOS menu with UniBIOS 1.0-3.0 support (MVS)
- Ad Hoc multiplayer (PSP)
- Command list display

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
| `LARGE_MEMORY` | Enable large memory mode (PSP-2000+) | OFF |
| `KERNEL_MODE` | Enable kernel mode (PSP) | OFF |
| `COMMAND_LIST` | Enable command list display | OFF |
| `ADHOC` | Enable Ad Hoc multiplayer | OFF |
| `NO_GUI` | Disable GUI (headless mode) | ON |
| `SAVE_STATE` | Enable save state support | OFF |
| `UI_32BPP` | Enable 32-bit color UI | ON |
| `RELEASE` | Release build | OFF |

---

## ROM Compatibility

- ROMs must be in **MAME 0.152** compatible format
- Place ROMs in ZIP format in the `roms/` directory
- Clone sets require the parent ROM in the same directory
- Some games require ROM conversion using the `romcnv` tools

### Supported Games

- **CPS1**: 113+ games (Street Fighter II, Final Fight, Ghouls'n Ghosts, etc.)
- **CPS2**: Capcom CPS2 arcade games
- **MVS**: 270+ games (King of Fighters, Metal Slug, Samurai Shodown, etc.)
- **NCDZ**: Neo-Geo CD games with MP3 audio support

See the `docs/` folder for complete game lists.

---

## ROM Conversion Tool (romcnv)

The `romcnv` tool is a separate utility that converts and splits ROM files into smaller cache files. This is essential for platforms with limited RAM (PSP, PS2) where the full ROM cannot be loaded into memory at once.

### Web Interface (Experimental)

A web-based ROM converter is available at: **[https://fjtrujy.github.io/NJEMU/](https://fjtrujy.github.io/NJEMU/)**

This allows you to convert ROMs directly in your browser without installing any software. 

> **Note:** The web converter is experimental and under active development.

### Why is it needed?

Many arcade ROMs (especially MVS and CPS2 games) have sprite/graphics data that exceeds the available RAM on PSP (~24-64MB) and PS2 (~32MB). The `romcnv` tool:

1. Extracts and processes sprite data from ROM files
2. Creates optimized cache files split into manageable chunks
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

# For PSP-2000 (slim) - skip PCM cache for unencrypted games
./romcnv_mvs /path/to/rom.zip -slim
```

### Output

The tool creates a `cache/` directory containing:
- `[game]_cache/` folder with processed sprite and sound data
- Cache files that the emulator loads instead of the full ROM

Copy the generated cache folder to your emulator's `cache/` directory alongside the original ROM in `roms/`.

See [romcnv/README_MVS.md](romcnv/README_MVS.md) and [romcnv/README_CPS2.md](romcnv/README_CPS2.md) for detailed instructions.

---

## Memory Requirements

Understanding memory allocation is crucial for PSP and PS2 platforms where RAM is limited.

### Platform Memory Constraints

| Platform | Available RAM | Notes |
|----------|--------------|-------|
| PSP (Fat) | ~24 MB | User memory only |
| PSP (Slim/2000+) | ~64 MB | With LARGE_MEMORY builds |
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

### Cache Configuration

| Constant | Normal Memory | LARGE_MEMORY |
|----------|---------------|--------------|
| MAX_CACHE_SIZE | 20 MB | 32 MB |
| MIN_CACHE_SIZE | 2 MB | 4 MB |
| BLOCK_SIZE | 64 KB | 64 KB |

### PSP Extended Memory (Slim/2000+)

PSP Slim models have 32 MB of additional memory accessible via `LARGE_MEMORY` builds:

```c
#define PSP2K_MEM_SIZE   0x2000000  // 32 MB
#define PSP2K_MEM_TOP    0xa000000
#define PSP2K_MEM_BOTTOM 0xbffffff
```

This extended memory is used primarily for large sprite ROMs in MVS games but cannot be freed once allocated.

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

Each platform implements the same driver interfaces:

| Driver | Purpose |
|--------|---------|
| `*_platform.c` | Platform initialization and main loop |
| `*_video.c` | Screen rendering and sprite drawing |
| `*_audio.c` | Sound output and mixing |
| `*_input.c` | Controller/keyboard input |
| `*_thread.c` | Threading and synchronization |
| `*_ticker.c` | Timing and frame pacing |
| `*_power.c` | Power management |

---

## Changelog

### Version 2.4.0 (Cross-Platform Port)
- Refactored codebase with platform abstraction layers (drivers)
- Ported **MVS core** to PS2 (PlayStation 2)
- Ported **MVS core** to PC/SDL2 for development and debugging
- Introduced CMake build system for unified cross-platform builds
- Maintained full PSP compatibility
- Note: Menu/GUI not yet ported to PS2 and PC (core emulation only)

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
- Multi-platform porting and CMake build system

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
