#ifndef IHDRGLOW_H
#define IHDRGLOW_H

#include <rwcore.h>

#include <types.h>

// PC-only: TSSM's HDR glow, xFXHighDynamicRange.cpp's render pass, drawn with
// render targets instead of GX. On PC xFXHighDynamicRangeRender calls this in
// place of its GameCube body (PLATFORM_PC).
//
// What the console does, from that body:
//
//   0. During the frame the EFB's alpha channel is the glow mask.
//      xFXHighDynamicRangeStartRender fills it with the scene's background
//      glow (10 of 255 by default) once the opaque world is drawn, and from
//      then on only draws that "brighten" (xFXHighDynamicRangeBrighten:
//      glowing models, decals, particles) write alpha; iDrawSetFBMSK masks it
//      for everything else. That part is portable and runs as it is: the
//      port's iDrawSetFBMSK is librw's colour write mask.
//   1. GXCopyTex the whole 640x480 EFB at half size with the box filter:
//      320x240 RGBA8 (texData[0]).
//   2. Draw that, bilinear, into the EFB's top-left 128x128 through five TEV
//      stages that each add texel x C0, so the colour and the alpha both come
//      out as min(1, 5 x C0 x texel). C0 is (U8)(255 x (2.4 x overbrighten +
//      0.2)) in all four channels; at the default overbrighten of 3 that is
//      1887, which the byte store keeps as 95, so the scale is 5 x 95 / 255 =
//      1.86. The blend is hdr_down_src/hdr_down_dst, SRCALPHA/ZERO: colour
//      times alpha, and alpha squared. That is the bright pass -- the mask
//      decides what glows -- and the corner is copied out (texData[1]).
//   3. Two more passes halve the corner again into texData[2] and [3], but
//      the composite below reads texData[1], so on the screen they change
//      nothing. They are not drawn here.
//   4. Put the corner back, then draw texData[1] stretched over the whole
//      screen, bilinear, colour times 0x80 and alpha times 1, blended
//      hdr_src/hdr_dst (SRCALPHA/ONE), with alpha writes off: the screen
//      gets 0.5 x glow x glow alpha added to it.
//
// Here, with no corner to borrow:
//
//   1. librw copies the virtual screen into a texture, and it is halved
//      (librw's 2D pipeline, one bilinear tap on the corner of each 2x2) to
//      the console's half size. Above 960 pixels wide it is halved again
//      until it is at most 480 wide, so the 128x128 sample below reads about
//      as many pixels per texel as the console's does.
//   2. The bright pass is drawn into a 128x128 texture of its own in two
//      steps, because 2D quads multiply by a vertex colour of at most 1: the
//      half picture is drawn at min(1, scale) with ONE/ZERO and added again at
//      the remainder with ONE/ONE (the target clamps the sum at 1, as the TEV
//      clamps each stage), then that is drawn into a second 128x128 texture
//      with the console's down blend (SRCALPHA/ZERO).
//   4. The second texture is drawn over the frame as the console draws it.
//
// The differences that remain: the console's EFB holds 6 bits per channel
// (RGBA6_Z24) and this holds 8, and above 640x480 the first step averages more
// pixels than the console's 2x2 box.
//
// `overbrighten` and `downsamples` are xFXHighDynamicRange's tweak values (a
// scene's configuration can change them); with fewer than 2 downsamples the
// console never refreshes texData[1] and composites what it held before, and
// so does this. The four blend factors are the hdr_* globals, as GX factors
// (GXBlendFactor, 0 to 7).
//
// `cam` is the camera being rendered, in an update. Its update is ended and
// begun again inside, because the passes render into their own cameras.
void iHDRGlowRender(RwCamera* cam, F32 overbrighten, S32 downsamples, S32 downSrc, S32 downDst,
                    S32 src, S32 dst);

// Whether iHDRGlowRender does anything; config.ini's video.hdr_glow, pushed
// down from iSystem.cpp at startup (this file compiles into tssm_rw, which
// does not read the settings). On by default, as on the console. Off leaves
// the frame as it is drawn before the glow, the same as the port before this
// pass existed.
void iHDRGlowSetEnabled(S32 enabled);

// Whether it is on: video.hdr_glow is the switch for the Xbox glow
// (iXboxGlow.h) and the menus' glow as well.
S32 iHDRGlowEnabled();

// TSSM_HDR_VIEW, for looking at the pass: "mask" replaces the frame with its
// alpha channel (the glow mask) in grey, "glow" with the glow alone over
// black. Read once, at the first render.

#endif
