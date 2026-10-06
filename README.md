# NJEMU - Multi-Platform Arcade Emulator

NJEMU is an open-source emulator for classic Capcom and SNK arcade hardware. It started as a PSP project and now shares one C codebase across PSP, PlayStation 2, PlayStation Vita, and Desktop.

The project emulates:

- Capcom CPS1
- Capcom CPS2
- Neo Geo MVS/AES
- Neo Geo CD (NCDZ)

NJEMU focuses on keeping the original low-memory console targets practical while sharing emulator policy, GUI code, rendering semantics, metadata, configuration, and tooling across platforms.

## Features

- CPS1, CPS2, MVS/AES, and Neo Geo CD emulation
- Common GUI/menu frontend plus a `GUI=OFF` launch path
- Save states and save-state previews
- Externalized game metadata and generated localization packs
- Runtime-selectable graphics backends where a platform provides more than one
- Streaming cache support for memory-constrained CPS2/MVS configurations
- Platform-specific rendering, audio, input, threading, timing, and memory optimizations behind common driver contracts
- Browser-based MVS/CPS2 ROM conversion tool for processed/cache assets

Optional or platform-specific capabilities are documented separately and are not implied to be available on every core/platform combination.

## Supported platforms

| Platform | Status | Notes |
| --- | --- | --- |
| PSP | Supported | Native GU backend; runtime memory planning; optional PSP-specific features |
| PlayStation 2 | Supported | Native gsKit/PS2SDK backend; configurable output modes |
| PlayStation Vita | Supported | Native GXM/vita2d and VitaGL backends are built together and selected at runtime |
| Desktop | Supported | SDL2 backend with OpenGL 3.3 available in the normal full build |

All four emulator cores use the common frontend and platform-driver architecture. Platform-specific feature differences are described in [docs/PLATFORMS.md](docs/PLATFORMS.md).

## Downloads

Ready-to-run release packages are published through the NJEMU project site:

**[NJEMU downloads and ROM converter](https://fjtrujy.github.io/NJEMU/)**

The site offers both the latest stable SemVer release and an automatically refreshed Development build from `master`. Official packages use one recommended full configuration per platform rather than exposing the CI option matrix. ROMs, BIOS files, caches, processed game data, saves, and other user-owned runtime data are not included.

## Building

NJEMU uses CMake and requires both an emulator target and a host platform. For example, a Desktop MVS build is:

```bash
cmake -S . -B build-desktop-mvs -DPLATFORM=DESKTOP -DTARGET=MVS -DGUI=ON
cmake --build build-desktop-mvs --parallel
cmake --install build-desktop-mvs
```

See [docs/BUILDING.md](docs/BUILDING.md) for prerequisites, toolchains, supported options, platform examples, canonical release configurations, and install/package behavior.

## Documentation

- [User guide](docs/USER_GUIDE.md) - controls, ROM setup, compatibility, and ROM conversion
- [Building NJEMU](docs/BUILDING.md) - toolchains, CMake options, builds, and install outputs
- [Platform notes](docs/PLATFORMS.md) - platform-specific capabilities and differences
- [Runtime files and assets](docs/RUNTIME_FILES_AUDIT.md) - authoritative external-file contract
- [Memory and cache notes](docs/MEMORY_AND_CACHE.md) - memory-constrained runtime background
- [Architecture reference](docs/ARCHITECTURE.md) - emulator internals and shared driver architecture
- [Platform porting guide](docs/PLATFORM_PORTING_GUIDE.md) - adding or maintaining host backends
- [Development guide](docs/DEVELOPMENT.md) - repository structure, validation, and contribution workflow
- [Release process](docs/RELEASING.md) - Semantic Versioning, tags, canonical packages, and Pages publication
- [Changelog](CHANGELOG.md) - release history from the SemVer transition onward

The planning/audit documents under `docs/` remain useful for implementation history, measurements, and focused subsystem work. They are not substitutes for the user-facing guides above.

## Contributing and bug reports

Contributions are welcome. Please read [docs/DEVELOPMENT.md](docs/DEVELOPMENT.md) before opening a pull request.

GitHub issue forms are provided for bugs, performance problems, and feature requests. Reproducible emulator bugs or performance problems that occur after gameplay starts should include a save state captured as close as possible to the problem so maintainers can reproduce the exact scene quickly.

## Credits

NJEMU is based on NJ's original PSP emulator work and the contributions that followed it, including MAME reference implementations and community improvements. The current repository extends that codebase with the multi-platform driver architecture, CMake builds, additional platform backends, metadata/tooling work, and the browser ROM converter.

## License

NJEMU is licensed under the GNU General Public License v3.0. See [Licence.txt](Licence.txt).

NJEMU does not include copyrighted ROMs or BIOS files. Users must provide legally obtained game/system data where required.
