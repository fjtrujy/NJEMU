# NJEMU Porting Plan

This document outlines the remaining work needed to complete the cross-platform port of NJEMU.

---

## Current Status Summary

> **Milestone: All emulator cores are fully ported to all platforms (PSP, PS2, Desktop).** The next phase is GUI/menu system porting.

### Emulator Core Porting

| Emulator | PSP | PS2 | PC | Notes |
|----------|-----|-----|-----|-------|
| **MVS** | ✅ Complete | ✅ Complete | ✅ Complete | Sprite rendering ported |
| **NCDZ** | ✅ Complete | ✅ Complete | ✅ Complete | Sprite rendering ported |
| **CPS1** | ✅ Complete | ✅ Complete | ✅ Complete | Sprite rendering ported (incl. stars) |
| **CPS2** | ✅ Complete | ✅ Complete | ✅ Complete | Sprite rendering ported (incl. Z-buffer masking) |

### Platform Drivers

| Driver | PSP | PS2 | PC | Purpose |
|--------|-----|-----|-----|---------|
| `*_platform.c` | ✅ | ✅ | ✅ | Platform init/main loop |
| `*_video.c` | ✅ | ✅ | ✅ | Screen rendering |
| `*_audio.c` | ✅ | ✅ | ✅ | Sound output |
| `*_input.c` | ✅ | ✅ | ✅ | Controller input |
| `*_thread.c` | ✅ | ✅ | ✅ | Threading |
| `*_ticker.c` | ✅ | ✅ | ✅ | Timing |
| `*_power.c` | ✅ | ✅ | ✅ | Power management |
| `*_ui_text.c` | ✅ | ✅ | ✅ | Basic text output |
| `*_no_gui.c` | ✅ | ✅ | ✅ | Stub UI for testing |

### GUI/Menu System

| Component | PSP | PS2 | PC | Notes |
|-----------|-----|-----|-----|-------|
| Drawing primitives (`ui_draw.c`) | ✅ 2434 lines | ❌ | ❌ | Heavily PSP-specific (sceGu) |
| Menu system (`ui_menu.c`) | ✅ 2667 lines | ❌ | ❌ | Mostly portable (uses `video_driver->`, `pad_pressed()`) |
| File browser (`filer.c`) | ✅ 1396 lines | ❌ | ❌ | Partially portable (uses `sceIoDread` for dirs) |
| UI framework (`ui.c`) | ✅ 1105 lines | ❌ | ❌ | Mostly portable (dialogs, progress, popups) |
| Configuration (`config.c`) | ✅ 555 lines | ❌ | ❌ | Portable logic, PSP paths |
| Font data (`font/*.c`) | ✅ | Shareable | Shareable | C arrays, no platform deps |
| Localization (`psp_ui_text.c`) | ✅ 2768 lines | ✅ (driver exists) | ✅ (driver exists) | `ui_text_driver` already abstracted |
| Wallpapers (`wallpaper.c`) | ✅ 151 lines | ❌ | ❌ | PNG loading needed |
| Per-system menus (`menu/*.c`) | ✅ | ❌ | ❌ | CPS: 24K, MVS: 8K, NCDZ: 4K |
| Per-system config (`config/*.c`) | ✅ | ❌ | ❌ | CPS: 42K, MVS: 17K, NCDZ: 4K |

**Total PSP GUI code: ~11,000 lines** (core files) + font data + per-system menu/config data.

---

## GUI Architecture Analysis

### What's Already Portable (Good News)

The PSP GUI code is better structured than expected. The higher-level files (`ui_menu.c`, `filer.c`, `ui.c`) already use platform-agnostic abstractions:

1. **Video output:** `video_driver->flipScreen()`, `video_driver->clearScreen()`, `video_driver->waitVsync()`
2. **Input:** `pad_pressed(PLATFORM_PAD_UP)`, `pad_update()`, `pad_wait_clear()` — all platform-independent
3. **Text:** `TEXT()` macro via `ui_text_driver` — already has PS2/Desktop drivers

### What's PSP-Specific (Needs Porting)

Only `ui_draw.c` is heavily PSP-coupled — it uses `sceGuDrawBufferList()` and direct VRAM pixel manipulation for every drawing primitive. This is the **single bottleneck** file.

The file browser (`filer.c`) uses `sceIoDread()` for PSP directory enumeration and `readHomeButton()` for PSP system button detection. These need platform alternatives.

### Layered Architecture

```
┌─────────────────────────────────────────────────┐
│  Application Logic (portable)                    │
│  ui_menu.c, filer.c, ui.c, config.c            │
│  Uses: boxfill(), uifont_print(), draw_dialog() │
├─────────────────────────────────────────────────┤
│  Drawing Primitives (platform-specific)          │
│  ui_draw.c — ONE file per platform              │
│  Implements: boxfill, hline, vline, fonts, icons│
├─────────────────────────────────────────────────┤
│  Video Driver (already ported)                   │
│  flipScreen, clearScreen, waitVsync             │
├─────────────────────────────────────────────────┤
│  Platform APIs                                   │
│  PSP: sceGu  |  PS2: gsKit  |  PC: SDL2        │
└─────────────────────────────────────────────────┘
```

### GUI Interface (from `main_ui_draw.h`)

All platforms must implement these **34 functions**:

```c
// Initialization
void ui_init(void);

// Font/text rendering (7 functions)
void small_font_print(int sx, int sy, const char *s, int bg);
void small_font_printf(int x, int y, const char *text, ...);
void uifont_print(int sx, int sy, int r, int g, int b, const char *s);
void uifont_print_center(int sy, int r, int g, int b, const char *s);
void uifont_print_shadow(int sx, int sy, int r, int g, int b, const char *s);
void uifont_print_shadow_center(int sy, int r, int g, int b, const char *s);
int  uifont_get_string_width(const char *s);

// Icons (2 functions)
void small_icon(int sx, int sy, int r, int g, int b, int no);
void small_icon_shadow(int sx, int sy, int r, int g, int b, int no);

// Drawing primitives (3 functions)
void boxfill_alpha(int sx, int sy, int ex, int ey, int r, int g, int b, int alpha);
void draw_dialog(int sx, int sy, int ex, int ey);
void draw_scrollbar(int sx, int sy, int ex, int ey, int disp_lines, int total_lines, int current_line);

// Status display (2 functions)
int draw_volume_status(int draw);
int draw_battery_status(int draw);

// Popup/messages (4 functions)
void ui_popup(const char *text, ...);
void ui_popup_reset(void);
int  ui_show_popup(int draw);
void msg_printf(const char *text, ...);

// Screen management (5 functions)
void load_background(int number);
void show_background(void);
void show_exit_screen(void);
void msg_screen_init(int wallpaper, int icon, const char *title);
void msg_screen_clear(void);

// Core entry points (2 functions)
void file_browser(void);
void showmenu(void);

// Configuration (3 functions)
void load_gamecfg(const char *name);
void save_gamecfg(const char *name);
void delete_files(const char *dirname, const char *pattern);

// Image/screenshot (2 functions)
int  load_png(const char *name, int number);
int  save_png(const char *path);

// Help (1 function)
int  help(int number);
```

Plus internal functions not in the header but called between GUI files:

```c
// Used by ui_draw.c (called from filer.c, ui_menu.c, ui.c)
void boxfill(int sx, int sy, int ex, int ey, int r, int g, int b);
void boxfill_gradation(...);
void hline(int sx, int ex, int y, int r, int g, int b);
void vline(int x, int sy, int ey, int r, int g, int b);
void hline_alpha(...);
void vline_alpha(...);
void small_icon_light(...);
void large_icon(...);
void large_icon_light(...);
void large_icon_shadow(...);
void logo(int x, int y, int r, int g, int b);
void textfont_print(...);
// etc.
```

---

## Phase 2: GUI Porting

### Strategy: Bottom-Up, Desktop-First

Port the GUI in layers, starting with Desktop (easiest to test), then PS2:

1. **Step 1:** Port drawing primitives (`ui_draw.c`) for Desktop using SDL2
2. **Step 2:** Port file system operations in `filer.c` for Desktop (replace `sceIoDread`)
3. **Step 3:** Move portable GUI logic to common, adapt for multi-platform
4. **Step 4:** Build and test complete Desktop GUI
5. **Step 5:** Port drawing primitives for PS2 using gsKit
6. **Step 6:** Build and test complete PS2 GUI

### 2.1 Port Drawing Primitives for Desktop (SDL2)

**Create:** `src/desktop/desktop_ui_draw.c`

This is the most critical file — implements all low-level 2D rendering. The PSP version (`src/psp/ui_draw.c`, 2434 lines) uses `sceGu` for VRAM access. Desktop version will use SDL2 renderer.

**Drawing primitive mapping:**

| PSP (`ui_draw.c`) | Desktop SDL2 equivalent |
|---|---|
| `sceGuDrawBufferList` + pixel write | `SDL_RenderDrawPoint` / `SDL_RenderFillRect` |
| `boxfill()` | `SDL_SetRenderDrawColor` + `SDL_RenderFillRect` |
| `boxfill_alpha()` | `SDL_SetRenderDrawBlendMode` + `SDL_RenderFillRect` |
| `hline()` / `vline()` | `SDL_RenderDrawLine` |
| `boxfill_gradation()` | Multiple `SDL_RenderDrawLine` calls with interpolated colors |
| Font bitmap rendering | `SDL_CreateTexture` for glyph atlas, `SDL_RenderCopy` per char |
| Icon rendering | Same as font — bitmap data from `font/graphic.c` |

**Font approach:** Reuse existing bitmap font data (`font/ascii_14.c`, `font/ascii_14p.c`, `font/latin1_14.c`). These are C arrays of pixel data that can be rendered to SDL textures. No need for SDL_ttf.

**Key implementation details:**
- PSP renders at 480x272; Desktop can use the same virtual resolution with SDL scaling
- Alpha blending: SDL2 has native `SDL_BLENDMODE_BLEND`
- Gradient fills: render line-by-line with interpolated colors
- Icon/font: create SDL textures from the C array bitmap data at init time

**Estimated effort:** ~800-1000 lines (simpler than PSP since SDL2 abstracts more)

### 2.2 Port File System Operations

**Modify:** `src/psp/filer.c` → extract portable version

The file browser uses PSP-specific APIs:
- `sceIoDread()` → replace with `opendir()`/`readdir()`/`closedir()` (POSIX)
- `dir.d_stat.st_attr == FIO_SO_IFDIR` → replace with `stat()` or `dirent.d_type`
- `readHomeButton()` → replace with keyboard/controller equivalent
- `sceIoDread` directory scanning → standard POSIX `opendir`/`readdir`

**Approach:** Create `src/common/filer.c` with portable directory enumeration using `#ifdef` for PSP vs POSIX. Or use a thin OS abstraction.

### 2.3 Move Portable GUI Logic to Common

These files are already mostly platform-agnostic and can be shared:

| PSP File | Common File | Changes Needed |
|---|---|---|
| `src/psp/ui.c` | `src/common/ui.c` | Remove `scePower*` calls, use `power_driver->` |
| `src/psp/ui_menu.c` | `src/common/ui_menu.c` | Minimal — already uses abstractions |
| `src/psp/filer.c` | `src/common/filer.c` | Replace `sceIoDread` with POSIX |
| `src/psp/config.c` | `src/common/config.c` | Adjust path handling |
| `src/psp/wallpaper.c` | `src/common/wallpaper.c` | Needs portable PNG loading |
| `src/psp/menu/*.c` | `src/common/menu/*.c` | No changes (pure data/logic) |
| `src/psp/config/*.c` | `src/common/config/*.c` | No changes (pure data/logic) |
| `src/psp/font/*.c` | `src/common/font/*.c` | No changes (pure data arrays) |

**Key principle:** The PSP version should still work after refactoring — `src/psp/ui_draw.c` remains PSP-specific, everything else moves to common.

### 2.4 Port Drawing Primitives for PS2 (gsKit)

**Create:** `src/ps2/ps2_ui_draw.c`

PS2 uses gsKit for 2D rendering. Drawing primitive mapping:

| Function | PS2 gsKit equivalent |
|---|---|
| `boxfill()` | `gsKit_prim_sprite(gsGlobal, x1, y1, x2, y2, z, color)` |
| `boxfill_alpha()` | gsKit with `GS_SETREG_ALPHA` blending |
| `hline()` / `vline()` | `gsKit_prim_line` or thin `gsKit_prim_sprite` |
| `boxfill_gradation()` | `gsKit_prim_sprite_goraud` (gouraud-shaded sprite) |
| Font rendering | Upload glyph bitmaps to GS texture, render with `gsKit_prim_sprite_texture` |
| Icon rendering | Same as font — texture upload from C array data |

**Key considerations:**
- PS2 GS operates on 2D primitives natively — good fit for GUI
- Need to manage GS VRAM for font/icon textures (allocate once at `ui_init`)
- PS2 screen is typically 640x448 interlaced or 640x224 — may need resolution adaptation

### 2.5 Update CMakeLists.txt

Modify the `NO_GUI` conditional to compile the right files:

```cmake
if (NO_GUI)
    set(OS_SRC ${OS_SRC}
        ${PLATFORM_LOWER}/${PLATFORM_LOWER}_no_gui.c
    )
else()
    set(OS_SRC ${OS_SRC}
        ${PLATFORM_LOWER}/${PLATFORM_LOWER}_ui_draw.c
        common/ui.c
        common/ui_menu.c
        common/filer.c
        common/config.c
        common/wallpaper.c
        common/menu/${TARGET_MENU}.c
        common/config/${TARGET_CONFIG}.c
        common/font/ascii_14.c
        common/font/ascii_14p.c
        common/font/latin1_14.c
        common/font/graphic.c
        common/font/logo.c
        common/font/bshadow.c
        common/font/font_s.c
    )
endif()
```

### 2.6 PNG Loading

**Current:** PSP uses custom `png.c` with PSP-specific texture upload.

**Options:**
1. **stb_image.h** (recommended) — single header, zero dependencies, decodes to raw pixels
2. **libpng** — more complex, but available on all platforms
3. **SDL_image** — Desktop only

**Recommendation:** `stb_image.h` for PNG decode → raw pixel buffer, then each platform uploads to its texture format. This keeps the common code dependency-free.

---

## Phase 3: Implementation Order (Step-by-Step)

### Step 1: Foundation — Font Data + Drawing Primitives

**Goal:** Get text rendering working on Desktop

1. [ ] Move font data files to `src/common/font/` (ascii_14.c, ascii_14p.c, latin1_14.c, graphic.c, logo.c, bshadow.c, font_s.c)
2. [ ] Create `src/common/font/font.h` — shared font structures and declarations (extract from `ui_draw.c`)
3. [ ] Create `src/desktop/desktop_ui_draw.c` — implement:
   - `ui_init()` — create SDL textures from font/icon bitmap data
   - `boxfill()`, `boxfill_alpha()`, `boxfill_gradation()`
   - `hline()`, `vline()` + alpha/gradient variants
   - `uifont_print()`, `uifont_print_shadow()`, `uifont_print_center()`, `uifont_print_shadow_center()`
   - `uifont_get_string_width()`
   - `small_icon()`, `small_icon_shadow()`, `small_icon_light()`
   - `large_icon()`, `large_icon_shadow()`, `large_icon_light()`
   - `draw_dialog()`, `draw_scrollbar()`
   - `logo()`
4. [ ] Create test harness: simple SDL2 app that calls drawing functions to verify text + boxes render correctly

### Step 2: Core UI — Progress, Popups, Messages

**Goal:** Get ROM loading UI working on Desktop

5. [ ] Move `src/psp/ui.c` → `src/common/ui.c` (replace `scePower*` with `power_driver->`)
6. [ ] Implement in common/ui.c:
   - `show_progress()` / `update_progress()` — progress bar dialog
   - `ui_popup()` / `ui_show_popup()` / `ui_popup_reset()` — notification popups
   - `msg_printf()` — message output
   - `msg_screen_init()` / `msg_screen_clear()`
   - `show_exit_screen()`
   - `draw_battery_status()` / `draw_volume_status()`
   - `messagebox()` — confirmation dialogs
7. [ ] Test: ROM loading should show progress bar instead of blank screen

### Step 3: File Browser

**Goal:** Browse and select ROMs on Desktop

8. [ ] Create `src/common/filer.c` — port from `src/psp/filer.c`:
   - Replace `sceIoDread` with `opendir()`/`readdir()`/`closedir()`
   - Replace `dir.d_stat.st_attr == FIO_SO_IFDIR` with `dirent.d_type == DT_DIR` or `stat()`
   - Replace `readHomeButton()` with Escape key or controller button
   - Keep all display logic (it already uses portable `uifont_print()`, `boxfill()`, etc.)
9. [ ] Implement `load_background()` / `show_background()` — can start with solid color background
10. [ ] Test: Should be able to browse `roms/` directory and launch a game

### Step 4: Configuration System

**Goal:** Load/save game settings on Desktop

11. [ ] Move `src/psp/config.c` → `src/common/config.c` (adjust file paths)
12. [ ] Move `src/psp/config/*.c` → `src/common/config/` (cps.c, mvs.c, ncdz.c)
13. [ ] Implement `load_gamecfg()` / `save_gamecfg()` with portable paths

### Step 5: In-Game Menu

**Goal:** Pause menu working on Desktop

14. [ ] Move `src/psp/ui_menu.c` → `src/common/ui_menu.c`
15. [ ] Move `src/psp/menu/*.c` → `src/common/menu/` (cps.c, mvs.c, ncdz.c)
16. [ ] Implement `showmenu()` — the full pause menu with:
    - Game configuration
    - Key configuration
    - DIP switch settings (CPS1/MVS)
    - Save/load state (if SAVE_STATE enabled)
    - Reset/restart emulation
    - Return to file browser
17. [ ] Implement `help()` — context-sensitive help screens
18. [ ] Test: Press menu key during game → full menu system works

### Step 6: PNG Loading + Wallpapers

**Goal:** Visual polish with backgrounds and screenshots

19. [ ] Add `stb_image.h` to project (or alternative)
20. [ ] Implement `load_png()` / `save_png()` for Desktop
21. [ ] Move `src/psp/wallpaper.c` → `src/common/wallpaper.c`
22. [ ] Port wallpaper backgrounds (WP_LOGO, WP_FILER, etc.)
23. [ ] Implement `delete_files()` with portable directory operations

### Step 7: PS2 GUI Drawing

**Goal:** Replicate Desktop GUI on PS2

24. [ ] Create `src/ps2/ps2_ui_draw.c` — implement same drawing primitives using gsKit:
    - Font texture upload to GS VRAM
    - `boxfill()` → `gsKit_prim_sprite`
    - `boxfill_gradation()` → `gsKit_prim_sprite_goraud`
    - Text rendering → `gsKit_prim_sprite_texture` with glyph atlas
    - All other primitives
25. [ ] Implement PS2-specific `load_png()` / `save_png()`
26. [ ] Test: PS2 file browser + menu system

### Step 8: PSP Compatibility

**Goal:** Ensure PSP still works after refactoring

27. [ ] Update PSP `CMakeLists.txt` to use common files instead of PSP-specific ones
28. [ ] Keep `src/psp/ui_draw.c` as the PSP drawing backend
29. [ ] Verify PSP builds and runs with the refactored code
30. [ ] Handle PSP-only features gracefully:
    - ADHOC multiplayer (`#ifdef ADHOC`)
    - Home button detection (`readHomeButton()`)
    - Battery status (already via `power_driver`)
    - Large memory mode (`#ifdef LARGE_MEMORY`)

---

## Phase 4: Additional Features

### Priority: LOW (after GUI is working)

### 4.1 Save States

**Current status:** Implemented for PSP in `src/common/state.c/h`

**Work needed:**
- Verify file I/O works on PS2/PC
- Test save/load functionality
- May need endianness handling for cross-platform saves
- Menu integration already handled by `ui_menu.c`

### 4.2 Cheats

**Current status:** Cheat system in `ui_menu.c` (`menu_cheatcfg()`)

**Work needed:**
- Cheat loading uses standard file I/O — should be portable
- Menu rendering uses portable primitives — no changes needed
- Test cheat INI file parsing on all platforms

### 4.3 Command Lists

**Current status:** PSP only, uses `font/command.c` for special command font

**Work needed:**
- Port command font rendering
- Port command list size reduction UI
- Lower priority — niche feature

### 4.4 Chinese Font Support (Optional)

**Current status:** `font/gbk_s14.c` is 15MB of Chinese glyph data

**Decision:** Skip for initial port (massive file, PSP-memory-optimized). Can add later if needed. Start with ASCII + Latin1 fonts only.

---

## Phase 5: Platform-Specific Optimizations

### Priority: LOW (after everything works)

### 5.1 PS2 Optimizations
- Optimize GS VRAM layout for font textures
- Use DMA for bulk text rendering
- Memory management for limited RAM

### 5.2 PC Optimizations
- Consider OpenGL/Vulkan renderer for better scaling
- Shader-based font rendering
- High-DPI support
- Keyboard shortcuts for menu navigation

### 5.3 Resolution Handling
- PSP: 480x272 (native)
- PS2: 640x448 interlaced or 640x224 progressive — need coordinate scaling
- Desktop: Configurable window size with virtual 480x272 scaled up

---

## File Creation Checklist

### Completed (Emulator Core)
- [x] `src/cps1/sprite_common.h` — Shared declarations
- [x] `src/cps1/sprite_common.c` — Platform-agnostic code
- [x] `src/cps1/ps2_sprite.c` — PS2 GSKit rendering
- [x] `src/cps1/desktop_sprite.c` — SDL2 rendering
- [x] `src/cps2/sprite_common.h` — Shared declarations
- [x] `src/cps2/sprite_common.c` — Platform-agnostic code
- [x] `src/cps2/ps2_sprite.c` — PS2 GSKit rendering with Z-buffer masking
- [x] `src/cps2/desktop_sprite.c` — SDL2 rendering with priority linked-lists

### GUI — Common (to create)
- [ ] `src/common/font/` — Move font data (ascii_14.c, ascii_14p.c, latin1_14.c, graphic.c, logo.c, bshadow.c, font_s.c)
- [ ] `src/common/font/font.h` — Shared font structures
- [ ] `src/common/ui.c` — Portable UI framework (from psp/ui.c)
- [ ] `src/common/ui_menu.c` — Portable menu system (from psp/ui_menu.c)
- [ ] `src/common/filer.c` — Portable file browser (from psp/filer.c)
- [ ] `src/common/config.c` — Portable config management (from psp/config.c)
- [ ] `src/common/wallpaper.c` — Portable wallpaper system (from psp/wallpaper.c)
- [ ] `src/common/menu/cps.c` — CPS menu definitions (from psp/menu/cps.c)
- [ ] `src/common/menu/mvs.c` — MVS menu definitions (from psp/menu/mvs.c)
- [ ] `src/common/menu/ncdz.c` — NCDZ menu definitions (from psp/menu/ncdz.c)
- [ ] `src/common/config/cps.c` — CPS config (from psp/config/cps.c)
- [ ] `src/common/config/mvs.c` — MVS config (from psp/config/mvs.c)
- [ ] `src/common/config/ncdz.c` — NCDZ config (from psp/config/ncdz.c)

### GUI — Desktop (to create)
- [ ] `src/desktop/desktop_ui_draw.c` — SDL2 drawing primitives, font rendering, icons

### GUI — PS2 (to create)
- [ ] `src/ps2/ps2_ui_draw.c` — gsKit drawing primitives, font rendering, icons

### GUI — PSP (to modify)
- [ ] `src/psp/ui_draw.c` — Keep as-is (PSP drawing backend)
- [ ] `src/psp/ui.c` — Remove, use common/ui.c
- [ ] `src/psp/ui_menu.c` — Remove, use common/ui_menu.c
- [ ] `src/psp/filer.c` — Remove, use common/filer.c
- [ ] `src/psp/config.c` — Remove, use common/config.c

---

## Recommended Task Order

### Completed (Core Functionality)

1. ✅ ~~MVS core for PS2/PC~~ (DONE)
2. ✅ ~~NCDZ core for PS2/PC~~ (DONE)
3. ✅ ~~CPS1 sprite rendering for PS2/PC~~ (DONE)
4. ✅ ~~CPS2 sprite rendering for PS2/PC~~ (DONE)

### Next: Desktop GUI (Steps 1-6)

5. 🔲 Move font data to `src/common/font/`
6. 🔲 Create `src/desktop/desktop_ui_draw.c` (drawing primitives + font rendering)
7. 🔲 Move `ui.c` to common (progress bars, popups, messages)
8. 🔲 Port file browser to common (replace `sceIoDread` with POSIX)
9. 🔲 Move config system to common
10. 🔲 Move menu system to common
11. 🔲 Add PNG loading (`stb_image.h`)
12. 🔲 Test complete Desktop GUI end-to-end

### Then: PS2 GUI (Step 7)

13. 🔲 Create `src/ps2/ps2_ui_draw.c` (gsKit drawing primitives)
14. 🔲 Test complete PS2 GUI end-to-end

### Then: PSP Compatibility (Step 8)

15. 🔲 Refactor PSP to use common GUI files
16. 🔲 Verify PSP still builds and runs correctly

### Finally: Polish

17. 🔲 Save states on all platforms
18. 🔲 Cheat system on all platforms
19. 🔲 Command lists (optional)
20. 🔲 Platform-specific optimizations

---

## Key Design Decisions

### Resolution Strategy

The PSP GUI is designed for 480x272. Options:
1. **Keep 480x272 virtual resolution** — render at 480x272 and scale up for Desktop/PS2 (simplest)
2. **Platform-specific resolution** — different layouts per platform (most work)
3. **Scalable coordinates** — use percentage-based or scaled coordinates (medium effort)

**Recommendation:** Option 1 for initial port. Use `SCR_WIDTH` / `SCR_HEIGHT` constants which are already used throughout the code. Desktop SDL2 can scale the render target to window size.

### Font Strategy

**Recommendation:** Reuse existing bitmap font C arrays. They're compact, tested, and platform-independent. Each `*_ui_draw.c` converts them to platform-native textures at init time:
- Desktop: SDL2 texture created from pixel data
- PS2: GS VRAM texture uploaded via gsKit
- PSP: Already working

### PNG Strategy

**Recommendation:** `stb_image.h` for decode, platform-specific texture upload:
- Decode PNG → raw RGBA pixel buffer (common)
- Upload to SDL texture / GS texture / PSP texture (platform-specific, in `*_ui_draw.c`)

---

## Notes

### Why Desktop First?

- Fastest iteration cycle (compile + run on dev machine)
- SDL2 is well-documented and easy to debug
- Once common code is validated on Desktop, PS2 port just needs `ps2_ui_draw.c`

### Risk Assessment

| Risk | Impact | Mitigation |
|---|---|---|
| PSP regression after refactoring | HIGH | Test PSP build at each step |
| PS2 memory constraints for fonts | MEDIUM | Skip Chinese fonts, use ASCII+Latin1 only |
| Resolution differences | LOW | Use virtual 480x272, scale on output |
| `ui_draw.c` complexity underestimated | MEDIUM | Start with minimal subset (text + boxes), add features incrementally |

### What NOT to Port

- **ADHOC multiplayer** — PSP-only feature, not relevant for Desktop/PS2
- **Chinese/GBK fonts** — 15MB of data, skip initially
- **PSP power management UI** — Desktop has no battery, PS2 has no variable CPU clock
- **Home button / volume buttons** — PSP-specific system buttons

---

## Resources

### PS2 Development
- [PS2DEV GitHub](https://github.com/ps2dev)
- [GSKit documentation](https://github.com/ps2dev/gsKit)

### SDL2 Development
- [SDL2 Wiki](https://wiki.libsdl.org/)
- [Lazy Foo SDL Tutorials](https://lazyfoo.net/tutorials/SDL/)

### Reference Implementations
- `src/psp/ui_draw.c` — PSP drawing primitives (2434 lines, reference for what needs porting)
- `src/psp/ui_menu.c` — Menu system (2667 lines, mostly portable)
- `src/psp/filer.c` — File browser (1396 lines, needs `sceIoDread` replacement)
- `src/psp/ui.c` — UI framework (1105 lines, mostly portable)
- `src/psp/font/*.c` — Bitmap font data (reusable as-is)
- `src/desktop/desktop_video.c` — Desktop video driver (reference for SDL2 patterns)
- `src/ps2/ps2_video.c` — PS2 video driver (reference for gsKit patterns)
