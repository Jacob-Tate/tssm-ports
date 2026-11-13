#ifndef ZUICONTROLLER_H
#define ZUICONTROLLER_H

#include "zUI.h"

struct zUIControllerAsset : zUIAsset
{
};

struct zUIController : zUI
{
    // DoInit is the key function that emits __vt__13zUIController.
    void DoInit();
    void DoRender() const;
    zUIControllerAsset* GetAsset() const
    {
        return (zUIControllerAsset*)zUI::GetAsset();
    }

    U32 GetSortKey() const
    {
        return asset->id;
    }
};

void zUIController_Init(zUIController* ui, zUIControllerAsset* asset);
void zUIController_Init(xBase& data, xDynAsset& asset, size_t);

#endif
