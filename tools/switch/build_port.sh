#!/bin/sh
# Builds the co-op program for Nintendo Switch homebrew: <build dir>/tssm-coop.nro
# (and tssm-coop.elf beside it, for addr2line on crash reports). docs/SWITCH.md.
#
#   build_port.sh <build dir> <prefix> [extra cmake arguments...]
#
#   build dir   created if missing; configured once, rebuilt after
#   prefix      where tools/switch/build_sdl.sh and build_ffmpeg.sh installed
#               SDL3 and FFmpeg (include/, lib/, lib/cmake/SDL3)
#
# Run it from the devkitPro MSYS2 shell (pacman: switch-dev, switch-cmake,
# switch-mesa, switch-libdrm_nouveau, switch-glad, switch-pkg-config). That
# shell's CMake, not a native Windows one: devkitPro's toolchain files refuse
# anything else. Two settings follow from that pairing:
#   -G "Unix Makefiles"                  there is no ninja in devkitPro's MSYS2
#   CMAKE_DEPENDS_USE_COMPILER=FALSE     the native GCC writes C:/ paths into
#                                        its .d files, which MSYS2's make reads
#                                        as two targets; CMake scans instead
#   CMAKE_DISABLE_PRECOMPILE_HEADERS=ON  MSYS2's CMake writes /e/... paths into
#                                        cmake_pch.h, which GCC cannot open

set -e

if [ $# -lt 2 ]; then
    echo "usage: $0 <build dir> <prefix> [cmake arguments...]" >&2
    exit 2
fi

BUILD=$1
PREFIX=$2
shift 2
HERE=$(cd "$(dirname "$0")" && pwd)
SRC=$(cd "$HERE/../.." && pwd)
DEVKITPRO=${DEVKITPRO:-/opt/devkitpro}

if [ ! -f "$BUILD/Makefile" ]; then
    cmake -S "$SRC" -B "$BUILD" -G "Unix Makefiles" \
        -DCMAKE_TOOLCHAIN_FILE="$DEVKITPRO/cmake/Switch.cmake" \
        -DCMAKE_BUILD_TYPE=RelWithDebInfo \
        -DCMAKE_DISABLE_PRECOMPILE_HEADERS=ON \
        -DCMAKE_DEPENDS_USE_COMPILER=FALSE \
        -DCMAKE_PREFIX_PATH="$PREFIX" -DCMAKE_FIND_ROOT_PATH="$PREFIX" \
        "$@"
fi

JOBS=${JOBS:-$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)}
cmake --build "$BUILD" -j"$JOBS"
ls -l "$BUILD"/*.nro
