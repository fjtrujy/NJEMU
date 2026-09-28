# MVS widescreen profiles and research tool

The renderer and patch engine are shared. Game-specific instructions are plain
INI files, loaded at runtime from `widescreen/<game_name>.ini`. Adding or editing
a profile does not require recompiling NJEMU; restart the game to reload it.

This is an assisted research workflow, not a universal automatic ROM patch.
The tool finds candidate code, fingerprints a revision, checks substitutions
and compares captures. It cannot infer from a constant alone whether that
constant controls rendering, sprite allocation, camera movement or collisions.

## Running existing profiles

Profiles are staged with the MVS resources by CMake. The current files are:

| File | Status | Scope |
| --- | --- | --- |
| `widescreen/mslug3.ini` | `verified` | Original decrypted MVS revision; 131 changed words, 487 guarded words. Prior scene comparisons and independent instruction tests retained. |
| `widescreen/kof96.ini` | `experimental` | Original KOF '96 revision; eight background visibility gates and the foreground right-hand drawing rejection. Stage-edge and off-screen-actor coverage is incomplete. |

`verified` records the author's tested profile, not exhaustive compatibility
with every mission, state, clone or gameplay situation. `experimental` requires
explicit opt-in. `draft` is never activated by the emulator, even with opt-in.

KOF '96 has a known finite-map discontinuity at a camera edge. The attempted
camera clamp also changed the demonstration's progression and is not shipped.
See [the recorded KOF '96 investigation](KOF96_WIDESCREEN_RESEARCH.md) for exact
coverage, paired-capture differences and the camera/parallax evidence.

For normal play from a prepared MVS runtime directory:

```sh
# Metal Slug 3: put mslug3 in game_name.ini for a GUI=OFF build.
NJEMU_MVS_TRUE_WIDE=1 ./MVS

# KOF '96: put kof96 in game_name.ini; explicitly allow the experimental profile.
NJEMU_MVS_ALLOW_EXPERIMENTAL=1 NJEMU_MVS_TRUE_WIDE=1 ./MVS
```

The existing 16:9 menu preset also requests wide mode. Without a forced
`NJEMU_MVS_TRUE_WIDE` override, changing away from that preset restores the
original program. The override does not force native mode when set to zero;
it merely stops forcing widescreen.

`NJEMU_MVS_PROFILE_DIR=/path/to/profiles` overrides the profile directory,
including in release builds. No fallback occurs if a requested directory or
profile is missing or invalid. The normal native/stretch behavior remains
available. On a failed transition from an already active profile, the engine
leaves the current instructions and geometry unchanged and logs the failure.

Only Desktop activates profiles at present. Both SDL and OpenGL rendering can
use them. OpenGL currently requires `GUI=OFF`. Keep test frame skipping, frame
limits, dumps and traces unset for normal play. On macOS, launch through a normal
interactive app/terminal when evaluating performance, not as a background
service child; see the measured launch effect in `MSLUG3_WIDESCREEN.md`.

## File format, version 1

Version 1 deliberately fixes geometry to **400x225** with a centered native
304x224 region and FIX layer. It does not expose arbitrary dimensions or camera
hooks. Hardware X is still 9-bit wrapped; the visible world interval is
`[-40,360)`. Source coordinates are `(40,15)..(440,240)` with work-frame X bias
80. The native image occupies `(48,1)..(352,225)` inside a wide source dump.

A minimal illustrative profile looks like this. The fingerprint and addresses
below are placeholders, NOT a patch for a real game:

```ini
[profile]
version = 1
game = example
ngh = 0x0000
program_size = 0x100000
program_crc32 = 0x00000000
status = draft
description = Research profile, not yet visually validated

[patch drawing-limit]
offsets = 0x2000, 0x2400
original = 303c 0140 4e75
wide = 303c 0168 4e75
```

Every `offsets` entry is a **byte offset into the complete decrypted program**,
not an index of words and not necessarily a banked 68000 CPU address. The words
in `original` and `wide` are always hexadecimal 16-bit values. Unchanged words
serve as instruction guards; include opcodes, branches and nearby context, not
only the immediate value that changes. Lists must have equal nonzero length.

Numbers outside the two word lists are decimal unless prefixed by `0x`. A
regularly repeated instruction group can be expressed without listing every
address:

```ini
[patch repeated-columns]
offsets = 0x3000
repeat = 32
stride = 0x1e
original = 0646 0400 b646 6300 0004 de41 0446 0400 2886
wide = 0646 1c00 b646 6300 0004 de41 0446 1c00 2886
```

`repeat` defaults to 1; `stride` defaults to 0 and is measured in bytes. This
syntax is used by the actual Metal Slug 3 profile. For a renderer-only research
probe with no program changes, explicitly set `viewport_only = 1` and omit
patch sections. This does not fix a game's internal clipping.

Metadata requires `version`, `game`, `ngh`, `program_size`, `program_crc32` and
`status`. Description and viewport-only mode are optional. Game identifiers
contain lowercase ASCII letters, digits, underscore or hyphen, at most 15
characters. Files are ASCII; `#` and `;` start comments anywhere on a line.
Values must fit on a single line; do not put semicolons in a description.

The loader rejects unknown or duplicate fields/sections, unaligned addresses,
overlaps (including overlapping guards), zero repeats, out-of-range writes,
mismatched word-list lengths, embedded control bytes and oversized input.
Limits are 64 KiB per profile, 2047 bytes per line, 128 patch sections, 128
numbers per list, 128 repetitions and 4096 expanded guard words. Programs are
even-sized and limited to 64 MiB. Patches cannot write below offset `0x100`.

## Revision identity and atomic application

The fingerprint is CRC32/ISO-HDLC of the decrypted program in canonical
**big-endian byte order**, from byte `0x80` through `program_size - 1`. The first
128 bytes are excluded because the BIOS replaces the interrupt vectors.
Do not hash the ZIP or a host-endian memory dump. CRC32 is a revision mismatch
check, not a cryptographic authenticity guarantee.

At each transition, the engine validates the full program size/fingerprint,
every guard and a uniform native-or-wide state before writing anything. For a
wide input it substitutes each profile word's original value while computing
the fingerprint. A mixture of native and patched words is rejected rather than
silently completed. A mismatch elsewhere in the program is also rejected.
The runtime separately checks the game name, NGH and authorization status.

The patch operates only on the emulator's in-memory program. It does not write
the ROM ZIP, cache or profile. Applying twice is idempotent; restoring must
recover the exact native program. Transitions happen between emulated frames.
There is no script execution, arbitrary RAM writing or per-frame file parsing
in a profile.

## Authoring workflow

### 1. Export the program that NJEMU actually uses

Use your own ROMs and an isolated runtime directory. A non-release Desktop build
(`RELEASE=OFF`) can export the post-load, decrypted program before profile
application:

```sh
NJEMU_TEARDOWN_TEST=1 NJEMU_MVS_DUMP_PROGRAM=/new/path/game.be ./MVS
```

Use the normal resource/configuration setup for that game. In `GUI=OFF`, its set
name comes from `game_name.ini`. The dump destination must not already exist.
Check the log for `Program dump written`; the export is disabled in release
builds. No ROM content is distributed with this tool, and dumps belong outside
tracked source directories.

### 2. Create an inert template and inspect candidates

From the source checkout:

```sh
python3 tools/widescreen.py draft --program /path/game.be --game game \
  --output /path/profiles/game.ini
python3 tools/widescreen.py scan --program /path/game.be --output /new/path/scan.json
python3 tools/widescreen.py disasm --program /path/game.be --start 0x2000 --end 0x2040
```

Drafting extracts the NGH from the Neo Geo header and calculates the fingerprint.
The draft remains inert until explicitly reviewed and promoted. The first two
commands need only Python 3's standard library. `disasm` optionally uses
Capstone's M68000 decoder and reports an incomplete/unaligned decode rather
than silently skipping data.

The scanner searches both likely width constants and the structural pattern
`ADDI.W #bias,Dn; CMPI.W #limit,Dn; BHI/BCC`, without requiring known constants.
The second method finds KOF '96's different bounds as well as Metal Slug's.
Results are candidates, not proof of executable code, coordinate units or a
safe substitution. The default scan covers the fixed first MiB; use `--start`
and `--end` for other program offsets. A banked CPU address needs the actual
bank mapping before it can be compared to a file offset.

An authorized viewport-only experimental probe can enable SCB4 tracing in a
non-release build:

```sh
NJEMU_MVS_PROFILE_DIR=/path/profiles NJEMU_MVS_ALLOW_EXPERIMENTAL=1 \
  NJEMU_MVS_TRUE_WIDE=1 NJEMU_MVS_TRACE_SCB4=1 ./MVS
python3 tools/widescreen.py scan --program /path/game.be --trace /path/run.log
```

The trace reports actual writer instruction PCs and write counts. `--writers-only`
filters candidates to those near fixed-region writer PCs. Buffered writers
can be far from the real culling decision: in KOF '96 the `0x9bxx` routine
uploads a RAM table, while the producer's SCB3 height decisions are at `0x6fxx`
through `0x73xx`. Trace proximity is only a navigation aid.

A non-release Desktop build can also export one read-only scene snapshot to
inspect actual RAM values, CPU registers and sprite-control blocks:

```sh
NJEMU_MVS_DUMP_SCENE=/new/path/scene.json NJEMU_MVS_DUMP_SCENE_FRAME=6500 ./MVS
python3 tools/widescreen.py scene /path/scene.json --ram-start 0x100000 --words 32 \
  --sprite-start 1 --sprites 32
```

The number counts rendered game frames since game initialization, not wall time.
The JSON contains numeric 16-bit RAM/VRAM words and CPU register values, making
it independent of host byte order. The tool bounds and validates the snapshot
before reporting a requested slice. SCB4 X values are raw: sticky columns inherit
their chain's position and height. This is an inspection export, **not a loadable
save state**. Existing files are not overwritten, and release builds omit these
exports. Leave both variables unset for normal play; writing a snapshot can
briefly pause the explicitly selected test frame.

### 3. Write guarded substitutions and validate them

Follow the candidate's inputs and branches to distinguish signed pixel values,
fixed-point coordinates, wrap masks, sprite-pool indices and gameplay bounds.
Edit the INI's patch sections, set `viewport_only=0` or omit it for real patches,
and leave the status as `draft` until the instructions have been reviewed.

```sh
python3 tools/widescreen.py validate /path/profiles/game.ini
python3 tools/widescreen.py validate /path/profiles/game.ini --program /path/game.be
```

The second command validates identity and instructions, applies the profile in
a private memory copy, and checks exact restoration and idempotence. It never
writes a patched program. `--output /new/path/report.json` saves a report;
existing reports/profiles are never silently overwritten. Validation succeeds
for a valid draft as an offline check; the emulator still refuses to activate it.
After instruction review, use `status=experimental` with explicit runtime opt-in.

### 4. Compare actual gameplay, not only the title

Use equivalently initialized native and wide runs with the same input sequence.
Capture source frames using `NJEMU_DUMP_FRAMES` and separate `NJEMU_DUMP_DIR`
directories; ensure directories exist first. `NJEMU_TEST_FRAME_LIMIT` counts
emulated loop frames, while dump numbers count presented frames. Avoid
`NJEMU_TEST_FAST` when comparing normal-play timing or judging smoothness.

```sh
python3 tools/widescreen.py compare --native /path/native/gl_05400.ppm \
  --wide /path/wide/gl_05400.ppm --require-center-match \
  --output /new/path/comparison.json
```

The comparison quantizes colors to the emulated 5-bit palette precision and
checks the central 304x224 crop. It reports nonblack pixels and color diversity
in each 48-pixel side strip. Colored margins alone do not prove correct scene
content: inspect for repeated tiles, uninitialized map columns, disappearing
actors, scrolling seams, finite-width logos, screen transitions and changes in
the native center. Scene timing or random-seed differences also invalidate a
strict pixel comparison. The separate SDL presentation regression checks the
actual window margins, not just a source crop.

A game may need more than culling changes. Background tile streaming, camera
edges and actor lifetime can remain native-sized even after the viewport is
wide. For example, expanding both tests of a fixed-width streaming window
without changing its initialization consistently can create an oscillating loop.
Do not treat sprite counts or allocator bounds as image dimensions.

## Tests and extension points

`tests/mvs_wide_tests.c` retains an independent Metal Slug instruction fixture
and tests the generic C engine, geometry, failure atomicity and exact restoration.
`tests/widescreen_tool_tests.py` tests both shipped profiles, full fingerprints,
malformed inputs, the structural scanner and PPM comparison. CTest runs a shared
valid/invalid profile corpus through both the Python and actual C parsers to
catch acceptance differences. These tests do not require ROMs.

The initial profile rollout was compiled and tested on macOS/arm64 in seven
Desktop configurations: MVS debug SDL (16 CTest entries), MVS release SDL (16),
MVS release GUI (17), MVS release OpenGL (16), CPS1 (14), CPS2 (14) and NCDZ (15).
All entries passed on the final source snapshot. The C profile tests and the
11 Python tool tests, including C/Python parser parity, also passed with the C
engine instrumented by AddressSanitizer and UndefinedBehaviorSanitizer.

Four additional local ROM-backed release checks confirmed that unauthorized
experimental profiles, drafts and a wrong full-program fingerprint leave the
source at native 304x224, while explicit experimental opt-in produces 400x225.
Release builds did not create requested debug program/scene exports. These
checks validate authorization and dimensions, not complete artwork or gameplay
coverage; the game-specific documents record the visual limitations separately.

The current split makes a future game a data/profile and validation task, not
a copy of the renderer. Future profile versions could add reviewed geometry or
scene-aware behavior, but must retain explicit revision identity, bounded
operations and failure without partial writes. Automatic scanning should remain
separate from authorization and visual/gameplay acceptance.
