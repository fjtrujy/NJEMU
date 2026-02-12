# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

NJEMU is a multi-platform arcade emulator written in C. It emulates Capcom and SNK arcade systems:
- **CPS1** (Capcom Play System 1)
- **CPS2** (Capcom Play System 2)
- **MVS** (Neo-Geo MVS/AES)
- **NCDZ** (Neo-Geo CD)

Builds are organized by two orthogonal axes: `TARGET` (arcade system) and `PLATFORM` (PSP, PS2, DESKTOP).

## Build Commands

CMake requires both `-DTARGET=` and `-DPLATFORM=`:

```bash
# Desktop MVS build (recommended for development)
mkdir build_desktop_mvs && cd build_desktop_mvs
cmake -DTARGET=MVS -DPLATFORM=DESKTOP ..
make -j4

# Desktop CPS1 build
mkdir build_desktop_cps1 && cd build_desktop_cps1
cmake -DTARGET=CPS1 -DPLATFORM=DESKTOP ..
make -j4

# Desktop CPS2 build
mkdir build_desktop_cps2 && cd build_desktop_cps2
cmake -DTARGET=CPS2 -DPLATFORM=DESKTOP ..
make -j4

# PSP build with extended memory
mkdir build_psp_mvs && cd build_psp_mvs
cmake -DTARGET=MVS -DPLATFORM=PSP -DLARGE_MEMORY=ON ..
make -j4

# PS2 CPS1 build
mkdir build_ps2_cps1 && cd build_ps2_cps1
cmake -DTARGET=CPS1 -DPLATFORM=PS2 ..
make -j4

# PS2 CPS2 build
mkdir build_ps2_cps2 && cd build_ps2_cps2
cmake -DTARGET=CPS2 -DPLATFORM=PS2 ..
make -j4

# PS2 MVS build
mkdir build_ps2_mvs && cd build_ps2_mvs
cmake -DTARGET=MVS -DPLATFORM=PS2 ..
make -j4
```

**Run from the build directory** so relative resource paths work. The executable is named after the TARGET (e.g., `MVS`, `CPS1`).

### Useful CMake Options

- `NO_GUI=ON` (default) - Disable GUI, use stub UI
- `USE_ASAN=ON` - AddressSanitizer
- `SAVE_STATE=ON` - Enable save states
- `COMMAND_LIST=ON` - Command list recording
- `LARGE_MEMORY=ON` - PSP Slim extended memory

## Architecture

### Driver-Based Abstraction

The codebase uses driver interfaces in `src/common/` to decouple emulation from platform-specific code:

- **`video_driver_t`** - Screen rendering, texture management, CLUT
- **`audio_driver_t`** - Sound output
- **`input_driver_t`** - Controller input
- **`platform_driver_t`** - Platform init and main loop
- **`thread_driver_t`** - Threading primitives
- **`ticker_driver_t`** - Timing and frame sync

Each platform implements these interfaces in `src/<platform>/` (e.g., `src/psp/psp_video.c`, `src/ps2/ps2_audio.c`).

### Source Layout

```
src/
├── common/          # Platform-agnostic code and driver interfaces
├── cpu/             # CPU cores (m68000, z80)
├── sound/           # Sound synthesis
├── zip/             # ROM archive handling
├── mvs/             # Neo-Geo MVS target
├── cps1/            # CPS1 target
├── cps2/            # CPS2 target
├── ncdz/            # Neo-Geo CD target
├── psp/             # PSP platform drivers
├── ps2/             # PS2 platform drivers
└── desktop/         # Desktop (SDL2) platform drivers
```

### Platform-Specific Sprite Rendering

Each TARGET has platform-specific sprite files following the pattern `src/<target>/<platform>_sprite.c`:
- `src/mvs/psp_sprite.c`, `src/mvs/ps2_sprite.c`, `src/mvs/desktop_sprite.c`
- `src/cps1/psp_sprite.c`, `src/cps1/ps2_sprite.c`, `src/cps1/desktop_sprite.c`
- `src/cps2/psp_sprite.c`, `src/cps2/ps2_sprite.c`, `src/cps2/desktop_sprite.c`
- `src/ncdz/psp_sprite.c`, `src/ncdz/ps2_sprite.c`, `src/ncdz/desktop_sprite.c`

Shared sprite logic lives in `<target>/sprite_common.c`.

### CPS2 Porting Notes

CPS2 is now fully ported to all platforms (PSP, PS2, Desktop). It is similar to CPS1 but simpler (no SCROLLH layer, no star field). It has 4 indexed texture atlases (OBJECT 16x16, SCROLL1 8x8, SCROLL2 16x16, SCROLL3 32x32), all 512x512 at 8-bit indexed. Key differences from CPS1: 8-level object priority with Z-buffer masking (`cps2_has_mask`), double-buffered object RAM, and optional 1-frame palette delay.

CPS2-specific implementation details:
- **PS2:** Z-buffer masking via GS ZBUF register (ZTST GEQUAL/ALWAYS modes), priority linked-lists with gsKit primitives
- **Desktop:** Priority linked-lists with struct Vertex arrays, CLUT batching, `desktop_clearFrame` for mask support
- **PSP:** Original sceGu-based rendering with swizzled textures

### Key Entry Points

- **`src/emumain.c`** - Main entry, driver initialization
- **`src/<target>/driver.c`** - Target-specific driver wiring
- **`src/<target>/memintrf.c`** - Memory interface (CPU, PPU, sound chip access)
- **`src/<target>/vidhrdw.c`** - Video hardware simulation

### Video Driver Integration

Each TARGET must define these globals in its core file (e.g., `src/mvs/mvs.c`, `src/cps1/cps1.c`):
- **`emu_layer_textures`** - Array of `layer_texture_info_t` describing texture atlas dimensions
- **`emu_layer_textures_count`** - Number of texture layers
- **`emu_clut_info`** - CLUT configuration (`clut_info_t` with base pointer, entries per bank, bank count)

These are passed to the video driver during initialization in `src/emumain.c`.

## Where to Make Changes

- **Emulator core logic:** `src/common/` and `src/cpu/`
- **Target-specific behavior:** `src/mvs/`, `src/cps1/`, `src/cps2/`, `src/ncdz/`
- **Platform glue:** `src/<platform>/` (e.g., `src/ps2/`)
- **Build configuration:** `CMakeLists.txt`

## External Dependencies

- **Desktop:** SDL2 (linked as `SDL2::SDL2-static`)
- **MP3 support:** libmad (optional, `-DLIB_MAD=ON`)
- **PS2/PSP:** Platform SDK libraries

## Current Porting Status

- **MVS:** Fully ported to all platforms (PSP, PS2, Desktop)
- **CPS1:** Fully ported to all platforms (PSP, PS2, Desktop)
- **CPS2:** Fully ported to all platforms (PSP, PS2, Desktop)
- **NCDZ:** Fully ported to all platforms (PSP, PS2, Desktop)
- **GUI:** Only available on PSP; other platforms use stub UI (`*_no_gui.c`)

All four emulator cores are complete. The next milestone is GUI/menu system porting. See `PORTING_PLAN.md` for detailed roadmap.
