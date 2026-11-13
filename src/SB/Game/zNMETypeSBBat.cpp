#include "zWadNME.h"

// Dennis animation choices per action (indices into g_strz_dennanim: Idle01-04, LeapLoop01-04,
// Attack01-04, Taunt01-04, Damage01-04). No GC code reads them, but retail's .data keeps them.
static S32 g_choices_Idle[4] = { 1, 2, 3, 4 };
static S32 g_choices_Leap[4] = { 8, 9, 10, 11 };
static S32 g_choices_Attack[4] = { 12, 13, 14, 15 };
static S32 g_choices_Taunt[4] = { 16, 17, 18, 19 };
static S32 g_choices_Damage[4] = { 24, 25, 26, 27 };

xAnimTable* zNMESBBat::CreateAnimTable(xAnimTable* table)
{
    if (table == NULL)
    {
        table = xAnimTableNew("zNMESBBat", 0);
    }

    CreateDennisAndSBBatAnimTable(table);
    return table;
}

U32 zNMESBBat::AnimPick(S32 gid, en_npcgspot gspot, xGoal* goal)
{
    U32 da_anim;

    if (npc_bossman == NULL)
    {
        return g_hash_dennanim[1];
    }

    da_anim = npc_bossman->AnimCurStateID();

    switch (gid)
    {
    case NME_GOAL_DENNIS_NORMAL:
        return da_anim;
    case NME_GOAL_DENNIS_EVADE:
        return da_anim;
    case NME_GOAL_DENNIS_BATTLE:
        return da_anim;
    case NME_GOAL_DENNIS_ATTACK:
        return da_anim;
    case NME_GOAL_DENNIS_TAUNT:
        return da_anim;
    case NME_GOAL_DENNIS_DAMAGE:
        return da_anim;
    }

    return da_anim;
}

void zNMESBBat::SelfSetup()
{
    xBehaveMgr* bmgr;
    xPsyche* psy;

    bmgr = xBehaveMgr_GetSelf();
    psy_self = bmgr->Subscribe(this, 0);

    psy = psy_self;
    psy->BrainBegin();

    psy->AddGoal(NME_GOAL_SBBAT_IDLE, NULL);
    psy->AddGoal(NME_GOAL_SBBAT_DEAD, NULL);

    psy->BrainEnd();

    psy->SetSafety(NME_GOAL_SBBAT_IDLE);
}

void zNMESBBat::Init(xEntAsset* asset)
{
    zNMECommon::Init(asset);

    update = NULL;
}

void zNMESBBat::Setup()
{
    zNPCMgr* npcmgr;
    char* nam_bossman;
    zNPC::base* nb;

    zNMECommon::Setup();

    npcmgr = zNPCMgrSelf();
    nam_bossman = "DENNIS_ENEMY_01";
    nb = npcmgr->FindNPC(xStrHash(nam_bossman));
    npc_bossman = (zNMEDennis*)nb;
}

void zNMESBBat::Process(F32 dt)
{
    psy_self->Timestep(dt, NULL);
    zNMECommon::Process(dt);
}

void zNMESBBat::Reset()
{
    zNMECommon::Reset();

    psy_self->GoalSet(NME_GOAL_SBBAT_IDLE, 1);
}

S32 zNMEGoalSBBatIdle::Process(en_trantype* trantype, F32 dt, void* updCtxt)
{
    zNMESBBat* npc = (zNMESBBat*)psyche->clt_owner;

    npc->FollowTheBoss();
    return 0;
}

S32 zNMEGoalSBBatDead::Enter(F32 dt, void* updCtxt)
{
    stylinDyin = DIE_SILENT;

    return zNMEGoalDead::Enter(dt, updCtxt);
}
