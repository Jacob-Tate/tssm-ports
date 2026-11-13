#include "zCar.h"
#include "zEntSimpleObj.h"
#include "zGrid.h"
#include "zNMECommon.h"
#include "zPlatform.h"
#include "zSurface.h"
#include "xEntBoulder.h"
#include "xGrid.h"
#include "xRay3.h"
#include "xScreenWarp.h"
#include "iCollide.h"
#include "zFX.h"
#include "zGlobals.h"
#include "zParPTank.h"
#include "zParEmitter.h"
#include "zScene.h"
#include "zUICustom.h"
#include "xstransvc.h"
#include "xString.h"

#include <rwcore.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "xRand.h"

#define ANIM_COUNT 78
extern U32 g_hash_caranim[ANIM_COUNT];
const char* g_strz_caranim[ANIM_COUNT];

F32 BOOST_DURATION = 2.5f;
S32 MAX_NITROS = 10;
static const S32 sTireBones[4] = { 3, 5, 7, 9 };
// SphereCollision collision user flag; retail loads it from .sdata2 instead of folding it
static const U32 zCOLLISION_NEEDS_REFLECTION = 0x1;
static char* carSmokeEmitterName = "CAR_SMOKE_EMIT";
static char* carKetchupEmitterName = "CAR_KETCHUP_EMIT";
static char* carMustardEmitterName = "CAR_MUSTARD_EMIT";
static char* carSeedsEmitterName = "CAR_SEEDS_EMIT";
static char* carGrassEmitterName = "CAR_GRASS_EMIT";
static char* carSandEmitterName = "CAR_SAND_EMIT";
static char* carDirtEmitterName = "CAR_DIRT_EMIT";
static char* carWaterEmitterName = "CAR_WATER_EMIT";
static char* carSteamEmitterName = "CAR_STEAM_EMIT";
static char* carOilEmitterName = "CAR_OIL_EMIT";
static char* carNitroUsedCounter = "COUNTER_NITRO";

iSndGroupHandle zCar::sSoundAsset[33];

static zParEmitter* sCarSmokeEmit;
static zParEmitter* sCarKetchupEmit;
static zParEmitter* sCarMustardEmit;
static zParEmitter* sCarSeedsEmit;
static zParEmitter* sCarGrassEmit;
static zParEmitter* sCarSandEmit;
static zParEmitter* sCarDirtEmit;
static zParEmitter* sCarWaterEmit;
static zParEmitter* sCarSteamEmit;
static zParEmitter* sCarOilEmit;
static RwTexture* sBrakeLightTex;

xModelInstance* sDamageModelInstances[4] = { NULL, NULL, NULL, NULL };

// Unreferenced (not linked into retail), but its strings are in the wad's string pool.
static char* surfaceNames[ezDRIVINGSURF_NumNormalDrivableSurfaces] = {
    "Unassigned", "Tarmac",   "Cement", "Hard Dirt",     "Railroad",  "Grass",
    "Soft Dirt",  "Sidewalk", "Sand",   "Surface Water", "Thick Oil", "Hot Sauce"
};

static driveSurfaceDescriptor surfaceDescriptors[ezDRIVINGSURF_NumNormalDrivableSurfaces] = {
    { 0.1f, 0.1f, 0.1f, FALSE, 0.3f, (iSndGroupHandle)-1, (iSndGroupHandle)-1 }, // Default
    { 0.8f, 0.6f, 4.0f, FALSE, 1.0f, (iSndGroupHandle)-1, (iSndGroupHandle)-1 }, // Tarmac
    { 0.8f, 0.6f, 4.0f, FALSE, 1.0f, (iSndGroupHandle)-1, (iSndGroupHandle)-1 }, // Cement
    { 0.8f, 0.6f, 4.0f, FALSE, 1.0f, (iSndGroupHandle)-1, (iSndGroupHandle)-1 }, // HardDirt
    { 0.8f, 0.6f, 4.0f, FALSE, 1.0f, (iSndGroupHandle)-1, (iSndGroupHandle)-1 }, // Railroad
    { 0.4f, 0.15f, 0.1f, FALSE, 0.85f, (iSndGroupHandle)-1, (iSndGroupHandle)-1 }, // Grass
    { 0.4f, 0.15f, 0.1f, FALSE, 0.7f, (iSndGroupHandle)-1, (iSndGroupHandle)-1 }, // SoftDirt
    { 0.4f, 0.15f, 0.1f, FALSE, 0.7f, (iSndGroupHandle)-1, (iSndGroupHandle)-1 }, // SideWalk
    { 0.7f, 0.5f, 0.3f, FALSE, 0.4f, (iSndGroupHandle)-1, (iSndGroupHandle)-1 }, // Sand
    { 0.2f, 0.1f, 0.1f, TRUE, 1.0f, (iSndGroupHandle)-1, (iSndGroupHandle)-1 }, // SurfaceWater
    { 0.2f, 0.1f, 0.1f, TRUE, 1.0f, (iSndGroupHandle)-1, (iSndGroupHandle)-1 }, // ThickOil
    { 0.2f, 0.1f, 0.1f, TRUE, 1.0f, (iSndGroupHandle)-1, (iSndGroupHandle)-1 }, // HotSauce
};

// Named PlayerStreakInfo in the PS2 DWARF (zWad1), but a zCar.cpp-local layout that differs from
// the player's PlayerStreakInfo (zEntPlayer.cpp)
struct PlayerStreakInfo
{
    U32 activated;
    U32 streakID;
    F32 alphaStart;
    S32 streakTaper;
    xColor_tag colA;
    xColor_tag colB;
    U32 bone;
    xVec3 a;
    xVec3 b;
};

static PlayerStreakInfo sStreakInfo[2] = {
    { FALSE, 0xDEAD, 0.5f, 1, { 0xFF, 0xFF, 0x80, 0xFF }, { 0xC4, 0xC4, 0x40, 0xFF }, 26 },
    { FALSE, 0xDEAD, 0.5f, 1, { 0xFF, 0xFF, 0x80, 0xFF }, { 0xC4, 0xC4, 0x40, 0xFF }, 27 },
};

// Unreferenced in retail (the values of a surface tweak paired with surfaceNames), but it is in
// the wad's .rodata.
static const U32 tweak_surface_values[ezDRIVINGSURF_NumNormalDrivableSurfaces] = {
    ezDRIVINGSURF_Default,      ezDRIVINGSURF_Tarmac,   ezDRIVINGSURF_Cement,
    ezDRIVINGSURF_HardDirt,     ezDRIVINGSURF_Railroad, ezDRIVINGSURF_Grass,
    ezDRIVINGSURF_SoftDirt,     ezDRIVINGSURF_SideWalk, ezDRIVINGSURF_Sand,
    ezDRIVINGSURF_SurfaceWater, ezDRIVINGSURF_ThickOil, ezDRIVINGSURF_HotSauce,
};

xModelBlur::config_data NitroBlurConfig = { 0, 2.0f, 1.0f, 1.0f, 1.0f, 0.4f, 0.3f, 100.0f, 16 };

zStreamedSound zCar::carOneLinersData[eCarOneLinerNumSounds + 1] = {
    { "OneL_Car_Smash_Crate_SG", 40.0f, 0xA00, 0.0f, (iSndGroupHandle)-1 },
    { "", 0.0f, 0, 0.0f, (iSndGroupHandle)-1 },
    { "OneL_Car_Boost_SG", 40.0f, 0xC80, 0.0f, (iSndGroupHandle)-1 },
    { "OneL_Car_Reverse_SG", 40.0f, 0x1900, 0.0f, (iSndGroupHandle)-1 },
    { "OneL_Car_Slippery_SG", 40.0f, 0x1900, 0.0f, (iSndGroupHandle)-1 },
    { "OneL_Car_Damaged_SG", 40.0f, 0x1080, 0.0f, (iSndGroupHandle)-1 },
    { "OneL_Car_Got_Health_SG", 40.0f, 0x2580, 0.0f, (iSndGroupHandle)-1 },
    { NULL, 0.0f, 0, 0.0f, (iSndGroupHandle)-1 },
};

inline void zCar::SceneInit()
{
    S32 i;
    char modelName[256];
    RpAtomic* imodel;

    sDamageModelInstances[0] = globals.player.model_car;
    for (i = 1; i < 4; i++)
    {
        sprintf(modelName, "patty_wagon_damage%1d_bind.dff", i);
        imodel = (RpAtomic*)xSTFindAsset(xStrHash(modelName), NULL);
        if (imodel)
        {
            sDamageModelInstances[i] = xModelInstanceAllocReuse(imodel, NULL, 0, 0, NULL, NULL);
        }
    }

    mpRumbleCollision = (xRumble::effect*)zSceneFindObject(xStrHash("RUMBLE_CAR_COLLIDE"));
    mpRumbleDamage = (xRumble::effect*)zSceneFindObject(xStrHash("RUMBLE_CAR_DAMAGE"));
    mpRumbleBoostStart = (xRumble::effect*)zSceneFindObject(xStrHash("RUMBLE_CAR_BOOST_START"));
    mpRumbleBoost = (xRumble::effect*)zSceneFindObject(xStrHash("RUMBLE_CAR_BOOST"));

    nitroBlur.create();
}

void zCar::Initialize(xEnt* pCarEnt)
{
    SoundInitAssetTable();
    carOneLiners.InitWithSounds(carOneLinersData, eCarOneLinerNumSounds);
    SceneInit();

    xVec3 dummyPos = { 0.0f, 0.0f, 0.0f };
    Reset(pCarEnt, 0.0f, &dummyPos);
}

void zCar::Reset(xEnt* pCarEnt, F32 initialYaw, xVec3* pPosition)
{
    pEnt = pCarEnt;
    if (pCarEnt->model != sDamageModelInstances[0])
    {
        pCarEnt->model = sDamageModelInstances[0];
    }

    SetFullHealth();

    engineRunning = FALSE;
    playShutDownSound = TRUE;
    soundBoostStartHandle = (iSndHandle)-1;
    numNitros = 0;
    modelYaw = initialYaw;
    animVirtualBunTopPos = *pPosition;
    xVec3Init(&animVirtualBunTopVel, 0.0f, 0.0f, 0.0f);
    animVirtualBunTopHeight = 0.0f;
    animVirtualBunTopVerticalVel = 0.0f;
    animJumpOpeness = 0.0f;
    animInJumpAnimation = FALSE;
    animSuccessMode = FALSE;
    animFailedMode = FALSE;
    animPropellorAngle = 0;
    animCrash = FALSE;
    animHeavyLanding = FALSE;
    isInDamageSpin = FALSE;
    xVec3Init(&velocity, 0.0f, 0.0f, 0.0f);
    shockExtension = 0.0f;
    inGroundContact = TRUE;
    boundYShift = 0.7f;
    boundRadius = 1.3f;
    shockHalfRange = 0.15f;
    boostTimeRemaining = 0.0f;
    oilPenaltyTimer = 0.0f;
    animLean = 0.0f;
    currentAnimState = eANIMSTATE_Idle;

    sCarSmokeEmit = zParEmitterFind(carSmokeEmitterName);
    sCarKetchupEmit = zParEmitterFind(carKetchupEmitterName);
    sCarMustardEmit = zParEmitterFind(carMustardEmitterName);
    sCarSeedsEmit = zParEmitterFind(carSeedsEmitterName);
    sCarGrassEmit = zParEmitterFind(carGrassEmitterName);
    sCarSandEmit = zParEmitterFind(carSandEmitterName);
    sCarDirtEmit = zParEmitterFind(carDirtEmitterName);
    sCarWaterEmit = zParEmitterFind(carWaterEmitterName);
    sCarSteamEmit = zParEmitterFind(carSteamEmitterName);
    sCarOilEmit = zParEmitterFind(carOilEmitterName);

    skidFXIntensity = 0.0f;
    steeringPosition = 0.0f;
    pCurrentSurface = &surfaceDescriptors[ezDRIVINGSURF_Default];
    currentSurfaceType = ezDRIVINGSURF_Default;

    // xStrHash("brakelight")
    sBrakeLightTex = (RwTexture*)xSTFindAsset(0xF09CA85F, NULL);

    damageLevel = eDamageNone;
    carOneLiners.Reset();
    soundEnginePitch = 1.0f;
    soundEngineHandle = (iSndHandle)-1;
    soundCurrentEngineType = eDamageMax;
    soundBoostLoopHandle = (iSndHandle)-1;
    soundGearCrunchReady = TRUE;
    soundReverseActivationTime = 0xFFFFFFFF;
    soundReverseBeepHandle = (iSndHandle)-1;
    pSoundCurrentSurface = NULL;
    soundDriveLoopHandle = (iSndHandle)-1;
    soundSkidLoopHandle = (iSndHandle)-1;

    for (S32 i = ezDRIVINGSURF_NumNormalDrivableSurfaces - 1; i >= 0; i--)
    {
        soundSkidLoopHandles[i] = (iSndHandle)-1;
    }

    oilOneLinerPlayed = FALSE;
    nitroUsedCounter = (_xCounter*)zSceneFindObject(xStrHash(carNitroUsedCounter));
}

void zCar::Exit()
{
    for (S32 i = 1; i < 4; i++)
    {
        if (sDamageModelInstances[i])
        {
            sDamageModelInstances[i]->Anim = NULL;
            xModelInstanceFree(sDamageModelInstances[i]);
            sDamageModelInstances[i] = NULL;
        }
    }
}

void zCar::HandleEvent(xBase* from, xBase* to, U32 toEvent, const F32* toParam,
                       xBase* toParamWidget, U32 UNUSEDtoParamWidgetID)
{
    S32 param;
    bool spinClockwise;

    switch (toEvent)
    {
    case eEventGiveHealth:
        param = (S32)toParam[0];
        if (param == 0)
        {
            param = 1;
        }

        if (param > 0)
        {
            GiveHealth(param);
        }
        else
        {
            spinClockwise = rand() % 2 == 1;
            TakeDamage(spinClockwise);
        }
        break;
    case eEventHit:
        spinClockwise = rand() % 2 == 1;
        TakeDamage(spinClockwise);
        break;
    case eEventPattyWagonStartEngine:
        engineRunning = TRUE;
        playShutDownSound = TRUE;
        break;
    case eEventPattyWagonStopEngine:
    {
        // Retail evaluates the condition before clearing engineRunning
        bool playSound = toParam == NULL || toParam[0] == 0.0f;
        engineRunning = FALSE;
        playShutDownSound = playSound;
        break;
    }
    default:
        zEntPlayerEventCB(from, to, toEvent, toParam, toParamWidget, UNUSEDtoParamWidgetID);
        break;
    }
}

// No GC symbol: inlined into HandleEvent and SphereCollision, the out-of-line copy dead-stripped
void zCar::TakeDamage(bool spinClockwise)
{
    if (!isInDamageSpin && !animStartSpinLeft && !animStartSpinRight && damageLevel != eDamage3)
    {
        if (spinClockwise)
        {
            animStartSpinRight = TRUE;
        }
        else
        {
            animStartSpinLeft = TRUE;
        }

        ModelSwap((eDamage)(damageLevel + 1), true);

        if (damageLevel == eDamage3)
        {
            zEntEvent(pEnt, 0, pEnt, eEventPlayerDeath, NULL, NULL, 0, FE_NO);
            engineRunning = FALSE;
            // PATTY_CAR_DESTROYED_SG
            xSndMgrPlay(sSoundAsset[20], 2, NULL, NULL, pEnt, NULL, NULL);
        }
        else
        {
            SoundPlayCollision(eCollisionTypeDamage, 1.0f);
            carOneLiners.PlaySound(eCarOneLinerTakeDamage);
        }
    }
}

void zCar::SetFullHealth()
{
    ModelSwap(eDamageNone, true);

    isInDamageSpin = FALSE;
    animStartSpinLeft = FALSE;
    animStartSpinRight = FALSE;
    animSuccessMode = FALSE;
    animFailedMode = FALSE;
}

void zCar::GiveHealth(S32 health)
{
    eDamage newDamageLevel;

    if (GetHealth() != 0)
    {
        if (eCarOneLinerGetHealth < carOneLiners.mNumSounds)
        {
            carOneLiners.mSounds[eCarOneLinerGetHealth].PlaySound();
        }

        newDamageLevel = (eDamage)(damageLevel - health);
        if (newDamageLevel <= eDamageNone)
        {
            newDamageLevel = eDamageNone;
        }

        if (newDamageLevel >= eDamageMax)
        {
            newDamageLevel = eDamage3;
        }

        ModelSwap(newDamageLevel, true);
    }
}

void zCar::SetMatrix(xMat4x3* pSource)
{
    lastMat = mat;
    mat = *pSource;
}

xMat4x3* zCar::GetMatrix()
{
    return &mat;
}

void zCar::AddNitros(S32 nitros)
{
    numNitros += nitros;
    // PATTY_BOOST_PICKUP_SG
    xSndMgrPlay(sSoundAsset[26], 2, NULL, NULL, pEnt, NULL, NULL);
}

U8 zCar::CanPickupNitros() const
{
    return numNitros < MAX_NITROS;
}

U32* zCar::GetVarNitros() const
{
    return (U32*)&numNitros;
}

// No GC symbol: inlined into CarUpdate, the out-of-line copy dead-stripped
void zCar::StartBoost()
{
    if (boostTimeRemaining > 0.0f || numNitros <= 0)
    {
        return;
    }

    numNitros--;
    if (nitroUsedCounter != NULL && nitroUsedCounter->count == 0)
    {
        zEntEvent(nitroUsedCounter, eEventIncrement);
    }

    boostTimeRemaining = BOOST_DURATION;
    soundBoostStartHandle = xSndMgrPlay(sSoundAsset[12], 2, NULL, NULL, pEnt, NULL, NULL);
    soundBoostLoopHandle = xSndMgrPlay(sSoundAsset[13], 2, NULL, NULL, pEnt, NULL, NULL);
    carOneLiners.PlaySound(eCarOneLinerStartBoost);

    if (mpRumbleBoostStart != NULL)
    {
        zEntEvent(mpRumbleBoostStart, eEventStartRumbleEffect, globals.currentActivePad, 0.0f, 0.0f,
                  0.0f);
    }

    if (mpRumbleBoost != NULL)
    {
        zEntEvent(mpRumbleBoost, eEventStartRumbleEffect, globals.currentActivePad, 0.0f, 0.0f,
                  0.0f);
    }

    nitroBlur.activate(0.15f, 0.3f, NitroBlurConfig, pEnt->model);
    xScreenWarp::emit(pEnt->bound.sph.center, 4.0f, 1.0f, xScreenWarp::TYPE_SHOCKWAVE, 1.5f, 1.0f,
                      0);
}

void zCar::CarUpdate(F32 dt, F32 steer, bool accelOn, bool brakeOn, bool boostOn)
{
    F32 speedPercentage;
    F32 steerFactor;
    F32 steerAngle;
    F32 accelerationFactor;
    F32 boostAccelFactor;
    xVec3 newVelocity;

    if (oilPenaltyTimer > 0.0f)
    {
        oilPenaltyTimer -= dt;
        oilPenaltyTimer = MAX(0.0f, oilPenaltyTimer);
        if (oilPenaltyTimer == 0.0f)
        {
            oilOneLinerPlayed = FALSE;
        }
    }

    if (inGroundContact)
    {
        timeOffGround = 0.0f;
    }
    else
    {
        timeOffGround += dt;
    }

    F32 speed = xVec3Length(&velocity);
    F32 forwardSpeed = xVec3Dot(&mat.at, &velocity);
    F32 rightSpeed = xVec3Dot(&mat.right, &velocity);
    F32 upSpeed = xVec3Dot(&mat.up, &velocity);

    speedPercentage = speed / 25.0f;
    F32 topSpeedPercentage = MAX(0.0f, speedPercentage / pCurrentSurface->topSpeedFactor);
    steerFactor = CLAMP(speedPercentage, 0.0f, 1.0f);

    if ((steeringPosition > 0.0f && steer < steeringPosition) ||
        (steeringPosition < 0.0f && steer > steeringPosition))
    {
        steeringPosition += steer - steeringPosition;
    }
    else
    {
        steeringPosition += steer - steeringPosition;
    }

    static F32 driveAngle = 0.0f;

    steerAngle = steeringPosition / 128.0f;
    if (steerFactor < 0.01f && !accelOn)
    {
        steerAngle = 0.0f;
    }

    // Retail's constant is 0x3F9AE147 (1.2099999f, also in the PS2 build), one ulp below 1.21f.
    steerAngle = dt * (1.2099999f * steerAngle);
    if (!inGroundContact)
    {
        steerAngle *= 0.5f;
    }

    if (IsDisabled())
    {
        steerAngle = 0.0f;
    }

    if (forwardSpeed < 0.0f && !(accelOn && forwardSpeed > -0.8f))
    {
        steerAngle *= -1.0f;
    }

    isSliding = xabs(rightSpeed) > pCurrentSurface->slideThreshold && inGroundContact;

    modelYaw += steerAngle;
    if (modelYaw > PI)
    {
        modelYaw -= 2.0f * PI;
    }
    else if (modelYaw < -PI)
    {
        modelYaw += 2.0f * PI;
    }

    if (boostOn && !IsDisabled())
    {
        StartBoost();
    }

    accelerationFactor = 0.0f;
    animBrakeLightsOn = FALSE;

    if (boostTimeRemaining > 0.0f)
    {
        boostAccelFactor = (29.0f - speed) / 29.0f;
        accelerationFactor = 2.0f * CLAMP(boostAccelFactor, 0.0f, 1.0f);

        boostTimeRemaining -= dt;
        boostTimeRemaining = MAX(0.0f, boostTimeRemaining);
        if (damageLevel == eDamage3)
        {
            boostTimeRemaining = 0.0f;
        }

        if (boostTimeRemaining == 0.0f)
        {
            StopBoost();
        }

        nitroBlur.move(pEnt->bound.sph.center);
    }
    else if (accelOn)
    {
        if (forwardSpeed < 0.0f)
        {
            accelerationFactor = 0.4f;
            animBrakeLightsOn = TRUE;
        }
        else
        {
            soundGearCrunchReady = TRUE;
            if (topSpeedPercentage <= 1.0f)
            {
                accelerationFactor = 0.6f * (1.0f - topSpeedPercentage);
            }
            else
            {
                accelerationFactor = 0.2f * (1.0f - topSpeedPercentage);
            }
        }
    }

    animReversing = FALSE;
    if (brakeOn)
    {
        if (forwardSpeed > 0.0f)
        {
            accelerationFactor = -0.4f;
            animBrakeLightsOn = TRUE;
        }
        else
        {
            if (forwardSpeed < -(13.0f * pCurrentSurface->topSpeedFactor))
            {
                accelerationFactor = 0.0f;
            }
            else
            {
                accelerationFactor = -0.15f;
            }

            animReversing = TRUE;
            if (soundGearCrunchReady)
            {
                xSndMgrPlay(sSoundAsset[11], 2, NULL, NULL, pEnt, NULL, NULL);
                soundGearCrunchReady = FALSE;
            }
        }
    }

    if (IsDisabled())
    {
        animBrakeLightsOn = FALSE;
        if (forwardSpeed > 0.0f)
        {
            accelerationFactor = -0.4f;
        }
        else
        {
            accelerationFactor = 0.4f;
        }
    }

    if (!inGroundContact)
    {
        accelerationFactor = 0.0f;
    }

    xVec3 acceleration = mat.at * accelerationFactor;

    if (!accelOn && !brakeOn && inGroundContact)
    {
        xVec3 forwardVelocity = mat.at * forwardSpeed;
        acceleration -= forwardVelocity * 0.01f;
    }

    xVec3 rightVelocity = mat.right * rightSpeed;
    if (IsDisabled())
    {
        acceleration -= rightVelocity * 0.1f;
    }
    else if (inGroundContact)
    {
        if (isSliding)
        {
            acceleration -= rightVelocity * 0.1f * pCurrentSurface->slidingTraction;
        }
        else
        {
            acceleration -= rightVelocity * 0.1f * pCurrentSurface->staticTraction;
        }
    }

    acceleration.y -= 0.2f;

    if (inGroundContact)
    {
        acceleration.y += 0.4f * (1.0f - shockExtension);
    }

    if (inGroundContact)
    {
        acceleration += mat.up * (-0.3f * upSpeed);
    }

    if (inGroundContact)
    {
        xVec3 across;
        xVec3Cross(&across, &g_NY3, &mat.up);

        F32 len = xVec3Length(&across);
        if (len > 0.00001f)
        {
            across /= len;
            xVec3 downHill;
            xVec3Cross(&downHill, &mat.up, &across);
            xVec3Normalize(&downHill, &downHill);

            if (!brakeOn && !accelOn && downHill.y < -0.17f && !IsDisabled())
            {
                // not in the DWARF: retail multiplies the stored dot product by the slope factor
                F32 dot = xVec3Dot(&downHill, &mat.at);
                acceleration += mat.at * (dot * -(0.4f * downHill.y));
            }
        }
    }

    xVec3 oldPos;
    xVec3 newPos;
    xVec3 groundNormal;
    xMat3x3 idealOrientation;
    xQuat currentQuat;
    xQuat idealQuat;
    xQuat slerped;

    newVelocity = velocity + acceleration;
    GetCenter(&oldPos);
    newPos = oldPos + newVelocity * dt;

    SphereCollision(&newPos, &newVelocity, &inGroundContact, &shockExtension, &groundNormal);
    ApplyCollisionRebounds(&newVelocity);
    velocity = newVelocity;
    DepenetrateCollisionList(&newPos, boundRadius);
    PreventPoppingThroughWorld(&oldPos, &newPos, &newVelocity);

    CalculateIdealOrientation(modelYaw, &idealOrientation, &groundNormal);
    xQuatFromMat(&currentQuat, &mat);
    xQuatFromMat(&idealQuat, &idealOrientation);
    xQuatSlerp(&slerped, &currentQuat, &idealQuat, 10.0f * dt);
    xQuatToMat(&slerped, &mat);

    GetPositionFromCenter(&mat.pos, &newPos);

    UpdateGroundCharacteristics();
}

void zCar::CalculateIdealOrientation(F32 yaw, xMat3x3* pIdealOrientation, xVec3* pGroundNormal)
{
    xMat3x3 yawMatrix;

    xMat3x3RotY(&yawMatrix, yaw);

    if (inGroundContact)
    {
        *pIdealOrientation = yawMatrix;

        // not in the DWARF: the dot product is tested and then passed to xacos.
        F32 dot = xVec3Dot(&g_Y3, pGroundNormal);
        if (dot < 0.9999f)
        {
            xVec3 inclineRotAxis;
            xMat3x3 inclineMatrix;

            xVec3Cross(&inclineRotAxis, &g_Y3, pGroundNormal);
            xVec3Normalize(&inclineRotAxis, &inclineRotAxis);
            xMat3x3Rot(&inclineMatrix, &inclineRotAxis, xacos(dot));
            xMat3x3Mul(pIdealOrientation, pIdealOrientation, &inclineMatrix);
        }
    }
    else if (timeOffGround < 0.06f)
    {
        *pIdealOrientation = mat;
    }
    else
    {
        xVec3 flightArcDirection;

        if (!(xVec3Normalize(&flightArcDirection, &velocity) < 0.00001f))
        {
            xVec3 flightArcRightVec;

            xVec3Cross(&flightArcRightVec, &flightArcDirection, &g_Y3);
            if (!(xVec3Normalize(&flightArcRightVec, &flightArcRightVec) < 0.00001f))
            {
                F32 tilt = xacos(xVec3Dot(&g_NY3, &flightArcDirection)) - 0.5f * PI;
                tilt *= 0.2f;
                xMat3x3 inclineMatrix;

                xMat3x3Rot(&inclineMatrix, &flightArcRightVec, tilt);
                xMat3x3Mul(pIdealOrientation, &yawMatrix, &inclineMatrix);
                return;
            }
        }

        *pIdealOrientation = yawMatrix;
    }
}

// No GC symbol: inlined into CarUpdate, the out-of-line copy dead-stripped
void zCar::UpdateGroundCharacteristics()
{
    for (S32 i = ezDRIVINGSURF_NumNormalDrivableSurfaces - 1; i >= 0; i--)
    {
        if (collisionTypes[i])
        {
            pCurrentSurface = &surfaceDescriptors[i];
            currentSurfaceType = (zDrivingSurf)i;

            if (currentSurfaceType == ezDRIVINGSURF_ThickOil)
            {
                oilPenaltyTimer = 2.0f;
                if (!oilOneLinerPlayed)
                {
                    oilOneLinerPlayed = TRUE;
                    carOneLiners.PlaySound(eCarOneLinerDetectedSlipperySurface);
                }
            }

            if (oilPenaltyTimer > 0.0f)
            {
                collisionTypes[ezDRIVINGSURF_ThickOil] = TRUE;
                pCurrentSurface = &surfaceDescriptors[ezDRIVINGSURF_ThickOil];
                currentSurfaceType = ezDRIVINGSURF_ThickOil;
            }

            return;
        }
    }
}

bool zCar::operator()(xEnt& ent)
{
    xCollis* pCollis;
    S32 collisionsDetected;
    S32 maxCollisions;

    if (numCollisions == ARRAY_SIZE(collisions))
    {
        return false;
    }

    if (!(ent.chkby & XENT_COLLTYPE_PLYR))
    {
        return true;
    }

    pCollis = &collisions[numCollisions];
    xBoundHitsBound(&bound, &ent.bound, pCollis);
    if (!(pCollis->flags & 0x1))
    {
        return true;
    }

    collisionsDetected = 0;
    if (ent.collLev == 5)
    {
        maxCollisions = ARRAY_SIZE(collisions) - numCollisions;
        xBoundHitsModel(&bound, ent.collModel, pCollis, maxCollisions, 0.3926991f);

        for (S32 f = 0; f < maxCollisions; f++)
        {
            if (!(pCollis[f].flags & 0x1))
            {
                break;
            }
            collisionsDetected++;
        }

        if (collisionsDetected == 0)
        {
            return true;
        }
    }
    else
    {
        collisionsDetected = 1;
    }

    for (S32 f = 0; f < collisionsDetected; f++)
    {
        pCollis[f].oid = ent.id;
        pCollis[f].optr = &ent;
        pCollis[f].mptr = ent.collModel;
    }

    numCollisions += collisionsDetected;
    return true;
}

// downVec, hitHeight, isShockContact and upDot are not in the PS2 DWARF (probably an inlined
// helper's locals), but retail computes each of them once.
void zCar::SphereCollision(const xVec3* pNewCarPosition, const xVec3* pVelocity,
                           bool* pInGroundContact, F32* pShockExtension, xVec3* pGroundNormal)
{
    for (S32 i = 0; i < ezDRIVINGSURF_MaxTypes; i++)
    {
        collisionTypes[i] = FALSE;
    }

    xSphere collisionSphere;
    collisionSphere.center = *pNewCarPosition;
    collisionSphere.r = boundRadius;

    memset(collisions, 0, sizeof(collisions));
    for (S32 i = 0; i < ARRAY_SIZE(collisions); i++)
    {
        collisionUserFlags[i] = 0;
        collisions[i].flags = k_HIT_0xF00 | k_HIT_CALC_HDNG;
    }

    numCollisions = xSphereHitsEnv3(&collisionSphere, globals.sceneCur->env, collisions, 6, 0.0f);

    bound.type = XBOUND_TYPE_SPHERE;
    bound.sph.r = boundRadius;
    xVec3Copy(&bound.sph.center, &collisionSphere.center);
    xQuickCullForSphere(&xqc_def_ctrl, &bound.qcd, &bound.sph);

    xGridCheckBound(colls_grid, bound, bound.qcd, *this);
    xGridCheckBound(colls_oso_grid, bound, bound.qcd, *this);
    xGridCheckBound(npcs_grid, bound, bound.qcd, *this);

    U32 numGroundCollisions = 0;
    F32 averageShockPos = 0.0f;
    xVec3 averageGroundNormal;
    averageGroundNormal.x = 0.0f;
    averageGroundNormal.y = 0.0f;
    averageGroundNormal.z = 0.0f;

    for (S32 i = 0; i < numCollisions; i++)
    {
        xSurface* pSurface = NULL;
        if (collisions[i].optr)
        {
            if (collisions[i].mptr)
            {
                pSurface = collisions[i].mptr->Surf;
            }
        }
        else
        {
            pSurface = zSurfaceGetSurface(collisions[i].oid);
        }

        bool isWallSurfaceType = false;
        if (pSurface)
        {
            zDrivingSurf surfaceType =
                (zDrivingSurf)((zSurfaceProps*)pSurface->moprops)->asset->driving_surface_type;
            isWallSurfaceType = (surfaceType == ezDRIVINGSURF_Wall);

            if (((zSurfaceProps*)pSurface->moprops)->asset->game_damage_type == 6 ||
                surfaceType == ezDRIVINGSURF_HotSauce)
            {
                TakeDamage(steeringPosition < 0.0f);
            }

            collisionTypes[surfaceType] = TRUE;
        }

        xEnt* pEntity = (xEnt*)collisions[i].optr;
        if (pEntity)
        {
            switch (pEntity->baseType)
            {
            case eBaseTypeBoulder:
            {
                xEntBoulder* pBoulder = (xEntBoulder*)pEntity;

                if ((pBoulder->basset->flags & 0x2) && !isInDamageSpin && !animStartSpinLeft &&
                    !animStartSpinRight)
                {
                    TakeDamage(xVec3Dot(&mat.right, &collisions[i].tohit) < 0.0f);
                }

                F32 dot = 0.75f * xVec3Dot(&collisions[i].tohit, &pBoulder->vel);
                dot -= 0.75f * xVec3Dot(&collisions[i].tohit, &velocity);
                if (dot < 0.0f)
                {
                    xVec3 newVel = pBoulder->vel + collisions[i].tohit * (-2.0f * dot);
                    pBoulder->vel = newVel;
                }

                F32 hitParams[4] = { 1.0f, 1.0f, 0.0f, 0.0f };
                zEntEvent(xglobals->___player_ent_dont_use_directly, 0, pEntity, eEventHit,
                          hitParams, NULL, 0, FE_NO);
                break;
            }
            case eBaseTypeNPC:
            {
                zNMECommon* pNME = (zNMECommon*)pEntity;

                pNME->Damage(DMGTYP_CARSMASH, pEnt, NULL, -1, ePowerupLevelNormal);
                collisions[i].flags &= ~k_HIT_IT;

                en_npctyp type = (en_npctyp)pNME->itemType;
                if (type >= NME_TYPE_TIKI && type <= NME_TYPE_TIKI_STONE &&
                    type != NME_TYPE_TIKI_THUNDER)
                {
                    carOneLiners.PlaySound(eCarOneLinerDestroyCrate);
                }
                continue;
            }
            case eBaseTypeStatic:
                if (((zEntSimpleObj*)pEntity)->destructible)
                {
                    F32 hitParams[4] = { 1.0f, 1.0f, 0.0f, 0.0f };
                    zEntEvent(NULL, 0, pEntity, eEventHit, hitParams, NULL, 0, FE_NO);
                    collisions[i].flags &= ~k_HIT_IT;
                    continue;
                }
                break;
            case eBaseTypePlatform:
            {
                F32 hitParams[4] = { 1.0f, 1.0f, 0.0f, 0.0f };
                zEntEvent(NULL, 0, pEntity, eEventHit, hitParams, NULL, 0, FE_NO);
                if (((zPlatform*)pEntity)->destructible)
                {
                    collisions[i].flags &= ~k_HIT_IT;
                    continue;
                }
                break;
            }
            }
        }

        xVec3 downVec;
        if (inGroundContact || timeOffGround < 250.0f)
        {
            downVec = -mat.up;
        }
        else
        {
            downVec = g_NY3;
        }

        F32 hitHeight = xVec3Dot(&downVec, &collisions[i].tohit);
        bool isShockContact = false;
        if (xVec3Dot(&downVec, &collisions[i].hdng) >
                (boundYShift - shockHalfRange) / boundRadius &&
            hitHeight > (boundYShift - shockHalfRange) - 0.01f)
        {
            isShockContact = true;
        }

        F32 upDot = xVec3Dot(&g_Y3, &collisions[i].norm);
        bool isSteepSurface = xacos(CLAMP(upDot, 0.0f, 1.0f)) > 0.7853982f;

        if ((isSteepSurface && !isShockContact) || isWallSurfaceType)
        {
            collisionUserFlags[i] = 2;
            if (xVec3Dot(&collisions[i].norm, pVelocity) < 0.0f)
            {
                collisionUserFlags[i] |= zCOLLISION_NEEDS_REFLECTION;
            }
        }
        else if (!isSteepSurface && isShockContact)
        {
            F32 distFromMidShockPos = xVec3Dot(&downVec, &collisions[i].tohit) - boundYShift;
            if (distFromMidShockPos < shockHalfRange)
            {
                numGroundCollisions++;
                averageShockPos += (distFromMidShockPos + shockHalfRange) / (2.0f * shockHalfRange);
                averageGroundNormal += collisions[i].norm;
                if (pVelocity->y < -7.0f)
                {
                    animHeavyLanding = TRUE;
                }
            }
        }
        else if (!isSteepSurface && !isShockContact)
        {
            collisionUserFlags[i] = 5;
        }
    }

    if (numGroundCollisions)
    {
        averageShockPos /= numGroundCollisions;
        averageGroundNormal /= numGroundCollisions;
        xVec3Normalize(&averageGroundNormal, &averageGroundNormal);
    }

    *pShockExtension = averageShockPos;
    *pGroundNormal = averageGroundNormal;
    *pInGroundContact = (numGroundCollisions != 0);
}

// No GC symbol: inlined into CarUpdate, the out-of-line copy dead-stripped
void zCar::PreventPoppingThroughWorld(const xVec3* pStartPosition, xVec3* pEndPosition,
                                      xVec3* pVelocity)
{
    xVec3 startPos;
    xVec3 endPos;
    xVec3 direction;
    F32 rayLength;
    xRay3 ray;
    xCollis collision;

    startPos = *pStartPosition;
    endPos = *pEndPosition;
    direction = endPos - startPos;
    rayLength = xVec3Normalize(&direction, &direction);

    xVec3Copy(&ray.origin, &startPos);
    xVec3Copy(&ray.dir, &direction);
    ray.max_t = rayLength;
    ray.min_t = 0.0f;
    ray.flags = 0xC00;
    collision.flags = 0x300;
    xRayHitsEnv(&ray, globals.sceneCur->env, &collision);

    if (collision.flags & 0x1)
    {
        *pEndPosition = startPos;

        F32 dot = xVec3Dot(&collision.norm, pVelocity);
        if (dot < 0.0f)
        {
            pVelocity->y -= collision.norm.y * (1.5f * dot);
            pVelocity->x -= collision.norm.x * (1.7f * dot);
            pVelocity->z -= collision.norm.z * (1.7f * dot);
        }
    }
}

// No GC symbol: inlined into CarUpdate, the out-of-line copy dead-stripped
void zCar::DepenetrateCollisionList(xVec3* pPosition, F32)
{
    for (S32 i = 0; i < numCollisions; i++)
    {
        if (collisionUserFlags[i] & 0x6)
        {
            if (collisionUserFlags[i] & 0x4)
            {
                F32 innerBound;
                xVec3 depen;
                F32 depenScale;

                // innerBound is set in two steps around the length: retail subtracts
                // boundYShift - shockHalfRange before the xsqrt and boundRadius after it.
                innerBound = boundYShift - shockHalfRange;
                depenScale = xVec3Length(&collisions[i].depen);
                innerBound = boundRadius - innerBound;
                depen = collisions[i].depen;

                if (depenScale > innerBound)
                {
                    depenScale = (depenScale - innerBound) / depenScale;
                    depen *= depenScale;
                    *pPosition += depen;
                }
            }
            else
            {
                *pPosition += collisions[i].depen;
            }
        }
    }
}

// dot, vertDelta and deltaSpeed are not in the PS2 DWARF, but retail computes them before the
// first test.
void zCar::ApplyCollisionRebounds(xVec3* pVelocity)
{
    for (S32 i = 0; i < numCollisions; i++)
    {
        if (!(collisionUserFlags[i] & 0x1))
        {
            continue;
        }

        F32 dot = xVec3Dot(&collisions[i].norm, pVelocity);
        if (dot >= 0.0f)
        {
            continue;
        }

        F32 oldVertSpeed = pVelocity->y;
        xVec3 initialVel = *pVelocity;

        pVelocity->y -= collisions[i].norm.y * (1.5f * dot);
        pVelocity->x -= collisions[i].norm.x * (1.7f * dot);
        pVelocity->z -= collisions[i].norm.z * (1.7f * dot);

        if (xacos(xVec3Dot(&mat.at, &collisions[i].norm)) > 2.5307274f)
        {
            xVec3 forwardVel = mat.at * xVec3Dot(&mat.at, &initialVel);
            if (-xVec3Dot(&collisions[i].norm, &forwardVel) > 10.0f)
            {
                SoundPlayCollision(eCollisionTypeHeadOn, 1.0f);
                animCrash = TRUE;
            }
        }
        else
        {
            xVec3 velocityDelta = *pVelocity - initialVel;
            F32 vertDelta = xabs(velocityDelta.y);
            F32 deltaSpeed = xVec3Length(&velocityDelta);

            if (oldVertSpeed < -3.0f && vertDelta > 3.0f)
            {
                F32 volumeFactor = (vertDelta - 3.0f) / 9.0f;
                volumeFactor = CLAMP(volumeFactor, 0.0f, 1.0f);
                SoundPlayCollision(eCollisionTypeJumpLand, volumeFactor);
            }
            else if (deltaSpeed > 10.0f && xabs(collisions[i].norm.y) < 0.4f)
            {
                F32 volumeFactor = (deltaSpeed - 10.0f) / 15.0f;
                volumeFactor = CLAMP(volumeFactor, 0.0f, 1.0f);
                if (volumeFactor)
                {
                    SoundPlayCollision(eCollisionTypeSideKnock, volumeFactor);
                }
            }
        }
    }
}

// The jump case's 'else return' gives the dead branch retail keeps after the jump-state assignment.
void zCar::UpdateAnimState(xEnt* pEnt)
{
    if (animInJumpAnimation)
    {
        pEnt->model->Anim->Single->BilinearLerp[0] = animJumpOpeness;
    }
    else
    {
        pEnt->model->Anim->Single->BilinearLerp[0] = animLean;
    }

    if (damageLevel == eDamage3)
    {
        currentAnimState = eANIMSTATE_Death;
    }
    else if (animStartSpinLeft)
    {
        currentAnimState = eANIMSTATE_SpinLeft;
        isInDamageSpin = TRUE;
        animStartSpinLeft = FALSE;
    }
    else if (animStartSpinRight)
    {
        currentAnimState = eANIMSTATE_SpinRight;
        isInDamageSpin = TRUE;
        animStartSpinRight = FALSE;
    }
    else if (!inGroundContact && timeOffGround > 0.06f)
    {
        if (currentAnimState != eANIMSTATE_JumpLandSquash)
        {
            currentAnimState = eANIMSTATE_Jump;
        }
        else
        {
            return;
        }
    }
    else if (animHeavyLanding)
    {
        animHeavyLanding = FALSE;
        currentAnimState = eANIMSTATE_JumpLandSquash;
    }
    else if (animSuccessMode)
    {
        currentAnimState = eANIMSTATE_Success;
    }
    else if (animFailedMode)
    {
        currentAnimState = eANIMSTATE_Failed;
    }
    else
    {
        F32 speed = GetSignedSpeed();

        if (animCrash)
        {
            currentAnimState = eANIMSTATE_Crash;
            animCrash = FALSE;
        }
        else if (animReversing)
        {
            currentAnimState = eANIMSTATE_Reversing;
        }
        else if (speed < 2.0f)
        {
            currentAnimState = eANIMSTATE_Idle;
        }
        else if (speed > 25.0f && boostTimeRemaining > 0.0f)
        {
            currentAnimState = eANIMSTATE_Boosting;
        }
        else if (pCurrentSurface->useSlideAnimation)
        {
            currentAnimState = eANIMSTATE_DriveSlippy;
        }
        else
        {
            currentAnimState = eANIMSTATE_Drive;
        }
    }
}

U32 zCar::IdleCheck(xAnimTransition*, xAnimSingle*, void*)
{
    zCar* pCar = zEntPlayerCarGetCar();
    return pCar->currentAnimState == eANIMSTATE_Idle;
}

U32 zCar::StartDrivingCheck(xAnimTransition*, xAnimSingle*, void*)
{
    zCar* pCar = zEntPlayerCarGetCar();
    return pCar->currentAnimState == eANIMSTATE_Drive;
}

U32 zCar::StartDrivingSlippyCheck(xAnimTransition*, xAnimSingle*, void*)
{
    zCar* pCar = zEntPlayerCarGetCar();
    return pCar->currentAnimState == eANIMSTATE_DriveSlippy;
}

U32 zCar::StartBoostingCheck(xAnimTransition*, xAnimSingle*, void*)
{
    zCar* pCar = zEntPlayerCarGetCar();
    return pCar->currentAnimState == eANIMSTATE_Boosting;
}

U32 zCar::ReverseCheck(xAnimTransition*, xAnimSingle*, void*)
{
    zCar* pCar = zEntPlayerCarGetCar();
    return pCar->currentAnimState == eANIMSTATE_Reversing;
}

U32 zCar::JumpCheck(xAnimTransition*, xAnimSingle*, void*)
{
    zCar* pCar = zEntPlayerCarGetCar();
    return pCar->currentAnimState == eANIMSTATE_Jump;
}

U32 zCar::JumpEndCheck(xAnimTransition*, xAnimSingle*, void*)
{
    zCar* pCar = zEntPlayerCarGetCar();
    return pCar->currentAnimState == eANIMSTATE_JumpLandSquash;
}

U32 zCar::CrashCheck(xAnimTransition*, xAnimSingle*, void*)
{
    zCar* pCar = zEntPlayerCarGetCar();
    return pCar->currentAnimState == eANIMSTATE_Crash;
}

U32 zCar::SpinLeftCheck(xAnimTransition*, xAnimSingle*, void*)
{
    zCar* pCar = zEntPlayerCarGetCar();
    return pCar->currentAnimState == eANIMSTATE_SpinLeft;
}

U32 zCar::SpinRightCheck(xAnimTransition*, xAnimSingle*, void*)
{
    zCar* pCar = zEntPlayerCarGetCar();
    return pCar->currentAnimState == eANIMSTATE_SpinRight;
}

U32 zCar::SuccessCheck(xAnimTransition*, xAnimSingle*, void*)
{
    zCar* pCar = zEntPlayerCarGetCar();
    return pCar->currentAnimState == eANIMSTATE_Success;
}

U32 zCar::FailedCheck(xAnimTransition*, xAnimSingle*, void*)
{
    zCar* pCar = zEntPlayerCarGetCar();
    return pCar->currentAnimState == eANIMSTATE_Failed;
}

U32 zCar::DeathCheck(xAnimTransition*, xAnimSingle*, void*)
{
    zCar* pCar = zEntPlayerCarGetCar();
    return pCar->currentAnimState == eANIMSTATE_Death;
}

U32 zCar::DamageSpinCompleteCB(xAnimTransition*, xAnimSingle* anim, void*)
{
    zCar* pCar = zEntPlayerCarGetCar();
    anim->BilinearLerp[0] = 1.0f;
    pCar->isInDamageSpin = FALSE;
    return 0;
}

U32 zCar::AnimDefaultCB(xAnimTransition*, xAnimSingle* anim, void*)
{
    zCar* pCar = zEntPlayerCarGetCar();
    anim->BilinearLerp[0] = pCar->animLean;
    return 0;
}

U32 zCar::JumpStartedCB(xAnimTransition*, xAnimSingle*, void*)
{
    zCar* pCar = zEntPlayerCarGetCar();
    pCar->animInJumpAnimation = TRUE;
    return 0;
}

U32 zCar::JumpEndedCB(xAnimTransition*, xAnimSingle*, void*)
{
    zCar* pCar = zEntPlayerCarGetCar();
    pCar->animInJumpAnimation = FALSE;
    return 0;
}

xAnimTable* zCar::CreateAnimTable()
{
    xAnimTable* table = xAnimTableNew("Car", 0);

    xAnimTableNewState(table, "Idle01", 0x10, 0x41, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, "Drive", 0x10, 0x46, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, "DriveSlippy", 0x10, 0x46, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, "Boosting", 0x10, 0x46, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, "Reversing", 0x10, 0x6, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, "Crash", 0x20, 0x6, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, "SpinLeft", 0x20, 0x6, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, "SpinRight", 0x20, 0x6, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, "Success", 0x10, 0x6, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, "Failed", 0, 0x6, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, "Death", 0, 0x6, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, "Jump", 0x10, 0x6, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, "JumpLandSquash", 0x20, 0x6, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);

    xAnimTableNewTransition(table, "Idle01 DriveSlippy Boosting Reversing", "Drive",
                            StartDrivingCheck, AnimDefaultCB, 0, 0, 0.0f, 0.0f, 1, 0, 0.2f, NULL);
    xAnimTableNewTransition(table, "Idle01 Drive Boosting Reversing", "DriveSlippy",
                            StartDrivingSlippyCheck, NULL, 0, 0, 0.0f, 0.0f, 1, 0, 0.2f, NULL);
    xAnimTableNewTransition(table, "Drive DriveSlippy Boosting Reversing", "Idle01", IdleCheck,
                            AnimDefaultCB, 0, 0, 0.0f, 0.0f, 1, 0, 0.2f, NULL);
    xAnimTableNewTransition(table, "Idle01 Drive DriveSlippy", "Boosting", StartBoostingCheck, NULL,
                            0, 0, 0.0f, 0.0f, 1, 0, 0.2f, NULL);
    xAnimTableNewTransition(table, "Idle01", "Reversing", ReverseCheck, AnimDefaultCB, 0, 0, 0.0f,
                            0.0f, 1, 0, 0.2f, NULL);
    xAnimTableNewTransition(table,
                            "Idle01 Drive DriveSlippy Boosting Reversing JumpLandSquash Crash",
                            "SpinLeft", SpinLeftCheck, NULL, 0, 0, 0.0f, 0.0f, 1, 0, 0.0f, NULL);
    xAnimTableNewTransition(table,
                            "Idle01 Drive DriveSlippy Boosting Reversing JumpLandSquash Crash",
                            "SpinRight", SpinRightCheck, NULL, 0, 0, 0.0f, 0.0f, 1, 0, 0.0f, NULL);
    xAnimTableNewTransition(table, "Jump", "SpinLeft", SpinLeftCheck, JumpEndedCB, 0, 0, 0.0f, 0.0f,
                            1, 0, 0.0f, NULL);
    xAnimTableNewTransition(table, "Jump", "SpinRight", SpinRightCheck, JumpEndedCB, 0, 0, 0.0f,
                            0.0f, 1, 0, 0.0f, NULL);
    xAnimTableNewTransition(table, "SpinLeft", "Drive", StartDrivingCheck, DamageSpinCompleteCB,
                            0x10, 0, 0.0f, 0.0f, 1, 0, 0.2f, NULL);
    xAnimTableNewTransition(table, "SpinRight", "Drive", StartDrivingCheck, DamageSpinCompleteCB,
                            0x10, 0, 0.0f, 0.0f, 1, 0, 0.2f, NULL);
    xAnimTableNewTransition(table, "SpinLeft", "Idle01", IdleCheck, DamageSpinCompleteCB, 0x10, 0,
                            0.0f, 0.0f, 1, 0, 0.2f, NULL);
    xAnimTableNewTransition(table, "SpinRight", "Idle01", IdleCheck, DamageSpinCompleteCB, 0x10, 0,
                            0.0f, 0.0f, 1, 0, 0.2f, NULL);
    xAnimTableNewTransition(table, "SpinLeft", "Boosting", StartBoostingCheck, DamageSpinCompleteCB,
                            0x10, 0, 0.0f, 0.0f, 1, 0, 0.2f, NULL);
    xAnimTableNewTransition(table, "SpinRight", "Boosting", StartBoostingCheck,
                            DamageSpinCompleteCB, 0x10, 0, 0.0f, 0.0f, 1, 0, 0.2f, NULL);
    xAnimTableNewTransition(table, "Drive DriveSlippy Boosting", "Crash", CrashCheck, NULL, 0, 0,
                            0.0f, 0.0f, 1, 0, 0.2f, NULL);
    xAnimTableNewTransition(table, "Crash", "Drive", NULL, NULL, 0x10, 0, 0.0f, 0.0f, 1, 0, 0.2f,
                            NULL);
    xAnimTableNewTransition(table, "Crash", "Reversing", ReverseCheck, NULL, 0x10, 0, 0.0f, 0.0f, 1,
                            0, 0.2f, NULL);
    xAnimTableNewTransition(table, "Drive Boosting Reversing", "Jump", JumpCheck, JumpStartedCB, 0,
                            0, 0.0f, 0.0f, 1, 0, 0.05f, NULL);
    xAnimTableNewTransition(table, "Jump Drive", "JumpLandSquash", JumpEndCheck, JumpEndedCB, 0, 0,
                            0.0f, 0.0f, 1, 0, 0.15f, NULL);
    xAnimTableNewTransition(table, "Jump", "Drive", StartDrivingCheck, JumpEndedCB, 0, 0, 0.0f,
                            0.0f, 1, 0, 0.15f, NULL);
    xAnimTableNewTransition(table, "Jump", "DriveSlippy", StartDrivingSlippyCheck, JumpEndedCB, 0,
                            0, 0.0f, 0.0f, 1, 0, 0.15f, NULL);
    xAnimTableNewTransition(table, "Jump", "Idle01", IdleCheck, JumpEndedCB, 0, 0, 0.0f, 0.0f, 1, 0,
                            0.15f, NULL);
    xAnimTableNewTransition(table, "Jump", "Boosting", StartBoostingCheck, JumpEndedCB, 0, 0, 0.0f,
                            0.0f, 1, 0, 0.15f, NULL);
    xAnimTableNewTransition(table, "Jump", "Reversing", ReverseCheck, JumpEndedCB, 0, 0, 0.0f, 0.0f,
                            1, 0, 0.15f, NULL);
    xAnimTableNewTransition(table, "JumpLandSquash", "Drive", StartDrivingCheck, NULL, 0x10, 0,
                            0.0f, 0.0f, 2, 0, 0.15f, NULL);
    xAnimTableNewTransition(table, "JumpLandSquash", "DriveSlippy", StartDrivingSlippyCheck, NULL,
                            0x10, 0, 0.0f, 0.0f, 2, 0, 0.15f, NULL);
    xAnimTableNewTransition(table, "JumpLandSquash", "Boosting", StartBoostingCheck, NULL, 0x10, 0,
                            0.0f, 0.0f, 2, 0, 0.15f, NULL);
    xAnimTableNewTransition(table, "JumpLandSquash", "Reversing", ReverseCheck, NULL, 0x10, 0, 0.0f,
                            0.0f, 2, 0, 0.15f, NULL);
    xAnimTableNewTransition(table, "JumpLandSquash", "Idle01", NULL, NULL, 0x10, 0, 0.0f, 0.0f, 1,
                            0, 0.15f, NULL);
    xAnimTableNewTransition(table,
                            "Idle01 Drive DriveSlippy Boosting Reversing Jump JumpLandSquash Crash",
                            "Success", SuccessCheck, NULL, 0, 0, 0.0f, 0.0f, 1, 0, 0.2f, NULL);
    xAnimTableNewTransition(table, "Success", "Idle01", IdleCheck, NULL, 0, 0, 0.0f, 0.0f, 1, 0,
                            0.0f, NULL);
    xAnimTableNewTransition(table,
                            "Idle01 Drive DriveSlippy Boosting Reversing Jump JumpLandSquash Crash",
                            "Failed", FailedCheck, NULL, 0, 0, 0.0f, 0.0f, 1, 0, 0.2f, NULL);
    xAnimTableNewTransition(table, "Failed", "Idle01", IdleCheck, NULL, 0, 0, 0.0f, 0.0f, 1, 0,
                            0.0f, NULL);
    xAnimTableNewTransition(table,
                            "Idle01 Drive DriveSlippy Boosting Reversing Jump JumpLandSquash Crash",
                            "Death", DeathCheck, NULL, 0, 0, 0.0f, 0.0f, 1, 0, 0.2f, NULL);
    xAnimTableNewTransition(table, "Death", "Idle01", IdleCheck, NULL, 0, 0, 0.0f, 0.0f, 1, 0, 0.0f,
                            NULL);

    return table;
}

void zCar::UpdateVirtualBunTop(F32 dt)
{
    xVec3 idealBunPos = mat.pos + mat.up * 2.5f;
    xVec3 delta = idealBunPos - animVirtualBunTopPos;

    if (isInDamageSpin)
    {
        delta = delta * (10.0f * dt);
    }
    else
    {
        delta = delta * (10.0f * (0.2f * dt));
    }

    if (xVec3Dot(&mat.at, &velocity) > 8.0f)
    {
        delta += mat.right * (-0.015f * dt * steeringPosition);
    }

    animVirtualBunTopVel = animVirtualBunTopVel * 0.8f + delta;
    animVirtualBunTopPos += animVirtualBunTopVel;

    xVec3 newDelta = animVirtualBunTopPos - idealBunPos;
    // Retail makes both xVec3Dot calls before the subtraction.
    F32 forwardDist = xVec3Dot(&mat.at, &newDelta);
    F32 sideDist = xVec3Dot(&mat.right, &newDelta);
    animVirtualBunTopPos -= mat.at * forwardDist;

    animLean = CLAMP(sideDist, -2.0f, 2.0f) / 2.0f + 1.0f;

    if (currentAnimState != eANIMSTATE_Jump)
    {
        animVirtualBunTopHeight = mat.pos.y;
        animVirtualBunTopVerticalVel = velocity.y;
    }

    animVirtualBunTopVerticalVel -= 0.15f;
    animVirtualBunTopHeight += animVirtualBunTopVerticalVel * dt;

    if (animVirtualBunTopHeight < mat.pos.y)
    {
        animVirtualBunTopHeight = mat.pos.y;
        animVirtualBunTopVerticalVel = velocity.y;
    }

    if (animVirtualBunTopHeight - mat.pos.y > 2.0f)
    {
        animVirtualBunTopHeight = mat.pos.y + 2.0f;
        animVirtualBunTopVerticalVel = velocity.y;
    }

    animJumpOpeness = (animVirtualBunTopHeight - mat.pos.y) / 2.0f;
    animJumpOpeness = isin((PI / 2.0f) * animJumpOpeness);
}

// No GC or PS2 symbol (always inlined into UpdateEffects); the name is inferred from
// DriveSurfaceFXInternal.
inline void zCar::DriveSurfaceFX(const xModelInstance* pModel, F32 dt)
{
    if (collisionTypes[ezDRIVINGSURF_ThickOil])
    {
        DriveSurfaceFXInternal(pModel, dt, ezDRIVINGSURF_ThickOil);
    }
    else if (collisionTypes[ezDRIVINGSURF_HotSauce])
    {
        DriveSurfaceFXInternal(pModel, dt, ezDRIVINGSURF_HotSauce);
    }
    else
    {
        for (S32 i = 0; i < ezDRIVINGSURF_MaxTypes; i++)
        {
            if (collisionTypes[i])
            {
                DriveSurfaceFXInternal(pModel, dt, (zDrivingSurf)i);
            }
        }
    }
}

void zCar::UpdateEffects(F32 dt)
{
    F32 rightSpeed = xVec3Dot(&mat.right, &velocity);

    skidFXIntensity = (xabs(rightSpeed) - 3.0f) / 3.0f;
    if (boostTimeRemaining > 0.0f)
    {
        skidFXIntensity = CLAMP(skidFXIntensity, 0.1f, 1.0f);
    }
    else
    {
        skidFXIntensity = CLAMP(skidFXIntensity, 0.0f, 1.0f);
    }

    UpdatePropellor(pEnt->model);
    DriveSurfaceFX(pEnt->model, dt);
    StreakFX(pEnt, dt);
    UpdateVirtualBunTop(dt);
}

// No PS2 DWARF entry (GameCube-only function); local names are ours.
void zCar::UpdatePropellor(xModelInstance* pModel)
{
    F32 speed = xVec3Length(&velocity);
    animPropellorAngle = (animPropellorAngle + (S32)(1.3f * speed)) % 360;
    xMat3x3Euler((xMat3x3*)&pModel->Mat[12], 0.0f, 0.0f, (PI / 180.0f) * animPropellorAngle);

    xVec3 propOffset = *(xVec3*)&pModel->Mat[12].pos;
    xVec3 propPos;
    xMat4x3Toworld(&propPos, (xMat4x3*)pModel->Mat, &propOffset);

    F32 bubbleSpeed = xVec3Length(&velocity);
    if (bubbleSpeed > 25.0f)
    {
        bubbleSpeed = 25.0f;
    }

    zFX_SpawnBubbleTrail(&propPos, (U32)(4.0f * (bubbleSpeed / 25.0f)) + 1);
}

// Note: w spans the matrix up axis and h the right axis.
static void zCarRenderOneBrakeLight(xMat4x3* pMat)
{
    static RxObjSpace3DVertex sStripVert[4];

    xVec3 w;
    xVec3 h;
    xVec3 d;
    xVec3 pos;

    h = pMat->right;
    w = pMat->up;
    d = pMat->at;
    pos = pMat->pos;

    h *= 0.125f;
    w *= 0.2f;
    d *= -0.06f;

    pos = pos + d;

    RwIm3DVertexSetPos(&sStripVert[0], pos.x - w.x - h.x, pos.y - w.y - h.y, pos.z - w.z - h.z);
    sStripVert[0].u = 0.0f;
    sStripVert[0].v = 0.0f;
    RwIm3DVertexSetRGBA(&sStripVert[0], 255, 255, 255, 255);

    RwIm3DVertexSetPos(&sStripVert[1], pos.x - w.x + h.x, pos.y - w.y + h.y, pos.z - w.z + h.z);
    sStripVert[1].u = 0.0f;
    sStripVert[1].v = 1.0f;
    RwIm3DVertexSetRGBA(&sStripVert[1], 255, 255, 255, 255);

    RwIm3DVertexSetPos(&sStripVert[2], pos.x + w.x - h.x, pos.y + w.y - h.y, pos.z + w.z - h.z);
    sStripVert[2].u = 1.0f;
    sStripVert[2].v = 0.0f;
    RwIm3DVertexSetRGBA(&sStripVert[2], 255, 255, 255, 255);

    RwIm3DVertexSetPos(&sStripVert[3], pos.x + w.x + h.x, pos.y + w.y + h.y, pos.z + w.z + h.z);
    sStripVert[3].u = 1.0f;
    sStripVert[3].v = 1.0f;
    RwIm3DVertexSetRGBA(&sStripVert[3], 255, 255, 255, 255);

    if (RwIm3DTransform(sStripVert, 4, NULL,
                        rwIM3D_VERTEXUV | rwIM3D_VERTEXXYZ | rwIM3D_VERTEXRGBA))
    {
        RwIm3DRenderPrimitive(rwPRIMTYPETRISTRIP);
        RwIm3DEnd();
    }
}

void zCar::RenderBrakeLights(const xModelInstance* pModel)
{
    xMat4x3 brakeLightMatL;
    xMat4x3 brakeLightMatR;

    if (!animBrakeLightsOn)
    {
        return;
    }

    if (!sBrakeLightTex)
    {
        return;
    }

    xMat4x3Mul(&brakeLightMatL, (xMat4x3*)&pModel->Mat[30], (xMat4x3*)pModel->Mat);
    xMat4x3Mul(&brakeLightMatR, (xMat4x3*)&pModel->Mat[31], (xMat4x3*)pModel->Mat);

    RwRenderStateSet(rwRENDERSTATETEXTURERASTER, (void*)sBrakeLightTex->raster);
    RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)rwBLENDSRCALPHA);
    RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDONE);

    zCarRenderOneBrakeLight(&brakeLightMatL);
    zCarRenderOneBrakeLight(&brakeLightMatR);

    RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)rwBLENDSRCALPHA);
    RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDINVSRCALPHA);
}

void zCar::DriveSurfaceFXInternal(const xModelInstance* pModel, F32 dt, zDrivingSurf collisionType)
{
    F32 intensityFactor;
    zParEmitter* pEmit;
    xParEmitterCustomSettings parEmitInfo;
    F32 deathSize;
    F32 rate;
    xMat4x3* pMat;
    xVec3 parVelStartPt;
    xVec3 parVelEndPt;

    if (!inGroundContact)
    {
        return;
    }

    intensityFactor = skidFXIntensity;
    if (intensityFactor == 0.0f &&
        (collisionType == ezDRIVINGSURF_Tarmac || collisionType == ezDRIVINGSURF_Cement ||
         collisionType == ezDRIVINGSURF_Railroad || collisionType == ezDRIVINGSURF_SideWalk ||
         collisionType == ezDRIVINGSURF_Wall))
    {
        return;
    }

    if (xVec3Length(&velocity) <= 0.5f)
    {
        return;
    }

    switch (collisionType)
    {
    case ezDRIVINGSURF_Tarmac:
    case ezDRIVINGSURF_Cement:
    case ezDRIVINGSURF_SideWalk:
        pEmit = sCarSmokeEmit;
        break;
    case ezDRIVINGSURF_Railroad:
        intensityFactor *= 0.25f;
        pEmit = sCarSmokeEmit;
        break;
    case ezDRIVINGSURF_Grass:
        pEmit = sCarGrassEmit;
        break;
    case ezDRIVINGSURF_Sand:
        intensityFactor = 0.5f * intensityFactor + 0.5f;
        pEmit = sCarSandEmit;
        break;
    case ezDRIVINGSURF_HardDirt:
        intensityFactor *= 0.5f;
        pEmit = sCarDirtEmit;
        break;
    case ezDRIVINGSURF_SoftDirt:
        pEmit = sCarDirtEmit;
        break;
    case ezDRIVINGSURF_SurfaceWater:
        pEmit = sCarWaterEmit;
        break;
    case ezDRIVINGSURF_HotSauce:
        pEmit = sCarSteamEmit;
        intensityFactor = 1.0f;
        break;
    case ezDRIVINGSURF_ThickOil:
        pEmit = sCarOilEmit;
        break;
    case ezDRIVINGSURF_Default:
    default:
        return;
    }

    if (pEmit == NULL)
    {
        return;
    }

    memset(&parEmitInfo, 0, sizeof(parEmitInfo));
    parEmitInfo.custom_flags = 0x318;

    if (collisionTypes[ezDRIVINGSURF_Railroad])
    {
        deathSize = 1.5f * intensityFactor + 1.5f;
    }
    else if (collisionTypes[ezDRIVINGSURF_Grass])
    {
        deathSize = 0.5f + intensityFactor;
    }
    else if (collisionTypes[ezDRIVINGSURF_ThickOil])
    {
        deathSize = 7.0f;
    }
    else
    {
        deathSize = 3.5f * intensityFactor + 3.5f;
    }
    parEmitInfo.size_death.set(deathSize, deathSize, 1.0f, 0);

    rate = 70.0f * intensityFactor + 10.0f;
    if (collisionTypes[ezDRIVINGSURF_Sand])
    {
        rate *= 0.25f;
    }
    else if (collisionTypes[ezDRIVINGSURF_HotSauce])
    {
        rate = 10.0f;
    }
    else if (collisionTypes[ezDRIVINGSURF_ThickOil])
    {
        rate = 10.0f;
    }
    else if (collisionTypes[ezDRIVINGSURF_SoftDirt])
    {
        rate *= 0.5f;
    }
    else if (collisionTypes[ezDRIVINGSURF_HardDirt])
    {
        rate *= 0.25f;
    }
    parEmitInfo.rate.set(rate, rate, 1.0f, 0);

    pMat = (xMat4x3*)pModel->Mat;

    parVelStartPt.x = parVelStartPt.y = parVelStartPt.z = 0.0f;

    if (collisionTypes[ezDRIVINGSURF_Sand])
    {
        xVec3Init(&parVelEndPt, 0.0f, 6.75f, 0.0f);
    }
    else if (collisionTypes[ezDRIVINGSURF_SurfaceWater])
    {
        xVec3Init(&parVelEndPt, 0.0f, 0.25f * xVec3Length(&velocity) + 2.0f, 0.0f);
    }
    else if (collisionTypes[ezDRIVINGSURF_HotSauce])
    {
        xVec3Init(&parVelEndPt, 0.0f, 0.25f * xVec3Length(&velocity) + 2.0f, 0.0f);
    }
    else if (collisionTypes[ezDRIVINGSURF_ThickOil])
    {
        xVec3Init(&parVelEndPt, 0.0f, 0.25f * xVec3Length(&velocity) + 2.0f, 0.0f);
    }
    else if (collisionTypes[ezDRIVINGSURF_Grass])
    {
        xVec3Init(&parVelEndPt, 0.0f, 0.125f * xVec3Length(&velocity) + 1.0f, 0.0f);
    }
    else if (collisionTypes[ezDRIVINGSURF_HardDirt])
    {
        xVec3Init(&parVelEndPt, 0.0f, 0.0f, -0.05f * xVec3Length(&velocity));
    }
    else if (collisionTypes[ezDRIVINGSURF_SoftDirt])
    {
        xVec3Init(&parVelEndPt, 0.0f, 0.0f, -0.05f * xVec3Length(&velocity));
    }
    else
    {
        xVec3Init(&parVelEndPt, 0.0f, 0.0f, -4.5f);
    }

    xMat4x3Toworld(&parVelStartPt, pMat, &parVelStartPt);
    xMat4x3Toworld(&parVelEndPt, pMat, &parVelEndPt);

    xVec3 parVel = parVelEndPt - parVelStartPt;

    if (collisionTypes[ezDRIVINGSURF_SurfaceWater])
    {
        xVec3 carVelocity = velocity;
        carVelocity *= 0.5f;
        parVel = parVel + carVelocity;
    }
    else if (collisionTypes[ezDRIVINGSURF_Grass])
    {
        xVec3 carVelocity = velocity;
        carVelocity *= 0.25f;
        parVel = parVel + carVelocity;
    }
    else if (collisionTypes[ezDRIVINGSURF_HotSauce])
    {
        xVec3 carVelocity = velocity;
        carVelocity *= 0.25f;
        parVel = parVel + carVelocity;
    }
    else if (collisionTypes[ezDRIVINGSURF_ThickOil])
    {
        xVec3 carVelocity = velocity;
        carVelocity *= 0.25f;
        parVel = parVel + carVelocity;
    }

    for (S32 i = 0; i < 4; i++)
    {
        xVec3 parPos = ((xMat4x3*)pModel->Mat)[sTireBones[i]].pos;
        parPos.y = 0.0f;
        xMat4x3Toworld(&parPos, pMat, &parPos);

        parEmitInfo.pos = parPos;
        parEmitInfo.vel = parVel;
        parEmitInfo.last_emit_position = lastPositionPar[i];
        parEmitInfo.last_emit_velocity = lastVelocityPar[i];
        xParEmitterEmitCustom(pEmit, dt, &parEmitInfo);
        lastPositionPar[i] = parEmitInfo.pos;
        lastVelocityPar[i] = parEmitInfo.vel;
    }
}

void zCar::StreakFX(xEnt* ent, F32 dt)
{
    xMat4x3* pMat = (xMat4x3*)ent->model->Mat;
    S32 i;

    if (boostTimeRemaining > 0.0f)
    {
        for (i = 0; i < 2; i++)
        {
            sStreakInfo[i].activated = FALSE;
        }

        sStreakInfo[0].activated = TRUE;
        sStreakInfo[1].activated = TRUE;

        for (i = 0; i < 2; i++)
        {
            if (sStreakInfo[i].activated && sStreakInfo[i].streakID == 0xDEAD)
            {
                sStreakInfo[i].streakID =
                    xFXStreakStart(0.0f, 4.0f, sStreakInfo[i].alphaStart, 0, &sStreakInfo[i].colA,
                                   &sStreakInfo[i].colB, sStreakInfo[i].streakTaper);
            }
            else if (!sStreakInfo[i].activated && sStreakInfo[i].streakID != 0xDEAD)
            {
                xFXStreakStop(sStreakInfo[i].streakID);
                sStreakInfo[i].streakID = 0xDEAD;
            }

            if (sStreakInfo[i].streakID != 0xDEAD)
            {
                sStreakInfo[i].a = ((xMat4x3*)ent->model->Mat)[sStreakInfo[i].bone].pos;
                xMat4x3Toworld(&sStreakInfo[i].a, pMat, &sStreakInfo[i].a);

                xMat4x3* pBoneMat = &((xMat4x3*)ent->model->Mat)[sStreakInfo[i].bone];
                xVec3 width = pBoneMat->right;
                if (i == 0)
                {
                    width *= 0.5f;
                }
                else
                {
                    width *= -0.5f;
                }

                sStreakInfo[i].b = pBoneMat->pos + width;
                xMat4x3Toworld(&sStreakInfo[i].b, pMat, &sStreakInfo[i].b);

                xFXStreakUpdate(sStreakInfo[i].streakID, &sStreakInfo[i].a, &sStreakInfo[i].b);
            }
        }

        if (sCarKetchupEmit != NULL && sCarMustardEmit != NULL)
        {
            xParEmitterCustomSettings ketchupInfo;
            memset(&ketchupInfo, 0, sizeof(ketchupInfo));
            ketchupInfo.custom_flags = 0x310;
            ketchupInfo.rate.set(10.0f, 20.0f, 1.0f, 2);

            xVec3 velStartPt;
            xVec3 velEndPt;
            velStartPt.assign(0.0f, 0.0f, 0.0f);
            velEndPt.assign(0.0f, 0.0f, -4.5f);
            xMat4x3Toworld(&velStartPt, pMat, &velStartPt);
            xMat4x3Toworld(&velEndPt, pMat, &velEndPt);

            xVec3 ketchupVel = velEndPt - velStartPt;
            xVec3 ketchupPos = ((xMat4x3*)ent->model->Mat)[14].pos;
            xMat4x3Toworld(&ketchupPos, pMat, &ketchupPos);

            ketchupInfo.vel = ketchupVel;
            ketchupInfo.pos = ketchupPos;

            if (xurand() > 0.5f)
            {
                ketchupInfo.last_emit_position = lastPositionKetchup;
                ketchupInfo.last_emit_velocity = lastVelocityKetchup;
                xParEmitterEmitCustom(sCarKetchupEmit, dt, &ketchupInfo);
                lastPositionKetchup = ketchupInfo.pos;
                lastVelocityKetchup = ketchupInfo.vel;
            }
            else
            {
                ketchupInfo.last_emit_position = lastPositionMustard;
                ketchupInfo.last_emit_velocity = lastVelocityMustard;
                xParEmitterEmitCustom(sCarMustardEmit, dt, &ketchupInfo);
                lastPositionMustard = ketchupInfo.pos;
                lastVelocityMustard = ketchupInfo.vel;
            }

            if (sCarSeedsEmit != NULL)
            {
                xParEmitterCustomSettings seedsInfo;
                memset(&seedsInfo, 0, sizeof(seedsInfo));
                seedsInfo.custom_flags = 0x100;
                seedsInfo.pos = pMat->pos;
                seedsInfo.last_emit_position = lastPositionSeed;
                xParEmitterEmitCustom(sCarSeedsEmit, dt, &seedsInfo);
                lastPositionSeed = seedsInfo.pos;
            }
        }
    }
}

// center is not in the PS2 DWARF, but retail stores a dead copy of the sum before the loop
void zCar::ModelSwap(eDamage newDamageLevel, bool swapEffects)
{
    xModelInstance* oldModelInstance;
    xVec3* posbuf;
    xVec3* velbuf;
    xVec3* pp;
    xVec3* vp;
    xSphere unitSphere;
    S32 j;

    if (damageLevel == newDamageLevel)
    {
        return;
    }

    oldModelInstance = pEnt->model;
    pEnt->model = sDamageModelInstances[newDamageLevel];
    pEnt->model->Anim = oldModelInstance->Anim;
    pEnt->model->Anim->ModelInst = pEnt->model;
    pEnt->model->Flags |= 0x104;
    *(xMat4x3*)pEnt->model->Mat = *(xMat4x3*)oldModelInstance->Mat;

    damageLevel = newDamageLevel;

    if (swapEffects)
    {
        posbuf = (xVec3*)xMemPushTemp(200 * sizeof(xVec3));
        velbuf = posbuf + 100;
        if (posbuf)
        {
            xVec3 center;

            pp = posbuf;
            vp = velbuf;
            unitSphere.r = 1.0f;
            xVec3Init(&unitSphere.center, 0.0f, 0.0f, 0.0f);

            GetCenter(&center);

            for (j = 0; j < 100; j++)
            {
                xSphereRandomPointOnSphere(&unitSphere, *vp);
                *pp = *vp;
                *pp *= boundRadius;
                *pp += center;
                pp++;
                vp++;
            }
        }

        zParPTankSpawnBubbles(posbuf, velbuf, 100, 1.0f);
        xMemPopTemp(posbuf);
    }
}

void zCar::SoundInitAssetTable()
{
    // Only the first entry is cleared; every entry in use is assigned below
    memset(sSoundAsset, 0, sizeof(iSndGroupHandle));

    sSoundAsset[1] = xSndMgrGetSoundGroup(xStrHash("ENGINE_LOOP_SG"));
    sSoundAsset[2] = xSndMgrGetSoundGroup(xStrHash("ENGINE_DAMAGE1_LOOP_SG"));
    sSoundAsset[3] = xSndMgrGetSoundGroup(xStrHash("ENGINE_DAMAGE2_LOOP_SG"));

    sSoundAsset[7] = xSndMgrGetSoundGroup(xStrHash("IGNITION_SG"));
    sSoundAsset[8] = xSndMgrGetSoundGroup(xStrHash("ENGINE_STOP_SG"));
    sSoundAsset[9] = xSndMgrGetSoundGroup(xStrHash("BACKFIRE_SG"));
    sSoundAsset[10] = xSndMgrGetSoundGroup(xStrHash("REVERSE_BEEP_SG"));
    sSoundAsset[11] = xSndMgrGetSoundGroup(xStrHash("GEAR_CRUNCH_SG"));
    sSoundAsset[12] = xSndMgrGetSoundGroup(xStrHash("BOOST_START_SG"));
    sSoundAsset[13] = xSndMgrGetSoundGroup(xStrHash("BOOST_LOOP_SG"));
    sSoundAsset[14] = xSndMgrGetSoundGroup(xStrHash("BOOST_END_SG"));
    sSoundAsset[15] = xSndMgrGetSoundGroup(xStrHash("JUMP_LAND_SG"));

    sSoundAsset[18] = xSndMgrGetSoundGroup(xStrHash("PATTY_HIT_SG"));
    sSoundAsset[19] = xSndMgrGetSoundGroup(xStrHash("PATTY_HIT_DAMAGE_SG"));
    sSoundAsset[20] = xSndMgrGetSoundGroup(xStrHash("PATTY_CAR_DESTROYED_SG"));

    sSoundAsset[25] = xSndMgrGetSoundGroup(xStrHash("PATTY_HEALTH_PICKUP_SG"));
    sSoundAsset[26] = xSndMgrGetSoundGroup(xStrHash("PATTY_BOOST_PICKUP_SG"));

    sSoundAsset[29] = xSndMgrGetSoundGroup(xStrHash("PATTY_HORN1_SG"));
    sSoundAsset[30] = xSndMgrGetSoundGroup(xStrHash("PATTY_HORN2_SG"));
    sSoundAsset[31] = xSndMgrGetSoundGroup(xStrHash("PATTY_HORN3_SG"));
    sSoundAsset[32] = xSndMgrGetSoundGroup(xStrHash("PATTY_HORN4_SG"));

    surfaceDescriptors[ezDRIVINGSURF_Tarmac].soundGroupSkid =
        xSndMgrGetSoundGroup(xStrHash("SKID_TARMAC_SG"));
    surfaceDescriptors[ezDRIVINGSURF_Cement].soundGroupSkid =
        xSndMgrGetSoundGroup(xStrHash("SKID_TARMAC_SG"));
    surfaceDescriptors[ezDRIVINGSURF_HardDirt].soundGroupDrive =
        xSndMgrGetSoundGroup(xStrHash("DRIVE_HARD_DIRT_SG"));
    surfaceDescriptors[ezDRIVINGSURF_HardDirt].soundGroupSkid =
        xSndMgrGetSoundGroup(xStrHash("SKID_HARD_DIRT_SG"));
    surfaceDescriptors[ezDRIVINGSURF_Railroad].soundGroupDrive =
        xSndMgrGetSoundGroup(xStrHash("DRIVE_RAILROAD_SG"));
    surfaceDescriptors[ezDRIVINGSURF_Railroad].soundGroupSkid =
        xSndMgrGetSoundGroup(xStrHash("SKID_RAILROAD_SG"));
    surfaceDescriptors[ezDRIVINGSURF_Grass].soundGroupDrive =
        xSndMgrGetSoundGroup(xStrHash("DRIVE_GRASS_SG"));
    surfaceDescriptors[ezDRIVINGSURF_Grass].soundGroupSkid =
        xSndMgrGetSoundGroup(xStrHash("SKID_GRASS_SG"));
    surfaceDescriptors[ezDRIVINGSURF_SoftDirt].soundGroupDrive =
        xSndMgrGetSoundGroup(xStrHash("DRIVE_SOFT_DIRT_SG"));
    surfaceDescriptors[ezDRIVINGSURF_SoftDirt].soundGroupSkid =
        xSndMgrGetSoundGroup(xStrHash("SKID_HARD_DIRT_SG"));
    surfaceDescriptors[ezDRIVINGSURF_SideWalk].soundGroupDrive =
        xSndMgrGetSoundGroup(xStrHash("DRIVE_SIDEWALK_SG"));
    surfaceDescriptors[ezDRIVINGSURF_SideWalk].soundGroupSkid =
        xSndMgrGetSoundGroup(xStrHash("SKID_TARMAC_SG"));
    surfaceDescriptors[ezDRIVINGSURF_Sand].soundGroupDrive =
        xSndMgrGetSoundGroup(xStrHash("DRIVE_SAND_SG"));
    surfaceDescriptors[ezDRIVINGSURF_Sand].soundGroupSkid =
        xSndMgrGetSoundGroup(xStrHash("SKID_SAND_SG"));
    surfaceDescriptors[ezDRIVINGSURF_SurfaceWater].soundGroupDrive =
        xSndMgrGetSoundGroup(xStrHash("DRIVE_WATER_SG"));
    surfaceDescriptors[ezDRIVINGSURF_SurfaceWater].soundGroupSkid =
        xSndMgrGetSoundGroup(xStrHash("SKID_WATER_SG"));
    surfaceDescriptors[ezDRIVINGSURF_ThickOil].soundGroupDrive =
        xSndMgrGetSoundGroup(xStrHash("DRIVE_OIL_SG"));
    surfaceDescriptors[ezDRIVINGSURF_ThickOil].soundGroupSkid =
        xSndMgrGetSoundGroup(xStrHash("SKID_OIL_SG"));
    surfaceDescriptors[ezDRIVINGSURF_HotSauce].soundGroupDrive =
        xSndMgrGetSoundGroup(xStrHash("DRIVE_HOT_SAUCE_SG"));
}

void zCar::SoundPlayHorn()
{
    if (zUICustomExtrasManager::IsCheatEnabled(eExtrasScreenCheats_AltCarHorn))
    {
        // PATTY_HORN4_SG
        xSndMgrPlay(sSoundAsset[32], 2, NULL, NULL, pEnt, NULL, NULL);
    }
    else
    {
        // PATTY_HORN1_SG .. PATTY_HORN3_SG
        // Retail really performs this -1.0f "no volume override" test; volume and horn are
        // probably the parameters of an inlined sound helper.
        F32 volume = -1.0f;
        S32 horn = rand() % 3 + 29;

        if (volume == -1.0f)
        {
            xSndMgrPlay(sSoundAsset[horn], 2, NULL, NULL, pEnt, NULL, NULL);
        }
        else
        {
            xSndMgrPlay(sSoundAsset[horn], 2, NULL, NULL, pEnt, &volume, NULL);
        }
    }
}

// No GC symbol: inlined into TakeDamage and the collision code. The volume copies and -1.0f tests
// are probably an inlined sound helper (as in SoundPlayHorn).
void zCar::SoundPlayCollision(eCollisionType type, F32 strength)
{
    xRumble::effect* rumble = NULL;

    if (type == eCollisionTypeDamage)
    {
        // PATTY_HIT_DAMAGE_SG
        F32 volume = strength;
        if (volume == -1.0f)
        {
            xSndMgrPlay(sSoundAsset[19], 2, NULL, NULL, pEnt, NULL, NULL);
        }
        else
        {
            xSndMgrPlay(sSoundAsset[19], 2, NULL, NULL, pEnt, &volume, NULL);
        }
        rumble = mpRumbleDamage;
    }
    else if (type == eCollisionTypeJumpLand)
    {
        // JUMP_LAND_SG
        F32 volume = strength;
        if (volume == -1.0f)
        {
            xSndMgrPlay(sSoundAsset[15], 2, NULL, NULL, pEnt, NULL, NULL);
        }
        else
        {
            xSndMgrPlay(sSoundAsset[15], 2, NULL, NULL, pEnt, &volume, NULL);
        }
        rumble = mpRumbleCollision;
    }
    else if (type == eCollisionTypeSideKnock || type == eCollisionTypeHeadOn)
    {
        // PATTY_HIT_SG
        F32 volume = 1.0f;
        xSndMgrPlay(sSoundAsset[18], 2, NULL, NULL, pEnt, &volume, NULL);
        rumble = mpRumbleCollision;
    }

    if (rumble != NULL)
    {
        rumble->SetIntensity(strength);
        zEntEvent(rumble, eEventStartRumbleEffect, globals.currentActivePad, 0.0f, 0.0f, 0.0f);
    }
}

void zCar::SoundUpdate()
{
    eDamage carDamage = damageLevel;

    if (!engineRunning)
    {
        if (soundEngineHandle != (iSndHandle)-1)
        {
            xSndMgrStop(soundEngineHandle);
            soundEngineHandle = (iSndHandle)-1;

            if (playShutDownSound)
            {
                // ENGINE_STOP_SG
                xSndMgrPlay(sSoundAsset[8], 2, NULL, NULL, pEnt, NULL, NULL);
            }
            else if (boostTimeRemaining > 0.0f)
            {
                xSndMgrStop(soundBoostStartHandle);
                soundBoostStartHandle = (iSndHandle)-1;
                xSndMgrStop(soundBoostLoopHandle);
                soundBoostLoopHandle = (iSndHandle)-1;
                boostTimeRemaining = 0.0f;
            }
        }
    }
    else if (soundCurrentEngineType != carDamage || soundEngineHandle == (iSndHandle)-1)
    {
        U8 wasPlaying = soundEngineHandle != (iSndHandle)-1;

        soundCurrentEngineType = carDamage;
        xSndMgrStop(soundEngineHandle);
        soundEngineHandle = (iSndHandle)-1;

        switch (carDamage)
        {
        case eDamageNone:
            // ENGINE_LOOP_SG
            soundEngineHandle = xSndMgrPlay(sSoundAsset[1], 2, NULL, NULL, pEnt, NULL, NULL);
            break;
        case eDamage1:
            // ENGINE_DAMAGE1_LOOP_SG
            soundEngineHandle = xSndMgrPlay(sSoundAsset[2], 2, NULL, NULL, pEnt, NULL, NULL);
            break;
        case eDamage2:
            // ENGINE_DAMAGE2_LOOP_SG
            soundEngineHandle = xSndMgrPlay(sSoundAsset[3], 2, NULL, NULL, pEnt, NULL, NULL);
            break;
        }

        if (!wasPlaying && soundEngineHandle != (iSndHandle)-1)
        {
            // IGNITION_SG
            xSndMgrPlay(sSoundAsset[7], 2, NULL, NULL, pEnt, NULL, NULL);
        }
    }

    if (engineRunning)
    {
        // BACKFIRE_SG
        if (xrand_GenRandInt32() % 200 == 0 && carDamage == eDamage1)
        {
            xSndMgrPlay(sSoundAsset[9], 2, NULL, NULL, pEnt, NULL, NULL);
        }
        if (xrand_GenRandInt32() % 100 == 0 && carDamage == eDamage2)
        {
            xSndMgrPlay(sSoundAsset[9], 2, NULL, NULL, pEnt, NULL, NULL);
        }
    }

    if (soundEngineHandle != (iSndHandle)-1)
    {
        F32 maxFreq = 0.0f;

        switch (damageLevel)
        {
        case eDamageNone:
            maxFreq = 15.0f;
            break;
        case eDamage1:
            maxFreq = 12.0f;
            break;
        case eDamage2:
            maxFreq = 9.0f;
            break;
        }

        // not in the DWARF: the forward speed is tested and used by both branches.
        F32 speed = xVec3Dot(&mat.at, &velocity);
        if (speed >= 0.0f)
        {
            F32 freq = speed / 25.0f;
            freq = CLAMP(freq, 0.0f, 1.0f);
            freq *= maxFreq - 1.0f;
            freq += 1.0f;
            soundEnginePitch = freq;
        }
        else
        {
            F32 freq = -speed / 13.0f;
            freq = CLAMP(freq, 0.0f, 1.0f);
            freq *= maxFreq - 1.0f;
            freq += 1.0f;
            soundEnginePitch = freq;
        }

        xSndMgrSetPitch(soundEngineHandle, soundEnginePitch);
    }

    if (animReversing && soundReverseBeepHandle == (iSndHandle)-1)
    {
        if (soundReverseActivationTime == 0xFFFFFFFF)
        {
            soundReverseActivationTime = iTimeGet() + 1000;
        }

        if (iTimeGet() > soundReverseActivationTime)
        {
            // REVERSE_BEEP_SG
            soundReverseBeepHandle = xSndMgrPlay(sSoundAsset[10], 2, NULL, NULL, pEnt, NULL, NULL);
            carOneLiners.PlaySound(eCarOneLinerStartReverse);
        }
    }

    if (!animReversing)
    {
        soundReverseActivationTime = 0xFFFFFFFF;
        if (soundReverseBeepHandle != (iSndHandle)-1)
        {
            xSndMgrStop(soundReverseBeepHandle);
            soundReverseBeepHandle = (iSndHandle)-1;
        }
    }

    if (!engineRunning)
    {
        xSndMgrStop(soundDriveLoopHandle);
        soundDriveLoopHandle = (iSndHandle)-1;
        xSndMgrStop(soundSkidLoopHandle);
        soundSkidLoopHandle = (iSndHandle)-1;
    }
    else if (pSoundCurrentSurface != pCurrentSurface)
    {
        xSndMgrStop(soundDriveLoopHandle);
        soundDriveLoopHandle = (iSndHandle)-1;
        xSndMgrStop(soundSkidLoopHandle);
        soundSkidLoopHandle = (iSndHandle)-1;

        pSoundCurrentSurface = pCurrentSurface;
        soundDriveLoopHandle =
            xSndMgrPlay(pSoundCurrentSurface->soundGroupDrive, 2, NULL, NULL, pEnt, NULL, NULL);
        soundSkidLoopHandle =
            xSndMgrPlay(pSoundCurrentSurface->soundGroupSkid, 2, NULL, NULL, pEnt, NULL, NULL);
    }

    if (inGroundContact)
    {
        xSndMgrSetVolume(soundDriveLoopHandle, MIN(xVec3Length(&velocity), 6.0f) / 6.0f);
        xSndMgrSetVolume(soundSkidLoopHandle, skidFXIntensity);
    }
    else
    {
        xSndMgrSetVolume(soundDriveLoopHandle, 0.0f);
        xSndMgrSetVolume(soundSkidLoopHandle, 0.0f);
    }
}
