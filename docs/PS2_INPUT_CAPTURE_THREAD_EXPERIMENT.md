# Experimental PS2 CPS1 input worker (2026-10-10)

This is a test of whether reading the EE-side PADMAN DMA buffer between CPS1
emulation frames can preserve brief button presses. It is **not** a replacement
for the existing input path and remains disabled in normal/canonical releases.

## Architecture

Set `-DPS2_INPUT_THREADED=ON` when building `PLATFORM=PS2` and
`TARGET=CPS1`. Other platform/target combinations are rejected at configure
time. The option defaults to OFF; `PS2_INPUT_PROFILE=ON` additionally
enables diagnostic read and Square-edge counts.

The experimental worker:

- Runs as a separate PS2 EE thread with priority 31 (emulation priority 32,
  audio priority 8) and nominal 4 ms `DelayThread()` intervals.
- Reads only the primary controller's port-0/slot-0 PADMAN DMA state.
  Controller discovery, mode-setting RPCs, and logical controller indexing
  remain in the main thread.
- Synchronizes a compact latest-state / pending-button snapshot with
  `DI()`/`EI()` critical sections on the single-core EE.
- Latches a rising transition for the four face buttons until the next
  emulation-frame consumer. Movement and system/menu controls remain
  level-triggered; they must not remain active after a short pulse.
- Resets pending presses when a disconnected/error pad is observed.
- Stops and joins before controller topology refresh or shutdown. If thread
  creation fails, the direct per-frame path remains available.
- Uses normal synchronous polling for any logical controller other than
  physical port-0/slot-0.

A 4 ms read interval is **not** proof that the underlying IOP publishes
new pad data every 4 ms. PS2SDK's `padRead()` copies the most recently
received IOP DMA packet; repeated EE reads of that packet do not recover
physical events that IOP polling already missed.

The worker deliberately does not count/replay multiple independent presses
of the *same* button within one emulated frame. Its one-frame pulse
accumulator is a boolean mask, not a general-purpose event queue. It can
also produce a one-frame press after a very brief physical tap ended; this
must be validated for gameplay semantics.

## PCSX2 evidence

PCSX2 2.9.93, CPS1 `dino.zip`, 240p, GUI OFF, synthetic host
`Keyboard/Z` events targeted at the PCSX2 process:

| Sequence | CPS1 frame polling (prior run) | Separate 4 ms EE worker |
| --- | ---: | ---: |
| 40 Square presses, 12 ms down / 32 ms up | 31 sampled of 40 | 27/40 (first run), 23/40 (repeat) |
| 20 Square presses, 100 ms down / 300 ms up | 20 sampled of 20 | 20/20 |

The worker reported roughly **2,479-2,480 PADMAN reads per 600 emulated
frames** (about four reads per frame), with PCSX2 running at 60 FPS.
The 40-press repetitions were separate runs with different timing
alignment and are not a statistically controlled proof of regression.
However, **no improvement has been demonstrated** by the worker.

In the second short-pulse run, it counted 23 new Square presses in the
worker but 22 distinct delivered rising edges. Two short transitions can
become one continuous frame-level button state even when both were observed
by the worker; this is a limitation of the current CPS1 input contract.

The result suggests that the controlling bottleneck for these tests is the
IOP/PCSX2 input-update cadence and/or timing of host virtual-button events,
not solely the number of times NJEMU's EE frame loop reads the pad buffer.
The worker is **not enabled by default**. The experimental thread also
introduces periodic alarm/semaphore scheduling overhead from PS2SDK's
`DelayThread()`, which needs CPU/IOP profiling on physical hardware
before any promotion.

## Remaining validation

- Test a real DualShock 2 on PS2 hardware, with a controlled button rig
  or high-speed video and logs, rather than assuming PCSX2 keyboard timing
  matches real hardware.
- Determine actual fresh PADMAN/IOP packet publication frequency.
- Capture the emulator's CPS1 port-1 Attack bit (active-low bit `0x0010`)
  alongside raw controller edges and the game's animation state.
- Verify analog-controller initialization, multiple controllers, reconnect,
  menu-open/close, and safe worker shutdown.
- Compare the scheduler overhead and gameplay frame time at equal audio/
  VSync settings before considering any thread by default.

If IOP provides button updates only once per ~16.7 ms, increasing the
frequency of EE-side reads will not fix shorter physical taps. Improvements
would require additional evidence and potentially lower-level input capture,
rather than blindly increasing EE thread frequency.

No files under `resources/` were modified. The CPS1 ROM-cache system is
not involved; CPS1 has `USE_CACHE=OFF`.
