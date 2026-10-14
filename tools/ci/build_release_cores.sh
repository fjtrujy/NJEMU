#!/usr/bin/env bash
# Build the four NJEMU release cores concurrently in independent build/install
# trees. Platform dependencies are installed once by the calling CI job.
set -euo pipefail

if [[ $# -ne 1 ]]; then
  echo "Usage: $0 DESKTOP|PSP|PS2|PSVITA" >&2
  exit 2
fi

platform="$1"
case "$platform" in
  DESKTOP) toolchain_file='' ;;
  PSP)
    : "${PSPDEV:?PSPDEV is required}"
    toolchain_file="$PSPDEV/psp/share/pspdev.cmake"
    ;;
  PS2)
    : "${PS2DEV:?PS2DEV is required}"
    toolchain_file="$PS2DEV/share/ps2dev.cmake"
    ;;
  PSVITA)
    : "${VITASDK:?VITASDK is required}"
    toolchain_file="$VITASDK/share/vita.toolchain.cmake"
    ;;
  *)
    echo "Unsupported release platform: $platform" >&2
    exit 2
    ;;
esac

if [[ ! -f CMakeLists.txt || ! -f cmake/NJEMUReleasePackage.cmake ]]; then
  echo "Run this script from the NJEMU repository root" >&2
  exit 2
fi

cpu_count="$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 2)"
if [[ ! "$cpu_count" =~ ^[1-9][0-9]*$ ]]; then
  cpu_count=2
fi

# Two concurrent cores avoid duplicating toolchain setup across four runners
# and leave memory headroom for PSP/PS2/Vita linking. Override to benchmark.
core_concurrency="${NJEMU_RELEASE_CORE_CONCURRENCY:-2}"
if [[ ! "$core_concurrency" =~ ^[124]$ ]]; then
  echo "NJEMU_RELEASE_CORE_CONCURRENCY must be 1, 2, or 4" >&2
  exit 2
fi
if (( core_concurrency > cpu_count )); then
  core_concurrency="$cpu_count"
fi
jobs_per_core="${NJEMU_RELEASE_CORE_BUILD_JOBS:-$((cpu_count / core_concurrency))}"
if [[ ! "$jobs_per_core" =~ ^[1-9][0-9]*$ ]]; then
  echo "NJEMU_RELEASE_CORE_BUILD_JOBS must be a positive integer" >&2
  exit 2
fi

log_dir="$PWD/build_release_core_logs"
mkdir -p "$log_dir"
echo "Release $platform: $core_concurrency concurrent cores, $jobs_per_core build jobs per core"

build_core() {
  local core="$1"
  local core_lower build install
  local -a feature_args=()

  core_lower="$(printf '%s' "$core" | tr '[:upper:]' '[:lower:]')"
  build="build-release-$core_lower"
  install="$PWD/release-stage/$core"

  if [[ "$platform" == PS2 ]]; then
    # Preserve the canonical PS2 release matrix, including IRX packaging.
    feature_args=(
      -DGUI=ON -DSAVE_STATE=ON -DCOMMAND_LIST=ON
      -DUSE_CACHE=OFF -DPS2_FAST_CACHE=OFF
      -DPS2_EXTERNAL_IRX_IMAGE=OFF -DPS2_DIRTY_SPRITE_UPLOADS=OFF
    )
    if [[ "$core" == CPS2 || "$core" == MVS ]]; then
      feature_args+=(
        -DUSE_CACHE=ON -DPS2_FAST_CACHE=ON -DPS2_EXTERNAL_IRX_IMAGE=ON
      )
    fi
    if [[ "$core" == MVS ]]; then
      feature_args+=(-DPS2_DIRTY_SPRITE_UPLOADS=ON)
    fi
  fi

  local -a configure_args=(
    -S . -B "$build"
    -DCMAKE_BUILD_TYPE=Release
    -DPLATFORM="$platform"
    -DTARGET="$core"
    -DNJEMU_CANONICAL_RELEASE=ON
    -DBUILD_TESTING=OFF
  )
  if [[ -n "$toolchain_file" ]]; then
    configure_args+=("-DCMAKE_TOOLCHAIN_FILE=$toolchain_file")
  fi
  if [[ "$platform" == PS2 ]]; then
    configure_args+=("${feature_args[@]}")
  fi
  cmake "${configure_args[@]}"

  if [[ "$platform" == PS2 ]]; then
    grep -q '^GUI:BOOL=ON$' "$build/CMakeCache.txt"
    grep -q '^SAVE_STATE:BOOL=ON$' "$build/CMakeCache.txt"
    grep -q '^COMMAND_LIST:BOOL=ON$' "$build/CMakeCache.txt"
    if [[ "$core" == CPS2 || "$core" == MVS ]]; then
      grep -q '^USE_CACHE:BOOL=ON$' "$build/CMakeCache.txt"
      grep -q '^PS2_FAST_CACHE:BOOL=ON$' "$build/CMakeCache.txt"
      grep -q '^PS2_EXTERNAL_IRX_IMAGE:BOOL=ON$' "$build/CMakeCache.txt"
    else
      grep -q '^USE_CACHE:BOOL=OFF$' "$build/CMakeCache.txt"
      grep -q '^PS2_FAST_CACHE:BOOL=OFF$' "$build/CMakeCache.txt"
      grep -q '^PS2_EXTERNAL_IRX_IMAGE:BOOL=OFF$' "$build/CMakeCache.txt"
    fi
    if [[ "$core" == MVS ]]; then
      grep -q '^PS2_DIRTY_SPRITE_UPLOADS:BOOL=ON$' "$build/CMakeCache.txt"
    fi
  fi

  cmake --build "$build" --parallel "$jobs_per_core"
  cmake --install "$build" --prefix "$install"
}

# Every core has an isolated CMake build and install prefix. Wait for all
# members of each batch, reporting each failed log rather than losing errors
# from asynchronous builds. Never package a partially built release.
wait_batch() {
  local i failed=0
  for i in "${!pids[@]}"; do
    if wait "${pids[$i]}"; then
      echo "Completed $platform / ${cores[$i]}"
    else
      echo "::error title=Release build failed::$platform / ${cores[$i]}" >&2
      tail -n 80 "$log_dir/${cores[$i]}.log" >&2
      failed=1
    fi
  done
  pids=()
  cores=()
  if (( failed )); then
    return 1
  fi
}

pids=()
cores=()
for core in CPS1 CPS2 MVS NCDZ; do
  echo "Starting $platform / $core (log: $log_dir/$core.log)"
  (build_core "$core") > "$log_dir/$core.log" 2>&1 &
  pids+=("$!")
  cores+=("$core")
  if (( ${#pids[@]} >= core_concurrency )); then
    wait_batch
  fi
done
if (( ${#pids[@]} > 0 )); then
  wait_batch
fi
