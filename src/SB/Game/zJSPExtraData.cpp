#include "zJSPExtraData.h"

#include "xEvent.h"
#include "zScene.h"

jspGroupPair zJSPExtraData::sJspGroupPairs[10];
S32 zJSPExtraData::sNumPairs;

static inline void AddGroupPair(U32 jspID, U32 groupID, xBase* widget)
{
    zJSPExtraData::sJspGroupPairs[zJSPExtraData::sNumPairs].jspID = jspID;
    zJSPExtraData::sJspGroupPairs[zJSPExtraData::sNumPairs].groupID = groupID;
    zJSPExtraData::sJspGroupPairs[zJSPExtraData::sNumPairs].pGroup = NULL;
    zJSPExtraData::sJspGroupPairs[zJSPExtraData::sNumPairs].pWidget = widget;
    zJSPExtraData::sNumPairs++;
}

void zJSPExtraData::ShowAllAssociatedGroups(bool show)
{
    for (S32 i = 0; i < sNumPairs; i++)
    {
        sJspGroupPairs[i].pGroup = zSceneFindObject(sJspGroupPairs[i].groupID);
        if (sJspGroupPairs[i].pGroup != NULL)
        {
            if (show)
            {
                zEntEvent(sJspGroupPairs[i].pGroup, eEventVisibilityCullOff);
            }
            else
            {
                zEntEvent(sJspGroupPairs[i].pGroup, eEventVisibilityCullOn);
            }
        }
    }
}

void zJSPExtraData::ShowAssociatedGroup(U32 jspAssetId, bool show)
{
    for (S32 i = 0; i < sNumPairs; i++)
    {
        if (sJspGroupPairs[i].jspID == jspAssetId)
        {
            if (show)
            {
                if (sJspGroupPairs[i].pGroup == NULL && sJspGroupPairs[i].groupID != 0)
                {
                    sJspGroupPairs[i].pGroup = zSceneFindObject(sJspGroupPairs[i].groupID);
                }
                if (sJspGroupPairs[i].pGroup != NULL)
                {
                    zEntEvent(sJspGroupPairs[i].pGroup, eEventVisibilityCullOff);
                }
                zEntEvent(sJspGroupPairs[i].pWidget, eEventOn);
            }
            else
            {
                if (sJspGroupPairs[i].pGroup == NULL && sJspGroupPairs[i].groupID != 0)
                {
                    sJspGroupPairs[i].pGroup = zSceneFindObject(sJspGroupPairs[i].groupID);
                }
                if (sJspGroupPairs[i].pGroup != NULL)
                {
                    zEntEvent(sJspGroupPairs[i].pGroup, eEventVisibilityCullOn);
                }
                zEntEvent(sJspGroupPairs[i].pWidget, eEventOff);
            }
        }
    }
}

void zJSPExtraData::ClearGroupAssociations()
{
    sNumPairs = 0;
}

void zJSPExtraData::Load(xBase& data, xDynAsset& asset, size_t)
{
    xBaseInit(&data, &asset);
    if (data.linkCount != 0)
    {
        data.link = (xLinkAsset*)((zJSPExtraDataAsset*)&asset + 1);
    }
    else
    {
        data.link = NULL;
    }

    AddGroupPair(((zJSPExtraDataAsset&)asset).jspID, ((zJSPExtraDataAsset&)asset).groupID, &data);
}
