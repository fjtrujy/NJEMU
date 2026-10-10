# NJEMU Menu Reorganization

Status: implemented on the `me_sound_coprocessor` working branch (2026-10-10).

## Navigation

The in-game main menu is organized by task:

1. **Resume Game:** immediately return to gameplay.
2. **Video & Display:** Game Display, Frame Pacing, FPS Overlay, Video Backend (only if multiple backends exist), PS2 Output Settings (PS2 only).
3. **Audio:** Sound Settings, Audio Processor (only with `PSP_ME_AUDIO`).
4. **Controls:** Active Controller, Button Configuration.
5. **Emulation:** Machine Settings, DIP Switches (if supported). The entire category is hidden for CPS2 builds without any applicable emulation settings.
6. **Performance:** Performance Level (capability-gated PSP/Vita) and Cache Read Size (only under `USE_CACHE`). The category itself disappears if neither entry is applicable.
7. **Tools:** Save/Load State (`SAVE_STATE`), Cheat Configuration, Command List (`COMMAND_LIST`).
8. **System:** Reset Emulation, Return to Browser, Exit Emulator.

Every submenu has an explicit return entry, and the standard cancel/back button remains functional. The legacy R-trigger save-state shortcut remains available from the main menu. The MVS BIOS selector and NCDZ BIOS boot action remain in the ROM browser.

## Existing settings and behavior

The new menu reuses the original `gamecfg2_t` option arrays through a common category filter in `src/common/ui_menu.c`, preserving each core's special choices, per-game disabling rules, and reset/restart behavior.

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

Physical PSP/PS2 visual and input-device walkthroughs are still recommended before a public release, especially low-resolution PS2 video changes and loading save states from the nested Tools page. The existing PSP/Vita performance-value labels are inherited from the earlier game configuration tables and can be improved in a separate localized labels pass.

## Emulator GUI validation (2026-10-10)

- **PPSSPP / PSP MVS (CPU audio GUI):** booted the packaged `EBOOT.PBP`, selected and launched *Fatal Fury: King of Fighters*, opened the in-game menu with Start+Select, navigated into Video & Display, verified that unsupported PS2 output/backend controls were omitted, checked the FPS Overlay page (Show FPS plus X/Y offsets), checked Frame Pacing, and confirmed the cancel/back path to the parent menu.
- **PCSX2 / PS2 MVS:** booted the GUI-enabled `MVS.ELF`, selected and launched *Metal Slug 3*, opened the in-game menu, and checked the main menu and Video & Display section. The PS2 Video Settings page included output mode and both offsets. Switching 480i -> 240p preserved visible UI rendering and selection; switching back through 480i to 480p also redrew the menu.
- **Follow-up needed on PS2 480p:** after the live change to 480p, the menu remained visible, but subsequent Back and D-pad inputs did not produce a visible response in PCSX2. The cause was not established; a VBlank/GS synchronization stall after the live mode switch is one hypothesis. **Do not count 480p interaction as validated**, and retest this separately on PCSX2 and real PS2 hardware before release. The emulator-mode implementation was deliberately left unchanged during this menu/icons task.
- The earlier FPS-overlay description extended beyond the PSP viewport in the highlighted submenu row. Its localized help string was shortened without removing the dedicated FPS X/Y controls.

The emulator sessions were launched from build directories. No ROMs, BIOS, caches, or user settings under `resources/` were modified or committed. These checks supplement, rather than replace, physical PSP/PS2 validation.
