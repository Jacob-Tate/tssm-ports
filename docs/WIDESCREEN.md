# The PC port's widescreen

The game was drawn for a 4:3 television. On a screen of any other shape the port shows more of the
world and stretches nothing:

- **3D:** the console's 4:3 view is the middle of a wider one. The vertical field of view stays and
  the horizontal one widens (Hor+). On a screen taller than 4:3 it is the other way round (Vert+).
- **2D:** the menus, the HUD, text, icons and talk boxes keep their 4:3 shape and layout, in a 4:3
  area in the middle of the screen (the UI box).
- **Full-screen layers** cover the whole screen: fades and flashes, the cutscene letterbox bars,
  the glow, the screen warp, the glare. Full-screen menu art covers it too (see below).
- **Movies** keep their own shape, with black bars.

On a 4:3 render size every part of this is the identity: the picture is the one the port drew before
widescreen existed, and `tools/regress.py` checks that against its references. Its widescreen
pass (`--size 1280x720`, against references of its own) covers the run to gameplay with the pause
menu, the DE01 fly-through and the PT03 belt at 16:9.

## Settings

`config.ini`, `[video]`:

| Key | Values | Meaning |
|---|---|---|
| `aspect` | `wide` (default), `4:3`, `custom` | `wide`: the display's shape. `4:3`: the console's. `custom`: exactly `width` x `height`. |
| `height` | 240 to 8640 | Render height in lines. With `wide` and `4:3` the width follows from it (rounded to an even number). |
| `width` | 320 to 15360 | Used only with `custom`. |
| `fov` | 30 to 140 | The field of view at 4:3, in degrees (75 is the game's). An offset on every angle the game asks for, so zooms and cutscene cameras keep their differences. |

The overlay's Settings tab has the same choices: **Aspect ratio** (Widescreen (match window), 4:3
(original), Custom size) and **Render size** as a list of heights with the width each one gives.

The shape is taken once, at startup, from the primary display. A window opened at that render size
has that shape. A window resized afterwards, or Alt+Enter, keeps the picture's shape and shows bars
until the next start. Changing the shape live would mean rebuilding every screen camera's raster at
once (see `iScreen.h`). On Android the screen's long side is always the width.

## How it works

**The rule:** the console's 4:3 frustum IS the UI box. Everything follows from that.

- `iCameraSetFOV` (`src/SB/Core/pc/iCamera.cpp`) divides the view window by the UI box's share of the
  screen on each axis (`iScreenUIFracXF`, `iScreenUIFracYF`). Every gameplay, cutscene, fly-through,
  boss and menu camera sets its angle through it (`xCamScreen::set_fov`). librw builds the clip
  planes from the same view window, and the game's own culling reads those planes, so nothing pops in
  or out at the edges. `iCameraCreate` does the same to its default window, and so does the HUD
  camera (`zHud.cpp`).
- `rw/im.cpp` maps every `RwIm2D` draw into the box: `x' = originX + x * fracX`, the same for y.
  That is one place for every 2D emitter in the game. It applies only to a draw into the screen: a
  `CAMERA` raster of the render size. Render-to-texture targets (shadows, camera textures, the glow's
  and warp's own targets) are another type or another size and are left alone.
- `xModelRender2D` (`xModel.cpp`) places a model in a 0..1 rect by shearing against the camera's view
  window. It uses the box's share of that window, so a HUD or menu model lands on the 2D drawn beside
  it and keeps its shape.

**Full-screen layers** open a screen-space scope around their draw (`iScreen2DScreenSpaceBegin` and
`End`, or the `iScreenScreenSpace` guard): `iScrFxDrawBox` (fades, letterbox bars, the screen-size
rectangle), the motion-blur overlay, the movie (`iFMV.cpp`), the glow, distortion, warp and screen
passes (`rw/`), and in the game code, under `PLATFORM_PC`: the full-screen glare (`xScrFx.cpp`), the
glow's alpha quad (`xFXHighDynamicRange.cpp`) and the out-of-bounds fade (`zEntPlayerOOBState.cpp`).

**Full-bleed art.** `rw/im.cpp` treats one quad (at most 6 vertices) that spans the whole width (the
whole height, on a tall screen) as an exception:

- textured, it is full-screen art (the title's sand and surf, the menu and pause sunbursts, the screen
  transition), and it **covers** the screen: one uniform scale about the centre, so it fills the width
  and loses a strip top and bottom (12.5% each at 16:9) instead of being stretched or leaving bars;
- untextured, it is a fill or a dim, and it is drawn as given, across the whole screen.

Text and other batches are always boxed, however wide they run.

Three game-code edits keep the box's shape where the game measured the screen: the out-of-bounds
hand's drag (`zEntPlayerOOBState.cpp`), the menu sparkles (`zMenuSB.cpp`) and `xModelRender2D`. Every
edit outside `src/SB/Core/pc` is under `PLATFORM_PC`, and the GameCube build is unchanged.

## Not done

- **An edge-anchored HUD.** The HUD sits in the box. `iScreen.h` keeps an anchoring scheme for a HUD
  at the screen's edges, but nothing calls it, and its zones were measured on another game's HUD.
- **Changing the shape live** (see Settings).
- **Save thumbnails** are taken of the whole screen, not the box.
- **Lens flares** are not drawn on PC at all (`docs/RENDERING.md`).
