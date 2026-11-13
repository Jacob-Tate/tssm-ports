#include "zUIBox.h"

#include "xEvent.h"
#include "zGlobals.h"
#include "zMain.h"

#include <PowerPC_EABI_Support/MSL_C++/MSL_Common/Include/new>

void zUIBox::DoInit()
{
    zUI::DoInit();
    if (linkCount)
    {
        link = (xLinkAsset*)(GetAsset() + 1);
    }

    curRotation = 0.0f;
}

void zUIBox::DoReset()
{
    zUI::DoReset();
    ResetTextures();
}

void zUIBox::DoResetMotion()
{
    zUI::DoResetMotion();
    curRotation = 0.0f;
}

void zUIBox::DoHandleEvent(xBase* from, U32 toEvent, const F32* toParam, xBase* toParamWidget,
                           U32 toParamWidgetID)
{
    switch (toEvent)
    {
    case eEventUIChangeTexture:
    {
        RwTexture* pTexture = (RwTexture*)toParamWidget;
        S32 which = toParam[0];

        if (which != 0)
        {
            if (pTexture)
            {
                textures[which - 1].Reference(pTexture);
            }
            else
            {
                textures[which - 1].Reference(toParamWidgetID, toParam[0] != 0.0f);
            }
        }
        else
        {
            for (S32 i = 0; i < 9; i++)
            {
                if (pTexture)
                {
                    textures[i].Reference(pTexture);
                }
                else
                {
                    textures[i].Reference(toParamWidgetID, toParam[0] != 0.0f);
                }
            }
        }
        break;
    }
    default:
        zUI::DoHandleEvent(from, toEvent, toParam, toParamWidget, toParamWidgetID);
        break;
    }
}

void zUIBox::DoApplyMotionFrame(const zUIMotionFrame* frame)
{
    zUI::DoApplyMotionFrame(frame);
    curRotation = frame->rotation;
}

void zUIBox::DoRender() const
{
    // Not in the PS2 DWARF; retail keeps the asset pointer in a register across RenderPart.
    const zUIBoxAsset* boxAsset = (const zUIBoxAsset*)asset;

    F32 x1 = current.x;
    F32 y1 = current.y;
    F32 x2 = x1 + current.width;
    F32 y2 = y1 + current.height;

    F32 curBorderWidth = boxAsset->borderWidth * current.width / boxAsset->width;
    F32 curBorderHeight = boxAsset->borderHeight * current.height / boxAsset->height;

    F32 x1i = x1 + curBorderWidth;
    F32 y1i = y1 + curBorderHeight;
    F32 x2i = x2 - curBorderWidth;
    F32 y2i = y2 - curBorderHeight;

    F32 xCenter = 0.0f;
    F32 yCenter = 0.0f;

    if (curRotation != 0.0f)
    {
        xCenter = (x1 + x2) / 2.0f;
        yCenter = (y1 + y2) / 2.0f;
    }

    RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)rwBLENDSRCALPHA);
    RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDINVSRCALPHA);
    RwRenderStateSet(rwRENDERSTATESHADEMODE, (void*)rwSHADEMODEFLAT);
    RwRenderStateSet(rwRENDERSTATETEXTUREFILTER, (void*)rwFILTERLINEAR);

    RenderPart(0, x1, y1, x1i, y1i, 1.0f, 1.0f, curRotation, xCenter, yCenter);
    RenderPart(1, x2i, y1, x2, y1i, 1.0f, 1.0f, curRotation, xCenter, yCenter);
    RenderPart(2, x1, y2i, x1i, y2, 1.0f, 1.0f, curRotation, xCenter, yCenter);
    RenderPart(3, x2i, y2i, x2, y2, 1.0f, 1.0f, curRotation, xCenter, yCenter);

    F32 yScale =
        (boxAsset->stretchUVsOnMotionScale ? boxAsset->height - 2.0f * boxAsset->borderHeight :
                                             y2i - y1i) /
        boxAsset->heightPerUV;
    RenderPart(4, x1, y1i, x1i, y2i, 1.0f, boxAsset->scaleVSide ? yScale : 1.0f, curRotation,
               xCenter, yCenter);
    RenderPart(5, x2i, y1i, x2, y2i, 1.0f, boxAsset->scaleVSide ? yScale : 1.0f, curRotation,
               xCenter, yCenter);

    F32 xScale =
        (boxAsset->stretchUVsOnMotionScale ? boxAsset->width - 2.0f * boxAsset->borderWidth :
                                             x2i - x1i) /
        boxAsset->widthPerUV;
    RenderPart(6, x1i, y1, x2i, y1i, boxAsset->scaleHSide ? xScale : 1.0f, 1.0f, curRotation,
               xCenter, yCenter);
    RenderPart(7, x1i, y2i, x2i, y2, boxAsset->scaleHSide ? xScale : 1.0f, 1.0f, curRotation,
               xCenter, yCenter);

    if (boxAsset->scaleCenter)
    {
        yScale =
            (boxAsset->stretchUVsOnMotionScale ? boxAsset->height - 2.0f * boxAsset->borderHeight :
                                                 y2i - y1i) /
            boxAsset->centerHeightPerUV;
        xScale =
            (boxAsset->stretchUVsOnMotionScale ? boxAsset->width - 2.0f * boxAsset->borderWidth :
                                                 x2i - x1i) /
            boxAsset->centerWidthPerUV;
    }
    else
    {
        xScale = yScale = 1.0f;
    }

    RenderPart(8, x1i, y1i, x2i, y2i, xScale, yScale, curRotation, xCenter, yCenter);
}

void zUIBox::RenderPart(S32 p, F32 x1, F32 y1, F32 x2, F32 y2, F32 uScale, F32 vScale, F32 rotation,
                        F32 xPivot, F32 yPivot) const
{
    static RwIm2DVertex vertex[4];

    const Part& part = ((const zUIBoxAsset*)asset)->parts[p];

    if (!part.enabled)
    {
        return;
    }

    F32 ulx, uly, urx, ury, llx, lly, lrx, lry;
    F32 tempScale;

    if (rotation != 0.0f)
    {
        if (rotation > 0.0f)
        {
            rotation = rotation - (S32)rotation / 360 * 360;
        }
        else
        {
            rotation = 360.0f + (rotation - (S32)rotation / 360 * 360);
        }

        rotation = (PI / 180.0f) * (360.0f - rotation);

        x1 -= xPivot;
        x2 -= xPivot;
        y1 -= yPivot;
        y2 -= yPivot;

        F32 sinA = isin(rotation);
        F32 cosA = icos(rotation);

        ulx = xPivot + (x1 * cosA + y1 * sinA);
        uly = yPivot + (-x1 * sinA + y1 * cosA);
        urx = xPivot + (x2 * cosA + y1 * sinA);
        ury = yPivot + (-x2 * sinA + y1 * cosA);
        llx = xPivot + (x1 * cosA + y2 * sinA);
        lly = yPivot + (-x1 * sinA + y2 * cosA);
        lrx = xPivot + (x2 * cosA + y2 * sinA);
        lry = yPivot + (-x2 * sinA + y2 * cosA);
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

    if (part.rotation == 90 || part.rotation == 270)
    {
        tempScale = uScale;
        uScale = vScale;
        vScale = tempScale;
    }

    RwTexture* pTexture = textures[p].GetTexture();
    if (!pTexture)
    {
        return;
    }

    RwRenderStateSet(rwRENDERSTATETEXTURERASTER, (void*)pTexture->raster);
    RwRenderStateSet(rwRENDERSTATETEXTUREADDRESSU,
                     (void*)(uScale == 1.0f ? rwTEXTUREADDRESSCLAMP : rwTEXTUREADDRESSWRAP));
    RwRenderStateSet(rwRENDERSTATETEXTUREADDRESSV,
                     (void*)(vScale == 1.0f ? rwTEXTUREADDRESSCLAMP : rwTEXTUREADDRESSWRAP));

    F32 z = RwIm2DGetNearScreenZ();
    if (xabs(z) <= 1e-5f)
    {
        z = z >= 0.0f ? 1e-5f : -1e-5f;
    }

    F32 cz = RwCameraGetNearClipPlane(globals.screen ? globals.screen->icam :
                                                       RwCameraGetCurrentCamera());
    F32 cooz = 1.0f / z;

    // The offset/sign terms are constant in both the GC and PS2 builds; Ratatouille lists them as
    // locals of this block under these names.
    {
        F32 offset_x = 0.0f;
        F32 offset_y = 0.0f;
        F32 offset_px = 0.0f;
        F32 offset_py = 0.0f;
        F32 offset_cx = 0.0f;
        F32 offset_cy = 0.0f;
        F32 g_center_x = 1000.0f;
        F32 g_center_y = 1000.0f;
        F32 x_sign = 1.0f;
        F32 y_sign = 1.0f;

        RwIm2DVertexSetScreenX(&vertex[0], offset_cx + (ulx + offset_x) + x_sign * offset_px);
        RwIm2DVertexSetScreenY(&vertex[0], offset_cy + (uly + offset_y) + y_sign * offset_py);
        RwIm2DVertexSetScreenZ(&vertex[0], z);
        RwIm2DVertexSetCameraZ(&vertex[0], cz);
        RwIm2DVertexSetRecipCameraZ(&vertex[0], cooz);
        RwIm2DVertexSetU(&vertex[0], part.u1 * uScale, cooz);
        RwIm2DVertexSetV(&vertex[0], part.v1 * vScale, cooz);
        RwIm2DVertexSetIntRGBA(&vertex[0], part.color.r, part.color.g, part.color.b,
                               current.color.a);

        x_sign = urx < g_center_x ? 1.0f : -1.0f;
        y_sign = ury < g_center_y ? 1.0f : -1.0f;
        RwIm2DVertexSetScreenX(&vertex[1], offset_cx + (urx - offset_x) + x_sign * offset_px);
        RwIm2DVertexSetScreenY(&vertex[1], offset_cy + (ury + offset_y) + y_sign * offset_py);
        RwIm2DVertexSetScreenZ(&vertex[1], z);
        RwIm2DVertexSetCameraZ(&vertex[1], cz);
        RwIm2DVertexSetRecipCameraZ(&vertex[1], cooz);
        RwIm2DVertexSetU(&vertex[1], part.u2 * uScale, cooz);
        RwIm2DVertexSetV(&vertex[1], part.v2 * vScale, cooz);
        RwIm2DVertexSetIntRGBA(&vertex[1], part.color.r, part.color.g, part.color.b,
                               current.color.a);

        x_sign = llx < g_center_x ? 1.0f : -1.0f;
        y_sign = lly < g_center_y ? 1.0f : -1.0f;
        RwIm2DVertexSetScreenX(&vertex[2], offset_cx + (llx + offset_x) + x_sign * offset_px);
        RwIm2DVertexSetScreenY(&vertex[2], offset_cy + (lly - offset_y) + y_sign * offset_py);
        RwIm2DVertexSetScreenZ(&vertex[2], z);
        RwIm2DVertexSetCameraZ(&vertex[2], cz);
        RwIm2DVertexSetRecipCameraZ(&vertex[2], cooz);
        RwIm2DVertexSetU(&vertex[2], part.u3 * uScale, cooz);
        RwIm2DVertexSetV(&vertex[2], part.v3 * vScale, cooz);
        RwIm2DVertexSetIntRGBA(&vertex[2], part.color.r, part.color.g, part.color.b,
                               current.color.a);

        x_sign = lrx < g_center_x ? 1.0f : -1.0f;
        y_sign = lry < g_center_y ? 1.0f : -1.0f;
        RwIm2DVertexSetScreenX(&vertex[3], offset_cx + (lrx - offset_x) + x_sign * offset_px);
        RwIm2DVertexSetScreenY(&vertex[3], offset_cy + (lry - offset_y) + y_sign * offset_py);
        RwIm2DVertexSetScreenZ(&vertex[3], z);
        RwIm2DVertexSetCameraZ(&vertex[3], cz);
        RwIm2DVertexSetRecipCameraZ(&vertex[3], cooz);
        RwIm2DVertexSetU(&vertex[3], part.u4 * uScale, cooz);
        RwIm2DVertexSetV(&vertex[3], part.v4 * vScale, cooz);
        RwIm2DVertexSetIntRGBA(&vertex[3], part.color.r, part.color.g, part.color.b,
                               current.color.a);
    }

    RwImVertexIndex indexes[4] = { 0, 2, 1, 3 };
    RwIm2DRenderIndexedPrimitive(rwPRIMTYPETRISTRIP, vertex, 4, indexes, 4);
}

void zUIBox_Init(xBase& data, xDynAsset& asset, size_t)
{
    zUIBox_Init((zUIBox*)&data, (zUIBoxAsset*)&asset);
}

void zUIBox_Init(zUIBox* ui, zUIBoxAsset* asset)
{
    new (ui) zUIBox;
    zUI_Init(ui, asset);
}
