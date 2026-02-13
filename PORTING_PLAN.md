# NJEMU Porting Plan

This document outlines the remaining work needed to complete the cross-platform port of NJEMU.

---

## Current Status Summary

> **Milestone: All emulator cores are fully ported to all platforms (PSP, PS2, Desktop). Video driver abstraction is complete.** The next phase is GUI/menu system porting.

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

### Video Driver Abstraction

| Feature | Status | Notes |
|---------|--------|-------|
| Frame buffer globals eliminated | ✅ Complete | `draw_frame`, `show_frame`, `work_frame` replaced by vtable getters |
| `drawFrame()` / `workFrame()` | ✅ Complete | Added to `video_driver_t`, implemented in PSP/PS2/Desktop. `showFrame()` was added then removed (no callers). |
| `beginFrame()` / `endFrame()` | ✅ Complete | Frame lifecycle abstraction for all platforms |
| `frameAddr()` / `scissor()` | ✅ Complete | Implemented in all 3 platform drivers |
| All GUI code uses `video_driver->` | ✅ Complete | No remaining bare global references |

### GUI/Menu System

| Component | PSP | PS2 | PC | Location | Notes |
|-----------|-----|-----|-----|----------|-------|
| Drawing primitives (`ui_draw.c`) | ✅ 2434 lines | ❌ | ❌ | `src/psp/` only | Heavily PSP-specific (sceGu). **Bottleneck file.** |
| Menu system (`ui_menu.c`) | ✅ 2667 lines | ❌ | ❌ | `src/psp/` only | Mostly portable (uses `video_driver->`, `pad_pressed()`) |
| File browser (`filer.c`) | ✅ 1396 lines | ❌ | ❌ | `src/psp/` only | Uses `sceIoDread` for dirs |
| UI framework (`ui.c`) | ✅ 1105 lines | ❌ | ❌ | `src/psp/` only | Mostly portable (dialogs, progress, popups) |
| Configuration (`config.c`) | ✅ 555 lines | ❌ | ❌ | `src/psp/` only | Portable logic, PSP paths |
| PNG handling (`png.c`) | ✅ | ❌ | ❌ | `src/psp/` only | PSP-specific texture upload |
| Font data (`font/*.c`) | ✅ | — | — | `src/psp/font/` only | Embedded C arrays — **replace with external files loaded at runtime** |
| Icon data (`icon/*.c`) | ✅ | — | — | `src/psp/icon/` only | Embedded C arrays — **replace with external files loaded at runtime** |
| Per-system menus (`menu/*.c`) | ✅ | — | — | `src/psp/menu/` only | Pure data/logic — **needs move to common** |
| Per-system config (`config/*.c`) | ✅ | — | — | `src/psp/config/` only | Pure data/logic — **needs move to common** |
| Localization (`*_ui_text.c`) | ✅ | ✅ | ✅ | Per-platform | `ui_text_driver` already abstracted |
| Cross-platform GUI API | ✅ | — | — | `src/common/main_ui_draw.h` | Declares all ~34 GUI functions |

**Total PSP GUI code: ~11,000 lines** (core files) + font data + per-system menu/config data.

### What's Done vs What Remains

```
✅ DONE                              ❌ REMAINING
─────────────────────────            ──────────────────────────────
Emulator cores (all 4)               ui_draw_driver_t interface
Platform drivers (all 9)             Refactor ui_draw.c → common (portable)
Video driver vtable                  PSP ui_draw_driver (wrap sceGu)
Frame buffer abstraction             Desktop ui_draw_driver (SDL2)
beginFrame/endFrame                  PS2 ui_draw_driver (gsKit)
frameAddr/scissor                    Font/icon → external files (not C arrays)
PSP GUI (fully working)              Portable GUI logic → src/common/
main_ui_draw.h (API contract)        CMake NO_GUI for PS2/Desktop
showFrame removed (no callers)       File browser POSIX porting
                                     PNG loading cross-platform
```

---

## ⚠️ Key Design Principle: No Embedded Image or Font Data

> **All image and font data must be loaded from external files at runtime — NEVER embedded as C arrays in source files.**

The current PSP codebase embeds font glyphs and icon bitmaps as large C arrays (e.g., `src/psp/font/ascii_14.c` = 308 lines of hex data, `src/psp/icon/cps_s.c` = 539 lines, `gbk_s14.c` = 15MB). This approach:
- Bloats the binary and compile times
- Makes assets hard to edit or replace
- Couples data to the build system

**New approach for the cross-platform port:**
- Convert all font glyph arrays and icon bitmap arrays to **external binary files** (e.g., `.bin`, `.dat`, or a custom format) stored in `resources/`
- Load them at runtime via standard file I/O in `ui_init()` → `ui_draw_driver->uploadTexture()`
- The font/icon `.c` files under `src/psp/font/` and `src/psp/icon/` will **NOT** be moved to `src/common/` — they will be **replaced** by runtime-loaded external assets
- Only the PSP build may retain the embedded C arrays if needed for backward compatibility (PSP has limited file I/O bandwidth), but even PSP should migrate if feasible

This applies to: font glyphs (ascii_14, latin1_14, graphic, logo, bshadow, font_s, command, gbk_s14, volume_icon), icons (cps_s/l, mvs_s/l, ncdz_s/l), and any future image assets.

---

## GUI Architecture Analysis

### What's Already Portable (Good News)

The PSP GUI code is better structured than expected. The higher-level files (`ui_menu.c`, `filer.c`, `ui.c`) already use platform-agnostic abstractions:

1. **Video output:** `video_driver->flipScreen()`, `video_driver->clearScreen()`, `video_driver->waitVsync()`, `video_driver->beginFrame()`, `video_driver->endFrame()`
2. **Frame buffers:** `video_driver->drawFrame()`, `video_driver->workFrame()` — no more global variables
3. **Input:** `pad_pressed(PLATFORM_PAD_UP)`, `pad_update()`, `pad_wait_clear()` — all platform-independent
4. **Text:** `TEXT()` macro via `ui_text_driver` — already has PS2/Desktop drivers

### Can `ui_draw.c` Become Fully Platform-Agnostic?

**Yes, it is feasible.** After detailed analysis, the ~2435-line `ui_draw.c` has a clear separation between **portable logic** and **PSP GPU rendering**. Here is the breakdown:

#### PSP-Specific Dependencies in `ui_draw.c`

| Category | PSP API | Count | What It Does |
|----------|---------|-------|-------------|
| **GPU command list** | `sceGuStart`/`sceGuFinish`/`sceGuSync` | ~20 each | Bracket every draw call — maps to `beginFrame`/`endFrame` |
| **Draw target** | `sceGuDrawBufferList` | ~15 | Set render target to draw frame — already via `video_driver->drawFrame()` |
| **Scissor** | `sceGuScissor` | ~15 | Set clipping rect — maps to `video_driver->scissor()` |
| **Blending** | `sceGuEnable/Disable(GU_BLEND)` | ~15 pairs | Enable alpha blending for a draw call |
| **Texture setup** | `sceGuTexMode`/`sceGuTexImage`/`sceGuTexFilter` | ~12 each | Bind a texture + set format/filtering |
| **Vertex alloc** | `sceGuGetMemory` | ~15 | Allocate vertices from GPU command list |
| **Draw primitives** | `sceGuDrawArray` | ~15 | Draw sprites/lines/triangles |
| **State toggles** | `sceGuEnable/Disable(GU_TEXTURE_2D, GU_ALPHA_TEST, GU_DITHER)` | ~10 | Misc render state |
| **Shading** | `sceGuShadeModel(GU_SMOOTH/GU_FLAT)` | ~4 | Gouraud vs flat shading |
| **VRAM texture addr** | `texture16_addr(x, y)` = `0x44000000 + offset` | 4 | Compute PSP VRAM address for texture storage |
| **Platform check** | `platform_driver->getDevkitVersion()` | 1 | Volume icon only on CFW ≥ 3.52 |

#### What's Already Portable in `ui_draw.c`

| Category | Lines | Notes |
|----------|-------|-------|
| Font code lookup (`uifont_get_code`, `gbk_get_code`, `latin1_get_code`, `command_font_get_code`) | ~260 | Pure logic, no platform calls |
| Font width calculation (`uifont_get_string_width`) | ~40 | Pure logic |
| Font/shadow/light texture generation (`make_font_texture`, `make_shadow_texture`, `make_light_texture`) | ~130 | Writes pixels to a buffer — portable if buffer is abstract |
| `ui_init` glyph setup (smallfont, boxshadow, volume icons) | ~180 | Writes pixels to a buffer — same |
| Palette data, Gauss blur tables, color macros | ~60 | Pure data |
| `uifont_print*` family (dispatch to `internal_*_putc`) | ~230 | Logic layer on top of internal draw functions |
| Icon functions (`small_icon*`, `large_icon*`) | ~100 | Same pattern: prepare `font_t`, call `internal_*_putc` |
| `ui_light_update`, color constants | ~30 | Pure logic |

**~1030 lines are already fully portable.** The remaining ~1400 lines are the rendering functions that use sceGu.

#### Key Insight: Repeating GPU Pattern

Nearly every GPU-rendering function follows the same 10-line boilerplate:

```c
sceGuStart(GU_DIRECT, gulist);
sceGuDrawBufferList(pixel_format, video_driver->drawFrame(video_data), BUF_WIDTH);
sceGuScissor(0, 0, SCR_WIDTH, SCR_HEIGHT);
sceGuEnable(GU_BLEND);
sceGuTexMode(GU_PSM_4444, 0, 0, GU_FALSE);
sceGuTexImage(0, 512, 512, BUF_WIDTH, tex_font);
sceGuTexFilter(GU_NEAREST, GU_NEAREST);
vertices = (struct Vertex *)sceGuGetMemory(2 * sizeof(struct Vertex));
// ... fill vertices ...
sceGuDrawArray(GU_SPRITES, TEXTURE_FLAGS, 2, NULL, vertices);
sceGuDisable(GU_BLEND);
sceGuFinish();
sceGuSync(0, GU_SYNC_FINISH);
```

This maps to a small set of abstract 2D operations:
1. **Draw a textured sprite** (font glyph, icon, box shadow, volume icon, logo)
2. **Draw a colored line** (`hline`, `vline`, and gradient variants)
3. **Draw a colored filled rect** (`boxfill`, and alpha/gradient variants)
4. **Draw a colored rect outline** (`box`)

#### Proposed Abstraction: `ui_draw_driver`

A new driver interface with **~8 functions** can replace all sceGu usage:

```c
typedef struct ui_draw_driver {
    /* Allocate/init platform texture storage for UI (fonts, icons, shadows) */
    void *(*init)(void *video_data);
    void (*free)(void *data);

    /* Upload a pixel buffer as a named UI texture slot */
    void (*uploadTexture)(void *data, int slot, uint16_t *pixels, int w, int h, int format);

    /* Draw a textured sprite from a texture slot to screen */
    void (*drawSprite)(void *data, int slot, int srcX, int srcY, int srcW, int srcH,
                       int dstX, int dstY, int dstW, int dstH, bool blend);

    /* Draw a filled rectangle (solid color + optional alpha) */
    void (*fillRect)(void *data, int x, int y, int w, int h,
                     uint8_t r, uint8_t g, uint8_t b, uint8_t a);

    /* Draw a gradient-filled rectangle */
    void (*fillRectGradient)(void *data, int x, int y, int w, int h,
                             uint8_t r1, uint8_t g1, uint8_t b1, uint8_t a1,
                             uint8_t r2, uint8_t g2, uint8_t b2, uint8_t a2,
                             int direction);

    /* Draw a line (solid color + optional alpha) */
    void (*drawLine)(void *data, int x1, int y1, int x2, int y2,
                     uint8_t r, uint8_t g, uint8_t b, uint8_t a);

    /* Draw a gradient line */
    void (*drawLineGradient)(void *data, int x1, int y1, int x2, int y2,
                             uint8_t r1, uint8_t g1, uint8_t b1, uint8_t a1,
                             uint8_t r2, uint8_t g2, uint8_t b2, uint8_t a2);

    /* Draw a rect outline (4 lines) */
    void (*drawRect)(void *data, int x, int y, int w, int h,
                     uint8_t r, uint8_t g, uint8_t b);
} ui_draw_driver_t;
```

With this, `ui_draw.c` becomes **fully portable**:
- `make_font_texture()` writes to a local pixel buffer → `uploadTexture(slot, buffer, ...)`
- `internal_font_putc()` → `drawSprite(SLOT_FONT, ...)`
- `hline()` → `drawLine(...)`, `hline_alpha()` → `drawLine(..., alpha)`
- `boxfill()` → `fillRect(...)`, `boxfill_gradation()` → `fillRectGradient(...)`
- `logo()` → build texture + `drawSprite(SLOT_FONT, ...)`

The 4 static texture pointers (`tex_font`, `tex_volicon`, `tex_smallfont`, `tex_boxshadow`) that currently point into PSP VRAM at `0x44000000` would instead be CPU-side buffers **loaded from external binary files at runtime** and uploaded via `uploadTexture()`.

#### Platform Implementations

| Platform | Implementation Complexity |
|----------|--------------------------|
| **PSP** | Thin wrapper around existing sceGu calls — each function maps directly |
| **Desktop/SDL2** | `SDL_RenderFillRect`, `SDL_RenderDrawLine`, `SDL_CreateTexture` + `SDL_RenderCopy` |
| **PS2/gsKit** | `gsKit_prim_sprite`, `gsKit_prim_line`, `gsKit_prim_sprite_texture` |

#### Alternative: Add to Existing `video_driver_t`

Instead of a separate `ui_draw_driver`, these could be added directly to `video_driver_t`. This avoids a new driver abstraction. The `video_driver` already handles the rendering pipeline, so UI 2D primitives are a natural extension.

### What Needs to Move to Common (Platform-Independent Data)

These directories currently live under `src/psp/` but contain **no PSP-specific code** — they are pure C data arrays and portable logic that should be shared across all platforms:

| Directory | Contents | Action |
|-----------|----------|--------|
| `src/psp/font/` | 10 bitmap font C arrays (ascii_14, latin1_14, graphic, logo, etc.) | **Convert to external binary files** in `resources/fonts/` — load at runtime, do NOT move as C files |
| `src/psp/icon/` | 6 icon C arrays (cps_s/l, mvs_s/l, ncdz_s/l) | **Convert to external binary files** in `resources/icons/` — load at runtime, do NOT move as C files |
| `src/psp/menu/` | 3 per-target menu definitions (cps.c, mvs.c, ncdz.c) | Move to `src/common/menu/` |
| `src/psp/config/` | 3 per-target config logic (cps.c, mvs.c, ncdz.c) | Move to `src/common/config/` |

### Layered Architecture (Updated)

```
┌─────────────────────────────────────────────────┐
│  Application Logic (portable)                    │
│  ui_menu.c, filer.c, ui.c, config.c            │
│  Uses: boxfill(), uifont_print(), draw_dialog() │
├─────────────────────────────────────────────────┤
│  Drawing Primitives (PORTABLE after refactor)    │
│  ui_draw.c — SINGLE file, shared across all     │
│  Uses: ui_draw_driver->fillRect(), drawSprite() │
├─────────────────────────────────────────────────┤
│  UI Draw Driver (platform-specific, ~200 lines) │
│  PSP: sceGu  |  PS2: gsKit  |  PC: SDL2        │
│  Implements: fillRect, drawLine, drawSprite     │
├─────────────────────────────────────────────────┤
│  Video Driver (already ported)                   │
│  flipScreen, clearScreen, waitVsync, beginFrame │
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

**Font approach:** Load bitmap font data from external binary files at runtime (converted from the original `font/ascii_14.c`, etc.). No embedded C arrays. No need for SDL_ttf.

**Key implementation details:**
- PSP renders at 480x272; Desktop can use the same virtual resolution with SDL scaling
- Alpha blending: SDL2 has native `SDL_BLENDMODE_BLEND`
- Gradient fills: render line-by-line with interpolated colors
- Icon/font: loaded from external files at `ui_init()`, then uploaded as SDL textures

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
| `src/psp/font/*.c` | `resources/fonts/*.bin` | **Convert to external binary files** — no longer compiled as C source |

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
| Icon rendering | Same as font — texture upload from external binary file data |

**Key considerations:**
- PS2 GS operates on 2D primitives natively — good fit for GUI
- Need to manage GS VRAM for font/icon textures (allocate once at `ui_init`)
- PS2 screen is typically 640x448 interlaced or 640x224 — may need resolution adaptation

### 2.5 Update CMakeLists.txt

The current `NO_GUI=OFF` block in `CMakeLists.txt` uses `${PLATFORM_LOWER}/` prefix for **all** GUI files, which only works for PSP since only `src/psp/` has the full file tree. After moving shared data to common, it should become:

```cmake
if (NO_GUI)
    set(OS_SRC ${OS_SRC}
        ${PLATFORM_LOWER}/${PLATFORM_LOWER}_no_gui.c
    )
else()
    # NOTE: Font and icon data are NO LONGER compiled as C source.
    # They are loaded from external files at runtime (resources/fonts/, resources/icons/).

    # Per-target menu/config (shared across platforms)
    set(COMMON_SRC ${COMMON_SRC}
        common/menu/${MENU_PREFIX}.c
        common/config/${CONFIG_PREFIX}.c
    )

    # Portable GUI logic (shared across platforms)
    set(COMMON_SRC ${COMMON_SRC}
        common/ui_draw.c          # Portable drawing — uses ui_draw_driver
        common/ui_draw_driver.c   # Driver interface + null driver
        common/ui.c
        common/ui_menu.c
        common/filer.c
        common/config.c
        common/png.c
    )

    # Platform-specific ui_draw_driver backend (one per platform)
    set(OS_SRC ${OS_SRC}
        ${PLATFORM_LOWER}/${PLATFORM_LOWER}_ui_draw.c
    )
endif()
```

**Note:** The current CMakeLists.txt compiles fonts from `${PLATFORM_LOWER}/font/`, GUI from `${PLATFORM_LOWER}/`, and icons from `${PLATFORM_LOWER}/icon/`. The PSP build currently works with `NO_GUI=OFF` but PS2 and Desktop would fail because those directories don't exist under `src/ps2/` or `src/desktop/`. After the refactor, font/icon `.c` files are removed entirely — all image/font data is loaded from external binary files at runtime via `ui_init()`.

### 2.6 PNG Loading

**Current:** PSP uses custom `png.c` with PSP-specific texture upload.

**Options:**
1. **stb_image.h** (recommended) — single header, zero dependencies, decodes to raw pixels
2. **libpng** — more complex, but available on all platforms
3. **SDL_image** — Desktop only

**Recommendation:** `stb_image.h` for PNG decode → raw pixel buffer, then each platform uploads to its texture format. This keeps the common code dependency-free.

---

## Phase 3: Implementation Order (Step-by-Step)

### Step 1: Foundation — `ui_draw_driver` Interface + Data Migration

**Goal:** Create the abstraction layer and convert embedded assets to external files

1. [ ] Create `src/common/ui_draw_driver.h` — define `ui_draw_driver_t` struct with ~8 function pointers:
   - `init`, `free`, `uploadTexture`, `drawSprite`, `fillRect`, `fillRectGradient`, `drawLine`, `drawLineGradient`, `drawRect`
2. [ ] Create `src/common/ui_draw_driver.c` — global `ui_draw_driver` pointer + null driver
3. [ ] Convert font C arrays (`src/psp/font/*.c`) to external binary files in `resources/fonts/`
4. [ ] Convert icon C arrays (`src/psp/icon/*.c`) to external binary files in `resources/icons/`
5. [ ] Move menu data to `src/common/menu/` (cps.c, mvs.c, ncdz.c)
6. [ ] Move per-target config to `src/common/config/` (cps.c, mvs.c, ncdz.c)
7. [ ] Update CMakeLists.txt — remove font/icon .c from build, add resource file copy rules
8. [ ] Verify PSP still builds with `NO_GUI=OFF`

### Step 2: Make `ui_draw.c` Portable

**Goal:** Refactor `ui_draw.c` to use `ui_draw_driver` instead of sceGu directly

9. [ ] Copy `src/psp/ui_draw.c` → `src/common/ui_draw.c`
10. [ ] Replace embedded C array font/icon data with runtime loading from external binary files via file I/O
11. [ ] Replace the 4 VRAM texture pointers (`tex_font`, `tex_volicon`, `tex_smallfont`, `tex_boxshadow`) with CPU-side buffers loaded from files + `uploadTexture()` calls
11. [ ] Replace all `sceGuStart`/`sceGuFinish`/`sceGuSync` boilerplate blocks with driver calls:
    - `internal_font_putc` → `ui_draw_driver->drawSprite()`
    - `hline`/`vline` + alpha/gradient variants → `ui_draw_driver->drawLine()`/`drawLineGradient()`
    - `boxfill`/`boxfill_alpha`/`boxfill_gradation` → `ui_draw_driver->fillRect()`/`fillRectGradient()`
    - `box` → `ui_draw_driver->drawRect()`
    - `logo()` → `ui_draw_driver->drawSprite()`
12. [ ] Remove all `#include <pspgu.h>` and sceGu references from common `ui_draw.c`
13. [ ] Verify the common `ui_draw.c` compiles with no platform-specific headers

### Step 3: PSP `ui_draw_driver` Backend

**Goal:** Make PSP work with the new driver interface (regression test)

14. [ ] Create `src/psp/psp_ui_draw.c` — implement `ui_draw_driver_t` wrapping existing sceGu calls:
    - `psp_ui_draw_init()` — set up VRAM texture slots (same as current `ui_init` GPU setup)
    - `psp_ui_draw_uploadTexture()` — copy pixel buffer to VRAM at `0x44000000`
    - `psp_ui_draw_drawSprite()` — `sceGuStart` + tex setup + vertex + `sceGuDrawArray` + `sceGuFinish`
    - `psp_ui_draw_fillRect()` — colored rectangle via sceGu
    - `psp_ui_draw_drawLine()` — line via sceGu
    - etc.
15. [ ] Wire PSP driver in `psp_platform.c` — `ui_draw_driver = &psp_ui_draw_driver;`
16. [ ] Build and test PSP with `NO_GUI=OFF` — must be identical to before

### Step 4: Desktop `ui_draw_driver` Backend

**Goal:** Get GUI rendering working on Desktop

17. [ ] Create `src/desktop/desktop_ui_draw.c` — implement `ui_draw_driver_t` using SDL2:
    - `desktop_ui_draw_init()` — nothing special (SDL renderer already exists)
    - `desktop_ui_draw_uploadTexture()` — `SDL_CreateTexture` from pixel buffer
    - `desktop_ui_draw_drawSprite()` — `SDL_RenderCopy` with src/dst rects
    - `desktop_ui_draw_fillRect()` — `SDL_SetRenderDrawColor` + `SDL_RenderFillRect`
    - `desktop_ui_draw_drawLine()` — `SDL_RenderDrawLine`
    - `desktop_ui_draw_fillRectGradient()` — multiple `SDL_RenderDrawLine` with interpolation
18. [ ] Wire Desktop driver in `desktop_platform.c`
19. [ ] Build Desktop with `NO_GUI=OFF` — test text rendering + boxes
20. [ ] Create simple test: file browser + menu visible and interactive

### Step 5: Extract Portable GUI Logic

**Goal:** Move the rest of the GUI code to common

21. [ ] Extract `src/psp/ui.c` → `src/common/ui.c` (replace `scePower*` with `power_driver->`)
22. [ ] Extract `src/psp/ui_menu.c` → `src/common/ui_menu.c` (minimal changes)
23. [ ] Extract `src/psp/filer.c` browser → `src/common/filer.c` (replace `sceIoDread` with POSIX)
24. [ ] Extract `src/psp/config.c` → `src/common/config.c` (adjust paths)
25. [ ] Verify PSP still builds and runs correctly after extraction
26. [ ] Test Desktop GUI end-to-end (file browser → load ROM → in-game menu)

### Step 6: PNG Loading + Wallpapers

**Goal:** Visual polish with backgrounds and screenshots

27. [ ] Add `stb_image.h` to project (or alternative)
28. [ ] Implement `load_png()` / `save_png()` in common (decode to raw pixels, upload via driver)
29. [ ] Port wallpaper backgrounds
30. [ ] Implement `delete_files()` with portable directory operations

### Step 7: PS2 `ui_draw_driver` Backend

**Goal:** Get GUI rendering working on PS2

31. [ ] Create `src/ps2/ps2_ui_draw.c` — implement `ui_draw_driver_t` using gsKit:
    - `ps2_ui_draw_uploadTexture()` — upload to GS VRAM via gsKit
    - `ps2_ui_draw_drawSprite()` — `gsKit_prim_sprite_texture`
    - `ps2_ui_draw_fillRect()` — `gsKit_prim_sprite`
    - `ps2_ui_draw_fillRectGradient()` — `gsKit_prim_sprite_goraud`
    - `ps2_ui_draw_drawLine()` — `gsKit_prim_line`
32. [ ] Wire PS2 driver in `ps2_platform.c`
33. [ ] Build and test PS2 GUI end-to-end

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

### Completed (Emulator Core + Driver Abstraction)
- [x] `src/cps1/sprite_common.h` — Shared declarations
- [x] `src/cps1/sprite_common.c` — Platform-agnostic code
- [x] `src/cps1/ps2_sprite.c` — PS2 GSKit rendering
- [x] `src/cps1/desktop_sprite.c` — SDL2 rendering
- [x] `src/cps2/sprite_common.h` — Shared declarations
- [x] `src/cps2/sprite_common.c` — Platform-agnostic code
- [x] `src/cps2/ps2_sprite.c` — PS2 GSKit rendering with Z-buffer masking
- [x] `src/cps2/desktop_sprite.c` — SDL2 rendering with priority linked-lists
- [x] `video_driver_t` — Added `drawFrame()`, `workFrame()`, `beginFrame()`, `endFrame()`, `frameAddr()`, `scissor()` (Note: `showFrame()` was added then removed — no callers)
- [x] Eliminated global `draw_frame`, `show_frame`, `work_frame` variables
- [x] All CPS1/CPS2 `blit_finish()` updated to use vtable
- [x] All PSP GUI files updated to use vtable (`ui_draw.c`, `ui.c`, `filer.c`, `ui_menu.c`, `png.c`, `adhoc.c`)
- [x] `src/common/adhoc.c` updated to use vtable
- [x] `src/mvs/biosmenu.c` updated to use vtable
- [x] `src/common/state.c` updated to use vtable

### GUI — Move to Common (pure data, no platform code)
- [ ] `resources/fonts/` — Convert font C arrays from `src/psp/font/*.c` to external binary files
- [ ] `resources/icons/` — Convert icon C arrays from `src/psp/icon/*.c` to external binary files
- [ ] `src/common/menu/` — Move 3 menu files from `src/psp/menu/` (cps.c, mvs.c, ncdz.c)
- [ ] `src/common/config/` — Move 3 per-target config files from `src/psp/config/` (cps.c, mvs.c, ncdz.c)

### GUI — Extract Portable Logic to Common
- [ ] `src/common/ui_draw_driver.h` — Define `ui_draw_driver_t` interface (~8 function pointers)
- [ ] `src/common/ui_draw_driver.c` — Global `ui_draw_driver` pointer + null driver
- [ ] `src/common/ui_draw.c` — Refactor from `src/psp/ui_draw.c` — replace all sceGu with `ui_draw_driver->` calls
- [ ] `src/common/ui.c` — Extract from `src/psp/ui.c` (replace `scePower*` with `power_driver->`)
- [ ] `src/common/ui_menu.c` — Extract from `src/psp/ui_menu.c` (minimal changes)
- [ ] `src/common/filer.c` — Expand existing file (currently only `find_file()`), port full browser from `src/psp/filer.c`
- [ ] `src/common/config.c` — Extract from `src/psp/config.c` (adjust path handling)
- [ ] `src/common/png.c` — Extract from `src/psp/png.c` (needs portable PNG decode)

### GUI — Platform-Specific `ui_draw_driver` Backends (to create)
- [ ] `src/psp/psp_ui_draw.c` — PSP backend wrapping sceGu calls (~300 lines)
- [ ] `src/desktop/desktop_ui_draw.c` — Desktop backend using SDL2 renderer (~300 lines)
- [ ] `src/ps2/ps2_ui_draw.c` — PS2 backend using gsKit (~300 lines)

### GUI — PSP (to modify after common extraction)
- [ ] `src/psp/ui_draw.c` — Remove (replaced by `src/common/ui_draw.c` + `src/psp/psp_ui_draw.c` driver)
- [ ] `src/psp/ui.c` — Remove, use `common/ui.c`
- [ ] `src/psp/ui_menu.c` — Remove, use `common/ui_menu.c`
- [ ] `src/psp/filer.c` — Remove browser logic, use `common/filer.c`
- [ ] `src/psp/config.c` — Remove, use `common/config.c`

---

## Recommended Task Order

### Completed

1. ✅ MVS core for PS2/PC
2. ✅ NCDZ core for PS2/PC
3. ✅ CPS1 sprite rendering for PS2/PC
4. ✅ CPS2 sprite rendering for PS2/PC
5. ✅ Video driver abstraction — `drawFrame()`, `showFrame()`, `workFrame()` vtable getters
6. ✅ Eliminate global `draw_frame`/`show_frame`/`work_frame` variables
7. ✅ `beginFrame()` / `endFrame()` lifecycle in `video_driver_t`
8. ✅ `frameAddr()` / `scissor()` implemented in all 3 platform drivers
9. ✅ All existing code updated to use `video_driver->` instead of globals

### Next: Create `ui_draw_driver` Interface + Move Shared Data (Step 1)

10. 🔲 Create `src/common/ui_draw_driver.h` — define `ui_draw_driver_t` with ~8 function pointers
11. 🔲 Create `src/common/ui_draw_driver.c` — global pointer + null driver
12. 🔲 Convert `src/psp/font/*.c` → `resources/fonts/*.bin` (external binary files)
13. 🔲 Convert `src/psp/icon/*.c` → `resources/icons/*.bin` (external binary files)
14. 🔲 Move `src/psp/menu/*.c` → `src/common/menu/`
15. 🔲 Move `src/psp/config/*.c` → `src/common/config/` (per-target configs)
16. 🔲 Update CMakeLists.txt — remove font/icon .c, add resource file copy rules
17. 🔲 Verify PSP still builds with `NO_GUI=OFF`

### Then: Make `ui_draw.c` Portable (Step 2)

18. 🔲 Copy `src/psp/ui_draw.c` → `src/common/ui_draw.c`
19. 🔲 Replace embedded C array data with runtime loading from external binary files
20. 🔲 Replace VRAM texture pointers with CPU-side buffers + `uploadTexture()`
21. 🔲 Replace all sceGu boilerplate with `ui_draw_driver->` calls
22. 🔲 Remove all PSP-specific headers from common `ui_draw.c`

### Then: PSP `ui_draw_driver` Backend (Step 3 — regression test)

22. 🔲 Create `src/psp/psp_ui_draw.c` wrapping existing sceGu calls
23. 🔲 Wire driver in `psp_platform.c`
24. 🔲 Build and test PSP with `NO_GUI=OFF` — must be identical to before

### Then: Desktop `ui_draw_driver` Backend (Step 4)

25. 🔲 Create `src/desktop/desktop_ui_draw.c` using SDL2 renderer
26. 🔲 Wire driver in `desktop_platform.c`
27. 🔲 Build and test Desktop with `NO_GUI=OFF`

### Then: Extract Portable GUI Logic (Step 5)

28. 🔲 Extract `src/psp/ui.c` → `src/common/ui.c`
29. 🔲 Extract `src/psp/ui_menu.c` → `src/common/ui_menu.c`
30. 🔲 Extract `src/psp/filer.c` → `src/common/filer.c` (POSIX dir ops)
31. 🔲 Extract `src/psp/config.c` → `src/common/config.c`
32. 🔲 Verify PSP + Desktop still build and run

### Then: PS2 `ui_draw_driver` Backend (Step 6)

33. 🔲 Create `src/ps2/ps2_ui_draw.c` using gsKit
34. 🔲 Wire driver in `ps2_platform.c`
35. 🔲 Build and test PS2 GUI end-to-end

### Then: PNG + Wallpapers + Polish (Step 7)

36. 🔲 Add PNG loading (`stb_image.h` or similar)
37. 🔲 Port wallpaper backgrounds
38. 🔲 Save states on all platforms
39. 🔲 Cheat system on all platforms
40. 🔲 Command lists (optional)
41. 🔲 Platform-specific optimizations

---

## Key Design Decisions

### Resolution Strategy

The PSP GUI is designed for 480x272. Options:
1. **Keep 480x272 virtual resolution** — render at 480x272 and scale up for Desktop/PS2 (simplest)
2. **Platform-specific resolution** — different layouts per platform (most work)
3. **Scalable coordinates** — use percentage-based or scaled coordinates (medium effort)

**Recommendation:** Option 1 for initial port. Use `SCR_WIDTH` / `SCR_HEIGHT` constants which are already used throughout the code. Desktop SDL2 can scale the render target to window size.

### Font & Icon Asset Strategy

**Principle: No embedded C arrays.** All font glyph and icon bitmap data must live in **external binary files** loaded at runtime.

**Approach:**
1. Write a one-time conversion tool (or script) to extract the pixel data from the existing `src/psp/font/*.c` and `src/psp/icon/*.c` C arrays into binary files (e.g., `resources/fonts/ascii_14.bin`, `resources/icons/cps_s.bin`)
2. Define a simple binary format: header (width, height, glyph count, format) + raw pixel data
3. At runtime, `ui_init()` loads each binary file via standard file I/O → pixel buffer → `ui_draw_driver->uploadTexture()`
4. Each platform driver converts the pixel buffer to its native texture format:
   - Desktop: `SDL_CreateTexture` from pixel data
   - PS2: GS VRAM texture uploaded via gsKit
   - PSP: Copy to VRAM at `0x44000000` (same result as before, but loaded from file)

**Benefits:** Smaller binaries, faster compile times, assets editable without recompilation, consistent approach across all platforms.

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
- `src/psp/font/*.c` — Bitmap font data (reference for conversion to external binary files)
- `src/desktop/desktop_video.c` — Desktop video driver (reference for SDL2 patterns)
- `src/ps2/ps2_video.c` — PS2 video driver (reference for gsKit patterns)
