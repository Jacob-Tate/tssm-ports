#ifndef ZUIUSERSTRING_H
#define ZUIUSERSTRING_H

#include "zUIText.h"

struct zUIUserStringAsset : zUITextAsset
{
    U8 hardMaxChars;
    U8 softMaxChars;
};

struct zUIUserString : zUIText
{
    char* textBuffer;
    S32 length;
    S32 maxLength;

    zUIUserStringAsset* GetAsset() const
    {
        return (zUIUserStringAsset*)zUIText::GetAsset();
    }

    U32 GetSortKey() const;
    void DoHandleEvent(xBase* from, U32 toEvent, const F32* toParam, xBase* toParamWidget,
                       U32 toParamWidgetID);
    void DoReset();
    void DoInit();
};

void zUIUserString_Init(zUIUserString* ui, zUIUserStringAsset* asset);
void zUIUserString_Init(xBase& data, xDynAsset& asset, size_t);

#endif
