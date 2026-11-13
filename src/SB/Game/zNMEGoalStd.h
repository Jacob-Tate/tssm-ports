#ifndef ZNMEGOALSTD_H
#define ZNMEGOALSTD_H

#include "zNMEGoalCommon.h"

enum en_diehow
{
    DIE_SILENT,
    DIE_DUPLOONLY,
    DIE_MUFFLED,
    DIE_QUIETLY,
    DIE_LOUDLY,
    DIE_NORMAL,
    DIE_NOMORE,
    DIE_FORCE = 0x7fffffff
};

// ---- zNMEGoalStd.h ----

enum en_loopstat
{
    LOOP_GOAL_NOT_DONE,
    LOOP_GOAL_IS_DONE
};

struct zNMEGoalPushAnim : zNMEGoalCommon
{
    S32 flg_pushanim;

    zNMEGoalPushAnim()
    {
        SetFlags(0x6);
    }

    S32 Enter(F32 dt, void* updCtxt);
    S32 Exit(F32 dt, void* updCtxt);
    S32 Resume(F32 dt, void* updCtxt);
    S32 Process(en_trantype* trantype, F32 dt, void* updCtxt);
};

struct zNMEGoalLoopAnim : zNMEGoalCommon
{
    S32 flg_loopanim;
    U32 anid_stage[3];
    S32 cnt_loop;
    U32 origAnimFlags;
    U32 animWeMolested;

    zNMEGoalLoopAnim()
    {
        SetFlags(0x2);
    }

    S32 Process(en_trantype* trantyp, F32 dt, void* ctxt)
    {
        en_loopstat lpstat;

        lpstat = GoalLoop_Process((zNMECommon*)psyche->clt_owner, dt);
        if (lpstat == LOOP_GOAL_IS_DONE)
        {
            *trantyp = GOAL_TRAN_POP;
            return 1;
        }

        return zNMEGoalCommon::Process(trantyp, dt, ctxt);
    }
    S32 Enter(F32 dt, void* updCtxt)
    {
        zNMECommon* npc = (zNMECommon*)psyche->clt_owner;

        GoalLoop_Enter(npc);
        return zNMEGoalCommon::Enter(dt, updCtxt);
    }
    S32 Exit(F32 dt, void* updCtxt)
    {
        zNMECommon* npc = (zNMECommon*)psyche->clt_owner;

        if (animWeMolested)
        {
            npc->AnimFindState(animWeMolested)->Flags = origAnimFlags;
            animWeMolested = 0;
            origAnimFlags = 0;
        }

        return xGoal::Exit(dt, updCtxt);
    }
    en_loopstat GoalLoop_Process(zNMECommon* npc, F32 dt);
    void GoalLoop_Enter(zNMECommon* npc);
    void UseDefaultAnims(zNMECommon* npc);
    void ValidateStages();
};

struct zNMEGoalDead : zNMEGoalCommon
{
    en_diehow stylinDyin;
    U16 old_moreFlags;
    S16 notUsed;

    zNMEGoalDead()
    {
        SetFlags(0x6);
    }

    // zNMEGoalStd.cpp in the PS2 build; no GC symbol (always inlined).
    void SendDeathNotice(zNMECommon* npc)
    {
        zNMECommon* duper = npc->npc_duplodude;

        switch (stylinDyin)
        {
        case DIE_SILENT:
            break;
        case DIE_DUPLOONLY:
            if (duper)
            {
                duper->DuploNotice(SM_NOTE_NMESTANDBY, npc);
            }
            break;
        case DIE_MUFFLED:
            if (duper)
            {
                duper->DuploNotice(SM_NOTE_NMEDIED, npc);
                duper->DuploNotice(SM_NOTE_NMESTANDBY, npc);
            }
            zEntEvent(npc, npc, eEventDeath);
            break;
        case DIE_QUIETLY:
            break;
        case DIE_LOUDLY:
        case DIE_NORMAL:
            if (duper)
            {
                duper->DuploNotice(SM_NOTE_NMEDIED, npc);
                duper->DuploNotice(SM_NOTE_NMESTANDBY, npc);
            }
            zEntEvent(npc, npc, eEventDeath);
            npc->GiveReward();
            break;
        }

        stylinDyin = DIE_NORMAL;
    }

    S32 Exit(F32 dt, void* ctxt)
    {
        zNMECommon* npc = (zNMECommon*)psyche->clt_owner;

        xEntShow(npc);
        npc->RestoreColFlags();
        npc->moreFlags = old_moreFlags;
        npc->model->Flags |= 0x2;
        npc->model->Flags |= 0x4;
        npc->flg_upward &= ~0x1;

        return xGoal::Exit(dt, ctxt);
    }

    S32 Enter(F32 dt, void* ctxt)
    {
        zNMECommon* npc = (zNMECommon*)psyche->clt_owner;

        old_moreFlags = npc->moreFlags;
        xEntHide(npc);
        npc->moreFlags = 0;
        npc->pflags = 0;
        npc->flg_colCheck = 0;
        npc->flg_penCheck = 0;
        npc->chkby = 0;
        npc->penby = 0;
        if (npc->model->Anim)
        {
            xAnimPlaySetState(npc->model->Anim->Single, npc->model->Anim->Table->StateList, 0.0f);
        }
        npc->model->Flags &= (U16)~0x2;
        npc->model->Flags &= (U16)~0x4;
        npc->flg_upward |= 0x1;

        SendDeathNotice(npc);

        return zNMEGoalCommon::Enter(dt, ctxt);
    }
};

struct zNMEGoalIdle : zNMEGoalCommon
{
    zNMEGoalIdle()
    {
        SetFlags(0xC);
    }

    S32 Enter(F32 dt, void* updCtxt);
    S32 Exit(F32 dt, void* updCtxt);
    S32 Suspend(F32 dt, void* updCtxt);
    S32 Resume(F32 dt, void* updCtxt);
    S32 Process(en_trantype* trantype, F32 dt, void* ctxt);
    S32 GoalHandleMail(NMEMsg* msg);
};

struct zNMEGoalPatrol : zNMEGoalCommon
{
    S32 flg_patrol;
    F32 tmr_wait;
    xVec3 pos_midpnt[4];
    S32 idx_midpnt;

    zNMEGoalPatrol()
    {
        SetFlags(0x6);
        flg_npcgauto |= 0x6;
    }

    S32 Enter(F32 dt, void* updCtxt);
    S32 Exit(F32 dt, void* updCtxt);
    S32 Resume(F32 dt, void* updCtxt);
    S32 Process(en_trantype* trantype, F32 dt, void* updCtxt);
    void DoOnArriveStuff(zNMECommon* npc);
    void MoveNormal(zNMECommon* npc, F32 dt);
    void MoveSpline(zNMECommon* npc, F32 dt);
    void Chk_AutoSmooth(zNMECommon* npc);
    void CalcSmoothPoints(zNMECommon* npc);
    void MoveAutoSmooth(zNMECommon* npc, F32 dt);
};

struct zNMEGoalWander : zNMEGoalCommon
{
    S32 flg_wand;
    F32 tmr_remain;
    F32 rad_wand;
    xVec3 pos_home;
    F32 tmr_newdir;
    xVec3 dir_cur;

    zNMEGoalWander()
    {
        SetFlags(0x6);
        flg_npcgauto |= 0x6;
        flg_wand = 0xFFFF0000;
    }

    S32 Enter(F32 dt, void* updCtxt);
    S32 Process(en_trantype* trantype, F32 dt, void* updCtxt);
    void VerticalWander(zNMECommon* npc, F32 spd_dt, const xVec3* vec_dest);
    void CalcNewDir(zNMECommon* npc);
};

struct zNMEGoalFidget : zNMEGoalPushAnim
{
    zNMEGoalFidget()
    {
        flg_npcgauto |= 0x6;
    }

    S32 Enter(F32 dt, void* updCtxt);
    S32 Exit(F32 dt, void* updCtxt);
};

struct zNMEGoalWaiting : zNMEGoalLoopAnim
{
    F32 tmr_waiting;

    zNMEGoalWaiting()
    {
    }

    S32 Enter(F32 dt, void* updCtxt);
    S32 Resume(F32 dt, void* updCtxt);
    S32 Exit(F32 dt, void* updCtxt);
    S32 Process(en_trantype* trantype, F32 dt, void* ctxt);
};

struct zNMEGoalNoManLand : zNMEGoalCommon
{
    zNMEGoalNoManLand()
    {
    }
};

struct zNMEGoalLimbo : zNMEGoalDead
{
    zNMEGoalLimbo()
    {
    }

    S32 Enter(F32 dt, void* updCtxt);
    S32 GoalHandleMail(NMEMsg* mail);
};

struct zNMEGoalDEVAnimView : zNMEGoalCommon
{
    U32 origAnimFlags;
    U32 animWeMolested;
    F32 ang_facing;

    zNMEGoalDEVAnimView()
    {
    }

    // No GC symbols (always inlined); BfBB's zNPCGoalDEVAnimSpin::ASTMolestAnim/ASTUnmolestAnim,
    // but taking the caller's npc (Enter reuses its model->Anim for the unmolest lookup).
    void ASTMolestAnim(zNMECommon* npc, xAnimState* ast)
    {
        if (animWeMolested)
        {
            ASTUnmolestAnim(npc);
        }

        origAnimFlags = ast->Flags;
        animWeMolested = ast->ID;
        ast->Flags &= ~0x20;
        ast->Flags |= 0x10;
    }
    void ASTUnmolestAnim(zNMECommon* npc)
    {
        if (animWeMolested)
        {
            npc->AnimFindState(animWeMolested)->Flags = origAnimFlags;
            animWeMolested = 0;
            origAnimFlags = 0;
        }
    }

    S32 Enter(F32 dt, void* updCtxt);
    S32 Exit(F32 dt, void* updCtxt);
    S32 Process(en_trantype* trantype, F32 dt, void* ctxt);
};

struct zNMEGoalDEVHero : zNMEGoalCommon
{
    zNMEGoalDEVHero()
    {
    }

    S32 Enter(F32 dt, void* updCtxt);
    S32 Exit(F32 dt, void* updCtxt);
    S32 Process(en_trantype* trantype, F32 dt, void* updCtxt);
};

#endif
