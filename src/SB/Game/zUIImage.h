#ifndef ZUIIMAGE_H
#define ZUIIMAGE_H

#include "zUI.h"
#include "zGlobals.h"
#include "xTextureManager.h"
#include "xstransvc.h"

struct zUIImageAsset : zUIAsset
{
    U32 image;
    F32 u1;
    F32 v1;
    F32 u2;
    F32 v2;
    F32 u3;
    F32 v3;
    F32 u4;
    F32 v4;
    F32 rotation;
    U16 iflags;
    U8 addressModeU;
    U8 addressModeV;
    xColor_tag color1;
    xColor_tag color2;
    xColor_tag color3;
    xColor_tag color4;
};

struct ImageState
{
    F32 rotation;
    F32 offsetU;
    F32 offsetV;
};

struct zUIImage : zUI
{
    ImageState currentImage;
    ImageState startMovementImage;
    // mutable: the const DoRender fetches the texture (GetTexture/IsMoviePlaying are non-const);
    // like zUIBox::textures, it also keeps retail's current.color loads after the vertex stores.
    mutable xTextureHandle texture;

    zUIImageAsset* GetAsset() const
    {
        return (zUIImageAsset*)zUI::GetAsset();
    }

    // vtable slot order (__vt__8zUIImage); Blends is the key function
    U32 GetSortKey() const
    {
        return ((zUIImageAsset*)asset)->image;
    }
    U8 Blends() const;
    void DoInit();
    void DoReset();
    void DoResetMotion();
    void DoHandleEvent(xBase* from, U32 toEvent, const F32* toParam, xBase* toParamWidget,
                       U32 toParamWidgetID);
    void DoRender() const;
    void DoInitMotion();
    void DoApplyMotionFrame(const zUIMotionFrame* frame);
    void Visible();
    void Invisible();

    // No symbols in the retail DOL: always inlined (also into zUICustomImageSlideshow).
    void PlayMovie(U32 assetID, bool looping, bool freezeframed, U32 startframe, U32 endframe)
    {
        if (globals.demoType != zDT_PUBLICITY)
        {
            texture.PlayMovie(assetID, looping, freezeframed, startframe, endframe);
        }
    }

    void SetTexture(U32 assetID, bool clearPrev)
    {
        if (assetID == 0)
        {
            texture = xTextureHandle();
        }
        else
        {
            bool isMovie = false;
            PKRAssetTOCInfo info;

            if (xSTGetAssetInfo(assetID, &info) && info.typeref->typetag == 'BINK')
            {
                isMovie = true;
            }

            if (isMovie)
            {
                PlayMovie(assetID, true, false, 0, 0);
            }
            else
            {
                texture.Reference(assetID, clearPrev);
            }
        }
    }
};

void zUIImage_Init(zUIImage* ui, zUIImageAsset* asset);
void zUIImage_Init(xBase& data, xDynAsset& asset, size_t);

#endif
