#include "xEvent.h"

#include "xEnt.h"
#include "xstransvc.h"
#include "zScene.h"

void zEntEvent(char* to, U32 toEvent)
{
    U32 id = xStrHash(to);
    xBase* sendTo = zSceneFindObject(id);

    if (sendTo)
    {
        zEntEvent(sendTo, toEvent);
    }
}

void zEntEvent(U32 toID, U32 toEvent)
{
    xBase* sendTo = zSceneFindObject(toID);

    if (sendTo)
    {
        zEntEvent(sendTo, toEvent);
    }
}

void zEntEvent(U32 toID, U32 toEvent, F32 toParam0, F32 toParam1, F32 toParam2, F32 toParam3)
{
    xBase* sendTo;
    F32 toParam[4];

    toParam[0] = toParam0;
    toParam[1] = toParam1;
    toParam[2] = toParam2;
    toParam[3] = toParam3;

    sendTo = zSceneFindObject(toID);

    if (sendTo)
    {
        zEntEvent(sendTo, toEvent, toParam);
    }
}

void zEntEvent(xBase* from, U32 fromEvent, xBase* to, U32 toEvent, const F32* toParam,
               xBase* toParamWidget, U32 toParamWidgetID, ForceEvent forceEvent)
{
    if (toEvent == eEventDisable)
    {
        xBaseDisable(to);
    }
    else if (toEvent == eEventEnable)
    {
        xBaseEnable(to);
    }

    if (to->eventFunc && (xBaseIsEnabled(to) || forceEvent == FE_YES))
    {
        to->eventFunc(from, to, toEvent, toParam, toParamWidget, toParamWidgetID);

        if (to->baseFlags & 0x20)
        {
            if (toEvent == eEventCollisionOn || toEvent == eEventCollision_Visible_On)
            {
                ((xEnt*)to)->collisionEventReceived = 1;
            }
            else if (toEvent == eEventCollisionOff || toEvent == eEventCollision_Visible_Off)
            {
                ((xEnt*)to)->collisionEventReceived = 2;
            }
        }
    }

    if (xBaseIsEnabled(to) && to->linkCount)
    {
        xLinkAsset* idx = to->link;

        for (S32 i = 0; i < to->linkCount; i++, idx++)
        {
            if (toEvent == idx->srcEvent)
            {
                if (!idx->chkAssetID || (from && idx->chkAssetID == from->id))
                {
                    xBase* sendTo = zSceneFindObject(idx->dstAssetID);

                    if (sendTo)
                    {
                        xBase* b = NULL;

                        if (idx->paramWidgetAssetID)
                        {
                            b = zSceneFindObject(idx->paramWidgetAssetID);

                            if (!b)
                            {
                                b = (xBase*)xSTFindAsset(idx->paramWidgetAssetID, NULL);
                            }
                        }

                        zEntEvent(to, toEvent, sendTo, idx->dstEvent, idx->param, b,
                                  idx->paramWidgetAssetID, FE_NO);
                    }
                }
            }
        }
    }
}
