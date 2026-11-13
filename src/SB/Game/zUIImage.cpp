#include "zUIImage.h"

#include "xEvent.h"

#include <PowerPC_EABI_Support/MSL_C++/MSL_Common/Include/new>

void zUIImage::DoInit()
{
    zUI::DoInit();
    link = (xLinkAsset*)(GetAsset() + 1);
}

void zUIImage::DoReset()
{
    zUI::DoReset();
    SetTexture(GetAsset()->image, false);
}

void zUIImage::DoResetMotion()
{
    zUI::DoResetMotion();
    currentImage.rotation = GetAsset()->rotation;
    currentImage.offsetU = 0.0f;
    currentImage.offsetV = 0.0f;
}

void zUIImage::DoHandleEvent(xBase* from, U32 toEvent, const F32* toParam, xBase* toParamWidget,
                             U32 toParamWidgetID)
{
    switch (toEvent)
    {
    case eEventUIChangeTexture:
    {
        RwTexture* pTexture = (RwTexture*)toParamWidget;
        if (pTexture)
        {
            texture.Reference(pTexture);
        }
        else
        {
            SetTexture(toParamWidgetID, toParam[0] != 0.0f);
        }
        break;
    }
    case eEventPlayMovie:
        PlayMovie(toParamWidgetID, toParam[0] != 0.0f, toParam[1] != 0.0f, toParam[2], toParam[3]);
        break;
    case eEventUISetBoxMapping:
    {
        F32 u0 = toParam[0];
        F32 v0 = toParam[1];
        F32 u1 = toParam[2];
        F32 v1 = toParam[3];
        zUIImageAsset* pAsset = (zUIImageAsset*)asset;

        pAsset->u1 = u0;
        pAsset->v1 = v0;
        pAsset->u2 = u1;
        pAsset->v2 = v0;
        pAsset->u3 = u0;
        pAsset->v3 = v1;
        pAsset->u4 = u1;
        pAsset->v4 = v1;
        break;
    }
    default:
        zUI::DoHandleEvent(from, toEvent, toParam, toParamWidget, toParamWidgetID);
        break;
    }
}

// Locals as in Ratatouille. bBlendROP is bool (the DWARF dumps have no bool type; a U8 adds a
// truncation retail lacks). rotation is assigned inside the test, as retail loads 0.0f first, and
// x2/y2 read current.x/y again rather than using x1/y1.
void zUIImage::DoRender() const
{
    static void* modeTranslate[3] = { (void*)rwTEXTUREADDRESSWRAP, (void*)rwTEXTUREADDRESSCLAMP,
                                      (void*)rwTEXTUREADDRESSMIRROR };

    RwTexture* pTexture = texture.GetTexture();
    if (!pTexture)
    {
        return;
    }

    RwRaster* raster = pTexture->raster;

    bool bBlendROP = !texture.IsMoviePlaying() && (GetAsset()->iflags & 0x2);

    RwRenderStateSet(rwRENDERSTATETEXTURERASTER, (void*)raster);

    void* oldAlphaFunction = NULL;
    if (bBlendROP)
    {
        RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)rwBLENDSRCALPHA);
        RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDINVSRCALPHA);
    }
    else
    {
        RwRenderStateGet(rwRENDERSTATEALPHATESTFUNCTION, &oldAlphaFunction);
        RwRenderStateSet(rwRENDERSTATEALPHATESTFUNCTION, (void*)rwALPHATESTFUNCTIONALWAYS);
        RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)rwBLENDONE);
        RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDZERO);
    }

    RwRenderStateSet(rwRENDERSTATESHADEMODE, (void*)rwSHADEMODEGOURAUD);
    RwRenderStateSet(rwRENDERSTATETEXTUREFILTER, (void*)rwFILTERLINEAR);
    RwRenderStateSet(rwRENDERSTATETEXTUREADDRESSU, modeTranslate[GetAsset()->addressModeU]);
    RwRenderStateSet(rwRENDERSTATETEXTUREADDRESSV, modeTranslate[GetAsset()->addressModeV]);

    RwIm2DVertex vertex[4];
    const zUIImageAsset* asset = GetAsset();
    F32 x1 = current.x;
    F32 y1 = current.y;
    F32 x2 = current.x + current.width;
    F32 y2 = current.y + current.height;
    F32 ulx, uly, urx, ury, llx, lly, lrx, lry;
    F32 rotation;

    if ((rotation = currentImage.rotation) != 0.0f)
    {
        rotation = (PI / 180.0f) *
                   (360.0f - (rotation > 0.0f ? rotation - (S32)rotation / 360 * 360 :
                                                360.0f + (rotation - (S32)rotation / 360 * 360)));

        F32 offx = (x1 + x2) / 2.0f;
        F32 offy = (y1 + y2) / 2.0f;

        x1 -= offx;
        x2 -= offx;
        y1 -= offy;
        y2 -= offy;

        F32 sinA = isin(rotation);
        F32 cosA = icos(rotation);

        ulx = offx + (x1 * cosA + y1 * sinA);
        uly = offy + (-x1 * sinA + y1 * cosA);
        urx = offx + (x2 * cosA + y1 * sinA);
        ury = offy + (-x2 * sinA + y1 * cosA);
        llx = offx + (x1 * cosA + y2 * sinA);
        lly = offy + (-x1 * sinA + y2 * cosA);
        lrx = offx + (x2 * cosA + y2 * sinA);
        lry = offy + (-x2 * sinA + y2 * cosA);
    }
    else
    {
        ulx = x1;
        uly = y1;
        urx = x2;
        ury = y1;
        llx = x1;
        lly = y2;
        lrx = x2;
        lry = y2;
    }

    ulx = (1.0f / 640.0f) * (ulx * FB_XRES);
    uly = (1.0f / 480.0f) * (uly * FB_YRES);
    urx = (1.0f / 640.0f) * (urx * FB_XRES);
    ury = (1.0f / 480.0f) * (ury * FB_YRES);
    llx = (1.0f / 640.0f) * (llx * FB_XRES);
    lly = (1.0f / 480.0f) * (lly * FB_YRES);
    lrx = (1.0f / 640.0f) * (lrx * FB_XRES);
    lry = (1.0f / 480.0f) * (lry * FB_YRES);

    const xColor_tag* color = &current.color;

    F32 z = RwIm2DGetNearScreenZ();
    if (xabs(z) <= 1e-5f)
    {
        z = z >= 0.0f ? 1e-5f : -1e-5f;
    }

    F32 u1, v1, u2, v2, u3, v3, u4, v4;
    if ((asset->iflags & 0x1) && raster)
    {
        u1 = 0.0f;
        v1 = 0.0f;
        u2 = asset->width / raster->width;
        v2 = 0.0f;
        u3 = 0.0f;
        v3 = asset->height / raster->height;
        u4 = asset->width / raster->width;
        v4 = asset->height / raster->height;
    }
    else
    {
        u1 = asset->u1;
        v1 = asset->v1;
        u2 = asset->u2;
        v2 = asset->v2;
        u3 = asset->u3;
        v3 = asset->v3;
        u4 = asset->u4;
        v4 = asset->v4;
    }

    if (currentImage.offsetU != 0.0f)
    {
        u1 += currentImage.offsetU;
        u2 += currentImage.offsetU;
        u3 += currentImage.offsetU;
        u4 += currentImage.offsetU;
    }

    if (currentImage.offsetV != 0.0f)
    {
        v1 += currentImage.offsetV;
        v2 += currentImage.offsetV;
        v3 += currentImage.offsetV;
        v4 += currentImage.offsetV;
    }

    F32 cz = RwCameraGetNearClipPlane(globals.screen ? globals.screen->icam :
                                                       RwCameraGetCurrentCamera());
    F32 cooz = 1.0f / z;

    {
        F32 offset_x = 0.0f;
        F32 offset_y = 0.0f;
        F32 offset_cx = 0.0f;
        F32 offset_cy = 0.0f;
        F32 x_sign = 1.0f;
        F32 y_sign = 1.0f;
        F32 g_center_x = 1000.0f;
        F32 g_center_y = 1000.0f;

        RwIm2DVertexSetScreenX(&vertex[0], (ulx + offset_x) + x_sign * offset_cx);
        RwIm2DVertexSetScreenY(&vertex[0], (uly + offset_y) + y_sign * offset_cy);
        RwIm2DVertexSetScreenZ(&vertex[0], z);
        RwIm2DVertexSetCameraZ(&vertex[0], cz);
        RwIm2DVertexSetRecipCameraZ(&vertex[0], cooz);
        RwIm2DVertexSetU(&vertex[0], u1, cooz);
        RwIm2DVertexSetV(&vertex[0], v1, cooz);
        RwIm2DVertexSetIntRGBA(&vertex[0], color->r * asset->color1.r / 255,
                               color->g * asset->color1.g / 255, color->b * asset->color1.b / 255,
                               color->a * asset->color1.a / 255);

        x_sign = llx < g_center_x ? 1.0f : -1.0f;
        y_sign = lly < g_center_y ? 1.0f : -1.0f;
        RwIm2DVertexSetScreenX(&vertex[1], (llx + offset_x) + x_sign * offset_cx);
        RwIm2DVertexSetScreenY(&vertex[1], (lly - offset_y) + y_sign * offset_cy);
        RwIm2DVertexSetScreenZ(&vertex[1], z);
        RwIm2DVertexSetCameraZ(&vertex[1], cz);
        RwIm2DVertexSetRecipCameraZ(&vertex[1], cooz);
        RwIm2DVertexSetU(&vertex[1], u3, cooz);
        RwIm2DVertexSetV(&vertex[1], v3, cooz);
        RwIm2DVertexSetIntRGBA(&vertex[1], color->r * asset->color3.r / 255,
                               color->g * asset->color3.g / 255, color->b * asset->color3.b / 255,
                               color->a * asset->color3.a / 255);

        x_sign = urx < g_center_x ? 1.0f : -1.0f;
        y_sign = ury < g_center_y ? 1.0f : -1.0f;
        RwIm2DVertexSetScreenX(&vertex[2], (urx - offset_x) + x_sign * offset_cx);
        RwIm2DVertexSetScreenY(&vertex[2], (ury + offset_y) + y_sign * offset_cy);
        RwIm2DVertexSetScreenZ(&vertex[2], z);
        RwIm2DVertexSetCameraZ(&vertex[2], cz);
        RwIm2DVertexSetRecipCameraZ(&vertex[2], cooz);
        RwIm2DVertexSetU(&vertex[2], u2, cooz);
        RwIm2DVertexSetV(&vertex[2], v2, cooz);
        RwIm2DVertexSetIntRGBA(&vertex[2], color->r * asset->color2.r / 255,
                               color->g * asset->color2.g / 255, color->b * asset->color2.b / 255,
                               color->a * asset->color2.a / 255);

        x_sign = lrx < g_center_x ? 1.0f : -1.0f;
        y_sign = lry < g_center_y ? 1.0f : -1.0f;
        RwIm2DVertexSetScreenX(&vertex[3], (lrx - offset_x) + x_sign * offset_cx);
        RwIm2DVertexSetScreenY(&vertex[3], (lry - offset_y) + y_sign * offset_cy);
        RwIm2DVertexSetScreenZ(&vertex[3], z);
        RwIm2DVertexSetCameraZ(&vertex[3], cz);
        RwIm2DVertexSetRecipCameraZ(&vertex[3], cooz);
        RwIm2DVertexSetU(&vertex[3], u4, cooz);
        RwIm2DVertexSetV(&vertex[3], v4, cooz);
        RwIm2DVertexSetIntRGBA(&vertex[3], color->r * asset->color4.r / 255,
                               color->g * asset->color4.g / 255, color->b * asset->color4.b / 255,
                               color->a * asset->color4.a / 255);
    }

    RwIm2DRenderPrimitive(rwPRIMTYPETRISTRIP, vertex, 4);

    if (!bBlendROP)
    {
        if (!oldAlphaFunction)
        {
            RwRenderStateSet(rwRENDERSTATEALPHATESTFUNCTION,
                             (void*)rwALPHATESTFUNCTIONGREATEREQUAL);
        }
        else
        {
            RwRenderStateSet(rwRENDERSTATEALPHATESTFUNCTION, oldAlphaFunction);
        }
    }
}

void zUIImage::DoInitMotion()
{
    zUI::DoInitMotion();
    startMovementImage = currentImage;
}

void zUIImage::DoApplyMotionFrame(const zUIMotionFrame* frame)
{
    zUI::DoApplyMotionFrame(frame);

    currentImage.rotation = startMovementImage.rotation + frame->rotation;
    currentImage.offsetU = startMovementImage.offsetU + frame->offsetU;

    if (GetAsset()->addressModeU != rwTEXTUREADDRESSWRAP)
    {
        if (currentImage.offsetU >= 1.0f)
        {
            currentImage.offsetU -= (S32)currentImage.offsetU;
        }
        else if (currentImage.offsetU < 0.0f)
        {
            currentImage.offsetU += 1 - (S32)currentImage.offsetU;
        }
    }

    currentImage.offsetV = startMovementImage.offsetV + frame->offsetV;

    if (GetAsset()->addressModeV != rwTEXTUREADDRESSWRAP)
    {
        // Retail bug: the V wrap truncates offsetU instead of offsetV.
        if (currentImage.offsetV >= 1.0f)
        {
            currentImage.offsetV -= (S32)currentImage.offsetU;
        }
        else if (currentImage.offsetV < 0.0f)
        {
            currentImage.offsetV += 1 - (S32)currentImage.offsetU;
        }
    }
}

void zUIImage::Visible()
{
    zUI::Visible();
}

void zUIImage::Invisible()
{
    if (texture.IsMoviePlaying())
    {
        texture.Reference((RwTexture*)NULL);
    }

    zUI::Invisible();
}

U8 zUIImage::Blends() const
{
    return (GetAsset()->iflags & 0x2) != 0;
}

void zUIImage_Init(xBase& data, xDynAsset& asset, size_t)
{
    zUIImage_Init((zUIImage*)&data, (zUIImageAsset*)&asset);
}

void zUIImage_Init(zUIImage* ui, zUIImageAsset* asset)
{
    new (ui) zUIImage;
    zUI_Init(ui, asset);
}
