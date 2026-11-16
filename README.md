# The SpongeBob SquarePants Movie: PC port

A native Windows version of *The SpongeBob SquarePants Movie* game, built from the
matching decompilation of the GameCube game ([tssm-decomp-fork](https://github.com/aidp2/tssm-decomp-fork), whose history
this repository starts from). The
game's own decompiled code runs unchanged in behaviour; a new platform layer
(`src/SB/Core/pc`) replaces the GameCube one, draws through librw (Direct3D 9, Direct3D
11 or OpenGL 3), and uses SDL 3 for the window, controllers and sound.

- Windows, 64-bit (x64) and 32-bit (x86). The code also has Linux and macOS paths, but
  they are not tested.
- Widescreen, any resolution, high frame rates, MSAA, a settings window (F1, or Back +
  Start on a controller), controller and keyboard bindings, mods, and save import and
  export with Dolphin and the GameCube.

This repository and its branches
--------------------------------

This repository holds the **ports**. The decompilation they are built on is its own
repository, [tssm-decomp-fork](https://github.com/aidp2/tssm-decomp-fork): this repository's history starts with that
one's, so every port sits on exactly that code. Each port is a branch here; pick one from
GitHub's branch menu, or clone it with `git clone --branch <name>`.

| Branch | What it is for |
|---|---|
| `pc` (this one) | Windows (64-bit and 32-bit; Direct3D 9, Direct3D 11, OpenGL). The main port: every other branch is built on it. |
| `android` | Android phones and tablets: the PC port as an app, with touch controls and an import screen for the game files. |
| `switch` | Nintendo Switch homebrew (custom firmware): `tssm.nro`. |
| `3ds` | New 3DS homebrew (custom firmware): `tssm.3dsx` and `tssm.cia`. **Still in progress**: it runs, but has been played on a console only a little. |
| `coop` | Windows with two-player co-op: online, on the home network, or two copies on one PC. Built on `pc`. |
| `coop-android` | The co-op app for Android, a second app beside the normal one. Built on `android`. |
| `coop-switch` | The co-op Switch program, `tssm-coop.nro`. Built on `switch`. |

Every branch builds from source; you bring your own game files.

You need your own copy of the game
----------------------------------

This repository contains **no game data**. The port reads the files of the **Xbox**
release of *The SpongeBob SquarePants Movie*, so you need your own copy of that disc and
its files copied off it (about 1 GB): the folder that directly holds `boot.HIP`,
`font.HIP` and `FMV\`. Never put those files in the repository.

Building on Windows
-------------------

You need:

- Visual Studio 2022 or its Build Tools, with the "Desktop development with C++" workload
  and a Windows SDK
- LLVM (`clang++`), CMake 3.16 or newer, Ninja, Python 3 and git
- optional, for the game's movies: FFmpeg from vcpkg,
  `vcpkg install ffmpeg[core,avcodec,avformat,swresample,swscale]:x64-windows`
  (and `:x86-windows` for a 32-bit build). Without it the movies are skipped.

Then, in a normal command prompt:

    git clone --branch pc <this repository's address> tssm
    cd tssm
    git submodule update --init --depth 1
    build-release.bat

`build-release.bat` finds Visual Studio by itself, and LLVM and CMake in their default
install folders. It puts `tssm.exe` and the DLLs it needs in `bin\`.
`build-release.bat GL3 x86` builds one renderer, 32-bit; `build-debug.bat` makes a debug
build. `src/SB/Core/pc/README.md` lists the CMake options and the rest.

Building with MinGW-w64
-----------------------

The PC port also builds with MinGW-w64's GCC in MSYS2: 64-bit in the MINGW64 shell and
32-bit in the MINGW32 shell, with the same renderers, settings, saves and mods. The result
is one `tssm.exe` with SDL and FFmpeg inside it, which needs no other DLLs. The steps, from
installing MSYS2 to the finished build, are in `src/SB/Core/pc/README.md`, section
"Building with MinGW (MSYS2)": `tools/pcbuild-mingw.sh` builds the game, and
`tools/mingw/build_ffmpeg.sh` builds the small static FFmpeg it uses for the movies.

Playing
-------

1. Start `bin\tssm.exe`. On the first run it writes `config.ini` beside itself.
2. Point the game at your Xbox files: set `path` under `[assets]` in `config.ini` (or the
   `TSSM_ASSETS` environment variable) to the folder that holds `boot.HIP`.
3. F1, or Back + Start on a controller, opens the settings window.

Saves go to `%APPDATA%\tssm\saves`, and the log of the last run to
`%APPDATA%\tssm\last_run.log`.

More documentation:

| File | What |
|---|---|
| `docs/CONTROLS.md` | the controls, and how each was worked out |
| `docs/GUI.md` | the settings window and the launcher |
| `docs/SAVES.md` | save files, and moving them to and from Dolphin or a GameCube |
| `docs/MODDING.md` | mods: replacing textures, sounds, text and whole assets |
| `docs/RENDERING.md`, `docs/WIDESCREEN.md` | how the console's screen effects and widescreen are done |
| `docs/ICON.md` | using your own picture as the program's icon |
| `src/SB/Core/pc/README.md` | building in detail, settings, switches and test runs |
| `src/SB/Core/pc/PORTING.md` | where every file of the platform layer came from, and what is still stubbed |

The GameCube build still works here
-----------------------------------

The port's changes to shared game code are all behind `PLATFORM_PC`, which only the port's
CMake build defines, so this branch still builds the original GameCube program exactly
(the decompilation repository's steps: your GameCube disc image in `orig/GGVE78/`, then
`python configure.py` and `ninja`, which checks `main.dol` against SHA-1
`f772db7f38b9b55e27169822ed17184fa2dc2574`). Every change to shared code is checked that
way.

How this was made
-----------------

This port, like the decompilation under it, was made with an AI coding assistant (Claude,
made by Anthropic), directed by the repository's owner, and tested by playing it. The
upstream decompilation project keeps AI-assisted work out of its own repository and treats
it as a separate reference: this repository is that kind of separate fork, not part of the
upstream project.

Credits
-------

- **The bfbbdecomp team and contributors**, for the upstream TSSM decompilation project
  this is based on, and the *Battle for Bikini Bottom* decompilation.
- **treedome**, the PC port of the *Battle for Bikini Bottom* decompilation (CC0) by
  [@joeyballentine](https://github.com/joeyballentine), whose platform layer this port
  started from.
- **blu** ([@bluisblu](https://github.com/bluisblu)), who guided the decompilation and tested
  the ports.
- **seil** ([@seilc](https://github.com/seilc)), for the references used in the decompilation.
- The authors of librw, SDL, Dear ImGui, FFmpeg and the other libraries below.

*The SpongeBob SquarePants Movie* and its characters belong to their owners. This project
is not affiliated with or endorsed by them, and it contains none of their game data.

Licenses
--------

The port's own code, the decompiled game code and the rest of the work in this repository
are released under CC0 1.0 (public domain); see `LICENSE`. That does not cover the parts
that belong to others: the RenderWare SDK headers (`include/rwsdk`, and the port's
adapted copies in `src/SB/Core/pc/rwsdk`) and the GameCube SDK, runtime and middleware
code under `src/dolphin`, `src/PowerPC_EABI_Support`, `src/rwsdk` and `src/FMOD` keep
their owners' copyright notices where the files carry them, as in the upstream project;
`dwarf/` is debugging information from the PlayStation 2 release, kept for reference.

The libraries the port is built with keep their own licences. The game shows their
notices in its settings window (About tab).

If you share a build you made (rather than the source), the libraries' terms travel with
it. In particular FFmpeg is under the LGPL: on Windows it is linked as separate DLLs,
which keeps that simple; on Android, the Switch and the 3DS it is linked into the program,
so a shared build also has to offer what the LGPL asks for (for example the object files
needed to relink it with another FFmpeg).

| Library | How it gets here | Licence |
|---|---|---|
| librw (a patched fork; the port's own fixes are in `tools/librw/*.patch`) | git submodule `third_party/librw` | MIT |
| LodePNG (inside librw) | with librw | zlib |
| SDL 3 | git submodule `third_party/SDL` | zlib |
| Dear ImGui | git submodule `third_party/imgui` | MIT |
| volk, Vulkan Memory Allocator (Vulkan renderer only) | git submodules | MIT |
| Vulkan-Headers (Vulkan renderer only) | git submodule | Apache 2.0 / MIT |
| FFmpeg (optional, for the movies) | not in the repository: you install or build it | LGPL 2.1 or later |
| treedome (code adapted into `src/SB/Core/pc`) | copied, see `src/SB/Core/pc/PORTING.md` | CC0 1.0 |
| MinGW-w64 winpthreads (MinGW builds only) | the MinGW-w64 toolchain, linked statically into `tssm.exe` | MIT |
| MinGW-w64 runtime, the C runtime's startup and support code (MinGW builds only) | the MinGW-w64 toolchain, linked statically | ZPL 2.1 and others |
| libgcc, libstdc++ (MinGW builds only) | MinGW-w64 GCC, linked statically | GPL 3 with the GCC Runtime Library Exception |
| FFmpeg in a MinGW build (optional) | built by `tools/mingw/build_ffmpeg.sh` from FFmpeg's source and linked statically into `tssm.exe` | LGPL 2.1 or later |

A MinGW build is the exception on Windows: FFmpeg is linked into `tssm.exe`, so whoever
shares one also has to offer the FFmpeg source it was built from, with
`tools/mingw/build_ffmpeg.sh` (which is how it was configured). The About tab lists the
MinGW-w64 runtime's notices with the others. Never share a build made with MSYS2's own
FFmpeg package: it is GPL, not LGPL.
