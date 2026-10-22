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
- The unrelated HDD change in `src/ps2/ps2_platform.c` was preserved in a dedicated Git stash before the PCSX2 automation work. User-owned files under `resources/` must remain untouched.

## Candidate and qualification matrix

The authoritative file-driven candidates live in `benchmarks/ps2/roms.tsv`. Remaining `unverified` entries are **unqualified for compiler speedup claims** until zero-I/O coverage and deterministic state comparisons exist, even if 10,500 frames complete. Two candidates were ruled out by the static 32 MiB RAM constraint; two additional CPS2 candidates failed empirical PCSX2 no-cache graphics allocation. These are not CPU-expense rankings. Keep raw allocation values, firmware/BIOS identity, processed asset digests and evidence per game in separate result manifests, not guessed in the source matrix.

| Core | Candidates | Starting hypothesis | Mandatory evidence |
| --- | --- | --- | --- |
| CPS1 | slammast, dino, punisher, sf2hf | No streaming-cache backend; CPS1 QSound and heavy sprite scenes may stress EE | Complete CPU/GFX/audio allocations, attract-mode suitability, stable state signature |
| CPS2 | avsp, ddtod, ssf2 (`19xx`, `ssf2t`, `sfa3ur1` excluded) | 16 MiB graphics are too large for some games once mandatory allocations grow; 12 MiB alternatives are feasible | Empirical `gfx_fully_resident`, all PCM allocated, `USE_CACHE=OFF`, successful 10,500 frames |
| MVS | pbobbl2n, trally, wjammers (kof96 excluded) | Fully resident PCSX2 smoke tests for Puzzle Bobble 2, Thrash Rally and Windjammers; replaced mslug after its no-cache allocation failure | Empirical `gfx_fully_resident`, `pcm_fully_resident` when needed, sound stays on, no processed-file reads during timing |

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
| CPS2 | ddtod | 2.5 | 12 | 4 | 1.5 | Full-resident PCSX2 30+300 smoke passed |
| CPS2 | ssf2 | 2.5 | 12 | 4 | 2.5 | Full-resident PCSX2 30+300 smoke passed |
| CPS2 | 19xx | 2.5 | 16 | 4 | 2 | **Ineligible in no-cache PCSX2: 14.7 MiB maximum GFX allocation** |
| CPS2 | ssf2t | 3.5 | 16 | 4 | 3 | **Ineligible in no-cache PCSX2: 12.6 MiB maximum GFX allocation** |
| CPS2 | sfa3ur1 | 4 | 32 | 8 | 1 | **Ineligible on retail PS2** |
| MVS | kof96 | 3 | 32 | 10 | 0 | **Ineligible on retail PS2** |
| MVS | mslug | 2 | 16 | 8 | 0 | Ineligible in no-cache PCSX2: 12.5 MiB GFX limit |
| MVS | wjammers | 1 | 4 | 4 | 0 | Full-length PCSX2 execution, state unverified |
| MVS | pbobblen | 1 | 5 | 3.5 | 0 | Local Puzzle Bobble archive missing `068-v1.v1` |
| MVS | trally | 1 | 3 | 1.5 | 0 | Full-length PCSX2 execution, state unverified |
| MVS | pbobbl2n | 1 | 10 | 8 | 0 | Full-length PCSX2 execution, state unverified |

`sfa3ur1` and `kof96` each exceed 32 MiB even before all mandatory regions are accounted for, so they cannot qualify as complete resident workloads on a stock PS2. The remaining titles must be classified by actual successful no-cache execution and allocator evidence; their lower ROM-region totals are not proof that enough RAM exists.

Empirical CPS2 full-length attempts confirmed this distinction: `avsp` reserves 16 MiB of graphics and completes 500+10,000 frames, but `19xx` could allocate at most 14.7 MiB after loading its mandatory regions, and `ssf2t` at most 12.6 MiB. Both print `Memory not enough` before emulation, with zero warm-up and measured frames. `ddtod` and `ssf2` are locally available, decoded 12 MiB-graphics alternatives; both passed separate 30+300 full-resident smoke tests. Additional short smoke runs of `choko`, `qndream` and `mpangj` (16 MiB GFX each) also passed, but they were not selected for the primary workload matrix because the chosen games better diversify arcade gameplay workloads. This selection is a functional sampling choice, not a measured expense ranking.

The MVS file-driven shortlist now uses `pbobbl2n` (Puzzle Bobble 2) and `trally` (Thrash Rally) instead of `mslug`. The local `pbobblen` (first Puzzle Bobble) ROM set failed loading because NJEMU requested the absent `068-v1.v1` sound file; **no ROM data was modified to work around it**. Puzzle Bobble 2 uses a separate locally complete ROM set and passed the full-length no-cache PCSX2 test. NJEMU also defines `neodrift` (Neo Drift Out) and `overtop`, but those ROM archives were not present in the user's local MVS ROM directory at audit time. This does not imply the local archive names are mandatory for a future run. The original `mslug` observation remains in the historical static and PCSX2 audits below.

## Milestones

### M1 — Eligibility and baseline identity
Audit per-game CPU/GFX/PCM allocation paths, ROM/processed-asset dependencies, actual PS2 heap probes and title-loop behavior. Use `USE_CACHE=OFF`; record the exact eligible, ineligible or unverified classification. Acceptance: no guessed eligible label and clear reasons for every excluded ROM.

### M2 — Opt-in runtime
Add a separate PS2-only CMake option and a small common benchmark recorder. At game startup enforce fixed runtime settings *after* `load_gamecfg()`, not before. At frame boundary warm up then measure exactly N completed frames; print a single parseable result, exit cleanly. Keep the default non-benchmark build unchanged. Include total, FPS, mean, min/max, P50/95/99, >16,666.7us count, frame count, build identity and completion code. Reserve space for CPU/render stage timing without manufacturing those values.

### M3 — I/O, residency and deterministic replay
Reject `USE_CACHE=ON` benchmark builds and any runtime cache activation. Add counted wrappers at real filesystem boundaries (including cache/processed assets and save-state path); instrument concurrent audio-thread access as needed. Separate I/O counters by operation and size. Capture a deterministic state signature each trial at the same emulated frame checkpoints, or use an input replay and state signature oracle. Reject divergent compiler runs before speedup calculation. A zero count is insufficient until all relevant filesystem paths are covered.

### M4 — Host C99 runner and reporter
Integrate `njemu-tool ps2-benchmark` with a TSV matrix and explicit command-line flags for `ps2client`, executable, ROM, compiler, frame count, repeats, timeout, output directory. Keep PS2Link capture logs, CSV trials and Markdown reports outside `resources/`. Parse only structured records; reject malformed output, incomplete runs, wrong ROM/core/compiler, unverified residency, nonzero I/O and missing state signatures. Three trials minimum; medians for duration and speedup; preserve all raw data. Never treat failure as zero time.

### M5 — GCC functional and PCSX2 baseline
Build three targets with GCC, `GUI=OFF`, `USE_CACHE=OFF`. Exercise short PCSX2 smoke tests, then fixed 500/10,000 runs in PCSX2. Record ROM digests, PCSX2 version and configuration, PS2SDK/gsKit/ps2_drivers revisions, compiler version/flags, commit, audio/output settings and exact run transcripts. PCSX2 is the primary available validation environment; **physical-PS2 testing is deferred and not required to complete this development phase**. PCSX2 benchmark timings are emulator-specific, not estimates of actual PS2 throughput.

### M6 — Experimental LLVM/R5900
First check toolchain driver/triple, MIPS/R5900 ABI, -O3 support, calling convention, float/double and alignment assumptions, startup contracts, inline assembly, builtins and libcalls. Compile NJEMU sources with LLVM and link GCC-provided runtime archives with GCC `ld` driver. Produce an object manifest proving each object origin. Disable incompatible GCC/LLVM LTO (including `-flto`); compare matching non-LTO GCC baseline to LLVM. Do not rebuild third parties. If toolchain is absent, retain an explicit blocker and don't claim validation.

### M7 — Comparable PCSX2 reports
For each accepted game/scene, execute >=3 PCSX2 trials per compiler under matching PCSX2 versions/settings on the same host, alternating compiler order to reduce run-order bias. Compare median total duration, speedup = GCC/LLVM, percentage improvement = (GCC-LLVM)/GCC*100. Report spread; investigate outliers. A differing state checksum, scenario, asset hash, sound/video configuration or execution environment invalidates the comparison. Any future physical-PS2 benchmarks must use separate tables and never be pooled with PCSX2 timings.

## Instrumentation semantics and open risks

The measured frame interval starts just before the first measured iteration and ends after the last measured iteration. This includes timer/CPU work, renderer, audio-thread contention and presentation except front-loaded startup. The initial warm-up may trigger a VBlank wait, but that is outside the measured interval. Background sound work may continue during or after exit; measured total is EE wall time, not summed CPU thread time. `core_update_us` measures `timer_update_cpu()`, which may itself invoke video drawing; it **must not** be described as CPU-only. `screen_update_us` measures `update_screen()`, including presentation/yielding, **not** all rendering. They are partial, separately scoped wall times and must not be summed to claim a complete CPU+GPU split.

One sample per frame (microseconds) gives quantiles; their sum may differ slightly from a separate total wall-clock clock read because of timestamp overhead. Record overhead by repeatable empty ticker-pair tests and compare an instrumented vs noninstrumented short run. Stage durations can be nested and should be named accordingly.

PS2Link's executable location, mount path, audio scheduling, and game selector are environment-specific; do not hard-code local memory-card or USB paths. This Mac has the GCC 15.2.0 PS2 toolchain at `/Users/fjtrujy/toolchains/ps2/ps2dev`; set `PS2DEV` and `PS2SDK=$PS2DEV/ps2sdk` before configuring. The experimental LLVM/R5900 compiler was not located in that toolchain. Historical PS2Link validation reports an IOP-reset/host-server conflict; the existing unrelated HDD edits to `src/ps2/ps2_platform.c` must not be used as a place to introduce a benchmark-specific workaround.

### Implementation and evidence so far

- Opt-in `PS2_COMPILER_BENCHMARK=ON` requires PS2, `GUI=OFF` and `USE_CACHE=OFF`; the existing core loops count full frames. The timing buffer captures per-frame microseconds without per-frame logging.
- The recorder also emits pre- and post-timing metadata, a selected mutable-memory/CPU-register fingerprint, diagnostic allocations and ticker overhead. For safety it currently labels every otherwise completed trial `state=unverified status=unverified`: an end-state fingerprint alone cannot certify equivalent emulation across compiler builds.
- Linker `--wrap` captures direct POSIX/stdio I/O but may miss direct SDK IOP file RPCs; a zero count is **not** a validated zero-I/O claim until that boundary audit is complete.
- The portable C99 host utility can inspect the TSV matrix, launch either PCSX2 or PS2Link in a staged private directory, collect per-trial logs and output CSV/Markdown. It rejects incomplete/unverified or inconsistent runs. The PCSX2 launcher copies the ELF into a unique private stage (PCSX2 resolves ELF symlinks outside the staging root), symlinks the remaining read-only runtime assets, and writes a private `game_name.ini`. Temporary PCSX2-created output directories are removed without following symlinks. PS2Link connectivity still needs device testing, but is not a blocker for the PCSX2 development baseline. Neither launcher writes into `resources/`.
- GCC builds of the three no-cache PS2 targets succeeded; host reporting/parser regression tests pass. Full PCSX2 baseline runs are being collected. No physical PS2 timing baseline or LLVM-vs-GCC comparison has been collected, nor is the former required in this development phase.
- The structured record includes the exact 40-character Git HEAD, a 64-character SHA256 of `git diff --binary HEAD` at CMake configuration time, the C compiler version, and a deterministic runtime-configuration fingerprint. Combined revision and tracked-diff identity must match between compared trials. The build directory also retains hashes of detected GCC-provided SDK/gsKit/driver archives. Reconfigure after source edits; current worktree metadata is a *configure-time* snapshot. Untracked ROM/BIOS and processed-asset digests remain a separate validation requirement.
- The reporter strictly validates record syntax and fingerprints, and explicitly rejects malformed logs, absent/ineligible titles, any failed trials, and divergent state/configuration/source identities. It detects the PCSX2 version from the emulator log, labels unknown environments conservatively, and rejects a comparison that mixes different detected environments. It does **not** confer state or I/O verification on provisional runtime records, nor certify that PCSX2 settings are identical.

### PCSX2 functional smoke tests (2026-10-20, **not PS2 hardware performance**)

The PCSX2 CLI accepts `-nogui -batch -elf <file>`. The ELF was copied (not linked) into an isolated ignored `build_ps2_compiler_smoke/<core>/` staging directory with a private `game_name.ini` and read-only symlinks to the build's runtime data. PCSX2 resolves symlinked ELFs to their original parent directory, which would select the wrong game; copying the ELF keeps its host root at the isolated directory. No ROM, BIOS, processed asset or configuration beneath `resources/` was modified.

| Core | ROM | Warm-up + measured | I/O counter | Residency | Result |
| --- | --- | --- | --- | --- | --- |
| CPS1 | slammast | 30 + 300 | 0 | Full GFX 6 MiB, PCM 4 MiB | Completed, state unverified |
| CPS2 | avsp | 30 + 300 | 0 | Full GFX 16 MiB, PCM 4 MiB | Completed, state unverified |
| MVS | wjammers | 30 + 300 | 0 | Full C-ROM 4 MiB, PCM 4 MiB | Completed, state unverified |
| MVS | mslug | 0 + 0 | N/A | Empirical 12.5 MiB graphics budget vs 16 MiB required | Failed no-cache allocation before emulation |
| MVS | pbobblen | 0 + 0 | N/A | 5 MiB C-ROM and 3.5 MiB samples declared | Failed pre-start: local archive lacks requested `068-v1.v1` |
| MVS | pbobbl2n | 30 + 300 | 0 | Full C-ROM 10 MiB, PCM 8 MiB | Completed, state unverified |
| MVS | trally | 30 + 300 | 0 | Full C-ROM 3 MiB, PCM 1.5 MiB | Completed, state unverified |

MVS has an optional **benchmark-only** BIOS choice to prevent an interactive BIOS menu from blocking NO_GUI benchmarking. Its preferred order is the existing `bios_crc[]` order, and its actual choice is emitted before timing (e.g. index 0, CRC 9036d879). This is not a production BIOS policy change.

These results prove only that the short opt-in workloads function under PCSX2's PS2 memory model. The timing values are deliberately **not** reported as real-PS2 performance numbers. Even a `io_ops=0` result only confirms the currently wrapped POSIX/stdio entry points; direct PS2SDK IOP-RPC coverage and state equivalence remain unverified. The 30/300 smoke ELFs are not the full-length baseline; separate 500/10,000 binaries have been built.

### MVS replacement ROMs: full-length PCSX2 functional runs (2026-10-20)

A separate `build_ps2_compiler_bench_mvs_10k` GCC PS2 build used `GUI=OFF`, `USE_CACHE=OFF`, `PS2_COMPILER_BENCHMARK=ON`, `PS2_BENCHMARK_WARMUP=500`, and `PS2_BENCHMARK_FRAMES=10000`. For each ROM, the same ELF was **copied** into a private per-ROM stage directory, with a private selector; no files within `resources/` were changed.

| ROM | CPU1 | Resident C-ROM | Resident PCM | Warm-up | Measured | Wrapped I/O | Final status |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| `trally` — Thrash Rally | 1 MiB | 3 MiB | 1.5 MiB | 500/500 | 10,000/10,000 | 0 ops | `unverified` |
| `pbobbl2n` — Puzzle Bobble 2 | 1 MiB | 10 MiB | 8 MiB | 500/500 | 10,000/10,000 | 0 ops | `unverified` |

Both runs logged their normal game ROM loads before the measured interval and emitted a single `NJEMU_BENCHMARK_V1` record with `cache=off`, `resident=on`, `sound=on` and an end-state fingerprint. The MVS BIOS selection was completed before warm-up. PCSX2 timings are not comparable with real-PS2 measurements; even a successful 10,000-frame run does not establish a reproducible game trajectory between compiler builds.

### Complete PCSX2 GCC candidate matrix (2026-10-20)

The opt-in GCC benchmark builds for CPS1, CPS2 and MVS were configured for `GUI=OFF`, `USE_CACHE=OFF`, 500 warm-up frames and exactly 10,000 measured frames. PCSX2 v2.9.93 ran each candidate in its own private ELF-copy staging directory. The following values are **single-run PCSX2 guest-timer observations, not real PS2 performance measurements, compiler speedups or cross-ROM workload rankings**. The 10 completed games each logged `sound=on`, `cache=off`, `resident=on`, `io_ops=0`, and `state=unverified`.

| Core | ROM | Resident graphics MiB | Resident sound MiB | Measured frames | PCSX2 timer s | Result |
| --- | --- | ---: | ---: | ---: | ---: | --- |
| CPS1 | `slammast` | 6 | 4 | 10,000 | 65.418 | Completed, state unverified |
| CPS1 | `dino` | 4 | 2 | 10,000 | 64.726 | Completed, state unverified |
| CPS1 | `punisher` | 4 | 2 | 10,000 | 63.120 | Completed, state unverified |
| CPS1 | `sf2hf` | 6 | 0.25 | 10,000 | 65.036 | Completed, state unverified |
| CPS2 | `avsp` | 16 | 4 | 10,000 | 79.948 | Completed, state unverified |
| CPS2 | `ddtod` | 12 | 4 | 10,000 | 82.394 | Completed, state unverified |
| CPS2 | `ssf2` | 12 | 4 | 10,000 | 83.507 | Completed, state unverified |
| CPS2 | `19xx` | — | — | 0 | — | Ineligible: GFX allocation 14.7 MiB vs 16 MiB required |
| CPS2 | `ssf2t` | — | — | 0 | — | Ineligible: GFX allocation 12.6 MiB vs 16 MiB required |
| MVS | `wjammers` | 4 | 4 | 10,000 | 55.183 | Completed, state unverified |
| MVS | `pbobbl2n` | 10 | 8 | 10,000 | 66.027 | Completed, state unverified |
| MVS | `trally` | 3 | 1.5 | 10,000 | 71.784 | Completed, state unverified |

These are the 10 active, resident test candidates out of 14 matrix entries; the remaining two entries `sfa3ur1` and `kof96` are statically ineligible even before allocation. `19xx` and `ssf2t` each emitted an explicit zero-frame `status=failed` record after reporting `Memory not enough`. The PCSX2 launcher now recognizes such failure records immediately, produces a failure row, and exits rather than waiting for the configured timeout; this was confirmed by rerunning `19xx` after the improvement. An ignored aggregate report (12 PCSX2 logs, 10 full completions, 2 failed) is available at:

`build_ps2_compiler_smoke/pcsx2_full_matrix/aggregate/summary.md` and `build_ps2_compiler_smoke/pcsx2_full_matrix/aggregate/trials.csv`.

**Same-build state divergence:** Two independent 10,000-frame `trally` executions of the *same ELF* and runtime-configuration fingerprint produced different final state hashes (`99e1e67d0018259d` versus `e75fcc13982b65d6`). To investigate, benchmark-only instrumentation was added that emits `NJEMU_BENCHMARK_STATE_PARTS_V1` immediately before the standard benchmark record; it separates 68000 registers, Z80 registers and mutable RAM without changing the accepted record schema or timing interval. In three additional same-build `trally` tests of 30 warm-up + 300 measured frames, the RAM fingerprints differed in **all three trials**, and the CPU-register fingerprints also differed between the first and the subsequent two. These runs are evidence of insufficient replay determinism, not a diagnosis of its cause. A deterministic game-state fixture and input/audio-thread synchronization audit are required before declaring compiler results comparable. No result was promoted from `state=unverified`.

### Repeatable GCC baseline in PCSX2 (2026-10-20)

To estimate timing repeatability without adding any runtime telemetry, every eligible ROM was executed twice more with the **same prebuilt GCC ELF**, build provenance, config fingerprint, PCSX2 v2.9.93, 500 warm-up frames and 10,000 measured frames. Together with each ROM's earlier full-length run, this produces three trials per ROM. `trally` also received **ten separate, new** full-length trials (the table below uses those ten rather than its earlier run). No test ran concurrently with another PCSX2 instance. All 37 baseline trials completed with `resident=on`, `sound=on`, `cache=off`, `io_ops=0`, and `state=unverified`. The zero-I/O result applies only to the instrumented POSIX/stdio wrappers, not all IOP RPC entry points.

| Core | ROM | Trials | Mean seconds | Median seconds | Coefficient of variation | Distinct final hashes |
| --- | --- | ---: | ---: | ---: | ---: | ---: |
| CPS1 | `slammast` | 3 | 65.290 | 65.226 | 0.1701% | 2 |
| CPS1 | `dino` | 3 | 64.722 | 64.720 | 0.0058% | 2 |
| CPS1 | `punisher` | 3 | 63.120 | 63.120 | 0.0009% | 2 |
| CPS1 | `sf2hf` | 3 | 65.052 | 65.056 | 0.0223% | 3 |
| CPS2 | `avsp` | 3 | 79.949 | 79.949 | 0.0012% | 2 |
| CPS2 | `ddtod` | 3 | 82.385 | 82.381 | 0.0093% | 2 |
| CPS2 | `ssf2` | 3 | 83.508 | 83.509 | 0.0012% | 2 |
| MVS | `wjammers` | 3 | 55.346 | 55.183 | 0.5440% | 3 |
| MVS | `pbobbl2n` | 3 | 66.034 | 66.027 | 0.0895% | 3 |
| MVS | `trally` | 10 | 71.699 | 71.699 | 0.1066% | 10 |

The coefficient of variation is the **sample** standard deviation divided by the mean, calculated from the original `total_us` records. Although all measured timing CVs are below 0.55%, **none of the ROMs reproduced exactly the same final-state hash across every trial**. In particular, `trally` produced ten distinct hashes in ten runs. The benchmark is useful as a *descriptive* baseline for future compiler work, but these observations do not establish identical emulated workloads or prove GCC/LLVM comparability. The PCSX2 guest-timer values are not real-PS2 benchmarks.

`njemu-tool ps2-benchmark report` now adds a **Repeated-run timing (descriptive only)** section showing mean, median, minimum/maximum, range spread and distinct final-state counts. It refuses to pool trials with different compiler versions, configurations, source fingerprints or environment identifiers, and preserves the existing strict `GCC versus LLVM` acceptance gate. This reuses the existing records rather than changing the runtime format. All 37 raw logs and the generated CSV/Markdown combined report are kept in the ignored, local directory:

`build_ps2_compiler_smoke/pcsx2_gcc_10x_20261020/aggregate/`

### Repeated GCC PCSX2 timing baseline (2026-10-20)

The 10 eligible ROMs were repeated serially in PCSX2 v2.9.93, with GCC 15.2.0, `GUI=OFF`, `USE_CACHE=OFF`, sound enabled, 500 warm-up frames and 10,000 measured frames per run. No simultaneous PCSX2 instances were used. There were **28 new successful runs**: 10 for `trally` and two for each of the other nine ROMs. Combining the nine earlier qualification logs with the new runs produces **37 complete observations**. Each ROM's prior and new logs have identical revision, tracked-worktree, configuration and compiler-version fingerprints. These figures are **descriptive within-GCC PCSX2 guest-clock timings**, not real-hardware timings, proof of a deterministic game trajectory, cross-ROM CPU expense rankings or a GCC/LLVM speedup.

| Core | ROM | Runs | Mean s | Median s | Sample std. dev. ms | CV % | Different final-state hashes |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: |
| CPS1 | `slammast` | 3 | 65.290 | 65.226 | 111.0 | 0.170 | 2 |
| CPS1 | `dino` | 3 | 64.722 | 64.720 | 3.8 | 0.006 | 2 |
| CPS1 | `punisher` | 3 | 63.120 | 63.120 | 0.6 | 0.001 | 2 |
| CPS1 | `sf2hf` | 3 | 65.052 | 65.056 | 14.5 | 0.022 | 3 |
| CPS2 | `avsp` | 3 | 79.949 | 79.949 | 1.0 | 0.001 | 2 |
| CPS2 | `ddtod` | 3 | 82.385 | 82.381 | 7.6 | 0.009 | 2 |
| CPS2 | `ssf2` | 3 | 83.508 | 83.509 | 1.0 | 0.001 | 2 |
| MVS | `wjammers` | 3 | 55.346 | 55.183 | 301.1 | 0.544 | 3 |
| MVS | `pbobbl2n` | 3 | 66.034 | 66.027 | 59.1 | 0.090 | 3 |
| MVS | `trally` | 10 | 71.699 | 71.699 | 76.4 | 0.107 | 10 |

CV is sample standard deviation divided by mean. All 37 runs reported 10,000 completed frames, `cache=off`, `sound=on`, `resident=on`, `io_ops=0` through the existing POSIX/stdio wrappers and `state=unverified`. Although every per-ROM timing CV was below 1%, **state fingerprints varied**, including 10 distinct combined hashes across the 10 new `trally` runs. Timing stability does not imply execution equivalence; comprehensive IOP-RPC I/O auditing and deterministic replay remain prerequisites before comparing GCC with LLVM. No extra per-frame traces were added.

The ignored local result directory is `build_ps2_compiler_smoke/pcsx2_gcc_10x_20261020/`. Its `aggregate/` directory contains `trials.csv`, the unchanged acceptance-gated `summary.md` (37 unverified trials, zero accepted for compiler speedups) and `descriptive_statistics.md` (per-ROM means, medians, sample standard deviations, CVs, ranges and unique state-hash counts). The original nine qualifying logs remain at the paths named in `build_ps2_compiler_smoke/pcsx2_full_matrix/aggregate/trials.csv`; 28 additional raw logs are preserved under the new result directory. ROM assets and private resource files were not modified.

Raw functional logs and the generated (all-unverified) CSV/Markdown reports are retained in ignored directories:

- `build_ps2_compiler_smoke/mvs-trally-10k/pcsx2_10k.log`
- `build_ps2_compiler_smoke/mvs-pbobbl2n-10k/pcsx2_10k.log`
- `build_ps2_compiler_smoke/qualified_10k_report/trials.csv`
- `build_ps2_compiler_smoke/qualified_10k_report/summary.md`

The PS2 toolchain build and host test suite pass, including matrix membership tests that require `trally` and `pbobbl2n` and reject the removed `mslug`. Benchmark runtime output still records `state=unverified` because a verified deterministic replay/oracle and complete filesystem-boundary instrumentation are outstanding.

### Native PCSX2 benchmark runner

`njemu-tool` now provides an alternative to PS2Link with the same matrix/ROM guards, log preservation and report validation. It directly invokes the PCSX2 CLI; the game is selected using the isolated `game_name.ini`, without editing the original build or any ROM assets:

```sh
build_ps2_compiler_host/njemu-tool ps2-benchmark pcsx2 \
  benchmarks/ps2/roms.tsv MVS trally GCC \
  /Applications/PCSX2.app/Contents/MacOS/PCSX2 \
  build_ps2_compiler_bench_mvs_10k MVS.ELF \
  build_ps2_compiler_smoke/pcsx2-trally-repeats \
  3 210 10000
```

Arguments after the output directory are trial count, per-trial timeout in seconds and expected **measured** frame count. The PCSX2 runner permits a single functional trial; 3 or more are required for comparable median statistics. Logs, `trials.csv` and `summary.md` remain in the ignored output directory, and existing raw trial logs are never overwritten. PCSX2 may create nested `PICTURE/` directories in the temporary staging root; the runner deletes only its own staging files/directories and never follows links into `resources/`. A POSIX host regression uses a fake PCSX2 executable to verify absolute ELF paths, independent ELF copies, private selectors, linked read-only assets and cleanup.

Passing 500/10,000 frames under PCSX2 is **functional evidence**, not proof of identical cross-compiler emulated state. Until the deterministic replay and comprehensive I/O audit are complete, completed runs deliberately remain `state=unverified` and cannot produce a speedup in the authoritative reporter. The reporter includes PCSX2 version provenance and uses per-core/ROM/compiler/environment trial numbering even in a combined multi-ROM report.

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

For an optional future physical-PS2/PS2Link trial, choose the target build and the real console's address, then run at least three trials. The command requires a working PS2Link server; it **must not** be treated as validated merely because an ELF compiled:

```sh
: "${PS2_IP:?Set PS2_IP to the real console address before running}"
build_ps2_compiler_host/njemu-tool ps2-benchmark run \
  benchmarks/ps2/roms.tsv CPS1 slammast GCC \
  "$PS2DEV/bin/ps2client" "$PS2_IP" \
  build_ps2_compiler_bench_cps1 CPS1.ELF \
  build_ps2_compiler_results 3 180 10000
```

The optional PS2Link runner stages runtime symlinks and a private `game_name.ini` outside `resources/`; it never writes to ROMs or game saves. It stores raw per-trial logs in the output directory, then writes `trials.csv` and `summary.md`. It rejects output directories that resolve into a directory named `resources` (including symlink targets). For the moment, the diagnostic firmware records will remain `unverified` pending deterministic replay/state validation and comprehensive I/O audits, both of which can be investigated using PCSX2.

### M3 follow-up: frozen RTC, isolated save fixtures and PS2SDK I/O boundaries (2026-10-20)

The repeated GCC baseline revealed two *actual* nondeterministic inputs, rather than simply timer measurement noise:

1. `neogeo_driver_reset()` initialized the emulated PD4990A calendar from `time(NULL)`/`localtime()`. Benchmark-only MVS builds now use a constant January 1, 2000 clock seed. Ordinary game builds still use the real host clock.
2. The PCSX2 runner copied the ELF but symlinked all other build entries, **including writable** `config`, `nvram` and `memcard` directories. A completed trial wrote its game configuration and SRAM/memory-card output back to the shared build. Subsequent trials therefore inherited the preceding run's state. The runner now creates private per-trial save directories, copying only the selected game's starting `.ini`, `.nv` and `.bin` fixture files from the build; `state` is private and initially empty. It refuses symlinked save fixtures and never writes to original saves or `resources/`. Host tests mutate private save files and verify the originals remain unchanged.

After fixing the RTC and isolating saves, **5/5** `trally` trials of 30 warm-up plus 300 measured frames in PCSX2 produced the same combined state hash (`1d9d2bbee0deaebd`), compared with initial divergence before both fixes. The 500+10,000-frame validation is more limited: **2/2** trials produced identical M68000 (`6dab89bb7f7e5a8b`) and RAM (`c46ff426882740e3`) diagnostic hashes, but their **Z80 hashes differed** (`c5ef8526e55d44d0`, `e1991980b49a1727`). The async audio thread renders against the original sound-enabled workload; sound-side state synchronization remains unverified. No compiler comparisons were promoted to `state=verified` or `status=ok`.

The I/O linker-wrap coverage now includes direct PS2SDK `fileXio*` and legacy `fio*` open/read/write/seek/close, directory and stat operations as well as the original POSIX/stdio operations, with the **same** existing `io_ops`/`io_bytes` fields. Two new 30+300 PCSX2 trials completed `io_ops=0`, `io_bytes=0`, `resident=on` and matching state hashes. The MVS ELF was checked to contain the new wrapper symbols. Full IOP RPC completeness, including indirect and intra-library file operations, still requires an independent static/link and runtime audit; these counters alone are not yet sufficient proof of zero disk I/O.

The PS2/GCC benchmark builds were rebuilt for **all three cores** after adding the SDK wrappers. Full 500+10,000-frame PCSX2 validation of CPS1 `dino` and CPS2 `avsp` also completed with all assets resident and `io_ops=0`, retaining their previously observed combined fingerprints (`598368570cb04116` and `287fc94462499bf5`, respectively). The four host tests pass, including the new save-fixture isolation regression. The elapsed times in these checks are PCSX2 observations only. The whole cross-core suite must still be rerun with frozen input fixtures and verified sound state before LLVM comparison.

The original 37 GCC timing observations remain useful as a *provisional* historical reference, but were collected before writable-stage isolation and should be rerun with frozen fixtures before any GCC-vs-LLVM comparison. The new validations reside in ignored directories named `build_ps2_compiler_smoke/mvs_fixed_rtc_*_20261020/`, `build_ps2_compiler_smoke/mvs_extended_io_diag_20261020/`, and `build_ps2_compiler_smoke/m3_extended_io_*_20261020/`. The benchmark record format and per-frame logging have not changed. PCSX2 host timing is not physical PS2 timing.

### First LLVM/R5900 integration attempt (2026-10-21)

The first Clang 24/R5900 build reached 64/68 CPS1, 61/64 CPS2 and 64/68 MVS LLVM-compiled objects; the remaining objects fail because the backend emits floating-point comparison instructions not supported by the R5900 assembler. A **diagnostic hybrid** CPS1 ELF using four GCC-compiled NJEMU objects linked and completed 10,000 frames in PCSX2, but is **not** a valid LLVM-only benchmark or GCC-versus-LLVM comparison. The old GCC baseline is also based on a different source fingerprint from the user-provided LZW replacement. Full compiler errors, a standalone C reproduction, object ownership, workaround findings, and next steps are in [PS2 LLVM/R5900 First Compiler Integration Report](PS2_LLVM_R5900_INITIAL_REPORT.md).

### Full LLVM 24 hybrid benchmark baseline (2026-10-21)

The **complete three-trial, ten-ROM matrix** was subsequently executed for the LLVM 24 hybrid builds: **30/30** 500+10,000-frame trials finished in PCSX2 v2.9.93 with sound on, cache off, resident ROMs and zero wrapped I/O. Descriptive hybrid elapsed times range from **5.26% to 18.26% slower** than GCC 15.2.0; the ten-ROM equal-weight geometric mean slowdown is **11.26%**. This is **not** an all-LLVM performance measurement: per-core GCC fallback objects remain and source-worktree provenance differs from the historical GCC reference. Combined final-state hashes reproduce in six ROMs; CPS1 `sf2hf` diverges in main CPU/RAM and MVS titles still diverge in Z80 state. The interrupted first `trally` attempt was preserved and excluded, with three fresh trials completed. See [PS2 LLVM 24 Hybrid Benchmark Report](PS2_LLVM_24_HYBRID_BENCHMARK_REPORT.md) and the locally generated `build_ps2_compiler_llvm_20261021/hybrid_matrix_20261021/aggregate/exploratory_gcc_vs_llvm24_hybrid.csv` for the full result matrix and exact build provenance. Official compiler-speedup acceptance remains blocked until the R5900 backend is fixed, both compilers use an identical source snapshot, and state/I/O equivalence is verified.

### Real PS2Link hardware follow-up (2026-10-21)

GCC and LLVM phase3 hybrid benchmark ELFs were built for all three cores, but real-hardware runs were **stopped without any valid trial** after two CPS1 startup failures returned the console to the OSD. The first reached video CLUT allocation; the second stopped earlier. The user reopened PS2Link. Neither automated PS2Link reset nor unattended relaunch is safe until diagnosed. See [PS2 Real-Hardware Benchmark — Incomplete Attempt](PS2_REAL_HARDWARE_BENCHMARK_ATTEMPT.md) for the preserved diagnostic artifacts, temporary IOP-reset workaround, and safe next steps. No real-PS2 performance comparison is available yet.

### Inputless PS2Link startup isolation (2026-10-22)

`PS2_BENCHMARK_NO_INPUT=ON` now disables physical controller/IRX startup **only** for headless PS2 compiler benchmarks. Combine it with `PS2_BENCHMARK_PS2LINK=ON` for real PS2Link (resident IOP), or leave PS2Link mode OFF for ordinary PCSX2. A separate GCC 240p inputless `dino` build completed 500+10,000 frames in PCSX2 with its earlier state fingerprint, while a **single real-console trial passed pad initialization and stopped at CLUT allocation**, yielding no timed result. The subsequent instrumented ELF launch produced no loader output (the previous EE program may still have been active), and host capture was stopped without remote reset. The existing protected diagnostic ELF and exact logs are described in [PS2 Real-Hardware Benchmark — Incomplete Attempt](PS2_REAL_HARDWARE_BENCHMARK_ATTEMPT.md). Do not start the automated matrix until one full hardware trial and the inter-trial launch lifecycle are validated.

The subsequent [offline startup investigation](PS2LINK_COMPILER_BENCHMARK_STARTUP_INVESTIGATION.md) found the second diagnostic ELF had **accidentally regained its `SifIopReset()` call** after a source restoration and incremental rebuild; the old manual compile flag was ineffective. The first GS/video hang remains unproven. An explicit default-OFF `PS2_BENCHMARK_PS2LINK=ON` build option now retains IOP-reset suppression across rebuilds, and the host runner prohibits unattended multi-trial PS2Link launches. New guarded GCC/phase3 CPS1 **240p** candidates were compiled and their platform objects audited, but not launched on the console. PCSX2 smoke tests passed in both 240p and 480i. Hardware timing remains pending.

## Acceptance checklist

- [x] All 10 active candidates completed 500+10,000 frames under PCSX2; four additional matrix entries are ineligible (two static, two empirical).
- [x] Exactly N completed frames, cache off and sound on for all 10 successful PCSX2 runs; no results were claimed for allocation failures.
- [ ] Comprehensive runtime filesystem/IOP-RPC audit proves no disk I/O during timing; POSIX/stdio plus direct fileXio/fio entry points are now wrapped, but indirect/intra-object and low-level RPC bypasses are not yet certified.
- [ ] Deterministic replay or state fixture makes same-build PCSX2 state fingerprints repeatable.
- [x] Structured trial output and report parsing validated, including synthetic >=3-trial GCC/LLVM medians and rejection of divergent state/environment/failure records; real comparable compiler medians still pending.
- [x] Desktop host tests and affected GCC PS2 benchmark builds pass; benchmark-only code remains opt-in.
- [x] Single-run PCSX2 GCC functional baseline collected and environment/limitations documented; future real-hardware trials optional and kept separate.
- [ ] Mixed-link LLVM object manifest verified; compiler comparisons validated.
