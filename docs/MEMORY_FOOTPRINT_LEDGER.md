# NJEMU Cumulative Memory-Footprint Ledger

Status: initial measurements, 2026-10-10. The production external-icon switch is **OFF**.

## What to measure

Track each optimization in bytes, even if the immediate saving is smaller than
one cache block. Do not equate an ELF file-size change with reclaimable memory.

| Metric | Meaning |
| --- | --- |
| `text_exec` | Sum of allocated, non-writable, executable ELF sections |
| `rodata_readonly` | Allocated, non-writable, non-executable sections |
| `data_writable` | Initialized writable allocated sections |
| `bss_zero_init` | Allocated zero-initialized sections |
| `allocated_total` | Sum of these section classes, excluding alignment gaps |
| `load_file_total` | Sum of ELF `PT_LOAD` file sizes |
| `load_memory_total` | Sum of ELF `PT_LOAD` memory sizes, including alignment/gaps |
| `elf_file` | Full ELF file size, **including debugging/nonresident sections** |

The first four categories are defined using ELF flags and section types,
including the corresponding section variants (for example `.sdata`, `.sbss`,
or allocated machine metadata), rather than only literal `.text`, `.data`,
`.rodata`, and `.bss` names. This avoids silently losing allocated bytes.

`load_memory_total` is the preferred **linked image footprint** measure;
it is not an allocation-probe result and does not describe all dynamic RAM
allocations. Comparing these metrics requires matching builds (same target,
platform, toolchain, flags, libraries, and release/debug configuration).

## Reproduce the measurement

The existing C99 host tool now has an `elf-footprint` subcommand, including
ELF32/64 and little/big-endian input support. Build and run it on the host:

```sh
cmake -S tools/host -B build-footprint-host -DCMAKE_BUILD_TYPE=Release
cmake --build build-footprint-host -j4

build-footprint-host/njemu-tool elf-footprint BASELINE.elf
build-footprint-host/njemu-tool elf-footprint BASELINE.elf CANDIDATE.elf
build-footprint-host/njemu-tool elf-footprint BASELINE.elf CANDIDATE.elf --csv
```

On PSP use the **unstripped** `MVS` ELF from the build directory, not
`EBOOT.PBP` or a stripped PRX. On PS2 use `MVS.ELF`.
A positive `saved_bytes` means the candidate occupies fewer bytes;
a negative value means it grew.

For a controlled A/B build, retain the embedded baseline binary and record
every CMake option before compiling the candidate. Build output paths are
local and are not required by release packaging. Do not alter `resources/`.

## First A/B entry: external menu-category icons

Source commit: `2f3d3543` (experimental implementation).
Both measured variants are MVS GUI with `USE_CACHE=ON`,
`SAVE_STATE=OFF`, `PSP_ME_AUDIO=OFF`; only
`NJEMU_EXTERNAL_MENU_ICONS` differs. The external build remains optional.

| Bytes | PSP embedded | PSP external | PSP saving | PS2 embedded | PS2 external | PS2 saving |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Executable allocated | 801,964 | 803,196 | -1,232 | 945,128 | 946,608 | -1,480 |
| Read-only allocated | 83,212 | 76,668 | +6,544 | 88,536 | 82,008 | +6,528 |
| Writable initialized | 13,700 | 13,700 | 0 | 409,932 | 409,932 | 0 |
| Zero-initialized | 1,877,796 | 1,877,796 | 0 | 1,648,176 | 1,648,176 | 0 |
| **PT_LOAD resident** | **2,776,744** | **2,771,432** | **+5,312** | **3,092,144** | **3,087,024** | **+5,120** |

The category data removed is 6,624 bytes. The runtime loader adds code, so
the net resident image saving is smaller. In the external variant,
`menu_icons.bin` is temporarily loaded into a 6,624-byte heap buffer while
the in-game menu is open and freed on exit.

`load_memory_total` includes image alignment. The previously reported
binutils `size` summary used a different classification and reported 5,584
and 5,320 bytes saved in its text column. Do **not** add those values to the
PT_LOAD savings: they describe the same change, using different accounting.
The full ELF file may even grow because of debug information.

### New experiment accounting (excluding earlier optimizations)

| Optimization | PSP image saving | PS2 image saving | Production status |
| --- | ---: | ---: | --- |
| External menu-category icons | 5,312 B | 5,120 B | Experimental, default OFF |
| **Realized savings from this icon experiment** | **0 B** | **0 B** | Not yet enabled |
| **Validated linked-image potential** | **5,312 B** | **5,120 B** | No runtime-cache claim |

Each future optimization should add its own row with a commit, build
configuration, `load_memory_total` delta, and runtime result. Recompute the
cumulative total against the **same original baseline** when multiple
optimizations are combined; individually measured deltas may overlap or
change with link layout.

## Previously completed: PS2 external IRX images

The PS2 IRX optimization is **already implemented**, not a future candidate.
`PS2_EXTERNAL_IRX_IMAGE=ON` links against `ps2_drivers_img` instead of
embedding the large IRX payload arrays in the game ELF. The standalone
bootstrap provides the filesystem drivers needed to read
`ps2_drivers.irximg` before NJEMU performs its IOP reset.

The earlier representative MVS audit in `docs/BINARY_SIZE_AUDIT.md`
reported a **375,408-byte reduction** in the GNU `size` runtime-image
summary (3,218,336 down to 2,842,928 bytes). This is a *historical,
separately measured* improvement, not an additional saving from the recent
menu-icon experiment. Its measurement method and build configuration differ
from the PT_LOAD baseline above: do not sum the numbers without rebuilding
a common baseline.

`NJEMU_CANONICAL_RELEASE=ON` already forces this external-driver format for
cached PS2 **CPS2 and MVS** releases, whereas **CPS1 and NCDZ** continue to
ship their classic embedded-driver ELFs. The normal developer-build default
remains OFF. See `cmake/NJEMUReleasePackage.cmake` and
`.github/workflows/release.yml` for the deployed formats.

No new IRX externalization work is necessary. Future work in this area
should focus on runtime-cache and boot-loader validation, not duplicating
the existing implementation.

## Runtime cache validation (a separate gate)

PS2 MVS Auto uses **16 KiB read requests**; PSP MVS Auto currently uses
**64 KiB reads**. Both retain **64 KiB allocation slots**. These are independent
quantities. A few KiB saved today may matter after later improvements even if
the allocator's block count does not change immediately.

For each physical hardware comparison, capture one controlled ROM load
with the same game, BIOS, configuration, emulator audio mode, and build flags.
Keep separate logs and record the emitted `[memory_plan]` fields:

- `probed` and `largest_alloc` (both are allocator-derived measurements).
- `reserve`, `tier`, `budget`, `gfx`, `pcm`, and `unused`.
- Cache-slot counts and demand-read size from the cache initialization log.

Report each build and repeat enough times to distinguish allocator variability.
A claim of additional cache capacity requires observed cache allocation
growth, not just a smaller `PT_LOAD` image. Existing PCSX2 and PPSSPP logs
can guide tests, but physical PSP and PS2 measurements are preferred.

## Candidates for future audits

Large renderer/CPU tables and persistent buffers should be
assessed for actual lifetime and alignment before changing their ownership.
Avoid trading link-time data for equivalent permanent heap allocations.

Prioritize future work by **measured PT_LOAD and actual cache changes**,
rather than only source-file size. Keep existing PSP/PS2/Desktop functionality
and runtime packaging intact.

## Promotion gate for the external-icon option

The original icon and loading/progress paths remain embedded, but the
experimental category icons still require work before default enablement:

1. Integrate the 6,624-byte pack into PSP/PS2/Desktop distribution and launch
   layouts, with deterministic C-based generation.
2. Validate the in-game menu and settings icons on PPSSPP, PCSX2, and hardware.
3. Define a visible fallback for missing or corrupted packs; currently the
   four category icons disappear and an error is logged.
4. Record repeated ROM cache probe results using matched binaries and ROMs.
5. Re-measure the combined size/RAM impact if other static data is externalized.

Until then, `NJEMU_EXTERNAL_MENU_ICONS=OFF` is the safe production default.
