# Supported Platforms

NJEMU builds the same four emulator cores for PSP, PlayStation 2, PlayStation Vita, and Desktop. Common emulator policy, menus, metadata, configuration, save-state logic, and target renderers are shared; host backends provide native video, audio, input, threading, timing, lifecycle, filesystem, and optional device capabilities.

## Platform matrix

| Target | PSP | PlayStation 2 | PlayStation Vita | Desktop |
| --- | --- | --- | --- | --- |
| CPS1 | Supported | Supported | Supported | Supported |
| CPS2 | Supported | Supported | Supported | Supported |
| MVS/AES | Supported | Supported | Supported | Supported |
| NCDZ | Supported | Supported | Supported | Supported |

“Supported” means the emulator core and common frontend build for that host. Optional features can still be platform/core-specific.

## PSP

- Native PSP GU video backend.
- Native PSP audio/input/thread/timing support.
- One EBOOT requests the expanded PSP user-memory partition and adapts its allocation policy to the memory actually available at runtime; there is no separate legacy “Slim” build.
- CPS2 and MVS compile streaming-cache support by default because constrained games may need it.
- PSP MVS has an optional Ad Hoc build capability.
- Experimental Media Engine audio support exists behind `PSP_ME_AUDIO`; the canonical release keeps it disabled so the same EBOOT remains usable in PPSSPP.

## PlayStation 2

- Native gsKit/PS2SDK rendering and native platform drivers.
- CPS2 and MVS compile streaming-cache support by default.
- Accelerated cache I/O is available for supported cached configurations.
- Output modes include 240p, 480i, and 480p, with global runtime configuration after startup.
- The normal build uses the embedded driver-image path. The external-IRX-image path remains a developer/validation variant rather than the recommended distribution.

## PlayStation Vita

- Every Vita build contains both graphics backends:
  - native GXM/vita2d;
  - VitaGL.
- The global `Video backend` setting selects `Auto`, `GXM`, or `VitaGL` at runtime.
- Vita defaults CPS2/MVS to full-resident operation (`USE_CACHE=OFF`), while cache-enabled variants remain available for development/compatibility validation.
- Packaged application assets live in `app0:` and are copied into the writable runtime root under `ux0:data/<target>/` when required.

## Desktop

- SDL2 is the baseline backend.
- OpenGL 3.3 is compiled alongside SDL in the normal build and selected at runtime.
- Desktop is the main host for unit/integration tests and development diagnostics such as ASan/UBSan.
- CPS2/MVS default to full-resident operation; streaming-cache support can still be enabled explicitly.

## Platform-independent runtime behavior

Across supported hosts:

- `START + SELECT` opens the emulator menu during gameplay when the frontend is available;
- generated localization catalogs and the external GBK font are runtime assets;
- the same generated game metadata/database formats are used;
- save states, per-game settings, NVRAM/memory-card behavior, and ROM/cache lookup are owned by common/core code unless a platform capability genuinely differs;
- target sprite renderers stay platform-neutral and emit data through the shared video-driver contract.

See [BUILDING.md](BUILDING.md) for toolchains/options and [RUNTIME_FILES_AUDIT.md](RUNTIME_FILES_AUDIT.md) for the exact external-file layout.
