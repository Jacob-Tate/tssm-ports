#ifndef ZJSPEXTRADATA_H
#define ZJSPEXTRADATA_H

#include "xBase.h"
#include "xDynAsset.h"

struct zJSPExtraDataAsset : xDynAsset
{
    U32 jspID;
    U32 groupID;
};

struct jspGroupPair
{
    U32 jspID;
    U32 groupID;
    xBase* pGroup;
    xBase* pWidget;
};

struct zJSPExtraData : xBase
{
    static jspGroupPair sJspGroupPairs[10];
    static S32 sNumPairs;

    static void Load(xBase& data, xDynAsset& asset, size_t);
    static void ClearGroupAssociations();
    static void ShowAssociatedGroup(U32 jspAssetId, bool show);
    static void ShowAllAssociatedGroups(bool show);
};

#endif
