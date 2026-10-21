# PS2Link Compiler Benchmark Startup — Offline Root-Cause Investigation

Date: 2026-10-21. Scope: physical CPS1 startup failures with the compiler benchmark. **No new PS2 hardware execution was performed during this follow-up.** The active NJEMU source branch was `feature/ps2-compiler-benchmark`.

## Findings: two distinct failures

### Diagnostic relaunch: confirmed unsafe executable

The second GCC CPS1 diagnostic ELF, `build_ps2_compiler_ps2link_20261021/VIDEO_INIT_DIAGNOSTIC_CPS1.ELF`, was built **after** the PS2Link-specific skip of `SifIopReset()` had been removed from the tracked source. Rebuilding the diagnostic after editing `src/ps2/ps2_video.c` also rebuilt `src/ps2/ps2_platform.c`, silently restoring an IOP-reset call. The installed toolchain's `mips64r5900el-ps2-elf-objdump -dr` confirms that the matching platform object contains an `R_MIPS_26 SifIopReset` relocation. In contrast, the phase3 and other previously guarded PS2Link platform objects do **not** reference `SifIopReset()`.

This is a concrete build mistake and **strongly explains** the diagnostic's abrupt stop shortly after the NJEMU startup/version banner, followed by loss of the PS2Link server and a return to the console OSD. It is not evidence of an LLVM compiler issue; both physical attempts ran **GCC** executables.

The first trial's archived `ps2-stage-*/CPS1.ELF` is a **symlink back to the mutable build output**, not an immutable snapshot. It now resolves to the later unsafe diagnostic ELF and cannot prove the contents of the earlier binary. During the original trial it reached CLUT allocation, indicating it was not interrupted at the earlier IOP setup. Do not claim its original ELF bytes can still be recovered from that symlink.

### Original GCC startup stall: GS completion vs video-mode hypothesis

The first real-hardware CPS1 `slammast` attempt printed through:

```
CLUT VRAM: 0x2e0000 (banks=1, entries/bank=3072, height=12, size/bank=32768)
```

The next operations in `ps2_init()` are three `video_driver->clearFrame()` calls and the initial `ps2_flipScreen()`. The queue submission and GS FINISH / GIF DMA fence are therefore an obvious suspect. The trace **does not establish exactly which instruction blocked**: there were no intermediate print markers or PS2 EE exception details.

Two relevant differences from earlier **successful physical CPS1 PS2Link** tests (`build_issue15_hw_nogui_cps1_240p/issue15_native_hardware_trace.log`, described in `docs/PS2_ISSUE15_NATIVE_OUTPUT_AND_INPUT_FOLLOWUP.md`) are:

1. Older real-hardware builds were explicitly **240p** (`PS2_VIDEO_MODE=240p`) and loaded the ROM after reporting `CLUT VRAM: 0x230000`. The failed compiler benchmark defaulted to **480i** (`CLUT VRAM: 0x2e0000`).
2. The newer, user-preserved video source changed `while (!GS_CSR_FINISH)` to `while (!(GS_CSR_FINISH))` in `gsKit_wait_finish()`. The installed gsKit header defines `GS_CSR_FINISH` as `((*GS_CSR) >> 1)&1` (without surrounding parentheses). Therefore **the old source had a precedence bug**: the `!` applied only to the shifted CSR before `& 1`. For CSR values `0x4`, `0x8`, `0x10`, or `0x8000` (FINISH clear, other status bits set), the old wait expression is false and immediately exits, while the corrected expression remains true and waits. A standalone C99 repro confirms this in `build_ps2_compiler_ps2link_20261021/offline_mode_matrix/gs_csr_wait_expr.c`. That old premature exit may have hidden an existing FINISH interrupt/completion fence issue. **Do not restore the incorrect expression as a production fix.**

The GS interrupt wait can block if the registered INTC_GS callback does not signal the semaphore, or if FINISH is cleared/missed while waiting. The exact hardware event ordering is unobserved. The change in interlaced video mode and the stricter FINISH test are **hypotheses, not proven root causes** of the original stall.

## Offline validations performed

- Built fresh GCC CPS1 **240p** and **480i** benchmark smoke configurations (sound on, cache off, warm-up 30, measured frames 300) in separate ignored directories under `build_ps2_compiler_ps2link_20261021/offline_mode_matrix/`. Both loaded `slammast` and generated complete `NJEMU_BENCHMARK_V1` records in PCSX2. This confirms that neither 240p nor 480i inherently fails under PCSX2; the physical-console wait remains unverified.
- Confirmed the original unsafe GCC platform object included an `SifIopReset` relocation; a PS2Link-guarded object excluded it. Implemented a fail-safe **CMake opt-in** `PS2_BENCHMARK_PS2LINK=ON`, default OFF, valid only on PS2 compiler benchmarks with `PS2_EXTERNAL_IRX_IMAGE=OFF`. This opt-in preserves the IOP across incremental rebuilds and keeps `SifInitRpc()`/`SifIopSync()`. The old inert `PS2_LINK_BENCHMARK` manual C flag now triggers a compile-time error unless the proper option is enabled, so it cannot silently produce an unsafe ELF.
- CMake configuration for non-PS2/normal builds with that flag is rejected; external IRX images are rejected; normal GCC CPS1 objects retain their IOP reset call; PS2Link GCC CPS1 objects lack it. The protected build was rebuilt twice with its option still in CMake cache and retained the correct object relocation state.
- The host PS2Link runner now rejects automatic multiple hardware trial launches. The ROM selection and PCSX2 runner continue to work independently. Each real-console launch must be a **separate verified session**, with logs preserved and no automatic `ps2client reset` or unattended relaunch after a failure. Host tests cover this rejection.
- Prepared fresh standalone 240p, 500 warm-up + 10,000-frame GCC and LLVM `r5900-phase3-mips4` CPS1 ELFs under `build_ps2_compiler_ps2link_20261021/safe_240p_10k/{gcc_cps1,phase3_cps1}/`. Both have **zero `SifIopReset` relocations in their platform object**. The phase3 candidate uses the exact same four GCC fallback units as before, but now copied from its matching fresh GCC build, preserving per-unit source and PS2Link compile-option parity. Neither binary has been launched on real hardware.
- All four host tests pass. No original ROM resources, stash contents, or the existing user-owned changes in `src/ps2/ps2_video.c` were modified.

## Safe next investigation

### Full-length 240p offline control (2026-10-21)

In addition to the short 30+300-frame mode checks above, a fresh **normal-IOP 240p GCC CPS1** build completed **500 warm-up + 10,000 measured `slammast` frames in PCSX2 v2.9.93**. The log is retained at `build_ps2_compiler_ps2link_20261021/control_240p_pcsx2_results/CPS1_slammast_GCC_001.log`, with its own CSV and summary. It reports:

- `CLUT VRAM: 0x230000` and successful `ROM set "slammast"` / audio initialization.
- `total_us=65541527`, `frames=10000`, `io_ops=0`, `sound=on`, `cache=off`, `resident=on`, `status=unverified`.
- `state_hash=cc8190d50bb2133f`, matching the recorded GCC PCSX2 `slammast` baseline; configuration fingerprint is different because this is a **240p** control rather than the earlier **480i** matrix.

This confirms that the updated benchmark workload can reach completion using a 240p GS mode in PCSX2. It does **not** establish that 480i caused the physical hang. Another deliberately PS2Link-guarded 240p ELF stalled shortly after its startup banner when run in PCSX2, as expected from its reliance on the resident PS2Link IOP environment: **PCSX2 cannot validate the PS2Link IOP lifecycle with this setup**. That experiment was stopped; no physical-console launch was made. The two environments must not be conflated.

The protected 240p GCC and phase3 hardware candidates in `safe_240p_10k/` were rechecked. Both have `PS2_BENCHMARK_PS2LINK=ON`, `PS2_EXTERNAL_IRX_IMAGE=OFF`, `PS2_VIDEO_MODE=240p`, and **no `SifIopReset` relocation** in their platform objects. Neither has been executed on the real PS2. The first physical attempt's original ELF is still unrecoverable as an immutable file because the runner staged it as a symlink to a subsequently rebuilt output.

1. Keep hardware idle until a supervised **single** test is arranged with the user. Start from a protected GCC **240p** candidate, matching the previously working video configuration. Do not automatically reset the console or hot-launch the next ELF. Inspect IOP reset relocations again before that session.
2. If startup still stalls after CLUT, instrument **only startup**, before the timed frame region, with markers before/after each clear and GS submit/wait; capture GS CSR FINISH, GS IMR, GIF DMA CHCR, `FirstFrame`, and callback/semaphore completion counts. Avoid logging from interrupt context. Ensure a missed interrupt cannot trigger repeated unattended launches. Prefer determining the failing fence over reverting an actual GS completion check.
3. When a first entire hardware trial succeeds and PS2Link can be returned to a usable state reliably, schedule the remaining 30-trial GCC/phase3 matrix with **one supervised trial per console session**, preserving exactly matched ROM, sound, cache, video mode, sources and compiler provenance.

**There are still zero valid real-PS2 benchmark timings. PCSX2 timing is not a substitute for physical-console results.**
