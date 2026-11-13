#include "xCounter.h"
#include "xEvent.h"
#include "xFont.h"
#include "xString.h"
#include "zScene.h"

#include <stdio.h>

#ifdef PLATFORM_PC
#include "iCoop.h"
#endif

void xCounterInit()
{
}

inline void xCounterInit(xBase* b, xCounterAsset* asset)
{
    _xCounter* t = (_xCounter*)b;

    xBaseInit(t, asset);

    t->eventFunc = xCounterEventCB;
    t->asset = asset;

    if (t->linkCount)
    {
        t->link = (xLinkAsset*)(t->asset + 1);
    }
    else
    {
        t->link = NULL;
    }

    t->state = 0;
    t->count = asset->count;
}

void xCounterInit(void* b, void* asset)
{
    xCounterInit((xBase*)b, (xCounterAsset*)asset);
}

void xCounterReset(xBase* b)
{
    _xCounter* t = (_xCounter*)b;

    xBaseInit(t, t->asset);

    if (t->linkCount)
    {
        t->link = (xLinkAsset*)(t->asset + 1);
    }
    else
    {
        t->link = 0;
    }

    t->count = t->asset->count;
    t->state = 0;
}

void xCounterSave(_xCounter* ent, xSerial* s)
{
    xBaseSave(ent, s);

    s->Write(ent->state);
    s->Write(ent->count);
}

void xCounterLoad(_xCounter* ent, xSerial* s)
{
    xBaseLoad(ent, s);

    s->Read(&ent->state);
    s->Read(&ent->count);
}

void xCounterEventCB(xBase*, xBase* to, U32 toEvent, const F32* toParam, xBase* toParamWidget, U32)
{
    _xCounter* t = (_xCounter*)to;

#ifdef PLATFORM_PC
    // Online co-op (docs/MULTIPLAYER.md 13): on the guest, a counter a text
    // shows takes the host's count in the host's level. FALSE at once when
    // co-op is off.
    if (iCoopCounterHeld(to, toEvent))
    {
        return;
    }
#endif

    switch (toEvent)
    {
    case eEventDecrement:
        if (t->state != 1)
        {
            t->count--;

            if (t->count <= 0)
            {
                zEntEvent(t, t, eEventExpired);
            }
            else if (t->count > 0 && t->count <= 20)
            {
                zEntEvent(t, t, t->count + (eEventCount1 - 1));
            }
        }
        break;
    case eEventIncrement:
        if (t->state != 1)
        {
            t->count++;

            if (t->count <= 0)
            {
                zEntEvent(t, t, eEventExpired);
            }
            else if (t->count > 0 && t->count <= 20)
            {
                zEntEvent(t, t, t->count + (eEventCount1 - 1));
            }
        }
        break;
    case eEventReset:
        t->state = 0;
        t->count = t->asset->count;
        break;
    case eEventExpired:
        t->count = 0;
        t->state = 1;
        break;
    case eEventSetCount:
        if (t->state != 1)
        {
            if (toParamWidget != NULL && toParamWidget->baseType == eBaseTypeCounter)
            {
                t->count = ((_xCounter*)toParamWidget)->count;
            }
            else
            {
                t->count = toParam[0];
            }
        }
        break;
    default:
        if (t->state != 1)
        {
            if (toEvent >= eEventCount1 && toEvent <= eEventCount20)
            {
                t->count = toEvent - (eEventCount1 - 1);
            }
            else if (toEvent == eEventCount0)
            {
                t->count = 0;
            }
        }
        break;
    }
}

void xCounterParseTag(xtextbox::jot& j, const xtextbox&, const xtextbox&,
                      const xtextbox::split_tag& splitTag);

static const xtextbox::tag_type xCounterTag = { SUBSTR("Counter"), xCounterParseTag, NULL, NULL };

void xCounterStartup()
{
    xtextbox::register_tags(&xCounterTag, 1);
}

void xCounterParseTag(xtextbox::jot& j, const xtextbox&, const xtextbox&,
                      const xtextbox::split_tag& splitTag)
{
    char buffer[64];
    char* c;
    U32 id;
    _xCounter* counter;

    j.reset_flags();
    j.flag.invisible = j.flag.ethereal = true;

    if (splitTag.action.size != 1 || splitTag.action.text[0] != ':' || splitTag.value.size == 0)
    {
        return;
    }

    for (c = (char*)splitTag.value.text;
         c <= splitTag.value.text + splitTag.value.size && c != NULL; c++)
    {
        if (*c == '}')
        {
            break;
        }
    }

    id = xStrHash(splitTag.value.text, c - splitTag.value.text);
    if (id == 0)
    {
        return;
    }

    counter = (_xCounter*)zSceneFindObject(id);
    if (counter == NULL)
    {
        return;
    }

    j.flag.insert = j.flag.dynamic = true;
    sprintf(buffer, "%d", counter->count);
    sprintf((char*)j.context, "%.*s", 31, buffer);
    j.context_size = 32;
}
