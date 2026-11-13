#include "zNMETypeCritter.h"

#include "zWadNME.h"
#include "zNMESupport.h"
#include "zFX.h"

extern _tagGameVidMode gVidMode;

U32 g_hash_critanim[5];
char* g_strz_critanim[5] = {
    "Unknwon", "Idle", "Move", "Attack", "Defeat",
};

namespace Critter
{
    namespace Ribbon
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
    } // namespace Ribbon
} // namespace Critter

// The helpers below have no GC symbol or PS2 DWARF entry (always inlined; the PS2 build inlines
// the same code), so their names are ours.

// Slows the throttle down / speeds it up by dspd, keeping it within [0, spd_max].
static inline void CritThrottleDown(zNMECritter* npc, F32 dspd, F32 spd_max)
{
    npc->spd_throttle -= dspd;
    npc->spd_throttle = CLAMP(npc->spd_throttle, 0.0f, spd_max);
}

static inline void CritThrottleUp(zNMECritter* npc, F32 dspd, F32 spd_max)
{
    npc->spd_throttle += dspd;
    npc->spd_throttle = CLAMP(npc->spd_throttle, 0.0f, spd_max);
}

// Moves the NPC's model to the point at t between a and b.
static inline void CritLerpPos(zNMECritter* npc, const xVec3* a, const xVec3* b, F32 t)
{
    F32 x = LERP(t, a->x, b->x);
    F32 y = LERP(t, a->y, b->y);
    F32 z = LERP(t, a->z, b->z);

    npc->model->Mat->pos.x = x;
    npc->model->Mat->pos.y = y;
    npc->model->Mat->pos.z = z;
}

// The world position of a bone of the NPC's model: a random one (skipping the first two) for
// idx_request < 1, else bone idx_request (the root when the model lacks it). The name is ours; the
// body is BfBB's zNPCJelly::JellyBoneWorldPos, which the Xbox build keeps out of line.
inline void zNMECritter::CritBonePos(xVec3* pos, S32 idx_request) const
{
    S32 idx;

    if (idx_request < 1)
    {
        idx = (S32)(model->BoneCount * xurand());
        if (idx < 2)
        {
            idx += 2;
        }
    }
    else
    {
        idx = (idx_request > model->BoneCount) ? 0 : idx_request;
    }

    xVec3 pos_place = *(xVec3*)BonePos(idx);
    xMat4x3Toworld(&pos_place, (xMat4x3*)model->Mat, &pos_place);
    *pos = pos_place;
}

// Arrival at the destination move point: tells it, cycles to the next one and pauses there when
// it has a long enough delay.
static inline void CritArrived(zNMEGoalCritPatrol* goal)
{
    zNMENavNet* navnet = ((zNMECommon*)goal->psyche->clt_owner)->navnet;

    if (navnet->nav_dest != NULL)
    {
        zEntEvent(goal->psyche->clt_owner, 0, navnet->nav_dest, eEventArrive, NULL, NULL, 0, FE_NO);
    }

    navnet->MvptCycle();

    F32 delay;
    F32 tmr_min = 10.0f * ((gVidMode == eGameVidModePAL) ? 0.02f : (1.0f / 60.0f));
    if (navnet->nav_curr != NULL)
    {
        delay = navnet->nav_curr->Delay();
    }
    else
    {
        delay = -1.0f;
    }
    if (delay > tmr_min)
    {
        goal->tmr_delay = delay;
        goal->flg_critpat |= 0x1;
        goal->DoAutoAnim(NME_GSPOT_PATROLPAUSE, 0);
    }
}

inline void zNMECritter_AnimTableAdd(xAnimTable* table)
{
    xAnimTableNewState(table, g_strz_critanim[1], 0x10, 1, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, g_strz_critanim[2], 0x10, 1, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, g_strz_critanim[3], 0, 1, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, g_strz_critanim[4], 0x10, 1, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);

    S32 ourAnims[5] = { 1, 2, 3, 4, 0 };
    NMEC_BuildStandardAnimTran(table, g_strz_critanim, ourAnims, 1, 0.2f);
}

void zMODCritter::ScenePrepare()
{
}

void zMODCritter::ScenePostInit()
{
    S32 hasJelly = zNMECommon_TypeInScene(NME_TYPE_CRITJELLY);
    S32 hasBucket = zNMECommon_TypeInScene(NME_TYPE_BUCKETJELLY);

    if (hasJelly || hasBucket)
    {
        Critter::Ribbon::rib_norm.FindTexture();
    }
}

// No GC symbol: inlined into zNMECentral_Startup (its unused copy is dead-stripped).
void ZNME_Critter_Startup()
{
    static zMODCritter mod_critter;

    zNMECentral_RegisterModule(&mod_critter, 'CRIT');
    for (S32 i = 0; i < 5; i++)
    {
        g_hash_critanim[i] = xStrHash(g_strz_critanim[i]);
    }
}

zMovePoint* zNMECritter::MvptFirstFollow()
{
    zMovePoint* nav;

    if (((zNMEAssetCritter*)nmeass)->aid_mvptFollow == 0)
    {
        return NULL;
    }

    nav = zMovePoint_From_xAssetID(((zNMEAssetCritter*)nmeass)->aid_mvptFollow);
    return nav;
}

void zNMECritter::Init(xEntAsset* entass)
{
    zNMECommon::Init(entass);
}

S32 zNMEGoalCritIdle::Process(en_trantype* trantype, F32 dt, void* ctxt)
{
    zNMECritter* npc = (zNMECritter*)psyche->clt_owner;
    zNMENavNet* navnet = npc->navnet;
    S32 nextgoal = 0;

    if (navnet != NULL && navnet->nav_dest != NULL)
    {
        nextgoal = NME_GOAL_CRIT_PATROL;
        *trantype = GOAL_TRAN_PUSH;
        npc->GoalPick(this, nextgoal, *trantype);
        return nextgoal;
    }

    return zNMEGoalCommon::Process(trantype, dt, ctxt);
}

S32 zNMEGoalCritPatrol::Enter(F32 dt, void* updCtxt)
{
    tmr_delay = -1.0f;
    flg_critpat = 0;
    return zNMEGoalCommon::Enter(dt, updCtxt);
}

S32 zNMEGoalCritPatrol::Process(en_trantype* trantype, F32 dt, void* ctxt)
{
    zNMECritter* npc = (zNMECritter*)psyche->clt_owner;
    zNMENavNet* navnet = npc->navnet;
    S32 nextgoal = 0;

    if (flg_critpat & 0x1)
    {
        tmr_delay = MAX(-1.0f, tmr_delay - dt);
        if (tmr_delay < 0.0f)
        {
            DoAutoAnim(NME_GSPOT_RESUME, 0);
            flg_critpat &= ~0x1;
        }
    }

    if (flg_critpat & 0x1)
    {
        CritThrottleDown(npc, dt * npc->cfg_common->movement.acc_move,
                         0.75f * npc->cfg_common->movement.spd_move);
    }
    else
    {
        CritThrottleUp(npc, dt * npc->cfg_common->movement.acc_move,
                       0.75f * npc->cfg_common->movement.spd_move);
    }

    if (!(flg_critpat & 0x1))
    {
        if (navnet->spl_mvptspline != NULL)
        {
            CritMoveSpline(dt);
        }
        else if (navnet->nav_dest != NULL)
        {
            CritMoveNormal(dt);
        }
        else
        {
            nextgoal = NME_GOAL_CRIT_IDLE;
            *trantype = GOAL_TRAN_SET;
            npc->GoalPick(this, nextgoal, *trantype);
            return nextgoal;
        }
    }

    return zNMEGoalCommon::Process(trantype, dt, ctxt);
}

void zNMEGoalCritPatrol::CritMoveNormal(F32 dt)
{
    zNMECritter* npc = (zNMECritter*)psyche->clt_owner;
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

        CritArrived(this);
    }
    else
    {
        dir_dest *= 1.0f / len;
        npc->MatQRotTowards(dt, &dir_dest, -1.0f);
        xVec3 delta = dir_dest * spd;
        npc->model->Mat->pos.x += delta.x;
        npc->model->Mat->pos.y += delta.y;
        npc->model->Mat->pos.z += delta.z;
    }
}

void zNMEGoalCritPatrol::CritMoveSpline(F32 dt)
{
    zNMECommon* npc = (zNMECommon*)psyche->clt_owner;
    zNMENavNet* navnet = npc->navnet;
    xSpline3* spl = navnet->spl_mvptspline;
    F32 newdist;
    xVec3 vec;
    xVec3 tgt;
    F32 u;
    xVec3 dir;
    F32 qdot;

    if (spl == NULL)
    {
        CritMoveNormal(dt);
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

        CritArrived(this);
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

    qdot = npc->MatQRotTowards(dt, &dir, -1.0f);
    if (qdot > 0.86f)
    {
        npc->model->Mat->pos.x += tgt.x - npc->model->Mat->pos.x;
        npc->model->Mat->pos.z += tgt.z - npc->model->Mat->pos.z;
        if (!(npc->flg_move & 0x2))
        {
            npc->model->Mat->pos.y += tgt.y - npc->model->Mat->pos.y;
        }
        navnet->dst_curspline = newdist;
    }
}

S32 zNMEGoalCritDying::Enter(F32 dt, void* updCtxt)
{
    zNMECritter* npc = (zNMECritter*)psyche->clt_owner;
    xVec3 dir_aim;

    if (flg_info & 0x10)
    {
        npc->XZVecToPos(&dir_aim, &pos_bumper, NULL);
    }
    else
    {
        npc->XZVecToPos(&dir_aim, xEntGetPos(xglobals->___player_ent_dont_use_directly), NULL);
    }

    flg_info = 0;

    xVec3Inv(&dir_aim, &dir_aim);
    dir_aim.y = 0.0f;
    // safe_normalize written out, so the player's frame is loaded only in the fallback branch, as
    // retail does.
    F32 len = dir_aim.length();
    if (len < 0.00001f)
    {
        dir_aim = xEntGetFrame(xglobals->___player_ent_dont_use_directly)->at;
    }
    else
    {
        dir_aim /= len;
    }

    CalcEndPoint(npc, &pos_grindin, &dir_aim);
    pos_bumpin = *xEntGetPos(npc);
    tmr_dying = 1.0f;

    NMEHelpRib::RibData* ribhelp = &Critter::Ribbon::rib_norm;
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

S32 zNMEGoalCritDying::Exit(F32 dt, void* updCtxt)
{
    zNMECritter* npc = (zNMECritter*)psyche->clt_owner;

    npc->SndPlayFromTable(4); // eNMESound_Death

    if (ribbon != NULL)
    {
        zFXRibbonPoolDelete(ribbon);
        ribbon = NULL;
    }

    return 0;
}

S32 zNMEGoalCritDying::Process(en_trantype* trantype, F32 dt, void* ctxt)
{
    S32 nextgoal = 0;
    zNMECritter* npc = (zNMECritter*)psyche->clt_owner;
    F32 rat_life;
    xVec3 pos;
    S32 doalpha;

    rat_life = (tmr_dying < 0.0f) ? 0.0f : tmr_dying;
    rat_life = 1.0f - CLAMP(rat_life, 0.0f, 1.0f);

    // Computed ahead of SMOOTH: retail evaluates it before SMOOTH's own constants.
    F32 tmp = 1.0f - rat_life;
    npc->AnimCurSingle()->CurrentSpeed = SMOOTH(tmp, 1.0f, 3.0f);

    CritLerpPos(npc, &pos_bumpin, &pos_grindin, rat_life);

    npc->CritBonePos(&pos, -1);
    zFX_SpawnBubbleTrail(&pos, 4);

    RibbonUpdate(npc, rat_life);

    doalpha = 1;
    if (npc->itemType == NME_TYPE_BUCKETJELLY)
    {
        doalpha = 0;
    }
    if (doalpha)
    {
        for (xModelInstance* minst = npc->model; minst != NULL; minst = minst->Next)
        {
            minst->Flags |= 0x4000;
            minst->Alpha = 1.0f - rat_life;
        }
    }

    if (xnegative(tmr_dying))
    {
        nextgoal = NME_GOAL_CRIT_DEAD;
        *trantype = GOAL_TRAN_SET;
        npc->GoalPick(this, nextgoal, *trantype);
        return nextgoal;
    }

    tmr_dying = MAX(-1.0f, tmr_dying - dt);
    return zNMEGoalCommon::Process(trantype, dt, ctxt);
}

// GC only (not in the PS2 DWARF): records where the hit came from in pos_bumper (Enter knocks the
// critter away from it when flg_info has 0x10).
S32 zNMEGoalCritDying::InputInfo(NMEDamageInfo* dmgmail)
{
    zNMECritter* npc = (zNMECritter*)psyche->clt_owner;

    if (dmgmail->dmg_from != NULL)
    {
        switch (dmgmail->dmg_from->baseType)
        {
        case eBaseTypeCamera:
            xVec3Copy(&pos_bumper, &globals.oldSkoolCamera.mat.pos);
            break;
        case eBaseTypePlayer:
        case eBaseTypePickup:
        case eBaseTypePlatform:
        case eBaseTypeStatic:
        case eBaseTypeDynamic:
        case eBaseTypeBubble:
        case eBaseTypePendulum:
        case eBaseTypeHangable:
        case eBaseTypeButton:
        case eBaseTypeDestructObj:
        case eBaseTypeNPC:
        case eBaseTypeBoulder:
            xVec3Copy(&pos_bumper, xEntGetPos((xEnt*)dmgmail->dmg_from));
            break;
        case eBaseTypeCruiseBubble:
            // Retail has a case for the cruise bubble (it sizes the jump table) that does nothing.
            break;
        }
        flg_info = 0x10;
    }
    else if (xVec3Length2(&dmgmail->vec_dmghit) > 0.001f)
    {
        xVec3Normalize(&pos_bumper, &dmgmail->vec_dmghit);
        xVec3Inv(&pos_bumper, &pos_bumper);
        xVec3AddTo(&pos_bumper, xEntGetPos(npc));
        flg_info = 0x10;
    }
    else
    {
        xVec3Copy(&pos_bumper, xEntGetPos(xglobals->___player_ent_dont_use_directly));
        flg_info = 0x10;
    }

    return flg_info;
}

void zNMEGoalCritDying::RibbonUpdate(zNMECritter* npc, F32 dt)
{
    NMEHelpRib::RibData* ribhelp;
    xVec3 a;
    xVec3 b;

    if (ribbon == NULL)
    {
        return;
    }

    cnt_skipRibbonJoint--;
    if (cnt_skipRibbonJoint > 0)
    {
        return;
    }

    ribhelp = &Critter::Ribbon::rib_norm;
    cnt_skipRibbonJoint = ribhelp->updinfo.frq_ribJoint;

    if (ribhelp->flags.dev_enable)
    {
        ribhelp->ConfigRibbon(ribbon);
    }

    npc->CritBonePos(&a, 2);
    npc->CritBonePos(&b, 3);
    xVec3 dir_emit = b - a;
    F32 dst = xVec3Normalize(&dir_emit, &dir_emit);
    ribbon->insert(a, dir_emit, dst * ribhelp->updinfo.scl_master, ribhelp->updinfo.alf_master, 0);
}

F32 zNMEGoalCritDying::CalcEndPoint(zNMECritter* npc, xVec3* pos_end, const xVec3* dir_aim)
{
    F32 dst_toEndPnt;
    xVec3 vec = *dir_aim * 4.0f;
    xVec3 pos = *npc->Pos() + vec;
    xCollis colrec;
    S32 rc;

    memset(&colrec, 0, sizeof(xCollis));
    colrec.flags = 0x1f00;
    rc = npc->HaveLOSToPos(&pos, 4.0f, xglobals->sceneCur, NULL, &colrec);
    if (rc)
    {
        dst_toEndPnt = 4.0f;
        *pos_end = pos;
    }
    else
    {
        dst_toEndPnt = colrec.dist;
        *pos_end = *xEntGetPos(npc) + *dir_aim * dst_toEndPnt;
    }

    return dst_toEndPnt;
}

S32 zNMEGoalCritDying::GoalHandleMail(NMEMsg* mail)
{
    return 1;
}

S32 zNMEGoalCritDead::GoalHandleMail(NMEMsg* mail)
{
    return 1;
}
