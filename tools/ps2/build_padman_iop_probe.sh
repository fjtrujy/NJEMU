#!/bin/sh
# Build an isolated opt-in PADMAN acquisition probe from PS2SDK source.
# Never modify the installed SDK or the NJEMU production IRX.
set -eu

if [ "$#" -lt 2 ] || [ "$#" -gt 3 ]; then
    echo "Usage: $0 /path/to/ps2sdk/source /path/to/ignored/build-dir [--timer-4ms]" >&2
    exit 2
fi
if [ "$#" -eq 3 ] && [ "$3" != "--timer-4ms" ]; then
    echo "Unknown option: $3" >&2
    exit 2
fi

sdk_source=$1
output=$2
script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)

case "$output" in
    resources|resources/*|*/resources|*/resources/*)
        echo "Do not build diagnostic files under resources/: $output" >&2
        exit 1
        ;;
esac

if [ ! -f "$sdk_source/iop/input/padman/Makefile" ]; then
    echo "PS2SDK PADMAN sources not found: $sdk_source" >&2
    exit 1
fi
if [ -e "$output" ]; then
    echo "Output already exists (use a new ignored build directory): $output" >&2
    exit 1
fi

mkdir -p "$output"
cp -R "$sdk_source/iop/input/padman/src" "$output/src"
cp -R "$sdk_source/iop/input/padman/include" "$output/include"
cp "$sdk_source/iop/input/padman/Makefile" "$output/Makefile"
patch -d "$output" -p1 --batch --forward < "$script_dir/padman_iop_probe.patch"
if [ "$#" -eq 3 ]; then
    patch -d "$output" -p1 --batch --forward < "$script_dir/padman_iop_timer_4ms.patch"
fi

PS2SDKSRC="$sdk_source" make -C "$output" IOP_BIN=irx/padman.irx -j4
echo "$output/irx/padman.irx"
