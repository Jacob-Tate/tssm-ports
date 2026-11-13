#include "zNMETypeStandard.h"
#include "zNMEMessenger.h"
#include "zEntPlayer.h"
#include "xMathInlines.h"
#include "zNMEHazard.h"
#include "xDebug.h"
#include "zNMETypeBuckOTron.h"
#include "zNMESupplement.h"
#ifdef PLATFORM_PC
#include "iCoop.h"
#endif

extern _tagGameVidMode gVidMode;

xCollis g_SharedCollisRecord;
U32 g_hash_stananim[STAN_ANIM_NOMORE];
char* g_strz_stananim[STAN_ANIM_NOMORE] = {
    "Unknown",       "Idle",          "Move",           "Idle_Alert",     "Move_Alert",
    "Flee",          "Panic",         "Scare_Loop",     "Scare_Done",     "Spook_Loop",
    "Notice",        "Taunt",         "Victory",        "Damage",         "Defeat",
    "Pray",          "Attack",        "Attack_Prep",    "Attack_Loop",    "Attack_Done",
    "Stun_Prep",     "Stun_Loop",     "Stun_Pickup",    "Stun_Carry",     "Toss_Prep",
    "Toss_Loop",     "Spin_Prep",     "Spin_Loop",      "Spin_Throw",     "PopUp",
    "PopDown",       "Deflect_Prep",  "Deflect_Loop",   "Deflect_Done",   "BombLeft_Prep",
    "BombLeft_Loop", "BombLeft_Done", "BombRight_Prep", "BombRight_Loop", "BombRight_Done",
};

namespace Standard
{
    namespace Config
    {
        WAD_STATIC const NMECfgStandard cfg_standard = { { 0.25f }, { 15.0f }, { 15.0f } };
    }

    namespace GlobalData
    {
        WAD_STATIC S32 cnt_numStdNME;
    }

    // Goals a NME_MID_STUN does not interrupt, and the goals of an enemy that is already
    // stunned (both used by the TypeHandleMail of the std types).
    static const en_npcgol noStunGList[8] = {
        NME_GOAL_DEAD,      NME_GOAL_LIMBO, NME_GOAL_AFTERLIFE, NME_GOAL_SPAWN,
        NME_GOAL_SCAREWAIT, NME_GOAL_SCARE, NME_GOAL_WOUND,     NME_GOAL_UNKNOWN,
    };
    static const en_npcgol alreadyStunnedList[6] = {
        NME_GOAL_EVILPAT,  NME_GOAL_STUNNED,  NME_GOAL_PATCARRY,
        NME_GOAL_PATTWIRL, NME_GOAL_PATTHROW, NME_GOAL_UNKNOWN,
    };
} // namespace Standard

namespace Carry
{
    static F32 ang_plyrLast;
} // namespace Carry

namespace Spawn
{
    static const xVec3 vec_offsetLeft = { 0.0f, 0.0f, 0.0f };
    static const xVec3 vec_offsetRhyt = { 0.0f, 0.0f, 0.0f };
    static const xVec3 vec_warpOffset = { 0.0f, 0.5f, 0.0f };
    static const F32 tym_spitWindow[2] = { 0.75f, 0.8f };

    WarpData warp_spawn = { 0, xScreenWarp::TYPE_SHOCKWAVE, 0.4f, 10.0f, 4.0f, 0.75f };

    namespace Ribbon
    {
        static const xFXRibbon::curve_node curvedata[5] = {
            { 0.0f, { 0xFF, 0x9B, 0x00, 0x55 }, 0.0f }, { 0.2f, { 0xFF, 0x64, 0xFF, 0x55 }, 0.8f },
            { 0.5f, { 0xFF, 0xFF, 0x00, 0x55 }, 1.0f }, { 0.8f, { 0xFF, 0x64, 0xFF, 0x55 }, 0.8f },
            { 1.0f, { 0xFF, 0x9B, 0x00, 0x55 }, 0.0f },
        };

        NMEHelpRib::RibData rib_emerge = {
            { 1, 2, 0, 0 },
            { "fx_spawn_appear", (xFXRibbon::curve_node*)curvedata, 5, 0.75f, 0.5f },
            { 1, 1.0f, 1.0f },
            { NULL, 0 },
        };
    } // namespace Ribbon
} // namespace Spawn

struct KnockSettings
{
    F32 acc_grav;
    F32 fac_elastic;
    F32 mag_upward;
    F32 spd_push;
    F32 fac_dampen;
    F32 tym_push;
};

namespace Wound
{
    // Indexed by FormOfPain.
    static const KnockSettings painparm[4] = {
        { 10.1f, 0.35f, 0.5f, 8.0f, 0.5f, 0.7f },
        { 10.1f, 0.35f, 0.25f, 10.0f, 0.5f, 0.7f },
        { 10.1f, 0.6f, 0.5f, 10.0f, 0.5f, 0.5f },
        { 10.1f, 0.6f, 0.2f, 16.0f, 0.5f, 0.5f },
    };

    // No GC symbol (always inlined); a plain function in the PS2 build (Wound::FormOfPain).
    static inline S32 FormOfPain(S32 pup, S32 hitpoints)
    {
        if (hitpoints > 0 && pup <= ePowerupLevelNormal)
        {
            return 0;
        }
        if (hitpoints < 1 && pup <= ePowerupLevelNormal)
        {
            return 1;
        }
        if (hitpoints > 0)
        {
            return 2;
        }
        return 3;
    }

    namespace Ribbon
    {
        namespace Normal
        {
            static const xFXRibbon::curve_node curvedata[4] = {
                { 0.0f, { 0x7F, 0x7F, 0x00, 0x20 }, 1.0f },
                { 0.2f, { 0x7F, 0x7F, 0x00, 0x5A }, 1.5f },
                { 0.6f, { 0x7F, 0x7F, 0x00, 0x3C }, 0.4f },
                { 1.0f, { 0x7F, 0x7F, 0x00, 0x00 }, 0.0f },
            };

            NMEHelpRib::RibData rib_norm = {
                { 1, 1, 0, 0 },
                { "particle_cloud", (xFXRibbon::curve_node*)curvedata, 4, 0.25f, 0.2f },
                { 1, 1.0f, 1.0f },
                { NULL, 0 },
            };
        } // namespace Normal

        namespace Glory
        {
            static const xFXRibbon::curve_node curvedata[3] = {
                { 0.0f, { 0xFF, 0x00, 0x00, 0x20 }, 1.0f },
                { 0.3f, { 0x7F, 0x7F, 0x7F, 0x5A }, 1.5f },
                { 0.6f, { 0x00, 0x00, 0xFF, 0x3C }, 0.4f },
            };

            NMEHelpRib::RibData rib_glory = {
                { 1, 1, 0, 0 },
                { "particle_cloud", (xFXRibbon::curve_node*)curvedata, 3, 0.25f, 0.2f },
                { 1, 1.0f, 1.0f },
                { NULL, 0 },
            };
        } // namespace Glory
    } // namespace Ribbon

    namespace PushBlur
    {
        BlurData blur_push = {
            { 1, 0, 0, 0 },
            { 0.3f, 1.0f, 0.35f },
            { 0, 2.0f, 1.0f, 1.0f, 1.0f, 0.2f, 0.25f, 100.0f, 6 },
        };
    } // namespace PushBlur
} // namespace Wound

namespace Stunned
{
    static const xVec3 vec_offsetNorm = { 0.0f, 2.5f, 0.0f };
    static const xVec3 vec_offsetCarried = { 0.0f, 1.25f, 0.0f };
    static const xVec3 ang_rotspd = { 1.5707964f, 0.0f, 0.0f };
    static const F32 tym_blinkOff[4][2] = {
        { 1.3f, 1.05f },
        { 0.8f, 0.55f },
        { 0.4f, 0.3f },
        { 0.2f, 0.1f },
    };
} // namespace Stunned

// Goals during which an enemy may break into a taunt when it gets NME_MID_CELEBRATE.
static S32 canPartyGoals[] = {
    NME_GOAL_IDLE,          NME_GOAL_PATROL,     NME_GOAL_WANDER,      NME_GOAL_FLINGER_NORMAL,
    NME_GOAL_POPPER_NORMAL, NME_GOAL_ZAP_NORMAL, NME_GOAL_MERV_NORMAL, NME_GOAL_NOMORE,
};

// No symbol (inlined into the TypeHandleMail of the std types); the name is not known.
inline void zNMEStandard::DfltCelebrateMail()
{
    S32 gid = psy_self->GIDOfActive();
    S32 canParty = 0;
    en_trantype trantype;
    S32 nextgoal;

    for (S32* list = canPartyGoals; *list != NME_GOAL_NOMORE; list++)
    {
        if (gid == *list)
        {
            canParty = 1;
            break;
        }
    }

    if (!canParty)
    {
        return;
    }

    trantype = GOAL_TRAN_SET;
    nextgoal = NME_GOAL_TRIGGER_VINIVICIVIDI;
    GoalPick(NULL, nextgoal, trantype);
    if (nextgoal == NME_GOAL_TAUNT)
    {
        zNMEGoalTaunt* taunt = (zNMEGoalTaunt*)psy_self->FindGoal(NME_GOAL_TAUNT);
        taunt->cnt_loop = 3;
        taunt->flg_info |= 0x10;
    }

    psy_self->GoalSet(nextgoal, 0);
}

inline F32 zNMEGoalPatCarry::CalcPlayerLean(zNMEStandard* npc, F32 dt)
{
    F32 diffAngle;
    F32 desiredLean;
    F32 rat_oldLean;

    F32 ang_plyr = xatan2(xEntGetFrame(xglobals->___player_ent_dont_use_directly)->at.x,
                          xEntGetFrame(xglobals->___player_ent_dont_use_directly)->at.z);
    diffAngle = xDangleClamp(ang_plyr - Carry::ang_plyrLast);

    desiredLean = xabs(diffAngle);
    if (desiredLean < 0.08726646f)
    {
        desiredLean = 0.0f;
    }
    else if (desiredLean > 0.2617994f)
    {
        desiredLean = 1.0f;
    }
    else
    {
        desiredLean = 5.7295775f * (desiredLean - 0.08726646f);
    }

    if (diffAngle > 0.0f)
    {
        desiredLean = -desiredLean;
    }

    desiredLean += 1.0f;
    rat_oldLean = npc->model->Anim->Single->BilinearLerp[0];
    Carry::ang_plyrLast = ang_plyr;

    return rat_oldLean + dt * (6.0f * (desiredLean - rat_oldLean));
}

// No symbol in TSSM or the PS2 DWARF (always inlined); counterpart of GlyphStart, the name is ours.
inline void zNMEGoalEvilPat::GlyphStop()
{
    zNMEStandard* npc = (zNMEStandard*)psyche->clt_owner;

    if (npc->haz_dazed != NULL)
    {
        npc->haz_dazed->flg_hazard |= 0x4;
        npc->haz_dazed = NULL;
    }
}

// No GC symbol: inlined into zNMECentral_Startup (its unused copy is dead-stripped).
void ZNME_Standard_Startup()
{
    static zMODStanEnemy mod_stannme;

    zNMECentral_RegisterModule(&mod_stannme, 'STAN');
    for (S32 i = 0; i < STAN_ANIM_NOMORE; i++)
    {
        g_hash_stananim[i] = xStrHash(g_strz_stananim[i]);
    }
}

void zMODStanEnemy::ScenePrepare()
{
    Standard::GlobalData::cnt_numStdNME = 0;
}

void zMODStanEnemy::ScenePostInit()
{
    if (Standard::GlobalData::cnt_numStdNME > 0)
    {
        Wound::Ribbon::Normal::rib_norm.FindTexture();
        Wound::Ribbon::Glory::rib_glory.FindTexture();
        Spawn::Ribbon::rib_emerge.FindTexture();
    }
}

zMovePoint* zNMEStandard::MvptFirstFollow()
{
    zMovePoint* nav;

    if (((zNMEAssetStandard*)nmeass)->aid_mvptFollow == 0)
    {
        return NULL;
    }

    nav = zMovePoint_From_xAssetID(((zNMEAssetStandard*)nmeass)->aid_mvptFollow);
    return nav;
}

U32 zNMEStandard::AnimPick(S32 gid, en_npcgspot gspot, xGoal* goal)
{
    U32 da_anim = g_hash_stananim[STAN_ANIM_IDLE];
    return da_anim;
}

xAnimTable* zNMEStandard::CreateAnimTable(xAnimTable* table)
{
    if (table == NULL)
    {
        table = xAnimTableNew("zNMEStandard", 0);
    }

    xAnimTableNewState(table, g_strz_stananim[STAN_ANIM_IDLE], 0x10, 1, 1.0f, NULL, NULL, 0.0f,
                       NULL, NULL, xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, g_strz_stananim[STAN_ANIM_MOVE], 0x10, 1, 1.0f, NULL, NULL, 0.0f,
                       NULL, NULL, xAnimDefaultBeforeEnter, NULL, NULL);

    S32 ourAnims[3] = { STAN_ANIM_IDLE, STAN_ANIM_MOVE, STAN_ANIM_UNKNOWN };
    NMEC_BuildStandardAnimTran(table, g_strz_stananim, ourAnims, 1, 0.2f);

    return table;
}

// No GC symbol: a plain function that the std types' Process inline. It sits here, between
// CreateAnimTable and DieTheGoodDeath as in the PS2 build, for retail's .rodata order.
void zNMEStandard::StdNME_SuicideIsPainless()
{
    S32 gid_curr;
    zNMEArena* arena;
    const NMECfgStandard* cfg;
    xVec3 pos_home;
    F32 hyt_test;
    F32 hyt_fall;

    if (!IsAlive())
    {
        return;
    }

    gid_curr = 0;
    if (psy_self != NULL)
    {
        S32 gid_carried[5] = { NME_GOAL_EVILPAT, NME_GOAL_STUNNED, NME_GOAL_PATCARRY,
                               NME_GOAL_PATTWIRL, 0 };

        gid_curr = psy_self->GIDOfActive();
        if (NMEStd_GoalInList(gid_curr, gid_carried))
        {
            return;
        }
    }

    arena = Gimme_NMEArena();
    if (arena == NULL)
    {
        return;
    }

    if (!(arena->rad_arena > 0.0f))
    {
        return;
    }

    cfg = cfg_std;
    if (cfg == NULL)
    {
        return;
    }

    if (cfg->terminate.hyt_fallToDeath <= 0.0f)
    {
        return;
    }

    pos_home = arena->pos_arena;
    hyt_test = pos_home.y - model->Mat->pos.y;

    hyt_fall = cfg->terminate.hyt_fallToDeath;
    if (gid_curr == NME_GOAL_PATTHROW)
    {
        hyt_fall *= 2.0f;
    }

    if (hyt_test < hyt_fall)
    {
        return;
    }

    Damage(DMGTYP_INSTAKILL, xglobals->___player_ent_dont_use_directly, &g_Y3, -1,
           ePowerupLevelNormal);
}

void zNMEStandard::DieTheGoodDeath()
{
    zNMEMsg_AreaNotify(this, NME_MID_NMEDIED, 20.0f, 2, NME_TYPE_UNKNOWN);
    SetPlayerKillsVillainTimer(4.0f);
    DeathFXKick();
}

void zNMEStandard::StdTyp_Process(F32 dt)
{
    xMat4x3 oldMat;
    xEntFrame tmpframe;

    oldMat = *(xMat4x3*)model->Mat;

    psy_self->Timestep(dt, NULL);

    Type_AddYourMove(dt);

    if (drv_data != NULL)
    {
        drv_data->VroomVroom(dt, (xMat4x3*)model->Mat);
    }

    if (flg_colCheck && colFreq < 0)
    {
        memset(&tmpframe, 0, sizeof(xEntFrame));
        frame = &tmpframe;
        frame->oldmat = oldMat;
        frame->mat = *(xMat4x3*)model->Mat;

        RunCollision(dt);
        colFreq = ColFreqReset();

        MatPosSet(&frame->mat.pos);
        frame = NULL;
    }
    else
    {
        colFreq--;
    }

    if (IsAlive())
    {
        ShadStufUpdate();
        zNMECommon::Process(dt);
    }
    else if (simpShadow != NULL || entShadow != NULL)
    {
        ShadStufIAmDead();
    }

    if (haz_dazed != NULL)
    {
        DazedFXUpdate(dt);
    }
}

S32 zNMEStandard::IsVulnerable(en_npcdmg dmgtype)
{
    if (!(flg_vuln & 0x1))
    {
        return 0;
    }

    switch (dmgtype)
    {
    case DMGTYP_ABOVE:
        if (gCurrentPlayer == eCurrentPlayerSpongeBob && !(flg_vuln & 0x40000))
        {
            return 0;
        }
        break;
    case DMGTYP_BELOW:
        if (!(flg_vuln & 0x20000))
        {
            return 0;
        }
        break;
    case DMGTYP_HITBYTOSS:
        if (!(flg_vuln & 0x4))
        {
            return 0;
        }
        break;
    case DMGTYP_NMEATTACK:
    case DMGTYP_FRIENDLYFIRE:
        if (!(flg_vuln & 0x8))
        {
            return 0;
        }
        break;
    case DMGTYP_ROPE:
        if (!(flg_vuln & 0x1000000))
        {
            return 0;
        }
        break;
    case DMGTYP_CRUISEBUBBLE:
        if (!(flg_vuln & 0x80000))
        {
            return 0;
        }
        break;
    case DMGTYP_BUBBOWL:
        if (!(flg_vuln & 0x100000))
        {
            return 0;
        }
        break;
    case DMGTYP_BBOWLZAP:
        return 1;
    case DMGTYP_BOULDER:
        if (!(flg_vuln & 0x200000))
        {
            return 0;
        }
        break;
    case DMGTYP_THUNDER_TIKI_EXPLOSION:
    case DMGTYP_EXPLOSION:
        if (!(flg_vuln & 0x2))
        {
            return 0;
        }
        break;
    case DMGTYP_DAMAGE_SURFACE:
    case DMGTYP_SURFACE:
        if (!(flg_vuln & 0x10))
        {
            return 0;
        }
        break;
    case DMGTYP_BUNGEED:
        if (!(flg_vuln & 0x400000))
        {
            return 0;
        }
        break;
    case DMGTYP_CARTWHEEL:
        if (!(flg_vuln & 0x40000000))
        {
            return 0;
        }
        break;
    }

    return 1;
}

void zNMEStandard::Boo(const zNMECommon* npc)
{
    S32 gid;
    static const S32 gid_allowSpook[8] = {
        NME_GOAL_IDLE,          NME_GOAL_PATROL,     NME_GOAL_WANDER,      NME_GOAL_FLINGER_NORMAL,
        NME_GOAL_POPPER_NORMAL, NME_GOAL_ZAP_NORMAL, NME_GOAL_MERV_NORMAL, NME_GOAL_UNKNOWN,
    };

    gid = psy_self->GIDOfActive();

    if (NMEStd_GoalInList(gid, gid_allowSpook) && psy_self->HasGoal(NME_GOAL_SPOOKED))
    {
        psy_self->GoalSet(NME_GOAL_SPOOKED, 0);
    }
}

void zNMEStandard::DuploOwner(zNMECommon* duper)
{
    npc_duplodude = duper;

    if (NMEC_DstSq(&asset->pos, &duper->asset->pos, NULL) > 400.0f)
    {
        xVec3 vec_near = { 10.0f, 10.0f, 10.0f };

        vec_near.x *= 2.0f * (xurand() - 0.5f);
        vec_near.y *= 2.0f * (xurand() - 0.5f);
        vec_near.z *= 2.0f * (xurand() - 0.5f);
        vec_near += 0.25f;
        vec_near += duper->nmeass->ent_asset.pos;
        asset->pos = vec_near;
    }
}

S32 zNMEStandard::SetCarryState(en_carystat stat)
{
    S32 result = 0;
#ifdef PLATFORM_PC
    // Online co-op (docs/MULTIPLAYER.md 5.5): the guest's Patrick does not
    // pick up an enemy the host owns (the host's game moves it; M3 sends the
    // guest's grabs to the host). FALSE at once when co-op is off.
    if (stat == zNMECARRY_ATTEMPTPICKUP && iCoopWorldGuestOwns((xBase*)this))
    {
        return 0;
    }
#endif
    xGoal* goal = psy_self->GetCurGoal();

    if (goal == NULL)
    {
        return 0;
    }

    S32 gid = goal->GetID();

    switch (stat)
    {
    case zNMECARRY_NONE:
        psy_self->GoalSet(NME_GOAL_AFTERLIFE, 0);
        break;
    case zNMECARRY_PICKUP:
        psy_self->GoalSwap(NME_GOAL_PATCARRY, 0);
        result = 1;
        break;
    case zNMECARRY_TWIRL:
        psy_self->GoalSwap(NME_GOAL_PATTWIRL, 0);
        break;
    case zNMECARRY_THROW:
        psy_self->GoalSwap(NME_GOAL_PATTHROW, 0);
        break;
    case zNMECARRY_ATTEMPTPICKUP:
        if ((flg_vuln & 0x20000000) && gid == NME_GOAL_STUNNED)
        {
            result = 1;
        }
        break;
    }

    return result;
}

void zNMEGoalSentry::Sentry_Update(zNMEStandard* npc, F32 dt)
{
    if (flg_sentry & 0x1)
    {
        tmr_delay = MAX(-1.0f, tmr_delay - dt);
        if (tmr_delay < 0.0f)
        {
            if (flg_npcgauto & 0x6)
            {
                DoAutoAnim(NME_GSPOT_RESUME, 0);
            }
            flg_sentry &= ~0x1;
            TellMyNukeFam(SENTRY_TELL_UNPAUSE, npc);
        }
    }

    if (flg_sentry & 0x1)
    {
        npc->spd_throttle = 0.0f;
    }
    else if (flg_dfltAccel)
    {
        npc->ThrottleAccel(dt, 1, 0.75f);
    }

    zNMENavNet* navnet = npc->navnet;
    if (!(flg_sentry & 0x1))
    {
        if (navnet->spl_mvptspline)
        {
            SentryMoveSpline(npc, dt);
        }
        else if (navnet->nav_dest)
        {
            SentryMoveNormal(npc, dt);
        }
    }
}

// No GC symbol: inlined into SentryMoveNormal and SentryMoveSpline, the out-of-line copy
// dead-stripped.
void zNMEGoalSentry::DoOnArriveStuff(zNMECommon* npc)
{
    zNMENavNet* navnet = npc->navnet;
    zMovePoint* navcur;

    if (navnet->nav_dest != NULL)
    {
        zEntEvent(npc, 0, navnet->nav_dest, eEventArrive, NULL, NULL, 0, FE_NO);
    }

    navnet->MvptCycle();

    navcur = navnet->nav_curr;
    if (!flg_ignoreDelay && navcur != NULL &&
        navcur->Delay() > 10.0f * (gVidMode == eGameVidModePAL ? 0.02f : (1.0f / 60.0f)))
    {
        tmr_delay = navcur->Delay();
        flg_sentry |= 0x1;
        if (flg_npcgauto & 0x6)
        {
            DoAutoAnim(NME_GSPOT_PATROLPAUSE, 0);
        }
    }

    TellMyNukeFam(SENTRY_TELL_ARRIVED, npc);
}

void zNMEGoalSentry::SentryMoveNormal(zNMECommon* npc, F32 dt)
{
    xVec3 pos_dest;
    xVec3 dir_dest;

    xVec3Copy(&pos_dest, npc->navnet->nav_dest->PosGet());
    xVec3Sub(&dir_dest, &pos_dest, (xVec3*)&npc->model->Mat->pos);
    if (npc->flg_move & 0x2)
    {
        dir_dest.y = 0.0f;
    }

    F32 spd = MAX(1e-5f, npc->spd_throttle * dt);
    F32 len = dir_dest.length();
    if (len < spd)
    {
        npc->model->Mat->pos.x = pos_dest.x;
        npc->model->Mat->pos.z = pos_dest.z;
        if (!(npc->flg_move & 0x2))
        {
            npc->model->Mat->pos.y = pos_dest.y;
        }
        DoOnArriveStuff(npc);
    }
    else
    {
        dir_dest *= 1.0f / len;
        if (flg_doRot)
        {
            npc->MatQRotTowards(dt, &dir_dest, -1.0f);
        }
        xVec3 delta = dir_dest * spd;
        npc->model->Mat->pos.x += delta.x;
        npc->model->Mat->pos.y += delta.y;
        npc->model->Mat->pos.z += delta.z;
    }
}

void zNMEGoalSentry::SentryMoveSpline(zNMECommon* npc, F32 dt)
{
    zNMENavNet* navnet = npc->navnet;
    xSpline3* spl = navnet->spl_mvptspline;
    F32 newdist;
    xVec3 vec;
    xVec3 tgt;
    F32 u;
    xVec3 dir;

    if (spl == NULL)
    {
        SentryMoveNormal(npc, dt);
        return;
    }

    if (navnet->dst_curspline < 0.08f)
    {
        if (npc->DstSqToPos(navnet->nav_curr->PosGet()) > npc->cfg_common->movement.dst_deviant)
        {
            npc->MatMoveToPos(navnet->nav_curr->PosGet(), dt, npc->spd_throttle, 1, -1.0f);
            return;
        }
    }

    newdist = navnet->dst_curspline + npc->spd_throttle * dt;
    if (newdist >= navnet->len_mvptspline)
    {
        vec = *navnet->nav_dest->PosGet();
        npc->model->Mat->pos.x = vec.x;
        npc->model->Mat->pos.z = vec.z;
        if (!(npc->flg_move & 0x2))
        {
            npc->model->Mat->pos.y = vec.y;
        }
        DoOnArriveStuff(npc);
        return;
    }

    u = xSpline3_EvalArcApprox(spl, newdist, 0, &tgt);
    xSpline3_EvalSeg(spl, u, 1, &dir);
    if (xVec3Length2(&dir) < 1e-6f)
    {
        if (u < 0.1f)
        {
            u += 0.01f;
        }
        else
        {
            u -= 0.01f;
        }
        xSpline3_EvalSeg(spl, u, 1, &dir);
    }

    if (flg_doRot)
    {
        npc->MatQRotTowards(dt, &dir, -1.0f);
    }

    xVec3 delta = tgt - *(xVec3*)&npc->model->Mat->pos;
    if (npc->flg_move & 0x2)
    {
        delta.y = 0.0f;
    }
    npc->model->Mat->pos.x += delta.x;
    npc->model->Mat->pos.y += delta.y;
    npc->model->Mat->pos.z += delta.z;

    navnet->dst_curspline = newdist;
}

S32 zNMEGoalSpooked::Enter(F32 dt, void* updCtxt)
{
    tmr_nextturn = 3.0f * (0.25f * (xurand() - 0.5f)) + 3.0f;
    tmr_spooked = 10.0f * (0.25f * (xurand() - 0.5f)) + 10.0f;
    PickHeading();
    return zNMEGoalCommon::Enter(dt, updCtxt);
}

S32 zNMEGoalSpooked::Process(en_trantype* trantype, F32 dt, void* ctxt)
{
    zNMEStandard* npc = (zNMEStandard*)psyche->clt_owner;

    if (tmr_spooked < 0.0f)
    {
        S32 nextgoal = NME_GOAL_TRIGGER_NORMAL;
        *trantype = GOAL_TRAN_SET;
        npc->GoalPick(this, nextgoal, *trantype);
        return nextgoal;
    }

    tmr_nextturn = MAX(-1.0f, tmr_nextturn - dt);
    tmr_spooked = MAX(-1.0f, tmr_spooked - dt);
    if (tmr_nextturn < 0.0f)
    {
        tmr_nextturn = 3.0f * (0.25f * (xurand() - 0.5f)) + 3.0f;
        PickHeading();
    }

    npc->MatQRotTowards(dt, &dir_desire, -1.0f);

    return zNMEGoalCommon::Process(trantype, dt, ctxt);
}

S32 zNMEGoalAware::Process(en_trantype* trantype, F32 dt, void* updCtxt)
{
    ((zNMECommon*)psyche->clt_owner)->MatFacePlayer(dt, PI, 1);
    return zNMEGoalCommon::Process(trantype, dt, updCtxt);
}

S32 zNMEGoalNotice::Enter(F32 dt, void* updCtxt)
{
    VillainNoticesPlayer((zNMECommon*)psyche->clt_owner);
    return zNMEGoalCommon::Enter(dt, updCtxt);
}

// No symbol in any build (the name is ours): retail's register use in Process needs the
// VisionTarget result to pass through an inline's return value.
static inline en_vis NMEStd_VisionPlayer(const zNMECommon* npc)
{
    return npc->VisionTarget(xglobals->___player_ent_dont_use_directly, NME_DEFCON_NORMAL, 0);
}

S32 zNMEGoalNotice::Process(en_trantype* trantype, F32 dt, void* updCtxt)
{
    zNMECommon* npc = (zNMECommon*)psyche->clt_owner;
    en_vis cansee;

    F32 tym_animCurr = npc->AnimTimeCurrent();
    if (tym_animCurr + dt >= npc->AnimDuration(NULL))
    {
        cansee = NMEStd_VisionPlayer(npc);
        if (cansee > VIS_GREY_AND_FOGGY)
        {
            S32 nextgoal = NME_GOAL_TRIGGER_SCARY;
            *trantype = GOAL_TRAN_SET;
            npc->GoalPick(this, nextgoal, *trantype);
            return nextgoal;
        }
        else
        {
            S32 nextgoal = NME_GOAL_TRIGGER_ALERT;
            *trantype = GOAL_TRAN_SET;
            npc->GoalPick(this, nextgoal, *trantype);
            return nextgoal;
        }
    }

    npc->spd_throttle = 0.0f;
    npc->MatFacePlayer(dt, 2.0f * PI, 1);
    return 0;
}

S32 zNMEGoalTaunt::Enter(F32 dt, void* updCtxt)
{
    zNMECommon* npc = (zNMECommon*)psyche->clt_owner;
    npc->SndPlayFromTable(1); // eNMESound_Taunt
    return zNMEGoalLoopAnim::Enter(dt, updCtxt);
}

S32 zNMEGoalTaunt::Process(en_trantype* trantype, F32 dt, void* ctxt)
{
    zNMEStandard* npc = (zNMEStandard*)psyche->clt_owner;
    en_loopstat lpstat;
    S32 nextgoal;

    if (globals.player.s->Health < 1 ||
        globals.player.DamageTimer >= npc->cfg_std->taunt.tym_keeplaffin)
    {
        cnt_loop = (cnt_loop > 1) ? cnt_loop : 1;
    }

    lpstat = GoalLoop_Process(npc, dt);
    if (lpstat == LOOP_GOAL_IS_DONE)
    {
        if (psyche->IndexInStack(GetID()) == 0)
        {
            *trantype = GOAL_TRAN_SET;
            nextgoal = NME_GOAL_TRIGGER_NORMAL;
            npc->GoalPick(this, nextgoal, *trantype);
            return nextgoal;
        }

        *trantype = GOAL_TRAN_POP;
        return 1;
    }

    npc->MatFacePlayer(dt, PI, 1);
    return zNMEGoalCommon::Process(trantype, dt, ctxt);
}

void zNMEStandard::WoundedInput(en_npcgol gid, NMEDamageInfo* dmgmail)
{
    xGoal* goal;

    if (gid == NME_GOAL_WOUND)
    {
        goal = psy_self->FindGoal(gid);
        if (goal != NULL)
        {
            ((zNMEGoalWound*)goal)->InputInfo(dmgmail);
        }
    }
}

void zNMEStandard::DazedFXUpdate(F32 dt)
{
    S32 gid;
    static const S32 gid_carried[4] = { NME_GOAL_PATCARRY, NME_GOAL_PATTWIRL, NME_GOAL_PATTHROW,
                                        0 };

    if (haz_dazed == NULL)
    {
        return;
    }

    haz_dazed->tym_lifespan = 1.0f;
    haz_dazed->tmr_remain = 1.0f;

    gid = psy_self->GIDOfActive();
    if (NMEStd_GoalInList(gid, gid_carried))
    {
        xVec3 pos = *(xVec3*)&model->Mat->pos + Stunned::vec_offsetCarried;

        haz_dazed->PosSet(&pos);
        haz_dazed->custdata.typical.rad_min = 0.15f;
        haz_dazed->custdata.typical.rad_max = 0.15f;
        haz_dazed->custdata.typical.rad_cur = 0.15f;
    }
    else
    {
        xVec3 pos = *(xVec3*)&model->Mat->pos + Stunned::vec_offsetNorm;

        haz_dazed->PosSet(&pos);
    }

    S32 turnOn = 1;
    for (S32 i = 0; i < 4; i++)
    {
        if (tmr_stunned > Stunned::tym_blinkOff[i][0] || tmr_stunned < Stunned::tym_blinkOff[i][1])
        {
            continue;
        }

        turnOn = 0;
        break;
    }

    if (turnOn)
    {
        haz_dazed->SetAlpha(0.75f);
    }
    else
    {
        haz_dazed->SetAlpha(0.25f);
    }
}

// No GC symbol (always inlined); out of line in the PS2 build.
inline void zNMEGoalWound::TakeItLikeAMan(zNMEStandard* npc)
{
    F32 tym_anim;
    const KnockSettings* knock =
        &Wound::painparm[Wound::FormOfPain(input.pup_plyr, npc->pts_health)];

    vel_fling = input.dir_pushed * knock->spd_push;

    tym_anim = (gVidMode == eGameVidModePAL ? 0.02f : (1.0f / 60.0f));
    tym_anim = npc->AnimDuration(NULL) - 3.0f * tym_anim;
    tmr_knocked = MIN(knock->tym_push, tym_anim);

    npc->spd_throttle = 0.0f;
}

// No GC symbol (always inlined); empty, as in the PS2 build.
inline void zNMEGoalWound::RibbonPrep(zNMEStandard* npc)
{
}

// No symbol (inlined; the name is ours): zNMEGoalCommon::DoAutoAnim for an owner the caller
// already holds.
static inline U32 WoundAutoAnim(zNMEGoalCommon* goal, zNMECommon* npc, en_npcgspot gspot,
                                S32 forceRestart)
{
    U32 anid;

    anid = npc->AnimPick(goal->GetID(), gspot, goal);
    if (anid)
    {
        if (npc->AnimStart(anid, forceRestart))
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

S32 zNMEGoalWound::Enter(F32 dt, void* updCtxt)
{
    zNMEStandard* npc = (zNMEStandard*)psyche->clt_owner;

    flg_wound = 0;
    if (!(flg_info & 0x10))
    {
        InputInfoDflt();
    }

    WoundAutoAnim(this, npc, NME_GSPOT_START, 0);

    TakeItLikeAMan(npc);
    zNMEMsg_AreaNMEYellBoo(npc, 10.0f, NULL);
    RibbonPrep(npc);

    if (Wound::PushBlur::blur_push.flags.enable && input.pup_plyr > ePowerupLevelNormal &&
        npc->pts_health <= 1)
    {
        if (pushBlur.activity != NULL)
        {
            pushBlur.set_lifetime(0.0f, Wound::PushBlur::blur_push.parms.tym_lifespan);
        }
        else
        {
            pushBlur.activate(Wound::PushBlur::blur_push.parms.tym_lifespan,
                              Wound::PushBlur::blur_push.parms.alf_master,
                              Wound::PushBlur::blur_push.cfg_blur, npc->model);
        }
    }

    if (input.pup_plyr == ePowerupLevelLevelMax)
    {
        npc->flg_colCheck = 0xFF;
        npc->flg_penCheck = 0xF7;
    }
    else
    {
        npc->flg_colCheck = 0xFF;
        npc->flg_penCheck = 0xFF;
    }

    return zNMEGoalCommon::Enter(dt, updCtxt);
}

S32 zNMEGoalWound::Exit(F32 dt, void* updCtxt)
{
    zNMEStandard* npc = (zNMEStandard*)psyche->clt_owner;

    if (ribbon != NULL)
    {
        zFXRibbonPoolDelete(ribbon);
        ribbon = NULL;
    }

    if (pushBlur.activity != NULL)
    {
        pushBlur.set_lifetime(0.0f, Wound::PushBlur::blur_push.parms.tym_fade);
    }

    input.pup_plyr = ePowerupLevelDisabled;
    flg_info &= ~0x10;
    npc->RestoreColFlags();

    return 0;
}

// No symbol (always inlined; the name is ours). Ends the wound when the goal was told to die or
// the knock time is over.
static inline S32 WoundFinished(zNMEGoalWound* goal, zNMEStandard* npc, en_trantype* trantype,
                                S32& nextgoal)
{
    if (goal->flg_wound & 0x4)
    {
        *trantype = GOAL_TRAN_SET;
        nextgoal = NME_GOAL_AFTERLIFE;
        return 1;
    }

    if (goal->tmr_knocked < 0.0f)
    {
        if (npc->pts_health < 1)
        {
            *trantype = GOAL_TRAN_SET;
            nextgoal = NME_GOAL_AFTERLIFE;
            return 1;
        }

        *trantype = GOAL_TRAN_SET;
        nextgoal = NME_GOAL_TRIGGER_BATTLE;
        npc->GoalPick(goal, nextgoal, *trantype);
        return 1;
    }

    return 0;
}

// No symbol (always inlined; the name is ours). Damps the fling velocity and applies the knock
// gravity.
static inline void WoundFling(zNMEGoalWound* goal, zNMEStandard* npc, F32 dt)
{
    const KnockSettings* knock =
        &Wound::painparm[Wound::FormOfPain(goal->input.pup_plyr, npc->pts_health)];

    goal->vel_fling -= goal->vel_fling * (knock->fac_dampen * dt);
    goal->vel_fling.y -= knock->acc_grav * dt;
}

// No symbol (always inlined; the name is ours). Moves the model by one step of 'vel', reading the
// model matrix again for every component like the other enemy movers.
static inline void WoundMove(zNMEStandard* npc, const xVec3& vel, F32 dt)
{
    xVec3 delta = vel * dt;
    npc->model->Mat->pos.x += delta.x;
    npc->model->Mat->pos.y += delta.y;
    npc->model->Mat->pos.z += delta.z;
}

S32 zNMEGoalWound::Process(en_trantype* trantype, F32 dt, void* ctxt)
{
    zNMEStandard* npc = (zNMEStandard*)psyche->clt_owner;
    S32 nextgoal = 0;
    xVec3 vec;

    if (WoundFinished(this, npc, trantype, nextgoal))
    {
        return nextgoal;
    }

    tmr_knocked = MAX(-1.0f, tmr_knocked - dt);

    WoundFling(this, npc, dt);
    WoundMove(npc, vel_fling, dt);

    vec = vel_fling;
    vec.y = 0.0f;
    if (vec.x * vec.x + vec.z * vec.z > 0.00001f)
    {
        vec /= -(vec.x * vec.x + vec.z * vec.z);
        npc->MatQRotTowards(dt, &vec, 4.0f * PI);
    }

    RibbonUpdate(npc, dt);

    if (pushBlur.activity != NULL)
    {
        pushBlur.move(npc->bound.sph.center);
    }

    return zNMEGoalCommon::Process(trantype, dt, ctxt);
}

// No symbol (always inlined; the name is ours). Knocks every NPC the flung enemy ran into along
// its collision heading at the fling speed.
static inline void WoundKnockNPCs(zNMEGoalWound* goal, zNMEStandard* npc)
{
    xEntCollis* npccol = npc->collis;

    if (npccol->npc_sidx < npccol->npc_eidx)
    {
        F32 spd = goal->vel_fling.length();

        for (S32 i = npccol->npc_sidx; i < npccol->npc_eidx; i++)
        {
            xCollis* colrec = &npccol->colls[i];
#ifdef PLATFORM_PC
            // PLATFORM_PC: optr is the xEnt that was hit; a host compiler puts an enemy's vptr
            // ahead of its xEnt, so the cast goes through xEnt* (it adjusts) rather than from void*,
            // which would make the Damage call below jump through the xEnt's id.
            zNMECommon* npc_hit = (zNMECommon*)(xEnt*)colrec->optr;
#else
            zNMECommon* npc_hit = (zNMECommon*)colrec->optr;
#endif
            xVec3 vec_hit = colrec->hdng * spd;

            npc_hit->Damage(DMGTYP_NMEKNOCKED, npc, &vec_hit, -1, ePowerupLevelNormal);
        }
    }
}

// No symbol (always inlined; the name is ours). Sends a hit event to every breakable dynamic
// object the flung enemy ran into.
static inline void WoundHurtObjects(zNMEStandard* npc)
{
    static const en_ZBASETYPE typesWeCanHurt[] = {
        eBaseTypePlatform, eBaseTypeStatic,      eBaseTypeDynamic, eBaseTypePendulum,
        eBaseTypeHangable, eBaseTypeHangableObj, eBaseTypeButton,  eBaseTypeDestructObj,
        eBaseTypeNPC,      eBaseTypeBoulder,     eBaseTypeUnknown,
    };
    xEntCollis* npccol = npc->collis;

    for (S32 i = npccol->dyn_sidx; i < npccol->dyn_eidx; i++)
    {
        xBase* ent_hit = (xBase*)npccol->colls[i].optr;
        S32 typ_hit = ent_hit->baseType;
        S32 canHurt = 0;

        for (S32 j = 0; typesWeCanHurt[j] != eBaseTypeUnknown; j++)
        {
            if (typ_hit == typesWeCanHurt[j])
            {
                canHurt = 1;
                break;
            }
        }

        if (canHurt)
        {
            F32 fparam[4] = { 1.0f, 0.0f, 0.0f, 0.0f };
            zEntEvent(npc, 0, ent_hit, eEventHit, fparam, NULL, 0, FE_NO);
        }
    }
}

S32 zNMEGoalWound::CollReview(void*)
{
    zNMEStandard* npc = (zNMEStandard*)psyche->clt_owner;
    xEntCollis* npccol = npc->collis;
    S32 badsurf;
    xVec3 vel_temp;
    S32 rc;

    if (npccol->colls[0].flags & k_HIT_IT)
    {
        flg_wound |= 0x1;
    }

    if (npccol->npc_sidx < npccol->npc_eidx)
    {
        WoundKnockNPCs(this, npc);
    }

    if (npccol->dyn_sidx < npccol->dyn_eidx)
    {
        WoundHurtObjects(npc);
    }

    if (npc->flg_vuln & 0x10)
    {
        badsurf = npc->ReviewCollSurfs(npc->collis, NULL);
        if (badsurf)
        {
            flg_wound |= 0x4;
        }
    }

    if (npccol->idx > 6)
    {
        vel_temp = vel_fling;

        const KnockSettings* knock =
            &Wound::painparm[Wound::FormOfPain(input.pup_plyr, npc->pts_health)];
        rc = npc->ReviewCollBounce(npc->collis, &vel_temp,
                                   knock->fac_elastic * (0.25f * (xurand() - 0.5f)) +
                                       knock->fac_elastic);
        if (rc)
        {
            flg_wound |= 0x2;
            vel_fling = vel_temp;
        }
    }

    return 1;
}

S32 zNMEGoalWound::InputInfoDflt()
{
    zNMEStandard* npc = (zNMEStandard*)psyche->clt_owner;
    xVec3 dir_plyr;

    input.pup_plyr = ePowerupLevelNormal;
    input.mag_pushed = 1.0f;

    npc->XZVecToPos(&dir_plyr, xEntGetPos(xglobals->___player_ent_dont_use_directly), NULL);

    F32 len = dir_plyr.length();
    if (len > 0.1f)
    {
        input.dir_pushed = dir_plyr * (1.0f / (-1.0f * len));
    }
    else
    {
        input.dir_pushed = globals.oldSkoolCamera.mat.at;
        input.dir_pushed.y = 0.0f;

        len = input.dir_pushed.length();
        if (len > 0.1f)
        {
            input.dir_pushed /= len;
        }
        else
        {
            input.dir_pushed = *(xVec3*)&npc->model->Mat->at * -1.0f;
        }
    }

    flg_info |= 0x10;
    return flg_info;
}

S32 zNMEGoalWound::InputInfo(NMEDamageInfo* dmgmail)
{
    zNMEStandard* npc = (zNMEStandard*)psyche->clt_owner;
    xVec3 dir_plyr;

    input.pup_plyr = dmgmail->pup_player;

    F32 len = dmgmail->vec_dmghit.length();
    if (len > 1e-5f)
    {
        input.mag_pushed = len;
        input.dir_pushed = dmgmail->vec_dmghit * (1.0f / len);
    }
    else
    {
        input.mag_pushed = 1.0f;

        npc->XZVecToPos(&dir_plyr, xEntGetPos(xglobals->___player_ent_dont_use_directly), NULL);

        len = dir_plyr.length();
        if (len > 0.1f)
        {
            input.dir_pushed = dir_plyr * (1.0f / (-1.0f * len));
        }
        else
        {
            input.dir_pushed = globals.oldSkoolCamera.mat.at;
            input.dir_pushed.y = 0.0f;

            len = input.dir_pushed.length();
            if (len > 0.1f)
            {
                input.dir_pushed /= len;
            }
            else
            {
                input.dir_pushed = *(xVec3*)&npc->model->Mat->at * -1.0f;
            }
        }
    }

    flg_info |= 0x10;
    return flg_info;
}

S32 zNMEGoalWound::GoalHandleMail(NMEMsg* mail)
{
    S32 snarfed;

    switch (mail->msgid)
    {
    case NME_MID_DAMAGE:
        snarfed = 1;
        break;
    default:
        snarfed = 0;
        break;
    }

    return snarfed;
}

void zNMEGoalWound::RibbonUpdate(zNMEStandard* npc, F32 dt)
{
    NMEHelpRib::RibData* ribhelp;
    F32 scale;
    F32 alpha;

    if (ribbon == NULL)
    {
        return;
    }

    cnt_skipRibbonJoint--;
    if (cnt_skipRibbonJoint > 0)
    {
        return;
    }

    ribhelp = (input.pup_plyr > ePowerupLevelNormal) ? &Wound::Ribbon::Glory::rib_glory :
                                                       &Wound::Ribbon::Normal::rib_norm;

    cnt_skipRibbonJoint = ribhelp->updinfo.frq_ribJoint;
    if (ribhelp->flags.dev_enable)
    {
        ribhelp->ConfigRibbon(ribbon);
    }

    alpha = ribhelp->updinfo.alf_master;
    scale = npc->bound.sph.r * ribhelp->updinfo.scl_master;

    // Emits from the last bone. Retail draws a random number here and discards it.
    xrand_RandomInt32();
    S32 idx = CLAMP(npc->model->BoneCount + 1, 1, npc->model->BoneCount);

    xMat4x3* mat = (xMat4x3*)&npc->model->Mat[idx];
    xVec3 pos_emit = mat->pos + *(xVec3*)&npc->model->Mat->pos;
    xVec3 dir_up = (xrand_RandomInt32() & 0x800000) ? mat->at : mat->up;
    ribbon->insert(pos_emit, dir_up, scale, alpha, 0);
}

static const en_npcmsg mailMessagesToDiscard[27] = {
    NME_MID_DAMAGE,         NME_MID_EXPLOSION,    NME_MID_BUNNYHOP,     NME_MID_DTRON_NMEEMIT,
    NME_MID_DTRON_NMEAVAIL, NME_MID_DTRON_ISDEAD, NME_MID_CHAT_REQUEST, NME_MID_CHAT_DECLINE,
    NME_MID_CHAT_ACCEPT,    NME_MID_CHAT_DONE,    NME_MID_CHAT_ABORT,   NME_MID_TALKSTART,
    NME_MID_TALKON,         NME_MID_TALKOFF,      NME_MID_SAWPLYR,      NME_MID_NEEDMEDIC,
    NME_MID_UNDERATTACK,    NME_MID_NMEDIED,      NME_MID_PLYRSPATULA,  NME_MID_BECOMESCARED,
    NME_MID_NOLONGERSCARED, NME_MID_CELEBRATE,    NME_MID_STUN,         NME_MID_SCRIPTBEGIN,
    NME_MID_SCRIPTEND,      NME_MID_SCRIPTHALT,   NME_MID_NONE,
};

static const en_xEventTags sysEventsToDiscard[16] = {
    eEventKill,
    eEventDestroy,
    eEventNPCKillQuietly,
    eEventHit,
    eEventDuploPause,
    eEventDuploResume,
    eEventHit_Melee,
    eEventHit_BubbleBounce,
    eEventHit_BubbleBash,
    eEventHit_BubbleBowl,
    eEventHit_PatrickSlam,
    eEventHit_Throw,
    eEventHit_Cartwheel,
    eEventHit,
    eEventNPCCheerForMe,
    eEventUnknown,
};

// No symbol (always inlined; the name is ours): looks a value up in a zero-terminated list.
template <class T> inline S32 NMEStd_InDiscardList(T val, const T* list)
{
    S32 found = 0;
    for (const T* p = list; *p != 0; p++)
    {
        if (val == *p)
        {
            found = 1;
            break;
        }
    }
    return found;
}

S32 zNMEGoalAfterlife::Enter(F32 dt, void* updCtxt)
{
    zNMECommon* npc = (zNMECommon*)psyche->clt_owner;

    if (stylinDyin == DIE_LOUDLY || stylinDyin == DIE_NORMAL)
    {
        npc->DieTheGoodDeath();
    }

    npc->MatPosSet(&npc->asset->pos);

    return zNMEGoalDead::Enter(dt, updCtxt);
}

S32 zNMEGoalAfterlife::GoalHandleMail(NMEMsg* mail)
{
    S32 snarfed = 1;
    zNMEGoalSpawning* spgoal;

    switch (mail->msgid)
    {
    case NME_MID_RESPAWN:
    {
        xPsyche* psy = psyche;
        if (psy->GIDInStack(NME_GOAL_SPAWN) == NULL && psy->GIDOfPending() != NME_GOAL_SPAWN)
        {
            spgoal = (zNMEGoalSpawning*)psy->FindGoal(NME_GOAL_SPAWN);
            if (spgoal != NULL)
            {
                spgoal->flg_info = 0x10;
                xVec3Copy(&spgoal->storage.pos_poofHere, &mail->spawning.pos_spawn);
                spgoal->storage.nav_firstMovepoint = mail->spawning.nav_firstMovepoint;
                spgoal->storage.nav_spawnReference = mail->spawning.nav_spawnReference;
                if (spgoal->storage.nav_spawnReference == NULL)
                {
                    spgoal->storage.nav_spawnReference = spgoal->storage.nav_firstMovepoint;
                }
                psy->GoalPush(NME_GOAL_SPAWN, 0);
                mail->spawning.spawnSuccess = 1;
            }
        }
        break;
    }
    case NME_MID_SYSEVENT:
        if (!NMEStd_InDiscardList(mail->sysevent.toEvent_asEnum, sysEventsToDiscard))
        {
            snarfed = 0;
        }
        break;
    default:
        if (!NMEStd_InDiscardList(mail->msgid, mailMessagesToDiscard))
        {
            snarfed = 0;
        }
        break;
    }

    return snarfed;
}

S32 zNMEGoalSpawning::Enter(F32 dt, void* updCtxt)
{
    zNMECommon* npc = (zNMECommon*)psyche->clt_owner;

    if (!(flg_info & 0x10))
    {
        storage.pos_poofHere = npc->asset->pos;
        storage.nav_firstMovepoint = npc->MvptFirstFollow();
        storage.nav_spawnReference = npc->MvptFirstFollow();
    }

    flg_info = 0;
    npc->MatPosSet(&storage.pos_poofHere);

    tmr_robobits = 0.35f;
    tmr_duperTakesTooLong = 1.5f;
    stat_spawning = SPAWN_STAT_PATIENCE;

    // Both branches are identical in retail.
    if (npc->npc_duplodude != NULL)
    {
        xEntHide(npc);
        tmr_robobits = 1.0f;
        flg_doSpawnball = 1;
    }
    else
    {
        xEntHide(npc);
        tmr_robobits = 1.0f;
        flg_doSpawnball = 1;
    }

    haz_spawn = NULL;
    ribbon = zFXRibbonPoolNew();
    if (ribbon != NULL)
    {
        Spawn::Ribbon::rib_emerge.ConfigRibbon(ribbon);
    }

    flg_doScreenWarp = 1;

    return zNMEGoalCommon::Enter(dt, updCtxt);
}

S32 zNMEGoalSpawning::Exit(F32 dt, void* updCtxt)
{
    if (ribbon != NULL)
    {
        zFXRibbonPoolDelete(ribbon);
        ribbon = NULL;
    }

    storage.nav_spawnReference = storage.nav_firstMovepoint = NULL;
    return 0;
}

S32 zNMEGoalSpawning::Process(en_trantype* trantype, F32 dt, void* ctxt)
{
    zNMECommon* npc = (zNMECommon*)psyche->clt_owner;
    S32 patienceDone;
    S32 emergeDone;
    S32 nextgoal;

    switch (stat_spawning)
    {
    case SPAWN_STAT_PATIENCE:
        patienceDone = PatienceMonitor(npc, dt);
        if (patienceDone)
        {
            stat_spawning = SPAWN_STAT_EMERGING;
            haz_spawn = NULL;
        }
        else if (haz_spawn != NULL && haz_spawn->mdl_hazard != NULL)
        {
            RwMatrix* mat_haz = haz_spawn->mdl_hazard->Mat;
            npc->model->Mat->pos.x = mat_haz->pos.x;
            npc->model->Mat->pos.y = mat_haz->pos.y;
            npc->model->Mat->pos.z = mat_haz->pos.z;
        }
        break;
    case SPAWN_STAT_EMERGING:
        emergeDone = EmergeYeBeastie(npc, dt);
        if (emergeDone)
        {
            stat_spawning = SPAWN_STAT_FINALACT;
        }
        break;
    case SPAWN_STAT_FINALACT:
        KickFromTheNest(npc);
        xEntShow(npc);
        if (npc->npc_duplodude != NULL)
        {
            npc->npc_duplodude->DuploNotice(SM_NOTE_NMEALIVE, npc);
        }
        tmr_robobits = -1.0f;

        nextgoal = NME_GOAL_TRIGGER_ALERT;
        *trantype = GOAL_TRAN_SET;
        npc->GoalPick(this, nextgoal, *trantype);
        return nextgoal;
    }

    return zNMEGoalCommon::Process(trantype, dt, ctxt);
}

S32 zNMEGoalSpawning::PatienceMonitor(zNMECommon* npc, F32 dt)
{
    en_dupspit dupspit;

    tmr_robobits = MAX(-1.0f, tmr_robobits - dt);
    tmr_duperTakesTooLong = MAX(-1.0f, tmr_duperTakesTooLong - dt);

    if (!flg_doSpawnball)
    {
        if (tmr_robobits < 0.0f)
        {
            tmr_robobits = 0.35f;
            return 1;
        }
        return 0;
    }

    static F32 tym_waitForBucky = 4.0f * (gVidMode == eGameVidModePAL ? 0.02f : (1.0f / 60.0f));

    if (tmr_robobits + tym_waitForBucky >= 1.0f)
    {
        return 0;
    }

    dupspit = ExamineDuperAnim(npc);
    if (dupspit == DUP_SPIT_NOW)
    {
        xVec3 pos_launch;
        F32 tym;

        CalcLaunchPoint(npc, &pos_launch, 0);
        NMEC_BurstBubble(NME_BURST_BALL, &pos_launch, 0.25f, 0);
        tym = LaunchSpawnBall(npc, &pos_launch);
        tmr_robobits = tym;
        flg_doSpawnball = 0;
    }
    else if (dupspit == DUP_SPIT_ABORT)
    {
        xVec3 pos_launch;
        F32 tym;

        CalcLaunchPoint(npc, &pos_launch, 1);
        tym = LaunchSpawnBall(npc, &pos_launch);
        tmr_robobits = tym;
        flg_doSpawnball = 0;
    }

    return 0;
}

// No GC symbol: inlined into PatienceMonitor (the PS2 build has it out of line).
en_dupspit zNMEGoalSpawning::ExamineDuperAnim(zNMECommon* npc)
{
    zNMECommon* npc_duper = npc->npc_duplodude;

    if (npc_duper == NULL)
    {
        return DUP_SPIT_ABORT;
    }

    if (npc_duper->itemType < NME_TYPE_BUCKOTRON_V1 || npc_duper->itemType > NME_TYPE_BUCKOTRON_V7)
    {
        return DUP_SPIT_ABORT;
    }

    if (tmr_duperTakesTooLong < 0.0f)
    {
        return DUP_SPIT_NOW;
    }

    U32 anid = npc_duper->AnimCurStateID();
    if (anid == g_hash_buckanim[3] || anid == g_hash_buckanim[4]) // Attack_Left, Attack_Right
    {
        F32 tym = npc_duper->AnimTimeCurrent();
        if (tym < Spawn::tym_spitWindow[0])
        {
            return DUP_SPIT_WAIT;
        }
        if (tym > Spawn::tym_spitWindow[1])
        {
            return DUP_SPIT_ABORT;
        }
        return DUP_SPIT_NOW;
    }

    return DUP_SPIT_ABORT;
}

// No GC symbol: inlined into Process (the PS2 build has it out of line).
void zNMEGoalSpawning::KickFromTheNest(zNMECommon* npc)
{
    en_allow allowDetect = npc->runtimeData.overrideDetect;
    en_allow allowAttack = npc->runtimeData.overrideAttack;
    zNMEGoalAfterlife* wanna;
    S32 nextgoal;
    en_trantype trantype;

    npc->Reset();

    for (xModelInstance* mdl = npc->model; mdl != NULL; mdl = mdl->Next)
    {
        mdl->Scale.x = 0.0f;
        mdl->Scale.y = 0.0f;
        mdl->Scale.z = 0.0f;
    }

    wanna = (zNMEGoalAfterlife*)psyche->FindGoal(NME_GOAL_AFTERLIFE);
    if (wanna != NULL)
    {
        wanna->stylinDyin = DIE_LOUDLY;
    }

    npc->model->Mat->pos.x = storage.pos_poofHere.x;
    npc->model->Mat->pos.y = storage.pos_poofHere.y;
    npc->model->Mat->pos.z = storage.pos_poofHere.z;

    if (npc->navnet != NULL)
    {
        npc->navnet->nav_past = storage.nav_spawnReference;
        npc->navnet->nav_curr = storage.nav_spawnReference;
        npc->navnet->nav_dest = storage.nav_firstMovepoint;
        npc->navnet->nav_lead = storage.nav_firstMovepoint;
    }

    npc->runtimeData.overrideDetect = allowDetect;
    npc->runtimeData.overrideAttack = allowAttack;

    if (npc->psy_self != NULL)
    {
        nextgoal = NME_GOAL_TRIGGER_ALERT;
        trantype = GOAL_TRAN_SET;
        npc->GoalPick(this, nextgoal, trantype);
        npc->psy_self->GoalSet(nextgoal, 1);
    }
}

void zNMEGoalSpawning::CalcLaunchPoint(zNMECommon* npc, xVec3* pos_launch, S32 useDefault)
{
    if (useDefault)
    {
        xVec3 pos = globals.oldSkoolCamera.mat.pos;
        pos.y += 3.0f;
        *pos_launch = pos;
    }
    else
    {
        zNMECommon* npc_duper = npc->npc_duplodude;
        U32 anid_animCurr = npc_duper->AnimCurStateID();
        xMat4x3* mat_bone;
        xVec3 pos_emit;
        xMat4x3* mat_root;

        if (anid_animCurr == g_hash_buckanim[3]) // Attack_Left
        {
            mat_bone = (xMat4x3*)&npc_duper->model->Mat[8];
            pos_emit = Spawn::vec_offsetLeft;
        }
        else
        {
            mat_bone = (xMat4x3*)&npc_duper->model->Mat[8];
            pos_emit = Spawn::vec_offsetRhyt;
        }

        xMat4x3Toworld(&pos_emit, mat_bone, &pos_emit);

        mat_root = (xMat4x3*)npc_duper->model->Mat;
        xMat4x3Toworld(&pos_emit, mat_root, &pos_emit);

        *pos_launch = pos_emit;
    }
}

S32 zNMEGoalSpawning::EmergeYeBeastie(zNMECommon* npc, F32 dt)
{
    F32 fac;

    if (tmr_robobits < 0.0f)
    {
        xModelSetScale(npc->model, 1.0f);
        return 1;
    }

    if (!xEntIsVisible(npc))
    {
        xEntShow(npc);
        npc->model->Flags |= 0x4;
        npc->model->Flags |= 0x2;
        xModelSetScale(npc->model, 1e-5f);
    }

    fac = tmr_robobits / 0.35f;
    fac = CLAMP(fac, 0.0f, 1.0f);

    CowCatcher(npc, dt);

    xModelSetScale(npc->model, SMOOTH(1.0f - fac, 1e-5f, 1.0f));
    for (xModelInstance* mdl = npc->model; mdl != NULL; mdl = mdl->Next)
    {
        mdl->Flags |= 0x4000;
        mdl->Alpha = SMOOTH(1.0f - fac, 1e-5f, 1.0f);
    }

    DoAppearFX(npc, dt, fac);

    tmr_robobits = MAX(-1.0f, tmr_robobits - dt);
    return 0;
}

// No symbol (always inlined; the name is ours). Picks the spawn ball that matches the duper's
// variant.
static inline en_haztyp SpawnBallForDuper(const zNMECommon* npc_duper)
{
    if (npc_duper == NULL)
    {
        return HAZ_TYP_SPAWNBALL_XX;
    }

    switch (npc_duper->SelfType())
    {
    case NME_TYPE_BUCKOTRON_V3:
        return HAZ_TYP_SPAWNBALL_GG;
    case NME_TYPE_BUCKOTRON_V5:
        return HAZ_TYP_SPAWNBALL_PT;
    case NME_TYPE_BUCKOTRON_V6:
        return HAZ_TYP_SPAWNBALL_TR;
    default:
        return HAZ_TYP_SPAWNBALL_XX;
    }
}

F32 zNMEGoalSpawning::LaunchSpawnBall(zNMECommon* npc, xVec3* pos_launch)
{
    zNMEHazard* haz;

    haz_spawn = NULL;

    haz = HAZ_Acquire();
    if (haz == NULL)
    {
        return -1.0f;
    }

    if (!haz->ConfigHelper(SpawnBallForDuper(npc->npc_duplodude)))
    {
        haz->Discard();
        return -1.0f;
    }

    haz->npc_owner = npc;

    xVec3 pos_tgt = storage.pos_poofHere;
    xVec3 vec_diff = pos_tgt - *pos_launch;
    F32 spd_flight;
    F32 tym_flight = xVec3Length(&vec_diff);

    spd_flight = tym_flight / 1.5f;
    if (tym_flight < 0.25f || spd_flight < 0.25f)
    {
        haz->Discard();
        return -1.0f;
    }

    tym_flight = tym_flight / spd_flight;

    haz->custdata.lobdata.pos_tgt = pos_tgt;
    haz->Start(pos_launch, tym_flight);

    haz_spawn = haz;
    return tym_flight;
}

// No GC symbol: inlined into EmergeYeBeastie (the PS2 build has it out of line). Pushes the player
// out of the spawn point while the beastie emerges.
void zNMEGoalSpawning::CowCatcher(zNMECommon* npc, F32)
{
    F32 fac;
    xBound bnd;
    xCollis* colrec;

    fac = tmr_robobits / 0.35f;
    fac = CLAMP(fac, 0.0f, 1.0f);

    memset(&bnd, 0, sizeof(xBound));
    bnd.type = XBOUND_TYPE_SPHERE;

    memset(&g_SharedCollisRecord, 0, sizeof(xCollis));
    colrec = &g_SharedCollisRecord;
    colrec->flags = 0x1f00;

    bnd.sph.r = 2.0f * (1.0f - fac);
    xVec3Copy(&bnd.sph.center, &storage.pos_poofHere);

    if (NMEC_chk_hitPlyr(&bnd, colrec))
    {
        zEntPlayer_DamageNPCKnockBack(npc, 1, xEntGetPos(npc), ePlayerNMEDamageDefault);
        colrec->depen.y = 0.0f;
        xVec3 tmp;
        xVec3SMul(&tmp, &colrec->depen, -1.0f);
        xVec3AddTo(xEntGetPos(xglobals->___player_ent_dont_use_directly), &tmp);
    }
}

void zNMEGoalSpawning::DoAppearFX(zNMECommon* npc, F32, F32 ratio)
{
    if (flg_doScreenWarp)
    {
        flg_doScreenWarp = 0;
        if (Spawn::warp_spawn.doWarpFX)
        {
            xVec3 pos = storage.pos_poofHere + Spawn::vec_warpOffset;
            xScreenWarp::emit(pos, Spawn::warp_spawn.rad_size, Spawn::warp_spawn.tym_duration,
                              Spawn::warp_spawn.typ_warp, Spawn::warp_spawn.fac_intensity,
                              Spawn::warp_spawn.frq_freq, 0);
        }
    }

    RibbonUpdate(&storage.pos_poofHere, ratio);
}

void zNMEGoalSpawning::RibbonUpdate(xVec3* pos_home, F32 ratio)
{
    NMEHelpRib::RibData* ribhelp;
    F32 rad;

    if (ribbon != NULL)
    {
        cnt_skipRibInsert--;
        if (cnt_skipRibInsert <= 0)
        {
            ribhelp = &Spawn::Ribbon::rib_emerge;

            cnt_skipRibInsert = ribhelp->updinfo.frq_ribJoint;
            if (ribhelp->flags.dev_enable)
            {
                ribhelp->ConfigRibbon(ribbon);
            }

            if (ratio < 0.5f)
            {
                F32 rat = 2.0f * ratio;
                rad = SMOOTH(rat, 1.5f, 1.5f);
            }
            else
            {
                F32 rat = 2.0f * (ratio - 0.5f);
                rad = SMOOTH(rat, 1.5f, 1.5f);
            }

            F32 ang = 2.0f * PI * xfmod(1.5f * ratio, 1.0f) - PI;
            F32 cos_ang = icos(ang);
            F32 sin_ang = isin(ang);
            xVec3 orient = g_X3 * sin_ang + g_Z3 * cos_ang;
            xVec3 pos_emit = *pos_home + orient * rad;

            ribbon->insert(pos_emit, g_Y3, 4.0f * ribhelp->updinfo.scl_master,
                           ribhelp->updinfo.alf_master, 0);
        }
    }
}

// Out of line in the PS2 build; no GC symbol (always inlined into GoalHandleMail).
inline void zNMEGoalScareWait::AbortAbort()
{
    zNMECommon* npc = (zNMECommon*)psyche->clt_owner;
    xVec3 pos_dest;
    S32 nextgoal;
    en_trantype trantype;

    if (npc->navnet == NULL || npc->navnet->nav_dest == NULL)
    {
        pos_dest = *(xVec3*)&npc->model->Mat->pos;
    }
    else
    {
        // Retail leaves pos_dest unset on this path.
        npc->navnet->MvptReset(NULL);
    }

    npc->model->Mat->pos.x = pos_dest.x;
    npc->model->Mat->pos.y = pos_dest.y;
    npc->model->Mat->pos.z = pos_dest.z;

    nextgoal = NME_GOAL_TRIGGER_NORMAL;
    trantype = GOAL_TRAN_SET;
    npc->GoalPick(this, nextgoal, trantype);
    psyche->GoalSet(nextgoal, 0);

    if (npc->npc_duplodude != NULL)
    {
        npc->npc_duplodude->DuploNotice(SM_NOTE_NMELIVINGINSIN, npc);
    }
}

S32 zNMEGoalScareWait::Enter(F32 dt, void* updCtxt)
{
    zNMEStandard* npc = (zNMEStandard*)psyche->clt_owner;

    npc->MatPosSet(&npc->asset->pos);

    stylinDyin = DIE_DUPLOONLY;

    if (npc->navnet != NULL)
    {
        npc->navnet->MvptReset(NULL);
    }

    zNMEGoalDead::Enter(dt, updCtxt);

    stylinDyin = DIE_LOUDLY;

    return 0;
}

S32 zNMEGoalScareWait::GoalHandleMail(NMEMsg* msg)
{
    S32 handled = 0;

    switch (msg->msgid)
    {
    case NME_MID_SYSEVENT:
        if (msg->sysevent.toEvent == eEventNMEScareSkip)
        {
            AbortAbort();
            handled = 1;
        }
        else if (msg->sysevent.toEvent == eEventNMEScareBegin)
        {
            psyche->GoalSet(NME_GOAL_SCARE, 0);
            if (((zNMECommon*)psyche->clt_owner)->npc_duplodude != NULL)
            {
#ifdef PLATFORM_PC
                // PLATFORM_PC: the spawner finds the enemy by its zNMECommon* (StatForNPC), and
                // clt_owner is its xBase*. A host compiler puts the vptr ahead of the xEnt, so the
                // two differ and the void* has to be the enemy itself, as the other notices pass.
                ((zNMECommon*)psyche->clt_owner)
                    ->npc_duplodude->DuploNotice(SM_NOTE_NMELIVINGINSIN,
                                                 (zNMECommon*)psyche->clt_owner);
#else
                ((zNMECommon*)psyche->clt_owner)
                    ->npc_duplodude->DuploNotice(SM_NOTE_NMELIVINGINSIN, psyche->clt_owner);
#endif
            }
            handled = 1;
        }
        break;
    default:
        handled = 0;
        break;
    }

    return handled;
}

// No symbol (always inlined into Enter); the name is ours.
inline void zNMEGoalScare::ParabSetup()
{
    zNMEStandard* npc = (zNMEStandard*)psyche->clt_owner;
    xVec3 vel;
    xVec3 dir = *npc->navnet->nav_dest->pos - *xEntGetPos(npc);

    vel = dir;
    vel *= 1.0f / 0.75f;
    vel.y += 35.0f * (0.5f * 0.75f);

    parab->initPos = *xEntGetPos(npc);
    parab->initVel = vel;
    parab->gravity = 35.0f;
    parab->minTime = 0.0f;
    parab->maxTime = 0.75f;
}

S32 zNMEGoalScare::Enter(F32 dt, void* updCtxt)
{
    zNMEStandard* npc = (zNMEStandard*)psyche->clt_owner;

    npc->flg_colCheck = 0;
    npc->flg_penCheck = 0;

    if (npc->navnet->nav_dest == NULL)
    {
        npc->navnet->MvptReset(NULL);
    }

    parab = NMEC_ParabReserve();
    if (parab != NULL)
    {
        ParabSetup();
    }

    return zNMEGoalLoopAnim::Enter(dt, updCtxt);
}

S32 zNMEGoalScare::Exit(F32 dt, void* updCtxt)
{
    zNMEStandard* npc = (zNMEStandard*)psyche->clt_owner;

    if (parab != NULL)
    {
        NMEC_ParabRelease(parab);
        parab = NULL;
    }

    npc->RestoreColFlags();

    return zNMEGoalLoopAnim::Exit(dt, updCtxt);
}

S32 zNMEGoalScare::Process(en_trantype* trantype, F32 dt, void* ctxt)
{
    S32 animsComplete;
    S32 nextgoal;

    cnt_loop = 2;
    flg_info |= 0x10;

    animsComplete = UpdateAnim(dt);
    S32 pathDone = PathUpdate(dt);
    if (animsComplete && pathDone)
    {
        nextgoal = NME_GOAL_TRIGGER_ALERT;
        *trantype = GOAL_TRAN_SET;
        ((zNMECommon*)psyche->clt_owner)->GoalPick(this, nextgoal, *trantype);
        return nextgoal;
    }

    if (GoalLoop_Process((zNMECommon*)psyche->clt_owner, dt) == LOOP_GOAL_IS_DONE)
    {
        *trantype = GOAL_TRAN_POP;
        return 1;
    }

    return zNMEGoalCommon::Process(trantype, dt, ctxt);
}

// No GC symbol: inlined into Process (the PS2 build has it out of line).
S32 zNMEGoalScare::UpdateAnim(F32 dt)
{
    zNMEStandard* npc = (zNMEStandard*)psyche->clt_owner;
    F32 tym_ingoal = psyche->TimerGet(XPSY_TYMR_CURGOAL);
    U32 anid_loop;
    U32 anid_done;

    if (tym_ingoal >= 1.15f)
    {
        return 1;
    }

    anid_loop = npc->AnimPick(GetID(), NME_GSPOT_START, this);
    if (anid_played == anid_loop)
    {
        if (tym_ingoal >= 0.75f)
        {
            anid_done = npc->AnimPick(GetID(), NME_GSPOT_FINISH, this);
            DoExplicitAnim(anid_done, 0);
        }
    }
    else
    {
        F32 tym_animCurr = npc->AnimTimeCurrent();
        if (tym_animCurr + dt >= npc->AnimDuration(NULL))
        {
            return 1;
        }
    }

    return 0;
}

S32 zNMEGoalScare::PathUpdate(F32 dt)
{
    if (parab != NULL)
    {
        return PathParabola(dt);
    }
    return PathDirect(dt);
}

S32 zNMEGoalScare::PathParabola(F32 dt)
{
    zNMEStandard* npc = (zNMEStandard*)psyche->clt_owner;
    S32 arrived = 0;
    F32 tym_ingoal = psyche->TimerGet(XPSY_TYMR_CURGOAL);
    xVec3 pos;
    xVec3 vel;
    xMat3x3 mat_rot;
    F32 mag;

    if (tym_ingoal >= 0.75f)
    {
        arrived = 1;
    }

    tym_ingoal = CLAMP(tym_ingoal, 0.0f, 0.75f);

    xParabolaEvalPos(parab, &pos, tym_ingoal);
    xParabolaEvalVel(parab, &vel, tym_ingoal);
    npc->MatPosSet(&pos);

    vel.y = 0.0f;
    mag = xVec3Length(&vel);
    if (mag > 0.00001f)
    {
        xVec3 dir = vel * (1.0f / -mag);
        xMat3x3LookVec(&mat_rot, &dir);
        *(xMat3x3*)npc->model->Mat = mat_rot;
    }

    return arrived;
}

S32 zNMEGoalScare::PathDirect(F32 dt)
{
    zNMECommon* npc = (zNMECommon*)psyche->clt_owner;
    return npc->MatMoveToPos(npc->navnet->nav_dest->pos, dt, 5.0f, 1, -1.0f);
}

S32 zNMEGoalEvilPat::Enter(F32 dt, void* updCtxt)
{
    GlyphStart();
    return zNMEGoalCommon::Enter(dt, updCtxt);
}

S32 zNMEGoalEvilPat::Exit(F32 dt, void* updCtxt)
{
    ((zNMEStandard*)psyche->clt_owner)->tmr_stunned = -1.0f;
    GlyphStop();
    return 0;
}

S32 zNMEGoalEvilPat::Process(en_trantype* trantype, F32 dt, void* updCtxt)
{
    S32 nextgoal = 0;
    zNMEStandard* npc = (zNMEStandard*)psyche->clt_owner;

    if (npc->tmr_stunned < 0.0f)
    {
        nextgoal = NME_GOAL_TRIGGER_ALERT;
        *trantype = GOAL_TRAN_SET;
        npc->GoalPick(this, nextgoal, *trantype);
        npc->SndPlayFromTable(6); // eNMESound_StunRecover
        return nextgoal;
    }

    nextgoal = NME_GOAL_STUNNED;
    *trantype = GOAL_TRAN_PUSH;
    npc->GoalPick(this, nextgoal, *trantype);
    return nextgoal;
}

S32 zNMEGoalEvilPat::GoalHandleMail(NMEMsg* msg)
{
    S32 handled;
    zNMEStandard* npc = (zNMEStandard*)psyche->clt_owner;

    switch (msg->msgid)
    {
    case NME_MID_STUN:
        npc->tmr_stunned =
            msg->stundata.tym_stuntime * (0.25f * (xurand() - 0.5f)) + msg->stundata.tym_stuntime;
        handled = 1;
        break;
    default:
        handled = 0;
        break;
    }

    return handled;
}

// The compiled-out asserts (conditions unknown) keep this from being auto-inlined into
// zNMEGoalEvilPat::Enter, as retail calls it. The spin matrix is set through the inlined
// zNMEHazard::TypData_RotMatStore (BfBB's body), as in ZapBoom::Configure.
void zNMEGoalEvilPat::GlyphStart()
{
    zNMEStandard* npc = (zNMEStandard*)psyche->clt_owner;

    xASSERT_PLACEHOLDER();
    xASSERT_PLACEHOLDER();

    zNMEHazard* haz = HAZ_Acquire();

    if (haz != NULL)
    {
        xASSERT_PLACEHOLDER();
        xASSERT_PLACEHOLDER();

        if (!haz->ConfigHelper(HAZ_TYP_DAZED))
        {
            haz->flg_hazard |= 0x4;
            return;
        }

        xASSERT_PLACEHOLDER();
        xASSERT_PLACEHOLDER();
        xASSERT_PLACEHOLDER();

        xVec3 ang_delta =
            Stunned::ang_rotspd * (gVidMode == eGameVidModePAL ? 0.02f : (1.0f / 60.0f));
        haz->TypData_RotMatStore(&ang_delta);

        xASSERT_PLACEHOLDER();
        xASSERT_PLACEHOLDER();
        haz->custdata.typical.rad_min = 0.25f;
        haz->custdata.typical.rad_max = 0.25f;

        xASSERT_PLACEHOLDER();
        xASSERT_PLACEHOLDER();

        haz->custdata.typical.rad_cur = 0.25f;
        npc->haz_dazed = haz;

        xASSERT_PLACEHOLDER();
        xASSERT_PLACEHOLDER();
        xASSERT_PLACEHOLDER();
        xASSERT_PLACEHOLDER();
        xASSERT_PLACEHOLDER();

        haz->Start(xEntGetCenter(npc), 1.0f);

        xASSERT_PLACEHOLDER();
    }
}

S32 zNMEGoalStunned::Enter(F32 dt, void* updCtxt)
{
    zNMEStandard* npc = (zNMEStandard*)psyche->clt_owner;

    if (!(flg_info & 0x10))
    {
        npc->tmr_stunned = 15.0f * (0.25f * (xurand() - 0.5f)) + 15.0f;
    }

    flg_info = 0;
    npc->spd_throttle = 0.0f;

    return zNMEGoalCommon::Enter(dt, updCtxt);
}

// No symbol (always inlined; the name is ours).
static inline S32 StunExpired(zNMEStandard* npc)
{
    if (npc->tmr_stunned < 0.0f)
    {
        return 1;
    }
    return 0;
}

S32 zNMEGoalStunned::Process(en_trantype* trantype, F32 dt, void* ctxt)
{
    S32 nextgoal = 0;
    zNMEStandard* npc = (zNMEStandard*)psyche->clt_owner;

    if (StunExpired(npc))
    {
        nextgoal = NME_GOAL_TRIGGER_ALERT;
        *trantype = GOAL_TRAN_SET;
        npc->GoalPick(this, nextgoal, *trantype);
        npc->SndPlayFromTable(6); // eNMESound_StunRecover
        return nextgoal;
    }

    npc->tmr_stunned = MAX(-1.0f, npc->tmr_stunned - dt);
    return zNMEGoalCommon::Process(trantype, dt, ctxt);
}

S32 zNMEGoalPatCarry::Enter(F32 dt, void* updCtxt)
{
    Carry::ang_plyrLast = xatan2(xEntGetFrame(xglobals->___player_ent_dont_use_directly)->at.x,
                                 xEntGetFrame(xglobals->___player_ent_dont_use_directly)->at.z);
    return zNMEGoalCommon::Enter(dt, updCtxt);
}

S32 zNMEGoalPatCarry::Process(en_trantype* trantype, F32 dt, void* ctxt)
{
    TowerOfPisa((zNMEStandard*)psyche->clt_owner, dt);
    return zNMEGoalCommon::Process(trantype, dt, ctxt);
}

// The compiled-out asserts (conditions unknown) keep this from being auto-inlined, as retail calls
// it from Process.
void zNMEGoalPatCarry::TowerOfPisa(zNMEStandard* npc, F32 dt)
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
    xASSERT_PLACEHOLDER();
    xASSERT_PLACEHOLDER();
    xASSERT_PLACEHOLDER();
    xASSERT_PLACEHOLDER();
    xASSERT_PLACEHOLDER();
    xASSERT_PLACEHOLDER();
    xASSERT_PLACEHOLDER();
    xASSERT_PLACEHOLDER();
    xASSERT_PLACEHOLDER();

    F32 rat_lean = CalcPlayerLean(npc, dt);

    xASSERT_PLACEHOLDER();
    xASSERT_PLACEHOLDER();
    xASSERT_PLACEHOLDER();
    xASSERT_PLACEHOLDER();
    xASSERT_PLACEHOLDER();
    xASSERT_PLACEHOLDER();
    xASSERT_PLACEHOLDER();
    xASSERT_PLACEHOLDER();
    xASSERT_PLACEHOLDER();

    npc->model->Anim->Single->BilinearLerp[0] = rat_lean;
}

S32 zNMEGoalPatTwirl::Process(en_trantype* trantype, F32 dt, void* ctxt)
{
    return zNMEGoalCommon::Process(trantype, dt, ctxt);
}

S32 zNMEGoalPatThrow::Enter(F32 dt, void* updCtxt)
{
    zNMEStandard* npc = (zNMEStandard*)psyche->clt_owner;

    npc->MatFaceAntiPlayer(dt, PI, 1);
    return zNMEGoalCommon::Enter(dt, updCtxt);
}
