# PS2 Real-Hardware Compiler Benchmark — Incomplete Attempt

Date: 2026-10-21. Branch: `feature/ps2-compiler-benchmark`.

## Status

**STOPPED — no valid measured frames, no hardware GCC or LLVM timing baseline.**

The physical PS2 running PS2Link was reached at `192.168.31.10:18193`. New GCC and `r5900-phase3-mips4` PS2Link-specific benchmark ELFs were built successfully for CPS1, CPS2 and MVS. The phase3 builds retain exactly the same 4/3/4 GCC-compiled fallback objects as the previous PCSX2 hybrid benchmark because the LLVM R5900 floating-point comparison lowering is still unsupported by the assembler.

The first GCC CPS1 `slammast` launch loaded via `ps2client execee host:CPS1.ELF` and printed platform initialization through:

```
CLUT VRAM: 0x2e0000 (banks=1, entries/bank=3072, height=12, size/bank=32768)
```

The log then stopped growing. This line occurs inside PS2 video initialization, immediately before the initial frame clear and GS submission/finish waits. A **video initialization hang or crash is suspected but unproven**; no exception trace was captured. The benchmark never reported a ROM set or `NJEMU_BENCHMARK_V1`.

A second, instrumented launch (without `ps2client reset`) printed only through the application version/startup line, then failed to produce further output. The console returned to the OSD. The user manually restarted PS2Link twice. Both aborted logs were retained under the ignored `build_ps2_compiler_ps2link_20261021/` directory, including `hardware_gcc/cps1-slammast/incomplete_video_init_20261021/`, `hardware_video_diagnostic/`, and `NO_RESET_SINGLE_DIAGNOSTIC.log`. **No timed hardware measurements exist and none may be reported as a compiler comparison.**

## PS2Link-specific requirements and safety

1. **Do not reset the IOP in the benchmark ELF.** The PS2Link-specific ELF builds were compiled with a temporary `PS2_LINK_BENCHMARK=1` guard around `SifIopReset(NULL, 0)`; `SifInitRpc(0)`, `SifIopSync()` and the normal PS2 driver initialization remain. The guard was restored out of the tracked source after building, so production and PCSX2 builds are unaffected. Previous successful PS2Link testing is documented in `PS2_ISSUE15_NATIVE_OUTPUT_AND_INPUT_FOLLOWUP.md`.
2. **Do not automatically reset or relaunch the physical PS2 after a failure.** An experimental runner change that invoked `ps2client reset` before each attempt was tested, but the sequence did not provide stable recovery; a following test without the reset also returned to the OSD. Thus it is incorrect to claim that the reset alone caused the failures, or that avoiding the reset resolves the crash. The experimental automatic-reset change was **fully reverted**. Do not launch a multi-trial hardware matrix until a single controlled startup, game load and measured run have succeeded and the inter-trial lifecycle is proven.
3. Log every run independently and preserve failed traces; never accept a run without its full `NJEMU_BENCHMARK_V1` record and expected frame count. Do not write to user ROMs under `resources/` or overwrite existing GCC/LLVM PCSX2 aggregate reports.
4. Avoid unattended EE launches during diagnosis: both observed failures forced the user to reopen PS2Link, and the existing runner lacks a validated method to recover the console autonomously.

## Offline artifacts and next investigation

- GCC PS2Link build roots: `build_ps2_compiler_ps2link_20261021/gcc_{cps1,cps2,mvs}/`.
- LLVM phase3 PS2Link hybrid build roots: `build_ps2_compiler_ps2link_20261021/phase3_{cps1,cps2,mvs}/`.
- Prebuilt diagnostic binary (not a valid measurement): `build_ps2_compiler_ps2link_20261021/VIDEO_INIT_DIAGNOSTIC_CPS1.ELF`.
- The user-owned modified `src/ps2/ps2_video.c` was backed up and byte-for-byte restored after diagnostic compilation, and remains uncommitted/untouched by the investigation. The diagnostic-only patch to `src/ps2/ps2_platform.c` and automatic-reset change to the runner were also fully restored. No ROM resources or user stashes were changed.

Before any renewed physical attempt: inspect the GS finish, VSYNC and GIF DMA synchronization during the *initial* video clear/flip, and validate any proposed fix offline in PCSX2 without changing the timed emulation workload. Reproduce only with a manually supervised, **single** hardware startup after explaining the plan to the user. A further OSD transition must end testing again, without automatic relaunch.

**The ten-ROM / three-trial real-hardware GCC-vs-phase3 result is pending; do not substitute PCSX2 timings for physical PS2 results.**

## One protected 240p GCC hardware retry (2026-10-21)

With the user explicitly confirming PS2Link was ready, **one** additional supervised `CPS1`/`dino` GCC hardware trial was launched (500 warm-up + 10,000 measured frames; sound on, cache off). The private immutable ELF copy had SHA-256 `d8e417561e46e7197203edd9cbc6f1e2272b2b7e7294fdec9c25e7e1d1facdc2`. The CMake source build was 240p, `PS2_BENCHMARK_PS2LINK=ON`, external IRX image off. The platform object was independently verified to reference `SifIopSync` and `sceSifInitRpc`, but **not `SifIopReset`**. Neither `ps2client reset` nor a second automatic ELF launch was performed.

The real console successfully loaded the ELF and printed startup messages through:

```
===> main, .../src/emumain.c:689
FREEMTAP v1.0
Pad Driver for OSD
```

The host log then **stopped growing for over 90 seconds**, without reaching the post-`pad_init()` message at `emumain.c:694`, CLUT allocation, `ROM set "dino"`, or any benchmark record. The launch client was stopped (without issuing an IOP reset or any additional remote ELF launch). PS2Link remained reachable on TCP port 18193 afterwards. The sole captured log and generated rejected-trial CSV/summary are preserved under `build_ps2_compiler_ps2link_20261021/hardware_gcc_240p_dino_supervised_20261021/`. The reporter correctly recorded **0 frames and `missing benchmark record`**, not an elapsed-time measurement.

This latest block occurs **inside or immediately around pad initialization**, before video initialization, not at the older GS/CLUT wait. `pad_init()` delegates to `ps2_input.c:ps2_init()`, whose first external operation is `init_joystick_driver(true)` in the precompiled `ps2_drivers` library, followed by multitap initialization and pad polling. The PADMAN/MTAPMAN/PS2Link IOP interaction is now a better **hypothesis for this particular retry**, but the last stdout line does not prove the precise blocked function. Do not conflate this with the earlier trial that reached CLUT allocation, or imply that the video-mode change solved the full problem.

The hardware tester has been left idle: no PS2Client process remains, the user-owned `src/ps2/ps2_video.c` edit remains unchanged, and **zero successful physical benchmark trials** have been collected. Before another physical launch, instrument the pad-init path only (outside timing), or evaluate a benchmark-only inputless attract-mode boot that uses exactly the same configuration for both compiler builds. Do not relaunch unattended while the console lifecycle remains unproven.

## Offline follow-up and confirmed diagnostic build error (2026-10-21)

The second diagnostic ELF was **inadvertently rebuilt with `SifIopReset()` active**, after reverting the temporary platform source guard. This has now been confirmed by the GCC platform object's linker relocation and explains the loss of PS2Link during that diagnostic launch much more concretely than the earlier reset speculation. The original first trial, which got past IOP initialization to CLUT allocation, remains a **separate unproven GS/video hang**. Its archived ELF is a symlink into a subsequently overwritten build, not a preserved snapshot. Earlier successful physical tests were 240p; the failed first run was 480i and used a corrected GS FINISH predicate that reveals a precedence bug in earlier source. Both video modes pass short offline PCSX2 tests. See [PS2Link Startup Offline Investigation](PS2LINK_COMPILER_BENCHMARK_STARTUP_INVESTIGATION.md) for logs, C99 predicate demonstration, the new default-OFF `PS2_BENCHMARK_PS2LINK` build guard, safe rebuilt 240p candidates, and the requirement not to perform unattended hardware relaunches.
