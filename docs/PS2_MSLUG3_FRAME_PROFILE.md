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
