#!/bin/sh
# Builds the FFmpeg DLLs a Windows build made with clang++ on the MSVC ABI
# (build-release.bat; the TSSM Dehydrated release) uses for the movies:
# avformat, avcodec, swscale, swresample and avutil, as DLLs, decoders only,
# LGPL: the decoder set of tools/mingw/build_ffmpeg.sh and of the Android and
# Switch builds. (vcpkg's ffmpeg, which src/SB/Core/pc/README.md also names,
# works the same; this one is smaller and leaves the folders it was built in
# out of the DLLs.)
#
#   tools/build_ffmpeg_msvc.sh <ffmpeg source> <work dir> <install prefix>
#
#   ffmpeg source   an extracted FFmpeg release (tested: n9.0.2)
#   work dir        scratch build directory, created if missing
#   install prefix  where bin/ (the DLLs), lib/, include/ and share/ go; hand
#                   it to the port's build as CMAKE_PREFIX_PATH (or TSSM_VCPKG
#                   for build-release.bat)
#
# Run it from MSYS2's MSYS shell started inside the Visual Studio environment
# of the width it is for, so that cl.exe is found: in an "x64 Native Tools
# Command Prompt" (x86 for 32-bit), or after vcvarsall.bat x64,
#     set MSYS2_PATH_TYPE=inherit
#     C:\msys64\usr\bin\bash -l
# Needs make and cmp: pacman -S make diffutils. None of the three paths may
# contain spaces: FFmpeg's configure does not support them.
#
# Threads and the nasm assembly are off, as in the MinGW build: none of these
# decoders uses frame or slice threads, and the movies are small (640x480).
# cl's default C runtime is the static one, so the DLLs need no Visual C++
# runtime DLL. No debug information: nothing names the folders it was built
# in, and the configure line compiled in (avcodec_configuration()) names them
# by their role.
set -e
if [ $# -ne 3 ]; then
    echo "usage: $0 <ffmpeg source> <work dir> <install prefix>" >&2
    exit 2
fi
SRC=$1
WORK=$2
PREFIX=$3
for p in "$SRC" "$WORK" "$PREFIX"; do
    case "$p" in
        *" "*)
            echo "'$p' has a space in it, which FFmpeg's configure cannot take" >&2
            exit 2
            ;;
    esac
done
if [ ! -x "$SRC/configure" ]; then
    echo "$SRC is not an FFmpeg source tree (no configure)" >&2
    exit 2
fi
for tool in make cmp cl; do
    if ! command -v $tool >/dev/null 2>&1; then
        case $tool in
            cl) echo "cl.exe not found: start this shell inside the Visual Studio environment" >&2 ;;
            *) echo "$tool not found: pacman -S make diffutils" >&2 ;;
        esac
        exit 2
    fi
done
case "$VSCMD_ARG_TGT_ARCH" in
    x64) ARCH=x86_64 ;;
    x86) ARCH=x86 ;;
    *)
        echo "VSCMD_ARG_TGT_ARCH is '$VSCMD_ARG_TGT_ARCH': start the shell after vcvarsall.bat x64 or x86" >&2
        exit 2
        ;;
esac
echo "FFmpeg for $VSCMD_ARG_TGT_ARCH ($ARCH) with cl"
SRC=$(cd "$SRC" && pwd)
mkdir -p "$WORK" "$PREFIX"
WORK=$(cd "$WORK" && pwd)
PREFIX=$(cd "$PREFIX" && pwd)
cd "$WORK"
# A copy of the source as src/ in the work folder: FFmpeg's configure then
# names it ./src, and cl's __FILE__, which FFmpeg's assertions carry, is the
# path it is given, so no folder of this machine gets into the DLLs.
if [ ! -f src/configure ]; then
    rm -rf src
    cp -R "$SRC" src
fi
# Headers are named by cl with their whole path; -d1trimfile takes the work
# folder off the front of those too.
WWORK=$(cygpath -m "$WORK")
./src/configure \
    --prefix="$PREFIX" \
    --toolchain=msvc --arch=$ARCH --target-os=win32 \
    --extra-cflags="-d1trimfile:$WWORK/" \
    --enable-shared --disable-static \
    --disable-autodetect --disable-programs --disable-doc --disable-debug \
    --disable-avdevice --disable-avfilter --disable-network \
    --disable-pthreads --disable-w32threads \
    --disable-x86asm \
    --disable-everything \
    --enable-avcodec --enable-avformat --enable-avutil --enable-swscale --enable-swresample \
    --enable-decoder=bink,binkaudio_rdft,binkaudio_dct \
    --enable-decoder=vorbis,opus,flac,mp3,mp3float \
    --enable-decoder=pcm_s16le,pcm_s24le,pcm_s32le,pcm_f32le,pcm_u8 \
    --enable-demuxer=bink,ogg,flac,mp3,wav \
    --enable-parser=vorbis,opus,flac,mpegaudio \
    --enable-protocol=file
# The configure line is compiled in as well: keep it, with the folders named
# by their role instead of their paths.
sedesc() { printf '%s' "$1" | sed 's/[][\.*^$|]/\\&/g'; }
sed -i -e "/^#define FFMPEG_CONFIGURATION /{
s|$(sedesc "$PREFIX")|<prefix>|g
s|$(sedesc "$(cygpath -m "$PREFIX")")|<prefix>|g
s|$(sedesc "$WWORK")|<build>|g
}" config.h
grep '^#define FFMPEG_CONFIGURATION ' config.h
JOBS=${JOBS:-$(nproc 2>/dev/null || echo 4)}
make -j"$JOBS"
make install
# vcpkg's layout, which the port's CMakeLists.txt looks for: the import
# libraries in lib/ (FFmpeg installs them beside the DLLs), and FFmpeg's
# licence in share/ffmpeg/copyright for the About tab.
mv "$PREFIX"/bin/*.lib "$PREFIX/lib/"
mkdir -p "$PREFIX/share/ffmpeg"
{ printf 'COPYING.LGPLv2.1:\n\n'; cat "$SRC/COPYING.LGPLv2.1"; } > "$PREFIX/share/ffmpeg/copyright"
ls "$PREFIX/bin"
