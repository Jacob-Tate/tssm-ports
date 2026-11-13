#ifndef ZNMETYPESTDSPINNER_H
#define ZNMETYPESTDSPINNER_H

#include "zNMETypeStandard.h"
#include "zThrown.h"

// Layout from the PS2 DWARF.
struct NMECfgSpinner
{
    struct Health
    {
        S32 pts_healthMax;
    } health;
    struct Battle
    {
        F32 spd_move;
    } battle;
};

// Members are in the original source order; CreateConfig is the key function that emits the
// vtable.
struct zNMESpinner : zNMEStandard
{
    zNMESpinner()
    {
    }
    const NMECfgSpinner* cfg_spinner; // 0x1F8
    zNMEArena arena; // 0x1FC
    xVec3 vel_physics;
    S32 flg_useBigRadius;

    void CreateConfig(const char* name);
    S32 DfltVulnFlags();
    U8 ColChkFlags() const;
    U8 ColPenFlags() const;
    void Init(xEntAsset* entass);
    void Setup();
    void Reset();
    void SelfSetup();
    U32 AnimPick(S32 gid, en_npcgspot gspot, xGoal* goal);
    xAnimTable* CreateAnimTable(xAnimTable* table);
    void BUpdate(xVec3* pos);
    void Process(F32 dt);
    void Type_AddYourMove(F32 dt);
    void CollideReview();
    S32 GoalPick(xGoal* goal, S32& nextgoal, en_trantype& trantype);
    S32 TypeHandleMail(NMEMsg* mail);
    S32 SpinMailDamage(NMEDamageInfo* dmgmail);
    en_vis VisionTarget(xEnt* ent, en_defcon defcon, S32 flags) const;
    F32 HealthRatio();
    static S32 Spin_SeePlyr(xGoal* goal, void* data, en_trantype* trantype, F32 dt, void* ctxt);

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

#endif
