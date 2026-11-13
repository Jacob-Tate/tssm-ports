#include "zNMETypeBuckOTron.h"
#include "zNMESpawner.h"
#include "zFX.h"

U32 g_hash_buckanim[6];
char* g_strz_buckanim[6] = {
    "Unknown", "Idle", "Attack", "Attack_Left", "Attack_Right", "Defeat",
};

namespace Buckotron
{
    NMEHelpBound::BndData bnd_info = {
        k_XBOUNDTYPE_BOX,
        { 0, 0, { 0, 0 } },
        { 1.0f, { 0.0f, 1.0f, 0.0f } },
        { { 2.75f, 5.0f, 2.75f }, { 0.0f, 2.5f, 0.0f } },
    };

    namespace Explode
    {
        static const xVec3 vec_offset = { 0.0f, 1.1f, 0.0f };
    } // namespace Explode
} // namespace Buckotron

// No GC symbol: inlined into zNMECentral_Startup (its unused copy is dead-stripped).
void ZNME_BuckOTron_Startup()
{
    for (S32 i = 0; i < 6; i++)
    {
        g_hash_buckanim[i] = xStrHash(g_strz_buckanim[i]);
    }
}

U32 zNMEBuckotron::AnimPick(S32 gid, en_npcgspot gspot, xGoal* goal)
{
    S32 idx = -1;
    U32 da_anim = 0;

    switch (gid)
    {
    case NME_GOAL_BUCK_RUNNING:
        idx = 1; // Idle
        break;
    case NME_GOAL_BUCK_BIRTHING:
    {
        // Alternate between the two attack anims.
        static S32 toggle = 0;
        idx = toggle ? 3 : 4; // Attack_Left : Attack_Right
        toggle = !toggle;
        break;
    }
    case NME_GOAL_BUCK_DYING:
        idx = 5; // Defeat
        break;
    default:
        idx = 1; // Idle
        break;
    }

    if (idx >= 0)
    {
        da_anim = g_hash_buckanim[idx];
    }

    return da_anim;
}

xAnimTable* zNMEBuckotron::CreateAnimTable(xAnimTable* table)
{
    if (table == NULL)
    {
        table = xAnimTableNew("zNMEBuckotron", 0);
    }

    xAnimTableNewState(table, g_strz_buckanim[1], 0x10, 1, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, g_strz_buckanim[3], 0, 1, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, g_strz_buckanim[4], 0, 1, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, g_strz_buckanim[5], 0, 1, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);

    S32 ourAnims[5] = { 1, 5, 3, 4, 0 }; // Idle, Defeat, Attack_Left, Attack_Right, Unknown
    NMEC_BuildStandardAnimTran(table, g_strz_buckanim, ourAnims, 1, 0.2f);

    return table;
}

void zNMEBuckotron::SelfSetup()
{
    xBehaveMgr* bmgr;
    xPsyche* psy;

    bmgr = xBehaveMgr_GetSelf();
    psy_self = bmgr->Subscribe(this, 0);

    psy = psy_self;
    psy->BrainBegin();

    psy->AddGoal(NME_GOAL_BUCK_RUNNING, NULL);
    psy->AddGoal(NME_GOAL_BUCK_BIRTHING, NULL);
    psy->AddGoal(NME_GOAL_BUCK_DYING, NULL);
    psy->AddGoal(NME_GOAL_BUCK_DEAD, NULL);

    psy->BrainEnd();
    psy->SetSafety(NME_GOAL_BUCK_RUNNING);
}

S32 zNMEBuckotron::DfltVulnFlags()
{
    return 0xCFFFFFF7;
}

void zNMEBuckotron::Init(xEntAsset* asset)
{
    zNMECommon::Init(asset);
    zNMEParFX_PartyInvite(NPAR_TYP_DUSTPUFF, NULL, 0);
}

void zNMEBuckotron::Setup()
{
    zNMECommon::Setup();
    SetupSpawning();
}

void zNMEBuckotron::BUpdate(xVec3* pos)
{
    Buckotron::bnd_info.UpdateBound(&bound, this, pos);
}

void zNMEBuckotron::Process(F32 dt)
{
    spawner->Timestep(dt);
    psy_self->Timestep(dt, NULL);
    zNMECommon::Process(dt);
}

void zNMEBuckotron::SetupSpawning()
{
    zNMEAssetBucketOTron* buckass = (zNMEAssetBucketOTron*)nmeass;
    xBase* grp_spawn;
    en_wavemode wm;

    spawner = zNMESpawner_GetInstance();
    spawner->Subscribe(this);

    grp_spawn = zSceneFindObject(buckass->aid_spawnGroup);
    ParseSpawnItem(grp_spawn);

    switch (buckass->spawnMode)
    {
    case NME_SPAWNMODE_CONTINUOUS:
        wm = SM_WAVE_CONTINUOUS;
        break;
    case NME_SPAWNMODE_WAVES:
        wm = SM_WAVE_DISCREET;
        break;
    case NME_SPAWNMODE_AMBUSHWAVE:
        wm = SM_WAVE_AMBUSHWAVE;
        break;
    case NME_SPAWNMODE_AMBUSHCONT:
        wm = SM_WAVE_AMBUSHCONT;
        break;
    default:
        wm = SM_WAVE_CONTINUOUS;
        break;
    }

    spawner->SetWaveMode(wm, buckass->spawnDelay, buckass->maxSpawn);
}

// The compiled-out asserts (conditions unknown) keep this recursion from being self-inlined one
// level deeper than in retail.
void zNMEBuckotron::ParseSpawnItem(xBase* baseitem)
{
    zNMECommon* nme;
    zMovePoint* mvpt;
    xGroup* grp;
    S32 cnt;
    S32 i;
    xBase* grpitem;

    switch (baseitem->baseType)
    {
    case eBaseTypeMovePoint:
        mvpt = (zMovePoint*)baseitem;
        spawner->AddSpawnPoint(mvpt);
        xASSERT_PLACEHOLDER();
        break;
    case eBaseTypeNPC:
        nme = (zNMECommon*)baseitem;
        spawner->AddSpawnNPC(nme);
        xASSERT_PLACEHOLDER();
        mvpt = nme->MvptFirstFollow();
        if (mvpt != NULL)
        {
            ParseSpawnItem(mvpt);
        }
        break;
    case eBaseTypeGroup:
        grp = (xGroup*)baseitem;
        cnt = xGroupGetCount(grp);
        for (i = 0; i < cnt; i++)
        {
            grpitem = xGroupGetItemPtr(grp, i);
            if (grpitem != NULL)
            {
                ParseSpawnItem(grpitem);
            }
        }
        break;
    }
}

void zNMEBuckotron::Reset()
{
    zNMEGoalBuckDead* goal;

    zNMECommon::Reset();

    spawner->Reset();

    flg_vuln = DfltVulnFlags();

    if (((zNMEAssetBucketOTron*)nmeass)->flg_buckass & 0x1)
    {
        DuploNotice(SM_NOTE_DUPPAUSE, NULL);
    }

    goal = (zNMEGoalBuckDead*)psy_self->FindGoal(NME_GOAL_BUCK_DEAD);
    goal->stylinDyin = DIE_LOUDLY;

    psy_self->GoalSet(NME_GOAL_BUCK_RUNNING, 1);
}

void zNMEBuckotron::DuploNotice(en_smnote note, void* data)
{
    spawner->Notify(note, data);
}

S32 zNMEBuckotron::TypeHandleMail(NMEMsg* mail)
{
    S32 handled = 1;

    switch (mail->msgid)
    {
    case NME_MID_DAMAGE:
        if (IsPresent())
        {
            psy_self->GoalSet(NME_GOAL_BUCK_DYING, 0);
        }
        else
        {
            psy_self->GoalSet(NME_GOAL_BUCK_DEAD, 0);
        }
        break;
    case NME_MID_SYSEVENT:
        handled = Buck_SysMail(&mail->sysevent);
        break;
    default:
        handled = 0;
        break;
    }

    return handled;
}

S32 zNMEBuckotron::Buck_SysMail(NMESysEvent* sysmail)
{
    S32 handled = 1;

    switch (sysmail->toEvent)
    {
    case eEventDuploWaveBegin:
    case eEventDuploWaveComplete:
    case eEventDuploNPCBorn:
    case eEventDuploNPCKilled:
    case eEventDuploExpiredMaxNPC:
        break;
    case eEventDuploKillKids:
        DuploNotice(SM_NOTE_KILLKIDS, NULL);
        break;
    case eEventDuploPause:
        DuploNotice(SM_NOTE_DUPPAUSE, NULL);
        break;
    case eEventDuploResume:
        DuploNotice(SM_NOTE_DUPRESUME, NULL);
        break;
    case eEventVisible:
        handled = DfltSysEventMail(sysmail);
        if (IsPresent() && psy_self->GIDOfActive() == NME_GOAL_BUCK_RUNNING)
        {
            SndPlayFromTable(0); // eNMESound_Idle
        }
        break;
    case eEventInvisible:
        if (snd_table->sndGroupHandle[0] != -1)
        {
            xSndMgrStop(sndID[0]);
            sndID[0] = (iSndHandle)-1;
        }
        handled = 0;
        break;
    case eEventNPCSetActiveOff:
        DuploNotice(SM_NOTE_DUPPAUSE, NULL);
        if (snd_table->sndGroupHandle[0] != -1)
        {
            xSndMgrStop(sndID[0]);
            sndID[0] = (iSndHandle)-1;
        }
        handled = 0;
        break;
    case eEventNPCSetActiveOn:
        handled = DfltSysEventMail(sysmail);
        if (IsPresent() && psy_self->GIDOfActive() == NME_GOAL_BUCK_RUNNING)
        {
            SndPlayFromTable(0); // eNMESound_Idle
        }
        break;
    default:
        handled = 0;
        break;
    }

    return handled;
}

// Placeholder for dead-stripped code: retail's .sdata2 keeps an unused 0.07f from this part of the
// file, probably from stripped code that blinked the lamps (tmr_lamps, cnt_lampsync and
// idx_currLamp are otherwise unused). The body is unknown.
static void __deadstripped_zNMETypeBuckOTron(F32* out)
{
    out[0] = 0.07f;
}

S32 zNMEGoalBuckRunning::Enter(F32 dt, void* updCtxt)
{
    zNMEBuckotron* npc = (zNMEBuckotron*)psyche->clt_owner;

    if (npc->IsPresent() && npc->psy_self->GIDOfActive() == NME_GOAL_BUCK_RUNNING)
    {
        npc->SndPlayFromTable(0); // eNMESound_Idle
    }

    return zNMEGoalCommon::Enter(dt, updCtxt);
}

S32 zNMEGoalBuckRunning::Exit(F32 dt, void* updCtxt)
{
    zNMEBuckotron* npc = (zNMEBuckotron*)psyche->clt_owner;

    if (npc->snd_table->sndGroupHandle[0] != -1)
    {
        xSndMgrStop(npc->sndID[0]);
        npc->sndID[0] = (iSndHandle)-1;
    }

    return 0;
}

S32 zNMEGoalBuckRunning::GoalHandleMail(NMEMsg* msg)
{
    S32 handled = 0;

    switch (msg->msgid)
    {
    case NME_MID_SYSEVENT:
        switch (msg->sysevent.toEvent)
        {
        case eEventDuploNPCBorn:
            psyche->GoalPush(NME_GOAL_BUCK_BIRTHING, 0);
            handled = 1;
            break;
        default:
            handled = 0;
            break;
        }
        break;
    }

    return handled;
}

S32 zNMEGoalBuckBirthing::Enter(F32 dt, void* updCtxt)
{
    ((zNMEBuckotron*)psyche->clt_owner)->SndPlayFromTable(7);
    return zNMEGoalCommon::Enter(dt, updCtxt);
}

S32 zNMEGoalBuckBirthing::Process(en_trantype* trantype, F32 dt, void* ctxt)
{
    BubsieSudsie((zNMEBuckotron*)psyche->clt_owner);
    return zNMEGoalPushAnim::Process(trantype, dt, ctxt);
}

void zNMEGoalBuckBirthing::BubsieSudsie(zNMEBuckotron* npc)
{
    U32 anid_animCurr = npc->AnimCurStateID();
    xVec3 vec_offsetLeft = { 0.0f, 0.0f, 0.0f };
    xVec3 vec_offsetRhyt = { 0.0f, 0.0f, 0.0f };
    xMat4x3* mat_bone;
    xVec3 pos_emit;
    xMat4x3* mat_root;

    if (anid_animCurr == g_hash_buckanim[3]) // Attack_Left
    {
        mat_bone = (xMat4x3*)&npc->model->Mat[8];
        pos_emit = vec_offsetLeft;
    }
    else
    {
        mat_bone = (xMat4x3*)&npc->model->Mat[8];
        pos_emit = vec_offsetRhyt;
    }

    xMat4x3Toworld(&pos_emit, mat_bone, &pos_emit);

    mat_root = (xMat4x3*)npc->model->Mat;
    xMat4x3Toworld(&pos_emit, mat_root, &pos_emit);

    zFX_SpawnBubbleTrail(&pos_emit, 3);
}

S32 zNMEGoalBuckDying::Enter(F32 dt, void* updCtxt)
{
    zNMEBuckotron* npc = (zNMEBuckotron*)psyche->clt_owner;

    npc->DuploNotice(SM_NOTE_DUPDEAD, this);

    if (npc->snd_table->sndGroupHandle[0] != -1)
    {
        xSndMgrStop(npc->sndID[0]);
        npc->sndID[0] = (iSndHandle)-1;
    }

    if (npc->IsPresent())
    {
        npc->SndPlayFromTable(8); // eNMESound_Special2
    }

    return zNMEGoalCommon::Enter(dt, updCtxt);
}

S32 zNMEGoalBuckDying::Exit(F32 dt, void* updCtxt)
{
    zNMEBuckotron* npc = (zNMEBuckotron*)psyche->clt_owner;

    if (npc->IsPresent())
    {
        npc->DeathFXKick();

        zNMEHazard* haz = HAZ_Acquire();
        if (haz != NULL)
        {
            if (!haz->ConfigHelper(HAZ_TYP_BUCKOBOOM_V1))
            {
                haz->Discard();
            }
            else
            {
                haz->npc_owner = npc;
                xVec3 pos = Buckotron::Explode::vec_offset + *(xVec3*)&npc->model->Mat->pos;
                haz->Start(&pos, -1.0f);
            }
        }
    }

    return 0;
}

S32 zNMEGoalBuckDying::Process(en_trantype* trantype, F32 dt, void* ctxt)
{
    zNMEBuckotron* npc = (zNMEBuckotron*)psyche->clt_owner;

    F32 tym_animCurr = npc->AnimTimeCurrent();
    if (tym_animCurr + dt >= npc->AnimDuration(NULL))
    {
        *trantype = GOAL_TRAN_SET;
        return NME_GOAL_BUCK_DEAD;
    }

    SDS_Countdown(npc, dt);
    return zNMEGoalCommon::Process(trantype, dt, ctxt);
}

S32 zNMEGoalBuckDying::GoalHandleMail(NMEMsg* mail)
{
    return 1;
}

// Weak in retail: bubbles stream from the dying Buck-O-Tron, swirling around it and sinking as the
// death anim plays out.
inline void FuriousBubbles(zNMECommon* npc)
{
    // Statics from the PS2 DWARF. Retail folds them away, but they set the numbering of the
    // statics after them.
    static const U32 num_bubbles = 16;
    static const F32 rad_outer = 5.0f;

    F32 tym = npc->AnimTimeCurrent();
    F32 dur = npc->AnimDuration(NULL);

    if (dur < 0.00001f)
    {
        return;
    }

    F32 pam = MAX(0.0f, MIN(tym / dur, 1.0f));

    static F32 hyt_descend = Buckotron::bnd_info.box.dim_bound.y;

    // 3.0f is the PS2 DWARF's static num_revolve, folded away like num_bubbles.
    F32 ang = xDangleClamp(PI - 3.0f * (2.0f * PI * pam));
    F32 cs = icos(ang);
    F32 sn = isin(ang);

    xMat4x3* mat = (xMat4x3*)npc->model->Mat;
    F32 drop = hyt_descend * (1.0f - (1.0f - (1.0f - pam) * SQ(1.0f - pam)));

    xVec3 dir = mat->at * -cs + mat->right * sn;
    xVec3 pos_from = mat->pos;
    xVec3 pos_to = mat->pos + dir * rad_outer;
    pos_from.y += drop;
    pos_to.y += drop;

    zFX_SpawnBubbleTrail(&pos_from, &pos_to, num_bubbles, NULL, NULL);
}

void zNMEGoalBuckDying::SDS_Countdown(zNMEBuckotron* npc, F32 dt)
{
    FuriousBubbles(npc);
}

S32 zNMEGoalBuckDead::Enter(F32 dt, void* updCtxt)
{
    if (!((zNMEBuckotron*)psyche->clt_owner)->IsPresent())
    {
        stylinDyin = DIE_SILENT;
    }

    return zNMEGoalDead::Enter(dt, updCtxt);
}

S32 zNMEGoalBuckDead::GoalHandleMail(NMEMsg* mail)
{
    return 1;
}
