#ifndef ZCAR_H
#define ZCAR_H

#include "types.h"
#include "xAnim.h"
#include "xBase.h"
#include "xEnt.h"
#include "zEnt.h"
#include "xModel.h"
#include "iSnd.h"
#include "xParEmitter.h"
#include "xModelBlur.h"
#include "xCounter.h"
#include "xRumbleEmitter.h"
#include "zStreamedSound.h"
#include "zEntPlayerCar.h"
#include "zGlobals.h"

// Remember:
// U8 is commonly a bool

enum eDamage
{
    eDamageNone,
    eDamage1,
    eDamage2,
    eDamage3,
    eDamageMax
};

enum zDrivingSurf
{
    ezDRIVINGSURF_Default,
    ezDRIVINGSURF_Tarmac,
    ezDRIVINGSURF_Cement,
    ezDRIVINGSURF_HardDirt,
    ezDRIVINGSURF_Railroad,
    ezDRIVINGSURF_Grass,
    ezDRIVINGSURF_SoftDirt,
    ezDRIVINGSURF_SideWalk,
    ezDRIVINGSURF_Sand,
    ezDRIVINGSURF_SurfaceWater,
    ezDRIVINGSURF_ThickOil,
    ezDRIVINGSURF_HotSauce,
    ezDRIVINGSURF_NumNormalDrivableSurfaces,
    ezDRIVINGSURF_Wall,
    ezDRIVINGSURF_SlideTubWood,
    ezDRIVINGSURF_SlideTubSand,
    ezDRIVINGSURF_SlideTubHardRock,
    ezDRIVINGSURF_SlideShellHardRock,
    ezDRIVINGSURF_SlideShellTongue,
    ezDRIVINGSURF_SlideCrownRubberHose,
    ezDRIVINGSURF_SlideCrownRubberCable,
    ezDRIVINGSURF_SlideCrownMetalLamp,
    ezDRIVINGSURF_MaxTypes
};

enum eCarOneLiner
{
    eCarOneLinerDestroyCrate,
    eCarOneLinerStartJump,
    eCarOneLinerStartBoost,
    eCarOneLinerStartReverse,
    eCarOneLinerDetectedSlipperySurface,
    eCarOneLinerTakeDamage,
    eCarOneLinerGetHealth,
    eCarOneLinerNumSounds
};

enum animState
{
    eANIMSTATE_Idle,
    eANIMSTATE_Drive,
    eANIMSTATE_DriveSlippy,
    eANIMSTATE_Boosting,
    eANIMSTATE_Reversing,
    eANIMSTATE_Jump,
    eANIMSTATE_JumpLandSquash,
    eANIMSTATE_Crash,
    eANIMSTATE_SpinLeft,
    eANIMSTATE_SpinRight,
    eANIMSTATE_Success,
    eANIMSTATE_Failed,
    eANIMSTATE_Death
};

struct driveSurfaceDescriptor
{
    F32 staticTraction;
    F32 slidingTraction;
    F32 slideThreshold;
    U8 useSlideAnimation;
    F32 topSpeedFactor;
    iSndGroupHandle soundGroupDrive;
    iSndGroupHandle soundGroupSkid;
};

struct zCar
{
    // Nested in zCar, as in the PS2 symbol of SoundPlayCollision
    enum eCollisionType
    {
        eCollisionTypeHeadOn,
        eCollisionTypeSideKnock,
        eCollisionTypeJumpLand,
        eCollisionTypeDamage
    };

    xEnt* pEnt;
    xMat4x3 lastMat;
    xMat4x3 mat;
    F32 boundYShift;
    F32 boundRadius;
    F32 shockHalfRange;
    U8 engineRunning;
    U8 playShutDownSound;
    iSndHandle soundBoostStartHandle;
    F32 modelYaw;
    xVec3 velocity;
    // bool: SphereCollision takes its address as a bool*
    bool inGroundContact;
    F32 timeOffGround;
    F32 shockExtension;
    F32 steeringPosition;
    U8 isSliding;
    U8 isInDamageSpin;
    F32 oilPenaltyTimer;
    driveSurfaceDescriptor* pCurrentSurface;
    zDrivingSurf currentSurfaceType;
    S32 numCollisions;
    xCollis collisions[12];
    U32 collisionUserFlags[12];
    xBound bound;
    U8 collisionTypes[22];
    S32 numNitros;
    F32 boostTimeRemaining;
    xModelBlur nitroBlur;
    animState currentAnimState;
    animState idealDrivingAnimState;
    U8 animReversing;
    U8 animBrakeLightsOn;
    S32 animPropellorAngle;
    F32 animLean;
    xVec3 animVirtualBunTopPos;
    xVec3 animVirtualBunTopVel;
    F32 animVirtualBunTopHeight;
    F32 animVirtualBunTopVerticalVel;
    F32 animJumpOpeness;
    U8 animCrash;
    U8 animStartSpinLeft;
    U8 animStartSpinRight;
    U8 animHeavyLanding;
    U8 animInJumpAnimation;
    U8 animSuccessMode;
    U8 animFailedMode;
    F32 skidFXIntensity;
    xVec3 lastPositionPar[4];
    xVec3 lastVelocityPar[4];
    xVec3 lastPositionSeed;
    xVec3 lastPositionKetchup;
    xVec3 lastVelocityKetchup;
    xVec3 lastPositionMustard;
    xVec3 lastVelocityMustard;
    eDamage damageLevel;
    F32 soundEnginePitch;
    iSndHandle soundEngineHandle;
    eDamage soundCurrentEngineType;
    iSndHandle soundBoostLoopHandle;
    S64 soundReverseActivationTime;
    iSndHandle soundReverseBeepHandle;
    driveSurfaceDescriptor* pSoundCurrentSurface;
    iSndHandle soundDriveLoopHandle;
    iSndHandle soundSkidLoopHandle;
    iSndHandle soundSkidLoopHandles[22];
    U8 soundGearCrunchReady;
    _xCounter* nitroUsedCounter;
    U8 oilOneLinerPlayed;
    zStreamedSoundList carOneLiners;
    xRumble::effect* mpRumbleCollision;
    xRumble::effect* mpRumbleDamage;
    xRumble::effect* mpRumbleBoostStart;
    xRumble::effect* mpRumbleBoost;

    static iSndGroupHandle sSoundAsset[33];
    static zStreamedSound carOneLinersData[eCarOneLinerNumSounds + 1];

    void SoundUpdate();
    void SoundPlayCollision(eCollisionType type, F32 strength);
    void SoundPlayHorn();
    static void SoundInitAssetTable();
    void ModelSwap(eDamage newDamageLevel, bool swapEffects);
    void StreakFX(xEnt* ent, F32 dt);
    void DriveSurfaceFXInternal(const xModelInstance* pModel, F32 dt, zDrivingSurf collisionType);
    void DriveSurfaceFX(const xModelInstance* pModel, F32 dt);
    void RenderBrakeLights(const xModelInstance* pModel);
    void UpdatePropellor(xModelInstance* pModel);
    void UpdateEffects(F32 dt);
    void UpdateVirtualBunTop(F32 dt);
    static xAnimTable* CreateAnimTable();
    static U32 JumpEndedCB(xAnimTransition*, xAnimSingle*, void*);
    static U32 JumpStartedCB(xAnimTransition*, xAnimSingle*, void*);
    static U32 AnimDefaultCB(xAnimTransition*, xAnimSingle* anim, void*);
    static U32 DamageSpinCompleteCB(xAnimTransition*, xAnimSingle* anim, void*);
    static U32 DeathCheck(xAnimTransition*, xAnimSingle*, void*);
    static U32 FailedCheck(xAnimTransition*, xAnimSingle*, void*);
    static U32 SuccessCheck(xAnimTransition*, xAnimSingle*, void*);
    static U32 SpinRightCheck(xAnimTransition*, xAnimSingle*, void*);
    static U32 SpinLeftCheck(xAnimTransition*, xAnimSingle*, void*);
    static U32 CrashCheck(xAnimTransition*, xAnimSingle*, void*);
    static U32 JumpEndCheck(xAnimTransition*, xAnimSingle*, void*);
    static U32 JumpCheck(xAnimTransition*, xAnimSingle*, void*);
    static U32 ReverseCheck(xAnimTransition*, xAnimSingle*, void*);
    static U32 StartBoostingCheck(xAnimTransition*, xAnimSingle*, void*);
    static U32 StartDrivingSlippyCheck(xAnimTransition*, xAnimSingle*, void*);
    static U32 StartDrivingCheck(xAnimTransition*, xAnimSingle*, void*);
    static U32 IdleCheck(xAnimTransition*, xAnimSingle*, void*);
    void UpdateAnimState(xEnt* pEnt);
    void ApplyCollisionRebounds(xVec3* pVelocity);
    // The unnamed F32 is in the PS2 symbol: CarUpdate passes boundRadius, the body never reads it.
    void DepenetrateCollisionList(xVec3* pPosition, F32);
    // Static: the PS2 DWARF gives it no 'this'
    static void PreventPoppingThroughWorld(const xVec3* pStartPosition, xVec3* pEndPosition,
                                           xVec3* pVelocity);
    void SphereCollision(const xVec3* pNewCarPosition, const xVec3* pVelocity,
                         bool* pInGroundContact, F32* pShockExtension, xVec3* pGroundNormal);
    bool operator()(xEnt& ent);
    void UpdateGroundCharacteristics();
    void CalculateIdealOrientation(F32 yaw, xMat3x3* pIdealOrientation, xVec3* pGroundNormal);
    void CarUpdate(F32 dt, F32 steer, bool accelOn, bool brakeOn, bool boostOn);
    void StartBoost();
    U32* GetVarNitros() const;
    U8 CanPickupNitros() const;
    void AddNitros(S32 nitros);
    xMat4x3* GetMatrix();
    void SetMatrix(xMat4x3* pSource);
    // Remaining hit points (0 when the car is wrecked); unsigned, as retail reads it
    U32 GetHealth() const
    {
        return eDamage3 - damageLevel;
    }
    // No symbol on GC or PS2 (always inlined); name unknown. True once the car is wrecked or the
    // level's success/failure animation has started: driving input is ignored from then on.
    bool IsDisabled() const
    {
        return damageLevel == eDamage3 || animSuccessMode || animFailedMode;
    }
    // No symbol on GC or PS2 (always inlined); name unknown. The car's speed, negative when it
    // moves backwards.
    F32 GetSignedSpeed()
    {
        F32 forwardSpeed = xVec3Dot(&mat.at, &velocity);
        F32 speed = xVec3Length(&velocity);
        if (forwardSpeed < 0.0f)
        {
            speed *= -1.0f;
        }
        return speed;
    }
    // No symbol on GC or PS2 (always inlined); name unknown. Ends the nitro boost: the
    // counterpart of StartBoost's sounds and rumble.
    void StopBoost()
    {
        xSndMgrStop(soundBoostStartHandle);
        soundBoostStartHandle = (iSndHandle)-1;
        xSndMgrPlay(sSoundAsset[14], 2, NULL, NULL, pEnt, NULL, NULL);
        xSndMgrStop(soundBoostLoopHandle);
        soundBoostLoopHandle = (iSndHandle)-1;

        if (mpRumbleBoost != NULL)
        {
            zEntEvent(mpRumbleBoost, eEventStopRumbleEffect, globals.currentActivePad, 0.0f, 0.0f,
                      0.0f);
        }
    }
    // No symbol on GC or PS2 (always inlined; the Xbox build has both). The collision sphere's
    // center lies boundYShift along the car's up axis from the matrix origin. GetCenter is
    // Ratatouille's name; GetPositionFromCenter's is invented.
    void GetCenter(xVec3* pOutput)
    {
        *pOutput = mat.pos + mat.up * boundYShift;
    }
    void GetPositionFromCenter(xVec3* pOutput, const xVec3* pCenter)
    {
        *pOutput = *pCenter + mat.up * -boundYShift;
    }
    void GiveHealth(S32 health);
    void SetFullHealth();
    void TakeDamage(bool spinClockwise);
    void HandleEvent(xBase* from, xBase* to, U32 toEvent, const F32* toParam, xBase* toParamWidget,
                     U32 UNUSEDtoParamWidgetID);
    void Exit();
    void Reset(xEnt* pCarEnt, F32 initialYaw, xVec3* pPosition);
    void Initialize(xEnt* pCarEnt);
    void SceneInit();
};

extern S32 MAX_NITROS;
extern F32 BOOST_DURATION;

zCar* zEntPlayerCarGetCar();

#ifdef PLATFORM_PC
// PLATFORM_PC: the share of a 60 fps step this frame's car update is (iTimeFrameShare), set by
// zEntPlayerCarUpdate before its steps; 1 at 60 fps and below.
void zCarSetStepShare(F32 share);
F32 zCarGetStepShare();
#endif

#endif
