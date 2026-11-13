#include "iScrFX.h"
#include "iCamera.h"

#include <rwcore.h>
#include <rwplcore.h>
#include <types.h>

#include <dolphin/gx/GXManage.h>
#include <dolphin/gx/GXPerf.h>

// RenderWare GameCube driver (dltoken.c). The token counter is volatile (shared with the GX
// draw-sync interrupt): retail loads it twice in a row with no store in between.
extern "C" {
extern volatile RwUInt16 _RwDlTokenCurrent;
RwBool _rwDlTokenQueryDone(RwUInt16 token);
}

extern RwTexture* gxFlareTexture;
extern RwFrame* gxFlareFrame;

static U32 sMotionBlurEnabled;
static _iMotionBlurData sMBD = {};

// RenderWare GameCube driver display-list token helpers.
static inline void _rwDlTokenSetDrawSync()
{
    GXSetDrawSync(_RwDlTokenCurrent);
    _RwDlTokenCurrent = (_RwDlTokenCurrent + 1) % 0xE000;
}

static inline void _rwDlTokenWait(RwUInt16 token)
{
    if (token == _RwDlTokenCurrent)
    {
        _rwDlTokenSetDrawSync();
    }

    while (!_rwDlTokenQueryDone(token))
    {
    }
}

static S32 DetermineFlareOcclusion(RwCamera* camera, RwTexture* flareTexture, RwFrame* frame);
static void RenderRect(RwRect* srcRect, F32 cameraZ);

static inline S32 iScrFxMotionBlurOpen(RwCamera* camera)
{
    return 0;
}

static inline void iCameraOverlayRender(RwCamera* pCamera, RwRaster* ras, RwRGBA col)
{
    for (S32 i = 0; i < 4; i++)
    {
        RwIm2DVertexSetIntRGBA(&sMBD.vertex[i], col.red, col.green, col.blue, col.alpha);
    }

    RwRenderStateSet(rwRENDERSTATETEXTUREFILTER, (void*)rwFILTERNEAREST);
    RwRenderStateSet(rwRENDERSTATEFOGENABLE, (void*)FALSE);
    RwRenderStateSet(rwRENDERSTATEZTESTENABLE, (void*)FALSE);
    RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)FALSE);
    RwRenderStateSet(rwRENDERSTATETEXTURERASTER, ras);
    RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)TRUE);
    RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)rwBLENDSRCALPHA);
    RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDINVSRCALPHA);

    RwIm2DRenderIndexedPrimitive(rwPRIMTYPETRILIST, sMBD.vertex, 4, sMBD.index, 6);

    RwRenderStateSet(rwRENDERSTATEFOGENABLE, (void*)FALSE);
    RwRenderStateSet(rwRENDERSTATEZTESTENABLE, (void*)TRUE);
    RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)TRUE);
    RwRenderStateSet(rwRENDERSTATETEXTURERASTER, NULL);
    RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)FALSE);
    RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)rwBLENDSRCALPHA);
    RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDINVSRCALPHA);
}

static inline void iScrFxMotionBlurRender(RwCamera* camera, U32 alpha)
{
    if (sMBD.motionBlurFrontBuffer != NULL)
    {
        RwRGBA col = { 0xff, 0xff, 0xff, (U8)alpha };
        iCameraOverlayRender(camera, sMBD.motionBlurFrontBuffer, col);
    }
}

void iScrFxInit()
{
}

void iScrFxBegin()
{
    RwRenderStateSet(rwRENDERSTATETEXTUREFILTER, (void*)rwFILTERNEAREST);
    RwRenderStateSet(rwRENDERSTATEFOGENABLE, (void*)FALSE);
    RwRenderStateSet(rwRENDERSTATEZTESTENABLE, (void*)FALSE);
    RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)FALSE);
    RwRenderStateSet(rwRENDERSTATETEXTURERASTER, NULL);
    RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)TRUE);
    RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)rwBLENDSRCALPHA);
    RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDINVSRCALPHA);
    RwRenderStateSet(rwRENDERSTATECULLMODE, (void*)rwCULLMODECULLNONE);
}

void iScrFxEnd()
{
    RwRenderStateSet(rwRENDERSTATEFOGENABLE, (void*)FALSE);
    RwRenderStateSet(rwRENDERSTATEZTESTENABLE, (void*)TRUE);
    RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)TRUE);
    RwRenderStateSet(rwRENDERSTATETEXTURERASTER, NULL);
    RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)FALSE);
    RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)rwBLENDONE);
    RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDZERO);
}

void iScrFxDrawBox(F32 x1, F32 y1, F32 x2, F32 y2, U8 red, U8 green, U8 blue, U8 alpha,
                   bool clear_z_to_far)
{
    static RwIm2DVertex v[4];
    static RwImVertexIndex indices[4] = { 0, 1, 2, 3 };
    F32 farz;
    S32 old_z_write;

    RwIm2DVertexSetScreenX(&v[0], x1);
    RwIm2DVertexSetScreenY(&v[0], y1);
    RwIm2DVertexSetScreenX(&v[1], x2);
    RwIm2DVertexSetScreenY(&v[1], y1);
    RwIm2DVertexSetScreenX(&v[2], x1);
    RwIm2DVertexSetScreenY(&v[2], y2);
    RwIm2DVertexSetScreenX(&v[3], x2);
    RwIm2DVertexSetScreenY(&v[3], y2);

    RwIm2DVertexSetIntRGBA(&v[0], red, green, blue, alpha);
    RwIm2DVertexSetIntRGBA(&v[1], red, green, blue, alpha);
    RwIm2DVertexSetIntRGBA(&v[2], red, green, blue, alpha);
    RwIm2DVertexSetIntRGBA(&v[3], red, green, blue, alpha);

    if (clear_z_to_far == TRUE)
    {
        farz = RwIm2DGetFarScreenZ();
        RwIm2DVertexSetScreenZ(&v[0], farz);
        RwIm2DVertexSetScreenZ(&v[1], farz);
        RwIm2DVertexSetScreenZ(&v[2], farz);
        RwIm2DVertexSetScreenZ(&v[3], farz);

        RwRenderStateGet(rwRENDERSTATEZWRITEENABLE, &old_z_write);
        RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)TRUE);
        RwIm2DRenderIndexedPrimitive(rwPRIMTYPETRISTRIP, v, 4, indices, 4);
        RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)old_z_write);
    }
    else
    {
        RwIm2DRenderIndexedPrimitive(rwPRIMTYPETRISTRIP, v, 4, indices, 4);
    }
}

void iCameraMotionBlurActivate(U32 activate)
{
    sMotionBlurEnabled = activate;
}

void iCameraSetBlurriness(F32 amount)
{
    if (amount <= 0.0f)
    {
        sMotionBlurEnabled = FALSE;
    }
    else
    {
        if (amount > 1.0f)
        {
            amount = 1.0f;
        }
        sMotionBlurEnabled = TRUE;
        sMBD.motionBlurAlpha = 254.0f * amount + 0.5f;
    }
}

void iScrFxCameraCreated(RwCamera* pCamera)
{
    U32 i;
    U32 v;

    sMBD.motionBlurAlpha = 0x90;
    sMBD.motionBlurFrontBuffer = NULL;

    // Triangle-list indices for the overlay quad: (v, v+1, v+2), (v, v+2, v+3) per 4 vertices.
    for (i = 0, v = 0; i < 6; i += 6, v += 4)
    {
        sMBD.index[i + 0] = v;
        sMBD.index[i + 1] = v + 1;
        sMBD.index[i + 2] = v + 2;
        sMBD.index[i + 3] = v;
        sMBD.index[i + 4] = v + 2;
        sMBD.index[i + 5] = v + 3;
    }

    iScrFxMotionBlurOpen(pCamera);
}

void iScrFxCameraEndScene(RwCamera* pCamera)
{
    if (sMotionBlurEnabled && sMBD.motionBlurAlpha != 0)
    {
        iScrFxMotionBlurRender(pCamera, (U8)sMBD.motionBlurAlpha);
    }
}

S32 iScrFxCameraDestroyed(RwCamera* pCamera)
{
    if (sMBD.motionBlurFrontBuffer != NULL)
    {
        RwRasterDestroy(sMBD.motionBlurFrontBuffer);
        sMBD.motionBlurFrontBuffer = NULL;
        return 1;
    }
    return 0;
}

// Defined here, between the motion-blur and lens-flare code: retail's .sdata has iScrFxDrawBox's
// indices before FLARE_SIZE.
U32 FLARE_SIZE = 64;

static void RenderRect(RwRect* srcRect, F32 cameraZ)
{
    RwIm2DVertex v[4];
    RwImVertexIndex indices[4];
    F32 recipZ = 1.0f / cameraZ;
    RwCamera* camera = RwCameraGetCurrentCamera();
    F32 screenZ;

    if (RwCameraGetProjection(camera) == rwPERSPECTIVE)
    {
        screenZ = recipZ * camera->zScale + camera->zShift;
    }
    else
    {
        screenZ = cameraZ * camera->zScale + camera->zShift;
    }

    RwIm2DVertexSetScreenX(&v[0], srcRect->x);
    RwIm2DVertexSetScreenY(&v[0], srcRect->y);
    RwIm2DVertexSetScreenZ(&v[0], screenZ);

    RwIm2DVertexSetScreenX(&v[1], srcRect->x + srcRect->w);
    RwIm2DVertexSetScreenY(&v[1], srcRect->y);
    RwIm2DVertexSetScreenZ(&v[1], screenZ);

    RwIm2DVertexSetScreenX(&v[2], srcRect->x + srcRect->w);
    RwIm2DVertexSetScreenY(&v[2], srcRect->y + srcRect->h);
    RwIm2DVertexSetScreenZ(&v[2], screenZ);

    RwIm2DVertexSetScreenX(&v[3], srcRect->x);
    RwIm2DVertexSetScreenY(&v[3], srcRect->y + srcRect->h);
    RwIm2DVertexSetScreenZ(&v[3], screenZ);

    RwIm2DVertexSetIntRGBA(&v[0], 255, 255, 255, 255);
    RwIm2DVertexSetIntRGBA(&v[1], 255, 255, 255, 255);
    RwIm2DVertexSetIntRGBA(&v[2], 255, 255, 255, 255);
    RwIm2DVertexSetIntRGBA(&v[3], 255, 255, 255, 255);

    indices[0] = 1;
    indices[1] = 0;
    indices[2] = 2;
    indices[3] = 3;

    RwIm2DRenderIndexedPrimitive(rwPRIMTYPETRISTRIP, v, 4, indices, 4);
}

// The area is first srcRect.w * srcRect.h and is recomputed in the 'srcRect.x < 0' branch before w
// is clipped, as in Ratatouille's debug build; retail shares the two products.
static S32 DetermineFlareOcclusion(RwCamera* camera, RwTexture* flareTexture, RwFrame* frame)
{
    RwRect srcRect;
    S32 size = FLARE_SIZE;
    RwVideoMode vidInfo;
    RwV3d FlarePositionPixel;
    RwMatrix invMtx;
    RwMatrix tmp;
    RwV3d FlarePosition;
    S32 area;
    RwMatrix* matrix;
    const RwV2d* vw;
    u32 top_pixels_in;
    u32 top_pixels_out;
    u32 bot_pixels_in;
    u32 bot_pixels_out;
    u32 clr_pixels_in;
    u32 copy_clks;

    matrix = RwFrameGetLTM(RwCameraGetFrame(camera));
    RwMatrixInvert(&invMtx, matrix);
    RwMatrixMultiply(&tmp, RwFrameGetLTM(frame), &invMtx);

    FlarePosition.x = tmp.pos.x;
    FlarePosition.y = tmp.pos.y;
    FlarePosition.z = tmp.pos.z;
    if (FlarePosition.z <= 0.0f)
    {
        return 0;
    }

    RwV3dScaleMacro(&FlarePositionPixel, &FlarePosition, 1.0f / FlarePosition.z);

    RwEngineGetVideoModeInfo(&vidInfo, RwEngineGetCurrentVideoMode());

    vw = &camera->viewWindow;
    FlarePositionPixel.x = vidInfo.width * (vw->x - FlarePositionPixel.x) / (2.0f * vw->x);
    FlarePositionPixel.y = vidInfo.height * (vw->y - FlarePositionPixel.y) / (2.0f * vw->y);
    srcRect.x = FlarePositionPixel.x - size / 2;
    srcRect.h = size * (vw->x / vw->y) * ((F32)vidInfo.height / (F32)vidInfo.width);
    srcRect.w = size;
    srcRect.y = FlarePositionPixel.y - srcRect.h / 2;
    area = srcRect.w * srcRect.h;

    if (srcRect.x + srcRect.w > vidInfo.width)
    {
        srcRect.w -= srcRect.x + srcRect.w - vidInfo.width;
        srcRect.x = vidInfo.width - srcRect.w;
    }
    else if (srcRect.x < 0)
    {
        area = srcRect.w * srcRect.h;
        srcRect.w += srcRect.x;
        srcRect.x = 0;
    }

    if (srcRect.y + srcRect.h > vidInfo.height)
    {
        area = srcRect.w * srcRect.h;
        srcRect.h -= srcRect.y + srcRect.h - vidInfo.height;
        srcRect.y = vidInfo.height - srcRect.h;
    }
    else if (srcRect.y < 0)
    {
        area = srcRect.w * srcRect.h;
        srcRect.h += srcRect.y;
        srcRect.y = 0;
    }

    if (srcRect.w <= 0 || srcRect.h <= 0)
    {
        return 0;
    }

    RwCameraBeginUpdateWrapper(camera);

    GXClearPixMetric();
    _rwDlTokenSetDrawSync();

    RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)TRUE);
    RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)rwBLENDZERO);
    RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDONE);
    RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)FALSE);
    RwRenderStateSet(rwRENDERSTATEZTESTENABLE, (void*)TRUE);
    RwRenderStateSet(rwRENDERSTATETEXTURERASTER, NULL);

    RenderRect(&srcRect, FlarePosition.z);

    RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)TRUE);
    RwRenderStateSet(rwRENDERSTATEZTESTENABLE, (void*)TRUE);
    RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)rwBLENDSRCALPHA);
    RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDINVSRCALPHA);

    U32 token = _RwDlTokenCurrent;
    _rwDlTokenSetDrawSync();
    _rwDlTokenWait(token);

    GXReadPixMetric(&top_pixels_in, &top_pixels_out, &bot_pixels_in, &bot_pixels_out,
                    &clr_pixels_in, &copy_clks);

    RwCameraEndUpdate(camera);

    S32 result =
        (F32)((top_pixels_in > bot_pixels_out ? top_pixels_in : bot_pixels_out) * 255) / (F32)area;
    return result > 255 ? 255 : result;
}

void iScrFxLensFlare_Reset()
{
}

void iScrFxLensFlare_Render(RwCamera* camera, iScrFxLensFlare* flare,
                            void (*renderCB)(RwRaster*, RwFrame*, U8, void*))
{
    S32 result;

    if (camera == NULL || flare == NULL)
    {
        return;
    }

    if (gxFlareTexture == NULL)
    {
        return;
    }

    RwFrame* frame = gxFlareFrame;
    RwFrameTranslate(frame, (RwV3d*)&flare->pos, rwCOMBINEREPLACE);
    RwFrameUpdateObjects(frame);

    RwCameraEndUpdate(camera);

    result = DetermineFlareOcclusion(camera, gxFlareTexture, frame);
    if (result > 0 && RwCameraBeginUpdateWrapper(camera))
    {
        RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)rwBLENDONE);
        RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDONE);

        renderCB(RwTextureGetRaster(gxFlareTexture), frame, result, flare->source);

        RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)rwBLENDSRCALPHA);
        RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDINVSRCALPHA);

        RwCameraEndUpdate(camera);
    }

    RwCameraBeginUpdateWrapper(camera);
}
