# CPS1 and CPS2 widescreen: Street Fighter II, Champion Edition and Street Fighter Alpha

CPS1 and CPS2 games show hardware X `[64,448)`, 384x224 pixels that the
arcade monitor displays at 4:3. NJEMU's wide view shows hardware X
`[16,496)`: 480x224 at the same pixel aspect, which is 5:3, close to 16:9.
The work frame of every backend is 480 pixels wide, so the renderer draws the
wide view shifted 16 pixels to the left (`cps1_wide_bias`, `cps2_wide_bias`).
CPS1 object X is 9 bits and unused objects parked at X 0 stay outside the
view; CPS2 object X is 10 bits. The scroll layers' vertex buffers are sized
for the 480-pixel view where it is built (`CPS_VIEW_MAX_WIDTH`).

Like MVS, a per-game profile in `widescreen/<game>.ini` enables the mode. It
uses the same file format, loader and atomic guarded patching
(`src/common/wide_profile.c`, see `MVS_WIDESCREEN_PROFILES.md`). CPS
profiles are version 1 files with `ngh = 0x0000`; the `[layers]` section is
MVS-only. On CPS1, `program_size` is the 2 MiB program region NJEMU
allocates. Encrypted CPS2 sets execute decrypted opcodes from the USER1
region, so their profiles describe and patch that region; the encrypted ROM
image is not modified.

In GUI builds the **Widescreen (16:9)** option of the game's Video settings
switches it (`Widescreen` in `config/<game>.ini`). Without the GUI, or in
tests, `NJEMU_CPS1_TRUE_WIDE=1` / `NJEMU_CPS2_TRUE_WIDE=1` forces it.
`NJEMU_CPS1_PROFILE_DIR`, `NJEMU_CPS1_ALLOW_EXPERIMENTAL=1` and their
`NJEMU_CPS2_` forms correspond to the MVS variables. The mode is built for
Desktop, PSP and PS Vita (`CPS_WIDESCREEN`) and only for horizontal screens.

## Street Fighter II: The World Warrior

`resources/cps1/widescreen/sf2.ini` is for the World 910522 program (`sf2`,
CRC32 `661a8e65`): 74 changed words, 99 guarded words. No ROM file is
modified.

### Why the view has to move

The three scroll layers come from 1024-pixel or wider tilemaps. During
fights the game keeps the scroll2 register `c` (= `$ff803a` - 64) in
`[0,384]`, so the native view sees stage X `[64,832)`. The tilemaps hold no
art outside that range: at the camera limits the wider view showed black
tiles, a cut-off mural and stepped floor rows (the floor uses per-line row
scroll). Parallax layers move more slowly: scroll3 moves 0.46 to 0.77 times
as fast as scroll2 depending on the stage.

### What the profile does

The view is shifted by `48 - c/4` pixels: 48 pixels inward at either camera
limit, none at the center. A linear shift over the whole camera range keeps
every layer and floor row that moves at least 1/4 as fast as scroll2 inside
the area the game shows natively. A shift only near the edges did not: at the
start of the ramp the slower layers moved backwards and exposed their edge.
The shift applies only while `$ff8009 == 2` (a fight) with `c` in range.

- **Scroll layers**: the vblank routine at `0x1736` writes last frame's scroll
  shadows to the CPS-A registers and computes the next ones as camera - 64.
  It now jumps to `0x1ff040`, adds the shift to that -64 for scroll1, scroll2
  and scroll3 alike, and jumps back.
- **Objects**: `0x7f546` computes an object's screen X as
  `x - camera + 64`, with the camera of the layer the object belongs to
  (`$b58`, `$bd8` or `$c58`). The four camera paths now call `0x1ff080`,
  which also subtracts the shift (computed from `$ff8bd8` - 256, the same
  scroll2 camera). Objects whose layer byte is negative skip the camera: they
  are the HUD (life bars, timer, scores, names) and keep their place.

The camera, walls and collisions are not modified.

### Constraints found in the game

- The routines live at `0x1ff000`, in the unused upper megabyte of the
  program region, which NJEMU maps and fetches like the rest of the ROM. The
  game's own ROM has no 0xffff fill, and its zero runs are palette tables.
- The routine at `0x931e` scans the vblank code `0x166a-0x1832` for NOP
  (0x4e71) and jumps to the test menu if it finds one. The scroll patch
  therefore leaves the original bytes after its jump instead of NOP padding.
- The vblank code runs with the supervisor stack at the top of work RAM.
  The scroll patch jumps out and back and uses only d0, which the original
  code reloads next. It also writes only the low word of d1.

### Validation

Validated on the local macOS OpenGL build (Desktop, `GUI=OFF`, non-release)
over the attraction sequence, which shows four stages (Guile, E. Honda,
Dhalsim and Ken/Ryu): captures at both camera limits, along the ramps and at
the center, inspected at 2x along both edges. No black columns, cut-off
layers or stepped floor rows remained, fighters stayed on their shadows and
the HUD stayed centered. With the option off, frames are identical to the
renderer before this change (0 pixels differ). Wide frames are identical on
the SDL and OpenGL backends.

The other four stages (Chun-Li, Zangief and the bosses) do not appear in the
attraction sequence and were not exercised. Manual play was not tested.

### Known limits

- At the start of a round on the Ken/Ryu stage, a block of the far
  background at the left margin is still black for about a second while the
  game finishes drawing it. It is outside the native view.
- Screens other than fights (title, character select, ranking) keep the
  width the game draws them with.
- Other revisions have different code and need their own profiles;
  Champion Edition has its own (below).

## Street Fighter II': Champion Edition

`resources/cps1/widescreen/sf2ce.ini` is for the World 920513 program
(`sf2ce`, CRC32 `7778f388`): 72 changed words, 93 guarded words. The older
World 920313 set is `sf2ceea`, which has no profile.

Champion Edition keeps the World Warrior engine with its code and work RAM
moved, so the profile applies the same `48 - c/4` shift at the equivalent
places:

- **Scroll layers**: the code at `0x15a2` computes the shadows as scroll1/2/3
  = `$32`/`$36`/`$3a` - 64. The patch replaces its first six bytes with a
  jump; the routine sets d1 to -64 + shift (from `$36` - 64, the scroll2
  register value), reloads d0 as the original does and jumps back to
  `0x15aa`. It uses only d0 and d1 and no stack.
- **Objects**: the layer-camera paths at `0x98050`, `0x9807a`, `0x980b6` and
  `0x980da` call the same object routine, with the scroll2 camera at
  `$ff8bc4` (+256). The HUD keeps the screen-fixed path.
- **Fight state**: `$ff8009 == 2`, as in World Warrior.
- The routines live at `0x1ff000`. The game's three ROMs fill only the first
  1.5 MiB of the 2 MiB program region. This revision has no NOP scan over its
  vblank code.

Validated on the local macOS OpenGL build (Desktop, `GUI=OFF`, non-release)
over about 40,000 frames of the attraction sequence. Its fights use six
stages: M. Bison/Sagat (Thailand), Ken/Guile, Vega/Chun-Li, Blanka and
Zangief (Las Vegas), Ryu/Ryu and Ryu/Sagat. No fight frame had a black
column at either edge, and the test menu never appeared. Black margins
remained only on the title, ranking, intro and character-select screens,
as in World Warrior. The SDL and OpenGL runs differed only in demo timing,
because the attraction sequence is not deterministic. Manual play and the
other stages were not tested.

## Street Fighter Alpha: Warriors' Dreams

`resources/cps2/widescreen/sfa.ini` is for the Europe 950727 program (`sfa`).
The fingerprint covers the 512 KiB decrypted opcode region (CRC32
`2baea2c3`): 65 changed words, 81 guarded words.

The engine follows the same convention as Street Fighter II: during fights
the scroll2 register `c` is in `[0,384]`, the native view sees stage X
`[64,832)` and the wider view showed black margins at the camera limits. The
same `48 - c/4` shift keeps the view inside the stages. Differences:

- **scroll1 is the HUD** (portraits, timer, names). The vblank routine at
  `0x16a4` computes the shadows as scroll1 = `$2a` - 64, scroll2 = `$2e` -
  0x100 and scroll3 = `$32` - 64. The patch jumps out after the scroll2
  write, adds the shift to the scroll2 and scroll3 shadows only and jumps
  back to `0x16de`, without using the stack.
- **Objects**: the origin routine at `0x1225a` uses the camera of the layer
  selected by byte 9 of the object (`x - camera + 64 - d0`); objects with no
  layer, such as the life bars and gauges, take the screen-fixed path at
  `0x122a8`. The patch subtracts the same shift at the end of the layer path.
  Its callers reload d0 immediately afterwards.
- **Fight state**: the shift applies while the scroll2 layer record at
  `$ff8280` is active (1), with `c` in range. Its X (`$ff8290`) is `c` +
  0x100 when objects are placed.
- The routines live at `0x7f000`, in the `0x6f088-0x7ffff` region that is
  0xffff fill in the encrypted ROM but still inside the decrypted region the
  CPU executes from.

Validated on the local macOS OpenGL build (Desktop, `GUI=OFF`, non-release)
over the attraction sequence, whose fights use seven stages (Ryu/Ken,
Adon/Sodom, Rose/Sagat, Charlie/Guy, Guy/Birdie, Chun-Li/Rose and
Charlie/Chun-Li): captures at both camera limits and at the center, inspected at
2x along both edges. No black margins remained, fighters stayed on the floor
and the HUD stayed in place. With the option off, frames are identical to
the renderer before this change, and wide frames are identical on the SDL
and OpenGL backends. The last frame of each demonstration fight fades to
black in both modes.

Before the vertex buffers were resized, the wider view overflowed them and
some scroll1 HUD tiles disappeared; the same overflow could occur on CPS1.
Manual play and the stages missing from the attraction sequence were not
tested. Other revisions and clones need their own profiles.
