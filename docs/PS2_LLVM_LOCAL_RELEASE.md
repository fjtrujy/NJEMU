# Local PS2 Release Builds with the R5900 LLVM Toolchain

This is an **opt-in, experimental** PS2 build path. The normal CI/release
builds still use GCC and `$PS2DEV/share/ps2dev.cmake`; they are not replaced
by this configuration.

## Compiler boundaries

- NJEMU's PS2 sources: installed R5900 Clang, target
  `--target=mips64el -mcpu=r5900 -mabi=n32`, `-O3 -ffast-math`.
- External assembler: the existing GNU `mips64r5900el-ps2-elf-as`, selected
  through a local `as` symlink created in the ignored output directory.
- Linker, runtime and dependencies: GCC 15.2 / existing GCC-built PS2SDK,
  gsKit and ps2_drivers; these libraries are **not** compiled with Clang.
- LLVM builds disable the project's usual PS2 LTO because Clang bitcode
  cannot be consumed by the GCC linker. GCC/PSP LTO policy is unchanged.
- Tested installed Clang: 24.0.0git, r5900-feature at
  `6f31245b7434ed1c55d341d544d10a7e8d003766`
  (2026-10-24). Rebuild after updating the installed toolchain.

## Local LLVM release build

From the NJEMU repository root on a host with the PS2 toolchain installed:

```sh
export PS2DEV=/path/to/ps2dev
export PS2SDK="$PS2DEV/ps2sdk"
NJEMU_RELEASE_CORE_CONCURRENCY=2 \
NJEMU_RELEASE_CORE_BUILD_JOBS=4 \
  bash tools/ci/build_ps2_llvm_local.sh
```

The script uses the **same canonical core matrix** as
`.github/workflows/release.yml` and `tools/ci/build_release_cores.sh PS2`,
with an explicit `cmake/PS2LLVMToolchain.cmake` override:

| Core | Canonical configuration | Launch binaries in staging |
| --- | --- | --- |
| CPS1 | GUI, save states and command list; cache off, internal IRX | `CPS1.ELF` |
| CPS2 | GUI, save states and command list; cache and external IRX on | `BOOT.ELF`, `CPS2`, `ps2_drivers.irximg`, `elf_path.ini` |
| MVS | As CPS2, plus dirty sprite upload | `BOOT.ELF`, `MVS`, `ps2_drivers.irximg`, `elf_path.ini` |
| NCDZ | GUI, save states and command list; cache off, internal IRX | `NCDZ.ELF` |

Files stay in ignored `build_llvm_ps2_release/`:

- `build-release-{cps1,cps2,mvs,ncdz}/`: individual build trees.
- `release-stage/{CPS1,CPS2,MVS,NCDZ}/`: per-core installed binaries
  and data required by the canonical packager.
- `release-assets/njemu-<version>-ps2.zip` and matching `.zip.json`:
  distributable archive and its SHA-256 sidecar.
- `build_release_core_logs/`: per-core configure/build/install logs.
- `gnu-as/as`: generated link to the installed GNU assembler, not committed.

The script aborts if any core fails and only packages after all four have
been installed and checked. It never stages the user's ROMs or caches from
`resources/`. The combined ZIP uses the same manifest and release rules
as the normal CI packaging job.

The script accepts an optional alternate ignored output directory, e.g.
`bash tools/ci/build_ps2_llvm_local.sh build_llvm_ps2_release_second_run`.

## GCC regression validation

GCC remains the default when `NJEMU_PS2_RELEASE_TOOLCHAIN_FILE` is unset.
To run the unmodified canonical PS2 build separately, without replacing
LLVM artifacts:

```sh
export PATH="$PS2DEV/ee/bin:$PS2SDK/bin:$PATH"
NJEMU_RELEASE_OUTPUT_ROOT="$PWD/build_gcc_ps2_release_validation" \
NJEMU_RELEASE_CORE_CONCURRENCY=2 NJEMU_RELEASE_CORE_BUILD_JOBS=4 \
  bash tools/ci/build_release_cores.sh PS2
bash tests/release_parallel_build_tests.sh
```

The GCC CI workflow at `.github/workflows/ps2.yml` is intentionally
unchanged. The `release.yml` job invokes the same script **without** the LLVM
override. CI itself runs on Linux with a separately provisioned PS2DEV
container, so local success is a compatibility check, not a substitute for
a green GitHub Actions run.

## Local validation on 2026-10-24

Using the installed LLVM revision above:

- LLVM: all **four** canonical PS2 cores compiled and installed; the normal
  CMake release packager validated their file layouts and produced a ZIP.
- GCC: all **four** canonical PS2 cores compiled and installed; the same
  release packager produced a separate ZIP. GCC selected the original
  `ps2dev.cmake`, not the LLVM toolchain.
- The existing release-concurrency/failure tests passed for all platforms,
  as did the new isolated-LLVM-override test.
- Clang-only strict-warning repairs were necessary in `src/common/filer.c`
  and `src/ncdz/cdrom.c` (cast bytes to `unsigned char` before
  `tolower`/`toupper`). Dead NCDZ upload state that was only ever
  assigned, never read, was removed.
- **Not tested:** running these complete release binaries on physical PS2
  hardware or guaranteeing LLVM performance there. See
  `docs/LLVM_R5900_CODEGEN_ISSUE_REPORT.md` for the separate PCSX2 and
  real-hardware performance evidence and its limitations.

## LLD linker feasibility investigation (2026-10-24)

**Outcome: LLVM LLD links small R5900/N32 programs, but cannot yet replace
the GCC/binutils link step for the current full NJEMU release and installed
SDK.** The existing GCC linker remains the supported default.

LLD 24.0.0 was built from the same `r5900-feature` revision,
`6f31245b7434ed1c55d341d544d10a7e8d003766`, in separate LLVM build
directories, without installing or modifying the compiler toolchain:

- The installed `$PS2DEV/llvm/bin` contains Clang but **no `ld.lld`**;
  its original LLVM build had `LLVM_ENABLE_PROJECTS=clang` and
  `LLVM_ENABLE_ZLIB=OFF`.
- A standalone LLD built against that LLVM installation could not consume
  PS2SDK's `ELFCOMPRESS_ZLIB` debug sections, so a separate LLD was built
  with `LLVM_ENABLE_ZLIB=FORCE_ON` and the host's zlib library.
- The resulting LLD accepts `-m elf32ltsmipn32`. A simple
  `--target=mips64el -mcpu=r5900 -mabi=n32` test object assembled by
  **Clang's integrated assembler** links to an ELF32/N32 executable
  retaining `EF_MIPS_MACH_5900`. A GNU-assembled object works as well
  after normalizing its symbol table with `llvm-objcopy`.
- Unmodified GNU-assembled objects such as `crt0.o`, `crtbegin.o` and
  `ps2_drivers.a` fail LLD's symbol table checks
  (`symbol (...) has invalid binding: 0`). Their `.symtab` tables have
  `STB_LOCAL` symbols at or after the advertised first-global index
  (`sh_info`). On temporary copies, `llvm-objcopy` reorders symbols
  and updates `sh_info`, allowing the link to progress.
- Current NJEMU Clang objects advertise `-mdouble-float` in
  `.MIPS.abiflags`, whereas GCC/PS2SDK use **`-msingle-float`**.
  LLD rejects the incompatible FP ABIs. Adding `-msingle-float` to Clang
  produces the expected single-precision ABI metadata. A separate full
  CPS1 configuration was successfully compiled and GNU-linked with that
  flag. This is a diagnostic build, **not** a release-validated flag change.
- After normalizing copies of the CPS1 objects, startup files and required
  archives and compiling CPS1 with `-msingle-float`, LLD still reports
  unresolved `gsKit_*` and `dmaKit_*` symbols. The installed
  GCC-built `libgskit.a` contains GCC LTO objects whose definitions
  are visible to `gcc-nm` with the GCC plugin, but not as ordinary native
  symbols to `nm` or LLD. `llvm-objcopy` cannot turn GCC LTO bytecode
  into native definitions. This is an **independent, remaining blocker**.

All reproductions are retained in ignored local directories:
`build_llvm_ps2_release/lld-link-probe/`,
`llvm-project/build-ps2-r5900-feature-lld/` and
`llvm-project/build-ps2-r5900-feature-lld-zlib/`. No SDK archive or
installed compiler/linker was overwritten.

Before enabling LLD for production, provide native/non-GCC-LTO copies of
the required PS2SDK/gsKit/ps2_drivers libraries (or a validated equivalent
link-plugin solution), fix the symbol-table interoperability rather than
requiring per-build copies, align Clang's FP ABI with PS2SDK, and then verify
**all four** canonical PS2 ELF layouts, release packaging, emulator startup
and real hardware. Passing a library-free linker smoke test alone is not
enough to change the release linker.
