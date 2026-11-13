#include "zNMETypeFrdMindy.h"
#include "zCamera.h"
#include "zEnt.h"
#include "zEntPlayer.h"
#include "zGlobals.h"
#include "zMusic.h"
#include "zSaveLoad.h"
#include "zScene.h"
#include "zTaskBox.h"
#include "zTextBox.h"

char* zNMEMindy::mindyAnims[13] = {
    "Unknown", "Celebration01", "Idle01", "Idle02", "Idle03", "Idle04", "Close01",
    "Open01",  "Opened01",      "Talk01", "Talk02", "Talk03", "Talk04",
};

U32 zNMEMindy::hashMindyAnims[13];

TVStaticTimes zNMEMindy::tvStaticTimes[7] = {
    { 0.0f, 1, 0 }, { 0.1f, 0, 1 }, { 0.2f, 1, 0 }, { 0.3f, 0, 1 },
    { 0.4f, 1, 0 }, { 0.5f, 0, 1 }, { 0.6f, 0, 1 },
};

TVStaticTimes zNMEMindy::mindyAppearTimes[7] = {
    { 0.0f, 1, 0 },  { 0.05f, 0, 1 }, { 0.15f, 1, 0 }, { 0.25f, 0, 1 },
    { 0.35f, 1, 0 }, { 0.45f, 0, 1 }, { 0.55f, 0, 1 },
};

TVStaticTimes* zNMEMindy::currentStaticTimes;

F32 zNMEMindy::oldMusicVolume;

namespace Mindy
{
    NMEHelpBound::BndData bindInfo = {
        k_XBOUNDTYPE_SPHERE,
        { 0, 0, { 0, 0 } },
        { 0.7f, { 0.0f, 0.75f, 0.35f } },
        { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } },
    };
}

// No GC symbol: inlined into zNMECentral_Startup (its unused copy is dead-stripped).
void ZNME_Friendly_Startup()
{
    for (S32 i = 0; i < 9; i++)
    {
        g_hash_chumanim[i] = xStrHash(g_strz_chumanim[i]);
    }
    for (S32 i = 0; i < 13; i++)
    {
        zNMEMindy::hashMindyAnims[i] = xStrHash(zNMEMindy::mindyAnims[i]);
    }
}

void zNMEMindy::PostSetup()
{
    matCopy = *model->Mat;
}

void zNMEMindy::Init(xEntAsset* asset)
{
    zNMEFriendly::Init(asset);

    flags &= (U8)~0x40;
}

void zNMEMindy::Setup()
{
    mindyAsset = (zNMEAssetMindy*)nmeass;
    currentState = eOutofRange;

    mindyModelID = asset->modelInfoID;
    celebrationAnimPlay = 0;
    currentShellState = eShellClosed;
    tvStaticTimer = 0.0f;
    tvStaticTimeIndex = 0;
    wiggleAmplitude = 0.003f;
    wiggleFrequency = 1.0f;

    makeMindyAppear = 0;
    makeMindyDisappear = 0;

    fadeStep = 0.18f;
    currentTVStaticState = eTVStatic;

    yaw = pitch = roll = 0.0f;
    offsetY = 0.5f;
    offsetYTimer = 0.5f;

    yawAmplitude = 0.0f;
    yawFrequency = 0.0f;

    pitchAmplitude = 0.0f;
    pitchFrequency = 0.5f;

    rollAmplitude = 0.1f;
    rollFrequency = 1.2f;

    quatT = 0.0f;
    goingDownTimeMultiplier = 4.0f;
    updateCullDistanceXZ = 30.0f;
    goingUpTimeMultiplier = 1.0f;

    clamOpenDistanceSqXZ = SQR(mindyAsset->clamOpenDistance);
    clamCloseDistanceSqXZ = SQR(mindyAsset->clamCloseDistance);
    updateCullDistanceSqXZ = SQR(updateCullDistanceXZ);

    taskBox = (ztaskbox*)zSceneFindObject(mindyAsset->taskBoxID);
    textBox = (ztextbox*)zSceneFindObject(mindyAsset->textBoxID);
    secondaryTaskBox = (ztaskbox*)zSceneFindObject(mindyAsset->secondaryTaskBoxID);
}

void zNMEMindy::Reset()
{
    zNMEFriendly::Reset();

#ifdef PLATFORM_PC
    // PLATFORM_PC: the callbacks below take what the animation passes them, the xEnt (they
    // say why), so this passes the xEnt too.
    CloseShellDone(NULL, NULL, (xEnt*)this);
#else
    CloseShellDone(NULL, NULL, this);
#endif

    offsetY = 0.5f;
    offsetYTimer = 0.5f;
}

void zNMEMindy::Destroy()
{
    zNMEFriendly::Destroy();
}

// PLATFORM_PC: an animation callback's object is the xEnt (xAnimPlay::Object: zEntParseModelInfo
// sets up Mindy's animation with her xEnt, and the player's callbacks and soundEffectCB read it
// as one). A host compiler puts an enemy's vptr at offset 0, ahead of its xEnt, so a cast from
// void* to zNMEMindy* is 8 bytes (x86: 4) off: the shell and celebration callbacks read and wrote
// whatever lies that far past the fields they name (on x64 the celebration check read
// tvStaticTimeIndex). Each callback reaches her by a cast through xEnt*, which adjusts.
U32 zNMEMindy::SpatulaGrabCB(xAnimTransition* tran, xAnimSingle* anim, void* object)
{
#ifdef PLATFORM_PC
    ((zNMEMindy*)(xEnt*)object)->celebrationAnimPlay = 0;
#else
    ((zNMEMindy*)object)->celebrationAnimPlay = 0;
#endif

    zCameraSetReward(1);
    zCameraDisableInput();

    if (!(globals.player.ControlOff & CONTROL_OWNER_TALK_BOX))
    {
        oldMusicVolume = zMusicGetCurrentVolume(0);
        zMusicSetVolume(gDialogueMusicLevel, 0.2f);
    }

    return 1;
}

U32 zNMEMindy::SpatulaGrabStopCB(xAnimTransition* tran, xAnimSingle* anim, void* object)
{
#ifdef PLATFORM_PC
    if (((zNMEMindy*)(xEnt*)object)->itemType == NME_TYPE_SPONGEBOB &&
        xStrHash(anim->State->Name) == hashMindyAnims[1])
#else
    if (((zNMEMindy*)object)->itemType == NME_TYPE_SPONGEBOB &&
        xStrHash(anim->State->Name) == hashMindyAnims[1])
#endif
    {
        if (globals.autoSaveFeature)
        {
            zSaveLoad_TriggerAutoSave();
        }

        zCameraEnableTracking(CO_REWARDANIM);
        xCameraSetFOV(&xglobals->oldSkoolCamera, 75.0f);
        zCameraSetReward(0);

        if (!(globals.player.ControlOff & CONTROL_OWNER_TALK_BOX))
        {
            zMusicSetVolume(oldMusicVolume, 0.75f);
        }

        zCameraEnableInput();
        zEntPlayerControlOn(CONTROL_OWNER_REWARDANIM);
        zEntEventAllOfType(eEventTokenPickupComplete, eBaseTypeDispatcher);
    }

    return 0;
}

U32 zNMEMindy::SpatulaGrabCheck(xAnimTransition* tran, xAnimSingle* anim, void* object)
{
#ifdef PLATFORM_PC
    return ((zNMEMindy*)(xEnt*)object)->celebrationAnimPlay;
#else
    return ((zNMEMindy*)object)->celebrationAnimPlay;
#endif
}

U32 zNMEMindy::OpenShellCheck(xAnimTransition* tran, xAnimSingle* anim, void* object)
{
#ifdef PLATFORM_PC
    return ((zNMEMindy*)(xEnt*)object)->currentShellState == eShellOpening;
#else
    return ((zNMEMindy*)object)->currentShellState == eShellOpening;
#endif
}

U32 zNMEMindy::OpenShellDone(xAnimTransition* tran, xAnimSingle* anim, void* object)
{
#ifdef PLATFORM_PC
    ((zNMEMindy*)(xEnt*)object)->currentShellState = eShellOpened;
#else
    ((zNMEMindy*)object)->currentShellState = eShellOpened;
#endif
    return 1;
}

U32 zNMEMindy::CloseShellCheck(xAnimTransition* tran, xAnimSingle* anim, void* object)
{
#ifdef PLATFORM_PC
    return ((zNMEMindy*)(xEnt*)object)->currentShellState == eShellClosing;
#else
    return ((zNMEMindy*)object)->currentShellState == eShellClosing;
#endif
}

U32 zNMEMindy::CloseShellDone(xAnimTransition* tran, xAnimSingle* anim, void* object)
{
    zNMEMindy* ent;
    xModelInstance* tvStaticAtomic;

#ifdef PLATFORM_PC
    ent = (zNMEMindy*)(xEnt*)object;
#else
    ent = (zNMEMindy*)object;
#endif

    ent->currentShellState = eShellClosed;
    ent->currentTVStaticState = eTVStatic;

    tvStaticAtomic = ent->ModelAtomicFind(1, -1, NULL);
    if (tvStaticAtomic != NULL)
    {
        tvStaticAtomic->Alpha = 1.0f;
    }

    tvStaticAtomic = ent->ModelAtomicFind(2, -1, NULL);
    if (tvStaticAtomic != NULL)
    {
        tvStaticAtomic->Alpha = 1.0f;
    }

    return 1;
}

xAnimTable* zNMEMindy::CreateAnimTable(xAnimTable* table)
{
    char** names;

    if (table == NULL)
    {
        table = xAnimTableNew("zNMEMindy", 0);
    }

    names = mindyAnims;

    xAnimTableNewState(table, mindyAnims[2], 0x10, 1, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, mindyAnims[3], 0x10, 1, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, mindyAnims[4], 0x10, 1, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, mindyAnims[5], 0x10, 1, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);

    xAnimTableNewState(table, mindyAnims[9], 0x10, 1, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, mindyAnims[10], 0x10, 1, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, mindyAnims[11], 0x10, 1, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, mindyAnims[12], 0x10, 1, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);

    if (itemType != NME_TYPE_MINDY)
    {
        xAnimTableNewState(table, names[1], 0x20, 0x80, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                           xAnimDefaultBeforeEnter, NULL, NULL);
        xAnimTableNewTransition(table, "Idle01 Idle02 Idle03 Idle04", names[1], SpatulaGrabCheck,
                                SpatulaGrabCB, 0, 0, 0.0f, 0.0f, 5, 0, 0.15f, NULL);
        xAnimTableNewTransition(table, names[1], names[2], SpatulaGrabStopCB, NULL, 0x10, 0, 0.0f,
                                0.0f, 4, 0, 0.15f, NULL);
        xAnimTableNewTransition(table, names[1], names[3], SpatulaGrabStopCB, NULL, 0x10, 0, 0.0f,
                                0.0f, 3, 0, 0.15f, NULL);
        xAnimTableNewTransition(table, names[1], names[4], SpatulaGrabStopCB, NULL, 0x10, 0, 0.0f,
                                0.0f, 2, 0, 0.15f, NULL);
        xAnimTableNewTransition(table, names[1], names[5], SpatulaGrabStopCB, NULL, 0x10, 0, 0.0f,
                                0.0f, 1, 0, 0.15f, NULL);
    }
    else
    {
        xAnimTableNewState(table, names[7], 0x20, 0, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                           xAnimDefaultBeforeEnter, NULL, NULL);
        xAnimTableNewState(table, names[8], 0x10, 0, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                           xAnimDefaultBeforeEnter, NULL, NULL);
        xAnimTableNewState(table, names[6], 0x20, 0, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                           xAnimDefaultBeforeEnter, NULL, NULL);
        xAnimTableNewTransition(table, "Idle01 Idle02 Idle03 Idle04", names[7], OpenShellCheck,
                                NULL, 0, 0, 0.0f, 0.0f, 5, 0, 0.15f, NULL);
        xAnimTableNewTransition(table, names[7], names[8], NULL, OpenShellDone, 0x10, 0, 0.0f, 0.0f,
                                0, 0, 0.15f, NULL);
        xAnimTableNewTransition(table, names[8], names[6], CloseShellCheck, NULL, 0, 0, 0.0f, 0.0f,
                                5, 0, 0.15f, NULL);
        xAnimTableNewTransition(table, names[6], names[2], NULL, CloseShellDone, 0x10, 0, 0.0f,
                                0.0f, 0, 0, 0.15f, NULL);
    }
    return table;
}

void zNMEMindy::UpdateTVStatic(F32 dt)
{
    xModelInstance* tvStaticAtomic;

    switch (currentTVStaticState)
    {
    case eTVStaticMindyAppear:
        if (!makeMindyAppear || !(tvStaticTimer >= 0.0f) || currentState != eTalking)
        {
            return;
        }
        currentStaticTimes = mindyAppearTimes;
        break;
    case eTVStaticMindyDisappear:
        if (!makeMindyDisappear || !(tvStaticTimer >= 0.0f) || currentState == eTalking)
        {
            return;
        }
        currentStaticTimes = mindyAppearTimes;
        break;
    case eTVStaticMindyAppearFadeInStart:
        tvStaticTimer += dt;
        tvStaticAtomic = ModelAtomicFind(2, -1, NULL);
        tvStaticAtomic->Alpha -= fadeStep;
        if (tvStaticAtomic->Alpha < 0.0f)
        {
            tvStaticAtomic->Alpha = 0.0f;
            currentTVStaticState = eTVStaticMindyAppearFadeInEnd;
            tvStaticTimer = 0.0f;
            tvStaticTimeIndex = 0;
        }
        return;
    case eTVStaticMindyAppearFadeInEnd:
        return;
    case eTVStaticMindyDisappearFadeOutStart:
        tvStaticTimer += dt;
        tvStaticAtomic = ModelAtomicFind(2, -1, NULL);
        tvStaticAtomic->Alpha += fadeStep;
        if (tvStaticAtomic->Alpha > 1.0f)
        {
            tvStaticAtomic->Alpha = 1.0f;
            currentTVStaticState = eTVStatic;
            tvStaticTimer = 0.0f;
            tvStaticTimeIndex = 0;
        }
        return;
    default:
        if (!(tvStaticTimer >= 0.0f) || currentState == eTalking)
        {
            return;
        }
        currentStaticTimes = tvStaticTimes;
        break;
    }

    tvStaticTimer += dt;

    if (tvStaticTimeIndex < 6 && currentStaticTimes[tvStaticTimeIndex].time <= tvStaticTimer &&
        currentStaticTimes[tvStaticTimeIndex + 1].time > tvStaticTimer)
    {
        if (currentStaticTimes[tvStaticTimeIndex].tvStatic1)
        {
            tvStaticAtomic = ModelAtomicFind(1, -1, NULL);
            if (tvStaticAtomic != NULL)
            {
                tvStaticAtomic->Flags |= 0x2;
                tvStaticAtomic->Flags |= 0x1;
            }
        }
        else
        {
            tvStaticAtomic = ModelAtomicFind(1, -1, NULL);
            if (tvStaticAtomic != NULL)
            {
                tvStaticAtomic->Flags &= ~0x1;
            }
        }

        if (currentStaticTimes[tvStaticTimeIndex].tvStatic2)
        {
            tvStaticAtomic = ModelAtomicFind(2, -1, NULL);
            if (tvStaticAtomic != NULL)
            {
                tvStaticAtomic->Flags |= 0x2;
                tvStaticAtomic->Flags |= 0x1;
            }
        }
        else
        {
            tvStaticAtomic = ModelAtomicFind(2, -1, NULL);
            if (tvStaticAtomic != NULL)
            {
                tvStaticAtomic->Flags &= ~0x1;
            }
        }
    }

    if (tvStaticTimer > currentStaticTimes[tvStaticTimeIndex].time && tvStaticTimeIndex < 7)
    {
        tvStaticTimeIndex++;
    }

    if (tvStaticTimeIndex >= 7)
    {
        switch (currentTVStaticState)
        {
        case eTVStaticMindyAppear:
            makeMindyAppear = 0;
            currentTVStaticState = eTVStaticMindyAppearFadeInStart;
            tvStaticTimer = 0.0f;
            tvStaticTimeIndex = 0;
            break;
        case eTVStaticMindyDisappear:
            makeMindyDisappear = 0;
            currentTVStaticState = eTVStaticMindyDisappearFadeOutStart;
            tvStaticTimer = 0.0f;
            tvStaticTimeIndex = 0;
            ModelAtomicFind(2, -1, NULL)->Alpha = 0.0f;
            break;
        default:
            tvStaticTimeIndex = 0;
            tvStaticTimer = 0.0f;
            break;
        }
    }
}

void zNMEMindy::BUpdate(xVec3* pos)
{
    Mindy::bindInfo.UpdateBound(&bound, this, pos);
}

// No GC symbol (always inlined into Process); the name is not known. Whether one of the task
// boxes (and so a conversation) is running. Defined here because the header only
// forward-declares ztaskbox.
inline bool zNMEMindy::IsTalking()
{
    return (taskBox != NULL && taskBox->is_running()) ||
           (secondaryTaskBox != NULL && secondaryTaskBox->is_running());
}

void zNMEMindy::Process(F32 dt)
{
    F32 jawval;
    xMat3x3 matrix;
    F32 diffY;
    xMat3x3 rotationMat;
    xQuat currentQuat;
    xQuat defaultQuat;
    xQuat resultQuat;

    switch (currentState)
    {
    case eInRange:
        if (PlayerInRange() && xEntIsVisible(this) && !IsTalking())
        {
            if (globals.pad0->pressed & 0x1000)
            {
                if (textBox != NULL)
                {
                    textBox->deactivate();
                }

                if (mindyAsset->primaryCharacter == gCurrentPlayer)
                {
                    if (taskBox != NULL)
                    {
                        taskBox->start_talk(NULL);
                    }
                }
                else if (secondaryTaskBox != NULL)
                {
                    secondaryTaskBox->start_talk(NULL);
                }

                currentShellState = eShellOpened;
                currentState = eTalking;
            }

            if (currentShellState != eShellOpening && currentShellState != eShellOpened)
            {
                currentShellState = eShellOpening;
            }
        }
        else
        {
            if (textBox != NULL)
            {
                textBox->deactivate();
            }
            currentState = eOutofRange;
            offsetYTimer = 0.5f;
        }
        break;
    case eOutofRange:
        if (xEntIsVisible(this) && !IsTalking())
        {
            if (PlayerInRange())
            {
                if (textBox != NULL)
                {
                    textBox->activate(false);
                }
                currentState = eInRange;
                currentShellState = eShellOpening;
            }
            else if (CameraOutOfRange(clamCloseDistanceSqXZ) && currentShellState != eShellClosed &&
                     currentShellState != eShellClosing)
            {
                currentShellState = eShellClosing;
            }
        }
        break;
    case eTalking:
        if (!IsTalking())
        {
            currentState = eFinishedTalking;
            TalkStop();
        }
        else if (talkData.data != NULL)
        {
            if (talkData.time < 0.2f || xSndMgrIsPlaying(talkData.sndid))
            {
                talkData.time += dt;
                jawval = xJaw_EvalData(talkData.data, talkData.time);
                model->Anim->Single->BilinearLerp[0] = jawval;
            }
            else
            {
                currentShellState = eShellOpened;
                TalkStop();
            }
        }
        break;
    case eFinishedTalking:
        if (!PlayerInRange())
        {
            currentState = eOutofRange;
            offsetYTimer = 0.5f;
        }
        break;
    }

    if (CameraOutOfRange(updateCullDistanceSqXZ))
    {
        zNMECommon::Process(dt);
        if (simpShadow != NULL || entShadow != NULL)
        {
            ShadStufIAmDead();
        }
        return;
    }

    ShadStufUpdate();

    if (itemType == NME_TYPE_MINDY)
    {
        switch (currentShellState)
        {
        case eShellOpened:
            UpdateTVStatic(dt);
            break;
        case eShellClosed:
            ModelAtomicShow(1, NULL);
            ModelAtomicShow(2, NULL);
            // fall through
        case eShellOpening:
        default:
            tvStaticTimer = 0.0f;
            tvStaticTimeIndex = 0;
            break;
        }

        if (gCurrentPlayer != eCurrentPlayerPatrick && gCurrentPlayer != eCurrentPlayerSpongeBob)
        {
            offsetYTimer = -1.0f;
            model->Mat->pos = *(RwV3d*)&mindyAsset->ent_asset.pos;
        }
        else
        {
            if (currentShellState == eShellClosed)
            {
                if (currentState == eOutofRange && offsetYTimer <= 0.0f)
                {
                    wiggleTimer += dt;
                    model->Mat->pos.y =
                        model->Mat->pos.y + wiggleAmplitude * isin(wiggleFrequency * wiggleTimer);

                    model->Mat->at = matCopy.at;
                    model->Mat->up = matCopy.up;
                    model->Mat->right = matCopy.right;

                    pitch = pitchAmplitude * isin(pitchFrequency * wiggleTimer);
                    yaw = yawAmplitude * isin(yawFrequency * wiggleTimer);
                    roll = rollAmplitude * isin(rollFrequency * wiggleTimer);

                    xMat3x3Euler(&matrix, pitch, yaw, roll);
                    xMat4x3RMul3x3((xMat4x3*)model->Mat, &matrix, (xMat4x3*)model->Mat);
                    quatT = 0.0f;
                }
                else if (offsetYTimer > 0.0f)
                {
                    model->Mat->pos.y += goingUpTimeMultiplier * (offsetY * dt) / 0.5f;
                    offsetYTimer -= dt * goingUpTimeMultiplier;
                    if (offsetYTimer < 0.0f)
                    {
                        offsetYTimer = -1.0f;
                    }
                }
            }
            else if (currentShellState != eShellClosed || currentState != eOutofRange)
            {
                wiggleTimer = 0.0f;

                diffY = model->Mat->pos.y - mindyAsset->ent_asset.pos.y;
                if (xabs(diffY) <= 0.001f)
                {
                    model->Mat->pos = *(RwV3d*)&mindyAsset->ent_asset.pos;
                }
                else if (model->Mat->pos.y < mindyAsset->ent_asset.pos.y)
                {
                    // Retail bug: diffY is negative here, so this moves away from the rest height.
                    model->Mat->pos.y += dt * (diffY * goingDownTimeMultiplier);
                }
                else if (model->Mat->pos.y > mindyAsset->ent_asset.pos.y)
                {
                    model->Mat->pos.y -= dt * (diffY * goingDownTimeMultiplier);
                }

                if (quatT <= 1.0f)
                {
                    // Retail: never true after the test above.
                    if (quatT > 1.0f)
                    {
                        quatT = 1.0f;
                    }

                    xMat3x3Euler(&matrix, pitch, yaw, roll);
                    xQuatFromMat(&currentQuat, &matrix);

                    rotationMat.at = *(xVec3*)&matCopy.at;
                    rotationMat.up = *(xVec3*)&matCopy.up;
                    rotationMat.right = *(xVec3*)&matCopy.right;
                    rotationMat.at = -rotationMat.at;

                    xQuatFromMat(&defaultQuat, &rotationMat);
                    xQuatSlerp(&resultQuat, &currentQuat, &defaultQuat, quatT);
                    xQuatToMat(&resultQuat, &matrix);

                    model->Mat->at = matCopy.at;
                    model->Mat->up = matCopy.up;
                    model->Mat->right = matCopy.right;

                    xMat4x3RMul3x3((xMat4x3*)model->Mat, &matrix, (xMat4x3*)model->Mat);
                }
                else
                {
                    quatT = 1.05f;
                }

                quatT += goingDownTimeMultiplier * dt;
            }
        }
    }
    else
    {
        xprintf("Anim State:%15s\n", model->Anim->Single->State->Name);
    }

    zNMECommon::Process(dt);
}

void zNMEMindy::Render()
{
    xEntRender(this);
}

S32 zNMEMindy::TypeHandleMail(NMEMsg* mail)
{
    S32 handled;

    switch (mail->msgid)
    {
    case NME_MID_SYSEVENT:
        handled = MindySysMail(&mail->sysevent);
        break;
    default:
        handled = 0;
        break;
    }

    return handled;
}

S32 zNMEMindy::MindySysMail(NMESysEvent* sysmail)
{
    S32 handled = 1;

    switch (sysmail->toEvent)
    {
    case eEventNPCSpeakStart:
        currentState = eTalking;
        if (textBox != NULL)
        {
            textBox->deactivate();
        }
        if (sysmail->toParam != NULL)
        {
            TalkStart((S32)sysmail->toParam[0], (iSndGroupHandle)sysmail->toParamWidgetID);
        }
        else
        {
            TalkStart(-1, (iSndGroupHandle)sysmail->toParamWidgetID);
        }
        break;
    case eEventNPCSpeakStop:
        currentState = eFinishedTalking;
        TalkStop();
        break;
    case eEventStartConversation:
        if (textBox != NULL)
        {
            textBox->deactivate();
        }
        currentState = eTalking;
        if (currentShellState != eShellOpened)
        {
            currentShellState = eShellOpening;
        }
        break;
    case eEventEndConversation:
        if (textBox != NULL)
        {
            currentState = eFinishedTalking;
        }
        TalkStop();
        break;
    case eEventCelebrationAnimPlay:
        celebrationAnimPlay = 1;
        break;
    case eEventMindyStart:
        if (currentTVStaticState == eTVStatic)
        {
            makeMindyAppear = 1;
            makeMindyDisappear = 0;
            tvStaticTimer = 0.0f;
            tvStaticTimeIndex = 0;
            currentTVStaticState = eTVStaticMindyAppear;
            if (currentShellState == eShellClosed && gCurrentPlayer != eCurrentPlayerPatrick &&
                gCurrentPlayer != eCurrentPlayerSpongeBob)
            {
                currentShellState = eShellOpened;
            }
        }
        break;
    case eEventMindyEnd:
        makeMindyAppear = 0;
        makeMindyDisappear = 1;
        tvStaticTimer = 0.0f;
        tvStaticTimeIndex = 0;
        currentTVStaticState = eTVStaticMindyDisappear;
        break;
    default:
        handled = DfltSysEventMail(sysmail);
        break;
    }

    return handled;
}
