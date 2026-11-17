#!/bin/sh
# Builds the SDL3 the 3DS port links: upstream SDL's own Nintendo 3DS port, from
# the vendored third_party/SDL (3.4.14), as a static library installed with its
# CMake package files. docs/3DS.md.
#
#   build_sdl.sh <SDL source> <build dir> <install prefix>
#
#   SDL source      third_party/SDL of this checkout (or any SDL 3.4 source)
#   build dir       scratch build directory, created if missing
#   install prefix  where include/, lib/ and lib/cmake/SDL3 go
#
# Run it from the devkitPro MSYS2 shell (pacman: 3ds-dev, 3ds-cmake,
# 3ds-pkg-config). The 3DS port is upstream's; tools/3ds/sdl/*.patch are
# applied to the source first, as on the Switch (skipped when already
# applied): the audio driver's waits made finite (0001), its channel reset
# also after the system cancelled the DSP (0002).
#
# What SDL is for on the 3DS: input (the video driver scans the buttons and
# turns "close" into SDL_EVENT_QUIT; the joystick driver reads them), audio
# (ndsp) and the app lifecycle. It never draws, so its renderer is off (the
# GUI launcher, the only thing that draws through it, is not built for the
# 3DS), and so are the camera, GPU, dialogs (no 3DS implementation), HIDAPI and
# Vulkan. Precompiled headers are off: MSYS2's CMake writes an MSYS path into
# cmake_pch.h that the native compiler cannot open.

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

cmake -S "$SRC" -B "$BUILD" -G "Unix Makefiles" \
    -DCMAKE_TOOLCHAIN_FILE="$DEVKITPRO/cmake/3DS.cmake" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX="$PREFIX" \
    -DCMAKE_DISABLE_PRECOMPILE_HEADERS=ON \
    -DCMAKE_DEPENDS_USE_COMPILER=FALSE \
    -DSDL_SHARED=OFF -DSDL_STATIC=ON \
    -DSDL_TESTS=OFF -DSDL_TEST_LIBRARY=OFF -DSDL_EXAMPLES=OFF -DSDL_INSTALL=ON \
    -DSDL_CAMERA=OFF -DSDL_GPU=OFF -DSDL_DIALOG=OFF -DSDL_HIDAPI=OFF -DSDL_VULKAN=OFF \
    -DSDL_RENDER=OFF

JOBS=${JOBS:-$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)}
cmake --build "$BUILD" -j"$JOBS"
cmake --install "$BUILD"
