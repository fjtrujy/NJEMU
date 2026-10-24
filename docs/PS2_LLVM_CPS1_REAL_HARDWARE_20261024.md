# Real PS2Link CPS1 boot using LLVM R5900 — 2026-10-24

## Goal and safety rules

Test a *single* LLVM-built NJEMU CPS1 executable on the real PS2 at
`192.168.1.10`, avoiding the CPS2/MVS external IRX image and streaming
cache. Preserve the resident PS2Link IOP server: **never run
`ps2client reset`, `SifIopReset()`, `poweroff`, or any automatic second
`execee`**. Do not interpret an open network port as proof the EE launch
service is ready for another trial.

The earlier, distinct GCC PS2Link startup investigations are documented in
`docs/PS2_REAL_HARDWARE_BENCHMARK_ATTEMPT.md` and
`docs/PS2LINK_COMPILER_BENCHMARK_STARTUP_INVESTIGATION.md`.

## Diagnostic code and options

New default-OFF `PS2_STARTUP_TRACE` is only accepted for CPS1 PS2 compiler
benchmark builds with `PS2_BENCHMARK_PS2LINK=ON`. Its logging is removed
from normal GCC and canonical-release builds. Instrumentation covers:

- IOP RPC setup and synchronization, driver staging, filesystem/audio
  driver startup, with `SifIopReset` disabled by the existing protected mode.
- GS init and 240p setup, semaphores/handlers, CLUT allocation and all
  three initial clears.
- First GS queue, FINISH status/interrupts, GIF DMA status/interrupts,
  VBLANK callback counts and the initial flip. No console logging from
  interrupt context; callbacks only increment counters.
- ROM ZIP candidates, file stat/errno, CRC and member lookup (added **after**
  the first physical run below, for the next run), plus first-frame,
  warm-up and per-1,000-frame benchmark progress.

The trace uses explicit stdout flushes. It is **diagnostic** and unsuitable
for performance comparisons; timings from this build must not be treated
as a matched GCC-vs-LLVM compiler benchmark.

Both LLVM 24 and GCC 15.2 diagnostic binaries build with
`PS2_COMPILER_BENCHMARK=ON`, `PS2_BENCHMARK_PS2LINK=ON`,
`PS2_BENCHMARK_NO_INPUT=ON`, `PS2_BENCHMARK_MASS_BOOT=ON`,
`PS2_BENCHMARK_ROM=dino`, `PS2_STARTUP_TRACE=ON`, `GUI=OFF`,
`USE_CACHE=OFF`, `PS2_EXTERNAL_IRX_IMAGE=OFF`,
`PS2_FAST_CACHE=OFF`, `PS2_VIDEO_MODE=240p`, 30 warm-up and 300
measured frames. Both were checked at the compiled-object level:
`ps2_platform.c.obj` references `sceSifInitRpc` and `SifIopSync`
and contains **no `SifIopReset` relocation**.

## Single physical LLVM trial

- Branch: `feature/ps2-compiler-benchmark`, source HEAD before the
  diagnostic edits: `c092cc4a`.
- Host: Mac / installed Clang 24 revision
  `6f31245b7434ed1c55d341d544d10a7e8d003766`.
- Launch: `ps2client -h 192.168.1.10 execee host:CPS1.ELF`,
  accompanied by a local `listen` process.
- Enabled PS2Link network exception reporting using
  `ps2client -h 192.168.1.10 -t 3 netdump` before launching.
  No exception dump was observed; netdump is not a substitute for a
  successful benchmark record.
- Immutable ELF:
  `build_ps2_link_llvm_cps1_dino_20261024/hardware_single_trial/CPS1.ELF`,
  SHA-256 `445d37018797428f0f7a4a8011f59958d3f94c87eb6f5f684fbf283d445ada5c`.
- Complete raw console capture:
  `build_ps2_link_llvm_cps1_dino_20261024/hardware_single_trial/listen.log`
  (4,612 bytes). The `launch.log` and `netdump-config.log` are empty.

**Observed success:** the ELF was loaded and began running on physical
R5900/PS2. It changed cwd to `mass:/NJEMU/CPS1`, completed the PS2Link-
protected IOP/RPC path, filesystem and audio driver startup, 240p GS setup,
CLUT allocation, three clears, the GS queue submission, GIF DMA, FINISH and
VSYNC fences, and the first `gsKit_flip`. The first flip finished with
`finish_irqs=1 gif_irqs=2 vsync_irqs=2`. The prior intermittent pre-ROM
GS stall did **not** reproduce in this execution.

**Actual failure:** immediately after selecting `dino`, console output
reported:

```
[ps2-benchmark] USB ROM selector: dino
Checking ROM info...
File not found. "cde_23a.rom"
Press any button.
NJEMU_BENCHMARK_STATE_PARTS_V1 m68k=0000000000000000 z80=0000000000000000 ram=0000000000000000
```

No ROM-loaded result, first-frame marker or `NJEMU_BENCHMARK_V1` record.
The console later stopped producing output after
`audsrv_adpcm_init()` during/after the unsuccessful game startup.
This is not evidence of a completed or valid hardware benchmark. The reason
for the ROM failure is **not yet established**.

The local, unmodified `resources/cps1/roms/dino.zip` **does** contain
`cde_23a.rom` and `unzip -t` passes. Therefore the diagnostic does not
prove the console's USB archive is incomplete: the USB file may differ from
the local archive, its path or access may fail, or ZIP CRC/reader behavior
may differ on this target. No USB filesystem is mounted on the Mac and the
console's archive could not be inspected.

The operator terminated **only the two local ps2client capture processes**
after the log stopped growing. No reset or automatic relaunch was sent.
The TCP PS2Link port 18193 was not reachable afterward. We cannot infer
from this alone that the IOP reset (the protected platform object proves
only that NJEMU's own IOP reset call was compiled out).

## Follow-up built locally — NOT launched

The source was further instrumented at `src/common/loadrom.c:file_open`
to report every attempted ZIP path, USB `stat` result and errno, archive
open, requested CRC, matching ZIP member and member-extraction result. These
checks execute only with `PS2_STARTUP_TRACE=ON`, before benchmark timing.

Both protected builds compile/link successfully:

- LLVM:
  `build_ps2_link_llvm_cps1_dino_20261024/CPS1.ELF`, SHA-256
  `1e181be19c06e1eaa8b16a76b9afb6aa53d045e143f31ba81defccdfb0dc0b39`.
- GCC:
  `build_ps2_link_gcc_cps1_dino_20261024/CPS1.ELF`, SHA-256
  `7564782b153ca711faf21508c4a8b9884d5e98bdf07dd2271cbc17f195c0aab6`.

Both platform objects were re-audited for IOP reset relocations.
The host CTest suite passes 4/4 and the release build concurrency tests
pass for Desktop, PSP, PS2, Vita and override isolation. A CMake negative
test confirms `PS2_STARTUP_TRACE=ON` without the PS2Link-preserving
benchmark flag is rejected.

### Next physical test prerequisites

1. **Manually rearm the console's PS2Link EE launcher.** Do not send
   `ps2client reset` or start a second program while the previous EE
   application is active. Check both the display and client readiness.
2. Inspect the real USB `mass:/NJEMU/CPS1/roms/dino.zip` if accessible,
   without modifying the source repo or its `resources/`.
3. Run **exactly one** fresh protected LLVM ELF, preferably the new
   instrumented variant above, with `listen` and `netdump` configured
   beforehand; keep an immutable ELF and complete console logs. Do not
   run the GCC follow-up in the same unverified EE session.
4. If ZIP path and CRC operations now succeed, verify that game loading,
   initial emulated frame, all 30 warm-up and 300 measured frames execute.
   Only then attempt repeatable GCC/LLVM hardware performance trials.

The canonical release toolchains, default PS2 linker, ROM files and
`resources/` were not changed.
