#include "xHudImage.h"
#include "xHudModel.h"

#include "xRand.h"
#include "xString.h"

#include "zMain.h"

#include <PowerPC_EABI_Support/MSL_C++/MSL_Common/Include/new>
#include <dolphin/gx.h>

// Fills the positions and texture coordinates of the quad starting at vert (no symbol in the
// target: inlined into the render_* functions).
static inline void set_quad(RwIm2DVertex* vert, F32 x, F32 y, F32 w, F32 h, F32 u0, F32 v0, F32 u1,
                            F32 v1)
{
    F32 x1 = x + w;
    F32 y1 = y + h;

    RwIm2DVertexSetScreenX(&vert[0], x);
    RwIm2DVertexSetScreenY(&vert[0], y);
    RwIm2DVertexSetU(&vert[0], u0, 0);
    RwIm2DVertexSetV(&vert[0], v0, 0);

    RwIm2DVertexSetScreenX(&vert[1], x1);
    RwIm2DVertexSetScreenY(&vert[1], y);
    RwIm2DVertexSetU(&vert[1], u1, 0);
    RwIm2DVertexSetV(&vert[1], v0, 0);

    RwIm2DVertexSetScreenX(&vert[2], x);
    RwIm2DVertexSetScreenY(&vert[2], y1);
    RwIm2DVertexSetU(&vert[2], u0, 0);
    RwIm2DVertexSetV(&vert[2], v1, 0);

    RwIm2DVertexSetScreenX(&vert[3], x1);
    RwIm2DVertexSetScreenY(&vert[3], y1);
    RwIm2DVertexSetU(&vert[3], u1, 0);
    RwIm2DVertexSetV(&vert[3], v1, 0);
}

// Sets the position and texture coordinates of one vertex (no symbol in the target: inlined into
// the rotating render_* functions, which address each vertex afresh).
static inline void set_vertex(RwIm2DVertex* vert, F32 x, F32 y, F32 u, F32 v)
{
    RwIm2DVertexSetScreenX(vert, x);
    RwIm2DVertexSetScreenY(vert, y);
    RwIm2DVertexSetU(vert, u, 0);
    RwIm2DVertexSetV(vert, v, 0);
}

// Sets the four vertices of a quad to white with the given alpha (0..1; no symbol in the
// target).
static inline void set_quad_alpha(RwIm2DVertex* vert, F32 fAlpha)
{
    U32 alpha = 255.0f * fAlpha;

    RwIm2DVertexSetIntRGBA(&vert[0], 255, 255, 255, alpha);
    RwIm2DVertexSetIntRGBA(&vert[1], 255, 255, 255, alpha);
    RwIm2DVertexSetIntRGBA(&vert[2], 255, 255, 255, alpha);
    RwIm2DVertexSetIntRGBA(&vert[3], 255, 255, 255, alpha);
}

void xhud::image_widget::load(xBase& data, xDynAsset& asset, size_t)
{
    init_base(data, asset, sizeof(xBase) + sizeof(image_widget));
    image_widget* widget = (image_widget*)(&data + 1);
    new (widget) image_widget((image_asset&)asset);
}

void xhud::image_widget::destroy()
{
    m_pRaster = NULL;
    m_pLayers = NULL;
    m_paVertices = NULL;
    m_paIndices = NULL;
    m_VertexMax = 0;
    m_IndexMax = 0;
    destruct();
}

U32 xhud::image_widget::type() const
{
    // Retail hashes the MODEL asset type name here (copy-paste from model_widget::type):
    // the target string pool has no "hud:image", and the PS2 demo does the same.
    static U32 myid = xStrHash(model_asset::type_name());
    return myid;
}

bool xhud::image_widget::is(U32 id) const
{
    return id == image_widget::type() || widget::is(id);
}

void xhud::image_widget::update(F32 dt)
{
    updater(dt);

    if (!visible() || rc.a <= 0.5f / 255.0f)
    {
        return;
    }

    for (U32 uLayer = 0; uLayer < m_uLayerCount; uLayer++)
    {
        if (m_pLayers[uLayer].eType == STROBE_EFFECT)
        {
            m_pLayers[uLayer].fStrobe +=
                dt * (1.0f + m_pLayers[uLayer].fStrobeHeight / (480.0f * rc.size.y)) /
                m_pLayers[uLayer].fStrobeFreq;

            if (m_pLayers[uLayer].fStrobe > 1.0f)
            {
                if (m_pLayers[uLayer].nStrobeCount > 0)
                {
                    m_pLayers[uLayer].nStrobeCount--;
                }
                m_pLayers[uLayer].fStrobe = 0.0f;
            }
        }
    }
}

void xhud::image_widget::render()
{
    U32 i;
    LAYER_FLAGS eLayerMode;
    bool bFlushTempBuffer;
    U32 uLayer;
    U32 uNewMode;
    bool bRendered;

    if (rc.a >= -0.00001f && rc.a <= 0.00001f)
    {
        return;
    }

    m_paVertices = (RwIm2DVertex*)iMemPushTemp(m_VertexMax * sizeof(RwIm2DVertex));
    m_paIndices = (U16*)iMemPushTemp(m_IndexMax * sizeof(U16));

    static F32 fNearZ = RwIm2DGetNearScreenZ();

    m_fRecipZ = 5.0f;
    if (RwCameraGetCurrentCamera())
    {
        m_fRecipZ = 1.0f / RwCameraGetNearClipPlane(RwCameraGetCurrentCamera());
    }

    for (i = 0; i < m_VertexMax; i++)
    {
        RwIm2DVertexSetScreenZ(&m_paVertices[i], fNearZ);
        RwIm2DVertexSetRecipCameraZ(&m_paVertices[i], m_fRecipZ);
        RwIm2DVertexSetIntRGBA(&m_paVertices[i], 255, 255, 255, 255);
    }

    RwRenderStateSet(rwRENDERSTATEZTESTENABLE, (void*)FALSE);
    RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)FALSE);
    RwRenderStateSet(rwRENDERSTATECULLMODE, (void*)rwCULLMODECULLBACK);
    RwRenderStateSet(rwRENDERSTATETEXTURERASTER, (void*)m_pRaster);

    m_uVertexCount = 0;
    eLayerMode = (LAYER_FLAGS)-1;
    m_uIndexCount = 0;
    bFlushTempBuffer = false;
    m_pVertices = m_paVertices;

    for (uLayer = 0; uLayer < m_uRenderLayerCount; uLayer++)
    {
        if (m_uVertexCount + 12 >= m_VertexMax)
        {
            bFlushTempBuffer = true;
        }

        if (m_uIndexCount + 24 >= m_IndexMax)
        {
            bFlushTempBuffer = true;
        }

        uNewMode = m_pLayers[uLayer].uFlags & BLEND_MASK;
        if (eLayerMode != uNewMode || bFlushTempBuffer)
        {
            if (m_uVertexCount != 0)
            {
                RwIm2DRenderIndexedPrimitive(rwPRIMTYPETRILIST, m_paVertices, m_uVertexCount,
                                             m_paIndices, m_uIndexCount);
                m_uVertexCount = 0;
                bFlushTempBuffer = false;
                m_uIndexCount = 0;
            }

            eLayerMode = (LAYER_FLAGS)uNewMode;
            if (uNewMode == BLEND)
            {
                RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)rwBLENDSRCALPHA);
                RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDINVSRCALPHA);
                iDrawSetFBMSK(0xFF000000, 0xFFFFFFFF, true);
            }
            else if (uNewMode == ADDITIVE)
            {
                RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)rwBLENDSRCALPHA);
                RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDONE);
                iDrawSetFBMSK(0xFF000000, 0xFFFFFFFF, true);
            }
            else if (uNewMode == SET_DEST_ALPHA)
            {
                RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)rwBLENDONE);
                RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDZERO);
                iDrawSetFBMSK(0x00FFFFFF, 0xFFFFFFFF, true);
                RwGameCubeSetAlphaCompare(GX_ALWAYS, 0, GX_AOP_AND, GX_ALWAYS, 0);
            }
            else if (uNewMode == USE_DEST_ALPHA)
            {
                RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)rwBLENDDESTALPHA);
                RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDONE);
                iDrawSetFBMSK(0xFF000000, 0xFFFFFFFF, true);
            }
        }

        bRendered = false;
        switch (m_pLayers[uLayer].eType)
        {
        case TOPLEFT:
        case BOTTOMLEFT:
        case BOTTOMRIGHT:
        case TOPRIGHT:
        case OFFSET_CENTERPT:
            bRendered = render_one_to_one(uLayer, m_pLayers[uLayer].eType);
            break;
        case ROTATE_CCW:
            bRendered = render_rotate_ccw(uLayer);
            break;
        case OFFSET_ROTATE_CCW:
            bRendered = render_offset_rotate_ccw(uLayer);
            break;
        case STROBE_EFFECT:
            bRendered = render_strobe_effect(uLayer);
            break;
        case SPECIAL_STROBE:
            bRendered = render_special_strobe(uLayer);
            break;
        case VARIABLE_TOP:
            bRendered = render_variable_top(uLayer);
            break;
        case VARIABLE_RIGHT:
            bRendered = render_variable_right(uLayer);
            break;
        case COUNTER_CLOCKWISE:
            bRendered = render_counter_clockwise(uLayer);
            break;
        case HALF_CCW:
            bRendered = render_half_ccw(uLayer);
            break;
        case RANDOM_V:
            bRendered = render_random_v(uLayer);
            break;
        }

        if (bRendered)
        {
            set_buffer_to_layer(m_pLayers[uLayer].eType);
        }
    }

    if (m_uVertexCount != 0)
    {
        RwIm2DRenderIndexedPrimitive(rwPRIMTYPETRILIST, m_paVertices, m_uVertexCount, m_paIndices,
                                     m_uIndexCount);
    }

    iMemPopTemp(m_paIndices);
    m_paIndices = NULL;
    iMemPopTemp(m_paVertices);
    m_paVertices = NULL;
    m_pVertices = NULL;
}

// Widget size in 640x480 screen units (no symbol in the target). A separate inline: retail
// rounds the product on its own (fmuls + fadds) instead of fusing it into a later fmadds.
static inline F32 screen_width(const xhud::render_context& rc)
{
    return 640.0f * rc.size.x;
}
static inline F32 screen_height(const xhud::render_context& rc)
{
    return 480.0f * rc.size.y;
}

// Half the widget size in 640x480 screen units (no symbol in the target). The half sizes are
// missing from the PS2 DWARF, and retail allocates them like an inline's return value.
static inline F32 half_screen_width(const xhud::render_context& rc)
{
    return 640.0f * (0.5f * rc.size.x);
}
static inline F32 half_screen_height(const xhud::render_context& rc)
{
    return 480.0f * (0.5f * rc.size.y);
}

// The GameCube build adds a half-texel inset of the UVs and doubles the quad size (the PS2 build
// does not), which needs the UV and centre locals below. uRange/vRange are our names.
bool xhud::image_widget::render_one_to_one(U32 uLayer, RENDERTYPE eType)
{
    if (m_pLayers == NULL || uLayer >= m_uLayerCount)
    {
        return false;
    }

    if (m_pLayers[uLayer].fAlpha == 0.0f &&
        (m_pLayers[uLayer].uFlags & BLEND_MASK) != SET_DEST_ALPHA)
    {
        return false;
    }

    F32 fTopU = m_pLayers[uLayer].upperLeft.u;
    F32 fTopV = m_pLayers[uLayer].upperLeft.v;
    F32 fBotU = m_pLayers[uLayer].lowerRight.u;
    F32 fBotV = m_pLayers[uLayer].lowerRight.v;
    F32 fQuadX = rc.loc.x * FB_XRES;
    F32 fQuadY = rc.loc.y * FB_YRES;
    F32 uRange = fBotU - fTopU;
    F32 vRange = fBotV - fTopV;
    F32 fQuadWidth = uRange * m_pRaster->width;
    F32 fQuadHeight = vRange * m_pRaster->height;

    F32 fHalfTexelU = 0.5f / m_pRaster->width;
    F32 fHalfTexelV = 0.5f / m_pRaster->height;
    fQuadWidth *= 2.0f;
    fQuadHeight *= 2.0f;
    fTopU += fHalfTexelU;
    fTopV += fHalfTexelV;
    fBotU -= fHalfTexelU;
    fBotV -= fHalfTexelV;

    if (eType != TOPLEFT)
    {
        if (eType == TOPRIGHT)
        {
            fQuadX = rc.loc.x * FB_XRES + screen_width(rc) - fQuadWidth;
        }
        else if (eType == BOTTOMLEFT)
        {
            fQuadY = rc.loc.y * FB_YRES + screen_height(rc) - fQuadHeight;
        }
        else if (eType == BOTTOMRIGHT)
        {
            fQuadX = rc.loc.x * FB_XRES + screen_width(rc) - fQuadWidth;
            fQuadY = rc.loc.y * FB_YRES + screen_height(rc) - fQuadHeight;
        }
        else if (eType == OFFSET_CENTERPT)
        {
            fQuadWidth *= m_pLayers[uLayer].fScaleX;
            fQuadHeight *= m_pLayers[uLayer].fScaleY;
            F32 fCenterX = rc.loc.x * FB_XRES + 0.5f * screen_width(rc);
            F32 fCenterY = rc.loc.y * FB_YRES + 0.5f * screen_height(rc);
            F32 fHalfImageWidth = 0.5f * fQuadWidth;
            F32 fHalfImageHeight = 0.5f * fQuadHeight;
            fQuadX = fCenterX + m_pLayers[uLayer].fOffsetX - fHalfImageWidth;
            fQuadY = fCenterY + m_pLayers[uLayer].fOffsetY - fHalfImageHeight;
        }
    }

    set_quad(m_pVertices + m_uVertexCount, fQuadX, fQuadY, fQuadWidth, fQuadHeight, fTopU, fTopV,
             fBotU, fBotV);
    set_quad_alpha(m_pVertices + m_uVertexCount, rc.a * m_pLayers[uLayer].fAlpha);
    return true;
}

bool xhud::image_widget::render_variable_top(U32 uLayer)
{
    if (m_pLayers == NULL || uLayer >= m_uLayerCount)
    {
        return false;
    }

    if (m_pLayers[uLayer].fAlpha == 0.0f &&
        (m_pLayers[uLayer].uFlags & BLEND_MASK) != SET_DEST_ALPHA)
    {
        return false;
    }

    F32 fPercent = m_pLayers[uLayer].fCurPercent;
    F32 fHeight = fPercent * (480.0f * rc.size.y);
    F32 fTopY = rc.loc.y * FB_YRES + 480.0f * rc.size.y - fHeight;
    F32 fTopV = fPercent * (m_pLayers[uLayer].upperLeft.v - m_pLayers[uLayer].lowerRight.v) +
                m_pLayers[uLayer].lowerRight.v;
    F32 fTopX = rc.loc.x * FB_XRES;
    F32 fTopU = m_pLayers[uLayer].upperLeft.u;
    F32 fBotU = m_pLayers[uLayer].lowerRight.u;

    set_quad(m_pVertices + m_uVertexCount, fTopX, fTopY, 640.0f * rc.size.x, fHeight, fTopU, fTopV,
             fBotU, m_pLayers[uLayer].lowerRight.v);
    set_quad_alpha(m_pVertices + m_uVertexCount, rc.a * m_pLayers[uLayer].fAlpha);
    return true;
}

bool xhud::image_widget::render_variable_right(U32 uLayer)
{
    F32 fPercent;
    F32 fTopX;
    F32 fTopY;
    F32 fTopV;
    F32 fBotU;
    F32 fBotV;

    if (m_pLayers == NULL || uLayer >= m_uLayerCount)
    {
        return false;
    }

    if (m_pLayers[uLayer].fAlpha == 0.0f &&
        (m_pLayers[uLayer].uFlags & BLEND_MASK) != SET_DEST_ALPHA)
    {
        return false;
    }

    fPercent = m_pLayers[uLayer].fCurPercent;
    fTopX = rc.loc.x * FB_XRES;
    fTopY = rc.loc.y * FB_YRES;
    fBotU = fPercent * (m_pLayers[uLayer].lowerRight.u - m_pLayers[uLayer].upperLeft.u) +
            m_pLayers[uLayer].upperLeft.u;
    fTopV = m_pLayers[uLayer].upperLeft.v;
    fBotV = m_pLayers[uLayer].lowerRight.v;

    set_quad(m_pVertices + m_uVertexCount, fTopX, fTopY, fPercent * (640.0f * rc.size.x),
             480.0f * rc.size.y, m_pLayers[uLayer].upperLeft.u, fTopV, fBotU, fBotV);
    set_quad_alpha(m_pVertices + m_uVertexCount, rc.a * m_pLayers[uLayer].fAlpha);
    return true;
}

// Statement order follows the PS2 line table (u0..v1, then the half sizes).
bool xhud::image_widget::render_counter_clockwise(U32 uLayer)
{
    if (m_pLayers == NULL || uLayer >= m_uLayerCount)
    {
        return false;
    }

    if (m_pLayers[uLayer].fAlpha == 0.0f &&
        (m_pLayers[uLayer].uFlags & BLEND_MASK) != SET_DEST_ALPHA)
    {
        return false;
    }

    F32 fHalfWidth;
    F32 fHalfHeight;
    F32 u0 = m_pLayers[uLayer].upperLeft.u;
    F32 v0 = m_pLayers[uLayer].upperLeft.v;
    F32 u1 = m_pLayers[uLayer].lowerRight.u;
    F32 v1 = m_pLayers[uLayer].lowerRight.v;
    fHalfWidth = 640.0f * (0.5f * rc.size.x);
    fHalfHeight = 480.0f * (0.5f * rc.size.y);
    F32 fCenterX = rc.loc.x * FB_XRES + fHalfWidth;
    F32 fCenterY = rc.loc.y * FB_YRES + fHalfHeight;
    F32 fCenterYPlusfHalfHeight = fCenterY + fHalfHeight;
    F32 fCenterYMinusfHalfHeight = fCenterY - fHalfHeight;
    F32 fCenterU = 0.5f * (u0 + u1);
    F32 fCenterV = 0.5f * (v0 + v1);

    F32 fRangeToEight = 8.0f * m_pLayers[uLayer].fCurPercent;
    S32 nWholeTris = fRangeToEight;
    S32 nWholeTrisPlus1 = nWholeTris + 1;
    S32 nWholeTrisPlus2 = nWholeTris + 2;

    // Builds a fan of 8 triangles around the centre, starting at the top and going counter
    // clockwise; only the vertices of the whole triangles are set here.
    switch (nWholeTrisPlus1)
    {
    case 9:
    case 8:
        set_vertex(m_pVertices + (m_uVertexCount + 9), fCenterX, fCenterYMinusfHalfHeight, fCenterU,
                   v0);
    case 7:
        set_vertex(m_pVertices + (m_uVertexCount + 8), fCenterX + fHalfWidth,
                   fCenterYMinusfHalfHeight, u1, v0);
    case 6:
        set_vertex(m_pVertices + (m_uVertexCount + 7), fCenterX + fHalfWidth, fCenterY, u1,
                   fCenterV);
    case 5:
        set_vertex(m_pVertices + (m_uVertexCount + 6), fCenterX + fHalfWidth,
                   fCenterYPlusfHalfHeight, u1, v1);
    case 4:
        set_vertex(m_pVertices + (m_uVertexCount + 5), fCenterX, fCenterYPlusfHalfHeight, fCenterU,
                   v1);
    case 3:
        set_vertex(m_pVertices + (m_uVertexCount + 4), fCenterX - fHalfWidth,
                   fCenterYPlusfHalfHeight, u0, v1);
    case 2:
        set_vertex(m_pVertices + (m_uVertexCount + 3), fCenterX - fHalfWidth, fCenterY, u0,
                   fCenterV);
    case 1:
        set_vertex(m_pVertices + (m_uVertexCount + 2), fCenterX - fHalfWidth,
                   fCenterYMinusfHalfHeight, u0, v0);
        set_vertex(m_pVertices + (m_uVertexCount + 1), fCenterX, fCenterYMinusfHalfHeight, fCenterU,
                   v0);
        set_vertex(m_pVertices + m_uVertexCount, fCenterX, fCenterY, fCenterU, fCenterV);
    }

    set_quad_alpha(m_pVertices + m_uVertexCount, rc.a);
    set_quad_alpha(m_pVertices + (m_uVertexCount + 4), rc.a);
    RwIm2DVertexSetIntRGBA(m_pVertices + m_uVertexCount + 8, 255, 255, 255, 255.0f * rc.a);
    RwIm2DVertexSetIntRGBA(m_pVertices + m_uVertexCount + 9, 255, 255, 255, 255.0f * rc.a);

    // The partial triangle: move its far vertex (and all the unused ones after it) along the
    // edge it is sweeping.
    if (nWholeTris < 8)
    {
        F32 fInterpAmount = fRangeToEight - nWholeTris;
        F32 xCoord;
        F32 yCoord;
        F32 uCoord;
        F32 vCoord;
        S32 vertexIndex0 = m_uVertexCount + nWholeTris;
        S32 vertexIndex1 = m_uVertexCount + nWholeTrisPlus1;
        S32 vertexIndex2 = m_uVertexCount + nWholeTrisPlus2;

        if (nWholeTris & 1)
        {
            // Odd triangles start at a corner: interpolate from the middle of the edge.
            xCoord = 0.5f *
                     (fInterpAmount * (m_pVertices[vertexIndex2].x - m_pVertices[vertexIndex0].x) +
                      (m_pVertices[vertexIndex2].x + m_pVertices[vertexIndex0].x));
            yCoord = 0.5f *
                     (fInterpAmount * (m_pVertices[vertexIndex2].y - m_pVertices[vertexIndex0].y) +
                      (m_pVertices[vertexIndex2].y + m_pVertices[vertexIndex0].y));
            uCoord = 0.5f *
                     (fInterpAmount * (m_pVertices[vertexIndex2].u - m_pVertices[vertexIndex0].u) +
                      (m_pVertices[vertexIndex2].u + m_pVertices[vertexIndex0].u));
            vCoord = 0.5f *
                     (fInterpAmount * (m_pVertices[vertexIndex2].v - m_pVertices[vertexIndex0].v) +
                      (m_pVertices[vertexIndex2].v + m_pVertices[vertexIndex0].v));
        }
        else
        {
            xCoord = fInterpAmount * m_pVertices[vertexIndex2].x +
                     (1.0f - fInterpAmount) * m_pVertices[vertexIndex1].x;
            yCoord = fInterpAmount * m_pVertices[vertexIndex2].y +
                     (1.0f - fInterpAmount) * m_pVertices[vertexIndex1].y;
            uCoord = fInterpAmount * m_pVertices[vertexIndex2].u +
                     (1.0f - fInterpAmount) * m_pVertices[vertexIndex1].u;
            vCoord = fInterpAmount * m_pVertices[vertexIndex2].v +
                     (1.0f - fInterpAmount) * m_pVertices[vertexIndex1].v;
        }

        for (S32 VertIndex = 9; VertIndex >= nWholeTrisPlus2; VertIndex--)
        {
            set_vertex(m_pVertices + (m_uVertexCount + VertIndex), xCoord, yCoord, uCoord, vCoord);
        }
    }

    return true;
}

// Statement order follows the PS2 line table (fClampTopY before fClampTopV, then the half sizes).
bool xhud::image_widget::render_half_ccw(U32 uLayer)
{
    if (m_pLayers == NULL || uLayer >= m_uLayerCount)
    {
        return false;
    }

    if (m_pLayers[uLayer].fAlpha == 0.0f &&
        (m_pLayers[uLayer].uFlags & BLEND_MASK) != SET_DEST_ALPHA)
    {
        return false;
    }

    F32 fPercent = m_pLayers[uLayer].fCurPercent;
    F32 fClampTopY = rc.loc.y * FB_YRES + screen_height(rc) -
                     m_pLayers[uLayer].fClampTopPercent * screen_height(rc);
    F32 fClampTopV = m_pLayers[uLayer].fClampTopPercent *
                         (m_pLayers[uLayer].upperLeft.v - m_pLayers[uLayer].lowerRight.v) +
                     m_pLayers[uLayer].lowerRight.v;
    F32 fHalfWidth = half_screen_width(rc);
    F32 fHalfHeight = half_screen_height(rc);
    F32 fCenterX = rc.loc.x * FB_XRES + fHalfWidth;
    F32 fCenterY = rc.loc.y * FB_YRES + fHalfHeight;
    F32 fCenterYPlusfHalfHeight = fCenterY + fHalfHeight;
    F32 fCenterYMinusfHalfHeight = fCenterY - fHalfHeight;

    fCenterY = fCenterY < fClampTopY ? fClampTopY : fCenterY;
    fCenterYPlusfHalfHeight =
        fCenterYPlusfHalfHeight < fClampTopY ? fClampTopY : fCenterYPlusfHalfHeight;
    fCenterYMinusfHalfHeight =
        fCenterYMinusfHalfHeight < fClampTopY ? fClampTopY : fCenterYMinusfHalfHeight;

    F32 u0 = m_pLayers[uLayer].upperLeft.u;
    F32 u1 = m_pLayers[uLayer].lowerRight.u;
    F32 fCenterU = 0.5f * (u0 + u1);
    F32 v0 = m_pLayers[uLayer].upperLeft.v;
    F32 v1 = m_pLayers[uLayer].lowerRight.v;
    F32 fCenterV = 0.5f * (v0 + v1);

    v0 = v0 < fClampTopV ? fClampTopV : v0;
    v1 = v1 < fClampTopV ? fClampTopV : v1;
    fCenterV = fCenterV < fClampTopV ? fClampTopV : fCenterV;

    F32 fRangeToFour = 4.0f * fPercent;
    S32 nWholeTris = fRangeToFour;
    S32 nWholeTrisPlus1 = nWholeTris + 1;
    S32 nWholeTrisPlus2 = nWholeTris + 2;

    // Two fans of 4 triangles (left half and right half, 6 vertices each) around the centre,
    // clipped against the clamp height.
    set_vertex(m_pVertices + (m_uVertexCount + 11), fCenterX, fCenterYMinusfHalfHeight, fCenterU,
               v0);
    set_vertex(m_pVertices + (m_uVertexCount + 10), fCenterX + fHalfWidth, fCenterYMinusfHalfHeight,
               u1, v0);
    set_vertex(m_pVertices + (m_uVertexCount + 9), fCenterX + fHalfWidth, fCenterY, u1, fCenterV);
    set_vertex(m_pVertices + (m_uVertexCount + 8), fCenterX + fHalfWidth, fCenterYPlusfHalfHeight,
               u1, v1);
    set_vertex(m_pVertices + (m_uVertexCount + 7), fCenterX, fCenterYPlusfHalfHeight, fCenterU, v1);
    set_vertex(m_pVertices + (m_uVertexCount + 6), fCenterX, fCenterY, fCenterU, fCenterV);
    set_vertex(m_pVertices + (m_uVertexCount + 5), fCenterX, fCenterYPlusfHalfHeight, fCenterU, v1);
    set_vertex(m_pVertices + (m_uVertexCount + 4), fCenterX - fHalfWidth, fCenterYPlusfHalfHeight,
               u0, v1);
    set_vertex(m_pVertices + (m_uVertexCount + 3), fCenterX - fHalfWidth, fCenterY, u0, fCenterV);
    set_vertex(m_pVertices + (m_uVertexCount + 2), fCenterX - fHalfWidth, fCenterYMinusfHalfHeight,
               u0, v0);
    set_vertex(m_pVertices + (m_uVertexCount + 1), fCenterX, fCenterYMinusfHalfHeight, fCenterU,
               v0);
    set_vertex(m_pVertices + m_uVertexCount, fCenterX, fCenterY, fCenterU, fCenterV);

    set_quad_alpha(m_pVertices + m_uVertexCount, rc.a);
    set_quad_alpha(m_pVertices + (m_uVertexCount + 4), rc.a);
    set_quad_alpha(m_pVertices + (m_uVertexCount + 8), rc.a);

    // The partial triangle of each half: move its far vertex (and all the unused ones after it)
    // along the edge it is sweeping.
    if (nWholeTris < 4)
    {
        F32 fInterpAmount = fRangeToFour - nWholeTris;
        U32 vertexIndex0;
        U32 vertexIndex1;
        U32 vertexIndex2;
        F32 xCoord[2];
        F32 yCoord[2];
        F32 uCoord[2];
        F32 vCoord[2];

        for (U32 uHalfOffset = 0; uHalfOffset <= 1; uHalfOffset++)
        {
            U32 uHalfOffsetX6 = uHalfOffset * 6;
            vertexIndex0 = m_uVertexCount + nWholeTris + uHalfOffsetX6;
            vertexIndex1 = m_uVertexCount + nWholeTrisPlus1 + uHalfOffsetX6;
            vertexIndex2 = m_uVertexCount + nWholeTrisPlus2 + uHalfOffsetX6;

            if (nWholeTris & 1)
            {
                // Odd triangles start at a corner: interpolate from the middle of the edge.
                xCoord[uHalfOffset] =
                    0.5f *
                    (fInterpAmount * (m_pVertices[vertexIndex2].x - m_pVertices[vertexIndex0].x) +
                     (m_pVertices[vertexIndex2].x + m_pVertices[vertexIndex0].x));
                yCoord[uHalfOffset] =
                    0.5f *
                    (fInterpAmount * (m_pVertices[vertexIndex2].y - m_pVertices[vertexIndex0].y) +
                     (m_pVertices[vertexIndex2].y + m_pVertices[vertexIndex0].y));
                uCoord[uHalfOffset] =
                    0.5f *
                    (fInterpAmount * (m_pVertices[vertexIndex2].u - m_pVertices[vertexIndex0].u) +
                     (m_pVertices[vertexIndex2].u + m_pVertices[vertexIndex0].u));
                vCoord[uHalfOffset] =
                    0.5f *
                    (fInterpAmount * (m_pVertices[vertexIndex2].v - m_pVertices[vertexIndex0].v) +
                     (m_pVertices[vertexIndex2].v + m_pVertices[vertexIndex0].v));
            }
            else
            {
                xCoord[uHalfOffset] = fInterpAmount * m_pVertices[vertexIndex2].x +
                                      (1.0f - fInterpAmount) * m_pVertices[vertexIndex1].x;
                yCoord[uHalfOffset] = fInterpAmount * m_pVertices[vertexIndex2].y +
                                      (1.0f - fInterpAmount) * m_pVertices[vertexIndex1].y;
                uCoord[uHalfOffset] = fInterpAmount * m_pVertices[vertexIndex2].u +
                                      (1.0f - fInterpAmount) * m_pVertices[vertexIndex1].u;
                vCoord[uHalfOffset] = fInterpAmount * m_pVertices[vertexIndex2].v +
                                      (1.0f - fInterpAmount) * m_pVertices[vertexIndex1].v;
            }

            for (U32 VertIndex = uHalfOffsetX6 + 5; VertIndex >= uHalfOffsetX6 + nWholeTrisPlus2;
                 VertIndex--)
            {
                set_vertex(m_pVertices + (m_uVertexCount + VertIndex), xCoord[uHalfOffset],
                           yCoord[uHalfOffset], uCoord[uHalfOffset], vCoord[uHalfOffset]);
            }
        }
    }

    return true;
}

bool xhud::image_widget::render_rotate_ccw(U32 uLayer)
{
    if (m_pLayers == NULL || uLayer >= m_uLayerCount)
    {
        return false;
    }

    if (m_pLayers[uLayer].fAlpha == 0.0f &&
        (m_pLayers[uLayer].uFlags & BLEND_MASK) != SET_DEST_ALPHA)
    {
        return false;
    }

    F32 fRadians = -m_pLayers[uLayer].fCurPercent * (2.0f * PI);
    F32 fTopU = m_pLayers[uLayer].upperLeft.u;
    F32 fTopV = m_pLayers[uLayer].upperLeft.v;
    F32 fBotU = m_pLayers[uLayer].lowerRight.u;
    F32 fBotV = m_pLayers[uLayer].lowerRight.v;

    F32 fTopX = rc.loc.x * FB_XRES;
    F32 fTopY = rc.loc.y * FB_YRES;

    F32 fCenterX = fTopX + m_pLayers[uLayer].fOffsetX;
    F32 fCenterY = fTopY + m_pLayers[uLayer].fOffsetY;

    F32 fBotX = fTopX + screen_width(rc);
    F32 fBotY = fTopY + screen_height(rc);

    xVec2 toTopLeft = { (fTopX - fCenterX) * m_pLayers[uLayer].fScaleX,
                        (fTopY - fCenterY) * m_pLayers[uLayer].fScaleY };
    xVec2 toBotLeft = { (fTopX - fCenterX) * m_pLayers[uLayer].fScaleX,
                        (fBotY - fCenterY) * m_pLayers[uLayer].fScaleY };
    xVec2 toTopRight = { (fBotX - fCenterX) * m_pLayers[uLayer].fScaleX,
                         (fTopY - fCenterY) * m_pLayers[uLayer].fScaleY };
    xVec2 toBotRight = { (fBotX - fCenterX) * m_pLayers[uLayer].fScaleX,
                         (fBotY - fCenterY) * m_pLayers[uLayer].fScaleY };

    F32 fSin = isin(fRadians);
    F32 fCos = icos(fRadians);

    F32 fTemp10 = toTopLeft.x * fCos - toTopLeft.y * fSin;
    F32 fTemp20 = toTopLeft.x * fSin + toTopLeft.y * fCos;
    toTopLeft.x = fTemp10;
    toTopLeft.y = fTemp20;

    fTemp10 = toBotLeft.x * fCos - toBotLeft.y * fSin;
    fTemp20 = toBotLeft.x * fSin + toBotLeft.y * fCos;
    toBotLeft.x = fTemp10;
    toBotLeft.y = fTemp20;

    fTemp10 = toTopRight.x * fCos - toTopRight.y * fSin;
    fTemp20 = toTopRight.x * fSin + toTopRight.y * fCos;
    toTopRight.x = fTemp10;
    toTopRight.y = fTemp20;

    fTemp10 = toBotRight.x * fCos - toBotRight.y * fSin;
    fTemp20 = toBotRight.x * fSin + toBotRight.y * fCos;
    toBotRight.x = fTemp10;
    toBotRight.y = fTemp20;

    set_vertex(m_pVertices + m_uVertexCount, fCenterX + toTopLeft.x, fCenterY + toTopLeft.y, fTopU,
               fTopV);
    set_vertex(m_pVertices + (m_uVertexCount + 1), fCenterX + toTopRight.x, fCenterY + toTopRight.y,
               fBotU, fTopV);
    set_vertex(m_pVertices + (m_uVertexCount + 2), fCenterX + toBotLeft.x, fCenterY + toBotLeft.y,
               fTopU, fBotV);
    set_vertex(m_pVertices + (m_uVertexCount + 3), fCenterX + toBotRight.x, fCenterY + toBotRight.y,
               fBotU, fBotV);
    set_quad_alpha(m_pVertices + m_uVertexCount, rc.a * m_pLayers[uLayer].fAlpha);
    return true;
}

bool xhud::image_widget::render_offset_rotate_ccw(U32 uLayer)
{
    if (m_pLayers == NULL || uLayer >= m_uLayerCount)
    {
        return false;
    }

    if (m_pLayers[uLayer].fAlpha == 0.0f &&
        (m_pLayers[uLayer].uFlags & BLEND_MASK) != SET_DEST_ALPHA)
    {
        return false;
    }

    F32 fRadians = -m_pLayers[uLayer].fCurPercent * (2.0f * PI);
    F32 fTopU = m_pLayers[uLayer].upperLeft.u;
    F32 fTopV = m_pLayers[uLayer].upperLeft.v;
    F32 fBotU = m_pLayers[uLayer].lowerRight.u;
    F32 fBotV = m_pLayers[uLayer].lowerRight.v;

    F32 fTopX = rc.loc.x * FB_XRES;
    F32 fTopY = rc.loc.y * FB_YRES;
    F32 fBotX = fTopX + screen_width(rc);
    F32 fBotY = fTopY + screen_height(rc);

    F32 fCenterX = 0.5f * (fTopX + fBotX);
    F32 fCenterY = 0.5f * (fTopY + fBotY);

    xVec2 toTopLeft = { (fTopX - fCenterX) * m_pLayers[uLayer].fScaleX,
                        (fTopY - fCenterY) * m_pLayers[uLayer].fScaleY };
    xVec2 toBotLeft = { (fTopX - fCenterX) * m_pLayers[uLayer].fScaleX,
                        (fBotY - fCenterY) * m_pLayers[uLayer].fScaleY };
    xVec2 toTopRight = { (fBotX - fCenterX) * m_pLayers[uLayer].fScaleX,
                         (fTopY - fCenterY) * m_pLayers[uLayer].fScaleY };
    xVec2 toBotRight = { (fBotX - fCenterX) * m_pLayers[uLayer].fScaleX,
                         (fBotY - fCenterY) * m_pLayers[uLayer].fScaleY };

    F32 fSin = isin(fRadians);
    F32 fCos = icos(fRadians);

    F32 fTemp10 = toTopLeft.x * fCos - toTopLeft.y * fSin;
    F32 fTemp20 = toTopLeft.x * fSin + toTopLeft.y * fCos;
    toTopLeft.x = fTemp10;
    toTopLeft.y = fTemp20;

    fTemp10 = toBotLeft.x * fCos - toBotLeft.y * fSin;
    fTemp20 = toBotLeft.x * fSin + toBotLeft.y * fCos;
    toBotLeft.x = fTemp10;
    toBotLeft.y = fTemp20;

    fTemp10 = toTopRight.x * fCos - toTopRight.y * fSin;
    fTemp20 = toTopRight.x * fSin + toTopRight.y * fCos;
    toTopRight.x = fTemp10;
    toTopRight.y = fTemp20;

    fTemp10 = toBotRight.x * fCos - toBotRight.y * fSin;
    fTemp20 = toBotRight.x * fSin + toBotRight.y * fCos;
    toBotRight.x = fTemp10;
    toBotRight.y = fTemp20;

    set_vertex(m_pVertices + m_uVertexCount, fCenterX + toTopLeft.x + m_pLayers[uLayer].fOffsetX,
               fCenterY + toTopLeft.y + m_pLayers[uLayer].fOffsetY, fTopU, fTopV);
    set_vertex(m_pVertices + (m_uVertexCount + 1),
               fCenterX + toTopRight.x + m_pLayers[uLayer].fOffsetX,
               fCenterY + toTopRight.y + m_pLayers[uLayer].fOffsetY, fBotU, fTopV);
    set_vertex(m_pVertices + (m_uVertexCount + 2),
               fCenterX + toBotLeft.x + m_pLayers[uLayer].fOffsetX,
               fCenterY + toBotLeft.y + m_pLayers[uLayer].fOffsetY, fTopU, fBotV);
    set_vertex(m_pVertices + (m_uVertexCount + 3),
               fCenterX + toBotRight.x + m_pLayers[uLayer].fOffsetX,
               fCenterY + toBotRight.y + m_pLayers[uLayer].fOffsetY, fBotU, fBotV);
    set_quad_alpha(m_pVertices + m_uVertexCount, rc.a * m_pLayers[uLayer].fAlpha);
    return true;
}

// fRealBottomY, fStrobeRangePixels and the two percents are not in the PS2 DWARF (an earlier
// build); retail's registers and operand order need them as named locals.
bool xhud::image_widget::render_strobe_effect(U32 uLayer)
{
    if (m_pLayers == NULL || uLayer >= m_uLayerCount)
    {
        return false;
    }

    if (m_pLayers[uLayer].fAlpha == 0.0f &&
        (m_pLayers[uLayer].uFlags & BLEND_MASK) != SET_DEST_ALPHA)
    {
        return false;
    }

    if (m_pLayers[uLayer].nStrobeCount == 0)
    {
        return false;
    }

    F32 fRealBottomY = screen_height(rc);
    F32 fStrobeRangePixels =
        (m_pLayers[uLayer].fMaxPercent - m_pLayers[uLayer].fMinPercent) * screen_height(rc) +
        m_pLayers[uLayer].fStrobeHeight;
    F32 fBottomY = (fRealBottomY + m_pLayers[uLayer].fStrobeHeight) -
                   fStrobeRangePixels * m_pLayers[uLayer].fStrobe;
    F32 fTopY = fBottomY - m_pLayers[uLayer].fStrobeHeight;

    if (fBottomY > fRealBottomY)
    {
        fBottomY = fRealBottomY;
    }

    F32 fRealTopY = (1.0f - m_pLayers[uLayer].fCurPercent) * (screen_height(rc));
    if (fRealTopY >= fBottomY)
    {
        fTopY = fBottomY;
    }
    else if (fTopY < fRealTopY)
    {
        fTopY = fRealTopY;
    }

    F32 fTopYPercent = fTopY / fRealBottomY;
    F32 fBottomYPercent = fBottomY / fRealBottomY;
    F32 vRange = m_pLayers[uLayer].lowerRight.v - m_pLayers[uLayer].upperLeft.v;
    F32 v0 = fTopYPercent * vRange + m_pLayers[uLayer].upperLeft.v;
    F32 v1 = fBottomYPercent * vRange + m_pLayers[uLayer].upperLeft.v;
    F32 u0 = m_pLayers[uLayer].upperLeft.u;
    F32 u1 = m_pLayers[uLayer].lowerRight.u;

    fTopY += rc.loc.y * FB_YRES;
    fBottomY += rc.loc.y * FB_YRES;
    F32 fTopX = rc.loc.x * FB_XRES;

    set_quad(m_pVertices + m_uVertexCount, fTopX, fTopY, screen_width(rc), fBottomY - fTopY, u0, v0,
             u1, v1);
    set_quad_alpha(m_pVertices + m_uVertexCount, rc.a * m_pLayers[uLayer].fAlpha);
    return true;
}

// fRealBottomY and fStrobeRangePixels are not in the PS2 DWARF (an earlier build); retail's
// registers and operand order need them as named locals, set from the screen_height() inline.
bool xhud::image_widget::render_special_strobe(U32 uLayer)
{
    if (m_pLayers == NULL || uLayer >= m_uLayerCount)
    {
        return false;
    }

    if (m_pLayers[uLayer].fAlpha == 0.0f &&
        (m_pLayers[uLayer].uFlags & BLEND_MASK) != SET_DEST_ALPHA)
    {
        return false;
    }

    F32 fRealBottomY = screen_height(rc);
    F32 fStrobeRangePixels =
        (m_pLayers[uLayer].fMaxPercent - m_pLayers[uLayer].fMinPercent) * screen_height(rc) +
        m_pLayers[uLayer].fStrobeHeight;
    F32 fBottomY = (fRealBottomY + m_pLayers[uLayer].fStrobeHeight) -
                   fStrobeRangePixels * m_pLayers[uLayer].fStrobe;
    F32 fTopY = fBottomY - m_pLayers[uLayer].fStrobeHeight;

    if (fBottomY > fRealBottomY)
    {
        fBottomY = fRealBottomY;
    }

    if (fTopY < 0.0f)
    {
        fTopY = 0.0f;
    }

    F32 v0 = m_pLayers[uLayer].upperLeft.v;
    F32 v1 = m_pLayers[uLayer].lowerRight.v;
    F32 u0 = m_pLayers[uLayer].upperLeft.u;
    F32 u1 = m_pLayers[uLayer].lowerRight.u;

    fTopY += rc.loc.y * FB_YRES;
    fBottomY += rc.loc.y * FB_YRES;
    F32 fTopX = rc.loc.x * FB_XRES;

    set_quad(m_pVertices + m_uVertexCount, fTopX, fTopY, screen_width(rc), fBottomY - fTopY, u0, v0,
             u1, v1);
    set_quad_alpha(m_pVertices + m_uVertexCount, rc.a * m_pLayers[uLayer].fAlpha);
    return true;
}

// fHeight and fTopV are initialised from inline helpers' return values (which is why the PS2
// DWARF drops them); the PS2 line table puts the height before the width.
bool xhud::image_widget::render_random_v(U32 uLayer)
{
    if (m_pLayers == NULL || uLayer >= m_uLayerCount)
    {
        return false;
    }

    if (m_pLayers[uLayer].fAlpha == 0.0f &&
        (m_pLayers[uLayer].uFlags & BLEND_MASK) != SET_DEST_ALPHA)
    {
        return false;
    }

    F32 fTopX = rc.loc.x * FB_XRES;
    F32 fTopY = rc.loc.y * FB_YRES;
    F32 fHeight = screen_height(rc);
    F32 fWidth = screen_width(rc) - 1.0f;

    F32 fDeltaV = fWidth / m_pRaster->height;

    F32 fTopU = m_pLayers[uLayer].upperLeft.u;
    F32 fTopV =
        xrand_RandomFloatRange(m_pLayers[uLayer].upperLeft.v, m_pLayers[uLayer].lowerRight.v);
    F32 fBotU = m_pLayers[uLayer].lowerRight.u;
    F32 fBotV = fTopV + fDeltaV;

    set_quad(m_pVertices + m_uVertexCount, fTopX, fTopY, fWidth, fHeight, fTopU, fTopV, fBotU,
             fBotV);
    set_quad_alpha(m_pVertices + m_uVertexCount, rc.a * m_pLayers[uLayer].fAlpha);
    return true;
}

// Dead-stripped placeholder, not recovered code: retail's .sdata2 has 0.0f and 1.0f before
// render_random_v's literals, from two stripped functions after it (The Incredibles has its
// layer accessors there). Their bodies are unknown.
static void __deadstripped_xHudImage(F32* out)
{
    out[0] = 0.0f;
    out[1] = 1.0f;
}

void xhud::image_widget::set_buffer_to_layer(RENDERTYPE eType)
{
    U16* pIndex = m_paIndices + m_uIndexCount;

    switch (eType)
    {
    case TOPLEFT:
    case BOTTOMLEFT:
    case BOTTOMRIGHT:
    case TOPRIGHT:
    case STROBE_EFFECT:
    case OFFSET_CENTERPT:
    case ROTATE_CCW:
    case OFFSET_ROTATE_CCW:
    case VARIABLE_TOP:
    case VARIABLE_RIGHT:
    case RANDOM_V:
    case SPECIAL_STROBE:
        pIndex[0] = m_uVertexCount;
        pIndex[1] = m_uVertexCount + 2;
        pIndex[2] = m_uVertexCount + 1;
        pIndex[3] = m_uVertexCount + 1;
        pIndex[4] = m_uVertexCount + 2;
        pIndex[5] = m_uVertexCount + 3;
        m_uVertexCount += 4;
        m_uIndexCount += 6;
        break;

    case COUNTER_CLOCKWISE:
        pIndex[0] = m_uVertexCount;
        pIndex[1] = m_uVertexCount + 1;
        pIndex[2] = m_uVertexCount + 2;
        pIndex[3] = m_uVertexCount;
        pIndex[4] = m_uVertexCount + 2;
        pIndex[5] = m_uVertexCount + 3;
        pIndex[6] = m_uVertexCount;
        pIndex[7] = m_uVertexCount + 3;
        pIndex[8] = m_uVertexCount + 4;
        pIndex[9] = m_uVertexCount;
        pIndex[10] = m_uVertexCount + 4;
        pIndex[11] = m_uVertexCount + 5;
        pIndex[12] = m_uVertexCount;
        pIndex[13] = m_uVertexCount + 5;
        pIndex[14] = m_uVertexCount + 6;
        pIndex[15] = m_uVertexCount;
        pIndex[16] = m_uVertexCount + 6;
        pIndex[17] = m_uVertexCount + 7;
        pIndex[18] = m_uVertexCount;
        pIndex[19] = m_uVertexCount + 7;
        pIndex[20] = m_uVertexCount + 8;
        pIndex[21] = m_uVertexCount;
        pIndex[22] = m_uVertexCount + 8;
        pIndex[23] = m_uVertexCount + 9;
        m_uVertexCount += 10;
        m_uIndexCount += 24;
        break;

    case HALF_CCW:
        pIndex[0] = m_uVertexCount;
        pIndex[1] = m_uVertexCount + 1;
        pIndex[2] = m_uVertexCount + 2;
        pIndex[3] = m_uVertexCount;
        pIndex[4] = m_uVertexCount + 2;
        pIndex[5] = m_uVertexCount + 3;
        pIndex[6] = m_uVertexCount;
        pIndex[7] = m_uVertexCount + 3;
        pIndex[8] = m_uVertexCount + 4;
        pIndex[9] = m_uVertexCount;
        pIndex[10] = m_uVertexCount + 4;
        pIndex[11] = m_uVertexCount + 5;
        pIndex[12] = m_uVertexCount + 6;
        pIndex[13] = m_uVertexCount + 7;
        pIndex[14] = m_uVertexCount + 8;
        pIndex[15] = m_uVertexCount + 6;
        pIndex[16] = m_uVertexCount + 8;
        pIndex[17] = m_uVertexCount + 9;
        pIndex[18] = m_uVertexCount + 6;
        pIndex[19] = m_uVertexCount + 9;
        pIndex[20] = m_uVertexCount + 10;
        pIndex[21] = m_uVertexCount + 6;
        pIndex[22] = m_uVertexCount + 10;
        pIndex[23] = m_uVertexCount + 11;
        m_uVertexCount += 12;
        m_uIndexCount += 24;
        break;
    }
}
