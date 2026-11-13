#include "zNMETypeStdPopper.h"
#include "xAntenna.h"
#include "zScene.h"

extern _tagGameVidMode gVidMode;

namespace Popper
{
    namespace Config
    {
        static const NMECfgPopper cfg_popper_v1 = {
            { 5 },
            { 5.5f, 20.0f, 25.0f, 35.0f, 40.0f },
            { HAZ_TYP_POPPER_V1, 7.5f, 3 },
            { 0.5f, 0.25f, 0.0f, -3.0f, 1.0f },
            { 2.0f, 4.5f },
            { 10.0f, 1.0f, 1.0f },
            { 10.0f, 0.0f, 0.75f },
            { 5.2f },
            { 20.0f },
        };
        static const NMECfgPopper cfg_popper_v2 = {
            { 6 },
            { 5.5f, 20.0f, 25.0f, 35.0f, 40.0f },
            { HAZ_TYP_POPPER_V2, 10.0f, 5 },
            { 0.5f, 0.25f, 0.5f, -4.0f, 1.0f },
            { 2.5f, 6.0f },
            { 10.0f, 1.0f, 1.0f },
            { 20.0f, 0.0f, 2.5f },
            { 3.2f },
            { 25.0f },
        };
        static const NMECfgPopper cfg_popper_v3 = {
            { 8 },
            { 5.5f, 20.0f, 25.0f, 35.0f, 40.0f },
            { HAZ_TYP_POPPER_V3, 12.5f, 7 },
            { 0.5f, 0.25f, 1.0f, -6.0f, 1.75f },
            { 2.0f, 10.0f },
            { 10.0f, 1.0f, 1.0f },
            { 25.0f, 0.0f, 3.5f },
            { 1.7f },
            { 30.0f },
        };
        const NMECfgPopper* cfg_pop[10] = { &cfg_popper_v1, &cfg_popper_v1, &cfg_popper_v2,
                                            &cfg_popper_v3 };
    } // namespace Config

    NMEHelpBound::BndData bnd_info = {
        k_XBOUNDTYPE_SPHERE,
        { 0, 0, { 0, 0 } },
        { 1.5f, { 0.0f, 1.5f, 0.0f } },
        { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } },
    };

    namespace LidParm
    {
        static const xVec3 vec_lidOffset = { 1.0f, 0.0f, 1.0f };
    } // namespace LidParm
} // namespace Popper

// Gas and exhaust emitters of zNMEGoalPopEvade, per popper variant (Trench: v1 and unknown
// types, Goof: v2, Plank: v3).
namespace PopEvade
{
    namespace Trench
    {
        PopGasEmitter gasFount = {
            NPAR_TYP_POPGAS,
            0.8f,
            { 0x78, 0x3C, 0x64, 0x3C },
            { 1.35f, 3.5f },
            { 0.0f, 0.0f, 0.0f },
            13.78539f,
            99,
            0,
            4,
            0,
            { 3.0f, 3.0f, 0.4f, 2.0f, 2.0f },
        };

        PopGasEmitter gasGround = {
            NPAR_TYP_POPGAS,
            2.0f,
            { 0xB4, 0x64, 0x00, 0x32 },
            { 1.25f, 2.0f },
            { 0.0f, -10.0f, 0.0f },
            0.7853982f,
            97,
            0,
            7,
            0,
            { 2.0f, 0.0f, 1.0f, 1.0f, 1.0f },
        };

        ExhaustCurve curve_data[4] = {
            { 0.0f, { 0x1E, 0x78, 0x1E, 0x64 }, 1.0f },
            { 0.6f, { 0x1E, 0x78, 0x1E, 0x32 }, 1.0f },
            { 0.8f, { 0x64, 0x64, 0x1E, 0x19 }, 0.5f },
            { 1.0f, { 0x64, 0x64, 0x1E, 0x00 }, 0.15f },
        };

        ExhaustEmitter evadeTrail = {
            NPAR_TYP_HEATED,
            { 0, 0, { 0, 0 } },
            { 20.0f, 0.0f, 5.0f },
            { 1.0f, 1.0f },
            { 0.25f, { 0.1f, 0.9f }, 0x20, { 0, 0, 0 } },
            { 0.7f, 0.5f },
            { 2, 0, 0.45f, -0.45f },
            { 2.2f, 7.0f, 0.0f, 0.0f, 1.25f, 1.25f, { 0.0f, 0.0f, 0.0f } },
            { curve_data, 4, 1.0f, 2.4f },
        };
    } // namespace Trench

    namespace Goof
    {
        PopGasEmitter gasFount = {
            NPAR_TYP_POPGAS,
            0.8f,
            { 0x78, 0x3C, 0x64, 0x3C },
            { 1.35f, 3.5f },
            { 0.0f, 0.0f, 0.0f },
            13.78539f,
            99,
            0,
            4,
            0,
            { 3.0f, 3.0f, 0.4f, 2.0f, 2.0f },
        };

        PopGasEmitter gasGround = {
            NPAR_TYP_POPGAS,
            2.0f,
            { 0x78, 0x64, 0x00, 0x32 },
            { 1.25f, 2.0f },
            { 0.0f, -10.0f, 0.0f },
            0.7853982f,
            97,
            0,
            7,
            0,
            { 2.0f, 0.0f, 1.0f, 1.0f, 1.0f },
        };

        ExhaustCurve curve_data[4] = {
            { 0.0f, { 0x78, 0x64, 0x00, 0xFF }, 1.0f },
            { 0.6f, { 0x78, 0x8C, 0x14, 0x64 }, 1.0f },
            { 0.8f, { 0xC8, 0xC8, 0x14, 0x32 }, 0.5f },
            { 1.0f, { 0xC8, 0xC8, 0x14, 0x00 }, 0.15f },
        };

        ExhaustEmitter evadeTrail = {
            NPAR_TYP_EXHAUST,
            { 0, 0, { 0, 0 } },
            { 20.0f, 0.0f, 5.0f },
            { 1.0f, 1.0f },
            { 0.25f, { 0.1f, 0.9f }, 0x20, { 0, 0, 0 } },
            { 0.7f, 0.5f },
            { 2, 0, 0.45f, -0.45f },
            { 2.2f, 7.0f, 0.0f, 0.0f, 1.25f, 1.25f, { 0.0f, 0.0f, 0.0f } },
            { curve_data, 4, 1.0f, 3.0f },
        };
    } // namespace Goof

    namespace Plank
    {
        PopGasEmitter gasFount = {
            NPAR_TYP_POPGAS,
            0.8f,
            { 0x78, 0x3C, 0x64, 0x3C },
            { 1.35f, 3.5f },
            { 0.0f, 0.0f, 0.0f },
            13.78539f,
            99,
            0,
            4,
            0,
            { 3.0f, 3.0f, 0.4f, 2.0f, 2.0f },
        };

        PopGasEmitter gasGround = {
            NPAR_TYP_POPGAS,
            2.0f,
            { 0x78, 0x64, 0x00, 0x32 },
            { 1.25f, 2.0f },
            { 0.0f, -10.0f, 0.0f },
            0.7853982f,
            97,
            0,
            7,
            0,
            { 2.0f, 0.0f, 1.0f, 1.0f, 1.0f },
        };

        ExhaustCurve curve_data[4] = {
            { 0.0f, { 0x1E, 0x78, 0xFF, 0xFF }, 1.0f },
            { 0.6f, { 0x1E, 0x78, 0xFF, 0x64 }, 1.0f },
            { 0.8f, { 0xC8, 0xC8, 0x3C, 0x32 }, 0.5f },
            { 1.0f, { 0xC8, 0xC8, 0x3C, 0x00 }, 0.15f },
        };

        ExhaustEmitter evadeTrail = {
            NPAR_TYP_EXHAUST,
            { 0, 0, { 0, 0 } },
            { 20.0f, 0.0f, 5.0f },
            { 1.0f, 1.0f },
            { 0.25f, { 0.1f, 0.9f }, 0x20, { 0, 0, 0 } },
            { 0.7f, 0.5f },
            { 2, 0, 0.45f, -0.45f },
            { 2.2f, 7.0f, 0.0f, 0.0f, 1.25f, 1.25f, { 0.0f, 0.0f, 0.0f } },
            { curve_data, 4, 1.0f, 3.0f },
        };
    } // namespace Plank
} // namespace PopEvade

void zNMEPopper::CreateConfig(const char* name)
{
    en_nmevar vidx;

    zNMEStandard::CreateConfig(name);
    vidx = NMEVariantIdx((en_npctyp)itemType);
    cfg_popper = Popper::Config::cfg_pop[vidx];
}

S32 zNMEPopper::DfltVulnFlags()
{
    return 0xCFFFFFF7;
}

void zNMEPopper::SelfSetup()
{
    xBehaveMgr* bmgr;
    xPsyche* psy;

    bmgr = xBehaveMgr_GetSelf();
    psy_self = bmgr->Subscribe(this, 0);

    psy = psy_self;

    psy->BrainBegin();

    psy->AddGoal(NME_GOAL_POPPER_NORMAL, NULL);
    psy->AddGoal(NME_GOAL_POPPER_AWARE, NULL);
    psy->AddGoal(NME_GOAL_POPPER_BATTLE, NULL);
    psy->AddGoal(NME_GOAL_POPPER_EVADE, NULL);
    psy->AddGoal(NME_GOAL_POPPER_WOUND, NULL);
    psy->AddGoal(NME_GOAL_POPPER_ATTACK, NULL);

    psy->AddGoal(NME_GOAL_TAUNT, NULL);
    psy->AddGoal(NME_GOAL_AFTERLIFE, NULL);
    psy->AddGoal(NME_GOAL_SPAWN, NULL);
    psy->AddGoal(NME_GOAL_SCAREWAIT, NULL);
    psy->AddGoal(NME_GOAL_SCARE, NULL);
    psy->AddGoal(NME_GOAL_NOTICE, NULL);
    psy->AddGoal(NME_GOAL_SPOOKED, NULL);

    psy->BrainEnd();

    psy->SetSafety(NME_GOAL_POPPER_NORMAL);
}

U32 zNMEPopper::AnimPick(S32 gid, en_npcgspot gspot, xGoal* goal)
{
    en_stananim idx;

    switch (gid)
    {
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
    case NME_GOAL_POPPER_WOUND:
        if (pts_health < 1)
        {
            idx = STAN_ANIM_DEFEAT;
        }
        else
        {
            idx = STAN_ANIM_DAMAGE;
        }
        break;
    case NME_GOAL_POPPER_NORMAL:
        idx = STAN_ANIM_IDLE;
        break;
    case NME_GOAL_POPPER_AWARE:
    case NME_GOAL_POPPER_BATTLE:
        idx = STAN_ANIM_ALERT;
        break;
    case NME_GOAL_POPPER_EVADE:
        idx = STAN_ANIM_CHASE;
        break;
    case NME_GOAL_POPPER_ATTACK:
        if (gspot == NME_GSPOT_START)
        {
            idx = STAN_ANIM_ATTACK_PREP;
        }
        else if (gspot == NME_GSPOT_RESUME)
        {
            idx = STAN_ANIM_ATTACK_LOOP;
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

    return g_hash_stananim[idx];
}

S32 zNMEPopper::GoalPick(xGoal* goal, S32& gid_would, en_trantype& trantype)
{
    S32 altered = 1;

    switch (gid_would)
    {
    case NME_GOAL_TRIGGER_NORMAL:
        gid_would = NME_GOAL_POPPER_NORMAL;
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
    case NME_GOAL_TRIGGER_WOUND:
        gid_would = NME_GOAL_POPPER_WOUND;
        break;
    case NME_GOAL_TRIGGER_ALERT:
        gid_would = NME_GOAL_POPPER_AWARE;
        break;
    case NME_GOAL_TRIGGER_BATTLE:
        gid_would = NME_GOAL_POPPER_BATTLE;
        break;
    case NME_GOAL_TRIGGER_ATTACK:
        gid_would = NME_GOAL_POPPER_ATTACK;
        break;
    default:
        altered = 0;
        break;
    }

    return altered;
}

xAnimTable* zNMEPopper::CreateAnimTable(xAnimTable* table)
{
    if (table == NULL)
    {
        table = xAnimTableNew("zNMEPopper", 0);
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
    xAnimTableNewState(table, g_strz_stananim[STAN_ANIM_SPOOK_LOOP], 0x410, 0x2000001, 1.0f, NULL,
                       NULL, 0.0f, NULL, NULL, xAnimDefaultBeforeEnter, NULL, NULL);
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
    xAnimTableNewState(table, g_strz_stananim[STAN_ANIM_ATTACK_PREP], 0x20, 1, 1.0f, NULL, NULL,
                       0.0f, NULL, NULL, xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, g_strz_stananim[STAN_ANIM_ATTACK_LOOP], 0x10, 1, 1.0f, NULL, NULL,
                       0.0f, NULL, NULL, xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, g_strz_stananim[STAN_ANIM_ATTACK_DONE], 0, 1, 1.0f, NULL, NULL, 0.0f,
                       NULL, NULL, xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, g_strz_stananim[STAN_ANIM_POP_DOWN], 0, 1, 1.0f, NULL, NULL, 0.0f,
                       NULL, NULL, xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, g_strz_stananim[STAN_ANIM_POP_UP], 0, 0x4000001, 1.0f, NULL, NULL,
                       0.0f, NULL, NULL, xAnimDefaultBeforeEnter, NULL, NULL);

    S32 ourAnims[18] = { STAN_ANIM_IDLE,        STAN_ANIM_MOVE,        STAN_ANIM_ALERT,
                         STAN_ANIM_CHASE,       STAN_ANIM_POP_DOWN,    STAN_ANIM_POP_UP,
                         STAN_ANIM_SCARE_LOOP,  STAN_ANIM_SCARE_DONE,  STAN_ANIM_SPOOK_LOOP,
                         STAN_ANIM_NOTICE,      STAN_ANIM_TAUNT,       STAN_ANIM_VICTORY,
                         STAN_ANIM_DAMAGE,      STAN_ANIM_DEFEAT,      STAN_ANIM_ATTACK_PREP,
                         STAN_ANIM_ATTACK_LOOP, STAN_ANIM_ATTACK_DONE, STAN_ANIM_UNKNOWN };
    NMEC_BuildStandardAnimTran(table, g_strz_stananim, ourAnims, 3, 0.2f);

    xAnimTableNewTransition(table, g_strz_stananim[STAN_ANIM_ATTACK_PREP],
                            g_strz_stananim[STAN_ANIM_ATTACK_LOOP], NULL, NULL, 0x10, 0, 0.0f, 0.0f,
                            0, 0, 0.2f, NULL);

    return table;
}

void zNMEPopper::Init(xEntAsset* asset)
{
    zNMEStandard::Init(asset);
    zNMEParFX_PartyInvite(NPAR_TYP_DUSTPUFF, NULL, 0);
    zNMEParFX_PartyInvite(NPAR_TYP_POPGAS, NULL, 0);
    zNMEParFX_PartyInvite(NPAR_TYP_PLASMA, NULL, 0);
}

void zNMEPopper::Setup()
{
    zNMEStandard::Setup();

    if (itemType == NME_TYPE_POPPER_V2 || itemType == NME_TYPE_POPPER_V3)
    {
        xAntennaAddAntenna(this, 0.0f, 0.0f, 7, 8, false);
    }
    else
    {
        xAntennaAddAntenna(this, 0.0f, 0.0f, 7, 8, true);
    }

    xAntennaAddAntenna(this, 0.0f, 0.0f, 9, 10, true);
    xAntennaAddAntenna(this, 0.0f, 0.0f, 11, 12, true);
}

void zNMEPopper::Reset()
{
    xPsyche* psy;
    zNMEGoalAfterlife* goal;

    zNMEStandard::Reset();

    // not a DWARF local: retail builds the group in a temporary before the one store (as
    // zNMEDennis::Reset and zNMEDenToo::Reset do)
    xGroup* grp = NULL;
    if (((zNMEAssetStandard*)nmeass)->aid_mvptGroup != 0)
    {
        grp = (xGroup*)zSceneFindObject(((zNMEAssetStandard*)nmeass)->aid_mvptGroup);
    }
    grp_mvpt = grp;

    if (grp_mvpt == NULL)
    {
        // The result is not used (also in the PS2 demo).
        MvptFirstFollow();
    }

    nav_evade = PickEvadePoint(NULL);
    nav_evadePrev = NULL;

    psy = psy_self;
    if (((zNMEAssetStandard*)nmeass)->flg_stdass & 0x1)
    {
        psy->GoalSet(NME_GOAL_SCAREWAIT, 1);
        nav_evade = MvptFirstFollow();
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
        psy->GoalSet(NME_GOAL_POPPER_NORMAL, 1);
    }

    pts_health = cfg_popper->health.pts_healthMax;
}

void zNMEPopper::BUpdate(xVec3* pos)
{
    Popper::bnd_info.UpdateBound(&bound, this, pos);
}

void zNMEPopper::Process(F32 dt)
{
    xMat4x3 oldMat;
    xEntFrame tmpFrame;

    if (flg_colCheck)
    {
        oldMat = *(xMat4x3*)model->Mat;
    }

    psy_self->Timestep(dt, NULL);

    if (IsAlive())
    {
        lid.LidUpdate(dt);
        zNMECommon::Process(dt);
    }

    if (IsAlive())
    {
        ShadStufUpdate();
    }
    else if (simpShadow != NULL || entShadow != NULL)
    {
        ShadStufIAmDead();
    }

    if (flg_colCheck)
    {
        memset(&tmpFrame, 0, sizeof(xEntFrame));
        tmpFrame.oldmat = oldMat;
        tmpFrame.mat = *(xMat4x3*)model->Mat;
        frame = &tmpFrame;
        RunCollision(dt);
        frame = NULL;
    }
}

void zNMEPopper::ProcessInvis(F32 dt)
{
    Process(dt);
}

en_vis zNMEPopper::VisionTarget(xEnt* ent, en_defcon defcon, S32 flags) const
{
    const NMECfgPopper* cfg;
    F32 dy;
    // Not PS2 DWARF locals: retail keeps the XZ delta on the stack and its length for every
    // range test below.
    xVec3 dir;
    F32 dst;

    if (NME::GlobalData::flg_wonder)
    {
        return VIS_NOBODY_AROUND;
    }

    if (runtimeData.overrideDetect == ALLOW_NEVER)
    {
        return VIS_NOBODY_AROUND;
    }

    cfg = cfg_popper;
    XZVecToPos(&dir, xEntGetPos(ent), &dy);
    dst = dir.length();

    if (runtimeData.overrideDetect == ALLOW_NORMAL && runtimeData.overrideAttack == ALLOW_NORMAL &&
        dst > cfg->ranges.rad_awaregrey)
    {
        return VIS_NOBODY_AROUND;
    }

    if (dst < cfg->ranges.rad_tooclose)
    {
        return VIS_OH_DEAR_GAHD;
    }

    if (runtimeData.overrideAttack != ALLOW_NEVER)
    {
        if (runtimeData.overrideAttack == ALLOW_ALWAYS || dst < cfg->ranges.rad_lob)
        {
            xVec3SMulBy(&dir, 1.0f / dst);
            if (xVec3Dot(&dir, (xVec3*)&model->Mat->at) < 0.96f)
            {
                return VIS_HORROR_FLIK;
            }
            return VIS_PREPARE_TO_DIE;
        }

        if (dst < cfg->ranges.rad_lobgrey)
        {
            xVec3SMulBy(&dir, 1.0f / dst);
            if (xVec3Dot(&dir, (xVec3*)&model->Mat->at) < 0.96f)
            {
                return VIS_HORROR_FLIK;
            }
            return VIS_GET_BACK_HERE;
        }
    }

    if (runtimeData.overrideDetect == ALLOW_ALWAYS)
    {
        return VIS_I_SEEE_YOUUU;
    }

    if (dst < cfg->ranges.rad_aware)
    {
        return VIS_I_SEEE_YOUUU;
    }

    if (dst < cfg->ranges.rad_awaregrey)
    {
        return VIS_GREY_AND_FOGGY;
    }

    return VIS_NOBODY_AROUND;
}

F32 zNMEPopper::HealthRatio()
{
    F32 rat;

    if (cfg_popper == NULL)
    {
        return 0.0f;
    }

    rat = pts_health;
    rat /= cfg_popper->health.pts_healthMax;
    rat = CLAMP(rat, 0.0f, 1.0f);
    return rat;
}

// No GC symbol (inlined into TypeHandleMail); out of line in the PS2 demo.
inline S32 zNMEPopper::PopMailDamage(NMEDamageInfo* dmgmail)
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

        psy_self->GoalSet(NME_GOAL_POPPER_WOUND, 0);
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

// No GC or PS2 symbol (inlined into TypeHandleMail in both builds); the name is ours, after
// zNMEZapper::ZapMailSys.
inline S32 zNMEPopper::PopMailSys(NMESysEvent* sysmail)
{
    S32 handled = 1;

    switch (sysmail->toEvent)
    {
    case eEventNMESetMovepointGroup:
        grp_mvpt = (xGroup*)sysmail->toParamWidget;
        if (grp_mvpt == NULL)
        {
            MvptFirstFollow();
        }
        break;
    default:
        handled = DfltSysEventMail(sysmail);
        break;
    }

    return handled;
}

S32 zNMEPopper::TypeHandleMail(NMEMsg* mail)
{
    S32 handled = 1;

    switch (mail->msgid)
    {
    case NME_MID_DAMAGE:
        handled = PopMailDamage(&mail->dmgdata);
        break;
    case NME_MID_SYSEVENT:
        handled = PopMailSys(&mail->sysevent);
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

// len2, dir and dst are not PS2 DWARF locals: retail reuses the squared length for the sqrt and
// keeps the per-point XZ delta on the stack.
zMovePoint* zNMEPopper::PickEvadePoint(zMovePoint* nav_from)
{
    zMovePoint* nav_there;
    U32 num;
    xVec3 dir_plyr;
    F32 dst_retreat;
    F32 dst_charge;
    zMovePoint* nav_retreat;
    zMovePoint* nav_charge;
    U32 i;
    xBase* baseitem;
    zMovePoint* mvpt;
    zMovePoint* nav_goto;
    F32 len2;

    if (grp_mvpt == NULL)
    {
        if (nav_from != NULL)
        {
            xMovePointGetNext(nav_from, NULL, (xMovePoint**)&nav_there, NULL);
            if (nav_there != NULL)
            {
                return nav_there;
            }
            return nav_from;
        }
        return MvptFirstFollow();
    }

    num = xGroupGetCount(grp_mvpt);
    if (num < 1)
    {
        return NULL;
    }

    len2 = XZDstSqToPlayer(&dir_plyr, NULL);
    if (len2 < 0.00001f)
    {
        dir_plyr = *(xVec3*)&model->Mat->at;
    }
    else
    {
        dir_plyr /= xsqrt(len2);
    }

    dst_retreat = -1.0f;
    dst_charge = -1.0f;
    nav_retreat = NULL;
    nav_charge = NULL;

    for (i = 0; i < num; i++)
    {
        xVec3 dir;
        F32 dst;

        baseitem = xGroupGetItemPtr(grp_mvpt, i);
        if (baseitem == NULL || baseitem == nav_from || baseitem->baseType != eBaseTypeMovePoint)
        {
            continue;
        }

        mvpt = (zMovePoint*)baseitem;
        XZVecToPos(&dir, mvpt->pos, NULL);
        dst = dir.length();
        if (dst < 1.0f)
        {
            continue;
        }

        dir /= dst;
        if (xVec3Dot(&dir, &dir_plyr) > 0.5f)
        {
            if (dst_charge < 0.0f || dst < dst_charge)
            {
                dst_charge = dst;
                nav_charge = mvpt;
            }
        }
        else
        {
            if (dst_retreat < 0.0f || dst < dst_retreat)
            {
                dst_retreat = dst;
                nav_retreat = mvpt;
            }
        }
    }

    if (dst_retreat > 0.0f)
    {
        nav_goto = nav_retreat;
    }
    else if (dst_charge > 0.0f)
    {
        nav_goto = nav_charge;
    }
    else
    {
        nav_goto = NULL;
    }

    return nav_goto;
}

// No GC or PS2 symbol (inlined); the name and local names are ours. The PS2 build places the lid
// in one statement, so it was a single inlined call.
static inline void LidPosLerp(xEnt* lid, F32 rat)
{
    xVec3 pos_shut = lid->asset->pos;
    xVec3 pos_open = lid->asset->pos + Popper::LidParm::vec_lidOffset;
    F32 lerp = EASE(rat);
    xVec3 pos;
    pos.x = LERP(lerp, pos_shut.x, pos_open.x);
    pos.y = LERP(lerp, pos_shut.y, pos_open.y);
    pos.z = LERP(lerp, pos_shut.z, pos_open.z);
    xVec3Copy((xVec3*)&lid->model->Mat->pos, &pos);
}

// No GC symbol (inlined into zNMEPopper::Process); out of line in the PS2 demo.
inline void PopTop::LidUpdate(F32 dt)
{
    S32 i;
    F32 rat;

    for (i = 0; i < 2; i++)
    {
        if (ent_lid[i] == NULL)
        {
            continue;
        }

        tmr_live[i] = MAX(-1.0f, tmr_live[i] - dt);
        // Clamped as a separate step (CLAMP(tmr_live[i], ...) allocates registers differently).
        rat = tmr_live[i];
        rat = CLAMP(rat, 0.0f, 1.0f);
        if (i == 0)
        {
            rat = 1.0f - rat;
        }

        LidPosLerp(ent_lid[i], rat);

        if (tmr_live[i] < 0.0f)
        {
            ent_lid[i] = NULL;
        }
    }
}

// No GC or PS2 symbol (inlined); the name is ours. zNMEGoalCommon::DoAutoAnim for an owner the
// caller already holds, so npc is not reloaded from psyche->clt_owner.
static inline U32 PopAutoAnim(zNMEGoalCommon* goal, zNMECommon* npc, en_npcgspot gspot)
{
    U32 anid;

    anid = npc->AnimPick(goal->GetID(), gspot, goal);
    if (anid)
    {
        if (npc->AnimStart(anid, 0))
        {
            goal->anid_played = anid;
        }
        else
        {
            goal->anid_played = 0;
        }
    }
    return anid;
}

S32 zNMEGoalPopNormal::Process(en_trantype* trantype, F32 dt, void* ctxt)
{
    zNMEPopper* npc = (zNMEPopper*)psyche->clt_owner;
    S32 nextgoal = 0;
    en_vis iseeplyr;

    if (globals.player.s->Health >= 1 && !xUtil_yesno(0.8f))
    {
        iseeplyr =
            npc->VisionTarget(xglobals->___player_ent_dont_use_directly, NME_DEFCON_NORMAL, 0);
        if (iseeplyr <= VIS_I_SEEE_YOUUU)
        {
            *trantype = GOAL_TRAN_SET;
            nextgoal = NME_GOAL_TRIGGER_DETECT;
            npc->GoalPick(this, nextgoal, *trantype);
            return nextgoal;
        }
    }

    if (bul_entered)
    {
        PopAutoAnim(this, npc, NME_GSPOT_START);
    }
    else if (bul_resumed)
    {
        PopAutoAnim(this, npc, NME_GSPOT_RESUME);
    }

    return zNMEGoalCommon::Process(trantype, dt, ctxt);
}

// No GC symbol (inlined into Process; out of line in the PS2 demo).
inline S32 zNMEGoalPopAware::ChkAware(zNMEPopper* npc, S32& nextgoal, en_trantype& trantype)
{
    en_vis iseeplyr;

    if (xUtil_yesno(0.8f))
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

    iseeplyr = npc->VisionTarget(xglobals->___player_ent_dont_use_directly, NME_DEFCON_NORMAL, 0);
    if (iseeplyr > VIS_GREY_AND_FOGGY)
    {
        trantype = GOAL_TRAN_SET;
        nextgoal = NME_GOAL_TRIGGER_NORMAL;
        npc->GoalPick(this, nextgoal, trantype);
        return 1;
    }

    if (iseeplyr < VIS_GET_BACK_HERE)
    {
        trantype = GOAL_TRAN_SET;
        nextgoal = NME_GOAL_TRIGGER_BATTLE;
        npc->GoalPick(this, nextgoal, trantype);
        return 1;
    }

    return 0;
}

S32 zNMEGoalPopAware::Process(en_trantype* trantype, F32 dt, void* ctxt)
{
    zNMEPopper* npc = (zNMEPopper*)psyche->clt_owner;
    S32 nextgoal = 0;
    S32 doTran;

    doTran = ChkAware(npc, nextgoal, *trantype);
    if (doTran)
    {
        return nextgoal;
    }

    npc->MatFacePlayer(dt, PI, 1);

    if (bul_entered)
    {
        PopAutoAnim(this, npc, NME_GSPOT_START);
    }
    else if (bul_resumed)
    {
        PopAutoAnim(this, npc, NME_GSPOT_RESUME);
    }

    return zNMEGoalCommon::Process(trantype, dt, ctxt);
}

S32 zNMEGoalPopBattle::Enter(F32 dt, void* updCtxt)
{
    tmr_tossDelay = -1.0f;
    return zNMEGoalCommon::Enter(dt, updCtxt);
}

S32 zNMEGoalPopBattle::Process(en_trantype* trantype, F32 dt, void* ctxt)
{
    zNMEPopper* npc = (zNMEPopper*)psyche->clt_owner;
    S32 nextgoal = 0;
    S32 doTran;

    doTran = ChkTranCases(npc, nextgoal, *trantype);
    if (doTran)
    {
        return nextgoal;
    }

    tmr_tossDelay = MAX(-1.0f, tmr_tossDelay - dt);
    npc->MatFacePlayer(dt, PI, 1);
    return zNMEGoalCommon::Process(trantype, dt, ctxt);
}

S32 zNMEGoalPopBattle::ChkTranCases(zNMEPopper* npc, S32& nextgoal, en_trantype& trantype)
{
    en_vis iseeplyr;
    zMovePoint* mvpt;

    if (globals.player.s->Health < 1)
    {
        trantype = GOAL_TRAN_SET;
        nextgoal = NME_GOAL_TRIGGER_VINIVICIVIDI;
        npc->GoalPick(this, nextgoal, trantype);
        return 1;
    }

    if (globals.player.DamageTimer > npc->cfg_std->taunt.tym_keeplaffin &&
        npc->XZDstSqToPlayer(NULL, NULL) > SQ(8.5f) && globals.player.DamageTimer < 1.0f)
    {
        trantype = GOAL_TRAN_PUSH;
        nextgoal = NME_GOAL_TRIGGER_VINIVICIVIDI;
        npc->GoalPick(this, nextgoal, trantype);
        return 1;
    }

    iseeplyr = npc->VisionTarget(xglobals->___player_ent_dont_use_directly, NME_DEFCON_NORMAL, 0);
    if (iseeplyr == VIS_OH_DEAR_GAHD)
    {
        mvpt = npc->PickEvadePoint(npc->nav_evade);
        npc->nav_evadePrev = npc->nav_evade;
        npc->nav_evade = mvpt;
        if (npc->nav_evadePrev == npc->nav_evade)
        {
            npc->nav_evadePrev = NULL;
        }

        trantype = GOAL_TRAN_SET;
        nextgoal = NME_GOAL_POPPER_EVADE;
        return 1;
    }

    if (iseeplyr > VIS_GET_BACK_HERE)
    {
        trantype = GOAL_TRAN_SET;
        nextgoal = NME_GOAL_TRIGGER_NORMAL;
        npc->GoalPick(this, nextgoal, trantype);
        return 1;
    }

    if (iseeplyr == VIS_HORROR_FLIK)
    {
        return 0;
    }

    if ((iseeplyr == VIS_PREPARE_TO_DIE || iseeplyr == VIS_GET_BACK_HERE) && tmr_tossDelay < 0.0f)
    {
        tmr_tossDelay = npc->cfg_popper->battle.tym_reload;
        trantype = GOAL_TRAN_PUSH;
        nextgoal = NME_GOAL_TRIGGER_ATTACK;
        npc->GoalPick(this, nextgoal, trantype);
        return 1;
    }

    return 0;
}

// After CreateAnimTable, as retail's .rodata has vec_boneOffset after that function's anim table.
namespace PopAttack
{
    static const xVec3 vec_boneOffset = { 0.0f, 0.0f, 0.4f };
} // namespace PopAttack

S32 zNMEGoalPopAttack::Enter(F32 dt, void* updCtxt)
{
    idx_shot = ((zNMEPopper*)psyche->clt_owner)->cfg_popper->attack.num_salvo;
    return zNMEGoalLoopAnim::Enter(dt, updCtxt);
}

S32 zNMEGoalPopAttack::Process(en_trantype* trantype, F32 dt, void* ctxt)
{
    PopGunUpdate((zNMEPopper*)psyche->clt_owner, dt);
    return zNMEGoalLoopAnim::Process(trantype, dt, ctxt);
}

// dst_plyr is not a PS2 DWARF local; the length is evaluated once. The lifespan reuses
// tym_animCurr (the PS2 DWARF has no other tym local). xEntGetFrame keeps the model matrix in a
// register across the pos_launch stores, as in retail.
void zNMEGoalPopAttack::PopGunUpdate(zNMEPopper* npc, F32 dt)
{
    if (idx_shot < 1)
    {
        return;
    }

    if (anid_played != anid_stage[1])
    {
        return;
    }

    F32 tym_animCurr = npc->AnimTimeCurrent();
    if (tym_animCurr < 0.15f)
    {
        return;
    }

    xVec3 pos_launch = ((xMat4x3*)&npc->model->Mat[30])->pos;
    pos_launch += PopAttack::vec_boneOffset;
    xMat4x3Toworld(&pos_launch, xEntGetFrame(npc), &pos_launch);

    // Out of PS2 DWARF order (it lists num after the F32 locals): retail's register choice needs
    // num declared before cfg.
    S32 num;
    const NMECfgPopper* cfg = npc->cfg_popper;
    xVec3 dir_aim;
    xVec3 dir_side;

    xVec3 dir_plyr = *xEntGetCenter(xglobals->___player_ent_dont_use_directly) - pos_launch;
    F32 dst_plyr = dir_plyr.length();
    if (dst_plyr < 0.1f)
    {
        dir_aim = *(xVec3*)&npc->model->Mat->at;
    }
    else
    {
        dir_aim = dir_plyr * (1.0f / dst_plyr);
        if (xVec3Dot((xVec3*)&npc->model->Mat->at, &dir_aim) < 0.75f)
        {
            dir_aim = *(xVec3*)&npc->model->Mat->at;
        }
    }

    F32 dst_farout;
    F32 rad_lr;
    F32 rad_ud;
    F32 f_salvoSeg;
    F32 inc_side;
    F32 spd_bonus;
    F32 spd_proj;
    S32 idx_ofSoundMaker;
    S32 i;

    xVec3Cross(&dir_side, &g_Y3, &dir_aim);

    dst_farout = cfg->popgun.dst_farout;
    rad_lr = cfg->popgun.rad_lr;
    rad_ud = cfg->popgun.rad_ud;
    num = cfg->attack.num_salvo;

    f_salvoSeg = MAX(1.0f, (F32)(num - 1));
    inc_side = (2.0f * rad_lr) / MAX(1.0f, f_salvoSeg);

    spd_proj = cfg->attack.spd_proj;
    spd_bonus = 0.05f * spd_proj;
    spd_proj -= spd_bonus;

    idx_ofSoundMaker = num / 2;

    for (i = 0; i < num; i++)
    {
        zNMEHazard* haz = HAZ_Acquire();
        if (haz == NULL)
        {
            continue;
        }

        if (!haz->ConfigHelper(cfg->attack.haztyp))
        {
            continue;
        }

        haz->npc_owner = npc;
        if (i != idx_ofSoundMaker)
        {
            haz->flg_custom |= 0x1;
        }

        F32 dst_side = rad_lr - i * inc_side;

        xVec3 pos_tgt = g_Y3 * (rad_ud * xurand());
        pos_tgt += dir_aim * dst_farout;
        pos_tgt += dir_side * dst_side;
        pos_tgt += pos_launch;
        xVec3Copy(&haz->custdata.lobdata.pos_tgt, &pos_tgt);

        tym_animCurr =
            MAX(0.25f, dst_farout / (spd_bonus * (1.0f - SQR(((F32)i / f_salvoSeg - 0.5f) * 2.0f)) +
                                     spd_proj));
        haz->Start(&pos_launch, tym_animCurr);

        idx_shot = 0;
    }

    npc->SndPlayFromTable(2); // eNMESound_Attack
}

S32 zNMEGoalPopWound::Process(en_trantype* trantype, F32 dt, void* ctxt)
{
    zNMEPopper* npc = (zNMEPopper*)psyche->clt_owner;
    zMovePoint* mvpt;

    // not a PS2 DWARF local, but the time is read on its own line (named as in
    // zNMEGoalFlingAttack::Process)
    F32 tym_animCurr = npc->AnimTimeCurrent();
    if (tym_animCurr + dt >= npc->AnimDuration(NULL))
    {
        if (npc->pts_health < 1)
        {
            npc->lid.LidStart(1, npc->nav_evade);
            *trantype = GOAL_TRAN_SET;
            return NME_GOAL_AFTERLIFE;
        }

        mvpt = npc->PickEvadePoint(npc->nav_evade);
        npc->nav_evadePrev = npc->nav_evade;
        npc->nav_evade = mvpt;
        if (npc->nav_evadePrev == npc->nav_evade)
        {
            npc->nav_evadePrev = NULL;
        }

        *trantype = GOAL_TRAN_SET;
        return NME_GOAL_POPPER_EVADE;
    }

    return zNMEGoalCommon::Process(trantype, dt, ctxt);
}

S32 zNMEGoalPopEvade::Enter(F32 dt, void* updCtxt)
{
    zNMEPopper* npc = (zNMEPopper*)psyche->clt_owner;
    zMovePoint* mvpt;

    if (npc->nav_evade == NULL)
    {
        mvpt = npc->PickEvadePoint(NULL);
        npc->nav_evadePrev = npc->nav_evade;
        npc->nav_evade = mvpt;
        if (npc->nav_evadePrev == npc->nav_evade)
        {
            npc->nav_evadePrev = NULL;
        }
    }

    npc->lid.LidStart(1, npc->nav_evadePrev);

    npc->flg_misc |= 0x1;
    npc->flg_colCheck = 0;
    npc->flg_penCheck = 0;
    npc->chkby = 0;
    npc->penby = 0;

    tmr_emitTrail = tmr_emitGround = tmr_emitFount = -1.0f;
    pos_lastTime = *xEntGetPos(npc);

    DoExplicitAnim(g_hash_stananim[STAN_ANIM_POP_DOWN], 0);
    return zNMEGoalCommon::Enter(dt, updCtxt);
}

S32 zNMEGoalPopEvade::Exit(F32 dt, void* updCtxt)
{
    zNMEPopper* npc = (zNMEPopper*)psyche->clt_owner;

    npc->flg_misc = npc->DfltMiscFlags();
    xEntShow(npc);
    npc->RestoreColFlags();
    return 0;
}

// The PS2 DWARF lists only tym_animPlusTime; the state ID, anim time and duration are computed
// once up front, as in zNMEGoalLoopAnim::GoalLoop_Process.
S32 zNMEGoalPopEvade::Process(en_trantype* trantype, F32 dt, void* ctxt)
{
    zNMEPopper* npc = (zNMEPopper*)psyche->clt_owner;
    S32 nextgoal = 0;
    const NMECfgPopper* cfg = npc->cfg_popper;
    F32 tym_animPlusTime;
    xVec3 pos_goto;
    S32 arrived;

    U32 anid = npc->AnimCurStateID();
    F32 tym = npc->AnimTimeCurrent();
    F32 dur = npc->AnimDuration(NULL);
    tym_animPlusTime = tym + dt;

    if (anid == g_hash_stananim[STAN_ANIM_POP_DOWN])
    {
        EmergeGround(npc, dt);
        npc->MatFacePlayer(dt, PI, 1);
        if (tym_animPlusTime >= dur)
        {
            DoExplicitAnim(g_hash_stananim[STAN_ANIM_CHASE], 0);
        }
    }
    else if (anid == g_hash_stananim[STAN_ANIM_CHASE])
    {
        xEntHide(npc);

        pos_goto = *npc->nav_evade->pos;
        arrived = npc->MatMoveToPos(&pos_goto, dt, cfg->evade.spd_evade, 0, -1.0f);
        if (arrived)
        {
            xEntShow(npc);
            DoExplicitAnim(g_hash_stananim[STAN_ANIM_POP_UP], 0);
            zEntEvent(npc, npc->nav_evade, eEventArrive);

            npc->lid.LidStart(0, npc->nav_evade);
        }

        EvadeTrail(npc, dt);
        zFX_SpawnBubbleTrail((xVec3*)&npc->model->Mat->pos, 1);
    }
    else
    {
        EmergeFount(npc, dt);
        EmergeGround(npc, dt);
        npc->MatFacePlayer(dt, PI, 1);
        if (tym_animPlusTime >= dur)
        {
            *trantype = GOAL_TRAN_SET;
            nextgoal = NME_GOAL_TRIGGER_BATTLE;
            npc->GoalPick(this, nextgoal, *trantype);
            return nextgoal;
        }
    }

    pos_lastTime = *xEntGetPos(npc);
    return zNMEGoalCommon::Process(trantype, dt, ctxt);
}

// No GC symbol (inlined into Process); out of line in the PS2 demo.
inline void zNMEGoalPopEvade::EmergeFount(zNMECommon* npc, F32 dt)
{
    PopGasEmitter* popgas;
    F32 delay;
    en_npctyp ntyp;

    tmr_emitFount = MAX(-1.0f, tmr_emitFount - dt);
    if (tmr_emitFount < 0.0f)
    {
        ntyp = (en_npctyp)npc->itemType;
        switch (ntyp)
        {
        case NME_TYPE_POPPER_V1:
            delay = (gVidMode == eGameVidModePAL ? 0.02f : (1.0f / 60.0f));
            popgas = &PopEvade::Trench::gasFount;
            break;
        case NME_TYPE_POPPER_V2:
            delay = (gVidMode == eGameVidModePAL ? 0.02f : (1.0f / 60.0f));
            popgas = &PopEvade::Goof::gasFount;
            break;
        case NME_TYPE_POPPER_V3:
            delay = (gVidMode == eGameVidModePAL ? 0.02f : (1.0f / 60.0f));
            popgas = &PopEvade::Plank::gasFount;
            break;
        default:
            delay = (gVidMode == eGameVidModePAL ? 0.02f : (1.0f / 60.0f));
            popgas = &PopEvade::Trench::gasFount;
            break;
        }

        tmr_emitFount = delay;
        popgas->Fountain((xVec3*)&npc->model->Mat->pos, NULL);
    }
}

// No GC symbol (inlined into Process); out of line in the PS2 demo.
inline void zNMEGoalPopEvade::EmergeGround(zNMECommon* npc, F32 dt)
{
    PopGasEmitter* popgas;
    F32 delay;
    en_npctyp ntyp;

    tmr_emitGround = MAX(-1.0f, tmr_emitGround - dt);
    if (tmr_emitGround < 0.0f)
    {
        ntyp = (en_npctyp)npc->itemType;
        switch (ntyp)
        {
        case NME_TYPE_POPPER_V1:
            delay = (gVidMode == eGameVidModePAL ? 0.02f : (1.0f / 60.0f));
            popgas = &PopEvade::Trench::gasGround;
            break;
        case NME_TYPE_POPPER_V2:
            delay = (gVidMode == eGameVidModePAL ? 0.02f : (1.0f / 60.0f));
            popgas = &PopEvade::Goof::gasGround;
            break;
        case NME_TYPE_POPPER_V3:
            delay = (gVidMode == eGameVidModePAL ? 0.02f : (1.0f / 60.0f));
            popgas = &PopEvade::Plank::gasGround;
            break;
        default:
            delay = (gVidMode == eGameVidModePAL ? 0.02f : (1.0f / 60.0f));
            popgas = &PopEvade::Trench::gasGround;
            break;
        }

        tmr_emitGround = delay;
        popgas->Fountain((xVec3*)&npc->model->Mat->pos, NULL);
    }
}

// No GC symbol (inlined into Process); out of line in the PS2 demo.
inline void zNMEGoalPopEvade::EvadeTrail(zNMECommon* npc, F32 dt)
{
    ExhaustEmitter* exhaust;
    F32 delay;
    en_npctyp ntyp;

    tmr_emitTrail = MAX(-1.0f, tmr_emitTrail - dt);
    if (tmr_emitTrail < 0.0f)
    {
        ntyp = (en_npctyp)npc->itemType;
        switch (ntyp)
        {
        case NME_TYPE_POPPER_V1:
            delay = (gVidMode == eGameVidModePAL ? 0.02f : (1.0f / 60.0f));
            exhaust = &PopEvade::Trench::evadeTrail;
            break;
        case NME_TYPE_POPPER_V2:
            delay = (gVidMode == eGameVidModePAL ? 0.02f : (1.0f / 60.0f));
            exhaust = &PopEvade::Goof::evadeTrail;
            break;
        case NME_TYPE_POPPER_V3:
            delay = (gVidMode == eGameVidModePAL ? 0.02f : (1.0f / 60.0f));
            exhaust = &PopEvade::Plank::evadeTrail;
            break;
        default:
            delay = (gVidMode == eGameVidModePAL ? 0.02f : (1.0f / 60.0f));
            exhaust = &PopEvade::Trench::evadeTrail;
            break;
        }

        tmr_emitTrail = delay;
        exhaust->EmitLineUp(&pos_lastTime, (xVec3*)&npc->model->Mat->pos,
                            (xVec3*)&npc->model->Mat->up, 1.0f, -1, -100000.0f);
    }
}
