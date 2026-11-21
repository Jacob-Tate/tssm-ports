# The PC port's rendering: the console's screen effects

RenderWare draws almost all of TSSM, and the port runs RenderWare's C API on librw (D3D9, D3D11,
OpenGL 3 and Vulkan, `src/SB/Core/pc/rw`). A few effects are not RenderWare at all: on the GameCube
they work directly in the embedded frame buffer (the EFB) through GX, the console's graphics API.
This page says what each of those does on the console, how the port draws it, how to switch it off,
and what is still missing.

Every pass below draws into the port's virtual screen, the render target of the render size
(`[video] aspect` and `height`, docs/WIDESCREEN.md) that each backend scales into the window. Each
pass covers the whole screen, not just the 4:3 UI box. None of them uses a pixel
shader of the port's own: each step is librw's 2D pipeline (texture times vertex colour) with the
console's blend states, so the passes work on every backend, the Direct3D 9 fixed-function path
included. The water's Xbox look (below) is the exception: it is the Xbox release's own effect, not
the GameCube's, and it needs shaders.

| Effect | Setting | On the console | On PC |
|---|---|---|---|
| Shadows | none | `xShadow.cpp` | the shadow camera has its own target, drawn before the scene; the EFB corner save is skipped |
| Glow | `[video] hdr_glow` | `xFXHighDynamicRange.cpp`, GX | `iHDRGlow.h`, `rw/hdrglow.cpp`; with `[extras] glow = xbox` the Xbox release's chain instead, `iXboxGlow.h`, `rw/xboxglow.cpp` (`docs/EXTRAS.md`) |
| Screen warp | `[video] screen_warp` | `xScreenWarp.cpp`, GX | `iScreenWarp.h`, `rw/screenwarp.cpp` |
| Lens flares | none | `iScrFX.cpp`, never drawn | not drawn (see below) |
| The Sonic Wave Guitar's screen distortion | `[extras] guitar_distortion` | not drawn on the GameCube (`cruise_bubble`'s `distort_screen` is empty); the Xbox's `0x1afcb0` | with `xbox`: `iXboxDistort.h`, `rw/xboxdistort.cpp`, a pixel shader of its own (`rw/shaders/xboxdistort_PS.hlsl`, `rw/shadersgl/xboxdistort.frag`) over a copy of the frame; not on D3D9's fixed path or Vulkan (`docs/EXTRAS.md`, "guitar_distortion") |
| Spotlights' light volumes and lamp flares | `[extras] spotlights` | not drawn on the GameCube (`xLightVolume.cpp` empty, flares collected and never drawn); the Xbox's `0x17ece0`, `0x60110` | with `xbox`: `iLightVolume.h`, `rw/lightvolume.cpp` (stencil), the flares in `zSpotlight.cpp` (`docs/EXTRAS.md`) |
| Movies on textures | none | `xFMV.cpp`, Bink; no retail use | not ported (see below) |
| Water (the Xbox files' water bodies) | `[extras] water` | plain models, some of them goo; the Xbox has its own `xWater`/`iWater` | the GameCube look: `iWaterBody.h`; the Xbox look: `iXboxWater.h`, `iXboxWaterPass.h`, `iXboxWaterDraw.h`, `rw/xboxwater.cpp` |

The two `[video]` settings, the glow and the warp, are on by default, which is the console's look.
They can be changed in the overlay's Settings tab (`docs/GUI.md`) while the game runs. The water
(`[extras] water`, `docs/EXTRAS.md`) is `xbox` by default and changes when a level loads.

## Shadows: the top-left corner

The GameCube renders the shadow camera into the top-left 128x128 of the EFB, the same memory the
picture is in. So `ShadowCameraUpdate` copies that corner out first (`GCSaveFrameBuffer`), renders the
shadow, copies it to the shadow texture, and draws the saved corner back (`GCRestoreFrameBuffer`).

librw gives the shadow camera its own render target, so the picture is never touched. Before the fix
the copy did nothing and the restore drew an empty texture: a black 128x128 square at the top left of
every frame that had a shadow. Now the copy and the draw are skipped under `PLATFORM_PC`
(`xShadow.cpp`); the render states set around the draw are still set. The
shadows themselves already rendered and are unchanged.

The shadows are drawn before the scene, not in the middle of it ("What a frame costs the GPU"
below).

## The glow (`xFXHighDynamicRange`)

The glow is driven by the frame buffer's **alpha channel**. During a frame:

1. Once the opaque world is drawn, `xFXHighDynamicRangeStartRender` fills the alpha channel with the
   scene's background glow (10 of 255 by default) and nothing else.
2. From then on only draws that "brighten" write alpha (`xFXHighDynamicRangeBrighten`, glowing model
   buckets, decals and particles); `iDrawSetFBMSK` masks alpha writes for everything else. On PC
   `iDrawSetFBMSK` is librw's colour write mask, so this part runs unchanged.
3. `xFXHighDynamicRangeRender` turns the alpha into a glow, after the warp and before the HUD.

What the console does in step 3, and what the port does:

| Console (GX) | PC |
|---|---|
| Copy the 640x480 EFB at half size, with the copy's box filter: 320x240. | Copy the virtual screen and halve it (one bilinear sample on the corner of each 2x2). Above 960 wide it is halved again, until it is at most 480 wide. |
| Draw that into the EFB's top-left 128x128 through five TEV stages that each add `texel x C0`, clamped: `min(1, 5 x C0 x texel)` in all four channels. Blend SRCALPHA/ZERO: colour times alpha, alpha squared. Copy the corner out. | The same into a 128x128 texture of its own: a draw at `min(1, 5 x C0)` with ONE/ZERO, then additive draws for the rest (the target clamps the sum, as the TEV clamps each stage), then that drawn into a second 128x128 with SRCALPHA/ZERO. |
| Two more passes halve the corner again, but the result is never read. | Not drawn. |
| Put the corner back. Draw the 128x128 over the whole screen, bilinear, colour times 0x80 and alpha times 1, blend SRCALPHA/ONE, alpha writes off. | The same. |

`C0` is computed as the console computes it: `255 x (2.4 x overbrighten + 0.2)` stored into a byte,
which keeps the low eight bits. At the default overbrighten of 3 that is 1887, kept as 95, so the
scale is 5 x 95 / 255 = 1.86. The menus' glow uses overbrighten 1: 663, kept as 151, scale 2.96.
Altogether a pixel adds `0.5 x min(1, k x colour) x min(1, k x mask)^3` around itself, so the glow
is strong only where the mask is near full: the police sirens' red, searchlight beams, glowing
models.

The chain is logged once: `tssm: HDR glow: 640x480 -> 320x240 -> 128x128, bright scale 95/255 x 5`.

Differences that remain: the console's EFB holds 6 bits per channel (RGBA6_Z24) and the virtual
screen 8; above 640x480 the first step averages more than the console's 2x2 box; with fewer than two
`downsamples` (a scene's configuration) the console composites whatever its texture held from before,
and the port composites the last glow it drew. The GX state the console leaves behind (its blend mode
and alpha update stay set until something else sets them) is not reproduced: the port restores
RenderWare's states after the pass.

## Camera textures: depth of their own

King Neptune's scope (`zNMETypeBossNeptune.cpp`, B401) draws the whole scene through a second camera
into a 128x128 `xFXCameraTexture`. On the console that camera draws into the EFB, which has depth, and
the picture is then copied out, so the game never gives the camera a z buffer. librw draws it into the
texture's own render target, which has no depth unless the camera has a z raster. With none, whatever
was drawn last was on top: SpongeBob came out inside-out in the scope and the frozen Mr. Krabs was
painted over. `xFXCameraTexture::create` now gives the camera a z raster under `PLATFORM_PC` when the
caller asks for depth (`need_zwrite`). The retail game in Dolphin shows the same picture in the scope.

## The screen warp (`xScreenWarp`)

Shockwaves, explosions, heat haze, the Tiki and enemy-spawn effects, the car's shockwave and the
rest call `xScreenWarp::emit`. The game keeps a 17x17 grid of texture coordinates over the screen
(16 cells a side), and each active warp pushes its points about. The console then copies the EFB into
a 640x480 RGB565 texture and draws it back as 16 columns of triangle strips over the grid: texture
colour only, ONE/ZERO, no depth test, alpha writes as `iDrawSetFBMSK` left them.

The port copies the virtual screen and draws the same strips at the render size. It leaves out
retail's 17th strip, which starts at the right edge of the screen and reads one row past the grid.
The copy keeps 8 bits per channel where the console's keeps 5-6-5. The warp's shape is computed for a
4:3 screen (`uvmap_yscale` is 480/640), so on a wider render size it is wider than it is tall, as the
game's own numbers make it.

## Water

The Xbox release draws water in two levels with a system the GameCube build does not have: the
tanks of SC02 (Google-Eyes and Smelly Knick Knacks, two water bodies) and the sea of B301 (Dennis
Strikes Back, one). Each is an `effect:water_body` object (DYNA) in the Xbox files only. It names
a body (a model whose mesh gives the water's extent and height), a list of models to draw into a
refraction target and a list to draw, mirrored, into a reflection target. The GameCube build has
no water renderer: its levels draw water as plain models, some of them goo that wobbles
(`zFXGoo`), and the textures that scroll do so through their surfaces. With the Xbox files,
`[extras] water` (one of the Xbox extras, `docs/EXTRAS.md`) picks how their water bodies look:

- `xbox` (the default): the Xbox look, the Xbox release's own water (`iXboxWater.h`).
- `gamecube`: the GameCube look, the water of the GameCube files (`iWaterBody.h`). It is also what
  a level gets whenever the Xbox water cannot run there (below).

Both looks make one object for each water body, `zWaterBody` (a dynamic type of `zScene.cpp`, after
`sInitTable`'s types, so it gets SceneBegin after the dispatchers). Collision and gameplay are those
of the files in use. A level whose files have no water body (every GameCube file, and every other
Xbox level) is unchanged and logs nothing.

**The look comes from the code, not the setting alone.** The GameCube release has no water
renderer, and its files hold none of the Xbox water's data: no `effect:water_body` in any level and
nothing in `plat.HIP` (the Xbox's holds the 17 bump frames). So with the GameCube files a level
always shows the GameCube files' own water, whatever `[extras] water` says, and the overlay's row
says so ("This level's files are GameCube files: they have no Xbox water."). With the Xbox files
and `gamecube` the water is the GameCube look below, made from the Xbox files (SC02's wobble is
coarser than with the GameCube files' own meshes, below). Which files a level has is decided
for each level, from its own package (`iExtras.h`), so a mod's GameCube level among Xbox files gets
the GameCube water.

Checked for SC02 and B301 (2026-11-17, x64, D3D9, the fixed-step test copy, every `[extras]` key
`xbox`):

| Files | Water | Log |
|---|---|---|
| Xbox files | the Xbox water | `tssm: water: Xbox water in B301: 1 body; ...` |
| the GameCube disc's | the GameCube files' own water | the level's `tssm: extras:` line only: `water gamecube [asked for xbox: This level's files are GameCube files: they have no Xbox water.]`; no water body, so no water line |
| Xbox files with the disc's level as a mod | the same as the GameCube disc's | the same one line |
| the GameCube disc with the Xbox files' level as a mod | the GameCube look of the Xbox files' bodies | `tssm: water: GameCube water in B301 -- plat.HIP has no bumps0 (GameCube or incomplete files); ...`, and the level's line gives that reason |

With the GameCube files (disc or mod), `water = xbox` and `water = gamecube` give the same frames
(SC02 10 of 10, B301 6 of 6), and every one of these runs gives the same frames as the build before
the GameCube files' extras.

### The GameCube look

Without a renderer for the water bodies, the Xbox files would show B301 with no sea at all (only
the GameCube file's dispatcher shows `WATER_BODY_NGC_SIMP`), with the Xbox reflection helpers (an
island, a vertical sun glow, copies of the buoys and boats) drawn as plain models and the Xbox
file's dispatcher making its lavender reflection sky a sky dome; and SC02's ponds as a flat, almost
invisible surface that never moves, plus six reflection and refraction models for the first second
after every scene start. `iWaterBody.h` draws those levels as the GameCube files do, in two steps:

- Before the scene reads its pipe tables and makes its objects (`iWaterBodyScenePrepare`, from
  `zSceneInit`): the helpers' visible bits are cleared in their assets, so they start hidden and
  every reset keeps them hidden, and SC02's ponds get the GameCube water's pipe settings (alpha
  blended, no light kit, and the UV transform that `xFXanimUVAtomicSetup` needs) and its surface
  `WATER_NGC_SURF` (a scroll of 0.1 a second).
- At every SceneBegin (`iWaterBodySceneBegin`, from the event handler of each water body the Xbox
  water does not take): the helpers are hidden again in case a link showed one, B301 shows the sea
  and the reflected sky and makes the sky a sky dome, as the GameCube file's `WATER_NGC_DISP` does,
  and SC02's ponds become goo with the GameCube wobble (0.25, 10, 0.25, 9).

The files may be modded, so both steps read or write an asset only when it is the type they expect
in the file (`SIMP`, `PLAT` or `GRUP`), large enough, and starts with its own ID; a group's items
past its end are not read, and a walk through groups stops after 1024 assets or 4 groups deep.

The Xbox package holds those pipe settings for two GameCube-style meshes nothing places
(`sc02_water_body_gc_01/02`), which are not used themselves: they have another texture at a quarter
alpha and look much fainter than the GameCube water. The Xbox water models keep their own mesh, a
single layer of 32 triangles per pond, where the GameCube draws two layers of 1024 triangles over
both ponds with tinted vertex colours, so the wobble is coarser; the texture, `sc02_caustics`, is
the GameCube's.

### The Xbox look

The body and both lists are hidden from the normal drawing. The water itself is a grid of flat
tiles over the body's bounds (16 units in SC02, 64 in B301), drawn once a frame with one vertex and
one pixel shader.

What the port reproduces, from the Xbox executable (the addresses are DEFAULT.XBE's; the research
notes are outside the repository and none of the Xbox's code, shader microcode or data is in it):

- **The objects** (`iXboxWater.cpp`): `zWaterBody::load`, `setup`, `setup_all`, `update_all`,
  `reset_all` and the hide (`stay_hidden_dammit`), in the order and places the Xbox calls them
  (`zScene.cpp`, `zGame.cpp`, all `PLATFORM_PC`).
- **The tiles** (`iXboxWaterGrid.h`, `tssm_watertest`): the grid over the mesh's world bounds,
  SC02's mask (only tiles with a mesh vertex inside), 17x17 vertices a tile (33x33 in B301) as one
  triangle strip, and each frame's cull (200 units in SC02, 358 in B301, and the view).
- **The ripples**: 17 du/dv frames, `bumps0`..`bumps16`, read from the player's own `plat.HIP` at
  run time (the first 0x8000 bytes of each: 128x128, two signed bytes a texel). The clock runs
  through them in 0.83 s (B301: 0.25 s). The data's quirk, dv = -1 wherever du is negative, is
  kept.
- **The two targets** (`iXboxWaterPass.cpp`): a copy of the scene camera draws the refraction list
  and the player into one, and the reflection list, mirrored about the water's height, into the
  other. The Xbox's are 512x512 for its 640x480 screen; the port keeps its texels per screen pixel
  (0.8 across, 16/15 down) at any render size, to a multiple of 16 and at most 2048 (1024 on
  Android and the Switch): 1536x1152 at 1920x1080. `TSSM_WATER_RT=512` gives the Xbox's size.
- **The surface** (`rw/xboxwater.cpp`, `shaders/water_VS.hlsl` and `water_PS.hlsl`,
  `shadersgl/water.vert` and `water.frag`): rewritten by hand from the decoded shaders. Per vertex,
  the screen position over the screen's size (interpolated as a texture coordinate, as on the
  Xbox, not taken per pixel) and the fog factor. The Xbox's shader writes
  `oFog = 1.00025 - 0.0025006 w`, but the NV2A takes a vertex shader's oFog as the distance into
  the fog table, not as the fog amount, and the water forces fog on and leaves the table as the
  scene set it: linear, from the camera's fog distance to its far plane (`0x1CF780` copies them
  whenever the scene's fog is set; the game's fog is always linear, `0x15E800`; Direct3D programs
  the table at `0x225F10`). So the factor is `(far - oFog) / (far - fog distance)`, clamped to 0..1,
  with the two distances from librw's fog constant. oFog is at most 1.00025, and with the fog from
  50 to 500 in B301 and from 0 to 700 in SC02 the factor is 1 in B301 and at least 0.9986 in SC02:
  the surface is not fogged. Per pixel:

      d   = bump frame (du, dv), signed
      o1  = refraction matrix x d     (-0.02 0.05; 0.05 -0.02) x min(1, 10 / distance)
      o2  = reflection matrix x d     (-0.02 0.25; 0.25 -0.02) x 20 / distance (B301: 30)
      r   = refraction target at screen + o1      (bilinear, clamped)
      l   = reflection target at screen + o2
      rgb = saturate(tint.rgb x r + tint.rgb x tint.a x l), mixed towards the fog colour by the factor
      a   = r.a

  `tint` is (0.55, 0.65, 0.85, 0.5); SC02 has rgb (0.6, 0.8, 0.9). The distance is the tile's
  centre's from the camera. The surface is opaque (blend ONE/ZERO), with z test and z write on,
  no alpha test, and the scene's cull mode and colour write mask; its alpha, the refraction's, is
  what the glow reads where the scene lets the water write alpha (it mostly does not: alpha is
  masked after `xFXHighDynamicRangeStartRender`). The Xbox binds a fresnel texture too and never
  reads it, so it is not loaded.

In B301 the refraction list is empty, so the refraction target holds only SpongeBob on black, and
the sea is half the tint of its reflection: dark, with the mirrored sky and islands in it, up to a
sharp horizon. Frames of the Xbox release in an emulator (Cxbx-Reloaded, which puts oFog through
the fog table the same way) show a flat dark sea with a sharp horizon too, a little more purple:
about (55, 48, 74) at the horizon where the port has (42, 45, 60) at a similar spot. That emulator
also alternates it with a churning white sea from frame to frame, so it cannot settle what the
console shows. The port's sea has not been tuned towards it.

Three of B301's hidden models (`REFLECTION_ISLAND_SIMP`, `REFLECTION_SUN_SIMP`,
`REFLECTION_SKY_XBOX_SIMP`) stay in the simple-object draw list and so are still drawn in the
scene, as on the Xbox. Its simple-object draw loop (`0x8D1F0`) reads only the slot's own bit
(slot + 0x1C), never the entity's visible bit, as the GameCube's does (`zEntSimpleObj.cpp`). The
slot is set when the scene is set up (`0x99610`, from `zGameSetup` before the water's setup); the
water's hide (`0x29E40`) and reset (`0x29F70`) leave it, and only `stay_hidden_dammit`
(`0x29E00`) clears it, which the water's update (`0x2A060`) calls for the body alone. The port
does the same. They are under the opaque sea: taking them out of the list in a test build did not
change the picture. The log says so at the surface's first frame and 600 frames later. SC02's are
all out of the list by then (the level's own timer hides them).


**What it costs.** Measured on the PC (RTX 5070 Ti, GL3 at 1920x1080 with 4x anti-aliasing, no
frame cap; the GL3 timer query and the frame metrics of `TSSM_PERF`), the same places with
`water = xbox` and `gamecube`, medians:

| | GPU ms a frame | the game thread's render, CPU ms |
|---|---|---|
| SC02 at the tanks | 0.234 to 0.289 (+0.055) | 0.117 to 0.153 (+0.036) |
| SC02, second spot | 0.232 to 0.264 (+0.032) | 0.116 to 0.149 (+0.033) |
| B301 | 0.313 to 0.367 (+0.054) | 0.231 to 0.263 (+0.032) |

Two targets of 1536x1152 a body at this size, about 14 MB each with its depth: 4 in SC02 (56 MB,
drawn before the scene) and 2 in B301, and a 16x16 pair for a body out of view (below).

**Vulkan** has no water shaders. Its blobs are SPIR-V, which needs a dxc with SPIR-V output (the
Vulkan SDK's, not the Windows SDK's), and none was built for the water:
`shaders/make_shaders.cmd` builds its Direct3D blobs only. Vulkan uses the GameCube look and
says so.

**Drawn before the scene.** The Xbox draws each body's two targets in the middle of the scene,
between `xLightVolume::render_all` and `xDecal::render_all`, and its surface right after. On a
phone's GPU every target drawn there means leaving the multisampled frame and coming back into it
("What a frame costs the GPU", below), so the port draws all the targets before the scene's camera
begins, beside the early shadows, under the render state the Xbox's place had the frame before,
with a pair of targets per body. Where that place's render state or fog turns out different this
frame, the scene's camera has moved since (a cutscene), or a Direct3D 9 device was lost or reset
in between, the targets are drawn again there. The surface is always drawn at the Xbox's place.
`TSSM_WATER_EARLY=0` draws the targets only there, as the Xbox does, into one pair.
`TSSM_WATER_VERIFY=1` draws every early target again at that place and compares the two.

Drawing the player before the scene moves the effects of its `Render` earlier in the frame, in
SC02 and B301 only. The lasso's skip-a-frame bit is used up there, so on the frame Patrick's lasso
starts, the scene draws it, where on the Xbox the scene leaves it out and the refraction target
has it. The effect models its `Render` adds to the alpha list (the cart shield, the macho bowl's
sphere) are sorted in with the scene's, where the Xbox adds them after the sort.

**A body out of view.** When none of a body's tiles is in view (all beyond the cull distance or
outside the view), nothing reads its targets, but its passes are not simply skipped: the
player's `Render` in the refraction pass has effects the game sees (it emits bubbles, drawing a
random number, uses up the lasso's bit and adds to the alpha list), and the render state the
passes leave is what the scene goes on with. So they make every call they would, into a 16x16
pair that nothing reads: the same frame, without the pixels. `TSSM_WATER_STANDIN=0` draws them
into the body's own pair, for comparison. The log says so the first time in a level, every
1000 times after, and at the level's end.

**Phones and the Switch.** The targets drawn before the scene are what keeps the multisampled
frame from being written out and read back around every target. On Android and the Switch the
targets are at most 1024 on a side and draw with one depth buffer between them
(`iXboxWaterPass.cpp`, `sSharedDepth`): each is cleared, drawn and finished before the next and
only its colour is read afterwards, so a depth buffer each, as on a PC, would only hold memory
there (two a body; eight in SC02, as much again as the colour). `TSSM_WATER_DEPTH=shared` gives a
PC the same, and `own` gives each its own anywhere; the log's targets line ends "one depth buffer
for all" when it is shared. The stand-in pair keeps its own 16x16 one. The cost on the Switch's
GPU has not been measured (docs/SWITCH.md).

**Co-op.** The water is opaque, so what is under it shows only through the refraction target, and
the Xbox draws only the player into that. A co-op partner wading in an SC02 tank would lose their
legs. `iXboxWaterSetPartnerDraw` (`iXboxWater.h`) takes a function that draws the partner's model,
which every refraction pass calls right after the player's: inside the target's camera, with
`SDRS_OpaqueModels` set afresh (whatever the player's `Render` left), before the scene's camera has
begun this frame, and once per body per frame (out of view too, into the stand-in). It must only
draw (not move, animate, or work out the name tag from the scene camera). The co-op branches fill it
once at start-up; `iCoopRenderPartner` as it is cannot be used, because it also places the name tag
from the scene camera.

**Differences from the Xbox that remain**: the Xbox's combiners have 9 bits of precision and the
port's shaders use floats (2 of 255 at most); the targets are larger than 512x512 above 640x480,
so they are sharper; the Xbox's half-texel offset of Direct3D 8 is not reproduced; the per-level
tint, clock speed and reflection strength carry over from the last level that set them on the
Xbox, where here a level without its own values gets the defaults (only a modded level can tell).
The fog: the Xbox's fog table takes its two distances from whichever camera was current when the
scene's fog was last set (with the fog off, the last ones stay), the port's from the camera it
draws with; in both the factor is 1, or within 0.0015 of it. That the NV2A puts a vertex shader's
oFog through the table rests on Direct3D's programming of it (`0x225F10`, which for no table
passes the factor through as oFog) and on the emulator, which does the same; it was not seen on a
console.

### Which look a level gets

Decided once a level, when it loads. The Xbox look needs the level's `[extras] water` decision to
be `xbox` (`iExtrasInForce`, made in `iExtrasSceneEnter` just before: the setting, the level's
package the Xbox release's, not Direct3D 9's fixed path or the 3DS), a water body whose model
exists, the 17 frames in `plat.HIP`, a renderer with the shaders (Direct3D 9's shader path,
Direct3D 11, OpenGL 3, OpenGL ES 3), and the shaders, buffers and targets made. Otherwise the level
gets the GameCube look, and one log line says why and what the GameCube look's first step did:

    tssm: water: GameCube water in SC02 -- extras.water is gamecube; 2 water bodies drawn the GameCube way: 7 reflection/refraction models hidden, 2 given the GameCube water's pipe settings
    tssm: water: GameCube water in SC02 -- extras: "This level's files are GameCube files: they have no Xbox water."; ...
    tssm: water: GameCube water in B301 -- plat.HIP has no bumps0 (GameCube or incomplete files); 1 water body drawn the GameCube way: 7 reflection/refraction models hidden
    tssm: water: GameCube water in SC02 -- Direct3D 9's fixed path has no water shaders; ...
    tssm: water: GameCube water in SC02 -- the Vulkan renderer has no water shaders; ...
    tssm: water: Xbox water in SC02: 2 bodies; targets drawn before the scene

The overlay's **Water** row (Xbox extras) says why when the level in play got the GameCube water
although the setting is `xbox`: the `[extras]` reason, or the water's own (`iExtrasStatus`, from
`iXboxWaterActive()` and the line above), and the level's `tssm: extras:` line says
`water gamecube [asked for xbox: ...]` then.

The decision comes in two steps, because the GameCube look changes some assets before the scene
reads its pipe tables and makes its objects, and does the rest at SceneBegin:

- `iXboxWaterWillRun()`, final before the pipe tables are read (`xWaterScenePrepare` in
  `zSceneInit`): everything above that the files and the renderer can answer, the bump textures,
  shaders and targets made, and a water body asset that names an entity with a model. While it is
  false, `zSceneInit` runs the GameCube look's first step right after.
- `iXboxWaterActive()`, final once the objects exist (`xWaterSceneInit`, before `zGameSetup` and
  the SceneBegin events): that, and a body object that can be used (its model has vertices). The
  same check decides for each body: a body whose object cannot be used (or past the 16th that
  can) is left to the GameCube look. While `iXboxWaterActive()` is false, every water body's
  SceneBegin runs the GameCube look's second step; while it is true, only a body left out does.

The two differ only with modded files whose body asset checks out and whose object does not
(`TSSM_WATER_FAIL=objects` makes that case for tests, for every body). The second step then also
clears that body's helpers' visible bits in their assets, so later resets keep them hidden, but the
pipe tables have been read by then: SC02's water keeps the Xbox file's pipe settings and surface,
and wobbles without being blended or scrolled the GameCube way. The log line says so, for every
body or for those left out beside the Xbox water:

    tssm: water: GameCube water in SC02 -- no usable water body (...); the GameCube look from SceneBegin, pipe settings and surfaces as in the files
    tssm: water: Xbox water in SC02: 1 body (left out, the GameCube look from SceneBegin with the files' pipe settings and surfaces: WATER_BODY_02: ...); targets drawn before the scene

## Lens flares: no flare in the game's data

`xScrFx.cpp` builds flares from "effect:Lens Flare Source" DYNA assets at scene start
(`LensFlareSourceCB`), and each frame `xScrFxLensFlare_Render` hands every flare in range to
`iScrFxLensFlare_Render`, which draws its elements after an occlusion test: on the console the flare's
square is drawn into the depth buffer and GX's pixel metrics count what passes
(`DetermineFlareOcclusion`). On PC that test reports the flare hidden, because librw has no
occlusion query.

No level has a flare to draw. None of the 5,469 DYNA assets in the GameCube packages, nor the 5,484
in the Xbox ones, is a lens flare source, flare element or flare parameter set, so the flare list is
empty on the console too and nothing is lost. The test only matters to a mod that adds flares; it
needs an occlusion query in the librw fork (D3D9 and D3D11 occlusion queries, GL_SAMPLES_PASSED).

## Movies on textures: no retail use

`xTextureHandle::PlayMovie` plays a Bink movie into a texture (`xFMV.cpp`, `iFMVOpen`). Only a UI
image (`zUIImage`) calls it, in two ways: when it receives the PlayMovie event, and when its texture
is set to an asset of type `BINK`. Neither happens in the game's data:

- In both the GameCube and the Xbox packages, all eight links that send PlayMovie go to dispatchers,
  which play the movie full screen (`zFMVPlay`, which works). None goes to a UI image.
- Neither data set has a single `BINK` asset (138 GameCube and 136 Xbox packages).

So `iFMVOpen` still returns NULL, and no level or menu loses anything.

## What a frame costs the GPU

A phone's GPU (Adreno, Mali, PowerVR) draws the picture in small tiles, in its own fast memory.
Each time a frame buffer is left for another, everything drawn into it so far is written out to
memory, and each time it is entered again it is all read back in: colour and depth, every sample
of them with anti-aliasing. At 2340x1080 with 4x that is about 80 MB each way, every time. A desktop
GPU does not work like that and barely notices. What the port does about it is below
(`iGpuOpt.h`). What is on by default does not change the picture, apart from the HUD's
anti-aliasing on GLES (the single-sample finish); the savings that would change more are off
unless asked for:

- **The shadows before the scene.** `xShadowManager_Render` runs in the middle of the scene, and
  each shadow it casts (up to three) ended the scene's camera, drew the caster into the 128x128
  shadow raster and began the scene's camera again; the shadow's receivers are drawn right after
  with that raster. Now the game loop runs the manager once more, just before the scene's camera
  begins (`zScenePreRenderShadows` in `zScene.cpp`, `xShadowManager_PreRender` in `xShadow.cpp`,
  both `PLATFORM_PC`), with everything but the shadow cameras switched off and everything it
  changes put back: each shadow is drawn into a raster of its own. When the scene reaches the
  manager, which runs as it always did, each shadow camera update that asks for what was drawn
  early -- the same model at the same place, the same shadow camera, the same render state --
  takes that raster instead of drawing; anything else is drawn there as before (a caster that
  appeared during the scene, a render state that differs). A shadow is drawn early under the
  render state its update met in the scene the frame before, and the update leaves the state
  exactly as drawing it there would have (`iRenderStateSave`, `rw/renderstate.cpp`). Same size,
  same filtering, same order of everything the scene draws. `TSSM_GPU_LOG=1` says how many were
  drawn early and how many in the scene.
- **The glow reads the frame where it is.** The glow's first halving read a copy of the whole
  frame; on GL3 it now reads the virtual screen's own texture (`rw::gl3::virtualScreenReadRaster`),
  which it does not draw into while it reads. The screen warp, which draws back into the frame,
  still copies it. The glow's intermediate targets are cleared before they are drawn over, so
  they are not read back in.
- **GLES: anti-aliasing in tile memory (`TSSM_GPU_MSRTT=1` only).** With
  `EXT_multisampled_render_to_texture` and `_2` (every Adreno, Mali and PowerVR of the last
  years) the multisampled frame is that extension's:
  the samples never leave the GPU's tile memory, and only the folded picture is sure to be
  written out, into the virtual screen's texture. The depth goes into a depth texture, which the
  extension lets the driver discard at the end of a pass (it promises to keep only a depth
  renderbuffer). There is no resolve blit any more. Without the extensions, and on desktop
  OpenGL, the explicit multisampled buffers and their resolve are as before.
  The samples do not outlive a render pass. The camera restarts the game still makes inside the
  scene (the z bias of `xDecal.cpp` and the aim arrow in `zEntPlayer.cpp`, the sky dome in
  `xSkyDome.cpp`) fold them there, so what is drawn after one meets folded colour at edges, and
  whatever depth the driver kept. Measured on the GLES path on a desktop driver, against every
  saving off: TR01 (no such restart) differs by at most 7 of 255; BB03 (4 entries a frame, not 2)
  has thin edge lines at frames 200-500, up to 0.08% of pixels off by more than 8 and up to 182,
  and at frames 800 and 900 the shadows under the two large characters fade over whole regions,
  6.2% and 3.4% of pixels off by more than 8, up to 105 and 116. BB03 frame 800 with
  `TSSM_GPU_MSRTT=1` against every saving off is the PC's check for this path.
  On the phone's Adreno it is worse: the multisampled depth is not kept across such a pass break,
  so in BB03 walls and the counter are drawn over the characters and shadows go missing. So it is
  off unless `TSSM_GPU_MSRTT=1`, and GLES draws into the explicit multisampled buffers, as on
  desktop OpenGL, and finishes the frame single-sampled after the glow (below), which desktop
  OpenGL does not.
- **Depth not written out.** The frame's depth and stencil are discarded at the present
  (`glInvalidateFramebuffer`), as nothing reads them after it, and a depth clear clears the
  stencil too (nothing in the game uses stencil), so a GPU that draws in tiles does not load the
  old depth to keep the old stencil.
  `TSSM_GPU_MSDISCARD=1` (off by default, and GLES only: librw has `glInvalidateFramebuffer` on
  GLES alone, so on desktop OpenGL it does nothing, and the log says so) also discards the
  explicit multisampled buffer whole, every sample of colour too, once it has been folded into
  the picture: after the fold for the glow, or at the present. That is about 40 MB a frame at
  2340x1080 with 4x that a phone's GPU would no longer write out. It is off because the next frame
  reads those samples: a frame clears only depth, and where the scene leaves a sample undrawn
  (cracks between triangles in TR01) or blends onto last frame's colour (larger patches in SC02)
  it shows last frame's colour, which after a discard is whatever the GPU left there. Filling the
  discarded buffer with magenta on the GLES path on a desktop driver changed up to 0.15% of SC02's
  pixels by more than 8 (up to 249).
- **GLES without render-to-texture (every GLES by default): the rest single-sampled.** Once the
  glow (or the screen warp) has read the frame, the rest of it -- the glow's composite, the HUD --
  is drawn into the single-sample virtual screen, with the scene's depth, instead of entering the
  explicit multisampled buffer a second time. That saves a write and a read of every sample a
  frame, and it is the one saving on by default that changes the picture: the HUD is not
  anti-aliased. Most of its edges are in its art; where a letter's quad cuts through the art, the
  quad's edge comes out hard instead of partly covered. On the GLES path on a desktop driver,
  SC02's dialog box differs from every saving off in 0.66-0.78% of its pixels, 0.23-0.24% by more
  than 8 and up to 110, all on the vertical edges of the letters; subtle side by side.
  `TSSM_GPU_OPT=post` puts the anti-aliased HUD back, for a comparison on a phone. Only on GLES
  without the extension: with it, entering the frame again reads the folded colour either way
  (not the depth, above), and on a desktop GPU it costs nothing, so the HUD keeps its
  anti-aliasing there (`TSSM_GPU_POST=always` uses it everywhere, for comparisons).
- **Android: the window's surface at the render size.** On a phone the picture was scaled up into
  the panel's 3120x1440 by the GPU, a whole extra pass every frame. The window's surface now has
  buffers of the render size at the window's shape, and the display scales them as it composes
  the screen (`ApplySurfaceBufferSize` in `iWindowSDL.cpp`; librw's present asks EGL how big the
  surface is). The touches, the touch controls and the settings overlay are laid out in the
  window's size as before and drawn into the smaller surface. Off unless
  `TSSM_GPU_FIXEDSIZE=1` until it has been measured and checked on a phone.
- The window's alpha is no longer cleared after the present where the window has no alpha.

Every one of these can be turned off for a comparison with `TSSM_GPU_OPT` (`iGpuOpt.h`):
`prepass`, `msrtt`, `post`, `invalidate`, `readview`, `fixedsize`, `msdiscard`, or `all`. Three
are off unless asked for: `msrtt` (`TSSM_GPU_MSRTT=1`), `fixedsize` (`TSSM_GPU_FIXEDSIZE=1`) and
`msdiscard` (`TSSM_GPU_MSDISCARD=1`).

Measured on a PC (RTX 5070 Ti, 1280x720, 4x), the hunt profiler counting what each frame does,
over the same 240 frames of TR01's start (three shadows):

| | before | after |
| --- | --- | --- |
| times the scene's multisampled buffer is entered | 2.8 | 2.0 (the scene; the glow's composite and the HUD) |
| full-frame copies and resolves (desktop GL) | 4.0 | 3.0 |
| full-frame copies and resolves (GLES, render-to-texture) | 4.0 | 1.0 (the present) |

`TSSM_GPU_VERIFY=1` draws every shadow taken from the pre-pass in the scene as well and compares:
over 1500 frames of TR01 (3000 shadows) on GL3, D3D11 and D3D9 the rasters were identical to the
last bit and the render state each left was identical, every one. In the same runs 3 shadows were
drawn in the scene instead (each the frame a caster first appeared). Frames captured at the same
places before and after differ no more than two runs of the same build do (the game's timing is not
repeatable from run to run); `tools/regress.py` passes.

The Xbox extras (`docs/EXTRAS.md`, "Per platform") add to this with the `[extras]` keys at `xbox`:
the Xbox glow's chain adds 7 render-target switches a frame; the spotlights' light volumes add a
stencil clear and three cone draws each and no switch (the camera's end and begin around the clear
keep the frame buffer bound on OpenGL), so on a tiled GPU they cost fill, not tile loads and stores.
TT01's disco, the worst place measured, is +0.25 ms on a desktop GPU at 2340x1080 with 4x.
The guitar's distortion adds, only while the wave is steered, one copy of the frame (a multisample
resolve with anti-aliasing on) and one full-screen draw: +0.08 ms of GPU time at 1920x1080 with 4x on OpenGL 3 (`docs/EXTRAS.md`,
"guitar_distortion").

## Switches for testing

| Variable | What it does |
|---|---|
| `TSSM_HDR_VIEW=mask` | Replace the frame with its alpha channel in grey, as the glow pass sees it: the glow mask. |
| `TSSM_HDR_VIEW=glow` | Replace the frame with the glow alone, over black. |
| `TSSM_HDR_VIEW=base` | Xbox glow only: leave the frame without the glow (everything else the same), for a software model of the chain to start from. |
| `TSSM_TEST_MENU_GLOW=<n>` | Xbox glow only: the title and pause menus' glow strength in place of the one picked (`iXboxGlow.h`). |
| `TSSM_TEST_SPOTLOG=<n>` | Every n frames, log the scene's spotlights: how many are on and drawn, how many lamp flares show with `[extras] spotlights = xbox`, whether the camera is in a beam (`iTestEvents.h`). |
| `TSSM_TEST_WARP=frame:type[:radius[:seconds[:intensity]]],...` | Emit a screen warp at the player at that frame (`iTestEvents.h`): `warble`, `lens`, `ripple`, `shockwave`, `heat` or `static`. The defaults (radius 4, 1 s, intensity 1) are the player's own shockwave. |
| `TSSM_DUMPSHADOW=1` | Write the first four shadow textures as TGA files (`rw/engine.cpp`). |
| `TSSM_TEST_DEVICE_LOST=present[:presents],...` | Direct3D 9: take the device as lost from that present for that many presents (60 by default), then as resettable, so the next camera update resets it for real (`iDeviceReset.h`). `reset[:presents]` in place of a present starts that loss right after the next reset instead (the end of the loss before it, a resize, F11): a device lost again just after a Reset that succeeded. The log says when it is lost and when it is back. |
| `TSSM_GL_NO_S3TC=1` | OpenGL: behave as a GPU without S3TC (most phones): every DXT texture is decoded to RGBA on the CPU instead of uploaded compressed (`rw/engine_start.cpp`). The log's `S3TC` line says which path is in use. |
| `TSSM_GL_DEBUG=1` | OpenGL: print every error and warning the driver reports, as it happens (KHR_debug, `rw/engine_start.cpp`). Each message is printed three times at most. |
| `TSSM_GPU_OPT=<list>` | Turn savings of "What a frame costs the GPU" off: `prepass`, `msrtt`, `post`, `invalidate`, `readview`, `fixedsize`, `msdiscard`, `all` (`iGpuOpt.h`). |
| `TSSM_GPU_MSDISCARD=1` | GLES: discard the explicit multisampled buffer whole, colour too, once it has been folded. Off by default: the next frame reads last frame's colour where its scene leaves a sample undrawn. Desktop OpenGL has no `glInvalidateFramebuffer` in librw, so there it does nothing (the log says so). |
| `TSSM_GPU_MSRTT=1` | GLES: anti-aliasing through `EXT_multisampled_render_to_texture` instead of the explicit multisampled buffers. Off by default: on the phone's Adreno the multisampled depth is not kept across a pass break inside the frame. `TSSM_GPU_OPT=msrtt` still turns it off. |
| `TSSM_GPU_LOG=1` | Every 1200 frames, how many shadows were drawn before the scene and how many in it. |
| `TSSM_GPU_POST=always` | GL3: draw the rest of the frame single-sampled after the glow on every GL, not only on GLES without render-to-texture. |
| `TSSM_GPU_VERIFY=1` | Draw every shadow taken from the pre-pass in the scene as well, as it always was, compare the two rasters (read back) and the render states each leaves, and say so every 300 shadows. Desktop only. |
| `TSSM_GPU_FIXEDSIZE=1` | Android: give the window's surface buffers of the render size, scaled by the display. |
| `TSSM_WATER_DEBUG=<words>` | The Xbox water: `tiles` outlines every tile (green drawn, red culled, grey masked), `rt` shows each body's two targets and the bump frame in the top right, `flat` draws the surface with no ripples, `refract` shows the refraction target alone, untinted (with `flat`, the water should vanish into the picture where the target holds what is behind it: the check that the targets line up), `nosurface` draws no surface. |
| `TSSM_WATER_DUMP=<frames>` | Write each body's targets and the bump frame of those frames as PNGs into `TSSM_CAPTURE_DIR`. |
| `TSSM_WATER_EARLY=0` | Draw the water's targets only where the Xbox draws them, in the scene. |
| `TSSM_WATER_VERIFY=1` | Draw every target drawn before the scene again in it, compare the two (read back) and the render states each leaves, and say so every 300 bodies. Desktop only. |
| `TSSM_WATER_RT=<n>` | n x n water targets (512 is the Xbox's) whatever the render size. |
| `TSSM_WATER_FAIL=<what>` | The GameCube look in place of the Xbox water, as if `bumps` were missing from `plat.HIP`, or the `shaders` or the `targets` could not be made, or the body `objects` could not be used though their assets checked out ("Water", "Which look a level gets"). |
| `TSSM_WATER_STANDIN=0` | A water body out of view draws its targets into its own pair, full size, rather than the 16x16 stand-in (the frames should not change). |

With `TSSM_CAPTURE_FRAMES`, `TSSM_EXIT_AFTER` and `TSSM_INPUT` (`iCapture.h`) these make unattended
checks. The script below starts a new game, answers No to the autosave prompt and skips the story
movie and BB02's opening cutscene; gameplay begins at about frame 1400 (at 60 Hz, with
`[game] intro_movies = off`):

    TSSM_INPUT=400:start,700:a,1000:a,1150:a,1350:a
    TSSM_TEST_WARP=1600:shockwave:8:1:5
    TSSM_CAPTURE_FRAMES=1250,1400,1595,1605,1615

## OpenGL and OpenGL ES (Android)

The gl3 backend draws the desktop's OpenGL 3.3 and the phone's OpenGL ES 3 with the same code.
Until this fix both showed the menus as Android 0.2 did: no backgrounds or panels, the text of every
earlier screen still on the picture, and the menu's bubbles smeared into white columns.

**Cause: the 2D quads were clipped away.** `zUIImage`, `zUIBox` and others set a 2D vertex's camera z
from the near screen z: `cooz = 1 / RwIm2DGetNearScreenZ()`, then `RwIm2DVertexSetRecipCameraZ(v,
cooz)`. The near screen z is the device's: 0 on Direct3D (the game clamps it to 1e-5), -1 on OpenGL,
whose depth range is -1..1. librw's 2D vertex shader multiplies the position by that camera z, so on
OpenGL every such vertex had a clip w of -1 and the whole quad was clipped. Those quads are the menu
backgrounds, the panels and the boxes. Text was not affected, because the fonts take their camera z
from the camera's near plane, which is positive.

The rest followed from that. In a scene without fog (the menus) the game clears only the depth buffer
each frame (`iCameraBegin`), as on the console, because the background covers the whole picture. With
no background, nothing covered the previous frame, so its text and the bubble particles stayed and
piled up. The clears themselves reach the virtual screen and work.

**Fix.** On PC `RwIm2DVertexSetRecipCameraZ` stores the magnitude of the camera z
(`rwsdk/rwplcore.h`). On Direct3D the value is unchanged.

**Textures without S3TC.** Most phone GPUs (Adreno, Mali, PowerVR) cannot sample DXT. Where
`GL_EXT_texture_compression_s3tc` is missing, librw's `xbox_to_gl3` declines the raster, and
`convertTexToCurrentPlatform` decodes every level to RGBA through an Image. Between the title and
BB02, 245 different textures are DXT (276 loads), among them SpongeBob, the loading screen, the
button prompts and the pickups.

That path had one fault of its own. GL's `rasterFromImage` records the alpha (opaque, keyed or graded)
of each level as that level arrives, so the last level, a few texels, decided how the whole texture
was drawn. Between the title and BB02, 49 textures came out graded (blended) where their top level
is keyed (cut out), grass edges and plants among them. All 49 were DXT; with S3TC, no texture
differed. `rw/texture.cpp` now classifies level 0 before the conversion and puts that answer back
afterwards, as the compressed path already does with `classifyDXTAlpha`.

**Testing the phone path on a desktop.**
- `TSSM_GL_NO_S3TC=1` forces the decode path.
- An OpenGL ES context needs a change to librw that the build does not apply.
  `tools/librw_gles_on_desktop.patch` adds `TSSM_GL_ES=1`, which skips librw's desktop profiles. To
  use it, run `git -C third_party/librw apply ../../tools/librw_gles_on_desktop.patch`, build, and
  revert the patch afterwards.
- NVIDIA's Windows driver then gives OpenGL ES 3.2. The log says `GLES profile`, and the shaders are
  compiled as `#version 310 es`.
- With the fix taken out, `TSSM_GL_ES=1 TSSM_GL_NO_S3TC=1` reproduces Android 0.2's menu exactly.

## Decals: the top level only

A projected decal is the SpongeBowl's aiming pin (`bowling_lane`), the throw arrow (`directional`), a
ring's shadow (`shadow_round`), the Tiki helmet's marker (`target`) and a thrown ball's shadow
(`fx_shadow_ball`). `xShadowVertical_DrawCache` and `xShadowReceiveShadow` lay the texture over whole
triangles, with UVs far outside 0..1 and CLAMP addressing, so the transparent border of the texture is
all that hides the rest of each triangle.

On the GameCube each of those textures has one level, so the border holds at any distance. The Xbox
art has a full mip chain, and on the levels below the top one box filtering pulled the art into the
edge rows: `bowling_lane`'s bottom row reaches alpha 119 at level 1 and 187 at level 2. The decal binds
a bare raster and sets no filter, so it took whatever filter the last material left on the stage,
usually trilinear. Once a level below the top was sampled, CLAMP repeated those rows along the whole
triangle: a dark band through the pin, about 30% darker than the sand beside it at a 480-line render,
and the pin itself blurred. It depends on the render size, because a larger render stays on the top
level. The band came and went with which particle was drawn last.

So under `PLATFORM_PC` these draws, and the lane's flat quad over a drop (same texture, UVs 0..1), use
`rwFILTERLINEAR` (`iDecalFilter.h`, `rw/renderstate.cpp`) and put the stage's filter back afterwards.
That is the sampling the console does, on every backend. A shadow drawn from the shadow camera's target
has one level anyway and is left alone.

## Opaque models: the console's order

The game sorts its opaque model buckets once a level is set up (`xModelBucket.cpp`,
`compare_opaque_buckets`): by the pipe table's layer and flags, and where those are equal, by the
address of the model's atomic. On the GameCube RenderWare allocates from the game's general heap,
which hands out memory from the top down (`gc/iMemMgr.cpp`: `xMemoryManagerGeneral::Down`, first
fit), so a model created later has a lower address and is drawn first: in a level, the models read
last come first.

The port's RenderWare heap has no such order (on 64-bit hosts `iLowHeap`'s TLSF block, which fills
upwards, on 32-bit ones the C library's), and drawing in its order mostly made it the other way
round. For a solid model that changes nothing the depth buffer does not put right. It does for a
model the pipe table has no entry for (so it is in the opaque pass, with depth writes) whose texture
or vertex colours are see-through: drawn before what is behind it, it blends with whatever the frame
held there and then hides, by depth, the walls drawn after it. In a level without fog the game clears
only depth (`iCameraBegin`; the console's RenderWare copies the frame out without clearing it, so it
keeps the last frame too), and what the frame held was the last frame's surface: it blended over
itself frame after frame, came out solid, and left trails behind when the camera moved.

A player's GameCube mod showed it (2026-11-20): a level built from simple objects, with no fog and no
pipe-table entries, whose fountain spouts use the `fountain` texture (C8 with a graded RGB5A3 alpha,
like `waterfall_de02`). Over the GameCube disc the port drew them solid bright cyan where Dolphin
shows faint streaks; over the Xbox files, where other things had been allocated first, mostly not.
With the console's order they are the faint streaks Dolphin shows over either.

So every atomic carries the number of the order it was created in (`iModelOrder.h`, a librw plugin
in `rw/atomic_order.cpp`), and under `PLATFORM_PC` the sort compares those numbers, latest first,
where the console compares addresses. The simple objects' list (`zEntSimpleObj.cpp`, `CmpSimpleMgr`),
which sets the order those objects are drawn in, is sorted by the same address first, so it compares
the numbers of their first atomics too; with only the buckets in order the fountain was right in some
runs and solid in others. The order is then the same on every host and width.
`tssm_ordertest` checks the numbering and the order it sorts to.

The console's order is the top-down heap's, not a rule of the game: a block freed during a load and
reused by a later model would put that model's atomic above an earlier one. The creation order is
the heap's order where a load leaves no such hole.

## Looks like a fault, but the console does it too

- **PT01 (Planktopolis): SpongeBob and the enemies dark and greenish after a respawn.** The lava
  lake's trigger boxes give the player `pt_lightkit_lava` when he enters and `pt02_light_kit` when he
  leaves. A death inside a box resets the triggers without a leave event, so he respawns with the lava
  kit, and every enemy takes the player's kit on its first step after the reset. SpongeBob recovers at
  the next trigger he leaves, the enemies at the next reset outside the boxes. The retail GameCube game
  in Dolphin draws the same colours (red-to-green ratio of his head 0.90 there, 0.89 here), so it stays.

## Direct3D 9 device resets

librw resets a D3D9 device when the window's client area stops matching the back buffer and when
Present reports the device lost, and it does not check that the reset can succeed or did. A failed
reset leaves its Im2D and Im3D buffers NULL, and the next 2D draw writes to address 0 (the screen
fade's `iScrFxDrawBox` was where it showed). Two things made resets fail:

- a minimised window restored from another process (the taskbar, the single-instance check raising
  the first copy) is no longer minimised for a moment before it has its size back, and librw read
  that moment's 0x0 client area as a resize to 0x0, which Reset refuses. Only windowed mode: a
  borderless window keeps its client area while minimised.
- librw's D3D9 `rasterLock` keeps a reference to every camera texture it reads back, so after the
  first save thumbnail (or `TSSM_CAPTURE_FRAMES` frame) one default-pool texture could never be
  released, and every later reset -- a resize, Alt+Enter, F11 -- failed.

A reset that succeeds could crash as well. librw binds the index buffer, the vertex streams and the
vertex declaration only when they differ from its cache of what is bound, and destroying a geometry
leaves its buffers in that cache, alive only through the device's binding. Its reset unbinds the
index buffer, the declaration and stream 0 on the device without clearing the cache, which frees
them, and the restore after Reset binds what the cache names. A movie makes no indexed draws, so
the index buffer of the scene before it was still cached, and resizing the window during
`story1.bik` crashed in `restoreD3d9Device`.

`rw/devicereset.cpp` (`iDeviceReset.h`) takes over librw's `beginUpdate`, `showRaster` and the
D3D9 `rasterLock` without changing librw: it lets librw reset only with a client area that is not
empty, a window that is not minimised and a device that `TestCooperativeLevel` says can be reset,
releases the overlay's default-pool objects first (and also when it holds a reset back from a
window that is not minimised, because librw reads the window again and a size that arrives in
between still resets), empties librw's binding cache through librw's own setters at every camera
update and present (all but the constant vertex stream, which lives as long as the device), resets
a device that can be reset and has not been at the next camera update, gives back the reference
`rasterLock` keeps, and while the device is lost skips Im2D, Im3D, `RwRasterRenderFast`, the
present and the screen passes. A reset that the device refuses -- it still asks to be reset
afterwards, or librw comes out of it holding less than it went in with -- stops the picture for
good, because librw cannot safely try again; a device merely lost again right after a reset (an
exclusive full screen switched away from at that instant) is held and reset again once it can be.
`TSSM_TEST_DEVICE_LOST` (above) walks both paths with a real Reset.

## Changes to librw

`third_party/librw` stays pinned at the upstream commit. The port's fixes to it are patches in
`tools/librw/`, which CMake applies to the checkout when it configures (`TSSM_LIBRW_PATCHES`, on by
default), so the desktop builds and the Gradle/Android build all get them. A patch that is already
in is left alone, and a patched file that is reverted is patched again at the next build. To change
one, edit the file in the submodule and write the patch again with
`git -C third_party/librw diff <file> > tools/librw/<name>.patch`.

A checkout that applied the old version of a patch that has since changed cannot take the new one:
the configure stops, saying that `tools/librw/<name>.patch` neither applies nor is in already.
Start librw from clean then -- `git -C third_party/librw checkout -- .` and
`git -C third_party/librw clean -fd` (the second removes the files the patches add, such as
`src/gl/gl3perf.inc`) -- and configure again. Both throw away edits in librw that are not in a
patch yet. A second patch on top of the first does not get round it where it edits the first
one's own lines: from then on the first neither applies nor reverse-applies, and every configure
stops.

- `d3d9_exclusive_reset.patch`: exclusive full screen on Direct3D 9 is reset at its display mode's
  size (`resetDevice`, called from `rw/devicereset.cpp`) and never at the window's: switched back
  to from another program, its window is for a moment no longer minimised and still the size of a
  title bar, librw reset to that, the reset was refused, and librw then read a render target the
  device had not handed back (a crash, or a picture that stopped). `restoreVideoMemory` also
  releases only what it was given.
- `xbox_destroy_raster.patch`: the Xbox raster's destructor was empty. The game's textures are Xbox
  rasters, converted to the running device's own when they load, and the Xbox raster is destroyed
  right after; its texels and palette stayed allocated, about 1.6 MB of the RenderWare heap per
  level, and around 150 level changes filled the heap.
- `d3d11_altenter.patch`: librw turned off DXGI's own Alt+Enter on the factory it lists display
  modes from, but `D3D11CreateDeviceAndSwapChain` makes the swap chain with a factory of its own,
  and a window association belongs to one factory. DXGI's Alt+Enter stayed on beside the port's
  own (borderless and windowed). It is now turned off on the swap chain's factory.
- `perf11_gpu.patch`: what a frame costs a tiling GPU ("What a frame costs the GPU" above):
  `EXT_multisampled_render_to_texture` for the virtual screen on GLES (off in `VSOPT_DEFAULT`),
  the single-sample finish (`VSOPT_SINGLESAMPLEPOST`), depth and stencil discarded at the present
  and, with `VSOPT_DISCARDMS` (off by default, GLES only), the explicit multisampled buffer
  discarded whole once it is folded, the frame lent out as a texture (`virtualScreenReadRaster`),
  the present into the surface's own size (`getWindowDrawableSize`), and no alpha clear of a
  window without alpha. `setVirtualScreenOptions` switches each.
- `matfx_uvtransform.patch`: the material-effects pipelines drew a `UVTRANSFORM` or
  `DUALUVTRANSFORM` material as a plain one, so no animated surface moved: conveyor belts, lava,
  water, waterfalls, caustics, clouds, the robots' eye glow and Patrick's cartwheel shield. The game
  animates a surface's texture through RpMatFX (`xFXanimUVAtomicSetup` in `xFX.cpp` points the
  material at a matrix it rewrites before every draw, and some models carry the effect in their
  DFF). The D3D9/D3D11 and GL3 matfx pipelines now read the material's base matrix at draw time and
  draw with the UV-transforming vertex shaders the default pipeline already has. The skinned matfx
  pipelines do the same with new UV-transforming builds of the skinning shaders (`skin_VS.hlsl` and
  `skin.vert` with `UVXFORM`; the transform sits at c239-c240 there, past the bones): the sea
  monster's back in the trench levels is a skinned conveyor (`hump_hump_bind`). The fixed-function
  D3D9 mode sets each such mesh's transform as the texture matrix. Two gaps: the second pass of
  `DUALUVTRANSFORM` is still not drawn (no surface in the game's files asks for it), and the Vulkan
  backend has no SPIR-V build of the skinned shaders (`make_shaders.cmd` needs a dxc with SPIR-V
  output), so a skinned transform draws untransformed there.
- `perf11_cpu.patch`: what the GL3 backend costs the game thread per draw, which matters where the
  GL driver runs on the calling thread and checks the vertex state again at every draw (a phone's
  GLES driver). Every instanced geometry and both immediate-mode streams get a vertex array object
  (`useVaos`, wherever the context is GL 3.0 or GLES 3.0), so a draw binds one object instead of
  binding two buffers and enabling, pointing and disabling each attribute; the binding is cached,
  and the default object is bound again after instancing so that GL calls from outside librw that
  set attributes without an object of their own cannot reach a geometry's. Two bugs in upstream's
  unused VAO path are fixed with it: im3d bound im2d's object, and a geometry's object was deleted
  with `glDeleteBuffers`. The im2d and im3d stream buffers are given storage of the batch's own
  size with the batch in it, in one call, instead of a 280-360 KB orphan and a write for every
  primitive. And the swap interval is set when it changes rather than before every swap (SDL3):
  each `SDL_GL_SetSwapInterval` reaches `eglSwapInterval` or `wglSwapIntervalEXT`, and NVIDIA's
  waits on its driver thread. `TSSM_GL_VAO=0`, `TSSM_GL_STREAM=0` and `TSSM_GL_SWAP_CACHE=0` put
  each old path back, for comparing runs of one build. Measured on the PC (GL3, 2340x1080, 4x,
  120 fps cap, SC02's scripted walk): about 6800-7800 GL calls a frame went to 1300-1900 for the
  same 450 draws (TR01 1375 to 617, DE01 1330 to 447), and the game thread's work from 0.55 to
  0.47-0.49 ms a frame; NVIDIA's threaded driver hides most of the per-call cost that a GLES
  driver pays on the game thread. The picture is unchanged: frame-exact captures (a fixed game
  clock) of SC02, TR01, PT03 with the glow and a screen warp, and DE01, old paths against new,
  differ by at most one level per pixel, as two runs of the old paths do (one of three DE01 runs
  of the new paths had a crate a few pixels away, a difference in the game's state that the other
  two did not repeat).
- `perf11_bench.patch`: the GL3 side of the frame metrics (`TSSM_PERF`, `src/SB/Core/pc/iPerf.h`):
  `gl3perf.inc` counts draws, passes, passes into the scene's buffer, blits, clears, uploads and
  program binds per frame and times the frame on the GPU with timer queries. It wraps glad's
  function pointers only once the metrics start, so a normal run pays nothing.
- `gc_native_geometry.patch`: GameCube native geometry and skins (`src/gcngeo.cpp`, after the
  reader in the BfBB PC port's librw fork, MIT like librw). A GameCube model or world clump keeps
  its vertices as GX display lists and vertex arrays (platform 6). Once a geometry's extensions are read, they become ordinary geometry
  that every backend draws the way it draws the Xbox files' geometry. On the GameCube disc tested (USA)
  every triangle comes out as on the Xbox disc, with the same winding and the same index count per
  mesh (`tssm_gcrwtest`, below). Every count, offset and index is checked against its chunk. A
  geometry that cannot be read becomes one vertex and triangles with no area, with an error line,
  and so does a native geometry that has no native data, one whose display lists name more than a
  million vertices (retail's name well under that), and one whose skin comes before its native data
  (a generic skin, sized for the welded vertices). The patch also keeps an atomic off the skin
  pipeline when its geometry has no skin.
- `gc_texture_levels.patch`: GameCube native textures (`src/gcntex.cpp`). The palette is read at
  0x68 (it was read 4 bytes late, shifting every C8 texture's colours), every stored level is
  decoded, and the size, level count and level bytes are checked first. CMPR is decoded the way the
  GameCube's texture unit decodes it: the two middle colours are 5/8 and 3/8 blends, and a
  transparent texel keeps the middle colour instead of black, so filtering draws no dark fringe on
  cut-out edges. A texture stored with one level keeps one level, as on the console.

`tssm_gcrwtest <GameCube files> [<Xbox files>] [--fuzz N]` reads every GameCube texture and clump
through these patches, without a device, and compares each with its Xbox twin. On the USA disc:
- all 3,007 textures decode, and each one's levels fill exactly the size its struct gives;
- 2,978 restored names equal the Xbox names (the other 12 Xbox names are empty);
- level 0 has a mean PSNR of 42.3 dB against the Xbox art. The 29 under 20 dB are different
  art, such as `bench`, `fx_spray` and the button pictures;
- 9,743 geometries come out as ordinary geometry, none as a stub;
- 1,276,476 of 1,276,476 triangles match the Xbox twin with the same winding, and 10,016 of
  10,016 mesh index counts are equal;
- skin weights are equal or within 4/128;
- the 71 normals that differ (5 models) are NaN in the GameCube data, and in most of their Xbox
  twins too.

## The GameCube release's files: glow and water

The port reads the GameCube release's files as well as the Xbox one's (`src/SB/Core/pc/README.md`,
"The game's files"). Two things about how they look, both raised on the project's Discord:

- **Glow.** It was said that on the Xbox the glow textures are see-through and drive the Xbox
  release's bloom, and that on the GameCube they are solid, because the GameCube release has no
  such bloom (Industrial Park draws them differently for each release). In the port both glows are
  code over the same mask: the GameCube's own pass, `xFXHighDynamicRange` (above), whose mask is
  written only by the draws the game marks as brightening, and with `[extras] glow = xbox` the
  Xbox release's chain over that mask (`iXboxGlow.h`, `docs/EXTRAS.md`). The BfBB Xbox pass
  (`iGlow.h`) stays off (`iSystem.cpp`). GameCube files therefore get either glow from the same
  code; nothing is converted. Measured on the two USA discs:
  - texture alpha agrees except `bench` in BB01 (solid on the GameCube) and 78 textures whose art
    is cut out on one disc and blended on the other. The effect and glow textures (`sun_glow_om`,
    `fx_*`, `lightning`, `Particle_cloud`) have the same alpha and the same colour times alpha on
    both discs, within a few levels;
  - prelight alpha agrees geometry by geometry;
  - the glow's mask (`TSSM_HDR_VIEW=mask`) of eight levels (PT01, BB01, B101, SC02, JK01, DE01,
    TT01, GG02) loaded from the GameCube files matches the same levels from the Xbox files: mean
    block difference 0.00-0.26, and TT01's spotlight cones up to 7, a little brighter on the
    GameCube. The glow alone (`TSSM_HDR_VIEW=glow`) of PT01, B101 and TT01: 0.00-0.93.
  - one place differs: with every `[extras]` key `xbox`, Mindy's talk window in TT01 glows only
    from the Xbox files, whose draw of it writes the glow's mask (mean difference 23.8, 42% of
    blocks, against 3.0 or less with the GameCube glow). The two discs store her talk texture in
    different formats; which byte of it decides is not pinned down yet.
- **Water.** The GameCube files always show the GameCube water: they have no water body and no
  bump frames ("Water", above).
- **BB02's ground.** The sand in BB02's gameplay looked orange in some frames and paler in others,
  from either disc. It is the police sirens' flashing light at a different point of its cycle:
  over 51 frames (1700 to 1800, the fixed-step test copy) the sand's average colour is the same
  from both discs (red 217.8 and 220.0, green 151.8 and 152.5, blue 101.1 and 101.1), with the same
  5 white glow frames. Gameplay starts at a different point of the cycle because the discs' opening
  cutscene tracks differ in length (40.87 s on the GameCube, 38.89 s on the Xbox). The "lighter
  patch" in front of SpongeBob that `tools/regress.py` allows a variant for is very likely the same
  light.

## What is still missing

- **The treedome passes** in `rw/` (the Xbox BfBB glow, the cruise bubble distortion, the
  loading-screen snapshot) are imported and switched off; they draw BfBB's effects, not TSSM's.
