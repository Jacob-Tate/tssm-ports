#ifndef TSSM_RW_SCREENPASS_H
#define TSSM_RW_SCREENPASS_H

// What TSSM's own screen passes share: the HDR glow (hdrglow.cpp, iHDRGlow.h)
// and the screen warp (screenwarp.cpp, iScreenWarp.h). TSSM-only; the treedome
// passes beside them (glow.cpp, distort.cpp, snapshot.cpp) keep their own
// copies of the same few steps so that pcimport.py --check can still compare
// them with treedome.
//
// On the GameCube both effects work in the EFB: GXCopyTex copies the picture
// into a texture, quads are drawn over it, and the glow does its blur in the
// EFB's top-left corner before putting the corner back. Here the picture is
// the virtual screen (a render target of the render size), the copy is
// librw's captureFrame or copyVirtualScreen, and every intermediate picture
// gets its own camera texture, so nothing has to be put back.
//
// Every pass here draws with librw's own 2D pipeline (texture times vertex
// colour) and the blend states, with no pixel shader of the port's own, so it
// runs on every backend, the Direct3D 9 fixed-function path included.

#include <rwcore.h>

#include <types.h>

// A camera that renders into a texture of its own. No Z raster: every pass
// draws 2D quads with the depth test off.
struct iScreenPassTarget
{
    RwCamera* camera;
    RwRaster* raster;
    RwInt32 width;
    RwInt32 height;
};

// The render states a pass changes, as they were before it, so it can put
// them back. The RenderWare ones go through RwRenderStateGet/Set, so the
// shim's own record of them stays true; the colour write mask and the alpha
// test are librw's (iDrawSetFBMSK and RwGameCubeSetAlphaCompare set them).
struct iScreenPassState
{
    void* raster;
    S32 filter;
    S32 addressU;
    S32 addressV;
    S32 ztest;
    S32 zwrite;
    S32 fog;
    S32 cull;
    S32 vertexAlpha;
    S32 srcBlend;
    S32 destBlend;
    U32 writeMask;
    U32 alphaFunc;
    U32 alphaRef;
};

// librw's colour write mask bits (rw::ColorWriteMask).
#define ISCREENPASS_WRITE_RGB 7u
#define ISCREENPASS_WRITE_ALL 15u

// Whether a device is open that can hand back the picture as a texture.
// False on the null backend and before RenderWare has started.
bool iScreenPassReady();

// The picture's size in pixels: the virtual screen, which every full-screen
// camera draws into.
void iScreenPassExtent(RwInt32* w, RwInt32* h);

// Whether the backend lets a pass run (the 3DS's measuring switches;
// always on elsewhere).
enum
{
    ISCREENPASS_GLOW,
    ISCREENPASS_WARP
};
bool iScreenPassOn(S32 pass);

// Whether a target drawn over whole is cleared first (iGpuOptOn's
// IGPUOPT_INVALIDATE: a tiled GPU then starts it from nothing instead of
// reading back what it held). The 3DS's backend says (its glowclear word).
bool iScreenPassInvalidates();

// Copy the picture so far into *screen, which is made (or remade, when the
// render size has changed) as a camera texture of the picture's size. The
// frame's camera must not be in an update: the copy reads the finished
// surface. False when the backend could not make the texture or copy into it.
bool iScreenPassCapture(RwRaster** screen);

// The picture so far, for a pass that reads it into a target of its own and
// does not draw into the frame while it reads: *picture is the frame itself
// where the backend can lend it (GL3, rw::gl3::virtualScreenReadRaster), so
// nothing is copied, and otherwise *screen after iScreenPassCapture. The
// frame's camera must not be in an update. False when neither works.
bool iScreenPassReadFrame(RwRaster** screen, RwRaster** picture);

// Make, or remake at a new size, a target. Its contents start cleared to
// transparent black.
bool iScreenPassMakeTarget(iScreenPassTarget* t, RwInt32 w, RwInt32 h);
void iScreenPassDestroyTarget(iScreenPassTarget* t);

// Clear a target to transparent black. Its camera must not be in an update.
void iScreenPassClear(iScreenPassTarget* t);

// Save the render states a pass changes, then set the ones every pass wants:
// no depth test or write, no fog, no culling, linear filtering, clamped
// addressing, an alpha test that passes everything, and all four channels
// written. iScreenPassEnd puts back what was saved.
void iScreenPassBegin(iScreenPassState* saved);
void iScreenPassEnd(const iScreenPassState* saved);

// The colour write mask, as librw's ColorWriteMask bits.
void iScreenPassSetWriteMask(U32 mask);

// One quad from (x0, y0) to (x1, y1) in the current camera's pixels, textured
// with `src` from (u0, v0) to (u1, v1), its texels multiplied by the colour
// (r, g, b, a). Blending is whatever the caller set; vertex alpha is turned on
// so the blend sees the alpha. The half-pixel shift Direct3D 9 needs for a
// screen-aligned quad is applied here.
void iScreenPassQuad(RwRaster* src, F32 x0, F32 y0, F32 x1, F32 y1, F32 u0, F32 v0, F32 u1,
                     F32 v1, U8 r, U8 g, U8 b, U8 a);

// A GX blend factor (GXBlendFactor, 0 to 7) as the RenderWare blend function
// that does the same, as a source factor or as a destination factor: GX's
// colour factors read the other side (GX_BL_SRCCLR as a source factor is the
// destination colour).
RwBlendFunction iScreenPassGXBlend(S32 factor, bool source);

#endif
