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
generation in the same PSP process.  It is **not** by itself evidence of a
physical PSP suspend/resume event.  A later real power-switch test did reach the
physical `SUSPENDING` callback and produced a clean worker stop at generation 3:

```text
reason=stop generation=3 frames=206 sent=48 matched=48 mismatches=0
send_failures=0 pending=0 pending_high_water=1 me_processed=48
command_high_water=1 command_overflow=0 event_high_water=1 event_overflow=0
fatal=0 emulated_time=1083037817
```

The PSP re-enumerated on USB after wake, but the existing PSPLink/usbhostfs
session did not recover and the console ultimately had to be restarted.  That
means the physical suspend half is proven, while the post-`RESUME_COMPLETE`
fresh-worker generation is still unobserved.  This remaining resume check is
explicitly deferred rather than treated as passing.

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

**C2 remains open only for observing the post-wake `RESUME_COMPLETE` worker
rebootstrap on real hardware.**  Normal init/reset/shutdown behavior, the real
physical suspend stop, and repeated bootstrap in one process are already
hardware validated.  The original plan treated the remaining resume observation
as a hard ownership gate; the current development decision explicitly defers it
so it no longer blocks C6 implementation or ownership experiments.  It must still
be revisited before final lifecycle/release validation.

### C2 - persistent ME sound worker bootstrap [in progress]

- start one persistent MIST-backed worker for the sound experiment;
- worker idles on shared-ring state rather than requiring one MIST trigger per
  event;
- implement RESET/SYNC/SHUTDOWN protocol and progress counters;
- prove two complete init -> work -> shutdown cycles in one PSP process;
- prove suspend/resume recovery.

**Gate:** lifecycle is deterministic before any Z80 state moves.

#### C3 implementation and hardware status (2026-10-03) [complete]

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
with `-Werror`.  A Desktop host oracle now also compiles the **same production
`psp_me_sound_worker.c`** behind no-op cache stubs and a POSIX-thread dispatch.
It validates 10,048 exact ordered shadow commands, equal timestamps, pending
shadow echoes interleaved with `RESET`, subsequent `SYNC`/shutdown, and fatal
rejection of an emulated-time regression.  To keep the shared worker context
cache-line exact on both ABIs, its padding is derived from pointer width; the
64-byte static assertion passes on both Desktop 64-bit and PSP 32-bit builds.
Desktop MVS is now **25/25 CTests green**.  PSP production with
`PSP_ME_AUDIO=OFF` builds, and the existing ADPCM-A-only ME configuration builds
unchanged.

PPSSPP fallback was revalidated with this same coprocessor-enabled PSP binary,
installed temporarily as normal homebrew so its `MEMSIZE=1` attribute was
preserved.  With `AudioProcessor = Main CPU`, `mslug3` loaded and ran for the
15-second compatibility window and NJEMU logged:

```text
[PSP_ME_AUDIO] Audio processor: Main CPU; ME initialization skipped
```

This confirms that compiling C3/ME support does not make ME execution mandatory
under PPSSPP.  This is fallback compatibility evidence only; it is not evidence
for any ME execution or coherency claim.

The real-PSP C3 oracle is now complete.  The standalone hardware harness first
ran four complete worker lifecycles with **256 timestamped shadow commands per
cycle**, including groups of commands sharing the same timestamp.  The final
cycle reported:

```text
passed=1 init=0 cycles=4 suspend_resume=1 elapsed_us=44179
generation=4 commands=260 resets=1 syncs=2 shutdowns=1
shadow_commands=256 shadow_sent=256 shadow_matched=256
shadow_mismatches=0 shadow_send_failures=0 shadow_pending=0
shadow_pending_high_water=1 heartbeat=261 fatal=0 emulated_time=19000
cmd_high_water=1 cmd_overflow=0 event_high_water=1 event_overflow=0
```

The integrated `mslug3` run exposed one lifecycle bug before the gameplay oracle
could start.  The PSP lightweight mutex protecting the shared worker object had
been created with `initialCount = 1`, which means the mutex starts locked.  The
first worker lock therefore failed before the persistent worker could bootstrap;
the standalone harness did not use this integration mutex and was unaffected.
Changing the initial count to zero makes the mutex start unlocked, after which
the integrated worker bootstrapped normally on the real PSP.

The final controlled gameplay run used the same C0 settings and the same
frame-indexed `mslug3` input sequence.  It ran through a normal emulator shutdown
so the worker's final pending queue was drained rather than terminating the PRX
from PSPLink.  Across the complete run the shadow oracle observed **231 commands
sent and 231 matched**, with:

- zero tuple/order/timestamp mismatches;
- zero send failures;
- zero command-ring and event-ring overflows;
- maximum pending depth of 1;
- command/event ring high-water of 1;
- `fatal=0` in every window;
- final `reason=stop` with `pending=0`.

The selected 1,200-frame C0 comparison window (300-frame windows 8-11) retained
the exact same command counts in both same-session runs: `4, 5, 4, 3` commands.
With the worker compiled but `AudioProcessor = Main CPU`, the current binary ran
those windows at **84.328 FPS**.  With the C3 shadow worker active it ran them at
**83.285 FPS** (-1.24%).  The scheduler/profiled control work moved from about
**50.34 us/frame** to **57.94 us/frame**, an absolute increase of only about
**7.60 us/frame**.  For additional context, the earlier C0 Main CPU baseline was
83.216 FPS, effectively identical to this C3 run; the small whole-emulator delta
is within the run-to-run variation already visible in the controlled hardware
measurements.  C3 also intentionally keeps YM2610 production on Allegrex and
reserves MIST for the persistent worker, so the ADPCM-A-only ME result is not the
appropriate baseline for measuring command-shadow overhead.

**C3 gate is closed successfully:** the real PSP preserves exact command order
and emulated timestamps with no queue loss and negligible synchronization cost.
C4 may now proceed under the temporary C2 resume-evidence waiver documented
above.  CPU Z80/YM2610 authority must remain unchanged while that waiver exists.

### C3 - timestamped sound-command shadowing [complete]

- leave CPU Z80/YM2610 authoritative;
- mirror sound-latch commands through the Allegrex -> ME event ring;
- ME records/echoes command/NMI timing only;
- compare event order/timestamps against the CPU path during real gameplay;
- measure events/frame and ring occupancy.

**Gate:** exact protocol ordering with no meaningful synchronization cost.

#### C4 implementation and hardware status (2026-10-03) [complete]

C4 now runs an isolated CZ80 instance inside the persistent ME worker while the
existing Allegrex Z80/YM2610 path remains fully authoritative.  CZ80 gained an
explicit pointer-free logical state representation plus per-instance read-base
configuration, removing the old MVS fast-path dependency on the global
`memory_region_cpu2` array.  Desktop CPS1/CPS2/NCDZ builds and PSP MVS builds
with both the normal CPU path and the earlier ADPCM-A-only ME mode were rebuilt
after this change to confirm that the shared core remains compatible outside the
new experiment.

The C4 snapshot contains the logical CZ80 state, the visible 64 KiB Z80 address
space, source ROM/bank metadata and the sound latch/result state.  The ME owns a
private 64 KiB copy and applies bank switches to that copy only.  The snapshot
storage is cache-line exact across the Allegrex/ME boundary; real hardware found
that a snapshot spanning a second cache line could otherwise retain a stale
generation field after a lifecycle restart, so the shared snapshot object is now
published/acquired as an aligned 128-byte object.

Each authoritative Allegrex Z80 slice records the sound-side I/O it actually
observed and sends one batched oracle record after the CPU slice completes.  The
ME then executes the same cycle budget and checks:

- logical CZ80 register/interrupt state;
- active Z80 ROM banks;
- periodic Z80 RAM hashes (first slice and every 64 slices);
- exact I/O operation order, port and value.

YM2610 remains entirely authoritative on Allegrex in C4.  YM reads are replayed
to the shadow Z80 using the value returned by the real YM2610, while YM writes
are validated but do not mutate any shadow YM device yet.  A hardware gameplay
failure also exposed an ordering requirement for YM-generated IRQ transitions:
an IRQ raised synchronously by a YM write must be recorded inside the current
slice trace and applied by the ME at that exact I/O point, rather than being sent
ahead of the batch.  IRQ transitions that occur outside a Z80 slice still use
the lightweight asynchronous IRQ command.

The host oracle executes the same synthetic program once as a reference CZ80
and once inside the production worker, including port reads/writes, an IRQ
transition, RAM writes and full state comparison.  Desktop MVS remains **25/25
CTest green**.  The final standalone real-PSP worker harness also completed four
lifecycles successfully; its final cycle reported:

```text
passed=1 init=0 cycles=4 suspend_resume=1 elapsed_us=716231
generation=4 commands=263 resets=1 syncs=2 shutdowns=1
shadow_commands=256 shadow_sent=256 shadow_matched=256
z80_snapshots=1 z80_irqs=1 z80_slices=1 z80_io=3
z80_state_mismatches=0 z80_ram_mismatches=0 z80_bank_mismatches=0
z80_io_mismatches=0 z80_send_failures=0 z80_last_mismatch=0
z80_batch_high_water=1 z80_batch_overflow=0 fatal=0
```

The controlled integrated `mslug3` hardware run then used the same C0/C3 BIOS,
configuration and frame-indexed input script and exited through the normal
shutdown path.  It produced 19 consecutive 300-frame C4 checkpoints plus the
final 130-frame drain window.  Across those windows the worker validated:

- **22,494 Z80 slices**;
- **503,561 Z80 I/O/IRQ trace entries**;
- **16,350 IRQ transitions**;
- **229 command-shadow sends and 229 matches**;
- zero Z80 state, RAM, bank or I/O mismatches;
- zero command, event or Z80-batch ring overflows;
- zero Z80 send failures, local failures or worker fatal errors;
- Z80 I/O peak **187/256** entries in one slice;
- Z80 batch-ring high-water **4/8**.

The earlier 128-entry I/O trace was intentionally fail-closed and exposed a
real gameplay slice reaching 135 entries.  The final sizing uses 256 I/O entries
per batch and reduces the batch ring from 16 to 8 slots, so the observed
gameplay margin increases without materially increasing the worker's batch-ring
memory budget.

The final integrated stop record remained fully active and clean:

```text
reason=stop generation=2 frames=130 sent=2 matched=2 mismatches=0
send_failures=0 pending=0 z80_active=1 z80_irqs=366 z80_slices=498
z80_io=13543 z80_state_mismatches=0 z80_ram_mismatches=0
z80_bank_mismatches=0 z80_io_mismatches=0 z80_send_failures=0
z80_io_peak=187 z80_batch_high_water=4 z80_batch_overflow=0 fatal=0
```

**C4 gate is closed successfully:** repeated real-PSP gameplay checkpoints match
the authoritative CPU Z80 with no state or transport divergence.  CPU Z80 and
YM2610 still own all emulation/audio output.  C5 proceeded under the deferred C2
resume-evidence waiver; that physical lifecycle check remains outstanding but is
not currently blocking C6 implementation.

### C4 - ME shadow Z80 execution [complete]

- snapshot the initial Z80 sound state into ME-owned storage;
- replay timestamped events and execute a shadow Z80 on ME;
- CPU Z80 remains authoritative and continues gameplay normally;
- compare Z80 register/RAM/bank/IRQ state at bounded synchronization points;
- do not yet let the shadow Z80 modify the authoritative YM2610.

**Gate:** repeated long hardware windows match the CPU oracle.

### C5 - ME shadow complete sound island [complete]

#### C5 control/timer shadow status (2026-10-03) [complete subphase]

The first C5 subphase now gives the persistent ME worker its own independent
YM2610 control/timer context while the Allegrex YM2610 remains authoritative.
The shared YM2610 core was refactored to support explicit contexts rather than a
single process-wide mutable instance; the legacy API still targets the default
context, and a host oracle proves that two control/timer contexts keep registers,
timer state, IRQ state and callback routing independent.

When the C4 Z80 snapshot is installed, Allegrex initializes a separate YM2610
context with the active sample rate and publishes it to the ME.  The ME then
binds its own timer/IRQ callbacks and executes the shadow Z80's YM2610 accesses
locally:

- ports `0x04`/`0x05` read shadow YM status/data and write address/data A;
- ports `0x06`/`0x07` write address/data B locally;
- port `0x06` ADPCM end/busy reads deliberately remain an Allegrex oracle until
  the PCM/ADPCM portion of C5 runs on the ME;
- Allegrex sends each authoritative YM timer overflow to the worker immediately
  before executing its own `YM2610TimerOver()`;
- the shadow `YM2610ContextTimerOver()` generates its own IRQ transition, applies
  it to the ME CZ80, and the existing C4 IRQ trace validates the exact transition
  rather than driving it.

This keeps the experiment fail-closed: any status, IRQ ordering or Z80 state
divergence disables the shadow path while CPU Z80/YM2610 and CPU-produced audio
continue to own emulation output.

Desktop MVS is **26/26 CTest green**, including the new independent-context and
worker YM timer/status oracles.  PSP builds also remain green with the sound
coprocessor enabled, with `PSP_ME_AUDIO=OFF`, and with the earlier ADPCM-A-only
ME mode.  Desktop NCDZ was rebuilt after the shared YM2610 refactor as an
additional compatibility check.

The standalone real-PSP C5 worker harness completed four lifecycles.  Its final
cycle reported:

```text
passed=1 init=0 cycles=4 suspend_resume=1 elapsed_us=833188
generation=4 commands=265 resets=1 syncs=2 shutdowns=1
shadow_commands=256 shadow_sent=256 shadow_matched=256
z80_snapshots=1 z80_irqs=1 z80_slices=2 z80_io=7
z80_state_mismatches=0 z80_ram_mismatches=0 z80_bank_mismatches=0
z80_io_mismatches=0 z80_send_failures=0 z80_last_mismatch=0
z80_batch_high_water=1 z80_batch_overflow=0 fatal=0
```

The deterministic integrated `mslug3` real-hardware run then kept the C5 YM
shadow active through normal shutdown.  Across 19 300-frame checkpoints plus the
final 130-frame drain window it validated:

- **22,494 Z80 slices**;
- **503,942 Z80 I/O/IRQ trace entries**;
- **16,350 validated YM-generated IRQ transitions**;
- **229 command-shadow sends and 229 matches**;
- zero Z80 state, RAM, bank or I/O mismatches;
- zero command/event/batch overflows, send failures, local failures or worker
  fatal errors;
- Z80 I/O peak **187/256**, batch-ring high-water **3/8**, and command-ring
  high-water **9/16**.

The final stop record remained fully active and clean:

```text
reason=stop generation=2 frames=130 sent=2 matched=2 mismatches=0
send_failures=0 pending=0 z80_active=1 z80_irqs=366 z80_slices=498
z80_io=13600 z80_state_mismatches=0 z80_ram_mismatches=0
z80_bank_mismatches=0 z80_io_mismatches=0 z80_send_failures=0
z80_io_peak=187 z80_batch_high_water=3 z80_batch_overflow=0 fatal=0
```

This closes only the **control/timer** portion of C5.  Shadow PCM generation,
ADPCM end/busy ownership and CPU-vs-ME PCM/state comparison remained required at
that checkpoint.  The deferred C2 physical-resume evidence remains an outstanding
lifecycle item, but under the current development decision it no longer blocks
C6 ownership experiments.

#### C5 PCM/render shadow status (2026-10-04) [complete subphase]

The persistent worker now renders the complete shadow YM2610 sound island while
Allegrex remains authoritative for audible output.  PCM file/cache ownership stays
on Allegrex: the ME first describes the exact compressed ADPCM-A/ADPCM-B byte
windows required by its own decoder state, Allegrex fills those bounded immutable
windows from the existing resident PCM or PCM cache, and the ME then renders into
a dedicated shared job buffer.  Allegrex compares both output channels bit for bit
and validates ADPCM-B status before accepting the period as a shadow match.  The ME
never performs filesystem I/O and never dereferences the mutable Allegrex PCM cache.

Several real-hardware races were intentionally allowed to fail closed during this
subphase and were fixed at their semantic owner rather than hidden by tolerance:

- preparing a PCM window from Allegrex decoder state was unsafe because cache I/O can
  yield while the main emulation thread advances; PCM ranges are now prepared from
  the ME-owned decoder state itself;
- an audio callback could preempt Allegrex halfway through a Z80 slice after its YM
  writes had happened but before the complete slice trace reached the ME; a PSP-only
  YM mutation/render gate now serializes Z80/timer YM mutations against a shadow
  render period while preserving the CPU path as authority;
- even with that gate, the first render could start from different phase/envelope/
  ADPCM state because the CPU audio thread may advance after `YM2610Reset()` and
  before the C4 Z80 snapshot.  Production snapshots now clone the **live authoritative
  YM2610 context** at the same gated boundary as the Z80 snapshot, then rebind every
  context-internal pointer/callback for ME ownership and switch only the PCM source to
  bounded windows.  Synthetic worker tests keep their independent fresh-context
  bootstrap.

The clone is semantic rather than a raw portable pointer copy: OPN channel pointers,
detune-table pointers, FM algorithm connections, ADPCM pan pointers, callbacks and
cache-only pointers are all rebound to the destination context.  A host oracle checks
that the cloned render state is equivalent while remaining independently mutable.

The final standalone real-PSP harness built from this code completed all four
lifecycles:

```text
passed=1 init=0 cycles=4 suspend_resume=1 elapsed_us=1338949
generation=4 commands=267 resets=1 syncs=2 shutdowns=1
shadow_commands=256 shadow_sent=256 shadow_matched=256
z80_snapshots=1 z80_irqs=1 z80_slices=2 z80_io=7
z80_state_mismatches=0 z80_ram_mismatches=0 z80_bank_mismatches=0
z80_io_mismatches=0 z80_send_failures=0 z80_last_mismatch=0
z80_batch_high_water=1 z80_batch_overflow=0 fatal=0
cmd_high_water=2 cmd_overflow=0 event_high_water=1 event_overflow=0
```

The deterministic integrated `mslug3` real-PSP workload then ran to its scripted
shutdown with the complete C5 shadow active through `reason=stop`.  Across 19
300-frame shadow windows plus the final 130-frame drain record it validated:

- **2,216 ME YM2610 renders / 1,651,171 stereo sample frames** against the CPU
  oracle bit for bit;
- **22,494 Z80 slices** and **502,547 Z80 I/O trace entries**;
- **16,350 validated YM-generated IRQ transitions**;
- **229 command-shadow sends and 229 matches**;
- zero PCM, ADPCM status, Z80 state, RAM, bank or I/O mismatches;
- zero YM render errors, send failures, local failures, worker fatal errors or
  command/event/Z80-batch overflows;
- maximum Z80 I/O payload **187/256**, command-ring high-water **6/16**,
  Z80-batch high-water **2/8**, event-ring high-water **1** and shadow-command
  pending high-water **1**.

The final hardware record remained fully active and clean:

```text
reason=stop generation=2 frames=130 sent=2 matched=2 mismatches=0
send_failures=0 pending=0 z80_active=1 z80_irqs=366 z80_slices=498
z80_io=13273 z80_state_mismatches=0 z80_ram_mismatches=0
z80_bank_mismatches=0 z80_io_mismatches=0 z80_send_failures=0
z80_io_peak=187 z80_batch_high_water=2 z80_batch_overflow=0
ym_renders=51 ym_samples=38001 ym_render_errors=0 ym_pcm_mismatches=0
ym_status_mismatches=0 ym_send_failures=0 fatal=0
```

Hardware-test bootstrap note: production `MVS.prx` must be launched directly from a
clean PSPLink state.  `libme-stask` loads its embedded `kcall` module itself.  Manually
pre-loading the run-directory `kcall.prx` caused `meSafeTaskMistInit()` to return
`-4` and correctly fall back to CPU; the same known-good older C5 binary reproduced
that behavior, while direct launch restored MIST immediately.  This is test-environment
state, not a C5 PCM failure.

The PCM/render subphase is therefore closed for the long `mslug3` hardware oracle.

#### C5 representative-game hardware closure (2026-10-04) [complete]

The representative-game gate exposed two C4/C5 assumptions that the original
`mslug3` workload did not stress enough, and both are now represented explicitly in
the shadow protocol rather than hidden by tolerance:

- `wjammers` can start/reprogram a YM2610 timer from inside the currently executing
  Z80 `OUT` instruction such that the authoritative `timer_adjust()` forces CZ80's
  `ICount` to zero before that instruction charges its cycles.  Allegrex now records a
  `PREEMPT` trace event at that exact callback boundary and sends the slice's original
  requested cycle budget.  The ME YM timer callback consumes the marker and zeroes its
  own CZ80 `ICount` at the same semantic point.  A host oracle reproduces this case
  directly.
- the previous 256-entry per-slice I/O trace was intentionally fail-closed and
  `wjammers` exceeded it.  The final layout uses **512 I/O entries per slice and 4
  batch slots** instead of 256 x 8.  Both layouts reserve 2,048 I/O-entry slots across
  the batch ring, so the larger single-slice headroom does not increase the dominant
  ring payload budget.  A host oracle now replays one **300-I/O-event** slice to keep
  this requirement covered.

The final exact scripted PSP binary was then run from a clean PSPLink state across
three materially different MVS titles.  CPU Z80/YM2610/audio remained authoritative
for every run:

| game | Z80 slices | Z80 I/O events | YM renders | sample frames | timer preempts | max I/O / slice | result |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| `mslug3` | 22,494 | 501,814 | 2,289 | 1,705,564 | 1 | 187 / 512 | clean through `reason=stop` |
| `wjammers` | 14,041 | 550,685 | 1,717 | 1,279,360 | 3 | 367 / 512 | clean through `reason=stop` |
| `fatfury1` | 16,823 | 144,212 | 1,973 | 1,470,109 | 0 | 284 / 512 | clean through `reason=stop` |

Across all three runs:

- every command-shadow send matched in order;
- `z80_active=1` remained true through the final stop record;
- PCM and ADPCM status remained bit-exact/state-equivalent to the CPU oracle;
- Z80 register, RAM, bank and I/O comparisons remained exact;
- YM render errors, local failures, send failures and worker fatal errors were zero;
- command, event and Z80-batch ring overflows were zero.

`wjammers` was the tightest observed transport case after the final sizing:
command-ring high-water **7/16**, event-ring high-water **4**, Z80 batch-ring
high-water **3/4**, and I/O peak **367/512**.  The 3/4 batch high-water is acceptable
for the shadow-only C5 gate but must be reconsidered before an authoritative C6 path
can rely on the worker without fail-closed CPU fallback.

The final regression matrix after these fixes is also clean: Desktop MVS is
**26/26 CTest green**, Desktop NCDZ builds, and PSP MVS builds in CPU-only,
ADPCM-A-only ME and full sound-coprocessor configurations.

**C5 gate is closed successfully:** the complete ME sound island now matches the CPU
oracle across representative real-PSP workloads while the CPU path remains fully
authoritative.  C6 may proceed under the explicit waiver for the deferred physical
post-`RESUME_COMPLETE` observation; that check remains a final lifecycle/release
item rather than a current development blocker.

- move a shadow copy of YM2610 and its sound-side timers into the ME worker;
- execute shadow Z80 -> YM2610 port writes locally on ME;
- generate shadow PCM into the shared PCM ring;
- CPU path remains authoritative for actual audio/output;
- compare Z80/YM2610 state, event order and PCM against CPU.

**Gate:** bit-exact/state-equivalent hardware oracle across representative games.

### C6 - ME becomes authoritative sound owner

#### C6 readiness: ME status snapshot dry-run (2026-10-04) [complete subphase]

Before moving any authority away from Allegrex, the worker now publishes the
main-visible sound communication state that C6 will eventually own.  This is a
dedicated **64-byte, ME-written cache-line snapshot** containing:

- lifecycle generation and a monotonically increasing snapshot sequence;
- completed emulated time;
- `sound_code`;
- `pending_command`;
- `result_code`;
- current YM/Z80 IRQ state;
- initialization state.

Allegrex only invalidates and reads this snapshot; it never writes it.  The ME
publishes it after snapshot bootstrap, sound-command application, Z80 slices,
timer/IRQ transitions and explicit `SYNC` completion.  A host oracle verifies a
complete sound-side transition where a pending command is consumed by Z80, the
result byte is written back and a later `SYNC` publishes the expected completed
emulated time.

The integrated MVS path remains deliberately **non-authoritative**.  Every 300
frames, after the existing C5 checkpoint `SYNC`, Allegrex reads the ME snapshot
and compares `sound_code`, `pending_command`, `result_code` and completed time
against the still-authoritative CPU state.  The 68000-visible read path in
`neogeo_timer_r()` is unchanged and continues to use the Allegrex-owned values.

Real-PSP dry-run evidence with the same complete C5 worker is clean:

- standalone worker: 4/4 lifecycles pass with the status snapshot read after the
  final `SYNC`, with no protocol/state/cache failure;
- `mslug3`: **19/19** 300-frame status checkpoints match, zero status mismatches,
  C5 remains active through `reason=stop` and all existing PCM/Z80/YM oracles stay
  clean;
- `wjammers`: **19/19** status checkpoints also match under the higher-command,
  timer-preemption workload, again with zero status or existing C5 mismatches.

Desktop MVS remains **26/26 CTest green** after this plumbing; Desktop NCDZ and
PSP MVS CPU-only, ADPCM-A-only ME and full sound-coprocessor builds also remain
green.

This proves the future **ME -> Allegrex status/result channel and
`sound_time_completed` representation**, but it does not yet prove autonomous ME
scheduling.  In this dry-run the ME still advances because Allegrex supplies the
C5 CPU-oracle `Z80_SLICE` stream, and `SYNC` merely confirms the time reached by
that already-replayed work.  Later C6 subphases replace this with scheduler-horizon
messages that let the ME execute Z80/YM without CPU I/O/state replay.

#### C6 readiness: autonomous Z80 advance + checkpoint oracle (2026-10-04) [complete subphase]

The next C6 dry-run now removes the per-slice CPU oracle from the production path.
After the initial Z80/YM snapshot, Allegrex sends an asynchronous `Z80_ADVANCE`
message containing only the requested Z80 cycle budget, the scheduler's current
`timer_left` value and the resulting emulated timestamp.  It no longer sends the
slice's I/O trace, expected CZ80 state, banks, RAM hash, YM IRQ trace or explicit
timer-preemption markers.  The existing C5 `Z80_SLICE` replay remains available to
the host/hardware oracle tests, but it is not used by the integrated autonomous
path.

The ME Z80 port handlers now consume the ME-owned `sound_code`, YM2610 context,
bank state and result byte directly.  YM IRQ transitions are applied directly to
the ME CZ80.  A timer started/reprogrammed by a Z80 `OUT` also reproduces the MVS
scheduler's exact preemption rule locally: `timer_adjust()` compares the new timer
duration against the scheduler's **`timer_left`**, not merely against CZ80's current
`ICount` remainder.  This distinction was exposed immediately by `wjammers`: the
first implementation used the latter approximation and the first 300-frame oracle
checkpoint diverged in `HL`.  Passing the semantic scheduler `timer_left` value to
the autonomous advance removed that divergence without reintroducing any CPU I/O
or CPU-state replay.  A focused host oracle covers the autonomous timer-start
preemption boundary directly.

CPU Z80/YM/audio are still authoritative in this subphase.  The CPU executes its
normal Z80 slice and produces the real audio output, while the ME executes the same
slice independently.  Allegrex captures CZ80 state, bank offsets and Z80 RAM hash
only at the existing **300-frame checkpoint** and submits a `Z80_CHECKPOINT` oracle.
The ME validates that checkpoint after all prior autonomous advances have completed.
The main-visible sound status snapshot is then compared at the same boundary.  This
reduces the production oracle from per-I/O/per-slice state replay to one bounded
checkpoint every 300 frames.

The same deterministic real-PSP workload used for C5 was rerun from clean PSPLink
state with the autonomous path:

| game | autonomous Z80 slices | CPU I/O/IRQ oracle events | checkpoints | YM renders | sample frames | result |
| --- | ---: | ---: | ---: | ---: | ---: | --- |
| `mslug3` | 22,494 | 0 | 19/19 | 2,241 | 1,669,799 | clean through `reason=stop` |
| `wjammers` | 14,041 | 0 | 19/19 | 1,527 | 1,137,788 | clean through `reason=stop` |
| `fatfury1` | 16,795 | 0 | 19/19 | 1,739 | 1,295,752 | clean through `reason=stop` |

Across all three runs:

- `z80_io=0` and `z80_irqs=0`, proving the integrated path is no longer replaying
  CPU Z80 I/O/IRQ events;
- all 57 CZ80/bank/RAM checkpoints matched exactly;
- all 57 ME status snapshots matched `sound_code`, `pending_command`, `result_code`
  and completed emulated time;
- PCM/status comparison remained exact, with zero YM render errors or mismatches;
- worker/local failures, send failures and command/event/batch overflows remained
  zero;
- the Z80 batch ring is now used only for checkpoints and had high-water **1/4** in
  the representative runs, eliminating the previous C5 `wjammers` 3/4 pressure.

The host autonomous status/result and timer-preemption oracles pass, and PSP MVS
continues to build with the same `-Wall -Wextra -Werror` configuration used by the
hardware workload.  The standalone real-PSP C5/oracle worker harness also remains
green after introducing the dual execution modes: 4/4 lifecycles, **256/256**
shadow commands, 2 replay slices / 7 I/O events in the final lifecycle, and zero
state/RAM/bank/I/O/protocol failures.

This closes **autonomous shadow scheduling**, not C6 ownership transfer.  External
MVS timer-overflow scheduling is still driven by Allegrex, the CPU Z80/YM path still
executes authoritatively, and ME-generated PCM is still only an oracle.  Those are
intentional safety boundaries for the next C6 step.

#### C6 readiness: ME-local YM timer overflow scheduling (2026-10-04) [host/build complete]

The next dry-run removes the explicit Allegrex `YM_TIMER` overflow stream from the
integrated autonomous path.  Timer A/B are now represented inside the ME worker as
local scheduler state:

- the YM timer callback records start/stop/reload state independently for each
  channel;
- Allegrex supplies only the semantic elapsed microseconds for each Z80 scheduler
  slice, in addition to the already-required cycle budget and `timer_left` value;
- a timer armed part-way through a Z80 slice discounts only the elapsed time after
  that exact arm point;
- a timer that reaches zero is expired locally before the next ME-visible sound,
  Z80, checkpoint, sync or PCM-observation boundary;
- `YM2610ContextTimerOver()` performs the real ME-side overflow, IRQ transition and
  timer reload.  The CPU `timer_callback_2610()` still runs for the authoritative
  CPU oracle, but in autonomous mode it no longer sends an overflow command to the
  ME worker.

This preserves the MVS scheduler's existing slice boundaries while removing the
timer-overflow replay dependency.  It is deliberately one step short of a fully
ME-owned scheduler: Allegrex still determines when a Z80 scheduler slice begins and
how much emulated time that slice consumes.

Focused host oracles now cover:

- Timer A start, local overflow, IRQ assertion and automatic reload;
- a second Timer A overflow after the reload, with no Allegrex timer event;
- Timer B start, stop, restart and later local overflow;
- the existing timer-start preemption boundary (`duration < timer_left`);
- a PCM render-preparation boundary where an already-due timer must be expired
  before the ME YM state is observed.

The post-rebase regression matrix is clean: Desktop MVS is **31/31 CTest green**,
Desktop NCDZ builds, and PSP MVS builds in CPU-only, ADPCM-A-only ME and full
sound-coprocessor configurations; the standalone PSP hardware harness also builds
with `-Wall -Wextra -Werror`.

Real-PSP execution of this exact timer-autonomy build is currently blocked by the
test environment rather than by the worker: `meSafeTaskMistInit()` returns `-4`
before worker startup, and an older previously validated C5 hardware PRX reproduces
the same `init=-4` result in the same PSPLink session.  Therefore no new gameplay
hardware result is claimed for this subphase yet.

Per the current development decision, the earlier physical C2 sleep / post-wake
`RESUME_COMPLETE` rebootstrap check remains **deferred** and does not block further
C6 implementation work.  It should still be revisited before final lifecycle/release
validation, but C6 development must not wait on that physical power-cycle oracle.

No production-default ownership transfer has happened yet.  The CPU path remains
authoritative while the remaining C6 ownership pieces are implemented and compared.

#### C6 readiness: validated ME PCM presentation dry-run (2026-10-04) [host/build complete]

The audio callback now exercises the first output-ownership step without removing
the CPU oracle.  Allegrex still renders the authoritative YM2610 period first, but
after the ME render completes the worker compares:

- left/right 32-bit PCM sample-for-sample;
- YM status B;
- generation/token/sample-count protocol metadata.

Only when that comparison is exact does Allegrex copy the already-produced ME PCM
over the callback's `stream_buffer`.  The normal `clip_stream()` / `resample_stream()`
path therefore consumes ME-produced samples for that validated period.  On timeout,
protocol failure, PCM mismatch or status mismatch no presentation copy occurs, so
the CPU-rendered buffer remains intact and the existing fail-closed worker fallback
continues to apply.

The validation-only worker API remains available for standalone/oracle tests.  The
integrated PSP producer uses a separate explicit `finish_present` path so ownership
intent is visible at the call site.  New counters record the number of ME-rendered
periods/samples that actually crossed this presentation boundary rather than merely
passing the shadow oracle.

The host oracle covers both directions explicitly:

- a matching ME/CPU render copies ME PCM into a separate sentinel output buffer and
  increments the presentation counters;
- a deliberately corrupted CPU oracle sample makes the validation fail, increments
  the PCM mismatch counter and leaves the sentinel output buffer byte-for-byte
  untouched.

Regression coverage after this change remains clean: Desktop MVS is **31/31 CTest
green**, Desktop NCDZ builds, and PSP MVS builds in CPU-only, ADPCM-A-only ME and
full sound-coprocessor configurations; the standalone PSP worker harness also
builds successfully.

Real-PSP execution is not claimed for this exact presentation build yet because the
current PSPLink/MIST environment still fails before worker startup with
`meSafeTaskMistInit() == -4`.  This is the same environment-level failure observed
before this change and is outside the PCM presentation path.  Per the current
development decision, neither this temporary MIST state nor the deferred physical
sleep/resume oracle blocks further C6 implementation.

This is still a dry-run rather than full sound ownership: CPU YM2610 execution is
required to produce the comparison oracle, and Allegrex still owns scheduler slice
boundaries and the final 16-bit PSP audio submission.  The next ownership step can
remove CPU YM/Z80 execution only after the remaining ME status/scheduler handoff is
able to fail back deterministically.

#### C6 readiness: non-blocking ME status/result presentation dry-run (2026-10-04) [host/build complete]

The M68000-visible `$320001` sound status read now has a validated ME presentation
path without adding a blocking synchronization point to status polling.  The CPU
`sound_code` / `pending_command` / `result_code` values remain the oracle, but the
read path may return `pending/result` from the ME-owned 64-byte status cache line
when all of the following are true:

- the sound worker and autonomous Z80 path are active;
- the producer can acquire the worker lock with `sceKernelTryLockLwMutex()`;
- the snapshot belongs to the current worker generation;
- its completed emulated time has reached the latest Z80/sound-command boundary
  submitted to the worker;
- `sound_code`, `pending_command` and `result_code` exactly match the CPU oracle.

If the worker lock is busy, the snapshot is stale/unavailable, or a sound command
has set CPU `pending_command` before the corresponding ME command has been
published, the read immediately falls back to the CPU values with no wait.  A
fresh snapshot that disagrees with the CPU oracle is treated as a real divergence:
the autonomous shadow path is disabled through the existing fail-closed mechanism.

The pending-command edge needs explicit treatment because the M68000 sets
`pending_command=1` immediately, while the actual sound-latch callback that sends
the ME command may occur later at the scheduler boundary.  The producer therefore
marks ME status presentation dirty at the M68000 write and clears that condition
only after the timestamped ME command has been successfully enqueued.  Z80 slice
completion similarly advances the minimum required ME status time, so a previous
snapshot cannot be presented after CPU Z80 has consumed a command or written a new
result.

The worker exposes a small deterministic status-validation helper used by the PSP
producer and host tests.  Host coverage verifies exact-match, stale, fresh-mismatch
and wrong-generation cases.  Runtime logging now distinguishes ME-presented reads
from busy/stale CPU fallbacks, allowing later hardware runs to measure how often
the non-blocking path can actually serve `$320001` without a barrier.

Regression coverage remains clean: Desktop MVS is **31/31 CTest green**, Desktop
NCDZ builds, and PSP MVS builds in CPU-only, ADPCM-A-only ME and full
sound-coprocessor configurations, including the standalone hardware harness.
Real-PSP execution is still blocked before worker startup by the existing
`meSafeTaskMistInit() == -4` environment state, so no new physical-runtime result is
claimed for this exact status-presentation build.

This is still not authoritative status ownership.  CPU communication state remains
live and is required for validation/fallback; the next C6 steps must make command
delivery, scheduler progress and failure recovery sufficient to remove that oracle
without turning M68000 status reads into synchronous ME barriers.

#### C6 readiness: ME-derived Z80 slice elapsed time (2026-10-04) [host/build complete]

The autonomous `Z80_ADVANCE` protocol no longer carries Allegrex's measured
`elapsed_us`.  Allegrex now sends only the requested Z80 cycle budget, the semantic
MVS scheduler `timer_left` value and the completed emulated timestamp.  The ME
derives the amount of scheduler time consumed by the slice locally.

This cannot use the raw `Cz80_Exec()` return value.  The focused preemption oracle
demonstrates why: a 200-cycle slice preempted by a YM timer at the same boundary as
MVS returns **211 cycles** from CZ80 after the current instruction completes, while
the MVS scheduler advances only **24 us / 96 cycles**.  The production scheduler
computes that boundary from the still-live `ICount` inside `timer_adjust()` before
forcing it to zero.

The ME now mirrors that exact rule:

- a normal slice consumes `requested_cycles / 4` microseconds, matching the fixed
  4 MHz MVS Z80 scheduler even if CZ80 internally overshoots the cycle budget to
  finish an instruction;
- when a YM timer start preempts the active slice, the ME timer callback captures
  `(requested_cycles - ICount) / 4` **before** forcing its CZ80 `ICount` to zero;
- that locally derived elapsed value is then used to age the ME-owned Timer A/B
  deadlines, so timer scheduling no longer depends on a CPU-measured slice duration.

The status cache line publishes the last locally derived slice elapsed value for
oracle/debug coverage.  Host tests assert both sides explicitly: a 22-cycle normal
slice reports **5 us**, while the Timer-A preemption workload reports **24 us**.
The autonomous Timer A reload and Timer B stop/restart tests continue to pass after
removing the elapsed-time field from the command protocol.

Regression coverage remains clean after this protocol reduction: Desktop MVS is
**31/31 CTest green**, Desktop NCDZ builds, and PSP MVS builds in CPU-only,
ADPCM-A-only ME and full sound-coprocessor configurations, including the standalone
hardware harness.

This removes another CPU oracle from each Z80 slice, but Allegrex still owns the
outer scheduler boundaries and supplies `timer_left`.  Full C6 ownership still
requires replacing those boundaries with ME-driven progress/deadlines before CPU
Z80/YM execution can be stopped.

#### C6 readiness: pre-dispatch Z80 scheduler horizon (2026-10-04) [host/build complete]

The integrated autonomous path no longer waits for the authoritative CPU Z80 to
finish before deciding how far the ME should execute.  Immediately before each
CPU Z80 slice, the MVS scheduler now publishes only information it already owns:

- the slice **horizon time**, computed as current scheduler time + `timer_ticks`;
- the semantic outer `timer_left` value used by `timer_adjust()`.

The worker derives the original Z80 cycle budget from its own `z80_time` and the
horizon (`delta_us * 4` for the fixed 4 MHz MVS Z80).  If a local YM timer start
preempts that execution, the ME advances its private `z80_time` only by the
locally-derived elapsed time rather than pretending it reached the horizon.  The
next scheduler iteration then supplies the next horizon.  `z80_time` is separate
from the worker protocol's `progress->emulated_time`, so timestamped sound commands,
PCM work or sync messages cannot accidentally make the Z80 skip execution time.

A post-CPU **timestamp-only** prototype was deliberately rejected by a focused
oracle.  With a 200-cycle CPU slice, Timer A programmed for a 288 us deadline and
an outer `timer_left` of 1000 us, MVS preempts the Z80 at 24 us even though that
timer expires beyond the short executed interval.  Replaying only the final 24 us
timestamp reached the same PC but diverged in CZ80 state because the core's
preemption path still observes the original requested cycle budget.  Publishing
the pre-dispatch horizon + `timer_left` reproduces the authoritative state exactly
without using any CPU Z80 result.

Host coverage now checks all of the relevant edges:

- Timer A preemption/reload across consecutive scheduler horizons;
- a timestamped sound command that advances protocol time while the independent
  Z80 clock still executes the full interval owed to the next horizon;
- the long-timer preemption case above, including a full CZ80/bank/RAM checkpoint.

The regression matrix remains clean: Desktop MVS is **31/31 CTest green**, Desktop
NCDZ builds, and PSP MVS builds in CPU-only, ADPCM-A-only ME and full
sound-coprocessor configurations, including the standalone hardware harness.
The current PSPLink/MIST session now runs the standalone worker cleanly again;
integrated gameplay evidence for later C6 subphases is recorded separately rather
than retroactively changing the gate of this scheduler-horizon step.

CPU Z80 execution is still retained as the bounded checkpoint oracle in this
subphase.  The important ownership change is that the ME's requested work is now
known **before** CPU Z80 execution and is derived entirely from scheduler state;
this makes a later CPU-Z80 skip possible without inventing a replacement cycle
budget from CPU results.

#### C6 readiness: FIFO status fence / bounded status barrier (2026-10-04) [host + standalone hardware complete]

The M68000-visible sound status path now has an explicit FIFO barrier for the cases
where the lock-free ME status snapshot is not immediately usable.  A new `FENCE`
worker command carries **no synthetic timestamp**.  When the ME reaches it in the
command ring it:

- applies any YM timer overflow that was already due from previously processed
  scheduler horizons;
- republishes the current sound status snapshot;
- returns `FENCE_ACK` with the worker's existing `emulated_time` unchanged.

That ACK therefore proves only that every command queued **before the fence** has
been observed.  It never moves Z80/YM time merely to satisfy a main-CPU read.

The normal `$320001` read keeps the non-blocking fast path: if the current ME
snapshot already matches the still-authoritative CPU state it is presented
immediately.  Any other validation result -- including an equal-timestamp value
mismatch, which can simply mean that a sound command is still ahead in the FIFO --
gets a bounded fence attempt.  The M68000 thread spins for at most **250 us**; if
the worker mutex is busy, a render is in flight, or the fence does not complete
inside that budget, the read falls back to the CPU-owned status values.

A timed-out fence is intentionally left in flight.  A later status read can finish
that same fence instead of injecting another barrier.  `FENCE_ACK.emulated_time`
is used as the coverage marker: if that old fence predates the current
`required_time`, the producer queues a fresh fence (within the same bounded wait)
rather than falsely declaring divergence.  Once a completed fence covers the
required time, any remaining stale/unavailable/mismatched state is a real oracle
failure and the ME shadow path fails closed back to CPU authority.

Host coverage now proves:

- a fence after a timestamped sound command observes that command without changing
  the emulated timestamp or synthetic Z80 elapsed time;
- an equal-timestamp pre-command snapshot is correctly classified as a mismatch,
  then resolves to an exact match after the FIFO fence;
- a fence queued **before** a later scheduler horizon reports the older covered
  time, and a subsequent fence observes the horizon;
- Timer A/B local scheduling and existing PCM/Z80/status oracles remain green.

The exact standalone PSP build also executes the fence on the physical ME in every
lifecycle.  The final real-hardware record is clean: **4/4 cycles**, `init=0`,
**268 commands** in the final lifecycle (one additional fence versus the previous
harness), `fatal=0`, zero command/event overflow, zero Z80/YM mismatches, and
`emulated_time=19000` before and after the fence/sync sequence.  The harness still
contains its historical synthetic in-process lifecycle exercise; no physical
sleep/resume claim is made or required for this subphase.

The final regression matrix is clean: Desktop MVS **31/31 CTest**, Desktop NCDZ,
PSP CPU-only, PSP ADPCM-A-only ME, and PSP full sound-coprocessor + hardware-harness
builds all pass.  A scripted `mslug3` launch on the current PSPLink session produced
normal CPU audio-profile windows but no ME shadow log, indicating startup fallback
before the status barrier became active; that run is therefore **not** counted as
integrated barrier evidence.  The standalone MIST worker immediately before/after
that attempt is healthy, so the fallback is tracked as an integration/bootstrap
environment issue rather than as a fence protocol failure.

CPU status remains the oracle and fallback in this dry-run.  The fence/barrier is
the ordering primitive needed before ME status can become authoritative; it does
not itself transfer ownership.

#### C6 readiness: enqueue-first sound command ownership dry-run (2026-10-04) [host/build complete]

The normal MVS sound-command path now exercises the next ownership boundary while
keeping the CPU Z80/YM implementation fully authoritative.  `neogeo_sound_write()`
publishes the timestamped command to the ME worker **before** updating the CPU-owned
`sound_code` latch and pulsing the CPU Z80 NMI.  The CPU write/NMI still runs
unconditionally immediately afterwards, so it remains both the oracle and the
fallback for gameplay.

This ordering means the ME command ring is now the primary ordered command queue in
the experimental path rather than a post-hoc copy of an already-applied CPU event.
The FIFO fence from the previous subphase supplies the bounded observation barrier:
once a fence completes, all earlier same-timestamp command messages have been
consumed in ring order before status is presented.

The transition is fail-closed.  If an active coprocessor path cannot acquire the
worker lock or cannot enqueue/poll the sound command, the experimental Z80/sound
path is marked failed and falls back to CPU authority; the CPU latch/NMI is still
applied.  When the ME path is unavailable or disabled, the same CPU behavior is
preserved without making MIST mandatory.

Host coverage now includes both sustained queue pressure and the equal-time FIFO
case needed for command ownership:

- the existing **10,048-message** shadow-command stress still completes with exact
  ordering, no corruption and no lost expectations;
- four commands with the **same emulated timestamp** (`0x77`, `0x88`, duplicate
  `0x88`, `0x99`) are enqueued before a fence, and the post-fence status resolves to
  the final FIFO value `0x99`;
- duplicate values are therefore not treated as an ordering shortcut, and equal
  timestamps do not collapse distinct command events;
- all existing autonomous Z80, local YM timer, PCM-presentation and status-fence
  oracles remain green.

The regression matrix after this dry-run is clean: Desktop MVS is **31/31 CTest
green**, Desktop NCDZ builds, and PSP MVS builds in CPU-only, ADPCM-A-only ME and
full sound-coprocessor configurations, including the hardware harness.

Current real-PSP integration evidence is explicitly **not counted as passing** for
this subphase.  The current PSPLink/MIST session runs `mslug3` with
`AudioProcessor=2`, but produces normal CPU audio/profile windows with no ME shadow
log and `me_wait_n=0`, which means startup fell back before the enqueue-first path
became active.  This is the same environment/bootstrap limitation seen while
testing surrounding C6 work, not an observed command-ordering mismatch.

This remains a dry-run: Allegrex still applies the authoritative sound latch/NMI,
executes the CPU Z80/YM oracle, and can recover immediately if ME enqueue fails.
The next ownership step can stop mirroring command application into the CPU only
after the ME command/status path is active in integrated hardware and the bounded
fallback semantics are proven there.

#### C6 readiness: ME-first M68000 status ownership dry-run (2026-10-04) [host/build complete]

The `$320001` M68000-visible status read now has an explicit ownership boundary.
`neogeo_timer_r()` no longer initializes the visible pending/result bytes from the
CPU globals before consulting the coprocessor.  When the ME status path succeeds,
the values consumed by the M68000 are written only from the validated ME snapshot.
The CPU `pending_command` / `result_code` values are read only in the explicit
fallback branch.

The worker-side presentation helper is deliberately fail-closed.  It first applies
the existing generation/time/value validation against the still-live CPU oracle and
writes the caller's output bytes only for `PSP_ME_SOUND_STATUS_MATCH`.  `STALE`,
`MISMATCH` and `UNAVAILABLE` leave the caller's outputs untouched.  The producer
therefore retains the previous bounded FIFO-fence behavior without allowing stale
or mismatched ME state to leak into the M68000-visible register.

This makes the successful read path ME-owned while preserving the current safety
model:

- an immediately valid snapshot is presented directly from the ME;
- an equal-time command race can use the existing bounded **250 us** FIFO fence and
  then present the newly covered ME snapshot;
- a busy worker, stale fence, unavailable snapshot or fence timeout returns false
  and `neogeo_timer_r()` explicitly falls back to the CPU values;
- a completed fence that proves coverage but still disagrees with the CPU oracle is
  treated as a real divergence, disables the experimental sound path, and falls
  back to CPU authority.

Host coverage now verifies the ownership property directly.  On an exact snapshot
match, the presentation helper replaces sentinel output bytes with the ME
`pending_command` / `result_code`.  Repeating the call with a future required time
or deliberately mismatched result returns `STALE` / `MISMATCH` and leaves those
sentinels unchanged.  The existing fence oracle continues to cover same-timestamp
FIFO ordering, old-fence coverage and bounded recovery.

The regression matrix is unchanged and clean: Desktop MVS **31/31 CTest**, Desktop
NCDZ, PSP CPU-only, PSP ADPCM-A-only ME and PSP full sound-coprocessor + hardware
harness builds all pass.

The current real-PSP integration environment still does not provide evidence for
this ownership boundary.  A short run using the exact current PRX again loaded MVS
but produced no ME shadow log, so the ME status path never became active.  This run
is therefore recorded as startup/bootstrap fallback, not as either passing or
failing status-ownership evidence.

CPU communication state remains alive as the oracle and restart fallback in this
subphase, and CPU Z80/YM execution is still retained.  What changes is the source
of the value visible to the M68000 on the successful experimental path: it is now
unambiguously the ME status snapshot rather than a CPU value optionally overwritten
after the fact.

#### C6 readiness: ME -> CPU recovery snapshot foundation (2026-10-04) [host/build complete]

Before Allegrex Z80/YM execution can be skipped safely, the CPU fallback must be
able to reconstruct the complete sound island from the ME rather than merely read
the small M68000-visible status snapshot.  The worker therefore now publishes a
separate coherent recovery payload at an explicit FIFO command boundary.

The recovery snapshot contains the logical CZ80 state, Z80 bank offsets,
communication latch/result state, YM IRQ state, ME Z80 time and both local YM timer
remaining/enabled values.  The existing shared Z80 image supplies the 2 KiB visible
RAM, and the shared ME YM2610 context is cloned into an Allegrex-owned aligned
context after the recovery command ACK.  The reader rejects snapshots that are not
from the active generation, are not autonomous, retain an in-slice timer arm offset,
or disagree between the published YM IRQ and CZ80 IRQ state.

The CPU-side restore pieces are also in place without importing ME-local pointers:

- CZ80 registers/IRQ state, bank mappings, visible RAM and sound command/result
  globals can be restored from the recovery payload;
- bank offsets are range-checked before any CPU state is mutated, including an
  explicit overflow guard for the ROM-base addition;
- YM2610 semantic state can be restored from a window-backed ME context while
  preserving the CPU context's callbacks, PCM/cache ownership and native handler
  pointers;
- the CPU YM timers are disabled/re-armed from the published remaining durations,
  so fallback does not restart a timer from its full original period.

Host coverage now exercises the round trip rather than only snapshot readability.
The worker runs an autonomous Z80 program that modifies both visible RAM and a
non-zero SSG register, exports recovery state, then the test deliberately corrupts
CPU CZ80/RAM/bank/communication/YM state and reconstructs it solely from that
payload.  The restored logical CPU state, RAM byte, banks, communication values and
YM register all match the ME snapshot.  The independent YM context oracle also
proves that native CPU timer callback bindings survive semantic restore.

The full regression matrix is clean after this foundation: Desktop MVS is
**31/31 CTest green**, Desktop NCDZ builds, and PSP MVS builds in CPU-only,
ADPCM-A-only ME and full sound-coprocessor configurations.  The PSP hardware
harness now contains an additional autonomous recovery-snapshot cycle so that its
cache-line/ABI path is ready for physical validation.

Physical execution is currently **environment-blocked rather than failed**.  From
a clean PSPLink reset, the rebuilt recovery harness exits before starting the
worker because `meSafeTaskMistInit()` returns `-4`; its log reports generation 0,
commands 0 and no worker fatal.  This is the same recurring MIST bootstrap state
seen in surrounding C6 testing, so no real-PSP recovery-snapshot pass is claimed
from that run.  Per the current development decision, the separate physical
sleep/resume check is also deferred and does not block continuing C6 ownership
work.

This foundation does **not** yet skip the CPU sound island.  The next ownership
subphase may only suppress Allegrex Z80/YM work after establishing a fence at which
the ME recovery snapshot can be consumed and the CPU oracle can be reconstructed
immediately if the ME path fails.

#### C6 ownership: ME-authoritative PCM render with hot CPU YM fallback (2026-10-04) [host/build complete]

The first actual ownership transfer is intentionally narrower than disabling the
entire Allegrex sound island.  CPU Z80 execution, YM register writes, timer
callbacks and M68000-visible fallback state remain live, but the normal successful
experimental audio callback no longer executes `YM2610Update()` on Allegrex.  The
PCM block presented to the existing clip/resample/output path is generated by the
ME worker.

The handoff is ordered under the existing YM gate:

1. Allegrex asks the ME-owned YM context for the compressed PCM window required by
   the next block and fills only those source bytes from the existing CPU-side ROM
   / cache provider;
2. the ME renders the block and writes back both the PCM job and its post-render YM
   semantic context before acknowledging completion;
3. Allegrex validates the render job metadata and status byte, then semantically
   restores the CPU YM singleton from that post-render ME context while preserving
   the CPU timer callbacks, PCM/cache ownership and native pointers;
4. only after that synchronization succeeds is the ME PCM copied into the stream
   buffer consumed by the unchanged clip/resample/output pipeline.

This keeps the CPU fallback **hot** even though it no longer pays the normal YM
render cost.  CPU Z80/control writes and external YM timers continue to mutate the
CPU context between audio blocks.  After each successful ME block the render-side
ADPCM/FM/SSG state is caught up from the ME.  If preparation, render ACK, metadata
validation or context synchronization fails, the experimental path is disabled,
the output buffer is left untouched, the YM gate is released, and Allegrex renders
that same block locally from the last synchronized CPU context.

The YM context rebinding path was hardened for this use: all source-internal DT and
pan pointers are validated **before** the destination context is overwritten.  A
bad ME/window context therefore cannot partially corrupt the CPU fallback context.

Host coverage proves both ownership and failback continuity.  Two independent YM
instances begin from identical non-trivial ADPCM state.  The first block is rendered
only by the ME; the authoritative finish copies that PCM to the presented output and
synchronizes the default CPU YM.  A second ME block is then completed with an
intentionally invalid requested sample count.  The authoritative finish rejects it,
leaves sentinel output samples untouched, and the CPU immediately renders that same
block bit-exactly against the independent reference.  Statistics distinguish
`ym_authoritative_renders` from earlier validation-only presentations and count
semantic context-sync failures separately.

The regression matrix remains clean: Desktop MVS is **31/31 CTest green**, Desktop
NCDZ builds, and PSP MVS builds in CPU-only, ADPCM-A-only ME and full
sound-coprocessor configurations including the standalone hardware harness.

Real-PSP execution is again **environment-blocked before this code runs**.  The
current harness built from this exact tree loads after a clean PSPLink reset, but
`meSafeTaskMistInit()` returns `-4`; the resulting log has generation 0 and
`commands=0`.  No real-hardware PCM-ownership pass or failure is therefore claimed
from that run.  The physical sleep/resume check remains explicitly deferred by the
current development decision.

This subphase transfers only **PCM/render authority**.  The CPU Z80 and YM control /
timer side are deliberately retained so fallback requires no recovery snapshot in
the audio callback.  The next C6 step can use the recovery foundation above to
remove CPU Z80/control execution at a separately fenced boundary.

#### C6 ownership: ME-authoritative Z80/control with synchronous CPU recovery (2026-10-04) [host/build complete]

The next ownership step now removes the normal Allegrex Z80/control execution while
the experimental worker is healthy.  A successful initial Z80/YM snapshot marks the
ME sound island authoritative.  At each MVS scheduler slice, Allegrex queues the
semantic horizon to the ME **before** executing Z80; once that enqueue succeeds, the
CPU CZ80 slice is skipped and the scheduler advances its normal emulated-time
accounting as if the slice had completed.  CPU YM Timer A/B callbacks are likewise
excluded from scheduler deadline selection while ME control is authoritative, so
timer overflow/IRQ evolution now comes only from the ME-owned YM context.

The main-visible communication path follows the same ownership model.  Sound
commands are enqueued to the ME first; the CPU latch value remains warm for recovery,
but the stale CPU CZ80 does not receive an NMI while its execution is suppressed.
M68000-visible pending/result reads consume the generation/time-fenced ME status
snapshot directly instead of comparing it to CPU communication state.  The 300-frame
diagnostic boundary now checks only that ME `emulated_time` and `z80_time` reached the
expected scheduler time; it no longer submits CPU CZ80/RAM/bank oracle checkpoints
while ME owns control.

Failback is deliberately synchronous and fail-closed.  Any recoverable command,
horizon, status, PCM, lock or worker failure marks CPU recovery required while keeping
CPU CZ80 and CPU YM timers suppressed.  At the next scheduler boundary Allegrex:

1. acquires the YM gate and worker lock so no render/control mutation can race the
   transfer;
2. requests a FIFO recovery snapshot containing logical CZ80 state, banks,
   communication state, IRQ state and ME-local timer deadlines, and copies the ME
   Z80 RAM image;
3. restores CPU CZ80/RAM/bank/latch state, restores the CPU YM singleton from the
   ME semantic context while preserving CPU-native pointers/callbacks, and rearms
   CPU YM timers from the **remaining** durations rather than their original periods;
4. replays a sound command/NMI if the command that triggered failback could not be
   enqueued to the ME;
5. only then clears ME authority and lets subsequent scheduler slices execute on
   Allegrex again.

If recovery itself cannot complete, CPU execution stays suppressed rather than
continuing from stale state.  Likewise, if an authoritative ME PCM render fails
before recovery, the audio callback emits silence for that block instead of rendering
from a stale CPU YM context; the scheduler then performs the exact restore before CPU
audio/control can resume.

Host coverage exercises the pieces needed by this transfer.  The autonomous horizon
oracle now consumes the complete requested scheduler horizon across internal YM timer
preemption boundaries, authoritative status presentation accepts only the active
generation and required time without consulting CPU values, and the recovery-snapshot
test supports metadata-only reads as used by production failback.  Existing recovery
coverage still proves reconstruction of CZ80/RAM/banks/latch/YM/timer state from ME
state after deliberately corrupting the CPU side.

The regression matrix is clean for this ownership step: Desktop MVS is **31/31 CTest
green**, Desktop NCDZ builds, and PSP MVS builds successfully in CPU-only,
ADPCM-A-only ME and full sound-coprocessor configurations; the standalone PSP worker
harness also compiles from the same tree.

Real-PSP execution is currently **environment-blocked before the worker starts**.
The rebuilt harness loads under PSPLink, but `meSafeTaskMistInit()` returns `-4` with
generation 0 and `commands=0`, the same bootstrap failure previously reproduced by
known-good C5 binaries.  No physical pass or failure of this ownership code is
therefore claimed from that run.  Per the current development decision, physical
sleep/resume validation remains deferred and does not block further C6 work.

- switch normal sound commands to the shared event ring;
- stop executing authoritative Z80/YM2610 on Allegrex;
- consume ME status/result snapshots at explicit synchronization points;
- consume ME-generated PCM through the Allegrex PSP audio output path;
- retain a restart-to-CPU fallback if protocol/ME failure occurs.

**Gate:** `mslug3` and a broader MVS test set run correctly with no sound or
emulation regressions.

### C7 - scheduler/barrier optimization

#### C7.1 - remove authoritative command echoes and diagnostic syncs (2026-10-04) [host/build complete]

The first C7 pass removes two pieces of synchronization that were useful while
the ME was only an oracle but are redundant after C6 ownership transfer.

Authoritative sound commands now reuse the existing fixed-size command message with
a `NO_ECHO` flag.  The worker still applies the latch/NMI and publishes status in
FIFO order, but it does not push a `SHADOW_SOUND_ECHO` event and Allegrex does not
allocate an expected-echo slot or set the per-frame pending-echo hint.  Legacy C3-C5
oracle paths continue to use the original echo protocol unchanged.  A host oracle
queues an authoritative no-echo command, then issues a later FIFO fence and verifies
that the command is visible in the ME status snapshot with zero sent/matched/pending
shadow-echo counters.  The fence succeeding also proves that dropping the echo does
not weaken command ordering.

The 300-frame diagnostic window no longer sends a blocking `SYNC` while Z80/control
is ME-authoritative.  Correctness ordering is already provided by explicit status
fences, render prepare/finish barriers and recovery fences at the points where state
is consumed.  The periodic window now only samples the shared status snapshot if it
is already available; it cannot stall the scheduler merely to satisfy diagnostics.
The older shadow/autonomous-oracle path retains its `SYNC` + checkpoint behavior.

This pass does not change the 32-byte protocol message ABI and does not alter ring
capacity.  The complete regression matrix remains clean: Desktop MVS is **31/31
CTest green**, Desktop NCDZ builds, and PSP MVS builds in CPU-only, ADPCM-A-only ME
and full sound-coprocessor configurations including the standalone hardware harness.
Real-PSP execution is still blocked by the previously documented MIST `init=-4`
environment state, so no new physical performance claim is made for this batch.

#### C7.2 - stop hot-syncing the CPU YM context under full ME control (2026-10-04) [host/build complete]

The second C7 pass removes the largest remaining cache/state copy from the normal
authoritative audio callback.  The previous C6 PCM ownership step restored the full
ME YM2610 semantic context into the CPU singleton after every successful block so a
CPU render fallback could be used immediately.  Once Z80/control/timers are also
ME-authoritative, that hot CPU context is stale again as soon as the next ME-side
write occurs, and C6 already has an explicit recovery-snapshot fence for exact
failback.  Paying the restore on every audio block is therefore redundant.

`psp_me_sound_worker_ym_render_finish_authoritative()` now makes CPU-context
synchronization explicit.  PCM-only ownership passes `sync_cpu_context=true` and
keeps the original behavior.  Full-control production passes `false`, so successful
render completion validates only the render job metadata, copies the ME PCM into the
existing stream buffer and leaves the CPU YM singleton untouched.  In this mode it
also avoids invalidating the shared ME YM context cache range on every block.  If the
ME path later fails, the already-established recovery command invalidates/fences the
ME context and restores the complete CPU YM state at the scheduler boundary before
CPU control resumes.

Host coverage makes the distinction observable.  The existing authoritative PCM
test still uses hot-sync mode and proves immediate CPU fallback continuity.  A new
full-control oracle renders two deliberately different consecutive blocks: after a
successful ME block with synchronization disabled, a CPU render must still reproduce
**block 1** from its pre-render state; if an accidental per-block restore occurred it
would instead produce block 2.  This test passes, proving the normal full-control
path no longer clones ME YM state into CPU every period.

The regression matrix remains clean: Desktop MVS is **31/31 CTest green**, Desktop
NCDZ builds, and PSP MVS builds in CPU-only, ADPCM-A-only ME and full
sound-coprocessor configurations including the hardware harness.  No new physical
runtime claim is made while the separate MIST `init=-4` bootstrap issue persists.

- measure actual barriers and wait time;
- reduce unnecessary end-of-slice synchronization while preserving timestamp
  semantics;
- coalesce `ADVANCE_TO_TIME` events where safe;
- tune ring sizes from observed high-water marks;
- optimize cache-maintenance granularity without violating ownership.

**Gate:** measurable whole-emulator improvement over the current ADPCM-A-only ME
path, not merely over Main CPU.

### C8 - lifecycle/save-state hardening

#### C8.1 - save/load round-trip through the existing CPU state format (2026-10-04) [host/build complete]

Save states continue to use the existing NJEMU MVS file format; no ME-specific state
chunk or version bump is introduced.  The normal format already serializes CZ80,
visible Z80 RAM, bank/latch/result state, MVS scheduler timers and the complete CPU
YM2610 semantic state.  C8 therefore materializes the authoritative sound island back
into those existing CPU owners before state I/O, then reseeds the ME afterward.

The MVS save/load menu now wraps `state_save()` / `state_load()` with two lifecycle
hooks while audio is already muted by the menu:

1. `neogeo_sound_state_prepare()` synchronously consumes the ME recovery snapshot and
   reconstructs CPU CZ80/RAM/banks/latch/YM/timers when ME owns sound;
2. the unchanged state serializer saves or loads the normal CPU-side format;
3. `neogeo_sound_state_resume()` snapshots the resulting CPU state back into the ME
   only if ME was authoritative when the state operation began.

That last condition is important: saving or loading while the emulator is already in
CPU fallback must not accidentally reactivate a worker that previously failed.  If
post-state ME reseeding itself fails, the newly saved/loaded CPU state remains valid
and execution stays on the CPU path.

Production Z80 snapshots now also carry the CPU scheduler's Timer A/B enabled state
and **remaining** durations.  The worker seeds its local YM deadlines from those
values when the snapshot is applied, so a save/load round trip does not restart an
active YM timer from its full period.  The original snapshot API remains as a
zero-timer wrapper for the C3-C7 host/hardware oracles.  A focused host oracle seeds
an already-active Timer A with 10 us remaining and proves the ME generates its
overflow at that inherited deadline without the Z80 reprogramming the timer.

Reset continues to use the same CPU-to-ME snapshot path after `timer_reset()` and
`sound_reset()`, so it now benefits from the same timer-state representation without
special reset-only protocol.

Validation is clean: the normal Desktop MVS suite remains **31/31 CTest green**,
Desktop NCDZ builds, and PSP MVS builds in CPU-only, ADPCM-A-only ME and full
sound-coprocessor configurations.  Dedicated `SAVE_STATE=ON` builds also succeed for
Desktop MVS and PSP full sound-coprocessor + hardware harness.  Building *all* Desktop
test executables with `SAVE_STATE=ON` still exposes a pre-existing test-link issue
(`ym2610_context_tests` / worker tests do not provide the global `state_buffer` used by
the save-state-only YM functions); the actual MVS executable builds successfully and
that unrelated test-harness issue is not part of this lifecycle change.

- save/load state with ME-owned sound state;
- game/browser/game transitions;
- AudioProcessor mode changes;
- reset;
- PSP suspend/resume;
- failure/restart fallback;
- repeated in-process initialization.

**Gate:** all existing M6 lifecycle coverage plus save/load-state coverage passes
on real hardware.

#### C8.2 - reset / restart lifecycle reconciliation (2026-10-04) [host/build complete]

The producer lifecycle now explicitly reconciles the requested `AudioProcessor`
mode with the worker that is actually alive at each sound reset.  Previously the
`Main CPU` reset branch only cleared `me_available`; if a worker was still running
because of an unusual runtime transition, the ME task could survive even though the
frontend had already selected CPU ownership.  Likewise, an inconsistent
`me_available == false` / `worker.running == true` state could make a later ME
bootstrap fail without first retiring the stale generation.

The reset policy is now a small platform-independent state machine with explicit
actions for keep-CPU, stop, reset-generation, start, stale-worker restart and
suspend-deferred cases.  The normal UI path still uses `LOOP_RESTART` when the
`AudioProcessor` setting changes, so the common case continues to tear down the
whole sound producer between games.  The explicit policy hardens direct/reset-time
transitions as well instead of relying on that UI behavior.

Generation reset and worker stop now also obey the same lock hierarchy as YM
render/control work: **YM gate -> worker mutex**.  This prevents `sound_reset()` on
the main thread from advancing the worker generation while the audio thread is
finishing a render from the previous generation.  Worker stop waits behind any
current YM operation before issuing `SHUTDOWN`, so game exit, browser return and
stale-worker cleanup cannot free shared state that is still being observed by the
audio path.

Lifecycle-local tracking is cleared at the same boundary.  In particular the
save/load `resume ME after state I/O` latch is discarded on reset/stop so an
exceptional state-operation sequence cannot request a later ME reseed after the
generation that created the request has already ended.  The legacy shadow failure
log latch is also reset per generation so a new lifecycle can report its own first
failure independently.

Host coverage now checks both policy and concrete worker reuse:

- all reset-policy combinations cover Main CPU, active ME, suspended ME and the
  inconsistent stale-worker cases;
- a worker is reset from generation 1 to generation 2 while a FIFO fence is still
  outstanding, proving the reset drains the earlier event and clears in-flight
  fence/render state;
- that worker is then shut down, all shared allocations are verified released, and
  the **same public worker object** is bootstrapped again at generation 7, synced and
  shut down cleanly.  This models browser -> new-game reuse within one emulator
  process without relying on fresh static storage.

The regression matrix is clean after the lifecycle hardening: Desktop MVS remains
**31/31 CTest green**, Desktop NCDZ builds, PSP MVS builds in CPU-only,
ADPCM-A-only ME and full sound-coprocessor configurations, and the standalone PSP
worker harness builds.  The C8-specific `SAVE_STATE=ON` Desktop MVS executable and
PSP full sound-coprocessor + hardware harness builds also pass.

No physical sleep/resume test is part of this subphase; that check remains deferred
by the current development decision.  The rebuilt standalone lifecycle harness was
also rerun on real PSP hardware without any suspend operation and passed all four
worker lifecycles cleanly (`init=0`, `cycles=4`, no Z80/RAM/bank/I/O mismatches,
no ring overflow and `fatal=0`).  Its historical `suspend_resume=1` field refers to
the harness's synthetic stop/rebootstrap ownership cycle only; it is not evidence
for a physical PSP sleep / `RESUME_COMPLETE` callback.

#### C8.3 - fatal-worker teardown and next-game restart (2026-10-04) [host/build complete]

The next lifecycle pass covers the case where the ME worker fails during one game,
the emulator returns to the browser, and a later game starts in the **same PRX
process**.  The producer object itself is reconstructed for every sound-thread
startup, but several ownership/recovery variables are static PSP state and therefore
outlive an individual MVS game unless they are reset explicitly.

`psp_audio_producer_init()` now invokes the same complete sound-island tracking reset
used at generation/worker teardown instead of resetting only a hand-picked subset of
flags.  A new game therefore cannot inherit stale `ME authoritative`, CPU-recovery,
save/load reseed, status-dirty, command-replay or shadow-failure state from the game
that just ended.  The per-game mutex/gate flags are still initialized separately
because they describe synchronization objects rather than sound-island ownership.

Fatal worker shutdown is also bounded now.  The ME publishes `fatal_error` and exits
its command loop before Allegrex observes the failure.  Previously a later normal
`psp_me_sound_worker_shutdown()` could still enqueue `SHUTDOWN` to that already-dead
consumer and wait for the full shutdown timeout before falling back to `abort()`.
Shutdown now samples the published progress first; when a fatal is already visible it
joins the completed dispatch and frees all shared state immediately.  Producer
shutdown has an additional final abort guard before deleting its mutexes, so an
exceptional gate/mutex teardown failure cannot leave a live ME task behind while a
new browser/game lifecycle begins.

Host coverage exercises the complete failure/restart sequence.  A generation-1
worker is deliberately killed with an emulated-time regression, the normal shutdown
API observes that fatal and releases every shared allocation, then the **same worker
object and host dispatch** are bootstrapped again at generation 2, synced and shut
down with `fatal_error == NONE`.  This is in addition to C8.2's clean
reset-generation / browser-new-game rebootstrap test.

The MIST dependency was also audited before adding any restart-specific workaround.
The installed `libme-stask` exposes no MIST deinit API, and its `meSafeTaskMistInit()`
path re-selects the active ME table on each call rather than rejecting an
already-initialized library instance.  Its observed `-4` path is the unsupported /
unrecognized-table result, not an `already initialized` status.  The intermittent
real-PSP `init=-4` seen in earlier sessions therefore must not be "fixed" by inventing
a per-game deinit sequence that the dependency does not provide.

The complete regression matrix remains clean after this hardening: Desktop MVS is
**31/31 CTest green**, Desktop NCDZ builds, PSP MVS builds in CPU-only,
ADPCM-A-only ME and full sound-coprocessor configurations including the hardware
harness, and both the Desktop MVS and PSP full-coprocessor `SAVE_STATE=ON` builds
pass.  Physical sleep/resume remains intentionally deferred and is not part of this
subphase.

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

#### C9 measurement harness and final PSP comparison (2026-10-04) [complete]

The profiling output has been tightened before collecting the final three-way
comparison so a fallback or a tail-latency regression cannot be hidden by a single
average.  `psp_me_sound_profile.log` now records, for every 300-frame window:

- existing wall time and uncapped whole-emulator FPS;
- per-frame average, p50, p95, p99 and maximum wall time;
- the configured `AudioProcessor` value;
- whether the PSP ME producer is actually available at runtime;
- whether the binary contains the full sound coprocessor;
- whether the full Z80/control + YM sound island is still ME-authoritative for that
  window.

The last field is important for C9: a full-coprocessor binary that has recovered to
CPU after a worker error must not be accidentally counted as a successful ME
performance window merely because MIST itself is still available.

`psp_audio_profile.log` now writes total time as well as average/max/count for every
metric.  The existing `me_wait` metric remains the common synchronization-cost
metric across modes: the ADPCM-A reference records its MIST job wait as before, while
the full coprocessor records both the synchronous PCM-window prepare ACK and the
render-completion ACK.  `me_wait_total / buffers` can therefore be compared directly
without being distorted by the full path having two waits per audio block.  The
existing status-fence counters in `psp_me_sound_shadow.log` remain the complementary
main-thread barrier measurement.

Three Release PSP/MVS binaries have been built with both profilers enabled and the
same temporary, non-committed 5,400-frame C0 input script (`mslug3`, credit/start,
then deterministic movement/fire/jump).  The input header remains outside the repo,
so profiling support does not hardcode a benchmark workload into production code.

| mode | build | SHA-256 |
| --- | --- | --- |
| Main CPU | `/tmp/njemu-c9-script-main/MVS.prx` | `0f92a49d3054a3adfe64f36efd891234aa62d5a12c9dcae83b32d6c2a8295692` |
| ADPCM-A ME reference | `/tmp/njemu-c9-script-adpcma/MVS.prx` | `70192dbb8c0b375d9833e8e66e1de2521f14bbfada9197a1f81923f41bae14ce` |
| full ME sound coprocessor | `/tmp/njemu-c9-script-full/MVS.prx` | `a4ff4c29bc55dd23ea6c4c6bc3511ca0d6e50d54898e1eb28f4b7b6b9f94b0cb` |

The endpoint later recovered without changing any of the three frozen binaries.
All three were then run on the same real PSP with the same scripted `mslug3`
workload.  The 300-frame windows align exactly by `sound_cmd` sequence across the
three modes.  As in the original C0 comparison, windows **8-11** (`4, 5, 4, 3`
commands) are used as the representative gameplay interval so boot/attract work does
not skew the result.

| mode | gameplay FPS | delta vs Main | frame avg | p50 | p95 | p99 | worst max | Allegrex Z80 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Main CPU | **85.61** | baseline | 11.681 ms | 11.073 ms | 18.292 ms | 20.625 ms | 23.372 ms | 2.673 ms/frame |
| ADPCM-A ME | **89.90** | **+5.02%** | 11.123 ms | 10.994 ms | 16.679 ms | 19.002 ms | 21.087 ms | 2.485 ms/frame |
| full ME sound coprocessor | **103.11** | **+20.44%** | **9.698 ms** | **9.123 ms** | 17.418 ms | 19.133 ms | 21.785 ms | **0.065 ms/frame** |

The full sound coprocessor is also **+14.69%** faster than the ADPCM-A-only ME
reference in the same four gameplay windows.  Its p95/p99 tail is close to the
ADPCM-A reference while its median and average frame times are substantially lower.
The remaining Allegrex-side Z80 time is only scheduler/control overhead; the
authoritative sound CPU itself is no longer being emulated there.

The audio-thread comparison shows where the remaining optimization opportunity is:

| mode | producer avg | callback avg | ME wait / audio buffer | waits / buffer | worst single ME wait |
| --- | ---: | ---: | ---: | ---: | ---: |
| Main CPU | 6.103 ms | 5.957 ms | 0 | 0 | 0 |
| ADPCM-A ME | **4.749 ms** | **4.614 ms** | **27.5 us** | ~1 | 93 us |
| full ME sound coprocessor | 7.395 ms | 7.258 ms | **6.762 ms** | ~2 | 9.725 ms |

The full path therefore wins whole-emulator performance despite spending much more
time waiting in the audio thread: each audio block currently has a synchronous PCM
window-prepare fence plus a render-completion fence.  Those waits are now the clearest
remaining performance target.  They should be reduced by deeper pipelining / fewer
round trips rather than by moving sound emulation back to Allegrex.

The full-coprocessor run remained genuinely authoritative throughout every measured
window (`me_available=1`, `me_coprocessor=1`, `me_authoritative=1`).  Across the
complete run the shadow log reports:

- **0** fatal errors and **0** CPU recovery attempts;
- **0** Z80/RAM/bank/I/O, PCM or YM-status mismatches;
- **0** command/event/batch overflows;
- **1,674** authoritative YM renders / **1,247,320** rendered sample frames;
- **22,493** autonomous Z80 slices and **6,059** ME-owned scheduler slices;
- status fences with **0 failures**, total recorded wait **295.009 ms** and maximum
  individual wait **301 us**.

The blocking PSP audio backend does not expose a hardware underrun counter.  As the
available timing proxy, every profiled run continued producing/outputting complete
300-buffer windows; the worst observed audio-loop period was 37.439 ms (Main),
38.683 ms (ADPCM-A) and 36.890 ms (full) against the 33.378 ms nominal period.  No
audible/output failure or producer abort was observed during the scripted runs.

**C9 decision:** for PSP/MVS builds that include ME audio support, the runtime
`Media Engine` mode should use the **full ME sound coprocessor**, not the legacy
ADPCM-A-only accelerator.  Keep Main CPU as the correctness/failure fallback and
retain ADPCM-A-only as a benchmark/diagnostic build path.  The generic CMake options
remain opt-in here; changing release/preset defaults is a separate packaging change
and should not be hidden inside this profiling commit.

The final regression matrix after the profiling changes is clean: Desktop MVS is
**31/31 CTest green**, Desktop NCDZ builds, and PSP Main CPU, ADPCM-A ME and full
sound-coprocessor profile builds all compile; the full PSP standalone hardware
harness also builds successfully.

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
