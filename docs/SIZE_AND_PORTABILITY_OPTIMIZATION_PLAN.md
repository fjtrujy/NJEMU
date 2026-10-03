# NJEMU Size, Performance and Portability Optimization Plan

Date: 2026-09-28

## 1. Purpose

This plan defines the next optimization phase after the platform-driver refactor,
reactive-memory work, ZIP/miniz migration, and PS Vita integration.

The priorities are deliberately strict:

1. **Never regress gameplay performance.** CPU, renderer, memory handlers and
   audio hot paths must remain at least as fast as the current implementation.
2. **Reduce executable/static-memory cost whenever possible.** On PSP-1000 and
   PS2, every avoidable byte of executable/static state reduces the memory left
   for ROM/cache data.
3. **Accept portability/usability improvements only when they do not make the
   existing platforms worse.** A cleaner abstraction is not sufficient if it
   adds per-frame copies, allocations, synchronization or measurable binary
   growth without a justified benefit.
4. **Prefer removing duplicate functionality over adding abstraction layers.**
   Shared code should exist because the semantics are genuinely shared, not
   merely to reduce file count.
5. **Measure before and after every size/performance milestone.** No optimization
   is complete without a reproducible delta.

This document complements `docs/BINARY_SIZE_AUDIT.md`,
`docs/PLATFORM_DRIVER_REFACTOR_PLAN.md`, and
`docs/REACTIVE_MEMORY_POLICY_PLAN.md`. Those documents remain the historical
record for the completed work; this plan owns the next phase.

## 2. Non-negotiable acceptance rules

Every phase must preserve the following invariants unless the phase explicitly
states otherwise:

- no additional work in CPU/audio/render hot paths without a measured win;
- no new permanent heap allocation that merely replaces `.bss`;
- no increase in PSP/PS2 runtime image or executable size without an explicitly
  documented reason and compensating benefit;
- no platform SDK types leaking back into target/common renderer code;
- no PSP/PS2 cost for Vita/Desktop-only developer infrastructure;
- GUI OFF builds must not retain GUI/PNG/screenshot implementation code;
- existing ZIP, save-state, translation, screenshot and NCDZ PNG behavior must
  remain functionally compatible unless intentionally versioned;
- no changes under `resources/`;
- real PSP/PS2 hardware remains authoritative for hot-path performance results.

For binary-size work, record at minimum:

- `.text`;
- `.rodata`;
- `.data`;
- `.bss`;
- runtime image (`text + rodata + data + bss`, or the platform-equivalent
  section accounting used by the existing audits);
- ELF/PRX size;
- EBOOT.PBP size on PSP where applicable.

For renderer/timing work, also record representative frame time/FPS and any
relevant submission/cache counters. For cold-path changes such as PNG encoding or
ROM-load setup, measure latency only to ensure there is no unreasonable
regression; gameplay frame time is the primary invariant.

## 3. Confirmed current-state findings

### 3.1 Vita hardware recorder is already correctly scoped

The Vita merge added:

- `src/common/hw_recorder.c`;
- `src/common/hw_recorder.h`;
- `src/common/hw_render.h`.

This does **not** currently inflate PSP or PS2 binaries. CMake only compiles
`hw_recorder.c` for:

- `PLATFORM=PSVITA`; or
- `PLATFORM=DESKTOP` with `USE_DESKTOP_GL=ON`.

The PSP/PS2 binaries inspected after the Vita rebase contain no `hw_rec_*`
symbols. `hw_render.h` is header-only. Therefore this component should **not**
receive a new optional build flag merely for size reduction: it is already absent
from the constrained platforms, and on Vita/Desktop-GL it is part of the actual
renderer architecture rather than optional diagnostics.

`desktop_frame_dump.c` is a different case: it is Desktop-only developer/test
infrastructure and can be considered for an optional build flag later, but it is
not a PSP/PS2 memory priority.

### 3.2 PNG is heavily duplicated by platform

Current source copies are approximately:

| Platform | File | Source size |
|---|---|---:|
| Desktop | `src/desktop/png.c` | 16.9 KiB |
| PSP | `src/psp/png.c` | 16.8 KiB |
| PS2 | `src/ps2/png.c` | 18.2 KiB |
| PS Vita | `src/psvita/png.c` | 18.3 KiB |

Desktop and PSP differ by only a handful of lines. PS2 and Vita also differ only
in a small backend-specific portion. The PNG parser, filters, chunk handling,
compression/decompression and metadata logic are effectively the same code.

The genuine platform differences are mostly:

- how a screenshot framebuffer is read back;
- where a decoded image can be staged;
- how that region is presented/copied;
- logical-to-physical UI transformation;
- optional PNG metadata identifying the host platform.

The current driver contracts already expose most of what is required
(`readFrame`, `frameAddr`, `copyRect`, UI layout helpers), so maintaining four
complete codecs is no longer justified.

A correctness issue was also observed during this audit: PS2 currently assigns
zlib `avail_out` from the **address** of the size variable in two places, whereas
Vita uses the size value. This must be covered by the PNG unification tests and
fixed before using PS2 as the canonical implementation.

### 3.3 NJEMU currently carries two DEFLATE stacks

NJEMU always uses upstream miniz for ZIP access, while libz is still linked for:

- PNG deflate/inflate;
- save-state `compress()` / `uncompress()`;
- CRC32 calls in filer/NCDZ paths.

Preliminary linked-symbol inspection shows both stacks present in console builds.
Representative PSP symbols include substantial miniz ZIP writer/deflate code even
though `src/common/zip_archive.c` only uses the ZIP reader APIs.

Approximate symbol totals from one current PSP build (not a guaranteed removable
size, but useful prioritization evidence):

| Function family | Approx. linked text |
|---|---:|
| miniz ZIP writer | ~26 KiB |
| miniz deflate/tdefl | ~24 KiB |
| miniz inflate/tinfl | ~13 KiB |
| miniz ZIP reader | ~20 KiB |
| zlib deflate | ~20 KiB |

PS2 shows the same broad pattern. These values must be re-measured in S0 using
controlled build configurations before making exact savings claims.

### 3.4 PNG object cost is small compared with compression-library cost

Representative GUI objects currently contribute only a few KiB directly:

- PSP `png.c` object: roughly 2.4 KiB of emitted sections in the inspected MVS
  build;
- PS2 `png.c` object: roughly 3.0 KiB in the inspected CPS2 build.

Therefore the main binary-size opportunity is **not** merely consolidating the
four PNG source files. The larger opportunity is making PNG share the already
required miniz compression implementation and then removing libz and unused miniz
writer/deflate APIs where possible.

## 4. Target architecture

The desired end state is:

```text
                         common PNG codec
                       /                  \
              PNG decode                  PNG encode
             (NCDZ only)              (screenshots only)
                   \                        /
                    \                      /
                     video/UI driver contracts
                              |
                       platform backends

                              +

                           miniz
              /               |              \
         ZIP reader       save states         PNG
              \               |              /
                       CRC / inflate / deflate
```

The goals are:

- one PNG codec implementation;
- no platform-private PNG implementation files;
- no direct PS2/Vita video-private calls from the PNG codec;
- miniz as the single compression/CRC implementation used by NJEMU;
- no ZIP writing APIs in console binaries because NJEMU never writes ZIP files;
- no deflate/writer code in builds that do not require any compression feature;
- compile-time feature slicing rather than runtime branches when functionality is
  genuinely absent from a build.

## 5. Execution phases

### S0 - Freeze post-Vita baselines

Before changing compression or PNG code, create reproducible baselines from the
current rebased branch.

**Status: complete (2026-09-28).** The reproducible PSP/PS2 matrix, section and
package sizes, PNG object costs, compression symbol families, functional-test
baseline, and measurement commands are recorded in
`docs/S0_POST_VITA_BASELINE.md`.

Required matrix at minimum:

- PSP: CPS1, CPS2, MVS, NCDZ;
- PS2: CPS1, CPS2, MVS, NCDZ;
- GUI OFF and GUI ON where meaningful;
- `SAVE_STATE=OFF/ON` for representative targets;
- `COMMAND_LIST=OFF/ON` where it materially changes the image;
- Vita CI artifacts recorded when available, although PSP/PS2 are the constrained
  size baselines.

Capture:

- section sizes and final executable/package sizes;
- top `.text`, `.rodata`, `.data`, `.bss` symbols;
- linked miniz and zlib function families;
- PNG object contribution;
- whether ZIP writer/deflate APIs remain linked;
- existing screenshot/save-state/ZIP functional tests.

Store the reproducible measurement commands and results in this document or a
small companion report under `docs/`.

**Acceptance:** no implementation changes in S0; all later deltas must reference
these baselines.

### S1 - Unify PNG codec in `src/common/`

**Status: complete (2026-09-28).** The four platform copies were replaced by
`src/common/png.c`. Screenshot readback now uses the common video-driver
contract, decoded NCDZ images use the common staging/copy path, and the
PSP/PS2 `avail_out` pointer bug was removed with the consolidation.

Replace:

```text
src/desktop/png.c
src/psp/png.c
src/ps2/png.c
src/psvita/png.c
```

with a common implementation, tentatively:

```text
src/common/png.c
src/common/png_io.h
```

The common codec must use platform-neutral services only. In particular:

- screenshots use `video_driver_t::readFrame` rather than a PS2-private
  `ps2_video_read_frame()` call;
- decoded NCDZ PNGs use common/backend-exposed staging and `copyRect` semantics;
- Vita logical-to-physical placement remains correct through UI layout helpers;
- host metadata such as `"System"` comes from a small platform identity contract
  or is removed if it has no functional value;
- PNG allocation continues to respect the existing cache/save-state scratch
  policy where required.

While consolidating, merge the correctness fixes that have diverged among the
platform copies:

- correct `z_stream.avail_out` size values;
- `O_TRUNC` when replacing screenshots;
- cleanup of partial output files on failure;
- complete PNG text-list cleanup;
- allocation-failure cleanup;
- framebuffer readback pitch correctness;
- PS2/Vita non-CPU-addressable framebuffer handling.

Do not introduce an additional copied full-frame staging buffer on platforms that
can already expose/read the required pixels efficiently. Backend-specific
readback implementation remains backend-owned.

Add focused host tests for:

- PNG signature/chunk generation;
- encoder round-trip for representative pixels;
- decoder filters/color conversion used by NCDZ;
- truncated/corrupt PNG rejection;
- output cleanup on encode failure where practical.

**Acceptance:** one codec implementation, identical screenshots/NCDZ behavior,
no measurable gameplay cost, and console binary size no larger than baseline.

### S2 - Make miniz the sole NJEMU compression/CRC backend

**Status: complete (2026-09-28).** PNG, save states and CRC callers now use the
miniz API directly and NJEMU no longer links libz. A frozen zlib-stream
compatibility test covers existing save-state DEFLATE compatibility. In the
representative full PS2/MVS build, removing the second compression stack
reduced the ELF from 3,130,260 to 3,090,432 bytes and the GNU-size runtime
image by 37,536 bytes; linked compression symbols are now `mz_*` only.

Migrate remaining libz users to the miniz zlib-compatible API or narrow miniz
primitives:

- filer/NCDZ `crc32()` -> miniz CRC;
- save-state `compress()` / `uncompress()` -> miniz equivalents;
- common PNG inflate/deflate -> miniz equivalents.

Then remove the unconditional:

```cmake
target_link_libraries(... z)
```

from NJEMU if no remaining user requires libz.

Preserve save-state file compatibility. The compressed DEFLATE stream must remain
readable by existing NJEMU state files; add compatibility tests using existing or
synthetically frozen state payloads before changing the implementation.

Do not expose miniz types throughout common headers. Keep the dependency local to
small codec/archive implementation boundaries.

**Acceptance:** no linked libz dependency/symbol family, ZIP/PNG/save-state/CRC
functional equivalence, and a measured console binary reduction or at minimum no
size increase.

### S3 - Introduce lean miniz package variants

**Status: deferred by design (2026-09-28).** NJEMU will use the existing
mainstream miniz packages rather than introduce lean package variants. Current
post-S2 MVS measurements show about 24.9 KiB of ZIP-writer symbols still linked
on each console, so the opportunity is understood and can be revisited if the
size/RAM trade-off becomes material enough to justify another package flavor.

After S2 makes miniz the sole compression backend, reduce what the static miniz
library contributes to each build.

NJEMU ZIP access is read-only, so console package/toolchain work should provide a
variant with archive writing disabled, e.g. using upstream-supported compile-time
configuration such as:

```text
MINIZ_NO_ARCHIVE_WRITING_APIS
```

A second variant may be useful when the build requires no compression/deflate at
all, for example some combinations of:

```text
GUI=OFF
SAVE_STATE=OFF
screenshots disabled
```

while still requiring ZIP reading/inflate/CRC.

Do **not** patch miniz upstream source inside NJEMU. Prefer package variants in
`psp-packages` / `ps2sdk-ports` (or equivalent installed CMake targets), so NJEMU
selects an appropriate target declaratively.

Candidate target names should clearly describe capabilities, e.g.:

```text
miniz::miniz
miniz::miniz_readonly
miniz::miniz_reader_only
```

Exact naming/configuration should be decided after checking which upstream
`MINIZ_NO_*` combinations still provide every S2 call.

Also verify that console packages are compiled with function/data sections where
supported. PS2 already uses linker GC; PSP cannot currently rely on the same
policy, making compile-time library slicing especially valuable there.

**Acceptance:** no NJEMU-local fork, no ZIP behavior regression, smaller PSP/PS2
images, and no runtime performance regression in ROM loading beyond measurement
noise.

### S4 - Feature-slice genuinely optional cold functionality

Add compile-time switches only when a feature is genuinely optional and the
switch removes real binary/runtime cost.

#### S4a - Screenshot encoder

Introduce a build option tentatively named `SCREENSHOTS`, default **ON** for
normal GUI builds to preserve usability.

When OFF:

- compile out PNG screenshot encoding and screenshot menu/action plumbing;
- retain NCDZ PNG decoding if the target requires it;
- do not remove other GUI functionality.

This is primarily useful for size-constrained PSP-1000/PS2 builds, not as a new
default.

#### S4b - Desktop frame dump

Audit `desktop_frame_dump.c`. If it is only needed for screenshot/golden-frame
CI and developer diagnostics, gate it behind a build option or `BUILD_TESTING`
without affecting normal Desktop Release binaries.

#### S4c - Hardware recorder

No action unless future architecture changes its ownership. It is already absent
from PSP/PS2 and required by the Vita/Desktop-GL renderer path.

**Acceptance:** default feature set remains unchanged; disabled builds remove
measurable code/data and still compile/test cleanly.

### S5 - Continue feature/lifetime-scoped RAM reduction

**Game-metadata follow-up completed (2026-10-03):** cold game-name/decryption/
ownership data and localized CPS1/MVS DIP menu schemas were moved to generated
runtime metadata. Most notably, CPS1 DIP compiled objects dropped by about
529 KiB while the selected menu profile is loaded only for the configuration
screen. See `docs/GAME_METADATA_EXTERNALIZATION_PLAN.md` and
`docs/BINARY_SIZE_AUDIT.md` for the detailed ownership and measurements.

Resume `.bss`/lifetime work only for candidates where memory can be **removed or
released**, not merely moved from static storage to permanent heap.

Use the successful R12-R16 pattern:

- identify a buffer/table used only by a subset of games/features/phases;
- allocate it only when that feature is present, or reuse storage across mutually
  exclusive phases;
- free load/decrypt/browser scratch before the runtime cache probe;
- keep the gameplay hot path unchanged.

Re-audit large current symbols per target, including existing MVS/CPS buffers and
new Vita-era common additions, but reject candidates that would add per-frame
allocation, indirection, recomputation or cache-unfriendly conversions.

Good questions for each candidate:

1. Is it required for every game in this target?
2. Is it required for the entire game lifetime?
3. Must it coexist with the other large buffers?
4. Is it deterministic/derived and cheap enough to replace without adding hot
   work?
5. Can a driver capability/metadata flag make ownership explicit?

**Acceptance:** measured `.bss` or peak-runtime reduction with unchanged gameplay
hot paths and no allocation-failure regression.

### S6 - Selective `-Os` for cold code only

Revisit the deferred selective-size optimization only after S1-S5 have removed
structural duplication.

Initial candidates:

- PNG codec;
- menu/UI/configuration code;
- translation plumbing;
- `memory_plan.c`;
- filer/browser code;
- ROM-load-only crypto/decode setup where load latency remains acceptable.

Keep `-O3` (or the current performance-oriented policy) for:

- CPU cores;
- memory handlers;
- sprite/render hot paths;
- video submission;
- audio mixing;
- frequently executed emulation support code.

Measure object and final binary deltas independently. Do not apply directory-wide
`-Os` merely because a directory is called `common`.

**Acceptance:** meaningful binary reduction with unchanged gameplay performance;
any cold-path slowdown must be measured and judged negligible for usability.

### S7 - Add binary/static-memory regression reporting to CI

Create a small deterministic size-report tool and CI artifact/report for PSP and
PS2 builds.

At minimum report per representative configuration:

```text
text
rodata
data
bss
runtime image
ELF/PRX
PBP (PSP)
```

Also report deltas against a versioned baseline or the merge-base build.

Rollout in two stages:

1. **report-only** until natural toolchain/build variance is understood;
2. introduce conservative failure thresholds for unexplained regressions.

Avoid a brittle rule such as "any +1 byte fails". Some correctness or usability
features may legitimately add a small amount of code; the important requirement
is that regressions are visible, reviewed and justified.

Where feasible, include a top-symbol diff to identify whether growth came from
NJEMU code, a static library, or `.bss` ownership.

**Acceptance:** every relevant PR exposes PSP/PS2 size deltas without manual
binary archaeology.

### S8 - Documentation and platform-matrix cleanup

After the implementation phases settle:

- update `PORTING_PLAN.md` to include PS Vita as a supported host instead of a
  future example;
- update any remaining three-platform wording in README/CLAUDE/porting docs;
- document Vita's GXM/vita2d and vitaGL backend variants;
- document the compression capability matrix and optional screenshot flag;
- update `BINARY_SIZE_AUDIT.md` with final S0-S7 measurements rather than
  duplicating historical tables here;
- record any package/toolchain changes required in `psp-packages` and
  `ps2sdk-ports`.

**Acceptance:** documentation describes the current four-host architecture and
future optimization work can start from measured, current facts.

## 6. Recommended execution order

The recommended order is:

```text
S0 baseline
   |
   v
S1 common PNG
   |
   v
S2 remove libz / use miniz everywhere
   |
   v
S3 lean miniz package variants
   |
   +------> S4 optional feature slicing
   |
   v
S5 lifetime/static-RAM audit
   |
   v
S6 selective -Os
   |
   v
S7 CI size gate
   |
   v
S8 documentation close-out
```

S1-S3 are the highest-priority implementation work because they simultaneously
improve portability and have credible binary-size upside without affecting the
emulation hot path.

S5 and S6 should remain opportunistic: only proceed when measurements identify a
real candidate. Do not create churn solely to complete a phase number.

S7 may be started earlier in report-only form if useful, but its baseline should
be refreshed after the large structural compression changes are complete.

## 7. Commit and validation policy

Use small milestone commits. A typical phase should follow:

```text
audit -> implement -> validate -> measure -> document -> commit
```

Do not combine unrelated size optimizations into one commit merely because they
all reduce the binary.

For S1-S4, expected validation normally includes:

- Desktop CPS1/CPS2/MVS/NCDZ application builds;
- applicable CTests and new PNG/compression tests;
- Desktop SDL plus Desktop OpenGL where driver contracts change;
- PSP and PS2 representative GUI ON/OFF builds;
- all four PSP/PS2 targets when common PNG/compression behavior changes;
- Vita CI for both GXM and vitaGL when common video/UI contracts change;
- save-state ON/OFF coverage for compression changes;
- NCDZ PNG-load coverage;
- screenshot generation/round-trip coverage;
- ZIP reader tests;
- `git diff --check`;
- explicit confirmation that `resources/` is untouched.

For any phase touching renderer, audio or scheduling code, add real-hardware PSP
and PS2 checks before declaring the performance result complete.

## 8. Explicitly deferred / out of scope

The following are not part of this plan unless new measurements justify reopening
them:

- VU1/VIF1 PS2 renderer work; it remains a separate measured experiment against
  the optimized direct-queue EE backend;
- globally replacing `-O3` with `-Os`;
- moving required static buffers to permanent heap merely to reduce `.bss`;
- removing user-visible default functionality solely to save a small amount of
  code;
- adding platform conditionals back into common target/render code;
- forking/upstream-patching miniz inside NJEMU;
- adding an `hw_recorder` disable flag for PSP/PS2, where it is already absent.

## 9. Immediate next step

Start with **S0** and produce fresh post-Vita PSP/PS2 baselines. Then implement
**S1 common PNG** before changing compression libraries. This preserves a clean
review boundary: first prove that all four platform PNG copies can collapse behind
the existing driver contracts, then switch that single codec plus save states/CRC
from libz to miniz in S2.
