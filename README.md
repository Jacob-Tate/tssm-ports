# The SpongeBob SquarePants Movie: New 3DS port (in progress)

*The SpongeBob SquarePants Movie* game as a homebrew program for the **New 3DS**, built
from the matching decompilation of the GameCube game ([tssm-decomp-fork](https://github.com/aidp2/tssm-decomp-fork))
and the PC port (the `pc` branch). It is the PC port's code built with devkitPro for the
3DS, drawing through citro3d on the top screen, with the settings menu on the bottom
screen.

- **Still in progress.** It runs the whole game, but it has been played on a console only
  a little; `docs/3DS.md` ("Known gaps") says what is still unchecked.
- **New 3DS, New 3DS XL and New 2DS XL only**, running custom firmware (Luma3DS). An
  original 3DS or 2DS is too slow and has too little memory; the game says so and stops.
- **Stereoscopic 3D** with the 3D slider: the world in 3D, the HUD and menus at the
  screen. Where a busy place cannot keep 30 fps in 3D, the game plays it in 2D for a while
  by itself.
- Two packages from the same program: `tssm.3dsx` for the Homebrew Launcher and
  `tssm.cia` for the HOME Menu.
- This branch still builds the Windows version too. It started from an early copy of the
  Switch port and still carries that copy: for the Switch, use the `switch` branch.

Two ways to play
----------------

- **Download it ready-made.** This repository's Releases page has *TSSM Dehydrated* 0.9.6
  for the New 3DS: `TSSM-Dehydrated-0.9.6-3DS.3dsx` and `TSSM-Dehydrated-0.9.6-3DS.cia`.
  They are this branch, built under that neutral name with the port's own icon, banner and
  chime, and have no game data. Install them as `tssm.3dsx` and `tssm.cia` below (the
  .3dsx renamed to `tssm.3dsx`). From 0.9.4 on it can update itself when you ask:
  settings menu (START + SELECT), Settings tab, *Updates* at the bottom, *Check for
  updates*. Coming from 0.9.2, install by hand once: 0.9.2's updater cannot reach GitHub
  on a real 3DS. `docs/3DS.md` ("Updates", and "Building the TSSM Dehydrated release" for the
  exact commands) has the details.
- **Build it yourself** from this source, as below. A build of your own is called *The
  SpongeBob SquarePants Movie Video Game*, and can make its HOME Menu icon and its
  animated Patty Wagon banner from your own game files (step 6 below). It has no updater
  unless you turn one on.

Either way you bring your own copy of the game's files (below).

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
| `switch` | Nintendo Switch homebrew (custom firmware): `tssm.nro`. |
| `3ds` (this one) | New 3DS homebrew (custom firmware): `tssm.3dsx` and `tssm.cia`. **Still in progress**: it runs, but has been played on a console only a little. |
| `coop` | Windows with two-player co-op: online, on the home network, or two copies on one PC. Built on `pc`. |
| `coop-android` | The co-op app for Android, a second app beside the normal one. Built on `android`. |
| `coop-switch` | The co-op Switch program, `tssm-coop.nro`. Built on `switch`. |

Every branch builds from source; you bring your own game files.

You need your own copy of the game
----------------------------------

This repository contains **no game data**, and neither does the program. It reads the
files of the **Xbox** release of *The SpongeBob SquarePants Movie*, so you need your own
copy of that disc and its files copied off it (about 1 GB). Never put those files in the
repository.

Building
--------

Everything but the two `.cia` tools comes from devkitPro. Build in devkitPro's MSYS2 shell
on Windows, or any shell with `DEVKITPRO` set on Linux or macOS.

1. devkitPro with the 3DS packages: `pacman -S 3ds-dev 3ds-cmake 3ds-pkg-config`
2. The source and its submodules:

       git clone --branch 3ds <this repository's address> tssm
       cd tssm
       git submodule update --init --depth 1

3. SDL 3 for the 3DS, from the `third_party/SDL` submodule:

       sh tools/3ds/build_sdl.sh third_party/SDL <sdl build folder> <prefix>

4. Optional, for the movies: FFmpeg from its own source (LGPL-only settings):

       sh tools/3ds/build_ffmpeg.sh <ffmpeg source> <work folder> <prefix>

5. The port:

       sh tools/3ds/build_port.sh <build folder> <prefix>

   This makes `<build folder>/tssm.3dsx` and `tssm.elf` beside it (crash reports refer to
   it). With makerom and bannertool on the `PATH` (or in the folder `TSSM_CTR_TOOLS`
   names) it also makes `tssm.cia`. Add `-DTSSM_PYTHON=<python 3>` so the HOME Menu shows
   the whole name (devkitPro's shell has no Python of its own). Pass it on the first run
   (or delete the build folder first): later runs do not configure again.
6. Optional: the `.cia`'s HOME Menu banner is a plain picture of the port's own. To have
   the Patty Wagon moving there instead, `tools/3ds/banner_from_game_files.py` makes a
   banner from your own game files and `-DTSSM_3DS_BANNER=<its banner.bnr>` builds it in
   (`docs/3DS.md`, "An animated HOME Menu banner from your own game files"). It is made
   from your copy of the game: keep that banner and that `.cia` to yourself, and keep the
   build folder outside this repository's folder (or under its `build/`). Likewise
   `tools/3ds/icon_from_game_files.py` makes the icon of both packages from your game
   files (the title image's life ring, or a Krabby Patty with `--image save`) and
   `-DTSSM_3DS_ICON=<its folder>` builds it in (`docs/3DS.md`, "An icon from your own game
   files"); the same goes for keeping it to yourself.

None of the folders may contain spaces (FFmpeg's configure). `docs/3DS.md` explains each
step and which versions were tested.

Playing
-------

1. Copy your Xbox game files into `/3ds/tssm/files/` on the SD card: the files at the top
   of the disc (`boot.HIP`, `font.HIP`, `plat.HIP`, `FMV/` and the level folders), not the
   folder above them and not a disc image.
2. Install one or both packages:
   - **.3dsx:** copy `tssm.3dsx` to `/3ds/tssm/tssm.3dsx` and start it from the Homebrew
     Launcher.
   - **.cia:** copy `tssm.cia` anywhere on the SD card, open FBI (or another CIA
     installer), find the file and choose *Install CIA*. The game is then on the HOME
     Menu.
3. For sound, dump the console's DSP firmware once: Rosalina menu (L + Down + SELECT),
   Miscellaneous options, Dump DSP firmware. Without it the bottom screen says so. In an
   emulator, copy that dump from your 3DS into the emulator's `sdmc/3ds` folder
   (`docs/3DS.md`, "No sound?").
4. START + SELECT opens the settings menu on the bottom screen. Settings, saves and the
   log are written into `/3ds/tssm/`.

More documentation:

| File | What |
|---|---|
| `docs/3DS.md` | installing, playing, controls, the settings menu, building, developer tools, how the 3DS layer works, known gaps |
| `docs/CONTROLS.md` | the controls |
| `docs/SAVES.md` | save files, and moving them to and from the PC, Dolphin or a GameCube |
| `docs/MODDING.md` | mods |
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
| libctru, citro3d | devkitPro's 3DS packages, linked statically; notes in `dist/3ds/licenses` | zlib |
| newlib (the C library), libgcc, libstdc++ | devkitARM, linked statically | newlib's permissive licences; GPL 3 with the GCC Runtime Library Exception |
| FFmpeg on the 3DS (optional) | built by `tools/3ds/build_ffmpeg.sh` and linked statically | LGPL 2.1 or later |
