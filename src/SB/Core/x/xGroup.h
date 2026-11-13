#ifndef XGROUP_H
#define XGROUP_H

#include "xBase.h"

struct xGroupAsset : xBaseAsset
{
    U16 itemCount;
    U16 groupFlags;
};

struct xGroup : xBase
{
    xGroupAsset* asset;
    xBase** item;
    U32 last_index;
    S32 flg_group;

    U32 get_any();
};

void xGroupInit(void* b, void* asset);
void xGroupSetup(xGroup* g);
void xGroupSave(xGroup* ent, xSerial* s);
void xGroupLoad(xGroup* ent, xSerial* s);
void xGroupEventCB(xBase* to, xBase* from, U32 toEvent, const F32* toParam, xBase* toParamWidget,
                   U32 toParamWidgetID);

inline void xGroupReset(xGroup* ent)
{
    xBaseReset(ent, ent->asset);
}
U32 xGroupGetCount(xGroup* g);
xBase* xGroupGetItemPtr(xGroup* g, U32 index);
U32 xGroupGetItem(xGroup* g, U32 index);

#endif
