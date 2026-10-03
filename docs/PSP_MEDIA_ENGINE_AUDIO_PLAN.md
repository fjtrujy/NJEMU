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
- `PSP_ME_AUDIO=ON` exposes one global runtime selector: Auto / Main CPU /
  Media Engine.  Auto is the default, Main CPU does not initialize MIST, and the
  other two modes retain the same safe CPU fallback if ME startup/probing fails.

PPSSPP does not implement the ME execution path required by this experiment.
The OFF build remains its reference configuration, but the final ON binary is
also usable there: Main CPU skips ME initialization entirely, while Auto and
Media Engine observe the unsupported MIST initialization and fall back to CPU.

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

The common producer contract exposes the lifecycle needed by this work:
`init`, `shutdown`, `reset`, `suspend`, `resume`, `render`, availability, and
bounded job-buffer/dispatch/wait operations.  The PSP ME producer owns ME
synchronization/fallback.  Generic sound-chip code does not contain
`PSP_ME_AUDIO` conditionals.

The CPU producer implementation itself is compiled in both OFF and ON builds.
OFF binds it directly; ON binds the PSP ME wrapper, which delegates lifecycle
and PCM rendering to that same CPU producer whenever no proven ME workload is
active.  This keeps the reference path available as a literal oracle/fallback,
not a second reimplementation of it.

## Upstream dependencies investigated

NJEMU uses the task-oriented library as its public ME integration layer:

- `mcidclan/psp-media-engine-safe-task`
- revision validated locally: `7d4c41f77b0be8815a720e0c8212ed504c01806d`
- MIT license
- built with `PRX_FREE=1` and `pspkubridge`.

Safe-task itself uses the lower-level mapper library:

- `mcidclan/psp-media-engine-custom-core`
- revision `fe871e12754060f3b42fbca6b251bb8ee3ade453`
- MIT license.

Custom-core maps native ME firmware entry points according to the detected ME
image and supplies the cache/DMACPLUS primitives used by safe-task and by the
NJEMU worker boundary.  Its raw `me-core` mode can replace the ME execution
environment with a custom handler; that is useful for experiments but is a
stronger ownership model than NJEMU needs for one bounded audio job per buffer.

Safe-task provides three discrete-task transports (Classic, Mini and MIST).
NJEMU initially validated Classic on hardware, then migrated to **MIST**.  MIST
injects the NJEMU task entry into an ME syscall-table slot through
DMACPLUS rather than hot-patching the ME core.  The audio producer registers
syscall index 13 once per producer initialization, triggers it for each job, and
waits with `meSafeTaskMistWait()`.  The ME thunk always returns through
`meSafeTaskMistFinish()`.

This removes the Classic path's `PSP_AV_MODULE_AVCODEC` load/unload cycle and the
`pspaudiocodec` link dependency while retaining System Controller to ME syscall
coexistence.  The upstream library also documents sleep/awake support for MIST;
NJEMU still treats a real suspend/resume test with its audio thread active as a
separate hardware validation item.

Earlier hardware isolation of Classic also established that safe-task's embedded
kernel-PRX mode (`PRX_FREE=0`) was not viable on the test PSP.  The same upstream
revision built with `PRX_FREE=1` works through `kubridge`.  MIST keeps that
PRX-free configuration, so NJEMU never needs to create/load a temporary
`kcall.prx` at runtime.

### Build integration

`PSP_ME_AUDIO=ON` currently expects the upstream libraries to be installed in
the active PSPDEV toolchain:

- headers `me-core-mapper/me-core-mapper.h` and
  `me-safe-task/me-stask-mist.h`;
- `libme-core-mapper.a`;
- `libme-stask.a` built with `PRX_FREE=1`;
- PSP `kubridge` support (`pspkubridge`).

The normal OFF build does not search for or link these libraries.  The regular
PSP GitHub Actions matrix explicitly configures `PSP_ME_AUDIO=OFF`.

The regular PSP matrix remains OFF-only.  A separate MVS build-only job installs
the two upstream dependencies from the exact revisions above, builds safe-task
with `PRX_FREE=1`, and compiles the MIST-backed `PSP_ME_AUDIO=ON` path; it does
not run PPSSPP or claim ME runtime coverage.  The custom-core dependency still
uses its own upstream kernel bridge build machinery, while NJEMU itself avoids
loading a temporary safe-task kernel PRX at runtime.

## Memory and synchronization findings

The upstream examples and safe-task implementation make the producer/consumer
boundary explicit:

- shared job/buffer storage must be at least 64-byte aligned;
- cache-maintenance ranges must also be rounded to 64-byte multiples; using an
  aligned pointer with a non-aligned byte count produced stale data on real PSP;
- Allegrex writes must be written back before ME consumes them;
- Allegrex must invalidate data written by ME before reading it;
- ME-side jobs use the mapped ME cache maintenance functions;
- `volatile` alone is not synchronization;
- a dispatched job must reach its transport wait completion before its buffers
  or referenced state can be reset/freed;
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

### M2 - optional ME dependency/bootstrap [hardware validated for MIST startup/producer teardown]

The PSP producer now initializes safe-task MIST only when `PSP_ME_AUDIO=ON`.
The dependency is built PRX-free, NJEMU links `pspkubridge`, and no AVCODEC
module is loaded.  Initialization failure logs the error and leaves the producer
on CPU.  Reset/shutdown waits for any outstanding ME work before releasing the
shared workspace.

On real PSP MIST initializes successfully and the ME-enabled MVS build reaches
`mslug3` with the normal PSP sound thread active.  A 900-frame hardware teardown
run exited through the normal emulation/sound shutdown path.  After the sound
thread stopped, direct state inspection showed `me_available=0`,
`me_job_in_flight=0`, null job/workspace pointers and zero workspace/cache sizes.
The later platform call to `sceKernelExitGame()` blocks under PSPLINK, so process
partition memory after that point is not used as producer-lifetime evidence.

A separate hardware harness exercised the actual NJEMU producer twice inside one
process: `init -> MIST job -> wait -> shutdown`, immediately followed by a second
identical cycle without a reset.  Both cycles produced the expected data.  The
final runtime-selector work later extended this to the full emulator and GUI ROM
browser; see M6.

### M3 - deterministic shared-memory proof [hardware validated]

The ON backend dispatches one small 64-byte-aligned job through MIST during
producer startup.  It deliberately uses the same 64-byte job descriptor and ME
entry thunk as real audio jobs.  Allegrex writes two input words and writes
back/invalidates the cache line; ME invalidates it, computes XOR and addition
results, writes the line back, and Allegrex invalidates before verifying both
words.  Dispatch error or result mismatch marks ME unavailable and leaves all
PCM production on CPU.  No emulator or sound-chip state is touched by the probe.

The MIST proof passed on the real PSP.  Inspection through psplink after startup
showed `me_available=1`.  The shared probe line contained the expected values:

- input A: `0x13579BDF`;
- input B: `0x2468ACE0`;
- ME XOR result: `0x373F373F`;
- ME addition result: `0x37C048BF`.

This confirms actual ME execution plus the Allegrex/ME cache-maintenance path;
it is not PPSSPP-derived validation.

### M4 - first real audio workload [hardware validated]

The selected workload is MVS YM2610 ADPCM-A decode in PCM-cache mode.  One job is
prepared per YM2610 output buffer rather than dispatching the per-sample helper.
Allegrex resolves any PCM-cache misses and copies only the bounded source bytes
needed by the six ADPCM-A channels into a 64-byte-aligned shared workspace.  The
job also contains a snapshot of the decoder state and the small decode tables;
the ME therefore never dereferences the mutable global YM2610 or PCM-cache
structures.  While ME decodes/mixes ADPCM-A, Allegrex continues FM, SSG and
ADPCM-B work, then waits once and merges the ADPCM-A contribution.

The first hardware comparison exposed a coherency bug that was useful in
clarifying the contract.  The shared allocation was aligned, but cache
maintenance used the logical 15,236-byte job size.  Upstream requires the range
size itself to be cache-line aligned.  Real PSP comparisons initially produced
299/300 mismatches; moving the decode tables into shared memory reduced that to
34/300, and rounding every Allegrex/ME cache-maintenance range to 64 bytes fixed
the remaining stale first-cache-line reads.  The final Classic hardware oracle
completed **300/300 jobs with 0 mismatches**.  After migrating the transport to
MIST, the same exact NJEMU decoder was run again against the independent CPU
reference on real hardware: **300/300 MIST jobs also completed with 0
mismatches**.  The logical job size was 15,236 bytes and every shared cache range
was rounded to 15,296 bytes.

If ME is unavailable, workspace allocation fails, source preparation cannot be
bounded, or dispatch fails before a job is in flight, that buffer follows the
existing CPU ADPCM-A path.  Key-on/key-off generation counters prevent completed
ME state from overwriting newer control state.  Reset/shutdown waits for any
in-flight job before shared storage can be released.

### M5 - hardware performance decision [first workload complete]

A real-PSP A/B run used the same MVS `mslug3` no-GUI Release configuration with
the speed limiter disabled and the focused audio profiler enabled.  In sustained
gameplay windows the representative averages were:

| metric | CPU path | ME ADPCM-A path | change |
| --- | ---: | ---: | ---: |
| YM2610 callback | ~6.29 ms | ~5.24 ms | ~-16.7% |
| total producer | ~6.44 ms | ~5.37 ms | ~-16.5% |
| post-process | ~0.14 ms | ~0.13 ms | negligible |
| blocking PSP output | ~26.93 ms | ~27.98 ms | expected slack transfer |

The sound-thread period remained locked to the 33.378 ms hardware output period,
so the lower compute time appears as additional time blocked waiting for the next
audio slot rather than changing audio cadence.

For whole-emulator throughput, the later sustained uncapped portion of the same
automated gameplay run averaged about **72.4 FPS on CPU vs 74.4 FPS with ME**,
roughly **+2.8%**.  This is a modest but positive whole-system improvement, not
just a profiler-counter reduction.  The temporary gameplay script was driven by
wall-clock time, so after the two builds diverge in speed their exact emulated
frame/input sequence is not perfectly synchronized; treat the +2.8% figure as an
indicative hardware measurement rather than a laboratory-grade frame-identical
benchmark.  No audio-period overruns or runtime instability were observed in the
profiling logs.  Audible-quality capture was not performed, so subjective glitch
assessment remains a manual hardware check.

Those whole-emulator figures were captured while the first implementation still
used safe-task Classic.  To isolate the transport change, a later real-PSP
microbenchmark ran 2,000 identical shared-memory `dispatch + wait` operations:

| safe-task transport | total | average/job |
| --- | ---: | ---: |
| Classic | ~176.0 ms | ~87 us |
| MIST | ~119.6 ms | ~59 us |

MIST therefore reduced the isolated transport overhead by roughly **32%** while
also removing the AVCODEC dependency.  The exact ADPCM-A output oracle remained
bit-identical after the migration.  A new whole-emulator MIST FPS number is not
claimed because the temporary wall-clock autoplay run did not stay in a valid,
comparable gameplay state; the transport microbenchmark is the accepted A/B for
this migration.

### M6 - runtime selector and lifecycle hardening [complete]

No second audio workload was migrated: the original profile shows ADPCM-B at only
about 2.4% sampled CPU time and no other remaining sound task with the same
coarse, bounded payoff as ADPCM-A.  Expansion therefore stops on evidence rather
than moving more sound code merely for ME coverage.

The ME-enabled PSP build now exposes a global `AudioProcessor` setting in
`njemu.ini` and in the system UI:

- `Auto` (default): try MIST, otherwise use the Main CPU;
- `Main CPU`: keep the CPU path and skip ME/MIST initialization completely;
- `Media Engine`: explicitly request MIST, with CPU fallback retained if the
  dispatcher or execution probe is unavailable.

The UI is compiled only when `PSP_ME_AUDIO=ON`.  Changing the setting persists it
and requests the emulator's normal restart flow so sound, memory and the producer
are torn down before the new processor choice takes effect.  Older `njemu.ini`
files have no migration requirement: the missing key leaves the initialized
default at Auto, and OFF builds ignore the extra key if they read a file written
by an ON build.

PPSSPP was used only for **fallback compatibility**, never as ME execution
evidence.  With the final ME-enabled producer, Main CPU completed startup without
invoking MIST.  Auto and Media Engine both received the expected unsupported
dispatcher result (`-4`) and continued successfully with `available=0`,
`canRunJobs=0` and CPU audio semantics.  All successful MIST/job claims below are
from real PSP hardware.

Real-PSP validation covered both the producer seam and full NJEMU:

- the producer harness passed Auto and Media Engine with a real MIST job,
  `suspend -> unavailable -> resume/reinitialize`, then a second correct MIST
  job; Main CPU stayed ME-free throughout;
- the same MVS PRX loaded `AudioProcessor=0/1/2` from `njemu.ini`; `mslug3`
  reached an active sound thread in all three modes, with `me_available=1/0/1`
  respectively;
- one running `mslug3` process was switched `Media Engine -> Main CPU -> Media
  Engine` through the emulator's real `LOOP_RESTART` lifecycle.  Each transition
  destroyed/recreated the sound thread and produced the expected `1 -> 0 -> 1`
  ME availability state;
- returning through `LOOP_BROWSER` left `me_available=0`, no job in flight,
  null/zero shared workspace state and no sound thread;
- a temporary browser-only hardware test then exercised the actual GUI
  `file_browser -> emu_main -> file_browser -> emu_main` sequence, switching from
  `mslug3` to `mslug` without unloading the MVS module.  The second ROM created a
  new sound thread and re-enabled MIST successfully.  The test hook was removed
  after validation.

Power handling is also lifecycle-aware now.  The PSP callback records suspend
and resume generations only; it no longer performs ME work in callback context
and no longer creates/registers another power callback on every notification.
The sound thread consumes those events, waits for outstanding producer work,
marks ME unavailable during suspend, and reinitializes/reinjects/probes MIST on
resume before allowing new ME jobs.  The sound thread and the CPS1/CPS2/MVS/NCDZ
emulation loops now share a 100 ms suspend poll interval rather than the previous
five-second waits; the NCDZ MP3 suspend loop uses the same value.  This avoids a
multi-second delay between `RESUME_COMPLETE` and resumed emulation/audio.

The producer-level suspend/resume sequence is hardware validated as described
above.  The full MVS sound-thread consumer was also exercised on real hardware by
injecting the exact `Sleep`/generation state transitions produced by the power
callback: while suspended, frames stopped, `me_available` became 0,
`me_suspended` became 1 and no job remained in flight; after the resume
generation was delivered, the same sound thread reinitialized MIST,
`me_available` returned to 1 and emulation resumed within the one-second
observation window.  This validates the in-process consumer path without claiming
that PSPLINK simulated a physical power event.

A literal PSP power-switch suspend/resume remains a manual validation operation
because PSPSDK exposes `scePowerRequestSuspend()` but no user-mode API to wake the
console again; triggering it remotely would intentionally sever the USB/PSPLINK
session.  This is a hardware-test limitation, not an unfinished producer
lifecycle path.

## Remaining risks

- Safe-task/custom-core firmware mapping support is still evolving upstream.
- Model-specific ME firmware behavior may still vary on PSP models other than the
  hardware tested here; PPSSPP cannot validate it.
- A physical power-switch suspend/resume with the final full emulator remains a
  manual hardware check; producer suspend/resume and post-resume MIST
  reinitialization have passed on real hardware.
- The first ADPCM-A workload is beneficial but only modestly at whole-system
  level; additional migrations should be attempted only when profiling shows a
  similarly coarse, state-bounded workload.
- The current job snapshots ADPCM-A state once per output buffer.  Future work
  that changes control/update timing must preserve the generation/lifecycle
  semantics proven here rather than exposing live YM2610 globals to ME.

## Validation checkpoint - 2026-10-03

Final local validation after the shared CPU fallback refactor:

- PSP MVS, `PSP_ME_AUDIO=OFF`, `PSP_AUDIO_PROFILE=OFF`: builds and packages;
- PSP MVS, MIST-backed `PSP_ME_AUDIO=ON`, `PSP_AUDIO_PROFILE=OFF`: builds and
  packages;
- the ME-enabled MVS build compiles both GUI and no-GUI variants so CI covers the
  runtime selector UI as well as the headless integration;
- PSP CPS1/CPS2/NCDZ, `PSP_ME_AUDIO=OFF`: all build and package;
- Desktop MVS builds and passes 23/23 CTests;
- PPSSPP fallback-only checks pass for all three runtime modes: Main CPU skips
  MIST, while Auto/Media Engine receive unsupported init and continue on CPU;
- real PSP producer tests pass Auto/Main CPU/Media Engine, including a real MIST
  job before and after producer suspend/resume;
- real PSP full-MVS tests pass config loading for `AudioProcessor=0/1/2`,
  `Media Engine -> Main CPU -> Media Engine` `LOOP_RESTART`, GUI
  `mslug3 -> browser -> mslug` switching, browser cleanup, and synthetic delivery
  of the power callback's suspend/resume generations to the live sound thread;
- the final MVS OFF ELF contains no `meSafe`, `meCore`, or ME-processing symbols;
- the final MVS ON ELF contains safe-task MIST and the NJEMU ME probe, with no
  `pspaudiocodec` link dependency;
- `git diff --check` is clean.

The focused profiler was also compiled successfully for MVS, NCDZ, CPS1 and
CPS2, and in combination with `PSP_ME_AUDIO=ON`.  With profiling disabled, the
MVS Release `.text`, `.data`, and `.bss` sizes are unchanged from the pre-profiler
build, confirming that the disabled instrumentation optimizes away.

Subsequent real-PSP validation completed M2/M3 and the first M4/M5 workload.
Dynamic cached YM2610 ADPCM-A is now dispatched as one bounded MIST job per output
buffer, passed a 300-job exact-output hardware oracle on both the original
Classic transport and final MIST transport, and the original ME workload reduced
the representative YM2610 callback by about 16.7% with an indicative ~2.8%
whole-emulator gain.  MIST itself measured ~32% lower isolated dispatch/wait
overhead than Classic and passed two consecutive producer init/job/shutdown
cycles in one PSP process.  Runtime Auto/Main CPU/Media Engine selection,
in-process mode restart, GUI ROM switching and producer suspend/resume recovery
are now implemented and hardware validated as described in M6.  No additional
audio workload currently meets the profiling threshold for further ME migration.
