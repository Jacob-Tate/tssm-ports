#include "zNMETypeStdMervyn.h"
#include "zEntPlayer.h"
#include "xutil.h"
#include "xAntenna.h"
#include "zDust.h"
#include "xEntBoulder.h"
#include "xDraw.h"
#include "xDebug.h"

namespace Mervyn
{
    namespace Config
    {
        static const NMECfgMervyn cfg_mervyn_v3 = {
            { 9 },
            { 0.5f, 9.0f, 10.0f, 35.0f, 40.0f, 45.0f, 50.0f },
            { 7.0f, 3.0f, DEG2RAD(720) },
            { HAZ_TYP_MERVBOMB_V3, 10.0f },
        };
        const NMECfgMervyn* cfg_merv[10] = { &cfg_mervyn_v3, &cfg_mervyn_v3, &cfg_mervyn_v3,
                                             &cfg_mervyn_v3 };
    } // namespace Config

    // Unreferenced in the GC build, but kept in retail's .rodata.
    namespace GunSight
    {
        static const xVec3 vec_offset = { 0.06f, -0.06f, 1.41f };
    } // namespace GunSight

    namespace MissilePod
    {
        // Offsets from the missile pod bones (20 left, 22 right) to the launch point.
        static const xVec3 vec_offLeft = { 0.0f, 0.0f, 0.5f };
        static const xVec3 vec_offRight = { 0.0f, 0.0f, 0.5f };

        // No GC or PS2 symbol (inlined into zNMEGoalMervBomb::BombsAway); the name is ours.
        // World position of the left or right missile pod's launch point.
        static inline void LaunchPos(zNMEMervyn* npc, S32 leftside, xVec3* pos_launch)
        {
            S32 idx_bone;
            xVec3 vec_off;

            if (leftside)
            {
                idx_bone = 20;
                vec_off = vec_offLeft;
            }
            else
            {
                idx_bone = 22;
                vec_off = vec_offRight;
            }

            // Through the BonePos accessor, as BfBB's zNPCJelly::JellyBoneWorldPos does.
            xVec3 pos = *(xVec3*)npc->BonePos(idx_bone);
            pos += vec_off;
            xMat4x3Toworld(&pos, (xMat4x3*)npc->model->Mat, &pos);
            *pos_launch = pos;
        }
    } // namespace MissilePod

    // Unreferenced in the GC build, but kept in retail's .rodata.
    namespace HelioFX
    {
        static const xVec3 vec_offRight = { 1.0f, 1.0f, 0.0f };
        static const xVec3 vec_offLeft = { -1.0f, 1.0f, 0.0f };
    } // namespace HelioFX

    namespace HelioDust
    {
        struct DustData
        {
            struct Flags
            {
                S8 enable;
                S8 dev_enable;
                S8 unused[2];
            } flags;
            struct CfgInfo
            {
                zDustRingEmitter::config_type dustConfig;
            } cfginfo;
            struct UpdateInfo
            {
                S32 frq_dustEmit;
                F32 fac_force;
            } updinfo;
        };

        DustData dust_helio = {
            { 0, 0, { 0, 0 } },
            { { 0.0f, 0.75f, 0.0f, 1.0f, 0.0f } },
            { 4, 0.5f },
        };

        static const xVec3 vec_offset = { 0.0f, -3.0f, 0.0f };
    } // namespace HelioDust

    NMEHelpBound::BndData bnd_info = {
        k_XBOUNDTYPE_SPHERE,
        { 0, 0, { 0, 0 } },
        { 2.0f, { 0.0f, 0.25f, 0.0f } },
        { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } },
    };
} // namespace Mervyn

// No symbol (always inlined); the name is ours. The move-brain goal that goes with a goal
// entered by the main brain (0: none).
static inline S32 MervMoveGoalFor(S32 gid)
{
    switch (gid)
    {
    case NME_GOAL_MERV_NORMAL:
    case NME_GOAL_MERV_AWARE:
    case NME_GOAL_MERV_BATTLE:
    case NME_GOAL_MERV_ZAP:
    case NME_GOAL_MERV_BOMB:
    case NME_GOAL_MERV_BOWL:
    case NME_GOAL_MERV_WOUND:
    case NME_GOAL_TAUNT:
        return NME_GOAL_MERV_MOVE;
    case NME_GOAL_DEV_ANIMVIEW:
    case NME_GOAL_DEV_HEROMODE:
    case NME_GOAL_AFTERLIFE:
    case NME_GOAL_SPAWN:
    case NME_GOAL_SPOOKED:
    case NME_GOAL_NOTICE:
    case NME_GOAL_SCAREWAIT:
    case NME_GOAL_SCARE:
        return NME_GOAL_NOMANLAND;
    }

    return 0;
}

void zNMEMervyn::CreateConfig(const char* name)
{
    en_nmevar vidx;

    zNMEStandard::CreateConfig(name);
    vidx = NMEVariantIdx((en_npctyp)itemType);
    cfg_mervyn = Mervyn::Config::cfg_merv[vidx];
}

S32 zNMEMervyn::DfltVulnFlags()
{
    return 0xCFFFFFF7;
}

void zNMEMervyn::Init(xEntAsset* asset)
{
    zNMEStandard::Init(asset);

    ang_blades = 0.0f;
    gun_omatic.Init(this);

    engine.mOwnerNPC = this;
    engine.mState = zMervEngine::eState_Off;

    zNMEParFX_PartyInvite(NPAR_TYP_PLASMA, NULL, 0);
    zNMEParFX_PartyInvite(NPAR_TYP_ENERGY, NULL, 0);
    zNMEParFX_PartyInvite(NPAR_TYP_MUZZLE, NULL, 0);
}

void zNMEMervyn::Setup()
{
    zNMEStandard::Setup();
    gun_omatic.Setup();
    xAntennaAddAntenna(this, 0.0f, 0.0f, 8, 9, false);
}

void zNMEMervyn::SelfSetup()
{
    MervMainBrain();
    MervMoveBrain();
}

void zNMEMervyn::SelfDestroy()
{
    xBehaveMgr* bmgr;

    bmgr = xBehaveMgr_GetSelf();
    bmgr->UnSubscribe(psy_move);
    psy_move = NULL;

    zNMECommon::SelfDestroy();
}

// Compiled-out asserts (conditions unknown). With them the function is too big to be
// auto-inlined into SelfSetup; retail calls it.
void zNMEMervyn::MervMainBrain()
{
    xASSERT_PLACEHOLDER();
    xASSERT_PLACEHOLDER();
    xASSERT_PLACEHOLDER();
    xASSERT_PLACEHOLDER();
    xASSERT_PLACEHOLDER();
    xASSERT_PLACEHOLDER();
    xASSERT_PLACEHOLDER();
    xASSERT_PLACEHOLDER();
    xASSERT_PLACEHOLDER();
    xASSERT_PLACEHOLDER();

    xBehaveMgr* bmgr;
    xPsyche* psy;

    bmgr = xBehaveMgr_GetSelf();
    psy_self = bmgr->Subscribe(this, 0);

    psy = psy_self;

    psy->BrainBegin();

    psy->AddGoal(NME_GOAL_AFTERLIFE, NULL);
    psy->AddGoal(NME_GOAL_SPAWN, NULL);
    psy->AddGoal(NME_GOAL_SCAREWAIT, NULL);
    psy->AddGoal(NME_GOAL_SCARE, NULL);
    psy->AddGoal(NME_GOAL_TAUNT, NULL);
    psy->AddGoal(NME_GOAL_NOTICE, NULL);
    psy->AddGoal(NME_GOAL_SPOOKED, NULL);

    psy->AddGoal(NME_GOAL_MERV_NORMAL, NULL);
    psy->AddGoal(NME_GOAL_MERV_AWARE, NULL);
    psy->AddGoal(NME_GOAL_MERV_WOUND, NULL);
    psy->AddGoal(NME_GOAL_MERV_BATTLE, NULL);
    psy->AddGoal(NME_GOAL_MERV_ZAP, NULL);
    psy->AddGoal(NME_GOAL_MERV_BOMB, NULL);
    psy->AddGoal(NME_GOAL_MERV_BOWL, NULL);

    psy->BrainEnd();

    psy->SetSafety(NME_GOAL_MERV_NORMAL);
}

U32 zNMEMervyn::AnimPick(S32 gid, en_npcgspot gspot, xGoal* rawgoal)
{
    en_stananim idx = STAN_ANIM_UNKNOWN;

    switch (gid)
    {
    case NME_GOAL_AFTERLIFE:
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
    case NME_GOAL_MERV_WOUND:
        if (pts_health < 1)
        {
            idx = STAN_ANIM_DEFEAT;
        }
        else
        {
            idx = STAN_ANIM_DAMAGE;
        }
        break;
    case NME_GOAL_MERV_NORMAL:
        if (gspot == NME_GSPOT_START)
        {
            idx = STAN_ANIM_IDLE;
        }
        else if (gspot == NME_GSPOT_STARTALT)
        {
            idx = STAN_ANIM_MOVE;
        }
        else if (gspot == NME_GSPOT_PATROLPAUSE)
        {
            idx = STAN_ANIM_IDLE;
        }
        else
        {
            idx = STAN_ANIM_MOVE;
        }
        break;
    case NME_GOAL_MERV_AWARE:
        if (gspot == NME_GSPOT_START)
        {
            idx = STAN_ANIM_ALERT;
        }
        else if (gspot == NME_GSPOT_STARTALT)
        {
            idx = STAN_ANIM_CHASE;
        }
        else if (gspot == NME_GSPOT_PATROLPAUSE)
        {
            idx = STAN_ANIM_ALERT;
        }
        else
        {
            idx = STAN_ANIM_CHASE;
        }
        break;
    case NME_GOAL_MERV_BATTLE:
        if (gspot == NME_GSPOT_START)
        {
            idx = STAN_ANIM_ALERT;
        }
        else if (gspot == NME_GSPOT_STARTALT)
        {
            idx = STAN_ANIM_CHASE;
        }
        else if (gspot == NME_GSPOT_PATROLPAUSE)
        {
            idx = STAN_ANIM_ALERT;
        }
        else
        {
            idx = STAN_ANIM_CHASE;
        }
        break;
    case NME_GOAL_MERV_ZAP:
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
        break;
    case NME_GOAL_MERV_BOWL:
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
        break;
    case NME_GOAL_MERV_BOMB:
        if (((zNMEGoalMervBomb*)rawgoal)->flg_leftside)
        {
            if (gspot == NME_GSPOT_START)
            {
                idx = STAN_ANIM_BOMBLEFT_PREP;
            }
            else if (gspot == NME_GSPOT_RESUME)
            {
                idx = STAN_ANIM_BOMBLEFT_PREP;
            }
            else if (gspot == NME_GSPOT_LOOP)
            {
                idx = STAN_ANIM_BOMBLEFT_LOOP;
            }
            else if (gspot == NME_GSPOT_FINISH)
            {
                idx = STAN_ANIM_BOMBLEFT_DONE;
            }
        }
        else
        {
            if (gspot == NME_GSPOT_START)
            {
                idx = STAN_ANIM_BOMBRYHT_PREP;
            }
            else if (gspot == NME_GSPOT_RESUME)
            {
                idx = STAN_ANIM_BOMBRYHT_PREP;
            }
            else if (gspot == NME_GSPOT_LOOP)
            {
                idx = STAN_ANIM_BOMBRYHT_LOOP;
            }
            else if (gspot == NME_GSPOT_FINISH)
            {
                idx = STAN_ANIM_BOMBRYHT_DONE;
            }
        }
        break;
    default:
        idx = STAN_ANIM_IDLE;
        break;
    }

    return g_hash_stananim[idx];
}

xAnimTable* zNMEMervyn::CreateAnimTable(xAnimTable* table)
{
    if (table == NULL)
    {
        table = xAnimTableNew("zNMEMervyn", 0);
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
    xAnimTableNewState(table, g_strz_stananim[STAN_ANIM_PRAY], 0x10, 1, 1.0f, NULL, NULL, 0.0f,
                       NULL, NULL, xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, g_strz_stananim[STAN_ANIM_DAMAGE], 0x10, 1, 1.0f, NULL, NULL, 0.0f,
                       NULL, NULL, xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, g_strz_stananim[STAN_ANIM_DEFEAT], 0x10, 1, 1.0f, NULL, NULL, 0.0f,
                       NULL, NULL, xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, g_strz_stananim[STAN_ANIM_ATTACK_PREP], 0, 1, 1.0f, NULL, NULL, 0.0f,
                       NULL, NULL, xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, g_strz_stananim[STAN_ANIM_ATTACK_LOOP], 0x10, 1, 1.0f, NULL, NULL,
                       0.0f, NULL, NULL, xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, g_strz_stananim[STAN_ANIM_ATTACK_DONE], 0, 1, 1.0f, NULL, NULL, 0.0f,
                       NULL, NULL, xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, g_strz_stananim[STAN_ANIM_BOMBLEFT_PREP], 0x20, 1, 1.0f, NULL, NULL,
                       0.0f, NULL, NULL, xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, g_strz_stananim[STAN_ANIM_BOMBLEFT_LOOP], 0x20, 1, 1.0f, NULL, NULL,
                       0.0f, NULL, NULL, xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, g_strz_stananim[STAN_ANIM_BOMBLEFT_DONE], 0, 1, 1.0f, NULL, NULL,
                       0.0f, NULL, NULL, xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, g_strz_stananim[STAN_ANIM_BOMBRYHT_PREP], 0x20, 1, 1.0f, NULL, NULL,
                       0.0f, NULL, NULL, xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, g_strz_stananim[STAN_ANIM_BOMBRYHT_LOOP], 0x20, 1, 1.0f, NULL, NULL,
                       0.0f, NULL, NULL, xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, g_strz_stananim[STAN_ANIM_BOMBRYHT_DONE], 0, 1, 1.0f, NULL, NULL,
                       0.0f, NULL, NULL, xAnimDefaultBeforeEnter, NULL, NULL);

    S32 ourAnims[23] = { STAN_ANIM_IDLE,          STAN_ANIM_MOVE,          STAN_ANIM_ALERT,
                         STAN_ANIM_CHASE,         STAN_ANIM_SCARE_LOOP,    STAN_ANIM_SCARE_DONE,
                         STAN_ANIM_SPOOK_LOOP,    STAN_ANIM_NOTICE,        STAN_ANIM_TAUNT,
                         STAN_ANIM_VICTORY,       STAN_ANIM_PRAY,          STAN_ANIM_DAMAGE,
                         STAN_ANIM_DEFEAT,        STAN_ANIM_ATTACK_PREP,   STAN_ANIM_ATTACK_LOOP,
                         STAN_ANIM_ATTACK_DONE,   STAN_ANIM_BOMBLEFT_PREP, STAN_ANIM_BOMBLEFT_LOOP,
                         STAN_ANIM_BOMBLEFT_DONE, STAN_ANIM_BOMBRYHT_PREP, STAN_ANIM_BOMBRYHT_LOOP,
                         STAN_ANIM_BOMBRYHT_DONE, STAN_ANIM_UNKNOWN };
    NMEC_BuildStandardAnimTran(table, g_strz_stananim, ourAnims, 3, 0.2f);

    xAnimTableNewTransition(table, g_strz_stananim[STAN_ANIM_BOMBLEFT_PREP],
                            g_strz_stananim[STAN_ANIM_BOMBLEFT_LOOP], NULL, NULL, 0x10, 0, 0.0f,
                            0.0f, 0, 0, 0.2f, NULL);
    xAnimTableNewTransition(table, g_strz_stananim[STAN_ANIM_BOMBLEFT_LOOP],
                            g_strz_stananim[STAN_ANIM_BOMBLEFT_DONE], NULL, NULL, 0x10, 0, 0.0f,
                            0.0f, 0, 0, 0.2f, NULL);
    xAnimTableNewTransition(table, g_strz_stananim[STAN_ANIM_BOMBRYHT_PREP],
                            g_strz_stananim[STAN_ANIM_BOMBRYHT_LOOP], NULL, NULL, 0x10, 0, 0.0f,
                            0.0f, 0, 0, 0.2f, NULL);
    xAnimTableNewTransition(table, g_strz_stananim[STAN_ANIM_BOMBRYHT_LOOP],
                            g_strz_stananim[STAN_ANIM_BOMBRYHT_DONE], NULL, NULL, 0x10, 0, 0.0f,
                            0.0f, 0, 0, 0.2f, NULL);

    return table;
}

S32 zNMEMervyn::GoalPick(xGoal* goal, S32& gid_would, en_trantype& typ_tran)
{
    S32 altered = 1;

    switch (gid_would)
    {
    case NME_GOAL_TRIGGER_NORMAL:
        gid_would = NME_GOAL_MERV_NORMAL;
        typ_tran = GOAL_TRAN_SET;
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
        gid_would = NME_GOAL_MERV_AWARE;
        typ_tran = GOAL_TRAN_SET;
        break;
    case NME_GOAL_TRIGGER_BATTLE:
        gid_would = NME_GOAL_MERV_BATTLE;
        typ_tran = GOAL_TRAN_SET;
        break;
    case NME_GOAL_TRIGGER_ATTACK:
        gid_would = NME_GOAL_MERV_ZAP;
        typ_tran = GOAL_TRAN_PUSH;
        break;
    default:
        altered = 0;
        break;
    }

    return altered;
}

void zNMEMervyn::Reset()
{
    xPsyche* psy;
    zNMEGoalAfterlife* goal;

    zNMEStandard::Reset();

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
        psy->GoalSet(NME_GOAL_MERV_NORMAL, 1);
    }

    pts_health = cfg_mervyn->health.pts_healthMax;
    ang_blades = 0.0f;
    dust_propwash = NULL;
    gun_omatic.Reset();
    engine.Reset();
}

void zNMEMervyn::BUpdate(xVec3* pos)
{
    xMat4x3* mat_bone;
    xMat4x3* mat_root;
    xVec3 pos_bone;

    mat_root = (xMat4x3*)model->Mat;
    mat_bone = (xMat4x3*)&model->Mat[3];
    xMat4x3Toworld(&pos_bone, mat_root, &mat_bone->pos);
    Mervyn::bnd_info.UpdateBound(&bound, this, &pos_bone);
}

void zNMEMervyn::DieTheGoodDeath()
{
    engine.Reset();
    zNMEStandard::DieTheGoodDeath();
}

void zNMEMervyn::Process(F32 dt)
{
    xMat4x3 oldMat;
    xEntFrame tmpFrame;

    if (flg_colCheck)
    {
        oldMat = *(xMat4x3*)model->Mat;
    }

    psy_move->Timestep(dt, NULL);
    psy_self->Timestep(dt, NULL);

    if (IsAlive())
    {
        flg_newtime = 1;

        zNMECommon::Process(dt);
    }

    engine.Process(dt);
    UpdateGunnerStatus();

    if (IsAlive())
    {
        ShadStufUpdate();
        HelioDustUpdate(dt);
    }
    else
    {
        if (simpShadow != NULL || entShadow != NULL)
        {
            ShadStufIAmDead();
        }

        if (dust_propwash != NULL)
        {
            if (dust_propwash->data != NULL)
            {
                dust_propwash->deactivate();
            }
            NMEC_DustRingRelease(dust_propwash);
            dust_propwash = NULL;
        }
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

void zNMEMervyn::NewTime(F32 dt)
{
    BladesUpdate(dt);
    gun_omatic.NewTimeUpdate(dt);
}

// Compiled-out asserts (conditions unknown). With them the function is too big to be
// auto-inlined into Process; retail calls it.
void zNMEMervyn::HelioDustUpdate(F32 dt)
{
    Mervyn::HelioDust::DustData* dusthelp;
    dusthelp = &Mervyn::HelioDust::dust_helio;

    if (dusthelp->flags.enable)
    {
        xASSERT_PLACEHOLDER();
        xASSERT_PLACEHOLDER();

        if (XYZDstSqToPlayer(NULL) > 400.0f)
        {
            if (dust_propwash != NULL)
            {
                if (dust_propwash->data != NULL)
                {
                    dust_propwash->deactivate();
                }
                NMEC_DustRingRelease(dust_propwash);
                dust_propwash = NULL;
            }
        }
        else
        {
            if (dust_propwash == NULL)
            {
                dust_propwash = NMEC_DustRingReserve();
                if (dust_propwash != NULL)
                {
                    dust_propwash->set_config(&dusthelp->cfginfo.dustConfig);
                    dust_propwash->activate();
                    cnt_nextDustEmit = -1;
                }
            }

            if (dust_propwash != NULL)
            {
                cnt_nextDustEmit--;
                if (cnt_nextDustEmit <= 0)
                {
                    xASSERT_PLACEHOLDER();
                    xASSERT_PLACEHOLDER();
                    cnt_nextDustEmit = dusthelp->updinfo.frq_dustEmit;
                    xVec3 pos = *xEntGetPos(this) + Mervyn::HelioDust::vec_offset;
                    dust_propwash->kickup(pos, dusthelp->updinfo.fac_force, dt);
                }
            }
        }
    }
}

void zNMEMervyn::RenderExtra()
{
    if (NME::GlobalData::flg_wonder & 1)
    {
        return;
    }

    if (model->Flags & 0x400)
    {
        return;
    }

    gun_omatic.Render();
}

en_vis zNMEMervyn::VisionTarget(xEnt* ent, en_defcon defcon, S32 flags) const
{
    const NMECfgMervyn* cfg;
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

    cfg = cfg_mervyn;
    XZVecToPos(&dir, xEntGetPos(ent), &dy);
    dst = dir.length();

    if (runtimeData.overrideDetect == ALLOW_NORMAL && runtimeData.overrideAttack == ALLOW_NORMAL &&
        dst > cfg->ranges.rad_awaregrey)
    {
        return VIS_NOBODY_AROUND;
    }

    if (runtimeData.overrideAttack != ALLOW_NEVER)
    {
        if (runtimeData.overrideAttack == ALLOW_ALWAYS || dst < cfg->ranges.rad_lob)
        {
            xVec3SMulBy(&dir, 1.0f / dst);
            if (xVec3Dot(&dir, (xVec3*)&model->Mat->at) < 0.9f)
            {
                return VIS_HORROR_FLIK;
            }
            return VIS_PREPARE_TO_DIE;
        }

        if (dst < cfg->ranges.rad_lobgrey)
        {
            xVec3SMulBy(&dir, 1.0f / dst);
            if (xVec3Dot(&dir, (xVec3*)&model->Mat->at) < 0.9f)
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

F32 zNMEMervyn::HealthRatio()
{
    F32 rat;

    if (cfg_mervyn == NULL)
    {
        return 0.0f;
    }

    rat = pts_health;
    rat /= cfg_mervyn->health.pts_healthMax;
    rat = CLAMP(rat, 0.0f, 1.0f);
    return rat;
}

S32 zNMEMervyn::ChkBowlingBall(xVec3* dir_NtoBall, F32* dst_NtoBall)
{
    xEntBoulder* bowl;
    xVec3 vec_NtoB;

    bowl = zEntPlayerGetBBowl();
    if (bowl == NULL)
    {
        return 0;
    }

    if (!xEntIsVisible(bowl))
    {
        return 0;
    }

    XYZVecToPos(&vec_NtoB, (xVec3*)&bowl->model->Mat->pos);

    if (vec_NtoB.y < -5.0f)
    {
        return 0;
    }
    if (vec_NtoB.y > -0.25f)
    {
        return 0;
    }
    if (SQ(vec_NtoB.x) + SQ(vec_NtoB.z) > SQ(12.0f))
    {
        return 0;
    }

    xVec3 dir_tgt = vec_NtoB * (1.0f / vec_NtoB.length());
    xVec3 dir_travel;
    F32 spd_ball = xVec3Normalize(&dir_travel, &bowl->vel);

    if (xVec3Dot(&dir_travel, &dir_tgt) > 0.0f && spd_ball > 4.0f)
    {
        return 0;
    }

    F32 dst = vec_NtoB.length();

    if (dst_NtoBall != NULL && dir_NtoBall != NULL)
    {
        if (dst < 0.00001f)
        {
            *dir_NtoBall = g_NY3;
            *dst_NtoBall = 1.0f;
        }
        else
        {
            *dir_NtoBall = vec_NtoB * (1.0f / dst);
            *dst_NtoBall = dst;
        }
    }

    return 1;
}

S32 zNMEMervyn::TypeHandleMail(NMEMsg* mail)
{
    S32 handled = 1;

    switch (mail->msgid)
    {
    case NME_MID_DAMAGE:
        handled = MervMailDamage(&mail->dmgdata);
        break;
    case NME_MID_SYSEVENT:
        handled = MervMailSys(&mail->sysevent);
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

// No GC symbol (inlined into TypeHandleMail).
inline S32 zNMEMervyn::MervMailDamage(NMEDamageInfo* dmgmail)
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

        psy_self->GoalSet(NME_GOAL_MERV_WOUND, 0);
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

S32 zNMEMervyn::MervMailSys(NMESysEvent* sysmail)
{
    switch (sysmail->toEvent)
    {
    case eEventNPCSetActiveOff:
        engine.SetState(zMervEngine::eState_Off);
        return DfltSysEventMail(sysmail);
    case eEventNPCSetActiveOn:
        switch (psy_self->GIDOfActive())
        {
        case NME_GOAL_MERV_NORMAL:
        case NME_GOAL_MERV_AWARE:
            engine.SetState(zMervEngine::eState_Idle);
            break;
        case NME_GOAL_MERV_BATTLE:
        case NME_GOAL_MERV_ZAP:
        case NME_GOAL_MERV_BOMB:
        case NME_GOAL_MERV_BOWL:
        case NME_GOAL_MERV_WOUND:
        case NME_GOAL_MERV_MOVE:
            engine.SetState(zMervEngine::eState_Alert);
            break;
        }
        return DfltSysEventMail(sysmail);
    }

    return DfltSysEventMail(sysmail);
}

// Placeholder for dead-stripped data, not recovered code: retail's .rodata keeps this goal list
// among the zNMEMervyn functions, but nothing references it and the function that used it is
// unknown.
static void __deadstripped_zNMETypeStdMervyn()
{
    en_npcgol unknown[12] = { NME_GOAL_MERV_NORMAL, NME_GOAL_TAUNT,     NME_GOAL_MERV_AWARE,
                              NME_GOAL_MERV_BATTLE, NME_GOAL_MERV_BOMB, NME_GOAL_MERV_WOUND,
                              NME_GOAL_SCARE,       NME_GOAL_SPOOKED,   NME_GOAL_NOTICE,
                              NME_GOAL_MERV_ZAP,    NME_GOAL_MERV_BOWL, NME_GOAL_UNKNOWN };
}

// No GC symbol (inlined into ChkExitCases and zNMEGoalMervZap::Process).
inline S32 zNMEMervyn::SafeToTaunt()
{
    if (gun_omatic.tmr_warmup < 0.0f && !(gun_omatic.tmr_megaprog < 0.0f))
    {
        return 0;
    }

    if (globals.player.s->Health < 1)
    {
        return 1;
    }

    return 0;
}

void zNMEMervyn::UpdateGunnerStatus()
{
    S32 gid_main;
    ZapGun::en_zapmod gun_state;
    const NMECfgMervyn* cfg;

    gid_main = psy_self->GIDOfActive();
    switch (gid_main)
    {
    case NME_GOAL_MERV_NORMAL:
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
    case NME_GOAL_MERV_AWARE:
    case NME_GOAL_SPOOKED:
    case NME_GOAL_NOTICE:
    case NME_GOAL_SCARE:
        gun_state = ZapGun::ZAP_MODE_WARN;
        break;
    case NME_GOAL_MERV_BATTLE:
    case NME_GOAL_MERV_ZAP:
    case NME_GOAL_MERV_BOMB:
    case NME_GOAL_MERV_WOUND:
        cfg = cfg_mervyn;
        if (XZDstSqToPlayer(NULL, NULL) < SQR(cfg->ranges.rad_zapgrey))
        {
            gun_state = ZapGun::ZAP_MODE_SHOOT;
        }
        else
        {
            gun_state = ZapGun::ZAP_MODE_WARN;
        }
        break;
    case NME_GOAL_MERV_BOWL:
        gun_state = ZapGun::ZAP_MODE_REPEL;
        break;
    case NME_GOAL_DEV_ANIMVIEW:
    case NME_GOAL_DEV_HEROMODE:
    case NME_GOAL_AFTERLIFE:
    case NME_GOAL_SPAWN:
    case NME_GOAL_SCAREWAIT:
        gun_state = ZapGun::ZAP_MODE_NOTHING;
        break;
    default:
        gun_state = ZapGun::ZAP_MODE_NOTHING;
        break;
    }

    gun_omatic.ModeSet(gun_state, 0);
}

void MervLetMeKnow::Notice(en_psynote note, xGoal* goal, void* data)
{
    xPsyche* psy_move = ((zNMEMervyn*)goal->psyche->clt_owner)->psy_move;
    S32 gid_move;
    S32 gid_want = goal->GetID();

    gid_move = psy_move->GIDOfActive();
    gid_want = MervMoveGoalFor(gid_want);
    if (gid_want == 0)
    {
        gid_want = NME_GOAL_NOMANLAND;
    }

    if (gid_want != gid_move)
    {
        psy_move->GoalSet(gid_want, 0);
    }
}

S32 zNMEGoalMervNormal::Enter(F32 dt, void* updCtxt)
{
    zNMEMervyn* npc = (zNMEMervyn*)psyche->clt_owner;

    ((zNMEGoalSentry*)npc->psy_move->FindGoal(NME_GOAL_MERV_MOVE))->Sentry_DoRot(1);
    npc->engine.SetState(zMervEngine::eState_Idle);
    return zNMEGoalCommon::Enter(dt, updCtxt);
}

S32 zNMEGoalMervNormal::Exit(F32 dt, void* updCtxt)
{
    ((zNMEGoalSentry*)((zNMEMervyn*)psyche->clt_owner)->psy_move->FindGoal(NME_GOAL_MERV_MOVE))
        ->Sentry_DoRot(0);
    return 0;
}

S32 zNMEGoalMervNormal::Process(en_trantype* trantype, F32 dt, void* ctxt)
{
    zNMEMervyn* npc = (zNMEMervyn*)psyche->clt_owner;
    S32 rc;

    if (globals.player.s->Health >= 1 && !xUtil_yesno(0.5f))
    {
        rc = npc->ChkBowlingBall(NULL, NULL);
        if (rc)
        {
            S32 nextgoal;

            *trantype = GOAL_TRAN_SET;
            nextgoal = NME_GOAL_TRIGGER_BATTLE;
            npc->GoalPick(this, nextgoal, *trantype);
            return nextgoal;
        }
        else
        {
            en_vis iseeplyr =
                npc->VisionTarget(xglobals->___player_ent_dont_use_directly, NME_DEFCON_NORMAL, 0);
            if (iseeplyr <= VIS_I_SEEE_YOUUU)
            {
                S32 nextgoal;

                *trantype = GOAL_TRAN_SET;
                nextgoal = NME_GOAL_TRIGGER_DETECT;
                npc->GoalPick(this, nextgoal, *trantype);
                return nextgoal;
            }
        }
    }

    return zNMEGoalCommon::Process(trantype, dt, ctxt);
}

S32 zNMEGoalMervAware::Process(en_trantype* trantype, F32 dt, void* ctxt)
{
    zNMEMervyn* npc = (zNMEMervyn*)psyche->clt_owner;
    S32 rc;

    rc = npc->ChkBowlingBall(NULL, NULL);
    if (rc)
    {
        S32 nextgoal;

        *trantype = GOAL_TRAN_SET;
        nextgoal = NME_GOAL_TRIGGER_BATTLE;
        npc->GoalPick(this, nextgoal, *trantype);
        return nextgoal;
    }
    else
    {
        en_vis iseeplyr =
            npc->VisionTarget(xglobals->___player_ent_dont_use_directly, NME_DEFCON_NORMAL, 0);
        if (iseeplyr > VIS_GREY_AND_FOGGY)
        {
            S32 nextgoal;

            nextgoal = NME_GOAL_TRIGGER_NORMAL;
            npc->GoalPick(this, nextgoal, *trantype);
            return nextgoal;
        }
        else if (iseeplyr < VIS_GET_BACK_HERE)
        {
            S32 nextgoal;

            nextgoal = NME_GOAL_TRIGGER_BATTLE;
            npc->GoalPick(this, nextgoal, *trantype);
            return nextgoal;
        }
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

S32 zNMEGoalMervBattle::Enter(F32 dt, void* updCtxt)
{
    zNMEMervyn* npc = (zNMEMervyn*)psyche->clt_owner;

    npc->engine.SetState(zMervEngine::eState_Alert);
    tmr_tossDelay = -1.0f;
    return zNMEGoalCommon::Enter(dt, updCtxt);
}

S32 zNMEGoalMervBattle::ChkExitCases(zNMEMervyn* npc, S32& nextgoal, en_trantype& trantype)
{
    S32 rc;
    en_vis iseeplyr;

    if (xUtil_yesno(0.5f))
    {
        return 0;
    }

    rc = npc->ChkBowlingBall(NULL, NULL);
    if (rc)
    {
        tmr_tossDelay = -1.0f;
        trantype = GOAL_TRAN_PUSH;
        nextgoal = NME_GOAL_MERV_BOWL;
        npc->GoalPick(this, nextgoal, trantype);
        return 1;
    }
    else if (npc->SafeToTaunt())
    {
        nextgoal = NME_GOAL_TRIGGER_VINIVICIVIDI;
        if (globals.player.s->Health < 1)
        {
            trantype = GOAL_TRAN_SET;
        }
        else
        {
            trantype = GOAL_TRAN_PUSH;
        }
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

    iseeplyr = npc->VisionTarget(xglobals->___player_ent_dont_use_directly, NME_DEFCON_NORMAL, 0);
    if (iseeplyr > VIS_GET_BACK_HERE)
    {
        trantype = GOAL_TRAN_SET;
        nextgoal = NME_GOAL_TRIGGER_NORMAL;
        npc->GoalPick(this, nextgoal, trantype);
        return 1;
    }
    else if (iseeplyr == VIS_HORROR_FLIK)
    {
        return 0;
    }
    else if (iseeplyr == VIS_OH_DEAR_GAHD)
    {
        tmr_tossDelay = 4.0f;
        trantype = GOAL_TRAN_PUSH;
        nextgoal = NME_GOAL_MERV_ZAP;
        npc->GoalPick(this, nextgoal, trantype);
        return 1;
    }
    else if ((iseeplyr == VIS_PREPARE_TO_DIE || iseeplyr == VIS_GET_BACK_HERE) &&
             tmr_tossDelay < 0.0f)
    {
        tmr_tossDelay = 4.0f;
        trantype = GOAL_TRAN_PUSH;
        nextgoal = NME_GOAL_MERV_BOMB;
        npc->GoalPick(this, nextgoal, trantype);
        return 1;
    }

    return 0;
}

S32 zNMEGoalMervBattle::Process(en_trantype* trantype, F32 dt, void* ctxt)
{
    S32 nextgoal = 0;
    zNMEMervyn* npc = (zNMEMervyn*)psyche->clt_owner;
    S32 doTran;

    doTran = ChkExitCases(npc, nextgoal, *trantype);
    if (doTran)
    {
        return nextgoal;
    }

    tmr_tossDelay = MAX(-1.0f, tmr_tossDelay - dt);
    npc->MatFacePlayer(dt, PI, 1);
    return zNMEGoalCommon::Process(trantype, dt, ctxt);
}

S32 zNMEGoalMervZap::Process(en_trantype* trantype, F32 dt, void* ctxt)
{
    zNMEMervyn* npc = (zNMEMervyn*)psyche->clt_owner;
    S32 nextgoal = 0;
    F32 tym_inGoal;
    en_vis vis;
    F32 spd_turn;

    tym_inGoal = psyche->TimerGet(XPSY_TYMR_CURGOAL);
    if (!(tym_inGoal < 1.0f))
    {
        if (npc->SafeToTaunt())
        {
            nextgoal = NME_GOAL_TRIGGER_VINIVICIVIDI;
            if (globals.player.s->Health < 1)
            {
                *trantype = GOAL_TRAN_SET;
            }
            else
            {
                *trantype = GOAL_TRAN_SWAP;
            }
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

    vis = npc->VisionTarget(xglobals->___player_ent_dont_use_directly, NME_DEFCON_BATTLE, 0);
    if (vis <= VIS_GET_BACK_HERE)
    {
        cnt_loop = 1;
        flg_info |= 0x10;
    }

    spd_turn = npc->cfg_mervyn->battle.spd_turn;
    npc->MatFacePlayer(dt, spd_turn, 1);
    return zNMEGoalLoopAnim::Process(trantype, dt, ctxt);
}

S32 zNMEGoalMervBowl::Enter(F32 dt, void* updCtxt)
{
    tmr_linger = 5.0f;
    tmr_repelMin = 1.0f;
    return zNMEGoalCommon::Enter(dt, updCtxt);
}

S32 zNMEGoalMervBowl::Process(en_trantype* trantype, F32 dt, void* ctxt)
{
    zNMEMervyn* npc = (zNMEMervyn*)psyche->clt_owner;
    S32 rc;

    if (tmr_linger < 0.0f)
    {
        *trantype = GOAL_TRAN_POP;
        return 1;
    }

    if (tmr_repelMin < 0.0f && !npc->ChkBowlingBall(NULL, NULL))
    {
        *trantype = GOAL_TRAN_POP;
        return 1;
    }

    tmr_linger = MAX(-1.0f, tmr_linger - dt);
    tmr_repelMin = MAX(-1.0f, tmr_repelMin - dt);

    RepelBowlBall(dt);

    if (npc->XZDstSqToPlayer(NULL, NULL) < SQR(5.0f))
    {
        *trantype = GOAL_TRAN_POP;
        return 1;
    }

    rc = zNMEGoalCommon::Process(trantype, dt, ctxt);
    return rc;
}

// The vectors are declared at first use: copy-initializing dir_NtoB builds the operator* result
// straight into it, as in retail. So the ratio comes first, though the DWARF lists it last.
S32 zNMEGoalMervBowl::RepelBowlBall(F32 dt)
{
    xEntBoulder* bowl;
    zNMEMervyn* npc;
    xVec3 vec_NtoB;
    F32 rat;

    bowl = zEntPlayerGetBBowl();
    npc = (zNMEMervyn*)psyche->clt_owner;

    npc->XZVecToPos(&vec_NtoB, xEntGetPos(bowl), NULL);

    rat = vec_NtoB.length();
    if (rat < 0.00001f)
    {
        return 1;
    }

    xVec3 dir_NtoB = vec_NtoB * (1.0f / rat);
    npc->MatQRotTowards(dt, &dir_NtoB, -1.0f);

    xVec3 vec_tmp;
    xVec3Inv(&vec_tmp, &vec_NtoB);

    xVec3 dir_side;

    // Not a DWARF local (optimized out on PS2); retail gives the point its own stack slot.
    xVec3 pos;
    pos = dir_NtoB * xVec3Dot(&vec_tmp, &dir_NtoB);
    pos += *xEntGetPos(bowl);
    npc->XYZVecToPos(&dir_side, &pos);
    dir_side.normalize();

    rat = 1.0f - rat / 12.0f;
    rat = MAX(0.25f, rat);

    vec_tmp = dir_NtoB * (60.0f * rat);
    xEntBoulder_AddForce(bowl, &vec_tmp);

    vec_tmp = dir_side * (60.0f * (0.25f * rat));
    xEntBoulder_AddForce(bowl, &vec_tmp);

    {
        // Debug draw of the two forces (xDrawLine is empty in release builds). Retail still
        // evaluates the end points, so the block is kept.
        xVec3 end;

        end = dir_NtoB * (60.0f * rat);
        end += *xEntGetCenter(bowl);
        xDrawLine(xEntGetCenter(bowl), &end);

        end = dir_side * (60.0f * (0.25f * rat));
        end += *xEntGetCenter(bowl);
        xDrawLine(xEntGetCenter(bowl), &end);
    }

    return 1;
}

S32 zNMEGoalMervBomb::Enter(F32 dt, void* updCtxt)
{
    idx_shot = 0;
    flg_leftside = !flg_leftside;
    DoAutoAnim(NME_GSPOT_START, 0);
    return zNMEGoalCommon::Enter(dt, updCtxt);
}

// tym_animCurr is not a PS2 DWARF local (optimized out there), but the time is read on its own
// line before each test, as in zNMEGoalFlingAttack::Process.
S32 zNMEGoalMervBomb::Process(en_trantype* trantype, F32 dt, void* ctxt)
{
    zNMEMervyn* npc = (zNMEMervyn*)psyche->clt_owner;
    U32 anid_prep;
    U32 anid_done;
    S32 rc;
    static const F32 tym_shoot[1] = { 0.18f };

    if (flg_leftside)
    {
        anid_prep = g_hash_stananim[STAN_ANIM_BOMBLEFT_PREP];
        anid_done = g_hash_stananim[STAN_ANIM_BOMBLEFT_DONE];
    }
    else
    {
        anid_prep = g_hash_stananim[STAN_ANIM_BOMBRYHT_PREP];
        anid_done = g_hash_stananim[STAN_ANIM_BOMBRYHT_DONE];
    }

    rc = npc->AnimCurStateID();
    if (rc != anid_prep)
    {
        if (rc == anid_done)
        {
            F32 tym_animCurr = npc->AnimTimeCurrent();

            if (tym_animCurr + dt >= npc->AnimDuration(NULL))
            {
                *trantype = GOAL_TRAN_POP;
                return 1;
            }
        }
        else
        {
            F32 tym_animCurr = npc->AnimTimeCurrent();

            if (idx_shot < 1 && tym_animCurr > tym_shoot[idx_shot])
            {
                if (BombsAway(npc))
                {
                    idx_shot++;
                }
            }
        }
    }

    rc = zNMEGoalCommon::Process(trantype, dt, ctxt);
    return rc;
}

// 'tym' is not in the PS2 DWARF, but retail computes the flight time once before clamping it.
S32 zNMEGoalMervBomb::BombsAway(zNMEMervyn* npc)
{
    zNMEHazard* haz;
    S32 rc;
    xVec3 pos_src;
    xVec3 pos_tgt;

    haz = HAZ_Acquire();
    if (haz == NULL)
    {
        return 0;
    }

    rc = haz->ConfigHelper(npc->cfg_mervyn->proj.haztype);
    if (!rc)
    {
        haz->Discard();
        return 0;
    }

    haz->npc_owner = npc;

    Mervyn::MissilePod::LaunchPos(npc, flg_leftside, &pos_src);

    PickTarget(npc, &pos_tgt, &pos_src);
    haz->custdata.lobdata.pos_tgt = pos_tgt;

    xVec3 delta = pos_tgt - pos_src;
    F32 tym = delta.length() / npc->cfg_mervyn->proj.spd_proj;
    tym = MAX(0.25f, tym);
    haz->Start(&pos_src, tym);

    npc->SndPlayFromTable(2); // eNMESound_MervBombLaunch
    return 1;
}

// 'tym' and 'dst' are not in the PS2 DWARF (it lists only dir_tgt), but retail evaluates the
// prediction time and the distance once each.
void zNMEGoalMervBomb::PickTarget(zNMEMervyn* npc, xVec3* tgt, const xVec3* src)
{
    F32 tym = xsqrt(npc->XYZDstSqToPlayer(NULL)) / npc->cfg_mervyn->proj.spd_proj;
    tym = MAX(0.25f, tym);
    tym = MIN(tym, 3.0f);
    zEntPlayer_PredictPos(tgt, tym, 1.0f, 1);
    tgt->y += 0.75f;

    xVec3 dir_tgt = *tgt - *src;
    F32 dst = dir_tgt.length();
    if (dst < 0.00001f)
    {
        *tgt = *(xVec3*)&npc->model->Mat->pos + *(xVec3*)&npc->model->Mat->at * 2.2f;
        tgt->y += 0.75f;
    }
    else
    {
        dir_tgt /= dst;
        if (xVec3Dot((xVec3*)&npc->model->Mat->at, &dir_tgt) < 0.5f)
        {
            *tgt = *xEntGetCenter(xglobals->___player_ent_dont_use_directly);
            tgt->y += 0.75f;
        }
    }
}

S32 zNMEGoalMervWound::Enter(F32 dt, void* updCtxt)
{
    tmr_inPain = 0.875f;
    return zNMEGoalCommon::Enter(dt, updCtxt);
}

// The '? 1 : 0' materializes the timer test as 0/1 (as BfBB's zNPCGoalRobo timer tests do).
// tym_animCurr is not a PS2 DWARF local, but the time is read on its own line, as in
// zNMEGoalMervBomb::Process.
S32 zNMEGoalMervWound::Process(en_trantype* trantype, F32 dt, void* ctxt)
{
    zNMEMervyn* npc = (zNMEMervyn*)psyche->clt_owner;

    if ((tmr_inPain < 0.0f) ? 1 : 0)
    {
        F32 tym_animCurr = npc->AnimTimeCurrent();

        if (tym_animCurr + dt >= npc->AnimDuration(NULL))
        {
            if (npc->pts_health < 1)
            {
                *trantype = GOAL_TRAN_SET;
                return NME_GOAL_AFTERLIFE;
            }

            *trantype = GOAL_TRAN_SET;
            return NME_GOAL_MERV_BATTLE;
        }
    }

    tmr_inPain = MAX(-1.0f, tmr_inPain - dt);

    if (bul_entered)
    {
        DoAutoAnim(NME_GSPOT_START, 0);
    }
    else if (bul_resumed)
    {
        DoAutoAnim(NME_GSPOT_RESUME, 0);
    }

    return zNMEGoalCommon::Process(trantype, dt, ctxt);
}

S32 zNMEGoalMervMove::Enter(F32 dt, void* updCtxt)
{
    zNMEMervyn* npc = (zNMEMervyn*)psyche->clt_owner;

    Sentry_Enter(npc);
    npc->spd_throttle = 0.0f;
    return zNMEGoalCommon::Enter(dt, updCtxt);
}

// No GC or PS2 symbol (always inlined); the name is ours (see ZapFiddleThrottle, the Zapper's
// twin). Throttles toward the speed of the main brain's goal.
static inline void MervFiddleThrottle(zNMEMervyn* npc, F32 dt)
{
    const NMECfgCommon* cfg_comm = npc->cfg_common;
    const NMECfgMervyn* cfg = npc->cfg_mervyn;
    S32 gid = npc->psy_self->GIDOfActive();
    F32 spd;
    F32 acc;

    switch (gid)
    {
    case NME_GOAL_MERV_NORMAL:
    case NME_GOAL_MERV_AWARE:
        spd = cfg_comm->movement.spd_move;
        acc = cfg_comm->movement.acc_move;
        break;
    case NME_GOAL_MERV_BATTLE:
    case NME_GOAL_TAUNT:
        spd = cfg->battle.spd_move;
        acc = cfg->battle.acc_move;
        break;
    default:
        spd = cfg_comm->movement.spd_move;
        acc = cfg_comm->movement.acc_move;
        break;
    }

    npc->ThrottleAdjust(dt, spd, acc);
}

S32 zNMEGoalMervMove::Process(en_trantype* trantype, F32 dt, void* ctxt)
{
    zNMEMervyn* npc = (zNMEMervyn*)psyche->clt_owner;

    if (flg_sentry & 1)
    {
        npc->spd_throttle = 0.0f;
    }
    else
    {
        MervFiddleThrottle(npc, dt);
    }

    Sentry_Update(npc, dt);
    return zNMEGoalCommon::Process(trantype, dt, ctxt);
}
