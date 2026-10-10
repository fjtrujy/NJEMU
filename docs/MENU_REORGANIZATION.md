# NJEMU Menu Reorganization

Status: implemented on the `me_sound_coprocessor` working branch (2026-10-10).

## Navigation

The in-game main menu has five configuration categories and four direct actions:

1. **Resume Game:** immediately return to gameplay.
2. **Video & Display:** one scrollable screen with display mode, orientation, VSync, frame skipping/limiting, FPS overlay and X/Y offsets, video backend (if selectable), and PS2 output mode/offsets on PS2.
3. **Audio:** one screen with sound, sample rate, volume, NCDZ CDDA, and PSP Media Engine processor selection where supported.
4. **Controls:** directly opens the game-specific button-binding editor with layout, analog sensitivity, autofire, hotkeys, active player/controller, and the optional command-list *viewer* action.
5. **Emulation:** one screen with region/machine mode, raster, NCDZ disc/loading options, per-game DIP switches and available cheat values.
6. **Performance:** one screen with PSP/Vita performance level and cache read size when supported; the root item disappears if neither applies.
7. **Reset Emulation:** direct confirmed action.
8. **Return to Game Browser:** direct confirmed action.
9. **Exit Emulator:** direct confirmed action.

There are no intermediate settings submenus: **Main Menu -> settings page -> Back**. Confirmation dialogs and the command-list informational viewer are not navigation categories. Save/Load State remains a separate **R-trigger shortcut from Main Menu**, including its existing slot operations; it is not duplicated among settings. The MVS BIOS selector and NCDZ BIOS boot action remain in the ROM browser.

## Existing settings and behavior

The new menu reuses the original `gamecfg2_t` option arrays through a shared row adapter in `src/common/menu/settings_page.inc` (included from `src/common/ui_menu.c`), preserving each core's special choices, per-game disabling rules, and reset/restart behavior. Specialized options use the same scrolling and left/right editing loop rather than launching another menu.

- **Video/Game Display:** display mode, CPS vertical screen orientation.
- **Video/Frame Pacing:** VSync, automatic frame skip, manual frame skip, frame limit.
- **Video/FPS Overlay:** Show FPS with its X/Y offsets on the same page. The show/hide flag retains its per-game scope; offsets retain global scope.
- **Audio/Sound Settings:** enable sound, sample rate where supported, sound volume, NCDZ CDDA enable and volume.
- **Controls:** active player/controller selection and the existing game-specific button, layout, analog, autofire and hotkey editor.
- **Emulation:** Neo Geo region/machine mode, raster, NCDZ loading screen and disc-speed emulation, and DIP-switch editor where available.
- **Performance:** supported PSP/Vita performance setting and MVS/CPS2 cache read size.

Global settings still use `njemu.ini`; game-specific settings and mappings still use `config/<game>.ini`. No settings migration or reset of user configuration is needed. Changing video backend, audio processor or cache read size retains the original restart behavior; PS2 output selection still performs live UI/GS rebuilds.

When Auto Frame Skip is active, manual Frame Skip remains stored but is no longer editable; disabling Auto restores manual editing. The runtime settings data structures are not duplicated per submenu.

## Icons and translations

Four new symbols are defined after the existing stable icon identifiers: `ICON_VIDEO`, `ICON_AUDIO`, `ICON_PERFORMANCE`, and `ICON_TOOLS`.

They use the existing 4-bit alpha glyph path with small (24x18), large (32x24), and highlighted versions, and are shared by all cores. New data is in `src/common/icon/menu_extra.c`; only GUI builds include the new art, and no external files need to be installed.

Twenty new stable translation IDs (409-428) extend the manifest and all five language sources. `UI_TEXT_SCHEMA_HASH` is updated to match generated packs. New binaries must be shipped with the matching generated `lang/*.lng` files, as is already required by NJEMU's strict schema checks.

The native host-tool ExternalProject now checks its sources on incremental builds, and the translation-pack rule explicitly depends on the installed generator binary. This prevents an old build directory from silently preserving language packs generated with a different schema hash.

## Memory footprint

Measured using the rebuilt GUI-enabled PS2 MVS ELF:

| Resident read-only glyph data | Bytes |
| --- | ---: |
| Existing small and large glyph packs | 12,864 |
| New four categories, small normal/highlight | 2,528 |
| New four categories, large normal/highlight | 4,096 |
| **Total linked glyph data** | **19,488** |

This is **6,624 extra bytes** of static glyph art, smaller than NJEMU's 64 KiB memory-planning unit. Assets remain embedded because external loading of this small pack would increase I/O and packaging complexity with no proven gain in the number of cache blocks available. Any future externalization needs direct heap and cache-allocation A/B measurements on physical hardware.

## Validation / follow-up

Build/test targets include all four Desktop core GUIs, PS2 MVS GUI, PSP MVS GUI with ME enabled, and PSP MVS GUI with ME disabled. `tests/menu_icon_tests.c` checks the new glyphs in both sizes and normal/highlight states. Translation and UI tests verify new IDs and the translation schema.

Physical PSP/PS2 visual and input-device walkthroughs are still recommended before a public release, especially low-resolution PS2 video changes and save/load state access through R trigger. The existing PSP/Vita performance-value labels are inherited from the earlier game configuration tables and can be improved in a separate localized labels pass.

## Earlier emulator GUI validation of the former nested menu (2026-10-10)

- **PPSSPP / PSP MVS (CPU audio GUI):** booted the packaged `EBOOT.PBP`, selected and launched *Fatal Fury: King of Fighters*, opened the in-game menu with Start+Select, navigated into Video & Display, verified that unsupported PS2 output/backend controls were omitted, checked the FPS Overlay page (Show FPS plus X/Y offsets), checked Frame Pacing, and confirmed the cancel/back path to the parent menu.
- **PCSX2 / PS2 MVS:** booted the GUI-enabled `MVS.ELF`, selected and launched *Metal Slug 3*, opened the in-game menu, and checked the main menu and Video & Display section. The PS2 Video Settings page included output mode and both offsets. Switching 480i -> 240p preserved visible UI rendering and selection; switching back through 480i to 480p also redrew the menu.
- **Follow-up needed on PS2 480p:** after the live change to 480p, the menu remained visible, but subsequent Back and D-pad inputs did not produce a visible response in PCSX2. The cause was not established; a VBlank/GS synchronization stall after the live mode switch is one hypothesis. **Do not count 480p interaction as validated**, and retest this separately on PCSX2 and real PS2 hardware before release. The emulator-mode implementation was deliberately left unchanged during this menu/icons task.
- The earlier FPS-overlay description extended beyond the PSP viewport in the highlighted submenu row. Its localized help string was shortened without removing the dedicated FPS X/Y controls.

The emulator sessions were launched from build directories. No ROMs, BIOS, caches, or user settings under `resources/` were modified or committed. These checks supplement, rather than replace, physical PSP/PS2 validation.

## One-level navigation follow-up (2026-10-10)

- **PPSSPP, MVS CPU audio:** verified the new one-level main menu while *Art of Fighting 2* was running. Video & Display opened directly to display/timing/FPS controls on a single scrolling page; Audio opened directly to sound/sample rate/volume; Controls opened directly to the game-specific key bindings. The Return to File Browser action was reachable directly from the main menu by scrolling, and the R-trigger save-state shortcut was left unchanged.
- All Desktop core GUI builds, PSP MVS CPU/ME GUI builds, and PS2 MVS GUI builds compiled with the final one-level structure. The shared Desktop test suite passed 35/35 checks during development.
- A fresh, comprehensive PCSX2 navigation walkthrough of the one-level code is still recommended before public release; the shared Mac desktop had simultaneous GUI activity during part of the test. The existing 480p live-switch caveat above remains open.
