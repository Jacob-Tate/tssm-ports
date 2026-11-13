#!/bin/sh
# Builds FFmpeg for Nintendo 3DS homebrew: static libavformat, libavcodec,
# libswscale, libswresample and libavutil for devkitARM (armv6k, the New 3DS's
# ARM11 MPCore), with the same decoder-only, LGPL configuration as the Switch
# (tools/switch/build_ffmpeg.sh) and Android builds.
#
#   build_ffmpeg.sh <ffmpeg source> <work dir> <install prefix>
#
#   ffmpeg source   an extracted FFmpeg release (tested: n9.0.2)
#   work dir        scratch build directory, created if missing
#   install prefix  where include/ and lib/ go
#
# Run it from the devkitPro MSYS2 shell (DEVKITPRO in MSYS2 form, e.g.
# /opt/devkitpro). None of the three paths may contain spaces: FFmpeg's
# configure does not support them.
#
# What is built in: the Switch's list (Bink video and audio for the game's
# movies; Vorbis, Opus, FLAC, MP3 and PCM for mods' sounds). LGPL only.
#
# Differences from the Switch:
#   --arch=arm --cpu=armv6k   the ARM11; no NEON (armv6 has none). FFmpeg's
#                             armv6 media-instruction code stays on.
#   the compile flags are 3DS.cmake's (Nintendo3DS.cmake): -march=armv6k
#   -mtune=mpcore -mfloat-abi=hard -mtp=soft, and -D__3DS__.
#   --disable-pthreads        as on the Switch: no decoder here uses threads.

set -e

if [ $# -ne 3 ]; then
    echo "usage: $0 <ffmpeg source> <work dir> <install prefix>" >&2
    exit 2
fi

SRC=$1
WORK=$2
PREFIX=$3

DEVKITPRO=${DEVKITPRO:-/opt/devkitpro}
if [ ! -x "$DEVKITPRO/devkitARM/bin/arm-none-eabi-gcc" ] &&
   [ ! -x "$DEVKITPRO/devkitARM/bin/arm-none-eabi-gcc.exe" ]; then
    echo "devkitARM not found under DEVKITPRO=$DEVKITPRO" >&2
    exit 2
fi
export PATH="$DEVKITPRO/devkitARM/bin:$PATH"

mkdir -p "$WORK"
cd "$WORK"

# A compiler for the build machine (configure's checks want one); as on the
# Switch, the LLVM clang in Program Files when MSYS2 has none.
if [ -z "$HOST_CC" ]; then
    if command -v cc >/dev/null 2>&1; then
        HOST_CC=cc
    elif command -v gcc >/dev/null 2>&1; then
        HOST_CC=gcc
    elif [ -x "/c/Program Files/LLVM/bin/clang.exe" ]; then
        printf '#!/bin/sh\nexec "/c/Program Files/LLVM/bin/clang.exe" "$@"\n' > "$WORK/host-cc"
        chmod +x "$WORK/host-cc"
        HOST_CC="$WORK/host-cc"
    else
        echo "no host C compiler: set HOST_CC" >&2
        exit 2
    fi
fi

CTR_ARCH="-march=armv6k -mtune=mpcore -mfloat-abi=hard -mtp=soft"
# devkitARM's uint32_t is unsigned long, so a uint32_t* handed to a function
# taking unsigned int* (the same 32 bits) is an "incompatible pointer", which
# GCC 14 and later make an error. A warning, as on every other target.
CTR_WARN="-Wno-error=incompatible-pointer-types -Wno-error=int-conversion"
CTR_CFLAGS="$CTR_ARCH -ffunction-sections -fdata-sections -D__3DS__ -O2 $CTR_WARN -I$DEVKITPRO/libctru/include"
CTR_LDFLAGS="-specs=3dsx.specs -L$DEVKITPRO/libctru/lib $CTR_ARCH"

"$SRC/configure" \
    --prefix="$PREFIX" \
    --host-cc="$HOST_CC" \
    --enable-cross-compile --target-os=none --arch=arm --cpu=armv6k \
    --cross-prefix=arm-none-eabi- \
    --extra-cflags="$CTR_CFLAGS" \
    --extra-ldflags="$CTR_LDFLAGS" \
    --extra-libs="-lctru -lm" \
    --enable-static --disable-shared \
    --disable-autodetect --disable-programs --disable-doc --disable-debug \
    --disable-avdevice --disable-avfilter --disable-network \
    --disable-pthreads --disable-runtime-cpudetect \
    --disable-neon --disable-vfpv3 \
    --disable-everything \
    --enable-avcodec --enable-avformat --enable-avutil --enable-swscale --enable-swresample \
    --enable-decoder=bink,binkaudio_rdft,binkaudio_dct \
    --enable-decoder=vorbis,opus,flac,mp3,mp3float \
    --enable-decoder=pcm_s16le,pcm_s24le,pcm_s32le,pcm_f32le,pcm_u8 \
    --enable-demuxer=bink,ogg,flac,mp3,wav \
    --enable-parser=vorbis,opus,flac,mpegaudio \
    --enable-protocol=file

JOBS=${JOBS:-$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)}
make -j"$JOBS"
make install

mkdir -p "$PREFIX/share/ffmpeg"
cp "$SRC/COPYING.LGPLv2.1" "$PREFIX/share/ffmpeg/copyright"
