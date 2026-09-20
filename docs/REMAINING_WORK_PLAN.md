# NJEMU remaining work plan

Status: 2026-09-20

This document records the work that remains after completion of:

- `docs/RUNTIME_COVERAGE_AUDIT.md`;
- `docs/EXHAUSTIVE_LOADER_AUDIT.md`.

Those audits are closed and should not be reopened merely to accumulate more
equivalent ROM coverage. The work below is deliberately separated into product
bugs, performance work, corpus-dependent validation and optional hardening.

## Priority overview

Recommended order:

1. investigate and fix the non-deterministic Desktop teardown SIGSEGV;
2. implement and benchmark the PS2 MVS cache-I/O improvements already designed
   in `docs/MVS_CACHE_IO_INVESTIGATION.md`;
3. extend the optimized cache path to CPS2 if MVS measurements justify it;
4. dynamically close the remaining MVS corpus gaps when valid source ROMs are
   available;
5. run longer functional/soak validation as a final hardening pass.

The first two items are the highest-value engineering work. Item 4 is blocked by
input data rather than missing emulator logic.

---

## Phase A - Desktop teardown SIGSEGV

### Why this remains

The exhaustive MVS sweep repeatedly observed exit status 139 after otherwise
successful game initialization. The failing title changed between runs:

- `fatfury1`;
- `aof2`;
- `fatfursp`;
- `mosyougi`;
- `irrmaze`;
- `popbounc`;
- `pbobbl2n`;
- `mslug5`;
- and other successful loader cases depending on the run.

The failure moving between games is strong evidence that this is a Desktop
lifecycle/teardown defect rather than a game-specific MVS loader failure.

### A0 - Reproduce under diagnostics

- build the Desktop MVS target with debug symbols;
- make the automated exit path deterministic;
- run repeated short launch/exit loops over several small and large MVS sets;
- collect the first useful native backtrace;
- repeat with AddressSanitizer where the current dependencies permit it;
- use UBSan as a secondary signal if ASan changes timing too much.

Do not classify a ROM as failing unless the crash happens before successful
loader/init completion or can be reproduced specifically with that ROM.

### A1 - Determine ownership/lifetime failure

Audit shutdown ordering around:

- emulation thread / ticker shutdown;
- audio shutdown;
- renderer/UI destruction;
- cache/file handles;
- CPU/timer state;
- save-state/progress helpers;
- globals freed by both platform and core cleanup paths.

Specifically look for:

- use-after-free;
- double free;
- callbacks firing after their owner has been destroyed;
- races between Desktop ticker/audio threads and core teardown;
- shutdown functions that are not idempotent;
- pointers retained across a core reset or driver exit.

### A2 - Fix and regression-test

The fix should be minimal and preserve platform ownership boundaries.

Acceptance criteria:

- no sanitizer finding in the reproduced path;
- at least 100 automated launch/exit iterations without SIGSEGV;
- representative CPS1, CPS2, MVS and NCDZ Desktop launch/exit smoke tests pass;
- GUI and no-GUI Desktop builds still pass;
- no audit-only instrumentation remains in production code.

Deliverable: one focused bug-fix commit plus any reusable regression harness that
is suitable for the repository.

---

## Phase B - PS2 MVS cache-I/O performance

The detailed investigation and design already exist in:

- `docs/MVS_CACHE_IO_INVESTIGATION.md`.

That document should remain the technical authority for this phase. The outline
below is a project roadmap, not a replacement for its measurements and design.

### B0 - Baseline and instrumentation

Instrument MVS cache misses without changing behavior.

At minimum measure:

- cache hits and misses;
- C-ROM and PCM reads;
- requested block index and file offset;
- sequential vs non-sequential requests;
- number of `lseek()` calls;
- bytes read;
- time spent in cache miss handling.

On PS2, add enough I/O-side counters to correlate NJEMU misses with filesystem
and block-device operations.

Primary representative title: `mslug3`.

Acceptance criteria:

- repeatable baseline captured in PCSX2;
- repeatable baseline captured on real PS2;
- instrumentation can be disabled without changing normal behavior.

### B1 - Low-risk NJEMU optimizations

Implement the cheap optimizations identified by the investigation:

- avoid redundant seeks during sequential `fill_cache()`;
- track the expected raw-cache file position;
- skip `lseek()` when the descriptor is already at the requested block;
- apply the same reasoning to PCM cache reads where appropriate.

Measure each change independently before combining them.

Acceptance criteria:

- cache contents remain bit-identical;
- no regression in MVS loader/cache tests;
- fewer seek/RPC operations are observed;
- startup and gameplay timings are recorded on real PS2.

### B2 - FatFs FastSeek experiment

Evaluate `FF_USE_FASTSEEK` as an isolated PS2SDK experiment.

This is a measurement phase, not an assumed final solution. Record:

- random/backward seek cost;
- memory overhead;
- effect on a 64 MiB contiguous `crom`;
- whether 64 KiB logical reads are still split by filesystem cluster boundaries.

Do not merge a global PS2SDK configuration change unless the measured tradeoff
is clearly acceptable for other users of the filesystem.

### B3 - Extent-aware cache reader prototype

Prototype the direct large-file path described by
`MVS_CACHE_IO_INVESTIGATION.md`:

- obtain and retain the physical fragment/extents of the cache file;
- read an arbitrary logical 64 KiB cache block through the extent map;
- bypass per-cluster `f_read()` splitting for aligned full-block reads;
- transfer data safely from IOP to EE;
- support reads crossing extent boundaries;
- retain the normal filesystem implementation as fallback.

The prototype must not assume that every cache file is contiguous.

Acceptance criteria:

- byte-for-byte equality with the existing raw cache reader;
- fragmented-file correctness;
- safe failure/fallback if the optimized backend cannot be opened;
- no dependency on the particular PCSX2 USB image layout.

### B4 - Integrate through ps2_drivers

Once the prototype proves useful, expose the optimized reader through a clean
PS2 storage abstraction rather than embedding PS2SDK internals throughout
NJEMU.

Expected properties:

- explicit open/read-at/close lifecycle;
- normal POSIX/fileXio backend remains available;
- the core cache code does not need to know FAT32/SCSI details;
- backend selection is compile-time or capability-driven, not game-specific.

### B5 - Benchmark and decide

Compare at least:

- current baseline;
- NJEMU seek-elision only;
- FastSeek experiment;
- extent-aware reader;
- optional prefetch only after the above are understood.

Measure:

- average miss latency;
- p95/p99 miss latency where practical;
- startup time;
- in-game stutter;
- I/O command count;
- CPU overhead;
- memory overhead.

Use PCSX2 for repeatable counters/correctness and real PS2 for the performance
decision.

### B6 - Extend to CPS2

Only after the MVS path is stable, audit CPS2 cache access against the same
storage abstraction.

Do not blindly share assumptions: first verify CPS2 block sizes, access pattern,
compression/raw-cache behavior and lifetime.

Acceptance criteria:

- existing CPS2 raw/folder/ZIP behavior remains correct;
- optimized path is used only where its storage semantics match;
- representative large CPS2 titles show measured benefit on PS2.

### B7 - PSP follow-up

PSP is explicitly secondary to PS2.

Reuse the storage abstraction if profiling shows an equivalent bottleneck, but
do not port the PS2 extent implementation merely for architectural symmetry.

---

## Phase C - MVS corpus-dependent dynamic closure

These are not known emulator defects and must not block other work.

### C1 - `pbobblen`

Current local set is incomplete. Shared ROM data is missing:

- `068-v1`;
- `068-v2`;
- `068-c1`;
- `068-c2`;
- `068-c3`;
- `068-c4`.

When a valid complete set is available:

1. validate it with the existing loader;
2. generate any required cache using the repository converter;
3. run raw/folder/ZIP cases only if the set actually exercises distinct policy;
4. record the result in the exhaustive audit addendum only if it changes a
   previously static-only conclusion.

Do not weaken ROM validation to accept an incomplete set.

### C2 - `ms5pcb`

The current `268-p1r.bin` and `268-p2r.bin` inputs are zero-filled and
invalid.

When valid P-ROMs are available:

- validate PCB CPU1/USER1/decrypt/init execution dynamically;
- confirm the already-audited PCB-specific protection/fix/cache behavior;
- compare against the static matrix recorded in the exhaustive audit.

Do not add compatibility code for the invalid zero-filled files.

Acceptance criteria for Phase C:

- both blockers are either dynamically validated with valid data or continue to
  be documented explicitly as corpus blockers;
- no source workaround is introduced solely to make bad ROM data pass.

---

## Phase D - Functional and soak hardening

This is optional quality work beyond branch coverage.

The completed audits prove loader/init/cache and semantically distinct runtime
branches. They do not prove that every title can run for hours without a later
state/lifecycle issue.

### D0 - Representative play/soak matrix

Choose a small set per core that stresses different subsystems:

- CPS1: ordinary, QSound, bootleg/conversion, multiplayer;
- CPS2: large cache user, multiplayer, paddle/analog, Phoenix/decrypted;
- MVS: raw cache, ZIP/folder cache, protected/encrypted, analog/special input;
- NCDZ: raster title, later-load title, patched title, large/multi-stage CD load.

For each representative case test:

- boot to gameplay;
- several scene/stage transitions;
- audio continuity;
- pause/menu transitions where supported;
- save/load state where supported;
- memcard/NVRAM persistence where relevant;
- repeated exit/relaunch.

### D1 - Long-running stress

After the Desktop teardown issue is fixed, run repeated automated or semi-
automated sessions looking for:

- leaks;
- resource-handle growth;
- stale cache state;
- timer drift;
- audio starvation;
- renderer corruption after repeated resets/state loads.

This phase should produce bugs only when there is a concrete reproduction. It
should not become another requirement to execute every clone.

---

## Work explicitly considered complete

Do not schedule these again unless a regression or new feature changes the
implementation:

- runtime/game-dependent branch coverage for CPS1/CPS2/MVS/NCDZ;
- exhaustive loader/decrypt/init/cache matrix;
- CPS2 raw/ZIP/folder cache validation;
- CPS2 Phoenix/decrypted and USER1/key coverage;
- MVS runtime/converter cache-policy parity;
- MVS KOF2003H program decrypt and PVC protection;
- MVS mixed parent/clone cache fallback;
- NCDZ initial IPL file-type coverage;
- NCDZ AOF3 / Last Blade / Last Blade 2 stateful later-load handlers;
- final Desktop/PS2 build matrix recorded in
  `docs/EXHAUSTIVE_LOADER_AUDIT.md`.

---

## Definition of overall completion

The remaining roadmap can be considered closed when:

1. the Desktop teardown crash has a root cause, fix and repeated-exit regression
   coverage;
2. the PS2 MVS cache path has been measured and either:
   - materially improved and integrated, or
   - demonstrated not to justify further complexity with measurements recorded;
3. CPS2 has been evaluated against the resulting storage solution;
4. corpus blockers remain accurately documented or are dynamically closed once
   valid ROM data becomes available;
5. a representative functional/soak pass shows no new reproducible core issue.

The priority is measurable product correctness and PS2 performance, not
increasing ROM-count statistics after semantic coverage is already complete.
