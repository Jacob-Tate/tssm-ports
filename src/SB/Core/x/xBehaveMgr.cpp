#include "xBehaveMgr.h"
#include "xBehaveGoalSimple.h"
#include "xDebug.h"

static S32 g_modinit;
static xBehaveMgr* g_behavmgr;

inline void xBehaveMgr::Startup(S32 size, S32 tmpAlloc)
{
    goalFactory = new ('BMGR', NULL) xFactory(tmpAlloc);

    g_behavmgr->RegBuiltIn();
    XOrdInit(&psylist, size, 0);
    psypool = (xPsyche*)xMemAlloc(gActiveHeap, size * sizeof(xPsyche), 0);
    memset(psypool, 0, size * sizeof(xPsyche));
}

void xBehaveMgr_Startup()
{
    if (g_modinit++ == 0)
    {
        g_behavmgr = new ('BMGR', NULL) xBehaveMgr();
        g_behavmgr->Startup(370, 250);
    }
}

void xBehaveMgr_Shutdown()
{
    g_modinit--;
    if (g_modinit == 0)
    {
        if (g_behavmgr != NULL)
        {
            delete g_behavmgr;
        }
        g_behavmgr = NULL;
    }
}

xBehaveMgr* xBehaveMgr_GetSelf()
{
    return g_behavmgr;
}

xPsyche* xBehaveMgr::Subscribe(xBase* owner, S32)
{
    xPsyche* psy = &this->psypool[this->psylist.cnt];
    XOrdAppend(&this->psylist, psy);
    psy->FreshWipe();
    psy->SetOwner(owner, NULL);
    return psy;
}

void xBehaveMgr::UnSubscribe(xPsyche* psy)
{
    psy->KillBrain(this->goalFactory);
    XOrdRemove(&this->psylist, psy, -1);
}

// The inline functions in this file have no symbol in the retail DOL (always inlined).
// The member ones stay out of class: xBehaveMgr::Startup uses the file-static g_behavmgr,
// and xPsyche::Lobotomy/SetTopState/GoalPopToBase need the complete xGoal.

inline xFactory* xBehaveMgr_GoalFactory()
{
    return g_behavmgr->GetFactory();
}

void xPsyche::BrainBegin()
{
    xFactory* factory = xBehaveMgr_GoalFactory();
    this->psystat = PSY_STAT_GROW;
    factory->GrowDataEnable(&this->fakebase, false);
}

void xPsyche::BrainEnd()
{
    xBehaveMgr_GoalFactory()->GrowDataDisable();
    this->psystat = PSY_STAT_THINK;
}

xGoal* xPsyche::AddGoal(S32 gid, void* createData)
{
    xGoal* goal = (xGoal*)xBehaveMgr_GoalFactory()->CreateItem(gid, createData, NULL);

    if (goal != NULL)
    {
        if (this->goallist != NULL)
        {
            goal->Insert(this->goallist);
        }
        else
        {
            this->goallist = goal;
        }
        goal->SetPsyche(this);
    }
    else
    {
        xUtil_idtag2string(gid, 0);
    }

    return goal;
}

S32 xPsyche::IndexInStack(S32 gid) const
{
    S32 da_idx = -1;

    for (S32 i = 0; i <= this->staktop; i++)
    {
        if (gid == this->goalstak[i]->GetID())
        {
            da_idx = i;
            break;
        }
    }

    return da_idx;
}

xGoal* xPsyche::GetCurGoal() const
{
    if (this->staktop < 0)
    {
        return NULL;
    }
    return this->goalstak[this->staktop];
}

xGoal* xPsyche::GIDInStack(S32 gid) const
{
    xGoal* da_goal = NULL;
    xGoal* tmpgoal;

    for (S32 i = 0; i <= this->staktop; i++)
    {
        tmpgoal = this->goalstak[i];
        if (gid == tmpgoal->GetID())
        {
            da_goal = tmpgoal;
            break;
        }
    }

    return da_goal;
}

S32 xPsyche::GIDOfActive() const
{
    if (this->staktop < 0)
    {
        return 0;
    }
    return this->goalstak[this->staktop]->GetID();
}

S32 xPsyche::GIDOfPending() const
{
    if (this->pendgoal != NULL)
    {
        return this->pendgoal->GetID();
    }
    return 0;
}

inline void xPsyche::SetTopState(en_GOALSTATE state)
{
    if (this->staktop >= 0)
    {
        this->goalstak[this->staktop]->SetState(state);
    }
}

// The pendgoal/Name() block is BfBB's; with TSSM's inline Name() it compiles to nothing. It and 5
// placeholders keep GC/2.6 from auto-inlining this, as retail calls it.
S32 xPsyche::GoalSet(S32 gid, S32 unk)
{
    S32 result = 0;
    xGoal* goal;
    en_trantype trantype;

    if (this->flg_psyche & 4)
    {
        return 0;
    }

    if (this->fun_remap != NULL)
    {
        trantype = GOAL_TRAN_SET;
        this->fun_remap(&gid, &trantype);
        if (trantype != GOAL_TRAN_SET)
        {
            return 0;
        }
    }

    if (this->gid_safegoal == 0)
    {
        this->SetSafety(gid);
    }

    if (this->pendgoal != NULL && !unk)
    {
        this->pendgoal->Name();
    }
    xASSERT_PLACEHOLDER();
    xASSERT_PLACEHOLDER();

    goal = this->FindGoal(gid);
    if (goal == NULL)
    {
        goal = this->FindGoal(this->gid_safegoal);
    }

    if (goal != NULL)
    {
        this->pendgoal = goal;
        result = 1;
        this->pendtype = PEND_TRAN_SET;
        this->SetTopState(GOAL_STAT_EXIT);
    }

    xASSERT_PLACEHOLDER();
    xASSERT_PLACEHOLDER();
    xASSERT_PLACEHOLDER();

    if (result != 0 && this->pendtype != PEND_TRAN_NONE && (this->flg_psyche & 1))
    {
        this->ForceTran(0.01f, NULL);
    }

    return result;
}

// The pendgoal/Name() block is BfBB's; with TSSM's inline Name() it compiles to nothing. It and a
// placeholder keep GC/2.6 from auto-inlining this, as retail calls it.
S32 xPsyche::GoalPush(S32 gid, S32 unk)
{
    S32 result = 0;
    xGoal* goal;
    en_trantype trantype;

    if (this->flg_psyche & 4)
    {
        return 0;
    }

    if (this->fun_remap != NULL)
    {
        trantype = GOAL_TRAN_PUSH;
        this->fun_remap(&gid, &trantype);
        if (trantype != GOAL_TRAN_PUSH)
        {
            return 0;
        }
    }

    if (this->pendgoal != NULL && !unk)
    {
        this->pendgoal->Name();
    }
    xASSERT_PLACEHOLDER();

    if (this->staktop >= 4)
    {
        return 0;
    }

    if (this->staktop >= 0 && !(this->goalstak[this->staktop]->GetFlags() & 4))
    {
        return 0;
    }

    goal = this->FindGoal(gid);
    if (goal == NULL)
    {
        goal = this->FindGoal(this->gid_safegoal);
    }

    if (goal != NULL && (goal->GetFlags() & 2))
    {
        this->pendgoal = goal;
        result = 1;
        this->pendtype = PEND_TRAN_PUSH;
        this->SetTopState(GOAL_STAT_SUSPEND);
    }

    if (result != 0 && this->pendtype != PEND_TRAN_NONE && (this->flg_psyche & 1))
    {
        this->ForceTran(0.01f, NULL);
    }

    return result;
}

inline S32 xPsyche::GoalPopToBase(S32 overpend)
{
    if (this->flg_psyche & 4)
    {
        return 0;
    }
    if (this->staktop < 1)
    {
        return 0;
    }

    this->GoalPop(this->goalstak[0]->GetID(), overpend);
    if (this->pendtype != PEND_TRAN_NONE && (this->flg_psyche & 1))
    {
        this->ForceTran(0.01f, NULL);
    }

    return 1;
}

// The compiled-out asserts (conditions unknown) keep GC/2.6 from auto-inlining this, as retail
// calls it.
S32 xPsyche::GoalPopRecover(S32 overpend)
{
    S32 result = 0;
    S32 i;
    xGoal* tmpgoal;
    xGoal* destgoal = NULL;

    if (this->flg_psyche & 4)
    {
        return 0;
    }
    xASSERT_PLACEHOLDER();
    xASSERT_PLACEHOLDER();
    xASSERT_PLACEHOLDER();

    if (this->staktop < 1)
    {
        return 0;
    }
    xASSERT_PLACEHOLDER();
    xASSERT_PLACEHOLDER();
    xASSERT_PLACEHOLDER();

    for (i = this->staktop - 1; i >= 0; i--)
    {
        tmpgoal = this->goalstak[i];
        if (tmpgoal->GetFlags() & 8)
        {
            destgoal = tmpgoal;
            break;
        }
    }

    if (destgoal != NULL)
    {
        this->GoalPop(destgoal->GetID(), overpend);
        result = 1;
    }
    xASSERT_PLACEHOLDER();
    xASSERT_PLACEHOLDER();
    xASSERT_PLACEHOLDER();

    if (result != 0)
    {
        if (this->pendtype != PEND_TRAN_NONE && (this->flg_psyche & 1))
        {
            this->ForceTran(0.01f, NULL);
        }
    }

    return result;
}

S32 xPsyche::GoalPop(S32 gid_popto, S32)
{
    S32 result = 0;
    xGoal* destgoal = NULL;
    xGoal* tmpgoal;
    S32 i;
    en_trantype trantype;

    if (this->flg_psyche & 4)
    {
        return 0;
    }

    if (gid_popto != 0 && this->fun_remap != NULL)
    {
        trantype = GOAL_TRAN_POPTO;
        this->fun_remap(&gid_popto, &trantype);
        if (trantype != GOAL_TRAN_POPTO)
        {
            return 0;
        }
    }

    if (this->staktop < 0)
    {
        return 0;
    }

    if (this->staktop < 1)
    {
        result = 0;
    }
    else if (gid_popto == 0)
    {
        this->pendgoal = NULL;
        this->pendtype = PEND_TRAN_POP;
        this->SetTopState(GOAL_STAT_EXIT);
        result = 1;
    }
    else
    {
        if (gid_popto == this->goalstak[this->staktop]->GetID())
        {
            destgoal = NULL;
        }
        else
        {
            for (i = 0; i < this->staktop; i++)
            {
                tmpgoal = this->goalstak[i];
                if (gid_popto == tmpgoal->GetID())
                {
                    destgoal = tmpgoal;
                    break;
                }
            }
        }

        if (destgoal != NULL)
        {
            this->pendgoal = destgoal;
            this->pendtype = PEND_TRAN_POPTO;
            this->SetTopState(GOAL_STAT_EXIT);
            result = 1;
        }
    }

    if (result != 0 && this->pendtype != PEND_TRAN_NONE && (this->flg_psyche & 1))
    {
        this->ForceTran(0.01f, NULL);
    }

    return result;
}

S32 xPsyche::GoalSwap(S32 gid, S32)
{
    S32 result = 0;
    en_trantype trantype;

    if (this->flg_psyche & 4)
    {
        return 0;
    }

    if (this->fun_remap != NULL)
    {
        trantype = GOAL_TRAN_SWAP;
        this->fun_remap(&gid, &trantype);
        if (trantype != GOAL_TRAN_SWAP)
        {
            return 0;
        }
    }

    xGoal* goal = this->FindGoal(gid);
    if (goal != NULL)
    {
        this->pendgoal = goal;
        this->pendtype = PEND_TRAN_SWAP;
        result = 1;
        this->SetTopState(GOAL_STAT_EXIT);
    }

    if (result != 0 && this->pendtype != PEND_TRAN_NONE && (this->flg_psyche & 1))
    {
        this->ForceTran(0.01f, NULL);
    }

    return result;
}

S32 xPsyche::GoalNone(S32, S32 denyExplicit)
{
    if (this->flg_psyche & 4)
    {
        return 0;
    }

    this->pendgoal = NULL;
    if (this->staktop < 0)
    {
        this->pendtype = PEND_TRAN_NONE;
    }
    else
    {
        this->pendtype = PEND_TRAN_POPALL;
        this->SetTopState(GOAL_STAT_EXIT);
    }

    if (this->pendtype != PEND_TRAN_NONE && (this->flg_psyche & 1))
    {
        if (denyExplicit)
        {
            S32 isActive = this->ExpTranIsOn();
            this->ExpTranOff();
            this->ForceTran(0.01f, NULL);
            if (isActive)
            {
                this->ExpTranOn();
            }
        }
        else
        {
            this->ForceTran(0.01f, NULL);
        }
    }

    return 1;
}

xGoal* xPsyche::FindGoal(S32 gid)
{
    xGoal* goal = this->goallist;

    while (goal != NULL)
    {
        if (gid == goal->GetID())
        {
            break;
        }
        goal = goal->Next();
    }

    return goal;
}

xGoal* xPsyche::HasGoal(S32 gid)
{
    xGoal* goal = this->goallist;

    while (goal != NULL)
    {
        if (gid == goal->GetID())
        {
            break;
        }
        goal = goal->Next();
    }

    return goal;
}

S32 xPsyche::Timestep(F32 dt, void* updCtxt)
{
    S32 old_psyflags = this->flg_psyche;
    S32 sanity;
    S32 moretodo;
    S32 trangid = 0;
    en_trantype trantyp;

    this->flg_psyche &= ~1;

    if (this->flg_psyche & 2)
    {
        this->cnt_transLastTimestep++;
    }
    else
    {
        this->cnt_transLastTimestep = 0;
    }

    this->TimerUpdate(dt);

    sanity = 0;
    do
    {
        sanity++;
        if (sanity > 16)
        {
            break;
        }

        trantyp = GOAL_TRAN_NONE;
        moretodo = 0;

        if (this->pendtype != PEND_TRAN_NONE)
        {
            moretodo = this->TranGoal(dt, updCtxt);
        }

        if (moretodo != 0 && this->pendtype == PEND_TRAN_POPALL)
        {
            break;
        }
        if (moretodo != 0)
        {
            break;
        }

        if (this->pendtype == PEND_TRAN_NONE)
        {
            if (this->staktop < 0)
            {
                break;
            }

            trangid = this->goalstak[this->staktop]->Process(&trantyp, dt, updCtxt);
            if (trantyp != GOAL_TRAN_NONE || trangid != 0)
            {
                this->ParseTranRequest(trantyp, trangid);
            }
        }
    } while (this->pendtype != PEND_TRAN_NONE);

    this->flg_psyche |= (old_psyflags & 1);

    return 0;
}

S32 xPsyche::ParseTranRequest(en_trantype trantyp, S32 trangid)
{
    S32 rc = 0;

    if (trantyp == GOAL_TRAN_NONE && trangid == 0)
    {
        rc = 1;
    }
    else if (trantyp == GOAL_TRAN_POP)
    {
        rc = this->GoalPop(0, 0);
    }
    else if (trantyp == GOAL_TRAN_POPALL)
    {
        rc = this->GoalNone(0, 0);
    }
    else if (trantyp == GOAL_TRAN_POPBASE)
    {
        rc = this->GoalPopToBase(0);
    }
    else if (trantyp == GOAL_TRAN_POPSAFE)
    {
        rc = this->GoalPopRecover(0);
    }
    else if (trangid != 0)
    {
        if (trantyp == GOAL_TRAN_PUSH)
        {
            rc = this->GoalPush(trangid, 1);
        }
        else if (trantyp == GOAL_TRAN_POPTO)
        {
            rc = this->GoalPop(trangid, 1);
        }
        else if (trantyp == GOAL_TRAN_SWAP)
        {
            rc = this->GoalSwap(trangid, 1);
        }
        else if (trantyp == GOAL_TRAN_SET)
        {
            rc = this->GoalSet(trangid, 1);
        }
    }

    return rc;
}

S32 xPsyche::TranGoal(F32 dt, void* updCtxt)
{
    S32 halfway = 0;
    S32 just_switched = 0;
    xGoal* topgoal;

    if (this->staktop < 0 && this->pendgoal != NULL)
    {
        this->staktop = 0;
        this->goalstak[this->staktop] = this->pendgoal;
        this->pendgoal->SetState(GOAL_STAT_ENTER);
        this->pendgoal = NULL;
        this->pendtype = PEND_TRAN_INPROG;
    }

    if (this->staktop < 0)
    {
        return 0;
    }

    if (this->pendtype != PEND_TRAN_NONE && this->pendtype != PEND_TRAN_INPROG)
    {
        S32 moretodo = 0;
        topgoal = this->goalstak[this->staktop];
        switch (topgoal->GetState())
        {
        case GOAL_STAT_SUSPEND:
            moretodo = topgoal->Suspend(dt, updCtxt);
            if (moretodo == 0)
            {
                topgoal->SetState(GOAL_STAT_PAUSED);
            }
            break;
        case GOAL_STAT_EXIT:
            moretodo = topgoal->Exit(dt, updCtxt);
            if (moretodo == 0)
            {
                topgoal->SetState(GOAL_STAT_DONE);
            }
            break;
        }

        if (moretodo != 0)
        {
            return moretodo;
        }
        halfway = 1;
    }

    if (halfway != 0)
    {
        switch (this->pendtype)
        {
        case PEND_TRAN_SET:
            if (this->staktop >= 0)
            {
                this->goalstak[this->staktop] = NULL;
            }
            this->staktop--;
            if (this->staktop >= 0)
            {
                this->goalstak[this->staktop]->SetState(GOAL_STAT_EXIT);
                break;
            }
        case PEND_TRAN_PUSH:
            this->staktop++;
        case PEND_TRAN_SWAP:
            this->goalstak[this->staktop] = this->pendgoal;
            this->goalstak[this->staktop]->SetState(GOAL_STAT_ENTER);
            this->pendgoal = NULL;
            this->pendtype = PEND_TRAN_INPROG;
            break;
        case PEND_TRAN_POP:
            this->goalstak[this->staktop] = NULL;
            this->staktop--;
            this->goalstak[this->staktop]->SetState(GOAL_STAT_RESUME);
            this->pendtype = PEND_TRAN_INPROG;
            break;
        case PEND_TRAN_POPTO:
            this->goalstak[this->staktop] = NULL;
            this->staktop--;
            topgoal = this->goalstak[this->staktop];
            if (topgoal == this->pendgoal)
            {
                topgoal->SetState(GOAL_STAT_RESUME);
                this->pendgoal = NULL;
                this->pendtype = PEND_TRAN_INPROG;
            }
            else
            {
                topgoal->SetState(GOAL_STAT_EXIT);
            }
            break;
        case PEND_TRAN_POPALL:
            this->goalstak[this->staktop] = NULL;
            this->staktop--;
            if (this->staktop >= 0)
            {
                this->goalstak[this->staktop]->SetState(GOAL_STAT_EXIT);
            }
            else
            {
                this->pendgoal = NULL;
                this->pendtype = PEND_TRAN_INPROG;
            }
            break;
        }

        if (this->pendtype != PEND_TRAN_INPROG)
        {
            return 0;
        }
        just_switched = 1;
    }

    if (this->staktop >= 0)
    {
        S32 moretodo = 0;
        topgoal = this->goalstak[this->staktop];
        switch (topgoal->GetState())
        {
        case GOAL_STAT_RESUME:
            moretodo = topgoal->Resume(dt, updCtxt);
            if (moretodo == 0)
            {
                topgoal->SetState(GOAL_STAT_PROCESS);
                if (this->cb_notice != NULL)
                {
                    this->cb_notice->Notice(PSY_NOTE_HASRESUMED, topgoal, NULL);
                }
            }
            break;
        case GOAL_STAT_ENTER:
            if (just_switched != 0)
            {
                this->TimerClear();
            }
            moretodo = topgoal->Enter(dt, updCtxt);
            if (moretodo == 0)
            {
                topgoal->SetState(GOAL_STAT_PROCESS);
                if (this->cb_notice != NULL)
                {
                    this->cb_notice->Notice(PSY_NOTE_HASENTERED, topgoal, NULL);
                }
            }
            break;
        }

        if (moretodo == 0)
        {
            this->pendtype = PEND_TRAN_NONE;
        }
    }

    if (this->pendtype != PEND_TRAN_NONE)
    {
        return 2;
    }

    if (this->staktop >= 0)
    {
        this->goalstak[this->staktop]->Name();
    }

    return 0;
}

F32 xPsyche::TimerGet(en_xpsytime tymr)
{
    if (this->staktop < 0)
    {
        return -1.0f;
    }
    return (&this->tmr_stack[0][this->staktop])[tymr];
}

inline void xPsyche::Lobotomy(xFactory* factory)
{
    while (this->goallist != NULL)
    {
        xGoal* goal = this->goallist->RemHead(&this->goallist);
        factory->DestroyItem(goal);
    }
}
