#ifndef ZUIMODEL_H
#define ZUIMODEL_H

#include "zUI.h"
#include "xModel.h"
#include "xAnim.h"
#include "xSurface.h"

struct zUIModelAsset : zUIAsset
{
    U32 model;
    U32 animationList;
    U32 surface;
};

struct zUIModel : zUI
{
    xModelInstance* model;
    xAnimTable* atbl;
    xSurface* surf;

    // Member functions follow the original source order (the PS2 DWARF lists them reversed);
    // DoInit is the key function that emits __vt__8zUIModel.
    void DoInit();
    void InitModel();
    void DoReset();
    void DoRender() const;
    void DoUpdate(F32 dt, U32 buttonsPressed);
    void DoExit();
    void DoHandleEvent(xBase* from, U32 toEvent, const F32* toParam, xBase* toParamWidget,
                       U32 toParamWidgetID);
    zUIModelAsset* GetAsset() const
    {
        return (zUIModelAsset*)zUI::GetAsset();
    }

    U32 GetSortKey() const
    {
        return ((zUIModelAsset*)asset)->model;
    }
};

void zUIModel_Init(zUIModel* ui, zUIModelAsset* asset);
void zUIModel_Init(xBase& data, xDynAsset& asset, size_t);

#endif
