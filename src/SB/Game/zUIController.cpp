#include "zUIController.h"

#include <PowerPC_EABI_Support/MSL_C++/MSL_Common/Include/new>

void zUIController::DoInit()
{
    zUI::DoInit();
    if (linkCount)
    {
        link = (xLinkAsset*)(GetAsset() + 1);
    }
}

void zUIController::DoRender() const
{
}

void zUIController_Init(xBase& data, xDynAsset& asset, size_t)
{
    zUIController_Init((zUIController*)&data, (zUIControllerAsset*)&asset);
}

void zUIController_Init(zUIController* ui, zUIControllerAsset* asset)
{
    new (ui) zUIController;
    zUI_Init(ui, asset);
}
