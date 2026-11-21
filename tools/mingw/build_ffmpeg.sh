#!/bin/sh
# Builds the FFmpeg a MinGW build of the PC port links for the movies: static
# libavformat, libavcodec, libswscale, libswresample and libavutil, decoders
# only, LGPL: the decoder set of the Android and Switch builds.
#
#   tools/mingw/build_ffmpeg.sh <ffmpeg source> <work dir> <install prefix>
#
#   ffmpeg source   an extracted FFmpeg release (tested: n9.0.2)
#   work dir        scratch build directory, created if missing
#   install prefix  where include/ and lib/ go; hand it to the port's build as
#                   TSSM_FFMPEG (tools/pcbuild-mingw.sh) or CMAKE_PREFIX_PATH
#
# Run it from the MSYS2 shell of the width it is for: MINGW64 for 64-bit,
# MINGW32 for 32-bit. It builds with that shell's gcc. Needs make and cmp:
# pacman -S make diffutils. None of the three paths may contain spaces:
# FFmpeg's configure does not support them.
#
# What is built in, and why:
#   bink, binkaudio_rdft, binkaudio_dct   the game's movies (FMV/*.bik, Bink 1)
#   vorbis, opus, flac, mp3, pcm          replacement sounds from mods
#                                         (iModAudio.cpp; WAV works without FFmpeg)
# LGPL only: no --enable-gpl, no --enable-nonfree.
#
# Threads and the nasm assembly are off: none of these decoders uses frame or
# slice threads, and the assembly would need nasm for little gain at the
# movies' size (640x480, one 720x486). The result needs no DLL; the port links it into
# tssm.exe.

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
for tool in make cmp; do
    if ! command -v $tool >/dev/null 2>&1; then
        echo "$tool not found: pacman -S make diffutils" >&2
        exit 2
    fi
done

CC=${CC:-gcc}
MACHINE=$($CC -dumpmachine)
EXTRA_CFLAGS=
ARCH_OPTIONS=
case "$MACHINE" in
    x86_64-*-mingw32) ARCH=x86_64 ;;
    i686-*-mingw32)
        ARCH=i686
        # 32-bit Windows guarantees a 4-byte aligned stack, and GCC's code
        # for it, the port's included, assumes no more: a function that needs
        # 16 realigns. FFmpeg's configure tells GCC to assume 16 on entry
        # (-mpreferred-stack-boundary=4) and realigns only at some API entry
        # points; MSYS2's gcc vectorises with SSE2, and an aligned store to a
        # misaligned stack slot faults (seen in ff_decode_receive_frame_internal
        # on the first movie, and in the Ogg probe). This puts the assumption
        # back at 4. Realigning takes a register, which some of the inline
        # assembly cannot spare (libswscale/x86/rgb2rgb.c does not compile),
        # so the inline assembly goes too; the C code keeps up with these
        # movies.
        EXTRA_CFLAGS="-mincoming-stack-boundary=2"
        ARCH_OPTIONS="--disable-inline-asm"
        ;;
    *)
        echo "$CC targets $MACHINE: run this from an MSYS2 MINGW64 or MINGW32 shell" >&2
        exit 2
        ;;
esac
echo "FFmpeg for $MACHINE ($ARCH) with $CC"

mkdir -p "$WORK"
cd "$WORK"

# The source and work folders as gcc sees them (MSYS2 hands it Windows paths).
# __FILE__, which FFmpeg's assertions carry, then starts at the source folder,
# so tssm.exe does not carry the folders FFmpeg was built in.
WSRC=$(cygpath -m "$(cd "$SRC" && pwd)")
WWORK=$(cygpath -m "$(pwd)")
EXTRA_CFLAGS="${EXTRA_CFLAGS:+$EXTRA_CFLAGS }-ffile-prefix-map=$WSRC/= -ffile-prefix-map=$WWORK/="

"$SRC/configure" \
    --prefix="$PREFIX" \
    --cc="$CC" \
    --arch=$ARCH --target-os=mingw32 \
    --extra-cflags="$EXTRA_CFLAGS" $ARCH_OPTIONS \
    --enable-static --disable-shared \
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

# The configure line is compiled in as well (avcodec_configuration()): keep it,
# with the three folders named by their role instead of their paths.
sedesc() { printf '%s' "$1" | sed 's/[][\.*^$|]/\\&/g'; }
sed -i -e "/^#define FFMPEG_CONFIGURATION /{
s|$(sedesc "$PREFIX")|<prefix>|g
s|$(sedesc "$(cygpath -m "$PREFIX")")|<prefix>|g
s|$(sedesc "$WSRC")|<source>|g
s|$(sedesc "$WWORK")|<build>|g
}" config.h
grep '^#define FFMPEG_CONFIGURATION ' config.h

JOBS=${JOBS:-$(nproc 2>/dev/null || echo 4)}
make -j"$JOBS"
make install

# The license, where the GUI's About tab reads it (CMakeLists.txt,
# tssm_gui_notice "FFmpeg").
mkdir -p "$PREFIX/share/ffmpeg"
cp "$SRC/COPYING.LGPLv2.1" "$PREFIX/share/ffmpeg/copyright"
echo "FFmpeg installed in $PREFIX"
