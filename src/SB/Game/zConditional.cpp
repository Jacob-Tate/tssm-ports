#include "zConditional.h"
#include "zVar.h"
#include "zScene.h"
#include "xEvent.h"

void zConditionalEventCB(xBase* from, xBase* to, U32 toEvent, const F32* toParam,
                         xBase* toParamWidget, U32 toParamWidgetID);
static inline void zConditionalInit(xBase* b, zCondAsset* asset);

void zConditionalInit(void* b, void* asset)
{
    zConditionalInit((xBase*)b, (zCondAsset*)asset);
}

static inline void zConditionalInit(xBase* b, zCondAsset* asset)
{
    _zConditional* c = (_zConditional*)b;
    S32 i;

    xBaseInit(c, asset);
    c->eventFunc = zConditionalEventCB;
    c->asset = asset;

    if (c->linkCount != 0)
    {
        c->link = (xLinkAsset*)(c->asset + 1);
    }
    else
    {
        c->link = NULL;
    }

    for (i = 0; i < 24; i++)
    {
        if (c->asset->expr1 == zVarEntryTable[i].varNameID)
        {
            c->varEntry = &zVarEntryTable[i];
            break;
        }
    }
}

void zConditionalSave(_zConditional* ent, xSerial* s)
{
    xBaseSave(ent, s);
}

void zConditionalLoad(_zConditional* ent, xSerial* s)
{
    xBaseLoad(ent, s);
}

U32 zConditional_GetCount(_zConditional* c)
{
    zVarEntry* v = NULL;
    void* context = NULL;
    S32 i;
    U32 id;
    U32 temp;

    for (i = 0; i < 24; i++)
    {
        if (c->asset->expr1 == zVarEntryTable[i].varNameID)
        {
            v = &zVarEntryTable[i];

            if (i == 11 || i == 12 || i == 13 || i == 22)
            {
                id = c->asset->value_asset;
                if (id != 0)
                {
                    context = zSceneFindObject(id);
                }
            }
        }
    }

    if (v == NULL)
    {
        return 0;
    }

    temp = v->varCB(context);

    if (temp >= 1 && temp <= 20)
    {
        return eEventCount1 + temp - 1;
    }

    if (temp == 0)
    {
        return eEventCount0;
    }

    return 0;
}

U32 zConditional_Evaluate(_zConditional* c)
{
    U32 temp;
    void* context = NULL;
    U32 id;

    if (c->varEntry == NULL)
    {
        return 0;
    }

    if (c->varEntry->entry == 11 || c->varEntry->entry == 12 || c->varEntry->entry == 13 ||
        c->varEntry->entry == 22)
    {
        id = c->asset->value_asset;
        if (id != 0)
        {
            context = zSceneFindObject(id);
        }
    }

    temp = c->varEntry->varCB(context);

    switch (c->asset->op)
    {
    case CONDITION_EQ:
        if (temp == c->asset->constNum)
        {
            return 1;
        }
        return 0;
    case CONDITION_GT:
        if (temp > c->asset->constNum)
        {
            return 1;
        }
        return 0;
    case CONDITION_LT:
        if (temp < c->asset->constNum)
        {
            return 1;
        }
        return 0;
    case CONDITION_GE:
        if (temp >= c->asset->constNum)
        {
            return 1;
        }
        return 0;
    case CONDITION_LE:
        if (temp <= c->asset->constNum)
        {
            return 1;
        }
        return 0;
    case CONDITION_NE:
        if (temp != c->asset->constNum)
        {
            return 1;
        }
        return 0;
    }

    return 0;
}

void zConditionalEventCB(xBase* from, xBase* to, U32 toEvent, const F32* toParam,
                         xBase* toParamWidget, U32 toParamWidgetID)
{
    _zConditional* t = (_zConditional*)to;
    U32 event;

    switch (toEvent)
    {
    case eEventEvaluate:
        if (zConditional_Evaluate(t))
        {
            zEntEvent(t, t, eEventTrue);
        }
        else
        {
            zEntEvent(t, t, eEventFalse);
        }
        break;

    case eEvaluateCounterValue:
        event = zConditional_GetCount(t);
        if (event != 0)
        {
            zEntEvent(t, t, event);
        }
        break;

    case eEventTrue:
    case eEventFalse:
        break;

    case eEventReset:
        xBaseReset(t, t->asset);
        break;
    }
}
