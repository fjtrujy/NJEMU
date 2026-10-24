#!/usr/bin/env bash
# Local, opt-in LLVM build of the exact canonical PS2 release core matrix.
# GCC continues to compile the installed PS2SDK, gsKit and ps2_drivers;
# this script never changes the default GCC workflow or its output paths.
set -euo pipefail

if [[ $# -gt 1 ]]; then
    echo "Usage: $0 [ignored-output-root]" >&2
    exit 2
fi
if [[ ! -f CMakeLists.txt || ! -f cmake/PS2LLVMToolchain.cmake ]]; then
    echo "Run from NJEMU repository root" >&2
    exit 2
fi
: "${PS2DEV:?Set PS2DEV to your installed PS2 toolchain root}"
: "${PS2SDK:?Set PS2SDK to your installed PS2SDK root}"
root="$PWD"
output_root="${1:-$root/build_llvm_ps2_release}"
mkdir -p "$output_root"
output_root="$(cd "$output_root" && pwd)"
[[ "$output_root" != "$root" ]] || {
    echo "Output root must be a separate, ignored directory" >&2; exit 2;
}
[[ -e "$PS2DEV/ee/bin/mips64r5900el-ps2-elf-as" ]] || {
    echo "Missing GNU R5900 assembler" >&2; exit 2;
}
[[ -x "$PS2DEV/llvm/bin/clang" ]] || {
    echo "Missing installed R5900 Clang" >&2; exit 2;
}
[[ -s "$PS2SDK/ports/lib/libps2_drivers_img.a" ]] || {
    echo "Install the GCC-built ps2_drivers into PS2SDK first" >&2; exit 2;
}

# -fno-integrated-as expects a generic executable named 'as'.
# Keep this shim in the isolated output directory, never in the toolchain.
as_dir="$output_root/gnu-as"
mkdir -p "$as_dir"
if [[ ! -e "$as_dir/as" && ! -L "$as_dir/as" ]]; then
    ln -s "$PS2DEV/ee/bin/mips64r5900el-ps2-elf-as" "$as_dir/as"
fi
[[ "$(cd "$as_dir" && realpath as)" == "$(realpath "$PS2DEV/ee/bin/mips64r5900el-ps2-elf-as")" ]] || {
    echo "Unexpected assembler symlink already exists: $as_dir/as" >&2; exit 2;
}

export PATH="$PS2DEV/ee/bin:$PS2SDK/bin:$PATH"
export NJEMU_PS2_LLVM_AS_DIR="$as_dir"
export NJEMU_PS2_RELEASE_TOOLCHAIN_FILE="$root/cmake/PS2LLVMToolchain.cmake"
export NJEMU_RELEASE_OUTPUT_ROOT="$output_root"
export NJEMU_RELEASE_CORE_CONCURRENCY="${NJEMU_RELEASE_CORE_CONCURRENCY:-2}"
export NJEMU_RELEASE_CORE_BUILD_JOBS="${NJEMU_RELEASE_CORE_BUILD_JOBS:-4}"

echo "Clang: $("$PS2DEV/llvm/bin/clang" --version | head -n 1)"
echo "Output: $output_root"

# Derive the same version as release.yml; release packaging validates all
# four stage/version.txt files against this value.
cmake -DNJEMU_SOURCE_DIR="$root" \
      -DNJEMU_VERSION_OUTPUT_FILE="$output_root/release-version.txt" \
      -P cmake/NJEMUVersion.cmake
version="$(cat "$output_root/release-version.txt")"

bash tools/ci/build_release_cores.sh PS2

cmake -DNJEMU_SOURCE_DIR="$root" \
      -DNJEMU_RELEASE_PLATFORM=ps2 \
      -DNJEMU_RELEASE_VERSION="$version" \
      -DNJEMU_INSTALL_CPS1="$output_root/release-stage/CPS1" \
      -DNJEMU_INSTALL_CPS2="$output_root/release-stage/CPS2" \
      -DNJEMU_INSTALL_MVS="$output_root/release-stage/MVS" \
      -DNJEMU_INSTALL_NCDZ="$output_root/release-stage/NCDZ" \
      -DNJEMU_RELEASE_OUTPUT_DIR="$output_root/release-assets" \
      -P cmake/NJEMUReleasePackage.cmake

echo "LLVM PS2 release package:"
ls -lh "$output_root/release-assets/"
