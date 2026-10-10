# PS2 IOP Audio Offload: Deferred Feasibility Plan

Status: Deferred by request (2026-10-10). This document preserves the design discussion for a future investigation. No IOP offloading is implemented or approved.

## Current NJEMU audio pipeline

1. On PS2, the common sound thread runs the CPU audio producer on the Emotion Engine (EE).
2. The emulated sound CPU (for example the MVS Z80) runs on the main emulation timeline. The sound producer performs chip synthesis (for example YM2610 or QSound), followed by conversion/clipping/resampling into stereo PCM.
3. `src/ps2/ps2_audio.c` applies game volume, optionally mixes the NCDZ MP3/CDDA ring, and sends completed PCM through PS2SDK's `audsrv` to the IOP and SPU2.
4. `PS2_AUDIO_DIRECT_SUBMIT` and prefix-aware partial-write retries already reduce synchronous audio delivery latency while preserving accepted PCM bytes. Do not bypass this validated path without a controlled A/B.

Relevant code: `src/common/sound.c`, `src/common/audio_producer_cpu.c`, `src/sound/sndintrf.c`, `src/ps2/ps2_audio.c`, `src/ps2/ps2_audio_submit.c`, and the target-specific sound-chip implementations.

## Evidence collected so far

The physical PS2 Metal Slug 3 save-state investigation, documented in `docs/PS2_MSLUG3_FRAME_PROFILE.md`, measured:

- An audio buffer of 1,472 stereo sample frames at 44,100 Hz represents about 33.378 ms of playback.
- The EE sound producer averaged around 8.1 ms per buffer in the validated direct-submit comparison. PCM post-processing/volume mixing was comparatively small.
- The direct-and-retry path reduced the blocking `audsrv` output stage to about 25.9 ms per buffer versus about 35.7 ms in the earlier legacy comparison. These are **elapsed wall-clock intervals containing waits and SIF RPCs**, not exclusive EE computation time.
- Short audio submissions were fully retried in the measured direct-submit run. However, no reliable IOP/SPU2 underrun counter or playback cursor trace exists yet.
- Disabling the EE audio thread changes scheduling, but this alone does not establish that moving YM2610 synthesis to IOP would improve emulation speed.

## Architectural constraints

The IOP has a much smaller memory and compute budget than the EE and already services SPU2 playback and other drivers (controllers, storage, USB/MX4SIO where relevant). Simply porting an EE synthesizer to the IOP may run slower or interfere with I/O.

Moving a workload would replace some full-PCM transfers with command, timing, sample-data and state synchronization; it would not eliminate communication between processors. Sound-register timestamps, YM2610 timer/IRQ fidelity, Z80 scheduling, pause/reset, suspend/resume, save states and sound-option fallbacks must be preserved.

Keep the PSP Media Engine architecture independent: this proposal is **PS2-only** and must not introduce IOP assumptions into generic audio-producer or sound-chip code. Preserve the existing EE audio backend as a user-selectable or build-selectable fallback during experimentation.

## Proposed investigation stages

1. **Measure producer-consumer behavior.** Add opt-in, low-overhead IOP-side audio-ring occupancy/underflow instrumentation with aggregated retrieval rather than high-frequency EE RPC polling. Distinguish a truly empty SPU2/audsrv playback queue from slow EE producer iterations.
2. **Measure available IOP headroom.** Run a deliberately bounded IOP benchmarking module under actual game and storage workloads, including MX4SIO if available. Record CPU service times, IOP RAM use, driver stability, audio glitches, and controller/input latency.
3. **Improve overlap before migrating synthesis.** Compare the present direct-submit path against a carefully synchronized asynchronous/pipelined transport that permits useful EE work while IOP handles audio delivery. Establish queue ownership, sequencing and backpressure, and measure physical-hardware A/B.
4. **Prototype one isolated workload only if there is headroom.** Candidate examples are bounded sample decoding, resampling or a specifically proven synthesis component. Use batched timestamped commands and a reference EE output oracle. Compare PCM hashes/tolerance, emulated register state, interrupts and timing at reset/load/seek boundaries.
5. **Consider SPU2 capabilities separately.** Hardware sample voices/mixing may be useful for certain sample-based cases, but cannot be assumed to reproduce the FM/YM2610 or QSound model without additional validation.

## Success / stop criteria

- Demonstrable reduction in EE *active* CPU time or sustained work-budget misses during frame-ID-matched physical PS2 gameplay, not merely shorter elapsed `audsrv` calls.
- No increases in verified audio underruns, lost samples, controller latency, storage/cache failures or IOP resets.
- PCM/audio behavior correct against an EE reference across multiple cores and representative titles; save states and fallback remain intact.
- If headroom or fidelity is insufficient, stop the IOP migration and keep the already validated EE/IOP design.

This is a future plan only. No experiments or emulator/menu behavior should be changed solely by saving it.
