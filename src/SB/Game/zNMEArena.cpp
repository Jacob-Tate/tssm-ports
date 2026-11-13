#include "zNMECommon.h"
#include "zMovePoint.h"
#include "xMath.h"
#include "xMath3.h"
#include "xDebug.h"

S32 zNMEArena::Cycle(zNMECommon* npc, S32 peek)
{
    zMovePoint* nav_near;

    pos_arena = g_O3;
    rad_arena = -1.0f;
    flg_arena = 0;
    nav_arena = NULL;
    nav_refer_dest = npc->navnet->nav_dest;
    nav_refer_curr = npc->navnet->nav_curr;

    if (!peek && npc->navnet->nav_curr != NULL && npc->navnet->nav_curr->RadiusArena() > 1.0f)
    {
        nav_arena = npc->navnet->nav_curr;
        SyncHomeFromNav();
    }
    else if (!peek && npc->navnet->nav_curr != NULL &&
             (nav_near = NextBestNav(npc, npc->navnet->nav_curr)) != NULL)
    {
        nav_arena = nav_near;
        SyncHomeFromNav();
    }
    else if (peek && npc->navnet->nav_dest != NULL && npc->navnet->nav_dest->RadiusArena() > 1.0f)
    {
        nav_arena = npc->navnet->nav_dest;
        SyncHomeFromNav();
    }
    else if (peek && npc->navnet->nav_dest != NULL &&
             (nav_near = NextBestNav(npc, npc->navnet->nav_dest)) != NULL)
    {
        nav_arena = nav_near;
        SyncHomeFromNav();
    }

    if (peek && !IsReady())
    {
        Cycle(npc, 0);
    }

    if (peek)
    {
        flg_arena |= 4;
    }

    return IsReady();
}

// The asserts are compiled-out placeholders (conditions unknown); 5 of them keep GC/2.6 from
// auto-inlining this (>= 30 statements), as retail calls it from Cycle.
zMovePoint* zNMEArena::NextBestNav(zNMECommon* npc, zMovePoint* nav_from)
{
    xASSERT_PLACEHOLDER();
    xASSERT_PLACEHOLDER();

    F32 rad_best = -1.0f;
    zMovePoint* nav_best = NULL;
    S32 i;
    // not in the DWARF: the node count is read once before the loop.
    S32 cnt_node = nav_from->NumNodes();

    for (i = 0; i < cnt_node; i++)
    {
        zMovePoint* node = nav_from->NodeByIndex(i);
        if (node == NULL)
        {
            continue;
        }

        xASSERT_PLACEHOLDER();

        F32 rad_node = node->RadiusArena();
        if (rad_node < 1.0f)
        {
            continue;
        }

        xASSERT_PLACEHOLDER();

        xVec3 delt;
        xVec3Sub(&delt, nav_from->PosGet(), node->PosGet());
        if (xabs(delt.y) > 5.0f)
        {
            continue;
        }

        delt.y = 0.0f;
        if (xVec3Length2(&delt) > SQR(rad_node))
        {
            continue;
        }

        if (rad_node > rad_best)
        {
            rad_best = rad_node;
            nav_best = node;
        }
        else
        {
            nav_best = node;
            break;
        }
    }

    xASSERT_PLACEHOLDER();

    return nav_best;
}
