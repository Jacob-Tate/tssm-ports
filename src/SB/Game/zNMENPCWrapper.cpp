#include "zNMENPCWrapper.h"

void NME_entwrap_setup(xEnt* ent)
{
    zNMENPCWrapper* npc = (zNMENPCWrapper*)ent;

    npc->Setup();
    npc->Reset();
}

void NME_entwrap_reset(xEnt* ent)
{
    ((zNMENPCWrapper*)ent)->Reset();
}

void NME_entwrap_update(xEnt* ent, xScene* xscn, F32 dt)
{
    zNMENPCWrapper* npc = (zNMENPCWrapper*)ent;
    npc->UpdateWrapper(dt);
}

void NME_entwrap_bupdate(xEnt* ent, xVec3* pos)
{
    ((zNMENPCWrapper*)ent)->BUpdate(pos);
}

void NME_entwrap_move(xEnt* ent, xScene* xscn, F32 dt, xEntFrame* frame)
{
    ((zNMENPCWrapper*)ent)->Move(dt);
}

void NME_entwrap_event(xBase* from, xBase* to, U32 toEvent, const F32* toParam,
                       xBase* toParamWidget, U32 toParamWidgetID)
{
    S32 used = 0;
    ((zNMENPCWrapper*)to)
        ->SysEvent(from, to, toEvent, toParam, toParamWidget, toParamWidgetID, &used);
}

void NME_entwrap_render(xEnt* ent)
{
    ((zNMENPCWrapper*)ent)->Render();
}
