#!/bin/sh
# Build the PC port with MinGW-w64 GCC, from an MSYS2 shell: MINGW64 for
# 64-bit, MINGW32 for 32-bit. The MinGW counterpart of build-debug.bat and
# build-release.bat (tools/pcbuild.bat), and laid out the same way.
#
#   tools/pcbuild-mingw.sh [Debug|Release] [backends]
#
#       Debug|Release   Release by default
#       backends        D3D9, D3D11 and GL3 in one executable by default. Name
#                       a list ("D3D11;GL3", quoted) or one (GL3, NULL) to
#                       build only that.
#
# The word size is the shell's: its gcc builds either 64-bit or 32-bit, never
# both. Each configuration builds into its own directory (build-mingw64-release,
# build-mingw32-debug, ...), and each puts tssm.exe and the test programs into
# bin\, as the MSVC scripts do: the last build wins; bin\BUILD-INFO.txt says
# which.
#
# Packages (pacman -S --needed, in the shell the build is for):
#     $MINGW_PACKAGE_PREFIX-toolchain $MINGW_PACKAGE_PREFIX-cmake $MINGW_PACKAGE_PREFIX-ninja
# plus git (pacman -S git, or Git for Windows, which is found in its default
# folder): CMake applies tools/librw/*.patch with it, to a checkout made by
# either.
#
# Movies (optional): TSSM_FFMPEG names an FFmpeg install prefix. Make one with
# tools/mingw/build_ffmpeg.sh: static, so tssm.exe needs no DLL. Unset, CMake
# looks in the shell's own prefix, where MSYS2's FFmpeg package is, if
# installed (64-bit only; its DLLs, about 100 with their dependencies, are
# copied into bin\; it is GPL-licensed, so such a build is for your own use).
# Without FFmpeg, movies are skipped.
#
# The game's files are not part of the build: see src/SB/Core/pc/README.md.

set -e

CONFIG=${1:-Release}
BACKENDS=$2
case "$CONFIG" in
    [Dd]ebug)     CONFIG=Debug ;;
    [Rr]elease)   CONFIG=Release ;;
    *) echo "ERROR: the configuration is Debug or Release, not $CONFIG" >&2; exit 1 ;;
esac

case "$MSYSTEM" in
    MINGW64|MINGW32) ;;
    *)
        echo "ERROR: run this from an MSYS2 MINGW64 shell (64-bit) or MINGW32 shell (32-bit)," >&2
        echo "       not from ${MSYSTEM:-a shell outside MSYS2}. MSYS2's Start menu has both, or run" >&2
        echo "       mingw64.exe or mingw32.exe in its folder. (UCRT64, the one MSYS2 opens by" >&2
        echo "       default, and CLANG64 are not tried yet.)" >&2
        exit 1
        ;;
esac

for tool in gcc cmake ninja; do
    if ! command -v $tool >/dev/null 2>&1; then
        echo "ERROR: $tool not found. In this shell:" >&2
        echo "       pacman -S --needed $MINGW_PACKAGE_PREFIX-toolchain $MINGW_PACKAGE_PREFIX-cmake $MINGW_PACKAGE_PREFIX-ninja" >&2
        exit 1
    fi
done
if ! command -v git >/dev/null 2>&1; then
    if [ -x "/c/Program Files/Git/cmd/git.exe" ]; then
        export PATH="$PATH:/c/Program Files/Git/cmd"
    else
        echo "ERROR: git not found (CMake applies tools/librw/*.patch with it): pacman -S git" >&2
        exit 1
    fi
fi

ROOT=$(cd "$(dirname "$0")/.." && pwd)
cd "$ROOT"
WROOT=$(cygpath -m "$ROOT")

TAG=$(echo "$MSYSTEM" | tr 'A-Z' 'a-z')
CFG=$(echo "$CONFIG" | tr 'A-Z' 'a-z')
BUILDDIR="build-$TAG-$CFG"

if [ ! -f third_party/librw/CMakeLists.txt ] || [ ! -f third_party/SDL/CMakeLists.txt ]; then
    echo "ERROR: the submodules are empty: git submodule update --init --depth 1" >&2
    exit 1
fi

set --
if [ -n "$BACKENDS" ]; then
    set -- "$@" "-DTSSM_RENDER_BACKENDS=$BACKENDS"
else
    set -- "$@" -UTSSM_RENDER_BACKENDS
fi
if [ -n "$TSSM_FFMPEG" ]; then
    if [ ! -f "$TSSM_FFMPEG/include/libavcodec/avcodec.h" ]; then
        echo "ERROR: TSSM_FFMPEG=$TSSM_FFMPEG has no include/libavcodec/avcodec.h" >&2
        exit 1
    fi
    set -- "$@" "-DCMAKE_PREFIX_PATH=$(cygpath -m "$TSSM_FFMPEG")"
fi
if [ -n "$TSSM_ICON_DIR" ]; then
    set -- "$@" "-DTSSM_ICON_DIR=$(cygpath -m "$TSSM_ICON_DIR")"
fi

echo "=== $CONFIG / $MSYSTEM -> $BUILDDIR, exe into bin/ ==="
cmake -S . -B "$BUILDDIR" -G Ninja \
    -DCMAKE_BUILD_TYPE="$CONFIG" \
    -DCMAKE_RUNTIME_OUTPUT_DIRECTORY="$WROOT/bin" \
    "$@"
cmake --build "$BUILDDIR"

BACKENDNAME=$(sed -n 's/^TSSM_RENDER_BACKENDS:[A-Z]*=//p' "$BUILDDIR/CMakeCache.txt")
printf '%s / %s / %s (MinGW gcc), built from %s\r\n' "$CONFIG" "$BACKENDNAME" "$MSYSTEM" "$BUILDDIR" \
    > bin/BUILD-INFO.txt
echo
echo "=== bin/tssm.exe is now $CONFIG / $BACKENDNAME / $MSYSTEM ==="
if [ ! -f bin/config.ini ]; then
    echo "    No bin/config.ini yet. The game writes one with the defaults on"
    echo "    first run; set [assets] path in it to your extracted Xbox disc."
fi
