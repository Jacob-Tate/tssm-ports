# `src/SB/Core/pc`: the PC platform layer, and where each file came from

This is the host counterpart of `src/SB/Core/gc`: the same `i*` interfaces the
game code calls, implemented for Windows (first), then Android arm64, Linux and
macOS. It lives only on the port branches, which are never merged into the
decompilation (`main`); the port branches take the decompilation in.

It is built from three sources:

- **treedome**, the BfBB PC port (CC0; its source is linked in the README's
  credits), branch `treedome`, commit `ae391db`. Its game-independent layer is taken as it stands, with the BfBB
  names renamed (`BFBB_` to `TSSM_` in environment switches and macros, `bfbb:`
  to `tssm:` in the log, the window title). `tools/pcimport.py` holds the rename
  table and the file list, and `tools/pcimport.py --check` re-derives every such
  file from a treedome checkout and names any that differ.
- **TSSM's own `src/SB/Core/gc`**, wherever a module has nothing GameCube in
  it. Those copies are byte-identical and their hashes are in `VERBATIM.txt`.
- **new code** for what is TSSM-only.

The rules are treedome's (its `docs/PCPORT.md`), and the one that matters most:
**the GameCube build stays byte-identical.** Nothing in this directory is on
the GameCube include path, and an edit to shared source (`include/`,
`src/SB/Core/x`, `src/SB/Game`) goes in only once the GameCube build
(`configure.py`, `ninja`) still makes the DOL with the retail sha1.
Port-specific behaviour stays here wherever it can.

## How the build finds this layer

`CMakeLists.txt` puts these first on the include path, in this order:

    src/SB/Core/pc/compat   shadows: types.h, MSL and Dolphin paths, C library extras
    src/SB/Core/pc          the platform layer
    src/SB/Core/pc/rwsdk    the RenderWare headers with the PC layouts
    include, include/bink, include/inline, src/SB/Core/x, src/SB/Game

`include/rwsdk`, `src/SB/Core/gc`, MSL and the Dolphin SDK are never on it. A
game file's `#include "iTime.h"` therefore finds `pc/iTime.h`, and xWad1.cpp's
`#include "iCamera.cpp"` finds `pc/iCamera.cpp`.

The game code is compiled as the same 35 units the GameCube build compiles
(the wads and the standalone units, read out of `configure.py`), with
`SBPrefix.h` forced in first as the console's precompiled prefix is. The five
`SB/Core/gc` units of `configure.py` are replaced: `gc/iWad.cpp` by
`pc/iWad.cpp` (the same unity build, over the modules copied from gc), and
`iTRC`, `iException`, `iScrFX` and `iARAMTmp` by their files here.

## Origins

### treedome, renamed only (checked by `pcimport.py --check`)

| Area | Files |
|---|---|
| compat | `compat/_stdfloatmath.h`, `compat/cmath`, `compat/intrin.h`, `compat/math.h`, `compat/mem.h`, `compat/string.h`, `intrin.cpp` |
| host seam | `iStub.h` (`iHost.h` and its backends: adapted, below) |
| window, render size | `iWindow.h`, `iWindowSDL.cpp`, `iWindowNull.cpp`, `iScreen.h`, `iScreen.cpp` |
| config.ini | `iConfig.h`, `iConfigEdit.h`, `iConfigEdit.cpp`, `iConfigTable.h` |
| input devices | `iPadStick.h`, `iPadStick.cpp`, `iCameraLook.h`, `iCameraLook.cpp`, `iTouchOverlay.h`, `iTouchOverlay.cpp` (the rest of treedome's input layer is adapted, below) |
| audio device | `iSndReverb.h`, `iSndReverb.cpp` (the seam and its backends are adapted, below) |
| movies | `iFMVAudio.h`, `iFMVAudioNull.cpp`, `iFMVDecoder.h`, `iFMVDecoderFFmpeg.cpp`, `iFMVDecoderNull.cpp`, `iRasterFill.h` |
| screen passes, frame capture | `iGlow.h`, `iDistort.h`, `iSnapshot.h`, `iFrameGrab.h`, `iSaveThumb.h` (implementations in `rw/`) |
| misc | `iBoot.h`, `iBoot.cpp` (config.ini's `game.boot`), `iFX.cpp` (the animated-UV pipeline; TSSM's `gc/iFX.cpp` is the same two functions on GX) |
| RenderWare C API on librw | everything in `rw/` (shim, `layout*.cpp` asserts, screen passes `glow.cpp`/`distort.cpp`/`snapshot.cpp`/`framegrab.cpp`/`savethumb.cpp`, compiled shaders, `rw/tests`, `rw/README.md`, `rw/TODO.md`) except the three below |

### treedome, adapted for TSSM

| File | What changed |
|---|---|
| `rw/engine_start.cpp`, `rw/stream.cpp`, `rw/ptank.cpp` | TSSM's RenderWare is 3.7: `RwMemoryFunctions` and `RwMalloc`/`RwRealloc` take a memory hint. Marked `TSSM-ADAPTED`. |
| `rw/engine_start.cpp` | also fills `RwGlobals::dOpenDevice` with the shim's render-state and Im2D/Im3D primitive functions: `xRenderStateSetAlphaDiscard` calls `fpRenderStateGet` through the table; installs `iDeviceReset.h`'s guard once the D3D9 device is up and removes it in `RwEngineStop`; OpenGL: `TSSM_GL_NO_S3TC=1` takes the no-S3TC (phone) texture path, `TSSM_GL_DEBUG=1` prints the driver's KHR_debug errors (`docs/RENDERING.md`) |
| `rw/renderstate.cpp` | `RwRenderStateGet` answers the alpha-test function and reference (read back from librw) |
| `rw/engine.cpp` | `RwGameCubeSetAlphaCompare` collapses a test written the same on both sides, which is how TSSM's alpha discard writes it |
| `rw/texture.cpp` | mods: `gRwTextureReplaceHook`, called for each texture `RwTexDictionaryStreamRead` reads (after the raster conversion), which `iModTexture.cpp` sets so a mod's image can replace the raster. Marked `TSSM-ADAPTED`. OpenGL: a converted texture keeps the alpha kind (opaque, keyed, graded) of its top level, which librw's per-level `rasterFromImage` otherwise takes from the last level (`docs/RENDERING.md`) |
| `iConfig.cpp` | the first line of a written config.ini names TSSM |
| `iConfigTable.cpp` | the settings of BfBB features not imported are removed (toon, hipoly, world lighting, day/night, fonts, cheats, fixes, Xbox screen-pass switches, menus, soundtrack, load-screen and shadow tuning, button glyphs, `video.profile`, `video.draw_distance`, `video.per_pixel_lighting`); help texts name TSSM's folders; TSSM's `[input]` settings: `preset`, `layout`, `button_icons`, `keyboard`, `stick_curve`, `deadzone`, `trigger_threshold`, `camera_sensitivity`, `camera_invert_x/y`, `mouse_camera`, `mouse_sensitivity`, `rumble`, `touch_controls`, `test`; `[assets] mods` and `mods_folder` (the mods folder) are added |
| `iPadBind.h`, `iPadBind.cpp`, `iPadBindTable.cpp` | TSSM's rows, named after the Xbox controller (`a b x y lt rt white black back start`, the d-pad, and the keyboard's `move_*`/`camera_*`/`walk` rows), each setting the bits the Xbox release sets; presets `xbox`, `playstation`, `switch`, `gamecube` (Z chords as on the console); a quiet parser and a formatter for a settings screen. docs/CONTROLS.md |
| `iPadLayout.h`, `iPadLayout.cpp` | TSSM's move layout: `input.layout` xbox (the Xbox release's move buttons) or gamecube, as `XPAD_MOVE` in `xPad.h` reads it |
| `iPadHost.h`, `iPadHostSDL.cpp`, `iPadHostNull.cpp` | raw sticks (the curve is `iPad.cpp`'s), analog triggers kept and clicking at `input.trigger_threshold`, the keyboard alongside a pad (`input.keyboard`), kinds `xbox`/`playstation`/`switch`/`gamecube`, rumble strength, the bindings and their generation for the prompts; the `TSSM_PAD` probe became `TSSM_INPUT_TEST` (`iPadTest.cpp`); the null backend also stands in for the SDL-only prompts and input test; while another copy of the game is the window in front, the pads read as connected and idle (`iHostOtherCopyInFront`), since SDL hands pad input to a window that is not in front |
| `iPadKeyboard.h`, `iPadKeyboardSDL.cpp` | stick rows instead of fixed WASD/IJKL, mouse buttons and wheel as keys, the mouse camera (relative mode, as C-stick deflection), a test source |
| `iPadTokens.h`, `iPadTokens.cpp` | share/capture, paddles, touchpad and the GameCube trigger clicks; `mouse1`-`mouse5`, `wheelup`, `wheeldown`; labels per controller family |
| `iPadTouch.h`, `iPadTouch.cpp` | TSSM's small buttons (Back, black); the drag also turns TSSM's camera through the C-stick (TSSM's `zCamera.cpp` has no `iCameraLook` hook) |
| `iConfig.cpp` | also: the `[pad]`/`[keyboard]` comments and row descriptions are TSSM's (`iPadBindDescribe`) |
| `iFile.h`, `iFile.cpp` | TSSM's interface: `const` names in `iFileLoad`/`iFileSetPath`, `iFileFind`'s flags argument, `iFileExists`, `iFileGetSectorSize`, `xFileDriveType`; backslashes become slashes in `iFileFullPath` (gc does it); the PLAT check accepts TSSM's Xbox id `BX`; the asset-override hook (BfBB's code menus) is gone. Mods (`iMod.h`): `iFileInit` starts the mod manager; a loose file from the mods folder is asked for after the single mod folder (same platform check); a package a mod replaces assets in is opened as a view (`tag_iFile::view`, read through `iModPkgRead`); `iFileLoad` applies INI overlays to the `.ini` files it loads |
| `iTime.h`, `iTime.cpp` | TSSM's `iTimeGetGame`, `iFuncProfileFuncs`; the tick rate is also `OS_TIMER_CLOCK`, which `xFont.cpp` and `zGame.cpp` use inline |
| `iPad.cpp` | TSSM's interface; TSSM's pulse-train rumble (`iPadStartRumble(pad, intensity)`, `iPadUpdateRumble`) on `iPadHostRumble`; the B+X+Start in-hand reset is not carried; `input.stick_curve` (the Xbox release's curve by default, treedome's GameCube octagon, or linear), camera sensitivity and inversion, the mouse's camera turn; calls the input test and the prompts once a frame for port 0 |
| `iDraw.cpp` | TSSM's `iDrawSetFBMSK(abgr, setMask, plusFrame2)` with `lastSetFBMSK`; `iDrawSetDisplayOffset` (PAL VI origin) a no-op; `iXFBreset`/`iXFBalloc` lend a heap block instead of the console's spare frame buffer |
| `iFMV.cpp` | TSSM's `iFMVPlay` signature (subtitle asset id, not drawn yet); the Xbox movies are `FMV/*.bik`, decoded by FFmpeg (treedome's `.xmv` rename is gone); a movie with no file reports finished (5 table entries have none on the Xbox disc); `game.intro_movies = off` skips the four boot logos; a mod's movie (`iModFindMovie`) is tried first; the game's sounds are paused while a movie plays (`xFMVPauseSound`, and the ostrich set to `PlayingMovie`), as gc/iFMV.cpp does: the end-of-level story movies are started by a level script with the level's sounds still live, and the mixer runs on its own thread; `iFMVOpen` and the rest are stubs (below) |
| `isavegame.h`, `isavegame.cpp` | TSSM's interface (`iSGCheckForCorruptFiles` returns `U8`, `iSGIsGameCorrupt` added); save files are `SpongeBobMovie01..03` as on the console, and hold the console's payload byte for byte: the game's buffer is converted to the console's byte order on the way out and back on the way in (`iSavePayloadConvert`), written through the save store (atomic, with backups); the save folder is `iSaveStoreRoot`'s; `iSGIsGameCorrupt` checks the payload header; at startup every slot is checked and one a crash left unusable is put back from the whole temporary or the newest good backup (`iSaveStoreRecover`), and `iSGCheckForCorruptFiles` reports nothing (the boot screen it led to was black); `iSGOnlyTargetWithGames` lets the load screen skip the card choice when one folder holds saves, and `iSGSelectGameDir` calls the no-saves hook (`iSGSetNoGamesHook`) for an empty folder (docs/SAVES.md) |
| `iHost.h`, `iHostWin32.cpp`, `iHostPosix.cpp` | `iHostSetFileTime` and `iHostCommandLine` added, for the save tools; `iHostFlushFile` and `iHostFlushDir`, for crash-safe saves; `iHostClaimInstance`, `iHostRaiseProcessWindow` and `iHostOtherCopyInFront`, for one copy of the game per save folder and its log; `TSSM_GAME_TITLE` for every window and message box |
| `iSndHost.h`, `iSndHostSDL.cpp`, `iSndHostNull.cpp` | the audio seam gains a reverb send per voice (the reverb runs on a send bus, as FMOD applied it per channel, instead of on the whole mix), a grouped pause (a stereo pair starts on one frame) and fed voices (a ring the game thread fills: cutscene tracks and streams); the SDL backend's no-device clock is per voice, so a voice started after a gap between updates is not advanced by time from before it; the SDL device closes and opens again through `iAudioSDL.h` (background, device removed, a device that stops asking for audio for a second). Marked `TSSM-ADAPTED` |
| `iFMVAudioSDL.cpp` | the movie device closes and opens again with the game's (`iAudioSDL.h`); while it is closed the movie keeps time on a stand-in queue drained at its rate; a queue that stops moving for a second asks for a restart. The device is closed before its stream is destroyed: destroying a stream that is still bound makes SDL unbind it in `SDL_UnbindAudioStreams`, which unlocks the wrong device if SDL moves the device to a new default meanwhile, and the game froze in a movie that way. Marked `TSSM-ADAPTED` |
| `iSndData.h`, `iSndData.cpp` | treedome's sample cache (lazy, LRU, pinned while playing), keyed by asset id and package; the bytes come from a package range iSnd.cpp names (from the package's own table of contents), decoded by `iSndXbox.cpp`; no soundtrack override; `TSSM_SND_DUMP` |
| `tssm_main.cpp` | `bfbb_main.cpp` renamed: crash handler, watchdog, unbuffered stdout, banner; starts the log's tee before anything prints (`iLog.h`), and the crash and abort reports are appended to `crash.log` as well (formatted on the stack) before the log is drained |
| `iLog.h`, `iLog.cpp` | the game's own log: stdout and stderr each go through a pipe whose thread writes to where they went before and to `last_run.log` (the run before kept as `last_run.prev.log`) in `%APPDATA%\tssm`, or beside `TSSM_SAVE_DIR`'s folder, or at `TSSM_LOG` (`off` for none); held in memory until `iSystemInit` knows the run is the game and not a save tool; one process per log file; `crash.log` beside it |

### TSSM `src/SB/Core/gc`, verbatim (`VERBATIM.txt`)

`iAnim.h`, `iAnim.cpp`, `iAnimSKB.cpp`, `iCollide.h`,
`iCollideFast.h`, `iColor.h`, `iCutscene.h`, `iDebug.h`, `iEnv.h`, `iEnv.cpp`,
`iFX.h`, `iLight.h`, `iLight.cpp`, `iMath.h`, `iMath.cpp`, `iMath3.h`,
`iMath3.cpp`, `iModel.h`, `iMorph.h`, `iMorph.cpp`, `iParMgr.h`, `iParMgr.cpp`,
`iScrFX.h`, `iAsync.h`, `iARAMTmp.h`, `iMemMgr.h`, `iPad.h`, `ngcrad3d.h`.

The `.cpp` copies are compiled inside `pc/iWad.cpp`, as on the console, because
they lean on `iWad.h` for their includes.

### TSSM `src/SB/Core/gc`, adapted

| File | What changed |
|---|---|
| `iWad.h`, `iWad.cpp` | the MSL, Dolphin and AX includes gone; the unity build holds only the modules copied from gc |
| `iCamera.cpp` | `iCameraPerformRenderWareHack` does nothing: it enlarges the camera's RpWorld extension, which librw does not register (the plugin offset would be -1). Compiled inside xWad1.cpp, as on the console |
| `iModel.cpp` | the `interpolator` write (librw's atomic has none), the 10-bone skin split (librw has no palette limit, no `RtSkinSplit`) and the resource-arena purge (librw has no arena) are `#ifndef PLATFORM_PC` |
| `iCutscene.cpp` | `DVDCancelAllAsync` becomes `iFileReadStop`; `iCSSoundPull` added at the end (the PC's form of `iCSSoundGetData`: the game thread takes each block of a cutscene track as soon as its chunk is read, and never re-reads the Play chunk for a late Stream chunk); a cutscene loaded frame by frame waits 0.63 s of game time before its first read, the console's disc seek, because the game relies on the scene updating that long (Neptune's last hit only stops his looped dizzy sound 0.1 s later; Ready sooner, the loop played through the credits). `zCutsceneMgrFinishLoad`'s blocking load runs through the wait |
| `iScrFX.cpp` | the lens-flare occlusion test (GX pixel metrics, draw tokens) is `#ifndef PLATFORM_PC`; on PC it reports the flare hidden |
| `iSnd.h` | `iSndGroupHandle` is a `U32` (an empty enum holding a pointer on the console; C++ forbids the cast). 32 bits because it shares a union with a `U32` in two asset structs. `iSndInfo` keeps only `channel` and `iSndFileInfo` keeps `uAssetID`, `uFlags`, `isStreamed()`/`isLooping()` (what shared code reads) over the PC's table indices; the FMOD/FSB/DSP-ADPCM structs are gone. Two PC-only declarations: `iSndEffectShutdown`, `iCSSoundPull` |
| `iFMV.h` | `MOVIE` keeps its Bink handle (xFMV.cpp reads it) and loses the DVD file |
| `iMemMgr.cpp` | TSSM's `MemoryManagerMulti` (fixed pools, resource arena, general heap) on treedome's low-4 GB arena from `iHostReserveLow`; the 4-byte pool is pointer-sized on 64-bit |
| `iSystem.h`, `iSystem.cpp` | includes `<dolphin.h>` (the PC subset, below) as gc's does; TSSM's two-step RenderWare start (`iSystemInit`, then `iSystemInitGraphics`) with treedome's host pieces: config.ini first, the asset check (Xbox `BX` packs), the window before the device, `iVSync` sleeping to 60 Hz; the log opened once the save tools are past (`iLogStart`); the save folder claimed for this process before the window opens (a second copy on the same folder brings the first forward and exits) and the load screen's no-saves hook set; ARAM is host memory; `iSystemGetWidthHeight` answers the render size |
| `iTRC.h`, `iTRC.cpp` | TSSM's 40-function interface minus ROMFont and the OS thread bookkeeping; no disc, no error screen (treedome's answer) |
| `iException.h`, `iException.cpp` | the out-of-memory crash reports on the console and in a message box, then exits |
| `iARAMTmp.cpp` | retail's algorithm with ARAM as a malloc'd buffer and the ARQ DMA as `memcpy` |
| `iAsync.cpp` | see stubs |
| `iAnimSKB.h` | `iAnimSKBHeader::ExtractedData` is an `iPtr32` (`compat/types.h`): 4 bytes on every host, because cutscene time chunks carry SKB1 animations that are evaluated in the stream buffer and cannot be widened. A pointer above 4 GB stored there (zEntPlayer's ledge table, a static) is kept through iPtr32's handle table |

### compat (new)

| File | What |
|---|---|
| `compat/types.h` | `include/types.h` (which has host branches) taken with `include_next`, plus the port's helpers: `UPtr`/`SPtr`, `TSSM_PTR64`, `TSSM_STATIC_ASSERT`, `SHARED_INLINE` (treedome's), and `iPtr32<T>`, a pointer kept in a 4-byte slot of data the game reads in place (`iAnimSKBHeader`, `xCutsceneData`): below 4 GB stored as it is, above it through a handle table (`iAssetWiden.cpp`). Earlier it replaced the shared header outright |
| `compat/ctype.h` | MSL's `_isspace`/`_toupper`/`_tolower` with MSL's meaning (zMain.cpp, zRewardsMgr.cpp) |
| `compat/extras.h` | MSL's `stricmp`/`strnicmp`/`strupr` |
| `compat/PowerPC_EABI_Support/...` | the MSL headers game code names by path, forwarded to the host's (`<new>`, `<stdlib.h>`, `<cmath>`, ...) |
| `compat/PowerPC_EABI_Support/Runtime/MWCPlusPlusLib.h`, `mwcplusplus.cpp` | `__construct_new_array`, which zParticleLocator.cpp calls by name |
| `compat/dolphin.h`, `compat/dolphin/*.h` | **only the SDK calls shared code still makes**, with the SDK's signatures: GX (zAsync's GXDrawDone, xHudImage's constants, and the types xFXHighDynamicRange and xScreenWarp declare data with -- their GX calls are GameCube-only now; the enums forward to `src/dolphin/include/dolphin/gx/GXEnum.h`), MTX (types only), VI, OS and CARD. `pc/iSystem.h` and `pc/isavegame.h` include `<dolphin.h>` as their gc counterparts do. The definitions are in `stubs/`. Each call should still move behind an `i*` function through the gate; a new declaration here to make a shared file compile is the wrong direction |

### rwsdk (merged)

`rwsdk/` is `include/rwsdk` (TSSM's RenderWare 3.7 headers, GameCube macro
forms) three-way merged with treedome's PC arms: `git merge-file` with base
the BfBB decompilation's `include/rwsdk`, ours = TSSM's,
theirs = treedome's (`ae391db`). Headers treedome did not change are TSSM's
copies. Conflicts resolved by keeping both sides, except where treedome's PC
arm already defines the type (`RxVertexIndex`/`RwImVertexIndex`). One semantic
fix the textual merge could not see: TSSM writes `RwRenderStateSet`,
`RwRenderStateGet`, `RwIm2DRender*` and `RwIm2DGet*ScreenZ` as macros through
`RWSRCGLOBAL(dOpenDevice)`; on PC they are declared as functions (the shim
defines them) under `#ifndef PLATFORM_PC`. And `RwIm2DVertexSetRecipCameraZ` stores the
magnitude of the camera z: game code takes it from the near screen z, which is -1 on OpenGL, and a
negative w clipped every such quad (`docs/RENDERING.md`). Added from treedome:
`rpcollbsptree.h` (its `RpCollisionData` removed: TSSM's `rpcollis.h` defines
the RW 3.7 one), `driver/gcn/dlrendst.h`, `world/bageomet.h`. `SHADOWED.txt`
records the TSSM headers' hashes, so a change to `include/rwsdk` is reported.

### new

`iWad.cpp` (unity), `iAsync.cpp`, `iARAMTmp.cpp`, `iException.*`, `iSnd.cpp`,
`iTRC.*`, `mwcplusplus.cpp`, the compat files above, `PORTING.md`,
`SHADOWED.txt`, and from phase 1:

| File | What |
|---|---|
| `common.cpp` | the game's linker-COMMON globals, which no source file defines (`gSnd`, `mPad`, `g_I3`, `sActiveCutscene`, `xqc_def_ctrl`, `anim_coll_old_mt`, `gClimate`, `zcam_backupcam`, `zcam_backupconvers`, `zcam_quat`, `xTransient::space_tree`) |
| `stubs/dolphin_*.cpp`, `stubs/bink.cpp`, `stubs/fmod.cpp` | the definitions of the SDK and middleware calls shared code still makes, one file per module; `stubs/STUBS.md` (generated by `tools/pcstubs.py`) says which are stubs |
| `rw/tssm.cpp` | the RenderWare calls TSSM makes and BfBB does not (20 functions), kept apart so `pcimport.py --check` still compares the shim with treedome |
| `iCapture.h`, `iCapture.cpp` | debug capture for unattended runs: `TSSM_CAPTURE_FRAMES=1,5,100-400/50` writes those presented frames as PNGs into `TSSM_CAPTURE_DIR` (through `iFrameGrabWrite`, before the flip), `TSSM_EXIT_AFTER=n` exits with code 0 after n frames, `TSSM_INPUT=400:start,1000:a/10` holds port 0's buttons from a script (through `iPadHostScript`); a frame hook on `iSnapshotCapture`, installed only when asked for |
| `iTestEvents.h`, `iTestEvents.cpp` | debug events on that hook, compiled with the game units because they call game code: `TSSM_TEST_WARP=1600:shockwave[:radius[:seconds[:intensity]]]` emits a screen warp at the player; `TSSM_TEST_PLACE=2700:-28.6:-8:41.2:90` puts the player down at a spot, facing a yaw in degrees; `TSSM_TEST_HOLD=301-600` puts the player (and camera) back at the latest place every frame of that range, so frames differ only by what animates; `TSSM_TEST_POWERUP=1250:2` sets every move's power-up at that frame; `TSSM_TEST_CAMLOG=1` logs the camera's position every frame and whether a level fly-through is playing (`tools/regress.py`'s fly-through check); `TSSM_TEST_CRASH=<frame>` writes through a null pointer at that frame, for the crash handler and the logs |
| `iTestSend.h`, `iTestSend.cpp` | `TSSM_TEST_SEND=1200:AUTOSAVE_DONE_SCRIPT[:event]`: at that frame, the game's own event (eEventRun when not given) to the scene object with that name, as a link sends it; in SC02, DE02 or GG02 that starts the level's end (its story movie, then the next level); compiled with the game units |
| `iHangDump.h`, `iHangDump.cpp` | `TSSM_TEST_HANG_DUMP=<seconds>`: when the game thread has made no progress (a window pump or a file read) for that long, every thread's stack, named and symbolised (dbghelp `StackWalk64`, `tssm_main.cpp`), goes to the log and the process ends with code 3; while it is set an SDL assertion is logged the same way and ignored, as a release build of SDL does, instead of SDL's message box. Windows only; the heartbeat is in `tssm_rw` |
| `iDecalFilter.h` | `iDecalFilterBegin`/`End` (in `rw/renderstate.cpp`): a projected decal draws from its top texture level with linear filtering, as on the GameCube, whose art has only that level; called under `PLATFORM_PC` from `xShadow.cpp` and `zEntPlayer.cpp` (`docs/RENDERING.md`, "Decals: the top level only") |
| `iHDRGlow.h`, `rw/hdrglow.cpp` | TSSM's HDR glow (xFXHighDynamicRange's GX passes) with camera textures and librw's 2D pipeline; `video.hdr_glow`; `TSSM_HDR_VIEW=mask\|glow` (`docs/RENDERING.md`) |
| `iFloatByte.h` | `iFloatToByte`: a float stored into a byte as the console does it (fctiwz saturates, stb keeps the low byte), for game code under `PLATFORM_PC` whose value can leave the S32 range (the glare in `xScrFx.cpp`, the spotlight flare) |
| `iScreenWarp.h`, `rw/screenwarp.cpp` | xScreenWarp's draw: the frame copied and drawn back through the warp grid; `video.screen_warp` |
| `rw/screenpass.h`, `rw/screenpass.cpp` | what those two share: the frame copy, camera-texture targets, render-state save and restore, a quad, GX blend factors as RenderWare's; not ready while a D3D9 device is lost |
| `iDeviceReset.h`, `rw/devicereset.cpp` | Direct3D 9 device resets kept to the moments they can succeed, by taking over librw's `beginUpdate`, `showRaster` and the D3D9 `rasterLock` (librw itself is unchanged): no reset to an empty or minimised client area or of a device that cannot be reset yet, the overlay's default-pool objects released first, librw's cache of the bound index buffer, vertex streams and declaration emptied at every camera update and present (a reset otherwise binds a destroyed geometry's freed buffer again), the camera-texture reference librw's `rasterLock` keeps given back, Im2D, Im3D, `RwRasterRenderFast`, the present and the screen passes held while the device is lost, and a device lost again right after a reset reset again later; `TSSM_TEST_DEVICE_LOST=present[:presents]` or `=reset[:presents]` simulates a loss (`docs/RENDERING.md`) |
| `iPadSettings.h`, `iPadSettings.cpp` | config.ini's `[input]` settings read once into plain values for the per-frame input code |
| `iPadPrompts.h`, `iPadPrompts.cpp` | keeps the game's button prompts true: the six move prompts follow `input.layout` (TEXT rewritten in place), "Pull"/"Press" follows the device, and the `pad_button*` rasters are swapped for glyphs drawn for the device and binding in use (keyboard keycaps, PlayStation shapes, Switch and GameCube letters; the disc's own Xbox pictures, reshuffled, on an Xbox pad); `input.button_icons` |
| `iPadTest.h`, `iPadTest.cpp` | the input test: `TSSM_INPUT_TEST=1` or `=overlay` (`input.test`) logs what the game reads from port 0 whenever it changes, and draws a controller (GL3/Vulkan) |
| `iPadControls.h`, `iPadControls.cpp` | the binding API for a settings screen (rows, get/set/add/remove/reset, capture, labels, conflicts, `[input]` settings, live state, mouse capture) |
| `tests/input_test.cpp`, `tests/input_test_xpad.cpp` | `tssm_input_test`: SDL virtual controllers (Xbox, DualSense, Switch Pro, GameCube adapter) and a stand-in keyboard and mouse through the game's own `xPadUpdate`; 127 checks |
| `iLowHeap.h`, `iLowHeap.cpp` | RenderWare's heap on 64-bit hosts: a TLSF heap over a block reserved below 4 GB with `iHostReserveLow` (256 MB, `TSSM_RWHEAP_MB`), passed to `RwEngineInit` by `iSystem.cpp`, so RenderWare and librw pointers the game keeps in 32 bits survive as on the console (where RenderWare allocates from `iMemMgr`). 32-bit builds keep the C library's heap; `TSSM_RWHEAP=1` uses this one anyway |
| `iSaveFormat.h`, `iSaveFormat.cpp` | the save formats as data (docs/SAVES.md): the payload's walk and checks, its CRC, canonical <-> host byte order, `.gci`/`.gcs` parse and build, raw card images, the card clock, the port's own banner and icon |
| `iSaveStore.h`, `iSaveStore.cpp` | the save folder (root, slots, atomic writes flushed to the disk, backups, the one-process claim on it, the repair at startup of what a crash left: `iSaveStoreRecover`), import from GameCube files and cards (each save's level and progress reported, even on a look-only pass), export to `.gci`, zip and a Dolphin GCI folder; the interface the in-game Saves tab calls. `iSaveStoreAutoSaveSlot` (the slot the game autosaves to) is defined in `isavegame.cpp`, beside the game's save code |
| `iSaveZip.h`, `iSaveZip.cpp` | stored-entry zips, for every save in one file |
| `iSaveText.h`, `iSaveText.cpp` | the load screen's "no saves" message (`LD NOGAMES TXT`, the Xbox disc's "No game saves present on the Xbox Hard Disk") rewritten in place to name the empty memory card slot, and the one holding the saves |
| `iSaveCLI.h`, `iSaveCLI.cpp` | `tssm --saves`, `--import-save`, `--export-save`, `--check-save`, `--restore-save`, `--delete-save`, run from `iSystemInit` |
| `tests/save_selftest*.cpp`, `tests/save_selftest_prefix.h` | the `save_selftest` program: saves and loads through the game's own `xsavegame`/`xserializer`, round-trips `.gci`, card and zip (CMake target, `ctest`) |
| `iMod.h`, `iMod.cpp` | mods: the mods folder (one folder per mod, `mod.ini`, `mods.ini` load order), the override index (loose files, assets by ID or name and package, textures, sounds, movies, INI overlays), conflicts, the log (`mods.log`), and the API for a mod manager (list, enable, reorder, save, rescan). `docs/MODDING.md` is the modder's guide |
| `iModPackage.h`, `iModPackage.cpp` | a HIP/HOP with some assets replaced, at the file level so the packer, `xTextureManager`, sounds and cutscenes all see the same DICT: in place when the new bytes fit the old slot, otherwise the asset's layer is moved to the end of the stream; SNDI entries of replaced sounds follow |
| `iModIni.h`, `iModIni.cpp` | `mod.ini`/`mods.ini` reading; overlays on the game's INI files (replace, add, remove keys by section) |
| `iModAudio.h`, `iModAudio.cpp` | replacement sounds to 16-bit PCM: WAV here, anything else through FFmpeg when the build has it |
| `iModTexture.cpp` | replacement images for textures by RenderWare name (PNG through librw's lodepng, DDS DXT1/3/5 and RGB(A), TGA, BMP), installed on `rw/texture.cpp`'s hook |
| `tests/modtest.cpp` | `tssm_modtest`: the mod system on its own, checked against real packages (`docs/MODDING.md`, "Testing") |
| `gui/iGui.h`, `gui/iGui.cpp` | the Dear ImGui overlay: its context, F1 / Back+Start / three-finger toggle, the SDL event watch it listens through, `iGuiFilterPad` (no game input while open; buttons held at close stay hidden until released), the panel and audio-listener registrations other modules use. `docs/GUI.md` |
| `gui/iGuiRender.h`, `gui/iGuiRender.cpp` | the overlay's renderer: ImGui's DX9, DX11 or OpenGL 3 backend for the running librw device, drawn after the virtual screen's stretch and before the flip by taking over `rw::engine->device.showRaster` (librw itself is unchanged); D3D9 default-pool objects released before any reset (also through `iDeviceResetSetReleaseHook`); the window capture for `TSSM_GUI_CAPTURE` |
| `gui/iGuiLauncher.cpp` | the launcher: its own SDL window and SDL_Renderer before the game's, for the asset folder (SDL's native folder picker) and the startup-only settings |
| `gui/iGuiSettings.cpp`, `gui/iGuiTabs.cpp`, `gui/iGuiUtil.cpp`, `gui/iGuiInternal.h` | the Settings tab (through `iConfig*`), the About tab and its third-party notices, and what the tabs share (the look, the system's folder and file dialogs, whole-file reads and writes through SDL) |
| `gui/iGuiControls.cpp` | the Controls tab on the input layer's binding API (`iPadControls.h`): every row's controller and keyboard binding (replace, add, remove, type, unbind, default), shared-input marks, the `[input]` settings drawn from the settings table |
| `gui/iGuiFilePicker.h/.cpp` | the built-in file browser that answers the folder and file requests where the system has no dialog (the Switch) or its dialog failed: a controller-, touch- and mouse-driven folder list in a modal popup; saves to a fixed export folder without asking |
| `gui/iGuiSaves.cpp` | the Saves tab on the save store (`iSaveStore.h`): slots with level, progress, date, backups and the autosave slot; import through the system's file dialog (each save found sent to a slot of the player's choosing, replace only when asked, a question before the autosave slot); export of a slot as `.gci`, of every save as a zip or into a Dolphin GCI folder; backups and restore; delete |
| `gui/iGuiMods.cpp` | the Mods tab on the mod system (`iMod.h`): load order with on/off, arrows and drag to reorder, rescan, the global switch, details, conflicts, the log; `[assets] mod` below |
| `gui/iGuiCoop.cpp` | the Co-op tab and the co-op HUD layer (the partner's name tag, join/leave notices). `docs/MULTIPLAYER.md` |
| `gui/iGuiSelfTest.cpp` | `TSSM_GUI_SELFTEST`: the overlay over a test picture through the real present, with an optional window resize and a scripted virtual controller |
| `gui/iGuiNull.cpp` | `iGui.h` with `TSSM_GUI=OFF` |
| `iNetSock.h`, `iNetSockCommon.cpp`, `iNetSockWin32.cpp`, `iNetSockPosix.cpp` | online co-op's UDP sockets on the system's own API (Winsock2, POSIX): dual-stack bind or loopback, oversized and truncated datagrams dropped unread, `SIO_UDP_CONNRESET` off on Windows, numeric-only address parsing (no resolver). `docs/MULTIPLAYER.md` |
| `iNetWire.h`, `iNetCrypto.h`, `iNetCrypto.cpp` | the bounded little-endian reader and writer every network field goes through; SHA-256, HMAC, PBKDF2, HKDF, constant-time compare and the OS random source (BCryptGenRandom, getrandom) |
| `iNetProto.h`, `iNetProto.cpp` | the co-op wire format: the handshake packets, the M1 payload (PLAYER_STATE, PING, PONG, DISCONNECT), M2's SNAPSHOT (host to guest), SNAP_ACK (guest to host) and STALL, and M3's EVENT (the guest's hit and pickup claims, the host's damage and grants), REL_ACK and PICKUPS (host to guest); strict canonical parsing with each direction enforced; INPUT (reserved) refused |
| `iNetSession.h`, `iNetSession.cpp` | the host and guest sessions: stateless cookies, the PBKDF2/HMAC password proof both ways, HKDF session keys, HMAC-tagged packets with a 64-packet replay window, rate limits, wrong-password blocks, kick, keepalives and timeouts; the reliable layer M3's events go through (run by `iCoop.cpp`'s I/O thread) |
| `iCoop.h`, `iCoop.cpp`, `iCoopInternal.h` | the co-op service: `[coop]`, `TSSM_COOP_*`, `--host` / `--join` / `--coop-name`, the I/O thread that owns the socket (started only when a player hosts or joins), the mailboxes between it and the game thread, the build ID, the status and notices for the GUI. `iCoopInit` and `iCoopShutdown` are called by `iSystem.cpp` |
| `iCoopAssets.cpp` | the game-files hash both players must share (SHA-256 over the asset folder's packages and INIs and the mods in use), cached in the save folder (`coop_files.cache`) |
| `iCoopPuppet.cpp` | the game-side half of co-op, compiled with the game units: reads the local player, places the partner (interpolation 100 ms back, short extrapolation, snaps), drives and draws the partner with the idle character model (animation state set, never updated; the player's `BeforeAnimMatrices` callbacks off while it is evaluated), the guest's switch to the other character, the name tag's position. Called from `zGame.cpp` and `zScene.cpp` (one `PLATFORM_PC` line each) |
| `iCoopWorld.cpp` | co-op's shared world (M2), compiled with the game units: the level's table of shared things (enemies, movers, breakables), the host's delta snapshots and the guest's acknowledgements, the guest's posing of host-owned things (interpolated 100 ms back by the host's clock; breaks, respawns and death effects), the host's stand-in (an enemy's update with the host's player at the guest's place) and update cull near the guest, the guest following the host's level, no autosave while joined. `docs/MULTIPLAYER.md` section 5 |
| `iCoopCombat.cpp`, `iCoopWorldInternal.h` | co-op's fights (M3), compiled with the game units: the guest's hits on the host's enemies as claims the host checks and applies, the enemies' hits on the guest (the stand-in's damage, projectiles) applied by the guest's own game, the guest's respawn beside the host instead of a lost chance, the host's pickups (which are taken, the guest's claims counted on the host). `iCoopWorldInternal.h` is its seam to `iCoopWorld.cpp`. `docs/MULTIPLAYER.md` section 6 |
| `iCoopFlow.cpp` | co-op's playing through together (M4), compiled with the game units: the host's level changes (the guest goes along, both start the level together), the host's cutscenes and fly-throughs on the guest (only the host skips them), the guest's portals and saves refused while in the host's level. `docs/MULTIPLAYER.md` section 7 |
| `iCoopLook.cpp` | co-op's characters and costumes (M4), compiled with the game units: switching at any time (the Co-op tab, LB), the costumes each player's own save has unlocked, the partner's outfit packs loaded with the level, and the partner's own model, built per level from the model its game says it wears. `docs/MULTIPLAYER.md` section 7 |
| `iCoopHud.cpp` | co-op's challenge counters and texts (protocol 7), compiled with the game units: the counters a level's texts show and the text boxes their logic writes, in one table on both games; in the host's level the guest's show the host's counts and texts (COUNTERS) and its own level does not count them. `docs/MULTIPLAYER.md` 13.4 |
| `tests/nettest.cpp`, `tests/netfuzzcore.h`, `tests/netfuzzcore.cpp`, `tests/netfuzz.cpp` | `tssm_nettest` (crypto known answers, the wire rules, loopback sessions with loss, hostile cases, a fixed-seed fuzz loop; run by `tools/regress.py`) and the libFuzzer harness `tssm_netfuzz` (`TSSM_NETFUZZ=ON`) |
| `iAssetWiden.h`, `iAssetWiden.cpp` | 64-bit hosts: copy-at-load widening of the asset types that keep pointers in 4-byte slots (ATBL, SGRP, LODT, COLL, CRDT, DEST, SHRP, LKIT, the scene-parameters DYNA, SPLN, the JSP info blocks) into the host's struct layouts, and iPtr32's handle table. The header says why and how |
| `iAssetHooks.cpp` | the packer side: `iAssetTypesInstall`, which `zAssetStartup` calls under `PLATFORM_PC`, wraps those types' `readXForm`/`assetUnloaded` in the game's handler table; the copies live in the low heap and are freed with their asset. `TSSM_WIDEN=log` lists every copy, `TSSM_WIDEN=0` turns it off; one summary line at exit. Nothing is installed on a 32-bit host |
| `tests/widentest.cpp` | `tssm_widentest <asset folder>`: every widened asset of every package walked the way the game walks it and checked against the disk bytes (1.25 million checks over the Xbox data), and iPtr32 |

The GUI also touches four existing files, each by a line or two: `iSystem.cpp`
runs the launcher after `iConfigLoad`, `iGuiInit` once RenderWare has started and
`iGuiShutdown` in `iSystemExit`; `iPad.cpp` passes each port's host state
through `iGuiFilterPad`; `iConfigTable.cpp` has the `[audio]` volumes and the
`[gui]` settings; `CMakeLists.txt` builds `third_party/imgui` as `tssm_imgui`
and turns on SDL's renderer and dialogs in the vendored SDL.

Online co-op (`docs/MULTIPLAYER.md`) touches: `iSystem.cpp`
(`iCoopInit` after the save folder is claimed, `iCoopShutdown` in
`iSystemExit`), `gui/iGui.cpp` (the Co-op tab, the HUD layer's frames with the
overlay closed, the overlay opening on the Co-op tab when a join has no
password), `iConfigTable.cpp` (`[coop]`), `CMakeLists.txt` (the sources,
`ws2_32` and `bcrypt`, `tssm_nettest`, `TSSM_NETFUZZ`, the build ID), and these
game files, each by `PLATFORM_PC` calls that return at once with co-op off:
`zGame.cpp` (`iCoopFrame` after the player update in `zGameLoop`),
`zScene.cpp` (`iCoopRenderPartner` after the player is drawn in
`zSceneRenderPreFX`; M2: `iCoopWorldGuestPose` in `zSceneUpdate`'s entity
loop, `iCoopWorldSceneExit` in `zSceneExit`), and for M2 `zNMECommon.cpp`
(`zMODEnemy::Timestep` / `PostTimestep`, `Damage`, `DeathFXKick`),
`zNMETypeTiki.cpp` and `zNMETypeStandard.cpp` (`Damage`, `SetCarryState`),
`xUpdateCull.cpp` (`xUpdateCull_DistanceSquaredCB`), `zEntPlayer.cpp`
(`zEntPlayer_Damage`, `zEntPlayerEventCB`) and `zSaveLoad.cpp`
(`zSaveLoadAutoSaveUpdate`); `docs/MULTIPLAYER.md` 5.8 lists them. M3 adds
`zNMECommon.cpp` and `zNMETypeTiki.cpp` (`Damage`: the guest's claims),
`zEntPlayer.cpp` (`zEntPlayer_PowerupLevel`, the damage functions and the
event handler during a stand-in, the end of a defeat), `zNMEHazard.cpp`
(`zMODHazard::Timestep`) and `zEntPickup.cpp` (`CheckPickupAgainstPlayer`);
`docs/MULTIPLAYER.md` 6.6 lists them. The GameCube build is unchanged by
them.

and from the sound work:

| File | What |
|---|---|
| `iSnd.cpp` | TSSM's iSnd on the Xbox sound data: gc/iSnd.cpp's voice rules (free-voice choice and stealing, loops moved to virtual voices when they lose their channel, streams never stolen, the dialog flag, volume times the category's damped level, pitch in semitones with doppler) over the Xbox SNDI tables and the mixer. Sound effects are decoded whole and cached; streams (SNDS) are read and decoded three seconds ahead into fed voices; cutscene tracks are fed from the cutscene's chunks and start on its first frame. The Settings tab's volumes, which the console does not have, are the mixer's voice groups (`iSndHostSetGroupGain`) and the movie's level (`iFMVAudioSetVolume`), under everything the game does about volume (`docs/GUI.md`). `TSSM_SND`, `TSSM_SNDMIX`, `TSSM_SND_DUMP`, `TSSM_SND_TEST`, `TSSM_SND_ROLLOFF` |
| `iSndXbox.h`, `iSndXbox.cpp` | the Xbox sound formats as bytes, nothing of the game: the SNDI table, Xbox ADPCM (decoded with the header sample first and the 64th code dropped, which the disc's data shows; treedome's BfBB decoder plays all 64 codes and clicks at every block edge), a package's table of contents, the sound records of a cutscene chunk, a WAV writer |
| `iAudioSDL.h`, `iAudioSDL.cpp` | the SDL audio subsystem shared by the mixer and the movie device: every device closed on `SDL_EVENT_WILL_ENTER_BACKGROUND` (on the game thread, before SDL pauses the audio) and opened from scratch on `SDL_EVENT_DID_ENTER_FOREGROUND`; a restart (all closed, `SDL_INIT_AUDIO` down to zero, all opened) on a removed playback device SDL will not move the game off (one of the game's own devices given up on, or any playback device on a driver that cannot move devices, as Android's) or a stall a user reports; on Windows, macOS, PulseAudio and PipeWire SDL moves both devices to the new default itself, and a removal restarts nothing unless two seconds later no playback device is left or SDL still holds a removed device (the game's sound is still on it: SDL could not open the new default, or none was named); failed restarts retried after 1, 2, 4, 8, 16, then every 30 s. SDL keeps a failed Android device as a zombie that plays nothing and cannot be reopened, and drops a failed restart of a paused AAudio stream, so this is the only way back |
| `iAudioSDLTest.h`, `iAudioSDLTest.cpp`, `iAudioSDLTestSDL.c` | `TSSM_TEST_AUDIO_RESTART=<moment>[+ms][*count[/gap]][@how]`: what leads to the audio restart, at a chosen moment (a frame, a movie's start, its sound running out, its close, a package load): an `SDL_EVENT_AUDIO_DEVICE_REMOVED` alone (`event`) or through the calls SDL's WASAPI backend makes when Windows reports the device gone (`lost`) and names a new default (`swap`, `default`) or one SDL cannot open (`swapfail`), or another device gone (`other`), or every device gone (`alllost`), or Android's put-away (`bg`), or SDL dropping the game's own devices (`drop`) or a device that stopped taking audio (`stall`), the last two paired with `default~r` to start a restart while SDL moves the devices; every audio device event is logged with its device's name, and about once a second a `level` line gives the mixer's callbacks and loudest sample and the frames the movie's device has taken. The `SDL` file is compiled into SDL3-static (Windows, SDL from `third_party`) because it needs SDL's private structures; nothing on the machine changes |
| `tests/sndtest.cpp` | `tssm_sndtest`: the tables, decoding, the mixer and the cutscene feed on one package of the user's own data, offline (or on the device at zero gain with `--device`) |

and from the Nintendo Switch work (`docs/SWITCH.md`):

| File | What |
|---|---|
| `iHostSwitch.cpp` | the iHost seam on libnx and newlib, in place of `iHostPosix.cpp`: everything in `/switch/tssm` on the SD card; a rename that replaces by moving the old file aside first (the SD card's rename does not); no directory flush, file times or second process; one instance always; 2 MB default thread stacks (`__wrap_pthread_create`, linked with `--wrap`); frames as `tssm.elf` offsets |
| `switch/iSwitch.h`, `switch/iSwitch.cpp` | the libnx side (never in a translation unit with the game's `types.h`: both typedef `u32`/`u64`): RomFS, the nxlink socket, the applet-mode screen, the applet hook (HOME, applets in front and sleep pause the game clock; coming back restarts the audio), libnx's exception handler (registers and the frame-pointer chain to `crash.log`), the error applet |
| `switch/iSwitchLowMem.cpp` | `iHostReserveLow` on the Switch: heap pages mapped below 4 GB with `svcMapProcessCodeMemory` + `svcSetProcessMemoryPermission` at a free address chosen with `svcQueryMemory`, or into the stack region when that is low, or left in the heap for the callers to refuse |

The Switch build also touches: `iLog.cpp` (the tee is a devoptab, not a pipe
and a thread), `tssm_main.cpp` (start-up calls, the abort handler),
`iLowHeap.cpp` (64 MB by default), `iPadHostSDL.cpp` (every pad is a Switch
pad; the face-button convention), `iWindowSDL.cpp` (no window modes) and
`compat/extras.h` (newlib's `strupr`), each behind `__SWITCH__`.

## Stubs, gaps and known divergences

Each says at its site what it does not do.

- **The SpongeBall keeps its full speed at low frame rates** (a deliberate,
  port-only difference; `zEntPlayer_BoulderVehicleSteps` in
  `zEntPlayer.cpp`, under `PLATFORM_PC`). On a device that cannot keep about
  45 fps, or with the frame rate cap below that, the ball now rolls as fast
  as at 60. The original slows it: the game moves the ball once a frame with
  the whole frame time, and `xEntBoulder_Update` stops a move longer than half
  the ball's radius (0.229 units on the ground), so its top speed is about
  `min(10, 0.229 / dt)` units a second: 7 at 30 fps and about 5 at 20, while the
  breakaway platforms of BL01-BL04 keep counting real time and break under the
  player. The GameCube runs these levels at 60, where the limit never acts. A
  frame longer than 1/48 s is now run as the fewest equal steps of at most
  1/60 s, each the steering push and `xEntBoulder_Update` as one 60 fps frame
  does them. The steady pushes the vents of BL04 give the ball once a frame
  (`zVent.cpp`, `xEntBoulder::force`) are given to every step, so a vent lifts
  and pushes the ball as at 60; a one-off kick (`instForce`) is given once.
  Frames of 1/48 s or less (a steady 60 fps and faster, a 60 Hz frame that
  comes late by a few milliseconds, 50 Hz) are one step with the whole frame
  time, exactly as before; a single longer frame at 60 (a hitch, the first
  frame after a SpongeBall level loads) is stepped too, so it covers the
  ground a 60 fps frame would. Everything else (the platforms' timers, the
  vents' own timers, the triggers, the camera, the check for the platform
  under the ball) still runs once a frame, so on a slow frame those checks
  look at a longer move (0.67 units a frame at 15 fps at full speed, against
  0.17 at 60): a very thin trigger, or a breakaway block the ball touched only
  in an earlier step of the frame, can be missed, as on foot at that rate.

- **Sound** (`iSnd.cpp`) is TSSM's console iSnd rewritten over the Xbox data
  and the mixer, not the console's FMOD code (the Xbox disc has no FMOD banks).
  Where it knowingly differs from the console: streams start at once rather
  than after FMOD opens them one at a time; a cutscene track starts on the
  cutscene's first frame (`iSndStartStereo`, empty on the console); FMOD's 3D
  panner is an equal-power pan on the listener's right axis, with no distance
  attenuation as FMOD's rolloff factor 0 had (`TSSM_SND_ROLLOFF=1` adds an
  inverse-distance falloff); Dolby Pro Logic II is mixed as stereo; the cave
  reverb is iSndReverb's I3DL2 reverb set to FMOD's numbers, not FMOD's
  algorithm. A cutscene whose sound starts but which is destroyed before its
  first update keeps a silent held voice until the scene's sounds stop (the
  console keeps its stream open the same way). Run on x86 and x64: the menu
  music, menu sounds and speech, the BB02 cutscene's track (fed in full, the
  same samples tssm_sndtest extracts) and the level's sounds; with
  `TSSM_SND=<n>` the two widths log the same tables and plays (only the menu's
  randomly chosen speech lines differ from run to run).
- **iAsync: no second thread.** The async load screen is not shown; the load
  itself runs as before. A real thread needs a lock where the console disables
  interrupts, and RenderWare on a second thread.
- **iFMV: the movie-texture path** (`iFMVOpen`/`iFMVClose`, xFMV.cpp's direct
  Bink calls) is not ported; `iFMVOpen` returns NULL. The game's data never
  takes it: neither data set has a `BINK` asset, and every PlayMovie link goes
  to a dispatcher, which plays the movie full screen (`docs/RENDERING.md`).
  Subtitles in `iFMVPlay` are not drawn.
- **Lens flares**: the occlusion test reports every flare hidden (no occlusion
  query in the librw fork yet). No package of either data set has a lens flare
  source, so the console draws none either (`docs/RENDERING.md`).
- **TSSM's screen passes** are PC passes behind `i*` functions:
  `xFXHighDynamicRange` (the glow) calls `iHDRGlowRender` and `xScreenWarp`'s
  draw calls `iScreenWarpRender` under `PLATFORM_PC`; `docs/RENDERING.md` has what each does on the console and here,
  and `video.hdr_glow` and `video.screen_warp` turn them off. treedome's own
  passes (glow, distortion, snapshot) are imported but switched off: they draw
  BfBB's Xbox effects.
- **xShadow's frame-buffer corner save and restore** is skipped on PC
  (`PLATFORM_PC` in xShadow.cpp). On the console the
  shadow camera renders into the top-left 128x128 of the EFB, so
  `ShadowCameraUpdate` copies that corner into `gc_saveraster` first
  (`GCSaveFrameBuffer`) and draws it back with ONE/ZERO blending afterwards
  (`GCRestoreFrameBuffer`). librw gives the shadow camera its own render
  target, so the picture is never touched; before the change the restore drew
  the never-filled raster as a black square over the corner of every frame
  with a shadow. The render states around the skipped draw are still set.
- **JSP node lights** are not tied to JSP atomics on PC (xJSP.cpp skips the
  dummy world-sector link under `PLATFORM_PC`; librw's atomic has no sector
  list). Another way to light them is needed.
- **zSpotlight's cone strip**: librw builds triangle lists and never calls a
  custom tri-strip method (`rw/tssm.cpp`), so the cone draws from its triangle
  list.
- **Memory-card calls** (`stubs/dolphin_card.cpp`): `CARDProbeEx` answers the
  two folder slots `isavegame` keeps; the boot check's format and delete path
  is stubbed, and is not reached because `isavegame` never reports a broken
  card.
- **iTRC**: no disc-error screen, no reset button; `ResetSystem` exits.
- **iSystemInitRWFailSafe/ExitRWFailSafe**: empty (they bracket the disc-error
  screen's RenderWare use on the console).
- **iFileGetSectorSize** answers 32, the console's; whether the Xbox packs'
  `TEXS` sector numbers use the same unit is unchecked.
- **iSystemGetWidthHeight** answers the configured render size instead of
  512x480 (the console's framebuffer, stretched by the VI).
- **iDrawSetDisplayOffset** (PAL picture position) does nothing.
- **iSGIsGameCorrupt** checks the save's header (magic, version, size); like
  the console's check it leaves the checksum to the load.
- **64-bit**: `xMemoryManagerGeneral::FreeNode` is 24 bytes on a 64-bit host
  while `AllocatedNode` is 16, and the allocator turns one into the other in
  place. Checked: nothing allocates from `MemoryManagerMulti` on the PC
  (RenderWare has its own heap, `iMemMgrMalloc` has no caller), so the general
  heap only ever holds the free node `Init` makes. It would have to be fixed
  before anything allocated there. Pointers stored in `U32`s are counted by
  `tools/pcprogress.py` (the "width" column): one is left, the documented
  `iSndGroupHandle` cast in `xSnd.h`.
- **Retail COMMON globals** have no definition in any source file (they are
  linker COMMON in the GameCube build); `common.cpp` defines them on PC. The
  opposite case, objects defined in a header several wads include (`gUIMgr`,
  `sSorted`, `zSaveLoadGameTable`), is `WAD_COMMON` in `include/types.h` (a
  C++17 inline variable on hosts).
- **64-bit: RenderWare allocates below 4 GB** (`iLowHeap.h`). Phase 1's x64
  boot stopped in `PKR_getmem` (xpkrsvc.cpp), which rounded an `RwMalloc`
  pointer through `(U32)(S32)` when `RwMalloc` was the C library's. The cast is
  `UPtr`, and the heap keeps every other RenderWare
  pointer the game stores in a `U32` exact. Asset data with 4-byte pointer
  slots (the "copy-at-load" markers) is widened as it loads
  (`iAssetWiden.h`), except the two structs also read out of cutscene stream
  buffers, which keep a 4-byte `iPtr32`. Two element sizes written as 4 were
  pointer arrays (`xAnimFileNewBilinear`, `xGroupInit`); the game code now
  sizes them with `sizeof`. With those, x64 plays as x86 does: boot, menus,
  the save, story1.bik, BB02's opening cutscene and gameplay, the frames
  matching x86's (`tools/regress.py`). Not covered: MPHT, CRV, VOLU and SPLP
  assets (none in the Xbox data; a 64-bit load of one is reported).
- **`iFileLoad`** allocates with `RwMalloc`, as the console's does, because
  its callers free with `RwFree` (zMainReadINI).
- **`s32`/`u32`** are `long` in `include/types.h` on every host, which is 64
  bits on LP64 (Linux, Android, macOS). Windows is LLP64 and unaffected.
- **Sorts that subtract two texture pointers** (`compare_buckets` in
  xParticleBatch.cpp, `compare_decals` in xDecal.cpp, `compare_types` in
  zSmoke.cpp) compare the addresses under `PLATFORM_PC`. The console's compiler
  really divides by `sizeof(RwTexture)`, so for two real textures the sign is the
  address order. A NULL texture is the one difference: the console's addresses
  are above 0x80000000, so its wrapped difference sorts NULL last, and the port
  sorts it first. Optimizing clang and gcc treat a pointer difference as an exact division,
  which is only true inside one array: the sign came out wrong, and the bucket
  quicksort, which relies on its comparison to stop it, ran off its array on
  32-bit builds. A debug (-O0) build really divides, so it never showed. These
  three are the only pointer differences in the game's code between separate
  objects where the divide is not a power of two (the compiler's own IR, every
  `sdiv exact`, checked by hand).

## Not imported from treedome (BfBB-specific)

`menu/` (BfBB's code-built menus), `iAssetOverride` (code-built packages; TSSM's mod support, `iMod*`, replaces assets at the file level instead), `iHipoly*`, `iToon`,
`iEnvNormals`, `iDayNight`, `iTour`, `iCheats`, `iFixes`, `iTextPatch` (TSSM's
prompt rewrites are `iPadPrompts.cpp`),
`iHostWords.h`, `iSoundtrack`, `iMenuFrame`, `iLoadScreen`, `iLoadTransition`,
`iScreenBleed`, `iDrawDist`, `iFont`/`iFontTexture` (TTF text), `iPadGlyph`
(TSSM draws its own glyphs in `iPadPrompts.cpp`),
`iSnd.cpp` (BfBB's Xbox sound front end: TSSM's is written for TSSM's
xSndMgr; `iSndData.cpp` is adapted, above), `iCollide.cpp`/`iCollideFast.cpp` (TSSM has neither), `configurator/`
(wxWidgets; optional), `android/` (Android comes after Windows), `res/` (BfBB's
icon and button glyphs), `tests/` (BfBB's interface tests), `bfbb_main.cpp`
(became `tssm_main.cpp`), and treedome's own docs (`README.md`, `PORTING.md`,
`LINKING.md`), whose lessons apply here and are worth reading in the treedome
checkout.

## Measuring

    tools/pcprogress.py [--link] [--files] [--errors N] [--build build-pc]
    tools/pcimport.py --check
    tools/pcstubs.py [--check]
    build-pc/tssm_sndtest <package> <out folder> [sounds] [--device]
    build-pc/tssm_widentest <asset folder> [--verbose]
    tools/regress.py --assets DIR --reference DIR --out DIR [--build] [--update-reference]

`pcprogress.py` compiles every unit CMake compiles, counts errors per unit and
per file (wad-aware), counts pointer-width casts separately, and with `--link`
links what compiled and lists what is undefined. `pcimport.py --check` says
which treedome copies, gc copies and shadowed shared files have drifted.
`pcstubs.py` rewrites `stubs/STUBS.md`, the stub inventory, from the source.
`tssm_sndtest` runs one package of the user's own data through the sound code
without the game and writes WAV files to the folder it is given.
