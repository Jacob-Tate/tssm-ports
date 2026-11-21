#ifndef ZMENUSB_H
#define ZMENUSB_H

#include "zUI.h"

struct ztalkbox;
struct xGroup;
struct xSoundFX;
struct zUIImage;
struct zUIModel;
struct zUIText;
struct zUITextAsset;

struct zUICustomDialogPlayer : zUICustom
{
    zUICustomDialogPlayer(zUI* ui);
    U8 HandleEvent(xBase* from, U32 toEvent, const F32* toParam, xBase* toParamWidget,
                   U32 toParamWidgetID);
};

struct zUICustomUpgradeTutorialContoller : zUICustom
{
    ztalkbox* upgradeTutorialTalkBox;
    zUIText* autoSave;
    U32 upgradeTutorialID;

    zUICustomUpgradeTutorialContoller(zUI* ui);

    void Reset();
    void PreUpdate(F32 dt);
    U8 HandleEvent(xBase* from, U32 toEvent, const F32* toParam, xBase* toParamWidget,
                   U32 toParamWidgetID);
};

struct zUICustomPalRedirector : zUICustom
{
    zUICustomPalRedirector(zUI* ui);
    U8 HandleEvent(xBase* from, U32 toEvent, const F32* toParam, xBase* toParamWidget,
                   U32 toParamWidgetID);
};

// Layout from the PS2 DWARF; the GC offsets of the fields up to mbIsHidden are confirmed by
// the constructor.
struct zUICustomExtrasIcon : zUICustom
{
    F32 mOffsetX;
    F32 mOffsetY;
    F32 mWidth;
    F32 mHeight;
    U8 mbFullReset;
    U8 misUnlocked;
    U8 mbIsHidden;
    xModelInstance* munlockedModel;
    xAnimTable* munlockedAtbl;
    F32 munlockedOffsetX;
    F32 munlockedOffsetY;
    F32 munlockedWidth;
    F32 munlockedHeight;

    // Member functions follow the original source order (the PS2 DWARF lists them reversed);
    // ResetMotion is the key function that emits the vtable.
    zUICustomExtrasIcon(zUI* ui);
    void InitModel(zUIModel* model, zUIModel* placeholder, F32 globalOffsetX, F32 globalOffsetY);
    void ResetMotion();
    void PostInitMotion();
    void PreUpdate(F32 dt);
    void PostUpdate(F32 dt);
    U8 HandleEvent(xBase* from, U32 toEvent, const F32* toParam, xBase* toParamWidget,
                   U32 toParamWidgetID);
};

// Layout from the PS2 DWARF (GC offsets confirmed by the inlined constructor in
// zUICustomExtrasManager::InitScreen).
struct zUICustomExtrasSound : zUICustom
{
    U8 mNumSounds;
    U8 mCurrentSound;
    U8 mExtraID;
    xGroup* mSoundGroup;
    xGroup* mTextGroup;
    xSoundFX* mSoundMove;
    xSoundFX* mSoundDeny;
    xSoundFX* mCurrentSoundFX;

    // No symbol on GC or PS2: inlined into zUICustomExtrasManager::InitScreen.
    zUICustomExtrasSound(zUI* ui)
        : zUICustom(ui), mNumSounds(0), mCurrentSound(0), mExtraID(0), mSoundGroup(NULL),
          mTextGroup(NULL), mSoundMove(NULL), mSoundDeny(NULL), mCurrentSoundFX(NULL)
    {
    }

    U8 HandleEvent(xBase* from, U32 toEvent, const F32* toParam, xBase* toParamWidget,
                   U32 toParamWidgetID);
    void InitScreen();
    void EnterScreen();
};

// Layout from the PS2 DWARF (GC size 0x28 from the operator new call in
// zUICustomExtrasManager::InitScreen).
struct zUICustomExtrasArt : zUICustom
{
    U8 mExtraID;
    zUIImage* mImage;
    zUIText* mDescription;
    xGroup* mImageGroup;
    xSoundFX* mSoundMove;
    iTime mImageTime;

    // No symbol on GC or PS2: inlined into zUICustomExtrasManager::InitScreen, which only
    // clears these three fields.
    zUICustomExtrasArt(zUI* ui) : zUICustom(ui), mExtraID(0), mImageGroup(NULL), mSoundMove(NULL)
    {
    }

    void UpdateDescription();
    void EnterScreen();
    U8 HandleEvent(xBase* from, U32 toEvent, const F32* toParam, xBase* toParamWidget,
                   U32 toParamWidgetID);
    void InitScreen();
};

// Static data/functions: the GC symbols are UpgradeSparkleEffect class statics
// (sparkleStart__20UpgradeSparkleEffect, Start__20UpgradeSparkleEffectFR5xVec3R5xVec3ffff, ...).
struct UpgradeSparkleEffect
{
    static xVec3 sparkleStart;
    static xVec3 sparkleEnd;
    static xVec3 sparkleScale;
    static xVec3 sparkleVel;
    static F32 sparkleTime;
    static F32 totalTimeForEffect;
    static F32 multiplier;
    static F32 randomYOffset;
    static F32 probability;
    static zUITextAsset* powerupText[10];
    static S32 powerupIndex;
    static U8 effectStarted;
    static F32 fmvDelay;
    static iSndHandle upgradeSound;
    static iSndGroupHandle upgradeSoundGroupHandle;

    static void PlayUpgradeFMV(S32 powerupIdx);
    static void Start(xVec3& start, xVec3& end, F32 timeForEffect, F32 randomYMultiplier,
                      F32 speedMultiplier, F32 probabilityToSpawn);
    static void Update(F32 dt);
};

struct zUICustomLoadingText : zUICustom
{
    zUICustomLoadingText(zUI* ui);
    U8 HandleEvent(xBase* from, U32 toEvent, const F32* toParam, xBase* toParamWidget,
                   U32 toParamWidgetID);
};

U32 zMenuSB_IsTaskUnlocked(U32 sceneID, U32 taskID);
void zMenuSB_LoadTaskDefaults();
void zMenuSB_InitTaskDefaults(xIniFile* ini);
void zMenuSB_LoadMenuCounters(xSerial* s);
void zMenuSB_SaveMenuCounters(xSerial* s);
bool zMenuSB_EnsureFreeAudioStream(bool willingToStopMainGameMusic);
void zMenuSB_InitPowerUpMenu();
void zMenuSB_UpdatePowerUpMenu();
void zMenuSB_UnlockAllTasks();

#ifdef PLATFORM_PC
// PC: for the Cheats tab (iCheats.h).
S32 zMenuSB_PCExtraCount();
bool zMenuSB_PCExtra(S32 index, S32* type, S32* id, S32* chestsNeeded, S32* status);
void zMenuSB_PCToggleExtra(S32 index);
void zMenuSB_PCSetExtraStatus(S32 index, S32 status);
#endif

#endif
