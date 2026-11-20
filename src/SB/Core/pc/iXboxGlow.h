#ifndef IXBOXGLOW_H
#define IXBOXGLOW_H

#include <rwcore.h>

#include <types.h>

// PC-only: the Xbox release's glow, beside the GameCube release's (iHDRGlow.h).
// xFXHighDynamicRangeRender calls this in place of iHDRGlowRender when
// [extras] glow = xbox (iExtras.h, docs/EXTRAS.md). Both read the same mask:
// the frame's alpha channel, filled with the scene's background glow by
// xFXHighDynamicRangeStartRender and raised by the draws that "brighten".
//
// What the Xbox does (DEFAULT.XBE: xFXHighDynamicRangeRender 0x1348f0, the
// chain 0x1b0510, its targets 0x1b1080):
//
//   1. Two render targets of 243x182 (640 x 0.38 by 480 x 0.38) are cleared
//      to (0, 0, 0, glow + 1).
//   2. The frame is drawn into the first with one bilinear tap per pixel, an
//      alpha test of alpha > glow and ONE/ZERO: every pixel brighter in the
//      mask than the background is copied at full colour (the GameCube weighs
//      colour by alpha instead).
//   3. Seven blur passes ping-pong between the two. Pass i reads the last
//      pass's rectangle and draws into one 5% x (i + 1) smaller, each pixel
//      the average of four diagonal taps 0.2 x i texels away (64/255 each).
//      Passes after the "doubling" one use a shader that doubles the sum; the
//      title screen's glow leaves that at 5, so in play the seventh doubles.
//      Nothing is cleared between passes.
//   4. The last rectangle (about 20% of the target) is stretched over the
//      frame and added, ONE/ONE, times (s, s, s): the scene's strength from
//      the table at 0x1b0eb4 (iExtrasGlowStrength).
//
// The title and pause menus run the same chain on the menu's own mask
// (zUIMgr.cpp) with 8 passes, a 2% step and the eighth doubled.
//
// Here:
//
//   - The targets are 182 high and as wide as the frame's shape asks (243 at
//     4:3), so the glow keeps the Xbox's size on a wide screen too.
//   - A frame taller than 480 is first halved (box filter) until it is not,
//     so the single tap of step 2 reads about the pixels the Xbox's does and
//     small lights do not flicker at large render sizes.
//   - The four taps are one draw through glow_blur_PS (the BfBB glow's
//     four-tap shader) on the shader backends; on Direct3D 9's fixed path they
//     are four additive draws, which round each tap to 8 bits.
//   - The composite writes colour only (the Xbox also adds to alpha, which
//     nothing reads afterwards).
//   - A strength above 1 (the menu's) is drawn in steps of at most 1.

// One run of the chain.
struct iXboxGlowChain
{
    S32 passes;      // blur passes
    F32 shrink;      // pass i draws into (1 - shrink x (i + 1)) of the last one's rectangle
    S32 doubleAfter; // passes after this one double their sum
};

// The scene's (after the title screen has run once, as it always has in play)
// and the menus'.
extern const iXboxGlowChain kXboxGlowScene;
extern const iXboxGlowChain kXboxGlowMenu;

// The menus' strength. The Xbox's is 3.5 times a field of the menu's saved
// configuration that nothing writes (0x12f220 reads leftover stack memory), so
// the console's own value is unknown. It is 0 here: TCRF's capture of the
// Xbox's options screen shows no glow round the selected label, which every
// strength from 0.5 up would give (docs/EXTRAS.md). At 0 the menus' chain is
// not drawn. TSSM_TEST_MENU_GLOW=<number> overrides it, to see it.
F32 iXboxGlowMenuStrength();

// `cam` is the camera being rendered, in an update (ended and begun again
// inside, as iHDRGlowRender does). `glow` is the background glow, the alpha
// test's reference (at least 1). Obeys video.hdr_glow (iHDRGlowEnabled).
void iXboxGlowRender(RwCamera* cam, S32 glow, F32 strength, const iXboxGlowChain& chain);

// TSSM_HDR_VIEW also applies here: "mask" shows the mask in grey, "glow" the
// glow alone over black, and "base" leaves the frame without the glow (for
// comparing with a software model of the chain).

#endif
