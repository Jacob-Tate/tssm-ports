#ifndef ZNMETYPESTDZAPPER_H
#define ZNMETYPESTDZAPPER_H

#include "zNMETypeStandard.h"
#include "zNMEZapGun.h"

// Layout from the PS2 DWARF.
struct NMECfgZapper
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
        F32 rad_aware;
        F32 rad_awaregrey;
    } ranges;
    struct Battle
    {
        F32 spd_move;
        F32 acc_move;
        F32 spd_turn;
    } battle;
};

struct ZapLetMeKnow : xPSYNote
{
    xPsyche* psy_zapper;

    // No GC or PS2 symbol (inlined into Notice); the name is ours. The move brain goal that goes
    // with the main brain's goal (0: none).
    S32 MoveGoalFor(S32 gid_main) const
    {
        S32 gid_move;

        switch (gid_main)
        {
        case NME_GOAL_ZAP_NORMAL:
        case NME_GOAL_ZAP_AWARE:
        case NME_GOAL_ZAP_BATTLE:
        case NME_GOAL_ZAP_ZAP:
        case NME_GOAL_TAUNT:
        case NME_GOAL_ZAP_WOUND:
            gid_move = NME_GOAL_ZAP_MOVE;
            break;
        case NME_GOAL_SCAREWAIT:
        case NME_GOAL_SCARE:
        case NME_GOAL_NOTICE:
        case NME_GOAL_SPOOKED:
        case NME_GOAL_AFTERLIFE:
        case NME_GOAL_SPAWN:
        case NME_GOAL_DEV_ANIMVIEW:
        case NME_GOAL_DEV_HEROMODE:
            gid_move = NME_GOAL_NOMANLAND;
            break;
        default:
            gid_move = 0;
            break;
        }

        return gid_move;
    }

    // No GC symbol (inlined into Notice); named in the PS2 DWARF.
    // Picks the gun mode that goes with the main brain's goal.
    ZapGun::en_zapmod SetGunnerState(S32 gid_main) const
    {
        ZapGun::en_zapmod gun_state;

        switch (gid_main)
        {
        case NME_GOAL_ZAP_NORMAL:
            gun_state = ZapGun::ZAP_MODE_IDLE;
            break;
        case NME_GOAL_TAUNT:
            if (globals.player.s->Health < 1)
            {
                gun_state = ZapGun::ZAP_MODE_IDLE;
            }
            else
            {
                gun_state = ZapGun::ZAP_MODE_WARN;
            }
            break;
        case NME_GOAL_ZAP_AWARE:
        case NME_GOAL_ZAP_BATTLE:
            gun_state = ZapGun::ZAP_MODE_WARN;
            break;
        case NME_GOAL_ZAP_WOUND:
        case NME_GOAL_NOTICE:
        case NME_GOAL_SCARE:
        case NME_GOAL_SPOOKED:
            gun_state = ZapGun::ZAP_MODE_IDLE;
            break;
        case NME_GOAL_ZAP_ZAP:
            gun_state = ZapGun::ZAP_MODE_SHOOT;
            break;
        case NME_GOAL_SCAREWAIT:
        case NME_GOAL_AFTERLIFE:
        case NME_GOAL_SPAWN:
        case NME_GOAL_DEV_ANIMVIEW:
        case NME_GOAL_DEV_HEROMODE:
            gun_state = ZapGun::ZAP_MODE_NOTHING;
            break;
        default:
            gun_state = ZapGun::ZAP_MODE_NOTHING;
            break;
        }

        return gun_state;
    }

    void Notice(en_psynote note, xGoal* goal, void* data);
};

// Members are in the original source order; CreateConfig is the key function that emits the
// vtable.
struct zNMEZapper : zNMEStandard
{
    zNMEZapper()
    {
    }
    xPsyche* psy_move; // 0x1F8
    const NMECfgZapper* cfg_zapper; // 0x1FC
    ZapGun::NMEZapomatic gun_omatic;
    zMervEngine engine;
    ZapLetMeKnow tellMeWhen;
    F32 tmr_emitExahust;

    void CreateConfig(const char* name);
    S32 DfltVulnFlags();
    void Init(xEntAsset* asset);
    void Setup();
    void SelfSetup();
    void SelfDestroy();
    void ZapMainBrain();
    // No GC or PS2 symbol (inlined into SelfSetup); the name is ours. Builds the move brain
    // that ZapLetMeKnow drives from the main brain's goal changes.
    void ZapMoveBrain()
    {
        xBehaveMgr* bmgr = xBehaveMgr_GetSelf();
        xPsyche* psy;

        psy_move = bmgr->Subscribe(this, 0);
        psy = psy_move;
        psy->BrainBegin();
        psy->AddGoal(NME_GOAL_ZAP_MOVE, NULL);
        psy->AddGoal(NME_GOAL_NOMANLAND, NULL);
        psy->BrainEnd();
        psy->SetSafety(NME_GOAL_ZAP_MOVE);

        tellMeWhen.psy_zapper = psy_self;
        psy_self->SetNotify(&tellMeWhen);
    }
    U32 AnimPick(S32 gid, en_npcgspot gspot, xGoal* goal);
    xAnimTable* CreateAnimTable(xAnimTable* table);
    S32 GoalPick(xGoal* goal, S32& nextgoal, en_trantype& trantype);
    void Reset();
    void BUpdate(xVec3* pos);
    void Process(F32 dt);
    void NewTime(F32 dt);
    void RenderExtra();
    en_vis VisionTarget(xEnt* ent, en_defcon defcon, S32 flags) const;
    F32 HealthRatio();
    void JetEngine(F32 dt);
    S32 TypeHandleMail(NMEMsg* mail);
    S32 ZapMailDamage(NMEDamageInfo* dmgmail);
    S32 ZapMailSys(NMESysEvent* sysmail);
    S32 SafeToTaunt();

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
