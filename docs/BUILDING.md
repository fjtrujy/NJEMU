# Building NJEMU

NJEMU uses CMake. Every emulator build selects two independent axes:

- `TARGET`: `CPS1`, `CPS2`, `MVS`, or `NCDZ`;
- `PLATFORM`: `PSP`, `PS2`, `PSVITA`, or `DESKTOP`.

Use out-of-tree build directories and keep runtime data outside source-control commits.

## Common build flow

A Desktop MVS build is the simplest example:

```sh
cmake -S . -B build-desktop-mvs \
  -DPLATFORM=DESKTOP \
  -DTARGET=MVS \
  -DGUI=ON
cmake --build build-desktop-mvs --parallel
cmake --install build-desktop-mvs
```

Unless an install prefix is supplied explicitly, NJEMU installs to `<build>/install`.
The install tree is the distributable runtime tree for that one core. It is built from an explicit manifest and intentionally excludes ROMs, BIOS files, processed assets, caches, saves, NVRAM, screenshots, and local configuration.

## Native host tools

NJEMU's build-time generators are portable C99 programs. Python is not required for a normal build, ROM converter build, test run, or release package.

The main build configures `tools/host/` as a separate native CMake sub-build through `cmake/NJEMUHostTools.cmake`. This is important for PSP, PS2, PS Vita, and WebAssembly builds: metadata, translation, and font generators must execute on the build machine rather than being compiled with the target toolchain.

Cross-builds locate a native `cc`, `clang`, or `gcc` outside the target sysroot. Set `NJEMU_HOST_C_COMPILER=/path/to/host/cc` when the automatic choice is not appropriate. The target toolchain file is never forwarded to the host-tool sub-build.

For manual generator/validator use:

```sh
cmake -S tools/host -B build-host-tools -DCMAKE_BUILD_TYPE=Release
cmake --build build-host-tools --parallel
./build-host-tools/njemu-tool --help
```

The unified tool provides translation, font, metadata/database, DIP, rominfo, CPS2 cache-layout, and frame-comparison commands. `docs/HOST_TOOLING_MIGRATION_PLAN.md` documents the architecture and format-parity history.

## Main CMake options

The current public feature switches include:

| Option | Purpose | Important constraints/defaults |
| --- | --- | --- |
| `GUI` | Common graphical frontend | Default `OFF`; official release builds enable it |
| `SAVE_STATE` | Save/load state support | Default `OFF`; official release builds enable it |
| `COMMAND_LIST` | MAME Plus!-style command list UI | Default `OFF`; data remains optional at runtime |
| `ADHOC` | PSP Ad Hoc support | PSP + MVS only |
| `USE_CACHE` | Streaming cache fallback | Valid for CPS2/MVS; default ON on PSP/PS2 and OFF on Desktop/Vita |
| `USE_DESKTOP_GL` | Compile Desktop OpenGL backend alongside SDL | Desktop only; default ON |
| `PS2_FAST_CACHE` | PS2 accelerated cache I/O | Requires PS2 + `USE_CACHE=ON`; default ON on PS2 |
| `PS2_EXTERNAL_IRX_IMAGE` | Use the external PS2 driver image/bootstrap path | PS2 only; default OFF |
| `PS2_VIDEO_MODE` | Initial PS2 output mode (`240p`, `480i`, `480p`) | PS2 only; default `480i`; runtime settings can change mode |
| `PSP_ME_AUDIO` | PSP Media Engine audio-production capability | PSP only; default OFF; the canonical PSP release enables it |
| `PSP_AUDIO_PROFILE` | PSP audio timing diagnostics | PSP only; default OFF |
| `USE_ASAN`, `USE_UBSAN`, `USE_PG` | Development diagnostics | Desktop only |
| `RELEASE` | Release-only compile behavior used by legacy code paths | Independent of CMake build type |

CMake rejects unsupported option combinations instead of silently ignoring them.

## Desktop

Requirements:

- CMake 3.12 or newer;
- a C/C++ compiler;
- SDL2 development files;
- OpenGL 3.3 support when the OpenGL backend is enabled.

Example:

```sh
cmake -S . -B build-desktop-cps2 \
  -DPLATFORM=DESKTOP \
  -DTARGET=CPS2 \
  -DGUI=ON \
  -DSAVE_STATE=ON \
  -DCOMMAND_LIST=ON
cmake --build build-desktop-cps2 --parallel
cmake --install build-desktop-cps2
```

The normal Desktop build contains SDL and OpenGL video backends and exposes renderer selection at runtime.

## PSP

Requirements:

- PSPSDK / pspdev;
- the PSPSDK CMake toolchain.

Example:

```sh
cmake -S . -B build-psp-mvs \
  -DCMAKE_TOOLCHAIN_FILE="$PSPDEV/psp/share/pspdev.cmake" \
  -DPLATFORM=PSP \
  -DTARGET=MVS \
  -DGUI=ON \
  -DSAVE_STATE=ON \
  -DCOMMAND_LIST=ON \
  -DPSP_ME_AUDIO=OFF
cmake --build build-psp-mvs --parallel
cmake --install build-psp-mvs
```

The PSP package is an `EBOOT.PBP`. NJEMU requests the large user-memory partition (`MEMSIZE=1`) in the package metadata and then sizes game allocations from the memory actually available at runtime. There is no separate PSP-1000/PSP-2000 build.

`PSP_ME_AUDIO` is the only public Media Engine capability switch. `OFF` binds the
normal CPU producer directly and is the strongest PPSSPP/reference configuration.
`ON` keeps the CPU producer as a first-class runtime fallback and selects the measured
target-specific PSP implementation automatically: CPS1 uses bounded QSound/OKIM6295
jobs, CPS2 uses persistent Z80 + QSound, and MVS/NCDZ use persistent Z80 + YM2610
(with the NCDZ machine profile for NCDZ). Runtime selection remains Auto / Main CPU /
Media Engine; Main CPU skips ME initialization.

Canonical PSP packages enable `PSP_ME_AUDIO`; CI also keeps explicit OFF builds as the
CPU/PPSSPP reference. `PSP_ME_SOUND_COPROCESSOR` is obsolete and is not a supported
build axis. Supplying it in an old CMake cache does not enable a second mode.

ME-specific dependency discovery, source selection, compile flags, hardware oracles,
and packaging are centralized in `cmake/NJEMUPSPMediaEngine.cmake`. Non-PSP builds and
PSP `PSP_ME_AUDIO=OFF` builds do not search for or link the ME libraries, and CMake
validates that their executable remains bound to `audio_producer_cpu`.

## PlayStation 2

Requirements:

- ps2dev / PS2SDK;
- the PS2 CMake toolchain;
- the dependencies used by the existing CI configuration.

Example:

```sh
cmake -S . -B build-ps2-mvs \
  -DCMAKE_TOOLCHAIN_FILE="$PS2DEV/share/ps2dev.cmake" \
  -DPLATFORM=PS2 \
  -DTARGET=MVS \
  -DGUI=ON \
  -DSAVE_STATE=ON \
  -DCOMMAND_LIST=ON
cmake --build build-ps2-mvs --parallel
cmake --install build-ps2-mvs
```

The normal PS2 build embeds the platform driver image and installs the target ELF directly. `PS2_EXTERNAL_IRX_IMAGE=ON` remains a developer/validation variant rather than the canonical distribution configuration.

## PlayStation Vita

Requirements:

- VitaSDK with `VITASDK` configured;
- vita2d;
- VitaGL / vitashark;
- the dependencies used by the Vita CI image.

Example:

```sh
cmake -S . -B build-vita-mvs \
  -DCMAKE_TOOLCHAIN_FILE="$VITASDK/share/vita.toolchain.cmake" \
  -DPLATFORM=PSVITA \
  -DTARGET=MVS \
  -DGUI=ON \
  -DSAVE_STATE=ON \
  -DCOMMAND_LIST=ON
cmake --build build-vita-mvs --parallel
cmake --install build-vita-mvs
```

Every Vita build contains both the native GXM/vita2d renderer and VitaGL. The renderer is selected at runtime; there is no `USE_VITAGL` build option.

## No-GUI builds

`GUI=OFF` removes the menu/browser frontend but does not remove all UI/runtime services. The no-GUI launcher reads `game_name.ini` from the runtime root. Generated language data and the external GBK font are still installed because overlays/loading UI use the common text/draw services.

## Runtime files

The source `resources/` tree is a development/validation convenience and may contain local user data. It is not the packaging source of truth.

See [RUNTIME_FILES_AUDIT.md](RUNTIME_FILES_AUDIT.md) for the complete per-core and per-platform contract, including generated metadata, BIOS/ROM ownership, cache/processed formats, save data, and lookup order.

## Canonical release builds

Official downloadable packages use the repository's canonical release configuration instead of exposing every CMake combination. The release configuration enables the stable user-facing features that can safely coexist on each platform and leaves experimental or compatibility-sensitive options off.

The canonical configuration and release workflow are documented in [RELEASING.md](RELEASING.md). The release automation builds each core separately, installs each explicit runtime tree, then packages one archive per platform.

## Validation

For local Desktop validation, use CTest from the configured build directory:

```sh
ctest --test-dir build-desktop-mvs --output-on-failure
```

A substantial common/driver change should be expanded across all four emulator targets and the affected console toolchains. Keep `git diff --check` clean and never stage runtime ROM/BIOS/cache/save data from `resources/`.
