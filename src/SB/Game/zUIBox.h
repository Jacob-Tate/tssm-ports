#ifndef ZUIBOX_H
#define ZUIBOX_H

#include "zUI.h"
#include "xTextureManager.h"

struct Part
{
    U32 image;
    xColor_tag color;
    F32 u1;
    F32 v1;
    F32 u2;
    F32 v2;
    F32 u3;
    F32 v3;
    F32 u4;
    F32 v4;
    S32 rotation;
    // GC only (not in the PS2 DWARF); name from Ratatouille.
    U8 enabled;
    U8 pad[3];
};

struct zUIBoxAsset : zUIAsset
{
    Part parts[9];
    F32 borderWidth;
    F32 borderHeight;
    F32 widthPerUV;
    F32 heightPerUV;
    F32 centerWidthPerUV;
    F32 centerHeightPerUV;
    U8 scaleHSide;
    U8 scaleVSide;
    U8 scaleCenter;
    U8 stretchUVsOnMotionScale;
    U8 forceAlphaWrite;
};

struct zUIBox : zUI
{
    // mutable: the const RenderPart fetches the texture (GetTexture is non-const). It also makes
    // CW treat the other members as aliased, which keeps retail's color loads after the stores.
    mutable xTextureHandle textures[9];
    F32 curRotation;

    // Member functions follow the original source order (the PS2 DWARF lists them reversed);
    // DoInit is the key function that emits __vt__6zUIBox.
    void DoInit();
    void DoReset();
    void DoResetMotion();
    void DoHandleEvent(xBase* from, U32 toEvent, const F32* toParam, xBase* toParamWidget,
                       U32 toParamWidgetID);
    void DoApplyMotionFrame(const zUIMotionFrame* frame);
    void DoRender() const;
    void RenderPart(S32 p, F32 x1, F32 y1, F32 x2, F32 y2, F32 uScale, F32 vScale, F32 rotation,
                    F32 xPivot, F32 yPivot) const;
    zUIBoxAsset* GetAsset() const
    {
        return (zUIBoxAsset*)zUI::GetAsset();
    }

    U32 GetSortKey() const
    {
        return asset->id;
    }

    // No symbol (always inlined): the PS2 DWARF of DoReset has no locals for this loop, and
    // retail's registers match it being an inlined helper.
    void ResetTextures()
    {
        for (S32 i = 0; i < 9; i++)
        {
            textures[i].Reference(((zUIBoxAsset*)asset)->parts[i].image, false);
        }
    }
};

void zUIBox_Init(zUIBox* ui, zUIBoxAsset* asset);
void zUIBox_Init(xBase& data, xDynAsset& asset, size_t);

#endif
