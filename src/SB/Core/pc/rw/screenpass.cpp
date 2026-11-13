// What TSSM's screen passes share. The interface, and why the passes need no
// shader of their own, are in screenpass.h.

#include <rwcore.h>

#if defined(RW_D3D9) || defined(RW_D3D11)
#include <windows.h>
#define WITH_D3D
#endif
#ifdef RW_D3D9
#include <d3d9.h>
#endif
#ifdef RW_D3D11
#include <d3d11.h>
#endif

#include "rw.h"

// After rw.h: types.h, which iScreen.h includes, breaks librw's rwengine.h
// when it comes first.
#include "iScreen.h"

#ifdef RW_D3D_ANY
#include "src/d3d/rwd3dimpl.h"
#endif
// GL3 needs no header of its own here: rw.h includes src/gl/rwgl3.h itself.
#ifdef RW_C3D
#include "3ds/c3d/rwc3d.h"
#endif

#include "backend.h"
#include "iDeviceReset.h"
#include "screenpass.h"

bool iScreenPassReady()
{
#ifdef RW_D3D_ANY
    if (iBackendIsD3D() || iBackendIsVulkan())
    {
        // Not while a D3D9 device is lost: the copy and the targets would fail,
        // and each pass takes a failure as the device refusing it for good.
        return rw::d3d::deviceOpen() != 0 && iDeviceUsable();
    }
#endif
#ifdef RW_GL3
    if (iBackendIsGL3())
    {
        return rw::gl3::virtualScreenFramebuffer() != 0;
    }
#endif
#ifdef RW_C3D
    if (iBackendIsC3D())
    {
        return rw::c3d::deviceOpen() != 0;
    }
#endif
    return false;
}

bool iScreenPassOn(S32 pass)
{
#ifdef RW_C3D
    if (iBackendIsC3D())
    {
        return rw::c3d::screenPassOn((int)pass) != 0;
    }
#endif
    (void)pass;
    return true;
}

void iScreenPassExtent(RwInt32* w, RwInt32* h)
{
    *w = 0;
    *h = 0;
#ifdef RW_D3D_ANY
    if (iBackendIsD3D() || iBackendIsVulkan())
    {
        rw::d3d::getScreenExtent(w, h);
        return;
    }
#endif
#ifdef RW_GL3
    if (iBackendIsGL3())
    {
        *w = (RwInt32)rw::gl3::virtualScreenWidth;
        *h = (RwInt32)rw::gl3::virtualScreenHeight;
    }
#endif
#ifdef RW_C3D
    if (iBackendIsC3D())
    {
        // The top screen, which the device draws every full-screen camera into.
        *w = 400;
        *h = 240;
    }
#endif
}

// The copy itself. Both want a camera texture of exactly the picture's size:
// D3D9 stretches with no filter, D3D11 and Vulkan copy the whole resource, and
// GL3 blits only between equal extents.
static bool copyFrame(RwRaster* dst)
{
#ifdef RW_D3D_ANY
    if (iBackendIsD3D() || iBackendIsVulkan())
    {
        return rw::d3d::captureFrame(reinterpret_cast<rw::Raster*>(dst)) != 0;
    }
#endif
#ifdef RW_GL3
    if (iBackendIsGL3())
    {
        return rw::gl3::copyVirtualScreen(reinterpret_cast<rw::Raster*>(dst)) != 0;
    }
#endif
#ifdef RW_C3D
    if (iBackendIsC3D())
    {
        // Every pass's copy shares one block: each is drawn from before the
        // next copy is made.
        return rw::c3d::copyScreen(reinterpret_cast<rw::Raster*>(dst), 0) != 0;
    }
#endif
    return false;
}

bool iScreenPassCapture(RwRaster** screen)
{
    RwInt32 w = 0;
    RwInt32 h = 0;
    iScreenPassExtent(&w, &h);
    if (w <= 0 || h <= 0)
    {
        return false;
    }

    if (*screen != NULL && ((*screen)->width != w || (*screen)->height != h))
    {
        RwRasterDestroy(*screen);
        *screen = NULL;
    }

    if (*screen == NULL)
    {
        *screen = RwRasterCreate(w, h, 32, rwRASTERTYPECAMERATEXTURE | rwRASTERFORMAT8888);
        if (*screen == NULL)
        {
            return false;
        }
    }

    return copyFrame(*screen);
}

bool iScreenPassReadFrame(RwRaster** screen, RwRaster** picture)
{
#ifdef RW_GL3
    if (iBackendIsGL3())
    {
        rw::Raster* frame = rw::gl3::virtualScreenReadRaster();
        if (frame != NULL)
        {
            *picture = reinterpret_cast<RwRaster*>(frame);
            return true;
        }
    }
#endif
    if (!iScreenPassCapture(screen))
    {
        return false;
    }
    *picture = *screen;
    return true;
}

bool iScreenPassMakeTarget(iScreenPassTarget* t, RwInt32 w, RwInt32 h)
{
    if (t->camera != NULL && t->width == w && t->height == h)
    {
        return true;
    }

    iScreenPassDestroyTarget(t);

    t->raster = RwRasterCreate(w, h, 32, rwRASTERTYPECAMERATEXTURE | rwRASTERFORMAT8888);
    if (t->raster == NULL)
    {
        return false;
    }

    t->camera = RwCameraCreate();
    if (t->camera == NULL)
    {
        RwRasterDestroy(t->raster);
        t->raster = NULL;
        return false;
    }

    RwCameraSetFrame(t->camera, RwFrameCreate());
    RwCameraSetRaster(t->camera, t->raster);

    // Im2D takes its screen z from these, so they have to be sane even though
    // nothing here is in a world or projected.
    RwCameraSetNearClipPlane(t->camera, 0.05f);
    RwCameraSetFarClipPlane(t->camera, 400.0f);

    t->width = w;
    t->height = h;

    iScreenPassClear(t);
    return true;
}

// The frame and the raster by hand, then the camera: RenderWare leaves both to
// the caller, as iCamera.cpp's destroy does.
void iScreenPassDestroyTarget(iScreenPassTarget* t)
{
    if (t->camera != NULL)
    {
        RwFrame* frame = RwCameraGetFrame(t->camera);
        if (frame != NULL)
        {
            RwCameraSetFrame(t->camera, NULL);
            RwFrameDestroy(frame);
        }

        RwCameraSetRaster(t->camera, NULL);
        RwCameraDestroy(t->camera);
    }

    if (t->raster != NULL)
    {
        RwRasterDestroy(t->raster);
    }

    t->camera = NULL;
    t->raster = NULL;
    t->width = 0;
    t->height = 0;
}

void iScreenPassClear(iScreenPassTarget* t)
{
    if (t->camera == NULL)
    {
        return;
    }

    // OpenGL's clear obeys the colour mask, which the game may have narrowed
    // (iDrawSetFBMSK), so all four channels are opened for it.
    U32 mask = rw::GetRenderState(rw::COLORWRITEMASK);
    rw::SetRenderState(rw::COLORWRITEMASK, ISCREENPASS_WRITE_ALL);

    RwRGBA clear = { 0, 0, 0, 0 };
    RwCameraClear(t->camera, &clear, rwCAMERACLEARIMAGE);

    rw::SetRenderState(rw::COLORWRITEMASK, mask);
}

void iScreenPassBegin(iScreenPassState* s)
{
    // Defaults first: a state the shim cannot report keeps the value the game
    // most often leaves there.
    s->raster = NULL;
    s->filter = rwFILTERLINEAR;
    s->addressU = rwTEXTUREADDRESSWRAP;
    s->addressV = rwTEXTUREADDRESSWRAP;
    s->ztest = TRUE;
    s->zwrite = TRUE;
    s->fog = FALSE;
    s->cull = rwCULLMODECULLBACK;
    s->vertexAlpha = FALSE;
    s->srcBlend = rwBLENDSRCALPHA;
    s->destBlend = rwBLENDINVSRCALPHA;

    RwRenderStateGet(rwRENDERSTATETEXTURERASTER, &s->raster);
    RwRenderStateGet(rwRENDERSTATETEXTUREFILTER, &s->filter);
    RwRenderStateGet(rwRENDERSTATETEXTUREADDRESSU, &s->addressU);
    RwRenderStateGet(rwRENDERSTATETEXTUREADDRESSV, &s->addressV);
    RwRenderStateGet(rwRENDERSTATEZTESTENABLE, &s->ztest);
    RwRenderStateGet(rwRENDERSTATEZWRITEENABLE, &s->zwrite);
    RwRenderStateGet(rwRENDERSTATEFOGENABLE, &s->fog);
    RwRenderStateGet(rwRENDERSTATECULLMODE, &s->cull);
    RwRenderStateGet(rwRENDERSTATEVERTEXALPHAENABLE, &s->vertexAlpha);
    RwRenderStateGet(rwRENDERSTATESRCBLEND, &s->srcBlend);
    RwRenderStateGet(rwRENDERSTATEDESTBLEND, &s->destBlend);

    s->writeMask = rw::GetRenderState(rw::COLORWRITEMASK);
    s->alphaFunc = rw::GetRenderState(rw::ALPHATESTFUNC);
    s->alphaRef = rw::GetRenderState(rw::ALPHATESTREF);

    RwRenderStateSet(rwRENDERSTATETEXTUREFILTER, (void*)rwFILTERLINEAR);
    RwRenderStateSet(rwRENDERSTATETEXTUREADDRESS, (void*)rwTEXTUREADDRESSCLAMP);
    RwRenderStateSet(rwRENDERSTATEZTESTENABLE, (void*)FALSE);
    RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)FALSE);
    RwRenderStateSet(rwRENDERSTATEFOGENABLE, (void*)FALSE);
    RwRenderStateSet(rwRENDERSTATECULLMODE, (void*)rwCULLMODECULLNONE);

    // Every texel is drawn, whatever its alpha. A pass that left the game's
    // test in force (alpha >= 1 most of the time) would keep last frame's
    // value wherever this frame's came out fully transparent.
    rw::SetRenderState(rw::ALPHATESTFUNC, rw::ALPHAALWAYS);
    rw::SetRenderState(rw::COLORWRITEMASK, ISCREENPASS_WRITE_ALL);
}

void iScreenPassEnd(const iScreenPassState* s)
{
    RwRenderStateSet(rwRENDERSTATETEXTURERASTER, s->raster);
    RwRenderStateSet(rwRENDERSTATETEXTUREFILTER, (void*)(UPtr)s->filter);
    RwRenderStateSet(rwRENDERSTATETEXTUREADDRESSU, (void*)(UPtr)s->addressU);
    RwRenderStateSet(rwRENDERSTATETEXTUREADDRESSV, (void*)(UPtr)s->addressV);
    RwRenderStateSet(rwRENDERSTATEZTESTENABLE, (void*)(UPtr)s->ztest);
    RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)(UPtr)s->zwrite);
    RwRenderStateSet(rwRENDERSTATEFOGENABLE, (void*)(UPtr)s->fog);
    RwRenderStateSet(rwRENDERSTATECULLMODE, (void*)(UPtr)s->cull);
    RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)(UPtr)s->vertexAlpha);
    RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)(UPtr)s->srcBlend);
    RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)(UPtr)s->destBlend);

    rw::SetRenderState(rw::ALPHATESTFUNC, s->alphaFunc);
    rw::SetRenderState(rw::ALPHATESTREF, s->alphaRef);
    rw::SetRenderState(rw::COLORWRITEMASK, s->writeMask);
}

void iScreenPassSetWriteMask(U32 mask)
{
    rw::SetRenderState(rw::COLORWRITEMASK, mask);
}

void iScreenPassQuad(RwRaster* src, F32 x0, F32 y0, F32 x1, F32 y1, F32 u0, F32 v0, F32 u1,
                     F32 v1, U8 r, U8 g, U8 b, U8 a)
{
    RwRenderStateSet(rwRENDERSTATETEXTURERASTER, src);
    RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)TRUE);

    // Direct3D 9 puts pixel x's centre at x, not x + 0.5, so a screen-aligned
    // quad has to move half a pixel up and left to sample texel centres
    // (glow.cpp has the measurement). The other backends need nothing.
    const F32 half = iBackendHalfPixel();
    const F32 z = RwIm2DGetNearScreenZ();

    RwIm2DVertex vx[4];
    for (S32 i = 0; i < 4; i++)
    {
        const bool right = (i & 2) != 0;
        const bool bottom = (i & 1) != 0;

        RwIm2DVertexSetScreenX(&vx[i], (right ? x1 : x0) - half);
        RwIm2DVertexSetScreenY(&vx[i], (bottom ? y1 : y0) - half);
        RwIm2DVertexSetScreenZ(&vx[i], z);
        RwIm2DVertexSetRecipCameraZ(&vx[i], 1.0f);
        RwIm2DVertexSetU(&vx[i], right ? u1 : u0, 1.0f);
        RwIm2DVertexSetV(&vx[i], bottom ? v1 : v0, 1.0f);
        RwIm2DVertexSetIntRGBA(&vx[i], r, g, b, a);
    }

    {
        // A pass quad covers its target, the screen included.
        iScreenScreenSpace screenSpace;
        RwIm2DRenderPrimitive(rwPRIMTYPETRISTRIP, vx, 4);
    }
}

RwBlendFunction iScreenPassGXBlend(S32 factor, bool source)
{
    switch (factor)
    {
    case 0: // GX_BL_ZERO
        return rwBLENDZERO;
    case 1: // GX_BL_ONE
        return rwBLENDONE;
    case 2: // GX_BL_SRCCLR, which as a source factor is GX_BL_DSTCLR
        return source ? rwBLENDDESTCOLOR : rwBLENDSRCCOLOR;
    case 3: // GX_BL_INVSRCCLR, as a source factor GX_BL_INVDSTCLR
        return source ? rwBLENDINVDESTCOLOR : rwBLENDINVSRCCOLOR;
    case 4: // GX_BL_SRCALPHA
        return rwBLENDSRCALPHA;
    case 5: // GX_BL_INVSRCALPHA
        return rwBLENDINVSRCALPHA;
    case 6: // GX_BL_DSTALPHA
        return rwBLENDDESTALPHA;
    case 7: // GX_BL_INVDSTALPHA
        return rwBLENDINVDESTALPHA;
    default:
        return source ? rwBLENDONE : rwBLENDZERO;
    }
}
