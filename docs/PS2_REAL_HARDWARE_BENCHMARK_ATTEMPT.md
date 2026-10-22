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

## Inputless PS2Link hardware retry and video diagnostics (2026-10-22)

To isolate the PADMAN/MTAP startup problem from the actual emulator workload, `PS2_BENCHMARK_NO_INPUT=ON` was added as an **opt-in, headless PS2 compiler-benchmark-only** CMake option (rejected for other platforms, GUI builds, and non-benchmark builds). When selected, `pad_init()` initializes common neutral pad state without calling the physical input backend; the normal gameplay and non-benchmark paths remain unchanged. Both GCC 15.2 and LLVM `r5900-phase3-mips4` were rebuilt for this configuration, with `PS2_BENCHMARK_PS2LINK=ON`, 240p video, sound on and cache off. The LLVM build still uses four explicit GCC fallback objects for unrelated unsupported R5900 floating-point predicates. Both protected PS2Link platform objects have no `SifIopReset` reference.

- **Offline control succeeded.** A separate ordinary-IOP GCC inputless binary completed 500 warm-up + 10,000 `dino` measured frames in PCSX2 v2.9.93, with `total_us=64124733`, `io_ops=0` and `state_hash=14e604d23dc83216` (matching the original GCC `dino` final-state hash). The normal-IOP build must not be launched through a resident PS2Link server. The inputless run is not timing-comparable to the older pad-enabled benchmark unless both compiler builds use the new identical inputless configuration. Logs: `build_ps2_compiler_ps2link_20261022/pcsx2_inputless_gcc_dino/`.
- **One physical inputless GCC trial started successfully**, using an immutable copy of the protected 240p binary (SHA-256 `8b43c7950e493a984ff2728322c35be85575dab54eec7ecd473ab7557a8f9b71`). The PS2 log shows `[ps2-benchmark] physical controller initialization skipped`, then the successful `emumain.c:694` marker, followed by `CLUT VRAM: 0x230000`. Output stopped there before `ROM set "dino"` or any `NJEMU_BENCHMARK_V1` record. This confirms that skipping input resolved the immediate **pad-initialization stall** on this test but exposed/reproduced the separate initial-video stall. Logs: `build_ps2_compiler_ps2link_20261022/hardware_gcc_cps1_dino_001/` (a rejected zero-frame trial).
- A separate immutable **startup-instrumented GCC PS2Link ELF** was built with markers around first clears and GS submission/wait. Before making it, the current user-owned video source was copied to `build_ps2_compiler_ps2link_20261022/video_user_before_diag.c` and was restored byte-for-byte after the build. The immutable diagnostic ELF has SHA-256 `06cb8b8f786af0c803c95d56107c184923441ef9f0afc661182e38cff0ed30fc`. It has no IOP reset call. Unfortunately the subsequent attempted hardware launch yielded **zero** loader/output bytes, despite an open PS2Link TCP port. The previous EE session may not have been recovered. This is *not* evidence that the diagnostic reached the GS code. Log: `build_ps2_compiler_ps2link_20261022/hardware_gcc_video_startup_diagnostic_001/`.
- Both launch clients were stopped by terminating their **host processes only**: no `ps2client reset`, no remote IOP reset, and no automatic retries were issued. Afterward PS2Link TCP port 18193 still accepted connections, but this does not prove EE-side launch readiness. Before using the already-built diagnostic, the physical PS2Link EE launch service needs to be restarted/rearmed in a way that **does not send the console to the OSD**. Never equate a successful TCP connection with this readiness check.

**No valid real-PS2 compiler timings were obtained.** The next useful physical action is **a single launch of the already-built protected and startup-instrumented ELF after a confirmed PS2Link EE restart**. The markers should then distinguish whether initial clears, GIF DMA, GS FINISH, VSYNC, or queue submission is blocking. Keep startup diagnostics outside measured frames; do not change GS synchronization based only on the last uninstrumented CLUT log. The full GCC/LLVM matrix should not be launched until an individual real-console test reliably completes and the next-launch lifecycle is proven safe.

## Fresh PS2Link diagnostic — video initialization and ROM loading succeed (2026-10-22)

After the user restarted PS2Link and confirmed readiness, one immutable, protected **GCC CPS1 `dino` startup-diagnostic** ELF was launched. The exact artifact is `build_ps2_compiler_ps2link_20261022/immutable_hardware_gcc_video_startup_diagnostic/CPS1.ELF`, SHA-256 `06cb8b8f786af0c803c95d56107c184923441ef9f0afc661182e38cff0ed30fc`. The build uses 240p video, `PS2_BENCHMARK_PS2LINK=ON`, `PS2_BENCHMARK_NO_INPUT=ON`, 500 warm-up and 10,000 measured frames; its platform object has **no IOP reset relocation**. The user-owned uncommitted `src/ps2/ps2_video.c` is byte-identical to its original pre-diagnostic backup; only the copied ELF contains diagnostic markers.

This time, the real PS2 printed successful startup markers all the way through the initial GS queue, GIF submission, FINISH fence, and flip:

```
[video-diag] before initial-flip
[video-diag] flip first wait CSR=551b600e first=0
[video-diag] submit before GS wait first=0 tag=121 csr=551b600e
[video-diag] submit before GIF wait
[video-diag] submit before GIF send
[video-diag] submit GIF issued mode=1
[video-diag] flip GS finish complete
[video-diag] after initial flip
Checking ROM info...
ROM set "dino"
```

It subsequently printed all ROM loads, initialized audio and ended the captured startup log with `Done.` **This disproves a consistent GS-video startup deadlock** with this guarded binary and confirms both pad bypass and real-console game loading are possible; it does not explain the earlier intermittent hangs. The last output was *not* a benchmark result.

**Runner timeout mistake:** The supervised host invocation used a **90-second timeout**, which covered initial startup and ROM loading but not the additional 10,000 measured frames. The benchmark host runner stops its `ps2client execee` and `listen` children and deletes the private runtime staging directory when that timeout elapses. The resulting CSV correctly contains `missing benchmark record` and **zero valid measured frames**, even though the game finished loading on the console. An attempt to reconnect **only** `ps2client listen` without relaunching or resetting the console yielded no new output. Reconnection was stopped; PS2Link TCP port 18193 still accepted connections, but that does not establish that the remote EE program is ready for the next launch.

- Captured startup trace: `build_ps2_compiler_ps2link_20261022/hardware_gcc_video_startup_diagnostic_002/CPS1_dino_GCC_001.log`.
- The corrected next **single-trial** hardware invocation should use a **900-second host capture timeout** to encompass network ROM loading plus 10,000 emulated frames, and should begin only after the real console's PS2Link EE launcher has been rearmed; **do not issue an automatic remote `reset`, and do not hot-launch an ELF into an active EE program**. The 900 seconds is only a host safety timeout, *not* a performance measurement. Record the complete `NJEMU_BENCHMARK_V1` line and verify `frames=10000`, I/O, ROM, compiler, state, and source provenance before accepting it.
- No physical LLVM phase3 ELF has been executed yet. Do not publish a physical GCC/LLVM comparison before independently completing both compiler configurations, with separate verified PS2Link launch sessions.

## Offline follow-up and confirmed diagnostic build error (2026-10-21)

The second diagnostic ELF was **inadvertently rebuilt with `SifIopReset()` active**, after reverting the temporary platform source guard. This has now been confirmed by the GCC platform object's linker relocation and explains the loss of PS2Link during that diagnostic launch much more concretely than the earlier reset speculation. The original first trial, which got past IOP initialization to CLUT allocation, remains a **separate unproven GS/video hang**. Its archived ELF is a symlink into a subsequently overwritten build, not a preserved snapshot. Earlier successful physical tests were 240p; the failed first run was 480i and used a corrected GS FINISH predicate that reveals a precedence bug in earlier source. Both video modes pass short offline PCSX2 tests. See [PS2Link Startup Offline Investigation](PS2LINK_COMPILER_BENCHMARK_STARTUP_INVESTIGATION.md) for logs, C99 predicate demonstration, the new default-OFF `PS2_BENCHMARK_PS2LINK` build guard, safe rebuilt 240p candidates, and the requirement not to perform unattended hardware relaunches.
