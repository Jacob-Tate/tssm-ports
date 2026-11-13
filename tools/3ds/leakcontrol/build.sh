#!/bin/sh
# Builds probe-11-control.3dsx (main.c beside this script) into $1 (default: the
# current directory) with devkitARM and libctru; nothing of the port's.
# Needs DEVKITPRO and DEVKITARM (devkitPro's shell sets them).
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
OUT="${1:-.}"
mkdir -p "$OUT"
ARCH="-march=armv6k -mtune=mpcore -mfloat-abi=hard -mtp=soft"
"$DEVKITARM/bin/arm-none-eabi-gcc" $ARCH -O2 -g -Wall -Wextra -mword-relocations -D__3DS__ \
  -I"$DEVKITPRO/libctru/include" "$HERE/main.c" -specs=3dsx.specs \
  -L"$DEVKITPRO/libctru/lib" -lctru -lm -o "$OUT/probe-11-control.elf"
"$DEVKITPRO/tools/bin/smdhtool" --create "TSSM probe 11 control" \
  "Leak ladder control: measures and quits" "TSSM port" \
  "$DEVKITPRO/libctru/default_icon.png" "$OUT/probe-11-control.smdh"
"$DEVKITPRO/tools/bin/3dsxtool" "$OUT/probe-11-control.elf" "$OUT/probe-11-control.3dsx" \
  --smdh="$OUT/probe-11-control.smdh"
