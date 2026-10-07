# PSP Media Engine Architecture Cleanup Plan

## Status

Implementation complete through A7; final cross-platform/hardware validation pending.

The implementation baseline was `06ed4f2` (`Plan PSP ME architecture cleanup`).
The structural cleanup landed as focused commits:

- `c9a7606` - `Add QSound context equivalence oracle`;
- `2a9d2d4` - `Neutralize target sound offload interfaces`;
- `4517753` - `Split PSP ME audio producer backends`;
- `1bd4cf2` - `Modularize PSP Media Engine CMake configuration`;
- `09e5efa` - `Guard non-PSP audio producer isolation`.

A4 intentionally produced no worker-common code. The cache callback/copy helpers are
only syntactically duplicated, while the workers already diverge in event pumping,
timeout semantics, asynchronous acknowledgements, recovery payloads, and render
ownership. In particular, QSound treats a zero timeout as immediately expired while
the Neo Geo worker uses zero as an unbounded wait. Extracting their wait/lifecycle
logic would therefore merge unlike invariants rather than create a trustworthy shared
protocol layer.

This plan starts from the validated PSP Media Engine (ME) audio implementation at
commit `b3b4e89` (`Document CPS1 ME sound-island benchmark`) on branch
`me_sound_coprocessor`.

The purpose of this milestone is not to add another ME execution mode. The purpose
is to consolidate the architecture after the ME bring-up work so that the PSP
implementation is easier to understand and maintain, while keeping the emulator
cores and every non-PSP platform isolated from PSP-specific details.

## 1. Motivation

The ME work has reached a stable production policy:

- CPS1 uses bounded ME audio jobs when `PSP_ME_AUDIO=ON`;
- CPS2 uses the persistent Z80 + QSound sound coprocessor;
- MVS uses the persistent Z80 + YM2610 sound coprocessor;
- NCDZ uses the persistent Z80 + YM2610 sound coprocessor with its NCDZ machine
  profile;
- `PSP_ME_AUDIO=OFF` keeps the normal CPU implementation;
- the CPU implementation remains the required runtime fallback;
- PPSSPP compatibility is preserved by the CPU path.

The implementation is functionally strong, but the development work necessarily
introduced some PSP-ME terminology and orchestration into target/common code,
expanded the main CMake file, and grew several PSP-specific translation units.
Now that the target-specific execution policy is measured and settled, the
architecture can be simplified without changing that policy.

The cleanup must preserve the useful architectural improvements created during
the ME work, especially:

- copyable QSound state;
- independent/copyable YM2610 contexts;
- explicit CZ80 state serialization;
- the generic `audio_producer_driver_t` boundary;
- fail-closed CPU recovery;
- persistent-worker lifecycle and recovery oracles;
- target-specific ME policies selected at build time.

## 2. Baseline and current cross-platform evidence

Before this plan was written, the current tree was audited specifically for
cross-platform leakage.

### Desktop

The current HEAD builds and passes the complete available test suite for all four
emulator targets:

| Target | Result |
| --- | --- |
| CPS1 | 29/29 tests |
| CPS2 | 31/31 tests |
| MVS | 37/37 tests |
| NCDZ | 29/29 tests |

### PS2

Using the local PS2SDK toolchain, release-style `GUI=OFF` builds were completed
successfully for:

- CPS1;
- CPS2;
- MVS;
- NCDZ.

The non-PSP builds do not define `PSP_ME_AUDIO`,
`PSP_ME_SOUND_COPROCESSOR`, or `AUDIO_PRODUCER_JOBS`, and their final
executables contain no `psp_me_*`, `cps2_me_sound_*`, or
`neogeo_me_sound_*` symbols.

### PS Vita

The repository build logic gates PSP ME sources and dependencies behind
`PLATFORM=PSP`. No VitaSDK toolchain was available locally during this audit,
so Vita must be validated in CI during this milestone rather than being described
as locally validated.

### Meaning of this baseline

There is no known current regression on Desktop or PS2 caused by the ME work.
The main cross-platform residual risk comes from intentional changes to shared
emulation code, primarily QSound, YM2610, CZ80 state handling, and the common
audio-producer boundary. The cleanup must improve encapsulation without
discarding those validated shared improvements.

## 3. Hard constraints

The following constraints apply to every phase.

1. Never modify, stage, or commit anything under `resources/`.
2. Use explicit staging only. Never use `git add -A` or `git commit -a`.
3. Keep all new code and documentation in English.
4. Preserve C99 compatibility and the repository's strict warning policy.
5. Preserve the normal CPU sound path on every target.
6. Preserve PPSSPP compatibility. ME execution must never become mandatory.
7. Do not introduce PSP SDK types, PSP headers, MIST types, or ME-specific
   concepts into emulator-core public APIs.
8. Do not introduce PS2/Vita/Desktop implementations of the PSP ME feature.
9. Do not change the measured production execution policy unless new hardware
   evidence demonstrates a regression in the current policy.
10. Do not reintroduce the rejected CPS1 persistent full-QSound sound island.
11. Do not merge the CPS2 and Neo Geo worker protocols merely to reduce line
    count. Share only infrastructure that is genuinely protocol-independent.
12. Preserve fail-closed behavior: any ME startup, protocol, lifecycle, state, or
    recovery failure must leave or restore a valid CPU-authoritative path.
13. Preserve save/load/reset/exit lifecycle semantics already covered by the ME
    tests and hardware validation.
14. Prefer one coherent abstraction boundary over additional feature-specific
    `#ifdef` branches in target code.
15. Keep each phase independently buildable, testable, and suitable for a
    focused commit.

## 4. Non-goals

This milestone is not intended to:

- add new ME workloads;
- make CPS1 use a persistent sound coprocessor;
- optimize the measured synthesis algorithms further;
- change audio sample rates or buffer sizes;
- change emulation timing;
- redesign the PSP native audio output backend;
- redesign MIST;
- change the shared-ring wire format without a demonstrated need;
- add multiplayer/adhoc behavior;
- solve the independent NCDZ video-stall investigation;
- remove the software/CPU fallback;
- create a universal sound-chip framework.

Performance work may follow later, but this cleanup should be behavior-preserving.

## 5. Desired end state

After this plan is complete, the architecture should have four clear layers.

### 5.1 Emulator target and sound-chip code

The emulator cores should express sound-system semantics, not PSP hardware
semantics.

Target code may need to express operations such as:

- sound CPU ownership/suppression;
- scheduler boundary;
- command publication;
- shared-memory synchronization;
- sound-state snapshot/recovery;
- render ownership;
- save/load handoff.

Those operations must use platform-neutral names. Target code must not refer to
the PSP Media Engine, MIST, PSP cache operations, PSP threads, or PSP mutexes.

### 5.2 Common audio producer contract

`audio_producer_driver_t` remains the common distinction between:

- producing/emulating an audio buffer; and
- presenting that audio buffer through `audio_driver_t`.

Non-PSP platforms bind the CPU producer. They should not need an ME-aware
implementation or ME-aware conditionals.

### 5.3 PSP audio acceleration layer

The PSP layer owns:

- runtime Auto/Main CPU/Media Engine selection;
- MIST initialization/probing;
- bounded job dispatch;
- persistent-worker bootstrap;
- PSP synchronization primitives;
- PSP cache-coherency operations;
- suspend/resume handling;
- failover from ME authority to CPU authority.

The main PSP producer should orchestrate these responsibilities through
target-specific private backends rather than directly accumulating all target
logic in one file.

### 5.4 Target-specific worker protocols

CPS2 QSound and Neo Geo YM2610 workers remain separate protocols.

They may share low-level transport/lifecycle helpers only where the behavior and
failure model are truly identical.

## 6. Phase A0 - freeze the baseline and define invariants

Before refactoring, record the current behavior that must remain true.

### Tasks

- Re-read the current ME planning documents and the current implementation; use
  the code as authority if it has moved beyond this document.
- Record the exact starting HEAD and active branch in this plan or in the first
  implementation commit message.
- Confirm the current CMake execution-mode mapping:
  - CPS1 + ME -> bounded jobs;
  - CPS2 + ME -> persistent QSound;
  - MVS + ME -> persistent YM2610;
  - NCDZ + ME -> persistent YM2610/NCDZ profile.
- Confirm `PSP_ME_AUDIO` is the only supported public ME feature switch.
- Confirm `PSP_ME_SOUND_COPROCESSOR` remains obsolete/internal rather than
  restoring it as a public configuration axis.
- Capture a before-refactor source/binary isolation check for Desktop and PS2.
- Run the focused Desktop suites before changing code.

### Exit criteria

- Current behavior and public configuration are unambiguous.
- No unexplained tracked changes are present.
- The baseline test results are recorded.
- No files under `resources/` are touched.

## 7. Phase A1 - add a dedicated QSound context equivalence oracle

QSound was refactored into a copyable context to support ME execution. That is a
useful general sound-core improvement, but it deserves a platform-independent
oracle comparable to the existing YM2610 context tests.

### Tasks

Add a focused host test, preferably `tests/qsound_context_tests.c`, that
exercises the production QSound context API without PSP dependencies.

Cover at least:

- independent initialization of two contexts;
- register/data writes;
- channel start/stop behavior;
- looping and non-looping samples;
- bank/address/pitch/volume/pan state;
- multi-block rendering;
- context clone;
- clone continuation producing byte/sample-identical PCM;
- restore from worker context;
- continuation after restore;
- isolation between contexts;
- representative CPS1 and CPS2 volume-shift behavior where it can be tested
  without depending on game-global state.

The oracle should compare complete stereo buffers and relevant semantic state,
not merely assert that functions return success.

### Constraints

- Do not add PSP stubs to this test.
- Do not change QSound behavior to make the test easier.
- If the existing public context surface is awkward, make the smallest
  platform-neutral API improvement needed to test it.

### Exit criteria

- The new QSound oracle passes on Desktop.
- Existing CPS1/CPS2 tests remain green.
- The normal QSound path remains behaviorally unchanged.

## 8. Phase A2 - neutralize ME terminology at the emulator-core boundary

This is the highest-value encapsulation change.

Current target code calls APIs whose names expose PSP ME implementation details,
for example the `cps2_me_sound_*` and `neogeo_me_sound_shadow_*` families.
When ME is disabled these compile to no-op inline stubs, which is efficient, but
the naming still makes platform-specific machinery part of the conceptual core.

### Design rule

Rename/restructure the target-facing boundary around sound execution ownership,
not around the device that implements it.

Possible naming directions include:

- `cps2_sound_offload_*`;
- `neogeo_sound_offload_*`;
- `*_sound_execution_*`;
- `*_sound_owner_*`.

Choose one vocabulary after inspecting all call sites and use it consistently.
Do not mechanically replace `me` with `offload` where a more precise semantic
verb already exists.

### Tasks

- Replace PSP-ME-specific target-facing headers with platform-neutral target
  sound-execution/offload headers.
- Keep disabled implementations zero-cost through static inline stubs or an
  equally cheap compile-time binding.
- Replace `PSP_ME_SOUND_COPROCESSOR` checks in target-facing headers with a
  neutral internal capability definition selected by CMake.
- Keep the concrete PSP implementation in `src/psp/`.
- Ensure CPS2, MVS, and NCDZ source code no longer needs to know that the
  implementation is a PSP Media Engine.
- Keep target-specific semantics target-specific. Do not force CPS2 and Neo Geo
  onto one oversized interface.

### Important performance constraint

Several hooks execute at scheduler or shared-memory frequency. The disabled
non-PSP path must remain compile-time eliminable. Do not replace zero-cost inline
stubs with unconditional indirect function-pointer calls on hot paths unless
measurement proves the cost irrelevant and the design benefit is substantial.

### Exit criteria

- No PSP/ME terminology remains in normal CPS2/MVS/NCDZ execution code except in
  comments that explicitly document historical/validation context.
- Non-PSP binaries still contain no PSP ME implementation symbols.
- Desktop and PS2 behavior remain unchanged.
- PSP host worker oracles remain green.

## 9. Phase A3 - split PSP producer orchestration from target-specific ME backends

`src/psp/psp_audio_producer.c` currently owns too many responsibilities:
generic producer lifecycle, runtime selection, MIST dispatch, bounded jobs,
persistent CPS2 orchestration, persistent Neo Geo orchestration, fallback, and
power lifecycle.

The goal is to keep `audio_producer_driver_t` as the public common contract
while making PSP internals easier to read.

### Proposed private PSP layering

The exact filenames may be adjusted after inspection, but the intended split is:

- `psp_audio_producer.c`
  - implements the common `audio_producer_driver_t`;
  - owns runtime selection and CPU fallback;
  - delegates acceleration-specific operations.
- a small private ME dispatch/transport module
  - MIST init/probe;
  - task dispatch boundary;
  - shared job workspace ownership where appropriate.
- bounded-job backend
  - CPS1 QSound/OKIM6295 job capability and lifecycle.
- CPS2 persistent backend
  - connects the producer lifecycle to `psp_cps2_me_sound`.
- Neo Geo persistent backend
  - connects the producer lifecycle to `psp_neogeo_me_sound`.

Prefer compile-time target selection so a PSP binary does not carry unrelated
target backends.

### Tasks

- Define a PSP-private backend contract containing only the lifecycle/render/job
  capabilities actually shared by the producer.
- Move target-specific branches out of the generic producer.
- Keep one obvious location for CPU fallback policy.
- Keep one obvious location for runtime Auto/Main CPU/Media Engine choice.
- Preserve current logging behavior unless a message becomes misleading after
  the rename.
- Avoid a generic abstraction that merely mirrors every existing function.

### Exit criteria

- `psp_audio_producer.c` is materially smaller and mostly target-agnostic.
- CPS1, CPS2, MVS, and NCDZ PSP builds select only their required backend.
- CPU-only PSP builds continue to avoid ME initialization and ME dependencies.
- Runtime fallback semantics remain unchanged.
- PPSSPP-safe CPU execution remains possible.

## 10. Phase A4 - assess and extract only genuinely shared worker infrastructure

The persistent workers are large, but size alone is not sufficient reason to
merge them.

The CPS2 QSound worker and Neo Geo YM2610 worker have different memory models,
event protocols, timing behavior, recovery state, and rendering semantics.
Their protocols should remain separate.

### Candidate shared infrastructure

Audit duplication around:

- worker generation/lifecycle state;
- MIST start/stop wrapper;
- command/event ring publication;
- bounded ACK polling;
- timeout/fatal-state handling;
- common statistics/high-water accounting;
- cache publication/consumption helpers;
- shutdown/abort hygiene.

### Decision rule

Extract a helper only when:

1. both workers implement the same semantic operation;
2. failure behavior is the same;
3. ordering requirements are the same;
4. the helper can be tested independently;
5. extraction reduces duplicated invariants rather than only duplicated syntax.

If those conditions are not met, document the intentional duplication and leave
the workers separate.

### Constraints

- Do not change the established command ordering.
- Do not change the 32-byte protocol message ABI merely for cleanup.
- Do not change ring sizes or timeout values without independent evidence.
- Do not weaken recovery snapshots.
- Do not hide target semantics behind callbacks so generic that the code becomes
  harder to reason about.

### Exit criteria

Either:

- a small, clearly protocol-independent worker transport/lifecycle module exists
  and both workers use it with all oracles green;

or:

- the audit documents why further extraction would reduce clarity or safety, and
  the workers remain intentionally separate.

Both outcomes are acceptable.

## 11. Phase A5 - modularize PSP ME build configuration

The main `CMakeLists.txt` currently contains ME mode policy, source selection,
special `-G0/-fno-pic` compilation, dependency discovery, hardware-test
targets, link libraries, and packaging rules in multiple regions.

Move PSP ME configuration into a dedicated CMake module, following the style of
the repository's other `cmake/NJEMU*.cmake` helpers.

Suggested name:

`cmake/NJEMUPSPMediaEngine.cmake`

### Responsibilities of the module

Where practical, centralize:

- public option validation;
- target -> ME mode selection;
- internal capability variables;
- PSP ME source lists;
- ME-specific source compile flags;
- dependency discovery;
- ME-specific link libraries;
- hardware-oracle target setup;
- artifact/package suffix information needed by the main build.

The main CMake file should retain high-level orchestration, not ME implementation
detail.

### Build-system invariants

- `PSP_ME_AUDIO=OFF` must not require ME libraries.
- Non-PSP platforms must not require, search for, compile, or link PSP ME
  dependencies.
- CPS1 ME builds must select bounded jobs only.
- CPS2/MVS/NCDZ ME builds must select their current persistent modes.
- Obsolete configuration combinations must not silently reappear.
- Source-specific `-G0/-fno-pic` requirements must remain explicit and limited
  to code that can execute on ME or crosses the ME ABI boundary.

### Exit criteria

- The main CMake file has one clear PSP-ME configuration entry point.
- The existing PSP build matrix produces the same execution modes.
- Desktop/PS2/Vita configuration does not traverse PSP ME dependency discovery.
- Hardware-oracle targets still build where applicable.

## 12. Phase A6 - strengthen cross-platform isolation checks

The present architecture is isolated correctly, but the property should be
protected against future regression.

### Tasks

Add lightweight validation that proves at least:

- Desktop builds bind `audio_producer_cpu`;
- PS2 builds bind `audio_producer_cpu`;
- Vita builds bind `audio_producer_cpu`;
- setting or leaving PSP-only options in the CMake cache cannot cause PSP sources
  or PSP ME libraries to leak into a non-PSP target;
- common sound-core tests do not depend on PSP headers.

Prefer configure/build assertions and existing CI matrices over brittle
post-link shell parsing. A symbol check may be used as an additional diagnostic,
not as the only guarantee.

Consider a focused CI/configure smoke build with `PSP_ME_AUDIO=ON` on a
non-PSP platform only if the intended contract is that the option is harmless
outside PSP. If the preferred contract is to reject that configuration, make the
error explicit and document it. Do not change this behavior accidentally.

### Exit criteria

A future accidental PSP dependency in Desktop/PS2/Vita is caught automatically.

## 13. Phase A7 - documentation and naming cleanup

Once the code structure is final, update documentation to describe the stable
architecture rather than the historical bring-up sequence.

### Update

At minimum inspect and update:

- `docs/ARCHITECTURE.md`;
- `docs/PLATFORM_PORTING_GUIDE.md`;
- `docs/BUILDING.md`;
- `docs/PLATFORMS.md`;
- `CLAUDE.md`;
- the ME plan documents where they describe options that are now obsolete.

### Required concepts to document

- `audio_driver_t` is native audio presentation/output.
- `audio_producer_driver_t` is audio generation/execution ownership.
- non-PSP platforms use the CPU producer.
- PSP may use bounded ME jobs or a persistent target-specific sound
  coprocessor.
- `PSP_ME_AUDIO` is the public build capability switch.
- runtime Main CPU selection remains the fallback/reference path.
- CPS1 deliberately remains bounded rather than persistent because hardware
  measurement showed the full QSound island regressed performance.
- PSP-specific implementation stays in `src/psp/`; target code only sees
  platform-neutral sound-execution semantics.

Do not rewrite the historical measurement documents into generic architecture
docs. Preserve the measured evidence and rejected experiments as historical
record.

### Exit criteria

A contributor can understand the current sound architecture without reading the
entire ME experiment history.

## 14. Phase A8 - final validation matrix

Run validation after each structural phase, with the full matrix at the end.

### 14.1 Desktop

Build and test all four targets:

- CPS1;
- CPS2;
- MVS;
- NCDZ.

The exact test count may legitimately change if new tests are added, but all
tests must pass.

### 14.2 PS2

Cross-build all four targets using the current supported PS2 configuration.

At minimum verify:

- CPS1;
- CPS2;
- MVS;
- NCDZ.

### 14.3 PS Vita

Use CI or a local VitaSDK environment if available to build:

- CPS1;
- CPS2;
- MVS;
- NCDZ.

This is required because the baseline audit could not perform a local Vita build.

### 14.4 PSP CPU mode

Build all four targets with `PSP_ME_AUDIO=OFF`.

This is the strongest build-time isolation oracle and the required PPSSPP-safe
configuration.

### 14.5 PSP ME mode

Build all four targets with `PSP_ME_AUDIO=ON` and verify the selected mode:

| Target | Required mode |
| --- | --- |
| CPS1 | bounded jobs |
| CPS2 | persistent QSound coprocessor |
| MVS | persistent YM2610 coprocessor |
| NCDZ | persistent YM2610/NCDZ coprocessor |

### 14.6 Host ME oracles

Run all existing worker/ring/context/job tests, including:

- QSound context oracle added by A1;
- QSound worker tests;
- YM2610 context tests;
- Neo Geo worker tests;
- NCDZ worker tests;
- SPSC ring tests;
- bounded audio job tests.

### 14.7 Real PSP regression

After the structural cleanup, perform a focused real-hardware regression rather
than repeating every historical benchmark.

Required coverage:

- CPS1 bounded path with a classic OKIM6295 title;
- CPS1 bounded QSound title;
- CPS2 persistent QSound title;
- MVS persistent YM2610 title;
- NCDZ worker hardware oracle at minimum, plus normal gameplay if the independent
  NCDZ video issue permits it;
- CPU runtime selection in an ME-capable build;
- reset and clean exit;
- one save/load cycle for persistent workers where supported;
- injected/recoverable failure oracle if the existing harness exposes it.

Performance only needs to show no material regression from the already measured
policy. A full optimization campaign is outside this milestone.

### 14.8 Repository hygiene

Before every commit and before completion:

- `git diff --check`;
- inspect `git status`;
- verify no `resources/` path is staged;
- use explicit staging only.

## 15. Recommended commit structure

Keep commits small enough that a regression can be bisected.

A suggested sequence is:

1. `Add QSound context equivalence oracle`
2. `Neutralize target sound offload interfaces`
3. `Split PSP ME audio producer backends`
4. `Extract shared PSP ME worker infrastructure` only if A4 justifies it
5. `Modularize PSP Media Engine CMake configuration`
6. `Guard non-PSP audio producer isolation`
7. `Document audio producer and PSP ME architecture`

The exact number of commits may differ, but do not combine unrelated cleanup
with behavior changes.

## 16. Risk register

### R1 - accidental hot-path overhead on non-PSP platforms

Neutral abstractions can accidentally introduce indirect calls where the current
disabled path is compiled away.

Mitigation: keep compile-time no-op bindings for scheduler/shared-memory hooks
and compare generated/link behavior where useful.

### R2 - QSound context refactor regression

QSound is shared by CPS1 and CPS2, and its copyable-state refactor affects every
platform even when ME is absent.

Mitigation: A1 must land before structural cleanup.

### R3 - YM2610 shared-code regression

YM2610 context and table optimizations are common to PSP, PS2, Vita, and Desktop.

Mitigation: preserve the existing reference-mode equivalence tests and run them
throughout the milestone. Do not move PSP orchestration back into the chip core.

### R4 - over-generalizing worker protocols

A universal worker can obscure ordering/recovery differences between QSound and
YM2610.

Mitigation: A4 is explicitly a go/no-go extraction audit. Intentional duplication
is preferable to a false abstraction.

### R5 - CMake behavior drift

Moving configuration into a module can accidentally alter which sources,
definitions, libraries, or `-G0` flags are applied.

Mitigation: compare the configured mode and source/definition behavior before and
after A5 for every PSP target and at least Desktop/PS2 non-PSP targets.

### R6 - weakening CPU fallback

Refactoring producer backends may split ownership state incorrectly.

Mitigation: keep fallback policy centralized, preserve recovery oracles, and test
runtime Main CPU selection in ME-capable PSP binaries.

### R7 - Vita compile regression hidden locally

No local VitaSDK was available during the baseline audit.

Mitigation: Vita CI is a hard final gate, not optional polish.

## 17. Completion criteria

This milestone is complete only when all of the following are true:

- target/core code no longer exposes PSP ME as a platform concept;
- QSound copy/restore behavior has a dedicated platform-independent equivalence
  oracle;
- `psp_audio_producer.c` is reduced to clear producer policy/orchestration;
- target-specific PSP acceleration lives behind private PSP backends;
- shared worker infrastructure is extracted only where it improves correctness
  and clarity;
- PSP ME CMake logic has one coherent module/entry point;
- Desktop, PS2, and Vita are protected against PSP ME dependency leakage;
- Desktop tests pass for all four targets;
- PS2 builds pass for all four targets;
- Vita CI builds pass for all four targets;
- PSP CPU builds pass for all four targets;
- PSP ME builds select the intended target-specific policies;
- host ME oracles pass;
- focused real-PSP regression passes;
- CPU fallback and PPSSPP-safe operation remain available;
- documentation describes the stable architecture;
- no file under `resources/` has been modified, staged, or committed.

## 18. Final architectural principle

The emulator cores should know **what sound ownership and synchronization they
need**, not **which PSP processor provides it**.

The PSP backend should know **how the Media Engine provides that capability**,
while the common audio layer should know only **how audio is produced and then
presented**.

That separation is the intended long-term boundary for all follow-up work.
