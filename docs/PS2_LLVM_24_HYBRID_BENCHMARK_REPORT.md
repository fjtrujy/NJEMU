# PS2 LLVM 24 R5900 — First Complete Hybrid Benchmark Report

Date: 2026-10-21. Repository branch: `feature/ps2-compiler-benchmark`.

## Outcome and classification

The first complete **LLVM 24.0.0git exploratory hybrid** PCSX2 benchmark has finished: **30/30 measured trials**, three per ROM, with 500 warm-up and 10,000 measured emulated frames per trial. The test matrix matches the ten GCC reference ROMs; all trials report resident assets, sound on, cache off, `io_ops=0` through the currently instrumented interfaces and `state=unverified`.

**This is a hybrid-compiler baseline, not an all-LLVM NJEMU baseline and not an accepted compiler speedup measurement.** The current LLVM/R5900 backend emits unsupported floating-point compare opcodes for a few compilation units. Those units were copied from the previous GCC 15.2.0 build. All remaining NJEMU C objects were compiled with LLVM/Clang 24, assembled using the GNU PS2 assembler, and linked using GCC with the same prebuilt GCC PS2SDK, gsKit and ps2_drivers libraries. The benchmark runner labels hybrid trials `compiler=LLVM`, but this is only the *majority compiler*: the explicit fallback object manifest below is authoritative.

## Performance results

Times are the benchmark's **in-guest elapsed microsecond measurements running under PCSX2 v2.9.93**, converted to seconds; they are **not** host wall-clock durations or real PS2 hardware performance. Each value is the mean of three independent, sequential trials of 10,000 measured frames. The difference is `(hybrid_mean / GCC_mean - 1) * 100`; positive values mean hybrid LLVM is slower.

| Core | ROM | GCC 15.2.0 mean (s) | LLVM 24 hybrid mean (s) | Hybrid slowdown | GCC distinct end hashes | Hybrid distinct end hashes |
| --- | --- | ---: | ---: | ---: | ---: | ---: |
| CPS1 | `slammast` | 65.407 | 71.368 | +9.11% | 1 | 1 |
| CPS1 | `dino` | 64.729 | 71.597 | +10.61% | 1 | 1 |
| CPS1 | `punisher` | 63.121 | 70.355 | +11.46% | 1 | 1 |
| CPS1 | `sf2hf` | 65.068 | 73.949 | +13.65% | 3 | 2 |
| CPS2 | `avsp` | 79.947 | 84.316 | +5.47% | 1 | 1 |
| CPS2 | `ddtod` | 82.392 | 86.726 | +5.26% | 1 | 1 |
| CPS2 | `ssf2` | 83.510 | 89.411 | +7.07% | 1 | 1 |
| MVS | `wjammers` | 55.166 | 65.240 | +18.26% | 2 | 2 |
| MVS | `pbobbl2n` | 66.025 | 76.580 | +15.99% | 3 | 3 |
| MVS | `trally` | 71.615 | 83.504 | +16.60% | 2 | 3 |

The **equal-ROM-weight geometric mean slowdown is +11.26%** across the ten elapsed-time ratios. Summing per-ROM mean elapsed times gives a slowdown of **11.20% for CPS1**, **5.94% for CPS2** and **16.87% for MVS** (not the same measure as the ten-ROM geometric mean). These numbers are *descriptive observations only*, subject to the build-equivalence and state concerns below.

### Repeatability and emulated state

- All 30 hybrid trials finished 10,000 frames and reported `io_ops=0` via the existing POSIX/stdio/fileXio/fio wrappers; full lower-level IOP-RPC I/O coverage is not yet certified. The hybrid run's per-ROM configuration hashes remain constant; timing CV is below 0.013% on all ten hybrid ROMs.
- CPS1 `slammast`, `dino`, `punisher` and CPS2 `avsp`, `ddtod`, `ssf2` have 3/3 matching combined final-state hashes **within both builds**. Their sampled M68000, Z80 and RAM hashes also match across GCC and the hybrid build.
- CPS1 `sf2hf` has differing hashes between and within compiler builds, including sampled M68000, Z80 and RAM state. Its timed runs must not be treated as a proven identical workload.
- MVS `wjammers`, `pbobbl2n`, `trally` each reproduce sampled M68000 and main RAM hashes across all six GCC/hybrid executions, but their sampled Z80 sound-CPU hashes vary. Asynchronous audio scheduling remains an unresolved source of end-state drift.
- Matching **final** sampled fingerprints do not prove identical frame-by-frame trajectories, audio/chip/video state or full determinism. The strict benchmark reporter correctly accepts **zero** GCC/LLVM speedups; all trial statuses remain `state=unverified`.

## Compiler composition, provenance and limitations

| Core | NJEMU objects from Clang 24 | NJEMU fallback objects from GCC 15.2.0 |
| --- | ---: | ---: |
| CPS1 | 64 | 4 (`sound/sndintrf.c`, `common/ui_layout.c`, `emumain.c`, `cps1/timer.c`) |
| CPS2 | 61 | 3 (`common/ui_layout.c`, `emumain.c`, `cps2/timer.c`) |
| MVS | 64 | 4 (`sound/sndintrf.c`, `common/ui_layout.c`, `emumain.c`, `sound/ym2610.c`) |

The fallback objects are the unmodified object binaries from the earlier GCC baseline; no NJEMU sources were compiled under GCC *in the hybrid build itself* except these explicit reused objects. `object_manifest.tsv` records the provider and SHA-256 for **all 200 NJEMU objects**. The existing GCC SDK archives were neither rebuilt nor modified; their SHA-256 fingerprints match those in the GCC baseline manifests.

**Compiler source provenance differs**, even though both build manifests cite NJEMU commit `710d6697a1da7c7e5afe1656835263cd7f62587f`. The GCC matrix was built from an empty tracked diff (SHA-256 `e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855`). The hybrid build used tracked-diff SHA-256 `9ce38678be9ad97fbb085a0b5eb7fa75c9aaf1479a6d45521e099a50e3659673`, including the user-provided C replacement for `lzw` in `ps2_video.c`, a PS2 GS finish-condition correction, and the no-cache Clang warning correction in `ps2_cache_storage.c`. Those changes should be included in **both** builds before claiming compiler-only performance differences. GCC also retains some GCC-only tuning flags (`-funroll-loops`, `-fipa-pta`, `-fsingle-precision-constant`), even though both use `-O3 -ffast-math`, LTO off. These implementation differences are part of the initial compiler comparison's limitations.

The experimental LLVM branch is Clang `24.0.0git` at LLVM revision `7543a1e5dadb`. The R5900 floating-point condition-code problem and stand-alone reproduction are documented in [PS2 LLVM/R5900 First Compiler Integration Report](PS2_LLVM_R5900_INITIAL_REPORT.md), using `benchmarks/ps2/llvm_r5900_fp_compare_probe.c`.

## Local artifacts and recovery after interruption

All benchmark evidence is stored in ignored local paths, not committed ROM files:

- GCC baseline: `build_ps2_compiler_smoke/pcsx2_gcc_m3_20261021/aggregate/{trials.csv,summary.md}` (30 trials, all GCC).
- LLVM 24 hybrid: `build_ps2_compiler_llvm_20261021/hybrid_matrix_20261021/aggregate/{trials.csv,summary.md}` (30 trials, all labelled LLVM *hybrid*).
- Descriptive comparison for future LLVM branches: `build_ps2_compiler_llvm_20261021/hybrid_matrix_20261021/aggregate/exploratory_gcc_vs_llvm24_hybrid.csv`. This reports mean/median seconds, slowdown, timing CV and state checks.
- Per-ROM PCSX2 logs and CSVs: `build_ps2_compiler_llvm_20261021/hybrid_matrix_20261021/{cps1,cps2,mvs}-<rom>/`. Reproduction launcher: `.../run_serial.sh`, which is for a **fresh output directory** and will refuse to overwrite existing logs.
- Object compiler provenance: `build_ps2_compiler_llvm_20261021/hybrid_matrix_20261021/object_manifest.tsv` and `EXPERIMENT_CLASSIFICATION.txt`; source toolchain file: `build_ps2_compiler_llvm_20261021/llvm-r5900-toolchain.cmake`.

The interrupted session completed 27 trials. Its first `trally` attempt was interrupted after ROM loading, with no benchmark result. The incomplete log and private stage were **preserved** under `mvs-trally/interrupted_trial_20261021/`; three fresh `trally` trials were then executed without rerunning or overwriting the other 27. The final aggregate contains **exactly 30 completed logs**; the interrupted log is excluded.

## Next LLVM branch comparison protocol

1. First eliminate all per-source GCC fallbacks by correcting the R5900 float-compare backend (including proper handling of ordered/unordered predicates). Confirm the standalone float probe assembles for `-march r5900` and behaves correctly on supported input values.
2. Build **both GCC and the next LLVM branch against precisely the same NJEMU source snapshot**, including the portable `lzw` change, with their C flags and object ownership explicitly recorded. Continue linking against unchanged GCC-built PS2SDK/gsKit/ps2_drivers.
3. Reuse the same resident ROM matrix, sound-on/cache-off configuration, isolated save fixtures, 500+10,000-frame workload, and three sequential runs per ROM. Place each compiler revision in a separate ignored results directory. Do not overwrite this first reference.
4. Compare both against the original GCC timing reference and **against the LLVM 24 hybrid reference**, but do not treat compiler gains as accepted until source, configuration, I/O, emulator environment and emulated execution equivalence checks pass.
5. Investigate CPS1 `sf2hf` main-state divergence and MVS Z80/audio scheduling separately from compiler performance.

**No modifications to `resources/` were needed. The unrelated stashes and the user's uncommitted `src/ps2/ps2_video.c` changes remain preserved.**
