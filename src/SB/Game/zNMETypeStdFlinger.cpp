#include "zNMETypeStdFlinger.h"

namespace Flinger
{
    namespace Config
    {
        static const NMECfgFlinger cfg_flinger_v1 = {
            { 3 },
            { 5.5f, 20.0f, 25.0f, 35.0f, 40.0f },
            { 4.0f, 7.0f, 6.0f },
            { HAZ_TYP_FLING_V1, 8.0f, 5.0f, 0.5f, { 0.0f, -0.2f, 0.05f } },
        };
        static const NMECfgFlinger cfg_flinger_v2 = {
            { 4 },
            { 5.5f, 20.0f, 25.0f, 35.0f, 40.0f },
            { 3.0f, 8.0f, 8.0f },
            { HAZ_TYP_FLING_V2, 8.0f, 5.0f, 0.5f, { 0.0f, -0.2f, 0.05f } },
        };
        static const NMECfgFlinger cfg_flinger_v3 = {
            { 6 },
            { 5.5f, 20.0f, 25.0f, 35.0f, 40.0f },
            { 2.0f, 9.0f, 9.0f },
            { HAZ_TYP_FLING_V3, 8.0f, 5.0f, 0.5f, { 0.0f, -0.2f, 0.05f } },
        };
        const NMECfgFlinger* cfg_fling[10] = { &cfg_flinger_v1, &cfg_flinger_v1, &cfg_flinger_v2,
                                               &cfg_flinger_v3 };
    } // namespace Config

    // Unreferenced in the GC build, but kept in retail's .rodata.
    namespace FX
    {
        static const PuffEmitter ickyDrops = {
            NPAR_TYP_DUSTPUFF,
            1.5f,
            { 0xAA, 0xAA, 0x78, 0x9B },
            { 0.25f, 0.35f },
            { 0.0f, -15.0f, 0.0f },
            7.2f,
            93,
            0,
            5,
            0,
            { 0.2f, 5.0f },
        };
    } // namespace FX

    NMEHelpBound::BndData bnd_info = {
        k_XBOUNDTYPE_SPHERE,
        { 0, 0, { 0, 0 } },
        { 1.5f, { 0.0f, 1.25f, 0.25f } },
        { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } },
    };
} // namespace Flinger

// No symbol (always inlined); the name is ours. The move-brain goal that goes with a goal
// entered by the main brain (0: none).
static inline S32 FlingMoveGoalFor(S32 gid)
{
    switch (gid)
    {
    case NME_GOAL_FLINGER_NORMAL:
    case NME_GOAL_FLINGER_AWARE:
    case NME_GOAL_FLINGER_BATTLE:
    case NME_GOAL_FLINGER_BOING:
    case NME_GOAL_FLINGER_FLEE:
    case NME_GOAL_FLINGER_PANIC:
    case NME_GOAL_TAUNT:
        return NME_GOAL_FLINGER_MOVE;
    case NME_GOAL_FLINGER_ATTACK:
    case NME_GOAL_DEV_ANIMVIEW:
    case NME_GOAL_DEV_HEROMODE:
    case NME_GOAL_AFTERLIFE:
    case NME_GOAL_SPAWN:
    case NME_GOAL_WOUND:
    case NME_GOAL_SPOOKED:
    case NME_GOAL_NOTICE:
    case NME_GOAL_SCAREWAIT:
    case NME_GOAL_SCARE:
        return NME_GOAL_NOMANLAND;
    }

    return 0;
}

void zNMEFlinger::CreateConfig(const char* name)
{
    en_nmevar vidx;

    zNMEStandard::CreateConfig(name);
    vidx = NMEVariantIdx((en_npctyp)itemType);
    cfg_flinger = Flinger::Config::cfg_fling[vidx];
}

S32 zNMEFlinger::DfltVulnFlags()
{
    return 0xCFFFFFF7;
}

void zNMEFlinger::Init(xEntAsset* asset)
{
    zNMEStandard::Init(asset);
    zNMEParFX_PartyInvite(NPAR_TYP_DUSTPUFF, NULL, 0);
}

void zNMEFlinger::BUpdate(xVec3* pos)
{
    Flinger::bnd_info.UpdateBound(&bound, this, pos);
}

void zNMEFlinger::Setup()
{
    zNMEStandard::Setup();

    if (itemType != NME_TYPE_FLINGER_V2)
    {
        xAntennaAddAntenna(this, 0.0f, 0.0f, 7, 8, false);
    }
}

void zNMEFlinger::SelfSetup()
{
    FlingMainBrain();
    FlingMoveBrain();
}

void zNMEFlinger::SelfDestroy()
{
    xBehaveMgr* bmgr;

    bmgr = xBehaveMgr_GetSelf();
    bmgr->UnSubscribe(psy_move);
    psy_move = NULL;

    zNMECommon::SelfDestroy();
}

// Compiled-out asserts (conditions unknown). With them the function is too big to be
// auto-inlined into SelfSetup; retail calls it.
void zNMEFlinger::FlingMainBrain()
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

    xBehaveMgr* bmgr;
    xPsyche* psy;

    bmgr = xBehaveMgr_GetSelf();
    psy_self = bmgr->Subscribe(this, 0);

    psy = psy_self;

    psy->BrainBegin();

    psy->AddGoal(NME_GOAL_WOUND, NULL);
    psy->AddGoal(NME_GOAL_AFTERLIFE, NULL);
    psy->AddGoal(NME_GOAL_SPAWN, NULL);
    psy->AddGoal(NME_GOAL_SCAREWAIT, NULL);
    psy->AddGoal(NME_GOAL_SCARE, NULL);
    psy->AddGoal(NME_GOAL_TAUNT, NULL);
    psy->AddGoal(NME_GOAL_NOTICE, NULL);
    psy->AddGoal(NME_GOAL_SPOOKED, NULL);

    psy->AddGoal(NME_GOAL_FLINGER_NORMAL, NULL);
    psy->AddGoal(NME_GOAL_FLINGER_AWARE, NULL);
    psy->AddGoal(NME_GOAL_FLINGER_BATTLE, NULL);
    psy->AddGoal(NME_GOAL_FLINGER_ATTACK, NULL);
    psy->AddGoal(NME_GOAL_FLINGER_FLEE, NULL);
    psy->AddGoal(NME_GOAL_FLINGER_BOING, NULL);
    psy->AddGoal(NME_GOAL_FLINGER_PANIC, NULL);

    psy->BrainEnd();

    psy->SetSafety(NME_GOAL_FLINGER_NORMAL);
}

U32 zNMEFlinger::AnimPick(S32 gid, en_npcgspot gspot, xGoal* goal)
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
    case NME_GOAL_FLINGER_NORMAL:
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
    case NME_GOAL_FLINGER_AWARE:
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
        else if (gspot == NME_GSPOT_PATROLPAUSE)
        {
            idx = STAN_ANIM_ALERT;
        }
        else
        {
            idx = STAN_ANIM_CHASE;
        }
        break;
    case NME_GOAL_FLINGER_BATTLE:
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
        else if (gspot == NME_GSPOT_PATROLPAUSE)
        {
            idx = STAN_ANIM_ALERT;
        }
        else
        {
            idx = STAN_ANIM_CHASE;
        }
        break;
    case NME_GOAL_FLINGER_ATTACK:
        idx = STAN_ANIM_ATTACK;
        break;
    case NME_GOAL_FLINGER_PANIC:
        idx = STAN_ANIM_PANIC;
        break;
    case NME_GOAL_FLINGER_FLEE:
        if (gspot == NME_GSPOT_START)
        {
            idx = STAN_ANIM_DAMAGE;
        }
        else if (gspot == NME_GSPOT_STARTALT)
        {
            idx = STAN_ANIM_FLEE;
        }
        else if (gspot == NME_GSPOT_RESUME)
        {
            idx = STAN_ANIM_FLEE;
        }
        else if (gspot == NME_GSPOT_PATROLPAUSE)
        {
            idx = STAN_ANIM_ALERT;
        }
        else
        {
            idx = STAN_ANIM_FLEE;
        }
        break;
    case NME_GOAL_FLINGER_BOING:
        if (gspot == NME_GSPOT_START)
        {
            idx = STAN_ANIM_DEFLECT_PREP;
        }
        else if (gspot == NME_GSPOT_RESUME)
        {
            idx = STAN_ANIM_DEFLECT_LOOP;
        }
        else if (gspot == NME_GSPOT_FINISH)
        {
            idx = STAN_ANIM_DEFLECT_DONE;
        }
        else
        {
            idx = STAN_ANIM_DEFLECT_PREP;
        }
        break;
    default:
        idx = STAN_ANIM_IDLE;
        break;
    }

    return g_hash_stananim[idx];
}

xAnimTable* zNMEFlinger::CreateAnimTable(xAnimTable* table)
{
    if (table == NULL)
    {
        table = xAnimTableNew("zNMEFlinger", 0);
    }

    xAnimTableNewState(table, g_strz_stananim[STAN_ANIM_IDLE], 0x10, 1, 1.0f, NULL, NULL, 0.0f,
                       NULL, NULL, xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, g_strz_stananim[STAN_ANIM_MOVE], 0x10, 1, 1.0f, NULL, NULL, 0.0f,
                       NULL, NULL, xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, g_strz_stananim[STAN_ANIM_ALERT], 0x10, 1, 1.0f, NULL, NULL, 0.0f,
                       NULL, NULL, xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, g_strz_stananim[STAN_ANIM_CHASE], 0x10, 1, 1.0f, NULL, NULL, 0.0f,
                       NULL, NULL, xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, g_strz_stananim[STAN_ANIM_FLEE], 0x10, 1, 1.0f, NULL, NULL, 0.0f,
                       NULL, NULL, xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, g_strz_stananim[STAN_ANIM_PANIC], 0x10, 1, 1.0f, NULL, NULL, 0.0f,
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
    xAnimTableNewState(table, g_strz_stananim[STAN_ANIM_DAMAGE], 0x20, 1, 1.0f, NULL, NULL, 0.0f,
                       NULL, NULL, xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, g_strz_stananim[STAN_ANIM_DEFEAT], 0x10, 1, 1.0f, NULL, NULL, 0.0f,
                       NULL, NULL, xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, g_strz_stananim[STAN_ANIM_ATTACK], 0, 1, 1.0f, NULL, NULL, 0.0f, NULL,
                       NULL, xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, g_strz_stananim[STAN_ANIM_DEFLECT_PREP], 0x20, 1, 1.0f, NULL, NULL,
                       0.0f, NULL, NULL, xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, g_strz_stananim[STAN_ANIM_DEFLECT_LOOP], 0x10, 1, 1.0f, NULL, NULL,
                       0.0f, NULL, NULL, xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, g_strz_stananim[STAN_ANIM_DEFLECT_DONE], 0, 1, 1.0f, NULL, NULL, 0.0f,
                       NULL, NULL, xAnimDefaultBeforeEnter, NULL, NULL);

    xAnimTableNewTransition(table, g_strz_stananim[STAN_ANIM_DEFLECT_PREP],
                            g_strz_stananim[STAN_ANIM_DEFLECT_LOOP], NULL, NULL, 0x10, 0, 0.0f,
                            0.0f, 0, 0, 0.2f, NULL);
    xAnimTableNewTransition(table, g_strz_stananim[STAN_ANIM_DAMAGE],
                            g_strz_stananim[STAN_ANIM_FLEE], NULL, NULL, 0x10, 0, 0.0f, 0.0f, 0, 0,
                            0.2f, NULL);

    return table;
}

S32 zNMEFlinger::GoalPick(xGoal* goal, S32& gid_would, en_trantype& trantype)
{
    S32 altered = 1;

    switch (gid_would)
    {
    case NME_GOAL_TRIGGER_NORMAL:
        gid_would = NME_GOAL_FLINGER_NORMAL;
        break;
    case NME_GOAL_TRIGGER_SCARY:
        gid_would = NME_GOAL_SPOOKED;
        break;
    case NME_GOAL_TRIGGER_DETECT:
        gid_would = NME_GOAL_NOTICE;
        break;
    case NME_GOAL_TRIGGER_ALERT:
        gid_would = NME_GOAL_FLINGER_AWARE;
        break;
    case NME_GOAL_TRIGGER_BATTLE:
        gid_would = NME_GOAL_FLINGER_BATTLE;
        break;
    case NME_GOAL_TRIGGER_ATTACK:
        gid_would = NME_GOAL_FLINGER_ATTACK;
        break;
    default:
        altered = 0;
        break;
    }

    return altered;
}

void zNMEFlinger::Reset()
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
        psy->GoalSet(NME_GOAL_FLINGER_NORMAL, 1);
    }

    ((zNMEGoalSentry*)psy_move->FindGoal(NME_GOAL_FLINGER_MOVE))->Sentry_DoRot(0);
    pts_health = cfg_flinger->health.pts_healthMax;
}

void zNMEFlinger::Process(F32 dt)
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
        ShadStufUpdate();

        zNMECommon::Process(dt);
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

en_vis zNMEFlinger::VisionTarget(xEnt* ent, en_defcon defcon, S32 flags) const
{
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

    XZVecToPos(&dir, xEntGetPos(ent), &dy);
    dst = dir.length();
    const NMECfgFlinger* cfg = cfg_flinger;

    if (runtimeData.overrideDetect == ALLOW_NORMAL && runtimeData.overrideAttack == ALLOW_NORMAL &&
        dst > cfg->ranges.rad_awaregrey)
    {
        return VIS_NOBODY_AROUND;
    }

    if (defcon == NME_DEFCON_NORMAL)
    {
        if (runtimeData.overrideAttack == ALLOW_ALWAYS)
        {
            return VIS_I_SEEE_YOUUU;
        }

        if (runtimeData.overrideDetect == ALLOW_ALWAYS)
        {
            return VIS_I_SEEE_YOUUU;
        }

        if (dst < cfg->ranges.rad_aware)
        {
            return VIS_I_SEEE_YOUUU;
        }

        return VIS_NOBODY_AROUND;
    }

    if (defcon == NME_DEFCON_AWARE)
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

        if (dst < cfg->ranges.rad_awaregrey)
        {
            return VIS_GREY_AND_FOGGY;
        }

        if (runtimeData.overrideDetect == ALLOW_ALWAYS)
        {
            return VIS_GREY_AND_FOGGY;
        }

        return VIS_NOBODY_AROUND;
    }

    if (defcon == NME_DEFCON_BATTLE)
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

        if (dst < cfg->ranges.rad_awaregrey)
        {
            return VIS_GREY_AND_FOGGY;
        }

        if (runtimeData.overrideDetect == ALLOW_ALWAYS)
        {
            return VIS_GREY_AND_FOGGY;
        }

        return VIS_NOBODY_AROUND;
    }

    return VIS_NOBODY_AROUND;
}

F32 zNMEFlinger::HealthRatio()
{
    F32 rat;

    if (cfg_flinger == NULL)
    {
        return 0.0f;
    }

    rat = pts_health;
    rat /= cfg_flinger->health.pts_healthMax;
    rat = CLAMP(rat, 0.0f, 1.0f);
    return rat;
}

S32 zNMEFlinger::TypeHandleMail(NMEMsg* mail)
{
    S32 handled = 1;

    switch (mail->msgid)
    {
    case NME_MID_DAMAGE:
        handled = FlingMailDamage(&mail->dmgdata);
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

S32 zNMEFlinger::FlingMailDamage(NMEDamageInfo* dmgmail)
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
    case DMGTYP_SIDE:
        psy_self->GoalSet(NME_GOAL_FLINGER_BOING, 0);
        break;
    case DMGTYP_ABOVE:
    case DMGTYP_BELOW:
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

        if (pts_health < 1)
        {
            WoundedInput(NME_GOAL_WOUND, dmgmail);
            psy_self->GoalSet(NME_GOAL_WOUND, 0);
        }
        else
        {
            WoundedInput(NME_GOAL_WOUND, dmgmail);
            psy_self->GoalSet(NME_GOAL_FLINGER_FLEE, 0);
        }
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

void zNMEFlinger::WoundedInput(en_npcgol gid, NMEDamageInfo* dmgmail)
{
    xGoal* goal;

    if (gid == NME_GOAL_FLINGER_FLEE)
    {
        // Retail still tests the FindGoal result, though nothing uses the goal.
        goal = psy_self->FindGoal(gid);
        if (goal == NULL)
        {
            return;
        }
    }
    else
    {
        zNMEStandard::WoundedInput(gid, dmgmail);
    }
}

void FlingLetMeKnow::Notice(en_psynote note, xGoal* goal, void* data)
{
    xPsyche* psy_move = ((zNMEFlinger*)goal->psyche->clt_owner)->psy_move;
    S32 gid_move = psy_move->GIDOfActive();
    S32 gid_want;

    gid_want = FlingMoveGoalFor(goal->GetID());
    if (gid_want == 0)
    {
        gid_want = NME_GOAL_NOMANLAND;
    }

    if (gid_want != gid_move)
    {
        psy_move->GoalSet(gid_want, 0);
    }
}

S32 zNMEGoalFlingNormal::Enter(F32 dt, void* updCtxt)
{
    ((zNMEGoalSentry*)((zNMEFlinger*)psyche->clt_owner)->psy_move->FindGoal(NME_GOAL_FLINGER_MOVE))
        ->Sentry_DoRot(1);
    return zNMEGoalCommon::Enter(dt, updCtxt);
}

S32 zNMEGoalFlingNormal::Exit(F32 dt, void* updCtxt)
{
    ((zNMEGoalSentry*)((zNMEFlinger*)psyche->clt_owner)->psy_move->FindGoal(NME_GOAL_FLINGER_MOVE))
        ->Sentry_DoRot(0);
    return 0;
}

S32 zNMEGoalFlingNormal::Process(en_trantype* trantype, F32 dt, void* ctxt)
{
    zNMEFlinger* npc = (zNMEFlinger*)psyche->clt_owner;
    S32 nextgoal = 0;
    en_vis iseeplyr;

    if (globals.player.s->Health >= 1)
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

    return zNMEGoalCommon::Process(trantype, dt, ctxt);
}

S32 zNMEGoalFlingAware::Process(en_trantype* trantype, F32 dt, void* ctxt)
{
    S32 nextgoal = 0;
    zNMEFlinger* npc = (zNMEFlinger*)psyche->clt_owner;
    en_vis iseeplyr;

    if (globals.player.s->Health < 1)
    {
        *trantype = GOAL_TRAN_SET;
        nextgoal = NME_GOAL_TRIGGER_NORMAL;
        npc->GoalPick(this, nextgoal, *trantype);
        return nextgoal;
    }

    iseeplyr = npc->VisionTarget(xglobals->___player_ent_dont_use_directly, NME_DEFCON_AWARE, 0);
    if (iseeplyr > VIS_GREY_AND_FOGGY)
    {
        *trantype = GOAL_TRAN_SET;
        nextgoal = NME_GOAL_TRIGGER_NORMAL;
        npc->GoalPick(this, nextgoal, *trantype);
        return nextgoal;
    }

    if (iseeplyr < VIS_GET_BACK_HERE)
    {
        *trantype = GOAL_TRAN_SET;
        nextgoal = NME_GOAL_TRIGGER_BATTLE;
        npc->GoalPick(this, nextgoal, *trantype);
        return nextgoal;
    }

    npc->MatFacePlayer(dt, PI, 1);
    return zNMEGoalCommon::Process(trantype, dt, ctxt);
}

S32 zNMEGoalFlingBattle::Enter(F32 dt, void* updCtxt)
{
    tmr_tossDelay = -1.0f;
    return zNMEGoalCommon::Enter(dt, updCtxt);
}

S32 zNMEGoalFlingBattle::Process(en_trantype* trantype, F32 dt, void* ctxt)
{
    zNMEFlinger* npc = (zNMEFlinger*)psyche->clt_owner;
    S32 nextgoal;
    S32 doTran;

    if (!(((zNMEGoalFlingMove*)npc->psy_move->FindGoal(NME_GOAL_FLINGER_MOVE))->tmr_delay < 0.0f))
    {
        nextgoal = 0;
        doTran = SubStateUpdate(npc, dt, nextgoal, *trantype);
        if (doTran)
        {
            return nextgoal;
        }
    }

    tmr_tossDelay = MAX(-1.0f, tmr_tossDelay - dt);
    npc->MatFacePlayer(dt, PI, 1);
    return zNMEGoalCommon::Process(trantype, dt, ctxt);
}

// No GC symbol (always inlined into Process).
inline S32 zNMEGoalFlingBattle::SubStateUpdate(zNMEFlinger* npc, F32 dt, S32& nextgoal,
                                               en_trantype& trantype)
{
    en_vis iseeplyr;

    if (globals.player.s->Health < 1)
    {
        trantype = GOAL_TRAN_SET;
        nextgoal = NME_GOAL_TRIGGER_NORMAL;
        npc->GoalPick(this, nextgoal, trantype);
        return 1;
    }

    iseeplyr = npc->VisionTarget(xglobals->___player_ent_dont_use_directly, NME_DEFCON_BATTLE, 0);
    if (iseeplyr > VIS_GET_BACK_HERE)
    {
        trantype = GOAL_TRAN_SET;
        nextgoal = NME_GOAL_TRIGGER_NORMAL;
        npc->GoalPick(this, nextgoal, trantype);
        return 1;
    }

    if (!(iseeplyr == VIS_HORROR_FLIK || iseeplyr == VIS_OH_DEAR_GAHD) && tmr_tossDelay < 0.0f)
    {
        tmr_tossDelay = npc->cfg_flinger->battle.tym_reload;
        if (npc->runtimeData.overrideAttack != ALLOW_NEVER)
        {
            nextgoal = NME_GOAL_FLINGER_ATTACK;
            trantype = GOAL_TRAN_PUSH;
            npc->GoalPick(this, nextgoal, trantype);
            return 1;
        }
    }

    return 0;
}

S32 zNMEGoalFlingAttack::Enter(F32 dt, void* updCtxt)
{
    idx_shot = 1;
    DoAutoAnim(NME_GSPOT_START, 0);
    return zNMEGoalCommon::Enter(dt, updCtxt);
}

S32 zNMEGoalFlingAttack::Process(en_trantype* trantype, F32 dt, void* ctxt)
{
    zNMEFlinger* npc = (zNMEFlinger*)psyche->clt_owner;
    F32 tym_animCurr;

    npc->MatFacePlayer(dt, PI, 1);
    if (idx_shot > 0 && npc->AnimTimeCurrent() > 1.15f)
    {
        if (ReadyAimFire(npc))
        {
            idx_shot--;
        }
    }

    tym_animCurr = npc->AnimTimeCurrent();
    if (tym_animCurr + dt >= npc->AnimDuration(NULL))
    {
        *trantype = GOAL_TRAN_POP;
        return 1;
    }

    return zNMEGoalCommon::Process(trantype, dt, ctxt);
}

S32 zNMEGoalFlingAttack::ReadyAimFire(zNMEFlinger* npc)
{
    const NMECfgFlinger* cfg = npc->cfg_flinger;
    xMat4x3* mat = (xMat4x3*)npc->model->Mat;
    xVec3 pos_launch;

    pos_launch = cfg->attack.vec_offsetLaunch;
    xMat4x3Toworld(&pos_launch, &mat[12], &pos_launch);
    xMat4x3Toworld(&pos_launch, mat, &pos_launch);

    npc->SndPlayFromTable(2); // eNMESound_Attack

    return npc->LaunchProjectile(cfg->attack.haztype, cfg->attack.spd_proj,
                                 cfg->attack.dst_minRange, &pos_launch, cfg->attack.tym_leadTarget,
                                 0.0f, -1.0f, -1.0f);
}

S32 zNMEGoalFlingBoing::Enter(F32 dt, void* updCtxt)
{
    zNMEFlinger* npc = (zNMEFlinger*)psyche->clt_owner;
    zNMEGoalFlingMove* goal; // not a PS2 DWARF local; the FindGoal result is used twice

    tmr_minBoing = 0.5f;
    DoAutoAnim(NME_GSPOT_START, 0);

    goal = (zNMEGoalFlingMove*)npc->psy_move->FindGoal(NME_GOAL_FLINGER_MOVE);
    goal->tmr_delay = -1.0f;
    goal->flg_ignoreDelay = 1;
    flg_toldMoverIgnore = 1;

    return zNMEGoalCommon::Enter(dt, updCtxt);
}

S32 zNMEGoalFlingBoing::Process(en_trantype* trantype, F32 dt, void* ctxt)
{
    zNMEFlinger* npc = (zNMEFlinger*)psyche->clt_owner;
    S32 nextgoal = 0;

    // The test is materialized as a 0/1 value first (as BfBB's zNPCGoalRobo timer tests).
    if ((tmr_minBoing < 0.0f) ? 1 : 0)
    {
        // Not a PS2 DWARF local, so the original shape is unknown; the result is used twice.
        zNMEGoalFlingMove* goal =
            (zNMEGoalFlingMove*)npc->psy_move->FindGoal(NME_GOAL_FLINGER_MOVE);
        if (flg_toldMoverIgnore)
        {
            goal->flg_ignoreDelay = 0;
            flg_toldMoverIgnore = 0;
        }

        if (!(goal->tmr_delay < 0.0f))
        {
            *trantype = GOAL_TRAN_SET;
            nextgoal = NME_GOAL_TRIGGER_BATTLE;
            npc->GoalPick(this, nextgoal, *trantype);
            return nextgoal;
        }
    }
    else
    {
        tmr_minBoing = MAX(-1.0f, tmr_minBoing - dt);
    }

    npc->MatFacePlayer(dt, PI, 1);
    return zNMEGoalCommon::Process(trantype, dt, ctxt);
}

S32 zNMEGoalFlingBoing::GoalHandleMail(NMEMsg* mail)
{
    S32 snarfed = 1;

    switch (mail->msgid)
    {
    case NME_MID_DAMAGE:
        if (!AutoAnimIsCurrent(NME_GSPOT_START))
        {
            snarfed = 0;
        }
        break;
    default:
        snarfed = 0;
        break;
    }

    return snarfed;
}

S32 zNMEGoalFlingFlee::Enter(F32 dt, void* updCtxt)
{
    zNMEFlinger* npc = (zNMEFlinger*)psyche->clt_owner;
    zNMEGoalFlingMove* goal; // not a PS2 DWARF local; the FindGoal result is used twice

    tmr_minFlee = 0.5f;
    DoAutoAnim(NME_GSPOT_START, 0);

    goal = (zNMEGoalFlingMove*)npc->psy_move->FindGoal(NME_GOAL_FLINGER_MOVE);
    goal->tmr_delay = -1.0f;
    goal->flg_ignoreDelay = 1;
    flg_toldMoverIgnore = 1;

    return zNMEGoalCommon::Enter(dt, updCtxt);
}

S32 zNMEGoalFlingFlee::Process(en_trantype* trantype, F32 dt, void* ctxt)
{
    zNMEFlinger* npc = (zNMEFlinger*)psyche->clt_owner;
    S32 nextgoal = 0;

    // The test is materialized as a 0/1 value first (as BfBB's zNPCGoalRobo timer tests).
    if ((tmr_minFlee < 0.0f) ? 1 : 0)
    {
        // Not a PS2 DWARF local, so the original shape is unknown; the result is used twice.
        zNMEGoalFlingMove* goal =
            (zNMEGoalFlingMove*)npc->psy_move->FindGoal(NME_GOAL_FLINGER_MOVE);
        if (flg_toldMoverIgnore)
        {
            goal->flg_ignoreDelay = 0;
            flg_toldMoverIgnore = 0;
        }

        if (!(goal->tmr_delay < 0.0f))
        {
            *trantype = GOAL_TRAN_SET;
            nextgoal = NME_GOAL_TRIGGER_BATTLE;
            npc->GoalPick(this, nextgoal, *trantype);
            return nextgoal;
        }
    }
    else
    {
        tmr_minFlee = MAX(-1.0f, tmr_minFlee - dt);
    }

    npc->MatFacePlayer(dt, PI, 1);
    return zNMEGoalCommon::Process(trantype, dt, ctxt);
}

S32 zNMEGoalFlingFlee::GoalHandleMail(NMEMsg* mail)
{
    S32 snarfed = 1;

    switch (mail->msgid)
    {
    case NME_MID_DAMAGE:
        if (!AutoAnimIsCurrent(NME_GSPOT_START))
        {
            snarfed = 0;
        }
        break;
    default:
        snarfed = 0;
        break;
    }

    return snarfed;
}

S32 zNMEGoalFlingMove::Enter(F32 dt, void* updCtxt)
{
    zNMEFlinger* npc = (zNMEFlinger*)psyche->clt_owner;

    Sentry_Enter(npc);
    npc->spd_throttle = 0.0f;
    return zNMEGoalCommon::Enter(dt, updCtxt);
}

S32 zNMEGoalFlingMove::Exit(F32 dt, void* updCtxt)
{
    tmr_delay = -1.0f;
    return 0;
}

S32 zNMEGoalFlingMove::Process(en_trantype* trantype, F32 dt, void* ctxt)
{
    zNMEFlinger* npc = (zNMEFlinger*)psyche->clt_owner;

    if (flg_sentry & 1)
    {
        npc->spd_throttle = 0.0f;
    }
    else
    {
        FiddleThrottle(npc, dt);
    }

    Sentry_Update(npc, dt);
    return zNMEGoalCommon::Process(trantype, dt, ctxt);
}

// No GC symbol (always inlined into Process). Throttles toward the speed of the main brain's goal.
inline void zNMEGoalFlingMove::FiddleThrottle(zNMEFlinger* npc, F32 dt)
{
    const NMECfgCommon* cfg_comm = npc->cfg_common;
    const NMECfgFlinger* cfg = npc->cfg_flinger;
    S32 gid = npc->psy_self->GIDOfActive();
    F32 spd;
    F32 acc;

    switch (gid)
    {
    case NME_GOAL_FLINGER_NORMAL:
    case NME_GOAL_FLINGER_AWARE:
        spd = cfg_comm->movement.spd_move;
        acc = cfg_comm->movement.acc_move;
        break;
    case NME_GOAL_FLINGER_BOING:
    case NME_GOAL_FLINGER_FLEE:
    case NME_GOAL_FLINGER_PANIC:
        spd = 1.5f * cfg->battle.spd_move;
        acc = 1.5f * cfg->battle.acc_move;
        break;
    case NME_GOAL_FLINGER_BATTLE:
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

void zNMEGoalFlingMove::TellMyNukeFam(en_sentell note, zNMECommon* npc)
{
}
