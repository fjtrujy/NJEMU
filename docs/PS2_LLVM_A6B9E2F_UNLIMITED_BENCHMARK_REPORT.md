# PS2 LLVM a6b9e2f benchmark: PCSX2 unlimited-mode rerun

Date: 2026-10-22. Status: **completed functional/timing run, compiler comparison unverified**.

## Scope and provenance

- Emulator: PCSX2 v2.9.93 on macOS, launched with `-nogui -batch -unlimited -elf` for every trial. This explicitly enables PCSX2's *unlimited fast-forward* setting, not merely the NJEMU guest frame-limit override. PCSX2 guest-clock timings are **not real PS2 hardware measurements**.
- NJEMU source: `684d20cf5c93b18fe64d4da27f9e5f2e5dd132b2` on `feature/ps2-compiler-benchmark`. Build manifests record tracked working-tree diff SHA256 `0f1fdf762ebef4e2041f1fe441bc3fe9360a966178d0d39dd97bb9cb99de8df3`; the pre-existing, unrelated `src/ps2/ps2_video.c` modification was preserved untouched.
- GCC: `mips64r5900el-ps2-elf-gcc` 15.2.0, normal PS2 toolchain with `-O2`.
- LLVM: Clang 24.0.0git, `rickgaiser/llvm-project` revision `a6b9e2f2e16046e44d1da5ae5d75a1a4927a5c7f`; installed binary SHA256 `db3e0f2908081a57ee8ccca59defbaa33c8a2579c9d9e74404b18f86268f4808`. LLVM uses `--target=mips64el -mcpu=r5900 -mabi=n32`, `-O3 -ffast-math`, with and without `-mvu0`.
- Six LLVM PS2 builds and three GCC PS2 builds succeeded. Ninja compile-command audits showed 68 CPS1, 64 CPS2, and 68 MVS Clang translation-unit compiles per LLVM variant, **zero GCC NJEMU object fallbacks**. The final linker driver, PS2SDK, gsKit, ps2_drivers and other installed SDK libraries remain GCC-built. SDK archive digests and flags are recorded in each `benchmark_build.txt`.
- Settings: CPS1/CPS2/MVS, `GUI=OFF`, `USE_CACHE=OFF`, `SAVE_STATE=ON`, 500 warm-up + exactly 10,000 measured frames, sound on, video on, no guest frame pacing, attract-mode workloads. Ten resident ROMs, three sequential trials per ROM per variant.
- The host runner was corrected to pass PCSX2 `-unlimited` and its fake-PCSX2 regression was updated; all four host CTest cases passed. Launcher change: `684d20cf`. Old partially captured normal-speed logs remain separate and were excluded.

## Descriptive results

Times below are the **median total measured duration** of three trials, in seconds. Improvement percentage is `(GCC_duration - LLVM_duration)/GCC_duration*100`; a negative percentage indicates LLVM is slower. These calculations are informational **only**, not results accepted by the strict equivalence gate.

| Core | ROM | GCC (s) | LLVM no VU0 (s) | LLVM VU0 (s) | LLVM VU0 improvement vs GCC |
| --- | --- | ---: | ---: | ---: | ---: |
| CPS1 | slammast | 65.527 | 70.122 | 70.113 | -7.00% |
| CPS1 | dino | 64.814 | 70.590 | 70.556 | -8.86% |
| CPS1 | punisher | 63.232 | 69.447 | 69.421 | -9.79% |
| CPS1 | sf2hf | 65.319 | 72.232 | 72.212 | -10.55% |
| CPS2 | avsp | 80.266 | 83.930 | 84.006 | -4.66% |
| CPS2 | ddtod | 82.726 | 86.296 | 86.433 | -4.48% |
| CPS2 | ssf2 | 83.957 | 88.663 | 88.732 | -5.69% |
| MVS | wjammers | 55.363 | 66.377 | 66.395 | -19.93% |
| MVS | pbobbl2n | 66.146 | 76.374 | 76.366 | -15.45% |
| MVS | trally | 72.019 | 84.253 | 84.230 | -16.96% |

- Equal-ROM-weight geometric mean of LLVM/GCC median **duration ratios**: 1.101975 without VU0 and 1.102184 with VU0. Thus the descriptive LLVM with-VU0 durations are approximately **10.22% longer** than GCC and its geometric-mean GCC/LLVM speedup is 0.90729x.
- VU0's observed timing effect is tiny, between about -0.16% and +0.05% per ROM when comparing the two LLVM variants directly. Bytewise object-file comparison showed CPS1 `ym2151.c.obj` and `ui_layout.c.obj` changed; only `ui_layout.c.obj` differed in CPS2 and MVS. This workload does not demonstrate a useful VU0 performance effect.
- MVS had the largest elapsed-time regressions under this emulator configuration; `wjammers` was approximately 19.93% slower in the VU0 build.
- Do **not** compare these elapsed times directly to the earlier LLVM 24 hybrid benchmark: the toolchain, source provenance and PCSX2 speed mode changed.

## Validation and limitations

All **90/90** raw logs contained exactly one `NJEMU_BENCHMARK_V1` record. Every record reported `warmup=500`, `frames=10000`, `expected=10000`, `resident=on`, `cache=off`, `sound=on`, `scenario=attract`, `io_ops=0`, `io_bytes=0`, and `state=unverified status=unverified`. The wrapper-level zero-I/O claim does not certify every possible indirect SDK/IOP access.

State fingerprint findings:
- `slammast`, `dino`, and `punisher` had one matching final combined state hash across all three variants and all three trials.
- `sf2hf` and all three MVS titles showed within-variant divergence; the three CPS2 titles had consistent hashes *within* each variant but different hashes *between* compiler variants.
- Same end hashes do not prove identical frame-by-frame execution, and differing hashes are a comparability blocker. The mandatory deterministic state trajectory oracle is still missing.

Consequently, **0/90** trials were accepted by the repository's strict compiler-speedup reporter. These are repeatable descriptive PCSX2 guest-clock observations, **not verified LLVM-versus-GCC speedups**. GCC and LLVM also have different optimization flags (`-O2` versus `-O3 -ffast-math`), so the table describes the exact configured toolchains rather than a flag-normalized compiler isolation test. Physical PS2 benchmarking remains pending because the PS2Link hardware startup problems documented elsewhere were not retested.

## Raw data and reports

Ignored local directory (do not commit raw ROM, BIOS, save, or transient logs):

`build_ps2_compiler_llvm_a6b9e2f_20261022/`

- `gcc_unlimited_matrix/aggregate/trials.csv` and `summary.md` (30 trials).
- `no_vu0_unlimited_matrix/aggregate/trials.csv` and `summary.md` (30 trials).
- `with_vu0_unlimited_matrix/aggregate/trials.csv` and `summary.md` (30 trials).
- `descriptive_comparison_unlimited.csv` (one median row per ROM).
- `gcc_{cps1,cps2,mvs}/benchmark_build.txt`, `no_vu0_{cps1,cps2,mvs}/benchmark_build.txt`, and `with_vu0_{cps1,cps2,mvs}/benchmark_build.txt` (source, compiler, flags, SDK archive hashes).
- Individual PCSX2 logs in each `*_unlimited_matrix/<core>-<rom>/` directory, all retained.

No file in `resources/` was modified.
