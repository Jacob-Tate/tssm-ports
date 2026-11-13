#include "zNMETypeTurBarrel.h"
#include "xRand.h"
#include "xutil.h"

namespace Barrel
{
    namespace Config
    {
        static const NMECfgTBarrel cfg_tb_v1 = {
            { 1 },
            { 40.0f, 42.0f, 37.0f, 38.0f },
            { DEG2RAD(90), DEG2RAD(180) },
            { HAZ_TYP_TURBALL_V1, 5.0f, 100.0f },
            { 1.75f, 5 },
        };
        static const NMECfgTBarrel cfg_tb_v2 = {
            { 1 },
            { 44.0f, 46.0f, 42.0f, 44.0f },
            { DEG2RAD(180), DEG2RAD(360) },
            { HAZ_TYP_TURBALL_V2, 10.0f, 100.0f },
            { 1.0f, 7 },
        };
        static const NMECfgTBarrel cfg_tb_v3 = {
            { 1 },
            { 40.0f, 45.0f, 40.0f, 43.0f },
            { DEG2RAD(360), DEG2RAD(360) },
            { HAZ_TYP_TURBALL_V3, 15.0f, 100.0f },
            { 0.00001f, 11 },
        };
        const NMECfgTBarrel* cfg_tb[4] = { &cfg_tb_v1, &cfg_tb_v1, &cfg_tb_v2, &cfg_tb_v3 };
    } // namespace Config

    NMEHelpBound::BndData bnd_info = {
        k_XBOUNDTYPE_OBB,
        { 0, 0, { 0, 0 } },
        { 1.0f, { 0.0f, 1.0f, 0.0f } },
        { { 1.0f, 1.25f, 2.2f }, { 0.0f, 0.5f, 0.3f } },
    };

    namespace Shoot
    {
        // Muzzle offset in the space of the gun bone (model matrix 16).
        static const xVec3 vec_offsetGun = { 0.0f, 0.05f, 0.1f };
    } // namespace Shoot

    namespace FX
    {
        namespace TDust
        {
            ExhaustCurve curve_data[4] = {
                { 0.0f, { 0x7F, 0x7F, 0x7F, 0x00 }, 0.25f },
                { 0.6f, { 0x7F, 0x7F, 0x7F, 0xC8 }, 1.0f },
                { 0.8f, { 0x28, 0x28, 0x28, 0xA0 }, 1.0f },
                { 1.0f, { 0x00, 0x00, 0x00, 0x00 }, 0.75f },
            };

            ExhaustEmitter tdust = {
                NPAR_TYP_CLOUDS,
                { 0, 0, { 0, 0 } },
                { 10.0f, 0.0f, 3.0f },
                { 1.0f, 1.0f },
                { 0.25f, { 0.1f, 0.9f }, 0x20, { 0, 0, 0 } },
                { 0.3f, 0.3f },
                { 10, 0, 0.65f, -1.05f },
                { 1.2f, 0.0f, 1.0f, 1.0f, 0.0f, 0.0f, { 0.0f, 0.0f, 0.0f } },
                { curve_data, 4, 1.0f, 0.6f },
            };

            // No GC symbol and no PS2 DWARF entry (always inlined); the name is ours. Kicks up a
            // ring of dust around the barrel while it turns, when the player is close enough.
            static inline void EmitTurnDust(zNMETurBarrel* npc)
            {
                if (npc->XYZDstSqToPlayer(NULL) > SQ(20.0f))
                {
                    return;
                }

                xVec3 pos_emit = *xEntGetPos(npc);
                xVec3 dir_emit = xEntGetFrame(npc)->up;
                tdust.EmitRing(&pos_emit, &dir_emit, -100000.0f, 1.0f, -1, NULL);
            }
        } // namespace TDust
    } // namespace FX
} // namespace Barrel

void zNMETurBarrel::CreateConfig(const char* name)
{
    en_nmevar vidx;

    zNMECommon::CreateConfig(name);
    vidx = NMEVariantIdx((en_npctyp)itemType);
    cfg_barrel = Barrel::Config::cfg_tb[vidx];
}

void zNMETurBarrel::SelfSetup()
{
    xBehaveMgr* bmgr;
    xPsyche* psy;

    bmgr = xBehaveMgr_GetSelf();
    psy_self = bmgr->Subscribe(this, 0);

    psy = psy_self;
    psy->BrainBegin();

    psy->AddGoal(NME_GOAL_TURR_BIDLE, NULL);
    psy->AddGoal(NME_GOAL_TURR_BTURN, NULL);
    psy->AddGoal(NME_GOAL_TURR_BSHOOT, NULL);
    psy->AddGoal(NME_GOAL_TURR_BHURT, NULL);
    psy->AddGoal(NME_GOAL_TURR_RELOAD, NULL);
    psy->AddGoal(NME_GOAL_TURR_DEAD, NULL);

    psy->BrainEnd();
    psy->SetSafety(NME_GOAL_TURR_BIDLE);
}

// anid is not a PS2 DWARF local (that build's AnimPick is an older version); retail reads the
// state ID before loading g_hash_turranim.
U32 zNMETurBarrel::AnimPick(S32 gid, en_npcgspot gspot, xGoal* goal)
{
    en_turranim idx;

    switch (gid)
    {
    case NME_GOAL_TURR_BIDLE:
        if (gspot == NME_GSPOT_START)
        {
            U32 anid = AnimCurStateID();
            if (anid != g_hash_turranim[TURR_ANIM_INACTIVE])
            {
                idx = TURR_ANIM_POPDOWN;
            }
            else
            {
                idx = TURR_ANIM_INACTIVE;
            }
        }
        else if (gspot == NME_GSPOT_STARTALT)
        {
            U32 anid = AnimCurStateID();
            if (anid == g_hash_turranim[TURR_ANIM_INACTIVE])
            {
                idx = TURR_ANIM_POPUP;
            }
            else
            {
                idx = TURR_ANIM_ACTIVE;
            }
        }
        else
        {
            idx = TURR_ANIM_ACTIVE;
        }
        break;
    case NME_GOAL_TURR_BHURT:
        idx = TURR_ANIM_DAMAGE;
        break;
    case NME_GOAL_TURR_RELOAD:
        idx = TURR_ANIM_RELOAD;
        break;
    case NME_GOAL_TURR_DEAD:
        idx = TURR_ANIM_INACTIVE;
        break;
    case NME_GOAL_TURR_BTURN:
        idx = TURR_ANIM_ACTIVE;
        break;
    case NME_GOAL_TURR_BSHOOT:
        idx = TURR_ANIM_SHOOT_ONCE;
        break;
    default:
        idx = TURR_ANIM_INACTIVE;
        break;
    }

    return g_hash_turranim[idx];
}

xAnimTable* zNMETurBarrel::CreateAnimTable(xAnimTable* table)
{
    if (table == NULL)
    {
        table = xAnimTableNew("zNMETurBarrel", 0);
    }

    xAnimTableNewState(table, g_strz_turranim[TURR_ANIM_INACTIVE], 0x10, 1, 1.0f, NULL, NULL, 0.0f,
                       NULL, NULL, xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, g_strz_turranim[TURR_ANIM_ACTIVE], 0x10, 1, 1.0f, NULL, NULL, 0.0f,
                       NULL, NULL, xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, g_strz_turranim[TURR_ANIM_POPUP], 0x20, 1, 1.0f, NULL, NULL, 0.0f,
                       NULL, NULL, xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, g_strz_turranim[TURR_ANIM_POPDOWN], 0x20, 1, 1.0f, NULL, NULL, 0.0f,
                       NULL, NULL, xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, g_strz_turranim[TURR_ANIM_SHOOT_ONCE], 0, 1, 1.0f, NULL, NULL, 0.0f,
                       NULL, NULL, xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, g_strz_turranim[TURR_ANIM_DAMAGE], 0x20, 1, 1.0f, NULL, NULL, 0.0f,
                       NULL, NULL, xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, g_strz_turranim[TURR_ANIM_RELOAD], 0, 1, 1.0f, NULL, NULL, 0.0f, NULL,
                       NULL, xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, g_strz_turranim[TURR_ANIM_TAUNT], 0x10, 1, 1.0f, NULL, NULL, 0.0f,
                       NULL, NULL, xAnimDefaultBeforeEnter, NULL, NULL);

    S32 ourAnims[9] = { TURR_ANIM_INACTIVE, TURR_ANIM_ACTIVE,     TURR_ANIM_POPUP,
                        TURR_ANIM_POPDOWN,  TURR_ANIM_SHOOT_ONCE, TURR_ANIM_DAMAGE,
                        TURR_ANIM_RELOAD,   TURR_ANIM_TAUNT,      TURR_ANIM_UNKNOWN };
    NMEC_BuildStandardAnimTran(table, g_strz_turranim, ourAnims, 2, 0.2f);

    xAnimTableNewTransition(table, g_strz_turranim[TURR_ANIM_POPUP],
                            g_strz_turranim[TURR_ANIM_ACTIVE], NULL, NULL, 0x10, 0, 0.0f, 0.0f, 0,
                            0, 0.2f, NULL);
    xAnimTableNewTransition(table, g_strz_turranim[TURR_ANIM_POPDOWN],
                            g_strz_turranim[TURR_ANIM_INACTIVE], NULL, NULL, 0x10, 0, 0.0f, 0.0f, 0,
                            0, 0.2f, NULL);
    xAnimTableNewTransition(table, g_strz_turranim[TURR_ANIM_DAMAGE],
                            g_strz_turranim[TURR_ANIM_INACTIVE], NULL, NULL, 0x10, 0, 0.0f, 0.0f, 0,
                            0, 0.2f, NULL);

    return table;
}

S32 zNMETurBarrel::DfltVulnFlags()
{
    return 0xCFFFFFF7;
}

void zNMETurBarrel::Init(xEntAsset* asset)
{
    zNMETurret::Init(asset);
    RuntimeDefaults();
    flags &= (U8)~0x40;
    // k_XBASE_RECEIVES_SHADOWS (a U16 flag in Ratatouille's xBase.h)
    baseFlags |= (U16)0x10;
}

void zNMETurBarrel::Reset()
{
    zNMEGoalTurrDead* goal;

    zNMECommon::Reset();

    psy_self->GoalSet(NME_GOAL_TURR_BIDLE, 0);
    pts_health = cfg_barrel->health.pts_healthMax;
    CalcEndPoints((xMat3x3*)model->Mat);
    flg_vuln = DfltVulnFlags();

    goal = (zNMEGoalTurrDead*)psy_self->FindGoal(NME_GOAL_TURR_DEAD);
    goal->stylinDyin = DIE_LOUDLY;
}

void zNMETurBarrel::BUpdate(xVec3* pos)
{
    Barrel::bnd_info.UpdateBound(&bound, this, pos);
}

void zNMETurBarrel::Process(F32 dt)
{
    psy_self->Timestep(dt, NULL);
    zNMETurret::Process(dt);
}

S32 zNMETurBarrel::TypeHandleMail(NMEMsg* mail)
{
    S32 handled;

    switch (mail->msgid)
    {
    case NME_MID_DAMAGE:
        handled = TurBarMailDamage(&mail->dmgdata);
        break;
    case NME_MID_SYSEVENT:
        handled = DfltSysEventMail(&mail->sysevent);
        break;
    default:
        handled = 0;
        break;
    }

    return handled;
}

// No GC symbol (inlined into TypeHandleMail).
inline S32 zNMETurBarrel::TurBarMailDamage(NMEDamageInfo* dmgmail)
{
    S32 handled = 1;

    switch (dmgmail->dmg_type)
    {
    case DMGTYP_KILLEVENT:
    {
        xPsyche* psy;
        xGoal* goal;

        pts_health = 0;
        psy = psy_self;
        goal = psy->FindGoal(NME_GOAL_TURR_DEAD);
        ((zNMEGoalTurrDead*)goal)->stylinDyin = DIE_MUFFLED;
        psy->GoalSet(NME_GOAL_TURR_DEAD, 0);
        break;
    }
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
    {
        xPsyche* psy = psy_self;

        if (psy == NULL)
        {
            break;
        }

        pts_health--;
        if (pts_health < 1)
        {
            psy->GoalSet(NME_GOAL_TURR_DEAD, 0);
        }
        else
        {
            psy->GoalSet(NME_GOAL_TURR_BHURT, 0);
        }
        break;
    }
    default:
        handled = 0;
        break;
    }

    return handled;
}

// No GC symbol (always inlined).
inline void zNMETurBarrel::CalcEndPoints(const xMat3x3* mat_ref)
{
    F32 ang_half;
    xMat3x3 mat_rot;
    xMat3x3 mat_tmp;
    xVec3 dir_around;

    if ((PI / 180.0f) * ((zNMEAssetTurret*)nmeass)->unk_60 < (PI / 180.0f))
    {
        xQuatFromMat(&quat_left, mat_ref);
        xQuatFromMat(&quat_right, mat_ref);
        xQuatNormalize(&quat_left, &quat_left);
        xQuatNormalize(&quat_right, &quat_right);
        return;
    }

    ang_half = (PI / 180.0f) * (0.5f * ((zNMEAssetTurret*)nmeass)->unk_60);

    dir_around = *(xVec3*)&model->Mat->up;
    if (asset->scale.x != 1.0f)
    {
        dir_around.normalize();
    }

    xMat3x3RotC(&mat_rot, dir_around.x, dir_around.y, dir_around.z, ang_half);
    xMat3x3Mul(&mat_tmp, &mat_rot, mat_ref);
    xQuatFromMat(&quat_left, &mat_tmp);

    xMat3x3RotC(&mat_rot, dir_around.x, dir_around.y, dir_around.z, -ang_half);
    xMat3x3Mul(&mat_tmp, &mat_rot, mat_ref);
    xQuatFromMat(&quat_right, &mat_tmp);

    xQuatNormalize(&quat_left, &quat_left);
    xQuatNormalize(&quat_right, &quat_right);
}

en_vis zNMETurBarrel::VisionTarget(xEnt* ent, en_defcon defcon, S32 flags) const
{
    const NMECfgTBarrel* cfg;

    if (NME::GlobalData::flg_wonder)
    {
        return VIS_NOBODY_AROUND;
    }

    if (runtimeData.overrideDetect == ALLOW_NEVER)
    {
        return VIS_NOBODY_AROUND;
    }

    // Not a PS2 DWARF local; the distance is computed once for every range test below.
    F32 dsq = XZDstSqToPos(xEntGetPos(ent), NULL, NULL);
    cfg = cfg_barrel;

    if (runtimeData.overrideDetect == ALLOW_NORMAL && runtimeData.overrideAttack == ALLOW_NORMAL &&
        dsq > SQ(cfg->ranges.rad_plyrGrey))
    {
        return VIS_NOBODY_AROUND;
    }

    if (runtimeData.overrideAttack != ALLOW_NEVER)
    {
        if (runtimeData.overrideAttack == ALLOW_ALWAYS || dsq < SQ(cfg->ranges.rad_shootBeg))
        {
            return VIS_PREPARE_TO_DIE;
        }

        if (dsq < SQ(cfg->ranges.rad_shootEnd))
        {
            return VIS_GET_BACK_HERE;
        }
    }

    if (runtimeData.overrideDetect == ALLOW_ALWAYS)
    {
        return VIS_I_SEEE_YOUUU;
    }

    if (dsq < SQ(cfg->ranges.rad_plyrSeen))
    {
        return VIS_I_SEEE_YOUUU;
    }

    if (runtimeData.overrideAttack == ALLOW_NEVER)
    {
        return VIS_I_SEEE_YOUUU;
    }

    if (dsq < SQ(cfg->ranges.rad_plyrGrey))
    {
        return VIS_GREY_AND_FOGGY;
    }

    return VIS_NOBODY_AROUND;
}

S32 zNMEGoalTurrBIdle::Process(en_trantype* trantype, F32 dt, void* ctxt)
{
    zNMETurBarrel* npc;
    en_vis vis;
    U32 anid_active;
    U32 anid_inactive;

    if (!xUtil_yesno(0.79f))
    {
        npc = (zNMETurBarrel*)psyche->clt_owner;
        vis = npc->VisionTarget(xglobals->___player_ent_dont_use_directly, NME_DEFCON_NORMAL, 0);
        if (vis <= VIS_PREPARE_TO_DIE)
        {
            *trantype = GOAL_TRAN_SET;
            return NME_GOAL_TURR_BTURN;
        }

        // Not a PS2 DWARF local; the current state is read once, before the vis tests.
        U32 anid_curr = npc->AnimCurStateID();

        if (vis <= VIS_I_SEEE_YOUUU)
        {
            anid_active = npc->AnimPick(GetID(), NME_GSPOT_STARTALT, this);
            if (anid_curr != anid_active)
            {
                DoExplicitAnim(anid_active, 0);
            }
        }

        if (vis > VIS_GREY_AND_FOGGY)
        {
            anid_inactive = npc->AnimPick(GetID(), NME_GSPOT_START, this);
            if (anid_curr != anid_inactive)
            {
                DoExplicitAnim(anid_inactive, 0);
            }
        }
    }

    return zNMEGoalCommon::Process(trantype, dt, ctxt);
}

S32 zNMEGoalTurrBTurn::Enter(F32 dt, void* updCtxt)
{
    zNMETurBarrel* npc = (zNMETurBarrel*)psyche->clt_owner;

    turnState = 0;
    if (xrand_RandomInt32() & 0x800000)
    {
        idx_shot = 0;
        flg_goLeft = 0;
    }
    else
    {
        idx_shot = npc->cfg_barrel->attack.num_salvo - 1;
        flg_goLeft = 1;
    }

    tmr_reloading = -1.0f;
    return zNMEGoalCommon::Enter(dt, updCtxt);
}

S32 zNMEGoalTurrBTurn::Process(en_trantype* trantype, F32 dt, void* ctxt)
{
    zNMETurBarrel* npc = (zNMETurBarrel*)psyche->clt_owner;
    en_vis vis;
    S32 nextgoal;
    S32 doTran;

    vis = npc->VisionTarget(xglobals->___player_ent_dont_use_directly, NME_DEFCON_NORMAL, 0);
    if (vis > VIS_GET_BACK_HERE)
    {
        *trantype = GOAL_TRAN_SET;
        return NME_GOAL_TURR_BIDLE;
    }

    nextgoal = 0;
    doTran = SubStateUpdate(npc, dt, nextgoal, *trantype);
    if (doTran)
    {
        return nextgoal;
    }

    return zNMEGoalCommon::Process(trantype, dt, ctxt);
}

S32 zNMEGoalTurrBTurn::SubStateUpdate(zNMETurBarrel* npc, F32 dt, S32& nextgoal,
                                      en_trantype& trantype)
{
    en_rotinf rotinf;
    en_nmevar nvar;

    if (turnState == 0)
    {
        if (!GoToStart(npc, dt))
        {
            return 0;
        }
        turnState = 1;
    }

    if (flg_doReload)
    {
        flg_doReload = 0;
        nextgoal = NME_GOAL_TURR_RELOAD;
        trantype = GOAL_TRAN_PUSH;
        return 1;
    }

    if (!((tmr_reloading < 0.0f) ? 1 : 0))
    {
        tmr_reloading = MAX(-1.0f, tmr_reloading - dt);
        return 0;
    }

    rotinf = RotateUpdate(dt);
    if (rotinf == ROTINF_ENDPOINT)
    {
        tmr_reloading = npc->cfg_barrel->attack.tym_reload;

        nvar = NMEVariantIdx((en_npctyp)npc->itemType);
        if (nvar == NME_VAR_3)
        {
            flg_doReload = 1;
        }
        else if (nvar == NME_VAR_2)
        {
            if (!flg_goLeft)
            {
                flg_doReload = 1;
            }
        }
        else if (nvar == NME_VAR_1)
        {
            flg_doReload = 1;
        }
        else
        {
            flg_doReload = 1;
        }

        if (!flg_doReload)
        {
            tmr_reloading = -1.0f;
        }

        if (((zNMEAssetTurret*)npc->nmeass)->unk_68 & 0x1)
        {
            RetargetPlayer(npc);
        }

        nextgoal = NME_GOAL_TURR_BSHOOT;
        trantype = GOAL_TRAN_PUSH;
        return 1;
    }
    else if (rotinf == ROTINF_ARRIVED)
    {
        nextgoal = NME_GOAL_TURR_BSHOOT;
        trantype = GOAL_TRAN_PUSH;
        return 1;
    }

    return 0;
}

void zNMEGoalTurrBTurn::RetargetPlayer(zNMETurBarrel* npc)
{
    xVec3 dir_plyr;
    F32 dst_plyr;
    xMat3x3 mat_ref;

    npc->XZVecToPos(&dir_plyr, xEntGetPos(xglobals->___player_ent_dont_use_directly), NULL);
    dst_plyr = dir_plyr.length();
    if (dst_plyr < 0.25f)
    {
        return;
    }

    dir_plyr /= dst_plyr;
    mat_ref.up = g_Y3;
    mat_ref.at = dir_plyr;
    xVec3Cross(&mat_ref.right, &g_Y3, &dir_plyr);

    npc->CalcEndPoints(&mat_ref);
    npc->flg_reacquire = 1;
}

en_rotinf zNMEGoalTurrBTurn::RotateUpdate(F32 dt)
{
    zNMETurBarrel* npc = (zNMETurBarrel*)psyche->clt_owner;
    xQuat quat_curr;
    xQuat quat_want;
    const NMECfgTBarrel* cfg;
    F32 spd_turn;
    S32 arrived;
    S32 atEndpoint;

    Barrel::FX::TDust::EmitTurnDust(npc);

    xQuatFromMat(&quat_curr, (xMat3x3*)npc->model->Mat);
    xQuatSlerp(&quat_want, &npc->quat_left, &npc->quat_right,
               idx_shot * (1.0f / (npc->cfg_barrel->attack.num_salvo - 1)));

    cfg = npc->cfg_barrel;
    spd_turn = cfg->move.spd_rotate;
    if (npc->flg_reacquire)
    {
        spd_turn = cfg->move.spd_retarget;
    }

    arrived = QRotTowards((xMat3x3*)npc->model->Mat, &quat_curr, &quat_want, dt, spd_turn);

    atEndpoint = 0;
    if (arrived)
    {
        npc->flg_reacquire = 0;
        atEndpoint = NextShot(npc);
    }

    if (atEndpoint)
    {
        return ROTINF_ENDPOINT;
    }

    return (en_rotinf)(arrived != 0);
}

// No GC symbol and no PS2 DWARF entry (always inlined); the name is ours. Steps to the next shot
// of the sweep and turns around at either end; returns 1 when an end point was reached.
inline S32 zNMEGoalTurrBTurn::NextShot(zNMETurBarrel* npc)
{
    S32 atEndpoint = 0;

    if (flg_goLeft)
    {
        idx_shot--;
        if (idx_shot < 0)
        {
            atEndpoint = 1;
            flg_goLeft = !flg_goLeft;
            idx_shot = 0;
        }
    }
    else
    {
        idx_shot++;

        const NMECfgTBarrel* cfg = npc->cfg_barrel;
        if (idx_shot >= cfg->attack.num_salvo)
        {
            atEndpoint = 1;
            flg_goLeft = !flg_goLeft;
            idx_shot = cfg->attack.num_salvo - 1;
        }
    }

    return atEndpoint;
}

S32 zNMEGoalTurrBTurn::GoToStart(zNMETurBarrel* npc, F32 dt)
{
    xQuat quat_curr;
    xQuat quat_want;
    F32 spd_turn;
    S32 arrived;

    Barrel::FX::TDust::EmitTurnDust(npc);

    if (flg_goLeft)
    {
        xQuatCopy(&quat_want, &npc->quat_right);
    }
    else
    {
        xQuatCopy(&quat_want, &npc->quat_left);
    }

    xQuatFromMat(&quat_curr, (xMat3x3*)npc->model->Mat);
    spd_turn = npc->cfg_barrel->move.spd_retarget;
    arrived = QRotTowards((xMat3x3*)npc->model->Mat, &quat_curr, &quat_want, dt, spd_turn);
    if (arrived)
    {
        npc->flg_reacquire = 0;
    }

    return arrived;
}

// No GC symbol (always inlined).
inline S32 zNMEGoalTurrBTurn::QRotTowards(xMat3x3* mat, const xQuat* qfrom, const xQuat* qdest,
                                          F32 dt, F32 spd)
{
    xQuat quat;
    F32 qdot;
    F32 ang_rotate;
    F32 ang_perstep;

    xQuatCopy(&quat, qdest);
    qdot = xQuatDot(&quat, qfrom);
    if (qdot < 0.0f)
    {
        xQuatFlip(&quat, &quat);
        qdot = -qdot;
    }

    if (qdot > 1.0f)
    {
        qdot = 1.0f;
    }

    ang_rotate = 2.0f * xacos(qdot);
    ang_perstep = spd * dt;
    if (ang_rotate <= ang_perstep)
    {
        xQuatToMat(&quat, mat);
        return 1;
    }

    xQuatSlerp(&quat, qfrom, &quat, ang_perstep / ang_rotate);
    xQuatNormalize(&quat, &quat);
    xQuatToMat(&quat, mat);
    return 0;
}

S32 zNMEGoalTurrBShoot::Enter(F32 dt, void* updCtxt)
{
    idx_shot = 0;
    return zNMEGoalPushAnim::Enter(dt, updCtxt);
}

S32 zNMEGoalTurrBShoot::Process(en_trantype* trantype, F32 dt, void* ctxt)
{
    zNMETurBarrel* npc = (zNMETurBarrel*)psyche->clt_owner;
    S32 rc;
    static const F32 tym_shoot[1] = { 0.05f };
    // Not a PS2 DWARF local; the anim time is read before the idx_shot check.
    F32 tym_curr = npc->AnimTimeCurrent();

    if (idx_shot < (S32)(sizeof(tym_shoot) / sizeof(tym_shoot[0])) &&
        tym_shoot[idx_shot] >= tym_curr)
    {
        rc = BangBang(npc);
        if (rc)
        {
            idx_shot++;
        }
    }

    return zNMEGoalPushAnim::Process(trantype, dt, ctxt);
}

// pos_tgt is declared at its initialization, so it is built in place. Both transforms are written
// out as xMat3x3RMulVec + xVec3AddTo, as the PS2 line table has them on separate lines.
S32 zNMEGoalTurrBShoot::BangBang(zNMETurBarrel* npc)
{
    zNMEHazard* haz;
    const NMECfgTBarrel* cfg;
    S32 rc;
    xMat4x3* mat_gun;
    xVec3 pos_src;

    haz = HAZ_Acquire();
    if (haz == NULL)
    {
        return 0;
    }

    cfg = npc->cfg_barrel;
    rc = haz->ConfigHelper(cfg->lobparm.haztype);
    if (!rc)
    {
        haz->Discard();
        return 0;
    }

    haz->npc_owner = npc;

    mat_gun = (xMat4x3*)&npc->model->Mat[16];
    pos_src = Barrel::Shoot::vec_offsetGun;
    xMat3x3RMulVec(&pos_src, mat_gun, &pos_src);
    xVec3AddTo(&pos_src, &mat_gun->pos);
    xMat3x3RMulVec(&pos_src, (xMat4x3*)npc->model->Mat, &pos_src);
    xVec3AddTo(&pos_src, &((xMat4x3*)npc->model->Mat)->pos);

    xVec3 pos_tgt = pos_src + ((xMat4x3*)npc->model->Mat)->at * cfg->lobparm.rad_lob;
    xVec3Copy(&haz->custdata.lobdata.pos_tgt, &pos_tgt);

    haz->Start(&pos_src, MAX(0.25f, cfg->lobparm.rad_lob / cfg->lobparm.spd_proj));
    return 1;
}

S32 zNMEGoalTurrBHurt::Enter(F32 dt, void* updCtxt)
{
    tmr_hurted = 5.0f;
    return zNMEGoalCommon::Enter(dt, updCtxt);
}

// tym_animCurr and tym_animDur are not PS2 DWARF locals (names from zNMETongueSpin::FXUpdate). The
// second 'if (amDone)' test is nested in the first, as retail branches straight to the MAX block.
S32 zNMEGoalTurrBHurt::Process(en_trantype* trantype, F32 dt, void* ctxt)
{
    zNMETurBarrel* npc = (zNMETurBarrel*)psyche->clt_owner;
    S32 amDone = (tmr_hurted < 0.0f) ? 1 : 0;

    if (amDone)
    {
        F32 tym_animCurr = npc->AnimTimeCurrent();
        F32 tym_animDur = npc->AnimDuration(NULL);
        amDone = 1;
        if (tym_animCurr + dt < tym_animDur)
        {
            amDone = 0;
        }

        if (amDone)
        {
            *trantype = GOAL_TRAN_SET;
            return NME_GOAL_TURR_BIDLE;
        }
    }

    tmr_hurted = MAX(-1.0f, tmr_hurted - dt);
    return zNMEGoalCommon::Process(trantype, dt, ctxt);
}

S32 zNMEGoalTurrBHurt::GoalHandleMail(NMEMsg* mail)
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
