# NJEMU Porting Plan

This document outlines the remaining work needed to complete the cross-platform port of NJEMU.

---

## Current Status Summary

### Emulator Core Porting

| Emulator | PSP | PS2 | PC | Notes |
|----------|-----|-----|-----|-------|
| **MVS** | ✅ Complete | ✅ Core done | ✅ Core done | Sprite rendering ported |
| **NCDZ** | ✅ Complete | ✅ Core done | ✅ Core done | Sprite rendering ported |
| **CPS1** | ✅ Complete | ❌ Not started | ❌ Not started | Missing sprite rendering |
| **CPS2** | ✅ Complete | ❌ Not started | ❌ Not started | Missing sprite rendering |

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

| Component | PSP | PS2 | PC | Files |
|-----------|-----|-----|-----|-------|
| UI Framework | ✅ | ❌ | ❌ | `ui.c/h`, `ui_draw.c/h` |
| Menu System | ✅ | ❌ | ❌ | `ui_menu.c/h` |
| File Browser | ✅ | ❌ | ❌ | `filer.c/h` |
| Configuration | ✅ | ❌ | ❌ | `config.c/h`, `config/*.c` |
| Font Rendering | ✅ | ❌ | ❌ | `font/*.c` |
| PNG Loading | ✅ | ❌ | ❌ | `png.c/h` |
| Wallpapers | ✅ | ❌ | ❌ | `wallpaper.c/h` |
| Icons | ✅ | ❌ | ❌ | `icon/*.c` |

---

## Phase 1: Complete Emulator Core Ports

### Priority: HIGH

The emulator cores should be ported first since this is the primary functionality.

### 1.1 Port CPS1 Sprite Rendering

**Current Structure (after refactoring):**
- `src/cps1/sprite_common.h` - Shared declarations (constants, structures, extern variables)
- `src/cps1/sprite_common.c` - Platform-agnostic code (hash table management, software rendering)
- `src/cps1/psp_sprite.c` - PSP-specific rendering (GU commands, swizzling)

**Files to create:**
- `src/cps1/ps2_sprite.c`
- `src/cps1/desktop_sprite.c`

**Reference:** 
- Use `src/cps1/psp_sprite.c` as template for platform-specific code
- Use `src/mvs/ps2_sprite.c` and `src/mvs/desktop_sprite.c` as reference implementations

**Steps:**
1. The common sprite management code is already in `sprite_common.c`
2. Only implement the `blit_*` functions for each platform
3. Create `ps2_sprite.c` using PS2 GSKit for rendering (see MVS example)
4. Create `desktop_sprite.c` using SDL2 for rendering (see MVS example)
5. CMakeLists.txt already supports `${PLATFORM_LOWER}_sprite.c`

**Estimated effort:** 1-2 days per platform (reduced due to sprite_common.c refactoring)

### 1.2 Port CPS2 Sprite Rendering

**Current Structure:**
- `src/cps2/psp_sprite.c` - Currently contains all sprite code (needs refactoring)

**Recommended first step:** Refactor similar to CPS1:
1. Create `src/cps2/sprite_common.h` - Shared declarations
2. Create `src/cps2/sprite_common.c` - Platform-agnostic code
3. Update `src/cps2/psp_sprite.c` - Keep only PSP-specific code

**Files to create after refactoring:**
- `src/cps2/sprite_common.h`
- `src/cps2/sprite_common.c`
- `src/cps2/ps2_sprite.c`
- `src/cps2/desktop_sprite.c`

**Reference:** 
- Use `src/cps1/sprite_common.c` as template for the refactoring
- Use `src/mvs/ps2_sprite.c` and `src/mvs/desktop_sprite.c` for platform implementations

**Estimated effort:** 
- Refactoring: 0.5 day
- Per platform: 1-2 days (reduced due to shared code)

### 1.3 Update CMakeLists.txt

Add support for building CPS1 and CPS2 on PS2 and PC platforms:

```cmake
if (${TARGET} STREQUAL "CPS1")
    set(TARGET_SRC ${TARGET_SRC}
        cps1/${PLATFORM_LOWER}_sprite.c
        # ... other CPS1 files
    )
endif()
```

---

## Phase 2: GUI Abstraction Layer

### Priority: MEDIUM

Before porting the GUI, create an abstraction layer to separate platform-specific code.

### 2.1 Define GUI Driver Interface

**Create:** `src/common/gui_driver.h`

Define interfaces for:
```c
// Drawing primitives
void gui_draw_rect(int x, int y, int w, int h, uint32_t color);
void gui_draw_text(int x, int y, const char *text, uint32_t color);
void gui_draw_image(int x, int y, void *image);
void gui_fill_rect(int x, int y, int w, int h, uint32_t color);
void gui_draw_line(int x1, int y1, int x2, int y2, uint32_t color);

// Font handling
void gui_font_init(void);
int gui_font_get_width(const char *text);
int gui_font_get_height(void);

// Image loading
void *gui_load_png(const char *path);
void gui_free_image(void *image);

// Screen management
void gui_begin_frame(void);
void gui_end_frame(void);
```

### 2.2 Refactor PSP UI Code

**Goal:** Separate platform-agnostic logic from PSP-specific rendering

1. Move menu logic to `src/common/ui_menu_common.c`
2. Move file browser logic to `src/common/filer_common.c`
3. Keep PSP rendering in `src/psp/ui_draw.c`

### 2.3 Create Platform GUI Drivers

**Files to create:**
- `src/ps2/ps2_gui.c` - PS2 GSKit implementation
- `src/desktop/desktop_gui.c` - SDL2 implementation

---

## Phase 3: Port Menu/GUI System

### Priority: MEDIUM-LOW

Once the GUI abstraction is in place, port the actual menu system.

### 3.1 Port File Browser

**Dependencies:** GUI driver, font rendering, input handling

**Features:**
- Directory listing
- ROM file selection
- Parent ROM detection
- Preview images (optional)

### 3.2 Port Configuration Menus

**Files to adapt:**
- Game configuration (`config/mvs.c`, `config/cps.c`, `config/ncdz.c`)
- Key configuration
- Display settings
- Sound settings

### 3.3 Port In-Game Menu

**Features:**
- Save/Load state
- Reset game
- Return to browser
- Quick settings

### 3.4 Port Font Rendering

**Options:**
1. **SDL_ttf** (PC) - Use TrueType fonts
2. **Bitmap fonts** (PS2) - Port existing bitmap font system
3. **Unified approach** - Create bitmap font renderer for all platforms

### 3.5 Port Image Loading (PNG)

**Options:**
1. Use **libpng** directly (works on all platforms)
2. Use **stb_image.h** (single-header, no dependencies)
3. Use **SDL_image** (PC only)

**Recommendation:** `stb_image.h` for simplicity and portability

---

## Phase 4: Additional Features

### Priority: LOW

### 4.1 Save States

**Current status:** Implemented for PSP

**Files:** `src/common/state.c/h`

**Work needed:**
- Verify file I/O works on PS2/PC
- Test save/load functionality
- May need endianness handling for cross-platform saves

### 4.2 Cheats

**Current status:** Implemented for PSP

**Files:** `src/common/cheat.c/h` (if exists), cheat loading in `filer.c`

**Work needed:**
- Port cheat file loading
- Port cheat menu

### 4.3 Command Lists

**Current status:** PSP only

**Files:** `src/common/cmdlist.c/h`

**Work needed:**
- Port command list display
- May require font rendering first

---

## Phase 5: Platform-Specific Optimizations

### Priority: LOW (after everything works)

### 5.1 PS2 Optimizations

- Use VU0/VU1 for sprite transformations
- Optimize DMA transfers
- Memory management for limited RAM

### 5.2 PC Optimizations

- Consider OpenGL/Vulkan renderer
- Shader-based rendering
- Resolution scaling

---

## Recommended Task Order

### Immediate (Core Functionality)

1. ✅ ~~MVS core for PS2/PC~~ (DONE)
2. ✅ ~~NCDZ core for PS2/PC~~ (DONE)  
3. 🔲 CPS1 sprite rendering for PS2
4. 🔲 CPS1 sprite rendering for PC
5. 🔲 CPS2 sprite rendering for PS2
6. 🔲 CPS2 sprite rendering for PC

### Short-term (Basic GUI)

7. 🔲 Define GUI driver interface
8. 🔲 Implement PS2 GUI driver (basic)
9. 🔲 Implement PC GUI driver (SDL2)
10. 🔲 Port file browser
11. 🔲 Port basic font rendering

### Medium-term (Full GUI)

12. 🔲 Port configuration menus
13. 🔲 Port in-game menu
14. 🔲 Port PNG loading
15. 🔲 Port wallpaper system

### Long-term (Polish)

16. 🔲 Save states
17. 🔲 Cheats
18. 🔲 Command lists
19. 🔲 Platform optimizations

---

## File Creation Checklist

### CPS1 Porting
- [x] `src/cps1/sprite_common.h` - Created (shared declarations)
- [x] `src/cps1/sprite_common.c` - Created (platform-agnostic code)
- [ ] `src/cps1/ps2_sprite.c`
- [ ] `src/cps1/desktop_sprite.c`

### CPS2 Porting
- [ ] `src/cps2/sprite_common.h` - Refactor from psp_sprite.c
- [ ] `src/cps2/sprite_common.c` - Refactor from psp_sprite.c
- [ ] `src/cps2/ps2_sprite.c`
- [ ] `src/cps2/desktop_sprite.c`

### GUI Abstraction
- [ ] `src/common/gui_driver.h`
- [ ] `src/common/gui_driver.c`
- [ ] `src/common/ui_menu_common.c`
- [ ] `src/common/filer_common.c`

### PS2 GUI
- [ ] `src/ps2/ps2_gui.c`
- [ ] `src/ps2/ps2_font.c`

### PC GUI
- [ ] `src/desktop/desktop_gui.c`
- [ ] `src/desktop/desktop_font.c`

---

## Notes

### Why start with sprite rendering?

The sprite rendering is the core visual output of each emulator. It's:
- Self-contained (doesn't depend on GUI)
- Essential for the emulator to be usable
- A good test of the platform driver architecture

### Why abstract the GUI?

The PSP GUI code is tightly coupled to PSP-specific APIs (GU). Creating an abstraction:
- Allows code reuse across platforms
- Makes maintenance easier
- Follows the same pattern used for the emulator core

### Alternative: Skip GUI for PS2/PC

If GUI porting is too complex, an alternative approach:
1. Keep `*_no_gui.c` approach
2. Use command-line arguments or config files
3. Focus on emulation accuracy instead

This is a valid approach for a "debug/development" platform.

---

## Resources

### PS2 Development
- [PS2DEV GitHub](https://github.com/ps2dev)
- [GSKit documentation](https://github.com/ps2dev/gsKit)

### SDL2 Development
- [SDL2 Wiki](https://wiki.libsdl.org/)
- [Lazy Foo SDL Tutorials](https://lazyfoo.net/tutorials/SDL/)

### Reference Implementations
- `src/mvs/ps2_sprite.c` - PS2 sprite rendering example
- `src/mvs/desktop_sprite.c` - SDL2 sprite rendering example
- `src/cps1/sprite_common.c` - Example of platform-agnostic sprite code separation
- `src/ncdz/sprite_common.c` - Another example of sprite code separation

