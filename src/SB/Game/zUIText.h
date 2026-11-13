#ifndef ZUITEXT_H
#define ZUITEXT_H

#include "zUI.h"

struct zUITextAsset : zUIAsset
{
    U32 textID;
    U8 font;
    U8 fontSizeW;
    U8 fontSizeH;
    U8 fontSpacingX;
    U8 fontSpacingY;
    U8 textBoxInsetTop;
    U8 textBoxInsetLeft;
    U8 textBoxInsetRight;
    U8 textBoxInsetBottom;
    U8 justifyX;
    U8 justifyY;
    U8 textFlags;
    xColor_tag shadowColor;
    F32 shadowOffsetX;
    F32 shadowOffsetY;
    F32 textScaleX;
    F32 textScaleY;
};

struct zUIText : zUI
{
    xColor_tag shadowColor;
    mutable char* text; // DoRender() const caches the looked-up string here

    // Member functions follow the original source order (the PS2 DWARF lists them reversed);
    // DoInit is the key function that emits __vt__7zUIText.
    void DoInit();
    void DoReset();
    void DoRender() const;
    void DoHandleEvent(xBase* from, U32 toEvent, const F32* toParam, xBase* toParamWidget,
                       U32 toParamWidgetID);
    void DoApplyMotionFrame(const zUIMotionFrame* frame);
    void setup_textbox(xtextbox& tb, const zUITextAsset* asset) const;
    zUITextAsset* GetAsset() const
    {
        return (zUITextAsset*)zUI::GetAsset();
    }

    U32 GetSortKey() const
    {
        return ((zUITextAsset*)asset)->textID;
    }
    U8 IsUIText()
    {
        return 1;
    }
};

void zUIText_Init(zUIText* ui, zUITextAsset* asset);
void zUIText_Init(xBase& data, xDynAsset& asset, size_t);
void zUITextInitSystem();

#endif
