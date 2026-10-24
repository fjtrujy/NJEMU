# LLVM / MIPS R5900: NJEMU code-generation performance issue and out-of-range branches

**Suggested issue title:** `[Mips/R5900] Large computed-goto interpreter: GCC 15.2 beats Clang 24 by ~20%; cold bus-error block changes runtime by ~9%; outlining triggers branch-out-of-range assembly errors`

- **Date:** 2026-10-22
- **Project:** NJEMU — PlayStation 2 (Emotion Engine / R5900) build, MVS/Neo Geo emulator
- **Repository:** https://github.com/fjtrujy/NJEMU
- **Benchmark branch:** `feature/ps2-compiler-benchmark`
- **Affected compiler:** Clang 24.0.0git, LLVM revision `a6b9e2f2e16046e44d1da5ae5d75a1a4927a5c7f`, from `rickgaiser/llvm-project`
- **Comparison compiler:** `mips64r5900el-ps2-elf-gcc` 15.2.0
- **Primary relevant NJEMU commit:** `bde750c6ceba1d92012cdf585e582d1ee8d2e43b` (aligned-optimization baseline); report/fix commits `f645184f`, `a853bdca`, `74e8cf7c`.

**Status:** Confirmed *PCSX2 code-generation performance difference* and assembler error in two source variations. **Not yet demonstrated to be a regression between LLVM revisions, nor localized specifically to the R5900 fork versus shared LLVM MIPS.**

## 1. Executive summary / requests for the LLVM developer

NJEMU is a large C emulator that uses a computed-goto 68000 interpreter, a Z80 interpreter, MVS memory callbacks and YM2610 sound synthesis. On the same source snapshot and with `-O3 -ffast-math` on both compilers, **Clang-generated NJEMU MVS code takes 66.369 s** to execute 10,000 timed *Windjammers* frames in PCSX2 unlimited mode, versus **55.349 s for GCC**: approximately **19.91% longer**.

Two highly actionable areas have emerged:

1. **MVS byte RAM-access dispatch (`src/mvs/memintrf.c`).** When an early `0x1xxxxx` RAM check precedes a sparse address `switch`, unhinted Clang places the common RAM case in a computed jump table. GCC places it on a direct early branch. A source-level `__builtin_expect` restores Clang's direct branch and saves **3.200 s / 10,000 frames** across four callbacks, with essentially no GCC change. A hint limited to `m68000_read_memory_8` accounts for **2.466 s** of that gain.
2. **68000 instruction dispatch (`src/cpu/m68000/c68k.c:C68k_Exec`).** An inlined, normally inactive bus-error branch sits on the path traversed by every emulated opcode. **Removing its entire block for diagnosis** (not a safe fix) reduces the already RAM-hinted Clang binary from 63.169 s to **57.126 s**, i.e. **6.043 s**. Keeping identical exception behavior and merely marking the branch unlikely saves a smaller but repeatable **0.594 s** (median of three trials); GCC remains unchanged. This strongly implicates sensitivity to compiler block layout, register pressure/allocation, branch selection or instruction scheduling rather than the cost of executing the exception handler itself.

**Separate build failure:** Moving the bus-error body to a `noinline` function (with or without `cold`) causes Clang-generated R5900 assembly to fail GNU `as` with **`Error: branch out of range`**. Injecting a diagnostic opcode-frequency counter into the same huge computed-goto function also triggers this assembler error. These variations are useful for debugging branch-range handling; a minimal standalone reproducer has not yet been reduced.

**Requested help:** Please inspect the optimized LLVM IR / Machine IR and emitted MIPS assembly in `C68k_Exec` **with and without the bus-error block**, and the early RAM check lowering in `m68000_read_memory_8`. Determine whether the fault lies in shared MIPS lowering/branch relaxation, R5900-specific instruction selection/scheduling, or a toolchain interaction. The measurements do **not** establish which.

## 2. Exact environment and provenance

| Dimension | Value |
| --- | --- |
| LLVM compiler | Clang 24.0.0git; `rickgaiser/llvm-project` commit `a6b9e2f2e16046e44d1da5ae5d75a1a4927a5c7f` |
| Installed Clang binary SHA-256 | `db3e0f2908081a57ee8ccca59defbaa33c8a2579c9e74404b18f86268f4808` |
| GCC compiler | PS2DEV GCC 15.2.0, `mips64r5900el-ps2-elf-gcc` |
| Target | PlayStation 2 Emotion Engine, little-endian R5900, MIPS N32 ABI |
| VU0 experiment switch | Disabled for the matched-flag Windjammers comparison; the earlier matrix checked both VU0 states |
| LLVM target flags | `--target=mips64el -mcpu=r5900 -mabi=n32 -fno-pic -mno-abicalls -G0 -mno-gpopt -fomit-frame-pointer` |
| Matched optimization | `-O3 -ffast-math -fomit-frame-pointer`; `-G0` |
| LLVM assembler | PS2DEV GNU `mips64r5900el-ps2-elf-as` via `-fno-integrated-as -B<dir-with-as>`, plus `-fno-addrsig` |
| Final linker | PS2DEV `mips64r5900el-ps2-elf-gcc` for **both** variants |
| Libraries | The same GCC-built PS2SDK, gsKit, ps2_drivers and other SDK archives |
| NJEMU source | Base commit `bde750c6ceba1d92012cdf585e582d1ee8d2e43b`; matched source/worktree for both compilers |
| NJEMU tracked worktree diff SHA-256 recorded by the baseline | `d17b5649e4ce5486e679e5573eec1bc69f534c56877e1e12175804a02268a244` |
| Emulator | PCSX2 v2.9.93 on macOS, `-nogui -batch -unlimited -elf` |
| Benchmark setup | `PLATFORM=PS2 TARGET=MVS GUI=OFF USE_CACHE=OFF SAVE_STATE=ON PS2_COMPILER_BENCHMARK=ON` |
| Game/scenario | `wjammers` (Windjammers), ordinary attract mode |
| Window | 500 warm-up frames + 10,000 measured guest frames; 3 runs for primary comparisons |
| Audio/video | Sound on, video on, no VSync or frame limiting, no cache streaming; resident game assets |
| Instrumented disk I/O | `io_ops=0`, `io_bytes=0` during timing (wrapper coverage only) |

**Source provenance qualification:** The source snapshot included a pre-existing, unrelated local edit to `src/ps2/ps2_video.c`, identically compiled in both variants, which was not committed or reset during the investigation. The SHA above records that source difference. Any external reproduction from a clean checkout may obtain different absolute values. The same build configurations were used in controlled within-compiler source experiments.

**Compiler-boundary qualification:** All NJEMU-owned objects in the homogeneous builds were produced by their respective compiler; there were **no GCC NJEMU fallback objects in the Clang build**. The SDK/archive and linker toolchain is GCC-built on purpose. GCC's global linker command must not inherit `-fno-lto` in this environment, because its gsKit archive needs LTO handling; the Clang C compilation used `-fno-lto`.

The **actual toolchain used** was an adapted, locally ignored `r5900-feature-toolchain.cmake`, not the historic `ps2dev_llvm.cmake` found on NJEMU's `origin/llvm` branch. The historic version hard-coded Clang 22 / GCC 15.1 include directories; this experiment used Clang 24 / GCC 15.2. Build-cache/compiler command provenance is preserved locally (Section 9).

Relevant SDK archive SHA-256 recorded in the benchmark manifests:

| Archive | SHA-256 |
| --- | --- |
| `libkernel.a` | `a908e0e45ecaaa2f72fd2fffd228c6188d5fc04ac7e2defc18211127d9d62eeb` |
| `libgskit.a` | `b34e0ef948b19a9f3c2234a16ccc4ef44bde8dc10c85ef51f33122266b8706e2` |
| `libps2_drivers.a` | `9703527752382915619f204a7d65b57944a7924e3e99162c1ef7394769a8de42` |

## 3. Primary results: matched flags and controlled source changes

Each number in this table is the **median of three** 10,000-frame measurements. Times are measured guest-frame work via the emulator's clock instrumentation under PCSX2 unlimited mode, **not actual wall-clock execution times or physical PS2 hardware measurements**.

| Configuration | GCC time (s) | Clang time (s) | Clang vs GCC |
| --- | ---: | ---: | ---: |
| Original unhinted MVS RAM callbacks; original 68000 dispatch | **55.349** | **66.369** | **+19.91%** |
| Hint all four MVS RAM callback fast paths; original dispatch | **55.353** | **63.169** | **+14.12%** |
| Same RAM hints + keep bus-error block but mark condition unlikely | **55.342** | **62.575** | **+13.07%** (approx.) |

The source changes in these rows are **branch-probability annotations only**; the actual address mapping and bus-error operations remain present. The Clang gain from the four RAM hints is **3.200 s**, and the further bus-error-branch hint saves **0.594 s**, or approximately **3.794 s combined** versus the unhinted Clang baseline. The same hints do not materially affect GCC timing here.

The one-function RAM control is particularly useful:

| Clang memory callbacks | Time (s) | Saving vs original |
| --- | ---: | ---: |
| Original | 66.369 | - |
| Hint only `m68000_read_memory_8` | 63.904 | **2.466 s** |
| Hint all four 8-/16-bit read/write callbacks | 63.169 | **3.200 s** |

The main numbers are unusually stable across repeats; raw records are retained locally (Section 9). This precision is descriptive, not proof of program-state equivalence.

### Diagnostic interpreter ablations (not accepted fixes)

These are **single-run** experiments using the four-RAM-hint LLVM build as the 63.169 s reference, unless noted.

| C source variation in `C68k_Exec` | Clang time (s) | Change relative to reference |
| --- | ---: | ---: |
| Remove the full `if (BusErrHandling)` block | **57.126** | **-6.043 s** |
| Change `if (CPU->ICount > 0)` to `if (__builtin_expect(CPU->ICount > 0, 1))` | 62.524 | -0.644 s |
| Hint the BNE opcode condition as likely | 63.145 | ~0 s |
| Spell out the NOP handler's cycle decrement | 63.145 | ~0 s |
| Apply both loop-condition and bus-error hints together | 63.693 | +0.524 s; the effects do not add |

The full branch deletion is **not semantically acceptable**. Its gain measures the global compiler sensitivity to the presence of that block (including code layout, liveness and register assignment), not necessarily the dynamic cost of testing `BusErrHandling`. The two loop-condition hints interact nonlinearly, strengthening the need to compare actual Machine IR rather than assume a local instruction-count explanation.

## 4. Concrete C trigger A: RAM-access dispatch

**Path:** `src/mvs/memintrf.c`, `m68000_read_memory_8` and its 16-bit/other write counterparts. This is PS2 MVS-specific and enabled by `PS2_MVS_RAM_FASTPATH=ON`.

The original C has the following structure:

```c
offset &= M68K_AMASK;
#ifdef PS2_MVS_RAM_FASTPATH
if ((offset >> 20) == 0x1)
    return READ_MIRROR_BYTE(neogeo_ram, offset, 0x00ffff);
#endif

switch (offset >> 20) {
case 0x0: return READ_BYTE(memory_region_cpu1, offset);
/* several other memory-mapped regions and callbacks */
}
```

GCC emits a dedicated early check for RAM page 1 **ahead of the generic decoder**. Unhinted Clang instead routes all pages, including 1, through a jump-table dispatch. In the retained unhinted object disassembly:

```asm
# GCC m68000_read_memory_8:
10: srl   a5,a4,0x14
14: beq   a5,a1,78       # early comparison with page 1
...
# LLVM m68000_read_memory_8:
18: srl   a1,v1,0x14
1c: sltiu at,a1,14
20: beqz  at,15c
28: sll   at,a1,0x2
...
3c: lw    at,0(at)      # jump-table entry
4c: jr    at             # indirect dispatch
```

The observed correction was:

```c
#define MVS_RAM_PAGE_LIKELY(offset) \
    __builtin_expect((((offset) >> 20) == 0x1), 1)

if (MVS_RAM_PAGE_LIKELY(offset))
    return READ_MIRROR_BYTE(neogeo_ram, offset, 0x00ffff);
```

The same annotation is used for `m68000_read_memory_16`, `m68000_write_memory_8` and `m68000_write_memory_16`, only when `PS2_MVS_RAM_FASTPATH` is enabled. This changes branch probability/code layout, **not the memory test or return result**. A separate 3-run single-callback control isolates the bulk of the saving to `m68000_read_memory_8` (2.466 s).

**What to investigate in LLVM:** jump-table threshold/formation, successor ordering, branch probability propagation, R5900 jump/branch costs, and whether a cheap early-return fast path should remain outside a sparse switch. Check whether this behavior is shared with upstream MIPS or specific to the R5900 patchset.

## 5. Concrete C trigger B: the 68000 computed-goto loop

**Path:** `src/cpu/m68000/c68k.c`, `C68k_Exec`, around `C68k_Exec_Next`. A simplified but representative excerpt of the **original** code (before the safe bus-error hint):

```c
C68k_Exec_Next:
    if (CPU->ICount > 0) {
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

Key supporting macros in `src/cpu/m68000/c68kmacro.h`:

```c
#define USE_CYCLES(A) CPU->ICount -= (A);
#define RET(A)        USE_CYCLES(A) goto C68k_Exec_Next;
#define READ_IMM_16() (*(uint16_t *)PC)
#define READ_MEM_16(A) CPU->Read_Word(A)
#define SET_PC(A) /* updates BasePC and PC through CPU->Fetch[] */
```

The `#include "c68k_op.c"` is intentional: it inserts **hundreds of label handlers** within the same C function; most use `RET(...)` to jump back to the common dispatch. The bus-error block is normally inactive in this workload, but its conditional and its large inline body are present in the same enormous control-flow graph.

**Critical observation:** Removing just this source block from the *Clang* translation unit (preserving every other source/object and relinking) saves **6.043 s** / 10,000 frames. This is the best source-level bisection to reproduce the pathology. It is **not a production fix** because it discards 68000 bus-error exception processing.

The semantically equivalent performance improvement committed in NJEMU at `74e8cf7c` is a one-line change:

```diff
-               if (BusErrHandling) {
+               if (__builtin_expect(BusErrHandling != 0, 0)) {
                    /* same exception handler body, unchanged */
```

A GCC and LLVM A/B comparison, each with three 10,000-frame runs, showed Clang 63.169 -> 62.575 s, GCC 55.353 -> 55.342 s. The block remains callable when the flag is set.

A repository-wide source search at the time found no writer for `BusErrHandling` other than initialization and the handler's own reset. That observation is **not** a license to delete the handler: out-of-tree, platform or future paths may depend on it, and it represents intentional emulator exception semantics.

**What to investigate in LLVM:** function-wide register interference caused by the rarely executed exception path, liveness at the shared label, spilling around memory callbacks, hot/cold placement, computed-goto indirect-branch emission, immediate/address calculation, and MIPS delay-slot scheduling. The magnitude of the ablation change cannot be attributed to a specific LLVM pass without a Machine IR / before-after assembly diff.

### Dynamic opcode-frequency evidence

For a 500 warm-up + 1,500 measured-frame Windjammers test, a **GCC diagnostic build** counted **35,274,090** decoded 68000 instructions. The five most frequent *opcode values* account for approximately 58% of them:

| Opcode | Meaning | Executions | Handler location |
| --- | --- | ---: | --- |
| `4e71` | NOP | 6,614,311 | `src/cpu/m68000/c68k_op.c:1207` |
| `0839` | BTST immediate, absolute long | 3,659,480 | `c68k_op.c:234` |
| `4a39` | TST byte, absolute long | 3,523,051 | `c68k_op.c:1051` |
| `66f8` | BNE -8 | 3,498,616 | `c68k_op.c:1626` |
| `66f2` | BNE -14 | 3,148,932 | `c68k_op.c:1626` |

The hot handler macros are `BITOP_STATIC(TST, 8, AL)`, `TST(8, AL)`, `Bcc_8(NE)` and `RET(4)`/`RET(...)`. These counts came from **GCC only**, not Clang: inserting the counter into the Clang interpreter failed assembly with `branch out of range`. BNE branch-probability and NOP spelling experiments had negligible effects; the shared dispatch/exception shape mattered more in these tests.

### Static machine-code evidence

Static counts from `mips64r5900el-ps2-elf-objdump -d` over each function's object section (not sampled runtime instructions):

| Function | Compiler | Code size (bytes) | Static instructions | `addiu` | `nop` | `jalr` |
| --- | --- | ---: | ---: | ---: | ---: | ---: |
| `C68k_Exec` | GCC | 215,536 | 53,884 | 4,592 | 371 | 3,080 |
| `C68k_Exec` | LLVM | 243,108 | 60,778 | 9,525 | 1,117 | 3,082 |
| `Cz80_Exec` | GCC | 23,436 | 5,860 | not counted | 972 | 76 |
| `Cz80_Exec` | LLVM | 15,136 | 3,784 | not counted | 120 | 75 |

For `C68k_Exec`, LLVM emits **6,894 more static instructions**, including **4,933 additional `addiu`** and **746 additional `nop`**. These differences suggest an address-arithmetic/constant-materialization and delay-slot/scheduling audit. However, the interpreter contains many opcode blocks, and **static counts do not imply frequency-weighted runtime cost**. `Cz80_Exec` demonstrates this caveat: LLVM's shorter code is still slower on the tested workload.

## 6. Related source-object isolation (same target and flags)

Each row below starts from the 63.169 s **four-RAM-hint, otherwise all-LLVM build**, swaps the named NJEMU-owned `.obj` files with corresponding GCC `-O3 -ffast-math` files, relinks via the same GCC linker, and runs **one** 10,000-frame trial. These are *diagnostic hybrid* ELFs, not homogeneous Clang measurements. Savings are not additive.

| GCC translation units substituted into LLVM | Total (s) | Gain vs 63.169 s |
| --- | ---: | ---: |
| `src/mvs/memintrf.c` | 60.557 | 2.612 |
| `src/cpu/m68000/c68k.c` | 59.713 | 3.455 |
| Both files above | 57.086 | 6.082 |
| Both + `src/cpu/z80/cz80.c` | 56.086 | 7.083 |
| Both + `src/cpu/z80/z80.c` *instead* | 57.087 | 6.082 |
| Both + `src/sound/ym2610.c` | 56.506 | 6.663 |
| Both + `src/sound/2610intf.c` | 57.113 | 6.055 |
| Both + `src/sound/sndintrf.c` | 57.091 | 6.078 |
| Both + Z80 core/wrapper + YM2610 and sound wrappers (7 files total) | **55.479** | **7.690** |
| All-GCC reference (three-trial median; not a hybrid) | **55.353** | - |

These seven translation units close **~98.4% of the 7.815 s measured four-hint LLVM-vs-GCC difference** for this single workload. The most specific current targets are:

- `m68000_read_memory_8`: directly source-isolated; LLVM jump-table lowering strongly supported by assembly.
- `C68k_Exec`: directly source-ablated; exceptional-path CFG/liveness sensitivity strongly supported; exact backend mechanism unknown.
- `Cz80_Exec`: the `cz80.c` object accounts for ~1.00 s *after* the 68000/memory swaps; `z80.c` wrapper does not.
- `YM2610ContextUpdate` is a plausible hot routine in `ym2610.c`, but **only the translation unit** was isolated (~0.58 s after the 68000/memory swaps). No function-specific attribution is yet proven.

The all-GCC reference and hybrid changes cannot be used as a substitute for per-frame semantic equivalence or physical PS2 benchmarks.

## 7. Separate assembler failure: `branch out of range` after harmless source variation

Two diagnostic changes to `C68k_Exec` fail **before linking** when assembling Clang-emitted MIPS/R5900 code with the external PS2 GNU assembler:

**A. Semantic-preserving outline attempt.** Move the original `BusErrHandling` body into a separate static `__attribute__((noinline))` helper, called only when the flag is set and returning the updated `PC`. An additional `cold,noinline` attempt also failed. No runnable binary was produced, so outlining has **no timing result**.

Representative compiler output:

```text
FAILED: CMakeFiles/MVS.dir/src/cpu/m68000/c68k.c.obj
.../c68k-513cea.s:24811: Error: branch out of range
.../c68k-513cea.s:24817: Error: branch out of range
clang: error: assembler command failed with exit code 1
```

**B. Opcode counting.** Add a `uint32_t counts[65536]` table and `++counts[Opcode]` after `READ_IMM_16()`, plus a diagnostic output function. The GCC build succeeded and captured the opcode distribution above. Clang emitted an invalid out-of-range assembly branch:

```text
FAILED: CMakeFiles/MVS.dir/src/cpu/m68000/c68k.c.obj
.../c68k-41dc09.s:27605: Error: branch out of range
clang: error: assembler command failed with exit code 1
```

These errors may involve conditional-branch reachability after block placement, assembler relaxation/expansion or an LLVM/GNU-as interaction. **The exact offending branch instruction/target and backend pass have not yet been extracted**, and this is not yet an isolated upstream-MIPS reproducer. An external developer should inspect `.s` near the indicated line numbers, and then inspect pre-/post-branch-relaxation Machine IR.

The project also historically encountered unsupported R5900 floating-point comparison predicates and missing instruction selection on older LLVM revisions. Those **earlier issues are distinct** from this report's tested performance case on `a6b9e2f`, which compiled *all NJEMU objects* successfully without GCC fallbacks.

## 8. Reproduction instructions

Reproducing the **C compiler output** does not require the ROM; running the full timing benchmark does require a legitimately obtained Windjammers ROM and a working PCSX2 setup. Do **not** distribute ROMs/BIOS as an LLVM bug-report attachment.

### 8.1 Source and SDK

```sh
git clone https://github.com/fjtrujy/NJEMU.git
cd NJEMU
git checkout bde750c6ceba1d92012cdf585e582d1ee8d2e43b

# Point these at the locally installed PS2DEV SDK/toolchain.
export PS2DEV=/path/to/ps2dev
export PS2SDK="$PS2DEV/ps2sdk"
export PATH="$PS2DEV/llvm/bin:$PS2DEV/ee/bin:$PS2DEV/ps2sdk/bin:$PATH"
```

At `bde750c6` the RAM-branch and bus-error hints are **not yet committed**. The study used the same pre-existing local `ps2_video.c` edit for both compilers; the exact historical build-manifest source fingerprint is in Section 2. For the *final safe hint versions*, examine commits `f645184f` (RAM) and `74e8cf7c` (bus error). Do not mistake a clean checkout's absolute times for the preserved historical numbers.

Check the Clang build identity:

```sh
"$PS2DEV/llvm/bin/clang" --version
"$PS2DEV/ee/bin/mips64r5900el-ps2-elf-gcc" --version
```

For an **exact local reproduction**, the ignored build tree retained in the original NJEMU checkout contains the actual LLVM toolchain file:

```text
build_ps2_compiler_llvm_r5900_feature_20261022/r5900-feature-toolchain.cmake
```

A fresh clone does not contain it. To make the reproduction independent of
that ignored local file, the **complete adapted cross-toolchain configuration**
used in the experiment is reproduced below. Save it as
`r5900-llvm-toolchain.cmake` at the NJEMU root; the added assembler arguments
come from the separately audited Clang flags, not from the toolchain's
`EE_CFLAGS` initializer.

```cmake
cmake_minimum_required(VERSION 3.0...3.12)
if(NOT DEFINED ENV{PS2DEV} OR NOT DEFINED ENV{PS2SDK})
  message(FATAL_ERROR "PS2DEV and PS2SDK must be set")
endif()
set(PS2DEV "$ENV{PS2DEV}")
set(PS2SDK "$ENV{PS2SDK}")
set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_VERSION 1)
set(CMAKE_SYSTEM_PROCESSOR mips)
set(CMAKE_C_COMPILER "$ENV{PS2DEV}/llvm/bin/clang")
set(CMAKE_CXX_COMPILER "$ENV{PS2DEV}/llvm/bin/clang++")
set(CMAKE_C_COMPILER_WORKS TRUE)
set(CMAKE_CXX_COMPILER_WORKS TRUE)
set(CMAKE_C_LINK_EXECUTABLE
    "mips64r5900el-ps2-elf-gcc <CMAKE_C_LINK_FLAGS> <LINK_FLAGS> <OBJECTS> -o <TARGET> <LINK_LIBRARIES>")
set(CMAKE_CXX_LINK_EXECUTABLE
    "mips64r5900el-ps2-elf-g++ <CMAKE_CXX_LINK_FLAGS> <LINK_FLAGS> <OBJECTS> -o <TARGET> <LINK_LIBRARIES>")
set(LLVM_TARGET_FLAGS "--target=mips64el -mcpu=r5900 -mabi=n32 -fno-pic -mno-abicalls -G0 -mno-gpopt -fomit-frame-pointer")
set(LLVM_INCLUDE_FLAGS "-isystem $ENV{PS2DEV}/llvm/lib/clang/24/include -I$ENV{PS2DEV}/ee/mips64r5900el-ps2-elf/include -I$ENV{PS2DEV}/ee/lib/gcc/mips64r5900el-ps2-elf/15.2.0/include")
set(EE_CFLAGS "${LLVM_TARGET_FLAGS} ${LLVM_INCLUDE_FLAGS} -I$ENV{PS2SDK}/ee/include -I$ENV{PS2SDK}/common/include -I$ENV{PS2DEV}/gsKit/include -I$ENV{PS2SDK}/ports/include -D_EE -DPS2 -D__PS2__ -O3 -ffast-math -Wall -fno-lto -Wno-logical-not-parentheses" CACHE STRING "EE C compiler flags" FORCE)
set(EE_LDFLAGS "-L$ENV{PS2SDK}/ee/lib -L$ENV{PS2DEV}/gsKit/lib -L$ENV{PS2SDK}/ports/lib -Wl,-zmax-page-size=128 -Wl,--no-warn-mismatch -T$ENV{PS2SDK}/ee/startup/linkfile")
set(CMAKE_TARGET_INSTALL_PREFIX "$ENV{PS2DEV}/ports")
set(CMAKE_FIND_ROOT_PATH "$ENV{PS2DEV}" "$ENV{PS2DEV}/ee" "$ENV{PS2DEV}/ee/ee" "$ENV{PS2SDK}" "$ENV{PS2SDK}/ports")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set_property(GLOBAL PROPERTY TARGET_SUPPORTS_SHARED_LIBS TRUE)
set(CMAKE_C_FLAGS_INIT "${EE_CFLAGS}")
set(CMAKE_CXX_FLAGS_INIT "${EE_CFLAGS}")
set(CMAKE_EXE_LINKER_FLAGS_INIT "${EE_LDFLAGS}")
set(CMAKE_SHARED_LIBRARY_CREATE_C_FLAGS "-nostartfiles -Wl,-r -Wl,-d")
find_program(PKG_CONFIG_EXECUTABLE NAMES mips64r5900el-ps2-elf-pkg-config HINTS "$ENV{PS2SDK}/bin")
if(NOT PKG_CONFIG_EXECUTABLE)
  message(FATAL_ERROR "Could not find mips64r5900el-ps2-elf-pkg-config")
endif()
set(PS2 TRUE)
set(PLATFORM_PS2 TRUE)
set(EE TRUE)
```

Use GNU `as` explicitly, as required by this experimental Clang configuration:

```sh
mkdir -p build-r5900-gnu-as
ln -sf "$PS2DEV/ee/bin/mips64r5900el-ps2-elf-as" build-r5900-gnu-as/as
export NJEMU_LLVM_AS_DIR="$PWD/build-r5900-gnu-as"
```

The toolchain above retains the exact SDK/gsKit search paths and linker script
from the original. Pass the Clang-only assembler options when configuring,
as below. Adapt the hard-coded Clang 24 / GCC 15.2 builtin header paths if
your toolchain versions differ. Exact compiler calls can be inspected with
`ninja -t commands` after configuration.

### 8.2 Compiler builds and benchmark

The original aligned experiments used CMake + Ninja for both compilers and included:

```sh
# The GCC flags below match the original CMAKE_C_FLAGS cache (without SDK
# driver defaults which the CMake toolchain itself supplies).
GCC_FLAGS="-I$PS2SDK/ee/include -I$PS2SDK/common/include -I$PS2DEV/gsKit/include -I$PS2SDK/ports/include -D_EE -DPS2 -D__PS2__ -O3 -ffast-math -fomit-frame-pointer -G0"
cmake -S . -B build-gcc-mvs -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE="$PS2DEV/share/ps2dev.cmake" \
    -DCMAKE_C_FLAGS="$GCC_FLAGS" \
    -DPLATFORM=PS2 -DTARGET=MVS -DGUI=OFF -DUSE_CACHE=OFF \
    -DSAVE_STATE=ON -DPS2_COMPILER_BENCHMARK=ON \
    -DPS2_BENCHMARK_WARMUP=500 -DPS2_BENCHMARK_FRAMES=10000
cmake --build build-gcc-mvs -j8

LLVM_FLAGS="--target=mips64el -mcpu=r5900 -mabi=n32 -fno-pic -mno-abicalls -G0 -mno-gpopt -fomit-frame-pointer -isystem $PS2DEV/llvm/lib/clang/24/include -I$PS2DEV/ee/mips64r5900el-ps2-elf/include -I$PS2DEV/ee/lib/gcc/mips64r5900el-ps2-elf/15.2.0/include -I$PS2SDK/ee/include -I$PS2SDK/common/include -I$PS2DEV/gsKit/include -I$PS2SDK/ports/include -D_EE -DPS2 -D__PS2__ -O3 -ffast-math -Wall -fno-lto -Wno-logical-not-parentheses -fno-integrated-as -B$NJEMU_LLVM_AS_DIR -fno-addrsig"
cmake -S . -B build-llvm-mvs -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE="$PWD/r5900-llvm-toolchain.cmake" \
    -DCMAKE_C_FLAGS="$LLVM_FLAGS" \
    -DPLATFORM=PS2 -DTARGET=MVS -DGUI=OFF -DUSE_CACHE=OFF \
    -DSAVE_STATE=ON -DPS2_COMPILER_BENCHMARK=ON \
    -DPS2_BENCHMARK_WARMUP=500 -DPS2_BENCHMARK_FRAMES=10000
cmake --build build-llvm-mvs -j8
```

For maximal reproducibility, inspect `build.ninja` or `compile_commands.json`
and retain exact `c68k.c` and `memintrf.c` compile commands alongside the
emitted `.s` and `.obj`. The example commands above supply the original
experiment's key cross-target and optimization flags, but other SDK and
host-specific defaults still come from the installed cross-toolchain.

The project's actual C99 host runner accepts the following positional call (substitute your PCSX2 binary and generated build directory):

```sh
build_ps2_compiler_host/njemu-tool ps2-benchmark pcsx2 \
    benchmarks/ps2/roms.tsv MVS wjammers GCC \
    /path/to/PCSX2 build-gcc-mvs MVS.ELF results/gcc 3 220 10000

build_ps2_compiler_host/njemu-tool ps2-benchmark pcsx2 \
    benchmarks/ps2/roms.tsv MVS wjammers LLVM \
    /path/to/PCSX2 build-llvm-mvs MVS.ELF results/llvm 3 220 10000
```

The runner stages the ROM/ELF and isolated save-state fixtures per run, launches PCSX2 with `-unlimited` and captures `NJEMU_BENCHMARK_V1` records. The `220` above is its per-trial launch timeout in seconds, *not* a guest-frame count. `3` denotes repeat trials. The benchmark must report `warmup=500 frames=10000 expected=10000 resident=on cache=off sound=on io_ops=0` for the intended configuration.

### 8.3 Focused compiler-only experiment (best first step)

Without PCSX2 or a ROM, compile `src/cpu/m68000/c68k.c` using **identical** Clang switches in three isolated source variants:

1. Original `if (BusErrHandling) { ... }`; keep exception handling intact.
2. Replace the condition only with `__builtin_expect(BusErrHandling != 0, 0)`.
3. Remove the entire `if (...) { ... }` **for diagnostic code-generation comparison only**; never ship or treat the resulting emulator as semantically correct.

Compare the `C68k_Exec` function's IR / MIR / assembly, with attention to:
- Live register intervals across the shared `C68k_Exec_Next` target.
- Stack spills/reloads, local address `addiu` counts and delay-slot `nop` instructions.
- Hot and cold successor placement around the 68000's indirect `goto *JumpTable[Opcode]`.
- Function-level changes that move instructions on *other* handler paths.
- The unexpected out-of-range branch created by a separately outlined handler.

To obtain exactly the production compile invocation in the original build tree:

```sh
ninja -C build_ps2_compiler_wjammers_aligned_20261022/llvm \
    -t commands CMakeFiles/MVS.dir/src/cpu/m68000/c68k.c.obj
ninja -C build_ps2_compiler_wjammers_aligned_20261022/llvm \
    -t commands CMakeFiles/MVS.dir/src/mvs/memintrf.c.obj
```

And to inspect function-level machine code:

```sh
"$PS2DEV/ee/bin/mips64r5900el-ps2-elf-nm" -S --size-sort \
    build-llvm-mvs/CMakeFiles/MVS.dir/src/cpu/m68000/c68k.c.obj
"$PS2DEV/ee/bin/mips64r5900el-ps2-elf-objdump" -dr \
    build-llvm-mvs/CMakeFiles/MVS.dir/src/cpu/m68000/c68k.c.obj
```

## 9. Original log and object locations (kept locally; not in Git)

All the following are ignored paths in the original working checkout and are **not** part of the published Git history:

```text
build_ps2_compiler_llvm_a6b9e2f_20261022/
    descriptive_comparison_unlimited.csv
    {gcc,no_vu0,with_vu0}_unlimited_matrix/
    {gcc,no_vu0,with_vu0}_{cps1,cps2,mvs}/benchmark_build.txt

build_ps2_compiler_wjammers_aligned_20261022/
    gcc/ and llvm/                          # full builds, ELFs, objects
    trials_full/{gcc,llvm}/                 # 3 x 10,000 with four RAM hints
    trials/{gcc_original_10000,llvm_original_10000,likely_read8_full}/
    trials_bus_hint/{gcc,llvm}/             # 3 x 10,000 per compiler
    trials/gcc_*_full/                      # diagnostic mixed-object runs
    trials/c68k_*_full/                    # targeted source edits
    c68k_macro_variants/                   # isolated Clang source/object variants
    {unhinted_objects,gcc_unhinted_objects,hybrid_backups}/
    llvm_split_plain_build.log             # noinline helper assembler error
    llvm_cold_helper_build.log             # cold helper assembler error
    run_hybrid.sh                           # restores objects and relinks ELFs
    probe_c68k_macros.sh                   # source-variation experiments

build_ps2_c68k_opcode_profile_20261022/
    trials/gcc/                            # opcode distribution
    llvm/build.log                         # instrumentation assembler error

build_ps2_compiler_llvm_r5900_feature_20261022/
    r5900-feature-toolchain.cmake
    gnu-as/as                              # symlink to PS2DEV GNU as
```

**Possible developer attachments** (only if explicitly shared): the unhinted/hinted Clang `c68k.c` `.s` and `.obj`, GNU-as error-producing `.s` from the noinline case, corresponding GCC `.obj`, exact `ninja -t commands` output, and benchmark CSV/logs. The ephemeral `/var/folders/.../c68k-*.s` files named in assembler errors may no longer exist; regenerate with `-save-temps` / `-S` or a preserved build command. The current repository includes this report; these raw artifacts have **not** been embedded in it.

## 10. Broader workload context (prior run; optimization flags not matched)

A previous **90/90 successfully executed** benchmark matrix used GCC `-O2` and Clang `-O3 -ffast-math`, so it should **not** be treated as an isolated compiler-performance comparison. It served to identify the worst affected core/ROM and establish the lack of useful VU0 influence.

| Core | ROM | GCC -O2 (s) | LLVM -O3 no VU0 (s) | LLVM -O3 with VU0 (s) |
| --- | --- | ---: | ---: | ---: |
| CPS1 | slammast | 65.527 | 70.122 | 70.113 |
| CPS1 | dino | 64.814 | 70.590 | 70.556 |
| CPS1 | punisher | 63.232 | 69.447 | 69.421 |
| CPS1 | sf2hf | 65.319 | 72.232 | 72.212 |
| CPS2 | avsp | 80.266 | 83.930 | 84.006 |
| CPS2 | ddtod | 82.726 | 86.296 | 86.433 |
| CPS2 | ssf2 | 83.957 | 88.663 | 88.732 |
| MVS | wjammers | 55.363 | 66.377 | 66.395 |
| MVS | pbobbl2n | 66.146 | 76.374 | 76.366 |
| MVS | trally | 72.019 | 84.253 | 84.230 |

Equal-ROM-weight geometric mean Clang/GCC duration ratio: approximately **1.102** (~10.2% longer under those configurations). VU0 effects were negligible in this application. The focused `-O3`/`-O3` tests in Sections 2–7 are the relevant code-generation controls; do **not** combine their medians with the earlier `-O2` run.

## 11. Build/test validation completed

- Both the GCC and Clang PS2 MVS benchmark ELFs compiled and finished the
  full three-trial 10,000-frame tests with the semantics-preserving RAM and
  bus-error branch hints. The baseline and hybrid measurements are preserved
  separately and were not merged into the official accepted-speedup CSV.
- The existing Desktop MVS application compiled successfully with AppleClang;
  the shared `c68k.c` translation unit compiled with PSP GCC 15.2. A full PSP
  application reconfiguration was blocked by an unrelated stale, PS2-only
  CMake cache option (`PS2_DIRTY_SPRITE_UPLOADS`), so full PSP validation is
  **not claimed**.
- The existing host CTest suite passed **4/4 tests**.
- The *reconstructed toolchain specification and Clang flags in Section 8*
  were independently checked by extracting the Markdown CMake block,
  configuring a fresh ignored MVS build directory, and compiling the
  production `C68k_Exec` translation unit successfully. This was a compiler
  reproducer sanity check, **not** a new timing benchmark or a fresh full ELF
  certification.
- No physical PlayStation 2 performance trials succeeded in this
  investigation. The documented real-console PS2Link startup problems remain
  a separate validation item.

## 12. Confidence levels, caveats and suggested issue split

**Confirmed facts:**
- Same-source, same-optimization-setting PCSX2 guest-clock timing gap between GCC and Clang.
- GCC and Clang choose different dispatch structures for an identical C memory fast path.
- Source-level branch-probability hint has measured positive effect for Clang and negligible GCC effect.
- Removing the bus-error block changes Clang performance dramatically; preserving it with a likely/unlikely hint provides a smaller repeatable gain.
- Clang-emitted `c68k.c` assembly fails with `branch out of range` after particular source transformations when using the required external PS2 GNU assembler.
- Full mixed-object analysis localizes most remaining workload time to four emulator translation-unit groups.

**Not yet demonstrated:**
- A temporal LLVM *regression* compared with a prior LLVM commit, or equivalence between upstream MIPS and R5900 fork backends.
- Exact pass/instruction/branch responsible for the bus-error-block-induced runtime loss.
- That `YM2610ContextUpdate` specifically (rather than other `ym2610.c` functions) owns the sound-source regression.
- Strict compiler-result equivalence: final M68000/RAM hashes matched in key controls, but Z80 state hashes varied **within the same binary** in repeated attract-mode runs due to the sound-thread timing/state issue; complete deterministic replay and frame-by-frame state validation are missing.
- Consequently, all timing records remain `status=unverified` and **zero
  trials satisfy the strict compiler-speedup acceptance gate**, even though
  the descriptive controls are repeatable.
- Comprehensive no-I/O proof beyond the instrumented wrappers, or a real PlayStation 2 hardware speedup measurement.

For tracking, two separate LLVM issues may be appropriate:

**Issue A — MIPS/R5900 optimization/codegen quality:** disproportionate changes in runtime and emitted instructions due to a sparse memory dispatch and a cold inlined exception block in a large indirect-goto function. Attach function assembly and source-control timings; bisect upstream MIPS versus R5900-specific target if possible.

**Issue B — MIPS/R5900 branch range / assembler:** semantic-preserving outlining or simple profiling growth causes invalid out-of-range branches in Clang-generated `c68k.c` assembly. Attach the actual failing `.s` plus the exact compiler and GNU-as versions and the offending branch/target. This is separate from source-optimization quality and may be a stronger initial compiler correctness reproducer.

**Bottom line:** This is a credible and reproducible *compiler code-generation issue candidate* worth investigating, but calling it conclusively an "LLVM regression" or a proven "R5900 backend bug" would overstate the available evidence. The source bisection and assembler failure make it actionable without that claim.

## 13. Retest: newly installed Clang, 2026-10-24

This section supersedes the **compiler revision and timing** in the
historical sections above; the older measurements are retained for comparison.
The LLVM developer's response to this report described two changes on
`r5900-feature`: `c93727950240` (long-branch expansion accounts for
aligned-block padding) and `b9274b908116` (fold repeated R5900 address
additions into memory offsets after register allocation). The developer also
reported a **separate synthetic real-PS2 test**, exercising about 11 C68k
handlers, where GCC 15.2 ran at 50.34 Mcycles/s and Clang at 49.95 before,
49.85 after the fold. That experiment did **not** exercise NJEMU's full
memory callbacks or its much larger game-specific instruction footprint.
Those hardware results are the developer's, **not** measurements reproduced
in the NJEMU checkout.

### Installed compiler and fix verification

- New installed Clang: **24.0.0git**, `rickgaiser/llvm-project` revision
  **`6f31245b7434ed1c55d341d544d10a7e8d003766`** (branch `r5900-feature`).
  Installed compiler SHA-256:
  `24bdb817d20adc1e17623f5cb98412c23efd21e5705df58d65b38adc5f7a9d1c`.
- GCC reference stays PS2DEV **15.2.0**.
- `c93727950240` is an ancestor of the installed revision. The specific
  `b9274b908116` hash from the developer's response was **not** verified as
  an ancestor under that exact hash; the new generated code directly shows
  the expected reduction in address additions. Do not assume a commit's
  identity solely from its intended effect.
- All four **complete** NJEMU MVS benchmark ELFs compiled successfully:
  GCC/Clang with the existing inline bus-error handling and GCC/Clang with a
  guarded `noinline` helper. The outlined Clang object previously failed
  GNU `as` with `branch out of range`; this compilation now succeeds.
- The existing RAM-page and bus-error `__builtin_expect` hints remain enabled
  in every trial, and the two compilers still use matched
  `-O3 -ffast-math -fomit-frame-pointer` settings, the same prebuilt GCC
  libraries and GCC linker, `-mvu0` **off**, and the same local source edit
  to `src/ps2/ps2_video.c` (left untouched).

### 3 x 10,000-frame Windjammers controls (PCSX2 only)

Warm-up **500** frames; sound on; GUI off; no streaming cache;
`-nogui -batch -unlimited -elf`; no VU0. Values are medians of **three**
successful 10,000-frame samples each, in seconds of measured guest time.

| Build | Inlined bus-error handler | Outlined handler | Outlining effect |
| --- | ---: | ---: | ---: |
| GCC 15.2.0 | **55.341542 s** | **55.304675 s** | -0.036867 s |
| New Clang `6f31245b` | **61.442633 s** | **61.188186 s** | **-0.254447 s** |

With the original inlined handler, the new Clang is about **11.0% slower**
than GCC in PCSX2, down from about **13.1%** for old Clang with the same
RAM/bus-error hints. Its median time improves from **62.575038 s** to
**61.442633 s**, or **1.132405 s (1.81%)**. No real-PS2 hardware timing
can be inferred from this comparison.

The outline variant improves the new Clang by about **0.41%** under PCSX2;
it does **not** explain the remaining gap. GCC changes by less than 0.1%.
The option is therefore retained as **opt-in**, not applied by default to
the released emulator. To build the variant, add
`-DPS2_C68K_OUTLINE_BUS_ERROR=ON` to a PS2 CMake configuration. Its
default is `OFF`; it is rejected for non-PS2 platforms.

The updated, *inlined* LLVM build was additionally tested on two more
resident MVS attract-mode ROMs, each with **three** 10,000-frame trials and
identical benchmark flags:

| MVS ROM | GCC 15.2.0 median | New Clang median | Clang time increase |
| --- | ---: | ---: | ---: |
| `wjammers` | 55.341542 s | 61.442633 s | **11.02%** |
| `pbobbl2n` | 65.930692 s | 70.789812 s | **7.37%** |
| `trally` | 72.036340 s | 79.511911 s | **10.38%** |

These results are consistent with a remaining PCSX2 timing difference
across MVS workloads. They still do **not** establish a comparable gap
on the real EE, whose cache/pipeline behavior PCSX2 does not accurately
reproduce. Only Windjammers was retested with the outlined handler.

### Current static C68k machine code

Instructions counted directly from the function's section in the retained
PS2 GCC/Clang `.obj` files with GNU `objdump -d`; static counts are **not**
dynamic instruction counts.

| Object | `C68k_Exec` bytes | Instructions | `addiu` | `nop` | `jalr` |
| --- | ---: | ---: | ---: | ---: | ---: |
| Previous Clang `a6b9e2f`, inline, with hints | 246,084 | 61,522 | 9,623 | 1,180 | 3,082 |
| New Clang `6f31245b`, inline, with hints | **227,436** | **56,860** | **5,122** | 935 | 3,082 |
| New Clang `6f31245b`, outlined | 225,868 | 56,468 | 4,665 | 983 | 3,075 |
| GCC 15.2.0, inline | 214,616 | 53,654 | 4,588 | 366 | 3,080 |
| GCC 15.2.0, outlined | 213,184 | 53,296 | 4,670 | 358 | 3,073 |

New Clang reduces the same inlined interpreter by **18,648 bytes (7.58%)**
and **4,501 static `addiu`** instructions. These numbers strongly support
the address-fold optimization described in the developer's response; they
do **not** prove that code-size reduction causes the observed PCSX2
performance change or that it has the same effect on R5900 hardware.

### Source change, semantic check and limitations

- `src/cpu/m68000/c68k.c` now has a guarded
  `C68k_HandleBusError(c68k_struc *, uintptr_t)` helper, containing **all**
  the original bus-error stack/PC/flag operations. The original inline
  implementation is still the default. The helper is only compiled when
  `C68K_OUTLINE_BUS_ERROR` is defined; PS2 CMake exposes this through the
  opt-in switch noted above.
- A new C99 host test builds the genuine interpreter with the handler both
  inline and outlined, forces `BusErrHandling=1`, injects an exception
  vector targeting a NOP, and checks the resulting PC, stack pointer,
  supervisor state, write count/order checksum and cycle consumption.
  Both configurations **pass** and produce identical expected states;
  the two new registered Desktop MVS CTest cases both passed.
- **All** captured emulator runs still report `status=unverified` because
  asynchronous sound/Z80 state diverges between otherwise repeated runs;
  wrapper I/O counts are zero but do not cover all possible SDK/IOP paths.
  These are *descriptive PCSX2 measurements*, not validated speedups.
- The old PS2Link-based **full-emulator hardware benchmarking** path remains
  blocked by a previous GS/CLUT startup problem. No new physical hardware
  experiment was completed. The compiler developer's small, real-hardware
  C68k test and this full NJEMU PCSX2 test must not be conflated.

### Reproduce locally (ignored artifacts)

`build_ps2_compiler_r5900_20261024/` contains all four PS2
build directories (`gcc`, `llvm`, `gcc_outline`, `llvm_outline`), raw
Windjammers, Puzzle Bobble 2 and Thrash Rally trials under `trials/`,
corresponding CMake configure/build logs and
the small, ignored exception-path comparison harness.

The old Clang objects and logs are preserved under
`build_ps2_compiler_wjammers_aligned_20261022/`. The new configuration
uses the existing ignored `r5900-feature-toolchain.cmake` and the **updated
installed LLVM binary** rather than modifying that historical file.

Recommended next step: run **full NJEMU on a real PS2**, or replay a larger
trace of executed C68k opcode handlers with the production memory callbacks
on the EE. PCSX2 alone cannot resolve whether the remaining ~11% difference
is an actual R5900 hardware performance issue.

## 2026-10-25 — new R5900 Clang `455ff9dd` Windjammers rerun

The freshly installed Clang 24.0.0git reports
`455ff9dd4b8a1c119df8046ddb19320d95f13fa8` from
`rickgaiser/llvm-project` (`r5900-feature`). NJEMU source was at
`f58bef110ac8810c5b37455afed0b8ccb668f420`; both compilers were
freshly built from this identical snapshot, without source modifications.
GCC is 15.2.0; existing GCC-built PS2SDK/gsKit/ps2_drivers and the
GNU linker are used for both applications. No LLD or PS2Link was used.

Each executable uses `-O3 -ffast-math`, the PS2 MVS RAM/native-word
fast paths, `GUI=OFF`, `USE_CACHE=OFF`, sound enabled and the default
**inlined** C68k bus-error handler. The benchmark runs in
**PCSX2 v2.9.142**, unlimited mode, with 500 warm-up and 10,000 measured
frames, three independent trials per configuration. All trials completed
with their expected frame count, 4 MiB resident graphics, 4 MiB resident
PCM, and zero *wrapped* I/O operations.

### Actual repeated-run timing

Numbers below are **guest-reported elapsed seconds**, not host wall time.
Smaller is better. Previous results came from the same PCSX2 version;
comparing the fresh GCC control with the fresh Clang run is the most
controlled comparison.

| Build | Individual 10,000-frame trials (s) | Median (s) | Difference vs current GCC |
| --- | --- | ---: | ---: |
| GCC 15.2.0 (fresh) | 55.313896, 55.326542, 55.326542 | **55.326542** | baseline |
| Previous Clang `6f31245b` | historical three trials | **61.442633** | +11.02% (against its contemporaneous GCC baseline) |
| New Clang `455ff9dd` | 58.574831, 58.534310, 58.579214 | **58.574831** | **+5.87%** |

The new Clang median is **2.867802 s (4.67%) faster** than the old
Clang median in PCSX2, but **3.248289 s slower (5.87%)** than the current
fresh GCC control. Roughly half of the previous relative regression has
been recovered, **not all** of it. CPU-core update time increased from
about **45.754 s** (GCC) to **48.061 s** (Clang), and screen update time
from about **9.101 s** to **10.040 s**. Timing spread was less than
0.08% for each pure-compiler build.

### Isolating the remaining code-generation gap

The following **three-trial exploratory hybrids** were made only inside
ignored build directories. Starting from the new LLVM MVS link, selected
NJEMU translation-unit objects were replaced with fresh GCC `-O3` objects;
all other LLVM objects, SDKs and linker remained unchanged. The hybrid
logs call themselves `compiler=LLVM`, meaning *majority compiler*, not
that every object was built by Clang. Swapping objects may also change
link layout; the values are diagnostic rather than accepted speedups.

| Objects replaced with GCC in new Clang ELF | Median (s) | Compared with pure new Clang |
| --- | ---: | ---: |
| None | 58.574831 | baseline |
| `src/cpu/m68000/c68k.c.obj` | **56.826993** | -1.747838 s |
| `c68k.c.obj`, `src/mvs/memintrf.c.obj`, `src/cpu/z80/cz80.c.obj`, `src/sound/ym2610.c.obj` | **55.171087** | -3.403744 s |
| Pure GCC (no substitutions) | **55.326542** | -3.248289 s |

Replacing **C68k alone recovers ~54% of the observed gap**. Replacing
those **four known hot translation units** brings the largely LLVM
binary within approximately 0.28% of the pure-GCC control (slightly
faster in this linker-layout-sensitive experiment). These same units
were independently identified in the earlier Windjammers investigation;
the current regression remains concentrated there rather than being
spread indiscriminately through the emulator.

The static `C68k_Exec` symbol size is **223,204 bytes** under new Clang,
versus **227,436 bytes** with previous Clang and **214,616 bytes** with
fresh GCC. Smaller code alone does not establish faster execution.

### Correctness and provenance limits

All fresh pure-GCC, pure-Clang and hybrid trials have matching sampled
M68000 fingerprint `1087498fd82be399` and RAM fingerprint
`d9c70061d1a1352e`. The Z80 fingerprint **varies between trials of
the same compiler**, so full end-state equivalence remains unverified.
Reported `state=unverified` and `status=unverified` are intentional;
the official report rightly rejects these as *validated speedups*.
The I/O wrapper does not audit every possible IOP operation.

All raw logs, individual `trials.csv`, `summary.md`, source revision,
installed compiler version, matched build directories and the temporary
hybrid script are preserved under the ignored
`build_ps2_wjammers_llvm455_20261025/`. The standalone LLVM
`MVS.ELF` and `c68k.c.obj` were restored **SHA-256 byte-for-byte**
after both hybrid runs. The existing ROMs and `resources/` were
neither modified nor committed.

**Recommended next compiler investigation:** compare the executed
`C68k_Exec` hot opcode handler paths and MVS memory callbacks in
GCC vs Clang first; also compare `Cz80_Exec` and
`YM2610ContextUpdate`. The benchmark shows real remaining **PCSX2**
overhead but says nothing conclusive about **physical EE** throughput.
