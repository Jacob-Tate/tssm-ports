# The SpongeBob SquarePants Movie: PC port

The decompiled game code, compiled with a host toolchain against the PC
platform layer in this directory, rendering through librw and reading the
**Xbox** release's files. It is based on treedome, the BfBB PC port (CC0).
`PORTING.md` beside this file says where every file came from and what is
stubbed, and `stubs/STUBS.md` lists every stub. Status: x64 and x86 both
boot through the logo movies, the menus, the first save and story1.bik into the
first level's opening cutscene and gameplay, with sound; stubs remain for some
SDK calls shared code still makes (`stubs/STUBS.md`). On a 64-bit host the
assets that keep pointers in 4-byte slots are widened as they load
(`iAssetWiden.h`). `tools/regress.py` runs both widths to gameplay and compares
their frames with stored references (below).

## Building on Windows

Needs: Visual Studio 2022 (or its Build Tools) with the C++ workload and a
Windows SDK, LLVM (clang++), CMake 3.16+, Ninja, Python 3, and optionally
vcpkg's FFmpeg for the movies (`vcpkg install ffmpeg[core,avcodec,avformat,swresample,swscale]:x64-windows`).

    git submodule update --init --depth 1
    build-debug.bat                 64-bit, D3D9 + D3D11 + GL3, into build-debug\ and bin\
    build-release.bat               the same, optimised
    build-debug.bat GL3 x86         one backend, 32-bit

The scripts enter the MSVC environment themselves (vswhere), add LLVM and
CMake from their default install folders when they are not on PATH, and point
CMake at `%USERPROFILE%\vcpkg\installed\<arch>-windows` (or `TSSM_VCPKG`).

By hand:

    cmake -S . -B build-pc -G Ninja -DCMAKE_BUILD_TYPE=Debug ^
          -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ ^
          -DCMAKE_PREFIX_PATH=%USERPROFILE%/vcpkg/installed/x64-windows
    cmake --build build-pc -- -k 0
    python tools/pcprogress.py --link

| CMake option | Default | |
|---|---|---|
| `TSSM_BUILD_32BIT` | OFF | 32-bit build; needs a fresh build directory and the x86 MSVC environment (MinGW: not needed, the MINGW32 shell's gcc builds 32-bit) |
| `TSSM_RENDER_BACKENDS` | `D3D9;D3D11;GL3` (Windows), `GL3` elsewhere | also `VULKAN`, or `NULL` alone for headless |
| `TSSM_INPUT_BACKEND` | `sdl` | or `null` |
| `TSSM_AUDIO_BACKEND` | `sdl` | or `null` |
| `TSSM_SDL` | `auto` (MinGW: `vendored`) | `system` / `vendored` (`third_party/SDL`) |
| `TSSM_GUI` | ON | the Dear ImGui launcher and in-game overlay (`third_party/imgui`, `docs/GUI.md`) |
| `TSSM_D3D11_DEBUG_LAYER` | OFF | Debug builds: librw asks Direct3D 11 for its debug layer. Needs Windows' optional Graphics Tools feature; without it the D3D11 device does not open, which is why it is off |
| `TSSM_LIBRW_PATCHES` | ON | apply the port's librw fixes (`tools/librw/*.patch`) to `third_party/librw`; needs git. `docs/RENDERING.md` |

## Building with MinGW (MSYS2)

MinGW-w64's GCC builds the same game as the MSVC build, 64-bit and 32-bit:
D3D9, D3D11 and GL3 in one `tssm.exe`, the settings overlay, sound, controllers
and the movies. The result is one file that needs no DLL besides Windows' own
(the GCC runtime, SDL and FFmpeg are linked into it), so it runs outside MSYS2.

### 1. Install MSYS2 and the packages

Install MSYS2 from https://www.msys2.org into a folder without spaces (its
default, `C:\msys64`, is fine). It has several shells; use **MINGW64** for a
64-bit build and **MINGW32** for a 32-bit one (in the Start menu as "MSYS2
MINGW64" and "MSYS2 MINGW32", or `mingw64.exe` and `mingw32.exe` in the MSYS2
folder). The UCRT64 shell MSYS2 opens by default has not been tried, and the
build script refuses it.

In the shell, bring MSYS2 up to date first (it may ask to close the window
and run the same command again), then install the packages. The toolchain
group asks which members to install: press Enter for all of them.

    pacman -Syu

    MINGW64:  pacman -S --needed mingw-w64-x86_64-toolchain mingw-w64-x86_64-cmake mingw-w64-x86_64-ninja git make diffutils
    MINGW32:  pacman -S --needed mingw-w64-i686-toolchain mingw-w64-i686-cmake mingw-w64-i686-ninja git make diffutils

`git` is for the submodules and for the librw patches the build applies; Git
for Windows works as well (the script finds it in its default folder), and so
does a checkout made by either. `make` and `diffutils` are only for building
FFmpeg (below).

### 2. Get the source

Clone the repository's `pc` branch, or `cd` into a checkout you already
have; its folder may have spaces in it. In the MSYS2 shell, `C:\` is `/c/`.
Then fetch the submodules:

    git submodule update --init --depth 1

### 3. FFmpeg, for the movies

The game's movies are Bink files, which FFmpeg decodes. `tools/mingw/build_ffmpeg.sh`
builds just the part the port uses (the Bink decoders, and Vorbis, Opus, FLAC
and MP3 for mods' replacement sounds) as static LGPL libraries. Download an
FFmpeg release's source from https://ffmpeg.org/download.html (tested: 9.0.2)
and unpack it into your MSYS2 home folder (`~`, whose path has no spaces,
which FFmpeg's build needs). Then, once per shell (about 3 minutes):

    MINGW64:  tools/mingw/build_ffmpeg.sh ~/ffmpeg-9.0.2 ~/ffmpeg-work-64 ~/ffmpeg-64
    MINGW32:  tools/mingw/build_ffmpeg.sh ~/ffmpeg-9.0.2 ~/ffmpeg-work-32 ~/ffmpeg-32

The three arguments are the unpacked source, a scratch folder, and where the
result goes. Without FFmpeg the game still builds and runs, but skips the
movies, and mods' sounds can only be plain PCM WAV files.

MSYS2's own FFmpeg package (`pacman -S mingw-w64-x86_64-ffmpeg`, 64-bit only)
also works, and CMake finds it without `TSSM_FFMPEG`. It is a DLL build, so
the build copies the roughly 100 DLLs `tssm.exe` then needs into `bin\`, and it
is GPL-licensed (CMake warns about it): use it for your own builds only, not
for ones you hand out.

### 4. Build

    MINGW64:  TSSM_FFMPEG=~/ffmpeg-64 tools/pcbuild-mingw.sh           Release, in build-mingw64-release\
    MINGW32:  TSSM_FFMPEG=~/ffmpeg-32 tools/pcbuild-mingw.sh           Release, in build-mingw32-release\
              TSSM_FFMPEG=~/ffmpeg-64 tools/pcbuild-mingw.sh Debug     a Debug build
              TSSM_FFMPEG=~/ffmpeg-64 tools/pcbuild-mingw.sh Release GL3    only one backend

Leave out `TSSM_FFMPEG=...` to build without movies. Either way `tssm.exe`
(and the test programs) end up in `bin\`, as with the MSVC scripts, and
`bin\BUILD-INFO.txt` says which build is there: the last one built wins. A
Release build takes about a minute on a recent PC. Before the first run, set
up the game's files as below.

By hand, in the same shell, with any build folder name:

    cmake -S . -B build-mingw64 -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=$(cygpath -m ~/ffmpeg-64)
    cmake --build build-mingw64

`tssm.exe` is then in `build-mingw64\`. All the CMake options in the table
above work the same way.

### From Linux (cross-compiling)

`tools/mingw/toolchain-x86_64.cmake` and `toolchain-i686.cmake` build the
Windows executables with the distribution's MinGW-w64 GCC (Debian and Ubuntu:
`apt install mingw-w64 cmake ninja-build`; Fedora: `dnf install mingw64-gcc-c++
mingw32-gcc-c++ cmake ninja-build`):

    cmake -S . -B build-mingw64-cross -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE=tools/mingw/toolchain-x86_64.cmake
    cmake --build build-mingw64-cross

An FFmpeg built for Windows goes in with `-DCMAKE_FIND_ROOT_PATH=<its folder>`;
`build_ffmpeg.sh` itself runs only in the MSYS2 shells. These toolchain files
have only been tried in CMake's cross mode with MSYS2's compilers on Windows,
not yet on a Linux machine.

### How it differs from the MSVC build

- Compiler and C runtime: GCC with MinGW-w64's runtime (msvcrt), linked
  statically; the MSVC build is clang++ with Microsoft's libraries.
- FFmpeg: the small static one from `build_ffmpeg.sh` inside `tssm.exe`,
  instead of vcpkg's DLLs beside it. SDL: always the bundled
  `third_party/SDL`, inside `tssm.exe` (`TSSM_SDL` defaults to `vendored`).
- Compiler flags, each explained in CMakeLists.txt: C++ with
  `-fno-lifetime-dse` (the game clears objects before constructing them in
  place), SSE2 floating point for 32-bit, a 64-bit `time_t` for 32-bit (as the
  MSVC x86 build has), and `__FILE__` written relative to the checkout.
- Same game otherwise: the same renderers, settings, `config.ini`, saves (a
  save is byte-identical whichever build wrote it) and mods.
- `tools/regress.py` runs MinGW builds too: configure them by hand (so each
  executable stays in its build folder) and pass `--build-dir-x64` and
  `--build-dir-x86`; `--build` rebuilds them.

### Known limits

- Only the MINGW64 and MINGW32 shells are tried; UCRT64 and CLANG64 are not.
- The Vulkan renderer has not been tried with MinGW.
- A crash log from a MinGW build has addresses but no function names (the
  crash handler looks names up through Windows' dbghelp, which does not read
  GCC's symbols).
- A Release `tssm.exe` is about 16 MB with FFmpeg inside it, and keeps its
  symbol table; `strip bin/tssm.exe` drops that and leaves about 13 MB. (The
  MSVC one is about 8 MB, with about 18 MB of FFmpeg DLLs beside it.)
- FFmpeg is built without threads; the port decodes movies on the main thread,
  and the movies are small enough for that.
- The 64-bit and 32-bit builds share `bin\`, as the MSVC scripts' do.

### Handing out a MinGW build

The About tab of the settings overlay lists every library in `tssm.exe` with
its license, including the two MinGW-w64 runtime parts a static build carries
(winpthreads, MIT; the C runtime, ZPL 2.1). FFmpeg is LGPL 2.1: whoever hands
out a `tssm.exe` with it linked in must also offer the FFmpeg source it was
built from and `tools/mingw/build_ffmpeg.sh`, which is how it was configured;
the port's own source covers the rest. Never hand out a build made with
MSYS2's GPL FFmpeg package.

## The game's files

Not part of the port, never committed, and never to be. Extract your own Xbox
disc and point `config.ini`'s `[assets] path` (written beside the executable on
the first run) or the `TSSM_ASSETS` environment variable at the folder that
directly contains `boot.HIP`, `font.HIP` and `FMV\`. The port checks for both
files, and for the Xbox platform id (`BX`) in `boot.HIP`, before it opens a
window.

## Saves

A save file here is a GameCube save's payload, byte for byte, so saves move
between the two with the card's wrapper put on or taken off
(`docs/SAVES.md`). `tssm.exe` does it from the command line, without opening
a window:

    tssm --saves                                 the save slots
    tssm --import-save 78-GGVE-SpongeBobMovie01.gci   a Dolphin export (also .gcs, a
                                                 raw card image, a zip of saves)
    tssm --export-save A1 SpongeBob1.gci         a slot back to Dolphin or a console
    tssm --export-save all saves.zip             every save, for another install
    tssm --saves-help                            the rest (--slot, --force, backups)

Every save that is replaced is kept as a backup (`--restore-save`).

## config.ini and switches

The settings table is `iConfigTable.cpp` (`[assets] path, mod, mods,
mods_folder`, `[game] boot,
intro_movies, save_folder`, `[video] mode, width, height, ui, framerate,
vsync, msaa, fov, backend, pipeline, hdr_glow, screen_warp`, `[input] controller, preset, layout,
button_icons, keyboard, stick_curve, deadzone, trigger_threshold,
camera_sensitivity, camera_invert_x, camera_invert_y, mouse_camera,
mouse_sensitivity, rumble, touch_controls, test`, `[audio] master_volume,
music_volume, sfx_volume, movie_volume`, `[gui] launcher, key, pad_combo,
scale`, and the `[pad]`/`[keyboard]` bindings). The controls, and how each
button was established, are in `docs/CONTROLS.md`. In the game, F1 (or
Back+Start on a controller) opens the settings overlay, which edits the same
file; `docs/GUI.md` describes it and the launcher, and lists the `TSSM_GUI_*`
debug switches. Environment switches keep treedome's meanings with a
`TSSM_` prefix: `TSSM_ASSETS`, `TSSM_MOD`, `TSSM_MODS` (0: no
mods), `TSSM_MODS_DIR`, `TSSM_CONFIG`, `TSSM_SAVE_DIR`,
`TSSM_TEX`, `TSSM_FPS`, `TSSM_WATCHDOG=<s>`, `TSSM_GL_NO_S3TC=1` and `TSSM_GL_DEBUG=1`
(OpenGL, `docs/RENDERING.md`), `TSSM_TEST_CRASH` (at startup, or
`=<frame>` at that frame), `TSSM_AUDIO=0` (no device; sounds still keep time);
`TSSM_LOG=<file>` (or `off`) moves the game's own log, which is otherwise
`%APPDATA%\tssm\last_run.log` (the run before: `last_run.prev.log`; crash reports
are added to `crash.log` beside it), or `last_run.log` beside `TSSM_SAVE_DIR`'s
folder when that is set (`iLog.h`); `TSSM_INPUT_TEST=1` (or `=overlay`) logs
what the game reads from the controller every time it changes.
`tssm_input_test` (built beside `tssm.exe`) checks the input layer with
virtual controllers and no game data.

Sound diagnostics (`iSnd.cpp`): `TSSM_SND=<n>` logs the sound tables loaded and
the first n plays; `TSSM_SNDMIX` logs the live voices once a second;
`TSSM_SND_DUMP=<folder>` writes every sound decoded, stream played and
cutscene track fed there as WAV; `TSSM_SND_TEST=<s>` plays a sound and a stream
of each table of the first scene through the game's sound manager and reports
their lengths; `TSSM_SNDCACHE=<MB>` sizes the decoded-sound cache (256);
`TSSM_SND_ROLLOFF=1` adds distance attenuation to 3D sounds.

## Gameplay differences from the original

The port runs the GameCube game's own code, and a behaviour of the original is
kept even where it looks like a bug. Changes made on purpose are listed here and
in `PORTING.md` (known divergences).

- **The SpongeBall keeps its full speed on a slow device.** On a device that
  cannot keep about 45 frames a second (or with the frame rate cap below that),
  the SpongeBall now rolls as fast as at 60. The original slows the ball at
  those rates while the falling blocks of the SpongeBall levels (BL01-BL04)
  keep real time, so the player falls through them; the GameCube runs these
  levels at 60, where this never happens. At 60 fps and faster nothing
  changes.

## Mods

A `mods/` folder beside the executable (or `[assets] mods_folder`) holds one
folder per mod: loose files, single assets of any package by ID or name, textures
by name (PNG/DDS), sounds (WAV, or anything FFmpeg reads), movies and INI
overlays, with a load order in `mods/mods.ini`. `docs/MODDING.md` has the
details, `tools/modkit.py` lists and extracts what a package holds, and
`tssm_modtest` checks the system against your assets.

## Unattended runs

For unattended runs (`iCapture.h`): `TSSM_CAPTURE_FRAMES=<n,a-b,a-b/step>`
writes those presented frames as `frame_NNNNN.png` into `TSSM_CAPTURE_DIR`, and
`TSSM_EXIT_AFTER=<n>` exits with code 0 after n frames, and
`TSSM_INPUT=<frame:button[+button][/frames],...>` plays port 0 from a script
(e.g. `400:start,1000:a`; real controllers are ignored while it is set);
`TSSM_TEST_WARP=<frame:type,...>` emits screen warps at the player,
`TSSM_TEST_PLACE=<frame:x:y:z[:yaw],...>` puts the player down at a spot and
`TSSM_TEST_CAMLOG=<n>` logs the camera every n frames (`iTestEvents.h`), and `TSSM_HDR_VIEW=mask|glow` shows the glow pass's input
or output (`docs/RENDERING.md`, which covers the console's screen effects on
PC). Keep captures of game frames out of the repository.
`TSSM_TEST_SEND=<frame:name[:event],...>` sends a scene object its event
(`1200:AUTOSAVE_DONE_SCRIPT` ends SC02, DE02 or GG02 with its story movie;
`iTestSend.h`). `TSSM_TEST_AUDIO_RESTART=<moment[+ms][*n[/gap]][@how],...>`
sets off what leads to an audio restart, at a frame, a movie's start, end
of sound or close, or a load: a removed playback device, as SDL's event
alone or through the calls SDL makes when Windows reports a device gone or
a new default (or one SDL cannot open, `swapfail`); SDL dropping the game's own devices (`drop`); a device that
stopped taking audio (`stall`); or Android's put-away (`bg`)
(`iAudioSDLTest.h`). It logs every audio device event and, about once a
second, the mixer's callbacks and loudest sample and what the movie's
device has taken.
`TSSM_TEST_HANG_DUMP=<s>` writes every thread's stack to the log and ends the
run (exit code 3) when the game thread stops for that long, and logs SDL
assertions with the stacks instead of SDL's message box (`iHangDump.h`).

## Regression run

    python tools/regress.py --assets <Xbox files> --reference <dir> --out <dir> [--build]

runs `build-pc-x86/tssm.exe` and `build-pc/tssm.exe` with one scripted
controller from the legal screen through the menus, the first save, story1.bik
and BB02's opening cutscene into gameplay (about a minute each), captures twelve
fixed frames, and compares each with the reference frame of its width by block
averages (16x16 pixels) against a tolerance per kind of frame; the x64 frames are
also compared with the x86 ones. It checks that each run exited by itself at the
last frame and, on x64, that the asset widening ran with nothing left over, then
runs every unit test of both widths (`save_selftest`, `tssm_input_test`,
`tssm_modtest`, `tssm_sndtest` on boot.HIP and mnuc.HIP, `tssm_widentest`) and
prints a pass/fail table; `report.html` in the run folder shows each frame
beside its reference. Two short level runs per width (about 25 seconds for
both) cover what that path never shows. Each boots its level directly through
a mod of `sb04.ini` with no button pressed. `de01_fly` captures DE01's
level-start fly-through, and checks from the game's camera log
(`TSSM_TEST_CAMLOG`) that the camera stays finite, inside the level and
smooth while it plays. `pt03_belt` holds the player in front of a PT03
conveyor belt, and checks that the belt has moved between two frames 12
apart. Both checks work without references, and the runs' frames are also
compared with references in `<width>/<run>/`. `--quick` leaves the level runs
out; `--runs` picks runs by name. `--build` builds the existing build directories first
(never configures them); `--update-reference` stores the run's frames as the
references instead of comparing, after they have been looked at, for the runs
made only (`--runs de01_fly,pt03_belt` leaves BB02's as they are). A picture
that comes out one of two ways from run to run (in BB02 a lighter patch on the
ground in front of SpongeBob is there in some runs and not in others, at either
width) is stored as a variant of the reference: `--add-variants` keeps the
run's unmatched frames as variants, and `--from-run <run folder>` scores (or
takes variants from) a run already made, without running the game again. A
frame may match its own width's reference or variants or the other width's.
`--widths x64`, `--skip-runs` and `--skip-tests` narrow it. Every run is pinned to 4:3
at 640x480 (`video.aspect = 4:3`), which is what the default references are.
`--size 1280x720` is the widescreen pass instead: all three runs render at
that size, the run to gameplay also captures the pause menu, the belt check
looks where the belt is in the wider picture, and the frames are compared
with references of their own (`--reference` pointing at a second folder). The script's
docstring lists every option. References and captures are game frames: keep
both outside the repository. A run waits while another `tssm.exe` is running,
because a second Direct3D device has been seen to fail to open then, and tries
again if its device did not open.

On 64-bit hosts RenderWare allocates from a heap below 4 GB (`iLowHeap.h`;
`TSSM_RWHEAP_MB=<n>` sizes it, 256 by default; `TSSM_RWHEAP=1` uses it in a
32-bit build too), and the widened asset copies come from the same heap
(`TSSM_WIDEN=log` lists them, `TSSM_WIDEN=0` turns the widening off).
