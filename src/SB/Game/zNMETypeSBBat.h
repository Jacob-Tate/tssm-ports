#ifndef ZNMETYPESBBAT_H
#define ZNMETYPESBBAT_H

#include "zNMECommon.h"
#include "zNMEGoalStd.h"

struct zNMEDennis;

void CreateDennisAndSBBatAnimTable(xAnimTable* table);

struct zNMESBBat : zNMECommon
{
    zNMESBBat()
    {
    }
    zNMEDennis* npc_bossman;

    // No GC symbol (always inlined into zNMEGoalSBBatIdle::Process); the name is not known. Rides
    // on the boss: same matrix and animation time.
    inline void FollowTheBoss();

    // No GC symbol (always inlined into the Dennis goals); the name is not known. Starts the bat's
    // animation for the boss goal's gspot.
    void MimicBossAnim(xGoal* goal, en_npcgspot gspot)
    {
        AnimStart(AnimPick(goal->GetID(), gspot, goal), 0);
    }

    // Source order (the PS2 DWARF lists them reversed); CreateAnimTable is the key function.
    xAnimTable* CreateAnimTable(xAnimTable* table);
    U32 AnimPick(S32 gid, en_npcgspot gspot, xGoal* goal);
    void SelfSetup();
    void Init(xEntAsset* asset);
    void Setup();
    void Process(F32 dt);
    void Reset();

    U8 ColChkFlags() const
    {
        return 0;
    }
    U8 ColPenFlags() const
    {
        return 0;
    }
    U8 ColChkByFlags() const
    {
        return 0;
    }
    U8 ColPenByFlags() const
    {
        return 0;
    }
    U8 PhysicsFlags() const
    {
        return 0;
    }
};

// ---- zNMETypeSBBat.h goals ----

struct zNMEGoalSBBatIdle : zNMEGoalCommon
{
    zNMEGoalSBBatIdle()
    {
    }

    S32 Process(en_trantype* trantype, F32 dt, void* updCtxt);
};

struct zNMEGoalSBBatDead : zNMEGoalDead
{
    zNMEGoalSBBatDead()
    {
    }

    S32 Enter(F32 dt, void* updCtxt);
};

#endif
