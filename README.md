The SpongeBob SquarePants Movie: decompilation
==============================================

A matching decompilation of the GameCube game *The SpongeBob SquarePants Movie* (USA,
`GGVE78`). All of the game's own code (the 40 units under `src/SB`) is decompiled into C
and C++ that compiles, with the original CodeWarrior compilers, into exactly the original
machine code. The build produces a `main.dol` that is byte for byte the same as the one on
the retail disc (SHA-1 `f772db7f38b9b55e27169822ed17184fa2dc2574`).

The SDK and middleware libraries the game was linked with (the Dolphin SDK, the C runtime,
RenderWare, FMOD) are only partly decompiled. As in every decomp-toolkit project, the
parts that are not yet in C are taken from your own copy of the original program when you
build.

> **Want to play it on a PC, a phone, a Switch or a 3DS?** This repository is only the
> decompilation; it builds the GameCube program, not a game you can run on a computer.
> The ports are in their own repository, **[tssm-ports](https://github.com/aidp2/tssm-ports)**: see
> [Ports](#ports) below.

This is an independent repository. It is based on the TSSM decompilation project of the
bfbbdecomp team (see [Credits](#credits)), but it is **not** that project's repository,
and nothing here has been submitted to it. See [How this was made](#how-this-was-made).

You need your own copy of the game
----------------------------------

This repository contains **no game data**: no disc image, no `main.dol`, no textures,
models, sounds or movies. To build it you need your own copy of the game's USA disc
(`GGVE78`, the revision 1 disc), from which the build reads the original program to
check its work. Keep the disc image and everything the build extracts from it out of the
repository (the `.gitignore` already does).

Building
--------

### Tools

- **Windows** (recommended: native tools, no WSL or MSYS2 needed): install
  [Python 3](https://www.python.org/downloads/) and
  [ninja](https://github.com/ninja-build/ninja/releases) (`pip install ninja` works) and
  put both on `PATH`.
- **Linux** (not tested for this repository): Python 3 and ninja from your package
  manager. On x86 and x86-64, [wibo](https://github.com/decompals/wibo), a small wrapper
  that runs the 32-bit Windows compilers, is downloaded automatically. On other CPUs
  install wine.
- **macOS** (not tested for this repository): `brew install ninja` and
  `brew install --cask --no-quarantine gcenx/wine/wine-crossover`.

Only the Windows build has been tested. The Linux and macOS steps are the ones the
decomp-toolkit template gives; reports of whether they work are welcome.

Everything else (the CodeWarrior compilers, decomp-toolkit, binutils, objdiff) is
downloaded by the build itself into `build/` on the first run.

### Steps

1. Clone the repository:

   ```sh
   git clone <this repository's address>
   cd <the folder it made>
   ```

2. Copy your disc image into the folder `orig/GGVE78/`, for example
   `orig/GGVE78/SpongeBob Movie.iso`. ISO and RVZ images were tested; the other
   formats decomp-toolkit reads (GCM, WIA, WBFS, CISO, NFS, GCZ, TGC) should work too.
   The build takes the program, `sys/main.dol`, out of the image into
   `orig/GGVE78/sys/main.dol`. After the first build the image can be deleted to save
   space: the extracted `main.dol` is enough from then on. If you already have the
   extracted `main.dol` (from Dolphin's "Extract System Data", for example), you can put
   it at `orig/GGVE78/sys/main.dol` instead of the image.

3. Configure and build:

   ```sh
   python configure.py
   ninja
   ```

The last step prints `build/GGVE78/main.dol: OK` when the result matches the retail
program.

### Comparing functions (objdiff)

After the first build there is an `objdiff.json` in the project root. Download
[objdiff](https://github.com/encounter/objdiff/releases), set the project directory, and
pick a unit on the left to compare the compiled code with the original, function by
function.

![](assets/objdiff.png)

What is in here
---------------

| Folder | What |
|---|---|
| `src/SB/Core/x`, `src/SB/Core/gc` | the engine: the shared `x` layer and the GameCube platform layer |
| `src/SB/Game` | the game itself (`z` files) |
| `src/dolphin`, `src/PowerPC_EABI_Support`, `src/runtime_libs`, `src/rwsdk`, `src/FMOD` | the GameCube SDK, runtime, RenderWare and FMOD code the game was linked with, as far as the game uses it |
| `include/` | shared headers, including the RenderWare SDK headers |
| `config/GGVE78/` | how the original program is split into units, and its symbols |
| `dwarf/` | debugging information dumped from the PlayStation 2 version, used as a reference for names and types |
| `tools/`, `configure.py` | the build (decomp-toolkit based) |
| `docs/` | the decomp tool chain's own documentation |

Ports
-----

The ports live in a separate repository, **[tssm-ports](https://github.com/aidp2/tssm-ports)**. Each port is
this decompiled code compiled for another system, plus a platform layer that replaces the
GameCube one; that repository's history starts with this one's, so the ports sit on
exactly this code. They read the game files of the **Xbox** release, which you also need
to own and copy from your own disc.

| Branch there | What |
|---|---|
| `pc` | Windows (64-bit and 32-bit): Direct3D 9, Direct3D 11 and OpenGL |
| `android` | Android phones and tablets (built on top of `pc`) |
| `switch` | Nintendo Switch homebrew, for consoles running custom firmware (built on top of `pc`) |
| `3ds` | New 3DS homebrew, still in progress (built on top of `pc`) |
| `coop`, `coop-android`, `coop-switch` | Windows, Android and Switch with two-player online and home-network co-op added |

How this was made
-----------------

The work in this repository on top of the upstream project was done with an AI coding
assistant (Claude, made by Anthropic), directed by the repository's owner. Every function
is checked the same way as in any matching decompilation: the build only passes when the
compiled program is identical to the original, byte for byte.

The upstream decompilation project keeps AI-assisted work out of its own repository and
treats it as a separate reference. That is what this repository is: a separate fork, not
part of the upstream project and not a pull request to it.

Credits
-------

- **The bfbbdecomp team and contributors**, for the upstream TSSM decompilation project
  this repository is based on (its history is kept here, unchanged, under this work), the
  project setup, and the sister decompilation of *Battle for Bikini Bottom*.
- **blu** ([@bluisblu](https://github.com/bluisblu)), who guided the decompilation to avoid
  fakematching, with coding standards, references and testing throughout.
- **seil** ([@seilc](https://github.com/seilc)), for the decompilation of the *Ratatouille*
  prototype and the Heavy Iron debugging-information dumps, used as references.
- **joeyballentine** ([@joeyballentine](https://github.com/joeyballentine)), whose
  AI-assisted *Battle for Bikini Bottom* fork and its notes helped with the matching.
- The authors of the tools the build uses: decomp-toolkit, objdiff, wibo and ninja.

*The SpongeBob SquarePants Movie* and its characters belong to their owners. This project
is not affiliated with or endorsed by them.

License
-------

The decompiled game code and the rest of the work in this repository are released under
CC0 1.0 (public domain), as in the upstream project; see `LICENSE`.

That does not cover the parts that belong to others, which this repository carries as
the upstream project does: the RenderWare SDK headers in `include/rwsdk`, and the SDK,
runtime and middleware code under `src/dolphin`, `src/PowerPC_EABI_Support`,
`src/rwsdk` and `src/FMOD`, keep their owners' copyright notices (Criterion, Metrowerks,
Nintendo and others) where the files carry them. `dwarf/` holds debugging information
dumped from the PlayStation 2 release, kept for reference. `tools/ninja_syntax.py` is
under the Apache License 2.0, as noted in the file. The tools the build downloads (the
compilers and the rest) are not part of this repository and keep their own licences.
