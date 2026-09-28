# Metal Slug 3: experimental Desktop widescreen

This mode renders additional scene content rather than stretching the native
image. It currently supports the original encrypted MVS `mslug3` program
(`INIT_mslug3`), after decryption. Other revisions and clones are not assumed to
share its instruction layout.

The game-specific instructions now live in
[`resources/mvs/widescreen/mslug3.ini`](../resources/mvs/widescreen/mslug3.ini),
not hardcoded C. See [the profile authoring guide](MVS_WIDESCREEN_PROFILES.md)
for the generic engine, KOF '96 research profile and command-line tools.

## Enabling it

On Desktop, select the existing **16:9** screen-size preset for the supported
set, or start the emulator with:

```sh
NJEMU_MVS_TRUE_WIDE=1 ./MVS
```

The environment override works in both debug and `RELEASE=ON` builds, including
`GUI=OFF`. With no override, changing away from the 16:9 preset restores the
original program instructions. A value beginning with `0`, or an empty/unset
override, stops forcing widescreen; it does not override a selected 16:9 preset.

The startup log confirms successful activation:

```text
[MVS_WIDE] mslug3: 400x225 viewport; profile enabled.
```

Unsupported or modified program layouts are rejected before any patch word is
written. They retain legacy rendering. In particular, the existing 16:9 preset
for games without an authorized matching profile is still an ordinary
presentation/stretch setting. Non-Desktop platforms do not activate profiles.
Keep `widescreen/mslug3.ini` alongside the runtime resources. An optional
`NJEMU_MVS_PROFILE_DIR` selects a different profile directory without rebuilding.

### Building and preparing a standalone runtime

Use a separate build/runtime directory, not `resources/`. For example:

```sh
cmake -S . -B build_mslug3_wide \
  -DTARGET=MVS -DPLATFORM=DESKTOP -DGUI=OFF -DUSE_DESKTOP_GL=ON \
  -DRELEASE=ON -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON \
  -DFETCHCONTENT_FULLY_DISCONNECTED=ON
cmake --build build_mslug3_wide -j4
ctest --test-dir build_mslug3_wide --output-on-failure
```

That configuration deliberately disables dependency fetching. It needs a
compatible installed miniz package. When using an already unpacked miniz 3.1.2
source tree instead, add
`-DFETCHCONTENT_SOURCE_DIR_MINIZ_UPSTREAM=/absolute/path/to/miniz-3.1.2`
to the configuration command.

This example selects the existing OpenGL 3.3 backend, which keeps the indexed
atlas/palette work on the GPU. It requires a compatible OpenGL context and
currently supports `GUI=OFF`. Use `-DUSE_DESKTOP_GL=OFF` for the ordinary SDL
renderer, including GUI builds; the widescreen patch works with either backend.

Supply the same legally obtained ROM/BIOS and cache data used by a normal MVS
run: `roms/mslug3.zip`, `roms/neogeo.zip`, and the corresponding data under
`cache/`. No ROM or decrypted program data is included with this feature.
For `GUI=OFF`, select the game **after** configuring/building, because resource
staging can replace `game_name.ini`:

```sh
cd build_mslug3_wide
printf 'mslug3\n' > game_name.ini
NJEMU_MVS_TRUE_WIDE=1 ./MVS
```

Keep runtime memory-card, NVRAM, configuration, logs and frame dumps in that
runtime directory. Do not point writable runtime-state directories at the only
copy of an existing installation's saved data.

### Normal play and macOS launch priority

Launch from a normal interactive application or terminal, not a background
automation-service child when evaluating frame pacing. On the tested M1 Pro,
the same release/OpenGL executable measured **59.17 emulated fps** when launched
through macOS LaunchServices, compared with **41.58 fps** as a child of the
connector. Both runs used audio, speed limiting, frameskip 0, no frame dumps,
no SCB4 trace, and 1,800 emulated frames; the nominal MVS rate is 59.185606 fps.
Those measurements exclude ROM loading and compare 1,799 frame intervals.

A temporary diagnostic showed the background-launched test requesting 8.097 s
of sleep but actually spending 24.889 s asleep. The interactive launch spent
21.809 s asleep for 20.288 s requested and completed those intervals in
30.406 s. The temporary per-wait diagnostic was removed after identifying this
launch-environment effect; there is no timer-priority override or busy-wait
workaround in the emulator. These are measurements of this machine and this
scene, not a universal performance guarantee.

Keep the normal play environment free of `NJEMU_TEST_FAST`, frame limits,
capture controls and writer traces. `NJEMU_MVS_TRUE_WIDE=1` is the only override
needed to enable the view. The local validation directory also contains an
isolated `.app` launcher so this workspace can be started through LaunchServices
without inheriting the connector's background execution class.

## Geometry and presentation

The source image is **400 × 225**. Its central 304 × 224 region is the native
NJEMU view: crop the wide image at `(48, 1)` to recover that region. There are
48 additional pixels on each horizontal side of this native crop. The FIX/HUD
layer stays centered instead of being stretched or moved to the new edges.

The renderer uses a +80 work-frame X bias and wraps the hardware coordinates
at 512. This exposes hardware X `-40..359`. The source rectangle is
`(40,15)..(440,240)`; active drawing remains on Y `16..239`. The extra line at
the top is backdrop, not a cropped gameplay scanline. This makes the source
exactly 16:9 without discarding any of the native 224 active lines.

The Desktop presentation fits that aspect ratio directly to the backend size:
640 × 480 receives a centered 640 × 360 image; 1280 × 720 receives 1280 × 720.
The ordinary native presets retain their existing sizing behavior. The OpenGL
backend reports its existing 960 × 544 logical canvas, receives a centered
960 × 540 game rectangle and then letterboxes that canvas into the physical
window; small canvas margins remain black.

The SDL backend clears the complete window target to opaque black before each
game-image transfer, resetting window-local viewport and clipping state first.
This masks the top/bottom letterbox margins and erases old image areas when
switching modes. Clearing only the work texture is insufficient: the window
pixels outside the destination rectangle would otherwise remain undefined or
show stale graphics.

## Why widening the renderer alone is insufficient

The original program discards sprite columns outside its native horizontal
window. Independently, its background writer assigns zero SCB3 height to
columns outside the same window. Increasing only NJEMU's viewport therefore
exposes mostly blank margins.

[`src/mvs/wide_profile.c`](../src/mvs/wide_profile.c) validates and applies the
INI's guarded substitutions in the emulator's host-endian program memory.
`src/mvs/wide.c` now owns only the shared geometry. The MS3 filters are:

| Filter | Original | Wide |
| --- | --- | --- |
| 32 sprite drawing variants, X in 10.6 fixed point | bias `0200`, span `5000` | bias `0e00`, span `6800` |
| 32 unrolled background columns, X in 9.7 fixed point | add/subtract bias `0400`, common span `a000` | bias `1c00`, span `d000` |

Both wide filters admit column origins in `[-56,360)`, allowing a 16-pixel
column to overlap the left edge of the visible `[-40,360)` interval. The
background coordinates additionally wrap at 512.

While wide mode is active, the background writer also refreshes active columns
when its camera is stationary. Two NOPs replace its unchanged-coordinate early
return. This prevents a mode change or a native save-state's cached SCB3 heights
from keeping the new side regions blank. Inactive-layer and sprite-pool checks
remain in place.

In total the patch changes **131 16-bit words**. Every expected instruction,
branch, immediate value and uniform native/wide state is checked first. A
mismatch, truncated buffer or mixed state leaves the entire program untouched.
The generic engine also fingerprints the entire decrypted program after byte
0x7f, not just the patch sites, to reject different revisions with similar code.
The fingerprint excludes the BIOS-replaced vectors and normalizes patched words
back to their original values, so both enable and restore can be checked.
Disabling the mode restores the original words. Mode transitions occur between
emulated frames, before CPU execution resumes; ROM ZIPs and cache files are not
patched.

There are no intentional camera, player-movement, collision, spawning or
sprite-pool-limit changes. In particular, `A4+4` and `A4+6` in the common sprite
writers are **sprite allocation indices**, not horizontal clipping bounds.
Increasing those values is not a widescreen fix.

## Verification and diagnostics

[`tests/mvs_wide_tests.c`](../tests/mvs_wide_tests.c) checks the native/wide
geometry, all 65,536 fixed-point input values, wrapped background positions,
output aspect fitting, exact patch/restore behavior, idempotence, and failure
without partial writes. Its assertions remain enabled in release test builds.
It uses synthetic instruction fixtures, not copied ROM contents.

A recorded real-ROM comparison of the same scene at presented-frame 120 gave
400 × 225 versus 304 × 224. The central crop had zero differing pixels at the
emulator's 5-bit-per-channel palette precision (68,096 pixels compared). Each
new outer 40-pixel strip contained scene content on all 224 active lines;
the renderer-only baseline had black margins. Frame 60's central crop also
matched; its fixed-width introductory artwork did not fill the new margins.
These are source-frame dumps, not screenshots of the scaled desktop window.

After the INI migration, the independent instruction fixture still checks all
131 changed words, and the real program passes fingerprint and exact-restore
validation. Fresh normal-play OpenGL runs activated the file-backed profile at
about 59.1 emulated fps. Their captures at presented frames 1200 and 1700 were
not pixel-identical to separately booted native runs (44,019 and 52,665 differing
center pixels). Visual inspection showed different camera/animation states;
their temporal correspondence and the cause of that difference have not been
established. Those later pairs are not counted as passed center-preservation
tests or a replacement for the earlier synchronized comparisons. The actual
window margins were black in both tested wide captures.

[`tests/desktop_presentation_tests.c`](../tests/desktop_presentation_tests.c)
separately exercises the actual SDL backend with a software renderer and dummy
window. It seeds the window with magenta pixels, a stale viewport and a small
clip rectangle, then reads every window pixel after wide and native transfers.
The regression failed on the unmodified backend at window pixel `(0,0)` and
passes with black margins, correct game placement and no old wide-image pixels
after changing back to native. It requires no ROMs or desktop screen capture.

Existing Desktop capture controls can reproduce such comparisons:

```sh
NJEMU_MVS_TRUE_WIDE=1 NJEMU_TEST_FAST=1 \
  NJEMU_TEST_FRAME_LIMIT=2200 NJEMU_DUMP_FRAMES=60,120 \
  NJEMU_DUMP_DIR=captures-wide ./MVS
```

Use separate, equivalently initialized runtime directories for native and wide
runs. `NJEMU_TEST_FRAME_LIMIT` counts emulation-loop frames, while
`NJEMU_DUMP_FRAMES` counts presented frames; frame skipping means these are not
the same counter. `NJEMU_TEST_FAST` is a no-GUI test setup that disables the
normal speed limiter/audio and increases frame skipping, not a play setting.
In particular, it presents only one out of twelve emulated frames, so the test
window looks very choppy even when emulation itself is fast. For normal play,
leave `NJEMU_TEST_FAST`, `NJEMU_TEST_FRAME_LIMIT`, `NJEMU_DUMP_FRAMES` and
`NJEMU_MVS_TRACE_SCB4` unset; only `NJEMU_MVS_TRUE_WIDE=1` is needed.

An explicitly frame-limited Desktop run prints one timing summary on completion,
excluding ROM loading. The summary measures emulated frame intervals and lists
frameskip, audio and speed-limit settings; it does not log every frame. With
frameskip enabled, emulated frames per second are not presentation frames per
second. Runs without a test frame limit do not print this summary.

For writer diagnostics in a non-release Desktop build, add
`NJEMU_MVS_TRACE_SCB4=1`. The trace groups SCB4 writes by the actual current
68000 instruction PC, tracks up to 128 writer addresses and reports the busiest
writers. Samples labelled `sprite_pool` describe allocation indices; reads are
restricted to aligned work RAM. This instrumentation is not required for the
widescreen patch and is excluded from release builds.

## Coverage limits

The supported profile is intentionally narrow. It has been checked with the
original MVS program and representative introductory/demo scene captures, not
an exhaustive playthrough of every mission, transition, ROM clone or save
state. Fixed-width logos, menus or cutscene artwork may still have empty side
areas; the patch does not invent artwork. Original gameplay boundaries and
object lifetime rules also remain in effect. Add another revision only after
verifying its code and adding a separate guarded profile and tests.
