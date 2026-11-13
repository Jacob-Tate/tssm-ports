#include "zNMETypeTurPopup.h"

void zNMETurPopup::SelfSetup()
{
    xBehaveMgr* bmgr;
    xPsyche* psy;

    bmgr = xBehaveMgr_GetSelf();
    psy_self = bmgr->Subscribe(this, 0);

    psy = psy_self;
    psy->BrainBegin();

    psy->AddGoal(NME_GOAL_TURR_PDORMANT, NULL);
    psy->AddGoal(NME_GOAL_TURR_PALERT, NULL);
    psy->AddGoal(NME_GOAL_TURR_PPATALPHA, NULL);
    psy->AddGoal(NME_GOAL_TURR_DEAD, NULL);

    psy->BrainEnd();
    psy->SetSafety(NME_GOAL_TURR_PDORMANT);
}

U32 zNMETurPopup::AnimPick(S32 gid, en_npcgspot gspot, xGoal* goal)
{
    en_turranim idx;

    switch (gid)
    {
    case NME_GOAL_TURR_PDORMANT:
        if (gspot == NME_GSPOT_START)
        {
            idx = TURR_ANIM_POPDOWN;
        }
        else if (gspot == NME_GSPOT_FINISH)
        {
            idx = TURR_ANIM_POPUP;
        }
        else
        {
            idx = TURR_ANIM_POPUP;
        }
        break;
    case NME_GOAL_TURR_PALERT:
        idx = TURR_ANIM_ACTIVE;
        break;
    case NME_GOAL_TURR_PPATALPHA:
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
        idx = TURR_ANIM_INACTIVE;
        break;
    }

    return g_hash_turranim[idx];
}

xAnimTable* zNMETurPopup::CreateAnimTable(xAnimTable* table)
{
    if (table == NULL)
    {
        table = xAnimTableNew("zNMETurPopup", 0);
    }

    xAnimTableNewState(table, g_strz_turranim[TURR_ANIM_INACTIVE], 0x10, 1, 1.0f, NULL, NULL, 0.0f,
                       NULL, NULL, xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, g_strz_turranim[TURR_ANIM_ACTIVE], 0x10, 1, 1.0f, NULL, NULL, 0.0f,
                       NULL, NULL, xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, g_strz_turranim[TURR_ANIM_POPUP], 0, 1, 1.0f, NULL, NULL, 0.0f, NULL,
                       NULL, xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, g_strz_turranim[TURR_ANIM_POPDOWN], 0x20, 1, 1.0f, NULL, NULL, 0.0f,
                       NULL, NULL, xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, g_strz_turranim[TURR_ANIM_SHOOT_PREP], 0x20, 1, 1.0f, NULL, NULL,
                       0.0f, NULL, NULL, xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, g_strz_turranim[TURR_ANIM_SHOOT_LOOP], 0x10, 1, 1.0f, NULL, NULL,
                       0.0f, NULL, NULL, xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, g_strz_turranim[TURR_ANIM_SHOOT_DONE], 0, 1, 1.0f, NULL, NULL, 0.0f,
                       NULL, NULL, xAnimDefaultBeforeEnter, NULL, NULL);

    S32 ourAnims[8] = { TURR_ANIM_INACTIVE,   TURR_ANIM_ACTIVE,     TURR_ANIM_POPDOWN,
                        TURR_ANIM_POPUP,      TURR_ANIM_SHOOT_PREP, TURR_ANIM_SHOOT_LOOP,
                        TURR_ANIM_SHOOT_DONE, TURR_ANIM_UNKNOWN };
    NMEC_BuildStandardAnimTran(table, g_strz_turranim, ourAnims, TURR_ANIM_POPDOWN, 0.2f);

    xAnimTableNewTransition(table, g_strz_turranim[TURR_ANIM_SHOOT_PREP],
                            g_strz_turranim[TURR_ANIM_SHOOT_LOOP], NULL, NULL, 0x10, 0, 0.0f, 0.0f,
                            0, 0, 0.2f, NULL);
    xAnimTableNewTransition(table, g_strz_turranim[TURR_ANIM_POPDOWN],
                            g_strz_turranim[TURR_ANIM_INACTIVE], NULL, NULL, 0x10, 0, 0.0f, 0.0f, 0,
                            0, 0.2f, NULL);

    return table;
}

void zNMETurPopup::Reset()
{
    zNMETurret::Reset();
    psy_self->GoalSet(NME_GOAL_TURR_PDORMANT, 0);
}

void zNMETurPopup::Process(F32 dt)
{
    psy_self->Timestep(dt, NULL);

    xModelSetScale(model, 0.45f);

    zNMETurret::Process(dt);
}

S32 zNMETurPopup::TypeHandleMail(NMEMsg* mail)
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

S32 zNMEGoalTurrPDormant::Process(en_trantype* trantype, F32 dt, void* ctxt)
{
    zNMETurret* npc = (zNMETurret*)psyche->clt_owner;
    S32 nextgoal = 0;

    if (npc->XZDstSqToPlayer(NULL, NULL) < 225.0f)
    {
        *trantype = GOAL_TRAN_SET;
        nextgoal = NME_GOAL_TURR_PALERT;
    }

    switch (*trantype)
    {
    case GOAL_TRAN_NONE:
        nextgoal = zNMEGoalCommon::Process(trantype, dt, ctxt);
        break;
    }

    return nextgoal;
}

S32 zNMEGoalTurrPAlert::Enter(F32 dt, void* updCtxt)
{
    tmr_shootDelay = 5.0f;
    return zNMEGoalCommon::Enter(dt, updCtxt);
}

S32 zNMEGoalTurrPAlert::Resume(F32 dt, void* updCtxt)
{
    tmr_shootDelay = 5.0f;
    return zNMEGoalCommon::Resume(dt, updCtxt);
}

S32 zNMEGoalTurrPAlert::Process(en_trantype* trantype, F32 dt, void* ctxt)
{
    zNMETurret* npc = (zNMETurret*)psyche->clt_owner;
    S32 nextgoal = 0;
    xVec3 dir_plyr;

    if (npc->XZDstSqToPlayer(&dir_plyr, NULL) > 400.0f)
    {
        *trantype = GOAL_TRAN_SET;
        nextgoal = NME_GOAL_TURR_PDORMANT;
    }
    else if (tmr_shootDelay < 0.0f)
    {
        *trantype = GOAL_TRAN_PUSH;
        nextgoal = NME_GOAL_TURR_PPATALPHA;
    }

    switch (*trantype)
    {
    case GOAL_TRAN_NONE:
        tmr_shootDelay = MAX(-1.0f, tmr_shootDelay - dt);
        nextgoal = zNMEGoalCommon::Process(trantype, dt, ctxt);
        break;
    }

    return nextgoal;
}
