# NJEMU Architecture Reference

This document preserves the detailed architecture reference previously embedded in the README. For the current platform-driver contract and porting rules, prefer [PLATFORM_PORTING_GUIDE.md](PLATFORM_PORTING_GUIDE.md) and [PLATFORM_DRIVER_REFACTOR_PLAN.md](PLATFORM_DRIVER_REFACTOR_PLAN.md) when they are more specific.

## Project Structure

```
NJEMU/
├── CMakeLists.txt          # Main CMake build configuration
├── src/
│   ├── common/             # Platform-agnostic code & driver interfaces
│   │   ├── audio_driver.c/h    # Audio abstraction
│   │   ├── video_driver.c/h    # Video abstraction
│   │   ├── input_driver.c/h    # Input abstraction
│   │   ├── thread_driver.c/h   # Threading abstraction
│   │   └── platform_driver.c/h # Platform abstraction
│   │
│   ├── cpu/                # CPU emulation cores
│   │   ├── m68000/         # Motorola 68000 (C68K)
│   │   └── z80/            # Zilog Z80 (CZ80)
│   │
│   ├── sound/              # Sound chip emulation
│   │   ├── ym2151.c/h      # Yamaha YM2151
│   │   ├── ym2610.c/h      # Yamaha YM2610
│   │   └── qsound.c/h      # QSound DSP
│   │
│   ├── mvs/                # MVS/Neo-Geo emulation
│   ├── cps1/               # CPS1 emulation
│   ├── cps2/               # CPS2 emulation
│   ├── ncdz/               # Neo-Geo CD emulation
│   │
│   ├── psp/                # PSP platform drivers
│   ├── ps2/                # PS2 platform drivers
│   └── desktop/            # PC/SDL platform drivers
│
├── romcnv/                 # ROM conversion tools
│   ├── CMakeLists.txt      # romcnv build configuration
│   └── src/                # romcnv source code
│       ├── mvs/            # MVS ROM conversion
│       └── cps2/           # CPS2 ROM conversion
└── docs/                   # Documentation and game lists
```

### Driver Architecture

A platform backend is selected at link time and implements the shared contracts declared in `src/common/`. The normal backend layout is:

| Backend file | Purpose |
|--------------|---------|
| `*_drivers.c` | Bind the common driver globals to this platform's implementations |
| `*_platform.c` | Startup, launch path, main loop, language and memory telemetry |
| `*_video.c` | Native GPU/display, texture layout, sprite submission and readback |
| `*_audio.c` | Native audio output |
| `*_input.c` | Raw physical controller/keyboard sampling |
| `*_thread.c` | Threading and synchronization |
| `*_ticker.c` | Monotonic timing/frame pacing support |
| `*_power.c` | Optional battery/performance capabilities |
| `*_ui_draw.c` | GUI texture storage/lifecycle adapter when `GUI=ON` |
| `png.c` | Platform image load/save/readback glue when `GUI=ON` |

The bound common services are `audio_driver_t`, `input_driver_t`, `platform_driver_t`, `power_driver_t`, `thread_driver_t`, `ticker_driver_t`, `video_driver_t`, and `ui_draw_driver_t`. `ui_draw_driver_t` is deliberately not a second renderer: low-level drawing belongs to `video_driver_t`, while the UI adapter only handles texture storage/lifetime details that genuinely differ by host.

All four target renderers are shared across platforms. A backend receives logical indexed/direct-color atlas updates plus compact `video_sprite_vertex_t`/`video_point_vertex_t` batches and chooses the fastest native execution path without exposing native GPU objects back to target code.

### Target Configuration

Each emulator target (MVS, NCDZ, CPS1, CPS2) defines configuration globals in its core file (e.g., `src/mvs/mvs.c`):

| Global | Type | Purpose |
|--------|------|---------|
| `emu_layer_textures` | `layer_texture_info_t[]` | Texture atlas dimensions per layer |
| `emu_layer_textures_count` | `uint8_t` | Number of texture layers |
| `emu_clut_info` | `clut_info_t` | CLUT configuration (base, entries, banks) |

**CLUT Configuration Example (MVS):**
```c
clut_info_t emu_clut_info = {
    .base = (uint16_t *)video_palettebank,
    .entries_per_bank = PALETTE_BANK_SIZE,  // 4096
    .bank_count = PALETTE_BANKS             // 2
};
```

**CLUT Configuration Example (CPS1):**
```c
clut_info_t emu_clut_info = {
    .base = (uint16_t *)video_palette,
    .entries_per_bank = CPS1_PALETTE_ENTRIES,  // 3072
    .bank_count = 1
};
```

These are passed to the video driver during initialization in `src/emumain.c`.

---

## Technical Architecture - Emulator Targets

This section documents the internal architecture of each emulator target, focusing on the sprite rendering systems. This information is essential for understanding the codebase and for porting to new platforms.

### Common Concepts

#### Texture Caching System

All targets use a hash-table based texture caching system to avoid re-decoding sprites every frame:

```
┌─────────────────────────────────────────────────────────────┐
│                    Texture Cache Flow                        │
├─────────────────────────────────────────────────────────────┤
│  1. Generate key from (code, attributes)                    │
│  2. Look up key in hash table                               │
│  3. If found: return cached texture index                   │
│  4. If not found:                                           │
│     a. Allocate slot in texture atlas                       │
│     b. Decode tile from ROM to texture memory               │
│     c. Insert into hash table                               │
│     d. Return new texture index                             │
│  5. Mark sprite as "used" this frame                        │
│  6. At frame end: evict sprites not used this frame         │
└─────────────────────────────────────────────────────────────┘
```

#### PSP Texture Swizzling (PSP-Specific)

The PSP GPU benefits from swizzled texture storage, but swizzling is no longer part of any target renderer. Common MVS/CPS/NCDZ code always describes atlas updates in logical rectangular coordinates through `video_driver_t::writeIndexedTextureRect()` / `writeDirectTextureRect()`.

The PSP backend translates those logical coordinates into its native swizzled T8 layout; PS2 and Desktop use their own backend-native layouts. This keeps cache/decode policy shared while preventing PSP memory-addressing rules from leaking back into target code. A new platform should implement the same logical texture-update contract rather than copying PSP swizzle helpers.

#### Color Table (CLUT) System

All targets use 4-bit indexed color (16 colors per palette). The `color_table` embeds palette indices into 8-bit texture pixels:

```c
static const uint32_t color_table[16] = {
    0x00000000, 0x10101010, 0x20202020, 0x30303030,
    0x40404040, 0x50505050, 0x60606060, 0x70707070,
    0x80808080, 0x90909090, 0xa0a0a0a0, 0xb0b0b0b0,
    0xc0c0c0c0, 0xd0d0d0d0, 0xe0e0e0e0, 0xf0f0f0f0
};
```

This allows storing a 4-bit palette index in the upper nibble of each 8-bit texture pixel, which is then used with CLUT (Color Look-Up Table) rendering.

---

### MVS (Neo-Geo) Target

**Files:** `src/mvs/sprite.c`, `src/mvs/sprite_common.c`, `src/mvs/sprite_common.h`

**Hardware Reference:** https://wiki.neogeodev.org/

#### Hardware Specifications

| Feature | Specification |
|---------|---------------|
| Max sprites per frame | 381 |
| Max sprites per scanline | 96 |
| Sprite width | Fixed 16 pixels |
| Sprite height | Up to 512 pixels (32 tiles) |
| Sprite scaling | Shrink only (no magnification) |
| Fix layer tiles | 4,096 (12-bit addressing) |
| Palettes | 2 banks × 256 palettes × 16 colors |
| Fix layer palettes | First 16 only |

#### VRAM Layout

The Neo Geo VRAM is organized into Sprite Control Blocks (SCB):

```
VRAM Address Map (word addresses):
─────────────────────────────────────────
$0000-$6FFF  SCB1 - Sprite tilemaps (28KB)
             - 64 words per sprite × 448 sprites
             - Even words: tile number (bits 0-15)
             - Odd words: palette, tile MSB, flip, auto-anim

$7000-$74FF  FIX Layer - 40×32 tilemap
             - Each word: palette (4 bits) | tile (12 bits)

$7500-$7FFF  Extension area (bankswitching)

$8000-$81FF  SCB2 - Shrink coefficients
             - Lower byte: Y shrink ($FF=full, $00=min)
             - Upper nibble: X shrink ($F=full, $0=min)

$8200-$83FF  SCB3 - Y position and size
             - Bits 7-15: Y position (496 - actual)
             - Bit 6: Sticky bit (chain to previous)
             - Bits 0-5: Height in tiles

$8400-$85FF  SCB4 - X position
             - Bits 7-15: X position
```

#### Code Organization

MVS uses one platform-neutral renderer:

| File | Purpose |
|------|---------|
| `sprite_common.h` | Target sprite/cache declarations and constants |
| `sprite_common.c` | Hash/cache management and shared target data |
| `sprite.c` | MVS decoding, batching, atlas/cache policy and portable draw submission |

#### Graphics Layers

| Layer | Name | Tile Size | Purpose |
|-------|------|-----------|---------|
| FIX | Fixed Layer | 8×8 | Text, HUD, static elements |
| SPR | Sprites | 16×16 | Characters, objects, effects |

#### Texture Cache Configuration

| Layer | Hash Size | Texture Size | Max Sprites/Frame |
|-------|-----------|--------------|-------------------|
| FIX | 0x200 | 512×512 / 8×8 tiles | 1,200 |
| SPR | 0x200 | 512×1536 / 16×16 tiles | 12,288 |

#### Texture Atlas Architecture

The emulator uses texture atlases to batch sprite rendering. Decoded tiles are stored in large textures and referenced by UV coordinates.

**FIX Layer Atlas (TEX_FIX):**
```
┌─────────────────────────────────────────────────────┐
│ Dimensions: 512×512 pixels (8-bit indexed)          │
│ Tile size: 8×8 pixels                               │
│ Tiles per row: 64 (512/8)                           │
│ Total rows: 64 (512/8)                              │
│ Max tiles: 4,096                                    │
├─────────────────────────────────────────────────────┤
│  Tile Layout (idx = tile index):                    │
│  ┌────┬────┬────┬────┬─────┬─────┐                  │
│  │ 0  │ 1  │ 2  │ 3  │ ... │ 63  │  row 0          │
│  ├────┼────┼────┼────┼─────┼─────┤                  │
│  │ 64 │ 65 │ 66 │ 67 │ ... │ 127 │  row 1          │
│  ├────┼────┼────┼────┼─────┼─────┤                  │
│  │... │... │... │... │ ... │ ... │                  │
│  └────┴────┴────┴────┴─────┴─────┘                  │
│                                                     │
│  UV Calculation:                                    │
│  u0 = (idx % 64) * 8 = (idx & 0x3f) << 3            │
│  v0 = (idx / 64) * 8 = (idx & 0xfc0) >> 3           │
└─────────────────────────────────────────────────────┘
```

**SPR Layer Atlas (TEX_SPR0/1/2):**
```
┌─────────────────────────────────────────────────────┐
│ Dimensions: 512×1536 pixels (3 banks × 512)         │
│ Tile size: 16×16 pixels                             │
│ Tiles per row: 32 (512/16)                          │
│ Total rows: 96 (1536/16)                            │
│ Max tiles: 3,072                                    │
├─────────────────────────────────────────────────────┤
│  Memory Organization (3 contiguous buffers):        │
│  ┌─────────────────────┐ ─┐                         │
│  │     TEX_SPR0        │  │                         │
│  │   512×512 (bank 0)  │  │                         │
│  ├─────────────────────┤  │                         │
│  │     TEX_SPR1        │  ├─ 512×1536 total         │
│  │   512×512 (bank 1)  │  │                         │
│  ├─────────────────────┤  │                         │
│  │     TEX_SPR2        │  │                         │
│  │   512×512 (bank 2)  │  │                         │
│  └─────────────────────┘ ─┘                         │
│                                                     │
│  UV Calculation:                                    │
│  u0 = (idx % 32) * 16 = (idx & 0x1f) << 4           │
│  v0 = (idx / 32) * 16 = (idx & 0x3e0) >> 1          │
│  bank = idx >> 10  (which 512×512 section)          │
└─────────────────────────────────────────────────────┘
```

#### CLUT (Color Look-Up Table) System

MVS uses a hardware CLUT for palette-based rendering:

```
┌─────────────────────────────────────────────────────┐
│              CLUT Organization                      │
├─────────────────────────────────────────────────────┤
│ Palette Banks: 2 (for raster effects)               │
│ Palettes per Bank: 256                              │
│ Colors per Palette: 16 (4-bit index)                │
│ Color Format: 15-bit RGB (5-5-5)                    │
│ Total Colors: 2 × 256 × 16 = 8,192                  │
├─────────────────────────────────────────────────────┤
│  Bank Layout (256×16 colors each):                  │
│  ┌────────────────────────────────────┐             │
│  │ Bank 0: Palettes 0-255             │             │
│  │   Palette 0:  colors 0-15          │ ← FIX uses  │
│  │   Palette 1:  colors 16-31         │   palettes  │
│  │   ...                              │   0-15 only │
│  │   Palette 15: colors 240-255       │             │
│  │   Palette 16: colors 256-271       │ ← SPR uses  │
│  │   ...                              │   all 256   │
│  │   Palette 255: colors 4080-4095    │             │
│  ├────────────────────────────────────┤             │
│  │ Bank 1: Palettes 0-255             │             │
│  │   (identical structure)            │             │
│  └────────────────────────────────────┘             │
│                                                     │
│  Texture Pixel Format (8-bit):                      │
│  ┌─────────────────────────────────┐                │
│  │ Bits 7-4: Palette offset (0-15) │                │
│  │ Bits 3-0: Color index (0-15)    │                │
│  └─────────────────────────────────┘                │
│                                                     │
│  color_table[] embeds palette offset:               │
│  0x00000000 = palette offset 0                      │
│  0x10101010 = palette offset 1                      │
│  ...                                                │
│  0xf0f0f0f0 = palette offset 15                     │
└─────────────────────────────────────────────────────┘
```

#### Sprite Cache Key Generation

Sprites are cached using unique keys to avoid re-decoding:

```c
// FIX tiles: code (12-bit) + palette (4-bit)
#define MAKE_FIX_KEY(code, attr)  (code | (attr << 28))

// SPR tiles: code (20-bit) + palette high nibble
#define MAKE_SPR_KEY(code, attr)  (code | ((attr & 0x0f00) << 20))
```

The hash table uses open addressing with linked lists for collision resolution.

#### Work Buffer System

The rendering pipeline uses multiple work buffers:

| Buffer | Type | Size | Purpose |
|--------|------|------|---------|
| SCRBITMAP | 16-bit RGB | 384×264 | Software rendering target |
| TEX_SPR0 | 8-bit indexed | 512×512 | Sprite atlas bank 0 |
| TEX_SPR1 | 8-bit indexed | 512×512 | Sprite atlas bank 1 |
| TEX_SPR2 | 8-bit indexed | 512×512 | Sprite atlas bank 2 |
| TEX_FIX | 8-bit indexed | 512×512 | FIX layer atlas |

#### Graphics Data Format

MVS graphics are stored with a simple nibble-packed format:
- Each 32-bit word contains 8 pixels (4 bits per pixel)
- Decoding extracts odd/even nibbles separately:

```c
tile = *(uint32_t *)(src + 0);
*(uint32_t *)(dst +  0) = ((tile >> 0) & 0x0f0f0f0f) | col;  // pixels 0,2,4,6
*(uint32_t *)(dst +  4) = ((tile >> 4) & 0x0f0f0f0f) | col;  // pixels 1,3,5,7
```

#### Sprite Shrinking (NOT Zooming)

**Important:** Neo Geo sprites can only SHRINK, not magnify. The hardware uses pixel-skipping with no interpolation:

```
Horizontal shrink (4-bit value in SCB2):
  $F = Full size (16 pixels)
  $7 = Half size (8 pixels, alternating)
  $0 = Minimum (1 pixel at center)

Vertical shrink (8-bit value in SCB2):
  $FF = Full size
  $00 = Minimum
```

The emulator uses lookup tables (`zoom_x_tables[]`) to determine which pixels to display for each shrink level. Full-size sprites use an optimized `drawgfxline_fixed()` path.

#### Sprite Rendering Features

- **Hardware path:** Used for full-screen updates (>15 scanlines)
- **Software path:** Scanline-by-scanline for partial updates and shrunk sprites
- **Palette banking:** Two palette banks for raster effects
- **ROM caching:** Large sprite ROMs can be cached to storage
- **Sprite chaining:** Horizontal chaining via "sticky bit" for wide objects

#### Screen Resolution

- **Native:** 304×224
- **With borders:** 320×224 (visible area starts at x=24, y=16)

---

### CPS1 (Capcom Play System 1) Target

**Files:** `src/cps1/sprite.c`, `src/cps1/sprite_common.c`, `src/cps1/sprite_common.h`

**Hardware Reference:**
- [Fabien Sanglard's CPS-1 Graphics Study](https://fabiensanglard.net/cps1_gfx/index.html)
- [Arcade Hacker CPS1 Technical Analysis](https://arcadehacker.blogspot.com/2015/04/capcom-cps1-part-1.html)
- [System16 Hardware Database](https://www.system16.com/hardware.php?id=793)

#### Hardware Specifications

| Component | Specification |
|-----------|---------------|
| **CPU** | Motorola 68000 @ 10MHz (primary), Zilog Z80 @ 3.579MHz (sound) |
| **Sound** | Yamaha YM2151 @ 3.579MHz + OKI6295 @ 7.576kHz |
| **Resolution** | 384×224 pixels @ 59.6294Hz |
| **Colors** | 65,536 available, 4,096 on-screen (192 palettes × 16 colors) |
| **Sprites** | 256 per scanline, 16×16 pixels, 16 colors each |
| **Tilemaps** | 3 layers: 512×512, 1024×1024, 2048×2048 pixels |
| **Memory** | 64KB work RAM + 192KB VRAM |

**History:** Released 1988 with *Forgotten Worlds*. In production for 12 years (1988-2000), hosting 32 game titles (~137 with revisions). Notable games include *Street Fighter II*, *Final Fight*, *Ghouls'n Ghosts*, and *Strider*.

#### File Organization

| File | Purpose |
|------|---------|
| `sprite_common.h` | CPS1 cache/decode declarations and constants |
| `sprite_common.c` | Shared sprite-cache management and target data |
| `sprite.c` | CPS1 object/scroll/stars/high-priority rendering and portable draw submission |

#### Graphics Layers

The CPS-1 composites six layers that can be stacked in any order:

| Layer | Name | Tile Size | Tilemap Size | Purpose |
|-------|------|-----------|--------------|---------|
| OBJECT | Sprites | 16×16 | N/A | Characters, projectiles (max 256 per scanline) |
| SCROLL1 | Text Layer | 8×8 | 512×512 | GUI, text, score (finest granularity) |
| SCROLL2 | Main BG | 16×16 | 1024×1024 | Primary scrolling, **supports per-line parallax** |
| SCROLL3 | Background | 32×32 | 2048×2048 | Large background tiles |
| SCROLLH | High Priority | varies | varies | Overlay effects (uses 16-bit direct color) |
| STAR1/STAR2 | Star Field | 1×1 | N/A | Background stars (Forgotten Worlds, etc.) |

**Layer Notes:**
- **SCROLL1** is typically used for GUI elements due to its 8×8 tile size offering the finest granularity
- **SCROLL2** has a special per-line horizontal scrolling feature used for parallax effects (e.g., Street Fighter II stages)
- **Priority masks** allow specific colors to appear over the OBJ layer, enabling effects like staircases in front of characters (Final Fight)
- GFX ROM is divided into four areas at hardware level (one per SCROLL layer + OBJ), sizes fixed at manufacturing

#### Texture Cache Configuration

| Layer | Hash Size | Texture Size | Max Sprites/Frame |
|-------|-----------|--------------|-------------------|
| OBJECT | 0x200 | 512×512 / 16×16 | 4,096 |
| SCROLL1 | 0x200 | 512×512 / 8×8 | ~1,500 |
| SCROLL2 | 0x100 | 512×512 / 16×16 | ~450 |
| SCROLL3 | 0x40 | 512×512 / 32×32 | ~150 |
| SCROLLH | 0x200 | 512×192 / varies | ~1,500 |

#### Texture Atlas Architecture

CPS1 uses **5 separate texture atlases** (vs MVS/NCDZ's 2), each optimized for different tile sizes:

**OBJECT Atlas (tex_object) - 16×16 sprites:**
```
┌─────────────────────────────────────────────────────┐
│ Dimensions: 512×512 pixels (8-bit indexed)          │
│ Tile size: 16×16 pixels                             │
│ Tiles per row: 32 (512/16)                          │
│ Max tiles: 1,024                                    │
├─────────────────────────────────────────────────────┤
│  UV Calculation:                                    │
│  u0 = (idx & 0x001f) << 4                           │
│  v0 = (idx & 0x03e0) >> 1                           │
│                                                     │
│  Flip handling via attribute bits 5-6:             │
│  attr ^= 0x60;                                      │
│  vertices[(attr & 0x20) >> 5].u += 16;  // X flip   │
│  vertices[(attr & 0x40) >> 6].v += 16;  // Y flip   │
└─────────────────────────────────────────────────────┘
```

**SCROLL1 Atlas (tex_scroll1) - 8×8 tiles:**
```
┌─────────────────────────────────────────────────────┐
│ Dimensions: 512×512 pixels (8-bit indexed)          │
│ Tile size: 8×8 pixels                               │
│ Tiles per row: 64 (512/8)                           │
│ Max tiles: 4,096                                    │
├─────────────────────────────────────────────────────┤
│  UV Calculation:                                    │
│  u0 = (idx & 0x003f) << 3                           │
│  v0 = (idx & 0x0fc0) >> 3                           │
└─────────────────────────────────────────────────────┘
```

**SCROLL2 Atlas (tex_scroll2) - 16×16 tiles:**
```
┌─────────────────────────────────────────────────────┐
│ Dimensions: 512×512 pixels (8-bit indexed)          │
│ Tile size: 16×16 pixels                             │
│ Tiles per row: 32 (512/16)                          │
│ Max tiles: 1,024                                    │
├─────────────────────────────────────────────────────┤
│  UV Calculation: Same as OBJECT                     │
│                                                     │
│  Special Feature: Per-line parallax scrolling       │
│  - When clip region < 16 lines: software rendering  │
│  - When clip region >= 16 lines: hardware rendering │
└─────────────────────────────────────────────────────┘
```

**SCROLL3 Atlas (tex_scroll3) - 32×32 tiles:**
```
┌─────────────────────────────────────────────────────┐
│ Dimensions: 512×512 pixels (8-bit indexed)          │
│ Tile size: 32×32 pixels                             │
│ Tiles per row: 16 (512/32)                          │
│ Max tiles: 256                                      │
├─────────────────────────────────────────────────────┤
│  UV Calculation:                                    │
│  u0 = (idx & 0x000f) << 5                           │
│  v0 = (idx & 0x00f0) << 1                           │
└─────────────────────────────────────────────────────┘
```

**SCROLLH Atlas (tex_scrollh) - High Priority Layer:**
```
┌─────────────────────────────────────────────────────┐
│ Dimensions: 512×192 pixels (16-bit DIRECT color)    │
│ Tile sizes: 8×8, 16×16, or 32×32 (varies by layer)  │
│ DIFFERENT from other layers: NOT indexed!           │
├─────────────────────────────────────────────────────┤
│  Key difference: Pre-rendered to 16-bit color       │
│  - No CLUT lookup at draw time                      │
│  - tpens bitmask controls which colors are visible  │
│  - Used for priority mask effects (e.g., staircases │
│    appearing in front of characters in Final Fight) │
└─────────────────────────────────────────────────────┘
```

#### CLUT (Color Look-Up Table) System

CPS1 uses a different palette organization than MVS/NCDZ:

```
┌─────────────────────────────────────────────────────┐
│              CPS1 CLUT Organization                 │
├─────────────────────────────────────────────────────┤
│ Total Palettes: 192 (vs MVS's 256)                  │
│ Colors per Palette: 16 (4-bit index)                │
│ Color Format: 15-bit RGB (5-5-5)                    │
│ On-screen colors: 192 × 16 = 3,072                  │
├─────────────────────────────────────────────────────┤
│  Palette Assignment by Layer:                       │
│  ┌────────────────────────────────────┐             │
│  │ Palettes 0-31:   OBJECT sprites    │             │
│  │ Palettes 32-63:  SCROLL1 tiles     │             │
│  │ Palettes 64-95:  SCROLL2 tiles     │             │
│  │ Palettes 96-127: SCROLL3 tiles     │             │
│  │ Palettes 128-191: Extended/unused  │             │
│  └────────────────────────────────────┘             │
├─────────────────────────────────────────────────────┤
│  Two CLUT Banks (bit 4 of attr):                    │
│  attr & 0x10 == 0: Bank 0 (lower 16 palettes)       │
│  attr & 0x10 != 0: Bank 1 (upper 16 palettes)       │
│                                                     │
│  PSP CLUT Loading:                                  │
│  OBJECT:  clut[0<<4] or clut[16<<4]                 │
│  SCROLL1: clut[32<<4] or clut[48<<4]                │
│  SCROLL2: clut[64<<4] or clut[80<<4]                │
│  SCROLL3: clut[96<<4] or clut[112<<4]               │
└─────────────────────────────────────────────────────┘
```

#### Sprite Cache Key Generation

CPS1 uses different key formats for normal and high-priority layers:

```c
// Normal layers: tile code + palette (4 bits)
#define MAKE_KEY(code, attr)      (code | ((attr & 0x0f) << 28))

// High priority layer: code + attr including tpens mask
#define MAKE_HIGH_KEY(code, attr) (code | ((attr & 0x19f) << 16))
```

#### Work Buffer System

CPS1 requires more memory due to multiple texture atlases:

| Buffer | Type | Size | Purpose |
|--------|------|------|---------|
| scrbitmap | 16-bit RGB | 512×272 | Software rendering target |
| tex_scrollh | 16-bit RGB | 512×192 | High priority layer (direct color) |
| tex_object | 8-bit indexed | 512×512 | OBJECT sprite atlas |
| tex_scroll1 | 8-bit indexed | 512×512 | SCROLL1 tile atlas |
| tex_scroll2 | 8-bit indexed | 512×512 | SCROLL2 tile atlas |
| tex_scroll3 | 8-bit indexed | 512×512 | SCROLL3 tile atlas |

**Memory Layout (PSP):**
```
work_frame ─┬─ scrbitmap    (512 × 272 × 2 bytes)
            ├─ tex_scrollh  (512 × 192 × 2 bytes)
            ├─ tex_object   (512 × 512 × 1 byte)
            ├─ tex_scroll1  (512 × 512 × 1 byte)
            ├─ tex_scroll2  (512 × 512 × 1 byte)
            └─ tex_scroll3  (512 × 512 × 1 byte)
```

#### CPS1 Graphics Data Format (Interleaved Planar)

**Important:** CPS1 graphics ROMs use an interleaved planar format. When decoding a 32-bit word, pixels are NOT sequential:

```c
// 16-bit direct color decoding (SCROLLH layers)
// Notice the interleaved pattern: 0, 4, 1, 5, 2, 6, 3, 7
dst[ 0] = pal[tile & 0x0f]; tile >>= 4;
dst[ 4] = pal[tile & 0x0f]; tile >>= 4;
dst[ 1] = pal[tile & 0x0f]; tile >>= 4;
dst[ 5] = pal[tile & 0x0f]; tile >>= 4;
dst[ 2] = pal[tile & 0x0f]; tile >>= 4;
dst[ 6] = pal[tile & 0x0f]; tile >>= 4;
dst[ 3] = pal[tile & 0x0f]; tile >>= 4;
dst[ 7] = pal[tile & 0x0f];
```

This pattern corresponds to CPS1's hardware graphics layout where bits are organized as:
- Bits 0-3: Pixel 0
- Bits 4-7: Pixel 4
- Bits 8-11: Pixel 1
- Bits 12-15: Pixel 5
- etc.

**This is NOT a PSP optimization - it's inherent to CPS1's graphics format and must be preserved on all platforms.**

#### Rendering Modes

1. **Hardware Rendering:** Uses GPU texture mapping for most layers
2. **Software Rendering:** Direct pixel writing for SCROLL2 when clipping is complex

```c
// Selection based on clip region size
if (scroll2_max_y - scroll2_min_y >= 16) {
    blit_draw_scroll2 = blit_draw_scroll2_hardware;
} else {
    blit_draw_scroll2 = blit_draw_scroll2_software;
}
```

#### Layer Priority System

CPS1 has a flexible layer priority system controlled by hardware registers:
- All layers can have their priority set freely (any stacking order)
- The SCROLLH (high-priority) layer handles tiles that need to appear above sprites
- **Priority masks** can be assigned to tiles, allowing specific colors (pen values) to appear in front of the OBJ layer instead of behind it. This creates effects like characters appearing "inside" background elements (e.g., staircases in *Final Fight*)

#### Screen Resolution

- **Native:** 384×224
- **With borders:** 512×256 work area (visible at x=64, y=16)

#### Rendering Pipeline (CPS1)

```
┌─────────────────────────────────────────────────────────────────┐
│                    CPS1 Frame Rendering Flow                     │
├─────────────────────────────────────────────────────────────────┤
│                                                                  │
│  1. blit_start(high_layer)                                      │
│     └─ Reset scrollh if layer changed                           │
│     └─ Delete dirty palette entries from scrollh cache          │
│     └─ Clear palette dirty marks                                │
│     └─ Clear work frame                                         │
│                                                                  │
│  2. For each layer (priority order varies per game):            │
│                                                                  │
│     SCROLL3: blit_draw_scroll3() → blit_finish_scroll3()        │
│     └─ Cache miss: decode 32×32 tile to tex_scroll3             │
│     └─ Add vertices, batch by CLUT bank (0 or 1)                │
│     └─ Draw with CLUT at palette 96 or 112                      │
│                                                                  │
│     SCROLL2: blit_set_clip_scroll2() sets render mode           │
│     └─ Clip >= 16 lines: hardware path                          │
│     └─ Clip < 16 lines: software path (for parallax)            │
│     └─ blit_draw_scroll2() → blit_finish_scroll2()              │
│     └─ Draw with CLUT at palette 64 or 80                       │
│                                                                  │
│     SCROLL1: blit_draw_scroll1() → blit_finish_scroll1()        │
│     └─ Cache miss: decode 8×8 tile to tex_scroll1               │
│     └─ gfxset parameter for different character sets            │
│     └─ Draw with CLUT at palette 32 or 48                       │
│                                                                  │
│     OBJECT: blit_draw_object() → blit_finish_object()           │
│     └─ Cache miss: decode 16×16 sprite to tex_object            │
│     └─ Track CLUT changes, batch draw when CLUT switches        │
│     └─ Draw with CLUT at palette 0 or 16                        │
│                                                                  │
│     SCROLLH (high priority): varies by scrollh_layer_number     │
│     └─ blit_draw_scroll1h/2h/3h() → blit_finish_scrollh()       │
│     └─ Uses 16-bit direct color (no CLUT at draw time)          │
│     └─ tpens bitmask controls visible colors (priority mask)    │
│                                                                  │
│     STARS: blit_draw_stars() (point rendering)                  │
│     └─ Draws 1×1 pixel stars as GPU points                      │
│     └─ Used in Forgotten Worlds, etc.                           │
│                                                                  │
│  3. blit_finish()                                               │
│     └─ Handle screen rotation/flip if enabled                   │
│     └─ Transfer work frame to display with scaling              │
│                                                                  │
└─────────────────────────────────────────────────────────────────┘
```

---

### CPS2 (Capcom Play System 2) Target

**File:** `src/cps2/sprite.c` (shared by PSP, PS2 and Desktop)

#### Graphics Layers

| Layer | Name | Tile Size | Purpose |
|-------|------|-----------|---------|
| OBJECT | Sprites | 16×16 | Characters, effects (with priority) |
| SCROLL1 | Text Layer | 8×8 | Text, HUD |
| SCROLL2 | Main Background | 16×16 | Primary scrolling |
| SCROLL3 | Background | 32×32 | Large background tiles |

#### Texture Cache Configuration

| Layer | Hash Size | Texture Size | Max Sprites/Frame |
|-------|-----------|--------------|-------------------|
| OBJECT | 0x200 | 512×512 / 16×16 | 5,120 |
| SCROLL1 | 0x200 | 512×512 / 8×8 | ~1,500 |
| SCROLL2 | 0x100 | 512×512 / 16×16 | ~450 |
| SCROLL3 | 0x40 | 512×512 / 32×32 | ~150 |

#### CPS2-Specific Features

**Object Priority System:**
CPS2 has 8 priority levels for sprites. Objects are sorted into priority buckets:

```c
static OBJECT *vertices_object_head[8];  // 8 priority levels
static OBJECT *vertices_object_tail[8];
static uint16_t object_num[8];
```

**Z-Buffer Rendering:**
Optional Z-buffer based rendering for complex priority scenes:

```c
void (*blit_finish_object)(int start_pri, int end_pri);
// Can be either:
// - blit_render_object()     - Standard rendering
// - blit_render_object_zb()  - Z-buffer based
```

#### Graphics Data Format

CPS2 uses the same interleaved planar format as CPS1 (see CPS1 section above).

#### Screen Resolution

- **Native:** 384×224
- **With borders:** 512×256 work area

---

### NCDZ (Neo-Geo CD) Target

**Files:** `src/ncdz/sprite.c`, `src/ncdz/sprite_common.c`, `src/ncdz/sprite_common.h`

**Hardware Reference:** https://wiki.neogeodev.org/

#### Hardware Specifications

| Feature | Specification |
|---------|---------------|
| Max sprites per frame | 381 |
| Max sprites per scanline | 96 |
| Sprite width | Fixed 16 pixels |
| Sprite height | Up to 512 pixels (32 tiles) |
| Sprite scaling | Shrink only (no magnification) |
| Fix layer tiles | 4,096 (12-bit addressing) |
| Palettes | 2 banks × 256 palettes × 16 colors |
| Fix layer palettes | First 16 only |
| Tile addressing | 15-bit (vs MVS 20-bit) |

#### VRAM Layout

The Neo Geo CD uses the same VRAM layout as MVS:

```
VRAM Address Map (word addresses):
─────────────────────────────────────────
$0000-$6FFF  SCB1 - Sprite tilemaps (28KB)
             - 64 words per sprite × 448 sprites
             - Even words: tile number (bits 0-14 for CD)
             - Odd words: palette, tile MSB, flip, auto-anim

$7000-$74FF  FIX Layer - 40×32 tilemap
             - Each word: palette (4 bits) | tile (12 bits)

$7500-$7FFF  Extension area (bankswitching)

$8000-$81FF  SCB2 - Shrink coefficients
             - Lower byte: Y shrink ($FF=full, $00=min)
             - Upper nibble: X shrink ($F=full, $0=min)

$8200-$83FF  SCB3 - Y position and size
             - Bits 7-15: Y position (496 - actual)
             - Bit 6: Sticky bit (chain to previous)
             - Bits 0-5: Height in tiles

$8400-$85FF  SCB4 - X position
             - Bits 7-15: X position
```

#### Code Organization

NCDZ uses one platform-neutral renderer similar to MVS:

| File | Purpose |
|------|---------|
| `sprite_common.h` | Neo Geo CD sprite/cache declarations and constants |
| `sprite_common.c` | Shared target cache management and data |
| `sprite.c` | NCDZ decoding, batching, atlas policy and portable draw submission |

#### Graphics Layers

| Layer | Name | Tile Size | Purpose |
|-------|------|-----------|---------|
| FIX | Fixed Layer | 8×8 | Text, HUD |
| SPR | Sprites | 16×16 | All game graphics |

#### Texture Cache Configuration

| Layer | Hash Size | Texture Size | Max Sprites/Frame |
|-------|-----------|--------------|-------------------|
| FIX | 0x200 | 512×512 / 8×8 | 1,200 |
| SPR | 0x200 | 512×1536 / 16×16 | 12,288 |

#### Texture Atlas Architecture

NCDZ uses the same texture atlas architecture as MVS. Since both systems share the Neo Geo hardware base, the rendering implementation is nearly identical.

**FIX Layer Atlas (TEX_FIX):**
```
┌─────────────────────────────────────────────────────┐
│ Dimensions: 512×512 pixels (8-bit indexed)          │
│ Tile size: 8×8 pixels                               │
│ Tiles per row: 64 (512/8)                           │
│ Max tiles: 4,096                                    │
├─────────────────────────────────────────────────────┤
│  UV Calculation (same as MVS):                      │
│  u0 = (idx & 0x3f) << 3                             │
│  v0 = (idx & 0xfc0) >> 3                            │
│                                                     │
│  Key difference from MVS:                           │
│  - Graphics from memory_region_gfx1 (loaded from CD)│
│  - Different tile decoding (bit expansion):         │
│    datal = ((tile & 0x0f) >> 0) | ...               │
│    (expands 4-bit to 8-bit per pixel)               │
└─────────────────────────────────────────────────────┘
```

**SPR Layer Atlas (TEX_SPR0/1/2):**
```
┌─────────────────────────────────────────────────────┐
│ Dimensions: 512×1536 pixels (3 banks × 512)         │
│ Tile size: 16×16 pixels                             │
│ Tiles per row: 32 (512/16)                          │
│ Max tiles: 3,072                                    │
├─────────────────────────────────────────────────────┤
│  Memory Organization:                               │
│  ┌─────────────────────┐                            │
│  │ TEX_SPR0 (bank 0)   │ ← tiles 0-1023             │
│  ├─────────────────────┤                            │
│  │ TEX_SPR1 (bank 1)   │ ← tiles 1024-2047          │
│  ├─────────────────────┤                            │
│  │ TEX_SPR2 (bank 2)   │ ← tiles 2048-3071          │
│  └─────────────────────┘                            │
│                                                     │
│  UV Calculation:                                    │
│  u0 = (idx & 0x1f) << 4                             │
│  v0 = (idx & 0x3e0) >> 1                            │
│  bank = idx >> 10                                   │
│                                                     │
│  Key difference from MVS:                           │
│  - Graphics from memory_region_gfx2 (loaded from CD)│
│  - No ROM caching needed (all data in RAM)          │
└─────────────────────────────────────────────────────┘
```

#### CLUT (Color Look-Up Table) System

NCDZ uses the same CLUT system as MVS:

```
┌─────────────────────────────────────────────────────┐
│              CLUT Organization                      │
├─────────────────────────────────────────────────────┤
│ Palette Banks: 2                                    │
│ Palettes per Bank: 256                              │
│ Colors per Palette: 16                              │
│ Color Format: 15-bit RGB                            │
├─────────────────────────────────────────────────────┤
│  Palette Selection in Attributes:                   │
│  ┌────────────────────────────────────┐             │
│  │ FIX: attr bits 0-3 = palette 0-15  │             │
│  │ SPR: attr bits 8-11 = palette 0-255│             │
│  └────────────────────────────────────┘             │
│                                                     │
│  color_table[] lookup (same as MVS):                │
│  col = color_table[(attr >> 8) & 0x0f]              │
│  Embeds palette offset into 8-bit texture pixels   │
└─────────────────────────────────────────────────────┘
```

#### Sprite Cache Key Generation

Same key generation as MVS:

```c
// FIX: 12-bit tile code + 4-bit palette
#define MAKE_FIX_KEY(code, attr)  (code | (attr << 28))

// SPR: 15-bit tile code + palette offset
#define MAKE_SPR_KEY(code, attr)  (code | ((attr & 0x0f00) << 20))
```

**Note:** NCDZ uses 15-bit tile codes (`code & 0x7fff`) vs MVS's 20-bit addressing.

#### Work Buffer System

| Buffer | Type | Size | Purpose |
|--------|------|------|---------|
| SCRBITMAP | 16-bit RGB | 384×264 | Software rendering target |
| TEX_SPR0 | 8-bit indexed | 512×512 | Sprite atlas bank 0 |
| TEX_SPR1 | 8-bit indexed | 512×512 | Sprite atlas bank 1 |
| TEX_SPR2 | 8-bit indexed | 512×512 | Sprite atlas bank 2 |
| TEX_FIX | 8-bit indexed | 512×512 | FIX layer atlas |

#### Graphics Data Format

NCDZ uses the same nibble-packed format as MVS:
- Each 32-bit word contains 8 pixels (4 bits per pixel)
- Decoding extracts odd/even nibbles separately:

```c
tile = *(uint32_t *)(src + 0);
*(uint32_t *)(dst +  0) = ((tile >> 0) & 0x0f0f0f0f) | col;  // pixels 0,2,4,6
*(uint32_t *)(dst +  4) = ((tile >> 4) & 0x0f0f0f0f) | col;  // pixels 1,3,5,7
```

#### Sprite Shrinking (NOT Zooming)

**Important:** Like MVS, Neo Geo CD sprites can only SHRINK, not magnify. The hardware uses pixel-skipping with no interpolation:

```
Horizontal shrink (4-bit value in SCB2):
  $F = Full size (16 pixels)
  $7 = Half size (8 pixels, alternating)
  $0 = Minimum (1 pixel at center)

Vertical shrink (8-bit value in SCB2):
  $FF = Full size
  $00 = Minimum
```

The emulator uses lookup tables (`zoom_x_tables[]`) to determine which pixels to display for each shrink level. Full-size sprites use an optimized `drawgfxline_fixed()` path.

#### Sprite Rendering Features

- **Hardware path:** Used for full-screen updates (>15 scanlines)
- **Software path:** Scanline-by-scanline for partial updates and shrunk sprites
- **Palette banking:** Two palette banks for raster effects
- **Sprite chaining:** Horizontal chaining via "sticky bit" for wide objects

#### NCDZ vs MVS Differences

NCDZ is similar to MVS but with key differences:
- **No ROM caching:** Graphics loaded from CD to RAM
- **Larger RAM:** Can hold more graphics data
- **Same graphics format:** Uses MVS-compatible tile format
- **Audio from CD:** MP3/CDDA instead of ROM-based audio
- **15-bit tile addressing:** Uses `code & 0x7fff` vs MVS's 20-bit addressing

#### Screen Resolution

- **Native:** 304×224 (same as MVS)
- **With borders:** 320×224 (visible area starts at x=24, y=16)

---

### Porting Guide

Target renderers are no longer ported separately per host. They emit portable texture updates and compact sprite/point batches through `video_driver_t`; PSP, PS2 and Desktop keep native texture layout, CLUT handling and GPU submission inside their backends.

For the current extension recipe, including logical-vs-physical geometry, driver binding, input capabilities, UI ownership, renderer performance requirements and the validation gate for a new host, see [PLATFORM_PORTING_GUIDE.md](PLATFORM_PORTING_GUIDE.md). The completed refactor and its measurements are recorded in [PLATFORM_DRIVER_REFACTOR_PLAN.md](PLATFORM_DRIVER_REFACTOR_PLAN.md).

## Internal Systems Documentation

This section provides detailed documentation of the emulator's internal systems, useful for developers and advanced users.

### Sound System Architecture

The sound system uses a multi-threaded architecture to ensure smooth audio output without blocking the main emulation loop.

#### Sound Thread (`src/common/sound.c`)

```
┌─────────────────────────────────────────────────────────────┐
│                    Sound Thread Flow                         │
├─────────────────────────────────────────────────────────────┤
│  Main Thread                    Sound Thread                 │
│  ───────────                    ────────────                 │
│  1. Initialize sound info       1. Wait for enable           │
│  2. Start sound thread          2. Call sound->update()      │
│  3. Enable sound output   ───►  3. Fill buffer with samples  │
│  4. Run emulation               4. Output via audio driver   │
│  5. Disable on pause      ───►  5. Loop or sleep             │
│  6. Stop thread on exit         6. Clean exit                │
└─────────────────────────────────────────────────────────────┘
```

| Configuration | CPS2 | MVS/NCDZ/CPS1 |
|--------------|------|---------------|
| Sample Rate | 24 KHz | 44.1 KHz |
| Buffer Size | 1,600 samples | 2,944 samples |
| Channels | 2 (stereo) | 2 (stereo) |

#### MP3 Thread (`src/common/mp3.c`) - NCDZ Only

The Neo-Geo CD emulator uses a separate thread for MP3 decoding (using libmad):

| Feature | Description |
|---------|-------------|
| Decoder | libmad (MPEG Audio Decoder) |
| Buffer | Double-buffered (736×2 samples each) |
| Auto-loop | Configurable track looping |
| Seek Support | Frame-based seeking for state load |
| Sleep Handling | File re-open after PSP sleep mode |

### State Save/Load System

The state system (`src/common/state.c`) provides save state functionality with thumbnails.

#### State File Format

```
┌─────────────────────────────────────────┐
│ Offset    │ Size      │ Content         │
├───────────┼───────────┼─────────────────┤
│ 0x00      │ 8 bytes   │ Version string  │
│ 0x08      │ 16 bytes  │ Timestamp       │
│ 0x18      │ 34,048 B  │ Thumbnail       │
│ Variable  │ Variable  │ Emulator state  │
└─────────────────────────────────────────┘
```

| System | Buffer Size | Compression |
|--------|-------------|-------------|
| CPS1 | 320 KB | None |
| CPS2 | 336 KB | None |
| MVS | 320 KB | None |
| NCDZ | 3 MB | zlib |

#### Thumbnail Dimensions

| Screen Type | Width | Height |
|-------------|-------|--------|
| Horizontal | 152 | 112 |
| Vertical (CPS1/CPS2) | 112 | 152 |

#### AdHoc State Synchronization

For multiplayer, states are synchronized between PSPs:

```
Server                           Client
──────                           ──────
1. Serialize state
2. Send state (0x400 chunks) ──► 3. Receive state
4. Wait for ACK             ◄── 5. Send ACK
                                 6. Deserialize state
```

### Command List System

The command list viewer (`src/common/cmdlist.c`) displays move lists from MAME Plus! format `command.dat` files.

#### Supported Character Encodings

| Encoding | Charset Tag | Use Case |
|----------|-------------|----------|
| GBK | `$charset=gbk` | Chinese |
| Shift_JIS | `$charset=shift_jis` | Japanese |
| ISO-8859-1 | `$charset=latin1` | Western European |

The system auto-detects encoding if not specified by analyzing byte patterns.

#### Command.dat Size Reduction

The emulator includes a utility to reduce `command.dat` size by extracting only the games supported by each emulator:

1. Navigate to File Browser
2. Access the command list reduction option
3. Creates backup as `command.org`
4. Outputs optimized `command.dat`

### Cache System Details

The cache system (`src/common/cache.c`) enables running games with graphics larger than available RAM.

#### Cache Types

| Type | Description | Use Case |
|------|-------------|----------|
| `CACHE_RAWFILE` | Uncompressed cache file | Faster loading |
| `CACHE_ZIPFILE` | ZIP compressed cache | Saves storage space |

#### Cache Block Management

```
┌─────────────────────────────────────────────────────────────┐
│                    LRU Cache Algorithm                       │
├─────────────────────────────────────────────────────────────┤
│  Block Request:                                              │
│  1. Check if block in cache (blocks[] array)                 │
│  2. If cached: move to tail (most recently used)             │
│  3. If not cached:                                           │
│     a. Evict head block (least recently used)                │
│     b. Load new block from storage                           │
│     c. Insert at tail                                        │
│  4. Return memory address                                    │
└─────────────────────────────────────────────────────────────┘
```

| Parameter | Value | Notes |
|-----------|-------|-------|
| Block Size | 64 KB | Single cache unit |
| Active cache size | Runtime-selected | Chosen from the retained allocation probe and game requirements |
| Addressability limit | Target/core-specific | Format limit, not a PSP model or memory tier |

#### PCM Cache (MVS Only)

For MVS games with large ADPCM sound data, the runtime planner can reserve a
separate PCM/V-ROM cache. Its active size is selected from the same runtime
memory plan instead of a PSP model-specific build mode.

### Input System

The input system (`src/common/input_driver.c`) provides unified controller handling across platforms.

#### Key Repeat Handling

```c
Initial Delay: 8 frames (~133ms at 60fps)
Repeat Acceleration: Decreases by 2 frames each repeat
Minimum Delay: 2 frames (~33ms)
```

#### Special Input Modes (MVS)

| Game | Mode | Function |
|------|------|----------|
| irrmaze | Analog | Trackball emulation via analog stick |
| popbounc | Analog | Paddle control via analog stick |
| fatfursp | Special | Modified input polling |

### ROM Loading System

The ROM loader (`src/common/loadrom.c`) handles ZIP file extraction and ROM verification.

#### ROM Search Order

```
1. {game_dir}/{game_name}.zip
2. {game_dir}/{parent_name}.zip
3. {launchDir}/roms/{parent_name}.zip
```

#### ROM Types

| Type | Description |
|------|-------------|
| `ROM_LOAD` | Standard sequential load |
| `ROM_CONTINUE` | Continue from previous file |
| `ROM_WORDSWAP` | Byte-swap during load |

#### Interleaved Loading

For ROMs with interleaved data:
```c
group: Number of bytes to read consecutively
skip: Bytes to skip between groups
```

Example: `group=2, skip=2` reads 2 bytes, skips 2, repeating.

### Coin Counter System

The coin counter (`src/common/coin.c`) tracks coin insertions for arcade authenticity:

- Supports up to 4 coin counters
- Lockout support (prevents coin insertion)
- State is saved/loaded with save states

---
