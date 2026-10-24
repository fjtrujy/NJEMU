# Windjammers LLVM/MIPS backend investigation — 2026-10-25

Scope: NJEMU MVS `wjammers`, GCC 15.2.0 versus LLVM/Clang 24.0.0git
`455ff9dd4b8a1c119df8046ddb19320d95f13fa8` from
`r5900-feature`. Branch `feature/ps2-compiler-benchmark`.
Baseline NJEMU HEAD at build: `f58bef110ac8810c5b37455afed0b8ccb668f420`.
No ROM, emulator source, LLVM source, or SDK file was changed.

## Conclusion

**At least one independently reproducible missed optimization is in shared
LLVM MIPS lowering**, not exclusively in the R5900 machine scheduler:
a branchy `uint8_t` return generates a redundant `andi result, 255`
after an `lbu` in both `-mcpu=r5900` and `-mcpu=mips3`. This pattern
occurs in NJEMU `m68000_read_memory_8`; GCC does not emit the mask on
the frequent RAM-return path. A standalone C99 reproducer is included at
[`docs/reproducers/mips_zeroext_phi_repro.c`](reproducers/mips_zeroext_phi_repro.c).

However, **do not conclude that one generic MIPS bug explains all four
hotspots**. Integer-only `Cz80_Exec` generated for generic MIPS III runs
*faster* than the R5900-targeted LLVM object in PCSX2, while the reverse
is true for `C68k_Exec`. Disabling the R5900-only issue pass has little
effect, and the generic MIPS delay-slot filler provides a large benefit.
YM2610 cannot be correctly built for generic MIPS III and executed on the
PS2: that code emits unsupported COP1/FPU operations.

All successful full-emulator measurements remain **unverified**: the 68000
and RAM end-state fingerprints agree across tested variants, but the
asynchronous Z80 final state varies even between identical trials. PCSX2
guest elapsed time is not a physical R5900 cycle-accurate measurement.

## Repeated homogeneous compiler baselines

- PCSX2 **v2.9.142** with `-nogui -batch -unlimited -elf`.
- Exactly 500 warm-up frames + 10,000 measured frames per trial;
  `-O3 -ffast-math`, GUI/cache off, sound on, ROM resident.
- NJEMU sources compiled with GCC or Clang; both link against the same
  GCC-built PS2SDK/gsKit/ps2_drivers using GNU ld. LLVM VU0 is **not**
  explicitly enabled.
- Each repeated run reports `io_ops=0` on audited wrappers; this does
  **not** exclude other indirect IOP I/O.

| Executable | Median guest time | Relative to fresh GCC | Trials |
| --- | ---: | ---: | ---: |
| Fresh GCC 15.2 | **55.326542 s** | baseline | 3 |
| LLVM 455ff9dd | **58.574831 s** | +5.87% | 3 |
| Prior LLVM 6f31245b | 61.442633 s | +11.02% against its contemporary GCC control | 3 |

The latest Clang gains **2.867802 s (4.67%)** over the previous LLVM
revision in PCSX2, but a **3.248289 s** gap remains to fresh GCC.

## Object-level isolation

Each experiment starts from the LLVM ELF and directly substitutes freshly
GCC-compiled `-O3` translation-unit objects with an otherwise identical
source snapshot, SDK and link command. Original LLVM objects and ELF are
restored after each test. One-run measurements are explicitly preliminary.

| GCC objects substituted into the LLVM build | Median/only run (s) | Repeat count | Saved vs pure LLVM (s) |
| --- | ---: | ---: | ---: |
| None | 58.574831 | 3 | — |
| `src/cpu/m68000/c68k.c.obj` | 56.826993 | 3 | 1.747838 |
| `src/mvs/memintrf.c.obj` | 58.316698 | 1 | 0.258133 |
| `src/cpu/z80/cz80.c.obj` | 57.718298 | 1 | 0.856533 |
| `src/sound/ym2610.c.obj` | 57.977319 | 1 | 0.597512 |
| `memintrf.c.obj` + `cz80.c.obj` | 57.490814 | 1 | 1.084017 |
| `memintrf.c.obj` + `ym2610.c.obj` | 57.748198 | 1 | 0.826633 |
| All four above | **55.171087** | 3 | **3.403744** |
| Pure GCC | **55.326542** | 3 | 3.248289 |

These differences are not necessarily additive because linking different
objects also changes code placement. The four-object hybrid is slightly
faster than the pure-GCC control, which illustrates this limitation. The
majority-LLVM hybrid logs still say `compiler=LLVM` regardless of their
substituted objects.

## Static R5900 code generation — actual function sections

Census extracted with GNU `objdump -d -j .text.<function>`, **not**
`--disassemble=<function>` (which on this toolchain can erroneously
display the wrong code across sections). Counts are static, not sampled.

| Function | Compiler | Function bytes | Instructions | `nop` | `addiu` | `sll` |
| --- | --- | ---: | ---: | ---: | ---: | ---: |
| `C68k_Exec` | GCC | 214,616 | 53,654 | 366 | 4,588 | 3,221 |
| `C68k_Exec` | Clang R5900 | 223,204 | 55,802 | 777 | 5,122 | 4,383 |
| `Cz80_Exec` | GCC | 23,436 | 5,860 | 972 | 510 | 148 |
| `Cz80_Exec` | Clang R5900 | 14,652 | 3,664 | 103 | 473 | 152 |
| `YM2610ContextUpdate` | GCC | 10,700 | 2,676 | 207 | 135 | 22 |
| `YM2610ContextUpdate` | Clang R5900 | 4,408 | 1,102 | 138 | 53 | 10 |

LLVM's giant `C68k_Exec` remains 8,588 bytes larger, with an additional
**411 `nop`**, **534 `addiu`** and **1,162 `sll`** instructions. The
fixed, heavily visited dispatch-loop structure makes this a meaningful
codegen lead, but without dynamic basic-block execution counts it does
not isolate a specific source of elapsed guest time.

The opposite result for `Cz80_Exec` and the YM2610 function is
important: LLVM emits substantially **less** code and far fewer static
NOPs, yet GCC substitution makes the emulator faster. Code size and
instruction totals therefore cannot be used as performance proxies.

## Concrete shared-MIPS zero-extension failure

The `m68000_read_memory_8` RAM hot path (see
`src/mvs/memintrf.c:2209`) in LLVM contains:

```asm
lbu   v0, 0(at)       # already provides a zero-extended 8-bit result
jr    ra
andi  v0, v0, 0xff    # redundant mask in the return delay slot
```

GCC instead emits the effective equivalent of:

```asm
jr    ra
lbu   v0, 0(base)    # useful load in the branch delay slot
```

The standalone C99 reproducer, which uses an early likely RAM-page
return plus switch/callback returns, exhibits the same `lbu; jr; andi`
under **both** `-mcpu=r5900` and `-mcpu=mips3` with Clang 455ff9dd.
GCC 15.2 does not emit the RAM-return mask. A simple
`unsigned char simple_read_byte(unsigned char *p) { return *p; }`
under the same Clang compiler does *not* exhibit the problem, so the
issue appears to involve a multi-path unsigned-byte return and
inter-block known-bit/return-value extension handling.

Reproduce without PS2SDK, input ROMs or hardware:

```sh
SRC=docs/reproducers/mips_zeroext_phi_repro.c
clang -target mips64el -mcpu=r5900 -mabi=n32 -O3 \
  -fno-pic -mno-abicalls -G0 -S "$SRC" -o r5900.s
clang -target mips64el -mcpu=mips3 -mabi=n32 -O3 \
  -fno-pic -mno-abicalls -G0 -S "$SRC" -o mips3.s
mips64r5900el-ps2-elf-gcc -O3 -G0 -S "$SRC" -o gcc.s
```

The Clang source checkout already contains the Oct 24 generic-MIPS
change **`1b2b846b835b`** (zero-extending i8/i16 loads in
`llvm/lib/Target/Mips/MipsISelLowering.cpp`, particularly
`performLOADCombine`). That change does **not** remove this case.
Trace the lowered SelectionDAG `ZEXTLOAD`, return `i8 zeroext`,
PHI/register live-out known bits, and final redundant ANDi. Do not
simply drop all ANDi instructions; only redundant masks may be removed.

This reproduces an optimization miss, **not** a semantic compiler
miscompile. The exact fraction of the 0.258 s whole-`memintrf`
substitution attributable to this one instruction is not established.

## Generic MIPS versus R5900-specific backend experiments

Variants were compiled from the exact same NJEMU C source with one
compiler option changed, substituted **one object at a time** into the
original full LLVM ELF and run in PCSX2. Except for the repeated generic
MIPS3 Z80 trial, these are one-run probes, not rigorous benchmarks.

| Single substituted Clang object/flag | Guest time (s) | Delta vs LLVM 58.574831 s |
| --- | ---: | ---: |
| `C68k_Exec`, R5900 `-disable-mips-r5900-issue` | 58.471695 | -0.103136 |
| `C68k_Exec`, R5900 `-disable-post-ra` | 58.534310 | -0.040521 |
| `C68k_Exec`, R5900 `-disable-mips-df-backward-search` | **61.974340** | **+3.399509** |
| `C68k_Exec`, `-mcpu=mips3` | 59.552051 | +0.977220 |
| `Cz80_Exec`, R5900 `-disable-mips-r5900-issue` | 58.506667 | -0.068164 |
| `Cz80_Exec`, `-mcpu=mips3` | **57.540627** | **-1.034204** (3 trials) |
| YM2610 object, R5900 `-disable-mips-r5900-issue` | 58.381428 | -0.193403 |
| YM2610 object, `-mcpu=mips3` | **invalid** | PCSX2 reported unsupported R5900 COP1/FPU opcodes; terminated, no result |

For integer-only `Cz80_Exec`, the generic MIPS3 function has **3,756**
static instructions versus **3,664** for R5900-targeted LLVM, but is
faster in the three repeated PCSX2 trials. It contains **zero**
`movn/movz` conditional moves compared with **19** in the R5900
variant; it has 84 NOPs versus 103, and 547 `addiu` versus 473.
Those differences identify useful codegen investigation points but
do not yet prove a specific instruction causes the difference.
The R5900-specific issue scheduler being disabled changes the timing
very little, so this cannot be attributed to that pass alone.

As a separate control, compiling the Z80 object with
`-mno-fix-r5900` produced an **identical SHA-256 binary** to the
regular R5900 build. Thus, the R5900 short-loop erratum workaround
does not explain the observed Z80 generic-vs-target-specific
difference in this object. Never disable the erratum fix for
real-hardware builds merely for an emulator performance comparison.

**YM2610 is not suitable for the generic MIPS3 substitution**:
it emitted unsupported R5900 COP1 opcodes (for example
`0x46340302`, `0x46800021`) and could not complete. Generic MIPS3
can choose floating-point instructions not implemented by the
Emotion Engine. This experiment proves neither a valid alternate
toolchain nor an LLVM miscompile.

Relevant backend code to inspect:

- `llvm/lib/Target/Mips/MipsISelLowering.cpp`:
  `performLOADCombine` and return extension/known bits.
- `llvm/lib/Target/Mips/MipsDelaySlotFiller.cpp`:
  shared MIPS delay slot filling, and its R5900-specific short-loop gate.
- `llvm/lib/Target/Mips/MipsTargetMachine.cpp`:
  generic instruction selection and delay filler plus R5900-only
  issue/fold-address/post-RA scheduling.
- `llvm/lib/Target/Mips/MipsR5900IssuePass.cpp`:
  `-disable-mips-r5900-issue` diagnostic option.
- `Cz80_Exec` generic MIPS3 versus R5900 MIR:
  focus on selection of `movn/movz` and surrounding branches/critical
  paths before attributing the guest timing delta to them.

## Validation, artifacts and next steps

- All successful trials: matching sampled M68000 fingerprint
  `1087498fd82be399` and RAM fingerprint
  `d9c70061d1a1352e`. Z80 hashes drift; no formal speedup accepted.
- All tests ran in ignored
  `build_ps2_wjammers_llvm455_20261025/`, with separate
  `trials/<variant>/`, `variants/`, and `static_analysis/`.
  `run_hybrid-diagnostic.sh` backs up and restores objects and
  relinks the original LLVM ELF on exit, including after a failed
  PCSX2 trial.
- The byte-for-byte SHA-256 restoration of the original Clang
  `MVS.ELF` and `c68k.c.obj` was independently verified after
  the substitutions.
- The source tree's `resources/` was **never touched**.
- All performance deltas are specific to the PCSX2 build/runtime,
  not proof of behavior on physical PS2 hardware.

**Priority for LLVM development:**

1. Add the self-contained `guest_read_byte` repro as a generic-MIPS
   codegen test; eliminate the provably redundant `andi` while
   preserving unsigned i8 return semantics and correct delay slot
   ordering.
2. Investigate the LLVM MIPS III versus R5900 `Cz80_Exec` difference
   with a path-sensitive instruction trace or MIR-based reduction.
   The R5900 issue pass and short-loop workaround have already been
   substantially ruled out as explanations for this object.
3. For `C68k_Exec`, focus on hot opcode handler path instruction
   selection, extra shifts/address arithmetic, and branch layout.
   Generic MIPS delay filling and R5900 optimization passes currently
   *help*, not hurt, overall on the tested workload. Do not use
   static instruction counts as a dynamic cost model.
4. Isolate sound-code regressions using an R5900-compatible target
   and targeted YM2610 function instrumentation. Do **not** substitute
   generic MIPS3 YM2610 code on real hardware.
