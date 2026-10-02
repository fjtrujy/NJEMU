# PSP Media Engine Audio Experiment

This document is the authoritative plan and status for the optional PSP Media
Engine (ME) audio experiment.

## Goals and compatibility contract

The existing Allegrex audio path remains the reference implementation.  ME
support is an additional PSP-only capability intended for real hardware and is
never required by the normal PSP build.

- `PSP_ME_AUDIO=OFF` is the default and the PPSSPP/CI reference configuration.
- `PSP_ME_AUDIO=OFF` must not link or initialize any ME-specific dependency.
- `PSP_ME_AUDIO=ON` keeps the CPU producer compiled as the runtime fallback.
- ME initialization or proof-of-execution failure must leave audio on the CPU.
- `src/psp/psp_audio.c` continues to own `sceAudio*` channel/output operations.
- No GUI/runtime selector is added during the experiment.  The producer seam is
  intentionally shaped so Auto/Main CPU/Media Engine can be added later without
  moving sound-chip code again.

PPSSPP does not implement the ME execution path required by this experiment.
Its role is to validate the OFF/reference build, not ME execution.

## Current architecture

PCM production now goes through a narrow producer driver:

```text
sound chip callback / mixing / conversion
                 |
                 v
       audio producer driver
          /             \
         /               \
  CPU producer       PSP ME producer
         \               /
          \             /
             PCM buffer
                 |
                 v
          PSP audio driver
                 |
                 v
             sceAudio*
```

The common producer contract currently exposes only the lifecycle actually
needed by this work: `init`, `shutdown`, `reset`, `render`, and availability.
The PSP ME producer owns ME synchronization/fallback.  Generic sound-chip code
does not contain `PSP_ME_AUDIO` conditionals.

The CPU producer implementation itself is compiled in both OFF and ON builds.
OFF binds it directly; ON binds the PSP ME wrapper, which delegates lifecycle
and PCM rendering to that same CPU producer whenever no proven ME workload is
active.  This keeps the reference path available as a literal oracle/fallback,
not a second reimplementation of it.

## Upstream dependencies investigated

The primary library was inspected at:

- `mcidclan/psp-media-engine-custom-core`
- revision `fe871e12754060f3b42fbca6b251bb8ee3ade453`
- MIT license

The custom-core library maps native ME firmware entry points according to the
detected ME image.  Its raw `me-core` mode resets the ME to a custom handler and
the upstream `audio-shared-buffer` example keeps a long-running ME loop.  That
example is useful for its 64-byte alignment, uncached/shared-memory and hardware
mutex patterns, but a permanent loop is not the preferred first NJEMU
integration because it takes stronger ownership of ME execution and complicates
sleep/syscall coexistence.

NJEMU therefore uses the related upstream safe-task layer for discrete jobs:

- `mcidclan/psp-media-engine-safe-task`
- revision validated locally: `7d4c41f77b0be8815a720e0c8212ed504c01806d`
- MIT license
- Classic dispatcher path (`meSafeTaskInitDispatcher`)

The Classic safe-task path is documented upstream as tested on both Phat and
Slim.  It preserves System Controller ME syscalls, provides explicit dispatch
and `waitReady` synchronization, and can load/unload the AV module used to
exercise the patched EDRAM path.  This is a better match for bounded audio jobs
and ROM/reset lifetime than replacing the ME with an infinite producer loop.

### Build integration

`PSP_ME_AUDIO=ON` currently expects the upstream libraries to be installed in
the active PSPDEV toolchain:

- headers `me-core-mapper/me-core-mapper.h` and `me-safe-task/me-stask.h`;
- `libme-core-mapper.a`;
- `libme-stask.a`.

The normal OFF build does not search for or link these libraries.  The regular
PSP GitHub Actions matrix explicitly configures `PSP_ME_AUDIO=OFF`.

The regular PSP matrix remains OFF-only.  A separate MVS build-only job installs
the two upstream dependencies from the exact revisions above and compiles
`PSP_ME_AUDIO=ON`; it does not run PPSSPP or claim ME runtime coverage.  The
upstream builds generate and embed a small kernel PRX and use host utilities
such as `xxd` and `sed`, so those tools are installed explicitly in that job.
The inspected revisions use GNU-style `sed -i`; macOS local validation used GNU
`sed` without modifying upstream source.

## Memory and synchronization findings

The upstream examples and safe-task implementation make the producer/consumer
boundary explicit:

- shared job/buffer storage must be at least 64-byte aligned;
- Allegrex writes must be written back before ME consumes them;
- Allegrex must invalidate data written by ME before reading it;
- ME-side jobs use the mapped ME cache maintenance functions;
- `volatile` alone is not synchronization;
- a dispatched job must reach `waitReady` before its buffers or referenced state
  can be reset/freed;
- the first real workload should use bounded input/output buffers rather than
  granting ME unrestricted access to mutable emulator globals.

ME tasks execute ordinary code by function pointer, but the first NJEMU task
must avoid libc/PSPSDK calls and other services whose safety on ME is not proven.
Use only deterministic arithmetic/memory work plus the mapped ME cache helpers
until runtime validation establishes a broader safe subset.

## Baseline profiling status

Earlier PSP performance work left reusable profiling instrumentation in Git
stashes that separated:

- PCM generation (`audio_update`);
- blocking PSP output (`audio_block`);
- time between sound-thread iterations (`audio_gap`);
- 68000 and Z80 execution;
- video/presentation and pacing.

That distinction is important: time blocked in `sceAudioSRCOutputBlocking()` is
not work that should be moved to ME.

`PSP_AUDIO_PROFILE=ON` now provides a focused replacement for the old temporary
instrumentation.  It is OFF by default and records 300-buffer windows to
`psp_audio_profile.log`, separating:

- total producer time (`producer`);
- sound-chip callback/synthesis (`callback`);
- clipping/resampling/PCM conversion (`post`);
- blocking native output (`output_block`);
- actual sound-thread loop period (`loop_period`).

The log also records the configured sample count/rate/channels and expected
buffer period.  File output occurs only once per 300 buffers so diagnostic I/O
does not contaminate every measured callback.

### Real-PSP MVS/mslug3 baseline

The current branch was profiled on a real PSP through psplinkusb using MVS,
`mslug3`, Release, no GUI, `PSP_ME_AUDIO=OFF`, and `PSP_AUDIO_PROFILE=ON`.
The host-backed no-GUI harness first exposed a path bug: relative paths such as
`roms/neogeo.zip` do not resolve through libc/miniz when a PRX is started from
`host0:` even though the psplink current directory is correct.  The no-GUI path
now derives ROM/cache/processed roots from `launchDir`, matching the normal GUI
path.  A standalone PSP probe verified that the same `neogeo.zip` opens through
an absolute `host0:` path and contains the expected Europe MVS v2 CRC.

Attract/demo mode is **not** a representative audio baseline for `mslug3`.
During demo play the music workload is absent and the game mainly emits sound
effects.  Those windows are retained only as a low-audio-load reference; they
showed roughly 3.0-4.1 ms of producer time per 33.378 ms output period.

For the representative run, a temporary, non-committed input script inserted a
credit, started player 1, and generated movement/fire/jump input.  A psplink
screenshot confirmed active gameplay.  The representative 300-buffer window
was:

| metric | average | maximum |
| --- | ---: | ---: |
| producer | 6.617 ms | 16.210 ms |
| YM2610 callback | 6.463 ms | 16.061 ms |
| clip/resample/post | 0.148 ms | 0.229 ms |
| `sceAudioSRCOutputBlocking` | 26.743 ms | 28.184 ms |
| sound-thread loop period | 33.378 ms | 36.745 ms |

The output format was 1472 stereo samples at 44.1 kHz, so the expected output
period is 33.378 ms.  The callback is therefore the meaningful Allegrex audio
compute target; post-processing is only about 0.15 ms and is too small to
justify an ME dispatch/copy/synchronization boundary.  The ~26.7 ms output wait
is blocking time, not computation.

### YM2610 hotspot breakdown

A second hardware run used PSPSDK `-pg`/`psp-gprof` for two gameplay windows.
The profiler overhead raises absolute callback timings, so the gprof run is used
for attribution rather than wall-clock comparison.  Across 20.01 s of sampled
CPU time:

- `OPNB_ADPCMA_calc_chan_dynamic`: 5.28 s / 26.37% of total sampled CPU time,
  2,421,797 calls;
- `OPNB_ADPCMB_calc_dynamic`: 0.48 s / 2.38%, 390,153 calls;
- `YM2610Update` as a whole accounted for about 5.82 s / 29% including children;
- the remaining YM2610/FM/SSG/mix self time was very small relative to ADPCM-A;
- `pcm_cache_read` was called only 572 times during the sample and was not itself
  the hot path, so the dominant cost is ADPCM-A decode/update work rather than
  storage I/O.

This selects **MVS YM2610 ADPCM-A decode** as the first workload worth studying
for ME migration.  It does *not* yet make the current per-sample dynamic helper
safe to dispatch directly: millions of tiny ME RPCs would be worse, and the
helper can touch PCM-cache state plus YM2610 state concurrently modified by Z80
register writes.  M4 therefore requires one bounded job per useful chunk/buffer
with explicit state/input/output ownership and cache coherency; directly calling
`OPNB_ADPCMA_calc_chan_dynamic()` on ME is rejected.

## Milestones and status

### M1 - CPU reference seam [complete]

- `PSP_ME_AUDIO` exists and defaults to OFF.
- OFF links the common CPU producer only.
- ON selects the PSP producer integration boundary while retaining CPU render as
  fallback.
- PSP MVS ON/OFF builds and PSP CPS1/CPS2/NCDZ OFF builds passed locally.
- OFF contains no `meLib`, `meCore`, or `meSafe` symbols.

### M2 - optional ME dependency/bootstrap [hardware validated for startup/clean teardown]

The PSP producer now initializes the safe-task Classic dispatcher and loads the
upstream AV module only when `PSP_ME_AUDIO=ON`.  Initialization failure logs the
error and leaves the producer on CPU.  Reset/shutdown wait for outstanding ME
work before unloading the module.

On real PSP the dispatcher initializes successfully, the upstream AV module is
loaded, and the ME-enabled MVS build reaches `mslug3` with the normal PSP sound
thread active.  A hardware-only teardown build exercised the normal
`neogeo_exit -> sound_exit -> producer shutdown -> memory_shutdown` path twice
in consecutive launches.  After each run MVS was no longer loaded and the user
partition returned exactly to the pre-run 57,655,296 free bytes.  This validates
startup, shutdown, unload, and repeated clean startup without a leak.

Sleep/wake and an in-process normal-UI ROM switch are still pending.  A synthetic
double-`emu_main()` no-GUI diagnostic was deliberately discarded as evidence
because that is not a supported normal application flow and its second video
initialization did not remain valid.

### M3 - deterministic shared-memory proof [hardware validated]

The ON backend dispatches one small 64-byte-aligned job during producer startup.
Allegrex writes two input words and writes back/invalidates the cache line; ME
invalidates it, computes XOR and addition results, writes the line back, and
Allegrex invalidates before verifying both words.  Dispatch error or result
mismatch unloads the AV module, marks ME unavailable, and leaves all PCM
production on CPU.  No emulator or sound-chip state is touched by the probe.

The proof passed on the real PSP.  Inspection through psplink after startup
showed `me_module_loaded=1` and `me_available=1`.  The shared probe line contained
the expected values:

- input A: `0x13579BDF`;
- input B: `0x2468ACE0`;
- ME XOR result: `0x373F373F`;
- ME addition result: `0x37C048BF`.

This confirms actual ME execution plus the Allegrex/ME cache-maintenance path;
it is not PPSSPP-derived validation.

### M4 - first real audio workload [selected; design/prototype pending]

The selected workload is MVS YM2610 ADPCM-A decode in PCM-cache mode.  The first
prototype must batch enough decode work to amortize ME dispatch, keep CPU cache
miss/file handling on Allegrex, and exchange only bounded decoder state, source
bytes/nibbles, and output contributions.  It must not grant ME unrestricted
access to the global YM2610 or PCM-cache structures.  CPU output remains the
oracle/fallback and deterministic PCM comparison is required before performance
claims.

### M5 - hardware performance decision

Compare CPU and ME paths using identical game/configuration.  Evaluate total
emulation speed, audio glitches/underruns, Allegrex time, dispatch/wait overhead,
frame pacing and sustained stability.  A lower audio counter without a whole-
system improvement is not sufficient.

### M6 - expansion and runtime selector

Expand to additional workloads only after M4/M5 demonstrate a real win.  Runtime
Auto/Main CPU/Media Engine selection comes later; it is not part of the initial
experiment.

## Remaining risks

- Safe-task/custom-core firmware mapping support is still evolving upstream.
- Kernel-PRX loading and model-specific ME firmware behavior may still vary on
  PSP models other than the hardware tested here; PPSSPP cannot validate them.
- A normal in-process ROM switch still needs hardware validation after an actual
  ME audio workload exists.
- Sleep/wake must be tested while no job is in flight and while the audio thread
  is active.
- ADPCM-A is individually hot, but its existing helper is called millions of
  times; batching and synchronization semantics determine whether the ME path is
  beneficial.

## Validation checkpoint - 2026-10-02

Final local validation after the shared CPU fallback refactor:

- PSP MVS, `PSP_ME_AUDIO=OFF`, `PSP_AUDIO_PROFILE=OFF`: builds and packages;
- PSP MVS, `PSP_ME_AUDIO=ON`, `PSP_AUDIO_PROFILE=OFF`: builds and packages;
- PSP CPS1/CPS2/NCDZ, `PSP_ME_AUDIO=OFF`: all build and package;
- Desktop MVS builds and passes 22/22 CTests;
- the final MVS OFF ELF contains no `meSafe`, `meCore`, or ME-processing symbols;
- the final MVS ON ELF contains the safe-task dispatcher and NJEMU ME probe;
- `git diff --check` is clean.

The focused profiler was also compiled successfully for MVS, NCDZ, CPS1 and
CPS2, and in combination with `PSP_ME_AUDIO=ON`.  With profiling disabled, the
MVS Release `.text`, `.data`, and `.bss` sizes are unchanged from the pre-profiler
build, confirming that the disabled instrumentation optimizes away.

Subsequent real-PSP validation completed the missing runtime work at this
checkpoint: M2 startup/clean teardown and M3 execution/coherency now pass on
hardware, and a fresh `MVS/mslug3` gameplay profile selected dynamic YM2610
ADPCM-A as the only first workload with enough measured cost to justify further
ME prototyping.  No real audio workload has been migrated yet, so there is still
no ME-vs-CPU performance claim.
