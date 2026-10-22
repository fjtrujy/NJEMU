# Windjammers: concrete C source behind LLVM/R5900 68000 slowdown

Date: 2026-10-22. Branch: `feature/ps2-compiler-benchmark`.
Status: source-level ablation and safe-hint experiment **complete in PCSX2**;
formal GCC/LLVM deterministic equivalence remains **unverified**.

## Exact hot C code

The strongest localized regression is in
`src/cpu/m68000/c68k.c`, at the `C68k_Exec_Next` dispatch loop:

```c
C68k_Exec_Next:
    if (CPU->ICount > 0)
    {
        if (BusErrHandling) {
            printf("BusError occured\n");
            SWAP_SP();
            PUSH_32_F(GET_PC() - 2);
            PUSH_16_F(GET_SR());
            CPU->A[7] -= 2;
            PUSH_32_F(BusErrAdr);
            CPU->A[7] -= 2;
            CPU->flag_S = C68K_SR_S;
            PC = READ_MEM_32((C68K_BUS_ERROR_EX) << 2);
            SET_PC(PC);
            BusErrHandling = 0;
        }

        Opcode = READ_IMM_16();
        PC += 2;
        goto *JumpTable[Opcode];
        #include "c68k_op.c"
    }
```

`BusErrHandling` is declared/initialized in the same source file;
a repository-wide source search found no writer other than its own reset
to zero. The handler still represents intended bus-error semantics and
**must not simply be deleted**. The nonzero branch remains visible to
the compiler at each dispatch, and the inlined stack/exception work
constrains its code generation in a ~240 KB computed-goto function.
The large slowdown is consistent with register-allocation and block
layout sensitivity; instruction-level cause is **not yet proven**.

`src/cpu/m68000/c68kmacro.h` is the other high-traffic C location:
- `RET(A)` / `USE_CYCLES(A)`: decrement `CPU->ICount` and return to
  the shared dispatch label for almost every instruction.
- `BITOP_STATIC(TST, 8, AL)`: used by opcode `0839`.
- `TST(8, AL)`: used by opcode `4a39`.
- `Bcc_8(NE)`: used by opcodes `66f8` and `66f2`.
These are frequently executed paths, but the specific slowdown
confirmed by source ablation is the *shared* bus-error branch, not the
individual opcode macro spellings.

## Measured opcode distribution

A **GCC-only diagnostic build** counted executed 68000 opcodes during
Windjammers attract mode (500 warm-up + 1,500 measured frames).
Clang cannot assemble the extra instrumentation in this large function
on revision `a6b9e2f`: GNU as reports `branch out of range`. Counts
come from the GCC emulator, not a sampled LLVM binary.

| Opcode | Handler | Dynamic executions |
| --- | --- | ---: |
| `4e71` NOP | `c68k_op.c:1207` | 6,614,311 |
| `0839` BTST byte static absolute-long | `c68k_op.c:234` | 3,659,480 |
| `4a39` TST byte absolute-long | `c68k_op.c:1051` | 3,523,051 |
| `66f8` BNE byte -8 | `c68k_op.c:1626` | 3,498,616 |
| `66f2` BNE byte -14 | `c68k_op.c:1626` | 3,148,932 |

Total 68000 instructions: **35,274,090**. These five opcode values
account for approximately **58%** of dynamic instructions. The
repeated dispatch loop and its `BusErrHandling` branch are therefore
reached tens of millions of times. Opcode frequencies do not indicate
per-opcode latency by themselves.

## Controlled C source experiments

Both compilers use the same NJEMU source snapshot and optimization
level `-O3 -ffast-math`. LLVM is Clang 24.0.0git at
`rickgaiser/llvm-project:a6b9e2f2e16046e44d1da5ae5d75a1a4927a5c7f`;
GCC is PS2 GCC 15.2.0. The installed GCC PS2SDK, gsKit, driver archives
and GCC final linker stay fixed. PCSX2 v2.9.93 is launched
`-nogui -batch -unlimited -elf`, no ROM streaming cache, sound on,
500 warm-up + 10,000 measured frames, one Windjammers ROM.
The previous four PS2 MVS RAM fast-path branch hints are active.

| LLVM interpreter experiment | 10,000-frame time (s) | Effect |
| --- | ---: | --- |
| Original C loop (3-trial median) | 63.169 | Reference |
| Remove entire `if (BusErrHandling)` block (1 trial) | 57.126 | **6.043 s saved** |
| Keep block, mark condition unlikely (3-trial median) | 62.575 | **0.594 s saved** |
| Mark `CPU->ICount > 0` likely (1 trial) | 62.524 | 0.644 s saved |
| Mark both likely/unlikely (1 trial) | 63.693 | Regression; effects not additive |
| Mark `Bcc_8(NE)` condition likely (1 trial) | 63.145 | No meaningful improvement |
| Explicitly spell out `OP(nop)` cycle decrement (1 trial) | 63.145 | No meaningful improvement |

**Critical separation:** deleting the bus-error branch is a
*diagnostic ablation that changes behavior*. It isolates the source
block that makes Clang's generated hot path markedly slower, not
an acceptable production fix. The absolute size of its gain can
include global register-allocation and code-layout changes, not simply
the instruction cost of testing a zero-valued variable.

The semantics-preserving replacement, implemented in `c68k.c`, is:

```c
if (__builtin_expect(BusErrHandling != 0, 0)) {
    /* All original bus-error exception operations remain unchanged. */
}
```

Three more GCC trials establish that the hint does not significantly
hurt GCC in this workload:

| Compiler | No bus hint (three-trial median) | Hint (three-trial median) |
| --- | ---: | ---: |
| GCC 15.2.0 | 55.353 s | 55.342 s |
| LLVM 24 a6b9e2f | 63.169 s | 62.575 s |

The `CPU->ICount > 0` hint also gives a promising single-trial result,
but was **not** committed because it was not retested with GCC and the
combined hint experiment was slower; compiler block layout is sensitive.

## Semantic-preserving outlining attempt and new LLVM defect

Outlining the entire exception body into a `noinline` function
preserves the bus-error state/stack/PC work and would remove this
large rare path from the already huge dispatch function. The
R5900 Clang compiler completed code generation, but **GNU as
rejected the resulting `c68k.c` assembly** with
`Error: branch out of range` (two such errors in the plain
`noinline` helper experiment). A `cold,noinline` helper also failed.
No resulting ELF was benchmarked; this is an LLVM/R5900 branch
relaxation or basic-block placement problem to investigate separately,
not proof that outlining would be faster. Source was restored before
the safe-hint benchmarks.

## Validation limitations and next LLVM work

All successful full benchmark logs have `warmup=500`,
`frames=expected=10000`, `cache=off`, `sound=on`,
`io_ops=0`, and `status=unverified`. The M68000 and RAM
end-state fingerprints were consistent in earlier controls, but
asynchronous Z80/audio fingerprints differ between otherwise repeated
runs. End-state equality does not establish frame-by-frame equivalence;
indirect IOP I/O is not fully audited. These are **PCSX2 guest-clock**
times, not real PS2 hardware measurements.

For LLVM/R5900 maintainers the precise repro target is
`src/cpu/m68000/c68k.c:C68k_Exec` with the inlined, normally
inactive `BusErrHandling` block. Compare optimized machine IR,
register interference, branch placement, and emitted MIPS instructions
between the original, `__builtin_expect`, and removed-branch
variants. A tiny standalone reproducer should preserve the same
computed-goto register pressure; simplifying it too aggressively may
erase the failure. The separate outlining case can serve as a
`branch out of range` backend reproducer.

## Local evidence

Ignored artifacts, retained on the user's Mac:

- `build_ps2_c68k_opcode_profile_20261022/trials/gcc/`:
  complete GCC opcode-frequency report and raw PCSX2 log.
- `build_ps2_compiler_wjammers_aligned_20261022/trials/`:
  one-run Clang macro / source-ablation experiments
  (`c68k_no_buserr_full`, `c68k_loop_likely_full`,
  `c68k_bne_likely_full`, etc.).
- `build_ps2_compiler_wjammers_aligned_20261022/trials_bus_hint/`:
  full 3-trial GCC/LLVM safe-hint controls and CSV/Markdown summaries.
- `build_ps2_compiler_wjammers_aligned_20261022/c68k_macro_variants/`:
  temporary original and modified C files, Clang objects and compiler
  logs. These are **not** production source files.
- `build_ps2_c68k_opcode_profile_20261022/llvm/build.log`
  and `build_ps2_compiler_wjammers_aligned_20261022/llvm_split_plain_build.log`:
  assembler failures for the counter and noinline-helper approaches.

The unrelated user-owned `src/ps2/ps2_video.c` changes and everything
under `resources/` remain untouched.

### Build and test checks

- Aligned PS2 MVS GCC and Clang builds both succeeded with the final
  unlikely-branch hint; six full PCSX2 control trials completed.
- The existing Desktop MVS application built successfully with AppleClang.
- The shared `c68k.c` translation unit compiled with the PSP GCC 15.2
  toolchain. An older complete PSP MVS build directory could not be
  reconfigured because its cached `PS2_DIRTY_SPRITE_UPLOADS` option was
  invalid for PSP; this is not a full PSP application build validation.
- Host CTest: **4/4 passed**. No real PS2 hardware performance test was
  attempted in this investigation.
