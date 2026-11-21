#ifndef ISCREEN_H
#define ISCREEN_H

#include <types.h>

// PC-only: the size the game renders at. There is no GameCube counterpart --
// a console's framebuffer is 640x480 and nothing chooses it -- so shared code
// includes this only under PLATFORM_PC. Most of it never needs to: FB_XRES and
// FB_YRES (zMain.cpp) already hold the render size.
//
// This is the render size, not the window size. The port draws into a virtual
// screen that each backend's blitVirtualScreen
// stretches into the back buffer at present time, keeping its aspect. The two
// are independent: a render size above the window supersamples, below it scales
// up. iSystem opens the window at the render size because that is the least
// surprising thing to do, not because anything requires it.
//
// One number serves the whole game because a Raster::CAMERA has no surface of
// its own. setRenderSurfaces binds the default render target for it, and
// rasterCreateZbuffer shares the engine's depth surface only when the Z
// raster's size equals the screen extent, allocating a private one otherwise. A
// depth surface smaller than the render target is invalid in D3D9, so a camera
// raster that does not match the virtual screen does not draw small -- it fails
// to bind depth and draws nothing. Every full-screen camera has to be built at
// this size, including the two instancing cameras in iEnv.cpp and iModel.cpp
// that never draw a pixel but still call RwCameraBeginUpdate.
//
// It is fixed at boot. The virtual screen is set once, inside RwEngineOpen, and
// changing it while the game is live would mean recreating every camera raster
// at once, so nothing calls the setter after startup.
//
// Widescreen follows from the size. A render size whose shape is not 4:3 shows
// more of the world, and nothing is stretched:
//
//   - 3D: the console's 4:3 frustum becomes the middle of a wider one. The
//     vertical field of view stays and the horizontal one widens (Hor+); on a
//     screen taller than 4:3 it is the other way round (Vert+). iCameraSetFOV
//     and iCameraCreate divide the view window by the UI box's share of the
//     screen, per axis. librw builds the clip planes from the same view window,
//     so culling and clipping agree with the picture.
//   - 2D: the game's 640x480 layouts keep their shape in the UI box below, a
//     centred 4:3 rectangle. rw/im.cpp maps every RwIm2D draw into the screen
//     there, so the HUD, the menus and the text need no change of their own.
//     Because the 4:3 frustum IS the box, a model placed in the HUD
//     (xModelRender2D) lands on the 2D drawn beside it.
//   - Full-screen layers -- fades, flashes, the letterbox bars, the glow and
//     warp passes, movies -- say so with the screen-space scope below and
//     cover the whole screen.
//
// The shape is chosen at boot from config.ini's video.aspect
// (iScreenSizeForAspect). On a 4:3 render size the box is the screen and all of
// the above is the identity. docs/WIDESCREEN.md has the whole of it.

// The size the game renders at. 640x480 until something says otherwise, so a
// target that never calls the setter -- rw_selftest does not -- behaves exactly
// as the port did before this existed.
S32 iScreenWidth();
S32 iScreenHeight();

// The same pair as floats, for the 2D layers that measure in pixels. Separate
// functions rather than a cast at ~20 call sites, and because the shared header
// has to be able to hand the console a float literal.
F32 iScreenWidthF();
F32 iScreenHeightF();

// The frustum's half-height over its half-width. 0.75 on a 4:3 screen, which is
// the constant retail's iCameraSetFOV has written into it.
F32 iScreenAspectF();

// Degrees to add to every field of view the game asks for, config.ini's
// video.fov less the 75 the game is built around. Zero unless something says
// otherwise.
//
// An offset rather than a value because the game varies the FOV constantly --
// cutscene cameras carry their own, the Cruise Bubble zooms, zCamera lerps
// between two -- and replacing the number would flatten all of that into one
// angle. It is applied at iCameraSetFOV, the one place a frustum is built, so
// xCameraGetFOV still reads back what the game asked for and the camera logic
// that measures against it is unchanged.
F32 iScreenFOVOffset();
void iScreenSetFOV(F32 degrees);

// The UI box: the largest 4:3 rectangle that fits in the render size, centred.
// On a 4:3 screen it IS the screen -- width and height are the render size and
// both origins are zero -- so nothing moves at the default.
F32 iScreenUIWidthF();
F32 iScreenUIHeightF();
F32 iScreenUIOriginXF();
F32 iScreenUIOriginYF();

// The same box as a fraction of the screen, per axis. Exactly one of the two is
// 1.0, and both are on a 4:3 screen.
//
// This is the form the view windows need: the 4:3 frustum is this share of the
// real one. xModelRender2D also uses it, to place a model's 0..1 rect in the box
// rather than across the whole frustum.
F32 iScreenUIFracXF();
F32 iScreenUIFracYF();

// How the HUD is placed on a screen that is not 4:3: config.ini's video.ui.
//
//   PILLARBOX  everything in the centred 4:3 box. Nothing moves relative to
//              anything else; the interface simply sits in the middle with
//              the wider picture either side of it. This is the default, and
//              it is exactly what the console drew.
//   NATIVE     each group of HUD widgets is anchored: a group authored at the
//              left of the 4:3 screen is carried out to the real left edge, one
//              at the right to the right edge, one in the middle stays (the
//              same top and bottom on a screen taller than 4:3). Menus, talk
//              boxes and full-screen art stay in the 4:3 box.
//
// The two are IDENTICAL on a 4:3 render size, where the box is the screen and
// the anchor is the identity, so this only ever means anything off 4:3.
//
// On a screen TALLER than 4:3 (a phone held upright, a tall window) the HUD is
// NATIVE whatever the setting says: a 4:3 band across the middle of a tall
// screen is not where a player looks for it. The safe margin still applies.
// The setting chooses only on a wide screen.
//
// Which widgets are HUD, and which belong together, is iHudAnchor.h's to say;
// here is only the arithmetic. It can change at any time: nothing is built
// from it, each frame's draws read it.
enum iScreenUIMode
{
    iSCREENUI_PILLARBOX,
    iSCREENUI_NATIVE
};

// The setting (video.ui).
void iScreenSetUIMode(iScreenUIMode mode);
iScreenUIMode iScreenGetUIMode();

// TRUE when the render size is taller than 4:3.
S32 iScreenUITall();

// The mode the HUD is drawn in: the setting, or NATIVE on a tall screen.
iScreenUIMode iScreenUIModeInForce();

// "pillarbox" or "native", any case; anything else is PILLARBOX.
iScreenUIMode iScreenUIModeFromName(const char* name);

// A safe area for a television that cuts the picture's edges off: how far short
// of the real edges the anchored HUD stops, as a share of the screen's width
// (of its height, on a tall screen) at each side, 0 to 0.25. config.ini's
// video.ui_margin, in percent. It never moves anything inwards of where the
// 4:3 box puts it, so it means nothing at 4:3, or in PILLARBOX on a wide
// screen.
void iScreenSetUISafeMargin(F32 share);
F32 iScreenGetUISafeMargin();

// The anchor is a TRANSLATION of everything drawn while it is set: -1 carries
// it to the left (top) edge, +1 to the right (bottom) edge, 0 leaves that axis
// alone, and a value between goes part of the way. A whole group gets one
// anchor, so an icon and the number beside it stay the distance apart they
// were drawn, and a widget sliding in or out keeps its motion.
//
// (Reading a position as a fraction of the real screen, the obvious mapping,
// would move a group's centre correctly and pull the group apart: the gap
// between two widgets is a distance and would be scaled with everything else.)
//
// Set around one widget's draw and cleared after it. rw/im.cpp adds it to
// every RwIm2D draw it puts in the box, and xModelRender2D to its rect. No
// effect when the mode in force is PILLARBOX, or at 4:3, where the reach is
// zero.
void iScreenSetAnchor(F32 sideX, F32 sideY);
void iScreenClearAnchor();

// A normalized UI-space coordinate, translated by the anchor in force -- still
// in UI space, so the box mapping that follows lands it on the screen.
F32 iScreenAnchorX(F32 x);
F32 iScreenAnchorY(F32 y);

// How far outside the 0..1 box the screen reaches, in UI units. Zero when the
// box is the screen.
F32 iScreenUIMarginXF();
F32 iScreenUIMarginYF();

// The same distance, but only as far as the anchor will actually reach: zero
// when the mode in force is PILLARBOX, and less the safe margin in NATIVE.
F32 iScreenAnchorMarginXF();
F32 iScreenAnchorMarginYF();

// What a 4:3 television does not show stays unseen on a tall screen.
//
// The game slides menu panels and HUD pieces in from, and out to, beyond the
// top and bottom of its 4:3 screen, and parks some there (the pause screen's
// sub-menu stays above it after the game resumes). A 4:3 television never
// shows that ground; a tall screen has room for it. So on a screen taller than
// 4:3, rw/im.cpp cuts a 2D draw it puts in the box at the box's top and
// bottom, for two kinds of draw:
//
//   - the menus (zUI): zUIMgrRender opens the scope below around them, and a
//     panel sliding in comes out of the box's edge, as from a 4:3 screen's
//     (one quad as wide as the screen is a backdrop layer, such as the title's
//     surf, and is not cut: rw/im.cpp);
//   - each HUD group while its anchor is set: at the top and bottom of the box
//     MOVED BY THE ANCHOR, the 4:3 screen that group is drawn for. At the real
//     edge with no margin, so a piece hides past the real edge as on a 4:3
//     screen; with a margin, at the margin's line, where the group's models
//     already stop (xModelRender2D drops a rect outside its 0..1).
//
// Nothing else is cut: talk boxes, prompts and the guitar's sights are where
// the game puts them (the sights sit on what they aim at, which a tall screen
// shows). Full-screen layers and full-bleed art are not drawn in the box. A
// wide screen is never cut. The menus' mouse (iMenuMouse.cpp) points at
// nothing outside the box on a tall screen, so it hits only what is shown.
//
// The scope nests.
void iScreen2DBoxClipBegin();
void iScreen2DBoxClipEnd();

#ifdef __cplusplus
struct iScreenBoxClip
{
    iScreenBoxClip()
    {
        iScreen2DBoxClipBegin();
    }
    ~iScreenBoxClip()
    {
        iScreen2DBoxClipEnd();
    }
};
#endif

// Where a 2D draw mapped into the box is cut now, in pixels of the render size
// from its top: the band from *top to *bottom is kept. FALSE when nothing is
// cut: the screen is not taller than 4:3, or neither the menus' scope nor a HUD
// anchor is in force.
S32 iScreen2DClipY(F32* top, F32* bottom);

// The 2D layer's coordinate space.
//
// rw/im.cpp maps every RwIm2D draw into the UI box when the current camera
// draws to the screen (a CAMERA raster of the render size). A layer that must
// cover the whole screen instead opens this scope around its draw. Scopes nest.
// Render-to-texture cameras are never mapped, so a pass that draws into its own
// target needs no scope; one that draws to the screen does.
void iScreen2DScreenSpaceBegin();
void iScreen2DScreenSpaceEnd();
S32 iScreen2DInScreenSpace();

#ifdef __cplusplus
// The scope above for one C++ block.
struct iScreenScreenSpace
{
    iScreenScreenSpace()
    {
        iScreen2DScreenSpaceBegin();
    }
    ~iScreenScreenSpace()
    {
        iScreen2DScreenSpaceEnd();
    }
};
#endif

// TRUE when the UI box is smaller than the screen: the render size is not 4:3.
S32 iScreenUIBoxed();

// The picture's shape, config.ini's video.aspect.
//
//   WIDE    the display's shape (and so, in a window, a window of that shape).
//           The default.
//   4:3     the console's shape, whatever the display: the 3D view and the UI
//           are exactly what the console drew.
//   CUSTOM  video.width and video.height exactly as written.
enum iScreenAspect
{
    iSCREENASPECT_WIDE,
    iSCREENASPECT_4_3,
    iSCREENASPECT_CUSTOM
};

// "wide", "4:3" or "custom", any case; anything else is WIDE.
iScreenAspect iScreenAspectFromName(const char* name);

// The render size for an aspect: the height as asked, and a width of the
// shape's aspect at that height (even, so the UI box's origin is a whole
// pixel). CUSTOM keeps the width as asked. A shape that is not known (zero) is
// taken as 4:3. The result is clamped to what iScreenSetSize accepts.
void iScreenSizeForAspect(iScreenAspect aspect, S32 width, S32 height, S32 shapeW, S32 shapeH,
                          S32* outWidth, S32* outHeight);

// Set by iSystem, from config.ini, before the window is opened -- and again
// with what the window actually gave, because engine_start takes the virtual
// screen from the window and the two must not disagree.
//
// A width or height that is not positive, or beyond what D3D9 will make a
// surface of, is reported and refused; the size already in force stands.
void iScreenSetSize(S32 width, S32 height);

// Samples per pixel the frame is rendered with. 1 is off.
S32 iScreenMultiSample();
void iScreenSetMultiSample(S32 samples);

// The samples the device actually granted (fewer when the card refuses the
// count), set by RwEngineStart once the surfaces are made. 0 until then.
S32 iScreenMultiSampleGranted();
void iScreenSetMultiSampleGranted(S32 samples);

S32 iScreenPerPixelLighting();
void iScreenSetPerPixelLighting(S32 on);

// Which of the two D3D9 paths draws.
//
//   SHADER  vertex and pixel shaders, Shader Model 2.0. Everything the port
//           adds -- the glow, the distortion, per-pixel lighting, the shadow
//           map -- is a shader and exists only here.
//   FIXED   D3D9's own transform, lighting, texture stages and fog. The bar
//           this lowers the port to is DX7-class hardware T&L, which is the
//           generation the game shipped on. It is a look-alike, not a match;
//           librw's d3d9ff.cpp lists where the two disagree.
//   AUTO    fixed only where the adapter cannot run ps_2_0. Resolved to one of
//           the two above before the device is made, so nothing downstream
//           ever sees AUTO.
//
// D3D9 only. The other backends have no fixed function to fall back to.
enum iScreenPipeline
{
    iSCREENPIPE_AUTO,
    iSCREENPIPE_SHADER,
    iSCREENPIPE_FIXED
};

iScreenPipeline iScreenGetPipeline();
void iScreenSetPipeline(iScreenPipeline pipeline);

// Which render backend draws.
//
// The executable carries every backend TSSM_RENDER_BACKENDS asked for, and one
// of them opens the device. RW_D3D9, RW_D3D11 and RW_GL3 say which are LINKED
// and are still what guards each backend's own code; this says which is
// RUNNING, and is what the parts written against more than one read.
//
//   D3D9   Direct3D 9. Windows only, and the only backend with the
//          fixed-function path above.
//   D3D11  Direct3D 11. Windows only. A second rw::d3d device beside D3D9;
//          the default Windows build carries both.
//   GL3    OpenGL 3.3. Runs off Windows.
//   VULKAN Vulkan 1.3. Runs off Windows too. librw's third rw::d3d device, so
//          it draws with the D3D pipelines and shaders.
//   NULL   No device at all, which is what a build with no render backend
//          resolves to. Headless, for compiling and for the self-tests. On
//          the 3DS video.backend offers it (null), for timing the game
//          without drawing; elsewhere it does not, because librw's null
//          driver asserts the first time anything asks it for a raster.
//   C3D    The Nintendo 3DS's PICA200, through citro3d (rw::c3d,
//          3ds/c3d/rwc3d.h). The 3DS only. After NULL so that no other
//          value moves.
//
// AUTO takes the first one the build has, in the order listed here. It is
// resolved in RenderWareInit -- which is the only place that knows what was
// linked -- so nothing downstream ever sees AUTO, exactly as with the pipeline
// above.
enum iScreenBackend
{
    iSCREENBACKEND_AUTO,
    iSCREENBACKEND_D3D9,
    iSCREENBACKEND_D3D11,
    iSCREENBACKEND_GL3,
    iSCREENBACKEND_VULKAN,
    iSCREENBACKEND_NULL,
    iSCREENBACKEND_C3D
};

iScreenBackend iScreenGetBackend();
void iScreenSetBackend(iScreenBackend backend);

// For the messages. "auto" before RenderWareInit has resolved it.
const char* iScreenBackendName(iScreenBackend backend);

// Light the world geometry at run time instead of reading the colour baked into
// its vertices.
//
// **Off is still the better setting on the shipped levels.** The artists painted
// their lighting vertex by vertex. A rig fitted to the level average cannot
// reproduce anything local: occlusion, bounce, or shadow put in by hand. Most of
// a bake is exactly that. The share no normal-based rig can reach is 54% of the
// colour variation on bb01 and 80% on hb01.
//
// iEnvDropPrelight keeps the paint that does a job rather than records light,
// per geometry. That covers the faked shadows, the blended ground decals and the
// invisible collision walls. PrelightIsArtwork in iEnvNormals.cpp tells those
// apart by the shape of the prelight, never by a name.
//
// Two things want this. One is a level authored to be lit. The other is any
// effect that needs the world's light to move: a shadow the world casts on
// itself, a sun that travels.
//
// OFF is what the consoles drew. The other two modes make iEnvNormals generate
// the normals the level never stored, and zScene enable a rig over the world.
// The world's lighting is then computed rather than looked up, so it can move
// and be occluded. 34 of the 55 levels ship no world normals.
//
// AUTO uses the level's own bspLightKit where there is one. The artists authored
// that kit FOR THE WORLD. Nothing has ever rendered it: zScene loads it into
// xEnv::lightKit and no code path enables it. 17 of the 55 levels carry one.
//
// BAKE always uses the reconstruction instead: an ambient and four directionals
// fitted to the baked vertex colour, per channel. It is the only option on the
// other 38 levels. On the 17 it is what the authored kit is worth comparing
// against.
//
// A level whose bake cannot be fit keeps its paint under every mode.
// iEnvDropPrelight drops the prelight only where there is something to replace
// it with. See iEnv::prelightDropped and iEnvNormals.h.
enum iWorldLightMode
{
    IWORLDLIGHT_OFF,
    IWORLDLIGHT_AUTO,
    IWORLDLIGHT_BAKE
};

S32 iScreenWorldLighting();
void iScreenSetWorldLighting(S32 mode);

// How far apart to pull the two ends of the fit, as a multiplier. BAKE only.
//
// Contrast does not touch the authored kit. Its lights are the artists' numbers,
// and there is nothing in them to scale against.
//
// 1.0 is the rig exactly as measured, and it holds the level's average
// brightness. bb01's bake averages 0.663 over the surfaces this lights, and the
// rig renders 0.663. Above 1.0 the setting scales the directionals and drops the
// ambient by what they gain on the average vertex, which holds the average until
// the ambient reaches zero.
//
// **iEnvRigAtContrast caps the setting there, per level and per channel.** bb01
// allows 1.61 and hb01 2.55. Past its cap a level keeps no ambient at all, so
// every face that none of the four lights reaches renders pure black, and the
// level also comes out under the brightness the compensation was holding.
//
// **1.0 by default, because the world and the objects share these lights.**
// zObjectLightKit puts characters and props on the same rig as the world, so
// whatever this does it does to both, and a character has no bake to be sharpened
// against. Above 1.0 the top end clips as well: saturated vertex colour is white,
// so the texture stops tinting the brightest ground and the Xbox glow carries it
// further. That is a look rather than a reconstruction.
F32 iScreenWorldLightContrast();
void iScreenSetWorldLightContrast(F32 contrast);

// Trace the level against itself at load and darken the vertices its own
// geometry hides from the light.
//
// **Off, because per-vertex shadows are too coarse to look like shadows.**
// Visibility is one bit per vertex and Gouraud interpolates between them, so a
// triangle with one corner in shadow becomes a gradient, and the edge can only
// land where a vertex happens to be -- bb01 has 39,647 for a whole town.
// Smoothing the mesh makes it worse rather than better, by giving the bit more
// places to flip. The artists faked bb01's building shadows with separate
// alpha-blended decals instead of darkening the world's vertices, which is what
// you do when this does not work.
//
// The rays themselves are sound and the machinery suits ambient occlusion, which
// is smooth enough to survive vertex interpolation. Real shadows want a shadow
// map.
//
// The rays go out through the collision tree the game already collides against,
// one per vertex per light, so this costs load time and nothing per frame.
//
// **It makes the world's lighting static, and it overrides AUTO.** The result
// has to live somewhere, and the only per-vertex channel the world has is the
// prelight. So iEnvBakeShadowedLight writes the finished colour there and clears
// rpGEOMETRYLIGHT, which means the sun cannot move afterwards and a level's own
// bspLightKit is not used even in AUTO. The rig is always the fit.
//
// Off keeps the run-time rig, which can move and be occluded by nothing.
S32 iScreenWorldLightShadows();
void iScreenSetWorldLightShadows(S32 on);

// Draw the game like the cartoon it came from: a character's light cut into
// steps, his colour pushed away from grey, and a black line drawn round him.
// iToon.h says how each part is made.
S32 iScreenToon();
F32 iScreenToonBands();
F32 iScreenToonSaturation();
F32 iScreenToonStrength();

// The hull's width in WORLD units, and its floor in pixels below. That way
// round because a line is a property of the thing it goes round: a character
// keeps the same weight of ink as he walks towards you, and the floor is only
// there to stop it disappearing in the distance.
F32 iScreenToonOutline();

// Where a character's shading is measured from.
//
//   0  the room's own brightest light, so he shades by where he is standing.
//   1  his own front, so his face is always lit and his sides always dark --
//      the show's convention, and true however he turns.
//   2  the camera, so whichever side of him you can see is the lit one and he
//      darkens as he walks away.
#define ITOON_LIGHT_SCENE 0
#define ITOON_LIGHT_FACE 1
#define ITOON_LIGHT_CAMERA 2

// How the rim light is put on. A rim is light and not paint, and the three
// answers are three statements about that.
//
// ROOM replaces: at full amount the band IS the colour of the room, so whatever
// the surface was doing stops at its edge. SCREEN keeps the surface, brightening
// by what is left of the range rather than by a fixed amount, so a texture still
// reads through the band; it cannot leave the range however bright either side
// already is. ADD is the brightest of the three and the only one that clips.
#define ITOON_RIM_ROOM 0
#define ITOON_RIM_SCREEN 1
#define ITOON_RIM_ADD 2

S32 iScreenToonFaceLight();
void iScreenSetToon(S32 on, F32 bands, F32 saturation, F32 outline, F32 strength, S32 faceLight);

// How much shade the level's placed models throw on the level, 0 for none.
//
// **A shadow the light rig cannot know about.** The world is lit from a
// direction and a colour, and a house standing in the way is neither. So the
// scene traces what its models block, over the arc the sun actually takes, and
// the cel shader scales its light term by what it finds.
//
// Needs the cartoon look on: the trace is delivered in the world's prelight, and
// the cel path is the only one that reads a prelight as a scale rather than
// adding it. Needs world_light_shadows off for the reason iDayNight.h gives.
F32 iScreenWorldModelShade();
void iScreenSetWorldModelShade(F32 amount);

// Give a prop modelled as a flat open sheet real thickness.
//
// **A sheet cannot be inked, whatever the ink is set to.** An inverted hull is
// the model inflated along its own normals with front faces culled, so what
// survives is the far side and the band of it that reaches past the silhouette.
// A shiny object is one open sheet of 45 triangles: every normal points out of
// the front, nothing lies behind to survive the cull, and nothing points
// sideways to widen the silhouette. On, such a mesh is rebuilt at first sight
// as a solid -- the sheet, a copy of it out the back, and a rim joining them --
// which is the shape the effect was designed for.
//
// Separate from the cartoon look on purpose. It changes the geometry rather than
// the shading, it costs a few hundred triangles per model, and it is worth
// having or not having on its own terms.
S32 iScreenSolidFlatProps();
void iScreenSetSolidFlatProps(S32 on);

// Whether the level itself is inked, as against only what stands in it.
//
// The hull is geometry, so a level costs what a level costs: it is drawn once
// more, in full, before it is drawn. Off leaves the world shaded but bare, which
// is what a painted background usually is.
S32 iScreenWorldOutline();
void iScreenSetWorldOutline(S32 on);

// Ink every model the game draws, not just the characters.
//
// Off is the show: a crate, a platform and a spatula are things in the world
// rather than people in a cartoon, and a line round each of them reads as a
// diagram. On is the whole scene drawn, which is a different look and a
// coherent one.
//
// The level itself is out of reach either way. It is drawn as world geometry
// rather than as models, and nothing in that path asks about an outline.
S32 iScreenToonAll();
void iScreenSetToonAll(S32 on);

// How many shades a character's own colours are rounded to. 0 leaves them.
F32 iScreenToonColors();
void iScreenSetToonFlatten(F32 colors);

// How strongly a level's own brightness shows in its shading.
//
// **A room indoors was reading as open sunlight.** The cel path asks the lights
// how bright a room is and they cannot say, so the level's paint is measured
// instead: hb01 is painted at 0.578 and the inside of SpongeBob's house at 0.487.
// See iEnvPaintLevel.
//
// This is the exponent on that ratio against a lit exterior. 1 shades a level by
// exactly the amount the artists painted it down, and above 1 exaggerates it;
// either way a level painted as brightly as an exterior is untouched, because one
// to any power is one. Applied to the level's own draw and not to what stands in
// it, because a character has his own kit.
F32 iScreenToonRoomLevel();
void iScreenSetToonRoomLevel(F32 power);

// The ink a character's line is drawn in.
//
// **A line is the surface darkened, which gets the hue right and the colour
// wrong.** Multiplying a surface down holds its saturation where it was and
// takes brightness off everything, so SpongeBob's olive line lands halfway to
// grey. Scale sets how dark, saturation puts the colour back, and gamma lifts
// the middle. All three leave a genuine black alone: there is nothing to push
// and no room to lift.
F32 iScreenToonInk();
F32 iScreenToonInkSaturation();
F32 iScreenToonInkGamma();
void iScreenSetToonInk(F32 scale, F32 saturation, F32 gamma);

// The rest of the cel, and the two bounds on the hull's width in pixels of the
// picture. iToon.h and librw's rwgl3.h say what each one does.
//
// The bounds are in pixels while the width itself is in world units. That is
// what keeps a drawn line reading as one: the floor stops it vanishing down the
// level, and the ceiling stops it swelling into a marker stroke when the camera
// closes on a character. 0 on either is no bound.
F32 iScreenToonWrap();
F32 iScreenToonRim();
F32 iScreenToonOcclusion();
F32 iScreenToonHardness();
F32 iScreenToonOutlineMin();
// In pixels of a 1440-line picture, so the line holds its weight at any
// resolution. See kInkRefHeight in iToon.cpp, and iScreenToonOutlineMin, which
// is in real pixels and says why the two differ.
F32 iScreenToonOutlineMax();
void iScreenSetToonLook(F32 wrap, F32 rim, F32 occlusion, F32 hardness, F32 outlineMin,
                        F32 outlineMax);

// Whether a prop built of flat panels catches the rim light the characters do.
//
// A rim is a band along a surface turning away from the eye, and a panel does
// not turn: it holds one facing and then breaks, so the band lands square across
// a whole face and the face reads as wet. That is what off is for. With the
// corners averaged the face does turn, and the rim traces the model the way it
// traces a character, which is why this is on.
// Whether a prop built of flat panels has its normals averaged at the corners.
//
// A panel has no shading in it: one normal across a whole face puts the whole
// face in a single band. Averaging gives the face somewhere to go, so the tone
// and the rim travel round the model instead of stepping from panel to panel.
// The line round it is unaffected, which reads its own normals. Off is the model
// as the artists built it.
S32 iScreenToonFlatSmooth();
void iScreenSetToonFlatSmooth(S32 on);

// Which of ITOON_RIM_ROOM, SCREEN or ADD the silhouette light uses.
S32 iScreenToonRimBlend();
void iScreenSetToonRimBlend(S32 mode);

S32 iScreenToonFlatRim();
void iScreenSetToonFlatRim(S32 on);

// Whether a tiki takes the rim light. Off, because a tiki is a stack of flat
// panels welded at the corners: the facing turns across a whole face rather than
// round an edge, so the light lands as a stripe on the front of it. The rest of
// the panelled family reads correctly and keeps it.
// Whether a skydome is drawn at full strength under the cel look.
//
// **A dome carries a vertex colour per corner**, which is a gradient across the
// largest surface in the shot, and a gradient is the thing the bands exist to
// remove. On paints those colours white, so what is left is the texture as
// painted. Off is the dome the console drew, which is what a level wants if its
// sky lives in those colours rather than in the texture.
//
// **Painted white and not switched off, which is not the same thing.** Clearing
// the prelit flag makes the renderer take its constant vertex colour instead,
// and that colour is black, so the dome goes out. Clearing the lit flag with it
// changes nothing either way: zScene.cpp enables NO light kit before the sky
// draws, on purpose, so the dome's own colours are the whole of its lighting and
// removing them can only leave nothing.
S32 iScreenToonSkyBright();
void iScreenSetToonSkyBright(S32 on);

S32 iScreenToonTikiRim();
void iScreenSetToonTikiRim(S32 on);

// How far a prop built of flat panels is carried from flat towards the ramp.
//
// Its own, because what the strength buys depends on how much of the ramp a
// model crosses, and a panel holds one value across its whole face. See
// iToonSetRampRow.
F32 iScreenToonFlatStrength();
void iScreenSetToonFlatStrength(F32 strength);

// How steep the goo's ripple is made to look, as a slope: 0.35 is a lean of 19
// degrees at the steepest part of the wave.
//
// **The real wave is far too small to shade by.** The levels author an amplitude
// of 0.01 units against a frequency of 1, which is a slope of 0.01. Nothing that
// reads a normal can show that. The wavelength and the phase stay the surface's
// own, so the shading wave is the one the goo is really making, and only how far
// it leans is a setting. 0 shades it by the true slope, which is flat.
F32 iScreenToonGooWave();
void iScreenSetToonGooWave(F32 slope);

// How brightly the goo catches a highlight, and how far round its surface that
// highlight starts.
//
// **A band and not a lobe.** A specular falloff is a gradient, and a gradient is
// what the cel look removes, so the highlight is thresholded the way the ramp
// thresholds the light term. What that draws is a glint with an edge on it, which
// is most of what says a surface is wet rather than painted. The goo's own
// warble supplies the moving normals it slides across.
F32 iScreenToonGooGloss();
void iScreenSetToonGooGloss(F32 amount);

F32 iScreenToonGooGlossEdge();
void iScreenSetToonGooGlossEdge(F32 edge);

// How far past the hull the ink's depth is read from, in widths of the line.
//
// The hull is an inflated copy of the model, so wherever the model comes within
// a line's width of something else the copy is inside it -- an arm's hull in the
// chest, a prop's in the floor -- and the ink wins the depth test against a
// surface standing in front of it. Reading the depth from further out again
// hands those pixels back. 0 is the ink where the geometry is.
//
// Below zero reads it from further in instead, and -1 lands on the vertex the
// hull was pushed from -- the model's own surface, with none of the copy's
// standing in space left in it.
F32 iScreenToonOutlineBias();
void iScreenSetToonOutlineBias(F32 widths);

// What the cel look does to the game's own text. iToonTextColor says why there
// is anything to do; 1 and 1 is the text exactly as it was painted.
F32 iScreenToonTextBrightness();
F32 iScreenToonTextSaturation();
void iScreenSetToonText(F32 brightness, F32 saturation);

#endif
