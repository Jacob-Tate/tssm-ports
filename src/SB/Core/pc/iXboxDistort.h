#ifndef IXBOXDISTORT_H
#define IXBOXDISTORT_H

#include <rwcore.h>

#include <types.h>

// PC-only: the Xbox release's screen distortion while the Sonic Wave Guitar's
// wave is steered ([extras] guitar_distortion, iExtras.h, docs/EXTRAS.md). The
// GameCube release draws none: cruise_bubble's distort_screen is an empty
// function there, with its five calls still in place.
//
// What the Xbox does (DEFAULT.XBE):
//
//   - distort_screen (0x1af2e0) keeps the amount: 0 to 1 on the camera's
//     S-curve while it flies to the wave, 1 while it rides the wave, 0
//     otherwise. cruise_bubble::deinit sets 0 too (0x91ac7).
//   - zSceneRenderPostFX (0x6a0f0) draws the pass (0x1afcb0) after the lens
//     flare and before the guitar's HUD, which the GameCube draws first. At
//     an amount of 0 it returns at once.
//   - The pass binds the frame and the swirl map BXCruiseBubbleDistort of
//     plat.HIP (512x512; red and green nearly equal, blue 255), draws one
//     quad into a 512x512 target through the pixel shader at 0x2cbcf0 (the
//     map's texel expanded to -1..1 and used in two dot products with the
//     quad's coordinates: a read of the frame at an offset), ONE/ZERO, linear,
//     clamped, and stretches that target back over the screen.
//   - The read, with (r, g) the expanded texel of the map's middle half, A =
//     15 x amount and t = KeTickCount x 0.002 (0x1afb60):
//         u = x + A (r sin t + g cos t)
//         v = y + A (r cos t - g sin t)        in 640x480 pixels
//     so about 21 pixels at most, turning once every 3.14 seconds of real
//     time, paused or not.
//
// Here:
//
//   - One pass at the picture's own size: the frame is copied
//     (iScreenPassCapture) and drawn back through rw/shaders/xboxdistort_PS.
//     The Xbox's trip through 512x512 softens its 640x480 picture a little;
//     at a larger render size it would blur it, so it is left out.
//   - The offsets are shares of the picture (A/640 across, A/480 down), so the
//     swirl is the same at any render size and covers a wide picture as the
//     Xbox's own arithmetic does on a 16:9 television.
//   - The same shader on Direct3D 9, Direct3D 11 and OpenGL 3 / GLES 3.
//     Direct3D 9's fixed path has no pixel shader, the Vulkan renderer has no
//     SPIR-V build of this one yet, and GameCube files have no swirl map: all
//     three get the GameCube look, and the overlay says why.

// --- the game's side (iXboxDistort.cpp) ----------------------------------------------

// cruise_bubble's distort_screen: the amount, 0 to 1.
void iXboxDistortSetAmount(F32 amount);
F32 iXboxDistortAmount();

// Whether the Xbox look is on: the setting in force is xbox, the swirl map is
// there and the pass has not failed. zSceneRenderPostFX then draws the pass
// where the Xbox does and the guitar's HUD after it; otherwise it keeps the
// GameCube's order.
bool iXboxDistortActive();

// The pass, for zSceneRenderPostFX. `cam` is the camera being rendered, in an
// update (ended and begun again inside). Nothing happens at an amount of 0.
void iXboxDistortRender(RwCamera* cam);

// Why the Xbox look is not drawn although the setting, the files' release and
// the renderer allow it (no swirl map in the loaded files; the shader or the
// frame's copy failed): a sentence for the overlay's row, else NULL.
const char* iXboxDistortStatus();

// TSSM_TEST_DISTORT=<amount>[,<milliseconds>]: the pass at that amount in every
// frame of a level, with the clock held at that time when one is given, for
// comparing a still frame with a model of the pass. TSSM_TEST_DISTORT=log: the
// game's own amount, one log line for every frame the pass is asked for.

// --- the renderer's side (rw/xboxdistort.cpp) ---------------------------------------

// One frame of the pass. `map` is the swirl map, `ms` the clock in
// milliseconds, or negative for the real one. FALSE when it was not drawn;
// after a failure it stays off for the run and the log says why.
bool iXboxDistortDraw(RwCamera* cam, RwTexture* map, F32 amount, F32 ms);

// Why this renderer does not draw the pass (Direct3D 9's fixed path, Vulkan,
// no device, a failure): a sentence for the overlay's row, else NULL.
const char* iXboxDistortRendererStatus();

// Names the shader's constant to librw's GL3 uniform registry, before any
// shader is made (RwEngineOpen), as iDistortRegisterShaderUniforms does.
void iXboxDistortRegisterShaderUniforms(void);

#endif
