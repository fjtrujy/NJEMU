# PS2 input-flow investigation (issue #15)

Date: 2026-10-10
Reference: https://github.com/fjtrujy/NJEMU/issues/15#issuecomment-6098377002

## What the reporter confirmed

QSound and gameplay hitches are fixed in `dino.zip`. Repeatedly tapping
Attack while stationary still sometimes fails to trigger an attack.

There is no evidence yet establishing whether the missing attack was absent
from the PS2 pad sample, lost during mapping, or rejected by game logic.

## Actual CPS1 input path

1. PADMAN/IOP publishes controller state; the PS2 EE driver uses
   `padGetState()` and copies the latest payload through `padRead()`.
2. The common driver converts digital pad status into `PLATFORM_PAD_*`
   flags (including D-pad), with analog directional thresholds added by
   `input_state_digital_buttons()`.
3. `cps1_run()` calls `update_inputport()` **once every emulated frame**
   before `timer_update_cpu()`, regardless of video presentation or frame
   skipping.
4. `update_inputport()` queries `gamepad_count()`, polls selected physical
   controllers, applies orientation and optional autofire, then writes
   active-low CPS1 input-port values. The emulated 68000 reads those registers
   via `cps1_inputport1_r()` / CPS1 memory callbacks.
5. For standard `dino.zip` and the default 3-button mapping,
   `P1_BUTTON1`/Attack is Square (`PLATFORM_PAD_B3`). Its first-player
   port-1 bit is `0x0010` (low means pressed). The three autofire mappings
   default to **disabled**. Actual saved per-game mappings may differ.

No extra input gating tied to VSync or video frame skip was identified in
the CPS1 frame loop.

## Confirmed code issues and changes

### 1. Transient PADMAN states removed controllers from the active list

Previously, `gamepad_count()` and `getActiveJoyInfo()` required
`PAD_STATE_STABLE` or `PAD_STATE_FINDCTP1` on every lookup. PS2SDK's
`padGetState()` can instead report `PAD_STATE_EXECCMD` when a controller
request is in progress, even when the underlying state is stable.

That meant a known controller could temporarily vanish from the logical
list, bypassing the existing one-missed-sample cache entirely. With two
controllers, this also allowed a transient logical player-index shift.

The new `ps2_pad_presence_update()` policy remembers a controller only
after it has been ready at least once, keeps it enumerable while PADMAN is
executing a command, and immediately clears its presence and last sample on
disconnect, error, or FINDPAD/discovery. The sample cache remains bounded
to at most one failed read.

### 2. Slow device discovery was reachable from each gameplay sample

The old `getActiveJoyInfo()` performed a second complete enumeration and
`refreshJoyInfo()` when a controller could not be selected.
`refreshJoyInfo()` can issue multitap port/discovery calls. It therefore
violated the existing driver contract that controller discovery stays
outside latency-sensitive gameplay.

The retry/discovery path has been removed from `getActiveJoyInfo()`.
The established `refresh_gamepads()` entry point remains available
when the in-game menu opens, and already opened pad ports still detect
state changes. Multitap/port discovery may now require opening the menu
if hardware is hot-plugged after startup.

### 3. Short positive packets must not become phantom button presses

`padRead()` copies a variable-length packet. The consumer now requires
that the returned length cover the `btns` field. A truncated nonzero
packet is treated as a failed sample rather than turning the zeroed
`btns` field into a false `0xffff` pressed mask.

## Important unresolved ambiguity: rapid button release/press

The existing one-sample cache has two possible effects:

- It prevents a transient failed PADMAN read from falsely releasing an
  actually held button.
- If the missing sample *really contained a release*, retaining the last
  pressed value may merge two fast taps into a single continuous hold.
  No algorithm based only on one sample per emulated frame can distinguish
  these cases reliably.

Likewise, a complete physical press/release between two emulation-frame
polls is invisible unless the system independently captures and queues
input transitions at a higher frequency. Adding such a queue or repeating
short presses would change semantics, so neither was introduced without
actual traces.

A game's attack-recovery/animation rules can also legitimately ignore
otherwise delivered presses.

## Optional diagnostic build

Compile with `-DPS2_INPUT_PROFILE=ON` for an opt-in, PS2-only diagnostic.
Normal builds and canonical releases keep it OFF.

Each **600 calls to controller 0 sample()**, the driver prints one line:

```text
[ps2_input] polls=600 fresh=... unavailable=... read_fail=... mode_wait=... cached=... square_raw=DOWN/UP square_sent=DOWN/UP
```

- `fresh`: samples with valid-length pad packets.
- `unavailable`: no connected/selected logical pad.
- `read_fail`: missing/short PADMAN payload.
- `mode_wait`: controller mode not yet configured/ready.
- `cached`: one-frame cached sample substituted for a failed read.
- `square_raw`: Square down/up transitions visible in **fresh PS2 samples**.
- `square_sent`: Square transitions delivered to the common input state,
  including synthetic releases on unavailable samples.

This is an aggregate of *sampled* edges, not every physical switch
transition. Output is rate-limited to one line per 600 calls; it is still
diagnostic I/O and should not be used for normal performance benchmarks.

### Physical PS2 reproduction

1. Run the profiled CPS1 build with the same `dino.zip` and default button
   mappings; ensure Autofire is OFF.
2. Stand still and repeat isolated Square presses at slow and fast cadences,
   recording the attempted presses and on-screen attacks on video.
3. Capture `[ps2_input]` output with ps2link/host logging over multiple
   600-sample windows; compare with a non-profiled build.
4. Check whether `unavailable`, `mode_wait`, `read_fail`, or `cached`
   are nonzero during the reproduction. Compare `square_raw` and
   `square_sent` press counts. Their difference implicates the pad-read /
   cache layer; equal counts with fewer attacks suggest investigating CPS1
   port delivery and game timing next.
5. If needed, capture CPU-visible `cps1_port_value[1]` changes alongside
   frame indices before changing emulation behavior.

The reporter's short video or save state can help correlate missing attack
animations with the sampled-button logs. A save state alone cannot recreate
physical PADMAN timing.

## Tests and scope

- Pure host tests cover ready/busy/disconnected/reconnect transitions and
  ensure two connected controllers keep their enumeration order during
  short command states.
- Desktop CPS1 suite: 39/39 passed.
- PS2 CPS1 and MVS normal builds: passed.
- PS2 CPS1 with `PS2_INPUT_PROFILE=ON`: compiled.
- Real PS2 raw-button and animation capture: **not yet performed**.

## PCSX2 synthetic Square-button test (2026-10-10)

Launched **PCSX2 2.9.93** with a dedicated CPS1 **NO_GUI** PS2 build
(`PS2_INPUT_PROFILE=ON`, 240p output) and `game_name.ini=dino`.
The emulator log confirmed `ROM set "dino"`, the game rendered, the
controller completed DualShock 2 setup, and Coin/Start/Square reached the
game. PCSX2 ran at 60 FPS. CPS1 does not use ROM caching.

Host-side test code under `build_issue15_pcsx2_dino/` sent targeted
macOS keyboard events directly to the PCSX2 PID, avoiding shared-desktop
foreground-focus interference. `Keyboard/Z` was bound to PS2 Square.
These were *synthetic keyboard events*, not a physical DualShock test.

| Sequence | Sent down/up events | Raw sampled Square down/up | Delivered Square down/up |
| --- | ---: | ---: | ---: |
| 100 ms held, 300 ms released | 20/20 | 20/20 | 20/20 |
| 35 ms held, 65 ms released | 30/30 | 30/30 | 30/30 |
| 25 ms held, 50 ms released | 30/30 | 29/29 | 29/29 |
| 12 ms held, 32 ms released | 40/40 | 31/31 | 31/31 |

Results sum complete 600-poll log windows where a sequence spanned a
report boundary. All reporting windows during these tests showed
`read_fail=0`, `unavailable=0`, `mode_wait=0`, `cached=0`,
and `fresh=600`.

**Interpretation:** for these runs, raw and delivered Square transitions
always matched. The missing 1/30 and 9/40 very short presses were already
absent from the sampled pad state, rather than disappearing in the CPS1
mapping layer or the one-sample fallback cache. The emulator samples at
roughly 60 Hz, so a button pulse shorter than one frame can fall between
polls. This is a plausible explanation for *very short* missing taps;
additional timing jitter or PCSX2's host keyboard-to-virtual-pad sampling
can also explain losses. The results do not prove where an event was lost
before the PS2 input driver and cannot be extrapolated directly to physical
DualShock polling on a real PS2.

The character-selection screen and an active playable character were
visually confirmed. However, there was **no frame-by-frame count of attack
animations**, so these tests do not prove the game accepted all delivered
presses. The reporter's original human-speed missed-attack behavior was
**not** reproduced in the 100 ms and 35 ms held sequences.

Evidence: `build_issue15_pcsx2_dino/pcsx2_input.log` and local PCSX2
screenshots (build artifacts, not committed).

## PCSX2 simultaneous-button follow-up (2026-10-10)

The PS2 input profiler now also reports Right, Cross, and Circle pressed
edges and the number of polls containing full button combinations. Counts
are reported as `raw/delivered`; a full-chord frame means all mask bits were
set in the same pad sample, not merely pressed within the same test interval.

Used the same CPS1 NO_GUI diagnostic build and `dino.zip` in PCSX2
2.9.93. The host delivered near-simultaneous key-down events for the
configured virtual keys (`Right`, `Z`=Square, `X`=Cross, `C`=Circle),
held them, then released in reverse order. Events were sent directly to
the PCSX2 process; this did **not** use a physical DualShock controller.

| Held combination | Injected cycles | Raw/delivered edges | Full-chord sample frames |
| --- | ---: | --- | ---: |
| Right + Square, 180 ms down / 240 ms up | 12 | Right 12/12, Square 12/12 | 129/129 |
| Square + Cross, 180 ms down / 240 ms up | 12 | Square 12/12, Cross 12/12 | 132/132 |
| Square + Cross + Circle, 180 ms down / 240 ms up | 12 | Each button 12/12 | 132/132 |
| Right + Square + Cross, 180 ms down / 240 ms up | 12 | Each button 12/12 | Right+Square 131/131, Cross+Square 131/131 |
| Hold Right continuously, tap Square 20 times (100 ms down / 200 ms up) | 20 | Right 1/1, Square 20/20 | Right+Square 124/124 |
| Right + Square + Cross, **12 ms down / 32 ms up** | 40 | Right 27/27, Square 25/25, Cross 24/24 | Right+Square 25/25, Cross+Square 24/24 |

Each edge-count pair above means *raw stage count/delivered stage count*;
the press/release pairs in these tests were balanced. Where individual
sequences crossed the 600-poll reporting boundary, consecutive windows
were combined. In every tested window there were **zero** `read_fail`,
`unavailable`, `mode_wait`, and `cached` observations after startup.

**Findings:** CPS1's default button mapping preserves simultaneously held
attack and movement bits. PCSX2 captured all the tested human-duration
combinations, including holding a direction while repeatedly attacking.
The shortest three-key pulses did **not** always arrive as the same
button set: only 27 Right, 25 Square, and 24 Cross presses were observed
from 40 injected cycles. The differences were visible in the **raw pad
samples** already; no further losses occurred in the PS2 delivery path.

The host injected the individual key events sequentially, even though
their press intervals overlapped. PCSX2's virtual-pad event timing may
therefore contribute to the short-pulse losses. A 60 Hz polling path can
also miss pulses shorter than one emulated frame, or capture only part of
a non-atomically delivered chord. These results do not establish physical
keyboard ghosting or a physical DualShock defect.

**Intentional combos:** Start+Select invokes the in-game menu. Holding
L+R with Start or Select invokes the existing emulator service controls.
These are not ordinary action chords and should not be mistaken for
missing game input.

**Still unverified:** the exact CPU-visible CPS1 port register transitions,
the number of resulting attack animations, and simultaneous-button
behavior on a real PS2 with a physical controller. Before changing
polling frequency or buffering press edges, compare real-hardware traces
for a held direction plus repeated Attack, and confirm the actual
mapping/autofire settings used by the issue reporter.

All changes are PS2-local except the build option and portable host test.
No `resources/` content was modified, and no GitHub issue comment was posted.

## Independent EE input-thread experiment

An optional `PS2_INPUT_THREADED=ON` CPS1/PS2 build now samples the
PADMAN DMA buffer approximately every 4 ms, preserving observed face-button
press edges until the next emulated frame. PCSX2 showed no measurable
improvement for 12 ms Square presses: 27/40 and 23/40 across two runs,
compared with a previous 31/40 frame-polling run. Ordinary 100 ms presses
still registered 20/20. This remains disabled by default because faster
EE reads do not necessarily mean newer IOP controller packets. Full design,
measurement caveats and physical-hardware validation requirements are in
`docs/PS2_INPUT_CAPTURE_THREAD_EXPERIMENT.md`.
