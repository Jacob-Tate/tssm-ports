#!/bin/sh
# Builds the SDL3 the Switch port links: devkitPro's SDL fork, branch
# switch-sdl-3.4, as a static library installed with its CMake package files.
#
#   build_sdl.sh <SDL source> <build dir> <install prefix>
#
#   SDL source      a clone of https://github.com/devkitPro/SDL, branch
#                   switch-sdl-3.4 (tested: c329016c0e338ab4397ad930f83330c9fe058348)
#   build dir       scratch build directory, created if missing
#   install prefix  where include/, lib/ and lib/cmake/SDL3 go
#
# Run it from the devkitPro MSYS2 shell (pacman: switch-dev, switch-mesa,
# switch-libdrm_nouveau, switch-glad). Applies tools/switch/sdl/*.patch first
# (skipped when already applied).
#
# Options: static only; no tests or examples; camera, GPU, dialogs (no Switch
# implementation; SDL's stubs report "unsupported"), HIDAPI and Vulkan off.
# The renderer stays on: the GUI launcher draws with SDL_Renderer before the
# game has a device. Precompiled headers are off: MSYS2's CMake writes an MSYS
# path into cmake_pch.h that the native compiler cannot open.

set -e

if [ $# -ne 3 ]; then
    echo "usage: $0 <SDL source> <build dir> <install prefix>" >&2
    exit 2
fi

SRC=$1
BUILD=$2
PREFIX=$3
HERE=$(cd "$(dirname "$0")" && pwd)
DEVKITPRO=${DEVKITPRO:-/opt/devkitpro}

for p in "$HERE"/sdl/*.patch; do
    [ -f "$p" ] || continue
    if git -C "$SRC" apply --check "$p" 2>/dev/null; then
        git -C "$SRC" apply "$p"
        echo "SDL: applied $(basename "$p")"
    elif git -C "$SRC" apply --check -R "$p" 2>/dev/null; then
        echo "SDL: $(basename "$p") already applied"
    else
        echo "SDL: $(basename "$p") does not apply" >&2
        exit 1
    fi
done

# The source and build folders as gcc sees them (on Windows, MSYS2 hands it
# Windows paths), mapped away: __FILE__, which SDL's assertions and error
# messages carry, then starts at the source folder, so tssm.nro does not carry
# the folders SDL was built in. CMake adds CFLAGS to the toolchain's own flags
# when it first configures a build folder.
gccpath() { if command -v cygpath >/dev/null 2>&1; then cygpath -m "$1"; else printf '%s' "$1"; fi; }
mkdir -p "$BUILD"
WSRC=$(gccpath "$(cd "$SRC" && pwd)")
WBUILD=$(gccpath "$(cd "$BUILD" && pwd)")
export CFLAGS="${CFLAGS:+$CFLAGS }-ffile-prefix-map=$WSRC/= -ffile-prefix-map=$WBUILD/="

cmake -S "$SRC" -B "$BUILD" -G "Unix Makefiles" \
    -DCMAKE_TOOLCHAIN_FILE="$DEVKITPRO/cmake/Switch.cmake" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX="$PREFIX" \
    -DCMAKE_DISABLE_PRECOMPILE_HEADERS=ON \
    -DSDL_SHARED=OFF -DSDL_STATIC=ON \
    -DSDL_TESTS=OFF -DSDL_TEST_LIBRARY=OFF -DSDL_EXAMPLES=OFF -DSDL_INSTALL=ON \
    -DSDL_CAMERA=OFF -DSDL_GPU=OFF -DSDL_DIALOG=OFF -DSDL_HIDAPI=OFF -DSDL_VULKAN=OFF \
    -DSDL_RENDER=ON

JOBS=${JOBS:-$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)}
cmake --build "$BUILD" -j"$JOBS"
cmake --install "$BUILD"
