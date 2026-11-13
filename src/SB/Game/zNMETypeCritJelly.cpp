#include "zNMETypeCritJelly.h"
#include "zNMESupport.h"
#include "zNMESupplement.h"
#include "zNMEShrapnel.h"
#include "zLightning.h"
#include "xMath.h"
#include "xAntenna.h"

#include <string.h>

namespace Jelly
{
    NMEHelpBound::BndData bnd_info = {
        k_XBOUNDTYPE_SPHERE,
        { 0, 0, { 0, 0 } },
        { 0.4f, { 0.0f, 0.0f, 0.0f } },
        { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } },
    };
    static NMEShadParms cfg_shadparm_jelly = { { 0.75f, "shadow_round", NULL },
                                               { 0 },
                                               { 35.0f, 20.0f } };

    namespace Danger
    {
        // Leg bones the warning zaps of DangerSigns start from, in turn.
        static const S32 idx_boneLegs[4] = { 8, 10, 12, 14 };
    } // namespace Danger
} // namespace Jelly

void zNMECritJelly::CreateConfig(const char* name)
{
    zNMECommon::CreateConfig(name);
    cfg_shadparm = &Jelly::cfg_shadparm_jelly;

    NMEShadParms* shadparm = cfg_shadparm;
    if (shadparm->simpshad.nam_shadowTexture == NULL)
    {
        shadparm->simpshad.rast_shadow = NULL;
    }
    else
    {
        shadparm->simpshad.rast_shadow = NMEC_FindRWRaster(shadparm->simpshad.nam_shadowTexture);
    }
}

S32 zNMECritJelly::DfltVulnFlags()
{
    return 0xCFFFFFF7;
}

void zNMECritJelly::Setup()
{
    if (itemType == NME_TYPE_BUCKETJELLY)
    {
        xAntennaAddAntenna(this, 0.0f, 0.0f, 5, 6, false);
    }
}

void zNMECritJelly::SelfSetup()
{
    xBehaveMgr* bmgr;
    xPsyche* psy;

    bmgr = xBehaveMgr_GetSelf();
    psy_self = bmgr->Subscribe(this, 0);

    psy = psy_self;
    psy->BrainBegin();

    psy->AddGoal(NME_GOAL_CRIT_IDLE, NULL);
    psy->AddGoal(NME_GOAL_CRIT_PATROL, NULL);
    psy->AddGoal(NME_GOAL_CRIT_JATTACK, NULL);
    psy->AddGoal(NME_GOAL_CRIT_JDYING, NULL);
    psy->AddGoal(NME_GOAL_CRIT_DEAD, NULL);

    psy->BrainEnd();
    psy->SetSafety(NME_GOAL_CRIT_IDLE);
}

U32 zNMECritJelly::AnimPick(S32 gid, en_npcgspot gspot, xGoal* goal)
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
    case NME_GOAL_CRIT_JATTACK:
        idx = 3;
        break;
    case NME_GOAL_CRIT_JDYING:
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

xAnimTable* zNMECritJelly::CreateAnimTable(xAnimTable* table)
{
    if (table == NULL)
    {
        table = xAnimTableNew("zNMECritJelly", 0);
    }

    zNMECritter_AnimTableAdd(table);

    return table;
}

void zNMECritJelly::Reset()
{
    zNMEGoalCritDead* goal;

    zNMECommon::Reset();

    flg_vuln = DfltVulnFlags();

    psy_self->GoalSet(NME_GOAL_CRIT_IDLE, 0);

    goal = (zNMEGoalCritDead*)psy_self->FindGoal(NME_GOAL_CRIT_DEAD);
    goal->stylinDyin = DIE_LOUDLY;

    navnet->MvptReset(NULL);
}

S32 zNMECritJelly::IsAlive()
{
    S32 gid_nolife[3] = { NME_GOAL_CRIT_JDYING, NME_GOAL_CRIT_DEAD, 0 };
    S32 gid;

    gid = psy_self->GIDOfActive();
    if (NMEStd_GoalInList(gid, gid_nolife))
    {
        return 0;
    }

    return 1;
}

void zNMECritJelly::BUpdate(xVec3* pos)
{
    xMat4x3* mat_bone;
    xMat4x3* mat_root;
    xVec3 pos_bone;

    mat_root = (xMat4x3*)model->Mat;
    mat_bone = (xMat4x3*)&model->Mat[3];
    xMat4x3Toworld(&pos_bone, mat_root, &mat_bone->pos);
    Jelly::bnd_info.UpdateBound(&bound, this, &pos_bone);
}

void zNMECritJelly::Process(F32 dt)
{
    xMat4x3 oldMat;
    xEntFrame tmpFrame;

    if (flg_colCheck)
    {
        oldMat = *(xMat4x3*)model->Mat;
    }

    psy_self->Timestep(dt, NULL);

    if (IsAlive())
    {
        if (!xnegative(tmr_warn))
        {
            tmr_warn = MAX(-1.0f, tmr_warn - dt);
        }
        else if ((flags & 0x85) == 0x1)
        {
            if (!(XYZDstSqToPlayer(NULL) > SQ(12.0f)))
            {
                DangerSigns();
                tmr_warn = 0.25f;
            }
        }

        ShadStufUpdate();
    }
    else if (simpShadow != NULL || entShadow != NULL)
    {
        ShadStufIAmDead();
    }

    zNMECommon::Process(dt);

    if (flg_colCheck)
    {
        memset(&tmpFrame, 0, sizeof(xEntFrame));
        tmpFrame.oldmat = oldMat;
        tmpFrame.mat = *(xMat4x3*)model->Mat;
        frame = &tmpFrame;
        RunCollision(dt);
        frame = NULL;
    }
}

S32 zNMECritJelly::TypeHandleMail(NMEMsg* mail)
{
    S32 handled = 1;
    xPsyche* psy;
    xGoal* goal;

    switch (mail->msgid)
    {
    case NME_MID_DAMAGE:
        handled = JellyMailDamage(&mail->dmgdata);
        break;
    case NME_MID_UNDERATTACK:
        psy = psy_self;
        goal = psy->GIDInStack(NME_GOAL_CRIT_JATTACK);
        if (goal == NULL)
        {
            psy->GoalPush(NME_GOAL_CRIT_JATTACK, 0);
        }
        break;
    case NME_MID_SYSEVENT:
        handled = DfltSysEventMail(&mail->sysevent);
        break;
    default:
        handled = 0;
        break;
    }

    return handled;
}

S32 zNMECritJelly::JellyMailDamage(NMEDamageInfo* dmgmail)
{
    S32 handled = 1;
    xPsyche* psy;
    xGoal* goal;

    switch (dmgmail->dmg_type)
    {
    case DMGTYP_KILLEVENT:
        psy = psy_self;
        goal = psy->FindGoal(NME_GOAL_CRIT_DEAD);
        ((zNMEGoalCritDead*)goal)->stylinDyin = DIE_MUFFLED;
        psy->GoalSet(NME_GOAL_CRIT_DEAD, 0);
        break;
    case DMGTYP_ABOVE:
    case DMGTYP_BELOW:
    case DMGTYP_SIDE:
    case DMGTYP_INSTAKILL:
    case DMGTYP_WOUNDEVENT:
    case DMGTYP_NMEATTACK:
    case DMGTYP_HITBYTOSS:
    case DMGTYP_NMEKNOCKED:
    case DMGTYP_ROPE:
    case DMGTYP_CRUISEBUBBLE:
    case DMGTYP_FRIENDLYFIRE:
    case DMGTYP_REFLECTED:
    case DMGTYP_BOULDER:
    case DMGTYP_BUBBOWL:
    case DMGTYP_THUNDER_TIKI_EXPLOSION:
    case DMGTYP_DAMAGE_SURFACE:
    case DMGTYP_BUNGEED:
    case DMGTYP_SURFACE:
    case DMGTYP_CARTWHEEL:
    case DMGTYP_SLIDE:
    case DMGTYP_EXPLOSION:
    case DMGTYP_BBOWLZAP:
        psy_self->GoalSet(NME_GOAL_CRIT_JDYING, 0);
        break;
    default:
        handled = 0;
        break;
    }

    return handled;
}

// The compiled-out asserts (conditions unknown) keep this from being auto-inlined into Process, as
// retail calls it. idx_destbone is in the PS2 DWARF but unused; its next-leg values are inferred
// from the name.
void zNMECritJelly::DangerSigns()
{
    xASSERT_PLACEHOLDER();
    xASSERT_PLACEHOLDER();
    xASSERT_PLACEHOLDER();
    xASSERT_PLACEHOLDER();
    xASSERT_PLACEHOLDER();
    xASSERT_PLACEHOLDER();
    xASSERT_PLACEHOLDER();
    xASSERT_PLACEHOLDER();

    _tagLightningAdd info;
    S32 idx_frombone;
    S32 idx_destbone;

    memset(&info, 0, sizeof(_tagLightningAdd));
    NMEC_MakeLightningInfo(NME_LYT_JELLYPINK, &info);

    idx_frombone = Jelly::Danger::idx_boneLegs[idx_leg];

    if (idx_leg >= 3)
    {
        idx_destbone = Jelly::Danger::idx_boneLegs[0];
    }
    else
    {
        idx_destbone = Jelly::Danger::idx_boneLegs[idx_leg + 1];
    }

    idx_leg++;
    if (idx_leg >= 4)
    {
        idx_leg = 0;
    }

    xMat4x3 matroot = *(xMat4x3*)model->Mat;
    xVec3 pos_from = ((xMat4x3*)model->Mat)[idx_frombone].pos;
    xMat4x3Toworld(&pos_from, &matroot, &pos_from);

    xVec3 pos_dest = ((xMat4x3*)model->Mat)->pos;
    pos_dest.y -= 2.5f;
    pos_dest.x += 1.25f * xurand();
    pos_dest.z += 1.25f * xurand();

    info.time = 0.3f;
    info.start = &pos_from;
    info.end = &pos_dest;
    info.segmentsPerMeter = 3.0f;
    zLightningAdd(&info);
}

S32 zNMEGoalJellyAttack::Enter(F32 dt, void* updCtxt)
{
    zNMECritJelly* npc = (zNMECritJelly*)psyche->clt_owner;

    npc->SndPlayFromTable(2); // eNMESound_Attack
    ZapperStart();
    return zNMEGoalPushAnim::Enter(dt, updCtxt);
}

S32 zNMEGoalJellyAttack::Exit(F32 dt, void* updCtxt)
{
    ZapperStop();
    return zNMEGoalPushAnim::Exit(dt, updCtxt);
}

S32 zNMEGoalJellyAttack::Process(en_trantype* trantype, F32 dt, void* ctxt)
{
    ZapperUpdate();
    return zNMEGoalPushAnim::Process(trantype, dt, ctxt);
}

void zNMEGoalJellyAttack::ZapperStart()
{
    zNMECritJelly* npc = (zNMECritJelly*)psyche->clt_owner;
    _tagLightningAdd info;
    xVec3 pos_bone;
    S32 i;

    memset(&info, 0, sizeof(_tagLightningAdd));
    NMEC_MakeLightningInfo(NME_LYT_JELLYPINK, &info);

    // Rotation and translation are separate statements (as in the PS2 line table), not one
    // xMat4x3Toworld.
    pos_bone = ((xMat4x3*)npc->model->Mat)[2].pos;
    xMat3x3RMulVec(&pos_bone, (xMat4x3*)npc->model->Mat, &pos_bone);
    pos_bone += ((xMat4x3*)npc->model->Mat)->pos;

    info.time = 15.0f;
    info.start = &pos_bone;
    info.end = &((xMat4x3*)xglobals->___player_ent_dont_use_directly->model->Mat)->pos;
    info.segmentsPerMeter = 3.0f;

    for (i = 0; i < 3; i++)
    {
        zap_lytnin[i] = zLightningAdd(&info);
    }

    g_mod_supple->StingerPrepare();
}

// tym_attackOn[0] is 0, so the first branch never runs (retail quirk, kept as is).
void zNMEGoalJellyAttack::ZapperUpdate()
{
    zNMECritJelly* npc = (zNMECritJelly*)psyche->clt_owner;
    F32 tym_ancur;
    F32 rat;
    xVec3 pos_bone;

    tym_ancur = npc->AnimTimeCurrent();
    rat = CLAMP(tym_ancur / npc->AnimDuration(NULL), 0.0f, 1.0f);

    static S32 idx_fromBone = 2;
    idx_fromBone++;
    if (idx_fromBone > npc->model->BoneCount)
    {
        idx_fromBone = 2;
    }
    if (idx_fromBone < 2)
    {
        idx_fromBone += 2;
    }

    pos_bone = ((xMat4x3*)npc->model->Mat)[idx_fromBone].pos;
    xMat4x3Toworld(&pos_bone, (xMat4x3*)npc->model->Mat, &pos_bone);

    static const F32 tym_attackOn[2] = { 0.0f, 1.0f };
    if (tym_ancur < tym_attackOn[0])
    {
        for (S32 i = 0; i < 3; i++)
        {
            if (zap_lytnin[i] != NULL)
            {
                S32 idx_tgtBone = (S32)(npc->model->BoneCount * xurand());
                xVec3 pos_tgt;

                if (idx_tgtBone < 2)
                {
                    idx_tgtBone += 2;
                }

                pos_tgt = ((xMat4x3*)npc->model->Mat)[idx_tgtBone].pos;
                xMat4x3Toworld(&pos_tgt, (xMat4x3*)npc->model->Mat, &pos_tgt);
                zLightningModifyEndpoints(zap_lytnin[i], &pos_bone, &pos_tgt);
            }
        }
    }
    else
    {
        static const xVec3 wt_tgt = { 0.25f, 0.75f, 0.25f };
        xVec3 pos_plyr = *xEntGetCenter(xglobals->___player_ent_dont_use_directly);

        npc->SndPlayFromTable(7); // eNMESound_Special1

        for (S32 i = 0; i < 3; i++)
        {
            if (zap_lytnin[i] != NULL)
            {
                xVec3 pos_tgt = pos_plyr;

                pos_tgt += xEntGetFrame(xglobals->___player_ent_dont_use_directly)->at *
                           (wt_tgt.z * xurand());
                pos_tgt += xEntGetFrame(xglobals->___player_ent_dont_use_directly)->up *
                           (wt_tgt.y * xurand());
                pos_tgt += xEntGetFrame(xglobals->___player_ent_dont_use_directly)->right *
                           (wt_tgt.x * xurand());

                zLightningModifyEndpoints(zap_lytnin[i], &pos_bone, &pos_tgt);
            }
        }
    }
}

S32 zNMEGoalJellyDying::Enter(F32 dt, void* updCtxt)
{
    zNMECritJelly* npc = (zNMECritJelly*)psyche->clt_owner;

    npc->PoofOfBubbles();
    npc->chkby = 0;
    npc->penby = 0;

    return zNMEGoalCritDying::Enter(dt, updCtxt);
}

S32 zNMEGoalJellyDying::Exit(F32 dt, void* updCtxt)
{
    zNMECritJelly* npc = (zNMECritJelly*)psyche->clt_owner;

    npc->PoofOfBubbles();

    zShrapnelAsset* shrap = zNMEShrapnelRetrieveShrapnel(npc->SelfType());
    if (shrap != NULL)
    {
        xVec3 vel = {};
        shrap->initCB(shrap, npc->model, &vel, NULL);
    }

    return zNMEGoalCritDying::Exit(dt, updCtxt);
}
