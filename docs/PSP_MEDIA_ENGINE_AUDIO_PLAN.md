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

No ME-enabled CI job is added yet.  The upstream builds generate and embed a
small kernel PRX and currently use host utilities (`xxd`, `sed`) in their build
pipeline.  The dependency needs a reproducible CI installation step before an
ON build-only job is appropriate.  In particular, the inspected revisions use
GNU-style `sed -i`; macOS validation used GNU `sed` without modifying upstream
source.

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
not work that should be moved to ME.  No retained log provides a sufficiently
current per-audio-stage baseline for the present branch, so the first workload
has **not** been selected yet.  A fresh real-PSP `MVS/mslug3` run is required
before migrating sound emulation.  During the current bootstrap work
`usbhostfs_pc` was running but `pspsh` did not establish a session, so no new
hardware measurement is claimed here.

## Milestones and status

### M1 - CPU reference seam [complete]

- `PSP_ME_AUDIO` exists and defaults to OFF.
- OFF links the common CPU producer only.
- ON selects the PSP producer integration boundary while retaining CPU render as
  fallback.
- PSP MVS ON/OFF builds and PSP CPS1/CPS2/NCDZ OFF builds passed locally.
- OFF contains no `meLib`, `meCore`, or `meSafe` symbols.

### M2 - optional ME dependency/bootstrap [implemented; hardware validation pending]

The PSP producer now initializes the safe-task Classic dispatcher and loads the
upstream AV module only when `PSP_ME_AUDIO=ON`.  Initialization failure logs the
error and leaves the producer on CPU.  Reset/shutdown wait for outstanding ME
work before unloading the module.

Build validation is complete locally.  Real-PSP validation is still required
for dispatcher startup, sleep/wake, repeated game changes and shutdown.

### M3 - deterministic shared-memory proof [implemented; hardware validation pending]

The ON backend dispatches one small 64-byte-aligned job during producer startup.
Allegrex writes two input words and writes back/invalidates the cache line; ME
invalidates it, computes XOR and addition results, writes the line back, and
Allegrex invalidates before verifying both words.  Dispatch error or result
mismatch unloads the AV module, marks ME unavailable, and leaves all PCM
production on CPU.  No emulator or sound-chip state is touched by the probe.

The proof is compiled and linked locally but still needs a real-PSP run before
it can be marked hardware-validated.

### M4 - first real audio workload [blocked on profiling]

Re-run the audio baseline on real PSP with `MVS/mslug3`, then choose one workload
only if its Allegrex compute cost is material and its state can be bounded.  Keep
the CPU implementation callable as oracle/fallback and compare produced PCM.

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
- Kernel-PRX loading and model-specific ME firmware behavior require real PSP
  validation; PPSSPP cannot validate them.
- Repeated dispatcher/module initialization across ROM changes needs hardware
  testing.
- Sleep/wake must be tested while no job is in flight and while the audio thread
  is active.
- Dispatch overhead may exceed the cost of small mixing/conversion jobs; profiling
  must choose job granularity before offloading real audio work.
