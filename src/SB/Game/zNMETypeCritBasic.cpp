#include "zNMETypeCritBasic.h"

void zNMECritBasic::SelfSetup()
{
    xBehaveMgr* bmgr;
    xPsyche* psy;
    bmgr = xBehaveMgr_GetSelf();
    psy_self = bmgr->Subscribe(this, 0);
    psy = psy_self;
    psy->BrainBegin();
    psy->AddGoal('NGC\0', NULL);
    psy->AddGoal('NGC\1', NULL);
    psy->AddGoal('NGC\4', NULL);
    psy->AddGoal('NGC\2', NULL);
    psy->AddGoal('NGC\3', NULL);
    psy->BrainEnd();
    psy->SetSafety(NME_GOAL_CRIT_IDLE);
}

U32 zNMECritBasic::AnimPick(S32 gid, en_npcgspot gspot, xGoal* goal)
{
    S32 idx = -1;
    U32 da_anim = 0;

    switch (gid)
    {
    case NME_GOAL_CRIT_IDLE:
        idx = 1;
        break;
    case NME_GOAL_CRIT_PATROL:
        if (gspot == NME_GSPOT_PATROLPAUSE)
        {
            idx = 1;
        }
        else
        {
            idx = 2;
        }
        break;
    case NME_GOAL_CRIT_BATTACK:
        idx = 3;
        break;
    case NME_GOAL_CRIT_DYING:
        idx = 4;
        break;
    case NME_GOAL_CRIT_DEAD:
        idx = 1;
        break;
    default:
        da_anim = 0;
        break;
    }

    if (idx >= 0)
    {
        da_anim = g_hash_critanim[idx];
    }

    return da_anim;
}

xAnimTable* zNMECritBasic::CreateAnimTable(xAnimTable* table)
{
    if (table == NULL)
    {
        table = xAnimTableNew("zNMECritBasic", 0);
    }

    zNMECritter_AnimTableAdd(table);

    return table;
}

void zNMECritBasic::Reset()
{
    zNMECommon::Reset();
    psy_self->GoalSet(NME_GOAL_CRIT_IDLE, 0);
    navnet->MvptReset(NULL);
}

void zNMECritBasic::Process(F32 dt)
{
    psy_self->Timestep(dt, NULL);
    zNMECommon::Process(dt);
}

S32 zNMECritBasic::TypeHandleMail(NMEMsg* mail)
{
    S32 handled = 1;
    xPsyche* psy = psy_self;

    switch (mail->msgid)
    {
    case NME_MID_DAMAGE:
        psy->GoalSet(NME_GOAL_CRIT_DYING, 0);
        break;
    case NME_MID_UNDERATTACK:
        psy->GoalPush(NME_GOAL_CRIT_BATTACK, 0);
        break;
    default:
        handled = 0;
        break;
    }

    return handled;
}
