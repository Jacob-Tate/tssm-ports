#ifndef ZNMETYPESTDMERVYN_H
#define ZNMETYPESTDMERVYN_H

#include "zNMETypeStandard.h"
#include "zNMEZapGun.h"

// Layout from the PS2 DWARF.
struct NMECfgMervyn
{
    struct Health
    {
        S32 pts_healthMax;
    } health;
    struct Ranges
    {
        F32 rad_tooclose;
        F32 rad_zap;
        F32 rad_zapgrey;
        F32 rad_lob;
        F32 rad_lobgrey;
        F32 rad_aware;
        F32 rad_awaregrey;
    } ranges;
    struct Battle
    {
        F32 spd_move;
        F32 acc_move;
        F32 spd_turn;
    } battle;
    struct Proj
    {
        en_haztyp haztype;
        F32 spd_proj;
    } proj;
};

struct zDustRingEmitter;

struct MervLetMeKnow : xPSYNote
{
    xPsyche* psy_mervyn;

    void Notice(en_psynote note, xGoal* goal, void* data);
};

// Members are in the original source order; CreateConfig is the key function that emits the
// vtable.
struct zNMEMervyn : zNMEStandard
{
    zNMEMervyn()
    {
    }
    xPsyche* psy_move; // 0x1F8
    const NMECfgMervyn* cfg_mervyn; // 0x1FC
    ZapGun::NMEZapomatic gun_omatic;
    zMervEngine engine;
    MervLetMeKnow tellMeWhen;
    F32 ang_blades;
    zDustRingEmitter* dust_propwash;
    S8 cnt_nextDustEmit;
    S8 unused[3];

    void CreateConfig(const char* name);
    S32 DfltVulnFlags();
    void Init(xEntAsset* asset);
    void Setup();
    void SelfSetup();
    void SelfDestroy();
    void MervMainBrain();
    // No GC or PS2 symbol (inlined into SelfSetup); the name is ours. Builds the move brain
    // that MervLetMeKnow drives from the main brain's goal changes.
    void MervMoveBrain()
    {
        xBehaveMgr* bmgr = xBehaveMgr_GetSelf();
        xPsyche* psy;

        psy_move = bmgr->Subscribe(this, 0);
        psy = psy_move;
        psy->BrainBegin();
        psy->AddGoal(NME_GOAL_MERV_MOVE, NULL);
        psy->AddGoal(NME_GOAL_NOMANLAND, NULL);
        psy->BrainEnd();
        psy->SetSafety(NME_GOAL_MERV_MOVE);

        tellMeWhen.psy_mervyn = psy_self;
        psy_self->SetNotify(&tellMeWhen);
    }
    U32 AnimPick(S32 gid, en_npcgspot gspot, xGoal* rawgoal);
    xAnimTable* CreateAnimTable(xAnimTable* table);
    S32 GoalPick(xGoal* goal, S32& nextgoal, en_trantype& trantype);
    void Reset();
    void BUpdate(xVec3* pos);
    void DieTheGoodDeath();
    void Process(F32 dt);
    void NewTime(F32 dt);
    void HelioDustUpdate(F32 dt);
    void RenderExtra();
    en_vis VisionTarget(xEnt* ent, en_defcon defcon, S32 flags) const;
    F32 HealthRatio();
    // No GC symbol: always inlined into NewTime.
    void BladesUpdate(F32 dt)
    {
        F32 rat;
        F32 spd_turn;
        F32 ang_perstep;
        xMat3x3 mat_rot;
        xMat4x3* mat_blades;

        rat = spd_throttle / cfg_mervyn->battle.spd_move;
        rat = CLAMP(rat, 0.0f, 1.0f);
        spd_turn = SMOOTH(rat, 9.0f * PI, 9.0f * PI);

        ang_perstep = spd_turn * dt;
        ang_blades += ang_perstep;
        if (ang_blades > 2.0f * PI)
        {
            ang_blades -= 2.0f * PI;
        }

        xMat3x3RotY(&mat_rot, ang_blades);
        mat_blades = (xMat4x3*)&model->Mat[27];
        xMat3x3Copy(mat_blades, &mat_rot);
    }
    S32 ChkBowlingBall(xVec3* dir_NtoBall, F32* dst_NtoBall);
    S32 TypeHandleMail(NMEMsg* mail);
    S32 MervMailSys(NMESysEvent* sysmail);
    S32 MervMailDamage(NMEDamageInfo* dmgmail);
    S32 SafeToTaunt();
    void UpdateGunnerStatus();

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
