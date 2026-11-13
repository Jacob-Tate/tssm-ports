#include "zNMETypeBossFrogfish.h"
#include "zNMESupport.h"
#include "zNMEHealthMeter.h"
#include "zNMEParFX.h"
#include "xRumbleEmitter.h"
#include "zEntPlayer.h"
#include "zCam.h"
#include "zNMETypeTongueSpin.h"

U32 gHashFrogFishAnim[33];
char* gStrFrogFishAnim[33] = {
    "Unknown",      "Idle",          "WalkLeft",   "WalkRight",   "Taunt1",       "Hit",
    "Recover",      "MeleePrep",     "MeleeHover", "MeleeLoop",   "MeleeDone",    "ChargePrep",
    "ChargeLoop",   "ChargeSkid",    "ChargeDone", "ChargeReset", "ChargeTeeter", "AngryArena",
    "FuriousArena", "FuriousTrench", "IdleHide",   "Taunt2",      "TonguePrep",   "TongueLoop",
    "TongueDone",   "Hit2",          "StunPrep",   "StunLoop",    "StunDone",     "Up",
    "Down",         "IdleLob",       "Lob",
};

zNMECfgFrogfish cfg_frogfish = {
    { 8, 12, 12, 3.0f, { 0.0f, 0.0f, -1.0f } },
    { 15.0f, 7.0f, -0.75f, -20.0f, 46.0f },
    { HAZ_TYP_FLING_FROGFISH, 16.0f, 5.0f, 0.5f, { 0.0f, -0.2f, 0.05f }, 3.0f, 0.7f, 0.7f },
    { -3.0f, -41.0f, 25.0f, 15.0f, 21.0f },
    { 1.63f, 10.0f },
    { 0.86f, 5.0f },
    { 4.712389f },
    { 1.0f, 2.0f, 0.05f, 0.2f, 100.0f },
};

xVec3 FFvec_emitOffset = { 0.0f, 0.35f, 0.0f };

PuffEmitter FFdustWhirl = {
    NPAR_TYP_DUSTPUFF,
    1.6f,
    { 0x96, 0x96, 0x96, 0x32 },
    { 0.5f, 2.0f },
    { 0.0f, 2.0f, 0.0f },
    3.84f,
    93,
    0,
    127,
    0,
    { 5.0f, 20.0f },
};

PuffEmitter FFpuffFeet = {
    NPAR_TYP_DUSTPUFF,
    0.5f,
    { 0x96, 0x96, 0x96, 0x32 },
    { 1.4f, 3.0f },
    { 0.0f, -2.0f, 0.0f },
    7.2f,
    93,
    0,
    5,
    0,
    { 5.0f, 1.0f },
};

PuffEmitter FFpuffTrail = {
    NPAR_TYP_DUSTPUFF,
    0.5f,
    { 0x96, 0x96, 0x96, 0x32 },
    { 1.0f, 3.0f },
    { 0.0f, -2.0f, 0.0f },
    7.2f,
    93,
    0,
    5,
    0,
    { 5.0f, 1.0f },
};

NMEHealthMeter::MeterData FFMeterHealth = {
    { 1, 0, { 0, 0 } },
    { { -1, 8, 12, 12 }, "particle_cloud", 0.14f, 0.25f },
    { { -1, 10, 10, 10 } },
};

static NMEShadParms cfg_shadparm_frogfish = { { 2.0f, "shadow_round", NULL },
                                              { 0 },
                                              { 35.0f, 35.0f } };

static const xVec3 svTongue = { 11.6f, 0.8f, 4.7f };

// No symbol (always inlined; the name is ours). Retail's stack layout in EnterState shows the
// TONGUESTART test was an inline of its own.
static inline bool IsAtTongue(const zNMEFrogfish* npc)
{
    return npc->XZDstSqToPos(&svTongue, NULL, NULL) < SQR(0.00001f);
}

S32 gFFBossAttackTypes[3][12] = {
    { 0 },
    { 2, 2 },
    { 1, 1, 0, 1 },
};
S32 gFFBossDamageToSwitchAttack[3] = { 3, 3, 3 };

zStreamedSound zNMEFrogfish::FFOneLinersData[12] = {
    { "OneL_FF_FFHit_SG", 3.0f, 0x25cf, 0.0f, (iSndGroupHandle)-1 },
    { "ONEL_FF_PATHIT_SG", 3.0f, 0x4f, 0.0f, (iSndGroupHandle)-1 },
    { "ONEL_FF_SBHINTS_ATTACK0_SG", 3.0f, 0x40, 0.0f, (iSndGroupHandle)-1 },
    { "ONEL_FF_SBHINTS_ATTACK1_SG", 3.0f, 0x40, 0.0f, (iSndGroupHandle)-1 },
    { "ONEL_FF_SBHINTS_ATTACK2_SG", 3.0f, 0x40, 0.0f, (iSndGroupHandle)-1 },
    { "ONEL_FF_SBHINTS_NOW_SG", 3.0f, 0x40, 0.0f, (iSndGroupHandle)-1 },
    { "ONEL_FF_SBCOMMENTS_SG", 3.0f, 0x25c0, 0.0f, (iSndGroupHandle)-1 },
    { "ONEL_FF_PATCOMMENTS_SG", 3.0f, 0x25c0, 0.0f, (iSndGroupHandle)-1 },
    { "ONEL_FF_SBINTRO_SG", 0.0f, 0x10, 0.0f, (iSndGroupHandle)-1 },
    { "ONEL_FF_PATINTRO_SG", 0.0f, 0x10, 0.0f, (iSndGroupHandle)-1 },
    { "ONEL_FF_PATPHLEGM_SG", 0.0f, 0x40, 0.0f, (iSndGroupHandle)-1 },
    { NULL, 0.0f, 0, 0.0f, (iSndGroupHandle)-1 },
};
extern zBossCam_Binary* gzBinaryCam;

void zNMEFrogfish::CreateConfig(const char* name)
{
    zNMECommon::CreateConfig(name);
    cfg = &cfg_frogfish;
    if (cfg_shadparm_frogfish.simpshad.nam_shadowTexture == NULL)
    {
        cfg_shadparm_frogfish.simpshad.rast_shadow = NULL;
    }
    else
    {
        cfg_shadparm_frogfish.simpshad.rast_shadow =
            NMEC_FindRWRaster(cfg_shadparm_frogfish.simpshad.nam_shadowTexture);
    }
    cfg_shadparm = &cfg_shadparm_frogfish;
}

S32 zNMEFrogfish::DfltVulnFlags()
{
    return 0xC0000001;
}

void zNMEFrogfish::Init(xEntAsset* asset)
{
    zNMECommon::Init(asset);

    zNMEParFX_PartyInvite(NPAR_TYP_DUSTPUFF, NULL, 0);
    zNMEParFX_PartyInvite(NPAR_TYP_FLINGVAPORS_TR, NULL, 0);

    patIntroTimer = 1.5f;
    sbIntroTimer = 0.5f;
    patIntroPlayed = 0;
    sbIntroPlayed = 0;
    cameraSetup = 0;
}

void zNMEFrogfish::Setup()
{
    S32 i;
    zNPCMgr* npcmgr;
    char* nam_tongue;
    zNPC::base* nb;

    zNMECommon::Setup();

    for (i = 0; i < 33; i++)
    {
        gHashFrogFishAnim[i] = xStrHash(gStrFrogFishAnim[i]);
    }

    SoundInitAssetTable();
    FFOneLiners.InitWithSounds(FFOneLinersData, eFFOneLinerNumSounds);

    rumbleMelee = (xRumble::effect*)zSceneFindObject(xStrHash("FF_RUMBLE_MELEE"));
    fragSkullStompL = (zShrapnelAsset*)xSTFindAsset(xStrHash("frag_skull_stompL"), NULL);
    fragSkullStompR = (zShrapnelAsset*)xSTFindAsset(xStrHash("frag_skull_stompR"), NULL);
    fragSlam = (zShrapnelAsset*)xSTFindAsset(xStrHash("frag_slam"), NULL);
    fragSkid = (zShrapnelAsset*)xSTFindAsset(xStrHash("frag_skull_skid"), NULL);

    npcmgr = zNPCMgrSelf();
    nam_tongue = "TONGUE_SPIN";
    nb = npcmgr->FindNPC(xStrHash(nam_tongue));
    npc_tongue = (zNMETongueSpin*)nb;

    shadow_cache_fudge_factor = 0.9f;

    FFMeterHealth.runtime.rast_inner = NMEC_FindRWRaster("fx_boss_bar");
    FFMeterHealth.runtime.rast_outer = NMEC_FindRWRaster("fx_boss_graph");
    FFMeterHealth.runtime.rast_solid_bar = NMEC_FindRWRaster("fx_boss_solid_bar");
    FFMeterHealth.runtime.rast_broken_bar = NMEC_FindRWRaster("fx_boss_broken_bar");

    FFMeterHealth.runtime.rast_logo = NMEC_FindRWRaster("uii_boss_meter_b101");
    if (FFMeterHealth.runtime.rast_logo == NULL)
    {
        FFMeterHealth.runtime.rast_logo = NMEC_FindRWRaster("uii_boss_meter_b201");
    }
    if (FFMeterHealth.runtime.rast_logo == NULL)
    {
        FFMeterHealth.runtime.rast_logo = NMEC_FindRWRaster("uii_boss_meter_b301");
    }
    if (FFMeterHealth.runtime.rast_logo == NULL)
    {
        FFMeterHealth.runtime.rast_logo = NMEC_FindRWRaster("uii_boss_meter_b401");
    }

    FFMeterHealth.cfginfo.tym_fadeIn = 0.14f;
    FFMeterHealth.cfginfo.tym_fadeOut = 0.25f;

    FFMeterHealth.status.healthCurr[1] = cfg->health.maxStage1;
    FFMeterHealth.status.healthCurr[2] = cfg->health.maxStage2;
    FFMeterHealth.status.healthCurr[3] = cfg->health.maxStage3;
    FFMeterHealth.cfginfo.stageHealth[1] = cfg->health.maxStage1;
    FFMeterHealth.cfginfo.stageHealth[2] = cfg->health.maxStage2;
    FFMeterHealth.cfginfo.stageHealth[3] = cfg->health.maxStage3;
}

void zNMEFrogfish::Reset()
{
    zNMECommon::Reset();

    anid_played = 0;
    state = ezFFBOSS_STATE_IDLE;
    timer = 1.0f;
    SetTheStage(ezFFBOSS_STAGE_INTRO);
    FFMeterHealth.status.healthCurr[1] = cfg->health.maxStage1;
    FFMeterHealth.status.healthCurr[2] = cfg->health.maxStage2;
    FFMeterHealth.status.healthCurr[3] = cfg->health.maxStage3;

    attackChangePending = 0;
    camState = ezFFBOSS_CAMERA_STATE_REGULAR;
    firstTimeTongue = 1;
    teeterSndHandle = (iSndHandle)-1;
    fxTimer = 0.0f;
    OneLinersReset();
}

void zNMEFrogfish::Destroy()
{
    zNMEBoss::Destroy();

    zCamRemove(*gzBinaryCam, false);
    gzBinaryCam->destroy();
    gzBinaryCam = NULL;

    cameraSetup = 0;
}

void zNMEFrogfish::SetCamState(eFFBossCameraState newState)
{
    xVec3 tongueLoc;

    if (newState == camState)
    {
        return;
    }

    camState = newState;

    switch (camState)
    {
    case ezFFBOSS_CAMERA_STATE_REGULAR:
        zEntPlayerControlOn(CONTROL_OWNER_BOSS);
        break;
    case ezFFBOSS_CAMERA_STATE_PREPJUMP:
        zEntPlayerControlOff(CONTROL_OWNER_BOSS);
        camTimer = 0.35f;
        break;
    case ezFFBOSS_CAMERA_STATE_AFTERJUMP:
        camTargetStart = *xEntGetPos(xglobals->___player_ent_dont_use_directly);
        CalcPos(&tongueLoc, ezFFBOSS_LOC_TONGUE);
        xVec3Lerp(&camTargetEnd, &camTargetStart, &tongueLoc, 0.75f);
        camInterpTime = 0.0f;
        camInterpDuration = 1.0f;
        camTimer = 0.5f + camInterpDuration;
        break;
    case ezFFBOSS_CAMERA_STATE_AFTERTONGUE:
        camTimer = 0.5f;
        break;
    case ezFFBOSS_CAMERA_STATE_PREPGENERATION:
        camTargetStart = camTarget1;
        camTargetEnd = *xEntGetPos(xglobals->___player_ent_dont_use_directly);
        camInterpTime = 0.0f;
        camInterpDuration = 1.0f;
        camTimer = camInterpDuration;
        break;
    case ezFFBOSS_CAMERA_STATE_AFTERGENERATION:
        camTimer = 1.0f;
        break;
    case ezFFBOSS_CAMERA_STATE_TRANSTOREGULAR:
        camTargetStart = camTarget1;
        camTargetEnd = *xEntGetPos(xglobals->___player_ent_dont_use_directly);
        camInterpTime = 0.0f;
        camInterpDuration = 1.0f;
        camTimer = camInterpDuration;
        break;
    }
}

void zNMEFrogfish::UpdateCam(F32 dt)
{
    extern zBossCam_Binary* gzBinaryCam;

    if (!cameraSetup)
    {
        static zBossCam_Binary theStaticBinaryCam;
        gzBinaryCam = &theStaticBinaryCam;

        zBossCam_Binary::config cfg = {
            { 6.0f, 3.0f, 2.0f },
            { 0.2f, 2.2f, -1.0f },
            { 1.0f, 0.2f, 1.5f },
            10.0f,
            10.0f,
            10.0f,
            10.0f,
            50.0f,
            -0.5235988f,
        };
        theStaticBinaryCam.cfg = cfg;
        gzBinaryCam->cfg_common.priority = 5;
        gzBinaryCam->create();
        gzBinaryCam->attach(xglobals->___player_ent_dont_use_directly);
        gzBinaryCam->set_targets(*xEntGetPos(xglobals->___player_ent_dont_use_directly),
                                 *(xVec3*)&model->Mat[3].pos, 0.5f);
        zCamAdd(*gzBinaryCam, true);
        cameraSetup = 1;
    }

    if (gzBinaryCam != NULL)
    {
        switch (camState)
        {
        case ezFFBOSS_CAMERA_STATE_REGULAR:
            camTarget2 = *(xVec3*)&model->Mat[3].pos;
            xMat3x3RMulVec(&camTarget2, (xMat3x3*)model->Mat, &camTarget2);
            camTarget2 += *(xVec3*)&model->Mat->pos;
            gzBinaryCam->set_targets(*xEntGetPos(xglobals->___player_ent_dont_use_directly),
                                     camTarget2, 0.5f);
            camTimer = -1.0f;
            break;
        case ezFFBOSS_CAMERA_STATE_PREPJUMP:
            CalcPos(&camTarget2, ezFFBOSS_LOC_TONGUE_TRENCH);
            gzBinaryCam->set_targets(*xEntGetPos(xglobals->___player_ent_dont_use_directly),
                                     camTarget2, 0.5f);
            camTimer -= dt;
            break;
        case ezFFBOSS_CAMERA_STATE_AFTERJUMP:
            xVec3Lerp(&camTarget1, &camTargetStart, &camTargetEnd, camInterpTime);
            camTarget2 = *(xVec3*)&model->Mat[3].pos;
            xMat3x3RMulVec(&camTarget2, (xMat3x3*)model->Mat, &camTarget2);
            camTarget2 += *(xVec3*)&model->Mat->pos;
            gzBinaryCam->set_targets(camTarget1, camTarget2, 0.5f);
            camInterpTime += dt / camInterpDuration;
            if (camInterpTime > 1.0f)
            {
                camInterpTime = 1.0f;
            }
            camTimer -= dt;
            break;
        case ezFFBOSS_CAMERA_STATE_AFTERTONGUE:
            camTimer -= dt;
            gzBinaryCam->set_targets(camTarget1, camTarget2, 0.5f);
            break;
        case ezFFBOSS_CAMERA_STATE_PREPGENERATION:
            xVec3Lerp(&camTarget1, &camTargetStart, &camTargetEnd, camInterpTime);
            camTarget2 = *(xVec3*)&model->Mat[3].pos;
            xMat3x3RMulVec(&camTarget2, (xMat3x3*)model->Mat, &camTarget2);
            camTarget2 += *(xVec3*)&model->Mat->pos;
            gzBinaryCam->set_targets(camTarget1, camTarget2, 0.5f);
            camInterpTime += dt / camInterpDuration;
            if (camInterpTime > 1.0f)
            {
                camInterpTime = 1.0f;
            }
            camTimer -= dt;
            break;
        case ezFFBOSS_CAMERA_STATE_AFTERGENERATION:
            camTimer -= dt;
            gzBinaryCam->set_targets(camTarget1, camTarget2, 0.5f);
            break;
        case ezFFBOSS_CAMERA_STATE_TRANSTOREGULAR:
            xVec3Lerp(&camTarget1, &camTargetStart, &camTargetEnd, camInterpTime);
            camTarget2 = *(xVec3*)&model->Mat[3].pos;
            xMat3x3RMulVec(&camTarget2, (xMat3x3*)model->Mat, &camTarget2);
            camTarget2 += *(xVec3*)&model->Mat->pos;
            gzBinaryCam->set_targets(camTarget1, camTarget2, 0.5f);
            camInterpTime += dt / camInterpDuration;
            if (camInterpTime > 1.0f)
            {
                camInterpTime = 1.0f;
            }
            camTimer -= dt;
            break;
        }
    }
}

// No GC symbol: inlined into Setup, the out-of-line copy dead-stripped
void zNMEFrogfish::SoundInitAssetTable()
{
    // Retail clears only the first handle (the size of one entry); all of them are assigned below.
    memset(sSoundAsset, 0, sizeof(iSndGroupHandle));
    sSoundAsset[eSoundStomp] = xSndMgrGetSoundGroup(xStrHash("FFStompGroup"));
    sSoundAsset[eSoundChargePrep] = xSndMgrGetSoundGroup(xStrHash("FFChargePrepGroup"));
    sSoundAsset[eSoundCharge] = xSndMgrGetSoundGroup(xStrHash("FFChargeGroup"));
    sSoundAsset[eSoundSkid] = xSndMgrGetSoundGroup(xStrHash("FFSkidGroup"));
    sSoundAsset[eSoundHit] = xSndMgrGetSoundGroup(xStrHash("FFHitGroup"));
    sSoundAsset[eSoundSmash] = xSndMgrGetSoundGroup(xStrHash("FFSmashGroup"));
    sSoundAsset[eSoundSpit] = xSndMgrGetSoundGroup(xStrHash("FFSpitGroup"));
    sSoundAsset[eSoundTaunt] = xSndMgrGetSoundGroup(xStrHash("FFTauntGroup"));
    sSoundAsset[eSoundYelp] = xSndMgrGetSoundGroup(xStrHash("FFYelpGroup"));
    sSoundAsset[eSoundLand] = xSndMgrGetSoundGroup(xStrHash("FFLandGroup"));
    sSoundAsset[eSoundTeeter] = xSndMgrGetSoundGroup(xStrHash("FFTeeterGroup"));
    sSoundAsset[eSoundRecover] = xSndMgrGetSoundGroup(xStrHash("FFRecoverGroup"));
    sSoundAsset[eSoundMad] = xSndMgrGetSoundGroup(xStrHash("FFMadGroup"));
    sSoundAsset[eSoundRoar] = xSndMgrGetSoundGroup(xStrHash("FFRoarGroup"));
    sSoundAsset[eSoundTongueBite] = xSndMgrGetSoundGroup(xStrHash("FFTongueBiteGroup"));
    sSoundAsset[eSoundDead] = xSndMgrGetSoundGroup(xStrHash("FFDeadGroup"));
    sSoundAsset[eSoundFootstepWalk] = xSndMgrGetSoundGroup(xStrHash("FFFootstepWalkGroup"));
    sSoundAsset[eSoundFootstepRun] = xSndMgrGetSoundGroup(xStrHash("FFFootstepRunGroup"));
    sSoundAsset[eSoundTongueTaunt] = xSndMgrGetSoundGroup(xStrHash("FFTongueTauntGroup"));
    sSoundAsset[eSoundTongueDown] = xSndMgrGetSoundGroup(xStrHash("FFTongueDownGroup"));
}

iSndHandle zNMEFrogfish::SoundPlayEffect(eSound sound, F32 volume)
{
    if (volume == -1.0f)
    {
        return xSndMgrPlay(sSoundAsset[sound], 2, NULL, NULL, this, NULL, NULL);
    }
    else
    {
        return xSndMgrPlay(sSoundAsset[sound], 2, NULL, NULL, this, &volume, NULL);
    }
}

bool zNMEFrogfish::AttackHit()
{
    bool hit = false;
    xVec3 delta = *xEntGetPos(xglobals->___player_ent_dont_use_directly) - *xEntGetPos(this);

    if (delta.length2() < SQR(cfg->attackA.range))
    {
        xVec3 at = xEntGetFrame(this)->at;
        if (delta.dot(at) > 0.0f)
        {
            hit = true;
        }
    }

    return hit;
}

bool zNMEFrogfish::LungeHit()
{
    bool hit = false;
    xVec3 delta = *xEntGetPos(xglobals->___player_ent_dont_use_directly) - *xEntGetPos(this);

    if (delta.length2() < SQR(cfg->attackA.range))
    {
        hit = true;
    }

    return hit;
}

void zNMEFrogfish::RumbleAndShake(F32 intensity)
{
    xRumble::effect* rumble = rumbleMelee;

    if (rumble != NULL)
    {
        rumble->SetIntensity(intensity);

        zEntEvent(rumble, eEventStartRumbleEffect, globals.currentActivePad, 0.0f, 0.0f, 0.0f);
    }
}

// Each transform is a rotate then '+= pos' (two lines each in the PS2 line table), not
// xMat4x3Toworld.
void zNMEFrogfish::FeetFXUpdate(F32 dt)
{
    F32 tym_animCurr;
    F32 tym_animDur;
    F32 tym_animPlusTime;
    U8 puffLeft;
    U8 puffRight;

    tym_animCurr = AnimTimeCurrent();
    // Retail uses the constant 1.0 here.
    tym_animDur = 1.0f;
    tym_animPlusTime = tym_animCurr + dt;
    puffLeft = 0;
    puffRight = 0;

    xVec3 vel = xVec3::m_Null;
    xVec3 pos_emit = xVec3::m_Null;
    xVec3 dir_emit;

    if (anid_played == gHashFrogFishAnim[12])
    {
        if (tym_animPlusTime > 0.16666667f && tym_animCurr <= 0.16666667f &&
            fragSkullStompL != NULL)
        {
            fragSkullStompL->initCB(fragSkullStompL, model, &vel, NULL);
            puffLeft = 1;
            xSndMgrPlay(sSoundAsset[eSoundFootstepRun], 2, NULL, NULL, this, NULL, NULL);
        }
        if (tym_animDur + tym_animCurr > 0.16666667f && tym_animCurr <= 0.16666667f)
        {
            puffLeft = 1;
        }

        if (tym_animPlusTime > 0.6666667f && tym_animCurr <= 0.6666667f && fragSkullStompR != NULL)
        {
            fragSkullStompR->initCB(fragSkullStompR, model, &vel, NULL);
            puffRight = 1;
            xSndMgrPlay(sSoundAsset[eSoundFootstepRun], 2, NULL, NULL, this, NULL, NULL);
        }
        if (tym_animDur + tym_animCurr > 0.6666667f && tym_animCurr <= 0.6666667f)
        {
            puffRight = 1;
        }
    }
    else if (anid_played == gHashFrogFishAnim[1])
    {
        if (tym_animPlusTime > 0.4f && tym_animCurr <= 0.4f)
        {
            xSndMgrPlay(sSoundAsset[eSoundFootstepWalk], 2, NULL, NULL, this, NULL, NULL);
        }
        if (tym_animPlusTime > 0.9f && tym_animCurr <= 0.9f)
        {
            xSndMgrPlay(sSoundAsset[eSoundFootstepWalk], 2, NULL, NULL, this, NULL, NULL);
        }
    }

    if (puffLeft || puffRight)
    {
        if (puffLeft)
        {
            xMat4x3* mat_bone = (xMat4x3*)&model->Mat[54];
            pos_emit = xVec3::m_Null;
            xMat3x3RMulVec(&pos_emit, mat_bone, &pos_emit);
            pos_emit += mat_bone->pos;
        }
        else
        {
            xMat4x3* mat_bone = (xMat4x3*)&model->Mat[58];
            pos_emit = xVec3::m_Null;
            xMat3x3RMulVec(&pos_emit, mat_bone, &pos_emit);
            pos_emit += mat_bone->pos;
        }

        xMat3x3RMulVec(&pos_emit, (xMat4x3*)model->Mat, &pos_emit);
        pos_emit += ((xMat4x3*)model->Mat)->pos;
        dir_emit = ((xMat4x3*)model->Mat)->at * 0.0f;
        dir_emit += ((xMat4x3*)model->Mat)->up * 0.0f;
        if (puffLeft)
        {
            dir_emit += ((xMat4x3*)model->Mat)->right * 8.0f;
        }
        else
        {
            dir_emit -= ((xMat4x3*)model->Mat)->right * 8.0f;
        }
        FFpuffFeet.Footstroke(&pos_emit, &dir_emit);
    }

    if (anid_played == gHashFrogFishAnim[12])
    {
        // The rotated pos_emit is overwritten right away (retail computes it too).
        xMat3x3RMulVec(&pos_emit, (xMat3x3*)model->Mat, &pos_emit);
        pos_emit = ((xMat4x3*)model->Mat)->pos;
        dir_emit = ((xMat4x3*)model->Mat)->at * -5.0f;
        dir_emit += ((xMat4x3*)model->Mat)->up;
        FFpuffTrail.Footstroke(&pos_emit, &dir_emit);
    }

    if (anid_played == gHashFrogFishAnim[13])
    {
        xMat4x3* mat_bone = (xMat4x3*)&model->Mat[54];
        pos_emit = xVec3::m_Null;
        xMat3x3RMulVec(&pos_emit, mat_bone, &pos_emit);
        pos_emit += mat_bone->pos;
        xMat3x3RMulVec(&pos_emit, (xMat4x3*)model->Mat, &pos_emit);
        pos_emit += ((xMat4x3*)model->Mat)->pos;
        dir_emit = ((xMat4x3*)model->Mat)->at * 7.5f;
        dir_emit += ((xMat4x3*)model->Mat)->up * 1.5f;
        FFpuffFeet.Footstroke(&pos_emit, &dir_emit);

        fxTimer -= dt;
        if (fxTimer < 0.0f)
        {
            if (fragSkid != NULL)
            {
                fragSkid->initCB(fragSkid, model, &vel, NULL);
            }
            fxTimer = 0.25f;
        }

        mat_bone = (xMat4x3*)&model->Mat[58];
        pos_emit = xVec3::m_Null;
        xMat3x3RMulVec(&pos_emit, mat_bone, &pos_emit);
        pos_emit += mat_bone->pos;
        xMat3x3RMulVec(&pos_emit, (xMat4x3*)model->Mat, &pos_emit);
        pos_emit += ((xMat4x3*)model->Mat)->pos;
        dir_emit = ((xMat4x3*)model->Mat)->at * 7.5f;
        dir_emit += ((xMat4x3*)model->Mat)->up * 1.5f;
        FFpuffFeet.Footstroke(&pos_emit, &dir_emit);
    }
    else if (anid_played == gHashFrogFishAnim[9])
    {
        fxTimer -= dt;
        if (fxTimer < 0.0f)
        {
            if (fragSlam != NULL)
            {
                fragSlam->initCB(fragSlam, model, &vel, NULL);
            }
            fxTimer = 0.05f;
        }
    }
}

// The casts on the baseFlags constants are as in zEntTeleportBox's identical pair. Both
// teeterSndHandle stores after xSndMgrStop are retail's.
void zNMEFrogfish::Process(F32 dt)
{
    baseFlags &= (U8)~0x40;
    baseFlags |= (U16)0x80;

    if (IsAlive())
    {
        ShadStufUpdate();
    }
    else if (simpShadow != NULL || entShadow != NULL)
    {
        ShadStufIAmDead();
    }

    if (IsAlive())
    {
        UpdateCam(dt);
    }
    else
    {
        zNMECommon::Process(dt);
        return;
    }

    xMat4x3 oldMat;
    xMat4x3Copy(&oldMat, (xMat4x3*)model->Mat);

    if (IsAlive())
    {
        Common_TimerUpd(dt);
        MeterUpdate();
        flg_hudrend = 1;
    }

    if (anid_played == 0)
    {
        PlayExplicitAnim(gHashFrogFishAnim[1], 0);
    }

    if (((state == ezFFBOSS_STATE_CHARGE && !AttackHit()) || state == ezFFBOSS_STATE_IDLE) &&
        LungeHit())
    {
        EnterState(ezFFBOSS_STATE_LUNGE);
    }

    F32 tym_animCurr = AnimTimeCurrent();
    F32 tym_animDur = AnimDuration(NULL);
    F32 tym_animPlusTime = tym_animCurr + dt;

    FeetFXUpdate(dt);

    switch (state)
    {
    case ezFFBOSS_STATE_IDLE:
        if (timer < 0.0f && xEntGetPos(xglobals->___player_ent_dont_use_directly)->y > -1.0f)
        {
            EnterState(ezFFBOSS_STATE_TURNING2);
        }
        timer -= dt;
        break;
    case ezFFBOSS_STATE_LUNGE:
        if (anid_played == gHashFrogFishAnim[8])
        {
            timer -= dt;
            if (timer > 2.0f * AnimDuration(NULL) - 0.33333334f)
            {
                xVec3 dirPlayer;
                dirPlayer /= xsqrt(XZDstSqToPlayer(&dirPlayer, NULL));
                MatQRotTowards(dt, &dirPlayer, 9.424778f);
            }

            if (timer > 2.0f * AnimDuration(NULL) - 0.33333334f)
            {
                xVec3 newPos = *xEntGetPos(this);
                newPos.y += 6.0f * (dt / 0.33333334f);
                MatPosSet(&newPos);
            }
            else if (timer < 0.33333334f)
            {
                xVec3 newPos = *xEntGetPos(this);
                newPos.y -= 6.0f * (dt / 0.33333334f);
                MatPosSet(&newPos);
            }

            if (timer < 0.0f)
            {
                xVec3 newPos = *xEntGetPos(this);
                newPos.y = 0.0f;
                MatPosSet(&newPos);

                if (LungeHit())
                {
                    DoDamageAndKnockback();
                    SoundPlayEffect(eSoundHit, -1.0f);
                    meleeSuccessfull = 1;
                }

                SoundPlayEffect(eSoundSmash, -1.0f);
                xVec3 pos_emit = *xEntGetPos(this) + FFvec_emitOffset;
                FFdustWhirl.ImpactRing(&pos_emit, NULL);
                RumbleAndShake(1.0f);
                fxTimer = 0.0f;
                PlayExplicitAnim(gHashFrogFishAnim[9], 0);
            }
        }
        else if (anid_played == gHashFrogFishAnim[9])
        {
            if (tym_animPlusTime > tym_animDur)
            {
                PlayExplicitAnim(gHashFrogFishAnim[10], 0);
            }
        }
        else if (anid_played == gHashFrogFishAnim[10] && tym_animPlusTime > tym_animDur)
        {
            if (meleeSuccessfull)
            {
                EnterState(ezFFBOSS_STATE_QUICKTAUNT);
            }
            else if (tym_animPlusTime > tym_animDur)
            {
                if (xEntGetPos(xglobals->___player_ent_dont_use_directly)->y < -0.5f)
                {
                    PlayExplicitAnim(gHashFrogFishAnim[1], 0);
                    EnterState(ezFFBOSS_STATE_IDLE);
                }
                else
                {
                    EnterState(ezFFBOSS_STATE_CHARGE);
                }
            }
        }
        break;
    case ezFFBOSS_STATE_CHARGE:
        if (anid_played == gHashFrogFishAnim[11] && tym_animPlusTime > tym_animDur)
        {
            PlayExplicitAnim(gHashFrogFishAnim[12], 0);
        }

        if (AttackHit())
        {
            DoDamageAndKnockback();
            SoundPlayEffect(eSoundHit, -1.0f);
            EnterState(ezFFBOSS_STATE_TAUNT);
        }
        else if (anid_played != gHashFrogFishAnim[11])
        {
            F32 deltaX = cfg->arena.centerX - xEntGetPos(this)->x;
            F32 deltaZ = cfg->arena.centerZ - xEntGetPos(this)->z;
            F32 distFromCenterSq;
            xVec3 arenaCenter;
            arenaCenter.x = cfg->arena.centerX;
            arenaCenter.y = 0.0f;
            arenaCenter.z = cfg->arena.centerZ;
            xVec3 centerToFF;
            XZVecToPos(&centerToFF, &arenaCenter, NULL);
            xVec3 centerToPlayer;
            xVec3 ffToPlayer;
            XZVecToPos(&ffToPlayer, xEntGetPos(xglobals->___player_ent_dont_use_directly), NULL);
            centerToPlayer.x =
                xEntGetPos(xglobals->___player_ent_dont_use_directly)->x - cfg->arena.centerX;
            centerToPlayer.z =
                xEntGetPos(xglobals->___player_ent_dont_use_directly)->z - cfg->arena.centerZ;
            F32 distPlayerFromCenterSq = SQR(centerToPlayer.x) + SQR(centerToPlayer.z);
            F32 stoppingRadiusSq = SQR(cfg->arena.stoppingRadius);
            F32 dotCenter = xVec3Dot(&centerToFF, &chargeVelDir);
            F32 dotPlayer = xVec3Dot(&ffToPlayer, &chargeVelDir);
            distFromCenterSq = SQR(deltaX) + SQR(deltaZ);

            if (distFromCenterSq < stoppingRadiusSq || dotCenter > 0.0f ||
                (distPlayerFromCenterSq >= stoppingRadiusSq && dotPlayer > 0.95f))
            {
                xVec3 newPos = chargeVelDir * chargeVelMagnitude * dt + *xEntGetPos(this);
                MatPosSet(&newPos);
            }
            else
            {
                EnterState(ezFFBOSS_STATE_STOPPING);
            }
            timer -= dt;
        }
        break;
    case ezFFBOSS_STATE_STOPPING:
    {
        if (AttackHit())
        {
            DoDamageAndKnockback();
            SoundPlayEffect(eSoundHit, -1.0f);
        }

        F32 absDeltaV = xabs(cfg->attackA.stoppingDecel * dt);
        if (absDeltaV < chargeVelMagnitude)
        {
            chargeVelMagnitude += cfg->attackA.stoppingDecel * dt;
        }
        else
        {
            chargeVelMagnitude = 0.0f;
        }

        F32 deltaX = cfg->arena.centerX - Pos()->x;
        F32 deltaZ = cfg->arena.centerZ - Pos()->z;
        F32 distFromCenterSq = SQR(deltaX) + SQR(deltaZ);
        if (distFromCenterSq < SQR(cfg->arena.innerRadius))
        {
            xVec3 newPos = chargeVelDir * chargeVelMagnitude * dt + *Pos();
            MatPosSet(&newPos);
        }

        if (timer < 0.0f)
        {
            deltaX = cfg->arena.centerX - Pos()->x;
            deltaZ = cfg->arena.centerZ - Pos()->z;
            distFromCenterSq = SQR(deltaX) + SQR(deltaZ);
            if (distFromCenterSq < SQR(cfg->arena.innerRadius))
            {
                xVec3 newPos = chargeVelDir * 10.0f * dt + *Pos();
                MatPosSet(&newPos);
            }
            else if (IsTeeterOk())
            {
                EnterState(ezFFBOSS_STATE_TEETER);
            }
            else
            {
                EnterState(ezFFBOSS_STATE_LUNGE);
            }
        }
        timer -= dt;
        break;
    }
    case ezFFBOSS_STATE_WAIT_IN_PIT:
        timer -= dt;
        if (timer < 1.0f && anid_played == gHashFrogFishAnim[5] && !roarInTrenchPlayed)
        {
            SoundPlayEffect(eSoundRoar, -1.0f);
            roarInTrenchPlayed = 1;
        }

        if (timer < 0.0f && anid_played == gHashFrogFishAnim[5])
        {
            xVec3 newPos;
            if (IsTongueAttackNext())
            {
                CalcPos(&newPos, ezFFBOSS_LOC_TONGUE_TRENCH);
            }
            else
            {
                CalcPos(&newPos, ezFFBOSS_LOC_SAME_SIDE_PLAYER);
            }
            MatPosSet(&newPos);

            xVec3 dirCenter;
            F32 dy;
            xVec3 arenaCenter;
            arenaCenter.x = cfg->arena.centerX;
            arenaCenter.y = 0.0f;
            arenaCenter.z = cfg->arena.centerZ;
            XZVecToPos(&dirCenter, &arenaCenter, &dy);
            dirCenter.normalize();
            dirCenter = -dirCenter;

            xMat3x3 mat_rot;
            xMat3x3LookVec(&mat_rot, &dirCenter);
            MatSet(&mat_rot);

            landFXPlayed = 0;
            meleeSuccessfull = 0;

            if (IsTongueAttackNext())
            {
                if (ptsHealth < 1)
                {
                    StageIncrement();
                    EnterState(ezFFBOSS_STATE_TONGUESTART);
                }
                else if (damageTakenAttackCurr >= gFFBossDamageToSwitchAttack[stageCurr])
                {
                    AttackIncrement();
                }
            }
            else
            {
                PlayExplicitAnim(gHashFrogFishAnim[6], 0);
            }
        }
        else if (anid_played == gHashFrogFishAnim[6])
        {
            if (tym_animPlusTime > 0.33333334f)
            {
                PlayExplicitAnim(gHashFrogFishAnim[8], 0);
                timer = 3.0f * AnimDuration(NULL) + 0.3f;

                xVec3 newPos = *xEntGetPos(this);
                newPos.y += 6.0f;
                MatPosSet(&newPos);
            }
        }
        else if (anid_played == gHashFrogFishAnim[8])
        {
            timer -= dt;
            if (timer < 0.33333334f)
            {
                xVec3 newPos = *xEntGetPos(this);
                newPos.y -= 6.0f * (dt / 0.33333334f);
                MatPosSet(&newPos);
            }

            if (timer < 0.0f)
            {
                xVec3 newPos = *xEntGetPos(this);
                newPos.y = 0.0f;
                MatPosSet(&newPos);

                if (LungeHit())
                {
                    DoDamageAndKnockback();
                    SoundPlayEffect(eSoundHit, -1.0f);
                    meleeSuccessfull = 1;
                }

                SoundPlayEffect(eSoundSmash, -1.0f);
                xVec3 pos_emit = *xEntGetPos(this) + FFvec_emitOffset;
                FFdustWhirl.ImpactRing(&pos_emit, NULL);
                RumbleAndShake(1.0f);
                fxTimer = 0.0f;
                PlayExplicitAnim(gHashFrogFishAnim[9], 0);
            }
        }
        else if (anid_played == gHashFrogFishAnim[9])
        {
            if (tym_animPlusTime > tym_animDur)
            {
                PlayExplicitAnim(gHashFrogFishAnim[10], 0);
            }
        }
        else if (anid_played == gHashFrogFishAnim[10] && tym_animPlusTime > tym_animDur)
        {
            if (meleeSuccessfull)
            {
                attackChangePending = 1;
                EnterState(ezFFBOSS_STATE_QUICKTAUNT);
            }
            else if (tym_animPlusTime > tym_animDur &&
                     xEntGetPos(xglobals->___player_ent_dont_use_directly)->y >= -0.5f)
            {
                if (ptsHealth < 1)
                {
                    StageIncrement();
                    EnterState(ezFFBOSS_STATE_TONGUESTART);
                }
                else if (damageTakenAttackCurr >= gFFBossDamageToSwitchAttack[stageCurr])
                {
                    AttackIncrement();
                }
                else
                {
                    EnterState(ezFFBOSS_STATE_TURNING2);
                }
            }
        }
        break;
    case ezFFBOSS_STATE_DAMAGE:
        if (anid_played == gHashFrogFishAnim[5] && tym_animPlusTime > tym_animDur)
        {
            EnterState(ezFFBOSS_STATE_WAIT_IN_PIT);
        }
        break;
    case ezFFBOSS_STATE_TEETER:
        if (anid_played == gHashFrogFishAnim[14])
        {
            if (tym_animPlusTime > tym_animDur)
            {
                PlayExplicitAnim(gHashFrogFishAnim[16], 0);
                teeterSndHandle = SoundPlayEffect(eSoundTeeter, -1.0f);

                if (stageCurr == ezFFBOSS_STAGE_A)
                {
                    timer = 4.0f;
                }
                else if (stageCurr == ezFFBOSS_STAGE_B)
                {
                    timer = 3.0f;
                }
                else if (stageCurr == ezFFBOSS_STAGE_C)
                {
                    timer = 2.0f;
                }
                roarPlayed = 0;
            }
        }
        else
        {
            if (timer < 0.2f && !roarPlayed)
            {
                roarPlayed = 1;
                SoundPlayEffect(eSoundRecover, -1.0f);
            }

            if (timer < 0.0f)
            {
                xSndMgrStop(teeterSndHandle);
                teeterSndHandle = (iSndHandle)-1;
                teeterSndHandle = (iSndHandle)-1;
                EnterState(ezFFBOSS_STATE_LUNGE);
            }
            timer -= dt;
        }
        break;
    case ezFFBOSS_STATE_TAUNT:
    case ezFFBOSS_STATE_QUICKTAUNT:
        if (anid_played == gHashFrogFishAnim[13])
        {
            F32 absDeltaV = xabs(cfg->attackA.stoppingDecelFast * dt);
            if (absDeltaV < chargeVelMagnitude)
            {
                chargeVelMagnitude += cfg->attackA.stoppingDecelFast * dt;
            }
            else
            {
                chargeVelMagnitude = 0.0f;
            }

            F32 deltaX = cfg->arena.centerX - Pos()->x;
            F32 deltaZ = cfg->arena.centerZ - Pos()->z;
            if (SQR(deltaX) + SQR(deltaZ) < SQR(cfg->arena.innerRadius))
            {
                xVec3 newPos = chargeVelDir * chargeVelMagnitude * dt + *Pos();
                MatPosSet(&newPos);
            }

            if (timer < 0.0f)
            {
                PlayExplicitAnim(gHashFrogFishAnim[1], 0);
            }
        }
        else if (anid_played == gHashFrogFishAnim[1])
        {
            xVec3 dirPlayer;
            dirPlayer /= xsqrt(XZDstSqToPlayer(&dirPlayer, NULL));
            F32 qdot = MatQRotTowards(dt, &dirPlayer, cfg->turn.turnRate);
            if (qdot > 0.98f)
            {
                PlayExplicitAnim(gHashFrogFishAnim[4], 0);
                SoundPlayEffect(eSoundTaunt, -1.0f);
                timer = 2.0f * AnimDuration(NULL);
            }
        }
        else if (timer < 0.0f)
        {
            if (xEntGetPos(xglobals->___player_ent_dont_use_directly)->y < -0.5f)
            {
                PlayExplicitAnim(gHashFrogFishAnim[1], 0);
                EnterState(ezFFBOSS_STATE_IDLE);
            }
            else if (attackChangePending)
            {
                attackChangePending = 0;
                if (ptsHealth < 1)
                {
                    StageIncrement();
                    EnterState(ezFFBOSS_STATE_TONGUESTART);
                }
                else if (damageTakenAttackCurr >= gFFBossDamageToSwitchAttack[stageCurr])
                {
                    AttackIncrement();
                }
                else
                {
                    EnterState(ezFFBOSS_STATE_TURNING2);
                }
            }
            else
            {
                EnterState(ezFFBOSS_STATE_CHARGE);
            }
        }
        timer -= dt;
        break;
    case ezFFBOSS_STATE_TURNING:
    {
        xVec3 dirPlayer;
        dirPlayer /= xsqrt(XZDstSqToPlayer(&dirPlayer, NULL));
        F32 qdot = MatQRotTowards(dt, &dirPlayer, cfg->turn.turnRate);
        if (qdot > 0.99f)
        {
            EnterState(ezFFBOSS_STATE_CHARGE);
        }
        break;
    }
    case ezFFBOSS_STATE_TURNING2:
    {
        xVec3 dirPlayer;
        dirPlayer /= xsqrt(XZDstSqToPlayer(&dirPlayer, NULL));
        MatQRotTowards(dt, &dirPlayer, cfg->turn.turnRate);

        if (tym_animPlusTime > 0.43333334f && !landFXPlayed)
        {
            landFXPlayed = 1;
            SoundPlayEffect(eSoundStomp, -1.0f);
        }

        if (tym_animPlusTime > tym_animDur)
        {
            EnterState(ezFFBOSS_STATE_CHARGE);
        }
        break;
    }
    case ezFFBOSS_STATE_TONGUESTART:
        if (anid_played == gHashFrogFishAnim[22])
        {
            if (tym_animPlusTime > 0.8666667f && !tongueFXPlayed)
            {
                SoundPlayEffect(eSoundTongueDown, -1.0f);
                tongueFXPlayed = 1;
            }

            if (tym_animPlusTime > tym_animDur)
            {
                if (camState == ezFFBOSS_CAMERA_STATE_AFTERJUMP && camTimer < 0.0f)
                {
                    SetCamState(ezFFBOSS_CAMERA_STATE_AFTERTONGUE);
                    npc_tongue->EnterState(ezTONGUESPIN_STATE_BURROW);
                }
                else if (camState == ezFFBOSS_CAMERA_STATE_AFTERTONGUE && camTimer < 0.0f)
                {
                    EnterState(ezFFBOSS_STATE_TONGUESPIN);
                    npc_tongue->EnterState(ezTONGUESPIN_STATE_POPUP);
                }
                else if (camState == ezFFBOSS_CAMERA_STATE_REGULAR)
                {
                    EnterState(ezFFBOSS_STATE_TONGUESPIN);
                    npc_tongue->EnterState(ezTONGUESPIN_STATE_POPUP);
                }
            }
        }
        else if (anid_played == gHashFrogFishAnim[15])
        {
            xVec3 dirPlayer;
            dirPlayer /= xsqrt(XZDstSqToPlayer(&dirPlayer, NULL));
            MatQRotTowards(dt, &dirPlayer, cfg->turn.turnRate);

            if (tym_animPlusTime > tym_animDur && camTimer < 0.0f)
            {
                xVec3 newPos = *xEntGetPos(this);
                newPos.y += 3.0f;
                MatPosSet(&newPos);
                PlayExplicitAnim(gHashFrogFishAnim[22], 0);
                tongueFXPlayed = 0;
            }
        }
        else if (anid_played == gHashFrogFishAnim[5])
        {
            if (camTimer < 0.0f)
            {
                CalcJump(ezFFBOSS_LOC_TONGUE);
                PlayExplicitAnim(gHashFrogFishAnim[6], 0);
            }
        }
        else if (anid_played == gHashFrogFishAnim[1])
        {
            timer -= dt;
            if (timer < 0.0f)
            {
                SoundPlayEffect(eSoundSmash, -1.0f);
                xVec3 pos_emit = *xEntGetPos(this) + FFvec_emitOffset;
                FFdustWhirl.ImpactRing(&pos_emit, NULL);
                RumbleAndShake(1.0f);

                if (fragSlam != NULL)
                {
                    xVec3 vel = { 0.0f, 0.0f, 0.0f };
                    fragSlam->initCB(fragSlam, model, &vel, NULL);
                }

                PlayExplicitAnim(gHashFrogFishAnim[15], 0);
                if (camState != ezFFBOSS_CAMERA_STATE_REGULAR)
                {
                    SetCamState(ezFFBOSS_CAMERA_STATE_AFTERJUMP);
                }
            }
        }
        else
        {
            xAnimSingle* single = AnimCurSingle();
            single->CurrentSpeed = 0.5f;
            S32 jumpDone = EvalJump(dt, cfg->jumpToStand.duration, true);
            if (jumpDone == 1 && tym_animPlusTime > 0.43333334f)
            {
                timer = 0.25f;
                PlayExplicitAnim(gHashFrogFishAnim[1], 0);
            }
        }
        break;
    case ezFFBOSS_STATE_TONGUESPIN:
        if (anid_played == gHashFrogFishAnim[32] && tym_animPlusTime > 0.5f && tym_animCurr <= 0.5f)
        {
            ReadyAimFire();
        }
        break;
    case ezFFBOSS_STATE_TAUNT_HIDE:
        if (anid_played == gHashFrogFishAnim[24])
        {
            if (tym_animPlusTime > tym_animDur)
            {
                PlayExplicitAnim(gHashFrogFishAnim[21], 0);
                SoundPlayEffect(eSoundTongueTaunt, -1.0f);
            }
        }
        else if (anid_played == gHashFrogFishAnim[21] && tym_animPlusTime > tym_animDur)
        {
            EnterState(ezFFBOSS_STATE_TONGUESTART);
        }
        break;
    case ezFFBOSS_STATE_TONGUEDAMAGE:
        if (anid_played == gHashFrogFishAnim[25])
        {
            if (tym_animPlusTime > tym_animDur)
            {
                if (ptsHealth < 1)
                {
                    PlayExplicitAnim(gHashFrogFishAnim[19], 0);
                    SoundPlayEffect(eSoundRoar, -1.0f);
                }
                else if (damageTakenAttackCurr >= gFFBossDamageToSwitchAttack[stageCurr])
                {
                    if (gFFBossAttackTypes[stageCurr][attackIndex] !=
                        gFFBossAttackTypes[stageCurr][attackIndex + 1])
                    {
                        PlayExplicitAnim(gHashFrogFishAnim[19], 0);
                        SoundPlayEffect(eSoundRoar, -1.0f);
                    }
                    else
                    {
                        AttackIncrement();
                    }
                }
                else
                {
                    EnterState(ezFFBOSS_STATE_TONGUESTART);
                }
            }
        }
        else if (anid_played == gHashFrogFishAnim[19] && tym_animPlusTime > tym_animDur)
        {
            if (ptsHealth < 1)
            {
                StageIncrement();
                EnterState(ezFFBOSS_STATE_TONGUESTART);
            }
            else if (damageTakenAttackCurr >= gFFBossDamageToSwitchAttack[stageCurr])
            {
                AttackIncrement();
            }
        }
        break;
    case ezFFBOSS_STATE_ATTACKAPREP:
    {
        S32 jumpDone = EvalJump(dt, cfg->fall.duration, true);
        if (jumpDone == 1)
        {
            EnterState(ezFFBOSS_STATE_TURNING2);
            landFXPlayed = 1;
            SoundPlayEffect(eSoundLand, -1.0f);
            RumbleAndShake(1.0f);
        }
        break;
    }
    case ezFFBOSS_STATE_TONGUESTUN:
        if (anid_played == gHashFrogFishAnim[26] && tym_animPlusTime > tym_animDur)
        {
            PlayExplicitAnim(gHashFrogFishAnim[27], 0);
        }
        break;
    case ezFFBOSS_STATE_TONGUESTUNDONE:
        if (tym_animPlusTime > tym_animDur)
        {
            EnterState(ezFFBOSS_STATE_TONGUESPIN);
        }
        break;
    case ezFFBOSS_STATE_DYING:
        if (!xSndMgrIsPlaying(deadSndHandle))
        {
            EnterState(ezFFBOSS_STATE_DEAD);
        }
        break;
    case ezFFBOSS_STATE_DEAD:
        break;
    }

    OneLinersUpdate(dt);
    zNMECommon::Process(dt);
}

void zNMEFrogfish::DoDamageAndKnockback()
{
    xVec3* pos_player = xEntGetPos(xglobals->___player_ent_dont_use_directly);
    xVec3* pos = xEntGetPos(this);
    xVec3 knockback;

    knockback.x = pos_player->x - pos->x;
    knockback.y = 0.0f;
    knockback.z = pos_player->z - pos->z;
    knockback.normalize();
    knockback *= cfg->attackA.knockbackDist;

    zEntPlayer_Damage(this, 1, &knockback);
    FFOneLiners.PlaySound(1);
}

void zNMEFrogfish::BUpdate(xVec3* pos)
{
    if (cfg != NULL)
    {
        bound.sph.center = *(xVec3*)&model->Mat[3].pos;
        xMat3x3RMulVec(&bound.sph.center, (xMat3x3*)model->Mat, &bound.sph.center);
        bound.sph.center += *(xVec3*)&model->Mat->pos;

        F32 radBound = 6.0f;
        xVec3 vecBoundShift = { 0.0f, -4.0f, -1.0f };

        if (state == ezFFBOSS_STATE_TEETER || state == ezFFBOSS_STATE_CHARGE)
        {
            vecBoundShift = cfg->health.vecBoundShift;
            radBound = cfg->health.radBound;
        }

        bound.sph.r = radBound;
        bound.sph.center.y += radBound;

        bound.sph.center += *(xVec3*)&model->Mat->right * vecBoundShift.x;
        bound.sph.center += *(xVec3*)&model->Mat->up * vecBoundShift.y;
        bound.sph.center += *(xVec3*)&model->Mat->at * vecBoundShift.z;

        xBoundUpdate(&bound);
        zGridUpdateEnt(this);
    }
}

bool zNMEFrogfish::IsTeeterOk()
{
    xVec3 dirCharge = chargeVelDir;
    dirCharge.normalize();

    xVec3 posAhead = *xEntGetPos(this) + dirCharge;

    xVec3 dirCenter;
    dirCenter.x = posAhead.x - cfg->arena.centerX;
    dirCenter.y = 0.0f;
    dirCenter.z = posAhead.z - cfg->arena.centerZ;
    dirCenter.normalize();

    return !(xabs(xVec3Dot(&dirCharge, &dirCenter)) < 0.85f);
}

// Both teeterSndHandle stores are retail's (the PS2 build has one).
void zNMEFrogfish::EnterState(eFFBossState newState)
{
    if (state == newState)
    {
        return;
    }

    if (state == ezFFBOSS_STATE_DYING)
    {
        if (newState == ezFFBOSS_STATE_DEAD)
        {
            state = newState;
            zEntEvent(NULL, 0, this, eEventDeath, NULL, NULL, 0, FE_NO);
        }
        return;
    }

    state = newState;

    switch (state)
    {
    case ezFFBOSS_STATE_IDLE:
        timer = 2.0f;
        break;
    case ezFFBOSS_STATE_LUNGE:
        meleeSuccessfull = 0;
        PlayExplicitAnim(gHashFrogFishAnim[8], 0);
        timer = 2.0f * AnimDuration(NULL);
        break;
    case ezFFBOSS_STATE_CHARGE:
    {
        xVec3 dirFFToPlayer;
        F32 dy;
        XZVecToPos(&dirFFToPlayer, xEntGetPos(xglobals->___player_ent_dont_use_directly), &dy);
        dirFFToPlayer.normalize();
        chargeVelDir = dirFFToPlayer;
        chargeVelMagnitude = cfg->attackA.chargeSpeed;

        xVec3 chargeVel = chargeVelDir * chargeVelMagnitude;
        F32 mag = xVec3Length(&chargeVel);
        if (mag > 0.00001f)
        {
            xMat3x3 mat_rot;
            xVec3 dir = -chargeVel * (1.0f / mag);
            xMat3x3LookVec(&mat_rot, &dir);
            *(xMat3x3*)model->Mat = mat_rot;
        }

        PlayExplicitAnim(gHashFrogFishAnim[11], 0);
        xSndMgrPlay(sSoundAsset[eSoundChargePrep], 2, NULL, NULL, this, NULL, NULL);
        npc_tongue->EnterState(ezTONGUESPIN_STATE_HIDDEN);
        break;
    }
    case ezFFBOSS_STATE_STOPPING:
        xSndMgrPlay(sSoundAsset[eSoundSkid], 2, NULL, NULL, this, NULL, NULL);
        PlayExplicitAnim(gHashFrogFishAnim[13], 0);
        timer = 0.75f;
        fxTimer = 0.0f;
        break;
    case ezFFBOSS_STATE_WAIT_IN_PIT:
        xSndMgrPlay(sSoundAsset[eSoundLand], 2, NULL, NULL, this, NULL, NULL);
        timer = 2.0f;
        roarInTrenchPlayed = 0;
        break;
    case ezFFBOSS_STATE_DAMAGE:
        xSndMgrStop(teeterSndHandle);
        teeterSndHandle = (iSndHandle)-1;
        teeterSndHandle = (iSndHandle)-1;
        xSndMgrPlay(sSoundAsset[eSoundYelp], 2, NULL, NULL, this, NULL, NULL);
        PlayExplicitAnim(gHashFrogFishAnim[5], 0);
        FFOneLiners.PlaySound(eFFOneLinerFFHit);
        break;
    case ezFFBOSS_STATE_TEETER:
        PlayExplicitAnim(gHashFrogFishAnim[14], 0);
        break;
    case ezFFBOSS_STATE_QUICKTAUNT:
        PlayExplicitAnim(gHashFrogFishAnim[1], 0);
        break;
    case ezFFBOSS_STATE_TAUNT:
        xSndMgrPlay(sSoundAsset[eSoundSkid], 2, NULL, NULL, this, NULL, NULL);
        PlayExplicitAnim(gHashFrogFishAnim[13], 0);
        timer = 0.75f;
        fxTimer = 0.0f;
        break;
    case ezFFBOSS_STATE_TURNING:
        PlayExplicitAnim(gHashFrogFishAnim[1], 0);
        XZVecToPos(&dirDest, xEntGetPos(xglobals->___player_ent_dont_use_directly), NULL);
        dirDest.normalize();
        break;
    case ezFFBOSS_STATE_TURNING2:
        landFXPlayed = 0;
        PlayExplicitAnim(gHashFrogFishAnim[15], 0);
        break;
    case ezFFBOSS_STATE_TONGUESTART:
        if (IsAtTongue(this))
        {
            PlayExplicitAnim(gHashFrogFishAnim[22], 0);
            tongueFXPlayed = 0;
        }
        else if (firstTimeTongue)
        {
            SetCamState(ezFFBOSS_CAMERA_STATE_PREPJUMP);
            firstTimeTongue = 0;
        }
        else
        {
            CalcJump(ezFFBOSS_LOC_TONGUE);
            PlayExplicitAnim(gHashFrogFishAnim[6], 0);
        }
        break;
    case ezFFBOSS_STATE_TONGUESPIN:
        if (!IsStageHealthDown())
        {
            PlayExplicitAnim(gHashFrogFishAnim[23], 0);
        }
        else
        {
            PlayExplicitAnim(gHashFrogFishAnim[32], 0);
        }
        break;
    case ezFFBOSS_STATE_TAUNT_HIDE:
        PlayExplicitAnim(gHashFrogFishAnim[24], 0);
        break;
    case ezFFBOSS_STATE_TONGUEDAMAGE:
        PlayExplicitAnim(gHashFrogFishAnim[25], 0);
        xSndMgrPlay(sSoundAsset[eSoundTongueBite], 2, NULL, NULL, this, NULL, NULL);
        FFOneLiners.PlaySound(eFFOneLinerFFHit);
        break;
    case ezFFBOSS_STATE_TONGUESTUN:
        PlayExplicitAnim(gHashFrogFishAnim[26], 0);
        break;
    case ezFFBOSS_STATE_TONGUESTUNDONE:
        PlayExplicitAnim(gHashFrogFishAnim[28], 0);
        break;
    case ezFFBOSS_STATE_ATTACKAPREP:
    {
        xVec3 newPos = *Pos();
        newPos.y -= 3.0f;
        MatPosSet(&newPos);
        CalcJump(ezFFBOSS_LOC_OPPOSITE_PLAYER);
        PlayExplicitAnim(gHashFrogFishAnim[6], 0);
        break;
    }
    case ezFFBOSS_STATE_DYING:
        PlayExplicitAnim(gHashFrogFishAnim[20], 0);
        deadSndHandle = xSndMgrPlay(sSoundAsset[eSoundDead], 2, NULL, NULL, this, NULL, NULL);
        zEntEvent(NULL, 0, this, eEventBossStageEndedC, NULL, NULL, 0, FE_NO);
        break;
    case ezFFBOSS_STATE_DEAD:
        break;
    }
}

xAnimTable* zNMEFrogfish::CreateAnimTable(xAnimTable* table)
{
    if (table == NULL)
    {
        table = xAnimTableNew("zNMEFrogFish", 0);
    }

    xAnimTableNewState(table, gStrFrogFishAnim[1], 0x10, 1, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, gStrFrogFishAnim[2], 0x10, 1, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, gStrFrogFishAnim[3], 0x10, 1, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, gStrFrogFishAnim[4], 0x10, 1, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, gStrFrogFishAnim[5], 0, 1, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, gStrFrogFishAnim[6], 0, 1, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, gStrFrogFishAnim[7], 0, 1, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, gStrFrogFishAnim[8], 0x10, 1, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, gStrFrogFishAnim[9], 0x10, 1, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, gStrFrogFishAnim[10], 0, 1, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, gStrFrogFishAnim[11], 0, 1, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, gStrFrogFishAnim[12], 0x10, 1, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, gStrFrogFishAnim[13], 0x10, 1, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, gStrFrogFishAnim[14], 0, 1, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, gStrFrogFishAnim[15], 0, 1, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, gStrFrogFishAnim[16], 0x10, 1, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, gStrFrogFishAnim[17], 0, 1, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, gStrFrogFishAnim[18], 0, 1, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, gStrFrogFishAnim[19], 0, 1, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, gStrFrogFishAnim[20], 0, 1, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, gStrFrogFishAnim[21], 0, 1, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, gStrFrogFishAnim[22], 0, 1, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, gStrFrogFishAnim[23], 0x10, 1, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, gStrFrogFishAnim[24], 0, 1, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, gStrFrogFishAnim[25], 0, 1, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, gStrFrogFishAnim[26], 0, 1, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, gStrFrogFishAnim[27], 0x10, 1, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, gStrFrogFishAnim[28], 0, 1, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, gStrFrogFishAnim[29], 0, 1, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, gStrFrogFishAnim[30], 0, 1, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, gStrFrogFishAnim[31], 0, 1, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, gStrFrogFishAnim[32], 0x10, 1, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);

    return table;
}

U32 zNMEFrogfish::PlayExplicitAnim(U32 anid, S32 forceRestart)
{
    if (AnimStart(anid, forceRestart))
    {
        anid_played = anid;
    }
    else
    {
        anid_played = 0;
    }

    return anid_played;
}

// The final else holds only a compiled-out assert (condition unknown). It keeps CalcPos from being
// an auto-inline candidate, so CalcJump is inlined into EnterState as in retail.
void zNMEFrogfish::CalcPos(xVec3* posDest, eFFBossLocation loc)
{
    if (loc == ezFFBOSS_LOC_TONGUE)
    {
        *posDest = svTongue;
    }
    else if (loc == ezFFBOSS_LOC_OPPOSITE_PLAYER)
    {
        xVec3 vCenter = { cfg->arena.centerX, 0.0f, cfg->arena.centerZ };
        xVec3 vCenterToEdge = *xEntGetPos(xglobals->___player_ent_dont_use_directly) - vCenter;
        vCenterToEdge.y = 0.0f;
        vCenterToEdge.normalize();
        vCenterToEdge *= -cfg->arena.innerRadius;

        *posDest = vCenter + vCenterToEdge;
        posDest->y = 0.0f;
    }
    else if (loc == ezFFBOSS_LOC_SAME_SIDE_PLAYER)
    {
        xVec3 vCenter = { cfg->arena.centerX, 0.0f, cfg->arena.centerZ };
        xVec3 vCenterToEdge = *xEntGetPos(this) - vCenter;
        vCenterToEdge.y = 0.0f;
        vCenterToEdge.normalize();
        vCenterToEdge *= cfg->arena.innerRadius;

        *posDest = vCenter + vCenterToEdge;
        posDest->y = 0.0f;
    }
    else if (loc == ezFFBOSS_LOC_TONGUE_TRENCH)
    {
        xVec3 vCenter = { cfg->arena.centerX, 0.0f, cfg->arena.centerZ };
        xVec3 vCenterToEdge = svTongue - vCenter;
        vCenterToEdge.y = 0.0f;
        vCenterToEdge.normalize();
        vCenterToEdge *= cfg->arena.radius;

        *posDest = vCenter + vCenterToEdge;
        posDest->y = 0.0f;
    }
    else
    {
        xASSERT_PLACEHOLDER(); // unknown location
    }
}

S32 zNMEFrogfish::TypeHandleMail(NMEMsg* mail)
{
    S32 handled;

    switch (mail->msgid)
    {
    case NME_MID_DAMAGE:
        handled = FrogfishMailDamage(&mail->dmgdata);
        break;
    case NME_MID_SYSEVENT:
        handled = FrogfishMailSys(&mail->sysevent);
        break;
    default:
        handled = 0;
        break;
    }

    return handled;
}

S32 zNMEFrogfish::FrogfishMailDamage(NMEDamageInfo* dmgmail)
{
    S32 handled;
    S32 amt_damage;
    en_plyrpup pup_level;

    if (state != ezFFBOSS_STATE_TEETER)
    {
        return 1;
    }

    handled = 1;

    switch (dmgmail->dmg_type)
    {
    case DMGTYP_ABOVE:
    case DMGTYP_BELOW:
    case DMGTYP_SIDE:
    case DMGTYP_INSTAKILL:
    case DMGTYP_KILLEVENT:
    case DMGTYP_NMEATTACK:
    case DMGTYP_HITBYTOSS:
    case DMGTYP_NMEKNOCKED:
    case DMGTYP_ROPE:
    case DMGTYP_CRUISEBUBBLE:
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
        amt_damage = NMEC_DmgAmtForAttack(dmgmail->dmg_type, &pup_level);
        if (state == ezFFBOSS_STATE_DAMAGE)
        {
            break;
        }

        ptsHealth -= amt_damage;
        damageTakenAttackCurr += amt_damage;
        if (ptsHealth < 0)
        {
            ptsHealth = 0;
        }

        if (ptsHealth < 1 && stageCurr == ezFFBOSS_STAGE_C)
        {
            EnterState(ezFFBOSS_STATE_DYING);
        }
        EnterState(ezFFBOSS_STATE_DAMAGE);
        break;
    case DMGTYP_FRIENDLYFIRE:
        break;
    default:
        handled = 0;
        break;
    }

    return handled;
}

// No GC symbol (the name is ours): a plain member like zNMEDennis::DenMailSys, inlined into its
// only caller, TypeHandleMail.
S32 zNMEFrogfish::FrogfishMailSys(NMESysEvent* sysmail)
{
    S32 handled = 1;

    switch (sysmail->toEvent)
    {
    case eEventBossStageSet:
        SetTheStage(ezFFBOSS_STAGE_A);
        break;
    case eEventSceneBegin:
    case eEventSceneEnd:
        break;
    default:
        handled = DfltSysEventMail(sysmail);
        break;
    }

    return handled;
}

void zNMEFrogfish::StageIncrement()
{
    S32 oldAttack = gFFBossAttackTypes[stageCurr][attackIndex];

    if (stageCurr == ezFFBOSS_STAGE_A)
    {
        SetTheStage(ezFFBOSS_STAGE_B);
    }
    else if (stageCurr == ezFFBOSS_STAGE_B)
    {
        SetTheStage(ezFFBOSS_STAGE_C);
    }
    else if (stageCurr == ezFFBOSS_STAGE_C)
    {
        SetTheStage(ezFFBOSS_STAGE_OUTRO);
    }
    attackIndex = 0;

    if (stageCurr != ezFFBOSS_STAGE_OUTRO)
    {
        attackCurr = gFFBossAttackTypes[stageCurr][attackIndex];
    }

    damageTakenAttackCurr = 0;

    if (stageCurr == ezFFBOSS_STAGE_OUTRO)
    {
        EnterState(ezFFBOSS_STATE_DYING);
    }
    else if (oldAttack == attackCurr)
    {
        if (attackCurr == 0)
        {
            EnterState(ezFFBOSS_STATE_TURNING2);
        }
        else if (attackCurr == 1)
        {
            EnterState(ezFFBOSS_STATE_TONGUESTART);
        }
        else if (attackCurr == 2)
        {
            EnterState(ezFFBOSS_STATE_TONGUESTART);
        }
    }
    else
    {
        if (attackCurr == 0)
        {
            EnterState(ezFFBOSS_STATE_ATTACKAPREP);
        }
        else if (attackCurr == 1)
        {
            EnterState(ezFFBOSS_STATE_TONGUESTART);
        }
        else if (attackCurr == 2)
        {
            EnterState(ezFFBOSS_STATE_TONGUESTART);
        }
    }
}

void zNMEFrogfish::AttackIncrement()
{
    S32 oldAttack;

    oldAttack = gFFBossAttackTypes[stageCurr][attackIndex];
    attackIndex++;
    attackCurr = gFFBossAttackTypes[stageCurr][attackIndex];

    if (stageCurr == ezFFBOSS_STAGE_C && ptsHealth <= 4 && attackCurr == 0)
    {
        attackCurr = 1;
    }

    damageTakenAttackCurr = 0;

    if (oldAttack == attackCurr)
    {
        if (attackCurr == 0)
        {
            EnterState(ezFFBOSS_STATE_TURNING2);
        }
        else if (attackCurr == 1)
        {
            EnterState(ezFFBOSS_STATE_TONGUESTART);
        }
        else
        {
            EnterState(ezFFBOSS_STATE_TONGUESTART);
        }
    }
    else
    {
        if (attackCurr == 0)
        {
            EnterState(ezFFBOSS_STATE_ATTACKAPREP);
        }
        else if (attackCurr == 1)
        {
            EnterState(ezFFBOSS_STATE_TONGUESTART);
        }
        else
        {
            EnterState(ezFFBOSS_STATE_TONGUESTART);
        }
    }
}

bool zNMEFrogfish::IsTongueAttackNext()
{
    if (ptsHealth < 1)
    {
        return true;
    }

    if (damageTakenAttackCurr >= gFFBossDamageToSwitchAttack[stageCurr])
    {
        return gFFBossAttackTypes[stageCurr][attackIndex + 1] != 0;
    }

    return false;
}

S32 zNMEFrogfish::ReadyAimFire()
{
    if (!IsStageHealthDown())
    {
        return 0;
    }

    xMat4x3* mat_root = (xMat4x3*)model->Mat;
    xVec3 pos_launch = cfg->attackC.vecOffsetLaunch;
    xMat4x3Toworld(&pos_launch, &mat_root[37], &pos_launch);
    xMat4x3Toworld(&pos_launch, mat_root, &pos_launch);

    S32 launched =
        LaunchProjectile(cfg->attackC.haztype, cfg->attackC.spdProj, cfg->attackC.dstMinRange,
                         &pos_launch, cfg->attackC.tymLeadTarget, 0.0f, cfg->attackC.radMin,
                         cfg->attackC.radMax);
    xSndMgrPlay(sSoundAsset[eSoundSpit], 2, NULL, NULL, this, NULL, NULL);

    return launched;
}

void zNMEFrogfish::CalcJump(eFFBossLocation loc)
{
    F32 gravity;
    F32 duration;
    xVec3 pos_dest;

    if (loc == ezFFBOSS_LOC_TONGUE)
    {
        gravity = cfg->jumpToStand.gravity;
        duration = cfg->jumpToStand.duration;
    }
    else
    {
        gravity = cfg->fall.gravity;
        duration = cfg->fall.duration;
    }

    CalcPos(&pos_dest, loc);

    xVec3 vel = pos_dest - *xEntGetPos(this);
    vel *= 1.0f / duration;
    vel.y += gravity * (0.5f * duration);

    jumpParabola.initPos = *xEntGetPos(this);
    jumpParabola.initVel = vel;
    jumpParabola.gravity = gravity;
    jumpParabola.minTime = 0.0f;
    jumpParabola.maxTime = duration;
    timeLeaping = 0.0f;
}

S32 zNMEFrogfish::EvalJump(F32 dt, F32 duration, bool setOrient)
{
    S32 arrived = 0;
    F32 mag;
    xVec3 pos;
    xVec3 vel;
    xMat3x3 mat_rot;

    mag = jumpParabola.initVel.length();
    if (mag < 0.00001f)
    {
        return 1;
    }

    timeLeaping += dt;
    if (timeLeaping >= duration)
    {
        arrived = 1;
    }
    timeLeaping = CLAMP(timeLeaping, 0.0f, duration);

    xParabolaEvalPos(&jumpParabola, &pos, timeLeaping);
    xParabolaEvalVel(&jumpParabola, &vel, timeLeaping);
    model->Mat->pos.x = pos.x;
    model->Mat->pos.y = pos.y;
    model->Mat->pos.z = pos.z;

    if (setOrient && mag > 0.00001f)
    {
        xVec3 dir = -vel * (1.0f / mag);
        dir.y = 0.0f;
        xMat3x3LookVec(&mat_rot, &dir);
        *(xMat3x3*)model->Mat = mat_rot;
        MatQRotTowards(dt, &dir, cfg->turn.turnRate);
    }

    return arrived;
}

S32 zNMEFrogfish::IsAlive()
{
    return state != ezFFBOSS_STATE_DEAD;
}

// No symbol (inlined into Reset); the name is ours. Retail's constant order shows it was a plain
// function defined here.
void zNMEFrogfish::OneLinersReset()
{
    sbHintTimer = 8.0f;
    sbCommentTimer = 13.0f;
    patCommentTimer = 18.0f;
}

void zNMEFrogfish::OneLinersUpdate(F32 dt)
{
    eFFOneLiner commentToPlay;

    sbHintTimer -= dt;
    if (sbHintTimer < 0.0f)
    {
        commentToPlay = eFFOneLinerSBHintsAttack0;
        if (attackCurr == 1)
        {
            commentToPlay = eFFOneLinerSBHintsAttack1;
        }
        else if (attackCurr == 2)
        {
            commentToPlay = eFFOneLinerSBHintsAttack2;
        }
        else if (state == ezFFBOSS_STATE_TEETER && (xrand_GenRandInt32() & 1))
        {
            commentToPlay = eFFOneLinerSBHintsNow;
        }

        FFOneLiners.PlaySound(commentToPlay);
        sbHintTimer = 15.0f;
    }

    sbCommentTimer -= dt;
    if (sbCommentTimer < 0.0f)
    {
        FFOneLiners.PlaySound(eFFOneLinerSBComment);
        sbCommentTimer = 15.0f;
    }

    patCommentTimer -= dt;
    if (patCommentTimer < 0.0f)
    {
        if (IsPhlegmOk() && (S32)xrand_GenRandInt32() % 5 == 0)
        {
            FFOneLiners.PlaySound(eFFOneLinerPatPhlegm);
        }
        else
        {
            FFOneLiners.PlaySound(eFFOneLinerPatComment);
        }
        patCommentTimer = 15.0f;
    }

    if (!sbIntroPlayed)
    {
        sbIntroTimer -= dt;
        if (sbIntroTimer < 0.0f)
        {
            FFOneLiners.PlaySound(eFFOneLinerSBIntro);
            sbIntroPlayed = 1;
        }
    }

    if (!patIntroPlayed)
    {
        patIntroTimer -= dt;
        if (patIntroTimer < 0.0f)
        {
            FFOneLiners.PlaySound(eFFOneLinerPatIntro);
            // Retail bug (PS2 demo too): the flag is cleared instead of set, so Pat's intro line
            // repeats every frame once the timer has run out.
            patIntroPlayed = 0;
        }
    }
}

void zNMEFrogfish::RenderHud()
{
    MeterRender();
}

void zNMEFrogfish::MeterUpdate()
{
    if (stageCurr == ezFFBOSS_STAGE_A)
    {
        FFMeterHealth.status.healthCurr[1] = ptsHealth;
    }
    else if (stageCurr == ezFFBOSS_STAGE_B)
    {
        FFMeterHealth.status.healthCurr[2] = ptsHealth;
    }
    else if (stageCurr == ezFFBOSS_STAGE_C)
    {
        FFMeterHealth.status.healthCurr[3] = ptsHealth;
    }
}

void zNMEFrogfish::MeterRender()
{
    FFMeterHealth.Render();
}
