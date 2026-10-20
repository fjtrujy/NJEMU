# PS2 compiler benchmark plan

Status: implementation in progress. Target: PS2 Emotion Engine / R5900; CPS1, CPS2, and MVS only. This is a development benchmark, not a canonical release configuration.

## Goal and experiment contract

Compare builds in which **only NJEMU-owned source objects** are compiled by GCC or experimental R5900 LLVM. PS2SDK, gsKit, ps2_drivers, startup objects, libc, archives and the final linker driver remain GCC-provided. Never infer real-hardware performance from PCSX2 wall time. Benchmark the original sound-enabled, rendering-enabled game loop, not a synthetic CPU loop.

The default workload is 500 unmeasured warm-up frames then exactly 10,000 measured emulated frames, with VSync off, limiter off, automatic/manual frame skipping off/zero, overlay off, and sound on. Identical physical output, audio samplerate, input policy and game settings are required between compiler runs. Save-state deserialization and ROM/processed-asset loading must finish *before* warm-up and timing. Standard attract mode is a provisional workload only; a verified in-memory replay or state fixture is required for cross-compiler comparability.

A run is accepted only when the complete ROM and audio samples are resident, the measured interval contains zero forbidden filesystem operations, both variants execute the same state trajectory, the number of frames is exact, and the measurement metadata agrees. An unsupported dependency, failed allocation, cache fallback, hash divergence, exit prior to completion, or log parse error is **ineligible/failed/incomparable**, never a speedup.

## Repository audit (2026-10-20)

- `src/emumain.c:update_screen()` is called once per emulated frame by CPS1, CPS2 and MVS after `timer_update_cpu()`. Frame pacing/presentation finishes in `update_screen()`. Instrument at this common frame boundary; do not create an alternate emulation loop. The Desktop `NJEMU_TEST_FRAME_LIMIT` is a separate test-only mechanism.
- `ticker_driver->currentUs(ticker_data)` exists on PS2 via the SDK system timer and can return microseconds. Quantiles should be computed once *after* timing from fixed-size frame samples; avoid console formatting in the timed loop.
- `src/common/no_gui.c` selects the ROM from `game_name.ini`, then calls `emu_main()`. Load-time configuration is reapplied inside `memory_init()` via `load_gamecfg()`; benchmark overrides must come after this config reload but before sound/CPU initialization.
- CPS1 has no streaming cache; each candidate still needs successful full ROM allocation and repeatable scene verification.
- CPS2 `load_rom_gfx1()` tries an empirical full-region allocation and `load_rom_gfx1_full_resident()`; with `USE_CACHE=ON` it may instead enter `cache_start()`. Benchmark with `USE_CACHE=OFF` and verify the selected allocation shape and load success.
- MVS `memory_init()` retains an empirical C-ROM/PCM allocation plan; `load_rom_gfx3()` supports processed assets or streaming and `load_rom_sound1()` can allocate resident PCM. The benchmark must reject `disable_sound` and any PCM-demand-read state. `USE_CACHE=OFF` removes demand-read caching, not processed-asset loading. Encrypted sets may still need preprocessed files.
- `NJEMU_CANONICAL_RELEASE=ON` forces cache on for PS2 CPS2/MVS and is incompatible with this benchmark. Use development builds with `NJEMU_CANONICAL_RELEASE=OFF`. PS2 currently enables global CMake LTO; mixed LLVM/GCC requires it disabled or made toolchain-aware.
- Prior `PS2_MSLUG3_FRAME_PROFILE` switches were experimental and are *not* assumed available. Its timings show that CPU, rendering, cache and GPU stages overlap; never sum nested durations.
- Unrelated local HDD change in `src/ps2/ps2_platform.c` and user-owned files under `resources/` must remain untouched.

## Candidate and qualification matrix

The authoritative file-driven candidates live in `benchmarks/ps2/roms.tsv`. Remaining `unverified` entries are **unqualified** until per-ROM PS2 allocation proof, 10,500-frame completion, zero-I/O audit and deterministic state comparisons exist. Two candidates have already been ruled out by their minimum ROM-region requirements alone. These are not expense rankings. Keep raw allocation values, firmware/BIOS identity, processed asset digests and evidence per game in separate result manifests, not guessed in the source matrix.

| Core | Candidates | Starting hypothesis | Mandatory evidence |
| --- | --- | --- | --- |
| CPS1 | slammast, dino, punisher, sf2hf | No streaming-cache backend; CPS1 QSound and heavy sprite scenes may stress EE | Complete CPU/GFX/audio allocations, attract-mode suitability, stable state signature |
| CPS2 | avsp, 19xx, ssf2t, sfa3ur1 | Decoded graphics size may exceed PS2 heap on larger sets | Empirical `gfx_fully_resident`, all PCM allocated, `USE_CACHE=OFF`, successful 10,500 frames |
| MVS | kof96, mslug, wjammers | Smaller early games are promising *candidates*, not guaranteed resident | Empirical `gfx_fully_resident`, `pcm_fully_resident` when needed, sound stays on, no processed-file reads during timing |

Do not substitute Metal Slug 3: previous real-hardware findings describe large demand-read C-ROM; this suite intentionally avoids streaming.

### Static ROM-region audit (2026-10-20)

The following numbers come directly from `REGION(...)` declarations in the existing `resources/*/rominfo.*` files, **read only**. They describe allocated regions, not archive/ZIP sizes, empirical free-memory measurements, or frame costs. The CPU2, optional USER1, extra RAM, renderer, sound runtime, and PS2SDK allocations add further memory pressure. The standard retail PS2 Emotion Engine has 32 MiB RAM.

| Core | ROM | CPU1 MiB | Graphics MiB | Samples MiB | Extra declared USER1 MiB | Residency assessment |
| --- | --- | ---: | ---: | ---: | ---: | --- |
| CPS1 | slammast | 2 | 6 | 4 | 0.031 | Unverified |
| CPS1 | dino | 2 | 4 | 2 | 0 | Unverified |
| CPS1 | punisher | 2 | 4 | 2 | 0 | Unverified |
| CPS1 | sf2hf | 2 | 6 | 0.25 | 0 | Unverified |
| CPS2 | avsp | 2 | 16 | 4 | 1 | Unverified |
| CPS2 | 19xx | 2.5 | 16 | 4 | 2 | Unverified |
| CPS2 | ssf2t | 3.5 | 16 | 4 | 3 | Unverified |
| CPS2 | sfa3ur1 | 4 | 32 | 8 | 1 | **Ineligible on retail PS2** |
| MVS | kof96 | 3 | 32 | 10 | 0 | **Ineligible on retail PS2** |
| MVS | mslug | 2 | 16 | 8 | 0 | Unverified, potentially tight |
| MVS | wjammers | 1 | 4 | 4 | 0 | Unverified, promising |

`sfa3ur1` and `kof96` each exceed 32 MiB even before all mandatory regions are accounted for, so they cannot qualify as complete resident workloads on a stock PS2. The remaining titles must be classified by actual successful no-cache execution and allocator evidence; their lower ROM-region totals are not proof that enough RAM exists.

## Milestones

### M1 — Eligibility and baseline identity
Audit per-game CPU/GFX/PCM allocation paths, ROM/processed-asset dependencies, actual PS2 heap probes and title-loop behavior. Use `USE_CACHE=OFF`; record the exact eligible, ineligible or unverified classification. Acceptance: no guessed eligible label and clear reasons for every excluded ROM.

### M2 — Opt-in runtime
Add a separate PS2-only CMake option and a small common benchmark recorder. At game startup enforce fixed runtime settings *after* `load_gamecfg()`, not before. At frame boundary warm up then measure exactly N completed frames; print a single parseable result, exit cleanly. Keep the default non-benchmark build unchanged. Include total, FPS, mean, min/max, P50/95/99, >16,666.7us count, frame count, build identity and completion code. Reserve space for CPU/render stage timing without manufacturing those values.

### M3 — I/O, residency and deterministic replay
Reject `USE_CACHE=ON` benchmark builds and any runtime cache activation. Add counted wrappers at real filesystem boundaries (including cache/processed assets and save-state path); instrument concurrent audio-thread access as needed. Separate I/O counters by operation and size. Capture a deterministic state signature each trial at the same emulated frame checkpoints, or use an input replay and state signature oracle. Reject divergent compiler runs before speedup calculation. A zero count is insufficient until all relevant filesystem paths are covered.

### M4 — Host C99 runner and reporter
Integrate `njemu-tool ps2-benchmark` with a TSV matrix and explicit command-line flags for `ps2client`, executable, ROM, compiler, frame count, repeats, timeout, output directory. Keep PS2Link capture logs, CSV trials and Markdown reports outside `resources/`. Parse only structured records; reject malformed output, incomplete runs, wrong ROM/core/compiler, unverified residency, nonzero I/O and missing state signatures. Three trials minimum; medians for duration and speedup; preserve all raw data. Never treat failure as zero time.

### M5 — GCC functional and real hardware
Build three targets with GCC, `GUI=OFF`, `USE_CACHE=OFF`. Exercise a short PCSX2 smoke test, then fixed 500/10,000 runs on *real* PS2 via PS2Link. Record ROM digests, hardware revision, PS2SDK/gsKit/ps2_drivers revisions, compiler version/flags, commit, audio/output settings and exact run transcripts. Only real hardware is accepted as the throughput baseline.

### M6 — Experimental LLVM/R5900
First check toolchain driver/triple, MIPS/R5900 ABI, -O3 support, calling convention, float/double and alignment assumptions, startup contracts, inline assembly, builtins and libcalls. Compile NJEMU sources with LLVM and link GCC-provided runtime archives with GCC `ld` driver. Produce an object manifest proving each object origin. Disable incompatible GCC/LLVM LTO (including `-flto`); compare matching non-LTO GCC baseline to LLVM. Do not rebuild third parties. If toolchain is absent, retain an explicit blocker and don't claim validation.

### M7 — Comparable reports
For each accepted game/scene, execute >=3 physical-hardware trials per compiler, alternating compiler order if thermal effects matter. Compare median total duration, speedup = GCC/LLVM, percentage improvement = (GCC-LLVM)/GCC*100. Report spread; investigate outliers. A differing state checksum, scenario, asset hash, sound or video configuration invalidates the comparison.

## Instrumentation semantics and open risks

The measured frame interval starts just before the first measured iteration and ends after the last measured iteration. This includes timer/CPU work, renderer, audio-thread contention and presentation except front-loaded startup. The initial warm-up may trigger a VBlank wait, but that is outside the measured interval. Background sound work may continue during or after exit; measured total is EE wall time, not summed CPU thread time. `core_update_us` measures `timer_update_cpu()`, which may itself invoke video drawing; it **must not** be described as CPU-only. `screen_update_us` measures `update_screen()`, including presentation/yielding, **not** all rendering. They are partial, separately scoped wall times and must not be summed to claim a complete CPU+GPU split.

One sample per frame (microseconds) gives quantiles; their sum may differ slightly from a separate total wall-clock clock read because of timestamp overhead. Record overhead by repeatable empty ticker-pair tests and compare an instrumented vs noninstrumented short run. Stage durations can be nested and should be named accordingly.

PS2Link's executable location, mount path, audio scheduling, and game selector are environment-specific; do not hard-code local memory-card or USB paths. This Mac has the GCC 15.2.0 PS2 toolchain at `/Users/fjtrujy/toolchains/ps2/ps2dev`; set `PS2DEV` and `PS2SDK=$PS2DEV/ps2sdk` before configuring. The experimental LLVM/R5900 compiler was not located in that toolchain. Historical PS2Link validation reports an IOP-reset/host-server conflict; the existing unrelated HDD edits to `src/ps2/ps2_platform.c` must not be used as a place to introduce a benchmark-specific workaround.

### Implementation and evidence so far

- Opt-in `PS2_COMPILER_BENCHMARK=ON` requires PS2, `GUI=OFF` and `USE_CACHE=OFF`; the existing core loops count full frames. The timing buffer captures per-frame microseconds without per-frame logging.
- The recorder also emits pre- and post-timing metadata, a selected mutable-memory/CPU-register fingerprint, diagnostic allocations and ticker overhead. For safety it currently labels every otherwise completed trial `state=unverified status=unverified`: an end-state fingerprint alone cannot certify equivalent emulation across compiler builds.
- Linker `--wrap` captures direct POSIX/stdio I/O but may miss direct SDK IOP file RPCs; a zero count is **not** a validated zero-I/O claim until that boundary audit is complete.
- The portable C99 host utility can inspect the TSV matrix, launch PS2Link in a staged private directory, collect per-trial logs and output CSV/Markdown. It rejects incomplete/unverified or inconsistent runs. PS2Link connectivity and the staging mechanism still need real-device testing. The runner never writes into `resources/`.
- GCC builds of the three no-cache PS2 targets succeeded; host reporting/parser regression tests pass. No physical PS2 timing baseline or LLVM-vs-GCC comparison has been collected.
- The structured record includes the exact 40-character Git HEAD, a 64-character SHA256 of `git diff --binary HEAD` at CMake configuration time, the C compiler version, and a deterministic runtime-configuration fingerprint. Combined revision and tracked-diff identity must match between compared trials. The build directory also retains hashes of detected GCC-provided SDK/gsKit/driver archives. Reconfigure after source edits; current worktree metadata is a *configure-time* snapshot. Untracked ROM/BIOS and processed-asset digests remain a separate validation requirement.
- The reporter strictly validates record syntax and fingerprints, and explicitly rejects malformed logs, absent/ineligible titles, any failed trials, and divergent state/configuration/source identities. It does **not** confer state or I/O verification on provisional runtime records.

### PCSX2 functional smoke tests (2026-10-20, **not PS2 hardware performance**)

The PCSX2 CLI accepts `-nogui -batch -elf <file>`. The ELF was copied (not linked) into an isolated ignored `build_ps2_compiler_smoke/<core>/` staging directory with a private `game_name.ini` and read-only symlinks to the build's runtime data. PCSX2 resolves symlinked ELFs to their original parent directory, which would select the wrong game; copying the ELF keeps its host root at the isolated directory. No ROM, BIOS, processed asset or configuration beneath `resources/` was modified.

| Core | ROM | Warm-up + measured | I/O counter | Residency | Result |
| --- | --- | --- | --- | --- | --- |
| CPS1 | slammast | 30 + 300 | 0 | Full GFX 6 MiB, PCM 4 MiB | Completed, state unverified |
| CPS2 | avsp | 30 + 300 | 0 | Full GFX 16 MiB, PCM 4 MiB | Completed, state unverified |
| MVS | wjammers | 30 + 300 | 0 | Full C-ROM 4 MiB, PCM 4 MiB | Completed, state unverified |
| MVS | mslug | 0 + 0 | N/A | Empirical 12.5 MiB graphics budget vs 16 MiB required | Failed no-cache allocation before emulation |

MVS has an optional **benchmark-only** BIOS choice to prevent an interactive BIOS menu from blocking NO_GUI benchmarking. Its preferred order is the existing `bios_crc[]` order, and its actual choice is emitted before timing (e.g. index 0, CRC 9036d879). This is not a production BIOS policy change.

These results prove only that the short opt-in workloads function under PCSX2's PS2 memory model. The timing values are deliberately **not** reported as real-PS2 performance numbers. Even a `io_ops=0` result only confirms the currently wrapped POSIX/stdio entry points; direct PS2SDK IOP-RPC coverage and state equivalence remain unverified. Rebuild the default 500/10,000 configuration before physical-baseline collection, as these smoke ELFs were built for 30/300 frames.

A further PS2 GCC build of `MVS` with `PS2_COMPILER_BENCHMARK=OFF` and `USE_CACHE=OFF` passed, confirming that the new benchmark-only BIOS path is excluded from normal builds. Host-tool tests also passed with synthetic comparable and divergent/incomplete logs. A real PCSX2 `wjammers` log was parsed through `njemu-tool ps2-benchmark report`; it produced CSV/Markdown with `unverified` status and no speedup.

### Build and runner examples

```sh
export PS2DEV=/Users/fjtrujy/toolchains/ps2/ps2dev
export PS2SDK="$PS2DEV/ps2sdk"
export PATH="$PS2DEV/ee/bin:$PS2DEV/iop/bin:$PATH"

cmake -S . -B build_ps2_compiler_bench_cps1 -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$PS2DEV/share/ps2dev.cmake" \
  -DPLATFORM=PS2 -DTARGET=CPS1 \
  -DGUI=OFF -DUSE_CACHE=OFF -DSAVE_STATE=ON \
  -DPS2_COMPILER_BENCHMARK=ON \
  -DPS2_BENCHMARK_WARMUP=500 -DPS2_BENCHMARK_FRAMES=10000
cmake --build build_ps2_compiler_bench_cps1 -j8

build_ps2_compiler_host/njemu-tool ps2-benchmark matrix benchmarks/ps2/roms.tsv
```

For a physical-PS2/PS2Link trial, choose the target build and the real console's address, then run at least three trials. The command requires a working PS2Link server; it **must not** be treated as validated merely because an ELF compiled:

```sh
: "${PS2_IP:?Set PS2_IP to the real console address before running}"
build_ps2_compiler_host/njemu-tool ps2-benchmark run \
  benchmarks/ps2/roms.tsv CPS1 slammast GCC \
  "$PS2DEV/bin/ps2client" "$PS2_IP" \
  build_ps2_compiler_bench_cps1 CPS1.ELF \
  build_ps2_compiler_results 3 180 10000
```

The runner stages runtime symlinks and a private `game_name.ini` outside `resources/`; it never writes to ROMs or game saves. It stores raw per-trial logs in the output directory, then writes `trials.csv` and `summary.md`. It rejects output directories that resolve into a directory named `resources` (including symlink targets). For the moment, the diagnostic firmware records will remain `unverified` pending real-machine replay/state validation and comprehensive I/O audits.

## Acceptance checklist

- [ ] All 11 candidates empirically classified on PS2, with allocation and 10,500-frame evidence.
- [ ] Exactly N full emulated frames, no load/save/INI/ROM I/O in the interval.
- [ ] No streaming cache or forced sound suppression, deterministic state/replay validated.
- [ ] Structured trial output, report parsing, >=3 medians, failures never averaged.
- [ ] Desktop host tests + affected PS2 builds pass; normal mode remains unchanged.
- [ ] Physical PS2 GCC data collected and clearly distinguished from PCSX2.
- [ ] Mixed-link LLVM object manifest verified; compiler comparisons validated.
