#ifndef XBEHAVIOUR_H
#define XBEHAVIOUR_H

#include "xBase.h"
#include "xListItem.h"
#include "xFactory.h"
#include "xScene.h"

enum en_GOALSTATE
{
    GOAL_STAT_UNKNOWN,
    GOAL_STAT_PROCESS,
    GOAL_STAT_ENTER,
    GOAL_STAT_EXIT,
    GOAL_STAT_SUSPEND,
    GOAL_STAT_RESUME,
    GOAL_STAT_PAUSED,
    GOAL_STAT_DONE,
    GOAL_STAT_NOMORE,
    GOAL_STAT_FORCE = 0x7fffffff
};

enum en_trantype
{
    GOAL_TRAN_NONE,
    GOAL_TRAN_SET,
    GOAL_TRAN_PUSH,
    GOAL_TRAN_POP,
    GOAL_TRAN_POPTO,
    GOAL_TRAN_POPALL,
    GOAL_TRAN_POPBASE,
    GOAL_TRAN_POPSAFE,
    GOAL_TRAN_SWAP,
    GOAL_TRAN_NOMORE,
    GOAL_TRAN_FORCE = 0x7fffffff
};

enum en_pendtype
{
    PEND_TRAN_NONE,
    PEND_TRAN_SET,
    PEND_TRAN_PUSH,
    PEND_TRAN_POP,
    PEND_TRAN_POPTO,
    PEND_TRAN_POPALL,
    PEND_TRAN_SWAP,
    PEND_TRAN_INPROG,
    PEND_TRAN_NOMORE
};

enum PSY_BRAIN_STATUS
{
    PSY_STAT_BLANK,
    PSY_STAT_GROW,
    PSY_STAT_EXTEND,
    PSY_STAT_THINK,
    PSY_STAT_NOMORE,
    PSY_STAT_FORCE = 0x7fffffff
};

enum en_psynote
{
    PSY_NOTE_HASRESUMED,
    PSY_NOTE_HASENTERED,
    PSY_NOTE_ANIMCHANGED,
    PSY_NOTE_NOMORE,
    PSY_NOTE_FORCE = 0x7fffffff
};

enum en_xpsytime
{
    XPSY_TYMR_CURGOAL,
    XPSY_TYMR_NOMORE
};

struct xGoal;

struct xPSYNote
{
    virtual void Notice(en_psynote note, xGoal* goal, void*)
    {
    }
};

typedef S32 (*xGoalProcessCallback)(xGoal*, void*, en_trantype*, F32, void*);
typedef S32 (*xGoalChkRuleCallback)(xGoal*, void*, en_trantype*, F32, void*);
typedef S32 (*xGoalPreCalcCallback)(xGoal*, void*, F32, void*);

struct xPsyche : RyzMemData
{
    xBase* clt_owner;
    xPSYNote* cb_notice;
    S32 flg_psyche;
    xGoal* goallist;
    xGoal* goalstak[5];
    F32 tmr_stack[1][5];
    S32 staktop;
    xGoal* pendgoal;
    en_pendtype pendtype;
    S32 gid_safegoal;
    void (*fun_remap)(S32*, en_trantype*);
    void* userContext;
    S32 cnt_transLastTimestep;
    PSY_BRAIN_STATUS psystat;
    xBase fakebase;

    xGoal* GIDInStack(S32 gid) const;
    void ImmTranOn();
    void ImmTranOff();
    S32 ImmTranIsOn();
    xGoal* HasGoal(S32 gid);
    xGoal* GetCurGoal() const;
    S32 GIDOfActive() const;
    S32 GIDOfPending() const;
    S32 GIDOfSafety() const
    {
        return gid_safegoal;
    }
    S32 Timestep(F32 dt, void* updCtxt);
    S32 ParseTranRequest(en_trantype trantyp, S32 trangid);
    S32 TranGoal(F32 dt, void* updCtxt);
    F32 TimerGet(en_xpsytime tymr);
    xGoal* FindGoal(S32 gid);
    S32 GoalSet(S32 gid, S32 r5);
    S32 GoalPop(S32 gid_popto, S32 r5);
    S32 GoalNone(S32 p1, S32 denyExplicit);
    S32 GoalSwap(S32 gid, S32 r5);
    S32 GoalPopRecover(S32 overpend);
    S32 GoalPopToBase(S32 overpend);
    S32 GoalPush(S32 gid, S32 r5);
    S32 GoalSet(S32 gid);

    void BrainBegin();
    void BrainExtend();
    void BrainEnd();
    xGoal* AddGoal(S32 gid, void* createData);
    void FreshWipe()
    {
        S32 i;

        for (i = 0; i < 5; i++)
        {
            this->goalstak[i] = NULL;
        }
        this->staktop = -1;
        this->gid_safegoal = 0;
        this->pendgoal = NULL;
        this->pendtype = PEND_TRAN_NONE;
        for (i = 0; i < 5; i++)
        {
            this->tmr_stack[0][i] = 0.0f;
        }
        this->clt_owner = NULL;
        this->userContext = NULL;
        this->fun_remap = NULL;
        this->cb_notice = NULL;
        this->psystat = PSY_STAT_BLANK;
        this->flg_psyche |= 1;
    }
    void SetOwner(xBase* clt_owner, void* userContext)
    {
        this->clt_owner = clt_owner;
        this->userContext = userContext;
        if (clt_owner == NULL)
        {
            return;
        }

        this->fakebase.id = clt_owner->id;
        this->fakebase.baseType = clt_owner->baseType + 0x80;
        this->fakebase.linkCount = 0;
        this->fakebase.baseFlags = 0;
        this->fakebase.link = NULL;
        this->fakebase.eventFunc = NULL;
    }
    void KillBrain(xFactory* factory)
    {
        this->Lobotomy(factory);
        this->fun_remap = NULL;
    }
    void Lobotomy(xFactory* factory);
    void ForceTran(F32 dt, void* ctx)
    {
        this->flg_psyche |= 2;
        this->Timestep(dt, ctx != NULL ? ctx : this->userContext);
        this->flg_psyche &= ~2;
    }
    void SetTopState(en_GOALSTATE state);
    S32 IndexInStack(S32 gid) const;
    void SetSafety(S32 goalID)
    {
        gid_safegoal = goalID;
    }
    void TimerClear()
    {
        if (this->staktop < 0)
        {
            return;
        }
        this->tmr_stack[0][this->staktop] = 0.0f;
    }
    void TimerUpdate(F32 dt)
    {
        if (this->staktop < 0)
        {
            return;
        }
        for (S32 i = 0; i < XPSY_TYMR_NOMORE; i++)
        {
            switch (i)
            {
            case XPSY_TYMR_CURGOAL:
                this->tmr_stack[i][this->staktop] += dt;
                break;
            default:
                this->tmr_stack[i][this->staktop] += dt;
                break;
            }
        }
    }
    void Amnesia(S32);
    void SetNotify(xPSYNote* notice)
    {
        cb_notice = notice;
    }

    xBase* GetClient()
    {
        return this->clt_owner;
    }
    S32 ExpTranIsOn()
    {
        return !(this->flg_psyche & 4);
    }
    void ExpTranOff()
    {
        this->flg_psyche |= 4;
    }
    void ExpTranOn()
    {
        this->flg_psyche &= ~4;
    }
};

struct xGoal : xListItem<xGoal>, xFactoryInst
{
    xPsyche* psyche; // 0x18
    en_GOALSTATE stat; // 0x1c
    S32 flg_able; // 0x20
    xGoalProcessCallback fun_process; // 0x24
    void* cbdata; // 0x28

    xGoal()
    {
        this->flg_able = 0;
        this->stat = GOAL_STAT_UNKNOWN;
    }

    S32 GetID() const
    {
        return this->itemType;
    }

    void SetFlags(S32 flags)
    {
        flg_able = flags;
    }
    void AddFlags(S32 flags);
    xPsyche* GetPsyche() const;
    S32 GetFlags() const
    {
        return flg_able;
    }

    const char* Name()
    {
        return NULL;
    }
    void SetPsyche(xPsyche* psyche)
    {
        this->psyche = psyche;
    }
    void SetState(en_GOALSTATE state)
    {
        this->stat = state;
    }
    en_GOALSTATE GetState() const
    {
        return this->stat;
    }
    xBase* GetOwner() const;
    S32 Goal_Process(en_trantype* trantype, F32 dt, void* ctxt);

    // vtable
    virtual void Clear() = 0;

    virtual S32 Enter(F32 dt, void* updCtxt)
    {
        return 0;
    }

    virtual S32 Exit(F32 dt, void* updCtxt)
    {
        return 0;
    }

    virtual S32 Suspend(F32 dt, void* updCtxt)
    {
        return 0;
    }

    virtual S32 Resume(F32 dt, void* updCtxt)
    {
        return 0;
    }

    virtual S32 Process(en_trantype* trantype, F32 dt, void* updCtxt)
    {
        return Goal_Process(trantype, dt, updCtxt);
    }

    virtual S32 SysEvent(xBase* from, xBase* to, U32 toEvent, const F32* toParam,
                         xBase* toParamWidget, S32* handled)
    {
        return 1;
    }
};

#endif
