#include "zNMETypeStdFogger.h"
#include "zThrown.h"
#include "zUICustom.h"

namespace Fogger
{
    namespace Config
    {
        static const NMECfgFogger cfg_fogger_v1 = {
            { 1 },
            { 2.0f, 1.0f, 0, 4.5f, 10.0f, DEG2RAD(270) },
            { 3.0f, 1.0f, 5.0f },
        };
        static const NMECfgFogger cfg_fogger_v2 = {
            { 2 },
            { 1.3f, 1.0f, 1, 5.5f, 10.0f, DEG2RAD(270) },
            { 5.0f, 1.0f, 5.0f },
        };
        static const NMECfgFogger cfg_fogger_v3 = {
            { 3 },
            { 0.75f, 1.0f, 1, 6.5f, 10.0f, DEG2RAD(270) },
            { 7.0f, 1.0f, 5.0f },
        };
        const NMECfgFogger* cfg_fog[10] = { &cfg_fogger_v1, &cfg_fogger_v1, &cfg_fogger_v2,
                                            &cfg_fogger_v3 };
    } // namespace Config

    namespace Cheat
    {
        // Model scale with the "tiny foggers" cheat on; a zero scale means unscaled.
        WAD_STATIC const xVec3 scl_cheat = { 0.55f, 0.55f, 0.55f };
        static const xVec3 scl_normal = { 0.0f, 0.0f, 0.0f };
    } // namespace Cheat

    // Odds of the outcomes in choices when an aware fogger loses sight of the player, per
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

    // Unreferenced in the GC build, but kept in retail's .rodata.
    static const FogBreathEmitter foggyWisp = {
        NPAR_TYP_FOGBREATH,
        0.5f,
        { 0x65, 0x84, 0x00, 0x80 },
        { 0.06f, 0.2f },
        { 0.0f, 30.0f, 0.0f },
        0.7853982f,
        60,
        0,
        0,
        0,
        { 0, 0.0f, 0.0f, { 0.0f, 0.0f, 0.0f } },
    };

    NMEHelpBound::BndData bnd_info = {
        k_XBOUNDTYPE_SPHERE,
        { 0, 0, { 0, 0 } },
        { 1.15f, { 0.0f, 1.15f, 0.0f } },
        { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } },
    };
} // namespace Fogger

// Footstep puffs while the fogger stands aware of the player.
namespace FogAware
{
    static const S8 footdown[2] = { 2, 1 };
    static const F32 tym_footstep[2][2] = { { 0.1f, 0.14f }, { 0.34f, 0.38f } };
    static const xVec3 vec_leftFootBoneOffset = { -0.3f, 0.0f, 0.0f };
    static const xVec3 vec_rightFootBoneOffset = { 0.3f, 0.0f, 0.0f };

    static const PuffEmitter puffFeet = {
        NPAR_TYP_DUSTPUFF,
        0.5f,
        { 0x78, 0x78, 0x78, 0x9B },
        { 0.35f, 0.75f },
        { 0.0f, -2.0f, 0.0f },
        7.2f,
        93,
        0,
        5,
        0,
        { 0.2f, 5.0f },
    };
} // namespace FogAware

namespace FogBattle
{
    // Footstep puffs while the fogger bounds after the player.
    namespace Bound
    {
        static const S8 footdown[2] = { 2, 1 };
        static const F32 tym_footstep[2][2] = { { 0.1f, 0.14f }, { 0.34f, 0.38f } };
        static const xVec3 vec_leftFootBoneOffset = { 0.0f, 0.0f, 0.0f };
        static const xVec3 vec_rightFootBoneOffset = { 0.0f, 0.0f, 0.0f };
    } // namespace Bound

    // Landing puffs while the fogger hops to its evade spot.
    namespace Hop
    {
        static const F32 tym_footstep[2] = { 0.14f, 0.18f };
        static const xVec3 vec_emitOffset = { 0.0f, 0.15f, 0.0f };
    } // namespace Hop

    static const PuffEmitter puffBounding = {
        NPAR_TYP_DUSTPUFF,
        0.55f,
        { 0x96, 0x96, 0x96, 0xB4 },
        { 0.05f, 0.5f },
        { 0.0f, 0.0f, 0.0f },
        3.5f,
        96,
        0,
        5,
        0,
        { 0.15f, 2.5f },
    };

    static const PuffEmitter puffHopping = {
        NPAR_TYP_DUSTPUFF,
        0.74f,
        { 0x96, 0x96, 0x96, 0x96 },
        { 0.05f, 0.75f },
        { 0.0f, 2.0f, 0.0f },
        3.84f,
        93,
        0,
        10,
        0,
        { 0.15f, 4.5f },
    };
} // namespace FogBattle

// Fog breath spewed from the mouth bone while the attack animation's loop stage plays.
namespace FogAttack
{
    static const xVec3 vec_boneOffset = { 0.0f, 0.0f, 0.0f };

    static const FogBreathEmitter foggyPain = {
        NPAR_TYP_FOGBREATH,
        1.23f,
        { 0x65, 0x84, 0x00, 0xC8 },
        { 0.05f, 1.4f },
        { 0.0f, 3.5f, 0.0f },
        0.5f,
        90,
        1,
        0,
        0,
        { 4, 5.0f, 5.0f, { 0.5f, 0.35f, 1.0f } },
    };

    static const FogBreathEmitter foggyChunks = {
        NPAR_TYP_FOGBREATH,
        2.25f,
        { 0x73, 0xC8, 0x00, 0x80 },
        { 0.05f, 0.6f },
        { 0.0f, 6.5f, 0.0f },
        4.5f,
        87,
        0,
        0,
        0,
        { 4, 5.0f, 5.0f, { 0.5f, 0.35f, 1.0f } },
    };
} // namespace FogAttack

void zNMEFogger::CreateConfig(const char* name)
{
    en_nmevar vidx;

    zNMEStandard::CreateConfig(name);
    vidx = NMEVariantIdx((en_npctyp)itemType);
    cfg_fogger = Fogger::Config::cfg_fog[vidx];
}

S32 zNMEFogger::DfltVulnFlags()
{
    return 0xFFFFFFF7;
}

U8 zNMEFogger::ColChkFlags() const
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

U8 zNMEFogger::ColPenFlags() const
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

void zNMEFogger::Init(xEntAsset* entass)
{
    zNMEStandard::Init(entass);
    flg_move |= 0x2;
    zNMEParFX_PartyInvite(NPAR_TYP_FOGBREATH, NULL, 0);
    zNMEParFX_PartyInvite(NPAR_TYP_DUSTPUFF, NULL, 0);
}

void zNMEFogger::Setup()
{
    zNMEStandard::Setup();

    if (itemType != NME_TYPE_FOGGER_V2 && bucketHead)
    {
        xAntennaAddAntenna(this, 0.0f, 0.0f, 7, 8, false);
    }
}

void zNMEFogger::SelfSetup()
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
    g->fun_process = Fog_SeePlyr;
    g->cbdata = NULL;
    g = psy->FindGoal(NME_GOAL_PATROL);
    g->fun_process = Fog_SeePlyr;
    g->cbdata = NULL;
    g = psy->FindGoal(NME_GOAL_WANDER);
    g->fun_process = Fog_SeePlyr;
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

    goal = psy->AddGoal(NME_GOAL_SPOOKED, NULL);
    goal->fun_process = Fog_SeePlyr;
    goal->cbdata = NULL;

    psy->AddGoal(NME_GOAL_NOTICE, NULL);

    psy->AddGoal(NME_GOAL_FOGGER_AWARE, NULL);
    psy->AddGoal(NME_GOAL_FOGGER_BATTLE, NULL);
    psy->AddGoal(NME_GOAL_FOGGER_ATTACK, NULL);

    psy->BrainEnd();
    psy->SetSafety(NME_GOAL_IDLE);
}

U32 zNMEFogger::AnimPick(S32 gid, en_npcgspot gspot, xGoal* goal)
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
    case NME_GOAL_FOGGER_AWARE:
        idx = STAN_ANIM_ALERT;
        break;
    case NME_GOAL_FOGGER_BATTLE:
        if (gspot == NME_GSPOT_START)
        {
            idx = STAN_ANIM_IDLE;
        }
        else if (gspot == NME_GSPOT_RESUME)
        {
            idx = STAN_ANIM_IDLE;
        }
        else if (gspot == NME_GSPOT_STARTALT)
        {
            idx = STAN_ANIM_CHASE;
        }
        else if (gspot == NME_GSPOT_ALTA)
        {
            idx = STAN_ANIM_FLEE;
        }
        else
        {
            idx = STAN_ANIM_IDLE;
        }
        break;
    case NME_GOAL_FOGGER_ATTACK:
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
            idx = STAN_ANIM_ATTACK_LOOP;
        }
        break;
    default:
        idx = STAN_ANIM_IDLE;
        break;
    }

    return g_hash_stananim[idx];
}

xAnimTable* zNMEFogger::CreateAnimTable(xAnimTable* table)
{
    if (table == NULL)
    {
        table = xAnimTableNew("zNMEFogger", 0);
    }

    xAnimTableNewState(table, g_strz_stananim[STAN_ANIM_IDLE], 0x10, 1, 1.0f, NULL, NULL, 0.0f,
                       NULL, NULL, xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, g_strz_stananim[STAN_ANIM_MOVE], 0x10, 1, 1.0f, NULL, NULL, 0.0f,
                       NULL, NULL, xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, g_strz_stananim[STAN_ANIM_ALERT], 0x410, 0x2000001, 1.0f, NULL, NULL,
                       0.0f, NULL, NULL, xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, g_strz_stananim[STAN_ANIM_CHASE], 0x410, 0x2000001, 1.0f, NULL, NULL,
                       0.0f, NULL, NULL, xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, g_strz_stananim[STAN_ANIM_FLEE], 0x410, 0x2000001, 1.0f, NULL, NULL,
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

    S32 ourAnims[26] = { STAN_ANIM_IDLE,        STAN_ANIM_MOVE,        STAN_ANIM_ALERT,
                         STAN_ANIM_CHASE,       STAN_ANIM_FLEE,        STAN_ANIM_SCARE_LOOP,
                         STAN_ANIM_SCARE_DONE,  STAN_ANIM_SPOOK_LOOP,  STAN_ANIM_TAUNT,
                         STAN_ANIM_VICTORY,     STAN_ANIM_NOTICE,      STAN_ANIM_DAMAGE,
                         STAN_ANIM_DEFEAT,      STAN_ANIM_ATTACK_PREP, STAN_ANIM_ATTACK_LOOP,
                         STAN_ANIM_ATTACK_DONE, STAN_ANIM_STUN_PREP,   STAN_ANIM_STUN_LOOP,
                         STAN_ANIM_STUN_PICKUP, STAN_ANIM_STUN_CARRY,  STAN_ANIM_TOSS_PREP,
                         STAN_ANIM_TOSS_LOOP,   STAN_ANIM_SPIN_PREP,   STAN_ANIM_SPIN_LOOP,
                         STAN_ANIM_SPIN_THROW,  STAN_ANIM_UNKNOWN };
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

void zNMEFogger::Reset()
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

    pts_health = cfg_fogger->health.pts_healthMax;
}

S32 zNMEFogger::GoalPick(xGoal* goal, S32& gid_would, en_trantype& trantype)
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
        gid_would = NME_GOAL_FOGGER_AWARE;
        break;
    case NME_GOAL_TRIGGER_BATTLE:
        gid_would = NME_GOAL_FOGGER_BATTLE;
        break;
    case NME_GOAL_TRIGGER_ATTACK:
        gid_would = NME_GOAL_FOGGER_ATTACK;
        break;
    default:
        altered = 0;
        break;
    }

    return altered;
}

void zNMEFogger::BUpdate(xVec3* pos)
{
    Fogger::bnd_info.UpdateBound(&bound, this, pos);
}

void zNMEFogger::Process(F32 dt)
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

    // Scale.assign rather than a struct copy: retail copies the scale per component (z, y, x).
    if (zUICustomExtrasManager::IsCheatEnabled(eExtrasScreenCheats_TinyFoggers))
    {
        for (xModelInstance* minst = model; minst != NULL; minst = minst->Next)
        {
            minst->Scale.assign(Fogger::Cheat::scl_cheat.x, Fogger::Cheat::scl_cheat.y,
                                Fogger::Cheat::scl_cheat.z);
        }
    }
    else
    {
        for (xModelInstance* minst = model; minst != NULL; minst = minst->Next)
        {
            minst->Scale.assign(Fogger::Cheat::scl_normal.x, Fogger::Cheat::scl_normal.y,
                                Fogger::Cheat::scl_normal.z);
        }
    }

    StdNME_SuicideIsPainless();
}

void zNMEFogger::Type_AddYourMove(F32 dt)
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

void zNMEFogger::CollideReview()
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

S32 zNMEFogger::TypeHandleMail(NMEMsg* mail)
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
        handled = FogMailDamage(&mail->dmgdata);
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
inline S32 zNMEFogger::FogMailDamage(NMEDamageInfo* dmgmail)
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

// No symbol (always inlined); the name is ours. Squared XZ-plane distance.
static inline F32 FogXZDstSq(const xVec3* pos_a, const xVec3* pos_b)
{
    xVec3 delta = *pos_a - *pos_b;
    delta.y = 0.0f;
    return xVec3Length2(&delta);
}

// No symbol (always inlined); the name is ours. Does the arena have an area?
static inline S32 FogArenaIsValid(const zNMEArena* arena)
{
    return arena->rad_arena > 0.0f;
}

// No symbol (always inlined); the name is ours. Is pos within the arena's radius (grown by
// rad_slop) on the XZ plane?
static inline S32 FogArenaHasPos(const zNMEArena* arena, const xVec3* pos, F32 rad_slop)
{
    if (!FogArenaIsValid(arena))
    {
        return 0;
    }

    return FogXZDstSq(pos, &arena->pos_arena) <= SQR(arena->rad_arena + rad_slop);
}

en_vis zNMEFogger::VisionTarget(xEnt* ent, en_defcon defcon, S32 flags) const
{
    const zNMEArena* arena;

    if (NME::GlobalData::flg_wonder)
    {
        return VIS_NOBODY_AROUND;
    }

    if (runtimeData.overrideDetect == ALLOW_NEVER)
    {
        return VIS_NOBODY_AROUND;
    }

    arena = &this->arena;
    if (!FogArenaIsValid(arena))
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

    if (FogArenaHasPos(arena, xEntGetPos(ent), 0.0f))
    {
        if (runtimeData.overrideAttack == ALLOW_NEVER)
        {
            return VIS_GREY_AND_FOGGY;
        }
        return VIS_I_SEEE_YOUUU;
    }

    if (FogArenaHasPos(arena, xEntGetPos(ent), 5.0f))
    {
        return VIS_GREY_AND_FOGGY;
    }

    return VIS_NOBODY_AROUND;
}

F32 zNMEFogger::HealthRatio()
{
    F32 rat;

    if (cfg_fogger == NULL)
    {
        return 0.0f;
    }

    rat = pts_health;
    rat /= cfg_fogger->health.pts_healthMax;
    rat = CLAMP(rat, 0.0f, 1.0f);
    return rat;
}

S32 zNMEFogger::Fog_SeePlyr(xGoal* rawgoal, void* data, en_trantype* trantype, F32 dt, void* ctxt)
{
    S32 nextgoal = 0;
    zNMEFogger* npc;
    zNMEArena* arena;
    en_vis vis;
    // Not a PS2 DWARF local (see zNMESpinner::Spin_SeePlyr).
    S32 rc;

    if (xUtil_yesno(0.25f))
    {
        return nextgoal;
    }

    if (globals.player.s->Health < 1)
    {
        return nextgoal;
    }

    npc = (zNMEFogger*)rawgoal->psyche->clt_owner;

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

S32 zNMEGoalFogAware::Enter(F32 dt, void* updCtxt)
{
    zNMEFogger* npc = (zNMEFogger*)psyche->clt_owner;

    npc->arena.Cycle(npc, 1);
    return zNMEGoalCommon::Enter(dt, updCtxt);
}

S32 zNMEGoalFogAware::Process(en_trantype* trantype, F32 dt, void* ctxt)
{
    zNMEFogger* npc = (zNMEFogger*)psyche->clt_owner;
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

    FeetFXUpdate(npc, dt);
    npc->MatFacePlayer(dt, PI, 1);
    return zNMEGoalCommon::Process(trantype, dt, ctxt);
}

S32 zNMEGoalFogAware::ChkTranCases(zNMEFogger* npc, S32& nextgoal, en_trantype& trantype)
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
        wtlist = Fogger::Aware::wt_person[vidx];
        // The PS2 DWARF has no local for the choice, so vidx is reused.
        vidx = xUtil_choose(Fogger::Aware::choices, 3, wtlist);
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

void zNMEGoalFogAware::FeetFXUpdate(zNMEFogger* npc, F32 dt)
{
    F32 ds2_cam;
    U32 anid;
    S32 rc;
    F32 tym_animCurr;
    S8 currFoot;
    S32 i;
    xVec3 pos_emit;

    ds2_cam = NMEC_DstSq(&globals.oldSkoolCamera.mat.pos, xEntGetPos(npc), NULL);
    if (ds2_cam > SQR(10.0f))
    {
        return;
    }

    anid = npc->AnimCurStateID();
    if (anid != g_hash_stananim[STAN_ANIM_ALERT])
    {
        return;
    }

    rc = npc->model->Anim->Single->Blend->State ? 1 : 0;
    if (rc)
    {
        return;
    }

    tym_animCurr = npc->model->Anim->Single->Time;
    currFoot = 0;
    for (i = 0; i < 2; i++)
    {
        if (tym_animCurr < FogAware::tym_footstep[i][0])
        {
            continue;
        }

        if (tym_animCurr > FogAware::tym_footstep[i][1])
        {
            continue;
        }

        currFoot = FogAware::footdown[i];
        break;
    }

    if (currFoot == 0)
    {
        return;
    }

    if (currFoot == lastFoot)
    {
        return;
    }

    lastFoot = currFoot;
    if (currFoot == 1)
    {
        xMat4x3* mat_bone = (xMat4x3*)&npc->model->Mat[30];
        pos_emit = FogAware::vec_leftFootBoneOffset;
        xMat4x3Toworld(&pos_emit, mat_bone, &pos_emit);
    }
    else
    {
        xMat4x3* mat_bone = (xMat4x3*)&npc->model->Mat[33];
        pos_emit = FogAware::vec_rightFootBoneOffset;
        xMat4x3Toworld(&pos_emit, mat_bone, &pos_emit);
    }

    xMat4x3Toworld(&pos_emit, (xMat4x3*)npc->model->Mat, &pos_emit);

    xVec3 dir_emit = *(xVec3*)&npc->model->Mat->at * -0.25f;
    dir_emit += *(xVec3*)&npc->model->Mat->up * 0.15f;
    if (currFoot == 1)
    {
        dir_emit += *(xVec3*)&npc->model->Mat->right * 0.25f;
    }
    else
    {
        dir_emit -= *(xVec3*)&npc->model->Mat->right * 0.25f;
    }

    dir_emit.normalize();
    FogAware::puffFeet.Footstroke(&pos_emit, &dir_emit);
}

S32 zNMEGoalFogBattle::Enter(F32 dt, void* updCtxt)
{
    ((zNMEFogger*)psyche->clt_owner)->spd_throttle = 0.0f;
    battlefog = FOGGER_BATTLE_CHASE;
    pos_evade = g_O3;
    tmr_evade = -1.0f;
    flg_info |= 0x2;
    return zNMEGoalCommon::Enter(dt, updCtxt);
}

S32 zNMEGoalFogBattle::Resume(F32 dt, void* updCtxt)
{
    ((zNMEFogger*)psyche->clt_owner)->spd_throttle = 0.0f;
    flg_info |= 0x2;
    return zNMEGoalCommon::Resume(dt, updCtxt);
}

S32 zNMEGoalFogBattle::Process(en_trantype* trantype, F32 dt, void* ctxt)
{
    zNMEFogger* npc = (zNMEFogger*)psyche->clt_owner;
    S32 nextgoal = 0;
    S32 doTran;

    doTran = CheckSituation(npc, nextgoal, *trantype);
    if (doTran)
    {
        return nextgoal;
    }

    doTran = SubStateUpdate(npc, dt, nextgoal, *trantype);
    if (doTran)
    {
        return nextgoal;
    }

    return zNMEGoalCommon::Process(trantype, dt, ctxt);
}

// No GC symbol: inlined into Process, the out-of-line copy dead-stripped
S32 zNMEGoalFogBattle::CheckSituation(zNMEFogger* npc, S32& nextgoal, en_trantype& trantype)
{
    en_vis vis;

    if (npc->model->Anim->Single->Blend->State != NULL)
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
        return 1;
    }

    if (battlefog == FOGGER_BATTLE_EVADE)
    {
        return 0;
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

S32 zNMEGoalFogBattle::SubStateUpdate(zNMEFogger* npc, F32 dt, S32& nextgoal, en_trantype& trantype)
{
    S32 doGoalTransition = 0;
    en_battlefog old_battlefog = battlefog;
    S32 subenter = flg_info & 0x2;
    xVec3 pos_goingHere;
    S32 inSpot;
    S32 canEvade;
    const NMECfgFogger* cfg;
    F32 tym_ideal;
    S32 arrived;
    // not a DWARF local: the arena ratio is tested twice
    F32 pct;
    // not a DWARF local (named as in BoundingFXUpdate); the blend tests are materialized
    S32 rc;

    flg_info &= ~0x6;

    pct = npc->arena.PctFromHome(xEntGetPos(npc));
    if (pct > 1.0f)
    {
        battlefog = FOGGER_BATTLE_ARENA;
        DoAutoAnim(NME_GSPOT_STARTALT, 0);
    }
    else if (battlefog == FOGGER_BATTLE_ARENA && pct < 0.5f)
    {
        battlefog = FOGGER_BATTLE_CHASE;
    }

    switch (battlefog)
    {
    case FOGGER_BATTLE_ARENA:
        GetInArena(npc, dt);
        break;
    case FOGGER_BATTLE_CHASE:
        if (subenter)
        {
            DoAutoAnim(NME_GSPOT_STARTALT, 0);
        }

        ChasePlayer(npc, dt, &pos_goingHere);
        BoundingFXUpdate(npc, dt);

        rc = npc->model->Anim->Single->Blend->State ? 1 : 0;
        if (!rc)
        {
            inSpot = CheckSpot(npc, dt, &pos_goingHere);
            if (inSpot)
            {
                battlefog = FOGGER_BATTLE_EVADE;
                trantype = GOAL_TRAN_PUSH;
                nextgoal = NME_GOAL_FOGGER_ATTACK;
                npc->GoalPick(this, nextgoal, trantype);
                doGoalTransition = 1;
            }
        }
        break;
    case FOGGER_BATTLE_EVADE:
        if (subenter)
        {
            canEvade = CalcEvadePos(npc, &pos_evade);
            if (!canEvade)
            {
                battlefog = FOGGER_BATTLE_CHASE;
                DoAutoAnim(NME_GSPOT_STARTALT, 0);
                break;
            }

            cfg = npc->cfg_fogger;
            tym_ideal = xsqrt(npc->XZDstSqToPos(&pos_evade, NULL, NULL)) / cfg->evade.spd_evade;
            tmr_evade = CLAMP(tym_ideal + tym_ideal * (0.25f * (xurand() - 0.5f)),
                              cfg->evade.tym_evadeMin, cfg->evade.tym_evadeMax);
            DoAutoAnim(NME_GSPOT_ALTA, 0);
        }

        rc = npc->model->Anim->Single->Blend->State ? 1 : 0;
        arrived = MoveEvadePos(npc, &pos_evade, dt);
        if (!rc && (arrived || tmr_evade < 0.0f))
        {
            battlefog = FOGGER_BATTLE_CHASE;
        }

        tmr_evade = MAX(-1.0f, tmr_evade - dt);
        HoppingFXUpdate(npc, dt);
        break;
    }

    if (battlefog != old_battlefog)
    {
        flg_info |= 0x2;
    }

    return doGoalTransition;
}

// No PS2 DWARF entry (the PS2 build inlines it into SubStateUpdate); the local names are ours.
void zNMEGoalFogBattle::ChasePlayer(zNMEFogger* npc, F32 dt, xVec3* pos_tgt)
{
    const NMECfgFogger* cfg;
    xVec3 pos_plyr;
    xVec3 dir_plyr;
    F32 dst_plyr;

    dst_plyr = PredictPlyrPos(npc, &pos_plyr, &dir_plyr);
    *pos_tgt = pos_plyr;

    cfg = npc->cfg_fogger;
    npc->MatQRotTowards(dt, &dir_plyr, cfg->battle.ang_turnrate);
    if (dst_plyr < 1.0f)
    {
        npc->ThrottleAdjust(dt, 0.1f, cfg->battle.acc_chase);
    }
    else
    {
        npc->ThrottleAdjust(dt, cfg->battle.spd_chase, cfg->battle.acc_chase);
        xVec3 vel = dir_plyr * (dt * npc->spd_throttle);
        npc->model->Mat->pos.x += vel.x;
        npc->model->Mat->pos.y += vel.y;
        npc->model->Mat->pos.z += vel.z;
    }
}

F32 zNMEGoalFogBattle::PredictPlyrPos(zNMEFogger* npc, xVec3* pos_plyr, xVec3* dir_plyr)
{
    const NMECfgFogger* cfg = npc->cfg_fogger;
    S32 allowRevision;
    F32 dst_plyr;
    F32 dst_revised;

    dst_plyr = npc->TymToReachPlayer(cfg->battle.tym_predict);
    zEntPlayer_PredictPos(pos_plyr, dst_plyr, cfg->battle.fac_predlead, cfg->battle.flg_predturn);

    allowRevision = 1;

    dst_plyr = npc->XZDstSqToPos(pos_plyr, dir_plyr, NULL);
    dst_plyr = xsqrt(dst_plyr);
    if (dst_plyr > 0.2f)
    {
        *dir_plyr /= dst_plyr;
    }
    else
    {
        dst_plyr = 1.0f;
        allowRevision = 0;
        *dir_plyr = *(xVec3*)&npc->model->Mat->right;
    }

    if (allowRevision)
    {
        dst_revised = RevisePrediction(npc, pos_plyr, dir_plyr);
        if (dst_revised >= 0.0f)
        {
            dst_plyr = dst_revised;
        }
    }

    return dst_plyr;
}

// If the fogger would have to fly past the player to reach the predicted spot, lead the player
// on the far side instead. Returns the distance to the revised spot, or -1 if no revision.
F32 zNMEGoalFogBattle::RevisePrediction(zNMEFogger* npc, xVec3* pos_tgt, xVec3* dir_tgt)
{
    xVec3 dir_PtoP;
    xVec3 dir_NtoP;
    xVec3 dir_plyr;

    // not DWARF locals: the lengths are reused across statements
    F32 dst_PtoP = xsqrt(
        NMEC_DstSq(xEntGetPos(xglobals->___player_ent_dont_use_directly), pos_tgt, &dir_PtoP));
    if (dst_PtoP < 0.1f)
    {
        return -1.0f;
    }
    xVec3SMulBy(&dir_PtoP, 1.0f / dst_PtoP);

    F32 dst_plyr = xsqrt(npc->XZDstSqToPlayer(&dir_plyr, NULL));
    if (dst_plyr < 0.1f)
    {
        return -1.0f;
    }
    xVec3SMulBy(&dir_plyr, 1.0f / dst_plyr);

    F32 dst_NtoP = xsqrt(npc->XZDstSqToPos(pos_tgt, &dir_NtoP, NULL));
    if (dst_NtoP < 0.1f)
    {
        return -1.0f;
    }
    xVec3SMulBy(&dir_NtoP, 1.0f / dst_NtoP);

    if (dst_NtoP > dst_plyr)
    {
        *pos_tgt = *xEntGetPos(xglobals->___player_ent_dont_use_directly);
        *dir_tgt = dir_plyr;
    }

    if (dir_plyr.dot(dir_NtoP) > -0.3f)
    {
        return -1.0f;
    }

    // not a DWARF local: retail negates dir_plyr into its own stack vector, without the copy
    // xVec3::operator-() would make
    xVec3 dir_PtoN;
    xVec3Inv(&dir_PtoN, &dir_plyr);
    F32 dot = xVec3Dot(&dir_PtoN, &dir_PtoP);

    xVec3 pos_repred = dir_PtoP * (0.75f * (dot * dst_plyr));
    pos_repred += *xEntGetPos(xglobals->___player_ent_dont_use_directly);

    xVec3 dir_revised;
    F32 ds2_revised = npc->XZDstSqToPos(&pos_repred, &dir_revised, NULL);
    if (ds2_revised < 0.00001f)
    {
        return -1.0f;
    }

    F32 dst_revised = xsqrt(ds2_revised);
    *pos_tgt = pos_repred;
    *dir_tgt = dir_revised * (1.0f / dst_revised);

    return dst_revised;
}

// No PS2 DWARF entry (the PS2 build inlines it into SubStateUpdate); the local names are ours.
// Head back toward the arena center.
void zNMEGoalFogBattle::GetInArena(zNMEFogger* npc, F32 dt)
{
    xVec3 dir_home;
    xVec3 dir_move;
    F32 dst_home;

    xVec3Sub(&dir_home, &npc->arena.pos_arena, xEntGetPos(npc));
    dst_home = xVec3Length(&dir_home);
    if (dst_home < 1.0f)
    {
        dir_home = *(xVec3*)&npc->model->Mat->right;
    }
    else
    {
        xVec3SMulBy(&dir_home, 1.0f / dst_home);
    }

    xVec3Copy(&dir_move, &dir_home);
    npc->ThrottleAdjust(dt, 2.5f, -1.0f);
    npc->MatQRotTowards(dt, &dir_move, -1.0f);

    xVec3 vel = dir_move * (npc->spd_throttle * dt);
    xEntGetPos(npc)->x += vel.x;
    xEntGetPos(npc)->y += vel.y;
    xEntGetPos(npc)->z += vel.z;
}

// Pick an evade spot: four points on a circle around the arena center (rotated by a random
// angle), keeping the one farthest from the player that is not right where the fogger stands.
// ds2_best holds the radius until the search starts (the PS2 DWARF has no radius local).
S32 zNMEGoalFogBattle::CalcEvadePos(zNMEFogger* npc, xVec3* pos)
{
    S32 canEvade;
    xVec3 pos_home;
    F32 ang;
    F32 rad_ca;
    F32 rad_sa;
    xVec3 pos_loca[4];
    F32 ds2_best;
    S32 idx_best;
    S32 i;

    pos_home = npc->arena.pos_arena;
    ds2_best = MIN(10.0f, npc->arena.Radius(1.0f) - 1.0f);
    if (ds2_best < 2.0f)
    {
        return 0;
    }

    ang = (0.5f * PI) * xurand();
    rad_ca = icos(ang);
    rad_sa = isin(ang);
    rad_ca *= ds2_best;
    rad_sa *= ds2_best;

    for (i = 0; i < 4; i++)
    {
        pos_loca[i] = pos_home;
    }

    pos_loca[0].x += rad_ca;
    pos_loca[0].z += rad_sa;

    pos_loca[1].x -= rad_sa;
    pos_loca[1].z += rad_ca;

    pos_loca[2].x -= rad_ca;
    pos_loca[2].z -= rad_sa;

    pos_loca[3].x += rad_sa;
    pos_loca[3].z -= rad_ca;

    idx_best = -1;
    ds2_best = -1.0f;
    for (i = 0; i < 4; i++)
    {
        if (npc->XZDstSqToPos(&pos_loca[i], NULL, NULL) < 2.0f)
        {
            continue;
        }

        F32 ds2 =
            NMEC_DstSq(xEntGetPos(xglobals->___player_ent_dont_use_directly), &pos_loca[i], NULL);
        if (ds2 > ds2_best)
        {
            ds2_best = ds2;
            idx_best = i;
        }
    }

    if (idx_best < 0 || ds2_best < 0.0f)
    {
        canEvade = 0;
        *pos = *xEntGetPos(npc);
    }
    else
    {
        canEvade = 1;
        *pos = pos_loca[idx_best];
    }

    return canEvade;
}

S32 zNMEGoalFogBattle::MoveEvadePos(zNMEFogger* npc, const xVec3* pos, F32 dt)
{
    S32 arrived = 0;
    xVec3 dir_evade;
    // not a DWARF local: the squared distance is used for the arrival test and the xsqrt
    F32 ds2;

    ds2 = npc->XZDstSqToPos(pos, &dir_evade, NULL);
    if (ds2 < 1.0f)
    {
        npc->spd_throttle = 0.0f;
        arrived = 1;
    }
    else
    {
        xVec3SMulBy(&dir_evade, 1.0f / xsqrt(ds2));
        npc->ThrottleAdjust(dt, npc->cfg_fogger->evade.spd_evade, -1.0f);
        npc->MatQRotTowards(dt, &dir_evade, -1.0f);

        xVec3 delta = dir_evade * (npc->spd_throttle * dt);
        npc->model->Mat->pos.x += delta.x;
        npc->model->Mat->pos.y += delta.y;
        npc->model->Mat->pos.z += delta.z;
    }

    return arrived;
}

// No GC symbol: inlined into SubStateUpdate, the out-of-line copy dead-stripped. Is pos_tgt a
// good spot to start evading from (level with the fogger, close, and in front of it)?
S32 zNMEGoalFogBattle::CheckSpot(zNMEFogger* npc, F32 dt, const xVec3* pos_tgt)
{
    xVec3 dir_plyr;
    F32 dy;

    // not in the DWARF: the squared distance is tested twice and then normalized
    F32 ds2 = npc->XZDstSqToPos(pos_tgt, &dir_plyr, &dy);
    if (xabs(dy) > 3.0f)
    {
        return 0;
    }

    if (ds2 < SQR(0.3f))
    {
        return 1;
    }

    if (ds2 > SQR(2.5f))
    {
        return 0;
    }

    xVec3SMulBy(&dir_plyr, 1.0f / xsqrt(ds2));
    if (xVec3Dot(&dir_plyr, (xVec3*)&npc->model->Mat->at) < 0.86f)
    {
        return 0;
    }

    return 1;
}

void zNMEGoalFogBattle::BoundingFXUpdate(zNMEFogger* npc, F32 dt)
{
    F32 ds2_cam;
    U32 anid;
    S32 rc;
    F32 tym_animCurr;
    S8 currFoot;
    S32 i;
    xVec3 pos_emit;

    ds2_cam = NMEC_DstSq(&globals.oldSkoolCamera.mat.pos, xEntGetPos(npc), NULL);
    if (ds2_cam > SQR(22.0f))
    {
        return;
    }

    anid = npc->AnimCurStateID();
    if (anid != g_hash_stananim[STAN_ANIM_CHASE])
    {
        return;
    }

    rc = npc->model->Anim->Single->Blend->State ? 1 : 0;
    if (rc)
    {
        return;
    }

    tym_animCurr = npc->model->Anim->Single->Time;
    currFoot = 0;
    for (i = 0; i < 2; i++)
    {
        if (tym_animCurr < FogBattle::Bound::tym_footstep[i][0])
        {
            continue;
        }

        if (tym_animCurr > FogBattle::Bound::tym_footstep[i][1])
        {
            continue;
        }

        currFoot = FogBattle::Bound::footdown[i];
        break;
    }

    if (currFoot == 0)
    {
        return;
    }

    if (currFoot == lastFoot)
    {
        return;
    }

    lastFoot = currFoot;
    if (currFoot == 1)
    {
        xMat4x3* mat_bone = (xMat4x3*)&npc->model->Mat[30];
        pos_emit = FogBattle::Bound::vec_leftFootBoneOffset;
        xMat4x3Toworld(&pos_emit, mat_bone, &pos_emit);
    }
    else
    {
        xMat4x3* mat_bone = (xMat4x3*)&npc->model->Mat[33];
        pos_emit = FogBattle::Bound::vec_rightFootBoneOffset;
        xMat4x3Toworld(&pos_emit, mat_bone, &pos_emit);
    }

    xMat4x3Toworld(&pos_emit, (xMat4x3*)npc->model->Mat, &pos_emit);
    FogBattle::puffBounding.ImpactRing(&pos_emit, NULL);
}

// No GC symbol: inlined into SubStateUpdate, the out-of-line copy dead-stripped. A puff of
// dust each time the evade hop lands.
void zNMEGoalFogBattle::HoppingFXUpdate(zNMEFogger* npc, F32 dt)
{
    if (NMEC_DstSq(&globals.oldSkoolCamera.mat.pos, xEntGetPos(npc), NULL) > SQR(22.0f))
    {
        return;
    }

    // not in this function's (older) PS2 DWARF; the state ID is read before the hash table, as
    // in BoundingFXUpdate
    U32 anid = npc->AnimCurStateID();
    if (anid != g_hash_stananim[STAN_ANIM_FLEE])
    {
        return;
    }

    // not a DWARF local (named as in BoundingFXUpdate); the blend test is materialized
    S32 rc = npc->model->Anim->Single->Blend->State ? 1 : 0;
    if (rc)
    {
        return;
    }

    if (npc->AnimTimeCurrent() < FogBattle::Hop::tym_footstep[0])
    {
        flg_needHopFX = 1;
        return;
    }

    if (!flg_needHopFX)
    {
        return;
    }

    if (npc->AnimTimeCurrent() > FogBattle::Hop::tym_footstep[1])
    {
        return;
    }

    xVec3 pos_emit = *(xVec3*)&npc->model->Mat->pos + FogBattle::Hop::vec_emitOffset;
    xVec3 vel_drift = *(xVec3*)&npc->model->Mat->at * npc->spd_throttle;
    FogBattle::puffHopping.ImpactRing(&pos_emit, &vel_drift);
}

S32 zNMEGoalFogAttack::Enter(F32 dt, void* updCtxt)
{
    zNMEStandard* npc = (zNMEStandard*)psyche->clt_owner;

    tmr_attack = 1.0f;
    bul_startedBreath = 0;
    GoalLoop_Enter(npc);
    return 0;
}

S32 zNMEGoalFogAttack::Exit(F32 dt, void* updCtxt)
{
    if (bul_startedBreath)
    {
        zNMEFogger* npc = (zNMEFogger*)psyche->clt_owner;

        if (npc->snd_table->sndGroupHandle[2] != -1)
        {
            xSndMgrStop(npc->sndID[2]);
            npc->sndID[2] = (iSndHandle)-1;
        }
    }

    return zNMEGoalLoopAnim::Exit(dt, updCtxt);
}

S32 zNMEGoalFogAttack::Process(en_trantype* trantype, F32 dt, void* ctxt)
{
    tmr_attack = MAX(-1.0f, tmr_attack - dt);
    if (tmr_attack < 0.0f)
    {
        cnt_loop = -1;
    }
    else
    {
        cnt_loop = 1;
        flg_info |= 0x10;
    }

    if (anid_played == anid_stage[1])
    {
        BreathAttack();
    }

    return zNMEGoalLoopAnim::Process(trantype, dt, ctxt);
}

void zNMEGoalFogAttack::BreathAttack()
{
    zNMEStandard* npc = (zNMEStandard*)psyche->clt_owner;
    xVec3 pos_emit;

    if (!bul_startedBreath)
    {
        npc->SndPlayFromTable(2); // eNMESound_Attack
        bul_startedBreath = 1;
    }

    xMat4x3* mat_bone = (xMat4x3*)&npc->model->Mat[4];
    pos_emit = FogAttack::vec_boneOffset;
    xMat4x3Toworld(&pos_emit, mat_bone, &pos_emit);
    xMat4x3Toworld(&pos_emit, (xMat4x3*)npc->model->Mat, &pos_emit);
    FogAttack::foggyPain.SpewVolume(&pos_emit, (xMat3x3*)npc->model->Mat);
    FogAttack::foggyChunks.SpewVolume(&pos_emit, (xMat3x3*)npc->model->Mat);
}
