#include "zNMETypeStdSpinner.h"
#include "iCollide.h"

namespace Spinner
{
    namespace Config
    {
        static const NMECfgSpinner cfg_spinner_v1 = {
            { 4 },
            { 2.0f },
        };
        static const NMECfgSpinner cfg_spinner_v2 = {
            { 5 },
            { 2.0f },
        };
        static const NMECfgSpinner cfg_spinner_v3 = {
            { 7 },
            { 2.0f },
        };
        const NMECfgSpinner* cfg_spin[10] = { &cfg_spinner_v1, &cfg_spinner_v1, &cfg_spinner_v2,
                                              &cfg_spinner_v3 };
    } // namespace Config

    // Odds of the outcomes in choices when an aware spinner loses sight of the player, per
    // variant (indexed by NMEVariantIdx).
    namespace Aware
    {
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

    NMEHelpBound::BndData bnd_infoBigMan = {
        k_XBOUNDTYPE_SPHERE,
        { 0, 0, { 0, 0 } },
        { 2.5f, { 0.0f, 2.5f, 0.0f } },
        { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } },
    };
    NMEHelpBound::BndData bnd_info = {
        k_XBOUNDTYPE_SPHERE,
        { 0, 0, { 0, 0 } },
        { 1.0f, { 0.0f, 1.0f, 0.0f } },
        { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } },
    };
} // namespace Spinner

namespace SpinBattle
{
    namespace FX
    {
        namespace Bubbles
        {
            S8 enable = 1;
            S8 frq_emit = 1;
            S8 num_emit = 1;
        } // namespace Bubbles
    } // namespace FX

    namespace Dust
    {
        xVec3 vec_emitOffset = { 0.0f, 0.15f, 0.0f };
        PuffEmitter dustWhirl = {
            NPAR_TYP_DUSTPUFF,
            0.2f,
            { 0x64, 0x64, 0x64, 0xC8 },
            { 0.25f, 0.6f },
            { 0.0f, 2.0f, 0.0f },
            3.84f,
            93,
            0,
            20,
            0,
            { 1.5f, 7.5f },
        };
    } // namespace Dust

    namespace WormHead
    {
        static const xFXRibbon::curve_node curvedata[4] = {
            { 0.0f, { 0xFF, 0x00, 0x00, 0xFF }, 1.0f },
            { 0.35f, { 0xFF, 0x46, 0x46, 0xC8 }, 0.9f },
            { 0.8f, { 0xFF, 0x8C, 0x46, 0x96 }, 0.3f },
            { 1.0f, { 0xFF, 0xFF, 0x00, 0x32 }, 0.0f },
        };

        NMEHelpRib::RibData rib_WormHead = {
            { 1, 1, 0, 0 },
            { "fx_spinner_swirl", (xFXRibbon::curve_node*)curvedata, 4, 0.35f, 0.6f },
            { 1, 1.0f, 1.0f },
            { NULL, 0 },
        };
    } // namespace WormHead
} // namespace SpinBattle

void zNMESpinner::CreateConfig(const char* name)
{
    en_nmevar vidx;

    zNMEStandard::CreateConfig(name);
    vidx = NMEVariantIdx((en_npctyp)itemType);
    cfg_spinner = Spinner::Config::cfg_spin[vidx];
}

S32 zNMESpinner::DfltVulnFlags()
{
    return 0xFFFFFFF7;
}

U8 zNMESpinner::ColChkFlags() const
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

U8 zNMESpinner::ColPenFlags() const
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

void zNMESpinner::Init(xEntAsset* entass)
{
    zNMEStandard::Init(entass);
    flg_move |= 0x2;
    zNMEParFX_PartyInvite(NPAR_TYP_DUSTPUFF, NULL, 0);
}

void zNMESpinner::Setup()
{
    zNMEStandard::Setup();
    SpinBattle::WormHead::rib_WormHead.FindTexture();
}

void zNMESpinner::Reset()
{
    xPsyche* psy;
    zNMEGoalAfterlife* goal;
    en_trantype trantype;
    S32 nextgoal;

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
        trantype = GOAL_TRAN_SET;
        nextgoal = NME_GOAL_TRIGGER_NORMAL;
        GoalPick(NULL, nextgoal, trantype);
        psy->GoalSet(nextgoal, 1);
    }

    pts_health = cfg_spinner->health.pts_healthMax;
}

void zNMESpinner::SelfSetup()
{
    xBehaveMgr* bmgr;
    xPsyche* psy;
    xGoal* goal;
    xGoal* g;
    en_trantype trantype;
    S32 nextgoal;

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
    g->fun_process = Spin_SeePlyr;
    g->cbdata = NULL;
    g = psy->FindGoal(NME_GOAL_PATROL);
    g->fun_process = Spin_SeePlyr;
    g->cbdata = NULL;
    g = psy->FindGoal(NME_GOAL_WANDER);
    g->fun_process = Spin_SeePlyr;
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
    goal->fun_process = Spin_SeePlyr;
    goal->cbdata = NULL;

    psy->AddGoal(NME_GOAL_SPINNER_AWARE, NULL);
    psy->AddGoal(NME_GOAL_SPINNER_BATTLE, NULL);

    psy->BrainEnd();

    trantype = GOAL_TRAN_SET;
    nextgoal = NME_GOAL_TRIGGER_NORMAL;
    GoalPick(NULL, nextgoal, trantype);
    psy->SetSafety(nextgoal);
}

U32 zNMESpinner::AnimPick(S32 gid, en_npcgspot gspot, xGoal* goal)
{
    S32 idx;
    U32 da_anim = 0;

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
    case NME_GOAL_SPINNER_AWARE:
        idx = STAN_ANIM_ALERT;
        break;
    case NME_GOAL_SPINNER_BATTLE:
        if (gspot == NME_GSPOT_START)
        {
            idx = STAN_ANIM_ATTACK_PREP;
        }
        else if (gspot == NME_GSPOT_RESUME)
        {
            idx = STAN_ANIM_ATTACK_PREP;
        }
        else if (gspot == NME_GSPOT_LOOP)
        {
            idx = STAN_ANIM_ATTACK_LOOP;
        }
        else if (gspot == NME_GSPOT_FINISH)
        {
            idx = STAN_ANIM_ATTACK_DONE;
        }
        else
        {
            idx = STAN_ANIM_ATTACK_PREP;
        }
        break;
    default:
        idx = STAN_ANIM_IDLE;
        break;
    }

    if (idx >= 0)
    {
        da_anim = g_hash_stananim[idx];
    }

    return da_anim;
}

xAnimTable* zNMESpinner::CreateAnimTable(xAnimTable* table)
{
    if (table == NULL)
    {
        table = xAnimTableNew("zNMESpinner", 0);
    }

    xAnimTableNewState(table, g_strz_stananim[STAN_ANIM_IDLE], 0x10, 1, 1.0f, NULL, NULL, 0.0f,
                       NULL, NULL, xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, g_strz_stananim[STAN_ANIM_MOVE], 0x10, 1, 1.0f, NULL, NULL, 0.0f,
                       NULL, NULL, xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, g_strz_stananim[STAN_ANIM_ALERT], 0x10, 1, 1.0f, NULL, NULL, 0.0f,
                       NULL, NULL, xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, g_strz_stananim[STAN_ANIM_CHASE], 0x10, 1, 1.0f, NULL, NULL, 0.0f,
                       NULL, NULL, xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, g_strz_stananim[STAN_ANIM_SCARE_LOOP], 0x10, 1, 1.0f, NULL, NULL,
                       0.0f, NULL, NULL, xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, g_strz_stananim[STAN_ANIM_SCARE_DONE], 0, 1, 1.0f, NULL, NULL, 0.0f,
                       NULL, NULL, xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, g_strz_stananim[STAN_ANIM_SPOOK_LOOP], 0x10, 1, 1.0f, NULL, NULL,
                       0.0f, NULL, NULL, xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, g_strz_stananim[STAN_ANIM_NOTICE], 0, 1, 1.0f, NULL, NULL, 0.0f, NULL,
                       NULL, xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, g_strz_stananim[STAN_ANIM_TAUNT], 0x10, 1, 1.0f, NULL, NULL, 0.0f,
                       NULL, NULL, xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, g_strz_stananim[STAN_ANIM_VICTORY], 0x10, 1, 1.0f, NULL, NULL, 0.0f,
                       NULL, NULL, xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, g_strz_stananim[STAN_ANIM_DAMAGE], 0x10, 1, 1.0f, NULL, NULL, 0.0f,
                       NULL, NULL, xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, g_strz_stananim[STAN_ANIM_DEFEAT], 0x10, 1, 1.0f, NULL, NULL, 0.0f,
                       NULL, NULL, xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, g_strz_stananim[STAN_ANIM_ATTACK_PREP], 0x20, 1, 1.0f, NULL, NULL,
                       0.0f, NULL, NULL, xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, g_strz_stananim[STAN_ANIM_ATTACK_LOOP], 0x10, 1, 1.0f, NULL, NULL,
                       0.0f, NULL, NULL, xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, g_strz_stananim[STAN_ANIM_ATTACK_DONE], 0, 1, 1.0f, NULL, NULL, 0.0f,
                       NULL, NULL, xAnimDefaultBeforeEnter, NULL, NULL);
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

    S32 ourAnims[24] = { STAN_ANIM_IDLE,        STAN_ANIM_MOVE,        STAN_ANIM_ALERT,
                         STAN_ANIM_CHASE,       STAN_ANIM_SCARE_LOOP,  STAN_ANIM_SCARE_DONE,
                         STAN_ANIM_SPOOK_LOOP,  STAN_ANIM_TAUNT,       STAN_ANIM_VICTORY,
                         STAN_ANIM_DAMAGE,      STAN_ANIM_DEFEAT,      STAN_ANIM_ATTACK_PREP,
                         STAN_ANIM_ATTACK_LOOP, STAN_ANIM_ATTACK_DONE, STAN_ANIM_STUN_PREP,
                         STAN_ANIM_STUN_LOOP,   STAN_ANIM_STUN_PICKUP, STAN_ANIM_STUN_CARRY,
                         STAN_ANIM_TOSS_PREP,   STAN_ANIM_TOSS_LOOP,   STAN_ANIM_SPIN_PREP,
                         STAN_ANIM_SPIN_LOOP,   STAN_ANIM_SPIN_THROW,  STAN_ANIM_UNKNOWN };
    NMEC_BuildStandardAnimTran(table, g_strz_stananim, ourAnims, 3, 0.2f);

    xAnimTableNewTransition(table, g_strz_stananim[STAN_ANIM_ATTACK_PREP],
                            g_strz_stananim[STAN_ANIM_ATTACK_LOOP], NULL, NULL, 0x10, 0, 0.0f, 0.0f,
                            0, 0, 0.2f, NULL);
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

void zNMESpinner::BUpdate(xVec3* pos)
{
    if (flg_useBigRadius)
    {
        Spinner::bnd_infoBigMan.UpdateBound(&bound, this, pos);
    }
    else
    {
        Spinner::bnd_info.UpdateBound(&bound, this, pos);
    }
}

void zNMESpinner::Process(F32 dt)
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
        flg_useBigRadius = 1;
        StdTyp_Process(dt);
        flg_useBigRadius = 0;
        BUpdate(xEntGetPos(this));
    }

    StdNME_SuicideIsPainless();
}

void zNMESpinner::Type_AddYourMove(F32 dt)
{
    if (flg_colCheck)
    {
        // Not a PS2 DWARF local: the product is not fused into the subtraction (as in
        // zNMESlammer::Type_AddYourMove).
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
    else if (vel_physics.y < 0.0f)
    {
        vel_physics.y = 0.0f;
    }
}

void zNMESpinner::CollideReview()
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

S32 zNMESpinner::GoalPick(xGoal* goal, S32& gid_would, en_trantype& trantype)
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
    case NME_GOAL_TRIGGER_VINIVICIVIDI:
        gid_would = NME_GOAL_TAUNT;
        break;
    case NME_GOAL_TRIGGER_DETECT:
        gid_would = NME_GOAL_NOTICE;
        break;
    case NME_GOAL_TRIGGER_ALERT:
        gid_would = NME_GOAL_SPINNER_AWARE;
        break;
    case NME_GOAL_TRIGGER_BATTLE:
        gid_would = NME_GOAL_SPINNER_BATTLE;
        break;
    case NME_GOAL_TRIGGER_ATTACK:
        gid_would = NME_GOAL_SPINNER_BATTLE;
        break;
    default:
        altered = 0;
        break;
    }

    return altered;
}

// The goals of a spinner Patrick has stunned, carries, twirls or throws. Unreferenced in the GC
// build, but kept in retail's .rodata.
static const en_npcgol gid_stunlist[6] = {
    NME_GOAL_EVILPAT,  NME_GOAL_STUNNED,  NME_GOAL_PATCARRY,
    NME_GOAL_PATTWIRL, NME_GOAL_PATTHROW, NME_GOAL_UNKNOWN,
};

S32 zNMESpinner::TypeHandleMail(NMEMsg* mail)
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
        handled = SpinMailDamage(&mail->dmgdata);
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
inline S32 zNMESpinner::SpinMailDamage(NMEDamageInfo* dmgmail)
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

en_vis zNMESpinner::VisionTarget(xEnt* ent, en_defcon defcon, S32 flags) const
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

    if (defcon == NME_DEFCON_NORMAL)
    {
        if (runtimeData.overrideDetect == ALLOW_ALWAYS)
        {
            return VIS_I_SEEE_YOUUU;
        }

        if (arena->IncludesPos(xEntGetPos(ent), 5.0f, NULL))
        {
            return VIS_I_SEEE_YOUUU;
        }

        return VIS_NOBODY_AROUND;
    }
    else if (defcon == NME_DEFCON_AWARE)
    {
        if (arena->IncludesPos(xEntGetPos(ent), 0.0f, NULL))
        {
            return VIS_PREPARE_TO_DIE;
        }

        if (runtimeData.overrideDetect == ALLOW_ALWAYS)
        {
            return VIS_GREY_AND_FOGGY;
        }

        if (arena->IncludesPos(xEntGetPos(ent), 5.0f, NULL))
        {
            return VIS_GREY_AND_FOGGY;
        }

        return VIS_NOBODY_AROUND;
    }
    else if (defcon == NME_DEFCON_BATTLE)
    {
        if (arena->IncludesPos(xEntGetPos(ent), 0.0f, NULL))
        {
            return VIS_PREPARE_TO_DIE;
        }

        if (arena->IncludesPos(xEntGetPos(ent), 5.0f, NULL))
        {
            return VIS_GREY_AND_FOGGY;
        }

        return VIS_NOBODY_AROUND;
    }

    return VIS_NOBODY_AROUND;
}

F32 zNMESpinner::HealthRatio()
{
    F32 rat;

    if (cfg_spinner == NULL)
    {
        return 0.0f;
    }

    rat = pts_health;
    rat /= cfg_spinner->health.pts_healthMax;
    rat = CLAMP(rat, 0.0f, 1.0f);
    return rat;
}

S32 zNMESpinner::Spin_SeePlyr(xGoal* rawgoal, void* data, en_trantype* trantype, F32 dt, void* ctxt)
{
    S32 nextgoal = 0;
    zNMESpinner* npc;
    zNMEArena* arena;
    en_vis vis;
    // Not a PS2 DWARF local (BfBB's zNPCRobot::Reset has the same one); the result is tested
    // for 2 and then for nonzero.
    S32 rc;

    if (xUtil_yesno(0.25f))
    {
        return nextgoal;
    }

    if (globals.player.s->Health < 1)
    {
        return nextgoal;
    }

    npc = (zNMESpinner*)rawgoal->psyche->clt_owner;

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
    if (vis >= VIS_GREY_AND_FOGGY)
    {
        return nextgoal;
    }

    *trantype = GOAL_TRAN_SET;
    nextgoal = (npc->tmr_common[2] < 0.0f) ? NME_GOAL_TRIGGER_DETECT : NME_GOAL_TRIGGER_ALERT;
    npc->GoalPick(rawgoal, nextgoal, *trantype);

    return nextgoal;
}

S32 zNMEGoalSpinAware::Enter(F32 dt, void* updCtxt)
{
    zNMESpinner* npc = (zNMESpinner*)psyche->clt_owner;

    npc->arena.Cycle(npc, 1);
    return zNMEGoalCommon::Enter(dt, updCtxt);
}

S32 zNMEGoalSpinAware::Process(en_trantype* trantype, F32 dt, void* ctxt)
{
    S32 nextgoal = 0;
    zNMESpinner* npc = (zNMESpinner*)psyche->clt_owner;
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

S32 zNMEGoalSpinAware::ChkTranCases(zNMESpinner* npc, S32& nextgoal, en_trantype& trantype)
{
    en_vis vis;
    S32 vidx;
    const F32* wtlist;

    if (npc->model->Anim->Single->Blend->State != NULL)
    {
        return 0;
    }

    if (xUtil_yesno(0.25f))
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
    else if (globals.player.DamageTimer > npc->cfg_std->taunt.tym_keeplaffin)
    {
        trantype = GOAL_TRAN_PUSH;
        nextgoal = NME_GOAL_TRIGGER_VINIVICIVIDI;
        npc->GoalPick(this, nextgoal, trantype);
        return 1;
    }

    vis = npc->VisionTarget(xglobals->___player_ent_dont_use_directly, NME_DEFCON_AWARE, 0);
    if (vis < VIS_I_SEEE_YOUUU)
    {
        trantype = GOAL_TRAN_SET;
        nextgoal = NME_GOAL_TRIGGER_BATTLE;
        npc->GoalPick(this, nextgoal, trantype);
        return 1;
    }
    else if (vis > VIS_GREY_AND_FOGGY)
    {
        vidx = NMEVariantIdx((en_npctyp)npc->itemType);
        wtlist = Spinner::Aware::wt_person[vidx];
        vidx = xUtil_choose(Spinner::Aware::choices, 3, wtlist);
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

S32 zNMEGoalSpinBattle::Enter(F32 dt, void* updCtxt)
{
    zNMESpinner* npc = (zNMESpinner*)psyche->clt_owner;
    // not a DWARF local (see Resume)
    NMEHelpRib::RibData* ribhelp;

    npc->flg_vuln &= ~0x100000;
    npc->flg_vuln &= ~0x40000000;
    npc->flg_vuln &= ~0x82010000;

    tmr_minAttack = 0.75f;

    npc->SndPlayFromTable(2); // eNMESound_Attack

    ribhelp = &SpinBattle::WormHead::rib_WormHead;
    if (ribhelp->flags.enable || ribhelp->flags.dev_enable)
    {
        ribbon = zFXRibbonPoolNew();
        if (ribbon != NULL)
        {
            ribhelp->ConfigRibbon(ribbon);
            cnt_skipRibbonJoint = ribhelp->updinfo.frq_ribJoint;
        }
    }

    return zNMEGoalCommon::Enter(dt, updCtxt);
}

S32 zNMEGoalSpinBattle::Exit(F32 dt, void* updCtxt)
{
    zNMESpinner* npc = (zNMESpinner*)psyche->clt_owner;

    npc->flg_vuln = npc->DfltVulnFlags();

    if (ribbon != NULL)
    {
        zFXRibbonPoolDelete(ribbon);
        ribbon = NULL;
    }

    if (npc->snd_table->sndGroupHandle[2] != -1)
    {
        xSndMgrStop(npc->sndID[2]);
        npc->sndID[2] = (iSndHandle)-1;
    }

    return 0;
}

S32 zNMEGoalSpinBattle::Suspend(F32 dt, void* updCtxt)
{
    zNMESpinner* npc = (zNMESpinner*)psyche->clt_owner;

    npc->flg_vuln = npc->DfltVulnFlags();

    if (ribbon != NULL)
    {
        zFXRibbonPoolDelete(ribbon);
        ribbon = NULL;
    }

    if (npc->snd_table->sndGroupHandle[2] != -1)
    {
        xSndMgrStop(npc->sndID[2]);
        npc->sndID[2] = (iSndHandle)-1;
    }

    return 0;
}

S32 zNMEGoalSpinBattle::Resume(F32 dt, void* updCtxt)
{
    zNMESpinner* npc = (zNMESpinner*)psyche->clt_owner;
    // not a DWARF local (the PS2 build has the ribbon block as one inline call); retail keeps
    // rib_WormHead's address in a saved register
    NMEHelpRib::RibData* ribhelp;

    tmr_minAttack = 0.75f;

    npc->flg_vuln &= ~0x100000;
    npc->flg_vuln &= ~0x40000000;
    npc->flg_vuln &= ~0x82010000;

    npc->SndPlayFromTable(2); // eNMESound_Attack

    ribhelp = &SpinBattle::WormHead::rib_WormHead;
    if (ribhelp->flags.enable || ribhelp->flags.dev_enable)
    {
        ribbon = zFXRibbonPoolNew();
        if (ribbon != NULL)
        {
            ribhelp->ConfigRibbon(ribbon);
            cnt_skipRibbonJoint = ribhelp->updinfo.frq_ribJoint;
        }
    }

    return zNMEGoalCommon::Resume(dt, updCtxt);
}

// No symbol in TSSM or the PS2 DWARF (always inlined); the name is ours. The PS2 line table has
// the whole bubble block on one line of Process.
static inline void SpinBattle_BubbleLoop(zNMEGoalSpinBattle* goal, zNMESpinner* npc)
{
    if (SpinBattle::FX::Bubbles::enable)
    {
        goal->cnt_bubemit--;
        if (goal->cnt_bubemit <= 0)
        {
            goal->cnt_bubemit = SpinBattle::FX::Bubbles::frq_emit;
            xVec3 pos_emit = *(xVec3*)&npc->model->Mat[27].pos;
            pos_emit += *(xVec3*)&npc->model->Mat->pos;
            zFX_SpawnBubbleTrail(&pos_emit, SpinBattle::FX::Bubbles::num_emit);
        }
    }
}

// No symbol in TSSM or the PS2 DWARF (always inlined); the name is ours. The PS2 line table has
// the animation test and dust emission on one line of Process.
static inline void SpinBattle_DustLoop(zNMEGoalSpinBattle* goal, zNMESpinner* npc)
{
    U32 anid_cur = npc->AnimCurStateID();
    U32 anid_loop = npc->AnimPick(goal->GetID(), NME_GSPOT_LOOP, goal);

    if (anid_cur == anid_loop)
    {
        xVec3 pos_emit = *xEntGetPos(npc) + SpinBattle::Dust::vec_emitOffset;
        SpinBattle::Dust::dustWhirl.PropWash(&pos_emit, NULL);
    }
}

S32 zNMEGoalSpinBattle::Process(en_trantype* trantype, F32 dt, void* ctxt)
{
    S32 nextgoal = 0;
    zNMESpinner* npc = (zNMESpinner*)psyche->clt_owner;
    S32 chkTrans = 1;
    en_vis vis;

    if (!npc->arena.IsReady())
    {
        *trantype = GOAL_TRAN_SET;
        nextgoal = NME_GOAL_TRIGGER_SCARY;
        npc->GoalPick(this, nextgoal, *trantype);
        return nextgoal;
    }

    vis = npc->VisionTarget(xglobals->___player_ent_dont_use_directly, NME_DEFCON_BATTLE, 0);

    if (!(tmr_minAttack < 0.0f))
    {
        chkTrans = 0;
        PursueAndDestroy(npc, dt);
    }
    else if (globals.player.DamageTimer > npc->cfg_std->taunt.tym_keeplaffin)
    {
        if (npc->XZDstSqToPlayer(NULL, NULL) > SQR(3.2f))
        {
            *trantype = GOAL_TRAN_PUSH;
            nextgoal = NME_GOAL_TRIGGER_VINIVICIVIDI;
            npc->GoalPick(this, nextgoal, *trantype);
            return nextgoal;
        }

        StepAwayFromTheSponge(npc, dt);
    }
    else if (vis <= VIS_GET_BACK_HERE)
    {
        PursueAndDestroy(npc, dt);
    }
    else if (!npc->arena.IncludesNPC(npc, 0.0f, NULL))
    {
        chkTrans = 0;
        S32 amDone = RetreatAndFinish(npc, dt);
        if (amDone)
        {
            *trantype = GOAL_TRAN_SET;
            nextgoal = NME_GOAL_TRIGGER_ALERT;
            npc->GoalPick(this, nextgoal, *trantype);
            return nextgoal;
        }
    }
    else
    {
        chkTrans = 0;
        S32 amDone = RetreatAndFinish(npc, dt);
        if (amDone)
        {
            *trantype = GOAL_TRAN_SET;
            nextgoal = NME_GOAL_TRIGGER_ALERT;
            npc->GoalPick(this, nextgoal, *trantype);
            return nextgoal;
        }
    }

    if (chkTrans)
    {
        if (globals.player.s->Health < 1)
        {
            *trantype = GOAL_TRAN_SET;
            nextgoal = NME_GOAL_TRIGGER_VINIVICIVIDI;
            npc->GoalPick(this, nextgoal, *trantype);
            return nextgoal;
        }

        if (NME::GlobalData::flg_wonder)
        {
            *trantype = GOAL_TRAN_SET;
            nextgoal = NME_GOAL_TRIGGER_NORMAL;
            npc->GoalPick(this, nextgoal, *trantype);
            return nextgoal;
        }
    }

    SpinBattle_BubbleLoop(this, npc);
    SpinBattle_DustLoop(this, npc);

    RibbonLoop(npc);

    tmr_minAttack = MAX(-1.0f, tmr_minAttack - dt);

    return zNMEGoalCommon::Process(trantype, dt, ctxt);
}

// The PS2 DWARF lists only anid_loop; retail reads the current state ID before picking it (as in
// PursueAndDestroy). The PS2 build turned with MatQRotTowards; retail calls MatFacePlayer.
void zNMEGoalSpinBattle::StepAwayFromTheSponge(zNMESpinner* npc, F32 dt)
{
    xVec3 dir_back;
    F32 len;
    U32 anid_cur;
    U32 anid_loop;

    dir_back = *xEntGetPos(xglobals->___player_ent_dont_use_directly);
    if (npc->navnet->nav_curr != NULL)
    {
        dir_back -= *npc->navnet->nav_curr->pos;
    }
    else
    {
        dir_back -= npc->arena.pos_arena;
    }

    dir_back.y = 0.0f;
    len = dir_back.length();
    if (len < 0.00001f)
    {
        npc->XZVecToPos(&dir_back, xEntGetPos(xglobals->___player_ent_dont_use_directly), NULL);
        len = dir_back.length();
        if (len < 0.00001f)
        {
            dir_back = *(xVec3*)&npc->model->Mat->at;
            len = 1.0f;
        }
    }

    dir_back /= -len;

    npc->MatFacePlayer(dt, PI, 1);

    anid_cur = npc->AnimCurStateID();
    anid_loop = npc->AnimPick(GetID(), NME_GSPOT_LOOP, this);
    if (anid_cur == anid_loop)
    {
        npc->ThrottleAdjust(dt, 5.0f, 1.0f);
        xVec3 pos_want = *xEntGetPos(xglobals->___player_ent_dont_use_directly) + dir_back * 3.5f;
        npc->MatMoveToPos(&pos_want, dt, npc->spd_throttle, 0, -1.0f);
    }
    else
    {
        npc->ThrottleAdjust(dt, 0.0f, 1.0f);
    }

    WormsAreMean(npc);
}

// The PS2 build turned with MatQRotTowards (retail calls MatFacePlayer) and lists no anid_cur,
// but retail reads the current state ID once.
S32 zNMEGoalSpinBattle::RetreatAndFinish(zNMESpinner* npc, F32 dt)
{
    S32 arrived;
    U32 anid_cur;
    U32 anid_loop;
    U32 anid_done;
    xVec3 pos_dest;
    S32 doneanddone;

    npc->MatFacePlayer(dt, PI, 1);

    anid_cur = npc->AnimCurStateID();
    anid_loop = npc->AnimPick(GetID(), NME_GSPOT_LOOP, this);
    anid_done = npc->AnimPick(GetID(), NME_GSPOT_FINISH, this);
    if (anid_cur == anid_done)
    {
        npc->ThrottleAdjust(dt, 0.0f, 1.0f);
        arrived = 1;
    }
    else if (anid_cur != anid_loop)
    {
        npc->ThrottleAdjust(dt, 0.0f, 1.0f);
        arrived = 0;
    }
    else
    {
        if (npc->navnet->nav_curr != NULL)
        {
            pos_dest = *npc->navnet->nav_curr->pos;
        }
        else
        {
            pos_dest = npc->arena.pos_arena;
        }

        npc->ThrottleAdjust(dt, 5.0f, 1.0f);
        arrived = npc->MatMoveToPos(&pos_dest, dt, npc->spd_throttle, 0, -1.0f);
    }

    doneanddone = 0;
    if (arrived)
    {
        U32 anid_loop = npc->AnimPick(GetID(), NME_GSPOT_LOOP, this);
        if (anid_cur == anid_loop)
        {
            DoAutoAnim(NME_GSPOT_FINISH, 0);
        }
        else
        {
            // not a PS2 DWARF local, but the time is read on its own line (named as in
            // zNMEGoalFlingAttack::Process)
            F32 tym_animCurr = npc->AnimTimeCurrent();
            if (tym_animCurr + dt >= npc->AnimDuration(NULL))
            {
                doneanddone = 1;
            }
        }
    }

    return doneanddone;
}

// The PS2 build differs (collision checks instead of WormsAreMean) and lists only anid_done and
// anid_loop; anid_cur is not in it, but retail reads the current state ID once for both checks.
S32 zNMEGoalSpinBattle::PursueAndDestroy(zNMESpinner* npc, F32 dt)
{
    U32 anid_cur;
    U32 anid_done;
    U32 anid_loop;

    anid_cur = npc->AnimCurStateID();
    anid_done = npc->AnimPick(GetID(), NME_GSPOT_FINISH, this);
    if (anid_cur == anid_done)
    {
        DoAutoAnim(NME_GSPOT_RESUME, 0);
    }

    WormsAreMean(npc);

    npc->MatFacePlayer(dt, 0.5f * PI, 1);

    anid_loop = npc->AnimPick(GetID(), NME_GSPOT_LOOP, this);
    if (anid_cur == anid_loop)
    {
        npc->ThrottleAdjust(dt, 5.0f, 1.0f);
        npc->MatMoveToPos(xEntGetPos(xglobals->___player_ent_dont_use_directly), dt,
                          npc->spd_throttle, 1, -1.0f);
    }
    else
    {
        npc->ThrottleAdjust(dt, 0.0f, 1.0f);
    }

    return 0;
}

void zNMEGoalSpinBattle::WormsAreMean(zNMESpinner* npc)
{
    U32 anid_cur = npc->AnimCurStateID();
    U32 anid_loop = npc->AnimPick(GetID(), NME_GSPOT_LOOP, this);

    if (anid_cur == anid_loop)
    {
        ColChk_Player(npc);
        ColChk_BBall(npc);
        ColChk_NMEDyn(npc);
    }
}

// No GC symbol (auto-inlined into WormsAreMean and dead-stripped).
S32 zNMEGoalSpinBattle::ColChk_Player(zNMESpinner* npc)
{
    xVec3 pos_nme;
    F32 upper;
    xVec3 pos_plyr;

    cnt_skipPlyrChecks--;
    if (cnt_skipPlyrChecks > 0)
    {
        return 0;
    }

    cnt_skipPlyrChecks = 2;

    pos_nme = *(xVec3*)&npc->model->Mat->pos;
    pos_plyr = xglobals->___player_ent_dont_use_directly->bound.sph.center;

    upper = pos_nme.y + 0.75f;
    if (pos_plyr.y < pos_nme.y + -0.5f)
    {
        return 0;
    }

    if (pos_plyr.y > upper)
    {
        return 0;
    }

    xVec3 vec_NtoB = pos_plyr - pos_nme;
    if (vec_NtoB.length2() < 0.00001f)
    {
        return 0;
    }

    if (vec_NtoB.length2() > 4.84f)
    {
        return 0;
    }

    zEntPlayer_DamageNPCKnockBack(npc, 1, (xVec3*)&npc->model->Mat->pos, ePlayerNMEDamageDefault);
    return 1;
}

// Locals are declared at first use, which avoids extra struct copies. 'len' is not a PS2 DWARF
// local; retail compares and divides by one length.
S32 zNMEGoalSpinBattle::ColChk_BBall(zNMESpinner* npc)
{
    cnt_skipBallChecks--;
    if (cnt_skipBallChecks > 0)
    {
        return 0;
    }

    cnt_skipBallChecks = 3;

    xEntBoulder* bowl = zEntPlayerGetBBowl();
    if (bowl == NULL)
    {
        return 0;
    }

    if (!xEntIsVisible(bowl))
    {
        return 0;
    }

    xVec3 pos_nme = *xEntGetPos(npc);
    F32 upper = pos_nme.y + 1.0f;
    xVec3 pos_ball = *xEntGetPos(bowl);
    if (pos_ball.y < pos_nme.y + -0.25f)
    {
        return 0;
    }

    if (pos_ball.y > upper)
    {
        return 0;
    }

    xVec3 vec_NtoB = pos_ball - pos_nme;
    if (vec_NtoB.length2() < 0.00001f)
    {
        return 0;
    }

    if (vec_NtoB.length2() > 6.25f)
    {
        return 0;
    }

    xVec3 dir_NtoB = vec_NtoB * (1.0f / vec_NtoB.length());

    xVec3 dir_trav = bowl->vel;
    F32 len = dir_trav.length();
    if (len > 0.00001f)
    {
        dir_trav /= len;
        if (xVec3Dot(&dir_trav, &dir_NtoB) > 0.0f)
        {
            return 0;
        }
    }

    xVec3 dir_smack;
    xVec3Cross(&dir_smack, &g_Y3, &dir_NtoB);
    xVec3 vec_smack = dir_smack * 35.0f;
    xEntBoulder_AddInstantForce(bowl, &vec_smack);
    return 1;
}

// No GC symbol (auto-inlined into WormsAreMean and dead-stripped).
S32 zNMEGoalSpinBattle::ColChk_NMEDyn(zNMESpinner* npc)
{
    xVec3 pos_head;
    BattleCollideData cbdata;
    static xCollis colrec;

    cnt_skipNMEChecks--;
    if (cnt_skipNMEChecks > 0)
    {
        return 0;
    }

    cnt_skipNMEChecks = 3;

    pos_head = *(xVec3*)&npc->model->Mat[27].pos;
    xMat4x3Toworld(&pos_head, (xMat4x3*)npc->model->Mat, &pos_head);

    memset(&cbdata, 0, sizeof(BattleCollideData));
    cbdata.goal_battle = this;
    cbdata.npc = npc;
    cbdata.bound.type = XBOUND_TYPE_SPHERE;
    cbdata.bound.sph.r = 0.75f;
    cbdata.bound.sph.center = pos_head;
    xQuickCullForSphere(&xqc_def_ctrl, &cbdata.bound.qcd, &cbdata.bound.sph);

    memset(&colrec, 0, sizeof(xCollis));
    xSphereHitsEnv3(&cbdata.bound.sph, xglobals->sceneCur->env, &colrec, 1, PI / 4.0f);

    xGridCheckBound(npcs_grid, cbdata.bound, cbdata.bound.qcd, cbdata);
    xGridCheckBound(colls_grid, cbdata.bound, cbdata.bound.qcd, cbdata);
    return 0;
}

U8 BattleCollideData::operator()(xEnt& ent)
{
    xCollis colrec;

    if (&ent == npc)
    {
        return 1;
    }

    if (!(ent.chkby & XENT_COLLTYPE_NPC))
    {
        return 1;
    }

    memset(&colrec, 0, sizeof(xCollis));
    if (ent.collLev == 5)
    {
        xBoundHitsModel(&bound, ent.model, &colrec, 1, PI / 4.0f);
    }
    else
    {
        xBoundHitsBound(&bound, &ent.bound, &colrec);
    }

    if (!(colrec.flags & k_HIT_IT))
    {
        return 1;
    }

    switch (ent.baseType)
    {
    case eBaseTypeNPC:
        ((zNMECommon*)&ent)->Damage(DMGTYP_NMEATTACK, npc, &colrec.tohit, -1, ePowerupLevelNormal);
        break;
    case eBaseTypeButton:
        break;
    case eBaseTypeStatic:
    case eBaseTypeDestructObj:
    {
        F32 hitArgs[4] = { 1.0f, 0.0f, 0.0f, 0.0f };
        zEntEvent(npc, 0, &ent, eEventHit, hitArgs, NULL, 0, FE_NO);
        break;
    }
    }

    return 1;
}

// Placeholder for dead-stripped data, not recovered code: retail's .rodata keeps an all-zero 0xC
// template here that nothing references; its owner and type are unknown.
static void __deadstripped_zNMETypeStdSpinner()
{
    xVec3 unknown = {};
}

void zNMEGoalSpinBattle::RibbonLoop(zNMESpinner* npc)
{
    NMEHelpRib::RibData* ribhelp;
    xVec3 pos_hand;
    xVec3 pos_head;
    xMat4x3* mat_root;

    if (ribbon == NULL)
    {
        return;
    }

    cnt_skipRibbonJoint--;
    if (cnt_skipRibbonJoint > 0)
    {
        return;
    }

    ribhelp = &SpinBattle::WormHead::rib_WormHead;
    cnt_skipRibbonJoint = ribhelp->updinfo.frq_ribJoint;
    if (ribhelp->flags.dev_enable)
    {
        ribhelp->ConfigRibbon(ribbon);
    }

    mat_root = (xMat4x3*)npc->model->Mat;
    pos_hand = *(xVec3*)&npc->model->Mat[13].pos;
    pos_head = *(xVec3*)&npc->model->Mat[27].pos;
    xMat4x3Toworld(&pos_hand, mat_root, &pos_hand);
    xMat4x3Toworld(&pos_head, mat_root, &pos_head);

    xVec3 dir_emit = pos_hand - pos_head;
    F32 dst = xVec3Normalize(&dir_emit, &dir_emit);
    ribbon->insert(pos_head, dir_emit, dst * ribhelp->updinfo.scl_master,
                   ribhelp->updinfo.alf_master, 0);
}
