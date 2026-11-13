#include <string.h>

#include "xAnim.h"

#include "zEntCruiseBubble.h"
#include "zEntPlayer.h"
#include "zEntPlayerBungeeState.h"

// Included from zEntPlayer.cpp (after CheckObjectAgainstGauntlet, as in retail); the player's
// (file-static) transition callbacks the tables reference are declared here because most of them
// are defined further down in zEntPlayer.cpp.
static U32 TrackFallCheck(xAnimTransition*, xAnimSingle*, void*);
static U32 TrackFallCB(xAnimTransition*, xAnimSingle*, void*);
static U32 TrackPrefallJumpCheck(xAnimTransition*, xAnimSingle*, void*);
static U32 JumpCheck(xAnimTransition*, xAnimSingle*, void*);
static U32 JumpCB(xAnimTransition*, xAnimSingle*, void*);
static U32 DblJumpCheck(xAnimTransition*, xAnimSingle*, void*);
static U32 DblJumpCB(xAnimTransition*, xAnimSingle*, void*);
static U32 LandTrackCheck(xAnimTransition*, xAnimSingle*, void*);
static U32 SlideTrackCB(xAnimTransition*, xAnimSingle*, void*);
static U32 SlideDecelerateCheck(xAnimTransition*, xAnimSingle*, void*);
static U32 SlideDecelerateCB(xAnimTransition*, xAnimSingle*, void*);
static U32 SlideAccelerateCheck(xAnimTransition*, xAnimSingle*, void*);
static U32 SlideAccelerateCB(xAnimTransition*, xAnimSingle*, void*);
static U32 SlideCruisingCheck(xAnimTransition*, xAnimSingle*, void*);
static U32 SlideCruisingCB(xAnimTransition*, xAnimSingle*, void*);
static U32 HitCheck(xAnimTransition*, xAnimSingle*, void*);
static U32 HitCB(xAnimTransition*, xAnimSingle*, void*);
static U32 IdleCB(xAnimTransition*, xAnimSingle*, void*);
static U32 DefeatedCheck(xAnimTransition*, xAnimSingle*, void*);
static U32 DefeatedCB(xAnimTransition*, xAnimSingle*, void*);
static U32 GooCheck(xAnimTransition*, xAnimSingle*, void*);
static U32 GooDeathCB(xAnimTransition*, xAnimSingle*, void*);
static U32 BoulderRollMoveCheck(xAnimTransition*, xAnimSingle*, void*);
static U32 BoulderRollIdleCheck(xAnimTransition*, xAnimSingle*, void*);
static U32 LassoSwingGroundedBeginCheck(xAnimTransition*, xAnimSingle*, void*);
static U32 LassoSwingGroundedBeginCB(xAnimTransition*, xAnimSingle*, void*);
static U32 LassoSwingBeginCheck(xAnimTransition*, xAnimSingle*, void*);
static U32 LassoSwingBeginCB(xAnimTransition*, xAnimSingle*, void*);
static U32 LassoSwingTossCB(xAnimTransition*, xAnimSingle*, void*);
static U32 LassoSwingCB(xAnimTransition*, xAnimSingle*, void*);
static U32 IdleSlipCheck(xAnimTransition*, xAnimSingle*, void*);
static U32 LassoSwingGroundedCB(xAnimTransition*, xAnimSingle*, void*);
static U32 IdleCheck(xAnimTransition*, xAnimSingle*, void*);
static U32 WalkCheck(xAnimTransition*, xAnimSingle*, void*);
static U32 RunAnyCheck(xAnimTransition*, xAnimSingle*, void*);
static U32 RunOutOfWorldCheck(xAnimTransition*, xAnimSingle*, void*);
static U32 RunSlipCheck(xAnimTransition*, xAnimSingle*, void*);
static U32 LassoSwingReleaseCheck(xAnimTransition*, xAnimSingle*, void*);
static U32 LassoSwingReleaseCB(xAnimTransition*, xAnimSingle*, void*);
static U32 PatCartwheelCheck(xAnimTransition*, xAnimSingle*, void*);
static U32 PatCartwheelCB(xAnimTransition*, xAnimSingle*, void*);
static U32 PatCartwheelNoCartwheelCheck(xAnimTransition*, xAnimSingle*, void*);
static U32 PatNoCartwheelCB(xAnimTransition*, xAnimSingle*, void*);
static U32 PatCartwheelLoopCB(xAnimTransition*, xAnimSingle*, void*);
static U32 SpatulaGrabCheck(xAnimTransition*, xAnimSingle*, void*);
static U32 SpatulaGrabCB(xAnimTransition*, xAnimSingle*, void*);
static U32 SpatulaGrabStopCB(xAnimTransition*, xAnimSingle*, void*);
static U32 AnyMoveCheck(xAnimTransition*, xAnimSingle*, void*);
static U32 AnyStopCheck(xAnimTransition*, xAnimSingle*, void*);
static U32 PatrickGrabThrowStart(xAnimTransition*, xAnimSingle*, void*);
static U32 PatrickGrabThrowStartCB(xAnimTransition*, xAnimSingle*, void*);
static U32 PatrickGrabCheck(xAnimTransition*, xAnimSingle*, void*);
static U32 PatrickGrabCB(xAnimTransition*, xAnimSingle*, void*);
static U32 PatrickGrabFailed(xAnimTransition*, xAnimSingle*, void*);
static U32 PatrickGrabKill(xAnimTransition*, xAnimSingle*, void*);
static U32 PatrickHammerCheck(xAnimTransition*, xAnimSingle*, void*);
static U32 PatrickHammerCB(xAnimTransition*, xAnimSingle*, void*);
static U32 PatrickHammerSpinCB(xAnimTransition*, xAnimSingle*, void*);
static U32 PatrickHammerThrowCheck(xAnimTransition*, xAnimSingle*, void*);
static U32 PatrickHammerThrowCB(xAnimTransition*, xAnimSingle*, void*);
static U32 PatrickAttackCheckMelee1(xAnimTransition*, xAnimSingle*, void*);
static U32 PatrickMeleeCB(xAnimTransition*, xAnimSingle*, void*);
static U32 PatrickAttackCheckMelee2(xAnimTransition*, xAnimSingle*, void*);
static U32 WalkStoicCheck(xAnimTransition*, xAnimSingle*, void*);
static U32 RunStoicCheck(xAnimTransition*, xAnimSingle*, void*);
static U32 BounceCheck(xAnimTransition*, xAnimSingle*, void*);
static U32 BounceCB(xAnimTransition*, xAnimSingle*, void*);
static U32 JumpApexCheck(xAnimTransition*, xAnimSingle*, void*);
static U32 IdleStoicCheck(xAnimTransition*, xAnimSingle*, void*);
static U32 LandCheck(xAnimTransition*, xAnimSingle*, void*);
static U32 LandSlipRunCheck(xAnimTransition*, xAnimSingle*, void*);
static U32 LandSlipIdleCheck(xAnimTransition*, xAnimSingle*, void*);
static U32 FallCheck(xAnimTransition*, xAnimSingle*, void*);
static U32 PatrickStunCheckButtslam(xAnimTransition*, xAnimSingle*, void*);
static U32 PatrickStunCheckHeadbutt(xAnimTransition*, xAnimSingle*, void*);
static U32 StunStartFallCB(xAnimTransition*, xAnimSingle*, void*);
static U32 StunRadiusCB(xAnimTransition*, xAnimSingle*, void*);
static U32 LedgeGrabCheck(xAnimTransition*, xAnimSingle*, void*);
static U32 LedgeGrabCB(xAnimTransition*, xAnimSingle*, void*);
static U32 LedgeFinishCB(xAnimTransition*, xAnimSingle*, void*);
static U32 SlideTrackCheck(xAnimTransition*, xAnimSingle*, void*);
static U32 NoslideTrackCheck(xAnimTransition*, xAnimSingle*, void*);
static U32 NoslideTrackCB(xAnimTransition*, xAnimSingle*, void*);
static U32 InactiveCheck(xAnimTransition*, xAnimSingle*, void*);
static U32 InactiveCB(xAnimTransition*, xAnimSingle*, void*);
static U32 Defeated01Check(xAnimTransition*, xAnimSingle*, void*);
static U32 Defeated02Check(xAnimTransition*, xAnimSingle*, void*);
static U32 NoSlipCB(xAnimTransition*, xAnimSingle*, void*);
static U32 SlipRunCB(xAnimTransition*, xAnimSingle*, void*);
static U32 BubbleBashCheckPunch(xAnimTransition*, xAnimSingle*, void*);
static U32 BubbleBashCB(xAnimTransition*, xAnimSingle*, void*);
static U32 BubbleBashCheckGauntlet(xAnimTransition*, xAnimSingle*, void*);
static U32 Hit01Check(xAnimTransition*, xAnimSingle*, void*);
static U32 Hit01CB(xAnimTransition*, xAnimSingle*, void*);
static U32 Hit02Check(xAnimTransition*, xAnimSingle*, void*);
static U32 Hit02CB(xAnimTransition*, xAnimSingle*, void*);
static U32 Hit03Check(xAnimTransition*, xAnimSingle*, void*);
static U32 Hit03CB(xAnimTransition*, xAnimSingle*, void*);
static U32 Hit04Check(xAnimTransition*, xAnimSingle*, void*);
static U32 Hit04CB(xAnimTransition*, xAnimSingle*, void*);
static U32 Hit05Check(xAnimTransition*, xAnimSingle*, void*);
static U32 Hit05CB(xAnimTransition*, xAnimSingle*, void*);
static U32 Defeated03Check(xAnimTransition*, xAnimSingle*, void*);
static U32 LandNoTrackCheck(xAnimTransition*, xAnimSingle*, void*);
static U32 LandNoTrackFastCheck(xAnimTransition*, xAnimSingle*, void*);
static U32 LandNoTrackWalkCheck(xAnimTransition*, xAnimSingle*, void*);
static U32 LandNoTrackSlipRunCheck(xAnimTransition*, xAnimSingle*, void*);
static U32 LandNoTrackSlipIdleCheck(xAnimTransition*, xAnimSingle*, void*);
static U32 TongueDblJumpCB(xAnimTransition*, xAnimSingle*, void*);
static U32 TongueDblSpinCB(xAnimTransition*, xAnimSingle*, void*);
U32 BoulderRollCheck(xAnimTransition*, xAnimSingle*, void*); // global in zEntPlayer.cpp
static U32 BoulderRollWindupCB(xAnimTransition*, xAnimSingle*, void*);
static U32 BoulderRollCB(xAnimTransition*, xAnimSingle*, void*);
static U32 WallJumpLaunchCheck(xAnimTransition*, xAnimSingle*, void*);
static U32 WallJumpLaunchCallback(xAnimTransition*, xAnimSingle*, void*);
static U32 WallJumpCallback(xAnimTransition*, xAnimSingle*, void*);
static U32 WallJumpFlightLandCheck(xAnimTransition*, xAnimSingle*, void*);
static U32 WallJumpFlightLandCallback(xAnimTransition*, xAnimSingle*, void*);
static U32 WallJumpLandFlightCheck(xAnimTransition*, xAnimSingle*, void*);
static U32 WallJumpLandFlightCallback(xAnimTransition*, xAnimSingle*, void*);
static U32 WallJumpStickCB(xAnimTransition*, xAnimSingle*, void*);
static U32 WallJumpFallCheck(xAnimTransition*, xAnimSingle*, void*);
static U32 WallJumpFallCB(xAnimTransition*, xAnimSingle*, void*);
static U32 LandCallback(xAnimTransition*, xAnimSingle*, void*);
static U32 LandHighCheck(xAnimTransition*, xAnimSingle*, void*);
static U32 LandRunCheck(xAnimTransition*, xAnimSingle*, void*);
static U32 LandSlipRunCallback(xAnimTransition*, xAnimSingle*, void*);
static U32 BubbleSpinCheck1(xAnimTransition*, xAnimSingle*, void*);
static U32 BubbleSpinCB(xAnimTransition*, xAnimSingle*, void*);
static U32 BubbleSpinCheck2(xAnimTransition*, xAnimSingle*, void*);
static U32 BubbleBounceCheck(xAnimTransition*, xAnimSingle*, void*);
static U32 BubbleBounceCB(xAnimTransition*, xAnimSingle*, void*);
static U32 BBashStrikeCheck(xAnimTransition*, xAnimSingle*, void*);
static U32 BBashStrikeCB(xAnimTransition*, xAnimSingle*, void*);
static U32 BBounceAttackCB(xAnimTransition*, xAnimSingle*, void*);
static U32 BBounceStrikeCheck(xAnimTransition*, xAnimSingle*, void*);
static U32 BBounceStrikeCB(xAnimTransition*, xAnimSingle*, void*);
static U32 BBounceToJumpCheck(xAnimTransition*, xAnimSingle*, void*);
static U32 BBounceToJumpCB(xAnimTransition*, xAnimSingle*, void*);
static U32 BbowlCheck(xAnimTransition*, xAnimSingle*, void*);
static U32 BbowlCB(xAnimTransition*, xAnimSingle*, void*);
static U32 BbowlWindupEndCheck(xAnimTransition*, xAnimSingle*, void*);
static U32 BbowlTossEndCB(xAnimTransition*, xAnimSingle*, void*);
static U32 BbowlRecoverWalkCheck(xAnimTransition*, xAnimSingle*, void*);
static U32 BbowlRecoverRunCheck(xAnimTransition*, xAnimSingle*, void*);
static U32 BbowlRecoverRunOutOfWorldCheck(xAnimTransition*, xAnimSingle*, void*);
static U32 BbowlRecoverRunSlipCheck(xAnimTransition*, xAnimSingle*, void*);
static U32 BbashStart01EffectMissCB(U32, xAnimActiveEffect*, xAnimSingle*, void*);

xAnimTable* zPatrick_AnimTable()
{
    xAnimTransition* tran;
    xAnimTable* table;

    table = xAnimTableNew("Patrick", 0);

    xAnimTableNewState(table, "Idle01", 0x10, 1, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, "SlipIdle01", 0x10, 1, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, "Inactive01", 0x20, 1, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, "Inactive02", 0x20, 1, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, "Walk01", 0x10, 0x44, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, "Walk02", 0x10, 0x44, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, "Walk03", 0x10, 0x44, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, "Run01", 0x10, 0x46, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);

    xAnimTableNewState(table, "Run02", 0x10, 0x46, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, "Run03", 0x10, 0x46, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, "RunOutOfWorld01", 0x10, 0x46, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, "SlipRun01", 0x10, 0x46, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, "JumpStart01", 0x20, 0xa, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, "Fall01", 0x10, 0xa, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, "Land01", 0x20, 0, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, "BounceStart01", 0x20, 0xa, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, "BounceLift01", 0x10, 0xa, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, "BounceApex01", 0x20, 0xa, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, "DJumpStart01", 0x20, 0xa, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, "FallHigh01", 0x10, 0xa, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, "LandHigh01", 0x20, 0, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, "LedgeGrab01", 0x20, 0x80, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, "StunJump", 0x20, 0x400a, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, "StunJump02", 0x20, 0x400a, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, "StunFall", 0x10, 0x400a, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, "StunFall02", 0x10, 0x400a, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, "StunLand", 0x20, 0x80, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, "StunLand02", 0x20, 0x80, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, "Hit01", 0x20, 0x80, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, "Hit02", 0x20, 0x80, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, "Hit03", 0x20, 0x80, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);

    xAnimTableNewState(table, "Melee01", 0x20, 0x80a, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, "Melee02", 0x20, 0x80a, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, "Defeated01", 0, 0x480, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, "Defeated02", 0, 0x480, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, "DefeatedGoo01", 0, 0x480, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, "DefeatedProjectile01", 0, 0x480, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, "ButtSlide01", 0x10, 0x1840, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);

    xAnimTableNewState(table, "Talk04", 0x10, 1, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, "Talk03", 0x10, 1, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, "Talk02", 0x10, 1, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, "Talk01", 0x10, 1, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);

    xAnimTableNewState(table, "Carry_Pickup", 0x20, 0x80, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, "Carry_PickupFail", 0x20, 0x80, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, "Carry_Idle", 0x10, 0, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, "Carry_Walk", 0x10, 4, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, "Carry_Throw", 0x20, 0, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, "Carry_PickupItem", 0x10, 0, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, "Carry_IdleItem", 0x10, 0, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, "Carry_WalkItem", 0x10, 0, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, "Carry_ThrowItem", 0x10, 0, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, "Carry_HammerStart", 0x20, 0, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, "Carry_HammerSpin", 0x10, 0, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, "Carry_HammerThrow", 0x20, 0, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, "Carry_HammerStartItem", 0x20, 0, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, "Carry_HammerSpinItem", 0x10, 0, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);

    xAnimTableNewState(table, "SpatulaGrab01", 0x20, 0x80, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);

    xAnimTableNewState(table, "Cartwheel01", 0x40, 0xc, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);

    xAnimTableNewState(table, "LassoSwingCatch01", 0x20, 0, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, "LassoSwingCatch02", 0x20, 0, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, "LassoSwing", 0x10, 0x40, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, "LassoSwingRelease", 0x20, 0, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);

    xAnimTableNewTransition(table, "Idle01 SlipIdle01 Inactive01 Inactive02 Walk01 Run01 SlipRun01",
                            "LassoSwingCatch01", LassoSwingGroundedBeginCheck,
                            LassoSwingGroundedBeginCB, 0, 0, 0.0f, 0.0f, 1, 0, 0.15f, NULL);

    xAnimTableNewTransition(
        table,
        "JumpStart01 Fall01 DJumpStart01 ButtSlide01 BounceStart01 BounceLift01 BounceApex01",
        "LassoSwingCatch01", LassoSwingBeginCheck, LassoSwingBeginCB, 0, 0, 0.0f, 0.0f, 1, 0, 0.15f,
        NULL);
    xAnimTableNewTransition(table, "LassoSwingCatch01", "LassoSwingCatch02", NULL, LassoSwingTossCB,
                            0x10, 0, 0.0f, 0.0f, 0, 0, 0.15f, NULL);
    xAnimTableNewTransition(table, "LassoSwingCatch02", "LassoSwing", NULL, LassoSwingCB, 0x10, 0,
                            0.0f, 0.0f, 0, 0, 0.15f, NULL);
    xAnimTableNewTransition(table, "LassoSwingCatch01 LassoSwingCatch02 LassoSwing", "SlipIdle01",
                            IdleSlipCheck, LassoSwingGroundedCB, 0, 0, 0.0f, 0.0f, 1, 0, 0.15f,
                            NULL);
    xAnimTableNewTransition(table, "LassoSwingCatch01 LassoSwingCatch02 LassoSwing", "Idle01",
                            IdleCheck, LassoSwingGroundedCB, 0, 0, 0.0f, 0.0f, 1, 0, 0.15f, NULL);
    xAnimTableNewTransition(table, "LassoSwingCatch01 LassoSwingCatch02 LassoSwing", "Walk01",
                            WalkCheck, LassoSwingGroundedCB, 0, 0, 0.0f, 0.0f, 1, 0, 0.15f, NULL);
    xAnimTableNewTransition(table, "LassoSwingCatch01 LassoSwingCatch02 LassoSwing", "Run01",
                            RunAnyCheck, LassoSwingGroundedCB, 0, 0, 0.0f, 0.0f, 1, 0, 0.15f, NULL);
    xAnimTableNewTransition(table, "LassoSwingCatch01 LassoSwingCatch02 LassoSwing",
                            "RunOutOfWorld01", RunOutOfWorldCheck, LassoSwingGroundedCB, 0, 0, 0.0f,
                            0.0f, 1, 0, 0.15f, NULL);
    xAnimTableNewTransition(table, "LassoSwingCatch01 LassoSwingCatch02 LassoSwing", "SlipRun01",
                            RunSlipCheck, LassoSwingGroundedCB, 0, 0, 0.0f, 0.0f, 1, 0, 0.15f,
                            NULL);
    xAnimTableNewTransition(table, "LassoSwingCatch01 LassoSwingCatch02 LassoSwing", "Fall01",
                            LassoSwingReleaseCheck, LassoSwingReleaseCB, 0, 0, 0.0f, 0.0f, 1, 0,
                            0.15f, NULL);

    xAnimTableNewTransition(
        table,
        "Idle01 SlipIdle01 Inactive01 Inactive02 Walk01 Walk02 Walk03 Run01 Run02 Run03 SlipRun01 Land01",
        "Cartwheel01", PatCartwheelCheck, PatCartwheelCB, 0, 0, 0.0f, 0.0f, 0, 0, 0.15f, NULL);
    xAnimTableNewTransition(table, "Cartwheel01", "Idle01", PatCartwheelNoCartwheelCheck,
                            PatNoCartwheelCB, 0, 0, 0.0f, 0.0f, 1, 0, 0.25f, NULL);
    xAnimTableNewTransition(table, "Cartwheel01", "Cartwheel01", NULL, PatCartwheelLoopCB, 0x10, 0,
                            0.0f, 0.0f, 1, 0, 0.01f, NULL);

    xAnimTableNewTransition(
        table,
        "Idle01 SlipIdle01 Inactive01 Inactive02 Walk01 Walk02 Walk03 Run01 Run02 Run03 Land01 LandHigh01 Melee01 Melee02 Carry_Idle Carry_Walk Carry_IdleItem Carry_WalkItem",
        "SpatulaGrab01", SpatulaGrabCheck, SpatulaGrabCB, 0, 0, 0.0f, 0.0f, 5, 0, 0.15f, NULL);

    xAnimTableNewTransition(table, "SpatulaGrab01", "Idle01", NULL, SpatulaGrabStopCB, 0x10, 0,
                            0.0f, 0.0f, 1, 0, 0.15f, NULL);

    xAnimTableNewTransition(table, "Carry_Pickup", "Carry_Idle", NULL, NULL, 0x10, 0, 0.0f, 0.0f, 0,
                            0, 0.1f, NULL);
    xAnimTableNewTransition(table, "Carry_Throw", "Idle01", NULL, NULL, 0x10, 0, 0.0f, 0.0f, 0, 0,
                            0.1f, NULL);
    xAnimTableNewTransition(table, "Carry_PickupFail", "Idle01", NULL, NULL, 0x10, 0, 0.0f, 0.0f, 0,
                            0, 0.1f, NULL);
    xAnimTableNewTransition(table, "Carry_Idle", "Carry_Walk", AnyMoveCheck, NULL, 0, 0, 0.0f, 0.0f,
                            0, 0, 0.15f, NULL);
    xAnimTableNewTransition(table, "Carry_Walk", "Carry_Idle", AnyStopCheck, NULL, 0, 0, 0.0f, 0.0f,
                            0, 0, 0.15f, NULL);
    xAnimTableNewTransition(table, "Carry_Idle Carry_Walk", "Carry_Throw", PatrickGrabThrowStart,
                            PatrickGrabThrowStartCB, 0, 0, 0.0f, 0.0f, 0, 0, 0.15f, NULL);

    xAnimTableNewTransition(
        table,
        "Idle01 SlipIdle01 Inactive01 Inactive02 Land01 Walk01 Walk02 Walk03 Run01 Run02 Run03 SlipRun01",
        "Carry_Pickup", PatrickGrabCheck, PatrickGrabCB, 0, 0, 0.0f, 0.0f, 0, 0, 0.15f, NULL);

    xAnimTableNewTransition(
        table,
        "Idle01 SlipIdle01 Inactive01 Inactive02 Land01 Walk01 Walk02 Walk03 Run01 Run02 Run03",
        "Carry_PickupFail", PatrickGrabFailed, NULL, 0, 0, 0.0f, 0.0f, 0, 0, 0.15f, NULL);
    xAnimTableNewTransition(table, "Carry_Idle Carry_Walk Carry_Pickup Carry_HammerSpin", "Idle01",
                            PatrickGrabKill, NULL, 0, 0, 0.0f, 0.0f, 0, 0, 0.15f, NULL);
    xAnimTableNewTransition(table, "Carry_Throw", "Carry_HammerStart", PatrickHammerCheck,
                            PatrickHammerCB, 0, 0, 0.0f, 0.0f, 0, 0, 0.15f, NULL);
    xAnimTableNewTransition(table, "Carry_HammerStart", "Carry_HammerSpin", NULL,
                            PatrickHammerSpinCB, 0x10, 0, 0.0f, 0.0f, 0, 0, 0.1f, NULL);

    xAnimTableNewTransition(table, "Carry_HammerSpin", "Carry_HammerThrow", PatrickHammerThrowCheck,
                            PatrickHammerThrowCB, 0, 0, 0.0f, 0.0f, 0, 0, 0.15f, NULL);
    xAnimTableNewTransition(table, "Carry_HammerThrow", "Idle01", NULL, NULL, 0x10, 0, 0.0f, 0.0f,
                            0, 0, 0.1f, NULL);

    xAnimTableNewTransition(
        table,
        "Idle01 SlipIdle01 Inactive01 Inactive02 Walk01 Walk02 Walk03 Run01 Run02 Run03 SlipRun01 Cartwheel01 Land01",
        "Melee01", PatrickAttackCheckMelee1, PatrickMeleeCB, 0, 0, 0.0f, 0.0f, 0, 0, 0.05f, NULL);

    xAnimTableNewTransition(
        table,
        "Idle01 SlipIdle01 Inactive01 Inactive02 Walk01 Walk02 Walk03 Run01 Run02 Run03 SlipRun01 Cartwheel01 Land01",
        "Melee02", PatrickAttackCheckMelee2, PatrickMeleeCB, 0, 0, 0.0f, 0.0f, 0, 0, 0.05f, NULL);
    xAnimTableNewTransition(table, "Melee01 Melee02", "SlipIdle01", IdleSlipCheck, NULL, 0x10, 0,
                            0.0f, 0.0f, 1, 0, 0.0f, NULL);
    xAnimTableNewTransition(table, "Melee01 Melee02", "Idle01", IdleCheck, NULL, 0x10, 0, 0.0f,
                            0.0f, 1, 0, 0.0f, NULL);
    xAnimTableNewTransition(table, "Melee01 Melee02", "Walk01", WalkStoicCheck, NULL, 0x10, 0, 0.0f,
                            0.0f, 1, 0, 0.15f, NULL);

    xAnimTableNewTransition(table, "Melee01 Melee02", "Run01", RunStoicCheck, NULL, 0x10, 0, 0.0f,
                            0.0f, 1, 0, 0.15f, NULL);

    xAnimTableNewTransition(table, "Melee01 Melee02", "RunOutOfWorld01", RunOutOfWorldCheck, NULL,
                            0x10, 0, 0.0f, 0.0f, 1, 0, 0.15f, NULL);
    xAnimTableNewTransition(table, "Melee01 Melee02", "SlipRun01", RunSlipCheck, NULL, 0x10, 0,
                            0.0f, 0.0f, 1, 0, 0.15f, NULL);

    xAnimTableNewTransition(table, "Inactive01 Land01 LandHigh01", "Idle01", NULL, NULL, 0x10, 0,
                            0.0f, 0.0f, 0, 0, 0.1f, NULL);

    xAnimTableNewTransition(table, "JumpStart01", "Fall01", NULL, NULL, 0x10, 0, 0.0f, 0.0f, 0, 0,
                            0.0f, NULL);
    xAnimTableNewTransition(table, "DJumpStart01", "Fall01", NULL, NULL, 0x10, 0, 0.0f, 0.0f, 0, 0,
                            0.0f, NULL);

    xAnimTableNewTransition(
        table,
        "Idle01 SlipIdle01 Walk01 Walk02 Walk03 Run01 Run02 Run03 RunOutOfWorld01 SlipRun01 Land01 LandHigh01 JumpStart01 DJumpStart01 Fall01 StunJump StunJump02 StunFall StunLand",
        "BounceStart01", BounceCheck, BounceCB, 0, 0, 0.0f, 0.0f, 15, 0, 0.15f, NULL);

    xAnimTableNewTransition(table, "BounceStart01", "BounceLift01", NULL, NULL, 0x10, 0, 0.0f, 0.0f,
                            0, 0, 0.15f, NULL);
    xAnimTableNewTransition(table, "BounceLift01", "BounceApex01", JumpApexCheck, NULL, 0, 0, 0.0f,
                            0.0f, 1, 0, 0.15f, NULL);
    xAnimTableNewTransition(table, "BounceApex01", "Fall01", NULL, NULL, 0x10, 0, 0.0f, 0.0f, 0, 0,
                            0.15f, NULL);

    xAnimTableNewTransition(
        table, "Idle01 Walk01 Walk02 Walk03 Run01 Run02 Run03 RunOutOfWorld01 SlipRun01",
        "SlipIdle01", IdleSlipCheck, NULL, 0, 0, 0.0f, 0.0f, 1, 0, 0.15f, NULL);
    xAnimTableNewTransition(table, "SlipIdle01 Walk01 Run01 RunOutOfWorld01 SlipRun01", "Idle01",
                            IdleCheck, NULL, 0, 0, 0.0f, 0.0f, 1, 0, 0.15f, NULL);

    xAnimTableNewTransition(table, "Walk02 Run02", "Idle01", IdleStoicCheck, IdleCB, 0, 0, 0.0f,
                            0.0f, 1, 0, 0.15f, NULL);

    xAnimTableNewTransition(table, "Walk03 Run03", "Idle01", IdleStoicCheck, IdleCB, 0, 0, 0.0f,
                            0.0f, 1, 0, 0.15f, NULL);

    xAnimTableNewTransition(
        table,
        "Idle01 SlipIdle01 Inactive01 Inactive02 Run01 Run02 Run03 RunOutOfWorld01 SlipRun01 Land01 LandHigh01",
        "Walk01", WalkStoicCheck, NULL, 0, 0, 0.0f, 0.0f, 1, 0, 0.15f, NULL);

    xAnimTableNewTransition(
        table,
        "Idle01 SlipIdle01 Inactive01 Inactive02 Walk01 Walk02 Walk03 Land01 LandHigh01 Run02 Run03 RunOutOfWorld01 SlipRun01",
        "Run01", RunStoicCheck, NULL, 0, 0, 0.0f, 0.0f, 1, 0, 0.15f, NULL);

    xAnimTableNewTransition(
        table,
        "Idle01 SlipIdle01 Inactive01 Inactive02 Walk01 Walk02 Walk03 Land01 LandHigh01 Run01 Run02 Run03 SlipRun01",
        "RunOutOfWorld01", RunOutOfWorldCheck, NULL, 0, 0, 0.0f, 0.0f, 1, 0, 0.15f, NULL);
    xAnimTableNewTransition(
        table,
        "Idle01 SlipIdle01 Inactive01 Inactive02 Walk01 Walk02 Walk03 Land01 LandHigh01 Run01 Run02 Run03 RunOutOfWorld01",
        "SlipRun01", RunSlipCheck, NULL, 0, 0, 0.0f, 0.0f, 1, 0, 0.15f, NULL);

    xAnimTableNewTransition(
        table,
        "Idle01 SlipIdle01 Inactive01 Inactive02 Walk01 Walk02 Walk03 Run01 Run02 Run03 RunOutOfWorld01 SlipRun01 Land01 LandHigh01",
        "JumpStart01", JumpCheck, JumpCB, 0, 0, 0.0f, 0.0f, 10, 0, 0.15f, NULL);
    xAnimTableNewTransition(table, "Cartwheel01", "JumpStart01", JumpCheck, JumpCB, 0, 0, 0.0f,
                            0.0f, 10, 0, 0.25f, NULL);
    xAnimTableNewTransition(table,
                            "JumpStart01 Fall01 FallHigh01 BounceStart01 BounceLift01 BounceApex01",
                            "DJumpStart01", DblJumpCheck, DblJumpCB, 0, 0, 0.0f, 0.0f, 10, 0, 0.15f,
                            NULL);

    xAnimTableNewTransition(table, "BounceApex01 Fall01 FallHigh01 JumpStart01 DJumpStart01",
                            "Land01", LandCheck, NULL, 0, 0, 0.0f, 0.0f, 1, 0, 0.15f, NULL);
    xAnimTableNewTransition(table, "BounceApex01 Fall01 FallHigh01 JumpStart01 DJumpStart01",
                            "SlipRun01", LandSlipRunCheck, NULL, 0, 0, 0.0f, 0.0f, 2, 0, 0.15f,
                            NULL);
    xAnimTableNewTransition(table, "BounceApex01 Fall01 FallHigh01 JumpStart01 DJumpStart01",
                            "SlipIdle01", LandSlipIdleCheck, NULL, 0, 0, 0.0f, 0.0f, 2, 0, 0.15f,
                            NULL);

    xAnimTableNewTransition(
        table,
        "Idle01 SlipIdle01 Inactive01 Inactive02 Walk01 Walk02 Walk03 Run01 Run02 Run03 RunOutOfWorld01 SlipRun01 Land01 LandHigh01",
        "Fall01", FallCheck, NULL, 0, 0, 0.0f, 0.0f, 1, 0, 0.15f, NULL);

    xAnimTableNewTransition(
        table, "JumpStart01 Fall01 FallHigh01 DJumpStart01 BounceStart01 BounceLift01 BounceApex01",
        "StunJump", PatrickStunCheckButtslam, NULL, 0, 0, 0.0f, 0.0f, 10, 0, 0.125f, NULL);
    xAnimTableNewTransition(
        table, "JumpStart01 Fall01 FallHigh01 DJumpStart01 BounceStart01 BounceLift01 BounceApex01",
        "StunJump02", PatrickStunCheckHeadbutt, NULL, 0, 0, 0.0f, 0.0f, 10, 0, 0.125f, NULL);
    xAnimTableNewTransition(table, "StunJump", "StunFall", NULL, StunStartFallCB, 0x10, 0, 0.0f,
                            0.0f, 0, 0, 0.0f, NULL);
    xAnimTableNewTransition(table, "StunFall", "StunLand", LandCheck, StunRadiusCB, 0, 0, 0.0f,
                            0.0f, 1, 0, 0.1f, NULL);
    xAnimTableNewTransition(table, "StunJump02", "StunFall02", NULL, StunStartFallCB, 0x10, 0, 0.0f,
                            0.0f, 0, 0, 0.0f, NULL);
    xAnimTableNewTransition(table, "StunFall02", "StunLand02", LandCheck, StunRadiusCB, 0, 0, 0.0f,
                            0.0f, 1, 0, 0.1f, NULL);
    xAnimTableNewTransition(table, "StunLand StunLand02", "Idle01", NULL, NULL, 0x10, 0, 0.0f, 0.0f,
                            0, 0, 0.0f, NULL);

    xAnimTableNewTransition(table, "JumpStart01 Fall01 FallHigh01 DJumpStart01", "LedgeGrab01",
                            LedgeGrabCheck, LedgeGrabCB, 0, 0, 0.0f, 0.0f, 11, 0, 0.1f, NULL);
    xAnimTableNewTransition(table, "LedgeGrab01", "Idle01", NULL, LedgeFinishCB, 0x10, 0, 0.0f,
                            0.0f, 0, 0, 0.1f, NULL);

    xAnimTableNewTransition(
        table,
        "Idle01 SlipIdle01 Inactive01 Inactive02 Walk01 Walk02 Walk03 Run01 Run02 Run03 RunOutOfWorld01 SlipRun01 Fall01 FallHigh01 Land01 LandHigh01",
        "ButtSlide01", SlideTrackCheck, SlideTrackCB, 0, 0, 0.0f, 0.0f, 9, 0, 0.15f, NULL);

    xAnimTableNewTransition(table, "Carry_PickupFail Carry_Idle Carry_Walk Carry_Throw",
                            "ButtSlide01", SlideTrackCheck, SlideTrackCB, 0, 0, 0.0f, 0.0f, 9, 0,
                            0.15f, NULL);
    xAnimTableNewTransition(table, "ButtSlide01", "Idle01", NoslideTrackCheck, NoslideTrackCB, 0, 0,
                            0.0f, 0.0f, 9, 0, 0.15f, NULL);
    xAnimTableNewTransition(table, "ButtSlide01", "Fall01", TrackFallCheck, TrackFallCB, 0, 0, 0.0f,
                            0.0f, 9, 0, 0.15f, NULL);
    xAnimTableNewTransition(table, "ButtSlide01", "JumpStart01", JumpCheck, JumpCB, 0, 0, 0.0f,
                            0.0f, 10, 0, 0.15f, NULL);
    xAnimTableNewTransition(table, "JumpStart01 Fall01 FallHigh01", "JumpStart01",
                            TrackPrefallJumpCheck, JumpCB, 0, 0, 0.0f, 0.0f, 15, 0, 0.15f, NULL);

    xAnimTableNewTransition(table, "Idle01", "Inactive01", InactiveCheck, InactiveCB, 0, 0x20000,
                            0.0f, 0.0f, 1, 0, 0.15f, NULL);
    xAnimTableNewTransition(table, "Idle01", "Inactive02", InactiveCheck, InactiveCB, 0, 0x20001,
                            0.0f, 0.0f, 1, 0, 0.15f, NULL);

    xAnimTableNewTransition(table, "Inactive01 Inactive02 ", "Idle01", NULL, IdleCB, 0x10, 0, 0.0f,
                            0.0f, 0, 0, 0.15f, NULL);

    xAnimTableNewTransition(table, "Hit01 Hit02 Hit03", "Idle01", NULL, IdleCB, 0x10, 0, 0.0f, 0.0f,
                            0, 0, 0.15f, NULL);

    tran = xAnimTableNewTransition(
        table,
        "Idle01 SlipIdle01 Inactive01 Inactive02 Walk01 Walk02 Walk03 Run01 Run02 Run03 RunOutOfWorld01 SlipRun01",
        "Defeated01", Defeated01Check, DefeatedCB, 0, 2, 0.0f, 0.0f, 15, 0, 0.15f, NULL);

    xAnimTableAddTransition(
        table, tran,
        "JumpStart01 Fall01 Land01 DJumpStart01 FallHigh01 LandHigh01 LedgeGrab01 StunJump StunJump02");
    xAnimTableAddTransition(table, tran,
                            "StunFall StunLand Hit01 Hit02 Hit03 Melee01 Melee02 ButtSlide01");
    xAnimTableAddTransition(
        table, tran,
        "Carry_Pickup Carry_PickupFail Carry_Idle Carry_Walk Carry_Throw Carry_PickupItem");
    xAnimTableAddTransition(table, tran, "Carry_IdleItem Carry_WalkItem Carry_ThrowItem");
    xAnimTableAddTransition(
        table, tran,
        "Carry_HammerStart Carry_HammerStartItem Carry_HammerSpin Carry_HammerSpinItem");

    tran = xAnimTableNewTransition(
        table,
        "Idle01 SlipIdle01 Inactive01 Inactive02 Walk01 Walk02 Walk03 Run01 Run02 Run03 RunOutOfWorld01 SlipRun01",
        "Defeated02", Defeated02Check, DefeatedCB, 0, 2, 0.0f, 0.0f, 15, 0, 0.15f, NULL);

    xAnimTableAddTransition(
        table, tran,
        "JumpStart01 Fall01 Land01 DJumpStart01 FallHigh01 LandHigh01 LedgeGrab01 StunJump StunJump02");
    xAnimTableAddTransition(table, tran,
                            "StunFall StunLand Hit01 Hit02 Hit03 Melee01 Melee02 ButtSlide01");
    xAnimTableAddTransition(
        table, tran,
        "Carry_Pickup Carry_PickupFail Carry_Idle Carry_Walk Carry_Throw Carry_PickupItem");
    xAnimTableAddTransition(table, tran, "Carry_IdleItem Carry_WalkItem Carry_ThrowItem");
    xAnimTableAddTransition(
        table, tran,
        "Carry_HammerStart Carry_HammerStartItem Carry_HammerSpin Carry_HammerSpinItem");

    tran = xAnimTableNewTransition(
        table,
        "Idle01 SlipIdle01 Inactive01 Inactive02 Walk01 Walk02 Walk03 Run01 Run02 Run03 RunOutOfWorld01 SlipRun01 Cartwheel01",
        "DefeatedGoo01", GooCheck, GooDeathCB, 0, 0, 0.0f, 0.0f, 16, 0, 0.15f, NULL);

    xAnimTableAddTransition(
        table, tran,
        "JumpStart01 Fall01 Land01 DJumpStart01 FallHigh01 LandHigh01 LedgeGrab01 StunJump StunJump02");
    xAnimTableAddTransition(table, tran,
                            "StunFall StunLand Hit01 Hit02 Hit03 Melee01 Melee02 ButtSlide01");
    xAnimTableAddTransition(
        table, tran,
        "Carry_Pickup Carry_PickupFail Carry_Idle Carry_Walk Carry_Throw Carry_PickupItem");
    xAnimTableAddTransition(table, tran, "Carry_IdleItem Carry_WalkItem Carry_ThrowItem");
    xAnimTableAddTransition(
        table, tran,
        "Carry_HammerStart Carry_HammerStartItem Carry_HammerSpin Carry_HammerSpinItem");

    xAnimTableNewTransition(table, "Idle01 SlipIdle01 Inactive01 Inactive02 ", "Talk01",
                            zPlayer::TalkCheck, NULL, 0, 0, 0.0f, 0.0f, 20, 0, 0.15f, NULL);
    xAnimTableNewTransition(table, "Idle01 SlipIdle01 Inactive01 Inactive02 ", "Talk02",
                            zPlayer::TalkCheck, NULL, 0, 1, 0.0f, 0.0f, 20, 0, 0.15f, NULL);
    xAnimTableNewTransition(table, "Idle01 SlipIdle01 Inactive01 Inactive02 ", "Talk03",
                            zPlayer::TalkCheck, NULL, 0, 2, 0.0f, 0.0f, 20, 0, 0.15f, NULL);
    xAnimTableNewTransition(table, "Idle01 SlipIdle01 Inactive01 Inactive02 ", "Talk04",
                            zPlayer::TalkCheck, NULL, 0, 3, 0.0f, 0.0f, 20, 0, 0.15f, NULL);
    xAnimTableNewTransition(table, "Talk01", "Idle01", zPlayer::TalkDoneCheck, IdleCB, 0, 0, 0.0f,
                            0.0f, 20, 0, 0.15f, NULL);
    xAnimTableNewTransition(table, "Talk02", "Idle01", zPlayer::TalkDoneCheck, IdleCB, 0, 1, 0.0f,
                            0.0f, 20, 0, 0.15f, NULL);
    xAnimTableNewTransition(table, "Talk03", "Idle01", zPlayer::TalkDoneCheck, IdleCB, 0, 2, 0.0f,
                            0.0f, 20, 0, 0.15f, NULL);
    xAnimTableNewTransition(table, "Talk04", "Idle01", zPlayer::TalkDoneCheck, IdleCB, 0, 3, 0.0f,
                            0.0f, 20, 0, 0.15f, NULL);

    return table;
}

xAnimTable* zEntPlayer_AnimTable()
{
    xAnimTransition* tran;
    xAnimTable* table;

    table = xAnimTableNew("SB", 0);

    xAnimTableNewState(table, "Idle01", 0x10, 1, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, "SlipIdle01", 0x10, 1, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, "Inactive01", 0x20, 1, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, "Inactive02", 0x20, 1, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, "Walk01", 0x10, 0x44, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, "Run01", 0x10, 0x46, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, "Run02", 0x10, 0x46, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, "Run03", 0x10, 0x46, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, "RunOutOfWorld01", 0x10, 0x46, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, "SlipRun01", 0x10, 0x46, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, "JumpStart01", 0x20, 0xa, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, "JumpLift01", 0x10, 0xa, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, "JumpApex01", 0x20, 0xa, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, "Fall01", 0x10, 0xa, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, "Land01", 0x20, 0, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, "LandRun01", 0x20, 6, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, "BounceStart01", 0x20, 0xa, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, "BounceLift01", 0x10, 0xa, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, "BounceApex01", 0x20, 0xa, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, "DJumpStart01", 0x20, 0xa, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, "DJumpLift01", 0x10, 0xa, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, "FallHigh01", 0x20, 0, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, "LandHigh01", 0x20, 0, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);

    xAnimTableNewState(table, "Bspin01", 0x20, 0x80a, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, "Bspin02", 0x20, 0x80a, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);

    xAnimTableNewState(table, "BbashStart01", 0x20, 0xe, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, "BbashStart02", 0x20, 0xe, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, "BbashStrike01", 0x20, 0x4000, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, "BbashMiss01", 0x20, 0, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, "BbounceAttack01", 0x10, 0x4000, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, "BbounceStart01", 0x20, 0, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, "BbounceStrike01", 0x20, 0, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, "Bbowl01", 0x20, 0, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, "BbowlStart01", 0x20, 0, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, "BbowlWindup01", 0x10, 0, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, "BbowlToss01", 0x20, 0, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, "BbowlRecover01", 0x20, 0x80, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, "LedgeGrab01", 0x20, 0x80, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, "Hit01", 0x20, 0x80, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, "Hit02", 0x20, 0x80, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, "Hit03", 0x20, 0x80, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, "Hit04", 0x20, 0x80, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, "Hit05", 0x20, 0x80, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, "Defeated01", 0, 0x480, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, "Defeated02", 0, 0x480, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, "Defeated03", 0, 0x480, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, "Defeated04", 0, 0x480, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, "Defeated05", 0, 0x480, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, "TongueSlide01", 0x10, 0x1840, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);

    xAnimTableNewState(table, "TongueStart01", 0x20, 0xa, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, "TongueJump01", 0x20, 0xa, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, "TongueJumpXtra01", 0x20, 0xa, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, "TongueDJumpApex01", 0x20, 0xa, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, "TongueFall01", 0x10, 0xa, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, "TongueLand01", 0x20, 0x1800, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, "TongueTumble01", 0x20, 0x1800, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, "Goo01", 0x10, 0, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, "Goo02", 0x20, 0, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, "GooDefeated", 0, 0x480, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);

    xAnimTableNewState(table, "WallLaunch01", 0x20, 0x8a, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, "WallFlight01", 0x20, 0xa, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, "WallFlight02", 0x10, 0xa, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, "WallLand01", 0x20, 0xa, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, "WallFall01", 0x10, 0xa, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, "WallStick01", 0x10, 0xa, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);

    xAnimTableNewState(table, "BoulderRoll01", 0x20, 0xa, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, "BoulderRoll02", 0x10, 0xa, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);

    xAnimTableNewState(table, "Talk04", 0x10, 1, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, "Talk03", 0x10, 1, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, "Talk02", 0x10, 1, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, "Talk01", 0x10, 1, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);

    xAnimTableNewState(table, "SpatulaGrab01", 0x20, 0x80, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);

    xAnimTableNewTransition(
        table, "Idle01 SlipIdle01 Inactive01 Inactive02 Walk01 Run01 Run02 Run03 Land01 LandRun01",
        "SpatulaGrab01", SpatulaGrabCheck, SpatulaGrabCB, 0, 0, 0.0f, 0.0f, 2, 0, 0.15f, NULL);

    xAnimTableNewTransition(table, "SpatulaGrab01", "Idle01", NULL, SpatulaGrabStopCB, 0x10, 0,
                            0.0f, 0.0f, 1, 0, 0.15f, NULL);

    xAnimTableNewTransition(table, "Inactive01", "Idle01", NULL, IdleCB, 0x10, 0, 0.0f, 0.0f, 0, 0,
                            0.15f, NULL);
    xAnimTableNewTransition(table, "Inactive02", "Idle01", NULL, IdleCB, 0x10, 0, 0.0f, 0.0f, 0, 0,
                            0.15f, NULL);

    xAnimTableNewTransition(table, "Land01", "Idle01", NULL, IdleCB, 0x10, 0, 0.0f, 0.0f, 0, 0,
                            0.1f, NULL);
    xAnimTableNewTransition(table, "LandHigh01", "Idle01", NULL, IdleCB, 0x10, 0, 0.0f, 0.0f, 0, 0,
                            0.1f, NULL);

    xAnimTableNewTransition(table, "Idle01 Walk01 Run01 Run02 Run03 RunOutOfWorld01 SlipRun01",
                            "SlipIdle01", IdleSlipCheck, IdleCB, 0, 0, 0.0f, 0.0f, 1, 0, 0.15f,
                            NULL);
    xAnimTableNewTransition(table, "LandRun01", "SlipIdle01", IdleSlipCheck, IdleCB, 0, 0, 0.0f,
                            0.0f, 1, 0, 0.1f, NULL);

    xAnimTableNewTransition(table, "SlipIdle01", "Idle01", IdleCheck, IdleCB, 0, 0, 0.0f, 0.0f, 1,
                            0, 0.15f, NULL);
    xAnimTableNewTransition(table, "Walk01", "Idle01", IdleCheck, IdleCB, 0, 0, 0.0f, 0.0f, 1, 0,
                            0.15f, NULL);
    xAnimTableNewTransition(table, "Run01", "Idle01", IdleCheck, IdleCB, 0, 0, 0.0f, 0.0f, 1, 0,
                            0.15f, NULL);

    xAnimTableNewTransition(table, "LandRun01", "Idle01", IdleCheck, IdleCB, 0, 0, 0.0f, 0.0f, 1, 0,
                            0.1f, NULL);
    xAnimTableNewTransition(table, "SlipRun01", "Idle01", IdleCheck, IdleCB, 0, 0, 0.0f, 0.0f, 1, 0,
                            0.15f, NULL);

    xAnimTableNewTransition(table, "Idle01", "Inactive01", InactiveCheck, InactiveCB, 0, 0x20000,
                            0.0f, 0.0f, 1, 0, 0.15f, NULL);
    xAnimTableNewTransition(table, "Idle01", "Inactive02", InactiveCheck, InactiveCB, 0, 0x20001,
                            0.0f, 0.0f, 1, 0, 0.15f, NULL);

    static const char* const STANDARD_STATES[11] = { "Idle01",          "Walk01",     "Run01",
                                                     "RunOutOfWorld01", "SlipRun01",  "SlipIdle01",
                                                     "Land01",          "LandHigh01", "Inactive01",
                                                     "Inactive02",      NULL };
    xAnimTransition* stdtran[7];

    stdtran[0] = xAnimTableNewTransition(table, STANDARD_STATES[0], "Walk01", WalkCheck, NoSlipCB,
                                         0, 0, 0.0f, 0.0f, 1, 0, 0.15f, NULL);
    stdtran[1] = xAnimTableNewTransition(table, STANDARD_STATES[0], "Run01", RunStoicCheck,
                                         NoSlipCB, 0, 0, 0.0f, 0.0f, 1, 0, 0.15f, NULL);

    stdtran[2] = xAnimTableNewTransition(table, STANDARD_STATES[0], "SlipRun01", RunSlipCheck,
                                         SlipRunCB, 0, 0, 0.0f, 0.0f, 2, 0, 0.15f, NULL);
    stdtran[3] =
        xAnimTableNewTransition(table, STANDARD_STATES[0], "RunOutOfWorld01", RunOutOfWorldCheck,
                                NoSlipCB, 0, 0x200100, 0.0f, 0.0f, 2, 0, 0.15f, NULL);
    stdtran[4] = xAnimTableNewTransition(table, STANDARD_STATES[0], "Fall01", FallCheck, NoSlipCB,
                                         0, 0, 0.0f, 0.0f, 1, 0, 0.15f, NULL);
    stdtran[5] =
        xAnimTableNewTransition(table, STANDARD_STATES[0], "BbashStart01", BubbleBashCheckPunch,
                                BubbleBashCB, 0, 0, 0.0f, 0.0f, 10, 0, 0.1f, NULL);
    stdtran[6] =
        xAnimTableNewTransition(table, STANDARD_STATES[0], "BbashStart02", BubbleBashCheckGauntlet,
                                BubbleBashCB, 0, 0, 0.0f, 0.0f, 10, 0, 0.1f, NULL);

    for (S32 i = 1; STANDARD_STATES[i] != NULL; i++)
    {
        for (S32 j = 0; j < sizeof(stdtran) / sizeof(stdtran[0]); j++)
        {
            // j never reaches 7 here; the retail code still tests it.
            if (strcmp(STANDARD_STATES[i], stdtran[j]->Dest->Name) != 0 && (i != 5 || j != 7))
            {
                xAnimTableAddTransition(table, stdtran[j], STANDARD_STATES[i]);
            }
        }
    }

    xAnimTableAddTransition(table, stdtran[2], "LandRun01");
    xAnimTableAddTransition(table, stdtran[3], "LandRun01");
    xAnimTableAddTransition(table, stdtran[4], "LandRun01");
    xAnimTableAddTransition(table, stdtran[5], "LandRun01");
    xAnimTableAddTransition(table, stdtran[6], "LandRun01");

    xAnimTableNewTransition(table, "LandRun01", "Walk01", WalkCheck, NULL, 0x10, 0, 0.0f, 0.0f, 1,
                            0, 0.15f, NULL);
    xAnimTableNewTransition(table, "LandRun01", "Run01", RunStoicCheck, NULL, 0x10, 0, 0.0f, 0.0f,
                            1, 0, 0.1f, NULL);

    xAnimTableNewTransition(table, "LandRun01", "RunOutOfWorld01", RunOutOfWorldCheck, NULL, 0x10,
                            0x200100, 0.0f, 0.0f, 3, 0, 0.1f, NULL);
    xAnimTableNewTransition(table, "LandRun01", "SlipRun01", RunSlipCheck, SlipRunCB, 0x10, 0, 0.0f,
                            0.0f, 2, 0, 0.1f, NULL);

    tran = xAnimTableNewTransition(table, "Idle01", "JumpStart01", JumpCheck, JumpCB, 0, 0, 0.0f,
                                   0.0f, 10, 0, 0.15f, NULL);
    xAnimTableAddTransition(table, tran, "Walk01");
    xAnimTableAddTransition(table, tran, "Run01");

    xAnimTableAddTransition(table, tran, "RunOutOfWorld01");
    xAnimTableAddTransition(table, tran, "SlipRun01");
    xAnimTableAddTransition(table, tran, "SlipIdle01");
    xAnimTableAddTransition(table, tran, "Land01");
    xAnimTableAddTransition(table, tran, "LandHigh01");
    xAnimTableAddTransition(table, tran, "LandRun01");
    xAnimTableAddTransition(table, tran, "Inactive01");
    xAnimTableAddTransition(table, tran, "Inactive02");
    xAnimTableAddTransition(table, tran, "Goo01");

    tran = xAnimTableNewTransition(table, "Idle01", "BounceStart01", BounceCheck, BounceCB, 0, 0,
                                   0.0f, 0.0f, 15, 0, 0.15f, NULL);
    xAnimTableAddTransition(table, tran, "SlipIdle01");
    xAnimTableAddTransition(table, tran, "Walk01");
    xAnimTableAddTransition(table, tran, "Run01");

    xAnimTableAddTransition(table, tran, "RunOutOfWorld01");
    xAnimTableAddTransition(table, tran, "SlipRun01");
    xAnimTableAddTransition(table, tran, "Land01");
    xAnimTableAddTransition(table, tran, "LandHigh01");
    xAnimTableAddTransition(table, tran, "LandRun01");
    xAnimTableAddTransition(table, tran, "JumpStart01");
    xAnimTableAddTransition(table, tran, "JumpApex01");
    xAnimTableAddTransition(table, tran, "DJumpStart01");
    xAnimTableAddTransition(table, tran, "DJumpLift01");
    xAnimTableAddTransition(table, tran, "Fall01");
    xAnimTableAddTransition(table, tran, "BbounceAttack01");
    xAnimTableAddTransition(table, tran, "BbounceStart01");
    xAnimTableAddTransition(table, tran, "BbashMiss01");

    static const char* const HIT_STATES[44] = { "Idle01",
                                                "SlipIdle01",
                                                "Walk01",
                                                "Run01",
                                                "SlipRun01",
                                                "Land01",
                                                "LandHigh01",
                                                "LandRun01",
                                                "Inactive01",
                                                "Inactive02",
                                                "TongueStart01",
                                                "TongueSlide01",
                                                "TongueJump01",
                                                "TongueJumpXtra01",
                                                "TongueDJumpApex01",
                                                "TongueLand01",
                                                "JumpStart01",
                                                "JumpApex01",
                                                "DJumpStart01",
                                                "DJumpLift01",
                                                "Fall01",
                                                "Bspin01",
                                                "Bspin02",
                                                "BbashStart01",
                                                "BbashStart02",
                                                "BbashStrike01",
                                                "BbounceStart01",
                                                "BbounceAttack01",
                                                "BbounceStrike01",
                                                "BounceStart01",
                                                "BounceLift01",
                                                "BounceApex01",
                                                "Bbowl01",
                                                "BbowlStart01",
                                                "BbowlWindup01",
                                                "BbowlToss01",
                                                "BbowlRecover01",
                                                "WallLaunch01",
                                                "WallFlight01",
                                                "WallFlight02",
                                                "WallLand01",
                                                "WallFall01",
                                                "WallStick01",
                                                NULL };
    xAnimTransition* hittran[9];

    hittran[0] = xAnimTableNewTransition(table, HIT_STATES[0], "Hit01", Hit01Check, Hit01CB, 0, 0,
                                         0.0f, 0.0f, 20, 0, 0.15f, NULL);
    hittran[1] = xAnimTableNewTransition(table, HIT_STATES[0], "Hit02", Hit02Check, Hit02CB, 0, 0,
                                         0.0f, 0.0f, 20, 0, 0.15f, NULL);
    hittran[2] = xAnimTableNewTransition(table, HIT_STATES[0], "Hit03", Hit03Check, Hit03CB, 0, 0,
                                         0.0f, 0.0f, 20, 0, 0.15f, NULL);
    hittran[3] = xAnimTableNewTransition(table, HIT_STATES[0], "Hit04", Hit04Check, Hit04CB, 0, 0,
                                         0.0f, 0.0f, 20, 0, 0.15f, NULL);
    hittran[4] = xAnimTableNewTransition(table, HIT_STATES[0], "Hit05", Hit05Check, Hit05CB, 0, 0,
                                         0.0f, 0.0f, 20, 0, 0.15f, NULL);
    hittran[5] = xAnimTableNewTransition(table, HIT_STATES[0], "Defeated01", Defeated01Check,
                                         DefeatedCB, 0, 3, 0.0f, 0.0f, 30, 0, 0.15f, NULL);
    hittran[6] = xAnimTableNewTransition(table, HIT_STATES[0], "Defeated02", Defeated02Check,
                                         DefeatedCB, 0, 3, 0.0f, 0.0f, 30, 0, 0.15f, NULL);
    hittran[7] = xAnimTableNewTransition(table, HIT_STATES[0], "Defeated03", Defeated03Check,
                                         DefeatedCB, 0, 3, 0.0f, 0.0f, 30, 0, 0.15f, NULL);

    hittran[8] = xAnimTableNewTransition(table, HIT_STATES[0], "GooDefeated", GooCheck, GooDeathCB,
                                         0, 0, 0.0f, 0.0f, 30, 0, 0.15f, NULL);

    for (S32 i = 1; HIT_STATES[i] != NULL; i++)
    {
        for (S32 j = 0; j < sizeof(hittran) / sizeof(hittran[0]); j++)
        {
            xAnimTableAddTransition(table, hittran[j], HIT_STATES[i]);
        }
    }

    xAnimTableNewTransition(
        table,
        "Idle01 SlipIdle01 Inactive01 Inactive02 Walk01 Run01 Run02 Run03 RunOutOfWorld01 Land01 LandHigh01 LandRun01 SlipIdle01 SlipRun01",
        "TongueStart01", SlideTrackCheck, SlideTrackCB, 0, 0, 0.0f, 0.0f, 9, 0, 0.15f, NULL);
    xAnimTableNewTransition(table, "TongueSlide01", "Idle01", NoslideTrackCheck, NoslideTrackCB, 0,
                            0, 0.0f, 0.0f, 9, 0, 0.15f, NULL);
    xAnimTableNewTransition(table, "TongueStart01", "TongueSlide01", NULL, NULL, 0x10, 0, 0.0f,
                            0.0f, 0, 0, 0.0f, NULL);
    xAnimTableNewTransition(table, "TongueSlide01 TongueLand01 TongueStart01", "TongueFall01",
                            TrackFallCheck, TrackFallCB, 0, 0, 0.0f, 0.0f, 9, 0, 0.15f, NULL);
    xAnimTableNewTransition(table, "TongueDJumpApex01", "TongueFall01", NULL, NULL, 0x10, 0, 0.0f,
                            0.0f, 0, 0, 0.2f, NULL);
    xAnimTableNewTransition(table, "TongueJump01", "TongueFall01", NULL, NULL, 0x10, 0, 0.0f, 0.0f,
                            0, 0, 0.08f, NULL);
    xAnimTableNewTransition(table, "TongueLand01", "TongueSlide01", NULL, NULL, 0x10, 0, 0.0f, 0.0f,
                            0, 0, 0.08f, NULL);
    xAnimTableNewTransition(table, "TongueSlide01 TongueLand01", "TongueJump01", JumpCheck, JumpCB,
                            0, 0, 0.0f, 0.0f, 10, 0, 0.15f, NULL);
    xAnimTableNewTransition(table, "TongueJump01 TongueFall01", "TongueJump01",
                            TrackPrefallJumpCheck, JumpCB, 0, 0, 0.0f, 0.0f, 15, 0, 0.15f, NULL);
    xAnimTableNewTransition(table, "TongueFall01", "TongueDJumpApex01", DblJumpCheck, DblJumpCB, 0,
                            0, 0.0f, 0.0f, 10, 0, 0.2f, NULL);
    xAnimTableNewTransition(table, "TongueJump01 TongueJumpXtra01 TongueDJumpApex01 TongueFall01",
                            "TongueLand01", LandTrackCheck, SlideTrackCB, 0, 0, 0.0f, 0.0f, 1, 0,
                            0.15f, NULL);
    xAnimTableNewTransition(table, "TongueJump01 TongueJumpXtra01 TongueDJumpApex01 TongueFall01",
                            "Land01", LandNoTrackCheck, NoslideTrackCB, 0, 0, 0.0f, 0.0f, 1, 0,
                            0.15f, NULL);
    xAnimTableNewTransition(table, "TongueJump01 TongueJumpXtra01 TongueDJumpApex01 TongueFall01",
                            "LandRun01", LandNoTrackFastCheck, NULL, 0, 0, 0.0f, 0.0f, 3, 0, 0.15f,
                            NULL);
    xAnimTableNewTransition(table, "TongueJump01 TongueJumpXtra01 TongueDJumpApex01 TongueFall01",
                            "Walk01", LandNoTrackWalkCheck, NULL, 0, 0, 0.0f, 0.0f, 3, 0, 0.15f,
                            NULL);
    xAnimTableNewTransition(table, "TongueJump01 TongueJumpXtra01 TongueDJumpApex01 TongueFall01",
                            "SlipRun01", LandNoTrackSlipRunCheck, SlipRunCB, 0, 0, 0.0f, 0.0f, 4, 0,
                            0.15f, NULL);
    xAnimTableNewTransition(table, "TongueJump01 TongueJumpXtra01 TongueDJumpApex01 TongueFall01",
                            "SlipIdle01", LandNoTrackSlipIdleCheck, NULL, 0, 0, 0.0f, 0.0f, 4, 0,
                            0.15f, NULL);

    xAnimTableNewTransition(table, "TongueJump01", "TongueJumpXtra01", DblJumpCheck,
                            TongueDblJumpCB, 0x20, 0, 0.0f, 0.0f, 10, 0, 0.0f, NULL);
    xAnimTableNewTransition(table, "TongueJumpXtra01", "TongueDJumpApex01", NULL, TongueDblSpinCB,
                            0x10, 0, 0.0f, 0.0f, 1, 0, 0.0f, NULL);

    xAnimTableNewTransition(
        table,
        "Idle01 Inactive01 Inactive02 Walk01 Run01 Run02 Run03 RunOutOfWorld01 SlipRun01 SlipIdle01 Land01 LandRun01",
        "BoulderRoll01", BoulderRollCheck, BoulderRollWindupCB, 0, 0, 0.0f, 0.0f, 10, 0, 0.5f,
        NULL);
    xAnimTableNewTransition(table, "BoulderRoll01", "BoulderRoll02", NULL, BoulderRollCB, 0x10, 0,
                            0.0f, 0.0f, 0, 0, 0.0f, NULL);
    xAnimTableNewTransition(table, "BoulderRoll02", "SlipIdle01", IdleSlipCheck, NULL, 0, 0, 0.0f,
                            0.0f, 10, 0, 0.25f, NULL);
    xAnimTableNewTransition(table, "BoulderRoll02", "Idle01", IdleCheck, NULL, 0, 0, 0.0f, 0.0f, 10,
                            0, 0.25f, NULL);
    xAnimTableNewTransition(table, "BoulderRoll02", "Walk01", WalkCheck, NULL, 0, 0, 0.0f, 0.0f, 10,
                            0, 0.25f, NULL);
    xAnimTableNewTransition(table, "BoulderRoll02", "Run01", RunStoicCheck, NULL, 0, 0, 0.0f, 0.0f,
                            10, 0, 0.25f, NULL);

    xAnimTableNewTransition(table, "BoulderRoll02", "RunOutOfWorld01", RunOutOfWorldCheck, NULL, 0,
                            0x200100, 0.0f, 0.0f, 10, 0, 0.25f, NULL);
    xAnimTableNewTransition(table, "BoulderRoll02", "SlipRun01", RunSlipCheck, SlipRunCB, 0, 0,
                            0.0f, 0.0f, 10, 0, 0.25f, NULL);
    xAnimTableNewTransition(table, "BoulderRoll02", "Fall01", FallCheck, NULL, 0, 0, 0.0f, 0.0f, 10,
                            0, 0.25f, NULL);

    xAnimTableNewTransition(table, "JumpStart01", "JumpApex01", NULL, NULL, 0x10, 0, 0.0f, 0.0f, 0,
                            0, 0.08f, NULL);

    xAnimTableNewTransition(table, "JumpApex01", "Fall01", NULL, NULL, 0x10, 0, 0.0f, 0.0f, 0, 0,
                            0.08f, NULL);

    xAnimTableNewTransition(table, "BounceStart01", "BounceLift01", NULL, NULL, 0x10, 0, 0.0f, 0.0f,
                            0, 0, 0.15f, NULL);
    xAnimTableNewTransition(table, "BounceLift01", "BounceApex01", JumpApexCheck, NULL, 0, 0, 0.0f,
                            0.0f, 1, 0, 0.15f, NULL);
    xAnimTableNewTransition(table, "BounceApex01", "Fall01", NULL, NULL, 0x10, 0, 0.0f, 0.0f, 0, 0,
                            0.15f, NULL);

    xAnimTableNewTransition(table, "DJumpStart01", "DJumpLift01", NULL, NULL, 0x10, 0, 0.0f, 0.0f,
                            0, 0, 0.0f, NULL);

    xAnimTableNewTransition(table, "JumpLift01", "JumpApex01", JumpApexCheck, NULL, 0, 0, 0.0f,
                            0.0f, 1, 0, 0.15f, NULL);

    xAnimTableNewTransition(
        table, "JumpStart01 JumpLift01 JumpApex01 BounceStart01 BounceLift01 BounceApex01 Fall01",
        "DJumpStart01", DblJumpCheck, DblJumpCB, 0, 0, 0.0f, 0.0f, 10, 0, 0.15f, NULL);
    xAnimTableNewTransition(
        table,
        "JumpStart01 JumpLift01 JumpApex01 Fall01 WallFlight01 WallFlight02 WallLand01 WallFall01 DJumpStart01 DJumpLift01 WallStick01",
        "WallLaunch01", WallJumpLaunchCheck, WallJumpLaunchCallback, 0, 0, 0.0f, 0.0f, 10, 0, 0.15f,
        NULL);
    xAnimTableNewTransition(table, "WallLaunch01", "WallFlight01", NULL, WallJumpCallback, 0x10, 0,
                            0.0f, 0.0f, 0, 0, 0.0f, NULL);
    xAnimTableNewTransition(table, "WallFlight01", "WallFlight02", NULL, NULL, 0x10, 0, 0.0f, 0.0f,
                            0, 0, 0.0f, NULL);

    xAnimTableNewTransition(table, "WallFlight01 WallFlight02", "WallLand01",
                            WallJumpFlightLandCheck, WallJumpFlightLandCallback, 0, 0, 0.0f, 0.0f,
                            0, 0, 0.08f, NULL);
    xAnimTableNewTransition(table, "WallLand01", "WallFlight02", WallJumpLandFlightCheck,
                            WallJumpLandFlightCallback, 0, 0, 0.0f, 0.0f, 0, 0, 0.08f, NULL);
    xAnimTableNewTransition(table, "WallLand01", "WallStick01", NULL, WallJumpStickCB, 0x10, 0,
                            0.0f, 0.0f, 0, 0, 0.15f, NULL);
    xAnimTableNewTransition(table, "WallStick01", "WallFall01", WallJumpFallCheck, WallJumpFallCB,
                            0, 0, 0.0f, 0.0f, 0, 0, 0.15f, NULL);

    xAnimTableNewTransition(
        table,
        "JumpApex01 Fall01 DJumpLift01 WallFlight01 WallFlight02 WallLand01 WallFall01 BbashStart01 BbashStart02",
        "Land01", LandCheck, LandCallback, 0, 0, 0.0f, 0.0f, 1, 0, 0.15f, NULL);
    xAnimTableNewTransition(
        table,
        "JumpApex01 Fall01 DJumpLift01 WallFlight01 WallFlight02 WallLand01 WallFall01 BbashStart01 BbashStart02",
        "LandHigh01", LandHighCheck, LandCallback, 0, 0, 0.0f, 0.0f, 2, 0, 0.15f, NULL);
    xAnimTableNewTransition(
        table,
        "JumpApex01 Fall01 DJumpLift01 WallFlight01 WallFlight02 WallLand01 WallFall01 BbashStart01 BbashStart02",
        "LandRun01", LandRunCheck, LandCallback, 0, 0, 0.0f, 0.0f, 3, 0, 0.15f, NULL);
    xAnimTableNewTransition(
        table,
        "JumpApex01 Fall01 DJumpLift01 WallFlight01 WallFlight02 WallLand01 WallFall01 BbashStart01 BbashStart02",
        "SlipRun01", LandSlipRunCheck, LandSlipRunCallback, 0, 0, 0.0f, 0.0f, 4, 0, 0.15f, NULL);
    xAnimTableNewTransition(
        table,
        "JumpApex01 Fall01 DJumpLift01 WallFlight01 WallFlight02 WallLand01 WallFall01 BbashStart01 BbashStart02",
        "SlipIdle01", LandSlipIdleCheck, LandCallback, 0, 0, 0.0f, 0.0f, 4, 0, 0.15f, NULL);

    tran = xAnimTableNewTransition(table, "Idle01", "Bspin01", BubbleSpinCheck1, BubbleSpinCB, 0, 0,
                                   0.0f, 0.0f, 10, 0, 0.05f, NULL);
    xAnimTableAddTransition(table, tran, "SlipIdle01");
    xAnimTableAddTransition(table, tran, "Inactive01");
    xAnimTableAddTransition(table, tran, "Inactive02");
    xAnimTableAddTransition(table, tran, "Walk01");
    xAnimTableAddTransition(table, tran, "Run01");

    xAnimTableAddTransition(table, tran, "SlipRun01");
    xAnimTableAddTransition(table, tran, "Land01");
    xAnimTableAddTransition(table, tran, "LandHigh01");
    xAnimTableAddTransition(table, tran, "LandRun01");
    xAnimTableAddTransition(table, tran, "JumpStart01");
    xAnimTableAddTransition(table, tran, "JumpLift01");
    xAnimTableAddTransition(table, tran, "JumpApex01");
    xAnimTableAddTransition(table, tran, "BounceStart01");
    xAnimTableAddTransition(table, tran, "BounceLift01");
    xAnimTableAddTransition(table, tran, "BounceApex01");
    xAnimTableAddTransition(table, tran, "DJumpStart01");
    xAnimTableAddTransition(table, tran, "DJumpLift01");
    xAnimTableAddTransition(table, tran, "Fall01");

    tran = xAnimTableNewTransition(table, "Idle01", "Bspin02", BubbleSpinCheck2, BubbleSpinCB, 0, 0,
                                   0.0f, 0.0f, 10, 0, 0.05f, NULL);
    xAnimTableAddTransition(table, tran, "SlipIdle01");
    xAnimTableAddTransition(table, tran, "Inactive01");
    xAnimTableAddTransition(table, tran, "Inactive02");
    xAnimTableAddTransition(table, tran, "Walk01");
    xAnimTableAddTransition(table, tran, "Run01");

    xAnimTableAddTransition(table, tran, "SlipRun01");
    xAnimTableAddTransition(table, tran, "Land01");
    xAnimTableAddTransition(table, tran, "LandHigh01");
    xAnimTableAddTransition(table, tran, "LandRun01");
    xAnimTableAddTransition(table, tran, "JumpStart01");
    xAnimTableAddTransition(table, tran, "JumpLift01");
    xAnimTableAddTransition(table, tran, "JumpApex01");
    xAnimTableAddTransition(table, tran, "BounceStart01");
    xAnimTableAddTransition(table, tran, "BounceLift01");
    xAnimTableAddTransition(table, tran, "BounceApex01");
    xAnimTableAddTransition(table, tran, "DJumpStart01");
    xAnimTableAddTransition(table, tran, "DJumpLift01");
    xAnimTableAddTransition(table, tran, "Fall01");

    tran = xAnimTableNewTransition(table, "JumpStart01", "BbounceStart01", BubbleBounceCheck,
                                   BubbleBounceCB, 0, 0, 0.0f, 0.0f, 10, 0, 0.15f, NULL);
    xAnimTableAddTransition(table, tran, "JumpLift01");
    xAnimTableAddTransition(table, tran, "JumpApex01");
    xAnimTableAddTransition(table, tran, "BounceStart01");
    xAnimTableAddTransition(table, tran, "BounceLift01");
    xAnimTableAddTransition(table, tran, "BounceApex01");
    xAnimTableAddTransition(table, tran, "DJumpStart01");
    xAnimTableAddTransition(table, tran, "DJumpLift01");
    xAnimTableAddTransition(table, tran, "Fall01");
    xAnimTableAddTransition(table, tran, "BbashMiss01");

    xAnimTableNewTransition(table, "Bspin01 Bspin02", "SlipIdle01", IdleSlipCheck, IdleCB, 0x10, 0,
                            0.0f, 0.0f, 1, 0, 0.15f, NULL);
    xAnimTableNewTransition(table, "Bspin01 Bspin02", "Idle01", IdleCheck, IdleCB, 0x10, 0, 0.0f,
                            0.0f, 1, 0, 0.15f, NULL);
    xAnimTableNewTransition(table, "Bspin01 Bspin02", "Walk01", WalkCheck, NULL, 0x10, 0, 0.0f,
                            0.0f, 1, 0, 0.15f, NULL);
    xAnimTableNewTransition(table, "Bspin01 Bspin02", "Run01", RunStoicCheck, NULL, 0x10, 0, 0.0f,
                            0.0f, 1, 0, 0.15f, NULL);

    xAnimTableNewTransition(table, "Bspin01 Bspin02", "RunOutOfWorld01", RunOutOfWorldCheck, NULL,
                            0x10, 0x200100, 0.0f, 0.0f, 3, 0, 0.15f, NULL);
    xAnimTableNewTransition(table, "Bspin01 Bspin02", "SlipRun01", RunSlipCheck, SlipRunCB, 0x10, 0,
                            0.0f, 0.0f, 4, 0, 0.15f, NULL);
    xAnimTableNewTransition(table, "Bspin01 Bspin02", "Fall01", NULL, NULL, 0x10, 0, 0.0f, 0.0f, 1,
                            0, 0.15f, NULL);

    xAnimTableNewTransition(table, "BbashStart01 BbashStart02", "Fall01", NULL, NULL, 0x10, 0, 0.0f,
                            0.0f, 1, 0, 0.1f, NULL);
    xAnimTableNewTransition(table, "BbashStart01 BbashStart02", "BbashStrike01", BBashStrikeCheck,
                            BBashStrikeCB, 0, 0, 0.0f, 0.0f, 2, 0, 0.01f, NULL);

    xAnimTableNewTransition(table, "BbashStrike01", "Fall01", NULL, NULL, 0x10, 0, 0.0f, 0.0f, 1, 0,
                            0.15f, NULL);

    xAnimTableNewTransition(table, "BbounceStart01", "BbounceAttack01", NULL, BBounceAttackCB, 0x10,
                            0, 0.0f, 0.0f, 1, 0, 0.1f, NULL);
    xAnimTableNewTransition(table, "BbounceAttack01", "BbounceStrike01", BBounceStrikeCheck,
                            BBounceStrikeCB, 0, 0, 0.0f, 0.0f, 1, 0, 0.0f, NULL);
    xAnimTableNewTransition(table, "BbounceAttack01", "JumpLift01", BBounceToJumpCheck,
                            BBounceToJumpCB, 0, 0, 0.0f, 0.0f, 1, 0, 0.1f, NULL);

    xAnimTableNewTransition(table, "BbounceStrike01", "SlipIdle01", IdleSlipCheck, IdleCB, 0x10, 0,
                            0.0f, 0.0f, 1, 0, 0.15f, NULL);
    xAnimTableNewTransition(table, "BbounceStrike01", "Idle01", IdleCheck, IdleCB, 0x10, 0, 0.0f,
                            0.0f, 1, 0, 0.15f, NULL);
    xAnimTableNewTransition(table, "BbounceStrike01", "Walk01", WalkCheck, NULL, 0x10, 0, 0.0f,
                            0.0f, 1, 0, 0.15f, NULL);
    xAnimTableNewTransition(table, "BbounceStrike01", "Run01", RunStoicCheck, NULL, 0x10, 0, 0.0f,
                            0.0f, 1, 0, 0.15f, NULL);

    xAnimTableNewTransition(table, "BbounceStrike01", "RunOutOfWorld01", RunOutOfWorldCheck, NULL,
                            0x10, 0x200100, 0.0f, 0.0f, 3, 0, 0.15f, NULL);
    xAnimTableNewTransition(table, "BbounceStrike01", "SlipRun01", RunSlipCheck, SlipRunCB, 0x10, 0,
                            0.0f, 0.0f, 4, 0, 0.15f, NULL);

    xAnimTableNewTransition(table,
                            "JumpStart01 JumpLift01 JumpApex01 Fall01 DJumpStart01 DJumpLift01",
                            "LedgeGrab01", LedgeGrabCheck, LedgeGrabCB, 0, 0, 0.0f, 0.0f, 11, 0,
                            0.1f, NULL);
    xAnimTableNewTransition(table, "LedgeGrab01", "Idle01", NULL, LedgeFinishCB, 0x10, 0, 0.0f,
                            0.0f, 0, 0, 0.1f, NULL);

    xAnimTableNewTransition(
        table,
        "Idle01 SlipIdle01 Inactive01 Inactive02 Walk01 Land01 LandHigh01 LandRun01 Run01 Run02 Run03 SlipRun01",
        "BbowlStart01", BbowlCheck, BbowlCB, 0, 0, 0.0f, 0.0f, 10, 0, 0.1f, NULL);

    xAnimTableNewTransition(table, "BbowlStart01", "BbowlWindup01", NULL, NULL, 0x10, 0, 0.0f, 0.0f,
                            1, 0, 0.15f, NULL);

    xAnimTableNewTransition(table, "BbowlWindup01", "BbowlToss01", BbowlWindupEndCheck, NULL, 0, 0,
                            0.0f, 0.0f, 1, 0, 0.15f, NULL);

    xAnimTableNewTransition(table, "BbowlToss01", "BbowlRecover01", NULL, BbowlTossEndCB, 0x10, 0,
                            0.0f, 0.0f, 1, 0, 0.15f, NULL);

    xAnimTableNewTransition(table, "BbowlRecover01", "Walk01", BbowlRecoverWalkCheck, NULL, 0, 0,
                            0.0f, 0.0f, 1, 0, 0.15f, NULL);
    xAnimTableNewTransition(table, "BbowlRecover01", "Run01", BbowlRecoverRunCheck, NULL, 0, 0,
                            0.0f, 0.0f, 1, 0, 0.15f, NULL);

    xAnimTableNewTransition(table, "BbowlRecover01", "RunOutOfWorld01",
                            BbowlRecoverRunOutOfWorldCheck, NULL, 0, 0x200100, 0.0f, 0.0f, 3, 0,
                            0.15f, NULL);
    xAnimTableNewTransition(table, "BbowlRecover01", "SlipRun01", BbowlRecoverRunSlipCheck,
                            SlipRunCB, 0, 0, 0.0f, 0.0f, 4, 0, 0.15f, NULL);
    xAnimTableNewTransition(table, "BbowlRecover01", "Idle01", NULL, IdleCB, 0x10, 0, 0.0f, 0.0f, 1,
                            0, 0.15f, NULL);

    bungee_state::insert_animations(*table);

    cruise_bubble::insert_player_animations(*table);

    xAnimTableNewTransition(table, "Hit01", "Idle01", NULL, IdleCB, 0x10, 0, 0.0f, 0.0f, 0, 0,
                            0.15f, NULL);
    xAnimTableNewTransition(table, "Hit02", "Idle01", NULL, IdleCB, 0x10, 0, 0.0f, 0.0f, 0, 0,
                            0.15f, NULL);
    xAnimTableNewTransition(table, "Hit03", "Idle01", NULL, IdleCB, 0x10, 0, 0.0f, 0.0f, 0, 0,
                            0.15f, NULL);
    xAnimTableNewTransition(table, "Hit04", "Idle01", NULL, IdleCB, 0x10, 0, 0.0f, 0.0f, 0, 0,
                            0.15f, NULL);
    xAnimTableNewTransition(table, "Hit05", "Idle01", NULL, IdleCB, 0x10, 0, 0.0f, 0.0f, 0, 0,
                            0.15f, NULL);

    xAnimTableNewTransition(table, "Idle01 SlipIdle01 Inactive01 Inactive02 ", "Talk01",
                            zPlayer::TalkCheck, NULL, 0, 0, 0.0f, 0.0f, 20, 0, 0.15f, NULL);
    xAnimTableNewTransition(table, "Idle01 SlipIdle01 Inactive01 Inactive02 ", "Talk02",
                            zPlayer::TalkCheck, NULL, 0, 1, 0.0f, 0.0f, 20, 0, 0.15f, NULL);
    xAnimTableNewTransition(table, "Idle01 SlipIdle01 Inactive01 Inactive02 ", "Talk03",
                            zPlayer::TalkCheck, NULL, 0, 2, 0.0f, 0.0f, 20, 0, 0.15f, NULL);
    xAnimTableNewTransition(table, "Idle01 SlipIdle01 Inactive01 Inactive02 ", "Talk04",
                            zPlayer::TalkCheck, NULL, 0, 3, 0.0f, 0.0f, 20, 0, 0.15f, NULL);
    xAnimTableNewTransition(table, "Talk01", "Idle01", zPlayer::TalkDoneCheck, NULL, 0, 0, 0.0f,
                            0.0f, 20, 0, 0.15f, NULL);
    xAnimTableNewTransition(table, "Talk02", "Idle01", zPlayer::TalkDoneCheck, NULL, 0, 1, 0.0f,
                            0.0f, 20, 0, 0.15f, NULL);
    xAnimTableNewTransition(table, "Talk03", "Idle01", zPlayer::TalkDoneCheck, NULL, 0, 2, 0.0f,
                            0.0f, 20, 0, 0.15f, NULL);
    xAnimTableNewTransition(table, "Talk04", "Idle01", zPlayer::TalkDoneCheck, NULL, 0, 3, 0.0f,
                            0.0f, 20, 0, 0.15f, NULL);

    xAnimState* state = xAnimTableGetState(table, "BbashStart01");

    xAnimStateNewEffect(state, 1, 100, 0.4f, 0.0f, BbashStart01EffectMissCB, 0);

    return table;
}

xAnimTable* zSpongeBobTongue_AnimTable()
{
    xAnimTable* table;

    table = xAnimTableNew("SBTongue", 0);
    xAnimTableNewState(table, "TongueSlide01", 0x10, 0, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, "TongueStart01", 0x10, 0, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, "TongueJump01", 0x10, 0, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, "TongueJumpXtra01", 0x10, 0, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, "TongueDJumpApex01", 0x10, 0, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, "TongueFall01", 0x10, 0, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, "TongueLand01", 0x10, 0, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    return table;
}

xAnimTable* zEntPlayer_BoulderVehicleAnimTable()
{
    xAnimTable* table;

    table = xAnimTableNew("BoulderVehicleTable", 0);

    xAnimTableNewState(table, "Idle01", 0x10, 0, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, "Move01", 0x10, 0, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);

    xAnimTableNewTransition(table, "Idle01", "Move01", BoulderRollMoveCheck, NULL, 0, 0, 0.0f, 0.0f,
                            1, 0, 0.45f, NULL);
    xAnimTableNewTransition(table, "Move01", "Idle01", BoulderRollIdleCheck, NULL, 0, 0, 0.0f, 0.0f,
                            1, 0, 0.45f, NULL);

    return table;
}

// Dead-stripped placeholder: retail's string pool has these unreferenced SB state names here, ahead
// of zEntPlayer_BoulderVehicleAnimTable's strings, so a stripped function at this position interned
// them. Real name and body unknown.
static void __deadstripped_zEntPlayerAnimationTables()
{
    xStrHash("SB");
    xStrHash("Idle02");
    xStrHash("Idle03");
    xStrHash("Idle04");
    xStrHash("Idle05");
    xStrHash("Idle06");
    xStrHash("Idle07");
    xStrHash("Idle08");
    xStrHash("Idle09");
    xStrHash("Idle10");
    xStrHash("Idle11");
    xStrHash("Idle12");
    xStrHash("Idle13");
    xStrHash("Inactive03");
    xStrHash("Inactive04");
    xStrHash("Inactive05");
    xStrHash("Inactive06");
    xStrHash("Inactive07");
    xStrHash("Inactive08");
    xStrHash("Inactive09");
    xStrHash("Inactive10");
    xStrHash("Defeated02");
    xStrHash("Defeated03");
    xStrHash("Defeated04");
    xStrHash("Defeated05");
    xStrHash("TongueFall01");
    xStrHash("TongueTumble01");
    xStrHash("Goo01");
    xStrHash("Goo02");
    xStrHash("BoulderRoll02");
    xStrHash("Talk04");
    xStrHash("Talk03");
    xStrHash("Talk02");
    xStrHash("Talk01");
    xStrHash("SpatulaGrab01");
}

xAnimTable* zSpongePat_AnimTable()
{
    xAnimTable* table;

    table = xAnimTableNew("SpongePat", 0);

    xAnimTableNewState(table, "Idle01", 0x10, 0x1840, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, "Fast01", 0x10, 0x1840, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, "Slow01", 0x10, 0x1840, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, "Hit01", 0x20, 0, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, "HitLeft01", 0x20, 0, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, "HitRight01", 0x20, 0, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, "Defeated01", 0, 0x480, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, "JumpApex", 0x20, 0x100a, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, "JumpDFall", 0x10, 0x100a, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, "JumpDLand", 0x20, 0x100a, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, "JumpD", 0x20, 0x100a, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, "JumpFall", 0x10, 0x100a, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, "JumpLand", 0x20, 0x100a, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, "GooDefeated", 0, 0x480, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);

    xAnimTableNewTransition(table, "Slow01 Idle01 Fast01", "JumpFall", TrackFallCheck, TrackFallCB,
                            0, 0, 0.0f, 0.0f, 9, 0, 0.15f, NULL);
    xAnimTableNewTransition(table, "JumpApex", "JumpFall", NULL, NULL, 0x10, 0, 0.0f, 0.0f, 0, 0,
                            0.08f, NULL);

    xAnimTableNewTransition(table, "JumpLand", "Idle01", NULL, NULL, 0x10, 0, 0.0f, 0.0f, 0, 0,
                            0.08f, NULL);
    xAnimTableNewTransition(table, "JumpDLand", "Idle01", NULL, NULL, 0x10, 0, 0.0f, 0.0f, 0, 0,
                            0.08f, NULL);

    xAnimTableNewTransition(table, "JumpD", "JumpDFall", NULL, NULL, 0x10, 0, 0.0f, 0.0f, 0, 0,
                            0.2f, NULL);
    xAnimTableNewTransition(table, "Slow01 Idle01 Fast01 JumpLand JumpDLand", "JumpApex", JumpCheck,
                            JumpCB, 0, 0, 0.0f, 0.0f, 10, 0, 0.2f, NULL);
    xAnimTableNewTransition(table, "JumpApex JumpFall", "JumpApex", TrackPrefallJumpCheck, JumpCB,
                            0, 0, 0.0f, 0.0f, 15, 0, 0.15f, NULL);
    xAnimTableNewTransition(table, "JumpApex JumpFall", "JumpD", DblJumpCheck, DblJumpCB, 0, 0,
                            0.0f, 0.0f, 10, 0, 0.1f, NULL);
    xAnimTableNewTransition(table, "JumpApex JumpFall JumpD", "JumpLand", LandTrackCheck,
                            SlideTrackCB, 0, 0, 0.0f, 0.0f, 1, 0, 0.15f, NULL);
    xAnimTableNewTransition(table, "JumpDFall", "JumpDLand", LandTrackCheck, SlideTrackCB, 0, 0,
                            0.0f, 0.0f, 1, 0, 0.15f, NULL);
    xAnimTableNewTransition(table, "Idle01 Fast01 JumpLand JumpDLand", "Slow01",
                            SlideDecelerateCheck, SlideDecelerateCB, 0, 0, 0.0f, 0.0f, 1, 0, 0.9f,
                            NULL);
    xAnimTableNewTransition(table, "Idle01 Slow01 JumpLand JumpDLand", "Fast01",
                            SlideAccelerateCheck, SlideAccelerateCB, 0, 0, 0.0f, 0.0f, 1, 0, 0.3f,
                            NULL);
    xAnimTableNewTransition(table, "Slow01 Fast01", "Idle01", SlideCruisingCheck, SlideCruisingCB,
                            0, 0, 0.0f, 0.0f, 1, 0, 0.3f, NULL);

    xAnimTableNewTransition(
        table, "Idle01 Fast01 Slow01 JumpApex JumpDFall JumpDLand JumpD JumpFall JumpLand ",
        "Hit01", HitCheck, HitCB, 0, 0, 0.0f, 0.0f, 1, 0, 0.3f, NULL);
    xAnimTableNewTransition(table, "Hit01", "Idle01", NULL, IdleCB, 0x10, 0, 0.0f, 0.0f, 0, 0,
                            0.15f, NULL);
    xAnimTableNewTransition(
        table, "Idle01 Fast01 Slow01 JumpApex JumpDFall JumpDLand JumpD JumpFall JumpLand ",
        "Defeated01", DefeatedCheck, DefeatedCB, 0, 5, 0.0f, 0.0f, 30, 0, 0.15f, NULL);
    xAnimTableNewTransition(
        table, "Idle01 Fast01 Slow01 JumpApex JumpDFall JumpDLand JumpD JumpFall JumpLand ",
        "GooDefeated", GooCheck, GooDeathCB, 0, 0, 0.0f, 0.0f, 30, 0, 0.15f, NULL);

    return table;
}
