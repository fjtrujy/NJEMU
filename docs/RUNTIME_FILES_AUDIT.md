# Runtime Files and Assets Audit

## Purpose

This document is the authoritative inventory of files that NJEMU reads, writes,
generates, stages, packages, or expects outside the emulator executable.

The inventory is derived from runtime source code, converter code, and CMake
staging/packaging rules. The contents of `resources/` are useful validation
data, but they are not treated as the source of truth because that tree can
contain local ROMs, BIOS files, caches, saves, and other intentionally
untracked data.

The main goals are:

- make the runtime contract explicit for CPS1, CPS2, MVS, and NCDZ;
- distinguish distributed assets from user-supplied and generated files;
- make platform-specific packaging requirements explicit;
- document all accepted legacy/alternative layouts before removing any of
  them;
- identify duplication that can be removed safely;
- prevent local ROMs, BIOS files, caches, saves, or configuration from being
  copied into release/install outputs accidentally.

## Rules

- Never infer a runtime requirement only from a file present under
  `resources/`.
- A file is considered mandatory only when the runtime path requires it for
  the selected build/core/use case.
- Identical bytes do not imply that two files can be consolidated. Lookup
  paths and ownership must be proven first.
- ROMs, BIOS data, processed game data, caches, configuration, save states,
  NVRAM, memory cards, screenshots, and other runtime-generated files are not
  source assets.
- Generated translation/font files are build artifacts even though they become
  mandatory runtime assets in packaged builds.

## File classes

| Class | Meaning |
| --- | --- |
| Mandatory runtime file | Required for the applicable core/build path to initialize or boot a game. |
| Optional runtime file | Used when present or when an optional feature is enabled; absence has a defined fallback. |
| Game ROM / BIOS | User-provided ROM or BIOS data. Never distributed by NJEMU. |
| Processed/generated game asset | Produced offline from ROM data and consumed by the emulator. Not runtime mutable. |
| Cache | Derived data used by a streaming runtime path. |
| Configuration | User-editable or emulator-generated INI/settings data. |
| Save/NVRAM/runtime data | Mutable emulator output such as save states, NVRAM, memory cards, or temporary files. |
| Translation/font/UI asset | UI data generated or packaged independently from game ROM data. |
| Platform support file | File required by a platform launch/bootstrap path rather than by an emulation core. |
| Build-time-only asset | Used to build/package an executable but not opened by the emulator at runtime. |
| Documentation-only asset | Shipped as reference material but never opened by runtime code. |

## Runtime root model

Most runtime paths are relative to `launchDir`.

| Platform | Effective runtime root | Source |
| --- | --- | --- |
| Desktop | Process current working directory. | `src/emumain.c`, `src/desktop/desktop_platform.c` |
| PSP | Directory containing the launched EBOOT/ELF, with PSP/PPSSPP path handling. | `src/emumain.c`, `src/psp/psp_platform.c` |
| PS2 | Process current working directory, normally the ELF directory exposed as the loader/PCSX2 root. | `src/emumain.c`, `src/ps2/ps2_platform.c` |
| PS Vita | `ux0:data/<target>/`; packaged app resources are copied from `app0:` on first/missing-file use. | `src/psvita/psvita_platform.c` |

The build tree mirrors the same layout from an explicit per-core staging set.
With `COPY_RESOURCES=OFF`, CMake symlinks known read-only entries from
`resources/<core>/` into the build root and makes private copies of known
mutable entries. With `COPY_RESOURCES=ON`, it copies that same explicit set
instead of broadening staging to the complete validation tree.

### Mutable build-tree entries

The current CMake staging code treats these names as mutable:

- `config/`
- `memcard/`
- `nvram/`
- `state/`
- `game_name.ini`
- `njemu.ini`
- `command.dat`
- `backup.bin`

This list describes staging policy, not necessarily a requirement for every
core.

## Common runtime files

### Translation catalogs

Canonical runtime paths:

~~~text
lang/en.lng
lang/ja.lng
lang/es.lng
lang/zh-Hans.lng
lang/zh-Hant.lng
~~~

Classification: generated translation/UI asset.

Generation:

- `tools/build_translations.py`
- input schema: `translations/messages.def`
- language sources: `translations/*.lang`
- generated into `\${CMAKE_BINARY_DIR}/lang/`

Runtime owner:

- `src/common/ui_text_driver.c:common_text_init()`
- `src/common/ui_text_catalog.c:ui_text_catalog_load()`

Rules:

- the catalog matching the platform language is attempted first;
- English is the fallback;
- if English cannot be loaded, UI text initialization fails;
- therefore `lang/en.lng` is mandatory on every platform/core;
- the other catalogs are optional individually, but are generated and packaged
  by the normal build.

The generated `generated/ui_unicode_glyph.c` is compiled into the executable
and is build-time-only, not a runtime external file.

### External GBK font

Canonical runtime path:

~~~text
font/gbk_s14.bin
~~~

Classification: generated font/UI asset.

Generation:

- `tools/build_font_asset.py`
- source data: `src/common/font/gbk_s14.c`
- mapping data: `src/common/font/gbk_tbl.c`

Runtime owner:

- `src/common/font/gbk_s14_runtime.c:gbk_s14_font_init()`
- called by `src/common/ui_draw.c:ui_init()`

The font is currently required by the regular UI renderer, including no-GUI
builds because `ui_draw.c` remains a runtime service for overlays and loading
UI. Packaging now installs/includes it unconditionally for the same reason;
source inclusion and runtime initialization are not conditional on `GUI`.

Other small/ASCII/Latin fonts in `src/common/font/*.c` are compiled into the
executable and are not external runtime files.

### Global configuration

Canonical runtime path:

~~~text
njemu.ini
~~~

Classification: configuration/runtime-generated data.

Runtime owner:

- `src/common/config.c:load_settings()`
- `src/common/config.c:save_settings()`

It is optional on first launch. Defaults are used and the file is created when
missing. An INI version mismatch can delete old per-game configuration and
NVRAM before rewriting defaults.

### Per-game configuration

Canonical runtime path:

~~~text
config/<game>.ini
~~~

Classification: configuration/runtime-generated data.

Runtime owner:

- `src/common/config.c:load_gamecfg()`
- `src/common/config.c:save_gamecfg()`

It is optional on first launch and is generated from defaults when absent.

### No-GUI game selector

Canonical runtime path:

~~~text
game_name.ini
~~~

Classification: configuration.

Runtime owner:

- `src/common/no_gui.c:file_browser()`

This is required to choose a game in the current no-GUI workflow. It is not a
ROM database and is safe to edit per build/runtime installation.

Release/install packaging generates a fresh empty selector in the build runtime
layout instead of copying `resources/<core>/game_name.ini`. Local development
build staging can still copy the resource-tree file so developers may keep a
private selection for runtime testing without affecting packaged output.

### Command list

Canonical runtime path:

~~~text
command.dat
~~~

Classification: optional runtime data.

Runtime owner:

- `src/common/cmdlist.c:load_commandlist()`

It is only relevant when `COMMAND_LIST` is compiled. Missing data disables the
command-list content rather than preventing emulation.

The command-list size-reduction UI also uses the core's base
`zipname.<core>` file (with `zipnamej.<core>` as a legacy fallback for that
operation).

### Cheats

Canonical runtime family:

~~~text
cheats/<game>.ini
~~~

Classification: optional runtime data.

Runtime owner: `src/common/ui_menu.c`.

Cheat files may include other cheat INI files. They are user-supplied data and
are not mandatory for emulation.

### Save states

Canonical runtime family:

~~~text
state/<game>.sv0
...
state/<game>.sv9
~~~

Classification: save/runtime-generated data.

Runtime owner:

- `src/common/state.c`
- `src/common/filer.c:find_state_file()`

When streaming cache and save-state support are both active, NJEMU also uses:

~~~text
state/cache.tmp
~~~

This is a temporary runtime-generated backup and must never be distributed.

## Common arcade ROM lookup: CPS1, CPS2, and MVS

`src/common/loadrom.c:file_open()` searches ROM ZIPs by CRC in this order:

1. `<game_dir>/<requested game>.zip`
2. `<game_dir>/<parent-or-BIOS>.zip`
3. `<launchDir>/roms/<parent-or-BIOS>.zip`

This means parent sets and MVS BIOS data can be resolved from the selected game
directory, with `launchDir/roms/` as the common fallback.

ROM members are identified primarily by CRC. Their member filenames are not
the source of truth.

## GUI game-name databases: CPS1, CPS2, and MVS

Canonical files:

~~~text
zipname.<core>
zipnamej.<core>
zipnamech1.<core>
zipnamech2.<core>
~~~

where `<core>` is `cps1`, `cps2`, or `mvs`.

Classification: distributed runtime metadata.

Runtime owner: `src/common/filer.c:load_zipname()`.

Rules:

- `zipname.<core>` is the mandatory fallback for GUI file browsing;
- Japanese, Simplified Chinese, and Traditional Chinese variants are optional
  localized alternatives;
- if the requested localized file is absent, the base file is used;
- these files are not required for the normal no-GUI game selector.

## ROM metadata databases: CPS1, CPS2, and MVS

Canonical files:

~~~text
rominfo.cps1
rominfo.cps2
rominfo.mvs
~~~

Classification: mandatory distributed runtime metadata.

Runtime owners are the respective core `memintrf.c:load_rom_info()`
implementations. They define supported sets, parent relationships, region
sizes, CRCs, machine/input/init information, and ROM layout.

The matching `romcnv` tools also consume the CPS2/MVS databases while
building derived assets.

## CPS1

### Mandatory inputs

| Path/family | Class | Required when | Owner |
| --- | --- | --- | --- |
| `rominfo.cps1` | Runtime metadata | Any game boot | `src/cps1/memintrf.c` |
| selected/parent `*.zip` | Game ROM | Any game boot | `src/common/loadrom.c` |
| `zipname.cps1` | Runtime metadata | GUI browser | `src/common/filer.c` |
| `lang/en.lng` | Generated UI asset | Any normal runtime | common text catalog |
| `font/gbk_s14.bin` | Generated UI asset | Current UI renderer initialization | common UI draw |

CPS1 has no streaming cache implementation. A `cache/` directory under a
CPS1 resource tree is not a CPS1 runtime requirement.

### Mutable/optional data

- `njemu.ini`
- `game_name.ini` for no-GUI selection
- `config/<game>.ini`
- `nvram/<game>.nv`
- `state/<game>.svN` when save states are enabled
- `cheats/<game>.ini`
- `command.dat` when command lists are enabled
- localized `zipname*.cps1` variants

There is no CPS1 BIOS requirement analogous to MVS. A `neogeo.zip` present
inside a local CPS1 ROM directory is unrelated local data, not a CPS1 asset.

## CPS2

### Mandatory inputs

| Path/family | Class | Required when | Owner |
| --- | --- | --- | --- |
| `rominfo.cps2` | Runtime metadata | Any game boot | `src/cps2/memintrf.c` |
| selected/parent `*.zip` | Game ROM | Resident regions and full-resident GFX | `src/common/loadrom.c` |
| `zipname.cps2` | Runtime metadata | GUI browser | `src/common/filer.c` |
| `lang/en.lng` | Generated UI asset | Any normal runtime | common text catalog |
| `font/gbk_s14.bin` | Generated UI asset | Current UI renderer initialization | common UI draw |

### CPS2 cache formats

Classification: offline-generated cache.

Generator: `romcnv_cps2` in `romcnv/src/cps2/romcnv.c`.

Canonical runtime root:

~~~text
cache/
~~~

Accepted layouts, in runtime priority order:

~~~text
cache/<game>.cache
cache/<game>_cache.zip
cache/<game>_cache/
    cache_info
    000
    001
    ...
~~~

Parent-cache fallbacks are supported where the core selects a parent cache.
ZIP and folder block names are three lowercase hexadecimal digits.

The cache is **conditional**, not unconditionally mandatory on PSP/PS2:

1. the current memory planner probes for a fully resident GFX allocation;
2. if the full decoded GFX region fits, CPS2 loads and decodes the original ROM
   ZIPs directly;
3. on a `USE_CACHE=ON` build, a failed/partial resident plan can fall back to
   the cache;
4. a `USE_CACHE=OFF` build has no streaming fallback and therefore requires
   the full resident allocation.

CMake defaults `USE_CACHE=ON` for CPS2 on PSP and PS2 and defaults it OFF on
Desktop and PS Vita.

### Mutable/optional data

- `njemu.ini`
- `game_name.ini` for no-GUI selection
- `config/<game>.ini`
- `nvram/<game>.nv`
- `state/<game>.svN`
- `state/cache.tmp` during save-state operations on a streaming-cache build
- `cheats/<game>.ini`
- `command.dat`
- localized `zipname*.cps2` variants

## MVS

### ROM/BIOS inputs

| Path/family | Class | Required when | Owner |
| --- | --- | --- | --- |
| `rominfo.mvs` | Runtime metadata | Any game boot | `src/mvs/memintrf.c` |
| selected/parent `*.zip` | Game ROM | Game boot | `src/common/loadrom.c` |
| `neogeo.zip` | BIOS ROM archive | MVS game boot | `src/mvs/biosmenu.c`, MVS memory loader |
| `zipname.mvs` | Runtime metadata | GUI browser | `src/common/filer.c` |

The MVS BIOS archive must provide compatible BIOS content plus the system FIX
ROM and low ROM by CRC. In particular, the loader expects:

- `sfix.sfx` content with CRC `c2ea0cfd`;
- `000-lo.lo` content with CRC `5a86cff2`;
- one supported MVS/AES/UniBIOS BIOS image selected by the BIOS menu/runtime
  configuration.

The archive is normally `roms/neogeo.zip`, although the common ROM lookup can
also resolve it from the selected game directory. MVS does **not** require a
top-level `000-lo.lo`.

### MVS processed assets

Classification: offline-generated processed/decrypted game assets.

Generator: `romcnv_mvs` in `romcnv/src/mvs/romcnv.c`.

Canonical root:

~~~text
processed/
~~~

Canonical formats:

~~~text
processed/<game>_cache/
    cache_info
    crom
    srom        # only when required by that set
    vrom        # only when required by that set/runtime sound path

processed/<game>_cache.zip
    cache_info
    000
    001
    ...         # C-ROM blocks
    srom        # when required
    vrom        # when required
~~~

Despite historical `*_cache` naming, these are processed ROM assets rather
than disposable runtime cache files. They can be required even by a
full-resident path for encrypted sets.

Runtime owner:

- `src/mvs/processed_assets.c`
- `src/mvs/memintrf.c`
- `src/common/cache.c` for streaming access

Parent processed assets can be selected independently for C-ROM, S-ROM, and
V-ROM according to the ROM metadata/conversion policy.

### Legacy MVS processed-asset path

Cache-enabled builds currently accept:

~~~text
cache/<game>_cache/
cache/<game>_cache.zip
~~~

as a compatibility fallback when no matching assets exist under
`processed/`.

`src/mvs/processed_assets.c:mvs_processed_asset_root()` deliberately does
**not** consult this legacy `cache/` root in `USE_CACHE=OFF` builds.

Therefore:

- `processed/` is the canonical location;
- `cache/` is a migration compatibility path, not a second canonical source;
- it must not be removed until installations using the legacy layout have a
  documented migration path;
- duplicated processed assets in both roots are unnecessary once migration is
  complete, but byte equality alone is not sufficient proof for deletion.

### Mutable/optional MVS data

- `njemu.ini`
- `game_name.ini` for no-GUI selection
- `config/<game>.ini`
- `memcard/<game>.bin`
- `nvram/<game>.nv`
- `state/<game>.svN`
- `state/cache.tmp` for streaming-cache save-state handling
- `cheats/<game>.ini`
- `command.dat`
- localized `zipname*.mvs` variants

## NCDZ

### BIOS files

NCDZ requires two top-level BIOS/support ROM files:

~~~text
neocd.bin
000-lo.lo
~~~

Classification: user-supplied BIOS ROM data.

Runtime owner: `src/ncdz/memintrf.c:load_bios()`.

Validated content:

| File | Bytes read | Expected CRC |
| --- | ---: | --- |
| `neocd.bin` | `0x80000` | `df9de490` |
| `000-lo.lo` | `0x20000` | `5a86cff2` |

The GUI performs an earlier `neocd.bin` validation in
`src/common/filer.c:check_neocd_bios()`; the actual core initialization still
loads both files.

The current local `resources/ncdz/` validation tree contains an untracked
`neocd.bin` but no `000-lo.lo`. That is a local validation gap, not evidence
that `000-lo.lo` is optional.

### NCDZ game data

NCDZ currently opens game content through `src/ncdz/resource_source.c` as
either:

- a directory; or
- a ZIP archive.

`IPL.TXT` is mandatory for a normal game boot. It identifies the game and
drives the set of files loaded by `src/ncdz/cdrom.c`. Runtime-recognized data
types include PRG, FIX, SPR, Z80, PCM, PAT, and Axx files.

The current runtime does not mount ISO/BIN/CUE disc images. Documentation that
describes those image formats as directly loadable is historical/stale unless
another loader is added.

Optional loading resources such as region-specific `LOGO_*.PRG` files are
loaded from the selected directory/ZIP when present.

### NCDZ audio tracks

Classification: game media.

The CDDA layer searches the configured MP3 directory for names containing the
two-digit track pattern:

~~~text
02.mp3
03.mp3
...
~~~

For a directory-backed game, the normal layout is:

~~~text
roms/<game>/mp3/
~~~

For a ZIP-backed game, both GUI and no-GUI paths use an `mp3/` directory
beside the ZIP rather than inside it:

~~~text
roms/<game>.zip
roms/mp3/
~~~

MP3 tracks are optional for boot but required for CDDA audio when MP3 playback
is enabled. Missing tracks acknowledge the CDDA command without playback.

### Optional loading image

Canonical runtime path:

~~~text
data/loading.png
~~~

Classification: optional UI asset.

Runtime owner: `src/ncdz/cdrom.c:show_loading_image()`.

If absent, the runtime displays a text-only loading message.

### NCDZ mutable data

- `njemu.ini`
- `game_name.ini` for no-GUI selection
- `config/<game>.ini`
- `backup.bin` Neo Geo CD backup memory, generated/updated by the emulator
- `state/<game>.svN`
- `command.dat` if command lists are compiled

NCDZ does not use CPS2/MVS streaming cache files.

## Platform-specific files

### Desktop

No platform-specific external support binary is required. SDL and other
libraries are build/link dependencies.

Runtime assets live beside the executable according to the common
`launchDir` layout.

### PSP

`data/<core>.png` is used by CMake as the EBOOT/PBP icon. It is a
build-time/package asset and is not opened by NJEMU at runtime.

PSP system/network modules loaded by the executable are platform facilities,
not files distributed from this repository.

### PS2

There are two support-driver packaging modes.

#### Embedded/default mode

The executable links the normal `ps2_drivers` library. There is no external
IRX image file in the NJEMU runtime directory.

#### External IRX image mode

With `PS2_EXTERNAL_IRX_IMAGE=ON`, CMake requires installed ps2_drivers
artifacts and stages:

~~~text
BOOT.ELF
<core>
elf_path.ini
ps2_drivers.irximg
~~~

Classification:

- `BOOT.ELF`: platform launcher/bootstrap;
- `elf_path.ini`: generated platform bootstrap configuration;
- `ps2_drivers.irximg`: platform support image;
- `<core>`: NJEMU executable.

CMake locates the image and bootstrap in the installed PS2SDK ports tree and
copies/generates the runtime files. The PS2 runtime uses the ps2_drivers image
API from `src/ps2/ps2_platform.c` and `src/ps2/ps2_input.c`.

These files are platform support artifacts, not per-core resources and not
assets that belong under `resources/`.

### PS Vita

The VPK always contains both GXM/vita2d and vitaGL runtime backends. No
external shader compiler/runtime asset is required for normal rendering:
compiled GXP shader blobs are embedded in `src/psvita/psvita_shaders.h`.

The Cg sources and vitaGL shader cache path documented in that header are a
shader-regeneration workflow, not a normal runtime requirement.

The VPK packages:

- translation packs;
- the same explicit target distribution manifest used by `cmake --install`;
- generated `_placeholder` files for the runtime directories relevant to the
  selected target/options;
- `font/gbk_s14.bin` for both GUI and no-GUI builds.

The VPK manifest does not glob `resources/<target>/`, so local ROMs, BIOS
files, processed assets, caches, saves, NVRAM, and configuration cannot be
included merely because they exist in the local validation tree.

At runtime, `src/psvita/psvita_platform.c` copies missing packaged resources
from `app0:` to `ux0:data/<target>/` and then uses that writable directory
as `launchDir`.

Optional Vita diagnostic files outside the core runtime root include:

~~~text
ux0:data/njemu_video.log
ux0:data/njemu_dump_frames.txt
ux0:data/njemu_dumps/*.ppm
~~~

They are debug/profiling input/output, not distributed runtime requirements.

## Build-time and documentation-only files

### Supported-game lists

Tracked files:

~~~text
resources/cps1/gamelist_cps1.txt
resources/cps2/gamelist_cps2.txt
resources/mvs/gamelist_mvs.txt
~~~

Classification: documentation-only assets.

No runtime or CMake loader references them. The root README currently points
to `docs/gamelist_*.txt`, which does not match their actual tracked location
and should be corrected.

### Resource README files and placeholders

`resources/<core>/README.md` files are documentation only.

`_placeholder` files exist to preserve otherwise-empty runtime directories in
source/package layouts. They are packaging structure, not runtime data.

Some historical placeholder directories are broader than the current runtime
needs. They should be removed only after documentation/install manifests no
longer depend on them; no cleanup should be inferred from their presence alone.

### Legacy SystemButtons file

`data/SystemButtons/SystemButtons.prx` is still tracked according to Git, but
the current source/CMake search finds no active loader or packaging rule for
it; the only textual reference found so far is historical README material.

Status: **candidate historical build artifact; do not delete yet**.

Before deletion, verify repository attributes/sparse state, old PSP packaging
requirements, and release compatibility.

## Core/platform requirement matrix

Legend:

- M = mandatory for the described runtime path
- C = conditional
- O = optional
- G = generated/mutable
- B = build/package-time only
- -- = not used by that core/path

| Item | CPS1 | CPS2 | MVS | NCDZ |
| --- | --- | --- | --- | --- |
| `rominfo.<core>` | M | M | M | -- |
| `zipname.<core>` | C: GUI | C: GUI | C: GUI | -- |
| localized `zipname*` | O | O | O | -- |
| arcade ROM ZIPs | M | M | M | -- |
| `roms/neogeo.zip` | -- | -- | M | -- |
| `neocd.bin` | -- | -- | -- | M |
| top-level `000-lo.lo` | -- | -- | -- | M |
| NCDZ directory/ZIP with `IPL.TXT` | -- | -- | -- | M |
| CPS2 `cache/` assets | -- | C: streaming fallback | -- | -- |
| MVS `processed/` assets | -- | -- | C: set/runtime dependent | -- |
| legacy MVS `cache/` processed assets | -- | -- | C: cache-build compatibility only | -- |
| `lang/en.lng` | M | M | M | M |
| `font/gbk_s14.bin` | M* | M* | M* | M* |
| `njemu.ini` | G/O | G/O | G/O | G/O |
| `game_name.ini` | C: no-GUI | C: no-GUI | C: no-GUI | C: no-GUI |
| `config/<game>.ini` | G/O | G/O | G/O | G/O |
| `command.dat` | O | O | O | O |
| `cheats/<game>.ini` | O | O | O | O |
| `state/<game>.svN` | G/O | G/O | G/O | G/O |
| NVRAM | G/O | G/O | G/O | -- |
| MVS memory card | -- | -- | G/O | -- |
| NCDZ `backup.bin` | -- | -- | -- | G/O |
| NCDZ MP3 tracks | -- | -- | -- | O |
| NCDZ `data/loading.png` | -- | -- | -- | O |

`M*`: required by current common UI-renderer initialization; packaging
conditions still need to be reconciled as noted above.

Platform overlays:

| Item | Desktop | PSP | PS2 | PS Vita |
| --- | --- | --- | --- | --- |
| Common launchDir tree | M | M | M | M in `ux0:data/<target>/` |
| `data/<core>.png` | -- | B: PBP icon | -- | -- |
| External ps2_drivers image set | -- | -- | C: external-image build only | -- |
| Embedded GXP shader blobs | -- | -- | -- | compiled into executable |
| Vita diagnostic log/dump files | -- | -- | -- | O/debug |

## Known duplication and stale-layout findings

### 1. MVS `processed/` versus `cache/`

The runtime already defines `processed/` as canonical. `cache/` is a
compatibility fallback only for cache-enabled builds. This is intentional
temporary duplication support, not two equal canonical locations.

Action:

- keep the compatibility fallback for now;
- document migration to `processed/`;
- remove the fallback only in a deliberate compatibility-breaking cleanup.

### 2. CPS2 cache alternatives

Raw, ZIP, and folder cache layouts are intentionally alternative formats
generated by `romcnv_cps2`. They are not duplicates that should be merged at
runtime. A user needs one valid representation, not all three.

### 3. MVS processed-asset alternatives

Folder and ZIP forms are intentionally alternative formats generated by
`romcnv_mvs`. A user needs one valid representation for the applicable
processed assets.

### 4. `000-lo.lo`

The same ROM content is used by MVS and NCDZ, but the lookup contracts differ:

- MVS expects it as BIOS-archive content in `neogeo.zip`;
- NCDZ expects a top-level `000-lo.lo`.

These cannot be consolidated without changing one or both runtime lookup
contracts. No deletion is justified by matching CRCs alone.

### 5. Translation/font build versus install copies

Translation packs and the GBK font are generated in the build tree, then
packaged/installed. Those copies are expected deployment artifacts rather than
source duplication.

### 6. Release/install manifest

Before this audit, generic `cmake --install` copied the complete
`resources/<core>/` tree. Because that same tree is intentionally used for
local validation, an install could accidentally contain untracked ROMs, BIOS
files, caches, processed assets, saves, configuration, or NVRAM.

Status: **fixed**.

CMake now owns an explicit `NJEMU_DISTRIBUTED_RESOURCE_FILES` manifest and
generates private empty runtime-directory placeholders in the build tree.
`cmake --install` and Vita VPK packaging consume the same manifest. In
particular:

- only NJEMU-distributed metadata/documentation is sourced from
  `resources/<core>/`;
- runtime directories are represented by generated placeholders rather than
  copied from the validation tree;
- the packaged `game_name.ini` is generated empty in the build runtime layout
  rather than copied from mutable resource-tree configuration;
- user ROM/BIOS/processed/cache/save/config data is excluded;
- a local top-level NCDZ `neocd.bin` is not installable/packageable by
  discovery;
- `font/gbk_s14.bin` is included in both GUI and no-GUI deployment output.

### 7. Root README supported-game path

The README points to `docs/gamelist_*.txt`, while the tracked lists are under
`resources/<core>/gamelist_<core>.txt`.

Status: **fixed in the root README**.

## Packaging/install ownership

The desired long-term separation is:

### Distributed by NJEMU

- executable/package;
- `rominfo.*` for CPS1/CPS2/MVS;
- `zipname*` metadata for CPS1/CPS2/MVS;
- generated `lang/*.lng`;
- generated `font/gbk_s14.bin`;
- documentation;
- empty-directory placeholders where the packaged layout needs them;
- PS2 external-image bootstrap/support files only when that mode is selected.

### User-provided or converter-provided

- arcade ROM ZIPs;
- MVS `neogeo.zip`;
- NCDZ `neocd.bin` and `000-lo.lo`;
- NCDZ extracted/ZIP game data and MP3 tracks;
- CPS2 caches from `romcnv_cps2`;
- MVS processed assets from `romcnv_mvs`;
- optional `command.dat` and cheat databases.

### Runtime-generated/private

- `njemu.ini`;
- per-game `config/`;
- `nvram/`;
- `memcard/`;
- `state/`;
- `backup.bin`;
- screenshots;
- temporary cache/state files;
- Vita diagnostic logs/dumps.

## Cleanup plan

### Phase A - authoritative inventory

- [x] Trace the common `launchDir` model.
- [x] Trace ROM ZIP lookup and parent/BIOS fallback.
- [x] Trace CPS1/CPS2/MVS ROM metadata.
- [x] Trace GUI `zipname*` metadata and language fallback.
- [x] Trace CPS2 cache layouts and runtime selection.
- [x] Trace MVS canonical processed assets and legacy fallback.
- [x] Trace NCDZ BIOS and directory/ZIP game resource loading.
- [x] Trace NCDZ MP3 and optional loading image.
- [x] Trace generated translation/font assets.
- [x] Trace mutable configuration/save/NVRAM paths.
- [x] Trace PS2 external IRX packaging.
- [x] Trace Vita resource/bootstrap and embedded shader behavior.
- [x] Trace converter output formats.

### Phase B - packaging safety

- [x] Replace the generic whole-`resources/` install copy with an explicit
  distribution-safe resource manifest/filter.
- [x] Reuse the same manifest for Vita VPK resource selection where practical
  so install/package behavior cannot drift.
- [x] Verify no local ROM, BIOS, cache, processed asset, save, NVRAM, or config
  can enter an install/package merely because it exists under `resources/`.
- [x] Reconcile `font/gbk_s14.bin` packaging with the fact that the common UI
  renderer initializes it in no-GUI builds too.

### Phase C - documentation

- [x] Replace the historical README directory examples with the authoritative
  file classes and conditional requirements above.
- [x] Correct the supported-game-list paths.
- [x] Correct CPS2 wording that describes caches as always mandatory.
- [x] Document `processed/` as the MVS canonical converter output.
- [x] Correct NCDZ documentation that implies direct ISO/BIN/CUE mounting.
- [x] Document NCDZ BIOS requirements explicitly, including `000-lo.lo`.
- [x] Document PS2 embedded versus external IRX-image deployments.
- [x] Document Vita's writable `ux0:data/<target>/` resource root.

### Phase D - compatibility cleanup

- [ ] Audit whether the legacy MVS `cache/` processed-asset fallback can be
  deprecated and eventually removed.
- [ ] Verify whether tracked `data/SystemButtons/SystemButtons.prx` has any
  remaining supported PSP packaging purpose before deleting it.
- [ ] Review historical empty resource directories per core and remove only
  those that no build/package/runtime workflow still needs.

## Validation requirements for changes driven by this audit

For every packaging/runtime lookup cleanup:

1. configure and build the directly affected core/platform combinations;
2. run focused tests and `ctest` where available;
3. run `git diff --check`;
4. verify no-GUI lookup through `game_name.ini`;
5. verify GUI `zipname*` lookup when that path is affected;
6. for CPS2, verify both full-resident and streaming-cache behavior when
   relevant;
7. for MVS, verify full-resident processed assets and streaming processed
   assets when relevant;
8. use the local ROM/BIOS data under `resources/` only for runtime
   validation, without modifying or committing it;
9. use CI for PSP/PS2/PS Vita toolchain coverage when local cross-platform
   validation is not sufficient.
