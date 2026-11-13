#ifndef ZLOADINGSCREEN_H
#define ZLOADINGSCREEN_H

#include "zAsync.h"
#include "zUI.h"

struct zLoadingScreen : zAsync::Screen
{
    zUI* controls[16];
    S32 controlCount;
    void* allocations[32];
    S32 allocationCount;

    // Init is the key function that emits __vt__14zLoadingScreen.
    void Init();
    void Update(F32 dt);
    void Render();
    void Exit();

    // No GC symbol: always inlined into CustomSignalHandler.
    void SignalControl(zUI* from, U32 dest, U32 event, const F32* param, xBase* paramWidget,
                       U32 paramWidgetID)
    {
        for (S32 i = 0; i < controlCount; i++)
        {
            if (dest == controls[i]->asset->id)
            {
                controls[i]->HandleEvent(from, event, param, paramWidget, paramWidgetID);
                return;
            }
        }
    }
};

#endif
