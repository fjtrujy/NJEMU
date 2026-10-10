# CPS1 / PS2 input: extended PCSX2 test matrix

Date: 2026-10-10
Issue: https://github.com/fjtrujy/NJEMU/issues/15#issuecomment-6098377002
Branch at start: `me_sound_coprocessor`
Test setup: PCSX2 2.9.93 on macOS, PS2 CPS1 `NO_GUI` builds,
`PS2_INPUT_PROFILE=ON`, `USE_CACHE=OFF` (CPS1 does not use ROM caching).

## Summary

A large set of synthetic keyboard-input sequences in PCSX2 reproduced **two
distinct transition-sampling losses**:

1. A short press may start and end between PADMAN publications.
2. A short release between two otherwise long presses may be missed, so two
   independent taps appear as a single uninterrupted button hold.

Both occur before CPS1 consumes the controller bits. There is no evidence
in these tests that CPS1 discards an observed button press because movement
or another face button was held. An opt-in CPS1 port-level trace additionally
showed that every delivered Square rising edge was represented in the active-low
CPS1 register; the emulated 68000 read that register **twice every frame**
during the inspected game windows.

The experimental 4 ms EE input thread did **not** consistently improve short
press or short release detection. The PADMAN publication diagnostic confirms
that approximately 2,479 EE reads per 600 emulated frames expose only
598-599 new DMA publications, not 2,479 new physical controller samples.

These are synthetic PCSX2 keyboard-to-controller tests, not a substitute for
real PS2/DualShock measurements. Individual requested durations are host
`usleep()` targets, not verified physical electrical press durations.
These results establish **button transport and register visibility**, not
the number of in-game attack animations accepted.

## Test method and caveats

- Booted `dino.zip` directly via `game_name.ini`; injected Coin,
  Start and character selection, then sent virtual controller keys directly
  to PCSX2 with `CGEventPostToPid`. All activity stayed outside
  `resources/`.
- Keyboard codes: Z=Square, X=Cross, C=Circle, macOS Right Arrow=PS2 Right.
- For overlapping chords, key-down and key-up events are sequentially
  delivered in quick succession. A group of overlapping keys is **not**
  an atomic physical button event, which can influence 12 ms results.
- The diagnostic logs aggregate over 600 CPS1 samples (about ten seconds).
  Each scenario waited for the last relevant window to finish, and counts
  are differences between the cumulative totals before and after the test.
- For the worker, `[ps2_capture]` counts newly observed Square rising
  edges at the worker and distinct rising edges reaching its consumer.
  `[ps2_input]` on the normal path counts edges in direct `padRead`.
- `[cps1_port]` counts rising/falling edges in CPS1 P1 input register
  bit `0x0010` (low means Attack), after gamepad mapping and before 68000
  execution.
- `[cps1_cpu_input]` counts actual 68000 reads of the same input register,
  after the emulated frame has executed.
- No individual punch animation counting or physical button measurement was
  performed. Differences of a few events across runs should not be read
  as statistically significant improvements or regressions.

## A. Square presses: two input paths at 240p, dino

Results are **raw Square rising edges / sent rising edges**.

| Injected interval | Attempts | Normal polling | 4 ms EE worker |
| --- | ---: | ---: | ---: |
| 12 ms pressed, 32 ms released, first run | 60 | 51/51 | 50/50 |
| 12 ms pressed, 32 ms released, repeated | 60 | 43/43 | 46/46 |
| 25 ms pressed, 50 ms released | 40 | 39/39 | 40/40 |
| 35 ms pressed, 65 ms released | 40 | 40/40 | 40/40 |
| 100 ms pressed, 200 ms released | 20 | 20/20 | 20/20 |
| Hold Right, 35/65 ms Square taps | 40 | 40/40 | 40/40 |
| Hold Right, 100/200 ms Square taps | 20 | 20/20 | 20/20 |

The two 12 ms runs disagree by eight events even with the same nominal
stimulus. This is the expected susceptibility of sub-frame pulses to event
phase, scheduling jitter and virtual-controller polling.

## B. Overlapping-button chords, dino, 240p

| Injected chord | Interval | Attempts | Normal observed | Worker observed |
| --- | --- | ---: | --- | --- |
| Right + Square + Cross | 12 ms down / 32 ms up | 60 | Square 47; Right 48; Cross 46 | Square 47 |
| Right + Square + Cross | 35 ms down / 65 ms up | 40 | Square 40; Right 40; Cross 40 | Square 40 |
| Square + Cross + Circle | 12 ms down / 32 ms up | 60 | Square 46; Cross 42; Circle 41 | Square 53 |

On the normal path, every observed button edge was delivered with the same
count. The threaded profiler records only Square edges, so **other-button
counts cannot be inferred for the threaded path**.

Short non-atomic host chords can produce partially captured combinations.
At 35 ms, no consistent simultaneous-button transport problem appeared.

## C. Short *releases* between long Square presses, dino, 240p

Every press is held for **80 ms**; only the release gap changes.

| Release interval | Attempts | Normal raw/sent | Worker raw/sent |
| --- | ---: | ---: | ---: |
| 5 ms | 40 | 18/18 | 22/22 |
| 10 ms | 40 | 32/32 | 28/27 |
| 20 ms | 40 | 40/40 | 40/40 |
| 40 ms | 40 | 40/40 | 40/40 |
| 10 ms, with Right held | 40 | 28/28 | 28/28 |
| 10 ms, Square+Cross+Circle chords | 40 | Square 25/25; Cross 29; Circle 33 | Square 29/27 |

A missed *release* can merge two valid long presses into one continuous
held input. This is directly relevant to the reporter's repeated Attack
symptom: short **release** intervals can be responsible even when each
press is longer than a normal frame.

In the threaded 10 ms scenarios, an additional 1-2 rising edges observed
by the worker did not become distinct delivered edges. The worker uses
a latest-state + pending-press-bitmask rather than an ordered event queue;
it can merge successive press/release cycles when the intervening release
is not represented at the CPS1 frame boundary.

## D. PS2 video mode control, dino, normal polling

| Test | 240p | 480i | 480p |
| --- | ---: | ---: | ---: |
| 12 ms pressed, 32 ms released (60 attempts) | 51 | 47 | 52 |
| 25 ms pressed, 50 ms released (40 attempts) | 39 | 39 | 40 |
| 80 ms pressed, 10 ms released (40 attempts) | 32 | 29 | 34 |
| 80 ms pressed, 20 ms released (40 attempts) | 40 | 40 | 40 |
| Right+Square+Cross, 35/65 ms (40 attempts, Square count) | 40 | 39 | 39 |

The 240p column is from the normal first-matrix run, not a simultaneous
paired run with the other modes. Differences do not establish a causal video
mode effect. All three modes exhibit short-transition susceptibility.

## E. Second CPS1 game: sf2hf, 240p, normal polling

| Test | Attempts | Observed Square presses |
| --- | ---: | ---: |
| 12 ms pressed, 32 ms released | 60 | 41 |
| 35 ms pressed, 65 ms released | 40 | 40 |
| 80 ms pressed, 10 ms released | 40 | 30 |
| 80 ms pressed, 20 ms released | 40 | 39 |
| Right+Square+Cross, 35/65 ms | 40 | 39 |

This is evidence that short-transition losses are not specific to
`dino.zip` mapping. Whether the game accepts individual attack actions
after CPU input-register reads is a separate question.

## F. CPS1 register and emulated-CPU read trace

New diagnostic, enabled only by `PS2_INPUT_PROFILE=ON` on PS2/CPS1:
`[cps1_port]` reports port-1 input transitions after mapping; the added
`[cps1_cpu_input]` report counts actual `cps1_inputport1_r()` calls.

Selected fully bracketed trials from dino:

| Input scenario | Mode | Injected | PS2 raw | PS2 sent | CPS1 port Attack rising | CPS1 port Attack falling |
| --- | --- | ---: | ---: | ---: | ---: | ---: |
| Square 35/65 ms | normal | 40 | 40 | 40 | 40 | 40 |
| Square 80/10 ms | normal | 40 | 29 | 29 | 29 | 29 |
| Square 12/32 ms | normal | 60 | 47 | 47 | 47 | 47 |
| Right+Square+Cross 35/65 ms | normal | 40 | 39 | 39 | 39 | 39 |
| Square 35/65 ms | threaded | 40 | 40 | 40 | 40 | 40 |
| Square 80/10 ms | threaded | 40 | 30 | 29 | 29 | 29 |
| Square 12/32 ms | threaded | 60 | 40 | 40 | 40 | 40 |
| Right+Square+Cross 35/65 ms | threaded | 40 | 40 | 40 | 40 | 40 |

In dino, steady `[cps1_cpu_input]` windows report
**`port1_reads=1200 read_frames=600 attack_unread_frames=0`**:
the emulated 68000 reads port 1 **twice in every emulated frame**, and no
frame with Attack asserted lacked a CPU read. This rules out skipped CPU
register polling as the cause of the *recorded* missing transitions in
these runs; it does not prove that dino's game logic accepts every
otherwise valid Attack input.

## G. PADMAN publication control

Steady-state diagnostic `[ps2_padman]` and `[ps2_padman_worker]` windows
during the long matrix:

| Mode | EE reads per 600 CPS1 frames | Distinct IOP-to-EE publications | Repeated publication reads |
| --- | ---: | ---: | ---: |
| normal | 600 | typically 597-599 | typically 1-3 |
| 4 ms worker | 2,479-2,480 | typically 598-599 | typically 1,880-1,882 |

The worker never yielded a consistent short-pulse advantage despite
substantially more EE reads. This is consistent with approximately one
fresh published controller snapshot per 60 Hz emulated frame in PCSX2.
It is **not** a direct physical controller-electronics measurement.

## Recommendations

1. **Do not enable `PS2_INPUT_THREADED` by default.** It adds memory,
   scheduling, synchronization and edge-coalescing tradeoffs without a
   reproducible short-tap benefit. Keep the experimental option for
   diagnostics only.
2. Maintain the original CPS1 input semantics. A FIFO of truly observed
   transitions could preserve history, but extending or replaying arbitrary
   presses across frames may change original arcade attack recovery/cancel
   behavior. In particular, do not blindly synthesize an extra punch
   whenever two physical taps occur within one emulated frame.
3. A stock-cadence **IOP event ring** can prevent loss between IOP
   acquisition and EE consumption, but **cannot recover** short button
   states never sampled by PADMAN. Before implementing one, demonstrate
   actual IOP-observed transitions being lost in the EE path.
4. If further improvement is desired, the next high-value research target
   is the rate at which PADMAN actually polls **the physical controller**.
   An optional test-only faster IOP polling cadence (with exclusive SIO2
   coordination) might help if the hardware and SDK permit it; PCSX2
   does not substitute for real PS2 validation.
5. If the original report still reproduces at human-speed presses (e.g.
   >35 ms press **and** >20 ms release), collect the reporter's controller
   configuration and game/save state and inspect game attack cooldown
   behavior. The present tests do not reproduce a transport loss for
   that interval class in dino under PCSX2.

## Evidence and reproduction

Ignored, local-only builds and files:

- `build_issue15_pcsx2_dino/matrix/results.tsv` and `normal.log` /
  `thread.log` — initial paired timing/chord matrix.
- `build_issue15_pcsx2_dino/matrix/gaps_results.tsv` — release-gap matrix.
- `build_issue15_pcsx2_dino/matrix/video_results.tsv` — 480i/480p controls.
- `build_issue15_pcsx2_dino/matrix/sf2hf_results.tsv` — second CPS1 game.
- `build_issue15_pcsx2_dino/matrix/port_results.tsv` — PS2 raw-to-CPS1
  register edges before CPU read instrumentation.
- `build_issue15_pcsx2_dino/matrix/port_cpu_results.tsv` and
  `port_cpu_normal.log` / `port_cpu_thread.log` — final register and
  68000 read diagnostics (additional repeat run).
- The host event generators `send_inputs` and `send_chords` and
  shell scripts in the same ignored build directory.

This study changes **only optional diagnostics and documentation**.
The physical PS2 is disconnected, real-controller results are unknown,
and no code or assets under `resources/` were changed.
