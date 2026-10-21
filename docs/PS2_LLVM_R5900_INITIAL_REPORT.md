# PS2 LLVM/R5900 First Compiler Integration Report

Date: 2026-10-21. Repository: `feature/ps2-compiler-benchmark`, source HEAD `710d6697a1da7c7e5afe1656835263cd7f62587f`.

## Purpose and result

Attempt the first mixed-toolchain NJEMU build: LLVM/Clang compiles NJEMU C files for R5900; the existing PS2 GCC compiler links the resulting objects against **unchanged GCC-built** PS2SDK, gsKit and ps2_drivers. Keep the established benchmark configuration (`GUI=OFF`, `USE_CACHE=OFF`, sound on, 500 warm-up frames, 10,000 measured frames, `-O3 -ffast-math`, LTO off). Build the CPS1, CPS2 and MVS cores separately.

**Status: LLVM compilation is partially working, but the first compiler version cannot yet produce an all-LLVM R5900 ELF. No valid LLVM/GCC performance comparison exists.** One explicitly hybrid **diagnostic** ELF linked and ran in PCSX2; it must never enter the accepted compiler benchmark aggregate.

## Environment

- LLVM Clang/Clang++: `$PS2DEV/llvm/bin`, Clang `24.0.0git` at source revision `7543a1e5dadb`.
- PS2 GCC linker and external assembler: `mips64r5900el-ps2-elf-gcc` and `mips64r5900el-ps2-elf-as`; GCC `15.2.0`.
- Adapted the user-supplied toolchain to the **installed** LLVM builtin headers at `$PS2DEV/llvm/lib/clang/24/include` and GCC builtin headers at `$PS2DEV/ee/lib/gcc/mips64r5900el-ps2-elf/15.2.0/include`. The original example's version `22` and `15.1.0` header directories are not present.
- Clang target flags: `--target=mips64el -mcpu=r5900 -mabi=n32 -fno-pic -mno-abicalls -G0 -mno-gpopt -fomit-frame-pointer`. GCC remains the ELF linker, with the supplied PS2SDK linker script and archive paths.
- All experiments and logs are under ignored `build_ps2_compiler_llvm_20261021/`, including the adapted `llvm-r5900-toolchain.cmake`. The historical GCC 30-trial baseline is under `build_ps2_compiler_smoke/pcsx2_gcc_m3_20261021/` and was **not** overwritten.

## Compile compatibility audit

A complete `ninja -k 0` pass was performed with Clang targeting R5900, using the external GNU PS2 assembler and `-fno-addrsig` (details below):

| Core | Clang-compiled objects | Failed source files | Full LLVM ELF |
| --- | ---: | ---: | --- |
| CPS1 | 64 | 4 | No |
| CPS2 | 61 | 3 | No |
| MVS | 64 | 4 | No |

The failed objects are:

- CPS1: `src/sound/sndintrf.c`, `src/common/ui_layout.c`, `src/emumain.c`, `src/cps1/timer.c`.
- CPS2: `src/common/ui_layout.c`, `src/emumain.c`, `src/cps2/timer.c`.
- MVS: `src/sound/sndintrf.c`, `src/common/ui_layout.c`, `src/emumain.c`, `src/sound/ym2610.c`.

The primary blocker is LLVM's floating-point predicate selection: Clang emits `c.olt.s`, `c.ole.s`, `c.ult.s` and `c.ule.s` for the R5900 despite these opcodes being rejected as unsupported by the installed PS2 GNU assembler. The problem reproduces in plain C **without NJEMU**, and is unchanged with `-fno-fast-math`. The legal R5900 `c.lt.s`, `c.le.s`, and `c.eq.s` instructions were separately accepted by the same assembler. Avoid blindly substituting unordered predicates: NaN behavior and branch inversion need proper target-specific lowering. An actionable source location is `llvm/lib/Target/Mips/MipsISelLowering.cpp`, function `condCodeToFCC` (and the corresponding GlobalISel/FastISel paths), which maps ordered/unsigned IR floating comparisons to unsupported R5900 predicates. Implement R5900-specific legal predicate mapping/expansion with tests in LLVM rather than bypassing assembler checks or generating MIPS IV instructions.

Minimal reproduction (`benchmarks/ps2/llvm_r5900_fp_compare_probe.c`):

```sh
export PS2DEV=/Users/fjtrujy/toolchains/ps2/ps2dev
export PATH="$PS2DEV/llvm/bin:$PS2DEV/ee/bin:$PATH"
clang --target=mips64el -mcpu=r5900 -mabi=n32 -fno-pic -mno-abicalls \
  -O3 -ffast-math -fno-addrsig -S \
  benchmarks/ps2/llvm_r5900_fp_compare_probe.c -o /tmp/r5900-compare.s
mips64r5900el-ps2-elf-as -march r5900 -mabi n32 -EL \
  -o /tmp/r5900-compare.o /tmp/r5900-compare.s
# Expected on this Clang build: unsupported c.olt.s / c.ole.s, etc.
```

Secondary compatibility findings:

1. The LLVM integrated assembler rejects the `ei` instruction in PS2SDK's `ExitHandler()` inline-assembly macro (`src/ps2/ps2_video.c`). Clang can still compile that source using the **GNU** assembler: `-fno-integrated-as -B/path/to/directory/containing/as`, where `as` is a symlink to `$PS2DEV/ee/bin/mips64r5900el-ps2-elf-as`. This is a *Clang compilation* with external assembly, not a GCC-compiled NJEMU object. `-fno-addrsig` is also needed because GNU `as` rejects LLVM's `.addrsig` directive.
2. In `USE_CACHE=OFF` builds, Clang `-Werror` caught an otherwise-unused `cache_storage_available` variable. The variable and assignment are now conditional on `PS2_FAST_CACHE`; the setter discards its argument in no-cache mode. This is a conventional C fix, not an optimization waiver.
3. Clang also diagnosed `while (!GS_CSR_FINISH)` in PS2 video: the SDK macro expands to a bitwise expression; testing its whole result requires `while (!(GS_CSR_FINISH))`. The local fix preserves the existing user-provided **C replacement for the `lzw` inline-assembly implementation**. `src/ps2/ps2_video.c` was already modified by the user before this session, so that file is intentionally **not included** in the report commit or reset.

## Hybrid ABI/functional diagnostic (not a compiler benchmark)

To exercise linking and runtime compatibility while waiting for the backend fix, **CPS1 only** was linked from 64 Clang-compiled NJEMU objects plus the **four GCC-built NJEMU objects listed above**, using the GCC linker and existing GCC-built SDK archives. This diagnostic ELF exists at `build_ps2_compiler_llvm_20261021/CPS1_HYBRID_LINK_PROBE.ELF` and links as a static MIPS N32 executable.

One PCSX2 `dino` trial **completed** 500 warm-up + 10,000 measured frames with `sound=on`, `cache=off`, `resident=on`, wrapped `io_ops=0`; it recorded `total_us=71595190` (71.595 seconds), `state=unverified`, `state_hash=14e604d23dc83216`. The private, misleadingly `compiler=LLVM` record is stored **only** under `build_ps2_compiler_llvm_20261021/CPS1_HYBRID_SMOKE_DIAGNOSTIC_ONLY/` and must be excluded from standard `GCC versus LLVM` reporting because it is a *hybrid* and the source tree differs from the historical GCC baseline. This value must **not** be presented as an LLVM slowdown/speedup. The trial is useful only to show that a majority-Clang binary can initialize, load a game, produce sound and finish in PCSX2.

## Required next steps

1. Fix R5900 floating-condition legalization in LLVM; prove `llvm_r5900_fp_compare_probe.c` assembles with `-march r5900` and validate floating comparison behavior, including exceptional inputs.
2. Either implement `ExitHandler()` inline `ei` support in the LLVM integrated assembler, or retain Clang compilation with the external PS2 GNU assembler, after validating its emitted instructions; keep `-fno-addrsig` when using GNU `as`.
3. Rebuild **all NJEMU sources** with Clang for CPS1, CPS2 and MVS; inspect the object manifest and confirm the only GCC-compiled code comes from the explicitly permitted SDK archives, linker startup and libraries. No hidden per-source GCC fallback is acceptable for the final test.
4. Rebuild/rerun the GCC baseline against **the same source tree and LZW/GS changes**, since its recorded worktree hash differs from the current LLVM experiments. Compare equivalent configuration, assets, compiler optimization policy and 500/10,000-frame workload.
5. Run three full trials per ROM on PCSX2, produce the first real LLVM aggregate report and keep `state=unverified` until known sound/Z80 and remaining CPS1 state divergence are resolved.

The first LLVM version is therefore sufficient to test Clang-to-GCC object compatibility and identify precise R5900 backend gaps, but **not yet to generate valid LLVM performance measurements**.
