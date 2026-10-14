# Experimental IOP Square edge FIFO and CPS1 frame handoff

Date: 2026-10-14
Related issue: https://github.com/fjtrujy/NJEMU/issues/15#issuecomment-6098377002
Prerequisite: docs/PS2_PADMAN_IOP_ACQUISITION_EXPERIMENT.md

## Finding: PADMAN-observed edges can be lost before CPS1

The PS2 diagnostic IRX already exposes a cumulative Square press/release
edge counter from `UpdatePadThread()`. The stock IOP/EE interface publishes
only the latest state, so a press and release observed by IOP between two
CPS1 frames can completely disappear from the CPS1 input register.

The new read-only `[ps2_iop_gap]` diagnostic compares *successive CPS1
frame samples* with the number of accepted Square edges observed by the IOP
between those samples. Unlike aggregate 600-frame counters, it can identify
full IOP-observed cycles that never reach CPS1.

In PCSX2 2.9.93, CPS1 `dino.zip`, 240p and the 4 ms IOP PADMAN timer,
an 80-cycle synthetic Square input sequence (12 ms down / 12 ms up)
produced in one steady 600-frame window:

| Metric | No replay, new per-frame probe |
| --- | ---: |
| Square edges accepted on IOP | 112 |
| Square edges presented on CPS1 frame samples | 104 |
| Unrepresented edges | 8 |
| Fully invisible press/release cycles | 4 |
| Frames with 2 or more IOP edges | 4 |
| Driver/counter discontinuities | 0 |

This is *specific evidence of a handoff loss*, not merely a missed IOP
hardware sample. In contrast, a 5/5 ms keyboard stimulus lost most
transitions before they could be observed even by the 4 ms IOP probe.

These results do not establish a physical DualShock 2 improvement:
PCSX2's keyboard-to-pad event timing and controller sampling may differ
from genuine SIO2 hardware.

## Experimental Square FIFO

A **second opt-in** CMake option is available:

```sh
-DPS2_INPUT_PROFILE=ON \
-DPS2_IOP_PADMAN_PROBE_IRX=/absolute/path/to/timer/padman.irx \
-DPS2_IOP_SQUARE_REPLAY=ON
```

The option requires PS2/CPS1, `GUI=OFF`, classic embedded drivers, and
a non-release build. Its default is **OFF**. Every other core, the
production PADMAN, and the original CPS1 input logic are unchanged.

The experimental EE-side FIFO uses the cumulative Square edge sequence
recorded by the instrumented **IOP**. For a single binary button, accepted
edges necessarily alternate (down/up), so its ordered edge history can
be reconstructed from the last known button state and the cumulative
IOP edge count without adding a PADMAN RPC endpoint.

The ring stores up to 8 alternating Square states and presents at most
one transition to CPS1 per emulated frame. It resets on malformed pad
samples, controller loss, refresh, sequence inconsistency, or overflow.
The other button bits (directions, Cross, Circle, etc.) are never
replayed or changed. The default frame-based input path remains a
reference oracle.

**Important fidelity tradeoff:** A fully sub-frame physical press/release
pair now becomes a one-frame pressed state followed by a release in a
later frame. That is **not** identical to the arcade system's
one-snapshot-per-frame polling. This can introduce late punches, delays
or unintended chord timing. The experimental FIFO is not appropriate
as a release default without agreement on the intended semantics and
validation of attack timing on real hardware.

`[ps2_iop_replay]` reports `emitted` (all Square press edges delivered
through the FIFO), `recovered` (presses emitted when the most recent raw
state was released), `overflows`, `resyncs`, `max_pending` and
`pending` per 600 CPS1 frames. The `recovered` counter does not count
held-button release/press cycles whose physical ending state is down;
compare `square_sent` with `square_raw` for total differences.

## PCSX2 replay trial

A subsequent trial of the same nominal 12/12 ms, 80-cycle synthetic
Square sequence with replay enabled produced:

| Metric | Result |
| --- | ---: |
| IOP Square edges | 114 |
| Original CPS1 raw Square press/release transitions | 47/47 |
| FIFO-delivered Square press/release transitions | **57/57** |
| Full IOP cycles invisible to raw CPS1 frames | 10 |
| FIFO presses recovered while raw state released | **10** |
| Ring overflows, sequence resyncs | **0 / 0** |
| Maximum pending edges | 2 |

In this *specific run*, the 57 down/up pairs delivered to CPS1 equaled
the IOP-observed 114 edges. The run without replay had different host
input phase and saw 112 IOP edges; **do not compare 57/47 as a
controlled statistical speedup**. It directly demonstrates the FIFO
reconstructing the events observed by the IOP in that run.

Additional replay controls:

| Sequence | Observed outcome |
| --- | --- |
| 10/10 ms, 80 attempts | Raw/delivered 34/34, no synthetic recovery in inspected window |
| 35/65 ms, 40 attempts | Raw/delivered 40/40, no recovery or overflow |
| 80/5 ms, 40 attempts | Short releases mostly lost at acquisition; no recovery in the observed windows |
| Right+Square+Cross, 35/65 ms, 40 attempts | 40/40 for all three buttons |
| Right+Square+Cross, 12/32 ms, 60 attempts | Square raw 37, delivered 46; Right 40, Cross 34; Square-only FIFO cannot recover other buttons |

Longer gameplay/real-user attack animation acceptance, multi-controller
operations, audio schedule effects, connection churn and CRT hardware
behavior remain **unverified**.

## Why not an all-button IOP ring yet

The present PADMAN diagnostic transfer uses eleven otherwise unused
bytes in PS2SDK's 128-byte per-pad DMA records: eight bytes for counts
plus the `NJ\x01` marker. It does **not** contain an ordered,
multi-button ring or an EE acknowledgement protocol.

An all-button ring requires a versioned event format including
controller/slot identity, event masks, ordering, overflow detection,
disconnection/reset semantics and either a sufficiently large
IOP-to-EE shared buffer or a supported RPC/DMA handshake. Adding an
unacknowledged OR-ed press mask would lose releases and simultaneous
ordering, repeating the weaknesses of the earlier 4 ms EE worker.

Before implementing that larger architecture, validate on physical
PS2 whether Square replay actually improves rapid punching *without*
undesirable extra delayed attacks or new input lag; then establish the
semantics for the other buttons. Do not generalize the Square FIFO
without an ordered source of events.

## Local test evidence

Ignored directories under this checkout (no `resources/` writes):
`build_issue15_iop_probe_fast/pcsx2_edge_gap.log`,
`build_issue15_iop_probe_fast/edge_gap_matrix.txt`,
`build_issue15_iop_probe_fast/pcsx2_replay.log`,
`build_issue15_iop_probe_fast/replay_matrix.txt`.

The replay and gap tracker have host unit tests for:
full invisible press/release cycles, intermediate release+press, frame
window reset, counter reset/discontinuity, ring ordering, maximum
capacity, overflow resynchronization and normal no-replay transitions.
