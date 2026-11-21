# Xbox extras

The Xbox release of the game has a few things the GameCube release cut or changed. The port
can show either release's version of each, with one setting per feature in `config.ini`:

```
[extras]
glow = xbox          ; or gamecube
spotlights = xbox
crowds = xbox
water = xbox
frame_lock = xbox
guitar_distortion = xbox
```

Every key is `xbox` by default (on the 3DS, `gamecube`: each one waits until it has been timed
on the console). `gamecube` is always one switch away. The overlay's Settings tab has them in
**Xbox extras**, after Display, with **All Xbox** and **All GameCube** buttons (`docs/GUI.md`).

The case does not matter. Any other value (`on`, `1`, `30`, a typo) is the key's default, the log
says so once (`tssm: config: extras.crowds is not xbox or gamecube, using xbox: on`), and the
overlay's row says what `config.ini` has and which look is used (`iExtrasSetting`,
`iExtrasBadValue`).

| Key | xbox | gamecube | In this build |
|---|---|---|---|
| `frame_lock` | Sandwich Driving 101 (BB01), Sundae Driving (GG02), the driving levels of PT02 and TT02, Bubble Blowing Baby Hunt (TT01) and Google-Eyes and Smelly Knick Knacks (SC02) run at the frame-rate cap, like every other level | those six levels are held at 30 frames a second, as on the GameCube | yes |
| `crowds` | the fish crowds of No Cheese! (BB02) and Sandwich Driving 101 (BB01) move | they stand still | yes |
| `glow` | the Xbox's wide, soft glow, stronger in some levels | the GameCube's tighter, fainter glow | yes |
| `spotlights` | the moving spotlights of No Cheese!, Bubble Blowing Baby Hunt and Turn the Tables on Plankton are on, and every spotlight lights what it shines on and has a lamp flare | those lights are off, and a spotlight is its cone only | yes |
| `water` | the Xbox water of SC02's tanks and B301's sea (reflections, refraction, ripples; `docs/RENDERING.md`, "Water") | the GameCube's water | yes |
| `guitar_distortion` | the screen swirls while the Sonic Wave Guitar's wave is steered | no swirl | yes |

`video.hdr_glow = off` still turns all glow off, the Xbox's and the menus' included.

## How a level decides

`src/SB/Core/pc/iExtras.h`. Under `PLATFORM_PC`, `zSceneInit` (`zScene.cpp`) calls
`iExtrasSceneEnter` right after the level's own package is read and before any of its objects is
made, and `iExtrasSceneReady` at its end. Each feature is decided once per level from three
things:

- the setting;
- whose files the level's package is, Xbox or GameCube: the `PLAT` chunk of the level's HIP, read
  by the name the game opens it by (`BB/BB02.HIP`, else `BB02.HIP`) and so a mod's copy when a mod
  replaces it (`iFilePackagePlatformOf`, `iGCPackage.h`). Before the first level (the menus), the
  overlay's rows use `boot.HIP`'s (`iFileAssetPlatform`). A set of Xbox files with a mod's
  GameCube level, or the other way round, is decided level by level;
- the renderer (shaders, Direct3D 9's fixed path, the 3DS).

When the files or the renderer cannot give the Xbox version, the level gets the GameCube one and
the overlay's row says why in a line under it.

The guitar's distortion is the one feature decided from the game's own files (`boot.HIP`'s
release) and not the level's: its swirl map is in `plat.HIP`.

### With GameCube files

A level whose package is GameCube files (the GameCube disc, or a mod's GameCube level among the
Xbox files) gets each Xbox extra as far as its files allow (`iExtrasGC.h`):

| Level | `spotlights = xbox` | `crowds = xbox` | `water = xbox` |
|---|---|---|---|
| BB02, No Cheese! | the Xbox's: the files switch the two searchlights off with a dispatcher of their own, whose links are dropped | the Xbox's moving crowd, from the skinned models and animation lists the files carry and do not use: 11 fish (the Xbox files have 18), drawn up to 75 units away (the Xbox files: 150) | the GameCube water (the level has none) |
| B401, Turn the Tables on Plankton | the Xbox's: the files' intro and restart switch the arena's lights off where the Xbox files' switch them on and run their script; those links are the Xbox files' now | no crowd | no water |
| TT01, Bubble Blowing Baby Hunt | the GameCube's: the files have no disco lights, and the level says so | no crowd | no water |
| BB01, Sandwich Driving 101 | no spotlights | the GameCube's: the files have no skinned crowd, so it stands still, and the level says so | no water |
| SC02, B301 | no spotlights | no crowd | the GameCube files' own water: they have no water body (and the disc's `plat.HIP` no bump frames), and the level says so |
| PT01-PT03 | the Xbox drawing (light volumes, flares) for the spotlights the files have; they switch none off | no crowd | no water |

The glow and the frame lock are code only: GameCube files get the Xbox glow and the Xbox frame rate
as the Xbox files do (the glow's mask is nearly the same from both discs, `docs/RENDERING.md`, "The
GameCube release's files").

With `gamecube`, nothing of the GameCube files is changed. With `xbox`, everything is checked
before it is changed, and a GameCube file that is not what the disc has there (a mod's) keeps its
GameCube version for the level, with a sentence in the level's line and the overlay
(`iExtrasFallBack`): "This level's GameCube files are not the GameCube disc's where they switch the
spotlights off, so they keep the GameCube look.", or for the crowd "This level's GameCube files
have no animation for their crowd, so it stands still."

How it is done, under `PLATFORM_PC` in `zSceneInit`:

- `iExtrasGCScenePrepare`, with the level's package read and before its objects are made (after
  the water's `xWaterScenePrepare`): each simple object with one of the rigid crowd models
  (`crowd01`-`crowd03`, 51947E72-74) and no animation list has its asset given the skinned model
  and its list (`crowd1_bind` C49163C8 and `CROWD1_ANIMLIST` 9AD84F76, `crowd2_bind` C015AF3B and
  `CROWD2_ANIMLIST` 6EAEF0D9, `crowd3_bind` BB99FAAE and `CROWD3_ANIMLIST` 4285923C;
  `iExtrasCrowdSkinned`), when the level has both. The game's own code then animates it as it
  does the Xbox files' crowd. It also checks the assets of the objects whose links change, against
  the GameCube disc's links (`iExtrasSpotlightLinks`).
- `iExtrasGCSceneObjects`, once the objects exist and before the first reset and SceneBegin:
  those objects get the Xbox files' links, in the scene's memory. A dispatcher reads its links
  from its asset again at every reset (`ZDSP_instReset`), so it is given a copy of its asset with
  the new links; the cutscene manager reads them once.

| Level | Object | The GameCube files | The Xbox files, and now |
|---|---|---|---|
| BB02 | `SPOTLIGHT_DISP_GC_ONLY` (DPAT E9B0DDB1, only in the GameCube files) | SceneBegin -> Off `SPOTLIGHT_01`; SceneBegin -> Off `SPOTLIGHT_02` | no links |
| B401 | `INTRO_CSNMANAGER` (CSNM 2F702A12) | Done -> Off `SPOTLIGHT_GROUP` (7th of 8) | Done -> On `SPOTLIGHT_GROUP`; Done -> Run `SPOTLIGHT_SCRIPT` (7th and 8th of 9) |
| B401 | `SCENE_RESTART_DISP` (DPAT A18EA13D) | SceneReset -> Off `SPOTLIGHT_GROUP` (7th of 7) | SceneReset -> On `SPOTLIGHT_GROUP`; SceneReset -> Run `SPOTLIGHT_SCRIPT` (7th and 8th of 8) |

Read from the two USA discs' `bb02.HIP` and `b401.HIP` (`tools/modkit.py`, every object's links
compared): these are the only differences in links that reach the spotlights, and the spotlights,
`SPOTLIGHT_GROUP` and `SPOTLIGHT_SCRIPT` are the same on both. The GameCube files' 11 crowd objects
differ from the Xbox files' in their model and list only (and in their names; 10 stand where 10 of
the Xbox files' 18 do).

One log line per level says what it got:

```
tssm: extras: BB02 (Xbox files): glow xbox 0.55, spotlights xbox, crowds xbox, water xbox, frame lock xbox, guitar distortion xbox
tssm: extras: BB02 (GameCube files): glow xbox 0.55, spotlights xbox (the GameCube files' links of 1 object as the Xbox's), crowds xbox (11 given the skinned crowd), water gamecube [asked for xbox: This level's files are GameCube files: they have no Xbox water.], frame lock xbox, guitar distortion xbox
tssm: extras: BB01 (GameCube files): glow xbox 0.45, spotlights xbox, crowds gamecube [asked for xbox: This level's files are GameCube files: they have no moving crowd, so it stands still.], water gamecube [asked for xbox: ...], frame lock xbox (a 29.97 fps level, at the cap), guitar distortion gamecube [asked for xbox: The game's files are GameCube files: they have no swirl map, so none is drawn.]
```

`water xbox` in a level without water says only what the setting gave: the water changes nothing
there.

The frame lock, the glow and the guitar's distortion follow the setting at once. The others hold
until the next level loads; their rows are tagged **next level** until then.

## frame_lock: the GameCube's 30 fps levels

On the GameCube, `zSceneInit` sets `globals.minVSyncCnt` to 2 for BB01, GG02, PT02, SC02, TT01 and
TT02 and to 1 for every other level, and `zGameLoop` passes it to `RwGameCubeSetMinRetraceCount`
every frame (`zGame.cpp`). The console's video driver then shows a new picture only every second
retrace: 29.97 frames a second. The Xbox release reads the same value and never waits, so it runs
those levels at 60. So did the port, whose `RwGameCubeSetMinRetraceCount` was empty.

- `rw/engine.cpp`'s `RwGameCubeSetMinRetraceCount` gives the count to `iExtrasSetRetraceCount`.
  With `gamecube` and a count of 2, the pacer holds one frame every two NTSC retraces
  (`iWindowSetFrameLock`): 2 x 1001/60000 s, the console's own period. The cap and the movie
  ceiling are still there; the pacer holds the lowest of the three, and a movie lifts the hold
  (movies stay at 60).
- The count is kept until the next call, which only `zGameLoop` makes. So the menu after a held
  level stays at 30 until the next level's first frame, as on the GameCube. Checked in Dolphin
  with the retail DOL: BB01 booted straight in, quit after 900 game frames
  (`zGameStateSwitch(eGameState_Exit)`, as the pause menu's quit does, from a Gecko hook), every
  presented frame logged: the game loop's 900 frames and the title menu's next 9472 all had the
  retrace count at 2 and a median period of 2.000 retraces (29.97 fps).
- The hold itself needs no game code change.

Measured (2026-11-16, D3D9, 640x480; frames 500-900 of each run, from `TSSM_PERF_CSV`):

| Run | Frame rate |
|---|---|
| BB01, `gamecube`, cap 60 / 120 / off / display rate with vsync | 29.971 / 29.971 / 29.970 / 29.970 |
| SC02 and TT01, `gamecube`, cap 60 | 29.969, 29.971 |
| BB02 (not a held level), `gamecube`, cap 60 | 60.000 |
| BB01, `xbox`, cap 120 | 120.01 |
| SC02, `gamecube`, cap 120, its end sent (story10.bik) | 29.97 in the level, 60.00 in the movie |

**The Patty Wagon under the hold drives as the Xbox's.** `zEntPlayerCarUpdate` makes
`ceilf(dt * 60)` steps of a frame longer than 1.5/60 s, each with the car's full per-step push,
so a held frame of about 2/60 s gets 2 or 3 steps depending on how its measured time falls
either side of 2/60 s. TCRF calls the GameCube's Patty Wagon the fastest of the three releases;
the question was whether the GameCube gives it a steady 3 steps a held frame (its frame is 2
retraces of 1001/60000 s, a hair over 2/60 s). Checked in Dolphin with the retail DOL
(a Gecko hook in `zEntPlayerCarUpdate` printing the step count and
`dt` through the game's `OSReport`, BB01 booted straight in, single core):

| BB01 held frames | 3 steps | 2 steps | other | mean `dt` | measured `dt` |
|---|---|---|---|---|---|
| 23615 | 16262 (68.9%) | 7348 (31.1%) | 4 frames of 1 and 1 of 4, as the level starts | 2.0000 retraces | 1.984-2.016 retraces |

So the GameCube does not give a steady 3: its measured frame jitters around 2 retraces, and about
a third of its frames get 2 steps (about 80 pushes a second against the Xbox's 60). With no
steady GameCube count to copy, the hold keeps the Xbox's driving: under it a frame gets one step
per 1/60 s, rounded (`iExtrasHeldCarSteps`; 2 a held frame), so the car drives as at 60. Without
the hold nothing changes. The `frame_lock` comment in `config.ini` and the overlay's row say that
the Patty Wagon drives as at 60 either way.

## crowds

The crowd objects are plain simple objects (SIMP). The Xbox files give them skinned models
(`crowd1_bind` C49163C8, `crowd2_bind` C015AF3B, `crowd3_bind` BB99FAAE) and an animation list;
the GameCube files give them rigid models (`crowd01` to `crowd03`) and no list, so the same code
animates one and not the other.

With `xbox` nothing is changed: the files' animation lists make the game's own code run the
cycle (`zEntInit` starts the idle state, `zEntSimpleObj_MgrInit` puts the object on the per-object
update list), in both levels, as on the Xbox. Like any object's, a crowd object's update is culled
by distance, so only the crowd near the camera moves on; how far it is drawn (150 in BB02's Xbox
files, 75 in the GameCube's) is the files'.

With `gamecube`, `zEntSimpleObj_Init` and `zEntSimpleObj_Reset` (under `PLATFORM_PC`) set the
animation's speed of an object with one of the three skinned models to 0, right after `zEntReset`
has started its idle cycle: the crowd keeps the cycle's first pose. Taking the animation list away
instead leaves BB01's skinned crowd in a heap on the ground.

Checked (2026-11-17, x64, D3D9, 640x480; the crowd objects logged every 60 frames,
`TSSM_TEST_CROWDLOG`):

| Run | `xbox` | `gamecube` |
|---|---|---|
| BB02, the player held facing the crowd | 18 objects, speed 1, 7 move on in every 60 frames; the crowd's part of the frame changes by a mean of 2.30 in 40 frames | 18, speed 0, none moves; 0.13 (SpongeBob's idle and the seaweed) |
| BB01, the Patty Wagon held by `CROWD_SIMP_03` and `_04` | 22 objects, speed 1, the 2 in range move on | 22, speed 0, none moves |
| BB01 the same at a cap of 120 | 22, speed 1, the same 2 move on | |

The held car's camera sways, so BB01's crowd is checked through the log, not the frames. Neither
choice reaches a save: `zEntSimpleObj_Save` writes the object through `zEntSave` (its flags and
visibility), not its animation, and spotlights have no save of their own.

**With GameCube files** ("With GameCube files", above). BB02's GameCube files give their 11 crowd
objects the rigid models and no list, but carry `crowd1_bind` to `crowd3_bind`, their three
animations and the three lists, the same as the Xbox files'. With `xbox` each object is given the
skinned model and its list before it is made, and the game's own code animates it; with
`gamecube` nothing is changed and the crowd stands still, as on the GameCube. The 7 crowd members
only the Xbox files have are not made up, and the crowd is drawn up to 75 units away, as the
GameCube files' level of detail table has it (the Xbox files': 150). BB01's GameCube files have
neither skinned models nor animations: its crowd stands still with either setting, and the row
says why.

Checked (2026-11-17, x64, D3D9, 640x480, the fixed-step test copy, BB02 held facing the crowd as
`bb02_crowd_xbox`):

| BB02 | crowd objects (`TSSM_TEST_CROWDLOG`) | crowd crop, frame 2500 against 2540 |
|---|---|---|
| Xbox files, `xbox` | 18, speed 1, 7 move on in every 60 frames | 2.30 |
| GameCube disc, `xbox` | 11, speed 1, 6 move on in every 60 frames | 2.36 |
| Xbox files, `gamecube` | 18, speed 0 | 0.12 |
| GameCube disc, `gamecube` | (rigid models, not logged) | 0.19 |

From the GameCube disc the skinned crowd draws as from the Xbox files (the GameCube's own skinned
geometry, read by `tools/librw/gc_*.patch`), in the same poses at frames 2500 and 2540. The Xbox files with the disc's BB02 as a mod give
the same (11 objects, 2.36).

## glow

Both releases draw the glow from the frame's alpha channel: `xFXHighDynamicRangeStartRender`
fills it with the scene's background glow (10 of 255 by default) once the opaque world is drawn,
and the draws that "brighten" (glowing models, decals, particles) raise it. What they do with it
differs (`DEFAULT.XBE` 0x1b0510; `src/SB/Core/pc/iXboxGlow.h` has the steps):

- The GameCube (`rw/hdrglow.cpp`) weighs each pixel's colour by its alpha into a 128x128 bright
  pass and adds half of that back: a tight, faint glow.
- The Xbox copies every pixel brighter in the mask than the background, at full colour, into a
  243x182 target, blurs it through seven passes that each shrink the picture by 5% x (pass + 1)
  (four diagonal taps a pass, the last pass doubled), and adds the last, about 48x36, rectangle
  over the frame at a strength set per scene: a wide, soft halo.

With `glow = xbox`, `xFXHighDynamicRangeRender` calls `iXboxGlowRender` (`rw/xboxglow.cpp`) in place
of `iHDRGlowRender`. The strength is the Xbox's table (0x1b0eb4-0x1b0f57, `iExtrasGlowStrength`):

| Scenes | Strength |
|---|---|
| every scene not named below: the menus, BB01, ... (0x2cc364) | 0.45 |
| JK01, JK02 | 0.3 |
| BB02 (No Cheese!), TR01-TR03 | 0.55 |
| B401 (Turn the Tables on Plankton), PT01-PT03, SC02 | 0.6 |
| TT01, TT02 | 0.7 |
| GG02 | 0.75 |
| BB03, B201 | 1.0 |

What the port does beyond the Xbox's arithmetic:

- The targets are 182 high and as wide as the frame's shape (243 at 4:3), so a wide screen gets the
  Xbox's halo, not a stretched one.
- A frame taller than 480 is halved (box filter) until it is not, before the copy: the Xbox's single
  tap per target pixel then reads about the pixels it reads at 640x480.
- The four taps of a blur pass are one draw through the BfBB glow's `glow_blur_PS` (D3D9's shader
  path, D3D11, OpenGL 3 and GLES 3, Vulkan); D3D9's fixed path draws them as four additive quads,
  which round each tap to 8 bits.
- The composite writes colour only.

**The particle tanks.** `xFXHighDynamicRangeBrightenPtank` and `RestorePtank` are empty on the
GameCube, so every particle tank writes the mask. With the Xbox glow they do what the Xbox's do
(0x134a50, 0x134ba0): a tank that brightens (flag 8: fire, explosions) writes the mask through an
alpha test of alpha > (glow + 1) / 2, and every other tank (dust, exhaust, splashes) is drawn with
alpha writes off, so it does not glow.

**The title and pause menus.** Both discs' menus send the event that turns the menus' glow on (the
title's `MNU3 PRESS START DISP`, the pause menu's `PAUSE STATE DISP`); the GameCube's
`zUIMgrEnableHDR` is empty and ignores it. The GameCube also switches the whole pass off for the main
menu (`zMenuSetup` calls `xFXHighDynamicRangeEnable(false)`); the Xbox's never does (its `enabled`,
0x2c8cf8, is only ever set to 1), so with the Xbox glow the pass stays on there too. With the
Xbox glow it stores the flag, and the menus' brightened elements are drawn into a mask of their own
and glow through the Xbox's menu chain (8 passes, a 2% step, the last doubled). The flag does not
carry from the title screen into play: `zSceneSetup` clears it at every level start
(`zUIMgrEnableHDR(false)`, `zScene.cpp`), and the pause menu's event sets it again.

The menus' strength is not known from the code: the Xbox's is 3.5 times a field of the menu's saved
configuration that nothing writes (0x12f220 reads leftover stack memory). It was picked against a
picture of the real thing, TCRF's capture of the Xbox's options screen (Vibration Mode selected), set
beside the port's options screen with the GameCube glow and with the Xbox menu glow at several
strengths (mean grey level added, 640x480, D3D9):

| Around the selected label "VIBRATION MODE" | above | below | further below |
|---|---|---|---|
| TCRF, Xbox minus GameCube | +0.2 | -0.3 | -0.2 |
| TCRF Xbox minus the port's GameCube frame | -0.5 | -0.6 | -0.4 |
| the port, menu strength 0.5, minus its GameCube frame | +12 to +27 | +13 to +20 | +24 |
| 1.0 | +23 to +57 | +25 to +39 | +47 |
| 2.0 | +46 to +108 | +50 to +78 | +92 to +94 |
| 3.5 | +78 to +136 | +89 to +131 | +135 to +137 |

The console's picture has no glow there at all, so whatever its leftover value is, it draws nothing
visible (a tiny, zero or negative float, which the combiners clamp to 0, would do that). The strength
is 0: the menus' brightened elements are still drawn into their mask as on the Xbox, and the chain
itself is skipped. `TSSM_TEST_MENU_GLOW=<n>` draws it at another strength.

`video.hdr_glow = off` turns off either glow, the menus' included, and greys out the overlay's row.

Checked (2026-11-17, x64, 640x480 unless said):

- **Against the software model** of the Xbox chain (a script kept outside the repository, fed the port's own
  frame without the glow and its mask, `TSSM_HDR_VIEW=base` and `=mask`), BB02 at 0.55, D3D9: mean
  difference 0.10 and 0.99 of 255 over frames 1200 and 1800, 0.65 and 2.09 over the 35,000 pixels
  the model lights (the second frame's runs are not at quite the same moment).
- **Backends,** the glow alone (`TSSM_HDR_VIEW=glow`), BB02 frames 1200 and 1800, against D3D9's
  shader path: D3D11 0.19 and 0.21, OpenGL 3 0.31 and 0.38, D3D9's fixed path (four draws a pass)
  0.11 and 0.27 mean of 255. GLES runs the same GLSL as OpenGL 3; it was not run on a phone here.
- **A wide frame** (3840x2160) halves three times to 480x270 and blurs in 324x182 targets: the
  halos stay round.
- **Cost,** all five keys `xbox` against all `gamecube`: B401 at 1920x1080 on OpenGL 3, GPU time
  0.203 to 0.272 ms a frame; on D3D9 at 3840x2160 uncapped, frame time 1.179 to 1.255 ms in B401
  (glow, light volumes, crowd) and 0.948 to 0.947 ms in BB02 facing the crowd (glow alone).

## spotlights

The spotlight objects are the same in both releases' files; the GameCube files switch the moving
ones off with events of their own (BB02's `SPOTLIGHT_DISP_GC_ONLY`, B401's intro and reset links)
or delete them (TT01's disco lights). The GameCube code also never draws a light volume (the lit
patch a beam casts) or a lamp flare.

With `gamecube` and the Xbox files, the lights the GameCube files keep off are kept off
(`iExtrasSpotlightKeptOff`, honoured in `zSpotlight`'s `reload_asset`, `reset` and `activate` under
`PLATFORM_PC`); a level from GameCube files keeps them off with its own links, as on the GameCube:

| Scene | Kept off |
|---|---|
| BB02 | `SPOTLIGHT_01`, `_02` |
| B401 | the arena's `SPOTLIGHT_02` ... `_12`, even numbers (the table lights stay on, as in both releases) |
| TT01 | `DISCO_BALL_SPOTLIGHT_01` ... `_08`, `DISCO_SPOTLIGHT_01` ... `_04` |

With `xbox`, the lights are on and drawn as on the Xbox:

- **Light volumes** (`xLightVolume::render_all` and `render_atomic`, empty on the GameCube; the
  Xbox's 0x185640 and 0x17ece0; `iLightVolume.h`, `rw/lightvolume.cpp`). Each volume clears the
  stencil buffer and draws the spotlight's cone, closed at both ends, three times: its inner faces
  counting up where hidden, its outer faces counting down where hidden, then its inner faces again
  where the count is 1, which is the scene inside the beam. The blend multiplies the picture by the
  light's colour plus its alpha (a black light counts as white). In B401 a check on the scene ID
  makes it plain adding with no alpha write, as on the Xbox.
- **Exactly as shipped**, including the odd blending TCRF mentions in No Cheese!: the Xbox's first
  pass already writes colour, with the material colour and blend constant the last volume left, so
  the far side of the whole beam is brightened (doubled in BB02), not only the lit patch. The
  colours kept for that pass belong to the level's cones, which `zSpotlight::scene_exit`
  destroys: they are forgotten when the next level makes its cones (`iLightVolumeForgetCones`,
  from `zSpotlight::setup_all`), which start white. The blend constant is the device's and
  carries over, as on the Xbox.
- **The cones** (`zSpotlight::render_aura`) are drawn with alpha writes off, as the Xbox's
  0x60020 does, so they do not glow; the GameCube's write the mask.
- **The lamp flares** (`zSpotlight.cpp`, the Xbox's `render_flares`, 0x60110; the GameCube
  collects and sorts them every frame and never draws one). A flare is a square the size its asset
  gives (1 unit), spanned by the lamp's right and up axes and centred on the lamp, in the flare's
  colour (white; PT01's and PT03's tiny eye lights have none), drawn after the cones in their states
  (adding, no depth write, alpha writes on), as triangle strips grouped by texture. Its alpha is
  `add_flare`'s: glow_max x cos^4 of the angle between the lamp and the camera while the lamp faces
  the camera (255 at most), glow_min (0) otherwise.
  - **When one shows.** The Xbox draws them with the alpha test RenderWare is left with there,
    alpha above 0: the GameCube's `xFXHighDynamicRangeSetGlowClamp`, which lets everything
    through, is empty on the Xbox, and the D3D8 driver's default (0x1c2566) and the particle tanks'
    restore (0x134c07) are both "greater than 0". So a flare shows only while its lamp faces the
    camera (within about 78 degrees), and then at full colour, the blend being adding. A lamp seen
    from the side or from behind shows none. Drawn with the GameCube's alpha test instead, every
    lamp in view had a white square on it, which the Cxbx-Reloaded capture `B401/b401_06` does not
    have; with the Xbox's they are gone.
  - **The texture.** No level's flare has one but six of TT01's disco lights, whose flare is
    `pad_button1` from font.HIP, the Xbox pad's A button. The port's button prompts swap that
    texture's raster for the device's glyph (a keycap, a PlayStation cross); the flare keeps the
    disc's picture (`iPadPromptsDiscRaster`).

Every PC renderer has the stencil buffer this needs, D3D9's fixed path included. GLES with
anti-aliasing through `TSSM_GPU_MSRTT` (off by default) has none; the volumes are left out there and
the log says so once.

Seen (2026-11-17, 640x480): in B401 the arena lights stand as bright white and blue-green columns
from the truss with a green beam on Plankton's robot, as in the Cxbx-Reloaded captures of the Xbox
game; with `gamecube` only the table lights' grey cones show. In
BB02 the two searchlights over the Krusty Krab 2 are bright blue-white bars against the sky, as in
the capture `BB02/bb02_05`; with `gamecube` they are gone. The same on D3D9 (both paths), D3D11 and
OpenGL 3.

**TT01, Bubble Blowing Baby Hunt.** Its disco lights are only in the Xbox files: 8 white beams from
the disco ball to platforms that move about the floor, and 4 red and green beams across the room, all
switched on by the disco machine's script (`DISCO_SCRIPT_01`) 5 s after it starts. Checked with the
script started by `TSSM_TEST_SEND` and the scene's spotlights logged every 10 frames
(`TSSM_TEST_SPOTLOG`; the script started at frame 700, the player held in the disco):

- `xbox`: all 12 come on 310 frames after the script starts (5.2 s) and stay on; the disco floor
  gets the white patches of the ball's beams, the walls the red and green ones, and the glow (0.7
  in TT01) spreads them. The same on D3D9 (both paths), D3D11 and OpenGL 3.
- `gamecube`: none of the 12 comes on through the whole script (1000 frames after it starts, at 30
  and at 60 fps; the On events are refused, `iExtrasSpotlightKeptOff`); the disco has only its
  lasers, as on the GameCube.
- There is no capture of the Xbox's TT01 to set this beside. The picture is the Xbox's code on the
  Xbox's data, like B401's and BB02's, which do match their captures.
- **The camera in a beam.** With the player held under the disco ball, the camera was inside one of
  its beams in 23 of the 53 lines logged from frame 1200 (up to 0.6 units in). The game then grows
  the volume's cone a little around the camera (`refresh_light_volume`, as on both consoles), and
  the Xbox's passes never read the volume's `inside`; the near plane cuts the cone there, which the
  count through hidden faces does not need. The frames show the scene as from outside a beam: no
  flash, hole or dark screen.

**PT01 to PT03** have spotlights in both releases' files (PT02's two searchlights on the buildings,
PT01's and PT03's tiny "eye" lights with no flare): with `xbox` they get light volumes and flares,
with `gamecube` cones only. PT02's were checked through the log only (on in both, out of view from
the spots tried).

**With GameCube files** ("With GameCube files", above). With `xbox`, BB02's and B401's GameCube
files get the Xbox files' links where they switch the lights off, so the lights are on when and
where the Xbox's are, and every spotlight is drawn the Xbox way. With `gamecube` nothing is
changed: the files' own links keep the lights off, as on the GameCube. TT01's GameCube files have
no disco lights, so that level is the GameCube's either way.

Checked (2026-11-17, x64, D3D9, 640x480, the fixed-step test copy, `TSSM_TEST_SPOTLOG`),
the same runs with the Xbox files, the GameCube disc's files, and the Xbox
files with the disc's level as a mod:

| Run | BB02: `SPOTLIGHT_01`, `_02` (every 60 frames to 2520) | B401: lights on (every 30 frames to 2310) | B401: the same lights on as the Xbox files with `xbox` |
|---|---|---|---|
| Xbox files, `xbox` | on in all 42 lines | from frame 330, up to 10 of 12; arena lights on in 67 of 77 lines | (the reference) |
| GameCube disc, `xbox` | on in all 42 | the same | 77 of 77 lines |
| Xbox files with the disc's level as a mod, `xbox` | on in all 42 | the same | 77 of 77 |
| each of the three, `gamecube` | off in all 42 with the Xbox files; with GameCube files on in the 5 lines under story1.bik (frames 60 to 300, nothing drawn) until the level's own links switch them off at SceneBegin, then off in 37 | from 330, the 5 table lights; arena lights never | 10 of 77 (the intro, all off) |
| GameCube disc and mod, `xbox`, the build before this | off in all 42 | as `gamecube` | 10 of 77 |

The frames of the Xbox files are the same, pixel for pixel, as the build before with either
setting, and so are those of every `gamecube` run with either files (BB02 4 frames, B401 4 frames
each; checked again on 2026-11-18 after the keep-off list was limited to Xbox files: the GameCube
files' lights are off by their own links from the first frame drawn). In the frames the GameCube disc's BB02 shows the two blue-white searchlights over the
Krusty Krab 2 as the Xbox files do, and its B401 the arena's white and blue-green columns.

- **BB02's dispatcher across a reset.** With the GameCube disc's files and `xbox`, the two lights
  switched on (`TSSM_TEST_SEND`, event 38), then `SPOTLIGHT_DISP_GC_ONLY` sent Reset (10), which
  makes a dispatcher read its links from its asset again, then SceneBegin (87): both lights stay
  on through frame 1960. The copy of its asset it reads has no links.
- **B401's restart.** The arena's group switched off (39), `SCENE_RESTART_DISP` sent Reset and
  then SceneReset (476), which it gets when the player loses a chance: with the Xbox files and
  with the GameCube disc's, both with `xbox`, 5 lights were on before the SceneReset and 11 after
  it (the group and its script again), the same lights every 10 frames through frame 2500 (250 of
  250 lines); with the disc's files and `gamecube`, 5 before and after.
- **A GameCube BB02 that is not the disc's** (a test mod: `SPOTLIGHT_DISP_GC_ONLY`'s second link
  sends Off to `SPOTLIGHT_03` instead, and the three crowd lists have other IDs), over the Xbox
  files and over the disc's: the level's line says `spotlights gamecube [asked for xbox: This
  level's GameCube files are not the GameCube disc's where they switch the spotlights off, so they
  keep the GameCube look.]` and `crowds gamecube [asked for xbox: This level's GameCube files have
  no animation for their crowd, so it stands still.]`; the crowd stands still, and the lights do
  what the mod's own links make them do, as on the GameCube: `SPOTLIGHT_02` stays on when they
  switch `SPOTLIGHT_01` off, drawn as a GameCube cone (the keep-off list kept it off until it was
  limited to Xbox files). The level plays on.

## The Patty Wagon above 60 fps (a port fix, not a setting)

`zCar::CarUpdate` adds its acceleration (the 0.6 push, the 0.2 gravity, grip and damping) once a
step with no time factor (`zCar.cpp`), and `zEntPlayerCarUpdate` makes one step of any frame up
to 1.5/60 s. Neither console ever runs the car above 60 (the Xbox runs it at 60, the GameCube at
30 in six levels), so above 60 the port applied all of it more often a second than either: at
120 fps the car was 1.6 times as quick off the line, fell twice as fast and jumped about half as
far.

Now a frame that is one step shorter than 1/60 s, while the pacer runs above 60 or uncapped,
scales those amounts by `stepDt * 60` (`iTimeFrameShare`, `zCarSetStepShare`); the bun's bounce
(`UpdateVirtualBunTop`, once a frame) is scaled the same way, its 0.8 damping as
`xpow(0.8, share)` like `zParPTank.cpp`'s bubbles. At a cap of 60 or below, and on frames of
several steps, the share is exactly 1 and the car is the original, bit for bit.

Measured in BB01 (2026-11-16, D3D9, 640x480, vsync off): drive held from the start line, then
the car lifted 40 units to time its fall. "Before" is the build without this fix, read at fixed frames;
"after" is this branch, timed from the car log
(`TSSM_TEST_CARLOG`, the game's own frame times), distances from where the car stood:

| Cap | Before: 0.5 s / 1.0 s / fall in 0.5 s | After: 0.5 s / 1.0 s / fall in 0.5 s | Share |
|---|---|---|---|
| 60 | 3.74 / 11.99 / 1.40 | 3.73 / 12.00 / 1.38 | exactly 1 on every frame |
| 120 | 5.96 / 16.95 / 2.90 | 3.66 / 11.88 / 1.42 | 0.43-0.57 |
| 144 | | 3.65 / 11.86 / 1.43 | 0.34-0.49 |
| 240 | | 3.62 / 11.81 / 1.43 | 0.19-0.33 |
| 60 and 120 with `frame_lock = gamecube` (29.97 fps) | | 3.73 / 11.99 / 1.32 | 1 (2 steps a frame) |

(An earlier batch, lifting the car after 1.0 s instead of 1.3 s: 12.00 / 11.88 / 11.86 / 11.85 at
1.0 s and falls of 1.45 / 1.47 / 1.48 / 1.50.)

The distance at 1.0 s is within 1.6% of 60 at every rate, and the fall within 4% (it was 2.07
times as far at 120). What is left is how a step of 1/60 s differs from a shorter one, not a
frame-rate bug: the car moves each step at its new speed, which puts a 60 fps car about half a
step ahead, and the first step after it leaves the ground still has the ground under it, so the
60 fps car starts to fall 1/60 s later. Both shrink with the step, so the faster rates are a
little behind at 0.5 s (3% at 240) and fall a few percent further. Exact agreement would need the
car stepped at a fixed 1/60 s with its pose blended between steps (a bigger change, not made).

The same at 120 and 144 with the fix turned off (`TSSM_TEST_FRAME_SHARE=0`, timed the same way):
6.00 / 16.99 / 2.87 and 6.67 / 18.16 / 3.46.

**Drives over the jumps** (drive held from the start line, no steering, car logged; position at
whole seconds against the 60 fps drive):

- BB01: over its bumps and the two hops for 8 s, within 0.5 units of the 60 fps path at 120, 144
  and 240, at the same speeds (24.2 u/s at 8 s).
- TT02: within 0.2 units of the 60 fps path for 3 s at 90, 120, 144 and 240. Then the bump at
  about (27, 2, -140), whose face the car either climbs or is thrown back from (a wall rebound in
  `ApplyCollisionRebounds`), and which way it goes depends on the frame times even at 60. Two
  drives, each run with the real clock and with a test-only clock that gives every frame exactly
  1/rate s (a test patch, never on the branch); the car's lowest
  speed on the bump (2026-11-17):

  | Drive | 60, real clock | 60, exact | 90 | 120 | 144 | 240 |
  |---|---|---|---|---|---|---|
  | A held from 22 s (18 u/s at the bump) | climbs, 16.6 (twice) | thrown back, 2.7 | thrown back, 2.7 | thrown back, 2.7 (both clocks) | thrown back, 2.7 | thrown back, 2.7 |
  | A tapped (12 u/s at the bump) | over, 8.8 | stalls, 1.5 | stalls, 1.6 (exact) | over, 9.3 (both clocks) | over, 9.3 (both clocks) | over, 9.3 (both clocks) |

  So a 60 fps car meets this bump both ways with the same buttons, and every rate above 60 lands
  on one of the two outcomes a 60 fps car gives: it is the bump, not the frame rate. (The car
  collides once a step; a frame a little longer or shorter moves where the step that meets the
  face falls.) A re-run of the tapped drive at 60 and 120 with the exact clock gave the same car
  log, bit for bit, as the run before. Fixed 1/60 s steps with the pose blended between them would
  make every rate step as the exact-clock 60 fps runs do, which in these two drives are the thrown
  back and the stalling ones, so they would not make this bump go as a real-clock 60 fps drive does
  either.
- GG02, ramp 2 (the car put down before it, `TSSM_TEST_PLACE`, then A held; exact clock): take-off
  within 1.1 units of the 60 fps one at every rate; the flight 1.567 s and 26.0 units at 60, 1.708
  s and 28.1 at 120, 1.632 s and 27.0 at 144, 1.650 s and 27.4 at 240 (4 to 9% longer); landing
  within 1.5 units. A straight drive from the start meets a wall after about 80 units, before any
  jump.
- PT02 was not driven. Its driving starts after the level's opening on foot, and a scripted A by
  the start opens the warp menu instead; every scripted try left the car standing. It needs a
  drive by hand.

## Tests

- `tssm_extrastest <scratch folder>` (`src/SB/Core/pc/tests/extrastest.cpp`): the decisions for
  every feature, setting, files and renderer; the spotlight and crowd tables against `xStrHash`
  of the names; the glow's strength for every scene; a level's decisions, the glow and the frame
  lock following their settings at once, the frame lock's count reaching the pacer, the old
  `[video] water` key and values that are neither `xbox` nor `gamecube`, through the real config
  layer; whose files a level is (the names asked, the flat-name fallback, `boot.HIP`'s answer
  before a level, kept once it has answered) and the water's own fallback reaching the overlay's
  sentence and the level's line; with GameCube files, the skinned crowd for each rigid one and the
  spotlight links turned into the Xbox files' on copies of the two discs' links (and refused for
  links that are not the disc's: one missing, one there twice, one with a parameter), and a
  level's own fallback (`iExtrasFallBack`) reaching the overlay and cleared by the next level;
  the guitar's distortion's key, its three fallbacks' sentences, its following the setting at
  once, the pass's own reason reaching the overlay's row and the game's files (not the level's)
  deciding it (1550 checks).
- `tssm_watertest` (`src/SB/Core/pc/tests/watertest.cpp`): the Xbox water's tiles, strips and bump
  clock on made-up meshes.
- `tools/regress.py` writes all six keys as `gamecube` into every run's `config.ini`, so a
  change to an `xbox` version cannot move the compared frames. References captured before the
  extras still show the faint beam of BB02's two searchlights, which `gamecube` keeps off: at most
  0.03 of the gameplay frames' mean limit of 4.0, so they pass, and references captured since show
  the GameCube versions. Its runs `bb02_crowd_xbox` and `bb02_crowd_gamecube`
  check that No Cheese!'s crowd moves with `xbox` and stands still with `gamecube`, and
  `bb02_xbox` is the run to gameplay with every key at `xbox` (the title, the main menu and BB02
  with the Xbox glow, spotlights and crowd), against references of its own. `tt01_spot_xbox` and
  `tt01_spot_gamecube` start TT01's disco (`TSSM_TEST_SEND`), hold the player there and check
  through `TSSM_TEST_SPOTLOG` that its 12 disco lights come on with `xbox` and never with
  `gamecube`. Their frames are captured for the report but not compared (`"compare": False`): the
  disco moves over the whole picture, and two runs differ by a mean of 10 at the same frame.
- `TSSM_TEST_SPOTLOG=<n>` logs the scene's spotlights every n frames (on, drawn, flares showing,
  the camera in a beam, and how many cone colours the light volumes keep when that changes), and
  `TSSM_TEST_CROWDLOG=<n>` the crowd objects and how many moved on in their cycle
  (`iTestEvents.h`).
- `TSSM_HDR_VIEW=base` and `=mask` give a software model of the Xbox chain its input
  (a model kept outside the repository); `TSSM_TEST_MENU_GLOW=<n>` sets the menus' strength.
- `TSSM_TEST_CARLOG=a-b` logs the Patty Wagon every frame (`iTestEvents.h`), and
  `TSSM_TEST_FRAME_SHARE=0` turns the car fix off (the share is always 1), so one build can be
  compared with and without it.

## The water

The Xbox water (`iXboxWater.h`, `docs/RENDERING.md`, "Water") was built with its own key,
`[video] water`; it is `[extras] water` now.

- A `config.ini` that has `[video] water` keeps working: `iConfig.cpp`'s moved keys read it as
  `[extras] water`, never append the new key over it, and move it to the new name the first time
  it is changed in the game. The settings table has only the `[extras]` row.
- `iExtrasResolve` gives the water the GameCube version on the 3DS, on Direct3D 9's fixed path
  and with GameCube files. `xWaterScenePrepare` reads the level's decision
  (`iExtrasAtLevelLoad`, `iExtrasInForce`) right after `iExtrasSceneEnter`, and then makes its own
  checks: the 17 bump frames in `plat.HIP`, a renderer with the water shaders (not Vulkan), the
  shaders and targets made, a water body that can be used. A bad `extras.water` is logged and
  replaced by the default in `iExtrasSetting`, as for every key.
- Where those checks fail in a level with water bodies, `iXboxWaterActive()` is false and the
  water's status (`iXboxWaterStatus`, cleared in a level without water bodies) is the overlay's
  sentence for the row (`iExtrasStatus`) and the `[asked for xbox: ...]` of the level's line.
- The row is in **Xbox extras**, tagged **next level**, like the crowds'.
- `tools/regress.py` pins `water = gamecube` with the other keys; `bb02_xbox` sets it to `xbox`
  (BB02 has no water, so it changes nothing there). Its runs `sc02_water_xbox`,
  `sc02_water_gamecube`, `b301_water_xbox` and `b301_water_gamecube` boot each level with water
  and check its log lines (`check_water`): with `xbox`, the Xbox water set up for every body
  ("Xbox water in SC02: 2 bodies") and its surface drawn; with `gamecube`, the GameCube water
  because the setting says so; and the level's extras line agrees. Their frames are not compared:
  how the water looks was checked with fixed-step runs, against the build before it and the Xbox.

## guitar_distortion: the Sonic Wave Guitar's swirl

On the Xbox the screen swirls while the camera flies to the Sonic Wave Guitar's wave and rides it.
The GameCube release draws nothing: `cruise_bubble`'s `distort_screen` is an empty function there,
with its five calls still in place (`zEntCruiseBubble.cpp`).

**What the Xbox does** (`DEFAULT.XBE`; `src/SB/Core/pc/iXboxDistort.h` has the addresses):

- `distort_screen` keeps an amount: 0 to 1 on the camera's S-curve while it flies to the wave (1.5
  s), 1 while it rides the wave (up to 6 s), 0 otherwise. `cruise_bubble::deinit` sets 0 as well.
  There is no level check: every level where SpongeBob has the guitar.
- `zSceneRenderPostFX` draws the pass after the screen warp, the glow and the lens flare, and the
  guitar's HUD after it, so the HUD is not swirled. The GameCube draws the HUD first.
- The pass reads the frame through a swirl map, `BXCruiseBubbleDistort` of the Xbox's `plat.HIP`
  (512x512; red and green nearly equal, blue 255; the GameCube's `plat.HIP` does not have it). With
  (r, g) the map's texel expanded to -1..1, read from the map's middle half, A = 15 x amount and
  t = milliseconds x 0.002, a pixel (x, y) of the 640x480 frame shows the frame at
  `x + A (r sin t + g cos t)`, `y + A (r cos t - g sin t)`: about 21 pixels at most, turning once
  every 3.14 seconds of real time, paused or not.
- It draws that into a 512x512 target and stretches the target back over the screen, which softens
  the whole picture a little while the swirl is on.

It is the pass of BfBB's cruise bubble under its old name, with three differences from the BfBB PC
port's version (`rw/distort.cpp`, kept in the tree unused): the map's middle half is read where
that one reads the whole map, the offset uses red and green in two dot products where that one
uses red alone (21 pixels at most against 15), and it is drawn from `zSceneRenderPostFX` and not
from `xScrFxRender`.

**What the port does** (`iXboxDistort.h`, `iXboxDistort.cpp`, `rw/xboxdistort.cpp`,
`rw/shaders/xboxdistort_PS.hlsl`, `rw/shadersgl/xboxdistort.frag`):

- Under `PLATFORM_PC`, `distort_screen` hands the amount to `iXboxDistortSetAmount`, and
  `cruise_bubble::deinit` sets 0. With the Xbox look on, `zSceneRenderPostFX` draws the pass after
  the lens flare and the guitar's HUD after it; with it off the GameCube's order is kept exactly.
  The HUD moves with this key alone: with `guitar_distortion = xbox` and `glow = gamecube` the
  guitar's HUD is drawn after the GameCube glow and the screen warp, so it does not glow.
- One pass at the picture's own size: the frame is copied (`iScreenPassCapture`) and drawn back
  through the pixel shader with ONE/ZERO, linear, clamped. The trip through 512x512 is left out:
  at 1920x1080 it would blur the picture, not soften it.
- The offsets are shares of the picture (A/640 across, A/480 down), so the swirl is the same at
  any render size, and on a wide picture it covers the whole of it, as the Xbox's own arithmetic
  does on a 16:9 television.
- The clock is real time (`std::chrono::steady_clock`), wrapped to one turn before it becomes a
  float.
- It follows the setting at once (`iExtrasApply`), like the glow.

**When it is not drawn.** The GameCube look is used, and the overlay's row says why:

| Case | Sentence |
|---|---|
| The game's files are the GameCube's (no swirl map) | "The game's files are GameCube files: they have no swirl map, so none is drawn." |
| Direct3D 9's fixed path (no pixel shader) | "Direct3D 9's fixed path has no pixel shader for the swirl: none is drawn." |
| The 3DS | "Not on the 3DS yet." |
| Android and the Switch (the pass is built for their OpenGL and has never been run there) | "Not on Android or the Switch yet." |
| Vulkan (no SPIR-V build of the shader: that needs a `dxc.exe` with SPIR-V output, `rw/shaders/make_shaders.cmd`) | "The Vulkan renderer has no shader for the swirl yet: none is drawn." |
| Xbox files whose `plat.HIP` has no map after all (a mod's) | "The game's files have no swirl map (BXCruiseBubbleDistort in plat.HIP): none is drawn." |
| The shader or the frame's copy failed (said once in the log) | "The swirl could not be drawn (the log says why): none is drawn." |

**How it was checked** (2026-11-19 and 2026-11-20). When the pass is drawn, and what is drawn over
it, were seen on the Xbox game itself, run in Cxbx-Reloaded: the pass is drawn exactly while the
wave flies, and the guitar's wind streaks, reticle and countdown are drawn on top of it, not
swirled, as here. The swirl's shape cannot be seen there (the emulator fills the pass with a flat
colour), so the shape is compared with a model of the Xbox's arithmetic, written from the
executable.

- `TSSM_TEST_DISTORT=<amount>[,<milliseconds>]` draws the pass at that amount in every frame of a
  level, with the clock held when a time is given. PT03 held still (`tools/regress.py`'s
  `pt03_belt` place), 640x480, amount 1 and time 0: the frame with `xbox` against the model run on
  the `gamecube` frame of the same build, over the wall above the lava (the lava does not repeat
  from run to run). The two differ by a mean of 0.10 of 255 on Direct3D 9 (64-bit and 32-bit) and
  OpenGL 3, where the pass itself moves that part of the picture by 5.5; at amount 0.5 and 785
  ms, by 0.06 (Direct3D 9). A second model, written separately from the executable, differs from
  the port by 0.19 on Direct3D 9, OpenGL 3 and Direct3D 11, at both widths. (Direct3D 11 is
  measured with `vsync` off: with it on, the small test window runs at about 30 frames a second
  on this PC and the camera is not at the same place in two runs.) Two `gamecube` runs differ by
  0.003, and so does `xbox` with the amount at 0 from `gamecube`.
- `TSSM_TEST_DISTORT=log` logs the game's own amount for every frame the pass is asked for.
  BB02 with the guitar given (`TSSM_TEST_POWERUP`) and fired with rt: with `xbox` the pass is
  asked for in 166 frames, the amount rising from 0 to 1 over about 95 of them and staying at 1
  until the wave ends; with `gamecube` in none, and the frames show no swirl. `tools/regress.py`'s
  runs `bb02_guitar_xbox` and `bb02_guitar_gamecube` check this from the log (`check_guitar`).
- What it costs, with the pass forced on in every frame (PT03 held still, 1920x1080, 4x
  anti-aliasing, no cap, x64 Debug, RTX 5070 Ti; medians of frames 300 to 1200, two runs each,
  `TSSM_PERF`): GPU time on OpenGL 3 0.35 to 0.44 ms a frame (+0.08 ms); the whole frame 1.09 to
  1.13 ms on OpenGL 3, 1.04 to 1.06 ms on Direct3D 11 and 1.19 to 1.22 ms on Direct3D 9. One more
  camera update a frame (17 to 18), for the copy. At an amount of 0, which is all the time the
  wave is not steered, nothing is drawn.

## Per platform

| Key | PC: D3D9 (both paths), D3D11, OpenGL 3 | Android: GLES 3 | Switch: OpenGL | 3DS |
|---|---|---|---|---|
| `glow` | xbox | xbox | xbox | gamecube, row hidden |
| `spotlights` | xbox | xbox; no light volumes with `TSSM_GPU_MSRTT` (no stencil buffer; off by default), said once in the log | xbox | gamecube, row hidden |
| `crowds` | xbox | xbox | xbox | gamecube, row hidden |
| `water` | xbox (D3D9's shader path, D3D11, OpenGL 3; Vulkan and D3D9's fixed path use the GameCube water) | xbox | xbox | gamecube, row hidden |
| `frame_lock` | xbox | xbox | xbox | gamecube, row hidden (the 3DS's cap is 30) |
| `guitar_distortion` | xbox (D3D9's shader path, D3D11, OpenGL 3; Vulkan and D3D9's fixed path draw none) | not yet: built for GLES 3, never run there, so none is drawn ("Other platform branches") | not yet, as Android | gamecube, row hidden |

Android and the Switch get the PC's rows and defaults, the guitar's included: its key defaults to
`xbox` there too, and `iExtrasResolve` answers `gamecube` ("Not on Android or the Switch yet.")
until the pass has been run on them. The 3DS defaults every key to `gamecube`
(`#ifdef __3DS__` in `iConfigTable.cpp`) and shows no rows (`ExtrasRowShown`) until each feature has
been timed on the console; its renderer also resolves the glow, the spotlights, the water and the guitar's
distortion to `gamecube` ("Not on the 3DS yet."), so only a hand-edited `crowds` or `frame_lock` would take
effect there.

**What the extras cost.** GPU time a frame on the PC (RTX 5070 Ti, OpenGL 3, which runs the GLSL the
GLES path runs; `TSSM_PERF`, 2026-11-17), every key `gamecube` against every key `xbox`:

| Where | Size | all `gamecube` | all `xbox` |
|---|---|---|---|
| TT01's disco, its 12 lights on | 3120x1440 (the phone's render size now) | 0.267 ms | 0.414 ms |
| the same | 2340x1080, 4x anti-aliasing | 0.285 ms | 0.533 ms |
| B401's arena | 3120x1440 | 0.279 ms | 0.352 ms |
| the same | 2340x1080, 4x | 0.312 ms | 0.390 ms |
| BB02 facing its crowd | 3120x1440 | 0.299 ms | 0.298 ms |

On D3D9 at 3840x2160 uncapped (one desktop PC; frame time, the D3D9 build has no GPU timer):
TT01's disco 1.355 to 1.584 ms, BB02 facing the crowd 0.903 to 0.956 ms, B401 1.179 to 1.255 ms.

B401's CPU time a frame on D3D9 (x64 Release, 1920x1080, 4x, cap 120, frames 1700-2680, medians of
update + render + present): 0.490 ms with every key `gamecube`, 0.60 to 0.68 ms with every key
`xbox` (three runs; a fourth with a 2 s stall, 0.97). The camera's end and begin around each stencil
clear, which the Xbox does and so does the port, are not that cost: a build that cleared inside the
update (3 fewer camera updates a frame) measured 0.55 to 0.69 ms on D3D9 and no better on OpenGL 3
(GPU 0.34 to 0.46 ms against 0.33 to 0.37), so it was not kept (2026-11-17).

- **What is added.** The Xbox glow's chain: 7 more render-target switches ("passes" 10 to 17 in
  all three places) and a few small draws. Each light volume in view: one stencil clear and three
  draws of a 512-triangle cone (TT01, all `xbox` against all `gamecube`: draws 246 to 275, clears
  9 to 14 a frame). The flares: a few squares. The crowd: its animation, on the CPU (BB02 facing
  it: render CPU 0.248 to 0.252 ms a frame; the skinned crowd is drawn with either setting).
- **Tiled GPUs (phones, the Switch).** The volumes add no render-target switch: on OpenGL the camera
  end and begin around each stencil clear keep the frame buffer bound (`gl3device.cpp`'s
  `endUpdate` is empty, `beginUpdate` binds the same one), so the tiles are not written out for
  them; B401 (3 or 4 volumes drawn), TT01 (8 to 10) and BB02 show the same +7 passes, the glow's.
- **Fill is what grows.** TT01's disco beams are long and each is drawn three times: +0.25 ms on
  this GPU at the phone's 1080/4x. A phone's GPU is several times slower, so TT01's disco is where
  the design's phone limit (+0.5 ms GPU at 1080/4x/120, no more missed frames than all
  `gamecube`) is most at risk. It was not timed on the S25 Ultra or the Switch here (their builds
  are the android and switch branches'); that, and the 3DS, is the next stage.
- **Builds.** This branch builds for Android arm64 (NDK r28c, GLES through the GL3 backend, CMake
  Debug): every file of the extras compiles; the link stops only where the android branch supplies
  the pieces (`tssm_main.cpp`'s `android/iAndroid.h`, `zMain.cpp`'s `strupr`), as it does without
  the extras.

## Other platform branches

- A platform with its own window layer (the 3DS's) needs `iWindowSetFrameLock` and
  `iWindowGetFrameLock`; with nothing to pace they can store nothing and return 0, as
  `iWindowNull.cpp` does.
- A platform with its own input layer needs `iPadPromptsDiscRaster` (the null one returns the
  texture's own raster, `iPadHostNull.cpp`).
- `iExtrasGC.cpp` is a game unit (`tssm_game` in `CMakeLists.txt`, beside `iWaterBody.cpp`); a
  platform with a CMake of its own adds it there, or the link misses `iExtrasGCScenePrepare` and
  `iExtrasGCSceneObjects`, which `zScene.cpp` calls under `PLATFORM_PC`.
- `iExtras.cpp` reads the water's answer (`iXboxWaterActive` in `iXboxWater.cpp`,
  `iXboxWaterStatus` in `rw/xboxwater.cpp`) and the packages' platforms (`iFilePackagePlatformOf`,
  `iFileAssetPlatform` in `iFile.cpp`). A platform build without those files needs stand-ins that
  say "no Xbox water" (false, NULL) and "Xbox files" (`IGCPLAT_XBOX`, NULL), as
  `tests/extrastest.cpp` has.
- The guitar's distortion: `iXboxDistort.cpp` is a game unit (`tssm_game` in `CMakeLists.txt`,
  beside `iExtrasGC.cpp`); a platform with a CMake of its own adds it there, or the link misses
  `iXboxDistortSetAmount`, `iXboxDistortActive` and `iXboxDistortRender`, which
  `zEntCruiseBubble.cpp` and `zScene.cpp` call under `PLATFORM_PC`, and `iXboxDistortStatus`,
  which `iExtras.cpp` calls. `rw/xboxdistort.cpp` comes with the `rw/*.cpp` glob. Android and the
  Switch run its GL3 arm (`rw/shadersgl/xboxdistort.frag`, written as the other passes' are;
  `iXboxDistortRegisterShaderUniforms` is called from `RwEngineOpen`) and need the Xbox files'
  `plat.HIP`; neither has been run, so `iExtrasResolve` gives them `gamecube` for now
  (`iEXTRAS_RENDERER_DEVICE_GL`, from `CurrentRenderer` in `iExtras.cpp`): once the swirl has
  been seen right on a device, that case goes. The swirl turns on `std::chrono::steady_clock`. The 3DS
  resolves it to `gamecube` (`iExtrasResolve`), and a renderer without Direct3D or GL3 gets
  `rw/xboxdistort.cpp`'s stand-in, which draws nothing.
- The 3DS: the `[extras]` defaults are already `gamecube` there and the overlay shows no rows
  (above). Its CMake globs `src/SB/Core/pc/rw/*.cpp`, so `rw/lightvolume.cpp` (with
  `iLightVolumeForgetCones`, which `zSpotlight::setup_all` calls) and `rw/xboxglow.cpp` come with
  the merge; neither draws there, since `iExtrasResolve` gives the 3DS renderer the GameCube glow
  and spotlights.
