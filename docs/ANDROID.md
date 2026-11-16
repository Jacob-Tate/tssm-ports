# The Android port

The PC port (`src/SB/Core/pc`), built as an Android app. It is treedome's
Android layer (the BfBB PC port, CC0), adapted to TSSM.

- App name: **SpongeBob Movie Port** (`-Ptssm.label` gives a build another)
- Package (application id): `io.github.tssmport.game`
- No game data in the APK. The player supplies the files from their own copy
  of the Xbox release of The SpongeBob SquarePants Movie (about 1 GB).

## Status

- Both APKs build: `arm64-v8a` (64-bit ARM) and `armeabi-v7a` (32-bit ARM).
- arm64-v8a has been played on one phone; armeabi-v7a has not run on a
  device yet.
- **arm64-v8a is the one for 64-bit phones** (Pixel 7 and later, and most
  phones sold since, run only 64-bit apps). It loads the game as the Windows
  x64 build does: the asset data that keeps pointers in 4-byte slots is
  widened as it loads (`iAssetWiden.h`, src/SB/Core/pc/PORTING.md), and x64
  plays to gameplay with the same frames as x86 (`tools/regress.py`).
- **armeabi-v7a is for older phones** that still run 32-bit apps. It has
  4-byte pointers, like the Windows x86 build.

## Requirements

- Android 7.0 (API 24) or newer. `minSdk` and `ANDROID_PLATFORM` are both 24.
  `targetSdk` and `compileSdk` are 35.
- OpenGL ES 3 (librw asks for ES 3.1, then 3.0, then 2.0; the manifest
  declares 2.0). The renderer is librw's GL3 backend on GLES.
- Vulkan is not built by default (`-Ptssm.backends=GL3;VULKAN` adds it).
  The settings overlay has no Vulkan renderer, so GL3 is the default.

## Building the APKs

Tools:
Android SDK with platform 35, build-tools 35, SDK CMake 3.22.1, NDK r28c
(28.2.13676358), Gradle 8.9 or newer (8.14.3 tested) and JDK 17 or 21. No
Gradle wrapper is checked in. The Android Gradle plugin is pinned to 8.7.3.

1. Submodules: `git submodule update --init --depth 1`. Vulkan's three are
   needed only for a Vulkan build.
2. FFmpeg, for the movies (optional). See [Movies](#movies).
3. Build:

   ```
   cd android
   gradle assembleDebug -Ptssm.ffmpeg=<folder of FFmpeg builds>
   ```

   On Windows set `JAVA_HOME` to the JDK and `ANDROID_HOME` to the SDK
   first.

The APKs are written to `android/app/build/outputs/apk/debug/`:

- `app-armeabi-v7a-debug.apk`
- `app-arm64-v8a-debug.apk`

They are signed with the SDK's debug key, so they install with
`adb install -r` or from a file manager ("install unknown apps").

Gradle builds the native code through `externalNativeBuild`, pointed at the
repository's own `CMakeLists.txt`. There is no second build system. Only the
`main` target is built. The debug build type compiles native code as
`RelWithDebInfo` (AGP's default for debug is `-O0`); the symbols are stripped
from the `.so` in the APK.

### Gradle properties

| Property | Default | Effect |
| --- | --- | --- |
| `-Ptssm.abis` | `armeabi-v7a,arm64-v8a` | The ABIs, one APK each (ABI splits). |
| `-Ptssm.backends` | `GL3` | Passed as `TSSM_RENDER_BACKENDS`: `GL3`, `VULKAN` or `GL3;VULKAN`. |
| `-Ptssm.ffmpeg` | none | A folder holding `<abi>/include` and `<abi>/lib` for each ABI, from `tools/android/build_ffmpeg.sh`. Passed as `TSSM_FFMPEG_ROOT`. The environment variable `TSSM_FFMPEG_ANDROID` works too. Without it the movies are skipped. |
| `-Ptssm.icon` | none | A folder made by `tools/make_icons.ps1` from a picture, outside the repository. Its `android/res` overrides the launcher icon (debug and release). Without it the APK has the default icon. [ICON.md](ICON.md). |
| `-Ptssm.releaseDebugKey` | `false` | `true` signs the release APKs with the SDK's debug key (`~/.android/debug.keystore`), the key the debug APKs have, so either kind updates the other in place and the co-op app's save sharing (same key only) still works. Without it the release APKs are unsigned. |
| `-Ptssm.label` | `SpongeBob Movie Port` | The app's name (its label), as plain text: under the launcher icon, in the recent apps, in Settings and in the import screen's All files access hint. It replaces `app_name` from `src/main/res/values/strings.xml`. |

### Release APKs

```
cd android
gradle assembleRelease -Ptssm.releaseDebugKey=true -Ptssm.ffmpeg=<folder>
```

They are written to `android/app/build/outputs/apk/release/`:
`app-<abi>-release.apk` with `-Ptssm.releaseDebugKey=true`,
`app-<abi>-release-unsigned.apk` without it (sign them with `apksigner`
after `zipalign`). They differ from the debug APKs only in that the app is
not debuggable (no `run-as`, no debugger, no test launches through `adb`).
The native code is compiled the same way, as `RelWithDebInfo`.

### What CMake does differently on Android

Each is an `if(ANDROID)` block in `CMakeLists.txt`.

- The game is a shared library, `libmain.so` (target `main`), not an
  executable. It also compiles `src/SB/Core/pc/android/iAndroid.cpp`,
  `iAndroidDocument.cpp` and `iAndroidMain.cpp`.
- `zMain.cpp` is compiled with `-include android/iAndroidGameMain.h`, which
  renames its `main` to `tssmGameMain`. `zMain.cpp` itself is unchanged. It
  also gets `-fno-strict-return`, because its `main` falls off the end after
  `iSystemExit`, which is only well defined while the function is `main`.
- SDL is the vendored submodule, linked statically into `libmain.so`.
- `TSSM_HOST_TOOLS` is off: no `save_selftest`, `tssm_input_test`,
  `tssm_sndtest` or `tssm_modtest`.
- `-fmacro-prefix-map` makes `__FILE__` (in librw's and the game's messages)
  relative to the checkout, so `libmain.so` does not carry the folder it was
  built in.
- `main` and `tssm_platform` link `liblog`; `main` also links `libandroid`.
- With `TSSM_FFMPEG_ROOT`, FFmpeg is found in `<root>/<abi>` (the NDK
  toolchain otherwise searches only its own sysroot) and linked statically,
  with `libm`. Its archives are linked with `--exclude-libs`, so their
  symbols are hidden inside `libmain.so`: FFmpeg's aarch64 assembly addresses
  its own tables PC-relative, which lld refuses for an exported symbol. SDL's
  archive is not hidden, because `JNI_OnLoad` must stay exported.
- `TSSM_BUILD_32BIT` is an error on Android. The ABI decides the word size.

Outside CMake, one source change was needed for arm64: NDK r28c's clang 19
crashes ("Cannot select: store (s16) ... bswap") on four byte stores that
write a colour in the reverse of the order its bytes arrive in, which is what
the RenderWare vertex colour macros did on PC (BGRA layout). They now write
one 32-bit word (`rwsdk/rwcore.h`, `rwsdk/rwplcore.h`, marked
`TSSM-ADAPTED`).

## Movies

TSSM's movies are Bink 1 (`FMV/*.bik`). FFmpeg decodes them, as on Windows.
On Android FFmpeg is built from source and linked statically into
`libmain.so`:

```
tools/android/build_ffmpeg.sh <ffmpeg source> <work dir> <abi> <install prefix>/<abi>
```

- Tested with FFmpeg n9.0.2, NDK r28c, API 24, for `arm64-v8a` and
  `armeabi-v7a` (NEON on both).
- It needs a POSIX shell with `make` (Linux, macOS, or MSYS2 on Windows) and
  `ANDROID_NDK_HOME`. On Windows, vcpkg's own MSYS2
  (`%USERPROFILE%\vcpkg\downloads\tools\msys2\<id>\usr\bin\bash.exe`) works.
- It also needs a host C compiler: swscale's aarch64 code is generated by a
  small program built during the build. On Windows the script uses LLVM's
  `clang.exe` from Program Files, which works without a Visual Studio
  prompt.
- No path may contain a space; FFmpeg's `configure` refuses them. Build in a
  folder without spaces and copy the install afterwards if needed.
- LGPL only. Built in: the `bink`, `binkaudio_rdft` and `binkaudio_dct`
  decoders and the `bink` demuxer (the movies), plus Vorbis, Opus, FLAC, MP3
  and PCM with their demuxers (replacement sounds from mods, `iModAudio.cpp`),
  the `file` protocol, swscale and swresample: 2.8 MB of archives for
  armeabi-v7a, 4.5 MB for arm64-v8a.
- The script copies FFmpeg's `COPYING.LGPLv2.1` to
  `<prefix>/share/ffmpeg/copyright`, where the GUI's About tab reads it.

Without FFmpeg, `iFMVDecoderNull.cpp` is built and every movie reports that it
ran to the end, so the game carries on without them. The startup banner in
the log says which decoder a build has (`Movie decoder: ffmpeg` or `none`).

The game's own sounds are paused while a movie plays and come back when it
ends, as on the console (`iFMVPlay` calls `xFMVPauseSound`). Up to 0.7.1 they
were not: the story movie at the end of a level starts while the level is
still running, so its music and ambience played a few seconds into the
movie and a looping effect nearby (SC02, DE02) played under all of it. The
same was true on Windows; the code is shared.

## First launch

`LauncherActivity` is the launcher entry.

1. If no game files are found, it opens `ImportActivity`.
2. Otherwise it starts `GameActivity`, which runs the game.

Long-press the app icon for **Import game files**, which opens the import
screen again (to re-import, or to switch between a copy and a linked folder).

### Importing the game files

`ImportActivity` explains what is needed and offers two choices. Both use the
system folder picker. The player picks the folder holding `boot.HIP` and
`font.HIP` (the root of the extracted Xbox disc), or the folder directly above
it.

**Copy into the app.** Needs no permission. Copies into
`Android/data/io.github.tssmport.game/files/assets`. Only the files the game
reads are copied, by the Xbox disc's layout:

- `.HIP` and `.HOP` files at the top level (`boot.HIP`, `font.HIP`,
  `plat.HIP` and the `_US` halves) and in the two-letter level folders
  (`AM`, `B1`-`B4`, `BB`, `BL`, `DE`, `FB`, `GG`, `JK`, `MN`, `PL`, `PT`,
  `SC`, `TR`, `TT`)
- the movies, `FMV/*.bik`
- the game's `.ini` files at the top level (`sb04.ini`, `explode.ini`,
  `water.ini`)

`DEFAULT.XBE` and anything else stays behind. The copy goes to
`assets.importing` and is renamed to `assets` when complete. If there is not
room for two copies, the old `assets` is deleted first. The screen stays on
during the copy.

**Use the folder where it is.** Needs All files access
(`MANAGE_EXTERNAL_STORAGE` on Android 11+, `READ_EXTERNAL_STORAGE` on 10 and
below). The folder must be on the device's storage or an SD card. Its path is
saved and passed to the game as `TSSM_ASSETS`. Any earlier copy in the app's
directory is deleted.

A linked folder that no longer holds `boot.HIP` and `font.HIP`, or that the
app can no longer read, sends the next launch back to the import screen.

### Where the game looks

`GameActivity` sets `TSSM_ASSETS` for a linked folder. Otherwise
`iAndroidStartup` sets it to `<external files>/assets` when that folder
exists. If neither applies, `[assets] path` in `config.ini` decides, as on
every other host. The game then checks for `boot.HIP` and `font.HIP` and for
the Xbox platform id in `boot.HIP`; a failure shows an error box and the log
says where it looked.

The ImGui launcher (`gui.launcher`) does not open on Android unless
`gui.launcher = always`: the import screen has already found the files, and
SDL's folder picker returns a `content://` URI the game cannot open.

## Playing

- **Touch controls** are on by default on Android
  (`input.touch_controls = auto`). Code: `iPadTouch.cpp` for input,
  `iTouchOverlay.cpp` and `rw/touch_overlay.cpp` for drawing.

  | On screen | Where | Game button | Does |
  | --- | --- | --- | --- |
  | left stick | anywhere on the left half (it centres where the thumb lands) | left stick | move |
  | right stick | bottom right, left of A B X Y (it centres where the thumb lands near it) | right stick | camera: orbit and tilt |
  | A | right, bottom of the diamond | A | jump, double jump; accept |
  | B | right, right of the diamond | B | SpongeBowl, Cartwheel; back |
  | X | right, left of the diamond | X | Karate Spin, Star Spin, Smash |
  | Y | right, top of the diamond | Y | Bash, swing |
  | L | top left | LT | camera behind the player; hold for the close-up camera; HUD |
  | R | top right | RT | talk, pick up and throw, Sonic Wave |
  | = | top left, under L | Back | the upgrade window |
  | Z | top right, under R | black | nothing in normal play |
  | > | top, left of R | Start | pause |

  Each button presses the row of the same name in the input layer's table
  (`kPadBindButtons`, `iPadBindTable.cpp`, docs/CONTROLS.md) and sets that
  row's bits, so it does what a controller's button of that name does, and
  `input.layout` (xbox or gamecube moves) applies to it as to a pad. The
  right stick is the controller's right stick, so `input.camera_sensitivity`
  and the camera inversions apply to it too.

  **Each finger belongs to the control it landed on until it lifts.** A
  thumb on the left stick only ever moves the left stick, wherever it slides;
  a finger that lands on A presses A while it stays near A, and lets go (and
  presses nothing else) when it slides off; a second finger on the left half
  while the stick is held, or a finger on empty screen, does nothing. The
  fingers are read from SDL's touch events in order (a watch on the event
  queue, as Android posts them from its UI thread), so a tap shorter than a
  frame still presses for one frame.

  SDL also turns the first finger on the screen into a mouse (with its left
  button held) for programs that only know mice. The keyboard-and-mouse layer
  now ignores that stand-in mouse (`iPadKeyboardSDL.cpp` reads only real
  mice): in 0.2 it made a thumb on the left stick turn the camera through the
  mouse camera, hold the left mouse button (X, attack), and switch the button
  prompts to keyboard keys.

  The controls keep clear of the screen's cut-outs and rounded corners (SDL's
  safe area). Settings, on the overlay's Controls tab under **Touch
  controls** and in `config.ini`:

  | Setting | Default | Does |
  | --- | --- | --- |
  | `input.touch_controls` | `auto` | on, off, or auto (on for Android); applies at the next start |
  | `input.touch_drag_look` | `off` | a drag on the empty right half (not a button, not near the right stick) also turns the camera, as in 0.2 |
  | `input.touch_scale` | `1.0` | size of the controls, 0.5 to 1.2 |
  | `input.touch_opacity` | `100` | how solid they are drawn, 10 to 100 percent |

  **Button prompts** show the on-screen buttons while the touch controls
  are what is being used (`input.button_icons = auto`, or `touch`): the
  disc's own Xbox A, B, X and Y, and the letter each other button carries on
  screen (L, R, =, Z, >). A controller or a keyboard used instead brings its
  own pictures back.
- **Settings overlay** (docs/GUI.md): three fingers down within a quarter of
  a second open or close it, wherever they land (on the controls too);
  Back+Start on a controller. Its tabs are
  Settings, Controls, Saves, Mods and About, and it is used by tapping.
  SDL's Java half sends touch, key and mouse events from Android's UI
  thread; the overlay queues them and handles them on the game's thread at
  the next frame (`gui/iGui.cpp`). While it is closed it queues only the
  events it acts on then (finger down and up, key down, window changes), and
  motion is merged, so a finger held on the stick through a level load
  cannot fill the queue and lose the release that the three-finger count
  depends on. Each frame the count is also lowered to the fingers SDL has
  down.
- **Saves** (the overlay's Saves tab, docs/SAVES.md): **Import...** opens
  the system's file picker for a Dolphin `.gci`, a GameShark `.gcs`, a
  memory card image (`.raw`, `.gcp`) or a `.zip` from Export. **Export**
  writes every save as one `.zip`, or one save as a `.gci` (its
  **Export...** button). The save picker opens with the name filled in
  (`SpongeBobMovie-saves.zip`, or the name Dolphin gives the `.gci`); a name
  typed instead keeps the extension typed. The game reads and writes the
  picked document through the app's own Java, not SDL's
  ([Saves and the file picker](#saves-and-the-file-picker)). There is no
  "GCI folder" export on Android: SDL has no folder picker there.
- **Mods** (docs/MODDING.md) go in the app's external files folder,
  `Android/data/io.github.tssmport.game/files/mods`, beside the imported
  game files and the log, where a computer can copy them over USB. The
  Mods tab's **Open the mods folder** creates it. The internal
  `files/mods` and `files/tssm/mods` are still searched after it.
- **Controllers** work through SDL (`iPadHostSDL.cpp`). The touch controls
  hide while a controller on port 0 is in use and return when the screen is
  touched.
- **Back button** pauses. It is trapped with
  `SDL_HINT_ANDROID_TRAP_BACK_BUTTON` and sent to the game as Start.
- **Orientation** is landscape, either way up.
- **Render size** defaults to 720 lines on Android (480 on the desktop), in
  the screen's own shape: 1600x720 on a 20:9 phone. The 3D view shows more at
  the sides, and the menus, HUD and text stay in a 4:3 area in the middle
  (docs/WIDESCREEN.md). `[video] aspect = 4:3` gives the original 960x720
  picture with bars. The touch controls are placed against the whole screen
  whatever the aspect.
- **Refresh rate.** On Android 11 and later the game asks for its frame-rate
  cap (60 by default) as the surface's rate (`ANativeWindow_setFrameRate`,
  `iWindowSDL.cpp`), so a 90 or 120 Hz panel can drop to 60 or 120 instead of
  showing frames for alternately one and two refreshes. The log says
  `tssm: asked Android to show the game at 60 Hz`. Android may decline when
  the switch would not be seamless, and before Android 11 there is no such
  call; then the pacing is the sleep alone, as before. While a movie plays
  the game paces at 60 at most (the movies are 30 frames a second or less)
  and asks for that rate, so a 120 Hz panel can drop to 60 until the movie
  ends; the log then has a `60 Hz` line for each movie.
- **Frame pacing at the panel's rate.** With vsync on and the cap at or
  above the panel's rate ("Display rate" on a 120 Hz phone), the panel's own
  refresh paces the frames (`iWindowPaceFrame`). Before 0.9 the game also
  slept to the cap's deadline, a second clock at nearly the panel's rate:
  as the two drifted, a frame whose work fitted could be shown a refresh late
  and the next one in the same refresh, a dropped frame. The sleep now only
  stands behind vsync, a quarter above the cap, in case the display does not
  wait. A cap under the panel's rate (60 on 120 Hz, movies) is paced by the
  sleep as before.

### Heat, battery and frame drops

**0.9 cuts the game's own work per frame, at any setting.** Most of the
game thread's time on a phone is the GL driver's, which on Android runs on
the thread that calls it and checks the vertex state again at every draw:

- **One vertex array object per model** (librw, `tools/librw/perf11_cpu.patch`).
  A draw binds one object instead of two buffers and an enable, a pointer and
  a disable per vertex attribute. On the PC at 2340x1080 that cut the GL calls
  per frame in Shell City (SC02) from about 7000 to about 1600 for the same
  450 draws, with the same picture (docs/RENDERING.md has the measurements).
- **Stream buffers of the batch's size.** The HUD, shadows and screen passes
  orphaned a 280 to 360 KB buffer for every quad they drew, about 400 MB of
  fresh buffer storage a second at 120 fps; each batch now gets storage of
  its own size, with its data, in one call.
- **The swap interval is set once**, not before every frame
  (`eglSwapInterval`, which reconfigures the surface's buffer queue).
- **Vsync paces the frames at the panel's rate** (above, under "Frame pacing
  at the panel's rate"), so a frame that fits is not shown late.
- **Performance hints** (Android 13 and later, `android/iAndroidPerf.cpp`):
  the game thread reports each frame's work and the paced frame time (8.33
  ms at 120) to `APerformanceHint`, so the CPU governor can run its core as
  slowly as still meets the frame time instead of guessing from a thread that
  wakes 120 times a second, and raise the clock as soon as a frame runs long.
  On Android 15 and later the session also asks for power efficiency. The
  log says `tssm: performance hints: on for the game thread, 8.33 ms a frame,
  preferring power efficiency`. Loading frames and the first frame back from
  the background are not reported.
- **No accelerometer.** SDL's surface listened to the accelerometer for as
  long as the game was in front (`SENSOR_DELAY_GAME`, 50 or more events a
  second, each waking the UI thread and native code). The game reads no
  accelerometer, so `GameActivity` never turns it on.

**0.9 also cuts what a frame costs the GPU**, at any setting
(docs/RENDERING.md, "What a frame costs the GPU"). A phone GPU draws the
picture in small tiles in its own memory, and every time the frame was
interrupted -- once for each shadow (up to three) and once for the glow --
the whole picture, every anti-aliasing sample of colour and depth, was
written out to memory and read back in. Now:

- the shadows are drawn before the scene, so the scene is drawn in one go;
- the glow's composite and the HUD are drawn single-sampled after the glow,
  so the anti-aliasing samples are written out and read back once a frame,
  not twice. The HUD is then not anti-aliased: the edges of the dialog
  letters' boxes come out sharp, a small difference (docs/RENDERING.md);
- the glow reads the picture where it is instead of copying it;
- depth is not written out at the end of the frame, nor read in to keep
  the stencil.

On a PC (TR01, three shadows) the scene's buffer went from being entered
2.8 times a frame on average to 2 (the scene, then the glow's composite and
the HUD), and the full-frame copies from 4 to 3.

Not on by default: giving the window's surface buffers of the render size,
so that the display rather than the GPU scales the 2340x1080 picture up to
the panel's 3120x1440 (`ApplySurfaceBufferSize` in `iWindowSDL.cpp`).
`TSSM_GPU_FIXEDSIZE=1` turns it on for a measurement; the touches, the touch
controls and the overlay keep the window's size.

Not on by default either: anti-aliasing through
`EXT_multisampled_render_to_texture`, whose samples never leave the tile
memory. On the S25 Ultra's Adreno it does not keep the depth when the
picture is left and entered again inside a frame, and in BB03 walls were
drawn over the characters. `TSSM_GPU_MSRTT=1` turns it on for a comparison.
`TSSM_GPU_MSDISCARD=1` discards the anti-aliasing samples once they are
folded, for measuring what that saves; it is off because the next frame
reads some of them (docs/RENDERING.md).

The final numbers on a phone will follow in this section. For comparing, a
test launch can put each old path back:
`TSSM_GL_VAO=0`, `TSSM_GL_STREAM=0`, `TSSM_GL_SWAP_CACHE=0`,
`TSSM_VSYNC_PACE=0`, `TSSM_PERF_HINT=0` (or `2`: hints without the power
efficiency request) and `TSSM_ANDROID_ACCEL=1`, and the GPU's savings with
`TSSM_GPU_OPT=<list>` (`prepass`, `post`, `invalidate`, `readview`, `all`;
docs/RENDERING.md). `TSSM_GPU_OPT=post` also gives the HUD its
anti-aliasing back. The paths that are off by default are turned on with
their own keys, above.

The measurements below are from 0.8, before these changes.

The phone's work per second is set by three settings in the overlay's
Settings tab, and each says what it costs in a line under it (docs/GUI.md):

- **Frame rate cap** is the biggest. The game draws and simulates every
  frame, so 120 is twice the work of 60 every second, for a game made for
  60. With "Display rate" on a 120 Hz phone the game runs at 120.
- **Anti-aliasing** costs GPU time in every frame. Most phones give at
  most 4x; asked for 8x they draw 4x, and the Settings tab says so. Since
  0.9 the samples are written out to memory and read back once a frame
  instead of two or more times (above), so on a phone it costs less than it
  did.
- **Render size** costs GPU time in every frame in proportion to its pixels.

Measured on a Galaxy S25 Ultra (Adreno 830, 120 Hz), from about 37 C, the
chip (AP) sensor after 60 seconds of the same scene:

| Render size | Anti-aliasing | Frame rate | Rise |
| --- | --- | --- | --- |
| 3120x1440 | 4x | 120 | +10.3 C |
| 2340x1080 | 4x | 120 | +7.3 C |
| 2340x1080 | off | 120 | +3.2 C |
| 2340x1080 | 4x | 60 | +1.0 C |
| 1600x720 | off | 60 (the defaults) | +0.5 C |

Over four minutes, 2340x1080 with 4x at 120 reached 49.5 C and was still
rising; at 60 it levelled off at 41 C. At 120 the game's thread used about
72% of a fast core (6 ms of the 8.3 ms a frame has), so a small slowdown
from heat costs frames; at 60 it used about a third, and no frame was
dropped.

**For a cool phone, set the frame rate cap to 60** and keep the render size
and anti-aliasing you like: 1080 lines with 4x at 60 stayed cool. 30 saves
more battery. The Android defaults are 720 lines, anti-aliasing off and 60.

At 30, or on a phone that cannot keep about 45 frames a second, the
SpongeBall keeps its full speed. That is a change from the original game,
which slows the ball at those rates while the falling blocks of the
SpongeBall levels do not slow (`src/SB/Core/pc/PORTING.md`, known
divergences). At 60 and faster nothing changes.

Two smaller costs are gone since 0.7.1, at any setting:

- Movies presented the same picture up to 120 times a second; they now
  present at 60 at most, and the panel can drop to 60 while they play.
- The frame pacer slept in 1 ms steps and spun out the last one, waking a
  fast core 700 to 900 times a second at 120 fps. It now sleeps once per
  frame (`iWindowPaceFrame`).

The window's own surface has no anti-aliasing of its own: librw's
`SDL_GL_MULTISAMPLESAMPLES` is left at 0, and the samples are only in the
game's render target.

## Files on the device

| What | Where |
| --- | --- |
| Game files (copied) | `/sdcard/Android/data/io.github.tssmport.game/files/assets` |
| Log | `/sdcard/Android/data/io.github.tssmport.game/files/tssm-log.txt` (the run before: `tssm-log.prev.txt`) |
| Crash reports | `/sdcard/Android/data/io.github.tssmport.game/files/crash.log` |
| Mods | `/sdcard/Android/data/io.github.tssmport.game/files/mods` |
| `config.ini` | internal storage: `files/config.ini` |
| Saves | internal storage: `files/tssm/saves` (docs/SAVES.md) |
| One-copy lock files | internal storage: `files/locks` |

"Internal storage" is the app's private directory
(`/data/data/io.github.tssmport.game/`). It needs `adb shell run-as` to reach,
and is wiped when the app is uninstalled or its storage is cleared. Settings
can be changed in the overlay; `config.ini` can also be edited over adb:

```
adb shell run-as io.github.tssmport.game cat files/config.ini > config.ini
# edit
adb push config.ini /data/local/tmp/
adb shell run-as io.github.tssmport.game cp /data/local/tmp/config.ini files/config.ini
```

## Logs and crashes

stdout and stderr go to two places:

- logcat, tag `tssm`, at INFO: `adb logcat -s tssm`
- `tssm-log.txt` in the external files directory, readable from a file
  manager

The file is started again at each launch, the run before kept as
`tssm-log.prev.txt` (as the desktop keeps `last_run.prev.log`), and capped at
8 MB. Lines printed before `iAndroidStartup` knows the file's path are held in
memory (up to 64 KB) and written when it opens. Fatal startup errors (missing
game files, non-Xbox assets) also show an SDL message box and go to logcat at
ERROR. On a crash the handler in `tssm_main.cpp` prints `tssm: CRASH --` with
the signal and a stack (`iHostPrintCallers`, `_Unwind_Backtrace` and `dladdr`
on Android), and appends a line saying when and what to `crash.log` beside
the log, which is never started again.

The desktop's own log (`iLog.h`: `last_run.log` through a tee of stdout and
stderr) is not used on Android; the pump above is the log there.
`iAndroidStartup` hands the file's path to `iLogUsePlatformLog`, so
`crash.log` goes in the external files directory, and the crash handler's
`iLogDrain` waits for the pump to write the report's last lines
(`iAndroidLogDrain`). Before that call there is no `crash.log`: the app's
directories are not known during `dlopen`, and the working directory is `/`.

`iAndroidStartup` prints the internal, external and assets paths:

```
tssm: android -- internal /data/user/0/io.github.tssmport.game/files
tssm: android -- external /storage/emulated/0/Android/data/io.github.tssmport.game/files
tssm: android -- assets /storage/emulated/0/Android/data/io.github.tssmport.game/files/assets
```

### One copy of the game

The game claims its save folder as on the desktop (`ClaimSaveFolder` in
`iSystem.cpp`, `iHostClaimInstance`): an `flock` on a lock file named from the
folder, which the kernel drops when the holder ends. On Android the lock files
are in `files/locks`, not the cache, which the system may empty while the game
runs. Within the app there is never a second copy (one process per app, and
SDL ends the process rather than start the game in it twice), so the only
other holder is the co-op app, which takes the same lock through
`SaveShareProvider` ([Saves shared with the co-op app](#saves-shared-with-the-co-op-app)).
While it holds it, the game says "The co-op version of the game is open and is
using your saves. Close it (swipe it away in Recent apps), then start the game
again." and closes before it reads or repairs a save.

When the game ends (Quit in its menu), `GameActivity.onDestroy` ends the app's
process as well, once SDL's thread has finished. The lock lives as long as the
process, and SDL keeps the process after its main returns, so without this the
other app would find the saves in use until Android cleared the process.

A fatal message box during play (a missing level file, out of memory) ends the
process too, with `_exit` once the log is drained (`iAndroidEndProcess`):
`exit()` there ran static destructors and `atexit` handlers under SDL's Java
side and the sound threads, and the app closed through a segfault.

### Saves shared with the co-op app

The co-op version installs beside this app as a second app (its own package,
the same signing key) and plays on this app's saves and game files. The saves
stay in `files/tssm/saves`. `SaveShareProvider` (authority
`io.github.tssmport.game.saves`) is the only way in, and only for an app signed
with this app's key: the manifest guards it with the signature permission
`io.github.tssmport.permission.SHARED_SAVES`, and since `call()` is not covered
by that, every entry point also checks the caller's permission and signing
certificate itself. The co-op app declares the same permission, so the two can
be installed in either order; an app with another key cannot define that name
once either is installed, and if it did so first, ours would not install.

What it offers:

| Call | What |
| --- | --- |
| `hello` | the API version; whether the game has started in this process |
| `list` | every save file (the store's own names only) with size, time and SHA-256 |
| `read <path>` | one of those files |
| `write <path>` | `data`, `backup`, `expect`, `mtime`: written as the game writes (`<name>.tmp`, flushed, renamed over; with `backup`, the old file becomes `backup/<name>.1`) |
| `remove <path>` | `backup`, `expect`: as the Saves tab's Delete |
| `settings` | `config.ini` and the game-files folder used in place |
| `config` | `data`, `expect`: `config.ini` replaced whole, as the game saves it (`config.ini.tmp`, flushed, renamed over); text only |
| `openFile claim rw` | the save folder's lock file, which the co-op app `flock`s and holds while it runs |

The co-op app copies the saves when it starts (holding the lock), plays on the
copy and sends each change back. `write` and `remove` change a file only if it
is the one the co-op app last saw (`expect`, its SHA-256, or empty for none),
and are refused once the game has started in this process; so is `config`,
the co-op app's settings changes, sent over the `config.ini` it started from. Paths are the save
store's names in folders A to J (`SpongeBobMovie01..03`, `.tga`, `.gchead`,
`backup/` copies) and nothing else; anything that would leave the save folder,
including through a link, is refused, as is a file over 256 KB. `SaveFolder`
holds those checks and the writes, without Android classes, and
`android/app/src/test/java/io/github/tssmport/game/SaveFolderTest.java` tests
them on a desktop JVM (the offline Gradle cache has no test framework, so it is
a plain program; its header has the two commands).

## Test launches and benchmarks

A test launch starts the game from adb with test settings for that launch
only, through the launcher's intent extras (`TestLaunch.java`):

```
adb shell am start -n io.github.tssmport.game/.LauncherActivity \
    --es TSSM_MOD mod_SC02 --es TSSM_EXIT_AFTER 19000 --es TSSM_PERF 5
```

- `LauncherActivity` passes its `TSSM_` string extras on to `GameActivity`,
  which sets them as environment variables (`nativeSetenv`) after SDL's
  library is loaded and before `SDL_main` reads any of them.
- Only these keys are taken: `TSSM_MOD`, `TSSM_MODS_DIR`, `TSSM_SAVE_DIR`,
  `TSSM_CONFIG`, `TSSM_LOG`, `TSSM_INPUT`, `TSSM_TEST_PLACE`, `TSSM_TEST_HOLD`,
  `TSSM_EXIT_AFTER`, `TSSM_CAPTURE_FRAMES`, `TSSM_CAPTURE_DIR`, `TSSM_PERF`,
  `TSSM_PERF_CSV`, `TSSM_PERF_PROFILE` and `TSSM_PERF_PROFILE_OUT`
  (`src/SB/Core/pc/iPerf.h`), and the comparison switches `TSSM_GL_VAO`,
  `TSSM_GL_STREAM`, `TSSM_GL_SWAP_CACHE`, `TSSM_VSYNC_PACE`, `TSSM_PERF_HINT`
  and `TSSM_ANDROID_ACCEL` ("Heat, battery and frame drops"), and the GPU's
  `TSSM_GPU_OPT`, `TSSM_GPU_POST`, `TSSM_GPU_FIXEDSIZE`, `TSSM_GPU_LOG`,
  `TSSM_GPU_VERIFY`, `TSSM_GPU_MSRTT` and `TSSM_GPU_MSDISCARD`
  (docs/RENDERING.md). Anything else is logged and ignored.
- They are taken only when `files/perf-test/armed` exists in the app's private
  storage. Only the app and `adb shell run-as` (a debuggable APK) can make it,
  so no other app can steer a launch. Without it the extras are ignored and
  the launch is a normal one.
- Every path key must resolve inside `files/perf-test` (a relative value is
  taken from there); one that would leave it is ignored. A test launch always
  has its own config, saves, mods and log: `TSSM_CONFIG`, `TSSM_SAVE_DIR`,
  `TSSM_MODS_DIR` and `TSSM_LOG` default to `perf-test/config.ini`, `saves/`,
  `mods/` and `tssm-log.txt`. The player's `config.ini`, saves, mods and
  `tssm-log.txt` are never read or written by it (`iAndroidStartup` writes
  the log to `TSSM_LOG` when a test launch names one).
- Nothing persists: the next launch without extras is unchanged. The game
  files are read from where the player put them, as always.

`tools/android_bench.py phone` does a whole run with this: it backs up the
app's files, pulls the installed APK, installs the one under test, makes
`files/perf-test` (a mod per scene whose `sb04.ini` has `BOOT=<scene>` and
`ShowMenuOnBoot = 0`, a config per run with the player's `[video]` size,
anti-aliasing, frame rate and vsync copied, empty save folders), runs each
scene with scripted play, samples the temperatures every 15 s, collects the
metrics, then removes `files/perf-test`, reinstalls the APK from before and
checks that the player's files are byte-identical. It waits while the screen
is off or locked or the player's own game is running. `tools/android_bench.py
pc` runs the same scenes and scripts on the desktop.

The metrics (`TSSM_PERF=<seconds>`, every platform) are per frame: the game
thread's wall and CPU time (`CLOCK_THREAD_CPUTIME_ID`) in its update, render,
present, swap and pacing phases, the GPU time from `EXT_disjoint_timer_query`
(read only when ready, never waited on), passes, draws, program binds and
uploads, the frame interval and the vsyncs it missed. A summary line goes to
the log every `<seconds>`, with the busiest threads; the CSV goes to
`TSSM_PERF_CSV`. `TSSM_PERF_PROFILE=<hz>` samples the game thread's stack on
a SIGPROF timer on its own CPU clock, and `tools/android_bench.py symbolize`
names the addresses from the unstripped `libmain.so` of the debug build
(`android/app/build/intermediates/cxx/RelWithDebInfo/*/obj/arm64-v8a`).

## How the platform pieces work

### Entry point

`SDLActivity` loads `libmain.so`, finds `SDL_main` with `dlsym`, and runs it
on its own thread.

- `GameActivity.getLibraries()` returns `{"main"}`. SDL is static, so there is
  no `libSDL3.so`.
- SDL's Java half is compiled from `third_party/SDL/android-project`, so both
  halves move with the submodule.
- `iAndroidMain.cpp` defines `SDL_main` with default visibility. It calls
  `iAndroidStartup`, then `tssmGameMain`.

### Startup in two halves

`src/SB/Core/pc/android/iAndroid.cpp`:

- `iAndroidOpenLog` runs from the `StartupBanner` static constructor in
  `tssm_main.cpp`, during `dlopen`, before the banner prints. It `dup2`s a
  pipe onto fds 1 and 2 and starts a thread that writes each line to logcat
  and the log file.
- `iAndroidStartup` runs at the top of `SDL_main`, once SDL's JNI is up. It
  sets the orientation and back-button hints, asks SDL for the app's
  directories, opens the log file, and sets `TSSM_ASSETS` to
  `<external>/assets` if that directory exists and `TSSM_ASSETS` is not
  already set.

### Host seam

`iHostPosix.cpp` has `__ANDROID__` arms. They read three environment
variables that `iAndroidStartup` sets from SDL:

| Variable | SDL call |
| --- | --- |
| `TSSM_ANDROID_INTERNAL` | `SDL_GetAndroidInternalStoragePath` |
| `TSSM_ANDROID_CACHE` | `SDL_GetAndroidCachePath` |
| `TSSM_ANDROID_EXTERNAL` | `SDL_GetAndroidExternalStoragePath` |

`iHostExeDir` and `iHostUserDataDir` return the internal directory,
`iHostTempDir` the cache directory, `iHostRunDetached` false, and `iHostName`
`"android"`. `iHostErrorBox` is in `iAndroid.cpp`: it logs at ERROR and shows
`SDL_ShowSimpleMessageBox`.

### Arena below 4 GB

The game allocator addresses its 64 MB arena as a `U32`. `iHostReserveLow`
in `iHostPosix.cpp` tries `mmap` hints from `0x04000000` upward in 64 MB
steps, with `MAP_FIXED_NOREPLACE`. On armeabi-v7a every address is below
4 GB anyway.

### GLES and the GL context

As treedome: librw's GL3 backend runs on GLES (ES shader preamble, DXT
decompressed on the CPU without S3TC, RGBA im2d colours). `GameActivity`
lists every configuration change in `android:configChanges`, so rotation,
keyboard, density (folding, the Display size setting, DeX, an external
display), colour mode, SIM and touchscreen changes do not recreate the
activity; librw has no path to rebuild its rasters, buffers or shaders if
the EGL context is lost. A change left off that list would make SDL end the
process without saving, since `SDL_HINT_ANDROID_ALLOW_RECREATE_ACTIVITY` is
not set.

From 0.9 librw gives each model and each immediate-mode stream a vertex
array object of its own on a GLES 3.0 context (`tools/librw/perf11_cpu.patch`;
on a 2.0 context it sets the attributes per draw as before). The touch
controls and the overlay bind their own, and librw forgets its cached binding
after them.

If Android takes the EGL context anyway (it may from a backgrounded app),
SDL makes a new one on resume and posts `SDL_EVENT_RENDER_DEVICE_RESET`.
`iWindowPump` logs it, shows a dialog saying the game must be started again,
and closes the game through its exit path.

The virtual screen's GL objects -- the explicit anti-aliasing buffers (or,
with `TSSM_GPU_MSRTT=1`, the render-to-texture ones and their depth
texture), the view of the picture the glow reads -- are made with the
virtual screen and go with it; a lost context ends the game as above,
so none of them has to be made again.

The crash handler is installed from a static constructor, which runs during
`System.loadLibrary` on Android's UI thread, but the game runs on SDL's own
thread. `SDL_main` (`android/iAndroidMain.cpp`) therefore calls
`iAndroidAdoptGameThread` first: the game thread gets its own alternate signal
stack, so a stack overflow on it is reported, and it is the thread the report
calls "the main thread" and the watchdog samples.

0.2's menus had no backgrounds or panels, the text of earlier screens piled
up on the picture, and the bubbles smeared into white columns. The cause was
the gl3 backend, desktop OpenGL included: the 2D quads' camera z came out
negative and the quads were clipped away (docs/RENDERING.md, "OpenGL and
OpenGL ES"). 0.3 has the fix, and a DXT texture decoded without S3TC now
keeps its top level's alpha kind. Both were checked on a desktop OpenGL ES
3.2 context with `TSSM_GL_NO_S3TC=1`, the phone's texture path; not on a
phone's GPU.

### Sound in the background

Up to 0.6 the sound could stay off after the app had been in the
background. SDL pauses the AAudio stream when the app goes to the
background (`Android_PauseAudio`, since `SDL_HINT_ANDROID_BLOCK_ON_PAUSE` is
on) and starts the same stream again when it comes back. If that start
fails, SDL only records the error (`ResumeOneDevice` in
`third_party/SDL/src/audio/aaudio/SDL_aaudio.c`). The device then never asks
for audio again, and no event says so. If AAudio reports the stream
disconnected and SDL's one attempt to rebuild it fails, SDL keeps Android's
one playback device as a "zombie" that plays nothing and cannot be opened
again. Only closing every device, so SDL's audio subsystem drops to zero,
brings a working device back.

`src/SB/Core/pc/iAudioSDL.cpp` does that for the mixer and the movie device
(src/SB/Core/pc/PORTING.md):

- Every device closes on `SDL_EVENT_WILL_ENTER_BACKGROUND`. The game thread
  sends that event inside `SDL_PumpEvents`, just before SDL pauses the audio
  and blocks. Every device then opens from scratch on
  `SDL_EVENT_DID_ENTER_FOREGROUND`. Nothing plays while the app is hidden,
  and a route change in the meantime (headphones, Bluetooth) is picked up
  on the way back.
- In the foreground the devices are also restarted after a removed playback
  device (`SDL_EVENT_AUDIO_DEVICE_REMOVED`, after a disconnect SDL could not
  recover from) and after a second in which a device took no audio.
  AAudio and OpenSL ES have Android's one default device and nowhere to move
  it to, so every removal still restarts here. (On Windows, macOS, PulseAudio
  and PipeWire SDL moves the game's devices to the new default itself, and
  since 0.9 a removal there restarts only if, two seconds later, no device is
  left or SDL still has the game's sound on the removed one; see
  `iAudioSDL.cpp`.)
- A restart with no device to open is retried after 1, 2, 4, 8, 16 and then
  every 30 seconds. Meanwhile the game and a movie keep time without sound.
- During a movie the same happens, and the movie keeps its place: the game
  thread is held while the app is away, the movie's sound waits on a
  stand-in queue, and on the way back both devices open again and the movie
  goes on where it was. Since 0.9 the movie's device is closed before its
  stream is destroyed, which a desktop needs (SDL could otherwise deadlock if
  it moved the device at that moment); Android never moves devices, so this
  changes nothing here. This was checked against SDL's Android code and with
  a simulation on Windows (`TSSM_TEST_AUDIO_RESTART=movie+2500@bg~1500`,
  which pushes the background and foreground events on the game thread and
  holds it as block-on-pause does); AAudio's own pause and resume are not
  part of that simulation.

The log (`tssm-log.txt`) has a line `tssm: audio: restarting the audio
devices (...)` for each restart, with its reason. SDL does not request
audio focus, so another app's sound plays alongside the game's rather than
stopping it. Checked on Windows with SDL's disk driver standing in for
AAudio (one default device, no hotplug), and on a Galaxy S25 Ultra: Home during
a logo or story movie, then back, 11 runs of the fixed build and 2 of 0.8. All
13 came back with the sound and the picture running, so the check shows the fix
does no harm; it does not show a cure, since 0.8 passed it too.

### Saves and the file picker

Up to 0.7.1 an export could leave an empty file. The save picker
(`ACTION_CREATE_DOCUMENT`) makes the document and gives the game a
`content://` URI for it, whose last part is the storage app's id for that
document (`.../document/1000004321` in Downloads), not a file name. The
Saves tab added `.zip` or `.gci` to an answer without a dot, as it does to a
desktop file name, and so wrote to a URI nobody had made or granted. The
document made stayed empty. The open then failed in a way SDL 3.4.14 does
not check: `SDLActivity.openFileDescriptor` catches only
`FileNotFoundException`, and Android refuses a URI it did not grant with
`SecurityException`, so C got fd 0 back with the exception still pending.
fd 0 is `/dev/null` in the app, so the save went there, the tab said it was
exported, fd 0 was closed, and the next call into Java on the game's thread
aborted the app (the APK is debuggable, so CheckJNI is on). SDL's picker also
sends no suggested name, and asks for any type when Android does not know the
extension (`.gci`), so a `.gci` export got no extension unless one was typed.

Now (`src/SB/Core/pc/android/iAndroidDocument.cpp`, and `GameActivity.java`):

- The answer is used as it is (`WithExtension` in `gui/iGuiSaves.cpp` leaves a
  URI alone).
- `GameActivity.showSaveDocument` opens the picker with `EXTRA_TITLE` set to
  the suggested name, typed `application/zip` for a zip and
  `application/octet-stream` for a `.gci` (a type storage apps keep the typed
  name for). If it cannot open, SDL's picker is used as before.
- `GameActivity.writeDocument` writes the document (`"wt"`, or `"w"` where a
  storage app has no `"wt"`), checks its size where the storage app can say,
  and closes it in Java after the data is in. `readDocument` reads one for
  Import. Any exception comes back to C, is cleared there, and is shown in
  red on the Saves tab as `Could not write <uri>: <what Java said>`; nothing
  is left pending and fd 0 is untouched.

Checked on a phone with a test-only storage app standing in for the picker
(it refuses a URI it did not make with `SecurityException`, as Android does).
The picker itself needs a person's hands.

## Known issues

- Played on one phone so far, a 64-bit Galaxy S25 Ultra (`arm64-v8a`). The
  32-bit APK (`armeabi-v7a`) is build-checked only: it has not run on a device.
- The overlay's folder choosers (the Settings tab's folders, the Mods tab's
  single mod folder) do nothing on Android: SDL has no folder picker there.
  File pickers (save import and export) work.
- 32-bit ARM faults on some unaligned multi-word loads (`LDRD`, `VLDR`) that
  x86 forgives, and 64-bit kernels do not fix them up for 32-bit apps. If the
  armeabi-v7a build dies with `SIGBUS`, that is the likely cause; the log's
  crash stack says where.
- GL3 has no recovery if the driver loses the EGL context: the game says
  so and closes (above).
- Using a folder in place needs All files access, which Google Play does not
  grant to games. The APKs are signed with a debug key and not meant for a
  store.
