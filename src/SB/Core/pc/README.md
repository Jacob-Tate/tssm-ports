# The SpongeBob SquarePants Movie: PC port

The decompiled game code, compiled with a host toolchain against the PC
platform layer in this directory, rendering through librw and reading the
**Xbox** or the **GameCube** release's files. It is based on treedome, the BfBB
PC port (CC0).
`PORTING.md` beside this file says where every file came from and what is
stubbed, and `stubs/STUBS.md` lists every stub. Status: x64 and x86 both
boot through the logo movies, the menus, the first save and story1.bik into the
first level's opening cutscene and gameplay, with sound; stubs remain for some
SDK calls shared code still makes (`stubs/STUBS.md`). On a 64-bit host the
assets that keep pointers in 4-byte slots are widened as they load
(`iAssetWiden.h`). `tools/regress.py` runs both widths to gameplay and compares
their frames with stored references (below).

There are two ways to play: download the ready-made *TSSM Dehydrated* zip from
the repository's Releases page (this port under that neutral name, with the
port's own simple icon), or build it yourself from this source as below. Either
way you bring your own copy of the game's files, Xbox or GameCube ("The game's
files", below). A build of your own is called *The SpongeBob SquarePants Movie
Video Game*. Both carry the port's sponge-block icon (`dist/pc`) on the
program, its windows and the taskbar; a build of your own can carry one made
from a picture of your choice instead (`docs/ICON.md`).

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
| `TSSM_FFMPEG_ROOT` | none | an FFmpeg install to link instead of searching; on Android a folder of `<abi>/` installs |
| `TSSM_D3D11_DEBUG_LAYER` | OFF | Debug builds: librw asks Direct3D 11 for its debug layer. Needs Windows' optional Graphics Tools feature; without it the D3D11 device does not open, which is why it is off |
| `TSSM_LIBRW_PATCHES` | ON | apply the port's librw fixes (`tools/librw/*.patch`) to `third_party/librw`; needs git. `docs/RENDERING.md` |
| `TSSM_PC_TITLE` | `The SpongeBob SquarePants Movie Video Game` | the program's name: the launcher's and the game's window titles, the launcher's heading, the settings window's title and About tab, message boxes, the first line of `config.ini`, and the name in `tssm.exe`'s version information (Explorer's Properties, Task Manager). No double quotes, backslashes or `%` |
| `TSSM_PC_VERSION` | `0.9.5` | the version in `tssm.exe`'s version information: up to four numbers |
| `TSSM_VERSION_LINE` | empty | the About tab's version line; empty: the branch and `git describe` |
| `TSSM_ICON_DIR` | empty | a folder made by `tools/make_icons.ps1`: the icon of `tssm.exe` and of its windows (`docs/ICON.md`); empty: the port's sponge block, `dist/pc` |

CMake caches these, so a build directory keeps what it was configured with until
it is configured again with `-D...`.

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

## Building the TSSM Dehydrated release

The ready-made Windows zips on the repository's Releases page are "TSSM
Dehydrated": this port under a neutral name, with the port's own sponge-block
icon and, like every build, none of the game's files (players bring their
own, as above). They are ordinary Release builds of this branch with clang++ on
the MSVC ABI (as `build-release.bat`), set up like this:

- `TSSM_PC_TITLE=TSSM Dehydrated`, `TSSM_PC_VERSION=0.9.5` and
  `TSSM_VERSION_LINE=0.9.5`: the name and version in every place the options
  table above lists;
- the default icon: `dist/pc/default_icon.ico` and `default_icon-64.png`,
  `tools/make_icons.ps1`'s output for the drawn sponge block
  (`dist/switch/default_icon.jpg` on the `switch` branch, the Switch's and the
  Android app's default icon), byte for byte what 0.9.2 and 0.9.3 were given
  through `TSSM_ICON_DIR`; no `TSSM_ICON_DIR` is needed any more;
- FFmpeg n9.0.2 from `tools/build_ffmpeg_msvc.sh`: five DLLs with the decoders
  the game's movies and mods' sounds need, LGPL 2.1 or later;
- the static C and C++ runtime (`CMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded`;
  FFmpeg's DLLs have it too), so nothing needs the Visual C++ runtime
  installed;
- only the `tssm` target built (`--target tssm`, and `TSSM_BUILD_TESTS=OFF`):
  the output folder holds `tssm.exe` and the FFmpeg DLLs, no test programs.

Build in a folder whose path has no spaces and nothing private in it: CMake
maps the checkout out of `__FILE__` (`-fmacro-prefix-map`),
`build_ffmpeg_msvc.sh` keeps its folders out of the DLLs, and a Release build
has no debug information. In a command prompt with the 64-bit Visual Studio
environment (`vcvarsall.bat x64`; for the 32-bit zip `x86`, the `-x86`
folders and `-DTSSM_BUILD_32BIT=ON`), MSYS2 with `make` and `diffutils`, and
FFmpeg n9.0.2's source
(https://github.com/FFmpeg/FFmpeg/archive/refs/tags/n9.0.2.tar.gz, sha256
`6e374ed621e48faa40639307dff48ba6fe574a509977956d2cce9669b7cc27e9`)
extracted into the work folder:

    set R=C:\tssm-rel\pc
    rem %R%\src: this branch with its submodules (Building on Windows)
    set MSYS2_PATH_TYPE=inherit
    C:\msys64\usr\bin\bash -l -c "sh /c/tssm-rel/pc/src/tools/build_ffmpeg_msvc.sh /c/tssm-rel/pc/FFmpeg-n9.0.2 /c/tssm-rel/pc/ff-x64 /c/tssm-rel/pc/ffmpeg-x64"
    cmake -S %R%\src -B %R%\build-x64 -G Ninja -DCMAKE_BUILD_TYPE=Release ^
          -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ ^
          -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded -DTSSM_BUILD_TESTS=OFF ^
          -DCMAKE_PREFIX_PATH=%R%/ffmpeg-x64 ^
          "-DTSSM_PC_TITLE=TSSM Dehydrated" -DTSSM_PC_VERSION=0.9.5 -DTSSM_VERSION_LINE=0.9.5 ^
          -DCMAKE_RUNTIME_OUTPUT_DIRECTORY=%R%/bin-x64
    cmake --build %R%\build-x64 --target tssm

`%R%\bin-x64` then holds `tssm.exe` and the DLLs of `avcodec`, `avformat`,
`avutil`, `swresample` and `swscale`: the zip is those six files with the
release's `README.txt`, `LEGAL-NOTICE.md` and `THIRD-PARTY-NOTICES.txt`.
Before sharing, check the name and version (Explorer: Properties, Details),
that no file imports a Visual C++ runtime DLL
(`llvm-objdump -p tssm.exe | findstr "DLL Name"`, and the same for each DLL),
and that no file names the work folder:

    findstr /m /i /c:"tssm-rel" %R%\bin-x64\*

prints nothing (look for your user name the same way). FFmpeg's LGPL asks
whoever hands out these DLLs to offer the FFmpeg source they were built from
(the release carries it) and `tools/build_ffmpeg_msvc.sh`, which says how it
was configured.

## Building for Android

`android/` is a Gradle project that builds the same CMakeLists.txt into
`libmain.so` and packages one APK per ABI (armeabi-v7a, arm64-v8a), with a
folder-import screen for the game's files and touch controls. FFmpeg for the
movies is built from source by `tools/android/build_ffmpeg.sh`.

    cd android
    gradle assembleDebug -Ptssm.ffmpeg=<folder of FFmpeg builds>

`docs/ANDROID.md` has the details, the first-launch flow and the known issues.

## The game's files

Not part of the port, never committed, and never to be. Extract your own Xbox
or GameCube disc and point `config.ini`'s `[assets] path` (written beside the
executable on the first run) or the `TSSM_ASSETS` environment variable at the
folder that holds them:

- **Xbox:** the folder that directly contains `boot.HIP`, `font.HIP` and
  `FMV\`, extracted from your Xbox disc image (XISO) with an XISO extractor.
- **GameCube:** Dolphin's *Extract Entire Disc* (right-click the game in
  Dolphin's list) writes `files\` and `sys\`; choose `files\`, or the folder
  holding both, which the port reads from its `files\`. Only the USA disc
  (GGVE78) has been tested. A disc image (`.iso`, `.rvz`) is not read
  directly.

The port checks for both files, and for the platform id in `boot.HIP` (`BX`
Xbox, `GC` GameCube; a PS2 set is refused), before it opens a window, and logs
which release it found (`tssm: game files: the GameCube release (US), disc
GGVE78`). A GameCube package is converted to the Xbox layout as it is opened
(`iMod.h`, `iGCSwap.h`), its textures and models are read in the GameCube's own
formats (`docs/RENDERING.md`), and its sounds are the GameCube's DSP-ADPCM
(`iSndGC.h`). Saves are the same on both: the game code that writes them is
the GameCube's either way (a new game saved with each wrote the same bytes).

What differs with GameCube files is what differs on the console: the
GameCube's wording ("Press" and "R Button" where the Xbox says "Pull" and "the
Right Trigger", the memory-card messages), its button pictures (the GameCube pad's, `iPadPrompts.h`), its
movies (512 wide, shown as the console shows them) and its cutscene audio.
The GameCube files also have none of the Xbox water's data, so every level has
the GameCube water with them (`docs/RENDERING.md`, "Water"), and the Xbox extras
get from them what they hold (below).

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
    tssm --import-save <Xbox save folder>        an Xbox save, whatever its signature
    tssm --export-save A1 <folder> --xbox emu    a slot as an Xbox save for Cxbx-Reloaded
                                                 and other copies of this port
    tssm --export-save A1 <folder> --xbox real --keys keys.bin
                                                 the same for a real Xbox or xemu
    tssm --import-save save.psu                  a PlayStation 2 save (also .max, .psv, a
                                                 memory card file, PCSX2's folder card)
    tssm --export-save A1 save.psu --ps2 psu     a slot as a .psu for uLaunchELF or mymc
    tssm --export-save A1 Mcd001.ps2 --ps2 card  into a PCSX2 memory card file (copied to
                                                 Mcd001.ps2.bak first)
    tssm --export-save A1 <folder> --psv standin a slot as a PlayStation 3 .psv, for other
                                                 copies of this port and PC save tools
    tssm --export-save A1 <folder> --psv real --psv-keys ps3keys.bin
                                                 the same with the PS3's own signature
    tssm --saves-help                            the rest (--slot, --force, backups)

Every save that is replaced is kept as a backup (`--restore-save`).

Xbox saves are the same save in the other byte order, in a folder, signed
(`docs/SAVES.md` sections 6 and 7). Importing one needs only the game's own
files (to know what each level saves). Exporting one needs `DEFAULT.XBE` from
your own copy of the Xbox game, which is beside the game's files when you play
from the Xbox ones; a save for a real Xbox or xemu also needs your `keys.bin`,
made from your own Xbox (the file Cxbx-Reloaded reads). The port has no key of
its own and never shows one. The Saves tab does all of it with file pickers;
the two paths are `[game] xbox_xbe` and `xbox_keys` in config.ini.

PlayStation 2 saves are that same little-endian save, bare, in the game's
folder on a memory card (`docs/SAVES.md` section 8). The port reads it from a
`.psu`, `.max` or `.psv` file, a memory card file or PCSX2's folder-type card,
and writes a `.psu`, or into a card file (after copying it) or a folder card.
An exported save carries the game's own icon if you point at `PLAT.HIP` from
your own PlayStation 2 disc or at a save the game made (`[game] ps2_icon`);
otherwise an icon the port draws itself.

A PlayStation 3 moves PlayStation 2 saves in and out of its virtual memory
cards as a signed `.psv` (`docs/SAVES.md` section 9). The port reads one
whatever its signature and says which it carries. It writes one with either
of two signatures: the stand-in, which other copies of the port and PC save
tools take but which a PS3 needs re-signed first (by a save tool on a modded
PS3, for example); or the PS3's own, made from a 32-byte key file you provide
(`--psv-keys`, `[game] psv_keys`), following the public description of how a
PS3 signs, not yet confirmed on a PS3. The port has no key of its own and
never shows one. The PS3's virtual card itself (`.VM2`) is read and written as
a memory card file.

## config.ini and switches

The settings table is `iConfigTable.cpp` (`[assets] path, mod, mods,
mods_folder`, `[game] boot,
intro_movies, save_folder, xbox_xbe, xbox_keys, ps2_icon, psv_keys, save_text` (the save screens' words, `iSaveText.h`,
`docs/GUI.md`), `[video] mode, aspect, width, height, ui and ui_margin (the HUD in the 4:3 area or at the screen's
edges, and always at the edges of a screen taller than 4:3, `docs/WIDESCREEN.md`), framerate,
vsync, msaa, fov, backend, pipeline, hdr_glow, screen_warp`, `[extras] glow, spotlights, crowds,
water, frame_lock, guitar_distortion` (the Xbox release's features, `docs/EXTRAS.md`), `[cheats] no_damage, fly,
unlock_extras` (the port's cheats, each off by default; `docs/CHEATS.md`), `[input] controller, preset, layout,
button_icons, keyboard, stick_curve, deadzone, trigger_threshold,
camera_sensitivity, camera_invert_x, camera_invert_y, mouse_look,
mouse_sensitivity_x, mouse_sensitivity_y, mouse_invert_x, mouse_invert_y, mouse_raw,
mouse_smoothing, mouse_release_key, mouse_menus, rumble, touch_controls, touch_drag_look,
touch_scale, touch_opacity, test`, `[audio] master_volume,
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
folder when that is set (`iLog.h`); the log's first line and each crash report
say the version, git commit and time of the build (`iBuildInfo.h`, rewritten on
every build); `TSSM_INPUT_TEST=1` (or `=overlay`) logs
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

- **A frame that comes on time runs on a steady frame time** (`docs/PACING.md`).
  On the consoles every frame is one NTSC retrace (1001/60000 s); the port's
  measured frames wobble by microseconds, and at exactly 60 or 120 frames a
  second that made about one enemy hurt or death in five play its animation
  once more (a Popper or Merv dying 0.6 to 1 s late, a Popper that stayed up and
  could be hit again). Each on-time frame now gets the same time, a hair longer
  than their average, and those animations end on the same frame as in Dolphin.
  What that gives the game too much or too little (a scene running a little over
  budget, the short frame after a late one) is paid back once it reaches three
  eighths of a frame, and a scene over budget gets its own steady time, so the
  game's clock stays on the wall clock and cutscenes on their sound under any
  load: 6 ms a minute ahead, and otherwise within 3 ms at 120 and 6 ms at 60.
  The frame rate and the pictures are unchanged. A frame more than a quarter of
  a frame off, and every frame with no cap, keeps its measured time.

- **The mouse turns the camera directly** (mouse look, `docs/CONTROLS.md`). While the mouse
  is what turns it, the on-foot camera stays where it is pointed: it does not ease back and
  does not swing behind the player, as the original's does after the stick is let go. A
  controller, or the camera keys, get the original camera exactly.
- **The SpongeBall keeps its full speed on a slow device.** On a device that
  cannot keep about 45 frames a second (or with the frame rate cap below that),
  the SpongeBall now rolls as fast as at 60. The original slows the ball at
  those rates while the falling blocks of the SpongeBall levels (BL01-BL04)
  keep real time, so the player falls through them; the GameCube runs these
  levels at 60, where this never happens. At 60 fps and faster nothing
  changes.
- **The Patty Wagon drives above 60 fps as at 60.** The car's code adds its
  push, gravity and grip once a step with no time factor, and neither console
  runs it above 60; at 120 fps the original was 1.6 times as quick off the
  line and fell twice as fast. Above 60 a step shorter than 1/60 s now gets
  its share of those; at 60 and below nothing changes (`docs/EXTRAS.md`).

## The Xbox release's extras

The Xbox release has a few things the GameCube release cut, and the port can
show either release's version of each: the settings window's (F1) **Xbox
extras** rows switch each one, and **All Xbox** / **All GameCube** set them all
at once (`[extras]` in config.ini; `docs/EXTRAS.md`). Every one is `xbox` by default (on the
3DS, `gamecube`). Each level decides from its own files. With GameCube files,
No Cheese!'s and Turn the Tables on Plankton's spotlights and No Cheese!'s crowd
are made to work as on the Xbox from what those files hold; the water, Sandwich
Driving 101's crowd and Bubble Blowing Baby Hunt's disco lights are not in them,
so those keep the GameCube version and the row says why. The glow and the 30 fps
levels are code only and follow the setting with either
(`docs/EXTRAS.md`, "With GameCube files").

- `glow`: the Xbox's wide, soft glow, stronger in some levels, or the
  GameCube's tighter, fainter one.
- `spotlights`: the moving spotlights of No Cheese!, Bubble Blowing Baby Hunt
  (its disco lights) and Turn the Tables on Plankton on, and every spotlight
  drawn as the Xbox draws it, lighting what it shines on and with a lamp
  flare; or those lights off and spotlights as cones only.
- `crowds`: the fish crowds of No Cheese! and Sandwich Driving 101 move, or
  stand still.
- `water`: the Xbox release's own water in the two levels whose Xbox files
  have it, SC02's tanks and B301's sea: refraction, reflections and moving
  ripples, from two extra pictures drawn each frame for each water area (four
  in SC02), sized with the render size (1536x1152 at 1920x1080); or the
  GameCube release's water. On Direct3D 9's fixed path or on Vulkan the
  GameCube water is used, and the log says why (`docs/RENDERING.md`, "Water").
  An older `config.ini`'s `[video] water` is read as this.
- `frame_lock`: the six levels the GameCube holds at 30 frames a second run at
  the frame-rate cap, or are held at 30 (the Patty Wagon drives as at 60
  either way).
- `guitar_distortion`: the screen swirls while the Sonic Wave Guitar's wave
  is steered, as on the Xbox, or does not, as on the GameCube. It needs the
  Xbox files (the swirl's map is in their `plat.HIP`) and a pixel shader: with
  GameCube files, on Direct3D 9's fixed path or on Vulkan no swirl is drawn,
  and the overlay's row says why.

## Cheats

The overlay's and the launcher's Cheats tab has the original game's cheats (its nine button codes
and the Extras screen's costumes and functions) as switches that run the game's own code, and
three of the port's own: `[cheats] no_damage`, `fly` and `unlock_extras`, off by default and never
in a save. `docs/CHEATS.md` lists every code and what it does; `tools/cheat_runs.py` is the
scripted check.

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
`TSSM_TEST_CAMLOG=<n>` logs the camera every n frames (with `TSSM_TEST_LOOKLOG=1` also its
angles and whether the mouse steers it), `TSSM_TEST_MOUSE=<frame:move:dx:dy[/frames],frame:at:x:y,frame:click,frame:esc,...>`
plays a mouse of the test's own (mouse look, and the pointer in the game's menus), `TSSM_TEST_KEYS=<frame:key[+key][/frames],...>`
a keyboard of the test's own through the [keyboard] bindings (e.g. `300:space/4,700:tab`; leave `TSSM_INPUT` unset), `TSSM_TEST_UILOG=<n>`
logs the menu widgets a pointer could mean, `TSSM_TEST_HUDLOG=<n>` the HUD widgets drawn and
where they land (`iHudAnchor.h`, the HUD at the screen's edges), `TSSM_TEST_SPOTLOG=<n>` the
scene's spotlights and `TSSM_TEST_CROWDLOG=<n>` its crowd (`iTestEvents.h`), `TSSM_TEST_CHEAT=<frame:what,...>` asks for a cheat as the Cheats tab does and `TSSM_TEST_CHEATLOG=<n>` logs the cheats' state (`docs/CHEATS.md`), `TSSM_TEST_DISTORT=<amount>[,<ms>]` draws the Sonic Wave Guitar's distortion at that amount in every frame (the clock held at that time when given; `=log` logs the game's own amount; `iXboxDistort.h`), and `TSSM_HDR_VIEW=mask|glow|base` shows the glow pass's input
or output, or (the Xbox glow) the frame without it (`docs/RENDERING.md`, which covers the console's screen effects on
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
A level load is one frame to all of these, so the loading screen has its own:
`TSSM_TEST_LOADLOG=1` logs each load's time, its loading frames and the
longest wait between two of them (2: every stretch that gave the screen no
chance of a frame), `TSSM_CAPTURE_LOAD=<n>` saves each load's first n loading
frames (`load<load>_<frame>.png`), `TSSM_TEST_LOADSLOW=<ms>` makes every
package step wait, and `TSSM_TEST_LOADEVENT=<frame>:mode|close` switches the
window mode or closes the window at a loading frame (`iTestEvents.h`).

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
