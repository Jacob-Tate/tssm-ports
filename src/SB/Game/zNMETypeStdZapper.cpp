#include "zNMETypeStdZapper.h"
#include "zEntPlayer.h"
#include "xAntenna.h"

namespace Zapper
{
    namespace Config
    {
        static const NMECfgZapper cfg_zapper_v1 = {
            { 6 },
            { 0.75f, 8.0f, 10.0f, 15.0f, 17.0f },
            { 4.0f, 3.0f, DEG2RAD(720) },
        };
        const NMECfgZapper* cfg_zap[10] = { &cfg_zapper_v1, &cfg_zapper_v1, &cfg_zapper_v1,
                                            &cfg_zapper_v1 };
    } // namespace Config

    NMEHelpBound::BndData bnd_info = {
        k_XBOUNDTYPE_SPHERE,
        { 0, 0, { 0, 0 } },
        { 1.25f, { 0.0f, 0.05f, 0.0f } },
        { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } },
    };

    // Unreferenced in the GC build, but kept in retail's .rodata.
    namespace GunSight
    {
        static const xVec3 vec_offset = { 0.0f, 0.0f, 1.0f };
    } // namespace GunSight

    namespace EngInfo
    {
        static const xVec3 vec_offset = { 0.0f, -0.25f, 0.0f };
    } // namespace EngInfo

    namespace JetSmoke
    {
        ExhaustCurve curve_data[4] = {
            { 0.0f, { 0x7F, 0x7F, 0x7F, 0x00 }, 0.25f },
            { 0.6f, { 0x7F, 0x7F, 0x7F, 0xC8 }, 1.0f },
            { 0.8f, { 0x28, 0x28, 0x28, 0xA0 }, 1.0f },
            { 1.0f, { 0x00, 0x00, 0x00, 0x00 }, 0.75f },
        };

        ExhaustEmitter jet_smoke = {
            NPAR_TYP_JETSMOKE,
            { 0, 0, { 0, 0 } },
            { 0.0f, 0.0f, 5.0f },
            { 1.0f, 1.0f },
            { 0.25f, { 0.1f, 0.9f }, 0x20, { 0, 0, 0 } },
            { 0.3f, 0.3f },
            { 2, 0, 0.25f, -0.25f },
            { 1.2f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, { 0.0f, 0.0f, 0.0f } },
            { curve_data, 4, 1.0f, 1.0f },
        };
    } // namespace JetSmoke

    namespace JetFlame
    {
        ExhaustCurve curve_data[5] = {
            { 0.0f, { 0xFA, 0xFA, 0xC8, 0xFF }, 0.1f },
            { 0.1f, { 0xFA, 0xF0, 0x19, 0xFF }, 0.25f },
            { 0.2f, { 0xFA, 0x8C, 0x32, 0xFF }, 0.75f },
            { 0.4f, { 0xBE, 0x00, 0x00, 0x7F }, 1.0f },
            { 1.0f, { 0x00, 0x00, 0x00, 0x00 }, 0.0f },
        };

        ExhaustEmitter jet_flame = {
            NPAR_TYP_JETFLAME,
            { 0, 0, { 0, 0 } },
            { 0.0f, 0.0f, 10.0f },
            { 1.0f, 1.0f },
            { 0.25f, { 0.1f, 0.9f }, 0x20, { 0, 0, 0 } },
            { 0.3f, 0.1f },
            { 3, 0, 0.12f, 0.0f },
            { 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, { 0.0f, 0.0f, 0.0f } },
            { curve_data, 5, 1.0f, 1.5f },
        };
    } // namespace JetFlame
} // namespace Zapper

void zNMEZapper::CreateConfig(const char* name)
{
    en_nmevar vidx;

    zNMEStandard::CreateConfig(name);
    vidx = NMEVariantIdx((en_npctyp)itemType);
    cfg_zapper = Zapper::Config::cfg_zap[vidx];
}

S32 zNMEZapper::DfltVulnFlags()
{
    return 0xCFFFFFF7;
}

void zNMEZapper::Init(xEntAsset* asset)
{
    zNMEStandard::Init(asset);

    gun_omatic.Init(this);

    engine.mOwnerNPC = this;
    engine.mState = zMervEngine::eState_Off;

    zNMEParFX_PartyInvite(NPAR_TYP_PLASMA, NULL, 0);
    zNMEParFX_PartyInvite(NPAR_TYP_ENERGY, NULL, 0);
    zNMEParFX_PartyInvite(NPAR_TYP_MUZZLE, NULL, 0);
    zNMEParFX_PartyInvite(NPAR_TYP_JETFLAME, NULL, 0);
    zNMEParFX_PartyInvite(NPAR_TYP_JETSMOKE, NULL, 0);
}

void zNMEZapper::Setup()
{
    zNMEStandard::Setup();
    gun_omatic.Setup();
    xAntennaAddAntenna(this, 0.0f, 0.0f, 8, 9, false);
}

void zNMEZapper::SelfSetup()
{
    ZapMainBrain();
    ZapMoveBrain();
}

void zNMEZapper::SelfDestroy()
{
    xBehaveMgr* bmgr;

    bmgr = xBehaveMgr_GetSelf();
    bmgr->UnSubscribe(psy_move);
    psy_move = NULL;

    zNMECommon::SelfDestroy();
}

// No GC symbol: always inlined into SelfSetup.
void zNMEZapper::ZapMainBrain()
{
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

    psy->AddGoal(NME_GOAL_ZAP_WOUND, NULL);
    psy->AddGoal(NME_GOAL_ZAP_NORMAL, NULL);
    psy->AddGoal(NME_GOAL_ZAP_AWARE, NULL);
    psy->AddGoal(NME_GOAL_ZAP_BATTLE, NULL);
    psy->AddGoal(NME_GOAL_ZAP_ZAP, NULL);

    psy->BrainEnd();

    psy->SetSafety(NME_GOAL_ZAP_NORMAL);
}

U32 zNMEZapper::AnimPick(S32 gid, en_npcgspot gspot, xGoal* goal)
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
    case NME_GOAL_ZAP_WOUND:
        if (pts_health < 1)
        {
            idx = STAN_ANIM_DEFEAT;
        }
        else
        {
            idx = STAN_ANIM_DAMAGE;
        }
        break;
    case NME_GOAL_ZAP_NORMAL:
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
    case NME_GOAL_ZAP_AWARE:
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
    case NME_GOAL_ZAP_BATTLE:
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
    case NME_GOAL_ZAP_ZAP:
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
    default:
        idx = STAN_ANIM_IDLE;
        break;
    }

    return g_hash_stananim[idx];
}

xAnimTable* zNMEZapper::CreateAnimTable(xAnimTable* table)
{
    if (table == NULL)
    {
        table = xAnimTableNew("zNMEZapper", 0);
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
    xAnimTableNewState(table, g_strz_stananim[STAN_ANIM_ATTACK_PREP], 0, 1, 1.0f, NULL, NULL, 0.0f,
                       NULL, NULL, xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, g_strz_stananim[STAN_ANIM_ATTACK_LOOP], 0x10, 1, 1.0f, NULL, NULL,
                       0.0f, NULL, NULL, xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, g_strz_stananim[STAN_ANIM_ATTACK_DONE], 0, 1, 1.0f, NULL, NULL, 0.0f,
                       NULL, NULL, xAnimDefaultBeforeEnter, NULL, NULL);

    S32 ourAnims[16] = { STAN_ANIM_IDLE,        STAN_ANIM_MOVE,        STAN_ANIM_ALERT,
                         STAN_ANIM_CHASE,       STAN_ANIM_SCARE_LOOP,  STAN_ANIM_SCARE_DONE,
                         STAN_ANIM_SPOOK_LOOP,  STAN_ANIM_NOTICE,      STAN_ANIM_TAUNT,
                         STAN_ANIM_VICTORY,     STAN_ANIM_DAMAGE,      STAN_ANIM_DEFEAT,
                         STAN_ANIM_ATTACK_PREP, STAN_ANIM_ATTACK_LOOP, STAN_ANIM_ATTACK_DONE,
                         STAN_ANIM_UNKNOWN };
    NMEC_BuildStandardAnimTran(table, g_strz_stananim, ourAnims, 1, 0.2f);

    return table;
}

S32 zNMEZapper::GoalPick(xGoal* goal, S32& gid_would, en_trantype& typ_tran)
{
    S32 altered = 1;

    switch (gid_would)
    {
    case NME_GOAL_TRIGGER_NORMAL:
        gid_would = NME_GOAL_ZAP_NORMAL;
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
        gid_would = NME_GOAL_ZAP_AWARE;
        break;
    case NME_GOAL_TRIGGER_BATTLE:
        gid_would = NME_GOAL_ZAP_BATTLE;
        break;
    case NME_GOAL_TRIGGER_ATTACK:
        gid_would = NME_GOAL_ZAP_ZAP;
        break;
    default:
        altered = 0;
        break;
    }

    return altered;
}

void zNMEZapper::Reset()
{
    xPsyche* psy;
    zNMEGoalAfterlife* goal;

    zNMEStandard::Reset();

    psy = psy_self;

    goal = (zNMEGoalAfterlife*)psy->FindGoal(NME_GOAL_AFTERLIFE);
    goal->stylinDyin = DIE_LOUDLY;

    if (((zNMEAssetStandard*)nmeass)->flg_stdass & 0x1)
    {
        psy->GoalSet(NME_GOAL_SCAREWAIT, 1);
    }
    else if (npc_duplodude)
    {
        zNMEGoalAfterlife* goal = (zNMEGoalAfterlife*)psy->FindGoal(NME_GOAL_AFTERLIFE);
        goal->stylinDyin = DIE_DUPLOONLY;
        psy->GoalSet(NME_GOAL_AFTERLIFE, 1);
        goal->stylinDyin = DIE_LOUDLY;
    }
    else
    {
        psy->GoalSet(NME_GOAL_ZAP_NORMAL, 1);
    }

    pts_health = cfg_zapper->health.pts_healthMax;
    gun_omatic.Reset();
    engine.Reset();
}

void zNMEZapper::BUpdate(xVec3* pos)
{
    xMat4x3* mat_bone;
    xMat4x3* mat_root;
    xVec3 pos_bone;

    mat_root = (xMat4x3*)model->Mat;
    mat_bone = (xMat4x3*)&model->Mat[3];
    xMat4x3Toworld(&pos_bone, mat_root, &mat_bone->pos);
    Zapper::bnd_info.UpdateBound(&bound, this, &pos_bone);
}

void zNMEZapper::Process(F32 dt)
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

        ShadStufUpdate();
        JetEngine(dt);
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

    engine.Process(dt);
}

void zNMEZapper::NewTime(F32 dt)
{
    gun_omatic.NewTimeUpdate(dt);
}

void zNMEZapper::RenderExtra()
{
    if (model->Flags & 0x400)
    {
        return;
    }

    if (NME::GlobalData::flg_wonder & 1)
    {
        return;
    }

    gun_omatic.Render();
}

en_vis zNMEZapper::VisionTarget(xEnt* ent, en_defcon defcon, S32 flags) const
{
    const NMECfgZapper* cfg;
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

    cfg = cfg_zapper;
    // Both branches are the same in retail and in the PS2 build.
    if (ent != xglobals->___player_ent_dont_use_directly)
    {
        XZVecToPos(&dir, xEntGetPos(ent), &dy);
    }
    else
    {
        XZVecToPos(&dir, xEntGetPos(ent), &dy);
    }
    dst = dir.length();

    if (runtimeData.overrideDetect == ALLOW_NORMAL && runtimeData.overrideAttack == ALLOW_NORMAL &&
        dst > cfg->ranges.rad_awaregrey)
    {
        return VIS_NOBODY_AROUND;
    }

    if (runtimeData.overrideAttack != ALLOW_NEVER)
    {
        if (dst < cfg->ranges.rad_tooclose)
        {
            return VIS_OH_DEAR_GAHD;
        }

        if (runtimeData.overrideAttack == ALLOW_ALWAYS || dst < cfg->ranges.rad_zap)
        {
            xVec3SMulBy(&dir, 1.0f / dst);
            if (xVec3Dot(&dir, (xVec3*)&model->Mat->at) < 0.9f)
            {
                return VIS_HORROR_FLIK;
            }
            return VIS_PREPARE_TO_DIE;
        }

        if (dst < cfg->ranges.rad_zapgrey)
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

F32 zNMEZapper::HealthRatio()
{
    F32 rat;

    if (cfg_zapper == NULL)
    {
        return 0.0f;
    }

    rat = pts_health;
    rat /= cfg_zapper->health.pts_healthMax;
    rat = CLAMP(rat, 0.0f, 1.0f);
    return rat;
}

void zNMEZapper::JetEngine(F32 dt)
{
    F32 delay;
    F32 ds2_plyr;
    xMat4x3* mat_bone;
    xMat4x3* mat_root;
    xVec3 pos_emit;

    tmr_emitExahust = MAX(-1.0f, tmr_emitExahust - dt);
    if (tmr_emitExahust < 0.0f)
    {
        delay = (gVidMode == eGameVidModePAL) ? 0.02f : (1.0f / 60.0f);
        tmr_emitExahust = 5.0f * delay;

        ds2_plyr = XYZDstSqToPlayer(NULL);
        if (ds2_plyr > 625.0f)
        {
            return;
        }

        mat_root = (xMat4x3*)model->Mat;
        mat_bone = (xMat4x3*)&model->Mat[25];

        pos_emit = Zapper::EngInfo::vec_offset;
        xMat4x3Toworld(&pos_emit, mat_bone, &pos_emit);
        xMat4x3Toworld(&pos_emit, mat_root, &pos_emit);

        xVec3 dir_emit = mat_bone->up * -1.0f;
        xMat3x3RMulVec(&dir_emit, mat_root, &dir_emit);

        Zapper::JetSmoke::jet_smoke.EmitRing(&pos_emit, &dir_emit, -100000.0f, 1.0f, -1, NULL);
        Zapper::JetFlame::jet_flame.EmitRing(&pos_emit, &dir_emit, -100000.0f, 1.0f, -1, NULL);
    }
}

S32 zNMEZapper::TypeHandleMail(NMEMsg* mail)
{
    S32 handled = 1;

    switch (mail->msgid)
    {
    case NME_MID_DAMAGE:
        handled = ZapMailDamage(&mail->dmgdata);
        break;
    case NME_MID_SYSEVENT:
        handled = ZapMailSys(&mail->sysevent);
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
inline S32 zNMEZapper::ZapMailDamage(NMEDamageInfo* dmgmail)
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

        psy_self->GoalSet(NME_GOAL_ZAP_WOUND, 0);
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

S32 zNMEZapper::ZapMailSys(NMESysEvent* sysmail)
{
    switch (sysmail->toEvent)
    {
    case eEventNPCSetActiveOff:
        engine.SetState(zMervEngine::eState_Off);
        return DfltSysEventMail(sysmail);
    case eEventNPCSetActiveOn:
        switch (psy_self->GIDOfActive())
        {
        case NME_GOAL_ZAP_NORMAL:
        case NME_GOAL_ZAP_AWARE:
            engine.SetState(zMervEngine::eState_Idle);
            break;
        case NME_GOAL_ZAP_BATTLE:
        case NME_GOAL_ZAP_WOUND:
        case NME_GOAL_ZAP_ZAP:
        case NME_GOAL_ZAP_MOVE:
            engine.SetState(zMervEngine::eState_Alert);
            break;
        }
        return DfltSysEventMail(sysmail);
    }

    return DfltSysEventMail(sysmail);
}

// No GC symbol (inlined into the ZapAware, ZapBattle and ZapZap goals).
inline S32 zNMEZapper::SafeToTaunt()
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

void ZapLetMeKnow::Notice(en_psynote note, xGoal* goal, void* data)
{
    zNMEZapper* zapper = (zNMEZapper*)goal->psyche->clt_owner;
    S32 gid_main = goal->GetID();
    xPsyche* psy_move = zapper->psy_move;
    S32 gid_move = psy_move->GIDOfActive();
    S32 gid_want;
    ZapGun::en_zapmod zapmode;

    gid_want = MoveGoalFor(gid_main);

    if (gid_want == 0)
    {
        gid_want = NME_GOAL_NOMANLAND;
    }

    if (gid_want != gid_move)
    {
        psy_move->GoalSet(gid_want, 0);
    }

    zapmode = SetGunnerState(gid_main);
    zapper->gun_omatic.ModeSet(zapmode, 0);
}

S32 zNMEGoalZapNormal::Enter(F32 dt, void* updCtxt)
{
    zNMEZapper* npc = (zNMEZapper*)psyche->clt_owner;

    ((zNMEGoalSentry*)npc->psy_move->FindGoal(NME_GOAL_ZAP_MOVE))->Sentry_DoRot(1);
    npc->engine.SetState(zMervEngine::eState_Idle);
    return zNMEGoalCommon::Enter(dt, updCtxt);
}

S32 zNMEGoalZapNormal::Exit(F32 dt, void* updCtxt)
{
    ((zNMEGoalSentry*)((zNMEZapper*)psyche->clt_owner)->psy_move->FindGoal(NME_GOAL_ZAP_MOVE))
        ->Sentry_DoRot(0);
    return 0;
}

S32 zNMEGoalZapNormal::Process(en_trantype* trantype, F32 dt, void* ctxt)
{
    zNMEZapper* npc = (zNMEZapper*)psyche->clt_owner;
    S32 nextgoal = 0;
    en_vis vis;

    if (globals.player.s->Health >= 1)
    {
        vis = npc->VisionTarget(xglobals->___player_ent_dont_use_directly, NME_DEFCON_NORMAL, 0);
        if (vis <= VIS_I_SEEE_YOUUU)
        {
            *trantype = GOAL_TRAN_SET;
            nextgoal = NME_GOAL_TRIGGER_ALERT;
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

    return zNMEGoalCommon::Process(trantype, dt, ctxt);
}

S32 zNMEGoalZapAware::Process(en_trantype* trantype, F32 dt, void* ctxt)
{
    S32 nextgoal = 0;
    zNMEZapper* npc = (zNMEZapper*)psyche->clt_owner;
    S32 doTran;

    doTran = ChkTheSituation(npc, nextgoal, *trantype);
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

    npc->MatFacePlayer(dt, npc->cfg_zapper->battle.spd_turn, 1);
    return zNMEGoalCommon::Process(trantype, dt, ctxt);
}

// No GC symbol (inlined into zNMEGoalZapAware::Process).
inline S32 zNMEGoalZapAware::ChkTheSituation(zNMEZapper* npc, S32& nextgoal, en_trantype& trantype)
{
    en_vis vis;

    if (npc->SafeToTaunt())
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

    vis = npc->VisionTarget(xglobals->___player_ent_dont_use_directly, NME_DEFCON_NORMAL, 0);
    if (vis > VIS_GREY_AND_FOGGY)
    {
        nextgoal = NME_GOAL_TRIGGER_NORMAL;
        trantype = GOAL_TRAN_SET;
        npc->GoalPick(this, nextgoal, trantype);
        return 1;
    }

    if (vis < VIS_GET_BACK_HERE)
    {
        trantype = GOAL_TRAN_SET;
        nextgoal = NME_GOAL_TRIGGER_BATTLE;
        npc->GoalPick(this, nextgoal, trantype);
        return 1;
    }

    return 0;
}

S32 zNMEGoalZapBattle::Enter(F32 dt, void* updCtxt)
{
    zNMEZapper* npc = (zNMEZapper*)psyche->clt_owner;

    tmr_tossDelay = -1.0f;
    npc->engine.SetState(zMervEngine::eState_Alert);
    return zNMEGoalCommon::Enter(dt, updCtxt);
}

S32 zNMEGoalZapBattle::Process(en_trantype* trantype, F32 dt, void* ctxt)
{
    S32 nextgoal = 0;
    zNMEZapper* npc = (zNMEZapper*)psyche->clt_owner;
    S32 doTran;

    if (npc->SafeToTaunt())
    {
        nextgoal = NME_GOAL_TRIGGER_VINIVICIVIDI;
        if (globals.player.s->Health < 1)
        {
            *trantype = GOAL_TRAN_SET;
        }
        else
        {
            *trantype = GOAL_TRAN_PUSH;
        }
        npc->GoalPick(this, nextgoal, *trantype);
        return nextgoal;
    }

    doTran = ChkTheSituation(npc, nextgoal, *trantype);
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

    npc->MatFacePlayer(dt, npc->cfg_zapper->battle.spd_turn, 1);
    return zNMEGoalCommon::Process(trantype, dt, ctxt);
}

// No GC symbol (inlined into zNMEGoalZapBattle::Process).
inline S32 zNMEGoalZapBattle::ChkTheSituation(zNMEZapper* npc, S32& nextgoal, en_trantype& trantype)
{
    en_vis vis;

    vis = npc->VisionTarget(xglobals->___player_ent_dont_use_directly, NME_DEFCON_NORMAL, 0);
    if (vis > VIS_GET_BACK_HERE)
    {
        trantype = GOAL_TRAN_SET;
        nextgoal = NME_GOAL_TRIGGER_NORMAL;
        npc->GoalPick(this, nextgoal, trantype);
        return 1;
    }

    if (vis == VIS_HORROR_FLIK)
    {
        return 0;
    }

    if (vis == VIS_PREPARE_TO_DIE || vis == VIS_GET_BACK_HERE)
    {
        trantype = GOAL_TRAN_PUSH;
        nextgoal = NME_GOAL_ZAP_ZAP;
        return 1;
    }

    return 0;
}

S32 zNMEGoalZapZap::Enter(F32 dt, void* updCtxt)
{
    cnt_loop = 10;
    flg_info |= 0x10;
    ((zNMEZapper*)psyche->clt_owner)->engine.SetState(zMervEngine::eState_Alert);
    return zNMEGoalLoopAnim::Enter(dt, updCtxt);
}

S32 zNMEGoalZapZap::Process(en_trantype* trantype, F32 dt, void* ctxt)
{
    S32 nextgoal = 0;
    zNMEZapper* npc = (zNMEZapper*)psyche->GetClient();
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
    else
    {
        *trantype = GOAL_TRAN_SET;
        nextgoal = NME_GOAL_TRIGGER_BATTLE;
        npc->GoalPick(this, nextgoal, *trantype);
        return nextgoal;
    }

    spd_turn = npc->cfg_zapper->battle.spd_turn;
    npc->MatFacePlayer(dt, spd_turn, 1);
    return zNMEGoalLoopAnim::Process(trantype, dt, ctxt);
}

S32 zNMEGoalZapWound::Enter(F32 dt, void* updCtxt)
{
    tmr_inPain = 0.875f;
    return zNMEGoalCommon::Enter(dt, updCtxt);
}

// The '? 1 : 0' materializes the timer test as 0/1 (as BfBB's zNPCGoalRobo timer tests do); the
// anim time test is nested as in zNMEGoalMervWound::Process.
S32 zNMEGoalZapWound::Process(en_trantype* trantype, F32 dt, void* ctxt)
{
    zNMEZapper* npc = (zNMEZapper*)psyche->clt_owner;

    if ((tmr_inPain < 0.0f) ? 1 : 0)
    {
        // not a PS2 DWARF local, but the time is read on its own line (named as in
        // zNMEGoalFlingAttack::Process)
        F32 tym_animCurr = npc->AnimTimeCurrent();
        if (tym_animCurr + dt >= npc->AnimDuration(NULL))
        {
            if (npc->pts_health < 1)
            {
                *trantype = GOAL_TRAN_SET;
                return NME_GOAL_AFTERLIFE;
            }

            *trantype = GOAL_TRAN_SET;
            return NME_GOAL_ZAP_BATTLE;
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

S32 zNMEGoalZapMove::Enter(F32 dt, void* updCtxt)
{
    zNMEZapper* npc = (zNMEZapper*)psyche->clt_owner;

    Sentry_Enter(npc);
    npc->spd_throttle = 0.0f;
    return zNMEGoalCommon::Enter(dt, updCtxt);
}

// No GC or PS2 symbol (always inlined); the name is ours (zNMEGoalFlingMove::FiddleThrottle is
// the Flinger's equivalent). Throttles toward the speed of the main brain's goal.
static inline void ZapFiddleThrottle(zNMEZapper* npc, F32 dt)
{
    const NMECfgCommon* cfg_comm = npc->cfg_common;
    const NMECfgZapper* cfg = npc->cfg_zapper;
    S32 gid = npc->psy_self->GIDOfActive();
    F32 spd;
    F32 acc;

    switch (gid)
    {
    case NME_GOAL_ZAP_NORMAL:
    case NME_GOAL_ZAP_AWARE:
        spd = cfg_comm->movement.spd_move;
        acc = cfg_comm->movement.acc_move;
        break;
    case NME_GOAL_ZAP_BATTLE:
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

S32 zNMEGoalZapMove::Process(en_trantype* trantype, F32 dt, void* ctxt)
{
    zNMEZapper* npc = (zNMEZapper*)psyche->clt_owner;

    if (flg_sentry & 1)
    {
        npc->spd_throttle = 0.0f;
    }
    else
    {
        ZapFiddleThrottle(npc, dt);
    }

    Sentry_Update(npc, dt);
    return zNMEGoalCommon::Process(trantype, dt, ctxt);
}
