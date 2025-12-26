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

### Build Directory Convention

Build directories follow the naming pattern: `build_{platform}_{target}`

Examples:
- `build_psp_cps1` - PSP build for CPS1
- `build_psp_mvs` - PSP build for MVS
- `build_ps2_mvs` - PS2 build for MVS
- `build_desktop_ncdz` - Desktop build for NCDZ

### Resource Folders

Each target has a corresponding resource folder named `{TARGET}_RESOURCE` containing files required for the emulator to work properly:

| Target | Resource Folder |
|--------|-----------------|
| CPS1 | `CPS1_RESOURCE/` |
| CPS2 | `CPS2_RESOURCE/` |
| MVS | `MVS_RESOURCE/` |
| NCDZ | `NCDZ_RESOURCE/` |

These resource files are automatically copied to the build directory when running the `cmake` command.

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
make
```

#### Example: Building CPS1 for PSP

```bash
export PSPDEV=/Users/fjtrujy/toolchains/psp/pspdev
export PATH=$PSPDEV/bin:$PATH

mkdir build_psp_cps1
cd build_psp_cps1
cmake -DPLATFORM="PSP" \
      -DCMAKE_TOOLCHAIN_FILE=$PSPDEV/psp/share/pspdev.cmake \
      -DCMAKE_BUILD_TYPE=RelWithDebInfo \
      -DTARGET=CPS1 \
      ..
make
```

#### Output

After a successful build, you'll find the following files in the build directory:
- `EBOOT.PBP` - The main executable for PSP
- Resource files copied from `{TARGET}_RESOURCE/`

#### Configuring the Game (NO_GUI builds)

For builds with `NO_GUI=ON` (default), the emulator reads the game to boot from the `game_name.ini` file in the build directory. Edit this file and set it to the ROM name (without extension):

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
cmake -DCMAKE_TOOLCHAIN_FILE=${PS2SDK}/ps2dev.cmake \
      -DCMAKE_BUILD_TYPE=RelWithDebInfo \
      -DTARGET={TARGET} \
      -DPLATFORM=PS2 \
      ..
```

Replace `{TARGET}` with one of: `MVS` or `NCDZ` (currently supported on PS2).

3. Build the project:

```bash
make
```

#### Example: Building MVS for PS2

```bash
export PS2DEV=/Users/fjtrujy/toolchains/ps2/ps2dev
export PS2SDK=$PS2DEV/ps2sdk
export PATH=$PATH:$PS2DEV/bin:$PS2DEV/ee/bin:$PS2DEV/iop/bin:$PS2DEV/dvp/bin:$PS2SDK/bin

mkdir build_ps2_mvs
cd build_ps2_mvs
cmake -DCMAKE_TOOLCHAIN_FILE=${PS2SDK}/ps2dev.cmake \
      -DCMAKE_BUILD_TYPE=RelWithDebInfo \
      -DTARGET=MVS \
      -DPLATFORM=PS2 \
      ..
make
```

#### Output

After a successful build, you'll find the following files in the build directory:
- `{TARGET}.elf` - The main executable for PS2
- Resource files copied from `{TARGET}_RESOURCE/`

#### Configuring the Game (NO_GUI builds)

For builds with `NO_GUI=ON` (default), the emulator reads the game to boot from the `game_name.ini` file in the build directory. Edit this file and set it to the ROM name (without extension):

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

Replace `{TARGET}` with the target name (e.g., `MVS`, `NCDZ`).

#### Debugging

For debugging on PS2, use `ps2client` with ps2link:

```bash
# Send and run the ELF file on PS2 running ps2link
ps2client -h <PS2_IP_ADDRESS> execee host:{TARGET}.elf
```

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

### PSP Extended Memory (LARGE_MEMORY - Slim/2000+)

PSP Slim (2000/3000) models have 32 MB of additional memory that's not accessible to standard PSP applications. NJEMU can use this extended memory when built with `-DLARGE_MEMORY=ON`.

#### Memory Address Space

```c
#define PSP2K_MEM_TOP    0xa000000   // Start of extended memory
#define PSP2K_MEM_BOTTOM 0xbffffff   // End of extended memory
#define PSP2K_MEM_SIZE   0x2000000   // 32 MB total
```

#### Custom Allocator

A custom allocator manages this extended memory region:

```c
static void *psp2k_mem_alloc(int32_t size);   // Allocate from extended memory
static void *psp2k_mem_move(void *mem, int32_t size);  // Move data to extended memory
static void psp2k_mem_free(void *mem);        // Free (only works for standard memory)
```

**Important Limitations:**
- Memory allocated in the extended region **cannot be freed** (causes system freeze)
- Allocations are linear - no fragmentation management
- Once a game uses extended memory, system must restart to reclaim it

#### Concrete Usage in Code

**Direct Allocation with `psp2k_mem_alloc()`:**

| Location | Region | Purpose | Size |
|----------|--------|---------|------|
| `src/mvs/memintrf.c:847` | `memory_region_gfx3` | Sprite ROM data (unencrypted games) | 8-64 MB |
| `src/mvs/memintrf.c:966` | `memory_region_sound1` | YM2610 ADPCM samples (fallback when malloc fails) | 1-8 MB |

**Memory Migration with `psp2k_mem_move()`:**

When SOUND1 allocation triggers extended memory use (`psp2k_mem_left != PSP2K_MEM_SIZE`), regions are moved to free main RAM for cache (`src/mvs/memintrf.c:1775-1785`):

| Region | Data Type | Typical Size |
|--------|-----------|--------------|
| `memory_region_user3` | Protection/banking data | Variable |
| `memory_region_gfx4` | Fixed layer sprites (FIX) | 128 KB - 1 MB |
| `memory_region_gfx2` | Zoom table data | 128 KB |
| `memory_region_gfx1` | Fixed layer ROM | 128 KB - 512 KB |
| `memory_region_cpu2` | Z80 program ROM | 128 KB - 512 KB |
| `memory_region_user1` | BIOS ROM | 128 KB |
| `memory_region_cpu1` | M68000 program ROM | 1-4 MB |
| `gfx_pen_usage[0-2]` | Sprite transparency tables | Variable |

**Cache Buffer Direct Assignment (`src/common/cache.c:710`):**

When no extended memory has been used yet, the cache buffer is placed directly at the start of extended memory:
```c
if (psp2k_mem_left == PSP2K_MEM_SIZE) {
    GFX_MEMORY = (uint8_t *)PSP2K_MEM_TOP;  // Direct pointer, up to 32 MB
}
```

**Safe Deallocation with `psp2k_mem_free()`:**

At shutdown (`src/mvs/memintrf.c:2025-2039`), all regions are passed through `psp2k_mem_free()` which only calls `free()` for standard memory addresses:
```c
// Only frees if address < PSP2K_MEM_TOP (0xa000000)
// Extended memory regions are silently ignored to prevent freeze
```

#### What Gets Placed in Extended Memory

**MVS (Priority order):**
1. **GFX3 (Sprite ROMs)** - Large sprite data (8-64 MB) goes directly to extended memory
2. **Cache buffer** - If GFX3 uses cache, the cache buffer uses extended memory (up to 32 MB)
3. **SOUND1 (ADPCM)** - If normal malloc fails, ADPCM data moves to extended memory
4. **Other regions** - CPU1, CPU2, GFX1, GFX2, GFX4, USER1, USER3 can be moved to free main RAM

**CPS2:**
- Disables the cache system entirely (`USE_CACHE=0`)
- Graphics data loaded directly into memory
- Only suitable for games with smaller graphics data

#### Impact on Cache System

| Setting | Normal (PSP Fat) | LARGE_MEMORY (PSP Slim) |
|---------|------------------|-------------------------|
| USE_CACHE (CPS2) | Enabled | **Disabled** |
| USE_CACHE (MVS) | Enabled | Enabled |
| MIN_CACHE_SIZE | 2 MB (0x20 blocks) | 4 MB (0x40 blocks) |
| MAX_CACHE_SIZE | 20 MB (0x140 blocks) | 32 MB (0x200 blocks) |
| Cache location | Main RAM | Extended memory (if available) |

#### Memory Optimization Strategy

When MVS detects that SOUND1 had to use extended memory (indicating main RAM pressure), it automatically moves previously allocated regions to extended memory in reverse allocation order:

```
USER3 → GFX4 → GFX2 → GFX1 → CPU2 → USER1 → CPU1 → pen_usage tables
```

This frees contiguous blocks in main RAM for the cache system.

#### Power Management

Extended memory state is preserved during PSP sleep/resume cycles. The system stores the last 4 MB of extended memory (`PSP2K_MEM_TOP + 0x1c00000`) to handle sleep mode properly.

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
