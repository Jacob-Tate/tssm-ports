#ifndef ZUICUSTOM_H
#define ZUICUSTOM_H

#include "zEntPlayer.h"

struct _zConditional;
struct xGroup;
struct xIniFile;
struct xSoundFX;
struct zUI;
struct zUIMotionFrame;
struct zUIText;

struct zUICustom
{
    // vtable at 0x0 (__vt__9zUICustom slot order)
    virtual void Init();
    virtual void Setup();
    virtual void Reset();
    virtual void ResetMotion();
    virtual U8 HandleEvent(xBase* from, U32 toEvent, const F32* toParam, xBase* toParamWidget,
                           U32 toParamWidgetID);
    virtual void PreUpdate(F32 dt);
    virtual void PostUpdate(F32 dt);
    virtual U8 PreRender();
    virtual void PostRender();
    virtual void Exit();
    virtual U8 PreAutoMenu();
    virtual void PreInitMotion();
    virtual void PostInitMotion();
    virtual void PreApplyMotionFrame(zUIMotionFrame* frame);
    virtual void PostApplyMotionFrame(zUIMotionFrame* frame);
    virtual U8 IsSystem(zUI* ui)
    {
        return 0;
    }
    virtual U8 IsSignalAllowed(U32 event)
    {
        return 1;
    }

    zUI* ui;

    // No GC symbol: inlined into the subclass constructors (__ct__*zUICustom*FP3zUI).
    zUICustom(zUI* ui) : ui(ui)
    {
    }

    static void* operator new(size_t size);
};

struct zUICustomConditionallyDisabled : zUICustom
{
    _zConditional* m_pCondition;
    U8 m_bCondition;

    zUICustomConditionallyDisabled(zUI* ui);
    U8 PreRender();
    U8 PreAutoMenu();
    void PreUpdate(F32 dt);
};

struct zUICustomImageSlideshow : zUICustom
{
    U32 m_CurrentImage;
    U32 m_NumImages;
    xGroup* m_TextureGroup;

    zUICustomImageSlideshow(zUI* ui);
    void UpdateTexture();
    void SetTextureList(xGroup* textureList);
    U8 HandleEvent(xBase* from, U32 toEvent, const F32* toParam, xBase* toParamWidget,
                   U32 toParamWidgetID);
    void Reset();
};

enum eExtrasScreenCheats
{
    eExtrasScreenCheats_AltTargetHand = 0x1,
    eExtrasScreenCheats_AlternateHand,
    eExtrasScreenCheats_AltCarHorn,
    eExtrasScreenCheats_LongAntenna,
    eExtrasScreenCheats_AntennaFountain,
    eExtrasScreenCheats_TinyFoggers,
    eExtrasScreenCheats_MagneticPickups,
    eExtrasScreenCheats_MAX
};

// Member functions follow the original source order (the PS2 DWARF lists them reversed);
// ResetAura is GC-only.
struct zUICustomExtrasManager : zUICustom
{
    U8 mCurRow;
    U8 mCurColumn;
    U8 mNumRows;
    U8 mNumColumns;
    F32 mOffsetX;
    F32 mOffsetY;
    U8 mbInSubScreen;
    zUI** mGrid;
    zUI** mIconTypes;
    xGroup* mAuraGroup;
    zUIText* mDescription;
    zUIText* mActionText;
    zUIText* mNextExtraText;
    xGroup* mNextExtraGroup;
    xSoundFX* mSoundMove;
    xSoundFX* mSoundDeny;
    xSoundFX* mSoundConfirm;
    zUICustom** mSubScreens;

    static U8 GetNumChestsNeeded();
    static U32 IsCostumeEnabled(eSNDCurrentPlayer player);
    static U32 IsCheatEnabled(eExtrasScreenCheats cheat);
    static U32 GivePlayerTreasureChest(U32 numChests);
    static void ResetExtrasStatus();
    static void InitExtrasList(xIniFile* ini);
    zUICustomExtrasManager(zUI* ui);
    void InitScreen();
    void EnterScreen();
    void ResetAura();
    void MoveSelection(U8 row, U8 column, bool playSound);
    void UpdateText();
    void SelectCurrentIcon();
    U8 HandleEvent(xBase* from, U32 toEvent, const F32* toParam, xBase* toParamWidget,
                   U32 toParamWidgetID);
    void PostUpdate(F32 dt);
};

#endif
