#include "zFXRibbonPool.h"

#include "xFX.h"
#include "xMemMgr.h"

#include <float.h>
#include <string.h>

namespace
{
    U32 ribbonCount;
    xFXRibbon* ribbonPool;
    U8* ribbonInUse;
} // namespace

void zFXRibbonPoolInit(U32 maxNumRibbons)
{
    U32 i;
    xFXRibbon::config_type* cfg;

    ribbonCount = maxNumRibbons;
    if (ribbonCount == 0)
    {
        return;
    }

    ribbonPool = (xFXRibbon*)xMemAlloc(gActiveHeap, ribbonCount * sizeof(xFXRibbon), 0);
    ribbonInUse = (U8*)xMemAlloc(gActiveHeap, ribbonCount, 0);

    for (i = 0; i < ribbonCount; i++)
    {
        ribbonPool[i].create(1);

        cfg = new (gActiveHeap) xFXRibbon::config_type;
        xFXRibbon::load_default_config(*cfg);

        ribbonPool[i].set_config(cfg);
    }
}

xFXRibbon* zFXRibbonPoolNew()
{
    U32 index = (U32)-1;

    for (U32 i = 0; i < ribbonCount; i++)
    {
        if (ribbonPool[i].size() == 0 && !ribbonInUse[i])
        {
            index = i;
            break;
        }
    }

    if (index == (U32)-1)
    {
        F32 remainingTime = FLT_MAX;

        for (U32 i = 0; i < ribbonCount; i++)
        {
            if (!ribbonInUse[i])
            {
                F32 remain = ribbonPool[i].time_remaining();
                if (remain < remainingTime)
                {
                    remainingTime = remain;
                    index = i;
                }
            }
        }
    }

    if (index != (U32)-1)
    {
        ribbonPool[index].clear();
        ribbonInUse[index] = true;
        return &ribbonPool[index];
    }

    return NULL;
}

void zFXRibbonPoolDelete(xFXRibbon*& ribbon)
{
    if (ribbon != NULL)
    {
        ribbonInUse[ribbon - ribbonPool] = false;
        ribbon = NULL;
    }
}

void zFXRibbonPool_SceneReset()
{
    if (ribbonCount >= 1)
    {
        memset(ribbonInUse, 0, ribbonCount);
    }
}
