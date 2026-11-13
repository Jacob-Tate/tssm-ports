// xScreenWarp's draw as a PC pass. What the console does is in iScreenWarp.h;
// the steps the passes share are in screenpass.h.

#include <rwcore.h>

#include "rw.h"

// After rw.h: types.h, which iScreen.h includes, breaks librw's rwengine.h
// when it comes first.
#include "iScreen.h"

#include "backend.h"
#include "iScreenWarp.h"
#include "screenpass.h"

#include <stdio.h>

// config.ini's video.screen_warp, pushed down by iSystem.cpp.
static S32 sEnabled = TRUE;

static RwRaster* sScreen;
static S32 sFailed;
static S32 sReported;

// One column of cells: two vertices per grid row.
#define WARP_MAX_CELLS 64

void iScreenWarpRender(RwCamera* cam, const F32* uv, S32 cells)
{
    if (!sEnabled || sFailed || cam == NULL || uv == NULL || cells < 1 ||
        cells > WARP_MAX_CELLS || !iScreenPassReady())
    {
        return;
    }

    iScreenPassState saved;
    iScreenPassBegin(&saved);

    RwCameraEndUpdate(cam);
    const bool copied = iScreenPassCapture(&sScreen);
    RwCameraBeginUpdate(cam);

    if (!copied)
    {
        sFailed = 1;
        printf("tssm: the screen warp is off -- the frame could not be copied into a texture\n");
        fflush(stdout);
        iScreenPassEnd(&saved);
        return;
    }

    if (!sReported)
    {
        sReported = 1;
        printf("tssm: screen warp: %dx%d, %dx%d cells\n", (int)sScreen->width,
               (int)sScreen->height, (int)cells, (int)cells);
        fflush(stdout);
    }

    RwRaster* frame = RwCameraGetRaster(cam);
    const F32 w = frame != NULL ? (F32)frame->width : (F32)sScreen->width;
    const F32 h = frame != NULL ? (F32)frame->height : (F32)sScreen->height;
    const F32 dx = w / (F32)cells;
    const F32 dy = h / (F32)cells;
    const F32 half = iBackendHalfPixel();
    const F32 z = RwIm2DGetNearScreenZ();

    // The console's states: texture colour, ONE/ZERO, and the write mask the
    // game left (GXSetAlphaUpdate is not touched).
    iScreenPassSetWriteMask(saved.writeMask);
    RwRenderStateSet(rwRENDERSTATETEXTURERASTER, sScreen);
    RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)FALSE);
    RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)rwBLENDONE);
    RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDZERO);

    const S32 stride = cells + 1;
    RwIm2DVertex strip[2 * (WARP_MAX_CELLS + 1)];

    for (S32 i = 0; i < cells; i++)
    {
        const F32* uv0 = uv + 2 * (i * stride);
        const F32* uv1 = uv + 2 * ((i + 1) * stride);
        const F32 x0 = dx * (F32)i - half;
        const F32 x1 = dx * (F32)(i + 1) - half;

        for (S32 j = 0; j <= cells; j++)
        {
            const F32 y = dy * (F32)j - half;
            RwIm2DVertex* a = &strip[2 * j];
            RwIm2DVertex* b = &strip[2 * j + 1];

            RwIm2DVertexSetScreenX(a, x0);
            RwIm2DVertexSetScreenY(a, y);
            RwIm2DVertexSetScreenZ(a, z);
            RwIm2DVertexSetRecipCameraZ(a, 1.0f);
            RwIm2DVertexSetU(a, uv0[2 * j], 1.0f);
            RwIm2DVertexSetV(a, uv0[2 * j + 1], 1.0f);
            RwIm2DVertexSetIntRGBA(a, 0xFF, 0xFF, 0xFF, 0xFF);

            RwIm2DVertexSetScreenX(b, x1);
            RwIm2DVertexSetScreenY(b, y);
            RwIm2DVertexSetScreenZ(b, z);
            RwIm2DVertexSetRecipCameraZ(b, 1.0f);
            RwIm2DVertexSetU(b, uv1[2 * j], 1.0f);
            RwIm2DVertexSetV(b, uv1[2 * j + 1], 1.0f);
            RwIm2DVertexSetIntRGBA(b, 0xFF, 0xFF, 0xFF, 0xFF);
        }

        {
            // The warp mesh covers the whole picture.
            iScreenScreenSpace screenSpace;
            RwIm2DRenderPrimitive(rwPRIMTYPETRISTRIP, strip, 2 * stride);
        }
    }

    iScreenPassEnd(&saved);
}

void iScreenWarpSetEnabled(S32 enabled)
{
    sEnabled = enabled ? TRUE : FALSE;
}
