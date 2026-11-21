# The PC port's widescreen

The game was drawn for a 4:3 television. On a screen of any other shape the port shows more of the
world and stretches nothing:

- **3D:** the console's 4:3 view is the middle of a wider one. The vertical field of view stays and
  the horizontal one widens (Hor+). On a screen taller than 4:3 it is the other way round (Vert+).
- **2D:** the menus, the HUD, text, icons and talk boxes keep their 4:3 shape and layout, in a 4:3
  area in the middle of the screen (the UI box). The HUD can be put at the real edges of the screen
  instead (`ui = native`, below), and on a screen taller than 4:3 it always is.
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
| `ui` | `pillarbox` (default), `native` | Where the HUD goes on a screen wider than 4:3. `pillarbox`: in the 4:3 area, where the original game draws it. `native`: at the screen's real edges (see "The HUD at the screen's edges"). On a screen taller than 4:3 the HUD is at the top and bottom edges with either value (see "A screen taller than 4:3"). |
| `ui_margin` | 0 to 25 | With `ui = native`, and on any screen taller than 4:3: how far short of the screen's edges the HUD stops, in percent of the screen's width (of its height, on a screen taller than 4:3). For a TV that cuts the edges of the picture off. |

The overlay's Settings tab has the same choices: **Aspect ratio** (Widescreen (match window), 4:3
(original), Custom size), **Render size** as a list of heights with the width each one gives,
**HUD position** (4:3 area (original), Screen edges) and, with the second or on a screen taller
than 4:3, **HUD edge margin**. The two HUD settings apply at once. On a screen taller than 4:3 the
tab says under **HUD position** that the HUD is at the top and bottom edges whichever is chosen.

- A `config.ini` from an old build may still say `ui = native` from when the key did nothing. It
  now puts the HUD at the screen's edges; **HUD position** sets it back.
- With a `ui_margin` above 0, a HUD model that slides out towards its own edge (chest, token,
  nitro, meter) disappears a margin's width before the real edge of the screen: `xModelRender2D`
  still drops a model once it has left the 4:3 box by the screen's full reach, and the margin moves
  the group by less than that. On a wide screen the text beside it slides on; on a screen taller
  than 4:3 the text is cut at the same line (below). Without a margin that point is the screen's
  edge.

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

## The HUD at the screen's edges

With `ui = native` each group of the HUD goes to its corner of the real screen on a wide shape
(16:9, 16:10, 21:9, 32:9), and on a shape taller than 4:3 it always does (next section):

| Group | Corner |
|---|---|
| The health (Krabby Patties) | top left |
| The manliness meter: bar, points, the upgrade icon and text, the combo counter | top right |
| The treasure chest and its count | bottom left |
| The Goofy Goober Token and its count; in the Patty Wagon, the nitro and its count | bottom right |
| The Sonic Wave Guitar's countdown, while the wave flies | bottom right |
| The bosses' health bar | bottom centre: it stays centred, so it only moves on a screen taller than 4:3, down to the bottom edge, level with the chest and the token as on a 4:3 screen |

- A group moves as one piece, by the distance the screen reaches past the 4:3 area (less
  `ui_margin`). So each one is as far from its corner of the screen as it is from that corner of a
  4:3 screen, its parts keep their places beside each other, and the slide in and out is the same.
  On a wide screen the groups move sideways; on a tall one, up and down.
- At 4:3 the two settings are the same picture. With `ui = pillarbox` on a wide screen nothing is
  different from a build without the setting.
- **What stays in the 4:3 area:** the menus and the pause screen, talk boxes, prompts and
  subtitles (all centred; on a tall screen they stay in the 4:3 area too), the guitar's sights
  (they sit on what they aim at), and the loading screen, title and movies. Nothing that lines up
  with the 3D world moves.
- **While the game is paused the HUD is in the 4:3 area too**: the pause screen is drawn around it
  (the chest and the token sit either side of the chest count's box). It glides between the two
  places in a fifth of a second. The mouse in the menus (`iMenuMouse.h`) points at the same places
  with either setting, because no menu widget moves.

## A screen taller than 4:3

A phone held upright, or a window taller than it is wide. The 4:3 area is a band across the middle,
with the 3D world above and below it (Vert+).

- **The HUD is at the real top and bottom edges whatever `ui` says**, as with `ui = native`: the
  health and the manliness meter at the top, the chest, the token, the nitro and the guitar's
  countdown at the bottom, the bosses' bar at the bottom edge. A band in the middle of a tall screen
  is not where a player looks for the HUD. `ui_margin` applies as with `native`.
- **What a 4:3 television would not show is not seen.** The game slides menu panels and HUD pieces
  in from beyond its 4:3 screen and out to it, and leaves some there: the pause screen's sub-menu
  (Save Game, Quit Game, Options, Extras) stays above the screen after the game resumes, and the
  manliness points wait above it once the HUD has hidden. A 4:3 TV never shows that ground; a tall
  screen has room for it. So on a tall screen `rw/im.cpp` cuts at a 4:3 screen's top and bottom:
  - **the menus** (everything `zUIMgrRender` draws, under `PLATFORM_PC` in `zUIMgr.cpp`) at the
    4:3 area's: a pause panel slides in from the band's edge. One quad as wide as the screen is a
    layer of a menu's backdrop (the title's surf over the sand) and is not cut, or the art under
    it would show a hard line there; what slides is narrower than the screen;
  - **each HUD group** at the top and bottom of the 4:3 screen it is moved with: with no margin
    that is the real edge, so a hidden piece is past the edge as on a 4:3 TV; with a margin it is
    the margin's line, where the group's models already stop.

  The cut is in `rw/im.cpp` (`DrawCut`): a 2D draw wholly inside goes through as it is; one that
  crosses the line is drawn as the triangles left (`iClip2D.h`), with the texture and colour that
  place had. The band is `iScreen2DClipY` (`iScreen.h`). Nothing else is cut: talk boxes and
  prompts (centred, in the band), the guitar's sights (they sit on what they aim at, which a tall
  screen shows), full-screen layers and full-bleed art. A 3D model in a menu (`zUIModel`) is not
  cut: the game already drops one that is wholly outside the 4:3 screen (`xModelRender2D`).
- **The mouse in the menus** points at nothing above or below the band (`iMenuMouse.cpp`), so it
  only reaches what is shown. A right click is still Back and the wheel still steps, anywhere.
- A wide screen is never cut, and keeps the setting as above.

How: the HUD is sixteen `xhud` widgets, all in `mnui.HIP`. `xhud::widget::render_all` (`xHud.cpp`,
under `PLATFORM_PC`) opens an `iHudAnchorScope` around each widget's draw. `iHudAnchor.cpp` looks
the widget up in a table of the fourteen names that belong to a corner and sets a translation
(`iScreenSetAnchor`); `rw/im.cpp` adds it to every 2D draw it puts in the box, and `xModelRender2D`
to a model's rect. The table is by name because the rectangles alone do not say which group a
widget is in (the upgrade icon is nearer the middle of the screen than the right). A widget the
table does not know (two unused ones in the files, or a mod's) goes by where its middle was drawn:
the left or right 40% of the box to that edge, the middle fifth stays. Two pieces are not widgets,
and get one `PLATFORM_PC` line each: the guitar's countdown (`zEntCruiseBubble.cpp`, `render_hud`)
is anchored by the point it is drawn at, and the bosses' meter (`zNMEHealthMeter.cpp`, `RwIm3D` in
the HUD camera) is given a world matrix that moves it down (`iHudAnchorBossMeter`, NULL as on the
console unless the screen is taller than 4:3). `TSSM_TEST_HUDLOG=<n>` logs every widget drawn, with
its corner and where it lands, and the countdown and the meter (`iTestEvents.h`). `tssm_hudtest`
checks the arithmetic on 4:3, 16:9, 16:10, 21:9, 32:9 and four tall shapes with both settings, the
band a tall screen's menus and HUD groups are cut to, and the cut triangles (`iClip2D.h`).

The setting is not game state: it changes where a widget is drawn and nothing the game reads. In a
co-op session each player keeps their own; the co-op builds must not take it from the host.

## Not done

- **Changing the shape live** (see Settings).
- **Save thumbnails** are taken of the whole screen, not the box.
- **Lens flares** are not drawn on PC at all (`docs/RENDERING.md`).
