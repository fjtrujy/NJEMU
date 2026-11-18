# PS2 rapid-input delivery investigation (Issue #33)

Date: 2026-11-18
Issue: https://github.com/fjtrujy/NJEMU/issues/33
Branch: `fix/ps2-rapid-input-delivery`

## Findings

The input path is PADMAN/libpad -> `ps2_input.c` ->
`sample_gamepad_index()` / `poll_gamepad_index()` ->
`cps1/inptport.c:update_inputport()` -> active-low CPS1 registers.
`cps1/cps1.c:cps1_run()` calls `update_inputport()` once per
emulated frame, ahead of CPU execution. Input is not resampled for each
CPS1 68000 register access.

The old `ps2_input_edge_latch.h` merges every observed rising edge of
a given button into one bit until consumption. Deterministic C99 tests
demonstrate that two observed short taps become one game-visible down
and that separately observed RIGHT and DOWN taps can be delivered as a
phantom RIGHT+DOWN chord. A separately reproducible interleaving allows
an older `padRead()` result to update the latch after a newer result
because the sampler and main thread both read before entering the
`DIntr()/EIntr()` critical section. These are verified *software*
failures, but their contribution to the reporter's real-PS2 complaint
has not been isolated.

Historical PADMAN diagnostics (retained in Git history immediately
before commit `0f83a687`) found approximately 597-598 valid IOP
acquisitions per 600 CPS1 frames in PCSX2 with stock PADMAN. A 4 ms EE
polling experiment still saw only about 598 distinct IOP publications.
An experimental 4 ms IOP acquisition timer saw ~250 valid IOP responses
per second in PCSX2, but was deliberately never enabled for production
without physical hardware validation. Faster EE sampling or an EE FIFO
cannot recover transitions that the IOP never acquired.

## Implementation and boundaries

In the working branch, a dedicated EE worker exclusively calls
`padRead()` when running. The main thread consumes validated published
snapshots and never races another `padRead()`; if the worker cannot
start, a synchronous main-thread sampling fallback remains. The
worker is paused around pad topology refresh, analog-mode
reconfiguration and menu/game capture transitions. Disconnects and
invalid repeated packets clear stale data. Existing physical
controller order, multitap enumeration, analog axes and Start/Select
remain unchanged.

`ps2_input_history.h` keeps a bounded ordered sequence of complete
gameplay digital-button states, not independent bitwise press flags.
The consumer presents the freshest state when intermediate changes can
be safely coalesced. If a button toggled more than once, it replays
ordered snapshots across emulated frames. The capacity is 32 snapshots;
more than eight pending snapshots causes a bounded resync to current
physical state. Capacity overflow also immediately resynchronizes.
These policies intentionally prioritize live held controls over a
large, stale backlog. The history is cleared on menu transitions and
disconnection. Start/Select always come from the latest published
physical level, never from replayed gameplay history.

`DIntr()/EIntr()` protects the EE-side published copy/history and
worker lifecycle flags against EE-thread preemption. It does not
freeze PADMAN's IOP acquisitions or SIF/DMA publication, and no code
assumes that it does. No dynamic allocations happen at input-poll time.

This cannot represent two entire physical press/release cycles inside
one CPS1 frame *faithfully*. They require at least four changes across
multiple game polls, which modifies timing; the game's actual input
scan and attack recovery may also ignore otherwise delivered input.
The history is not a promise to preserve the precise timing of actions
faster than the emulated game's input granularity.

## Deterministic and emulator results

| Test | Old edge latch | History consumer |
| --- | --- | --- |
| Two observed B1 taps within 16 ms | 1 visible down | 2 visible downs, at 16 and 48 ms |
| RIGHT then separate DOWN, released before frame | Spurious RIGHT+DOWN | RIGHT, release, DOWN, release |
| Old snapshot processed after newer sample | Stale latch state can be resurrected | Not possible via two concurrent `padRead()` owners |
| Short 0-5 ms pulse before t=16 ms poll | One pressed frame | One pressed frame at t=16 ms; release at t=32 ms |

The two-tap timeline is *post-acquisition*: at t=0/4/8/12 ms
there are four **already observed** state transitions. It does not
claim a stock PADMAN can physically capture 4 ms pulses. In this trace,
the second press is shown 40 ms after its observed down transition,
illustrating the unavoidable replay/timing tradeoff.

The idealized, deterministic 16.667 ms game frame / 8 ms worker-phase
model also finds that ~23.7% of uniformly enumerated publication/worker
phase pairs could miss a freshly published state that a direct
frame-boundary `padRead()` would see. Its worst-case additional
delivery delay is one game frame (~16.7 ms). This is not a physical
measurement. **The single-owner change is therefore not yet proven
free of input-latency regressions.**

PCSX2 virtual-pad input (not a physical DualShock) used 40 Square taps
at 35/65 ms, 40 Square taps at 12/32 ms, and 20 Right+Square chords at
80/120 ms: 120 expected digital button-down transitions in all.
Counts were taken from the opt-in `PS2_INPUT_PROFILE` build:

| CPS1 game | Observed down/up by worker | Delivered down/up by input backend | Maximum backlog | Overflow/resync |
| --- | --- | --- | --- | --- |
| `sf2` | 118/118 | 118/118 | 1 | 0/0 |
| `dino` | 109/109 | 109/109 | 1 | 0/0 |

Neither game had an observed input lost *between the EE sampler and
backend consumer* in these PCSX2 runs. However, 2 and 11 injected
button-down events respectively were not observed by the worker. These
differences may reflect PADMAN acquisition, PCSX2 virtual-key timing,
host event-injection jitter or another earlier stage. No conclusion
about physical-PS2 controller acquisition follows from those counts.
The profiler does not currently inspect CPS1 active-low input-port
transitions or game attack animations, so it cannot prove what the
game logic actually accepted.

For `sf2`, the cumulative 1,200-poll PCSX2 report showed 17,459 us
of instrumented per-poll processing (~14.5 us/poll, peak 238 us) and
49,538 us across 2,770 worker passes (~17.9 us/pass, peak 34 us).
These are PCSX2 timings with diagnostic timer overhead, **not** EE
performance measurements from real PS2 hardware. The 33 invalid
samples in both cumulative reports occurred during startup; no
additional invalid count accumulated in the following report window.
The profiler is OFF by default and forced OFF for canonical releases.

## Validation

- Portable C99 reproduction and history policy tests pass.
- All 50 Desktop CTest cases pass.
- PS2 GCC 15.2.0 builds pass for CPS1, CPS2, MVS and NCDZ.
- PCSX2 booted `sf2` and `dino` through their ROM and analog-mode
  startup paths; scripted virtual-controller transitions reached the
  PS2 input backend and all observed transitions were delivered.
- The PS2Link endpoint `192.168.31.10` later became reachable on
  2026-11-18. A standalone, non-IOP-reset PADMAN probe ran on the
  physical PS2 and recorded 3,796 valid worker samples, zero invalid
  samples, and 105 observed press/release transitions each. Both the
  legacy latch and ordered-history consumer delivered 105/105; the
  maximum backlog was one, with zero overflow/resynchronizations.
  This validates basic real-hardware operation but does not reproduce
  the FIFO-vs-latch difference or prove lower input latency.
- A second isolated test is in `tests/ps2_issue33_hardware/`. Its
  `input_port_probe.c` compares a direct frame-boundary `padRead()`
  reference, the dedicated 8 ms worker snapshot, the legacy latch,
  and the ordered-history consumer. It projects their button levels
  to active-low Street Fighter II P1 CPS1 ports using the default
  six-button map from `config/sf2.ini` and `cps1/inptport.c`. The
  companion host test verifies the mapping and reversal order.
  This projection does **not** execute `update_inputport()` or any
  68000 game logic; its direct-vs-worker comparison deliberately has
  two readers and is only a diagnostic, not production architecture.
  `make -C tests/ps2_issue33_hardware check-host` and the PS2 GCC
  build both pass; the desktop CTest suite now contains 51 tests.
- An unattended physical run of this second test completed 7,200
  frame-boundary reads and 14,967 worker reads, all valid, with zero
  observed button transitions. The two references therefore remained
  equal, but this is only a **no-input baseline**, not latency evidence.
  The next session requires intentional physical button transitions.
  The revised probe also counts onset/release discrepancies against
  the direct frame-boundary read separately from raw level mismatches.
- To run the standalone hardware diagnostic, export `PS2DEV`, `PS2SDK`
  and the EE toolchain `PATH`, run
  `make -C tests/ps2_issue33_hardware`, then from that directory launch
  `ps2client -h 192.168.31.10 -t 1200 -e '[SF2PROBE] END' execee host:input_port_probe.elf`
  with ps2link already running. It neither resets the IOP nor exits
  to OSD; press START+SELECT to finish. **Do not send a simultaneous
  `ps2client ping`/`health` while its host: console connection is in
  use**: an exploratory run's console connection was closed after
  another client opened `host:/ps2client.ping`.
- There is no proven before/after physical-to-emulated input latency
  result, game-level attack-count comparison or live dual-controller /
  multitap regression test.

## Release / PR gate

**Do not open or merge a PR on these results alone.** Reproduce the
report on a real PS2, compare the old and new builds with identical
controller/game setup, inspect input-to-CPS1-port timing, test rapid
motions and simultaneously pressed attacks, reconnection, multitap,
menu exit and frame-time costs. If the additional sampler-phase delay
is perceptible, revise the delivery design before merging. A hardware
failure caused by IOP acquisition rather than EE delivery requires an
IOP/PADMAN investigation; a generalized EE FIFO will not fix it.
