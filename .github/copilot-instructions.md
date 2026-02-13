# Copilot instructions — NJEMU

Purpose: provide concise, actionable guidance for AI agents editing NJEMU so they can be productive immediately.

- **Big picture:** NJEMU is a C-based emulator driven by CMake. Builds are organized by two orthogonal axes: `TARGET` (CPS1, CPS2, MVS, NCDZ) and `PLATFORM` (PSP, PS2, DESKTOP). See [CMakeLists.txt](CMakeLists.txt#L1) for the authoritative list and logic.
- **Subsystems & layout:** primary source lives under `src/` with subsystem folders: `cpu/`, `sound/`, `common/`, and per-target folders like `src/mvs/`, `src/cps1/`, `src/ncdz/`.
- **Platform pattern:** platform-specific code is included by CMake using a lowercased platform name and filename patterns such as `mvs/${PLATFORM_LOWER}_sprite.c`. Example: [src/mvs/ps2_sprite.c](src/mvs/ps2_sprite.c#L1).

- **Build basics (required):** CMake requires both `-DTARGET=` and `-DPLATFORM=`. Example desktop build for MVS:

```
mkdir build_desktop_mvs
cd build_desktop_mvs
cmake -DTARGET=MVS -DPLATFORM=DESKTOP ..
make -j4
```

- **Resources behavior:** during configure CMake copies `resources/<target>/` into the build directory (see the `file(COPY ...)` block in [CMakeLists.txt](CMakeLists.txt#L300)). Expect assets and ROM descriptors under `resources/` and `build/` after configure.

- **Common flags & options:** useful CMake options exposed in the top-level file include `GUI` (defaults OFF), `USE_ASAN`, `SAVE_STATE`, `COMMAND_LIST`, and platform-specific warning tweaks. Toggle them with `-D<OPTION>=ON` when running `cmake`.

- **Where to make changes:**
  - Modify emulator core logic in `src/common/` and `src/cpu/`.
  - Target-specific behavior lives in `src/mvs/`, `src/cps1/`, `src/ncdz/`.
  - Platform glue lives in `src/<platform_lower>/` (e.g., `src/ps2/`).

- **Integration & external deps:**
  - Desktop builds require SDL2 (CMake uses `find_package(SDL2)` and links `SDL2::SDL2-static`).
  - MP3 support enables libmad (`LIB_MAD`) and is optional via CMake flags.
  - PS2/PSP targets link against platform SDK libs configured in CMake.

- **Binary names & run pattern:** the executable is named exactly as the `TARGET` (e.g., `MVS`, `CPS1`) because CMake calls `add_executable(${TARGET} ...)`. Run the produced binary from the build directory so relative resource paths work.

- **Conventions for changes:**
  - Keep C style consistent with existing files (no large refactors in the same PR).
  - When adding a new PLATFORM or TARGET, update the lists in [CMakeLists.txt](CMakeLists.txt#L1) and add platform-specific source patterns following existing naming (use `${PLATFORM_LOWER}` substitution).

- **Porting status:** All four emulator cores (MVS, NCDZ, CPS1, CPS2) are fully ported to all platforms (PSP, PS2, Desktop). The next milestone is GUI/menu system porting.

- **Quick pointers to inspect for common tasks:**
  - Entry & driver wiring: `src/emumain.c` and `src/mvs/driver.c`.
  - Sprite/video interfaces: `src/mvs/sprite_common.c`, `src/cps1/sprite_common.c`, and `src/cps2/sprite_common.c`.
  - ROM loading and zip handling: `src/zip/` and `src/common/loadrom.c`.
  - Video driver change: each TARGET must define `emu_layer_textures`, `emu_layer_textures_count`, and `emu_clut_info` in its core file; see `src/mvs/mvs.c` for an example and `src/emumain.c` for the init call.
  - CPS2 Z-buffer masking: PS2 uses GS ZBUF register (`src/ps2/ps2_video.c`), Desktop uses `desktop_clearFrame` (`src/desktop/desktop_video.c`).

If any section is unclear or you want examples for a specific task (add a new PLATFORM, debug audio, run DESKTOP with GUI), tell me which task and I will expand or adjust the instructions.
