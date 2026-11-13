#include "zWadNME.h"
#include "xDebug.h"

S32 zNMEGoalPushAnim::Enter(F32 dt, void* updCtxt)
{
    flg_pushanim &= 0x10006;
    return zNMEGoalCommon::Enter(dt, updCtxt);
}

S32 zNMEGoalPushAnim::Exit(F32 dt, void* updCtxt)
{
    flg_pushanim = 0;
    return 0;
}

// zNMEGoalPushAnim's members are defined ahead of their address-order slot (the end of this
// file) so that the derived goals can inline them, as the target does.
S32 zNMEGoalPushAnim::Resume(F32 dt, void* updCtxt)
{
    flg_pushanim |= 0x1;
    return zNMEGoalCommon::Resume(dt, updCtxt);
}

// 7 compiled-out placeholders (conditions unknown) keep GC/2.6 from auto-inlining this (>= 30
// statements), as retail calls it from zNMEGoalTurrBShoot, zNMEGoalSlamAttack, zNMEGoalJellyAttack
// and zNMEGoalBuckBirthing::Process.
// tym_animCurr and tym_dur are not PS2 DWARF locals: retail reads the anim time and the duration
// before the flag tests and adds dt afterwards into a new value.
S32 zNMEGoalPushAnim::Process(en_trantype* trantype, F32 dt, void* ctxt)
{
    zNMECommon* npc = (zNMECommon*)psyche->clt_owner;
    F32 tym_animPlusTime;

    xASSERT_PLACEHOLDER();
    xASSERT_PLACEHOLDER();

    F32 tym_animCurr = npc->AnimTimeCurrent(); // not in the DWARF (see above)
    F32 tym_dur = npc->AnimDuration(NULL); // not in the DWARF (see above)
    tym_animPlusTime = tym_animCurr + dt;

    xASSERT_PLACEHOLDER();
    xASSERT_PLACEHOLDER();

    if (flg_pushanim & 0x1)
    {
        *trantype = GOAL_TRAN_POP;
        return 1;
    }

    if (flg_pushanim & 0x6)
    {
        xASSERT_PLACEHOLDER();

        if (anid_played != npc->AnimCurStateID())
        {
            if (flg_pushanim & 0x2)
            {
                flg_pushanim &= ~0x2;
                anid_played = npc->AnimCurStateID();
            }
            else if (flg_pushanim & 0x4)
            {
                flg_pushanim &= ~0x4;
                anid_played = npc->AnimCurStateID();
            }
        }
    }
    else if (tym_animPlusTime >= tym_dur && !(flg_pushanim & 0x6))
    {
        *trantype = GOAL_TRAN_POP;
        return 1;
    }

    xASSERT_PLACEHOLDER();
    xASSERT_PLACEHOLDER();

    return zNMEGoalCommon::Process(trantype, dt, ctxt);
}

void zNMEGoalLoopAnim::GoalLoop_Enter(zNMECommon* npc)
{
    if (!(flg_info & 0x10))
    {
        cnt_loop = 0;
    }

    cnt_loop = CLAMP(cnt_loop, 0, 10000);

    origAnimFlags = 0;
    animWeMolested = 0;

    if (!(flg_info & 0x20))
    {
        UseDefaultAnims(npc);
    }

    ValidateStages();

    xAnimState* ast = npc->AnimFindState(anid_stage[1]);
    origAnimFlags = ast->Flags;
    animWeMolested = ast->ID;
    ast->Flags &= ~0x20;
    ast->Flags |= 0x10;

    if (anid_stage[0] != 0)
    {
        DoExplicitAnim(anid_stage[0], 1);
    }
    else
    {
        DoExplicitAnim(anid_stage[1], 1);
    }

    flg_info = 0;
}

en_loopstat zNMEGoalLoopAnim::GoalLoop_Process(zNMECommon* npc, F32 dt)
{
    // The PS2 DWARF lists only tym_animPlusTime, but the target computes the state ID, the
    // anim time and the duration once up front (time loaded before the duration, added after).
    U32 anid = npc->AnimCurStateID();
    F32 tym = npc->AnimTimeCurrent();
    F32 dur = npc->AnimDuration(NULL);
    F32 tym_animPlusTime = tym + dt;

    if (flg_loopanim & 0x4)
    {
        if (anid != anid_played)
        {
            flg_loopanim &= ~0x4;
            anid_played = anid;
        }
        else if (anid_played == anid_stage[0] && tym_animPlusTime >= dur)
        {
            DoExplicitAnim(anid_stage[1], 0);
            flg_loopanim &= ~0x4;
        }
    }
    else if (anid == anid_stage[1])
    {
        if (cnt_loop > 0)
        {
            if (tym_animPlusTime >= dur)
            {
                cnt_loop--;
            }
        }
        else if (tym_animPlusTime >= dur)
        {
            if (flg_loopanim & 0x8)
            {
                DoExplicitAnim(anid_stage[2], 0);
            }
            else
            {
                return LOOP_GOAL_IS_DONE;
            }
        }
    }
    else if (flg_loopanim & 0x8)
    {
        if (anid != anid_stage[2] || tym_animPlusTime >= dur)
        {
            return LOOP_GOAL_IS_DONE;
        }
    }
    // Retail still tests the time here, but either way the goal is done.
    else if (tym_animPlusTime >= dur)
    {
        return LOOP_GOAL_IS_DONE;
    }
    else
    {
        return LOOP_GOAL_IS_DONE;
    }

    return LOOP_GOAL_NOT_DONE;
}

// No GC symbols: inlined into GoalLoop_Enter and the out-of-line copies dead-stripped (the PS2
// build has them out of line in this file).
void zNMEGoalLoopAnim::UseDefaultAnims(zNMECommon* npc)
{
    U32 anid;

    anid_stage[1] = npc->AnimPick(GetID(), NME_GSPOT_LOOP, this);

    anid = npc->AnimPick(GetID(), NME_GSPOT_START, this);
    if (anid == anid_stage[1])
    {
        anid = 0;
    }
    anid_stage[0] = anid;
    if (anid != 0)
    {
        flg_loopanim |= 0x4;
    }

    anid = npc->AnimPick(GetID(), NME_GSPOT_FINISH, this);
    if (anid == anid_stage[1])
    {
        anid = 0;
    }
    anid_stage[2] = anid;
    if (anid != 0)
    {
        flg_loopanim |= 0x8;
    }
}

void zNMEGoalLoopAnim::ValidateStages()
{
    if (anid_stage[0] == anid_stage[1])
    {
        anid_stage[0] = 0;
        flg_loopanim &= ~0x4;
    }

    if (anid_stage[2] == anid_stage[1])
    {
        anid_stage[2] = 0;
        flg_loopanim &= ~0x8;
    }

    if ((flg_loopanim & 0x4) && anid_stage[0] == 0)
    {
        flg_loopanim &= ~0x4;
    }

    if ((flg_loopanim & 0x8) && anid_stage[2] == 0)
    {
        flg_loopanim &= ~0x8;
    }
}

S32 zNMEGoalIdle::Enter(F32 dt, void* updCtxt)
{
    zNMECommon* npc = (zNMECommon*)psyche->clt_owner;

    if (npc->navnet != NULL && npc->navnet->nav_dest == NULL)
    {
        npc->navnet->MvptCycle();
    }

    npc->spd_throttle = 0.0f;
    return zNMEGoalCommon::Enter(dt, updCtxt);
}

S32 zNMEGoalIdle::Exit(F32 dt, void* updCtxt)
{
    zNMECommon* npc = (zNMECommon*)psyche->clt_owner;

    npc->RestoreColFlags();
    return 0;
}

S32 zNMEGoalIdle::Suspend(F32 dt, void* updCtxt)
{
    zNMECommon* npc = (zNMECommon*)psyche->clt_owner;

    npc->RestoreColFlags();
    return 0;
}

S32 zNMEGoalIdle::Resume(F32 dt, void* updCtxt)
{
    if (((zNMECommon*)psyche->clt_owner)->navnet != NULL &&
        ((zNMECommon*)psyche->clt_owner)->navnet->nav_dest == NULL)
    {
        ((zNMECommon*)psyche->clt_owner)->navnet->MvptCycle();
    }

    return zNMEGoalCommon::Resume(dt, updCtxt);
}

S32 zNMEGoalIdle::Process(en_trantype* trantype, F32 dt, void* ctxt)
{
    zMovePoint* nav;

    if (((zNMECommon*)psyche->clt_owner)->navnet != NULL &&
        ((zNMECommon*)psyche->clt_owner)->navnet->nav_dest != NULL)
    {
        *trantype = GOAL_TRAN_PUSH;
        return NME_GOAL_PATROL;
    }

    if (((zNMECommon*)psyche->clt_owner)->navnet != NULL)
    {
        nav = ((zNMECommon*)psyche->clt_owner)->navnet->nav_curr;
        if (nav != NULL && nav->Delay() > 0.0f && nav->RadiusZone() > 0.0f)
        {
            *trantype = GOAL_TRAN_PUSH;
            return NME_GOAL_WANDER;
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

S32 zNMEGoalIdle::GoalHandleMail(NMEMsg* msg)
{
    S32 handled = 0;
    zNMEGoalCommon* goal;

    switch (msg->msgid)
    {
    case NME_MID_BECOMESCARED:
    case NME_MID_NOLONGERSCARED:
        goal = (zNMEGoalCommon*)psyche->GetCurGoal();
        if (goal != NULL)
        {
            goal->DoAutoAnim(NME_GSPOT_RESUME, 0);
        }
        handled = 0; // as BfBB's zNPCGoalIdle::NPCMessage
        break;
    }

    return handled;
}

S32 zNMEGoalPatrol::Enter(F32 dt, void* updCtxt)
{
    zNMECommon* npc = (zNMECommon*)psyche->clt_owner;

    flg_patrol = 0;

    if (npc->navnet->nav_dest == NULL)
    {
        DoOnArriveStuff(npc);
    }
    else
    {
        Chk_AutoSmooth(npc);
    }

    return zNMEGoalCommon::Enter(dt, updCtxt);
}

S32 zNMEGoalPatrol::Exit(F32 dt, void* updCtxt)
{
    zNMECommon* npc = (zNMECommon*)psyche->clt_owner;
    zNMENavNet* navnet = npc->navnet;

    if (navnet->spl_mvptspline != NULL)
    {
        navnet->nav_dest = navnet->nav_curr;
        navnet->nav_lead = navnet->nav_curr;
        navnet->spl_mvptspline = NULL;
        navnet->len_mvptspline = 0.0f;
        navnet->dst_curspline = 0.0f;
    }

    if (flg_patrol & 0x4)
    {
        if (navnet->nav_dest != NULL)
        {
            Chk_AutoSmooth(npc);
        }
        flg_patrol &= ~0x4;
    }

    return 0;
}

S32 zNMEGoalPatrol::Resume(F32 dt, void* updCtxt)
{
    zNMECommon* npc = (zNMECommon*)psyche->clt_owner;

    if (flg_patrol & 0x4)
    {
        if (npc->navnet->nav_dest != NULL)
        {
            Chk_AutoSmooth(npc);
        }
        flg_patrol &= ~0x4;
    }

    return zNMEGoalCommon::Resume(dt, updCtxt);
}

S32 zNMEGoalPatrol::Process(en_trantype* trantype, F32 dt, void* ctxt)
{
    zNMECommon* npc = (zNMECommon*)psyche->clt_owner;
    zNMENavNet* navnet;
    S32 nextgoal;

    if (flg_patrol & 0x1)
    {
        if (flg_patrol & 0x2)
        {
            npc->spd_throttle = 0.0f;
            tmr_wait = MAX(-1.0f, tmr_wait - dt);
            if (tmr_wait < 0.0f)
            {
                navnet = npc->navnet;
                if (navnet->nav_dest == NULL && !navnet->MvptCycle())
                {
                    if (navnet->nav_curr == NULL)
                    {
                        tmr_wait = 300.0f;
                    }
                    else
                    {
                        tmr_wait = navnet->nav_curr->Delay();
                    }
                    tmr_wait = MAX(2.0f, tmr_wait);
                }
                else
                {
                    flg_patrol &= ~0x2;
                    DoAutoAnim(NME_GSPOT_RESUME, 0);
                }
            }
        }

        if (!(flg_patrol & 0x2))
        {
            nextgoal = 0;
            if (npc->navnet->nav_curr != NULL && npc->navnet->nav_curr->Delay() > 0.0f &&
                npc->navnet->nav_curr->RadiusZone() > 0.0f)
            {
                *trantype = GOAL_TRAN_PUSH;
                nextgoal = NME_GOAL_WANDER;
                flg_patrol |= 0x4;
                flg_patrol &= ~0x1;
            }
            else if (npc->navnet->nav_dest == NULL)
            {
                *trantype = GOAL_TRAN_SET;
                nextgoal = NME_GOAL_IDLE;
            }

            if (*trantype != GOAL_TRAN_NONE)
            {
                return nextgoal;
            }

            flg_patrol &= ~0x1;
        }
    }

    if (flg_patrol & 0x2)
    {
        npc->spd_throttle = 0.0f;
    }
    else
    {
        npc->ThrottleAccel(dt, 1, 0.75f);
    }

    if (!(flg_patrol & 0x2))
    {
        if (npc->navnet->spl_mvptspline != NULL)
        {
            MoveSpline(npc, dt);
        }
        else if (flg_patrol & 0x8)
        {
            MoveAutoSmooth(npc, dt);
        }
        else if (npc->navnet->nav_dest != NULL)
        {
            MoveNormal(npc, dt);
        }
    }

    return zNMEGoalCommon::Process(trantype, dt, ctxt);
}

// The asserts are compiled-out placeholders (conditions unknown): these 5 and the 2 in the inlined
// Chk_AutoSmooth push its inline cost past GC/2.6's 512 auto-inline limit, as retail calls it.
void zNMEGoalPatrol::DoOnArriveStuff(zNMECommon* npc)
{
    zNMENavNet* navnet = npc->navnet;

    xASSERT_PLACEHOLDER();
    xASSERT_PLACEHOLDER();

    if (navnet->nav_dest != NULL)
    {
        zEntEvent(npc, 0, navnet->nav_dest, eEventArrive, NULL, NULL, 0, FE_NO);
    }

    xASSERT_PLACEHOLDER();

    flg_patrol |= 0x1;
    flg_patrol &= ~0x8;

    navnet->MvptCycle();

    xASSERT_PLACEHOLDER();

    if (navnet->nav_curr != NULL && navnet->nav_curr->Delay() > 0.0f &&
        navnet->nav_curr->RadiusZone() < 0.25f)
    {
        xASSERT_PLACEHOLDER();
        tmr_wait = navnet->nav_curr->Delay();
        flg_patrol |= 0x2;
        DoAutoAnim(NME_GSPOT_PATROLPAUSE, 0);
    }
    else
    {
        Chk_AutoSmooth(npc);
    }
}

// pos_dest is not in the PS2 DWARF, but GC copies nav_dest's position with xVec3Copy first, and
// computes spd and len once. delta is block-local (a function-scope one adds a second copy); len
// comes from xVec3::length(), as retail's registers need.
void zNMEGoalPatrol::MoveNormal(zNMECommon* npc, F32 dt)
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
        if (npc->flg_move & 0x2)
        {
            pos_dest.y = npc->model->Mat->pos.y;
        }
        npc->model->Mat->pos.x = pos_dest.x;
        npc->model->Mat->pos.y = pos_dest.y;
        npc->model->Mat->pos.z = pos_dest.z;
        DoOnArriveStuff(npc);
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

// pos_dest is not in the PS2 DWARF: GC copies nav_dest's position with xVec3Copy (as in
// MoveNormal).
void zNMEGoalPatrol::MoveSpline(zNMECommon* npc, F32 dt)
{
    zNMENavNet* navnet = npc->navnet;
    xSpline3* spl = navnet->spl_mvptspline;
    xVec3 pos_dest;
    F32 newdist;
    xVec3 tgt;
    F32 u;
    xVec3 dir;
    F32 qdot;

    if (spl == NULL)
    {
        MoveNormal(npc, dt);
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
        xVec3Copy(&pos_dest, navnet->nav_dest->PosGet());
        if (npc->flg_move & 0x2)
        {
            pos_dest.y = npc->model->Mat->pos.y;
        }
        npc->model->Mat->pos.x = pos_dest.x;
        npc->model->Mat->pos.y = pos_dest.y;
        npc->model->Mat->pos.z = pos_dest.z;
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

// No GC symbol: inlined into every caller and dead-stripped. Its two compiled-out xASSERTs count
// toward DoOnArriveStuff's inline cost, keeping that out of line as in retail. navnet points to
// const (the PS2 DWARF drops const), so the inlined copies reload nav_dest as retail does.
void zNMEGoalPatrol::Chk_AutoSmooth(zNMECommon* npc)
{
    xASSERT_PLACEHOLDER();

    const zNMENavNet* navnet = npc->navnet;

    xASSERT_PLACEHOLDER();

    if (navnet->spl_mvptspline != NULL || navnet->nav_curr == NULL || navnet->nav_dest == NULL)
    {
        return;
    }

    if (navnet->nav_curr == NULL || navnet->nav_dest == NULL || navnet->nav_lead == NULL ||
        navnet->nav_curr == navnet->nav_dest || navnet->nav_curr == navnet->nav_lead)
    {
        return;
    }

    static F32 ds2_min = SQ(1.5f);

    if (npc->XZDstSqToPos(navnet->nav_dest->PosGet(), NULL, NULL) > ds2_min)
    {
        flg_patrol |= 0x18;
    }
}

// No GC symbol: inlined into MoveAutoSmooth, its only caller (out-of-line copy dead-stripped).
void zNMEGoalPatrol::CalcSmoothPoints(zNMECommon* npc)
{
    zNMENavNet* navnet = npc->navnet;
    xVec3* refbase[4];
    xVec3* refmid[4];

    refbase[0] = navnet->nav_past->PosGet();
    refbase[1] = navnet->nav_curr->PosGet();
    refbase[2] = navnet->nav_dest->PosGet();
    refbase[3] = navnet->nav_lead->PosGet();

    if (npc->XZDstSqToPos(refbase[2], NULL, NULL) < NMEC_DstSq(refbase[1], refbase[2], NULL))
    {
        refbase[1] = xEntGetPos(npc);
    }

    refmid[0] = &pos_midpnt[0];
    refmid[1] = &pos_midpnt[1];
    refmid[2] = &pos_midpnt[2];
    refmid[3] = &pos_midpnt[3];
    NMEC_GenSmooth(refbase, refmid);

    flg_patrol &= ~0x10;
    idx_midpnt = 0;
}

// spd and len are not in the PS2 DWARF (GC computes each once). len comes from vec_dest.length(),
// and dir_dest and delta are block-local, as in MoveNormal.
void zNMEGoalPatrol::MoveAutoSmooth(zNMECommon* npc, F32 dt)
{
    zNMENavNet* navnet = npc->navnet;
    // not in the DWARF (the PS2 build read pos_midpnt directly): GC copies it with xVec3Copy
    // first, as MoveNormal does for nav_dest's position.
    xVec3 pos_dest;
    xVec3 vec_dest;

    if (navnet->nav_curr == NULL || navnet->nav_dest == NULL || navnet->nav_lead == NULL)
    {
        flg_patrol &= ~0x8;
        MoveNormal(npc, dt);
    }
    else
    {
        if (flg_patrol & 0x10)
        {
            CalcSmoothPoints(npc);
        }

        xVec3Copy(&pos_dest, &pos_midpnt[idx_midpnt]);
        xVec3Sub(&vec_dest, &pos_dest, (xVec3*)&npc->model->Mat->pos);
        if (npc->flg_move & 0x2)
        {
            vec_dest.y = 0.0f;
        }

        F32 spd = MAX(1e-5f, dt * npc->spd_throttle);
        F32 len = vec_dest.length();
        if (len < spd)
        {
            if (npc->flg_move & 0x2)
            {
                pos_dest.y = npc->model->Mat->pos.y;
            }
            npc->model->Mat->pos.x = pos_dest.x;
            npc->model->Mat->pos.y = pos_dest.y;
            npc->model->Mat->pos.z = pos_dest.z;
            if (idx_midpnt == 3)
            {
                DoOnArriveStuff(npc);
                flg_patrol |= 0x10;
            }
            else
            {
                idx_midpnt++;
            }
        }
        else
        {
            xVec3 dir_dest = vec_dest * (1.0f / len);
            if (xVec3Dot(&dir_dest, (xVec3*)&npc->model->Mat->at) < 8.86f)
            {
                npc->MatQRotTowards(dt, &dir_dest, 4.0f * PI);
            }
            else
            {
                npc->MatQRotTowards(dt, &dir_dest, -1.0f);
            }
            xVec3 delta = dir_dest * spd;
            npc->model->Mat->pos.x += delta.x;
            npc->model->Mat->pos.y += delta.y;
            npc->model->Mat->pos.z += delta.z;
        }
    }
}

S32 zNMEGoalFidget::Enter(F32 dt, void* updCtxt)
{
    ((zNMECommon*)psyche->clt_owner)->spd_throttle = 0.0f;
    return zNMEGoalPushAnim::Enter(dt, updCtxt);
}

S32 zNMEGoalFidget::Exit(F32 dt, void* updCtxt)
{
    zNMECommon* npc = (zNMECommon*)psyche->clt_owner;

    npc->RestoreColFlags();
    return zNMEGoalPushAnim::Exit(dt, updCtxt);
}

S32 zNMEGoalWander::Enter(F32 dt, void* updCtxt)
{
    zNMECommon* npc = (zNMECommon*)psyche->clt_owner;
    // PS2 DWARF local: zero-filled and never read; retail keeps its unreferenced .rodata template.
    xVec3 vec = { 0.0f, 0.0f, 0.0f };
    zMovePoint* nav = npc->navnet->nav_curr;

    tmr_remain = 0.0f;
    rad_wand = 0.0f;

    if (nav != NULL)
    {
        xVec3Copy(&pos_home, nav->PosGet());
        tmr_remain = nav->Delay();
        rad_wand = nav->RadiusZone();
    }
    else
    {
        xVec3Copy(&pos_home, xEntGetPos(npc));
    }

    tmr_remain = MAX(1.0f, tmr_remain);
    rad_wand = MAX(1.0f, rad_wand);

    CalcNewDir(npc);
    return zNMEGoalCommon::Enter(dt, updCtxt);
}

S32 zNMEGoalWander::Process(en_trantype* trantype, F32 dt, void* ctxt)
{
    zNMECommon* npc = (zNMECommon*)psyche->clt_owner;

    tmr_remain = MAX(-1.0f, tmr_remain - dt);
    tmr_newdir = MAX(-1.0f, tmr_newdir - dt);

    if (tmr_newdir < 0.0f)
    {
        CalcNewDir(npc);
    }

    if (tmr_remain < 0.0f)
    {
        zMovePoint* nav = npc->navnet->nav_curr;
        if (nav == NULL)
        {
            tmr_remain = 3.0f;
        }
        else if (npc->navnet->nav_dest == NULL)
        {
            zMovePoint* mvpt;
            zMovePointGetNext(nav, NULL, &mvpt, NULL);
            if (mvpt == NULL)
            {
                tmr_remain = nav->Delay();
            }
        }
    }

    if (tmr_remain < 0.0f)
    {
        *trantype = GOAL_TRAN_POP;
        return 1;
    }

    npc->MatQRotTowards(dt, &dir_cur, -1.0f);
    npc->ThrottleAccel(dt, 1, 0.75f);

    F32 rate = npc->spd_throttle * dt;
    xVec3 delta = dir_cur * rate;
    npc->model->Mat->pos.x += delta.x;
    npc->model->Mat->pos.y += delta.y;
    npc->model->Mat->pos.z += delta.z;

    flg_wand &= ~0x2;

    xVec3 vec_dest;
    if (npc->flg_move & 0x4)
    {
        // dist and rat are not in the PS2 DWARF (it lists only the two rates), but the GC asm
        // computes the distance once before the branches and reuses it; both names and the
        // rat = rate / dist step come from BfBB's PS2 DWARF for zNPCGoalWander::Process.
        F32 dist = npc->XYZDstSqToPos(&pos_home, &vec_dest);
        F32 rate = npc->spd_throttle * dt;
        if (flg_wand & 0x2)
        {
            VerticalWander(npc, rate, &vec_dest);
        }
        else if (iabs(vec_dest.y) > 0.1f)
        {
            F32 rat = rate / dist;
            npc->model->Mat->pos.y += rat * vec_dest.y;
        }
    }

    return zNMEGoalCommon::Process(trantype, dt, ctxt);
}

// No GC symbol: auto-inlined into Process (its only caller) and the out-of-line copy dead-stripped.
void zNMEGoalWander::VerticalWander(zNMECommon* npc, F32 spd_dt, const xVec3* vec_dest)
{
    // Retail bug: this is (flg_wand & 0x4) | 0x8, which is never zero.
    if (!(flg_wand & 0x4 | 0x8))
    {
        flg_wand |= 0x4;
    }

    if (vec_dest->y > 0.0f)
    {
        npc->model->Mat->pos.y += spd_dt * vec_dest->y;
        flg_wand &= ~0xC;
        flg_wand |= 0x4;
    }
    else if (2.25f + vec_dest->y < 0.0f)
    {
        npc->model->Mat->pos.y += spd_dt * vec_dest->y;
        flg_wand &= ~0xC;
        flg_wand |= 0x8;
    }
    else
    {
        // The PS2 DWARF lists only rat_hyt, but the GC asm evaluates the clamped sine once and
        // reuses it for the compare, so rat is a real local (BfBB's PS2 DWARF lists both rats).
        F32 rat_hyt = vec_dest->y / 2.25f;

        if (flg_wand & 0x4)
        {
            F32 rat = CLAMP(iabs(isin(PI * (0.5f * rat_hyt))), 0.0f, 1.0f);
            npc->model->Mat->pos.y += 2.25f * rat + pos_home.y;
            if (rat > 0.9f)
            {
                flg_wand &= ~0xC;
                flg_wand |= 0x8;
            }
        }
        else if (flg_wand & 0x8)
        {
            F32 rat = CLAMP(iabs(isin(PI * (0.5f * rat_hyt + 0.5f))), 0.0f, 1.0f);
            npc->model->Mat->pos.y += 2.25f * rat + pos_home.y;
            if (rat < 0.1f)
            {
                flg_wand &= ~0xC;
                flg_wand |= 0x8;
            }
        }
    }
}

// spd, rad_max and rad_min are not in the PS2 DWARF, but GC computes spd once before the branches
// (across the xrand call) and 2 * rad_wand before the xsqrt.
void zNMEGoalWander::CalcNewDir(zNMECommon* npc)
{
    xVec3 vec_plyr;
    F32 rad_half;
    F32 spd = MAX(0.15f, npc->cfg_common->movement.spd_move);

    // not in the DWARF: the home delta is fully scalarized (GC keeps x/z in FPRs and drops the dead
    // y difference).
    xVec3 vec_home;
    npc->XZVecToPos(&vec_home, &pos_home, NULL);
    if (vec_home.length() < 1.0f)
    {
        npc->XZVecToPos(&vec_plyr, xEntGetPos(xglobals->___player_ent_dont_use_directly), NULL);
        if (vec_plyr.length() > 1.0f)
        {
            dir_cur = vec_plyr * -1.0f;
            dir_cur.normalize();
        }
        else
        {
            dir_cur = *(xVec3*)&npc->model->Mat->at;
        }

        rad_half = 0.5f * rad_wand;
        tmr_newdir = (rad_half * xurand() + rad_half) / spd;
    }
    else
    {
        NMEC_aimVary(&dir_cur, xEntGetPos(npc), &pos_home, rad_wand, 0x10, NULL);
        if (npc->flg_move & 0x2)
        {
            dir_cur.y = 0.0f;
        }

        F32 rad_max = 2.0f * rad_wand;
        F32 rad_min = xsqrt(1.0f + SQ(rad_wand));
        tmr_newdir = ((rad_max - rad_min) * xurand() + rad_min) / spd;
    }
}

S32 zNMEGoalWaiting::Enter(F32 dt, void* updCtxt)
{
    zNMECommon* npc = (zNMECommon*)psyche->clt_owner;

    if (!(flg_info & 0x10))
    {
        zMovePoint* nav = npc->navnet->nav_curr;
        if (nav != NULL && nav->Delay() > 0.75f)
        {
            tmr_waiting = (0.25f * (xurand() - 0.5f)) * nav->Delay() + nav->Delay();
        }
        else
        {
            tmr_waiting = 10.0f * (0.25f * (xurand() - 0.5f)) + 10.0f;
        }
    }

    flg_info = 0;
    tmr_waiting = MAX(0.75f, tmr_waiting);

    npc->flg_colCheck = 0;
    npc->flg_penCheck = 0;
    npc->pflags &= ~0xF;

    return zNMEGoalCommon::Enter(dt, updCtxt);
}

S32 zNMEGoalWaiting::Resume(F32 dt, void* updCtxt)
{
    zNMECommon* npc = (zNMECommon*)psyche->clt_owner;

    if (tmr_waiting < 0.15f)
    {
        tmr_waiting = 0.5f;
    }

    npc->flg_colCheck = 0;
    npc->flg_penCheck = 0;
    npc->pflags &= ~0xF;

    return zNMEGoalCommon::Resume(dt, updCtxt);
}

S32 zNMEGoalWaiting::Exit(F32 dt, void* updCtxt)
{
    zNMECommon* npc = (zNMECommon*)psyche->clt_owner;

    npc->RestoreColFlags();
    return 0;
}

S32 zNMEGoalWaiting::Process(en_trantype* trantype, F32 dt, void* ctxt)
{
    ((zNMECommon*)psyche->clt_owner)->spd_throttle = 0.0f;

    if (tmr_waiting < 0.0f)
    {
        cnt_loop = -1;
    }
    else
    {
        cnt_loop = 2;
        flg_info |= 0x10;
    }

    tmr_waiting = MAX(-1.0f, tmr_waiting - dt);

    return zNMEGoalCommon::Process(trantype, dt, ctxt);
}

S32 zNMEGoalLimbo::Enter(F32 dt, void* updCtxt)
{
    stylinDyin = DIE_SILENT;
    return zNMEGoalDead::Enter(dt, updCtxt);
}

S32 zNMEGoalLimbo::GoalHandleMail(NMEMsg* mail)
{
    S32 handled = 1;

    switch (mail->msgid)
    {
    case NME_MID_SYSEVENT:
    {
        // psy is not in the PS2 DWARF, but both builds load psyche before the toEvent compares
        // (BfBB's zNPCGoalLimbo::NPCMessage has the same copy).
        xPsyche* psy = psyche;

        if (mail->sysevent.toEvent != eEventNPCSetActiveOff)
        {
            if (mail->sysevent.toEvent == eEventNPCSetActiveOn)
            {
                psy->GoalSet(psy->GIDOfSafety(), 0);
            }
            else if (mail->sysevent.toEvent != eEventNPCRespawn)
            {
                handled = 0;
            }
        }
        break;
    }
    case NME_MID_RESPAWN:
    case NME_MID_DAMAGE:
        break;
    default:
        handled = 0;
        break;
    }

    return handled;
}

S32 zNMEGoalDEVAnimView::Enter(F32 dt, void* updCtxt)
{
    zNMECommon* npc = (zNMECommon*)psyche->clt_owner;
    // ast is not in the PS2 DWARF (BfBB's DWARF has it), but the target reloads ast->ID after
    // molesting, so the state pointer is kept.
    xAnimState* ast = npc->AnimCurState();

    ASTMolestAnim(npc, ast);
    DoExplicitAnim(ast->ID, 1);

    ang_facing = xatan2(npc->model->Mat->at.x, npc->model->Mat->at.z);

    return zNMEGoalCommon::Enter(dt, updCtxt);
}

S32 zNMEGoalDEVAnimView::Exit(F32 dt, void* updCtxt)
{
    zNMECommon* npc = (zNMECommon*)psyche->clt_owner;

    if (animWeMolested)
    {
        npc->AnimFindState(animWeMolested)->Flags = origAnimFlags;
        animWeMolested = 0;
        origAnimFlags = 0;
    }

    return 0;
}

S32 zNMEGoalDEVAnimView::Process(en_trantype* trantype, F32 dt, void* ctxt)
{
    ((zNMECommon*)psyche->clt_owner)->spd_throttle = 0.0f;
    return zNMEGoalCommon::Process(trantype, dt, ctxt);
}

S32 zNMEGoalDEVHero::Enter(F32 dt, void* updCtxt)
{
    return zNMEGoalCommon::Enter(dt, updCtxt);
}

S32 zNMEGoalDEVHero::Exit(F32 dt, void* updCtxt)
{
    return 0;
}

S32 zNMEGoalDEVHero::Process(en_trantype* trantype, F32 dt, void* ctxt)
{
    zNMECommon* npc = (zNMECommon*)psyche->clt_owner;

    npc->spd_throttle = 0.0f;
    npc->MatQRotTowards(dt, &g_Z3, -1.0f);

    return zNMEGoalCommon::Process(trantype, dt, ctxt);
}
