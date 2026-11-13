#include "zNMETypeTurTurner.h"

void zNMETurTurner::SelfSetup()
{
    xBehaveMgr* bmgr;
    xPsyche* psy;

    bmgr = xBehaveMgr_GetSelf();
    psy_self = bmgr->Subscribe(this, 0);

    psy = psy_self;
    psy->BrainBegin();

    psy->AddGoal(NME_GOAL_TURR_TREADY, NULL);
    psy->AddGoal(NME_GOAL_TURR_TTURN, NULL);
    psy->AddGoal(NME_GOAL_TURR_TSHOOT, NULL);
    psy->AddGoal(NME_GOAL_TURR_DEAD, NULL);

    psy->BrainEnd();
    psy->SetSafety(NME_GOAL_TURR_TREADY);
}

U32 zNMETurTurner::AnimPick(S32 gid, en_npcgspot gspot, xGoal* goal)
{
    S32 idx = -1;
    U32 da_anim = 0;

    switch (gid)
    {
    case NME_GOAL_TURR_TREADY:
        idx = TURR_ANIM_INACTIVE;
        break;
    case NME_GOAL_TURR_TTURN:
        idx = TURR_ANIM_ACTIVE;
        break;
    case NME_GOAL_TURR_TSHOOT:
        if (gspot == NME_GSPOT_START)
        {
            idx = TURR_ANIM_SHOOT_PREP;
        }
        else if (gspot == NME_GSPOT_RESUME)
        {
            idx = TURR_ANIM_SHOOT_PREP;
        }
        else if (gspot == NME_GSPOT_LOOP)
        {
            idx = TURR_ANIM_SHOOT_LOOP;
        }
        else if (gspot == NME_GSPOT_FINISH)
        {
            idx = TURR_ANIM_SHOOT_DONE;
        }
        else
        {
            idx = TURR_ANIM_SHOOT_LOOP;
        }
        break;
    case NME_GOAL_TURR_DEAD:
        idx = TURR_ANIM_INACTIVE;
        break;
    default:
        da_anim = g_hash_turranim[TURR_ANIM_INACTIVE];
        break;
    }

    if (idx >= 0)
    {
        da_anim = g_hash_turranim[idx];
    }

    return da_anim;
}

xAnimTable* zNMETurTurner::CreateAnimTable(xAnimTable* table)
{
    if (table == NULL)
    {
        table = xAnimTableNew("zNMETurTurner", 0);
    }

    xAnimTableNewState(table, g_strz_turranim[TURR_ANIM_INACTIVE], 0x10, 1, 1.0f, NULL, NULL, 0.0f,
                       NULL, NULL, xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, g_strz_turranim[TURR_ANIM_ACTIVE], 0x10, 1, 1.0f, NULL, NULL, 0.0f,
                       NULL, NULL, xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, g_strz_turranim[TURR_ANIM_SHOOT_PREP], 0x20, 1, 1.0f, NULL, NULL,
                       0.0f, NULL, NULL, xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, g_strz_turranim[TURR_ANIM_SHOOT_LOOP], 0x10, 1, 1.0f, NULL, NULL,
                       0.0f, NULL, NULL, xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, g_strz_turranim[TURR_ANIM_SHOOT_DONE], 0x20, 1, 1.0f, NULL, NULL,
                       0.0f, NULL, NULL, xAnimDefaultBeforeEnter, NULL, NULL);

    S32 ourAnims[6] = { TURR_ANIM_INACTIVE,   TURR_ANIM_ACTIVE,     TURR_ANIM_SHOOT_PREP,
                        TURR_ANIM_SHOOT_LOOP, TURR_ANIM_SHOOT_DONE, TURR_ANIM_UNKNOWN };
    NMEC_BuildStandardAnimTran(table, g_strz_turranim, ourAnims, 1, 0.2f);

    xAnimTableNewTransition(table, g_strz_turranim[TURR_ANIM_SHOOT_PREP],
                            g_strz_turranim[TURR_ANIM_SHOOT_LOOP], NULL, NULL, 0x10, 0, 0.0f, 0.0f,
                            0, 0, 0.2f, NULL);
    xAnimTableNewTransition(table, g_strz_turranim[TURR_ANIM_SHOOT_DONE],
                            g_strz_turranim[TURR_ANIM_ACTIVE], NULL, NULL, 0x10, 0, 0.0f, 0.0f, 0,
                            0, 0.2f, NULL);

    return table;
}

void zNMETurTurner::Reset()
{
    zNMETurret::Reset();
    psy_self->GoalSet(NME_GOAL_TURR_TREADY, 0);
}

void zNMETurTurner::Process(F32 dt)
{
    psy_self->Timestep(dt, NULL);
    zNMETurret::Process(dt);
}

S32 zNMETurTurner::TypeHandleMail(NMEMsg* mail)
{
    S32 handled = 1;
    xPsyche* psy = psy_self;

    switch (mail->msgid)
    {
    case NME_MID_DAMAGE:
        if (psy != NULL)
        {
            psy->GoalSet(NME_GOAL_TURR_DEAD, 0);
        }
        break;
    default:
        handled = 0;
        break;
    }

    return handled;
}

S32 zNMEGoalTurrTReady::Process(en_trantype* trantype, F32 dt, void* ctxt)
{
    zNMETurret* npc = (zNMETurret*)psyche->clt_owner;
    S32 nextgoal = 0;

    if (npc->XZDstSqToPlayer(NULL, NULL) < 225.0f)
    {
        *trantype = GOAL_TRAN_SET;
        nextgoal = NME_GOAL_TURR_TTURN;
    }

    switch (*trantype)
    {
    case GOAL_TRAN_NONE:
        nextgoal = zNMEGoalCommon::Process(trantype, dt, ctxt);
        break;
    }

    return nextgoal;
}

S32 zNMEGoalTurrTTurn::Enter(F32 dt, void* updCtxt)
{
    tmr_shootDelay = 5.0f;
    return zNMEGoalCommon::Enter(dt, updCtxt);
}

S32 zNMEGoalTurrTTurn::Resume(F32 dt, void* updCtxt)
{
    tmr_shootDelay = 5.0f;
    return zNMEGoalCommon::Resume(dt, updCtxt);
}

S32 zNMEGoalTurrTTurn::Process(en_trantype* trantype, F32 dt, void* ctxt)
{
    zNMETurret* npc = (zNMETurret*)psyche->clt_owner;
    S32 nextgoal = 0;
    xVec3 dir_plyr;
    F32 dsq = npc->XZDstSqToPlayer(&dir_plyr, NULL);

    if (dsq > 400.0f)
    {
        *trantype = GOAL_TRAN_SET;
        nextgoal = NME_GOAL_TURR_TREADY;
    }
    else if (tmr_shootDelay < 0.0f)
    {
        *trantype = GOAL_TRAN_PUSH;
        nextgoal = NME_GOAL_TURR_TSHOOT;
    }

    switch (*trantype)
    {
    case GOAL_TRAN_NONE:
        tmr_shootDelay = MAX(-1.0f, tmr_shootDelay - dt);
        if (dsq > 1.0f)
        {
            xVec3SMulBy(&dir_plyr, 1.0f / xsqrt(dsq));
            npc->MatQRotTowards(dt, &dir_plyr, PI / 4);
        }
        nextgoal = zNMEGoalCommon::Process(trantype, dt, ctxt);
        break;
    }

    return nextgoal;
}
