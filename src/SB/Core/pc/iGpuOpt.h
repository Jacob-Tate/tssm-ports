#ifndef IGPUOPT_H
#define IGPUOPT_H

#include <types.h>

// PC-only: what the port does to make a frame cheaper for the GPU, above all
// for a phone's GPU, which draws in tiles and pays for every time a render
// target is left and entered again (docs/RENDERING.md, "What a frame costs the
// GPU"). What is on by default does not change the picture, with one
// exception on GLES: "post" draws the HUD without anti-aliasing (below). The
// savings that are off by default do change it, which is why they are off.
//
// Each saving can be turned off for a comparison, with the environment (all
// are on but fixedsize, msrtt and msdiscard):
//
//   TSSM_GPU_OPT=<list>   comma-separated names of savings to turn OFF:
//     prepass     the shadows are drawn before the scene, not in the middle
//                 of it (xShadow.cpp, PLATFORM_PC)
//     msrtt       GLES: anti-aliasing through EXT_multisampled_render_to_texture.
//                 Off unless TSSM_GPU_MSRTT=1: on the phone's Adreno the
//                 multisampled depth is not kept across a pass break (a camera
//                 restart inside the scene), so walls are drawn over the
//                 characters in BB03; the explicit buffers are the default
//     post        GLES without render-to-texture anti-aliasing (every GLES
//                 by default): once the glow has read the frame, the rest of
//                 it is drawn single-sampled instead of in the multisampled
//                 buffer again. The glow's composite and the HUD are then not
//                 anti-aliased: where a letter's quad cuts through its art,
//                 the edge is hard (dialog text, up to about 110 of 255 at a
//                 few pixels a line); the rest of the HUD's edges are in its art
//     invalidate  depth and stencil are discarded where nothing reads them (GL3),
//                 targets drawn over whole are cleared first, not read back
//                 (the glow's), and a window without alpha gets no alpha clear
//     readview    GL3: the glow reads the frame itself, not a copy of it
//     fixedsize   Android: the window's surface is the render size, and the
//                 display scales it, not the GPU. Off unless
//                 TSSM_GPU_FIXEDSIZE=1: not yet measured or checked on a phone
//     msdiscard   GLES, with invalidate: the explicit multisampled buffer is
//                 discarded whole, colour too, once it has been folded, so a
//                 phone's GPU does not write every sample out (about 40 MB a
//                 frame at 2340x1080 with 4x). Off unless TSSM_GPU_MSDISCARD=1:
//                 a frame clears only depth, and the next frame reads last
//                 frame's colour where its scene leaves a sample undrawn or
//                 blends onto it (cracks in TR01, more in SC02), which after a
//                 discard is whatever the GPU left there. Desktop GL has no
//                 glInvalidateFramebuffer in librw, so it does nothing there
//     all         every one of them
//   TSSM_GPU_MSRTT=1      GLES: turn "msrtt" on (TSSM_GPU_OPT=msrtt still
//                         turns it off)
//   TSSM_GPU_MSDISCARD=1  GLES: turn "msdiscard" on (for measuring it)
//   TSSM_GPU_POST=always  GL3: "post" on every GL, desktop and render-to-texture
//                         included (it costs them the HUD's anti-aliasing and
//                         saves them nothing; for comparisons)
//   TSSM_GPU_LOG=1        say how the shadows were drawn, every 300 frames
//   TSSM_GPU_VERIFY=1     check the shadow pre-pass: every shadow taken from
//                         the pre-pass is drawn in the scene as well, as it
//                         always was, and the two rasters and the render
//                         states each leaves are compared; the scene goes on
//                         with what it drew itself (desktop only: it reads
//                         the rasters back)
enum
{
    IGPUOPT_PREPASS = 1,
    IGPUOPT_MSRTT = 2,
    IGPUOPT_POST = 4,
    IGPUOPT_INVALIDATE = 8,
    IGPUOPT_READVIEW = 16,
    IGPUOPT_FIXEDSIZE = 32,
    IGPUOPT_MSDISCARD = 64
};

// Whether that saving is on (all are but fixedsize, msrtt and msdiscard, unless
// TSSM_GPU_OPT turns them off).
S32 iGpuOptOn(U32 opt);

// The whole render state -- RenderWare's as the shim records it and librw's as
// the device has it -- saved and put back. For the shadow pre-pass, which draws
// the shadows early under the render state they would have had, and leaves
// the state as drawing them would have left it.
struct iRenderStateSnap
{
    U32 words[64];
};

void iRenderStateSave(iRenderStateSnap* out);
// keepTexture leaves the bound texture as it is: a snapshot from an earlier
// frame may name a texture that is gone.
void iRenderStateRestore(const iRenderStateSnap* in, S32 keepTexture);
// Whether two are the same, the bound texture left out of it when
// ignoreTexture is set.
S32 iRenderStateSame(const iRenderStateSnap* a, const iRenderStateSnap* b, S32 ignoreTexture);

// The shadow pre-pass's count for a frame: shadows drawn before the scene and
// used, and shadows that had to be drawn in the middle of it after all.
void iGpuOptShadowFrame(S32 early, S32 late);

// TSSM_GPU_VERIFY: whether it is on, whether two rasters hold the same pixels
// (read back), and the count of what it checked.
S32 iGpuOptVerify();
S32 iGpuOptSameRaster(struct RwRaster* a, struct RwRaster* b);
void iGpuOptVerifyResult(S32 sameState, S32 samePixels);

#endif
