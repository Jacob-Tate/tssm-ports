#include "xGroup.h"

#include "xEvent.h"
#include "xRand.h"

inline void xGroupInit(xBase* b, xGroupAsset* asset)
{
    xGroup* t = (xGroup*)b;

    xBaseInit(b, asset);

    b->eventFunc = xGroupEventCB;
    t->asset = asset;

    if (b->linkCount)
    {
        b->link =
            (xLinkAsset*)((U8*)t->asset + sizeof(xGroupAsset) + asset->itemCount * sizeof(U32));
    }
    else
    {
        b->link = NULL;
    }

    U32 numItems = xGroupGetCount(t);
    xBase** item;

    if (numItems != 0)
    {
        item = (xBase**)xMemAllocSize(numItems * sizeof(xBase*));
    }
    else
    {
        item = NULL;
    }

    t->item = item;
    t->last_index = 0;
    t->flg_group = 0;
}

void xGroupInit(void* b, void* asset)
{
    xGroupInit((xBase*)b, (xGroupAsset*)asset);
}

void xGroupSetup(xGroup* g)
{
    if ((g->flg_group & 1) == 0)
    {
        U32 count = xGroupGetCount(g);
        for (U32 i = 0; i < count; i++)
        {
            U32 id = xGroupGetItem(g, i);
            g->item[i] = zSceneFindObject(id);
            if (g->item[i] != NULL)
            {
                g->flg_group |= 2;
            }
            else
            {
                g->item[i] = (xBase*)xSTFindAsset(id, NULL);
                if (g->item[i] != NULL)
                {
                    g->flg_group |= 4;
                }
            }
        }
        g->flg_group |= 1;
    }
}

void xGroupSave(xGroup* ent, xSerial* s)
{
    xBaseSave((xBase*)ent, s);
}

void xGroupLoad(xGroup* ent, xSerial* s)
{
    xBaseLoad((xBase*)ent, s);
}

void xGroupEventCB(xBase* to, xBase* from, U32 toEvent, const F32* toParam, xBase* toParamWidget,
                   U32 toParamWidgetID)
{
    xGroup* g = (xGroup*)from;

    switch (toEvent)
    {
    case eEventReset:
        xGroupReset(g);
        break;
    case eEventDisableGroupContents:
        toEvent = eEventDisable;
        break;
    }

    S32 rand = -1;
    if (g->asset->groupFlags & 1)
    {
        rand = xrand_RandomChoice(g->asset->itemCount);
    }

    switch (toEvent)
    {
    case eEventFastVisible:
    case eEventFastInvisible:
    case eEventVisibilityCullOn:
    case eEventVisibilityCullOff:
    {
        void (*entVisEventHandler)(xEnt*);

        switch (toEvent)
        {
        case eEventFastVisible:
            entVisEventHandler = xEntShow;
            break;
        case eEventFastInvisible:
            entVisEventHandler = xEntHide;
            break;
        case eEventVisibilityCullOn:
            entVisEventHandler = xEntVisibilityCullOn;
            break;
        case eEventVisibilityCullOff:
            entVisEventHandler = xEntVisibilityCullOff;
            break;
        }

        for (S32 i = 0; i < g->asset->itemCount; i++)
        {
            if (rand == -1 || rand == i)
            {
                xBase* b = g->item[i];
                if (b)
                {
                    if (b->baseFlags & 0x20)
                    {
                        if (xBaseIsEnabled(b))
                        {
                            entVisEventHandler((xEnt*)b);
                        }
                    }
                    else
                    {
                        zEntEvent(NULL, 0, b, toEvent, toParam, toParamWidget, toParamWidgetID,
                                  FE_NO);
                    }
                }
            }
        }
        break;
    }
    default:
        for (S32 i = 0; i < g->asset->itemCount; i++)
        {
            if (rand == -1 || rand == i)
            {
                xBase* b = g->item[i];
                if (b)
                {
                    zEntEvent(NULL, 0, b, toEvent, toParam, toParamWidget, toParamWidgetID, FE_NO);
                }
            }
        }
        break;
    }
}

U32 xGroupGetCount(xGroup* g)
{
    return g->asset->itemCount;
}

xBase* xGroupGetItemPtr(xGroup* g, U32 index)
{
    if ((g->flg_group & 1) == 0)
    {
        xGroupSetup(g);
    }
    return g->item != NULL ? g->item[index] : NULL;
}

U32 xGroupGetItem(xGroup* g, U32 index)
{
    return (U32)((U32*)(g->asset + 1))[index];
}

U32 xGroup::get_any()
{
    if (asset->itemCount == 0)
    {
        return NULL;
    }

    U32 id = ((U32*)(asset + 1))[last_index];
    last_index = (last_index + 1) % asset->itemCount;
    return id;
}
