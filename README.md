# The SpongeBob SquarePants Movie: Android port

*The SpongeBob SquarePants Movie* game as an Android app, built from the matching
decompilation of the GameCube game ([tssm-decomp-fork](https://github.com/aidp2/tssm-decomp-fork), whose history
this repository starts from) and the PC port
(the `pc` branch). The app is the PC port's code built for Android: the same game code
and platform layer, with touch controls, controller support and an import screen for the
game files.

- Android 7.0 or newer, OpenGL ES 3. Two APKs: `arm64-v8a` (64-bit phones, which is most
  phones today) and `armeabi-v7a` (older 32-bit phones). The 64-bit app has been played
  on one phone so far; the 32-bit one is build-checked only.
- This branch still builds the Windows version too, exactly as on the `pc` branch.
- **Prebuilt:** this repository's Releases page has both APKs as *TSSM Dehydrated* 0.9
  (`TSSM-Dehydrated-0.9-Android.apk` for 64-bit phones, `TSSM-Dehydrated-0.9-Android-32bit.apk`
  for older 32-bit-only ones), with no game data (you still need your own game files,
  below): this branch's release build (`docs/ANDROID.md`, "Release APKs") with
  `-Ptssm.releaseDebugKey=true`, `"-Ptssm.label=TSSM Dehydrated"`, FFmpeg for the movies
  and the port's own icon (`tools/make_icons.ps1 -Background '#1E6FB0'` on the `switch`
  branch's `dist/switch/default_icon.jpg`, passed as `-Ptssm.icon`).

This repository and its branches
--------------------------------

This repository holds the **ports**. The decompilation they are built on is its own
repository, [tssm-decomp-fork](https://github.com/aidp2/tssm-decomp-fork): this repository's history starts with that
one's, so every port sits on exactly that code. Each port is a branch here; pick one from
GitHub's branch menu, or clone it with `git clone --branch <name>`.

| Branch | What it is for |
|---|---|
| `pc` | Windows (64-bit and 32-bit; Direct3D 9, Direct3D 11, OpenGL). The main port: every other branch is built on it. |
| `android` (this one) | Android phones and tablets: the PC port as an app, with touch controls and an import screen for the game files. |
| `switch` | Nintendo Switch homebrew (custom firmware): `tssm.nro`. |
| `3ds` | New 3DS homebrew (custom firmware): `tssm.3dsx` and `tssm.cia`. **Still in progress**: it runs, but has been played on a console only a little. |
| `coop` | Windows with two-player co-op: online, on the home network, or two copies on one PC. Built on `pc`. |
| `coop-android` | The co-op app for Android, a second app beside the normal one. Built on `android`. |
| `coop-switch` | The co-op Switch program, `tssm-coop.nro`. Built on `switch`. |

Every branch builds from source; you bring your own game files.

You need your own copy of the game
----------------------------------

This repository contains **no game data**, and neither does the app. It reads the files of
the **Xbox** release of *The SpongeBob SquarePants Movie*, so you need your own copy of
that disc and its files copied off it (about 1 GB): the folder that holds `boot.HIP`,
`font.HIP` and `FMV`. Never put those files in the repository.

Building the APKs
-----------------

You need the Android SDK (platform 35, build-tools 35, the SDK's CMake 3.22.1), NDK r28c
(28.2.13676358), Gradle 8.9 or newer (8.14.3 tested), JDK 17 or 21, and git. No Gradle
wrapper is included.

    git clone --branch android <this repository's address> tssm
    cd tssm
    git submodule update --init --depth 1
    cd android
    gradle assembleDebug

On Windows, set `JAVA_HOME` to the JDK and `ANDROID_HOME` to the SDK first. The APKs are
written to `android/app/build/outputs/apk/debug/`.

For the game's movies, build FFmpeg for Android first with
`tools/android/build_ffmpeg.sh` (from FFmpeg's own source; LGPL-only settings) and add
`-Ptssm.ffmpeg=<the folder it made>` to the `gradle` command. Without it the movies are
skipped. `docs/ANDROID.md` has every option and the details.

Playing
-------

1. Install the APK for your phone (`app-arm64-v8a-debug.apk` on almost every phone made in
   the last years).
2. On the first start the app asks for the game files: pick the folder that holds
   `boot.HIP` (copied onto the phone or an SD card). It can copy them into the app, or
   read them where they are.
3. On-screen touch controls are on by default; a Bluetooth or USB controller works too.
   `docs/ANDROID.md` ("Playing") shows the touch layout and how to open the settings
   window.

More documentation:

| File | What |
|---|---|
| `docs/ANDROID.md` | building, importing the game files, playing, files on the device, known issues |
| `docs/CONTROLS.md` | the controls |
| `docs/SAVES.md` | save files, and moving them to and from the PC, Dolphin or a GameCube |
| `docs/MODDING.md` | mods |
| `docs/ICON.md` | using your own picture as the app's icon |
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
| FFmpeg on Android (optional) | built by `tools/android/build_ffmpeg.sh` from FFmpeg's source and linked statically into the app | LGPL 2.1 or later |
