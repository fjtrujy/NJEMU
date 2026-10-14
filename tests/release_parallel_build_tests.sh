#!/usr/bin/env bash
# Exercise release-core concurrency and failure propagation without toolchains.
set -euo pipefail

repo_root="$(cd "$(dirname "$0")/.." && pwd)"
script="$repo_root/tools/ci/build_release_cores.sh"
fixture="$(mktemp -d)"
trap 'rm -rf "$fixture"' EXIT
mkdir -p "$fixture/bin"

cat > "$fixture/bin/cmake" <<'MOCK'
#!/usr/bin/env bash
set -euo pipefail
case "$1" in
  -S)
    build='' core='' cache=OFF fast=OFF irx=OFF dirty=OFF
    for arg in "$@"; do
      case "$arg" in
        -B) ;; # -B is followed by the actual build directory
        -DTARGET=*) core="${arg#-DTARGET=}" ;;
        -DUSE_CACHE=*) cache="${arg#-DUSE_CACHE=}" ;;
        -DPS2_FAST_CACHE=*) fast="${arg#-DPS2_FAST_CACHE=}" ;;
        -DPS2_EXTERNAL_IRX_IMAGE=*) irx="${arg#-DPS2_EXTERNAL_IRX_IMAGE=}" ;;
        -DPS2_DIRTY_SPRITE_UPLOADS=*) dirty="${arg#-DPS2_DIRTY_SPRITE_UPLOADS=}" ;;
      esac
    done
    while (( $# > 0 )); do
      if [[ "$1" == -B ]]; then build="$2"; break; fi
      shift
    done
    [[ -n "$build" && -n "$core" ]]
    mkdir -p "$build"
    printf 'GUI:BOOL=ON\nSAVE_STATE:BOOL=ON\nCOMMAND_LIST:BOOL=ON\nUSE_CACHE:BOOL=%s\nPS2_FAST_CACHE:BOOL=%s\nPS2_EXTERNAL_IRX_IMAGE:BOOL=%s\nPS2_DIRTY_SPRITE_UPLOADS:BOOL=%s\n' "$cache" "$fast" "$irx" "$dirty" > "$build/CMakeCache.txt"
    ;;
  --build)
    core="${2##*build-release-}"
    printf 'start:%s\n' "$core" >> "$NJEMU_TEST_EVENTS"
    sleep 0.2
    if [[ "${NJEMU_TEST_FAIL_CORE:-}" == "$core" ]]; then
      echo "deliberate failure for $core" >&2
      exit 1
    fi
    printf 'end:%s\n' "$core" >> "$NJEMU_TEST_EVENTS"
    ;;
  --install)
    core="${2##*build-release-}"
    while (( $# > 0 )); do
      if [[ "$1" == --prefix ]]; then
        mkdir -p "$2"
        printf '%s\n' "$core" > "$2/core.txt"
        break
      fi
      shift
    done
    ;;
  *) echo "Unexpected mocked CMake call: $*" >&2; exit 1 ;;
esac
MOCK
chmod +x "$fixture/bin/cmake"

for platform in DESKTOP PSP PS2 PSVITA; do
  root="$fixture/$platform"
  mkdir -p "$root/cmake"
  : > "$root/CMakeLists.txt"
  : > "$root/cmake/NJEMUReleasePackage.cmake"
  (
    cd "$root"
    PATH="$fixture/bin:$PATH" \
      PSPDEV=/mock/pspdev PS2DEV=/mock/ps2dev VITASDK=/mock/vitasdk \
      NJEMU_TEST_EVENTS="$root/events.txt" \
      NJEMU_RELEASE_CORE_CONCURRENCY=2 \
      NJEMU_RELEASE_CORE_BUILD_JOBS=1 \
      bash "$script" "$platform" > "$root/output.txt"
  )
  for core in CPS1 CPS2 MVS NCDZ; do
    test -f "$root/release-stage/$core/core.txt"
  done
  # The first two cores must overlap instead of finishing serially.
  awk '
    /^start:cps1$/ { first_start=NR }
    /^start:cps2$/ { second_start=NR }
    /^end:cps1$/ { first_end=NR }
    /^end:cps2$/ { second_end=NR }
    END { exit !(first_start && second_start && first_end && second_end &&
                 first_start < second_end && second_start < first_end) }
  ' "$root/events.txt"
  echo "PASS $platform: four installs, concurrent starts, preserved arguments"
done

root="$fixture/failure"
mkdir -p "$root/cmake"
: > "$root/CMakeLists.txt"
: > "$root/cmake/NJEMUReleasePackage.cmake"
if (
  cd "$root"
  PATH="$fixture/bin:$PATH" \
    PSPDEV=/mock/pspdev PS2DEV=/mock/ps2dev VITASDK=/mock/vitasdk \
    NJEMU_TEST_EVENTS="$root/events.txt" \
    NJEMU_TEST_FAIL_CORE=cps2 \
    NJEMU_RELEASE_CORE_CONCURRENCY=2 \
    NJEMU_RELEASE_CORE_BUILD_JOBS=1 \
    bash "$script" PS2 > "$root/output.txt" 2>&1
); then
  echo "Expected the failed core to abort release packaging" >&2
  exit 1
fi
grep -q 'Release build failed' "$root/output.txt"
test -f "$root/release-stage/CPS1/core.txt"
test ! -f "$root/release-stage/MVS/core.txt"
echo 'PASS failure handling: logs surfaced, later batches skipped'
