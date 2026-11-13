#ifndef ZNMETYPESTDFOGGER_H
#define ZNMETYPESTDFOGGER_H

#include "zNMETypeStandard.h"

// Layout from the PS2 DWARF.
struct NMECfgFogger
{
    struct Health
    {
        S32 pts_healthMax;
    } health;
    struct Battle
    {
        F32 tym_predict;
        F32 fac_predlead;
        S32 flg_predturn;
        F32 spd_chase;
        F32 acc_chase;
        F32 ang_turnrate;
    } battle;
    struct Evade
    {
        F32 spd_evade;
        F32 tym_evadeMin;
        F32 tym_evadeMax;
    } evade;
};

// Members are in the original source order; CreateConfig is the key function that emits the
// vtable.
struct zNMEFogger : zNMEStandard
{
    zNMEFogger()
    {
    }
    zNMEArena arena; // 0x1F8
    const NMECfgFogger* cfg_fogger; // 0x218
    xVec3 vel_physics;

    void CreateConfig(const char* name);
    S32 DfltVulnFlags();
    U8 ColChkFlags() const;
    U8 ColPenFlags() const;
    void Init(xEntAsset* entass);
    void Setup();
    void SelfSetup();
    U32 AnimPick(S32 gid, en_npcgspot gspot, xGoal* goal);
    xAnimTable* CreateAnimTable(xAnimTable* table);
    void Reset();
    S32 GoalPick(xGoal* goal, S32& nextgoal, en_trantype& trantype);
    void BUpdate(xVec3* pos);
    void Process(F32 dt);
    void Type_AddYourMove(F32 dt);
    void CollideReview();
    S32 TypeHandleMail(NMEMsg* mail);
    S32 FogMailDamage(NMEDamageInfo* dmgmail);
    en_vis VisionTarget(xEnt* ent, en_defcon defcon, S32 flags) const;
    F32 HealthRatio();
    static S32 Fog_SeePlyr(xGoal* goal, void* data, en_trantype* trantype, F32 dt, void* ctxt);

    // No symbol (always inlined); the name is ours. The time to reach the player at the current
    // speed (at least 2 units/s), capped at tym_max.
    F32 TymToReachPlayer(F32 tym_max)
    {
        F32 dst = xsqrt(XYZDstSqToPlayer(NULL));
        F32 tym = dst / MAX(spd_throttle, 2.0f);
        return MIN(tym, tym_max);
    }

    U8 ColChkByFlags() const
    {
        return XENT_COLLTYPE_NPC | XENT_COLLTYPE_PLYR;
    }
    U8 ColPenByFlags() const
    {
        return XENT_COLLTYPE_NPC | XENT_COLLTYPE_PLYR;
    }
    U8 PhysicsFlags() const
    {
        return 0;
    }
    zNMEArena* Gimme_NMEArena()
    {
        return &arena;
    }
};

#ifdef PLATFORM_PC
// Online co-op (docs/MULTIPLAYER.md 13.1), the guest: a fogger copy the host
// owns posed in its breath attack's loop (`anim`), and its breath shown
// there without harm.
bool zNMEFogger_PcBreathAnim(xEnt* ent, U32 anim);
void zNMEFogger_PcBreathShown(xEnt* ent);
#endif

#endif
