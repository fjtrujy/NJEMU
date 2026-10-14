# PS2 PADMAN IOP acquisition experiment (PCSX2 only)

Date: 2026-10-14
Related issue: https://github.com/fjtrujy/NJEMU/issues/15#issuecomment-6098377002

## Why this experiment exists

The original EE-side 4 ms controller-reading thread did not improve short
press reliability because calling `padRead()` repeatedly does not force new
SIO2 controller transactions. PADMAN's stock transfer thread is started by
the IOP VBlank interrupt. Its 32-bit per-pad `frame` counter is incremented
by `DmaSendEE()`, not directly by the successful `ReadData()` call; the
previous EE-side counter therefore measured **DMA publication**, not
verified physical controller acquisition.

This experiment instruments the *original PS2SDK PADMAN code* without
modifying either installed PS2SDK or ps2_drivers. It is opt-in, CPS1-only,
non-release, and tested so far **only in PCSX2**.

## Implementation

`tools/ps2/build_padman_iop_probe.sh` copies
`<PS2SDK-source>/iop/input/padman` to a new ignored build directory and
applies `tools/ps2/padman_iop_probe.patch`. The patch:

- In `UpdatePadThread()`, increments `nj_probe_acquired` **only after
  ReadData succeeds and the returned pad data passes PADMAN validation**.
- Counts Square press/release transitions in the accepted raw IOP response.
  Square is bit 7 of `outbuffer[4]`, the high byte of the active-low
  PS2 button field.
- Copies both counters into the eleven reserved bytes at the end of
  PS2SDK's **new** 128-byte per-pad DMA record, with the marker `NJ\x01`.
  No existing libpad field or public ABI is replaced.

An additional **explicit** `--timer-4ms` parameter also applies
`tools/ps2/padman_iop_timer_4ms.patch`. This experimental PADMAN replaces
VBlank-triggered transfers with a 4,000-us IOP `SetAlarm()` while retaining
the original PADMAN update thread, SIO2 transaction ownership, VBlank
shutdown callback and pad/rumble configuration paths. A failed alarm
registration falls back to ordinary VBlank acquisition. The timer is
cancelled during `padEnd()` after active ports have closed.

A second opt-in CMake parameter,
`PS2_IOP_PADMAN_PROBE_IRX=/absolute/path/to/padman.irx`, embeds the custom
IRX as the PADMAN descriptor for a **classic PS2 CPS1 diagnostic ELF**. It
requires `PS2_INPUT_PROFILE=ON`, `PS2_EXTERNAL_IRX_IMAGE=OFF`, and
`RELEASE=OFF`; other platforms and release builds reject the option.
All other IRX modules continue to come from the usual ps2_drivers library.

When the correct diagnostic IRX is running, the EE prints:

```
[ps2_padman] frames=600 reads=... advanced=... skipped=...
[ps2_iop_acquire] frames=600 valid=1 acquired=... square_edges=...
```

A stock PADMAN never exposes the marker, so the decoder rejects its
reserved bytes. Counter differences are taken modulo 32 bits and the
selection of the newest 128-byte DMA record handles frame-counter wrap.
The test covers the marker, byte layout, oldest/newest selection, truncated
DMA buffers and wrap behavior.

## Reproduction

Run from the NJEMU checkout, with the PS2 EE/IOP toolchains installed:

```sh
export PS2DEV=/path/to/ps2dev
export PS2SDK="$PS2DEV/ps2sdk"
export PATH="$PS2DEV/bin:$PS2DEV/ee/bin:$PS2DEV/iop/bin:$PS2SDK/bin:$PATH"
PS2SDK_SOURCE=/path/to/ps2sdk/source

tools/ps2/build_padman_iop_probe.sh \
  "$PS2SDK_SOURCE" build_issue15_padman_stock_probe

tools/ps2/build_padman_iop_probe.sh \
  "$PS2SDK_SOURCE" build_issue15_padman_timer_probe --timer-4ms

cmake -S . -B build_issue15_padman_test \
  -DCMAKE_TOOLCHAIN_FILE="$PS2DEV/share/ps2dev.cmake" \
  -DPLATFORM=PS2 -DTARGET=CPS1 -DGUI=OFF -DPS2_VIDEO_MODE=240p \
  -DPS2_INPUT_PROFILE=ON \
  -DPS2_IOP_PADMAN_PROBE_IRX="$PWD/build_issue15_padman_timer_probe/irx/padman.irx"

cmake --build build_issue15_padman_test -j4
```

To compare against ordinary PADMAN, configure a separate CPS1 debug build
pointing at `build_issue15_padman_stock_probe/irx/padman.irx` instead.
Use equivalent ROM, settings, audio, video and PCSX2 configuration.
Do not overwrite any user ROMs or files under `resources/`.

## PCSX2 observations

PCSX2 2.9.93, CPS1 `dino.zip`, 240p, NO_GUI, synthetic host keyboard
events through the existing `send_inputs` experiment helper. Counts use
*10-second, 600-frame reporting windows*. Exclude controller startup and
mode negotiation from steady-state comparisons.

| Steady state (600 CPS1 frames) | Stock VBlank PADMAN | 4 ms IOP timer PADMAN |
| --- | ---: | ---: |
| Valid IOP controller acquisitions | 597-598 | 2,471 at startup; 2,496 steady |
| Accepted Square edges, no input | 0 | 0 |
| Approximate valid acquisition rate | ~60 per second | ~249-250 per second |
| CPS1 frames during same interval | 600 | 600 |

The 4 ms timer therefore causes **genuinely more accepted IOP SIO2 pad
responses**, unlike the previous 4 ms EE sampler experiment. At least
several steady 600-frame windows and a normal CPS1 boot executed under
PCSX2 without an observed stall. This is **not** a PS2 hardware timing,
performance or stability guarantee.

Synthetic button sequences from separate runs:

| Sequence | Attempts | VBlank IOP acquisition: distinct delivered Square downs | 4 ms IOP acquisition: distinct delivered Square downs | 4 ms IOP observed Square edges |
| --- | ---: | ---: | ---: | ---: |
| 35 ms down / 65 ms up | 40 | 40 | 40 | 80 |
| 80 ms down / 10 ms up | 40 | 28 | 32 | 64 |
| 12 ms down / 32 ms up | 60 | 53 | 59 | 118 |
| 80 ms down / 5 ms up | 40 | Not compared in this run | 21 down, 20 up | 42 |

The last scenario's IOP edge count includes a transition at the edge of
a reporting window. These are not exactly synchronized paired samples;
the host's `usleep` durations and PCSX2's virtual-controller event
handling have jitter. Treat the differences as **promising exploratory
evidence**, not causal proof or stable capture probabilities.

An additional concurrent-key test injected Right + Square + Cross as
separate host keyboard events per chord. With the 4 ms IOP alarm,
the 12/32 ms sequence delivered Square 47/60, Right 48/60 and Cross
47/60; the corresponding IOP Square-edge count was 94. With
35/65 ms chords, **all three buttons were delivered 40/40**, with 80
IOP Square edges. The shorter chord is not atomically applied by the
host event injector, so it does not establish a real-controller
simultaneous-button limitation or improvement.

For the 12 ms and 10 ms-gap tests, the IOP's observed edges matched the
EE's delivered rising and falling transitions in the reporting windows.
In those particular tests, an event FIFO would not recover additional
transitions, because the missing events were already unobserved by IOP.
For other workloads or emulation stalls, a properly acknowledged IOP
ring could still be useful, but its added complexity requires evidence.

### Local, ignored logs

- `build_issue15_iop_probe/pcsx2_iop_corrected.log`: 60 Hz acquisition.
- `build_issue15_iop_probe_fast/pcsx2_fast.log`: 4 ms timer acquisition.
- `build_issue15_iop_probe/build_probe.log` and
  `build_issue15_iop_probe_fast/build_timer.log`: IOP compilation.
- `build_issue15_iop_probe/cps1_build.log` and
  `build_issue15_iop_probe_fast/cps1_build.log`: PS2 ELF compilation.

## Risks and next steps

The 4 ms timer is **strictly experimental**, not a product default:

- IOP SIO2 bus contention with multitaps, memory cards, rumble and other
  peripherals is not validated on actual hardware.
- Increased IOP transactions and SIF DMA traffic may affect sound timing,
  frame pacing, input startup/exit behavior and PS2Link use.
- Some games and controllers might not support a constant ~250 Hz poll
  schedule. PCSX2's keyboard input is not a physical DualShock 2.
- Actual repeated quick attacks can also be rejected by original CPS1
  game recovery logic even when the input register is read correctly.
- Replacing the driver in a release or silently changing native input
  semantics is not justified. Keep the stock PADMAN as the default.

If hardware becomes available, validate SIO2/multitap, simultaneous
face buttons, rumble, disconnect/reconnect, menus, suspension/restart and
audio/video performance before any proposal to enable faster acquisition.
Only implement an IOP event ring after demonstrating genuine IOP-observed
transitions lost before the EE consumes them.

### Follow-up: IOP-observed edges missing at the CPS1 frame boundary

The follow-up diagnostic did find such a case with 12/12 ms Square taps:
112 IOP-accepted Square edges versus 104 CPS1 frame-level edges in a
600-frame window, representing four complete missed press/release cycles.
A strictly opt-in, Square-only EE event FIFO reconstructed ten otherwise
unrepresented full cycles in a separate PCSX2 test window; this changes
input duration semantics and is **not** a production default. See
`docs/PS2_IOP_SQUARE_EVENT_REPLAY_EXPERIMENT.md` for implementation,
controls, limitations, and the reason not to generalize to other buttons
without a properly ordered multi-button IOP event protocol.
