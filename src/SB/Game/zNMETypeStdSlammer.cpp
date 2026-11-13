#include "zNMETypeStdSlammer.h"
#include "xDebug.h"

namespace Slammer
{
    namespace Config
    {
        static const NMECfgSlammer cfg_slammer_v1 = {
            { 2 },
            { 5.5f, 4.0f, DEG2RAD(90) },
            { 4.0f, 2.5f, 0.5f },
            { 2.0f, 1.0f, 0 },
            { 3.5f, 5.5f, 4.0f, DEG2RAD(90), 4.0f, 0.35f, 2.5f },
        };
        static const NMECfgSlammer cfg_slammer_v2 = {
            { 2 },
            { 6.5f, 5.0f, DEG2RAD(180) },
            { 4.5f, 2.5f, 0.67f },
            { 2.0f, 1.0f, 1 },
            { 3.5f, 6.5f, 5.0f, DEG2RAD(180), 5.0f, 0.65f, 2.5f },
        };
        static const NMECfgSlammer cfg_slammer_v3 = {
            { 3 },
            { 8.5f, 16.0f, DEG2RAD(360) },
            { 5.0f, 2.5f, 0.86f },
            { 2.0f, 1.0f, 1 },
            { 3.5f, 9.5f, 12.0f, DEG2RAD(360), 5.5f, 1.5f, 2.5f },
        };
        const NMECfgSlammer* cfg_slam[10] = { &cfg_slammer_v1, &cfg_slammer_v1, &cfg_slammer_v2,
                                              &cfg_slammer_v3 };
    } // namespace Config

    namespace Aware
    {
        // Extra radius beyond the arena in which the player is still glimpsed.
        F32 dst_arenaDetectBonus = 5.0f;

        // Odds of the outcomes in choices when an aware slammer loses sight of the player, per
        // variant (indexed by NMEVariantIdx).
        static const F32 wt_person_v1[3] = { 0.45f, 0.45f, 0.1f };
        static const F32 wt_person_v2[3] = { 0.25f, 0.5f, 0.25f };
        static const F32 wt_person_v3[3] = { 0.1f, 0.45f, 0.45f };
        static const S32 choices[3] = { 0, 1, 2 };

        const F32* wt_person[10] = {
            wt_person_v1,
            wt_person_v1,
            wt_person_v2,
            wt_person_v3,
        };
    } // namespace Aware

    NMEHelpBound::BndData bnd_info = {
        k_XBOUNDTYPE_SPHERE,
        { 0, 0, { 0, 0 } },
        { 1.0f, { 0.0f, 1.0f, 0.45f } },
        { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } },
    };
} // namespace Slammer

namespace SlamAttack
{
    // Window of the attack animation (seconds) in which the swipe can hurt the player.
    static const F32 tym_animDamage[2] = { 0.8f, 1.4f };

    // Window of the attack animation (seconds) in which the shock wave is spawned.
    static const F32 tym_kickWave[2] = { 0.99f, 1.1f };

    // Window of the attack animation (seconds) in which the swipe ribbon is emitted.
    static const F32 tym_ribemit[2] = { 0.82f, 1.1f };

    namespace SwipeFX
    {
        static const xFXRibbon::curve_node curvedata[5] = {
            { 0.0f, { 0xFF, 0x9B, 0x00, 0x55 }, 0.0f }, { 0.2f, { 0xFF, 0x64, 0xFF, 0x55 }, 0.8f },
            { 0.5f, { 0xFF, 0xFF, 0x00, 0x55 }, 1.0f }, { 0.8f, { 0xFF, 0x64, 0xFF, 0x55 }, 0.8f },
            { 1.0f, { 0xFF, 0x9B, 0x00, 0x55 }, 0.0f },
        };

        NMEHelpRib::RibData rib_swipeThug = {
            { 1, 1, 0, 0 },
            { "fx_streak2", (xFXRibbon::curve_node*)curvedata, 5, 0.75f, 0.5f },
            { 1, 1.0f, 1.0f },
            { NULL, 0 },
        };

        NMEHelpRib::RibData rib_swipeJunk = {
            { 1, 1, 0, 0 },
            { "fx_streak2", (xFXRibbon::curve_node*)curvedata, 5, 0.75f, 0.5f },
            { 1, 1.0f, 1.0f },
            { NULL, 0 },
        };

        NMEHelpRib::RibData rib_swipeGoof = {
            { 1, 1, 0, 0 },
            { "fx_streak2", (xFXRibbon::curve_node*)curvedata, 5, 0.75f, 0.5f },
            { 1, 1.0f, 1.0f },
            { NULL, 0 },
        };

        // Indexed by NMEVariantIdx() of the slammer type.
        NMEHelpRib::RibData* rib_swipe[10] = {
            &rib_swipeThug,
            &rib_swipeThug,
            &rib_swipeJunk,
            &rib_swipeGoof,
        };
    } // namespace SwipeFX
} // namespace SlamAttack

void zNMESlammer::CreateConfig(const char* name)
{
    en_nmevar vidx;

    zNMEStandard::CreateConfig(name);
    vidx = NMEVariantIdx((en_npctyp)itemType);
    cfg_slammer = Slammer::Config::cfg_slam[vidx];
}

S32 zNMESlammer::DfltVulnFlags()
{
    return 0xFFFFFFF7;
}

U8 zNMESlammer::ColChkFlags() const
{
    U8 flags = 0x28;

    if (((zNMEAssetStandard*)nmeass)->flg_stdass & 0x8)
    {
        flags |= 0x2;
    }
    if (((zNMEAssetStandard*)nmeass)->flg_stdass & 0x4)
    {
        flags |= 0x4;
    }

    return flags;
}

U8 zNMESlammer::ColPenFlags() const
{
    U8 flags = 0x28;

    if (((zNMEAssetStandard*)nmeass)->flg_stdass & 0x8)
    {
        flags |= 0x2;
    }
    if (((zNMEAssetStandard*)nmeass)->flg_stdass & 0x4)
    {
        flags |= 0x4;
    }

    return flags;
}

void zNMESlammer::Init(xEntAsset* entass)
{
    zNMEStandard::Init(entass);
    flg_move |= 0x2;
}

void zNMESlammer::Setup()
{
    zNMEStandard::Setup();

    SlamAttack::SwipeFX::rib_swipeThug.FindTexture();
    SlamAttack::SwipeFX::rib_swipeJunk.FindTexture();
    SlamAttack::SwipeFX::rib_swipeGoof.FindTexture();

    if (itemType == NME_TYPE_SLAMMER_V2)
    {
        xAntennaAddAntenna(this, 0.0f, 0.0f, 8, 9, false);
    }
}

void zNMESlammer::Reset()
{
    xPsyche* psy;
    zNMEGoalAfterlife* goal;

    zNMEStandard::Reset();

    vel_physics = g_O3;

    psy = psy_self;
    if (((zNMEAssetStandard*)nmeass)->flg_stdass & 0x1)
    {
        psy->GoalSet(NME_GOAL_SCAREWAIT, 1);
    }
    else if (npc_duplodude)
    {
        goal = (zNMEGoalAfterlife*)psy->FindGoal(NME_GOAL_AFTERLIFE);
        goal->stylinDyin = DIE_DUPLOONLY;
        psy->GoalSet(NME_GOAL_AFTERLIFE, 1);
        goal->stylinDyin = DIE_LOUDLY;
    }
    else
    {
        psy->GoalSet(NME_GOAL_IDLE, 1);
    }

    pts_health = cfg_slammer->health.pts_healthMax;
}

void zNMESlammer::SelfSetup()
{
    xBehaveMgr* bmgr;
    xPsyche* psy;
    xGoal* goal;
    xGoal* g;

    bmgr = xBehaveMgr_GetSelf();
    psy_self = bmgr->Subscribe(this, 0);

    psy = psy_self;

    psy->BrainBegin();

    goal = psy->AddGoal(NME_GOAL_IDLE, NULL);
    goal->fun_process = NULL;
    goal->cbdata = NULL;
    goal = psy->AddGoal(NME_GOAL_WANDER, NULL);
    goal->fun_process = NULL;
    goal->cbdata = NULL;
    goal = psy->AddGoal(NME_GOAL_PATROL, NULL);
    goal->fun_process = NULL;
    goal->cbdata = NULL;

    g = psy->FindGoal(NME_GOAL_IDLE);
    g->fun_process = Slam_SeePlyr;
    g->cbdata = NULL;
    g = psy->FindGoal(NME_GOAL_PATROL);
    g->fun_process = Slam_SeePlyr;
    g->cbdata = NULL;
    g = psy->FindGoal(NME_GOAL_WANDER);
    g->fun_process = Slam_SeePlyr;
    g->cbdata = NULL;

    psy->AddGoal(NME_GOAL_EVILPAT, NULL);
    psy->AddGoal(NME_GOAL_STUNNED, NULL);
    psy->AddGoal(NME_GOAL_PATCARRY, NULL);
    psy->AddGoal(NME_GOAL_PATTWIRL, NULL);
    psy->AddGoal(NME_GOAL_PATTHROW, NULL);

    psy->AddGoal(NME_GOAL_WOUND, NULL);
    psy->AddGoal(NME_GOAL_AFTERLIFE, NULL);
    psy->AddGoal(NME_GOAL_SPAWN, NULL);
    psy->AddGoal(NME_GOAL_SCAREWAIT, NULL);
    psy->AddGoal(NME_GOAL_SCARE, NULL);
    psy->AddGoal(NME_GOAL_TAUNT, NULL);
    psy->AddGoal(NME_GOAL_NOTICE, NULL);

    goal = psy->AddGoal(NME_GOAL_SPOOKED, NULL);
    goal->fun_process = Slam_SeePlyr;
    goal->cbdata = NULL;

    psy->AddGoal(NME_GOAL_SLAMMER_AWARE, NULL);
    psy->AddGoal(NME_GOAL_SLAMMER_BATTLE, NULL);
    psy->AddGoal(NME_GOAL_SLAMMER_ATTACK, NULL);

    psy->BrainEnd();
    psy->SetSafety(NME_GOAL_IDLE);
}

U32 zNMESlammer::AnimPick(S32 gid, en_npcgspot gspot, xGoal* goal)
{
    en_stananim idx;

    switch (gid)
    {
    case NME_GOAL_IDLE:
        idx = STAN_ANIM_IDLE;
        break;
    case NME_GOAL_PATROL:
    case NME_GOAL_WANDER:
        if (gspot == NME_GSPOT_PATROLPAUSE)
        {
            idx = STAN_ANIM_IDLE;
        }
        else
        {
            idx = STAN_ANIM_MOVE;
        }
        break;
    case NME_GOAL_NOTICE:
        idx = STAN_ANIM_NOTICE;
        break;
    case NME_GOAL_TAUNT:
        if (globals.player.s->Health < 1)
        {
            idx = STAN_ANIM_VICTORY;
        }
        else
        {
            idx = STAN_ANIM_TAUNT;
        }
        break;
    case NME_GOAL_WOUND:
        if (pts_health < 1)
        {
            idx = STAN_ANIM_DEFEAT;
        }
        else
        {
            idx = STAN_ANIM_DAMAGE;
        }
        break;
    case NME_GOAL_SPAWN:
    case NME_GOAL_SCAREWAIT:
        idx = STAN_ANIM_IDLE;
        break;
    case NME_GOAL_SCARE:
        if (gspot == NME_GSPOT_LOOP)
        {
            idx = STAN_ANIM_SCARE_LOOP;
        }
        else if (gspot == NME_GSPOT_FINISH)
        {
            idx = STAN_ANIM_SCARE_DONE;
        }
        else
        {
            idx = STAN_ANIM_SCARE_LOOP;
        }
        break;
    case NME_GOAL_SPOOKED:
        idx = STAN_ANIM_SPOOK_LOOP;
        break;
    case NME_GOAL_SLAMMER_AWARE:
        idx = STAN_ANIM_ALERT;
        break;
    case NME_GOAL_SLAMMER_BATTLE:
        if (gspot == NME_GSPOT_START)
        {
            idx = STAN_ANIM_ALERT;
        }
        else if (gspot == NME_GSPOT_RESUME)
        {
            idx = STAN_ANIM_ALERT;
        }
        else if (gspot == NME_GSPOT_STARTALT)
        {
            idx = STAN_ANIM_CHASE;
        }
        else
        {
            idx = STAN_ANIM_ALERT;
        }
        break;
    case NME_GOAL_SLAMMER_ATTACK:
        idx = STAN_ANIM_ATTACK;
        break;
    case NME_GOAL_STUNNED:
        idx = STAN_ANIM_STUN_PREP;
        break;
    case NME_GOAL_PATCARRY:
        idx = STAN_ANIM_STUN_PICKUP;
        break;
    case NME_GOAL_PATTWIRL:
        idx = STAN_ANIM_SPIN_PREP;
        break;
    case NME_GOAL_PATTHROW:
        if (gspot == NME_GSPOT_START)
        {
            // Retail compares the state ID against the en_stananim index itself, not its
            // hash (g_hash_stananim[...]), so this never picks the spin throw.
            U32 anid = AnimCurStateID();
            if (anid == STAN_ANIM_SPIN_LOOP || anid == STAN_ANIM_SPIN_PREP)
            {
                idx = STAN_ANIM_SPIN_THROW;
            }
            else
            {
                idx = STAN_ANIM_TOSS_PREP;
            }
        }
        else
        {
            idx = STAN_ANIM_TOSS_LOOP;
        }
        break;
    default:
        idx = STAN_ANIM_IDLE;
        break;
    }

    return g_hash_stananim[idx];
}

xAnimTable* zNMESlammer::CreateAnimTable(xAnimTable* table)
{
    if (table == NULL)
    {
        table = xAnimTableNew("zNMESlammer", 0);
    }

    xAnimTableNewState(table, g_strz_stananim[STAN_ANIM_IDLE], 0x10, 1, 1.0f, NULL, NULL, 0.0f,
                       NULL, NULL, xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, g_strz_stananim[STAN_ANIM_MOVE], 0x10, 1, 1.0f, NULL, NULL, 0.0f,
                       NULL, NULL, xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, g_strz_stananim[STAN_ANIM_ALERT], 0x410, 0x2000001, 1.0f, NULL, NULL,
                       0.0f, NULL, NULL, xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, g_strz_stananim[STAN_ANIM_CHASE], 0x410, 0x2000001, 1.0f, NULL, NULL,
                       0.0f, NULL, NULL, xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, g_strz_stananim[STAN_ANIM_SCARE_LOOP], 0x10, 1, 1.0f, NULL, NULL,
                       0.0f, NULL, NULL, xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, g_strz_stananim[STAN_ANIM_SCARE_DONE], 0, 1, 1.0f, NULL, NULL, 0.0f,
                       NULL, NULL, xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, g_strz_stananim[STAN_ANIM_SPOOK_LOOP], 0x10, 1, 1.0f, NULL, NULL,
                       0.0f, NULL, NULL, xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, g_strz_stananim[STAN_ANIM_NOTICE], 0, 1, 1.0f, NULL, NULL, 0.0f, NULL,
                       NULL, xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, g_strz_stananim[STAN_ANIM_TAUNT], 0x410, 0x2000001, 1.0f, NULL, NULL,
                       0.0f, NULL, NULL, xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, g_strz_stananim[STAN_ANIM_VICTORY], 0x410, 0x2000001, 1.0f, NULL,
                       NULL, 0.0f, NULL, NULL, xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, g_strz_stananim[STAN_ANIM_DAMAGE], 0x10, 1, 1.0f, NULL, NULL, 0.0f,
                       NULL, NULL, xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, g_strz_stananim[STAN_ANIM_DEFEAT], 0x10, 1, 1.0f, NULL, NULL, 0.0f,
                       NULL, NULL, xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, g_strz_stananim[STAN_ANIM_ATTACK], 0, 1, 1.0f, NULL, NULL, 0.0f, NULL,
                       NULL, xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, g_strz_stananim[STAN_ANIM_STUN_PREP], 0x20, 1, 1.0f, NULL, NULL, 0.0f,
                       NULL, NULL, xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, g_strz_stananim[STAN_ANIM_STUN_LOOP], 0x10, 1, 1.0f, NULL, NULL, 0.0f,
                       NULL, NULL, xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, g_strz_stananim[STAN_ANIM_STUN_PICKUP], 0x20, 1, 1.0f, NULL, NULL,
                       0.0f, NULL, NULL, xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, g_strz_stananim[STAN_ANIM_STUN_CARRY], 0x10, 1, 1.0f, NULL, NULL,
                       0.0f, NULL, NULL, xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, g_strz_stananim[STAN_ANIM_TOSS_PREP], 0x20, 1, 1.0f, NULL, NULL, 0.0f,
                       NULL, NULL, xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, g_strz_stananim[STAN_ANIM_TOSS_LOOP], 0x10, 1, 1.0f, NULL, NULL, 0.0f,
                       NULL, NULL, xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, g_strz_stananim[STAN_ANIM_SPIN_PREP], 0x20, 1, 1.0f, NULL, NULL, 0.0f,
                       NULL, NULL, xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, g_strz_stananim[STAN_ANIM_SPIN_LOOP], 0x10, 1, 1.0f, NULL, NULL, 0.0f,
                       NULL, NULL, xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, g_strz_stananim[STAN_ANIM_SPIN_THROW], 0x20, 1, 1.0f, NULL, NULL,
                       0.0f, NULL, NULL, xAnimDefaultBeforeEnter, NULL, NULL);

    S32 ourAnims[23] = { STAN_ANIM_IDLE,        STAN_ANIM_MOVE,       STAN_ANIM_ALERT,
                         STAN_ANIM_CHASE,       STAN_ANIM_SCARE_LOOP, STAN_ANIM_SCARE_DONE,
                         STAN_ANIM_SPOOK_LOOP,  STAN_ANIM_NOTICE,     STAN_ANIM_TAUNT,
                         STAN_ANIM_VICTORY,     STAN_ANIM_DAMAGE,     STAN_ANIM_DEFEAT,
                         STAN_ANIM_ATTACK,      STAN_ANIM_STUN_PREP,  STAN_ANIM_STUN_LOOP,
                         STAN_ANIM_STUN_PICKUP, STAN_ANIM_STUN_CARRY, STAN_ANIM_TOSS_PREP,
                         STAN_ANIM_TOSS_LOOP,   STAN_ANIM_SPIN_PREP,  STAN_ANIM_SPIN_LOOP,
                         STAN_ANIM_SPIN_THROW,  STAN_ANIM_UNKNOWN };
    NMEC_BuildStandardAnimTran(table, g_strz_stananim, ourAnims, 3, 0.2f);

    xAnimTableNewTransition(table, g_strz_stananim[STAN_ANIM_STUN_PREP],
                            g_strz_stananim[STAN_ANIM_STUN_LOOP], NULL, NULL, 0x10, 0, 0.0f, 0.0f,
                            0, 0, 0.2f, NULL);
    xAnimTableNewTransition(table, g_strz_stananim[STAN_ANIM_STUN_PICKUP],
                            g_strz_stananim[STAN_ANIM_STUN_CARRY], NULL, NULL, 0x10, 0, 0.0f, 0.0f,
                            0, 0, 0.2f, NULL);
    xAnimTableNewTransition(table, g_strz_stananim[STAN_ANIM_TOSS_PREP],
                            g_strz_stananim[STAN_ANIM_TOSS_LOOP], NULL, NULL, 0x10, 0, 0.0f, 0.0f,
                            0, 0, 0.2f, NULL);
    xAnimTableNewTransition(table, g_strz_stananim[STAN_ANIM_SPIN_PREP],
                            g_strz_stananim[STAN_ANIM_SPIN_LOOP], NULL, NULL, 0x10, 0, 0.0f, 0.0f,
                            0, 0, 0.2f, NULL);
    xAnimTableNewTransition(table, g_strz_stananim[STAN_ANIM_SPIN_THROW],
                            g_strz_stananim[STAN_ANIM_TOSS_LOOP], NULL, NULL, 0x10, 0, 0.0f, 0.0f,
                            0, 0, 0.2f, NULL);

    return table;
}

void zNMESlammer::BUpdate(xVec3* pos)
{
    Slammer::bnd_info.UpdateBound(&bound, this, pos);
}

void zNMESlammer::Process(F32 dt)
{
    S32 gid;

    gid = psy_self->GIDOfActive();
    if (gid == NME_GOAL_PATTHROW)
    {
        psy_self->Timestep(dt, NULL);
        zThrown_NMEUpdate(this, xglobals->sceneCur, dt);
    }
    else
    {
        StdTyp_Process(dt);
    }

    StdNME_SuicideIsPainless();
}

void zNMESlammer::Type_AddYourMove(F32 dt)
{
    if (flg_colCheck)
    {
        if (psy_self->GIDOfActive() != NME_GOAL_WOUND)
        {
            // Not a PS2 DWARF local: the product is not fused into the subtraction.
            F32 spd_grav = cfg_common->physics.acc_grav * dt;

            vel_physics.y -= spd_grav;
            if (!(vel_physics.y > cfg_common->physics.spd_maxFall))
            {
                vel_physics.y = cfg_common->physics.spd_maxFall;
            }

            xVec3 delta = vel_physics * dt;
            model->Mat->pos.x += delta.x;
            model->Mat->pos.y += delta.y;
            model->Mat->pos.z += delta.z;
        }
    }
    else if (vel_physics.y < 0.0f)
    {
        vel_physics.y = 0.0f;
    }
}

void zNMESlammer::CollideReview()
{
    if (collis->colls[0].flags & 0x1)
    {
        if (vel_physics.y < 0.0f)
        {
            vel_physics.y = 0.0f;
        }
    }

    StdTyp_CollideReview();
}

S32 zNMESlammer::GoalPick(xGoal* goal, S32& gid_would, en_trantype& trantype)
{
    S32 altered = 1;

    switch (gid_would)
    {
    case NME_GOAL_TRIGGER_NORMAL:
        gid_would = NME_GOAL_IDLE;
        break;
    case NME_GOAL_TRIGGER_SCARY:
        gid_would = NME_GOAL_SPOOKED;
        break;
    case NME_GOAL_TRIGGER_DETECT:
        gid_would = NME_GOAL_NOTICE;
        break;
    case NME_GOAL_TRIGGER_VINIVICIVIDI:
        gid_would = NME_GOAL_TAUNT;
        break;
    case NME_GOAL_TRIGGER_ALERT:
        gid_would = NME_GOAL_SLAMMER_AWARE;
        break;
    case NME_GOAL_TRIGGER_BATTLE:
        gid_would = NME_GOAL_SLAMMER_BATTLE;
        break;
    case NME_GOAL_TRIGGER_ATTACK:
        gid_would = NME_GOAL_SLAMMER_ATTACK;
        break;
    default:
        altered = 0;
        break;
    }

    return altered;
}

S32 zNMESlammer::TypeHandleMail(NMEMsg* mail)
{
    S32 handled = 1;
    xPsyche* psy;
    S32 gid;

    switch (mail->msgid)
    {
    case NME_MID_STUN:
        psy = psy_self;
        gid = psy->GIDOfActive();
        if (NMEStd_GoalInList((en_npcgol)gid, Standard::noStunGList))
        {
            tmr_stunned = -1.0f;
        }
        else
        {
            tmr_stunned = mail->stundata.tym_stuntime * (0.25f * (xurand() - 0.5f)) +
                          mail->stundata.tym_stuntime;
            if (!NMEStd_GoalInList((en_npcgol)gid, Standard::alreadyStunnedList))
            {
                psy->GoalSet(NME_GOAL_EVILPAT, 0);
            }
        }
        break;
    case NME_MID_DAMAGE:
        handled = SlamMailDamage(&mail->dmgdata);
        break;
    case NME_MID_SYSEVENT:
        handled = DfltSysEventMail(&mail->sysevent);
        break;
    case NME_MID_CELEBRATE:
        DfltCelebrateMail();
        break;
    default:
        handled = 0;
        break;
    }

    return handled;
}

// No GC symbol (inlined into TypeHandleMail); out of line in the PS2 build.
inline S32 zNMESlammer::SlamMailDamage(NMEDamageInfo* dmgmail)
{
    S32 rc;
    S32 handled;
    xPsyche* psy;
    xGoal* goal;

    rc = IsVulnerable(dmgmail->dmg_type);
    if (!rc)
    {
        return 1;
    }

    handled = 1;
    switch (dmgmail->dmg_type)
    {
    case DMGTYP_ABOVE:
    case DMGTYP_BELOW:
    case DMGTYP_SIDE:
    case DMGTYP_INSTAKILL:
    case DMGTYP_WOUNDEVENT:
    case DMGTYP_NMEATTACK:
    case DMGTYP_HITBYTOSS:
    case DMGTYP_NMEKNOCKED:
    case DMGTYP_ROPE:
    case DMGTYP_CRUISEBUBBLE:
    case DMGTYP_FRIENDLYFIRE:
    case DMGTYP_REFLECTED:
    case DMGTYP_BOULDER:
    case DMGTYP_BUBBOWL:
    case DMGTYP_THUNDER_TIKI_EXPLOSION:
    case DMGTYP_DAMAGE_SURFACE:
    case DMGTYP_BUNGEED:
    case DMGTYP_SURFACE:
    case DMGTYP_CARTWHEEL:
    case DMGTYP_SLIDE:
    case DMGTYP_EXPLOSION:
    case DMGTYP_BBOWLZAP:
        pts_health -= dmgmail->amt_damage;
        if (pts_health < 0)
        {
            pts_health = 0;
        }

        WoundedInput(NME_GOAL_WOUND, dmgmail);
        psy_self->GoalSet(NME_GOAL_WOUND, 0);
        break;
    case DMGTYP_KILLEVENT:
        psy = psy_self;
        goal = psy->FindGoal(NME_GOAL_AFTERLIFE);
        ((zNMEGoalAfterlife*)goal)->stylinDyin = DIE_MUFFLED;
        psy->GoalSet(NME_GOAL_AFTERLIFE, 0);
        break;
    default:
        handled = 0;
        break;
    }

    return handled;
}

en_vis zNMESlammer::VisionTarget(xEnt* ent, en_defcon defcon, S32 flags) const
{
    zNMEArena* arena;

    if (NME::GlobalData::flg_wonder)
    {
        return VIS_NOBODY_AROUND;
    }

    if (runtimeData.overrideDetect == ALLOW_NEVER)
    {
        return VIS_NOBODY_AROUND;
    }

    arena = (zNMEArena*)&this->arena;
    if (!arena->IsReady())
    {
        return VIS_NOBODY_AROUND;
    }

    if (runtimeData.overrideAttack == ALLOW_ALWAYS)
    {
        return VIS_I_SEEE_YOUUU;
    }

    if (runtimeData.overrideDetect == ALLOW_ALWAYS)
    {
        return VIS_I_SEEE_YOUUU;
    }

    if (arena->IncludesPos(xEntGetPos(ent), 0.0f, NULL))
    {
        if (runtimeData.overrideAttack == ALLOW_NEVER)
        {
            return VIS_GREY_AND_FOGGY;
        }
        return VIS_I_SEEE_YOUUU;
    }
    else if (arena->IncludesPos(xEntGetPos(ent), Slammer::Aware::dst_arenaDetectBonus, NULL))
    {
        return VIS_GREY_AND_FOGGY;
    }

    return VIS_NOBODY_AROUND;
}

F32 zNMESlammer::HealthRatio()
{
    F32 rat;

    if (cfg_slammer == NULL)
    {
        return 0.0f;
    }

    rat = pts_health;
    rat /= cfg_slammer->health.pts_healthMax;
    rat = CLAMP(rat, 0.0f, 1.0f);
    return rat;
}

// The PS2 DWARF lists only the five vectors. The lengths, dir_inv and dot (our names) exist
// because retail computes each sqrt once and passes a negated copy of dir_plyr to xVec3Dot.
void zNMESlammer::StraddleClamp(xVec3* pos_pred)
{
    xVec3 dir_NtoP;
    xVec3 dir_plyr;
    xVec3 dir_PtoP;
    F32 dst_NtoP;
    F32 dst_plyr;
    F32 dst_PtoP;

    dst_NtoP = xsqrt(XZDstSqToPos(pos_pred, &dir_NtoP, NULL));
    dst_plyr = xsqrt(XZDstSqToPlayer(&dir_plyr, NULL));
    dst_PtoP = xsqrt(
        NMEC_DstSq(xEntGetPos(xglobals->___player_ent_dont_use_directly), pos_pred, &dir_PtoP));

    if (dst_PtoP < 0.1f)
    {
        return;
    }
    xVec3SMulBy(&dir_PtoP, 1.0f / dst_PtoP);

    if (dst_NtoP < 0.1f)
    {
        return;
    }
    xVec3SMulBy(&dir_NtoP, 1.0f / dst_NtoP);

    if (dst_plyr < 0.1f)
    {
        return;
    }
    xVec3SMulBy(&dir_plyr, 1.0f / dst_plyr);

    if (dir_plyr.dot(dir_NtoP) > 0.0f)
    {
        return;
    }

    xVec3 dir_inv;
    xVec3Inv(&dir_inv, &dir_plyr);
    F32 dot = xVec3Dot(&dir_inv, &dir_PtoP);
    xVec3 pos_repred = dir_PtoP * (0.75f * (dot * dst_plyr));
    pos_repred += *xEntGetPos(xglobals->___player_ent_dont_use_directly);

    xVec3 dir_revised;

    if (XZDstSqToPos(&pos_repred, &dir_revised, NULL) < 0.00001f)
    {
        return;
    }

    *pos_pred = pos_repred;
}

S32 zNMESlammer::Slam_SeePlyr(xGoal* rawgoal, void* data, en_trantype* trantype, F32 dt, void* ctxt)
{
    S32 nextgoal = 0;
    zNMESlammer* npc;
    zNMEArena* arena;
    en_vis vis;
    // Not a PS2 DWARF local (see zNMESpinner::Spin_SeePlyr).
    S32 rc;

    if (xUtil_yesno(0.5f))
    {
        return nextgoal;
    }

    if (globals.player.s->Health < 1)
    {
        return nextgoal;
    }

    npc = (zNMESlammer*)rawgoal->psyche->clt_owner;

    arena = &npc->arena;
    rc = arena->NeedToCycle(npc);
    if (rc == 2)
    {
        arena->Cycle(npc, 1);
    }
    else if (rc)
    {
        arena->Cycle(npc, 0);
    }

    vis = npc->VisionTarget(xglobals->___player_ent_dont_use_directly, NME_DEFCON_NORMAL, 0);
    if (vis > VIS_GREY_AND_FOGGY)
    {
        return nextgoal;
    }

    *trantype = GOAL_TRAN_SET;
    nextgoal = (npc->tmr_common[2] < 0.0f) ? NME_GOAL_TRIGGER_DETECT : NME_GOAL_TRIGGER_ALERT;
    npc->GoalPick(rawgoal, nextgoal, *trantype);

    return nextgoal;
}

S32 zNMEGoalSlamAware::Enter(F32 dt, void* updCtxt)
{
    zNMESlammer* npc = (zNMESlammer*)psyche->clt_owner;

    npc->arena.Cycle(npc, 1);
    return zNMEGoalCommon::Enter(dt, updCtxt);
}

S32 zNMEGoalSlamAware::Process(en_trantype* trantype, F32 dt, void* ctxt)
{
    zNMESlammer* npc = (zNMESlammer*)psyche->clt_owner;
    S32 nextgoal = 0;
    S32 doTran;

    doTran = ChkTranCases(npc, nextgoal, *trantype);
    if (doTran)
    {
        return nextgoal;
    }

    if (bul_entered)
    {
        DoAutoAnim(NME_GSPOT_START, 0);
    }
    else if (bul_resumed)
    {
        DoAutoAnim(NME_GSPOT_RESUME, 0);
    }

    npc->MatFacePlayer(dt, PI, 1);
    return zNMEGoalCommon::Process(trantype, dt, ctxt);
}

S32 zNMEGoalSlamAware::ChkTranCases(zNMESlammer* npc, S32& nextgoal, en_trantype& trantype)
{
    en_vis vis;
    S32 vidx;
    const F32* wtlist;

    if (npc->model->Anim->Single->Blend->State != NULL)
    {
        return 0;
    }

    if (xUtil_yesno(0.5f))
    {
        return 0;
    }

    if (globals.player.s->Health < 1)
    {
        trantype = GOAL_TRAN_SET;
        nextgoal = NME_GOAL_TRIGGER_VINIVICIVIDI;
        npc->GoalPick(this, nextgoal, trantype);
        return 1;
    }
    else if (!npc->arena.IsReady())
    {
        trantype = GOAL_TRAN_SET;
        nextgoal = NME_GOAL_TRIGGER_NORMAL;
        npc->GoalPick(this, nextgoal, trantype);
        return 1;
    }
    else if (NME::GlobalData::flg_wonder)
    {
        trantype = GOAL_TRAN_SET;
        nextgoal = NME_GOAL_TRIGGER_NORMAL;
        npc->GoalPick(this, nextgoal, trantype);
        return 1;
    }

    vis = npc->VisionTarget(xglobals->___player_ent_dont_use_directly, NME_DEFCON_NORMAL, 0);
    if (vis <= VIS_I_SEEE_YOUUU)
    {
        trantype = GOAL_TRAN_SET;
        nextgoal = NME_GOAL_TRIGGER_BATTLE;
        npc->GoalPick(this, nextgoal, trantype);
        return 1;
    }
    else if (vis > VIS_GREY_AND_FOGGY)
    {
        vidx = NMEVariantIdx((en_npctyp)npc->itemType);
        wtlist = Slammer::Aware::wt_person[vidx];
        // The PS2 DWARF has no local for the choice, so vidx is reused.
        vidx = xUtil_choose(Slammer::Aware::choices, 3, wtlist);
        if (vidx == 1)
        {
            trantype = GOAL_TRAN_SET;
            nextgoal = NME_GOAL_TRIGGER_NORMAL;
            npc->GoalPick(this, nextgoal, trantype);
            return 1;
        }
        else if (vidx == 0)
        {
            trantype = GOAL_TRAN_SET;
            nextgoal = NME_GOAL_TRIGGER_SCARY;
            npc->GoalPick(this, nextgoal, trantype);
            return 1;
        }
        else if (vidx == 2)
        {
            trantype = GOAL_TRAN_SET;
            nextgoal = NME_GOAL_TRIGGER_VINIVICIVIDI;
            npc->GoalPick(this, nextgoal, trantype);
            return 1;
        }
    }

    return 0;
}

S32 zNMEGoalSlamBattle::Enter(F32 dt, void* updCtxt)
{
    alertslam = SLAMMER_BATTLE_BEGIN;
    zNMEGoalCommon::Enter(dt, updCtxt);
    ((zNMESlammer*)psyche->clt_owner)->model->Anim->Single->BilinearLerp[0] = 1.0f;
    return 0;
}

S32 zNMEGoalSlamBattle::Resume(F32 dt, void* updCtxt)
{
    zNMEGoalCommon::Resume(dt, updCtxt);
    ((zNMESlammer*)psyche->clt_owner)->model->Anim->Single->BilinearLerp[0] = 1.0f;
    return 0;
}

S32 zNMEGoalSlamBattle::Process(en_trantype* trantype, F32 dt, void* ctxt)
{
    zNMESlammer* npc = (zNMESlammer*)psyche->clt_owner;
    S32 nextgoal = 0;
    S32 doTran;

    doTran = Chk_EarlyExit(npc, nextgoal, *trantype);
    if (doTran)
    {
        return nextgoal;
    }

    doTran = SubStateProcess(npc, dt, nextgoal, *trantype);
    if (doTran)
    {
        return nextgoal;
    }

    return zNMEGoalCommon::Process(trantype, dt, ctxt);
}

// No GC symbol (inlined into Process); out of line in the PS2 build.
inline S32 zNMEGoalSlamBattle::Chk_EarlyExit(zNMESlammer* npc, S32& nextgoal, en_trantype& trantype)
{
    en_vis vis;

    if (npc->model->Anim->Single->Blend->State)
    {
        return 0;
    }

    if (globals.player.s->Health < 1)
    {
        trantype = GOAL_TRAN_SET;
        nextgoal = NME_GOAL_TRIGGER_VINIVICIVIDI;
        npc->GoalPick(this, nextgoal, trantype);
        return 1;
    }

    if (globals.player.DamageTimer > npc->cfg_std->taunt.tym_keeplaffin)
    {
        trantype = GOAL_TRAN_PUSH;
        nextgoal = NME_GOAL_TRIGGER_VINIVICIVIDI;
        npc->GoalPick(this, nextgoal, trantype);
        flg_info |= 0x2;
        return 1;
    }

    if (NME::GlobalData::flg_wonder)
    {
        trantype = GOAL_TRAN_SET;
        nextgoal = NME_GOAL_IDLE;
        return 1;
    }

    if (!npc->arena.IncludesPos(xEntGetPos(xglobals->___player_ent_dont_use_directly), 0.0f, NULL))
    {
        trantype = GOAL_TRAN_SET;
        nextgoal = NME_GOAL_TRIGGER_ALERT;
        npc->GoalPick(this, nextgoal, trantype);
        return 1;
    }

    vis = npc->VisionTarget(xglobals->___player_ent_dont_use_directly, NME_DEFCON_NORMAL, 0);
    if (vis > VIS_I_SEEE_YOUUU)
    {
        trantype = GOAL_TRAN_SET;
        nextgoal = NME_GOAL_TRIGGER_ALERT;
        npc->GoalPick(this, nextgoal, trantype);
        return 1;
    }

    return 0;
}

S32 zNMEGoalSlamBattle::SubStateProcess(zNMESlammer* npc, F32 dt, S32& nextgoal,
                                        en_trantype& trantype)
{
    S32 doGoalTransition = 0;
    en_alertslam old_alertslam = alertslam;
    S32 subenter = flg_info & 0x2;

    flg_info &= ~0x6;

    switch (alertslam)
    {
    case SLAMMER_BATTLE_NOTICE:
        if (subenter)
        {
            DoAutoAnim(NME_GSPOT_RESUME, 0);
            npc->model->Anim->Single->BilinearLerp[0] = 1.0f;
        }
        alertslam = SLAMMER_BATTLE_CHASE;
        break;
    case SLAMMER_BATTLE_BEGIN:
        if (subenter)
        {
            DoAutoAnim(NME_GSPOT_RESUME, 0);
            npc->model->Anim->Single->BilinearLerp[0] = 1.0f;
        }
        alertslam = SLAMMER_BATTLE_CHASE;
        break;
    case SLAMMER_BATTLE_CHASE:
    {
        xVec3 pos_tgt;
        S32 rc;

        PredictPlyrPos(npc, &pos_tgt);
        npc->StraddleClamp(&pos_tgt);

        rc = CanIAttackNow(npc, &pos_tgt);
        if (rc && !npc->model->Anim->Single->Blend->State && !(globals.player.DamageTimer > 0.0f))
        {
            alertslam = SLAMMER_BATTLE_WHAM;
            break;
        }

        if (subenter)
        {
            DoAutoAnim(NME_GSPOT_STARTALT, 1);
            npc->model->Anim->Single->BilinearLerp[0] = 1.0f;
            tmr_alertham = 0.0f;
        }

        MoveChase(npc, dt, &pos_tgt);

        if (tmr_alertham < 0.0f)
        {
            tmr_alertham = 0.0f;
        }
        tmr_alertham += dt;
        if (tmr_alertham > 1.0f)
        {
            tmr_alertham = fmod(tmr_alertham, 1.0);
        }
        break;
    }
    case SLAMMER_BATTLE_WHAM:
        alertslam = SLAMMER_BATTLE_EVADE;
        trantype = GOAL_TRAN_PUSH;
        nextgoal = NME_GOAL_SLAMMER_ATTACK;
        doGoalTransition = 1;
        break;
    case SLAMMER_BATTLE_EVADE:
        if (subenter)
        {
            tmr_alertham = 0.25f;
        }

        if (tmr_alertham < 0.0f)
        {
            alertslam = SLAMMER_BATTLE_CHASE;
            break;
        }

        MoveEvade(npc, dt);
        npc->MatFacePlayer(dt, PI, 1);
        tmr_alertham = MAX(-1.0f, tmr_alertham - dt);

        if (subenter)
        {
            DoAutoAnim(NME_GSPOT_STARTALT, 0);
            npc->model->Anim->Single->BilinearLerp[0] = 1.0f;
        }
        break;
    }

    if (alertslam != old_alertslam)
    {
        flg_info |= 0x2;
    }

    return doGoalTransition;
}

// The normalized delta (toward the arena center) is unused in retail too; it survives because
// xVec3Copy is out of line. The position is updated per component, as retail re-reads model->Mat
// for each. dst and vel are not PS2 DWARF locals.
void zNMEGoalSlamBattle::MoveEvade(zNMESlammer* npc, F32 dt)
{
    // Not in PS2 DWARF order (dir_dest, cfg, delta); declaring them in that order changes the code.
    xVec3 delta;
    const NMECfgSlammer* cfg;
    xVec3 dir_dest;
    F32 dst;

    xVec3Sub(&delta, &npc->arena.pos_arena, xEntGetPos(npc));
    dst = xVec3Length(&delta);
    if (dst < 1.0f)
    {
        xVec3Copy(&delta, (xVec3*)&npc->model->Mat->right);
    }
    else
    {
        xVec3SMulBy(&delta, 1.0f / dst);
    }

    xVec3Sub(&dir_dest, xEntGetPos(xglobals->___player_ent_dont_use_directly), xEntGetPos(npc));
    dst = xVec3Length(&dir_dest);
    if (dst < 0.5f)
    {
        dir_dest = *(xVec3*)&xglobals->___player_ent_dont_use_directly->model->Mat->right;
    }
    else
    {
        xVec3SMulBy(&dir_dest, 1.0f / dst);
    }

    xVec3SMulBy(&dir_dest, -1.0f);
    cfg = npc->cfg_slammer;
    npc->ThrottleAdjust(dt, cfg->chase.spd_chase, cfg->chase.acc_chase);
    xVec3 vel = dir_dest * (dt * npc->spd_throttle);
    xEntGetPos(npc)->x += vel.x;
    xEntGetPos(npc)->y += vel.y;
    xEntGetPos(npc)->z += vel.z;
}

// No symbol in TSSM or the PS2 DWARF (inlined into SubStateProcess); the name and shape follow
// zNMEGoalFogBattle::PredictPlyrPos.
inline void zNMEGoalSlamBattle::PredictPlyrPos(zNMESlammer* npc, xVec3* pos_plyr)
{
    const NMECfgSlammer* cfg = npc->cfg_slammer;
    F32 tym_lead = xsqrt(npc->XYZDstSqToPlayer(NULL)) / cfg->chase.spd_chase;

    zEntPlayer_PredictPos(pos_plyr, MIN(tym_lead, cfg->predict.tym_leadMax), cfg->predict.fac_lead,
                          cfg->predict.flg_useTurn);
}

// No GC symbol (inlined into SubStateProcess); out of line in the PS2 build. dst_tgt is not a
// PS2 DWARF local; the length is computed once and used three times.
inline S32 zNMEGoalSlamBattle::CanIAttackNow(zNMESlammer* npc, const xVec3* pos_tgt)
{
    const NMECfgSlammer* cfg;
    xVec3 vec_tgt;
    F32 dy;
    F32 dst_tgt;

    cfg = npc->cfg_slammer;
    npc->XZVecToPos(&vec_tgt, pos_tgt, &dy);
    dst_tgt = vec_tgt.length();

    if (dy > cfg->wedge.hyt_wedge)
    {
        return 0;
    }
    if (dy < -cfg->wedge.hyt_wedge)
    {
        return 0;
    }
    if (dst_tgt < 0.25f)
    {
        return 1;
    }
    if (dst_tgt > cfg->wedge.rad_wedge)
    {
        return 0;
    }

    xVec3 dir_tgt = vec_tgt * (1.0f / dst_tgt);
    if (xVec3Dot((xVec3*)&npc->model->Mat->at, &dir_tgt) < cfg->wedge.dot_wedge)
    {
        return 0;
    }

    return 1;
}

void zNMEGoalSlamBattle::MoveChase(zNMESlammer* npc, F32 dt, const xVec3* pos_tgt)
{
    const NMECfgSlammer* cfg = npc->cfg_slammer;
    xVec3 dir_tgt;
    F32 dst_tgt;

    if (npc->XZDstSqToPos(pos_tgt, NULL, NULL) < 6.25f)
    {
        // not a DWARF local: the slowed speed goes through a local, as the Flinger/Mervyn move
        // goals pass a 'spd' local to ThrottleAdjust
        F32 spd = 0.25f * cfg->chase.spd_chase;
        npc->ThrottleAdjust(dt, spd, cfg->chase.acc_chase);
    }
    else
    {
        npc->ThrottleAdjust(dt, cfg->chase.spd_chase, cfg->chase.acc_chase);
    }

    npc->MatMoveToPos(pos_tgt, dt, npc->spd_throttle, 1, cfg->chase.spd_turnRate);

    npc->XZVecToPos(&dir_tgt, pos_tgt, NULL);
    dst_tgt = xVec3Length(&dir_tgt);
    if (dst_tgt > 0.1f)
    {
        xVec3SMulBy(&dir_tgt, 1.0f / dst_tgt);
        npc->model->Anim->Single->BilinearLerp[0] = CalcLean(npc, dt, &dir_tgt);
    }
}

// Compiled-out asserts (conditions unknown). With them the function is too big to be
// auto-inlined into MoveChase; retail calls it.
F32 zNMEGoalSlamBattle::CalcLean(zNMESlammer* npc, F32 dt, const xVec3* dir_dest)
{
    xASSERT_PLACEHOLDER();
    xASSERT_PLACEHOLDER();
    xASSERT_PLACEHOLDER();
    xASSERT_PLACEHOLDER();
    xASSERT_PLACEHOLDER();
    xASSERT_PLACEHOLDER();
    xASSERT_PLACEHOLDER();
    xASSERT_PLACEHOLDER();

    F32 rat_oldLean = npc->model->Anim->Single->BilinearLerp[0];
    xVec3 dir_plyr;
    F32 dst_plyr;
    F32 ang_plyr;
    F32 diffAngle;
    F32 desiredLean;

    npc->XZVecToPos(&dir_plyr, xEntGetPos(xglobals->___player_ent_dont_use_directly), NULL);
    dst_plyr = xsqrt(xVec3Length2(&dir_plyr));
    if (dst_plyr < 0.00001f)
    {
        desiredLean = rat_oldLean - 1.0f;
        desiredLean *= 0.95f;
        desiredLean += 1.0f;
        return CLAMP(desiredLean, 0.0f, 2.0f);
    }

    xVec3SMulBy(&dir_plyr, 1.0f / dst_plyr);
    ang_plyr = xatan2(dir_plyr.x, dir_plyr.z);
    diffAngle = xDangleClamp(ang_plyr - xatan2(dir_dest->x, dir_dest->z));

    desiredLean = xabs(diffAngle);
    if (desiredLean < 0.08726646f)
    {
        desiredLean = 0.0f;
    }
    else if (desiredLean > 0.7853982f)
    {
        desiredLean = 1.0f;
    }
    else
    {
        desiredLean = 1.4323944f * (desiredLean - 0.08726646f);
    }

    if (diffAngle > 0.0f)
    {
        desiredLean = -desiredLean;
    }

    desiredLean += 1.0f;
    return rat_oldLean + dt * (6.0f * (desiredLean - rat_oldLean));
}

S32 zNMEGoalSlamAttack::Enter(F32 dt, void* updCtxt)
{
    zNMESlammer* npc = (zNMESlammer*)psyche->clt_owner;

    flg_hitTarget = 0;
    flg_hitPlayer = 0;
    flg_didEffect = 0;
    flg_needWave = 1;

    RibbonPrep(npc);

    pos_lastVert = *(xVec3*)&npc->model->Mat[20].pos;
    xMat3x3RMulVec(&pos_lastVert, (xMat3x3*)npc->model->Mat, &pos_lastVert);
    pos_lastVert += *(xVec3*)&npc->model->Mat->pos;
    pos_oldVert = pos_lastVert;

    return zNMEGoalPushAnim::Enter(dt, updCtxt);
}

S32 zNMEGoalSlamAttack::Exit(F32 dt, void* updCtxt)
{
    if (ribhorz != NULL)
    {
        zFXRibbonPoolDelete(ribhorz);
    }
    ribhorz = NULL;

    if (ribvert != NULL)
    {
        zFXRibbonPoolDelete(ribvert);
    }
    ribvert = NULL;

    return zNMEGoalPushAnim::Exit(dt, updCtxt);
}

S32 zNMEGoalSlamAttack::Process(en_trantype* trantype, F32 dt, void* ctxt)
{
    S32 nextgoal = 0;
    zNMESlammer* npc = (zNMESlammer*)psyche->clt_owner;

    S32 doTran = ChkPrelimTran(npc, nextgoal, *trantype);
    if (doTran)
    {
        return nextgoal;
    }

    npc->AnimCurSingle()->CurrentSpeed = 1.5f;
    npc->ThrottleAdjust(dt, 0.0f, 10.0f);

    xVec3 delta = ((xMat4x3*)npc->model->Mat)->at * (dt * npc->spd_throttle);
    npc->model->Mat->pos.x += delta.x;
    npc->model->Mat->pos.y += delta.y;
    npc->model->Mat->pos.z += delta.z;

    DoCapsuleTests(npc);

    if (flg_needWave)
    {
        S32 rc = WaveChecks(npc);
        if (rc)
        {
            flg_needWave = 0;
        }
    }

    RibbonStep(npc);
    return zNMEGoalPushAnim::Process(trantype, dt, ctxt);
}

// No GC symbol (inlined into Process); out of line in the PS2 build.
inline S32 zNMEGoalSlamAttack::ChkPrelimTran(zNMESlammer* npc, S32& nextgoal, en_trantype& trantype)
{
    if (globals.player.s->Health < 1)
    {
        trantype = GOAL_TRAN_SET;
        nextgoal = NME_GOAL_TRIGGER_VINIVICIVIDI;
        npc->GoalPick(this, nextgoal, trantype);
        return 1;
    }

    if (npc->AnimTimeRemain(NULL) < 0.15f &&
        globals.player.DamageTimer > npc->cfg_std->taunt.tym_keeplaffin)
    {
        trantype = GOAL_TRAN_SWAP;
        nextgoal = NME_GOAL_TRIGGER_VINIVICIVIDI;
        npc->GoalPick(this, nextgoal, trantype);
        return 1;
    }

    if (NME::GlobalData::flg_wonder)
    {
        trantype = GOAL_TRAN_SET;
        nextgoal = NME_GOAL_TRIGGER_NORMAL;
        npc->GoalPick(this, nextgoal, trantype);
        return 1;
    }

    return 0;
}

S32 zNMEGoalSlamAttack::WaveChecks(zNMESlammer* npc)
{
    F32 tym_animCurr;
    zNMEHazard* haz;

    tym_animCurr = npc->model->Anim->Single->Time;
    if (tym_animCurr < SlamAttack::tym_kickWave[0])
    {
        return 0;
    }

    if (tym_animCurr > SlamAttack::tym_kickWave[1])
    {
        return 1;
    }

    haz = HAZ_Acquire();
    if (haz == NULL)
    {
        return 0;
    }

    en_haztyp htyplist[10] = {
        HAZ_TYP_SLAMWAVE_V1,
        HAZ_TYP_SLAMWAVE_V1,
        HAZ_TYP_SLAMWAVE_V2,
        HAZ_TYP_SLAMWAVE_V3,
    };
    en_nmevar vidx = NMEVariantIdx((en_npctyp)npc->itemType);
    S32 rc = haz->ConfigHelper(htyplist[vidx]);
    if (!rc)
    {
        haz->Discard();
        return 0;
    }

    haz->npc_owner = npc;

    xVec3 pos_emit = *(xVec3*)&npc->model->Mat[20].pos;
    xMat4x3Toworld(&pos_emit, (xMat4x3*)npc->model->Mat, &pos_emit);
    haz->Start(&pos_emit, 0.0f);

    return 1;
}

void zNMEGoalSlamAttack::DoCapsuleTests(zNMESlammer* npc)
{
    F32 tym_animCurr;
    xVec3 pos_head;
    xVec3 pos_hand;
    xSweptSphere sws;

    tym_animCurr = npc->model->Anim->Single->Time;
    if (!(tym_animCurr < SlamAttack::tym_animDamage[0]) &&
        !(tym_animCurr > SlamAttack::tym_animDamage[1]))
    {
        pos_head = *(xVec3*)&npc->model->Mat[20].pos;
        pos_hand = *(xVec3*)&npc->model->Mat[16].pos;
        xMat3x3RMulVec(&pos_head, (xMat3x3*)npc->model->Mat, &pos_head);
        xMat3x3RMulVec(&pos_hand, (xMat3x3*)npc->model->Mat, &pos_hand);
        xVec3AddTo(&pos_head, (xVec3*)&npc->model->Mat->pos);
        xVec3AddTo(&pos_hand, (xVec3*)&npc->model->Mat->pos);

        memset(&sws, 0, sizeof(xSweptSphere));
        xSweptSpherePrepare(&sws, &pos_hand, &pos_head, 0.75f);

        if (!flg_hitPlayer &&
            xSweptSphereToSphere(&sws, &xglobals->___player_ent_dont_use_directly->bound.sph))
        {
            zEntPlayer_DamageNPCKnockBack(npc, 1, &npc->bound.sph.center, ePlayerNMEDamageDefault);
            flg_hitPlayer = 1;
        }
    }
}

// Placeholders for two dead-stripped functions: retail keeps these literals, the totypes object
// and two .rodata templates here, but nothing uses them and the bodies are unknown. totypes
// matches BfBB's zNPCGoalAttackHammer::TellBunnies, which notifies with an 8.0f radius.
static void __deadstripped2_zNMETypeStdSlammer(F32* out)
{
    F32 unknown[4] = { 1.0f, 0.0f, 0.0f, 0.0f };
    out[0] = 0.85f;
    out[1] = 1.15f;
}

static void __deadstripped_zNMETypeStdSlammer(F32* out)
{
    static en_npctyp totypes[4] = { NME_TYPE_FOGGER_V1, NME_TYPE_FOGGER_V2, NME_TYPE_FOGGER_V3,
                                    NME_TYPE_UNKNOWN };
    xVec3 unknown = {};
    out[0] = 8.0f;
}

void zNMEGoalSlamAttack::RibbonPrep(zNMESlammer* npc)
{
    S32 vidx;
    NMEHelpRib::RibData* ribhelp;

    vidx = NMEVariantIdx((en_npctyp)npc->itemType);
    ribhelp = SlamAttack::SwipeFX::rib_swipe[vidx];
    if (ribhelp->flags.enable || ribhelp->flags.dev_enable)
    {
        ribhorz = zFXRibbonPoolNew();
        ribvert = zFXRibbonPoolNew();

        if (ribhorz != NULL)
        {
            ribhelp->ConfigRibbon(ribhorz);
        }
        if (ribvert != NULL)
        {
            ribhelp->ConfigRibbon(ribvert);
        }

        cnt_skipRibbonJoint = (S8)ribhelp->updinfo.frq_ribJoint;
    }
}

void zNMEGoalSlamAttack::RibbonStep(zNMESlammer* npc)
{
    F32 tym_animCurr;
    S32 vidx;
    NMEHelpRib::RibData* ribhelp;

    tym_animCurr = npc->model->Anim->Single->Time;
    if (!(tym_animCurr < SlamAttack::tym_ribemit[0]) &&
        !(tym_animCurr > SlamAttack::tym_ribemit[1]))
    {
        cnt_skipRibbonJoint--;
        if (cnt_skipRibbonJoint <= 0)
        {
            vidx = NMEVariantIdx((en_npctyp)npc->itemType);
            ribhelp = SlamAttack::SwipeFX::rib_swipe[vidx];
            cnt_skipRibbonJoint = (S8)ribhelp->updinfo.frq_ribJoint;

            if (ribvert != NULL)
            {
                RibbonVert(npc, ribvert, ribhelp);
            }
        }
    }
}

void zNMEGoalSlamAttack::RibbonVert(zNMESlammer* npc, xFXRibbon* rib, NMEHelpRib::RibData* ribhelp)
{
    xVec3 pos_head;
    xVec3 pos_hand;
    xMat4x3* mat_root;

    if (ribhelp->flags.dev_enable)
    {
        ribhelp->ConfigRibbon(rib);
    }

    mat_root = (xMat4x3*)npc->model->Mat;
    pos_head = *(xVec3*)&npc->model->Mat[20].pos;
    pos_hand = *(xVec3*)&npc->model->Mat[16].pos;
    xMat4x3Toworld(&pos_hand, mat_root, &pos_hand);
    xMat4x3Toworld(&pos_head, mat_root, &pos_head);

    xVec3 dir_emit = pos_hand - pos_head;
    F32 dst = xVec3Normalize(&dir_emit, &dir_emit);
    rib->insert(pos_head, dir_emit, dst * ribhelp->updinfo.scl_master, ribhelp->updinfo.alf_master,
                0);
}
