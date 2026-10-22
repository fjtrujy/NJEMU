# PS2 LLVM/R5900 Windjammers regression: aligned-flag object isolation

Date: 2026-10-22. Branch: `feature/ps2-compiler-benchmark`.
Status: **measured in PCSX2; compiler-result equivalence remains unverified**.

## Question and controlled workload

The previous ten-ROM run identified MVS `wjammers` as the worst regression
(LLVM approximately 20% slower than GCC), but used GCC `-O2` versus Clang
`-O3 -ffast-math`. This investigation rebuilds **both** NJEMU MVS targets with
`-O3 -ffast-math -fomit-frame-pointer`, sound on, GUI off, cache off, and VU0 off.
Architectural/compiler-specific switches remain necessarily different. GCC
uses the installed PS2DEV GCC toolchain; Clang 24 uses the prior experimental
`r5900-feature-toolchain.cmake`, with R5900/N32 target selection, GNU assembler
and GCC final linker. The LLVM toolchain file is in an **ignored earlier
build directory**, not the `origin/llvm:ps2dev_llvm.cmake` tracked file.
GCC links the same prebuilt GCC PS2SDK/gsKit/ps2_drivers archives; its
link step must retain LTO support for gsKit (appending `-fno-lto` to GCC's
global C flags broke archive symbol resolution). No LLVM NJEMU object
fallbacks in the two primary full compiler builds.

Provenance:
- NJEMU base commit `bde750c6ceba1d92012cdf585e582d1ee8d2e43b`
  on `feature/ps2-compiler-benchmark`. The existing, unrelated user edit to
  `src/ps2/ps2_video.c` was preserved; both compilers consumed that same file.
- GCC `mips64r5900el-ps2-elf-gcc` 15.2.0.
- LLVM/Clang 24.0.0git, commit
  `a6b9e2f2e16046e44d1da5ae5d75a1a4927a5c7f`;
  binary SHA256 `db3e0f2908081a57ee8ccca59defbaa33c8a2579c9d9e74404b18f86268f4808`.
- PCSX2 v2.9.93, `-nogui -batch -unlimited -elf`, attract-mode
  `wjammers`, 500 warm-up + 10,000 measured frames, 3 trials for
  each homogeneous baseline and each single-function source-hint control.
  Additional source-object substitutions used one 10,000-frame trial each;
  1,500-frame preliminary probes are not included in the main timing table.
- All trials captured `frames=10000`, `io_ops=0`, `cache=off`,
  `sound=on`, and `status=unverified`. Indirect IOP I/O is not
  comprehensively audited.

## Aligned-optimization baselines and source-hint controls

Values below are medians of **three** trials, total measured time (seconds).
All six homogeneous baseline trials were compiled with matched optimization
flags, and all source-hint comparisons use the same C flags and SDK.

| Build | Median (s) | Notes |
| --- | ---: | --- |
| GCC, original memory callbacks | 55.349 | Original tracked `memintrf.c` object relinked into GCC build |
| GCC, four RAM branch hints | 55.353 | Negligible 0.005 s difference |
| LLVM, original memory callbacks | 66.369 | Original tracked `memintrf.c` object relinked into LLVM build |
| LLVM, `m68000_read_memory_8` hint only | 63.904 | Saves **2.466 s** against original LLVM |
| LLVM, four RAM branch hints | 63.169 | Saves **3.200 s** against original LLVM |

Without the hints, LLVM is **19.91% slower** than GCC under aligned
`-O3 -ffast-math` settings. With the hints, LLVM is **14.12% slower**.
The read-byte hint alone accounts for **77%** of the combined LLVM
improvement; the other three hints add the remainder.

### Specific code generation mechanism

`src/mvs/memintrf.c` contains the PS2 MVS fast path for Neo Geo RAM
(`0x100000..0x1fffff`). The 68000 interpreter calls its memory callbacks
frequently. For `m68000_read_memory_8`, GCC emits an explicit early test
and direct return for that RAM page, while unhinted Clang folds the RAM
page into the generic jump-table address decoder. Clang consequently
executes more dispatch work on frequent RAM reads.

Under `PS2_MVS_RAM_FASTPATH`, applying `__builtin_expect(page == 1, 1)`
to the four 8-/16-bit read/write fast-path conditions makes Clang generate
the direct RAM-page branch again. This is purely a code-layout hint; the
address test and memory semantics are unchanged, and other platforms
remain unaffected. The single-function read-byte control validates the
mechanism under the same 10,000-frame workload.

## GCC-object substitution to attribute the remaining slowdown

Each row below starts with the **four-hint LLVM** build and replaces only
the listed NJEMU-owned translation-unit objects with GCC objects compiled
with matching `-O3 -ffast-math` optimization. SDKs/linker are unchanged.
These single-run hybrid relinks do not constitute all-GCC or all-LLVM
measurements and may change code layout. The `LLVM` label in captured
hybrid logs describes the majority compiler, **not** the substituted
object(s). Compare substitution effects, not absolute verified speedups.

| Replacements in Clang build | Total (s) | Improvement vs four-hint LLVM (s) |
| --- | ---: | ---: |
| None (three-trial median) | 63.169 | - |
| `memintrf.c.obj` | 60.557 | 2.612 |
| `c68k.c.obj` | 59.713 | 3.455 |
| `memintrf.c.obj` + `c68k.c.obj` | 57.086 | 6.082 |
| Above + `cz80.c.obj` | 56.086 | 7.083 |
| Above + `z80.c.obj` instead of `cz80.c.obj` | 57.087 | 6.082 |
| 68000/memory pair + `ym2610.c.obj` | 56.506 | 6.663 |
| 68000/memory pair + `2610intf.c.obj` | 57.113 | 6.055 |
| 68000/memory pair + `sndintrf.c.obj` | 57.091 | 6.078 |
| 68000/memory + both Z80 objects + all three sound objects | 55.479 | 7.690 |
| Pure GCC (three-trial median) | 55.353 | - |

In this controlled PCSX2 workload, the seven-object combination removes
about **98.4%** of the 7.815 s gap between pure GCC and four-hint LLVM.
The separate savings are **not additive**; cross-object and layout
effects are possible.

### Function-level shortlist

1. **`m68000_read_memory_8`** - the highest-confidence, directly
   source-isolated codegen pathology. Single-function hint recovers 2.466 s.
   Other affected callbacks: `m68000_read_memory_16`,
   `m68000_write_memory_8`, and `m68000_write_memory_16`.
2. **`C68k_Exec`** (`src/cpu/m68000/c68k.c`) - main computed-goto
   68000 interpreter. GCC object substitution alone recovers 3.455 s
   from the hinted LLVM baseline. GCC emits **215,536 bytes** of
   `C68k_Exec` machine code, LLVM **243,108 bytes** (~12.8% more);
   larger code is a clue, not proof of the bottleneck mechanism.
3. **`Cz80_Exec`** (`src/cpu/z80/cz80.c`) - adding the GCC CPU
   core after the 68000/memory pair saves **1.000 s**. Swapping only
   the `z80.c` wrapper does not help; inspect `Cz80_Exec`'s generated
   instruction dispatch and branch scheduling.
4. **`YM2610ContextUpdate`** (`src/sound/ym2610.c`) - likely sound
   hot function. Swapping the `ym2610.c` translation unit saves
   **0.580 s** on top of the 68000/memory pair. This identifies the
   **source file**, not conclusively one function; a targeted internal
   function-level timer or generated-code audit is needed before
   assigning all of the cost to `YM2610ContextUpdate`.
   Swapping `2610intf.c` and `sndintrf.c` does not explain the gap.

### Static R5900 instruction census (diagnostic, not sampled execution)

GNU MIPS `objdump -d` over the interpreter's respective object sections yields:

| Function | Compiler | Total instructions | `addiu` | `nop` | `jalr` |
| --- | --- | ---: | ---: | ---: | ---: |
| `C68k_Exec` | GCC | 53,884 | 4,592 | 371 | 3,080 |
| `C68k_Exec` | LLVM | 60,778 | 9,525 | 1,117 | 3,082 |
| `Cz80_Exec` | GCC | 5,860 | - | 972 | 76 |
| `Cz80_Exec` | LLVM | 3,784 | - | 120 | 75 |

LLVM's 68000 interpreter has 6,894 more static instructions, including
4,933 more `addiu` and 746 more `nop` instructions. Repeated
address arithmetic, immediate folding, and delay-slot scheduling are
plausible **LLVM backend audit targets**, not proven dynamic causes:
the 68000 opcode interpreter has many distinct basic blocks, and these
counts do not weight how frequently each block executes. LLVM's
smaller `Cz80_Exec` is a useful counterexample to treating code
size as a direct performance predictor. Per-block execution profiling or
a smaller opcode-level reproducer is still needed.

## Remaining validation limits and next steps

- PCSX2 guest-clock measurements are **not physical PS2 performance**.
  The M68000 and RAM end fingerprints matched across three full-length
  homogeneous builds, but the **Z80 fingerprints diverged even between
  trials of the same build**. A deterministic audio/input replay and
  per-frame state oracle are still required; the strict speedup
  acceptance count is zero. `io_ops=0` measures the instrumented
  wrappers, not every possible indirect SDK/IOP call.
- The source hint preserves address semantics and compiled under both
  compilers, but this is a provisional PS2/PCSX2 codegen optimization.
  Retest on real R5900 hardware before making production performance
  claims.
- Next investigate why R5900 LLVM emits larger/slower code for
  `C68k_Exec`, then `Cz80_Exec`, then the YM2610 synthesis path.
  Prefer small backend reproductions and register/branch/instruction
  comparisons before changing emulator semantics.
- The original `origin/llvm:ps2dev_llvm.cmake` is still separate.
  Consolidating the experimental LLVM toolchain flags into a canonical
  tracked configuration is recommended before broader compiler matrix
  reruns.

## Local artifacts

Everything below is ignored and remains in the user's working checkout:

`build_ps2_compiler_wjammers_aligned_20261022/`

- `gcc/` and `llvm/`: matching-optimization MVS build directories and ELFs.
- `trials_full/{gcc,llvm}/`: 3 x 10,000-frame hinted baselines.
- `trials/{gcc_original_10000,llvm_original_10000,likely_read8_full}/`:
  3 x 10,000-frame source-hint controls each.
- `trials/gcc_*_full/`: individual and combined GCC-object substitution logs.
- `patches/`, `custom_objects/`, `unhinted_objects/`,
  `gcc_unhinted_objects/`, `likely_read8_objects/`,
  `hybrid_backups/`: isolated test inputs, generated objects and link
  transcripts.
- `run_hybrid.sh`: ephemeral zsh harness for direct object substitution
  and restoring/relinking the original ELF after each run. Never run
  Ninja's normal incremental build between swapping objects and the
  direct link: it would silently recompile the substituted source.
- No ROM, BIOS, cache or save-state content under `resources/` was edited.
