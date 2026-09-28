# KOF '96 widescreen: observed coverage and camera limitation

The shipped `resources/mvs/widescreen/kof96.ini` is **experimental**. It changes
17 program words guarded by 53 words, for the original 3 MiB MVS program with
canonical big-endian CRC32 `cb72d23c` over bytes `[0x80, 0x300000)`.
It extends eight background-visibility gates and one foreground right-hand
rendering rejection. It does not include the camera experiment below.

## What was exercised

On the local macOS OpenGL build, fresh native and wide runs were each allowed
to reach actual demonstration combat, not just the title. Both ran 6,600
emulated frames with audio and frameskip 0. The measured rates were about
58.95--59.01 emulated frames per second with a few explicit frame captures.
These measurements describe this machine and scenario, not a platform guarantee.

The renderer-only probe left the additional side regions largely empty during
combat. The 17-word profile exposed additional background scenery. The center
crop comparison at 5-bit palette precision gave:

| Presented frame | Different pixels in the central 304x224 crop |
| --- | ---: |
| 5400 | 0 / 68096 |
| 6000 | 124 / 68096 |
| 6500 | 0 / 68096 |

The cause of the 124 differing pixels has not been established; do not report
all captures as identical. All tested wide window captures had black exterior
margins. An obvious discontinuity remained at the right-hand stage edge.
Other stages, manual play, zoom extremes and actor lifetime outside the native
view have not been exhaustively tested.

## Scene snapshot and map-edge evidence

The generic `NJEMU_MVS_DUMP_SCENE` diagnostic captured RAM, VRAM and CPU registers
at rendered frame 6500. In that snapshot A5 was `0x108000`. The background
records start at `A5+0x30ae` and have a `0x100`-byte stride. The following field
interpretations come from the original program's producer/consumer routines,
not merely from plausible numeric values:

| Record field | Meaning in the observed full-map layers |
| --- | --- |
| `+0x18` | X scroll, 16.16 fixed point |
| `+0x70` | First SCB1 word offset, divided by 64 for sprite index |
| `+0x78` | Allocated column count |
| `+0x7a`, `+0x7c` | Map width/height in tiles for this map encoding |
| `+0x80` | Map pointer |
| `+0x88`, `+0x8a` | Right/left bounds of the loaded map window |
| `+0xa4`, `+0xa8` | Main-layer camera minimum/maximum, 16.16 |

Layer 0 had X=448, camera limits 0..448, and 48 map columns (768 pixels).
Layer 1 had X=224, 34 map columns (544 pixels), and follows half of the main
scroll. Layer 3 also followed the main scroll. The snapshot's inactive records
still contained old data; they must not be mistaken for active overlapping
sprite allocations.

At `0x04fafa`, the main-layer update clamps its scroll against `+0xa4/+0xa8`
(comparisons at `0x04fb42` and `0x04fb56`). Layer 1's callback at `0x051d26`
loads the main scroll and arithmetically shifts it right by one. The full-map
lookup at `0x0078b2` divides a pixel X by 16 and wraps the column index modulo
the map width.

The original main view ends at `448+320=768`, exactly the map's right edge.
The wide view exposes `[448-40, 448+360)=[408,808)`: forty pixels are beyond
that edge and wrap to the start of the map. For the half-speed layer, the wide
right edge is `224+360=584`, forty pixels beyond its 544-pixel map. Moving only
the main camera forty pixels inward would still leave twenty excess pixels in
that layer. This confirms a finite-map/camera issue, not simply missing renderer
scissor coverage.

There is a separate loading-window concern. The full-map streaming routine at
`0x00769e` tests `camera+336` and `camera-16`. The observed records stored a
514-pixel window, initialized by the `0x006bd2` path using offsets -97 and +417.
The other map mode initialized at `0x006b38` uses a 352-pixel window and a
different streaming routine. Increasing thresholds without respecting the
corresponding window size can make a refill loop oscillate. Fixing stale
columns also does not invent artwork beyond the finite map boundary.

## Camera experiment: not shipped

An isolated copy of the profile changed only the observed stage's camera-limit
initializers at `0x051b92` and `0x051b9a`: the minimum became 80 and maximum 368,
instead of 0..448. Those limits fit a 400-pixel aperture inside both the main
768-pixel map and the half-speed 544-pixel map:

- main aperture at maximum: `[328,728)`;
- half-speed aperture at maximum: `[144,544)`.

This 19-word trial did move the scroll inward. However, the same demonstration
sequence no longer followed the original combat progression: by capture 6500
it had already moved to the score-ranking screen, while the unmodified camera
was still in combat. That is a behavioral regression signal, not proof that a
particular collision formula changed. The trial was **not promoted** to the
shipped profile, and no game ROM file was changed.

A next implementation should distinguish a visual camera from camera-dependent
fighter constraints and stage logic, preserve the intended fight area, account
for each parallax layer and validate stage transitions. A constant global clamp
should not be called a universal widescreen solution. Keeping this example
experimental and documenting its failure is part of the profile-validation
workflow.

## Reproducing the investigation

Use the generic commands documented in `MVS_WIDESCREEN_PROFILES.md`: export the
actual loaded program, create a draft profile, scan and disassemble candidate
ranges, capture native/wide gameplay and inspect a scene snapshot. These
commands do not distribute or rewrite ROM data. The code addresses above apply
only to the fingerprinted revision, and RAM field meanings apply only to the
identified background structures.
