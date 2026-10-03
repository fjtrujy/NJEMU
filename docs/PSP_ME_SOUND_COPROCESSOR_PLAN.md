# PSP Media Engine Sound Coprocessor Plan

This document is the authoritative plan for the next PSP Media Engine experiment:
move the Neo Geo/MVS sound subsystem from the Allegrex CPU to the PSP Media
Engine (ME) as a persistent sound coprocessor.

This is a follow-up to `docs/PSP_MEDIA_ENGINE_AUDIO_PLAN.md`.  The existing work
already proved real ME execution through MIST and currently offloads only the
YM2610 ADPCM-A decode/mix workload.  The new goal is broader: investigate whether
ME can own the emulated Z80 + YM2610 sound machine, while Allegrex continues the
main 68000/video/game workload.

The implementation must remain evidence-driven.  Do not remove the existing CPU
sound path or the current ADPCM-A-only ME path until the full sound-coprocessor
path is correct, stable and measurably better on real PSP hardware.

## 1. Goal

Map the original Neo Geo sound architecture onto the PSP processors as closely as
practical:

```text
Neo Geo                          PSP
--------                         ---
68000 main CPU             ->    Allegrex
Z80 sound CPU + YM2610      ->    Media Engine
DAC / audio output          ->    Allegrex PSP audio backend
```

The intended runtime shape is:

```text
                 shared-memory protocol
        +--------------------------------------+
        |                                      |
        v                                      v
+-------------------+                 +-------------------+
| Allegrex          |                 | Media Engine      |
|                   |                 |                   |
| M68000            |  sound events   | Z80               |
| timers/video      | --------------> | YM2610 control    |
| game logic        |                 | FM / SSG          |
| renderer          | <-------------- | ADPCM-A / ADPCM-B |
|                   | status/results  | PCM generation    |
+-------------------+                 +-------------------+
        |                                      |
        |            PCM shared ring           |
        +<-------------------------------------+
        |
        v
  sceAudio* output
```

The ME must be treated as a persistent owner of the sound island, not as an RPC
accelerator called once for each Z80 instruction, YM2610 register write or audio
sample.

## 2. Why use shared-memory rings

The current ADPCM-A implementation dispatches one bounded MIST job per output
buffer.  That is appropriate for a coarse isolated DSP workload, but it would be
the wrong communication model for a complete sound CPU.

Moving the Z80/YM2610 subsystem to ME creates frequent small logical events:

- 68000 -> Z80 sound commands;
- reset/start/stop/lifecycle events;
- emulated-time advancement requests;
- reads that require a sound-side status/result to be current;
- PCM buffers produced by the sound subsystem;
- optional diagnostics and fault notifications.

A shared-memory ring avoids one MIST dispatch/wait per event.  MIST should be used
to bootstrap and run the persistent ME worker; normal communication should then
happen through shared memory.

The preferred design is several single-producer/single-consumer (SPSC) structures
rather than one contended bidirectional queue:

1. **Allegrex -> ME control/event ring**
   - written only by Allegrex;
   - read only by ME;
   - timestamped sound commands and lifecycle/scheduler events.
2. **ME -> Allegrex status/event ring**
   - written only by ME;
   - read only by Allegrex;
   - result/status changes that must be observed by the main emulation.
3. **ME -> Allegrex PCM ring**
   - written only by ME;
   - consumed by the PSP sound/output side;
   - contains complete output blocks or descriptors for complete output blocks.

A small shared status page may exist for monotonic counters and latest-state
snapshots, but it must not become an unstructured shared-global escape hatch.

## 3. Ring layout and coherency contract

All shared structures must follow the cache-coherency lessons already proven on
real PSP hardware.

### 3.1 Alignment

- ring headers are 64-byte aligned;
- producer and consumer counters live on separate cache lines where practical;
- entries are sized/aligned so cache maintenance never requires touching an
  unrelated ownership domain;
- all Allegrex/ME cache-maintenance ranges are rounded to 64-byte multiples;
- no producer and consumer may concurrently modify different words of the same
  cache line.

### 3.2 Sequence counters

Use monotonically increasing sequence numbers rather than only wrapped array
indices.

Conceptually:

```c
struct ring_cursor {
    uint32_t sequence;
    uint8_t padding[60];
};

struct shared_ring {
    ring_cursor producer;
    ring_cursor consumer;
    entry entries[POWER_OF_TWO_CAPACITY];
};
```

The actual C layout may differ after measurement, but the ownership rule must
remain clear:

- only the producer writes the producer cursor;
- only the consumer writes the consumer cursor;
- wrapped slot = `sequence & (capacity - 1)`;
- full/empty detection uses sequence distance;
- overflow/underflow is detected explicitly and treated as a correctness failure,
  not silently overwritten.

### 3.3 Publishing an entry

The producer must:

1. populate the complete entry;
2. write back the entry cache range;
3. update/publish its producer sequence;
4. write back the producer cursor cache line.

The consumer must:

1. invalidate/read the producer cursor;
2. invalidate the entry before consuming it;
3. process the entry completely;
4. update its consumer sequence;
5. write back the consumer cursor cache line.

Do not rely on `volatile` as a replacement for cache maintenance.

### 3.4 Backpressure

The normal path should not busy-wait on every event.  If a ring approaches full:

- first force the consumer to make progress;
- if the protocol cannot make progress within a bounded interval, fail the ME
  path and fall back to the CPU reference path through the normal restart flow;
- instrumentation must record the high-water mark so ring capacity can be tuned
  from real workloads instead of guessed excessively large.

## 4. Event protocol

Every Allegrex -> ME event must have an explicit type and emulated-time position.
Do not communicate by sharing mutable emulator globals directly.

Candidate event types:

```text
SOUND_RESET
SOUND_COMMAND
ADVANCE_TO_TIME
SYNC_POINT
SUSPEND
RESUME
SHUTDOWN
```

Additional types may be introduced only when a concrete dependency is identified.

Each timing-sensitive event should carry at least:

- monotonically increasing protocol sequence;
- event type;
- emulated timestamp or target emulated time;
- payload small enough to fit directly in the ring entry when practical.

The timestamp must be based on NJEMU emulation time, not wall-clock time.

### 4.1 Sound command path

Today the 68000 sound write path does approximately:

```text
68000 write -> SOUNDLATCH_TIMER -> sound_code -> pulse Z80 NMI
```

The ME path should preserve those semantics by enqueueing the command at the
correct emulated time.  The ME worker advances the Z80 sound machine to that time,
applies `sound_code`, and performs the NMI transition locally.

The main CPU should not synchronously wait for the Z80 merely because it wrote a
sound command.

### 4.2 Status/result path

Current main-CPU-visible state includes at least:

- `pending_command`;
- `result_code`.

These are modified by Z80-side behavior and read by the 68000-side emulation.
For the ME design, the authoritative sound-side values belong to ME.

A main-side read that requires an up-to-date result is a real synchronization
boundary:

1. publish an `ADVANCE_TO_TIME`/`SYNC_POINT` if ME has not yet reached the main
   CPU's current emulated time;
2. wait only until ME acknowledges that target time;
3. invalidate/read the sound status snapshot;
4. continue main CPU execution.

This preserves correctness while avoiding unconditional per-timeslice waits.

The implementation must measure how frequently these barriers occur in real MVS
games.  If they occur so often that useful overlap disappears, the full sound
coprocessor design may not be worthwhile.

## 5. ME ownership boundary

The eventual ME-owned state should include, as one coherent island:

- Z80 CPU core state (`CZ80`, registers, IRQ/NMI state, cycle accounting);
- Z80 RAM and bank-selection state required by sound execution;
- Neo Geo sound latch/result state that belongs to Z80 communication;
- YM2610 register/control state;
- YM2610 timers and IRQ generation insofar as they affect the Z80 sound island;
- FM, SSG, ADPCM-A and ADPCM-B synthesis state;
- PCM generation state.

The Allegrex side should retain:

- 68000 state and main scheduler ownership;
- video, input, game logic and rendering;
- PSP platform/audio device APIs (`sceAudio*` remains outside ME);
- global emulator lifecycle and save/load orchestration.

Do not let both processors mutate the same Z80/YM2610 state concurrently.
Ownership should move as a unit.

## 6. Scheduler strategy

The current MVS scheduler executes both CPUs serially inside every timer slice:

```c
cpu_execute(CPU_M68000);
cpu_execute(CPU_Z80);
```

The ME experiment must not simply replace the second call with `dispatch + wait`,
because that creates little or no parallelism.

The target model is asynchronous advancement:

```text
Allegrex                               ME
--------                               --
execute M68000 slice --------------->  advance Z80/sound toward target time
continue timer/video/main work          continue Z80/YM2610 work
...
only synchronize when main side needs a sound result or lifecycle barrier
```

The sound side must track a monotonic `sound_time_completed` value.  Allegrex may
publish future target times, but ME must process all timestamped events in order
and never advance past an event that has not yet been applied.

### 6.1 First scheduler implementation

Start conservatively:

- one requested target emulated time per current NJEMU timer slice;
- enqueue target/events without immediate wait;
- wait at the end of the slice only if required to keep the sound side from
  falling behind or before a main-side read dependency;
- record actual overlap and wait duration.

Only coarsen the synchronization interval after bit-exact/state-equivalent
validation shows that doing so preserves behavior.

### 6.2 YM2610 timers

YM2610 timers currently participate in the common timer subsystem and can assert
Z80 IRQ state.  Once Z80 + YM2610 live together on ME, prefer keeping their
internal timer/IRQ interaction local to the sound island.

The plan must first audit exactly which timer values are externally visible to
the 68000/global scheduler.  Do not split timer ownership until this dependency is
fully understood.

## 7. PCM ring

The complete sound-coprocessor design should produce final PCM on ME and place it
in a shared PCM ring.

The Allegrex sound thread remains responsible for PSP device output:

```text
ME:       produce PCM block N, N+1, ... -> PCM ring
Allegrex: consume ready block ----------> sceAudioSRCOutputBlocking()
```

The ring should have enough depth to absorb normal scheduling jitter without
adding unnecessary latency.  Start with a small measured depth (for example 3 or
4 complete buffers), instrument underrun/high-water behavior, and adjust only from
real hardware evidence.

Each PCM slot should carry:

- sequence number;
- sample count/format generation;
- completion/status flags;
- complete interleaved PCM buffer or a descriptor to a fixed shared slot.

The consumer must never call `sceAudio*` from ME.

## 8. Relationship to the existing ADPCM-A ME path

The current ADPCM-A-only offload remains the known-good ME implementation and an
important comparison point.

During development there should be three distinguishable execution modes:

1. **Main CPU reference**
   - current complete CPU sound path;
   - PPSSPP-compatible baseline.
2. **ME ADPCM-A accelerator**
   - current validated implementation;
   - useful performance/correctness reference.
3. **ME sound coprocessor (experimental)**
   - Z80 + YM2610 + PCM generation on ME through shared rings.

The third mode may initially be exposed only through a developer build option or
internal test selector.  Do not expose it as the normal user-facing `Media
Engine` mode until hardware validation is complete.

Once the full sound-coprocessor path is proven, decide from measurements whether:

- it replaces the ADPCM-A-only ME mode;
- ADPCM-A-only remains as a fallback tier;
- or the full design is abandoned and the current accelerator remains final.

## 9. PPSSPP and fallback contract

PPSSPP still does not execute the required ME path.

The existing compatibility rules remain mandatory:

- CPU reference path must remain buildable and runnable without ME dependencies;
- an ME-capable binary must retain a safe CPU fallback;
- `Main CPU` runtime mode must not initialize MIST;
- unsupported ME initialization must not prevent startup;
- no PPSSPP result may be cited as proof of ME correctness or performance.

The new persistent worker/ring implementation must be isolated so a failed ME
bootstrap cannot leave shared sound state half-owned by ME.

## 10. Save states, reset and lifecycle

Full sound ownership makes lifecycle correctness more important than in the
ADPCM-A accelerator.

Before any operation that serializes, resets or frees sound state:

1. stop accepting new producer events;
2. request ME to advance to an explicit synchronization point;
3. wait for acknowledgement;
4. drain or invalidate pending PCM as appropriate;
5. copy/serialize authoritative ME-owned state back into the normal NJEMU state
   representation if needed;
6. perform reset/save/load/switch;
7. reinitialize shared-ring generations and ME ownership before resuming.

Required scenarios include:

- emulator reset;
- game -> browser -> game switch;
- runtime AudioProcessor change;
- save state;
- load state;
- PSP suspend/resume;
- normal shutdown;
- ME transport failure.

Generation counters must make stale pre-reset/pre-load entries impossible to
apply after a lifecycle transition.

## 11. Failure containment

The full ME sound path is optional.  Correctness is more important than keeping
it alive after an error.

Detect at minimum:

- ring overflow/underflow;
- malformed/unknown protocol event;
- non-monotonic timestamp or sequence;
- ME worker heartbeat/progress timeout;
- cache-coherency oracle mismatch during validation builds;
- PCM underrun caused by ME falling behind;
- lifecycle generation mismatch.

On a failure that cannot be recovered safely in place, request the existing
emulator restart/lifecycle path and return to Main CPU rather than attempting to
continue with split ownership.

## 12. Profiling requirements

Before changing ownership, establish a new real-PSP baseline for MVS `mslug3`.
The current historical numbers are useful but insufficient to justify the full
migration.

Measure separately:

- M68000 execution time;
- Z80 execution time;
- YM2610 update/synthesis time;
- current ADPCM-A ME time/wait;
- main timer/scheduler overhead;
- video/render work;
- sound-thread producer/output wait;
- total uncapped frame time/FPS.

For the new path, additionally measure:

- Allegrex -> ME ring events/frame;
- ME -> Allegrex status events/frame;
- synchronous barriers/frame;
- time Allegrex spends waiting for ME;
- ring high-water marks;
- PCM ring underruns/high-water;
- ME sound emulated-time lag/lead;
- cache-maintenance cost;
- whole-emulator FPS.

The design is successful only if whole-emulator performance improves.  Moving
more code to ME while increasing synchronization enough to erase the gain is not
a success.

### C0 audit and instrumentation status (2026-10-03)

The ownership audit is complete enough to define the sound-island boundary and
the synchronization points that the later shared-ring protocol must preserve.
No ownership has moved yet.

#### 68000 <-> sound communication surface

The current MVS path crosses the main/sound boundary through the following
state and events:

- `neogeo_z80_w()` sets `pending_command` and schedules a zero-delay
  `SOUNDLATCH_TIMER` event carrying the command byte;
- `neogeo_sound_write()` applies that event by updating `sound_code` and pulsing
  the Z80 NMI line;
- Z80 port `0x00` reads `sound_code` and clears `pending_command`;
- Z80 port `0x0c` writes `result_code`;
- `neogeo_timer_r()` exposes both `result_code` and the command-pending state to
  the 68000 side.  This is therefore a genuine future Allegrex -> ME status
  synchronization boundary when ME becomes authoritative;
- YM2610 timer/status handling can assert or clear Z80 IRQ line 0 through
  `neogeo_sound_irq()`;
- Z80 ports `0x08..0x0b` select the four sound ROM banks.  The implementation
  currently materializes bank selection by copying ROM windows into
  `memory_region_cpu2`, so the semantic bank indices must be tracked explicitly
  by an ME implementation rather than comparing host pointers or copied backing
  addresses.

The command ordering contract remains:

```text
68000 sound write
    -> pending_command
    -> SOUNDLATCH_TIMER at current emulated time
    -> sound_code update
    -> Z80 NMI pulse
```

The future event-ring timestamp must represent the emulated time of this
transition, not the PSP wall clock.

#### Mutable state belonging to the sound island

The authoritative semantic state that a complete ME owner must eventually
contain or reproduce includes at least:

- CZ80 architectural state: primary/alternate registers, PC/SP, I/R, IFF/IM,
  halt/status and IRQ state;
- the writable Z80 RAM window at `0xf800..0xffff`;
- the four semantic Z80 bank selections and the ROM data visible through those
  windows;
- `sound_code`, `result_code` and `pending_command`;
- YM2610 register file, address latch, FM channel/operator/envelope/LFO state,
  timer/status/mode/IRQ state, SSG counters/envelope/RNG state, all six ADPCM-A
  decoder channels and ADPCM-B decoder state;
- synthesis accumulators required to make the next generated sample
  deterministic (`out_fm`, SSG/ADPCM accumulators and related FM intermediates);
- PCM ROM/cache view required by ADPCM-A/B decoding.  Cache pointers are not
  semantic oracle values and must be reconstructed or represented by stable
  offsets/blocks;
- sound frontend/resampler progress (`samples_this_update`, fractional sample
  carry and complete output-buffer sequencing) if final PCM generation moves to
  ME.  `sceAudio*` output itself remains Allegrex-owned.

The current ADPCM-A MIST job buffer and its `adpcma_control_generation[]`
counters are accelerator transport/concurrency state, not emulated YM2610
hardware state.  They remain part of the existing reference implementation
until the full coprocessor is proven.

#### Timer and synchronization dependencies

YM2610 Timer A/B currently live in the common MVS timer scheduler.  A timer can
shorten an active CPU slice; when this happens while the Z80 is active the
existing scheduler can also suspend the 68000 until the corresponding emulated
time.  Timer overflow updates YM2610 status/IRQ and may therefore alter Z80
interrupt state.

The audited future barrier classes are consequently:

- 68000 reads through `neogeo_timer_r()` when sound result/pending state must be
  current;
- reset, save/load state, game switch, suspend/resume and shutdown;
- any explicit scheduler barrier needed to preserve a YM2610 timer/status
  dependency while authority is split during migration.

Ordinary end-of-slice execution is **not** a required barrier.  The eventual ME
worker should advance independently toward timestamped emulated-time targets.

#### Focused PSP profiling

`PSP_ME_SOUND_PROFILE=ON` is now an opt-in PSP/MVS-only build option.  It leaves
normal builds unchanged and writes 300-frame windows to
`psp_me_sound_profile.log`.  It records:

- wall time / uncapped FPS for the window;
- M68000 execution total/average/max and invocation count;
- Z80 execution total/average/max and invocation count;
- non-CPU timer/scheduler work;
- timer-slice count;
- sound command, latch, command-read and result-write counts;
- 68000 sound-status reads, which are candidate synchronous dependencies;
- YM2610 status/data reads, Timer A/B callbacks, IRQ transitions and timer-driven
  slice preemptions.

`PSP_AUDIO_PROFILE=ON` remains the sound-thread profiler and supplies the
complementary YM2610 callback/producer/output measurements.  It now also reports
`me_wait`, the time spent specifically waiting for the current ADPCM-A MIST job.
Using the two profilers together keeps the main-scheduler statistics and audio
thread statistics single-writer rather than introducing profiling races.

Both instrumented PSP MVS configurations build successfully with `-Werror`:

- Main CPU: `PSP_ME_AUDIO=OFF`, `PSP_AUDIO_PROFILE=ON`,
  `PSP_ME_SOUND_PROFILE=ON`;
- current ADPCM-A accelerator: `PSP_ME_AUDIO=ON`,
  `PSP_AUDIO_PROFILE=ON`, `PSP_ME_SOUND_PROFILE=ON`.

#### Baseline status

The fresh controlled C0 baseline is now complete on real PSP hardware.  An
earlier `ldstart` attempt had been made from a fragmented long-lived PSPLink
session and was not used as evidence.  Resetting PSPLink through its own
`LoadExec` path restored the user partition to a clean state; the application
PRX then entered its normal `main()`/platform/file-browser/emulation lifecycle
and produced both profiler logs normally.

Both runs used MVS `mslug3`, Release, no GUI, highest performance level, sound
enabled at 44.1 kHz / 1472 stereo samples, no vsync/autoframeskip, and the 60 FPS
limit disabled.  A temporary **non-committed** input script indexed by
`frames_displayed` inserted a credit/start and then repeated movement, fire and
jump input.  The identical script was used for both binaries.  PSPLink
framebuffer captures confirmed active gameplay in both runs rather than attract
mode or the soldier-select transition.

The scheduler comparison below uses frame-aligned windows 8-11: four identical
300-frame ranges (1,200 emulated frames total).  The audio comparison uses
buffers 2-7, after startup/attract load had transitioned into the scripted
high-audio workload.

| metric | Main CPU | current ADPCM-A ME | difference |
| --- | ---: | ---: | ---: |
| uncapped whole-emulator FPS | 83.216 | 89.809 | +7.92% |
| YM2610 callback average | 6.900 ms | 4.860 ms | -29.57% |
| total producer average | 7.050 ms | 4.992 ms | -29.19% |
| post-process average | 0.144 ms | 0.127 ms | -0.017 ms |
| ADPCM-A `me_wait` average | 0 | 0.0265 ms | +0.0265 ms |
| M68000 execution / frame | 6.293 ms | 6.041 ms | measured only; ownership unchanged |
| Z80 execution / frame | 2.777 ms | 2.443 ms | measured only; ownership unchanged |
| scheduler-only work / frame | 0.0480 ms | 0.0475 ms | effectively unchanged |

The Main CPU run is the ownership-cost reference for later Z80 migration.  In
the selected gameplay windows the Z80 executed 3.8325 slices/frame, averaging
about 0.724 ms per slice and **2.777 ms total per frame**.  The lower measured
Z80/M68000 wall time in the ADPCM-A run must not be interpreted as either CPU
having moved to ME; both remain on Allegrex and their wall-clock timing can move
slightly as the competing audio-thread load changes.

The same 1,200 gameplay frames quantify the synchronization/event surface:

- 68000 sound-status reads: exactly **2.0/frame** (`2,400 / 1,200`); these are
  the primary candidate synchronous dependency once ME owns `result_code` /
  `pending_command`;
- 68000 -> sound commands: **0.0133/frame** (`16 / 1,200`) in these windows;
  these are asynchronous timestamped ring events, not inherent barriers;
- YM2610 Timer A callbacks: **2.819/frame**; Timer B was inactive;
- timer-driven active-slice preemptions: **0** in the selected gameplay windows
  (one was observed during startup), so end-of-slice synchronization is not
  justified by the measured gameplay path;
- Z80-local YM2610 status traffic was much more frequent (about 30.8 status-A
  reads/frame plus 2.819 status-B reads/frame), reinforcing that these accesses
  must remain local to the ME sound island rather than crossing a shared ring.

This fresh run also confirms that the existing ADPCM-A implementation remains a
meaningful performance target for the full coprocessor: the later authoritative
ME design must beat roughly 89.8 FPS / 4.99 ms producer time in this controlled
workload, not merely beat the Main CPU reference.

**C0 gate is closed successfully:** Z80 cost and the actual cross-domain
dependency/event frequency are now measured.  Z80 ownership still must not move
until the later lifecycle/shadow gates are satisfied.

## 13. Correctness/oracle strategy

Do not jump directly from the CPU implementation to exclusive ME ownership.
Build deterministic comparison stages.

### State oracle

At selected synchronization points compare CPU-reference and ME-shadow state for:

- Z80 registers and interrupt state;
- bank state and writable Z80 RAM;
- sound latch/result state;
- YM2610 registers/timers/status;
- relevant synthesis state;
- generated PCM.

Do not require pointer values to match; compare semantic state.

### PCM oracle

For bounded deterministic windows, the ME path should produce bit-identical PCM
to the CPU reference whenever the existing implementation itself is deterministic.
Any intentional difference must be understood and documented before performance
work continues.

### Timing oracle

Record and compare sound-command/NMI order and timestamps.  A matching final PCM
buffer alone is not enough if command timing has drifted.

## 14. Milestones

### C0 - document and baseline [complete]

- audit the complete MVS 68000 <-> Z80/YM2610 communication surface;
- identify every mutable object that belongs to the sound island;
- identify every timer callback/cross-domain read that forces synchronization;
- add focused real-PSP timing counters for M68000, Z80 and YM2610;
- establish Main CPU and current ADPCM-A-ME baselines on `mslug3`;
- record results in this document before changing ownership.

**Gate:** do not migrate the Z80 until its cost and synchronization frequency are
measured.

### C1 - shared-ring transport prototype [complete]

- implement generic PSP-private 64-byte-safe SPSC shared ring primitives;
- add deterministic host/unit tests for wrap, full, empty and sequence behavior;
- validate producer/consumer cache protocol on real PSP with MIST;
- run millions of synthetic ring messages and verify exact order/data;
- measure event throughput/latency without emulator state.

**Gate:** zero mismatches and no silent overwrite/underflow.

#### C1 validation status (2026-10-03) [complete]

The generic transport is implemented in `src/psp/psp_me_spsc_ring.*`.  The ring
uses fixed-size slots and deliberately makes cache ownership visible in the
layout rather than relying on `volatile` or implicit coherence:

- producer and consumer cursors occupy separate 64-byte cache lines;
- immutable ring configuration occupies a third cache line;
- every slot starts on a 64-byte boundary and its stride is rounded to 64 bytes;
- producer and consumer sequence counters are monotonic 32-bit counters, with
  power-of-two masking used only for the physical slot index;
- only the producer writes producer state and only the consumer writes consumer
  state;
- publication/acquisition use explicit memory barriers plus platform cache
  callbacks;
- full, empty, sequence-mismatch and corrupt-distance conditions are reported
  explicitly, with saturating diagnostic counters;
- producer high-water occupancy is measured rather than using a guessed large
  production ring.

The Desktop oracle covers full/empty handling, ownership, sequence corruption,
wrap across `UINT32_MAX`, 64-byte cache-maintenance ranges and 1,000,000 exact
ordered messages.  It passes under the normal Desktop MVS CTest configuration.
The identical transport source also cross-compiles in the PSP ME build with the
ME boundary compiled `-G0 -fno-pic`.

For hardware validation, `PSP_ME_RING_SELFTEST=ON` builds a standalone
`psp_me_ring_hardware_test` PRX and can also run the same oracle from the NJEMU
ME bootstrap.  The standalone PRX avoids ROM loading and emulator state; MIST is
used once to start an ME worker, after which normal traffic flows only through
two shared SPSC rings:

```text
Allegrex -> ME ring -> persistent test worker -> ME -> Allegrex ring
```

The real-PSP run completed exactly **1,004,096 round-trips** with `error=0`:

| hardware metric | result |
| --- | ---: |
| ping-pong messages | 4,096 |
| ping-pong wall time | 34.201 ms |
| average ping-pong round-trip | 8.349 us |
| bulk round-trips | 1,000,000 |
| bulk wall time | 6.345116 s |
| bulk round-trips / second | 157,601 |
| Allegrex -> ME high-water | 1 / 256 slots |
| ME -> Allegrex high-water | 2 / 256 slots |
| ring overflows | 0 |
| sequence mismatches | 0 |
| corrupt-distance detections | 0 |

The test deliberately busy-polls both queues, so empty-ring observations are
expected and explicitly counted (`5,054,954` on the ME consumer and `4,098` on
the Allegrex consumer).  They are detected underflows/empty polls, not missing
messages: all 1,004,096 expected payloads returned in exact sequence.  There
were no silent overwrites, no overflow and no payload mismatch.

The very low synthetic high-water values are evidence that capacity 256 is
oversized for this tight transport loop, but they are **not** used to choose the
event or PCM ring depth for the emulator.  C3/C5 gameplay measurements must size
those rings from their own scheduling jitter and latency requirements.

Compared with the previously measured approximately 59 us isolated MIST
dispatch/wait cost, the 8.349 us shared-ring ping-pong demonstrates why the
persistent-worker architecture is worth pursuing: normal messages no longer pay
one MIST trigger/wait per event.  This result is transport-only and does not yet
claim whole-emulator performance improvement.

**C1 gate is closed successfully:** the shared transport passed both deterministic
host validation and the real-PSP MIST cache/coherency oracle with zero semantic
mismatches.  C2 may proceed.  The independent C0 baseline gate still prevents
making the Z80 authoritative on ME.

#### C2 implementation and hardware status (2026-10-03)

`PSP_ME_SOUND_COPROCESSOR=ON` now builds an experimental persistent worker on
top of the C1 rings.  It is deliberately isolated from Z80/YM2610 ownership:
the CPU sound path remains authoritative, and the PSP producer reports generic
ME audio jobs unavailable while this option is active so the validated ADPCM-A
one-shot MIST job cannot race the persistent worker.

The worker protocol currently contains only lifecycle/control messages:

- `READY` after the single MIST trigger has entered the worker loop;
- `RESET(generation)` / `RESET_ACK`;
- `SYNC(generation, emulated_time)` / `SYNC_ACK`;
- `SHUTDOWN(generation)` / `SHUTDOWN_ACK`;
- explicit ring/protocol/generation/time-regression failure reporting.

Generation, command counts, reset/sync/shutdown counts, heartbeat, fatal error,
last token and current emulated time live in a dedicated ME-produced cache line.
The abort request lives in a separate Allegrex-produced cache line.  Command and
event rings retain the C1 single-writer cursor ownership and cache protocol.

A standalone real-PSP harness using this exact worker implementation completed
four complete worker cycles in one process.  Every cycle used one MIST trigger
and executed:

```text
READY -> RESET -> SYNC -> SYNC -> SHUTDOWN
```

The final-cycle hardware result was:

```text
passed=1 init=0 cycles=4 suspend_resume=1 elapsed_us=13864
generation=4 commands=4 resets=1 syncs=2 shutdowns=1 heartbeat=5
fatal=0 emulated_time=19000
cmd_high_water=1 cmd_overflow=0 cmd_underflow=52
event_high_water=1 event_overflow=0 event_underflow=2
```

The `suspend_resume=1` field in that harness means the ownership transition used
by suspend/resume was exercised as stop-worker -> fresh bootstrap with a new
generation in the same PSP process.  It is **not** evidence of a physical PSP
suspend/resume event.  PSPSDK can request suspend, but this unattended test has
no reliable software wake path; an actual power-callback suspend/resume remains
the final C2 hardware gate.

The normal PSP producer lifecycle is wired to the same implementation under the
experimental option:

- startup probes MIST, then bootstraps and resets the worker;
- emulator reset advances the lifecycle generation and sends `RESET`;
- suspend and shutdown stop the worker before ME ownership is dropped;
- resume follows the existing MIST reinitialization path and bootstraps a fresh
  worker generation;
- bootstrap/reset failure disables the experimental ME path while CPU sound
  remains available.

The integrated PSP MVS build passes with `-Werror`.  No Z80/YM2610 state has
moved and no MVS scheduler path has changed.

**C2 gate remains open only for a physical PSP suspend/resume callback cycle.**
Normal init/reset/shutdown behavior and repeated bootstrap in one process are
already hardware validated.

### C2 - persistent ME sound worker bootstrap [in progress]

- start one persistent MIST-backed worker for the sound experiment;
- worker idles on shared-ring state rather than requiring one MIST trigger per
  event;
- implement RESET/SYNC/SHUTDOWN protocol and progress counters;
- prove two complete init -> work -> shutdown cycles in one PSP process;
- prove suspend/resume recovery.

**Gate:** lifecycle is deterministic before any Z80 state moves.

#### C3 implementation status (2026-10-03) [hardware gameplay oracle pending]

Timestamped command shadowing is now implemented without changing sound
authority.  The CPU Z80/YM2610 path still performs the real latch/NMI handling
and produces all audible PCM.  The mirror is emitted from `neogeo_sound_write()`
**after** the authoritative `sound_code` update and Z80 NMI pulse, at the same
`SOUNDLATCH_TIMER` semantic transition measured in C0.

The timestamp is an integer 64-bit emulated-time value from the MVS timer
scheduler (`seconds * 1,000,000 + current scheduler microseconds`).  It does not
use PSP wall-clock time or floating-point `timer_get_time()`.  Equal timestamps
are legal; only regression within one lifecycle generation is rejected by the
ME worker.

Each mirrored event contains:

- lifecycle generation;
- monotonic protocol token;
- exact emulated-time timestamp;
- sound command byte.

The ME worker echoes that tuple without touching Z80 or YM2610 state.  Allegrex
keeps an ordered expectation FIFO and compares every echo semantically.  The
normal command path is non-blocking: it performs one ring publication attempt
and never waits for an ME response.  Echoes are polled asynchronously; lifecycle
`RESET`, `SYNC` and `SHUTDOWN` waits also drain any older shadow echoes in order
so control ACKs cannot be confused with data events.  Send failures, tuple/order
mismatches and pending high-water are explicit counters.

Because C2 lifecycle callbacks can run on the PSP sound thread while MVS
scheduler events run on the main emulation thread, access to the worker object is
serialized with a PSP lightweight mutex.  The frame loop does **not** take that
mutex unconditionally: it only samples the worker when a shadow echo is pending
or once per 300-frame diagnostic window.  The window/frame hints use explicit
atomic operations rather than `volatile`.

`psp_me_sound_shadow.log` records 300-frame windows with sent/matched/mismatch
counts, send failures, pending/high-water occupancy, ME-processed shadow count,
ring overflow state, fatal status and final emulated time.  This is sufficient to
measure commands/frame and prove exact order/timestamp equivalence during the
same controlled `mslug3` workload used for C0.

The standalone C2 hardware harness has also been extended to exercise 256
timestamped shadow commands per worker cycle (including groups sharing an equal
timestamp) and checks exact echo statistics in addition to lifecycle state.  The
updated harness and integrated PSP MVS coprocessor configuration build cleanly
with `-Werror`.  Desktop MVS remains **24/24 CTests green**, PSP production with
`PSP_ME_AUDIO=OFF` builds, and the existing ADPCM-A-only ME configuration builds
unchanged.

The real-PSP C3 oracle still has to be run after PSPLink is available again.
Until that run shows exact matches, zero send failures/overflows and negligible
whole-emulator impact, the C3 gate remains open and C4 Z80 shadow execution must
not begin.

### C3 - timestamped sound-command shadowing [in progress]

- leave CPU Z80/YM2610 authoritative;
- mirror sound-latch commands through the Allegrex -> ME event ring;
- ME records/echoes command/NMI timing only;
- compare event order/timestamps against the CPU path during real gameplay;
- measure events/frame and ring occupancy.

**Gate:** exact protocol ordering with no meaningful synchronization cost.

### C4 - ME shadow Z80 execution

- snapshot the initial Z80 sound state into ME-owned storage;
- replay timestamped events and execute a shadow Z80 on ME;
- CPU Z80 remains authoritative and continues gameplay normally;
- compare Z80 register/RAM/bank/IRQ state at bounded synchronization points;
- do not yet let the shadow Z80 modify the authoritative YM2610.

**Gate:** repeated long hardware windows match the CPU oracle.

### C5 - ME shadow complete sound island

- move a shadow copy of YM2610 and its sound-side timers into the ME worker;
- execute shadow Z80 -> YM2610 port writes locally on ME;
- generate shadow PCM into the shared PCM ring;
- CPU path remains authoritative for actual audio/output;
- compare Z80/YM2610 state, event order and PCM against CPU.

**Gate:** bit-exact/state-equivalent hardware oracle across representative games.

### C6 - ME becomes authoritative sound owner

- switch normal sound commands to the shared event ring;
- stop executing authoritative Z80/YM2610 on Allegrex;
- consume ME status/result snapshots at explicit synchronization points;
- consume ME-generated PCM through the Allegrex PSP audio output path;
- retain a restart-to-CPU fallback if protocol/ME failure occurs.

**Gate:** `mslug3` and a broader MVS test set run correctly with no sound or
emulation regressions.

### C7 - scheduler/barrier optimization

- measure actual barriers and wait time;
- reduce unnecessary end-of-slice synchronization while preserving timestamp
  semantics;
- coalesce `ADVANCE_TO_TIME` events where safe;
- tune ring sizes from observed high-water marks;
- optimize cache-maintenance granularity without violating ownership.

**Gate:** measurable whole-emulator improvement over the current ADPCM-A-only ME
path, not merely over Main CPU.

### C8 - lifecycle/save-state hardening

- save/load state with ME-owned sound state;
- game/browser/game transitions;
- AudioProcessor mode changes;
- reset;
- PSP suspend/resume;
- failure/restart fallback;
- repeated in-process initialization.

**Gate:** all existing M6 lifecycle coverage plus save/load-state coverage passes
on real hardware.

### C9 - final performance decision

Use identical frame-driven workloads on a real PSP to compare:

1. Main CPU;
2. current ADPCM-A-only ME accelerator;
3. full ME sound coprocessor.

Record:

- average and percentile frame time/FPS;
- Z80/YM2610 CPU time removed from Allegrex;
- ME wait/barrier cost;
- ring/cache overhead;
- audio underruns;
- correctness/oracle results.

Only then decide which ME mode should ship as the default `Media Engine` runtime
implementation.

## 15. Representative validation games

Start with `mslug3` because it is already the demanding hardware/performance
baseline and its ROM/cache setup is known.

Before declaring the design complete, add a small MVS set that exercises:

- frequent sound commands;
- heavy music/ADPCM-A;
- ADPCM-B usage;
- raster/timer-heavy gameplay;
- save/load state if enabled.

Choose the exact games from profiling/coverage evidence available in the repo and
local ROM corpus.  Do not hardcode assumptions merely to make the experiment
pass one title.

## 16. Non-goals

This plan does not initially attempt to:

- move the M68000 to ME;
- move graphics/GU submission to ME;
- call `sceAudio*` from ME;
- expose arbitrary common emulator globals directly to ME;
- change CPS1/CPS2/NCDZ audio architecture at the same time;
- remove the CPU sound implementation;
- treat PPSSPP as ME validation.

If the MVS sound-coprocessor experiment succeeds, the shared-ring/runtime pattern
can later be evaluated for other targets on its own profiling evidence.

## 17. Completion criteria

The experiment is complete only when all of the following are true:

- shared rings are deterministic and hardware-validated;
- Z80 + YM2610 state ownership is unambiguous;
- cross-domain timing semantics are documented and validated;
- generated PCM is validated against the CPU reference;
- save/load/reset/suspend/game-switch lifecycles are safe;
- CPU fallback remains functional;
- PPSSPP-compatible CPU operation remains functional;
- real PSP performance is measured with frame-identical or equivalently
  controlled workloads;
- the full sound-coprocessor path beats the existing ADPCM-A-only ME path enough
  to justify its complexity, or the plan explicitly records that it does not and
  retains the simpler implementation.
