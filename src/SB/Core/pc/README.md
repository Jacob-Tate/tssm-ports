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
| `TSSM_BUILD_32BIT` | OFF | 32-bit build; needs a fresh build directory and the x86 MSVC environment |
| `TSSM_RENDER_BACKENDS` | `D3D9;D3D11;GL3` (Windows), `GL3` elsewhere | also `VULKAN`, or `NULL` alone for headless |
| `TSSM_INPUT_BACKEND` | `sdl` | or `null` |
| `TSSM_AUDIO_BACKEND` | `sdl` | or `null` |
| `TSSM_SDL` | `auto` | `system` / `vendored` (`third_party/SDL`) |
| `TSSM_GUI` | ON | the Dear ImGui launcher and in-game overlay (`third_party/imgui`, `docs/GUI.md`) |
| `TSSM_FFMPEG_ROOT` | none | an FFmpeg install to link instead of searching; on Android a folder of `<abi>/` installs |
| `TSSM_D3D11_DEBUG_LAYER` | OFF | Debug builds: librw asks Direct3D 11 for its debug layer. Needs Windows' optional Graphics Tools feature; without it the D3D11 device does not open, which is why it is off |
| `TSSM_LIBRW_PATCHES` | ON | apply the port's librw fixes (`tools/librw/*.patch`) to `third_party/librw`; needs git. `docs/RENDERING.md` |

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
mouse_sensitivity, rumble, touch_controls, touch_drag_look, touch_scale,
touch_opacity, test`, `[audio] master_volume,
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

## Online co-op

Two players, each with their own copy of the game and its files, can play in the
same level over the internet or a home network: one hosts (the overlay's
**Co-op** tab, `[coop] mode = host` or `tssm --host[=port]`), the other joins by
the host's numeric address and join code (`tssm --join=203.0.113.7:2171`). The
host forwards UDP port 2171 on their router. Only game state travels, never
game files. `docs/MULTIPLAYER.md` is the design and the guide; `config.ini`'s
`[coop]` section and `TSSM_COOP`, `TSSM_COOP_PORT`, `TSSM_COOP_ADDR`,
`TSSM_COOP_NAME`, `TSSM_COOP_PASSWORD`, `TSSM_COOP_BIND=loopback`,
`TSSM_COOP_FOLLOW=0|1`, `TSSM_COOP_RECONNECT=0|1`, `TSSM_COOP_LOG=<n>`,
`TSSM_COOP_NETSIM=<delay_ms,jitter_ms,loss_pct>` and
`TSSM_COOP_SYNC_CAPTURE=<every_ms>:<count>` set it up from outside (the last
three for tests). In the same level the host's enemies, moving platforms,
breakable objects and pickups are the guest's too, the guest's hits count on
the host, the host leads the way from level to level (both start each level
together) and its cutscenes play on both. Each player picks their character
(LB or Z in play, or the Co-op tab) and costume (`[coop] character`,
`costume_spongebob`, `costume_patrick`). A guest whose connection drops joins
again by itself for a minute (`[coop] reconnect`), and both see the
connection's quality on screen (`[coop] quality`). `docs/HOSTING.txt` and
`docs/JOINING.txt` are the two players' guides (plain text, for the release).
With none of them, co-op is off and nothing of it runs.
`tssm_nettest` tests the network code on its own and `tools/coop_test.py` runs
two copies on one PC through 127.0.0.1 (`--scene BB02|DE01|TT01|PT01`,
`--variant netsim|wrong-password|wrong-version|wrong-files|no-answer|kick|blackout|
follow|levels|look|...`, `--guest-exe` for the 32-bit build).

## Unattended runs

For unattended runs (`iCapture.h`): `TSSM_CAPTURE_FRAMES=<n,a-b,a-b/step>`
writes those presented frames as `frame_NNNNN.png` into `TSSM_CAPTURE_DIR`, and
`TSSM_EXIT_AFTER=<n>` exits with code 0 after n frames, and
`TSSM_INPUT=<frame:button[+button][/frames],...>` plays port 0 from a script
(e.g. `400:start,1000:a`; real controllers are ignored while it is set);
`TSSM_TEST_WARP=<frame:type,...>` emits screen warps at the player,
`TSSM_TEST_PLACE=<frame:x:y:z[:yaw],...>` puts the player down at a spot
(`TSSM_TEST_PLACE_CAMERA=reset` brings the camera along),
`TSSM_TEST_SCENE=<frame>:<SCEN>` goes to another level,
`TSSM_TEST_KILL=<frame>:<radius>` defeats the nearest enemy,
`TSSM_TEST_FACE=<first>-<last>/<every>:<id>:<dist>` keeps putting the player a
step from an entity, facing it, and `TSSM_TEST_HEALTH=<frame>:<n>` sets the
player's health (co-op's fight tests),
`TSSM_TEST_STALL=<frame>:<seconds>` stops the game thread for a while (as a
held window does; co-op's stall tests),
`TSSM_TEST_BLACKOUT=<frame>:<seconds>` cuts co-op's connection for a while and
`TSSM_TEST_COOP_KICK=<frame>` kicks the guest (co-op's drop and kick tests;
`TSSM_TEST_COOP_BUILD_SALT` and `TSSM_TEST_COOP_ASSET_SALT` make a copy's
version or game files differ),
`TSSM_TEST_EVENT=<frame>:<id>:teleport|cutscene|run` sends a level's object
(a portal, a cutscene, a fly-through) its own event (co-op's level change and
cutscene tests; a teleport, like `TSSM_TEST_SCENE`, waits until the game is in
plain play, with no pause or task screen open, as a portal can only be walked
into then), `TSSM_TEST_COSTUMES=all` offers co-op every costume and
`TSSM_TEST_COOP_LOOK=<character>,<sb>,<pat>` starts with those co-op choices,
and
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
`tssm_modtest`, `tssm_sndtest` on boot.HIP and mnuc.HIP, `tssm_widentest`,
`tssm_nettest`) and
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
