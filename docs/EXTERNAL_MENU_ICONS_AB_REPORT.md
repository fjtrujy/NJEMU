# External Menu Icon A/B Experiment

Status: experimental, default OFF. No files under `resources/` are modified.

## Scope

Only the four added category icons (Video, Audio, Performance, Tools)
are externalized. The legacy target-specific icon tables remain linked because
ROM browser/loading/progress code can use them before the game runs.

Set `-DNJEMU_EXTERNAL_MENU_ICONS=ON` to replace
`src/common/icon/menu_extra.c` with `menu_extra_external.c`.
The in-game menu loads `menu_icons.bin` from `launchDir` when it opens,
and frees the heap buffer before gameplay resumes. The normal build keeps all
icons embedded and does not open any extra file.

The pack is raw 4-bpp glyph data in this exact order:

| Glyph set | Size |
| --- | ---: |
| Four small, normal (4 x 216) | 864 bytes |
| Four small, highlighted (4 x 416) | 1,664 bytes |
| Four large, normal (4 x 384) | 1,536 bytes |
| Four large, highlighted (4 x 640) | 2,560 bytes |
| Total | 6,624 bytes |

## Building the pack

The host C generator is intentionally separate from cross-compiled PSP/PS2
targets:

```sh
cc -std=c99 -Isrc tools/menu_icon_pack.c src/common/icon/menu_extra.c -o build_icon_probe/menu_icon_pack
build_icon_probe/menu_icon_pack build_icon_probe/menu_icons.bin
```

Copy `menu_icons.bin` next to the emulator's launch directory files in the
*build output* for the experimental binary. Do not put it under `resources/`.
The current experiment does **not** integrate the pack into release packaging;
a missing or malformed pack logs an error and leaves these four icons blank.
The emulator otherwise keeps running. Production releases must not enable this
option until packaging and missing-file behavior are completed.

## Matched linked footprint (PSP and PS2 MVS GUI, CPU audio, USE_CACHE)

Both sides use fresh build directories and identical CMake options except
`NJEMU_EXTERNAL_MENU_ICONS`:

| Platform | Embedded text | External text | Reduction |
| --- | ---: | ---: | ---: |
| PSP | 885,176 | 879,592 | 5,584 bytes |
| PS2 | 1,033,664 | 1,028,344 | 5,320 bytes |

Data and BSS from `size` were equal within each matched pair. The PSP PBP
shrunk from 1,092,483 to 1,087,195 bytes (5,288 bytes). The raw external
pack is 6,624 bytes; gzip -n reduces it to 1,239 bytes, but no decompressor
has been added. A dynamic 6,624-byte allocation exists only while the in-game
menu is open; malloc metadata/fragmentation is not included in the table.

**These are binary-footprint measurements, not ROM-cache-capacity measurements.**
NJEMU allocates cache in 64 KiB units. Removing the four static icon arrays
changes baseline memory pressure by approximately 6.5 KiB, minus any loader
overhead and allocator/segment effects. That alone does not guarantee one more
cache block. Whether a boundary case improves must be tested using the runtime
memory probes on physical PSP/PS2, comparing identical ROMs and settings.

The native `njemu-tool elf-footprint` comparator now also measures all ELF
`PT_LOAD` segments. Against matched MVS GUI binaries, its load-memory
measurements decrease by **5,312 bytes on PSP** and **5,120 bytes on PS2**.
These differ slightly from the original binutils text-column measurements
because program segments include alignment and initialized/writable regions.
See `docs/MEMORY_FOOTPRINT_LEDGER.md` for the reproducible baseline and
cumulative accounting rules. The PS2 MVS Auto read size is 16 KiB, independently
of the 64 KiB cache allocation unit.

The existing legacy small/large icon arrays also occupy static memory, but
externalizing them was deliberately excluded from this experiment because
they are needed outside the in-game menu.

## Validation

- Embedded glyphs and the generated pack were compared byte-for-byte for all
  16 combinations (four icons, small/large, normal/highlight).
- The external mode compiled for PSP MVS GUI and PS2 MVS GUI.
- Desktop MVS GUI external mode compiled and the suite passed 44/44 tests,
  including `menu_icon_pack_tests`.
- A fresh PCSX2 process booted the external PS2 MVS executable to its splash
  screen. End-to-end menu glyph rendering and actual cache capacity remain
  **unverified**. A separate existing PPSSPP session was not interrupted.

## Decision

Leave `NJEMU_EXTERNAL_MENU_ICONS` disabled by default. The A/B variant is
useful for testing, but the 5-6 KiB linked size improvement is not sufficient
evidence for adding a required runtime asset to production. Before promotion,
collect real memory-probe outputs for the same PSP/PS2 ROM under both modes and
validate browser/menu/save-state flows, packaging, and error handling.
