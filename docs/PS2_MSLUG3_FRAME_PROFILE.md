# Metal Slug 3 PS2 frame profiling

This is an opt-in diagnostic for a real PS2 running Metal Slug 3 (MVS).
It does not alter normal builds, save-state files, or any runtime data under
`resources/`. Compile it only when tracing a reproducible slowdown.

**Status:** This document records real-hardware findings. The experimental
`PS2_FRAME_PROFILE` instrumentation and PS2Link adaptations were developed
in a separate, uncommitted worktree change set; a checkout containing only
this report does not yet support the diagnostic build flags below. Preserve
the measurements independently of those provisional code changes.

## Diagnostic build

Use the PS2 toolchain with `TARGET=MVS`, `PLATFORM=PS2`,
`GUI=OFF`, `SAVE_STATE=ON`, `USE_CACHE=ON`, and
`PS2_FAST_CACHE=ON`. The following additional C flags enable the tracer:

```text
-DPS2_FRAME_PROFILE=1
-DPS2_FRAME_PROFILE_AUTO_STATE_SLOT=0
```

The optional state-slot flag attempts to load
`<launchDir>/state/mslug3.sv0` once, after Neo Geo reset. A failed load is
reported to the debug console and does not overwrite the state file.
For the current PS2Link setup, the staged platform code initially enters
`mass:/NJEMU/MVS_cache/`, but that directory does **not** exist on the
console's storage device. The opt-in profiler corrects the working directory
to `mass:/NJEMU/MVS/` before resolving runtime paths. It selects
`mslug3` only if the no-GUI game selector was empty.

When using the staged PS2Link changes in this worktree, also pass
`-DPS2LINK_HOST_TRANSLATIONS=1 -DPS2LINK_FORCE_480I=1` as C flags.
Keep the PS2 toolchain's required `-D_EE -DPS2` definitions. The first
option loads language packs from `host:/lang/` without changing the
runtime data root. When combined with `PS2_FRAME_PROFILE`, it also loads
the required UI font from `host:/font/gbk_s14.bin`. The second option
requests a known 480i mode.

Disable `PS2_EXTERNAL_IRX_IMAGE` for this test: launch the standalone
`MVS.ELF` directly with `ps2client execee host:MVS.ELF`, not BOOT.ELF
or the external-image bootstrap.

### Real-hardware boot validation (2026-10-09)

The non-IMGIRX PS2Link ELF booted on a real PS2, with its UI font loaded
from `host:/font/gbk_s14.bin`. The first BIOS check failed because the
working directory pointed to the nonexistent `MVS_cache` path. A
read-only directory scan found the installed `mass:/NJEMU/MVS/` folder.
With the diagnostic-only directory override, the console found
`roms/neogeo.zip`, `roms/mslug3.zip`, and `state/mslug3.sv0`.
The game's ROM loading, cache initialization, audio initialization,
and automatic save-state loading all completed. Save-state load returned 1.

The direct PS2Link logs are retained in the ignored
`build_ms3_ps2_profile/` directory. The user-owned runtime files were
not copied, overwritten, or modified.

For an audio scheduling A/B test, add
`-DPS2_FRAME_PROFILE_NO_AUDIO_THREAD=1` to the diagnostic C flags and
build in a **separate** output directory. This only skips starting the
sound producer/output thread after the MVS sound-chip initialization;
it does not change the ROM selector or save-state slot. In particular,
setting the volume to zero or using the normal sound option is not an
equivalent control: those configurations may still generate and submit
audio buffers. Do not enable the no-audio flag in production builds.

The resulting `MVS.ELF` can be launched with `ps2client` against the
running PS2Link host. Run the client from the build output directory so
host-side language files are visible. Capture its console output to a
file outside the runtime data tree. Do not print a trace for every frame:
the profiler buffers 240 samples and reports the three slowest frames
once every four seconds of emulated time.

## Measurements

Each `[ps2-frame]` window reports:

- Total elapsed time, frames requiring over 16,667 microseconds excluding
  **VBlank sleep and explicit scheduler yields** (`late_work`), frames
  where the emulation phase alone exceeds the budget (`late_emu`), and
  maximum interval between executions of the audio thread
- CPU emulation, individual M68000 and Z80 execution, MVS rendering,
  MVS sprite drawing, C-ROM cache-miss I/O, native PS2 sprite command
  construction, and CLUT/atlas upload command construction
- GS FINISH waits, GIF DMA waits, complete flip time, and VBlank waits
- Audio-side PCM-cache misses and maximum elapsed PCM-cache read
- `[ps2-cpu]` reports time in `update_screen()`, scheduler yields,
  input polling, fixed-layer drawing, and sprite-batch submission
- Three slowest individual frames, with rendering-skipped and
  VBlank-synchronized flags

**Important:** The stages are nested rather than mutually exclusive:
rendering and CPU core execution are inside CPU emulation, C-ROM cache I/O
is inside sprite drawing, GPU waits can be inside rendering or
flip, and VBlank is inside flip. Presentation contains the explicit yield
and flip; the MVS sprite-batch duration is nested in sprite drawing.
Do not sum nested columns. The difference
between total and accounted stages can include input, audio scheduling,
cache reads, and other uninstrumented work. The audio gap is observed from
another thread and must not be added to the main-frame time. Elapsed PCM
read time can include EE thread scheduling delays while IOP I/O is in
progress; it is not a pure MX4SIO transfer benchmark.
`late_work` is wall-clock time less the *measured* explicit VBlank and
scheduler waits, not a CPU utilization counter: preemption or I/O inside
the other stages can still affect it.

A large native sprite or upload time supports investigating `video_driver`.
A large GS/GIF wait points to GPU/DMA synchronization. A large
emulation-minus-render delta points to CPU emulation or cache I/O.
A large audio gap without a large frame duration may indicate sound
producer/PCM cache stalls rather than the renderer.

## Validation boundaries

Compile and PCSX2 smoke tests can verify the diagnostic code path, but
PCSX2 timing does **not** establish PS2 hardware performance. The frame
trace must be collected on the physical console before attributing the
reported stuttering to a particular subsystem. Re-run the same state
without profiling before claiming a performance improvement.

## Findings on Metal Slug 3 (MVS / real PS2)

The runtime used an approximately 9.2 MiB C-ROM cache and a 3 MiB PCM
cache. The resolved cache demand-read size was 16 KiB, with the C-ROM
extent-aware reader active.

### Intermittent stalls: C-ROM cache misses

On the first frame after loading the state, total elapsed time was
approximately 517 ms, including 494 ms of synchronous C-ROM reads inside
the MVS sprite renderer. Later frames exceeding 50 ms included over
36 ms of C-ROM I/O. The sprite-to-GS command construction was typically
well under 1 ms and GS FINISH / GIF DMA waits were only a few
microseconds. **The stalls are not caused by blocking on PS2 GS/GIF
render submission.**

### Sustained slowdown without C-ROM misses

In a later busy part of the saved scene, windows with zero measured
C-ROM I/O still averaged approximately 21 ms per frame. One
240-frame window had 219 frames requiring over 16.667 ms excluding
VBlank sleep. The typical measured per-frame work included approximately
9.8 ms of M68000 emulation, 3.5 ms of Z80 emulation, and 2.3 ms
of MVS CPU rendering. The native PS2 flip was approximately 0.03 ms.
**Adaptive VSync can avoid waiting but cannot make that CPU work meet the
60 FPS deadline.** Execution/core improvements are needed in addition
to cache-miss improvements.

In a later lightweight scene, CPU emulation fell to about 10.5 ms and
the VBlank wait averaged 6 ms, so the same console maintained its
approximately 16.7 ms display cadence. This confirms the slowdown is
scene-dependent rather than a constant PS2 presentation penalty.

### CPU and rendering breakdown (2026-10-09 follow-up)

An expanded opt-in trace measured both the emulation work and the previously
unattributed time at the end of each frame. For 240 frames ending at
frame 1440 in a demanding scene **with zero measured C-ROM I/O**, the
real PS2 reported:

| Elapsed stage | Average per frame |
| --- | ---: |
| M68000 execution | 10.211 ms |
| Z80 execution | 3.458 ms |
| MVS CPU rendering | 2.371 ms |
| Other emulation/timer work, approximately | 1.723 ms |
| **Total emulation phase** | **17.763 ms** |
| Explicit EE scheduler yield | 3.850 ms |
| Input polling | 0.062 ms |
| Native PS2 frame flip | 0.026 ms |
| GS FINISH + GIF DMA waits | 0.009 ms |
| **Total elapsed frame time** | **21.883 ms** |

The `other emulation/timer` row is derived from total emulation minus
M68000, Z80, and MVS rendering; it includes timer/interrupt bookkeeping
and profiling overhead. Sub-stages must not be added to the total again.
Inside rendering, average MVS sprite drawing took 2.206 ms, of which
sprite batching/submission consumed 0.964 ms; fixed-layer drawing
accounted for 0.117 ms. All measurements are elapsed time rather than
hardware CPU-cycle counts.

The corrected counters showed `late_work=219` and
`late_emu=197` out of 240 frames for the same window. Thus an
explicit scheduler yield contributes substantially to *elapsed frame
duration* (often 3-4 ms in busy scenes), but is not purely wasted work:
it permits the lower-priority audio thread to run. **Even before the yield,
the emulation phase exceeds the 16.667 ms nominal deadline in a majority
of these profiled frames.** Removing the yield to improve the elapsed-frame
figure would be unsafe for audio scheduling.

The timing probes execute inside CPU slices and thus add nonzero overhead.
Treat the above numbers as bottleneck rankings and profiling measurements,
not as the definitive uninstrumented CPU utilization. Before optimizing,
compare a minimal instrumentation build and a production build on the same
state to estimate measurement overhead.

The practical priority outside cache I/O is to profile and optimize
M68000 execution first, then Z80 execution, and finally the MVS sprite
batch/rendering path. Avoid changing the PS2 GS/GIF synchronization to
address this slowdown: the measured waits are negligible.

### PS2 scheduler yield experiment

The PS2 main thread has priority 32 and the sound thread has priority 33.
The original cooperative yield queried the current thread status, lowered
the caller's priority to 33, rotated the ready queue, then restored its
priority. A simpler PS2-specific implementation temporarily lowers the
main thread to priority 34, letting ready sound work preempt it directly,
and restores priority 32 when the sound thread blocks. The experimental
legacy implementation remains selectable with `PS2_YIELD_LEGACY` for
diagnostic A/B builds.

On the same Metal Slug 3 save state, the 240-frame busy window ending at
frame 1440 gave the following **elapsed-time** measurements:

| Metric | Original yield | Simpler yield |
| --- | ---: | ---: |
| Frame elapsed average | 21.883 ms | 22.035 ms |
| Emulation phase average | 17.763 ms | 17.888 ms |
| Explicit yield average | 3.850 ms | 3.877 ms |
| Longest observed audio thread interval | 81.469 ms | 82.106 ms |
| Frames with emulation over budget | 197/240 | 206/240 |
| Measured C-ROM cache I/O | 0 | 0 |

This does **not** demonstrate an FPS improvement. The variation in the
emulation phase and thread scheduling is larger than the expected savings
from removing a couple of kernel calls. A yield with no other runnable
work is typically only a few microseconds in the traced frames. When
the sound thread is ready, the apparent 3-4 ms yield cost represents
elapsed time while audio work runs, not 3-4 ms of kernel overhead.
Removing the handoff could make frame-wall-time graphs look better while
causing worse sound. Keep the audio handoff intact and evaluate its
overhead separately from the audio processing it enables.

### Audio-thread-disabled PS2 A/B benchmark

Both builds used the same `mslug3.sv0` and a standalone `MVS.ELF`
launched through PS2Link (no IMGIRX). The sound-disabled diagnostic
logged `diagnostic audio producer/output thread DISABLED`; it reported
zero audio-thread wakeups and PCM cache misses. The audio-on run logged
successful PS2 audio initialization and save-state loading. The matched
240-frame window ending at frame 1440 had **no C-ROM I/O in either run**.

| Measured elapsed time, average per frame | Audio on | Audio thread off |
| --- | ---: | ---: |
| M68000 execution | 10.253 ms | 9.809 ms |
| Z80 execution | 3.449 ms | 3.444 ms |
| MVS CPU rendering (included in emulation) | 2.370 ms | 2.347 ms |
| Other emulation/timer work (derived) | 1.820 ms | 1.643 ms |
| **Complete emulation phase** | **17.892 ms** | **17.243 ms** |
| Yield wall-clock duration | 3.920 ms | 0.011 ms |
| Total frame elapsed | 22.082 ms | 17.524 ms |
| Emulation phases exceeding 16.667 ms | 206/240 | 167/240 |
| C-ROM I/O | 0 ms | 0 ms |

The no-audio figure gives a much better estimate of the *uncontended*
yield overhead: approximately **11 microseconds averaged across these
frames**, versus 3.920 ms when ready audio work was allowed to run. The
3.9 ms difference is largely useful scheduled audio work, **not** time
spent executing PS2 EE scheduler syscalls. Average frame elapsed time
fell by 4.558 ms, but this does not establish a 4.558 ms kernel overhead.

Without the audio thread, the emulation phase still averaged 17.243 ms
and exceeded the nominal 60 Hz frame budget in 167/240 frames. About
0.649 ms of the audio-on vs audio-off difference is *inside* the
emulation phase; the sources of this difference have not been isolated.
Disabling audio synthesis can also change sound-chip/timer progression,
which may affect later emulated behavior. The nearly equal Z80 and MVS
render times and matching sprite counts make this window useful for an
initial comparison, but it is not a perfect controlled experiment of
identical emulated execution. Neither profile represents uninstrumented
production CPU utilization.

Logs: `build_ms3_ps2_profile/ps2_mslug3_audio_on_ab.log` and
`build_ms3_ps2_profile_no_audio/ps2_mslug3_audio_off_yield.log`.

### Rebased ME branch: repeat PS2 hardware benchmark (2026-10-09)

After rebasing `me_sound_coprocessor` onto `ps2_vsync_improvements`,
the same `mslug3.sv0` was profiled on the physical PS2 through the
standalone non-IMGIRX MVS ELF. The original diagnostic probes and
audio-producer-thread-disabled control were integrated with the new
Neo Geo offload interfaces and timer implementation; no PS2 IOP audio
offload was enabled. Both versions used the same PS2 build options as
the earlier comparison. A fresh PS2Link launch and save-state load
completed for each variant.

The following values are 240-frame averages for the demanding,
**C-ROM-cache-miss-free** window ending at frame 1440:

| Average elapsed time | Before ME, audio ON | After ME, audio ON | Before ME, audio OFF | After ME, audio OFF |
| --- | ---: | ---: | ---: | ---: |
| M68000 | 10.253 ms | 10.057 ms | 9.809 ms | 9.795 ms |
| Z80 | 3.449 ms | 3.446 ms | 3.444 ms | 3.399 ms |
| MVS CPU rendering | 2.370 ms | 2.348 ms | 2.347 ms | 2.374 ms |
| Complete emulation phase | 17.892 ms | 17.508 ms | 17.243 ms | 17.167 ms |
| Yield wall-clock time | 3.920 ms | 3.027 ms | 0.011 ms | 0.011 ms |
| Whole frame elapsed | 22.082 ms | 20.807 ms | 17.524 ms | 17.448 ms |
| Frames exceeding emulation budget | 206/240 | 178/240 | 167/240 | 162/240 |

All four windows recorded **zero C-ROM I/O**. Audio-disabled runs
recorded zero PCM cache reads and no audio-thread wakeup gaps, confirming
that the audio producer was not active. The post-ME audio-enabled run
recorded six PCM cache misses (maximum 39.529 ms), compared with six
(maximum 37.354 ms) before ME in the same 240-frame window.

The audio-off emulation time improved by just **0.076 ms** (17.243 to
17.167 ms), which is too small to establish a genuine improvement
without repeated trials and measurement-overhead calibration. The
audio-on wall-clock average decreased by **1.275 ms**, mostly through
a shorter yield interval (3.920 to 3.027 ms), but this interval includes
audio-thread execution and scheduling, not a kernel yield cost. It is
not evidence that the PSP Media Engine is accelerating the PS2: the
PS2 runs the ordinary CPU audio backend and no new IOP worker exists.
Sound-thread scheduling and cache timing can vary between launches.

The persistent result is that the demanding section still exceeds
the 16.667 ms emulation budget with audio disabled. A worthwhile next
experiment is opt-in PS2 audio-producer stage profiling, separating
sound synthesis, mixing, cache reads, and blocking output, followed
by an isolated M68000 hotspot audit.

Post-ME raw logs (ignored build outputs):

- `build_ms3_ps2_profile/ps2_mslug3_me_rebased_audio_on.log`
- `build_ms3_ps2_profile_no_audio/ps2_mslug3_me_rebased_audio_off.log`

### PS2 audio pipeline timing (2026-10-09)

Commit `f632e025` added the opt-in `PS2_AUDIO_PROFILE=ON` CMake option.
It reuses the common sound producer/callback/post/output instrumentation
and adds PS2-specific time buckets for volume/MP3 mixing,
`audsrv_wait_audio()`, and `audsrv_play_audio()`. Profiling is disabled
in ordinary builds; the PSP audio metrics retain their existing layout.
For hardware collection, it was enabled alongside the experimental
`PS2_FRAME_PROFILE` and save-state auto-load options, launching the
standalone PS2 ELF through PS2Link after a successful reset.

The audio thread reports each 240 completed output buffers as one
`[ps2-audio]` record. Six completed windows covered the same
`mslug3.sv0` gameplay session. Each buffer contained 1,472 stereo
sample frames at 44,100 Hz (33.378 ms of playback).

| Sound-thread metric | Measured average across six windows |
| --- | ---: |
| YM2610 synthesis callback | 7.095-10.238 ms per buffer |
| Resampling/post-processing | approximately 0.103 ms (one window 0.172 ms) |
| Volume / MP3 mixing | typically 0.001 ms |
| `audsrv_wait_audio()` | 17.563-18.505 ms |
| `audsrv_play_audio()` | 16.553-17.431 ms |
| **Combined blocking output** | **34.128-35.948 ms** |
| **Audio-thread loop period** | **41.850-46.384 ms** |

Across all six windows (1,440 completed buffers), the mean producer elapsed
time was **8.702 ms** per buffer: **8.579 ms** in the YM2610 callback and
**0.115 ms** in resampling/post-processing. Volume/MP3 mixing averaged
**0.013 ms** (normally about 0.001 ms, with one scheduling outlier).
The output driver averaged **18.061 ms** in `audsrv_wait_audio()` and
**17.032 ms** in `audsrv_play_audio()`, or **35.118 ms** for the enclosing
output call. The mean loop period was **43.913 ms**, about 10.535 ms longer
than the 33.378 ms of playback represented by one buffer. This is a
throughput warning, **not** a measured SPU2 underrun count: the current
profiler does not observe IOP buffer occupancy or the actual playback cursor.

The sound producer itself averaged 7.275-10.349 ms. Large per-buffer
synthesis spikes were also observed (up to 113.019 ms elapsed), which
can include PCM cache misses or the audio thread being preempted.
The output stage's individual maxima reached approximately 54-56 ms.

The YM2610 callback includes FM, SSG, ADPCM-A/B decoding, and sample
fetches; this opt-in profile cannot yet assign exclusive elapsed or EE CPU
time to each component. The Z80 is executed by the main MVS emulation
timeline and is accounted for by the separate frame tracer, not by the
audio-thread callback. PCM cache-miss durations are recorded by the frame
diagnostic and may include blocked I/O and unrelated EE thread execution.

**These are wall-clock stages, not isolated EE CPU utilization.**
The `audsrv_wait_audio` time includes backpressure while waiting for
IOP audio-buffer capacity. `audsrv_play_audio` includes blocking
SIF RPC calls and transfer/service latency. Their sum therefore
cannot be interpreted as time spent executing a CPU audio codec.
Profiling reads the PS2 hardware timer frequently, and emitting a
summary on the sound thread every 240 buffers can perturb scheduling.
No actual SPU2 underrun counter was collected.

The 42-46 ms audio-thread loop period is longer than the 33.378 ms
represented by a block of samples. This is consistent with a
producer/output pipeline struggling to service real-time playback,
but needs an explicit underrun counter or output-buffer occupancy
trace to verify the audible failure mechanism.

This measurement **does not justify immediately moving YM2610 onto the
IOP**. The existing `audsrv` path already consumes substantial IOP/RPC
elapsed time. First isolate RPC/transfer from output-ring backpressure,
measure underruns and buffer occupancy, and benchmark whether asynchronous
or pipelined output can overlap generation with IOP transfer. Then
evaluate the available IOP budget against controller and MX4SIO I/O
before considering additional sound synthesis there.

Raw capture: `build_ms3_ps2_profile/ps2_mslug3_audio_stages_fresh.log`.

### audsrv queue and short-submission follow-up (2026-10-09)

The opt-in PS2 audio profiler now records `audsrv_wait_audio()` errors,
`audsrv_play_audio()` negative/short results, producer periods longer than
the block playback duration, and sampled IOP audio-ring occupancy. The
`PS2_AUDIO_PROFILE_QUEUE_INTERVAL` C definition defaults to 128 output
buffers per sample; setting it to 0 disables the extra ring-query RPCs
while retaining all timing and submission-result counters. Each sample
queries available ring bytes before waiting and queued ring bytes after
submission. These are **IOP audsrv ring snapshots**, not SPU2 hardware
underrun counters.

The physical PS2 ran the same `mslug3.sv0` save state twice after PS2Link
resets: first with queue sampling every 32 buffers, then with queue
sampling disabled. The first 240-buffer window of each run included
startup/state-loading stalls and is excluded from the following comparison.
Each column averages five subsequent 240-buffer windows (1,200 buffers):

| Audio-thread metric | Query every 32 | No queue queries |
| --- | ---: | ---: |
| YM2610 producer | 8.656 ms | 8.279 ms |
| `audsrv_wait_audio()` | 18.092 ms | 18.110 ms |
| `audsrv_play_audio()` | 16.884 ms | 17.142 ms |
| Complete output call | 36.081 ms | 35.265 ms |
| Loop period | 44.782 ms | 43.656 ms |
| Incomplete submissions | 115/1,200 | 117/1,200 |
| Wait errors / negative submit results | 0 / 0 | 0 / 0 |

Without additional occupancy queries, the incomplete submissions totaled
**575,264 bytes** across 1,200 requested 5,888-byte chunks. This is
approximately **8.1% of the requested PCM bytes**, with shortfalls up to
the entire buffer size. At the time of this measurement the application
ignored the positive byte count returned by `audsrv_play_audio()`, so any
unqueued tail of a short submission was not retried. The short submissions reproduce without
queue probes, which rules out those probes as their sole cause.

With a query every 32 buffers, each extra IOP RPC averaged 17.227 ms.
The audio-thread period was about 1.126 ms higher than in the no-query
control, consistent with the additional queries perturbing the run.
Among 38 sampled pre-wait snapshots, five showed less free space than
one requested output buffer. None of the 38 post-submit samples reported
an empty queue. These sparse snapshots cannot establish the actual
underrun rate or explain the apparent wait-success/short-submit mismatch.

The PS2SDK EE `audsrv_play_audio()` implementation reports the actual
bytes accepted by the IOP and divides large requests into synchronous
SIF RPCs. Its IOP implementation caps accepted bytes to the currently
available ring space. A 5,888-byte NJEMU buffer fits inside one normal
EE-side RPC packet, so the observed 16-17 ms submit stage is not due to
multiple packets per buffer. The precise cause of insufficient available
space *after* a successful wait remains unproven. Consult the
[PS2SDK EE RPC source](https://ps2dev.github.io/ps2sdk/audsrv__rpc_8c_source.html)
before changing output semantics.

**Priority:** investigate and handle incomplete submissions correctly,
then benchmark an overlapped/pipelined `audsrv` output path. Preserve the
current EE producer and do not move YM2610 to the IOP on the strength of
these wall-clock measurements alone. The hardware PCM playback cursor and
true SPU2 underrun count remain unobserved.

Raw captures (ignored diagnostic build output):

- `build_ms3_ps2_profile/ps2_mslug3_audsrv_queue_profile_reset.log`
- `build_ms3_ps2_profile/ps2_mslug3_audsrv_queue_off_profile.log`

### Completing partial audsrv writes (2026-10-09)

Inspection of the installed PS2SDK EE and IOP `audsrv` sources establishes
that the IOP caps each play request to currently available ring space and
returns the accepted byte count. The EE wrapper aggregates accepted counts
for up to 16,380 bytes per packet. NJEMU now uses PS2-private, testable
logic to advance by the **accepted prefix**, never by the requested bytes;
its individual calls are capped at 8,192 bytes to preserve this contract.
Three consecutive zero-progress replies abort a buffer instead of spinning.
The ordinary EE synthesis and NCDZ MP3 mixer remain intact.

The three build policies are:

- `PS2_AUDIO_RETRY_SHORT_WRITES=OFF` with `PS2_AUDIO_DIRECT_SUBMIT=OFF`:
  reproduce the previous single wait and play call (drops a partial tail).
- `PS2_AUDIO_RETRY_SHORT_WRITES=ON`, `PS2_AUDIO_DIRECT_SUBMIT=OFF`:
  wait before each partial-tail retry; reliable but slower on hardware.
- `PS2_AUDIO_RETRY_SHORT_WRITES=ON`, `PS2_AUDIO_DIRECT_SUBMIT=ON`:
  send directly, retry accepted tails, and wait only after zero progress.
  This is the new PS2 default. The IOP clamps direct writes to ring capacity.

All physical-PS2 runs launched the same `mslug3.sv0` state through a fresh
standalone PS2Link ELF; queue queries were disabled to avoid perturbation.
The legacy and direct figures below cover five subsequent 240-buffer windows
(1,200 buffers) after startup, while the wait-before-retry trial covers four
ordinary steady windows (960 buffers); its fifth contained a multi-second
outlier and is not used in this timing comparison.

| Mean audio-thread elapsed time per block | Legacy | Wait + retry | Direct + retry |
| --- | ---: | ---: | ---: |
| Audio producer | 8.279 ms | 7.729 ms | 7.835 ms |
| Complete audsrv output | **35.265 ms** | **41.977 ms** | **25.836 ms** |
| Audio loop period | 43.656 ms | 49.791 ms | **33.772 ms** |
| Previously short buffers | 117 | 127 | 549 |
| Successfully recovered short buffers | 0 | 127 | **549** |
| Unaccepted PCM bytes | 575,264 | 0 | **0** |
| Recovery failures | n/a | 0 | **0** |

The direct path needed 571 additional play RPCs across 1,200 buffers and
recorded 12 zero-progress calls. They were handled without residual lost
bytes. It recovered **607,000 bytes** rejected by initial plays. The
number of initial partial plays increases when the mandatory initial wait
is removed, but it does not imply missing audio: accepted bytes are retried.
The audio-loop period is now close to one block's 33.378 ms playback
duration. There was no SPU2 underrun counter or controlled listening test;
avoid claiming verified audible perfection or general 60 FPS gameplay.

These measurements are wall-clock timings with the frame/audio profiler
enabled and can vary with game scene, PCM cache stalls, and main-thread
scheduling. They demonstrate a substantial reduction in **audio output
latency**, not yet a causal improvement in emulation frame rate. Persistent
main CPU emulation hotspots and C-ROM cache misses still require separate
investigation.

The direct path is selected by default on PS2 only. The other two modes
remain available for regression comparisons. Desktop host tests exercise
full, partial, zero-progress, invalid, and failing submissions, including
accepted-prefix continuity.

Raw logs (ignored diagnostic build output):

- `build_ms3_ps2_profile/ps2_mslug3_retry_all.log`
- `build_ms3_ps2_profile/ps2_mslug3_direct_submit.log`

### Paired physical-PS2 frame pacing and audio continuity (2026-10-09)

To determine whether the faster output path also accelerates MVS emulation,
the physical PS2 was reset between **two new runs from the identical
`mslug3.sv0` save state**, both using commit `e1a552dd` and the same
PS2Link frame diagnostics, cache, VSync/game configuration and opt-in
`PS2_AUDIO_PROFILE`. Diagnostic queue queries were disabled. Only these
CMake switches changed:

- Baseline: `PS2_AUDIO_RETRY_SHORT_WRITES=OFF`,
  `PS2_AUDIO_DIRECT_SUBMIT=OFF`.
- Optimized: `PS2_AUDIO_RETRY_SHORT_WRITES=ON`,
  `PS2_AUDIO_DIRECT_SUBMIT=ON`.

The frame tracer accumulates 240 emulated frames per window. The comparison
joins windows by **identical `end=` frame indices**, between frames 1200
and 4320, and includes only records present in both PS2Link logs. One
baseline record (ending at frame 3120) was missing from the captured log;
it is excluded from *both* sides. Thus there are 12 matched windows,
covering 2,880 emulated frames. Startup/save-load transients are excluded.

| Metric, matched frame windows | Baseline | Direct + retry |
| --- | ---: | ---: |
| Frames whose non-wait work exceeds 16.667 ms | 2,282 / 2,880 | 2,262 / 2,880 |
| Frames whose emulation exceeds 16.667 ms | 2,039 / 2,880 | 2,024 / 2,880 |
| Mean MVS emulation elapsed | 17.378 ms | 17.359 ms |
| Mean M68000 elapsed | 10.008 ms | 9.998 ms |
| Mean Z80 elapsed | 3.227 ms | 3.217 ms |
| Mean rendering elapsed | 2.370 ms | 2.370 ms |
| Mean complete frame elapsed | 20.222 ms | 21.065 ms |
| Mean explicit scheduler yield | 2.572 ms | 3.434 ms |
| PCM cache misses observed | 108 | 112 |

The 20 fewer work-over-budget frames are only 0.69 percentage points of
the sample. This is **not evidence of a meaningful 60 FPS improvement**:
roughly 79% of measured busy-scene frames still exceed the non-wait work
budget. The audio optimization does not materially change M68000, Z80 or
rendering work. The 0.843 ms rise in average complete-frame elapsed time
closely matches the 0.862 ms rise in explicit scheduler yield: the audio
thread receives more execution time, rather than the MVS emulator doing
more work. The tracer's `late_work` subtracts explicit yield/VBlank waits;
it is not itself a display-present or dropped-frame counter.

The audio-thread windows have a different clock from the emulated-frame
windows. The following comparison uses the five subsequent 240-buffer
audio windows from **each** run, excluding the first startup window;
these are not claimed to be frame-number-aligned:

| Metric, 1,200 completed audio buffers per run | Baseline | Direct + retry |
| --- | ---: | ---: |
| Audio producer elapsed per buffer | 8.316 ms | 8.116 ms |
| Complete audsrv output per buffer | 35.734 ms | **25.941 ms** |
| Audio loop period | 44.138 ms | **34.113 ms** |
| Loops longer than 33.378 ms of playback | 1,176 | 618 |
| Loops longer than 66.756 ms | 70 | **11** |
| Buffers with unaccepted PCM bytes | 122 | **0** |
| Unaccepted PCM bytes after all attempts | 589,888 | **0** |
| Initially short buffers recovered | 0 | **550** |
| Recovery failures | n/a | **0** |

The old path abandoned about 8.35% of the PCM bytes it requested.
The direct path recovered 625,952 bytes across 550 initially short
submissions, with no remaining short buffers. It reduced mean output
time by 27.4% and the mean audio-loop period by 22.7%. The number of
audio-loop intervals exceeding **twice the nominal buffer duration** fell
by about 84% (70 to 11). This is clear evidence of more continuous PCM
delivery and fewer *observed scheduling-delay risks*, **not** a measured
reduction in audible SPU2 underrun events.

The installed PS2SDK IOP implementation computes a 44.1 kHz, stereo,
16-bit ring capacity of approximately 18,800 bytes (about 106.6 ms at
176,400 bytes/second). Actual play-ahead depends on occupancy, and the
IOP playback thread does not expose an underflow counter through the
public `audsrv` API. Some audio-loop maximum gaps still approach or exceed
that theoretical full-ring duration, and no physical audio capture or
controlled listening comparison was conducted. Proving actual underrun
counts would require an opt-in **IOP-side audsrv probe**, with correct
producer/consumer accounting and a low-overhead way to return aggregated
events; polling ring occupancy from EE adds substantial RPC overhead.

**Next decision:** retain direct + suffix retry as the PS2 audio default
for PCM completeness and lower output latency, but optimize the M68000/Z80
and cache/rendering hotspots separately for frame pacing. Do not justify
IOP YM2610 offloading or claim glitch-free playback from these results.

Fresh raw console logs (ignored diagnostic build outputs):

- `build_ms3_ps2_profile/frame_ab_legacy.log`
- `build_ms3_ps2_profile/frame_ab_direct.log`

### PS2 MVS 68000 native-word memory A/B (2026-10-09)

The paired audio experiment above showed about 10 ms per frame in the
M68000 alone. To determine whether part of that cost came from memory
access, a temporary `PS2_C68K_OPCODE_PROFILE` sampled one opcode and
instruction address every 256 executed C68K instructions, reporting only
every 240 emulated frames. This identified the `0x000000-0x01ffff`
game-code range as the dominant source of sampled instructions. Instruction
frequency does **not** measure instruction execution cost, so the sampler
was **disabled for the final timing comparison**.

The MVS memory interface previously assembled even-address 16-bit words
from two byte loads and writes. On little-endian PS2 EE, aligned memory
can instead be loaded/stored as one native halfword. The
`mvs/native_word_access.h` helpers use `memcpy` with explicit alignment
assumptions so the MIPS compiler can emit native halfword instructions
without violating C effective-type aliasing. Odd addresses retain the
original bytewise behavior; mirrored addresses preserve wraparound.
An object-code check verified an EE `lhu` instruction for the even-address
read path. The optimization is confined to **MVS on PS2**, controlled by
`PS2_MVS_NATIVE_WORD_ACCESS` (default ON for that combination); OFF
retains the original word-access implementation.

Using the physical PS2 at `192.168.1.10`, the same diagnostic
`mslug3.sv0` state was loaded after resets in both configurations.
Identical `PS2_FRAME_PROFILE`, `PS2_AUDIO_PROFILE`, PCM/C-ROM caching,
audio direct+retry, and game/output settings were maintained. The C68K
opcode sampler was not compiled into either ELF. An initial OFF startup
stalled before ROM selection and produced no usable frame data; its
capture was discarded. A fresh PS2Link reset and OFF relaunch succeeded,
including `state load result=1`.

The frame logs contain occasional missing 240-frame reports. Only
**ten common gameplay windows**, selected by their identical `end=`
frame indices between 1200 and 4320, are included below. This is
2,400 matched emulated frames, excluding startup and easier attract
scenes after the gameplay sequence:

| Metric, matched frame windows | Portable bytes (OFF) | Native words (ON) |
| --- | ---: | ---: |
| M68000 stage mean | 10.000 ms | **9.803 ms** |
| Total MVS emulation mean | 17.253 ms | **17.059 ms** |
| Whole-frame elapsed mean | 20.945 ms | **20.730 ms** |
| Z80 stage mean | 3.165 ms | 3.169 ms |
| Rendering stage mean | 2.314 ms | 2.304 ms |
| Frames over 16.667 ms non-wait work | 1,877 / 2,400 | **1,707 / 2,400** |
| Frames over 16.667 ms emulation | 1,646 / 2,400 | **1,560 / 2,400** |
| PCM cache misses | 100 | 99 |

Native-word access saves about **0.197 ms per frame in M68000 execution**
(1.97%) and **0.194 ms in total emulation** in this state. There are
170 fewer non-wait work-budget misses, a 7.1 percentage-point difference,
but **1,707 of 2,400 frames still exceed budget**. The work-budget
counter excludes explicit scheduler yields/VBlank; it is not a direct
count of dropped display frames. This improvement is measurable, not
sufficient for consistent 60 FPS.

The isolated host test `mvs_native_word_access_tests` verifies all byte
offsets across aligned and odd reads, masked mirrored reads, writes,
and end-of-region wraparound against a bytewise reference. Production
PSP/Vita/Desktop code and the other emulator targets keep their
existing memory paths. Both paths remain selectable for regressions.

Raw hardware captures and build output (ignored diagnostic directory):

- `build_ms3_ps2_profile/word_ab_on_192_168_1_10.log`
- `build_ms3_ps2_profile/word_ab_off_retry_192_168_1_10.log`
- `build_ms3_ps2_profile/c68k_opcode_profile.log`
- `build_ms3_ps2_profile/c68k_memcpy_profile.log`

### Audio producer / PCM cache

PCM cache miss durations ranged up to 0.88-1.47 s during startup/early
gameplay; those elapsed spans may include scheduling delays and must not
be interpreted as raw media transfer times. In later stabilized windows,
the largest PCM miss lasted roughly 36-42 ms, while the maximum observed
interval between audio-thread iterations was commonly 80-150 ms.
This provides concrete evidence of audio-side delays compatible with the
reported stuttering, but an audio-buffer underrun counter is still needed
to establish exactly when audible underruns occur.

The sustained busy-scene measurements were obtained from the save state
with `PS2_FRAME_PROFILE` enabled. Instrumentation itself adds timer
reads and therefore a small measurement overhead; the normal binary must
be used for final performance claims.

### Next optimization experiments

1. Separate IOP transfer time from EE/audio-thread scheduling for PCM
   misses; count actual audio buffer underruns.
2. Investigate asynchronous or predictive C-ROM/PCM cache prefetch so
   misses do not synchronously block sprite rendering or audio production.
3. Investigate M68000 and Z80 execution and timer slice overhead;
   on the tested busy scene these dominate the cache-free emulation budget.
   The MVS sprite batching/rendering path is a secondary opportunity.
4. Repeat the same state with an uninstrumented baseline and one
   optimization at a time; do not assume 60 FPS based solely on PCSX2.
