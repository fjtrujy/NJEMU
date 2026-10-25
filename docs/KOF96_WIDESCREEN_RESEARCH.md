# KOF '96 and KOF '98 widescreen: finite stage maps and a shifted view

`resources/mvs/widescreen/kof96.ini` is a **verified** version 2 profile for the
original 3 MiB MVS program, canonical big-endian CRC32 `cb72d23c` over bytes
`[0x80, 0x300000)`. It changes 64 program words guarded by 134 words and
describes the game's stage-layer records in a `[layers]` section. Camera,
walls, collisions, fighter logic and the HUD are not modified.

## The problem: every stage map is 768 pixels wide

The background records start at `0x10b0ae` (`A5+0x30ae`, A5 = `0x108000`)
with a `0x100`-byte stride. The scheduler at `0x9140` runs the callback in a
record's first long word while bit 7 of byte `+0x90` is set. The fields used:

| Record field | Meaning |
| --- | --- |
| `+0x00` | Callback (long); its high word is only the callback's 64 KiB bank |
| `+0x18` | X scroll, 16.16 fixed point; the main layer's X is the camera |
| `+0x70` | First SCB1 word, divided by 64 for the first sprite |
| `+0x78` | Allocated sprite columns |
| `+0x7a` | Map width in 16-pixel tiles |
| `+0x88`, `+0x8a` | Right/left bounds of the streamed map window |
| `+0x90` | Bit 7 of the byte: the layer is running |
| `+0xa4`, `+0xa8` | Main-layer camera minimum/maximum, 16.16 |

The stage dispatcher at `0x4fc32` indexes a table at `0x4fc48` with byte
`$27e8(a5)`. All eleven indices (0 to 10) were exercised: the main layer is
always record 0, 48 tiles wide (768 pixels), with camera limits `0..448`.
The native 320-pixel view ends exactly at the map edge, `448+320 = 768`.
A 400-pixel view centered on the same camera covers `[X-40, X+360)`: at both
camera limits forty pixels fall outside the map. The full-map lookup at
`0x0078b2` wraps column indexes modulo the map width, so those pixels showed
the opposite end of the stage. Parallax layers run out sooner or later than
the main layer: for example a 544-pixel layer at half speed, a 512-pixel one
at about 0.43, and 864- to 896-pixel foreground layers at about 1.29.

Moving the camera limits inward is not a fix. The walls and the fighters'
screen bounds follow the camera, so a narrower camera range changes the
fight area.

## The solution: shift the view, not the camera

Near an edge, the view is shifted inward by up to 40 pixels: the shift is
`clamp(0, 40-X, W-360-X)` for a layer with scroll X and map width W pixels,
bounded to `[-40,40]`. It is zero whenever the centered view already fits.
At camera 0 the view shows `[0,400)` and at camera 448 it shows `[368,768)`;
the fighters move toward the screen edge instead of the stage scrolling past
its end. The gameplay camera is unchanged.

- **Stage layers** are shifted by the renderer. Every frame, before the CPU
  runs, `wide_layers_pan()` reads the `[layers]` records and fills a
  per-sprite shift table that `draw_sprites()` adds to each chain origin.
  Each running streamed layer (a map wider than its resident columns) at
  least 400 pixels wide gets its own shift, so every parallax layer stays
  inside its own map and keeps moving in its own direction. Other running
  layers (fully resident scenery pieces such as signs) follow the main layer. With no running main layer at least 400 pixels wide, every
  shift is zero: menus, intros and ranking screens use narrower maps or stale,
  stopped records.
- **World objects** are shifted by the game. A routine in the unused `0xffff`
  fill at `0x6be00`, called from the object-part producer at `0x53b2`,
  subtracts the main layer's shift from the part's screen X for objects not
  fixed to the screen. Bit 4 of `$3b(a4)` marks those screen-fixed objects:
  the draw loop at `0x50ae` skips the camera transform for them: HUD sprites
  such as the portraits, "ROUND" and "MAXIMUM" stay centered. The life
  bars, timer, names and POW gauges are FIX tiles and never move.

The two use the same camera value. The game updates the layers (scheduler,
`0x90f6`) before drawing objects (`0x5090`) in the same logic frame, and the
vblank upload of that frame's sprite list is what the next emulated frame
displays. Measured at frameskip 0 over 14,000 frames, whenever the VRAM
column positions at a render differed from the RAM X at that render (329
frames, all with a moving camera), they matched the RAM X present when that
frame started.

### Supporting changes

- The eight SCB3 height gates (`0x6fe0`...`0x7308`) no longer hide columns.
  Each streamed layer has 32 columns, a ring that is unique modulo the
  512-pixel hardware wrap, so every column is drawn at its true position and
  the renderer clips the shifted image to the view.
- The streamer at `0x769e`, called only by the 21 stage callbacks, keeps
  `[X-48, X+376)` loaded instead of `[X-16, X+336)`. Its 514-pixel windows
  (`0x6bd2`) leave 90 pixels of hysteresis, so the refill loops cannot
  oscillate. The 352-pixel windows of `0x6b38` are never streamed by it.
- Object parts are rejected from the right at 360 instead of 320, and from
  the left only when they end before -40 (`0x6be60` stores `-16*width-40`
  in `$6018(a5)`). Both bounds are applied to the shifted position.

## Validation

Validated on the local macOS OpenGL build (Desktop, `GUI=OFF`, non-release),
with all eleven stage indices forced in the attraction sequence by a
temporary test hook. Captures at both camera limits and at the center were
inspected at 2x along both edges: no wrapped columns, no missing actors and no
seams. The same captures show consistent fighter/floor alignment and HUD
placement. The center view is unchanged when the shift is zero.

The cost on this machine, unthrottled at frameskip 0, was 746 native versus
723 wide emulated frames per second (about 3%).

## Why the demonstration diverges from the native run

Paired native/wide demonstration runs diverge after some seconds. This is
not a gameplay change:

- the random generator at `0x2b2c` adds the current raster line
  (`$3c0006`), so any change in the cycles spent before a call changes the
  random values, the CPU teams and the demonstration;
- scene transitions also depend on CPU timing against vblank and the Z80;
- the emulated uPD4990A clock starts from host time and the demonstration
  order is kept in backup RAM, so even two native runs differ unless NVRAM
  is cleared and the clock fixed.

The patch code only rewrites the part's screen X in `d5`, the per-part
temporary `$6018(a5)`, the registers `d0`/`d1` that are reloaded before their
next use, the column gates and the streaming window. None of them is read
by the game logic.

The previous experimental profile only extended the gates and the right-hand
object bound, and kept the seams at both camera limits. Its camera-limit
trial (minimum 80, maximum 368) did narrow the fight area. Its "changed
demonstration" signal was most likely the raster-based random generator
described above.

## Known limits

- Static backdrops narrower than 400 pixels (a 320-pixel sky in two stages)
  do not scroll and keep their native width: the margins next to them show
  what the game draws there.
- Screens other than fights (intro, title, team selection, demonstration
  letterbox bars) are drawn for 304 pixels by the game and keep the
  rendered-but-unextended margins they had before.
- Manual play was not exercised. Clones and other revisions are rejected by
  the fingerprint.

## KOF '98

`resources/mvs/widescreen/kof98.ini` applies the same method to the original
6 MiB MVS program after decryption (CRC32 `c84c6f28`, NGH `0x0242`): 68 changed
words, 152 guarded words. The engine is the same with other addresses:

| Item | KOF '96 | KOF '98 |
| --- | --- | --- |
| Layer records / scheduler | `0x10b0ae` / `0x9140` | `0x10b0b4` / `0xa332` |
| Stage dispatcher index | `$27e8(a5)`, 11 entries | `$27ea(a5)`, 9 entries |
| Object-part producer | `0x53b2`, temporary `$6018` | `0x627a`, temporary `$64b6` |
| Shift routines (0xffff fill) | `0x6be00`, `0x6be60` | `0xa7e00`, `0xa7e60` |
| Native column gate | `(x+15) <= 324` | `(x+15) <= 332` |
| Stage column window | 514 pixels (`0x6bd2`) | 352 pixels (`0x7bba`, `0x7b20`) |

The important difference is the streamed window. KOF '98 keeps only
`[X-16, X+336)` up to date in its 32-column ring, while the row streamer
(vertical scrolling) reloads `[X-96, X+416)`. The profile widens both
initializers to `[X-64, X+384)` and sets the streamer (`0x8686`, called only
by stage callbacks) to the thresholds `+0x180` and `-0x40`. As in the original,
they add up to the window width, so the refill loops are stable. The window is
always inside the 512-pixel ring that the row streamer writes. Widening the
thresholds without the window would make the refill loop oscillate forever.

All nine stage indices were forced in the attraction sequence and captured at
both camera limits and at the center, with the 2x edge inspection used for
KOF '96: no wrapped columns or seams. Unthrottled at frameskip 0 the cost was
658 native versus 640 wide emulated frames per second. One content limit was
seen in the market stage (index 3): with the camera at its left limit, a narrow
gap between two buildings near the right edge shows black. The 144-pixel sky
piece behind it has its own parallax and only covers that gap when it is
inside the native view.

## Reproducing the investigation

Use the generic commands in `MVS_WIDESCREEN_PROFILES.md`: export the loaded
program, disassemble the ranges above and capture native/wide frames. For a
stage at a camera limit, the scene snapshot shows the record fields in RAM.
The addresses apply only to the fingerprinted revision.
