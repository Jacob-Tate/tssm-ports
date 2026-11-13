# Switch toolchain smoke tests

Three tiny .nro programs that prove the Switch toolchain works end to end.
They are not part of the port's build.

| Folder | Proves | On the console |
|---|---|---|
| `hello` | devkitA64 + libnx + `Switch.cmake` + `nx_create_nro` | Text console; + exits |
| `sdltest` | SDL3 (switch-sdl-3.4) + Mesa GL through EGL | Yellow screen; + (Start) exits; logs the drivers |
| `fftest` | the decoder-only FFmpeg links with libnx | Prints the FFmpeg version and whether the Bink decoders exist |

Build each from the devkitPro MSYS2 shell (prefix = where `build_sdl.sh` and
`build_ffmpeg.sh` installed SDL3 and FFmpeg):

    cmake -S <folder> -B <folder>/build -G "Unix Makefiles" \
        -DCMAKE_TOOLCHAIN_FILE=/opt/devkitpro/cmake/Switch.cmake \
        -DCMAKE_PREFIX_PATH=<prefix> -DCMAKE_FIND_ROOT_PATH=<prefix>
    cmake --build <folder>/build

Run on a console with `nxlink -s <file>.nro` from hbmenu's netloader (Y), or
copy the .nro to `sd:/switch/`.
