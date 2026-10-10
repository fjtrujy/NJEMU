# Menu Icon Inventory and Residency Audit

Status: baseline measurements and design decisions from 2026-10-10. The four category icons and reorganized menus have since been implemented; see `docs/MENU_REORGANIZATION.md`. The measurements below describe the **pre-change** PS2 MVS executable.

## Current icon format and usage

The shared icon identifiers are defined by `src/common/ui_defs.h`. Their numeric values are part of the current glyph lookup convention:

| ID | Identifier | Intended current use |
| ---: | --- | --- |
| 0 | `ICON_CONFIG` | Game configuration |
| 1 | `ICON_KEYCONFIG` | Button configuration |
| 2 | `ICON_FOLDER` | Browser / folder |
| 3 | `ICON_SYSTEM` | Generic system settings / reset |
| 4 | `ICON_RETURN` | Resume game |
| 5 | `ICON_EXIT` | Exit |
| 6 | `ICON_DIPSWITCH` | DIP switches / currently reused for cheats |
| 7 | `ICON_CMDLIST` | Command list |
| 8 | `ICON_UPPERDIR` | Parent directory (small only) |
| 9 | `ICON_MEMSTICK` | Memory card / save slots (small only) |
| 10 | `ICON_ZIPFILE` | ROM archives (small only) |
| 11-14 | `ICON_BATTERY1` to `ICON_BATTERY4` | Battery levels (small only) |
| 15 | `ICON_COMMANDDAT` | Command-data file (small only) |
| 16 | `ICON_COLORCFG` | Color configuration (small only; no present call site found) |

The large icon glyph tables contain only IDs **0-7**. Small glyph tables contain IDs **0-16**. An attempt to use any small-only ID as a main-menu icon would produce no large glyph.

Source files: `src/common/icon/cps_s.c`, `cps_l.c`, `mvs_s.c`, `mvs_l.c`, `ncdz_s.c`, `ncdz_l.c`. CPS1 and CPS2 share the CPS pack. CMake includes only the selected target's pair, not every core's pair. Icon glyphs are small (~24x18) and large (~32x24), stored as packed 4-bit-per-pixel data, with separate highlighted/light artwork. The common drawing functions consume the glyph data directly; it is not pre-decoded as a dedicated icon texture atlas.

## Exact binary measurement

Measured from the existing GUI-enabled **PS2 MVS** executable `build_display_ps2/MVS.ELF` with `mips64r5900el-ps2-elf-nm -S`:

| Linked icon symbols | Resident read-only data |
| --- | ---: |
| Small glyph arrays and lookup tables | 7,088 bytes |
| Large glyph arrays and lookup tables | 5,776 bytes |
| **Total** | **12,864 bytes (~12.6 KiB)** |

The four actual bitmap arrays contributed 2,688 + 4,128 + 2,384 + 3,264 = **12,464 bytes**; the remaining 400 bytes are position/metrics tables. All 24 measured symbols were marked `r` (read-only data) by the ELF symbol table. This is linked static RAM on PS2, *not* just a file-size saving.

In the existing NO_GUI `build_a8_ps2_mvs/MVS.ELF`, the linker retained only 2,824 bytes of normal small icon data/metrics. GUI-specific large and highlighted icon arrays are already discarded in that build.

The runtime cache policy allocates in **64 KiB** planning units (`MEMORY_PLAN_BLOCK_SIZE`); even removing all current GUI icons is smaller than one such unit. Savings might influence allocation thresholds or fragmentation near a limit, but **do not assume** it will produce another ROM/PCM cache block. Measure largest successful allocations and cache-block counts in a matched PS2/PSP A/B.

## Proposed new menu categories

Recommended top-level layout and symbols:

| Menu entry | Proposed icon | Exists as large glyph? |
| --- | --- | --- |
| Resume Game | `ICON_RETURN` | Yes |
| Video & Display | `ICON_VIDEO` (monitor) | **New** |
| Audio | `ICON_AUDIO` (speaker) | **New** |
| Controls | `ICON_KEYCONFIG` | Yes |
| Emulation | `ICON_CONFIG` | Yes (optional distinct chip icon later) |
| Performance | `ICON_PERFORMANCE` (gauge) | **New** |
| Tools | `ICON_TOOLS` (toolbox/wrench) | **New** |
| System | `ICON_SYSTEM` | Yes |

Keep existing icons, identifiers and numerical indices stable. Add at least four new *paired* large/small glyphs, plus their corresponding highlight variants, rather than renumbering the current 17 small icon entries. Existing submenus can continue using their current glyphs. If the future root menu exposes Save States independently, consider adding a corresponding large save-state glyph instead of using the small-only `ICON_MEMSTICK`.

Use the same monochrome/tinted pixel glyph design, dimensions, alpha/shadow behavior, and highlighted effects as existing icons on all three core palettes. New concept labels should be consistent across cores and platforms.

## External-asset proposal and trade-offs

The current icons are `static const` arrays linked into the resident ELF/PRX. External assets **could** reduce resident memory if the bulk of the pixels is not loaded permanently:

- A consolidated small binary atlas/pack would be simpler to distribute and faster to open than numerous individual PNG files. Keep fixed-sized indexing/metrics to avoid repeated filesystem work and a PNG decoder for tiny glyphs.
- A naive load-all-on-startup implementation simply replaces ~12.6 KiB of read-only storage with an equivalent heap allocation, adding I/O and missing-file failure modes. It does **not** increase cache headroom meaningfully.
- An open-when-needed or menu-lifetime pack could free glyph RAM while gameplay runs, but browser, save-state UI, progress screens, notifications and UI reinitialization must all be handled. Avoid per-icon disk reads inside frame drawing or menu navigation.
- Keep bootstrap/error handling viable if an optional asset is missing. Consider one minimal built-in fallback icon while the rest come from an external pack.
- A new icon asset would have to be included in the explicit packaging/install manifest for PSP, PS2, Desktop and PS Vita, with the same path resolution guarantees as the already external `font/gbk_s14.bin`. No runtime asset should be written into `resources/` by the build.

**Decision:** the four missing icons have been added using the existing embedded glyph format. Do not externalize the entire icon pipeline until matched runtime probes show an actual gain in contiguous cache allocations or availability. If experimenting, use a compile-time switch, preserve the embedded fallback, and compare main-RAM/heap use, binary size, cold-start I/O and menu responsiveness on physical PSP/PS2.

The original audit was documentation-only. Subsequent menu work is documented separately in `docs/MENU_REORGANIZATION.md`.
