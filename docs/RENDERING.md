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
included.

| Effect | Setting | On the console | On PC |
|---|---|---|---|
| Shadows | none | `xShadow.cpp` | the shadow camera has its own target, drawn before the scene; the EFB corner save is skipped |
| Glow | `[video] hdr_glow` | `xFXHighDynamicRange.cpp`, GX | `iHDRGlow.h`, `rw/hdrglow.cpp` |
| Screen warp | `[video] screen_warp` | `xScreenWarp.cpp`, GX | `iScreenWarp.h`, `rw/screenwarp.cpp` |
| Lens flares | none | `iScrFX.cpp`, never drawn | not drawn (see below) |
| Movies on textures | none | `xFMV.cpp`, Bink; no retail use | not ported (see below) |

Both settings are on by default, which is the console's look. They can be changed in the overlay's
Settings tab (`docs/GUI.md`) while the game runs.

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

## Switches for testing

| Variable | What it does |
|---|---|
| `TSSM_HDR_VIEW=mask` | Replace the frame with its alpha channel in grey, as the glow pass sees it: the glow mask. |
| `TSSM_HDR_VIEW=glow` | Replace the frame with the glow alone, over black. |
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

## What is still missing

- **The treedome passes** in `rw/` (the Xbox BfBB glow, the cruise bubble distortion, the
  loading-screen snapshot) are imported and switched off; they draw BfBB's effects, not TSSM's.
- **The loading screen** is not drawn while a level loads (`iAsync` has no second thread).
