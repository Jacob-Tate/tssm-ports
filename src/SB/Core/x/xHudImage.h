#ifndef XHUDIMAGE_H
#define XHUDIMAGE_H

#include "xHud.h"
#include "xstransvc.h"
#include <rwcore.h>

// Layout from the PS2 DWARF (struct image_widget/image_asset/LayerInfo in xWad2.cpp).

namespace xhud
{
    struct image_asset : asset
    {
        U32 image;

        static const char* type_name()
        {
            return "hud:image";
        }
    };

    struct image_widget : widget
    {
        enum RENDERTYPE
        {
            TOPLEFT,
            BOTTOMLEFT,
            BOTTOMRIGHT,
            TOPRIGHT,
            STROBE_EFFECT,
            OFFSET_CENTERPT,
            ROTATE_CCW,
            OFFSET_ROTATE_CCW,
            VARIABLE_TOP,
            VARIABLE_RIGHT,
            COUNTER_CLOCKWISE,
            HALF_CCW,
            RANDOM_V,
            SPECIAL_STROBE
        };

        enum LAYER_FLAGS
        {
            BLEND = 0x1,
            ADDITIVE = 0x2,
            SET_DEST_ALPHA = 0x4,
            USE_DEST_ALPHA = 0x8,
            BLEND_MASK = 0xf
        };

        struct LayerInfo
        {
            RENDERTYPE eType;
            U32 uFlags;
            RwTexCoords upperLeft;
            RwTexCoords lowerRight;
            F32 fCurPercent;
            F32 fMinPercent;
            F32 fMaxPercent;
            F32 fClampTopPercent;
            F32 fOffsetX;
            F32 fOffsetY;
            F32 fScaleX;
            F32 fScaleY;
            F32 fAlpha;
            F32 fStrobe;
            F32 fStrobeFreq;
            F32 fStrobeHeight;
            S32 nStrobeCount;
        };

        RwRaster* m_pRaster;
        LayerInfo* m_pLayers;
        U32 m_uLayerCount;
        U32 m_uRenderLayerCount;
        // RwSky2DVertex on PS2
        RwIm2DVertex* m_pVertices;
        RwIm2DVertex* m_paVertices;
        U16* m_paIndices;
        U16 m_uVertexCount;
        U16 m_uIndexCount;
        F32 m_fRecipZ;
        U16 m_VertexMax;
        U16 m_IndexMax;

        // No symbol in the target: inlined into load().
        image_widget(const image_asset& a) : widget(a)
        {
            m_pRaster = NULL;
            m_pLayers = NULL;
            m_paVertices = NULL;
            m_paIndices = NULL;
            m_uVertexCount = 0;
            m_uIndexCount = 0;
            m_fRecipZ = 5.0f;
            m_VertexMax = 0;
            m_IndexMax = 0;

            RwTexture* pTexture = (RwTexture*)xSTFindAsset(a.image, NULL);
            m_pRaster = pTexture->raster;
        }

        static void load(xBase& data, xDynAsset& asset, size_t);

        virtual void destroy();
        virtual U32 type() const;
        virtual bool is(U32 id) const;
        virtual void update(F32 dt);
        virtual void render();

        void set_buffer_to_layer(RENDERTYPE eType);
        bool render_random_v(U32 uLayer);
        bool render_special_strobe(U32 uLayer);
        bool render_strobe_effect(U32 uLayer);
        bool render_offset_rotate_ccw(U32 uLayer);
        bool render_rotate_ccw(U32 uLayer);
        bool render_half_ccw(U32 uLayer);
        bool render_counter_clockwise(U32 uLayer);
        bool render_variable_right(U32 uLayer);
        bool render_variable_top(U32 uLayer);
        bool render_one_to_one(U32 uLayer, RENDERTYPE eType);
    };
} // namespace xhud

#endif
