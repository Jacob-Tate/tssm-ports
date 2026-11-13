#ifndef ZNMETYPESTDSLAMMER_H
#define ZNMETYPESTDSLAMMER_H

#include "zNMETypeStandard.h"
#include "xAntenna.h"
#include "zThrown.h"

// Layout from the PS2 DWARF.
struct NMECfgSlammer
{
    struct Health
    {
        S32 pts_healthMax;
    } health;
    struct Chase
    {
        F32 spd_chase;
        F32 acc_chase;
        F32 spd_turnRate;
    } chase;
    struct Wedge
    {
        F32 rad_wedge;
        F32 hyt_wedge;
        F32 dot_wedge;
    } wedge;
    struct Predict
    {
        F32 tym_leadMax;
        F32 fac_lead;
        S32 flg_useTurn;
    } predict;
    struct LegacyBattle
    {
        F32 spd_moveSlow;
        F32 spd_moveFast;
        F32 acc_move;
        F32 spd_turnRate;
        F32 rad_plyrNear;
        F32 rad_threshold;
        F32 hyt_threshold;
    } legacy_battle;
};

// Members are in the original source order; CreateConfig is the key function that emits the
// vtable.
struct zNMESlammer : zNMEStandard
{
    zNMESlammer()
    {
    }
    const NMECfgSlammer* cfg_slammer; // 0x1F8
    zNMEArena arena; // 0x1FC
    xVec3 vel_physics;

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
    S32 SlamMailDamage(NMEDamageInfo* dmgmail);
    en_vis VisionTarget(xEnt* ent, en_defcon defcon, S32 flags) const;
    F32 HealthRatio();
    void StraddleClamp(xVec3* pos_pred);
    static S32 Slam_SeePlyr(xGoal* goal, void* data, en_trantype* trantype, F32 dt, void* ctxt);

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
