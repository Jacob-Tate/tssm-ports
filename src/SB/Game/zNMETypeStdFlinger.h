#ifndef ZNMETYPESTDFLINGER_H
#define ZNMETYPESTDFLINGER_H

#include "zNMETypeStandard.h"
#include "xAntenna.h"

// Layout from the PS2 DWARF.
struct NMECfgFlinger
{
    struct Health
    {
        S32 pts_healthMax;
    } health;
    struct Ranges
    {
        F32 rad_tooclose;
        F32 rad_lob;
        F32 rad_lobgrey;
        F32 rad_aware;
        F32 rad_awaregrey;
    } ranges;
    struct Battle
    {
        F32 tym_reload;
        F32 spd_move;
        F32 acc_move;
    } battle;
    struct Attack
    {
        en_haztyp haztype;
        F32 spd_proj;
        F32 dst_minRange;
        F32 tym_leadTarget;
        xVec3 vec_offsetLaunch;
    } attack;
};

struct FlingLetMeKnow : xPSYNote
{
    xPsyche* psy_flinger;

    void Notice(en_psynote note, xGoal* goal, void* data);
};

// Members are in the original source order; CreateConfig is the key function that emits the
// vtable.
struct zNMEFlinger : zNMEStandard
{
    zNMEFlinger()
    {
    }
    xPsyche* psy_move; // 0x1F8
    const NMECfgFlinger* cfg_flinger; // 0x1FC
    FlingLetMeKnow tellMeWhen;

    void CreateConfig(const char* name);
    S32 DfltVulnFlags();
    void Init(xEntAsset* asset);
    void BUpdate(xVec3* pos);
    void Setup();
    void SelfSetup();
    void SelfDestroy();
    void FlingMainBrain();
    // No GC or PS2 symbol (inlined into SelfSetup); the name is ours. Builds the move brain
    // that FlingLetMeKnow drives from the main brain's goal changes.
    void FlingMoveBrain()
    {
        xBehaveMgr* bmgr = xBehaveMgr_GetSelf();
        xPsyche* psy;

        psy_move = bmgr->Subscribe(this, 0);
        psy = psy_move;
        psy->BrainBegin();
        psy->AddGoal(NME_GOAL_FLINGER_MOVE, NULL);
        psy->AddGoal(NME_GOAL_NOMANLAND, NULL);
        psy->BrainEnd();
        psy->SetSafety(NME_GOAL_FLINGER_MOVE);

        tellMeWhen.psy_flinger = psy_self;
        psy_self->SetNotify(&tellMeWhen);
    }
    U32 AnimPick(S32 gid, en_npcgspot gspot, xGoal* goal);
    xAnimTable* CreateAnimTable(xAnimTable* table);
    S32 GoalPick(xGoal* goal, S32& nextgoal, en_trantype& trantype);
    void Reset();
    void Process(F32 dt);
    en_vis VisionTarget(xEnt* ent, en_defcon defcon, S32 flags) const;
    F32 HealthRatio();
    S32 TypeHandleMail(NMEMsg* mail);
    S32 FlingMailDamage(NMEDamageInfo* dmgmail);
    void WoundedInput(en_npcgol gid, NMEDamageInfo* dmgmail);

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
};

#endif
