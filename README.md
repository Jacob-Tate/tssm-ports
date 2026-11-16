# The SpongeBob SquarePants Movie: Nintendo Switch port

*The SpongeBob SquarePants Movie* game as a Nintendo Switch homebrew program
(`tssm.nro`), built from the matching decompilation of the GameCube game
([tssm-decomp-fork](https://github.com/aidp2/tssm-decomp-fork)) and the PC port (the `pc` branch). It is the PC port's code
built with devkitPro for the Switch: the same game code and platform layer, drawing
through OpenGL on the console's Mesa driver.

- For consoles running custom firmware (Atmosphere) and the Homebrew Menu. Using custom
  firmware online can get a console banned: play offline.
- It has been played on one console so far; `docs/SWITCH.md` lists what is still
  unchecked.
- This branch still builds the Windows version too, exactly as on the `pc` branch.
- **Prebuilt:** this repository's Releases page has `TSSM-Dehydrated-0.9-Switch.nro` as
  *TSSM Dehydrated* 0.9: this branch built under that name, with the port's own icon and
  no game data (you still need your own game files, below). Copy it as
  `/switch/tssm/tssm.nro` (below). `docs/SWITCH.md` ("Building the TSSM Dehydrated
  release") has the exact commands.

This repository and its branches
--------------------------------

This repository holds the **ports**. The decompilation they are built on is its own
repository, [tssm-decomp-fork](https://github.com/aidp2/tssm-decomp-fork): this repository's history starts with that
one's, so every port sits on exactly that code. Each port is a branch here; pick one from
GitHub's branch menu, or clone it with `git clone --branch <name>`.

| Branch | What it is for |
|---|---|
| `pc` | Windows (64-bit and 32-bit; Direct3D 9, Direct3D 11, OpenGL). The main port: every other branch is built on it. |
| `android` | Android phones and tablets: the PC port as an app, with touch controls and an import screen for the game files. |
| `switch` (this one) | Nintendo Switch homebrew (custom firmware): `tssm.nro`. |
| `3ds` | New 3DS homebrew (custom firmware): `tssm.3dsx` and `tssm.cia`. **Still in progress**: it runs, but has been played on a console only a little. |
| `coop` | Windows with two-player co-op: online, on the home network, or two copies on one PC. Built on `pc`. |
| `coop-android` | The co-op app for Android, a second app beside the normal one. Built on `android`. |
| `coop-switch` | The co-op Switch program, `tssm-coop.nro`. Built on `switch`. |

Every branch builds from source; you bring your own game files.

You need your own copy of the game
----------------------------------

This repository contains **no game data**, and neither does `tssm.nro`. It reads the files
of the **Xbox** release of *The SpongeBob SquarePants Movie*, so you need your own copy of
that disc and its files copied off it (about 1 GB). Never put those files in the
repository.

Building
--------

Everything comes from devkitPro. Build in devkitPro's MSYS2 shell on Windows, or any shell
with `DEVKITPRO` set on Linux or macOS.

1. devkitPro with the Switch packages:
   `pacman -S switch-dev switch-cmake switch-mesa switch-libdrm_nouveau switch-glad switch-pkg-config`
2. The source and its submodules:

       git clone --branch switch <this repository's address> tssm
       cd tssm
       git submodule update --init --depth 1

3. SDL 3 for the Switch (devkitPro's SDL fork), built with this repository's patches:

       git clone --depth 1 --branch switch-sdl-3.4 https://github.com/devkitPro/SDL.git <sdl>
       sh tools/switch/build_sdl.sh <sdl> <sdl build folder> <prefix>

4. Optional, for the movies: FFmpeg from its own source (LGPL-only settings):

       sh tools/switch/build_ffmpeg.sh <ffmpeg source> <work folder> <prefix>

5. The port:

       sh tools/switch/build_port.sh <build folder> <prefix>

   This makes `<build folder>/tssm.nro` (and `tssm.elf` beside it, which crash reports
   refer to).

`docs/SWITCH.md` explains each step and which versions were tested.

Playing
-------

1. Copy `tssm.nro` to `/switch/tssm/tssm.nro` on the SD card, and your Xbox game files to
   `/switch/tssm/assets/` (the folder with `boot.HIP`, `FMV/` and the level folders).
2. Start the Homebrew Menu **with full memory**: hold R while you start any game from the
   HOME menu, then pick TSSM. (From the Album it gets too little memory, and says so.)
3. Settings, saves and the log are written into `/switch/tssm/`.

More documentation:

| File | What |
|---|---|
| `docs/SWITCH.md` | building, playing, controls, testing on a console, how the Switch layer works |
| `docs/CONTROLS.md` | the controls |
| `docs/SAVES.md` | save files, and moving them to and from the PC, Dolphin or a GameCube |
| `docs/MODDING.md` | mods |
| `docs/ICON.md` | using your own picture as the program's icon |
| `src/SB/Core/pc/README.md`, `src/SB/Core/pc/PORTING.md` | the platform layer, and where each file came from |

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
| libnx, Mesa, libdrm_nouveau | devkitPro's Switch packages, linked statically; notes in `dist/switch/licenses` | ISC, MIT, MIT |
| newlib (the C library), libgcc, libstdc++ | devkitA64, linked statically | newlib's permissive licences; GPL 3 with the GCC Runtime Library Exception |
| FFmpeg on the Switch (optional) | built by `tools/switch/build_ffmpeg.sh` and linked statically | LGPL 2.1 or later |
