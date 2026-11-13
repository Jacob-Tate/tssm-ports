#include "zScript.h"

#include "xEvent.h"
#include "xstransvc.h"
#include "zScene.h"

#include <types.h>

namespace
{
    U8 gSceneReset;
}

static U8 zScriptExecuteEvents(_zScript* t, F32 start, F32 end);

inline void zScriptInit(xBase* b, xScriptAsset* tasset)
{
    _zScript* t = (_zScript*)b;

    xBaseInit(t, tasset);
    t->eventFunc = zScriptEventCB;
    t->tasset = tasset;

    if (t->linkCount)
    {
        // Links follow the script's event list in the asset
        // (xLinkAsset and xScriptEventAsset are both 0x20 bytes).
        t->link = (xLinkAsset*)(t->tasset + 1);
        t->link += tasset->eventCount;
    }
    else
    {
        t->link = NULL;
    }

    zScriptReset(t);
}

void zScriptInit(void* b, void* tasset)
{
    zScriptInit((xBase*)b, (xScriptAsset*)tasset);
}

void zScriptReset(_zScript* t)
{
    xBaseReset(t, t->tasset);
    t->time = 0.0f;
    t->current = (xScriptEventAsset*)(t->tasset + 1);
    t->state = ZSCRIPT_STATE_READY;
    t->scaleFactor = t->tasset->scaleFactor;
}

void zScriptEventCB(xBase*, xBase* to, U32 toEvent, const F32* toParam, xBase*, U32)
{
    _zScript* t = (_zScript*)to;

    switch (toEvent)
    {
    case eEventRun:
        t->state = ZSCRIPT_STATE_RUNNING;
        if (t->time == 0.0f)
        {
            zScriptExecuteEvents(t, 0.0f, 1e-9f);
            if (t->state == ZSCRIPT_STATE_RUNNING)
            {
                t->time = 1e-9f;
            }
        }
        break;
    case eEventStop:
        if (t->state == ZSCRIPT_STATE_RUNNING)
        {
            t->state = ZSCRIPT_STATE_READY;
        }
        break;
    case eEventTimerSet:
        if (toParam)
        {
            t->time = toParam[0];
            t->current = (xScriptEventAsset*)(t->tasset + 1);
        }
        break;
    case eEventExpired:
        zScriptReset(t);
        if (t->tasset->loop)
        {
            t->state = ZSCRIPT_STATE_RUNNING;
        }
        break;
    case eEventScriptReset:
        zScriptReset(t);
        break;
    case eEventReset:
        zScriptReset(t);
        break;
    case eEventSceneReset:
        gSceneReset = TRUE;
        break;
    case eEventSetScaleFactor:
        if (toParam)
        {
            t->scaleFactor = toParam[0];
        }
        break;
    }
}

static U8 zScriptExecuteEvents(_zScript* t, F32 start, F32 end)
{
    xScriptEventAsset* endEvent;
    xBase* paramWidget;
    xScriptEventAsset* curr;

    gSceneReset = FALSE;

    endEvent = (xScriptEventAsset*)(t->tasset + 1);
    endEvent += t->tasset->eventCount;

    while (t->current != endEvent)
    {
        curr = t->current;

        if (curr->time >= start)
        {
            if (curr->time < end)
            {
                if (curr->paramEvent != eEventScriptNoop)
                {
                    xBase* widget = zSceneFindObject(curr->widget);
                    if (widget)
                    {
                        // not in the DWARF: the widget id is tested and used for two lookups.
                        U32 paramWidgetID = curr->paramWidget;
                        paramWidget = NULL;
                        if (paramWidgetID)
                        {
                            paramWidget = zSceneFindObject(paramWidgetID);
                            if (!paramWidget)
                            {
                                paramWidget = (xBase*)xSTFindAsset(paramWidgetID, NULL);
                            }
                        }

                        if (widget != t)
                        {
                            zEntEvent(t, widget, t->current->paramEvent, t->current->param,
                                      paramWidget, t->current->paramWidget);
                        }
                        else
                        {
                            // The script sends to itself (e.g. a reset): step past this event first.
                            curr = t->current;
                            t->current++;
                            zEntEvent(t, t, curr->paramEvent, curr->param, paramWidget,
                                      curr->paramWidget);
                            return TRUE;
                        }
                    }
                }

                if (gSceneReset)
                {
                    return TRUE;
                }
            }
            else
            {
                return FALSE;
            }
        }

        t->current++;
    }

    zEntEvent(t, eEventExpired);
    return TRUE;
}

// No GC symbol: inlined into zSceneUpdate, the out-of-line copy dead-stripped.
void zScriptUpdate(xBase* to, xScene* sc, F32 dt)
{
    _zScript* t = (_zScript*)to;

    if (t->state == ZSCRIPT_STATE_RUNNING && !zScriptExecuteEvents(t, t->time, t->time + dt))
    {
        t->time += dt / t->scaleFactor;
    }
}
