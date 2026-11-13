#include "zUIUserString.h"

#include "xEvent.h"
#include "xMemMgr.h"
#include "xTextAsset.h"

#include <PowerPC_EABI_Support/MSL_C++/MSL_Common/Include/new>
#include <string.h>

namespace
{
    // Defined in zUI.cpp.
    extern void* (*staticAllocator)(U32, void*);
    extern void* staticUser;
} // namespace

void zUIUserString::DoInit()
{
    zUIText::DoInit();
    if (linkCount)
    {
        link = (xLinkAsset*)(GetAsset() + 1);
    }

    U32 size = ((zUIUserStringAsset*)asset)->hardMaxChars + 1;
    textBuffer = (char*)(staticAllocator ? staticAllocator(size, staticUser) :
                                           xMemAlloc(gActiveHeap, size, 0));
    text = textBuffer;
}

void zUIUserString::DoReset()
{
    zUIText::DoReset();
    text = textBuffer;

    const char* str = text;
    maxLength = ((zUIUserStringAsset*)asset)->softMaxChars;
    length = 0;
    textBuffer[0] = '\0';

    if (str)
    {
        strcpy(textBuffer, str);
        length = strlen(str);
    }
}

void zUIUserString::DoHandleEvent(xBase* from, U32 toEvent, const F32* toParam,
                                  xBase* toParamWidget, U32 toParamWidgetID)
{
    U32 len;
    char* asset;
    char ch;

    switch (toEvent)
    {
    case eEventSetText:
        asset = xTextFindString(toParamWidgetID, &len);
        if (asset == NULL)
        {
            textBuffer[0] = '\0';
            length = 0;
            zEntEvent(this, this, eEventUIStringEmpty);
        }
        else
        {
            strcpy(textBuffer, asset + 1);
            length = len;
            if (length >= maxLength)
            {
                zEntEvent(this, this, eEventUIStringEmpty);
            }
        }
        break;
    case eEventUIAddChar:
        ch = (S32)toParam[0];
        if (length < maxLength)
        {
            textBuffer[length++] = ch;
            textBuffer[length] = '\0';
            if (length == maxLength)
            {
                zEntEvent(this, this, eEventUIStringFull);
            }
        }
        break;
    case eEventUIDelChar:
        if (length > 0)
        {
            textBuffer[--length] = '\0';
            if (length == 0)
            {
                zEntEvent(this, this, eEventUIStringEmpty);
            }
        }
        break;
    case eEventUISetMaxChars:
        maxLength = toParam[1];
        if (maxLength > ((zUIUserStringAsset*)this->asset)->hardMaxChars)
        {
            maxLength = ((zUIUserStringAsset*)this->asset)->hardMaxChars;
        }
        if (length > maxLength)
        {
            length = maxLength;
            textBuffer[length] = '\0';
        }
        break;
    case eEventUISendStringAsCheat:
        // Nothing is done with this event in the GameCube build.
        break;
    default:
        zUI::DoHandleEvent(from, toEvent, toParam, toParamWidget, toParamWidgetID);
        break;
    }
}

U32 zUIUserString::GetSortKey() const
{
    return asset->id;
}

void zUIUserString_Init(xBase& data, xDynAsset& asset, size_t)
{
    zUIUserString_Init((zUIUserString*)&data, (zUIUserStringAsset*)&asset);
}

void zUIUserString_Init(zUIUserString* ui, zUIUserStringAsset* asset)
{
    new (ui) zUIUserString;
    zUI_Init(ui, asset);
}
