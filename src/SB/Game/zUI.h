#ifndef ZUI_H
#define ZUI_H

#include "xGroup.h"
#include "xIni.h"
#include "xMgr.h"
#include "zConditional.h"
#include "zEnt.h"
#include "zTextBox.h"
#include "zUICustom.h"
#include "zWadUI.h"
#include "xstransvc.h"

struct zUIAsset : xDynAsset
{
    F32 x;
    F32 y;
    F32 z;
    F32 width;
    F32 height;
    S32 flags;
    U32 color;
    U32 selectedMotion;
    U32 unselectedMotion;
    U8 brightness;
    U8 pad[3];
    U32 autoMenuUp;
    U32 autoMenuDown;
    U32 autoMenuLeft;
    U32 autoMenuRight;
    U32 custom;
    U32 customWidget;
};

struct zUIMotionAsset : xBaseAsset
{
    U8 cmdCount;
    U8 in;
    U8 pad[2];
    U32 cmdSize;
    F32 totalTime;
    F32 loopTime;
};

struct zUIMotionCmdAsset
{
    U32 type;
    F32 startTime;
    F32 endTime;
    F32 accelTime;
    F32 decelTime;
    U8 enabled;
};

struct zUIMotionCmdMove : zUIMotionCmdAsset
{
    F32 distX;
    F32 distY;
};

struct zUIMotionCmdScale : zUIMotionCmdAsset
{
    F32 amountX;
    F32 amountY;
    U8 centerPivot;
    F32 centerOffsetX;
    F32 centerOffsetY;
};

struct zUIMotionCmdRotate : zUIMotionCmdAsset
{
    F32 rotation;
    F32 centerOffsetX;
    F32 centerOffsetY;
};

struct zUIMotionCmdOpacity : zUIMotionCmdAsset
{
    U8 startOpacity;
    U8 endOpacity;
};

struct zUIMotionCmdAbsoluteScale : zUIMotionCmdAsset
{
    F32 startX;
    F32 startY;
    F32 endX;
    F32 endY;
    U8 centerPivot;
    U8 textScale;
};

struct zUIMotionCmdBrightness : zUIMotionCmdAsset
{
    U8 startBrightness;
    U8 endBrightness;
};

struct zUIMotionCmdColor : zUIMotionCmdAsset
{
    U8 startRed;
    U8 startGreen;
    U8 startBlue;
    U8 endRed;
    U8 endGreen;
    U8 endBlue;
};

struct zUIMotionCmdUVScroll : zUIMotionCmdAsset
{
    F32 amountU;
    F32 amountV;
};

struct zUI;

struct zUIMotionFrame
{
    F32 offsetX;
    F32 offsetY;
    F32 scaleX;
    F32 scaleY;
    F32 centerScaleX;
    F32 centerScaleY;
    F32 textScaleX;
    F32 textScaleY;
    F32 rotation;
    F32 offsetU;
    F32 offsetV;
    xColor_tag color;
    U8 brightness;
};

struct State
{
    F32 x;
    F32 y;
    F32 width;
    F32 height;
    xColor_tag color;
    U8 brightness;
    U8 pad[3];
};

namespace
{
    // Defined in zUI.cpp.
    extern void* (*staticAllocator)(U32, void*);
    extern void* staticUser;
    extern void (*signalHandler)(zUI*, U32, U32, const F32*, xBase*, U32, void*);
    extern void* signalUser;
} // namespace

struct zUI : xBase
{
    zUIAsset* asset;
    State current;
    State startMovement;
    F32 z;
    zUIMotionAsset* selectedMotion;
    zUIMotionAsset* unselectedMotion;
    zUICustom* custom;
    U8 visible;
    U8 focus;
    U8 lastFocus;
    U8 selected;
    U8 brighten;
    U8 hdrPass;
    U8 locked;

    // No GC symbol (always inlined).
    zUIAsset* GetAsset() const
    {
        return asset;
    }

    // vtable at 0x5C (__vt__3zUI slot order)
    virtual U32 GetSortKey() const = 0;
    virtual U8 Blends() const;
    virtual U8 IsUIText()
    {
        return 0;
    }
    virtual void DoInit();
    virtual void DoSetup();
    virtual void DoReset();
    virtual void DoResetMotion();
    virtual void DoHandleEvent(xBase* from, U32 toEvent, const F32* toParam, xBase* toParamWidget,
                               U32 toParamWidgetID);
    virtual void DoRender() const = 0;
    virtual void DoUpdate(F32 dt, U32 buttonsPressed);
    virtual void DoExit();
    virtual void DoInitMotion();
    virtual void DoApplyMotionFrame(const zUIMotionFrame* frame);
    virtual void FocusOn();
    virtual void FocusOff();
    virtual void Select();
    virtual void Unselect();
    virtual void Visible();
    virtual void Invisible();

    zUIMotionAsset* motion;
    F32 motionTime;
    U8 motionFiredEvent;
    U8 motionLoop;

    static U8 bAllowInput;

    void AutoMenu(U32 zUIAsset::* item, U32 event);
    void ApplyMotion();
    F32 CalcAcceleratedDistance(F32 totalDistance, F32 startTime, F32 endTime, F32 accelTime,
                                F32 decelTime);
    void RunMotion(zUIMotionAsset* asset, bool loop, bool reset);
    void Signal(U32 event)
    {
        Signal(this, event);
    }

    // No GC symbol: always inlined (Signal(U32), AutoMenu).
    void Signal(xBase* from, U32 event)
    {
        if (custom)
        {
            custom->HandleEvent(from, event, NULL, NULL, 0);
        }

        if (!signalHandler)
        {
            for (S32 i = 0; i < linkCount; i++)
            {
                if (link[i].srcEvent == (U16)event)
                {
                    xBase* dest = zSceneFindObject(link[i].dstAssetID);
                    if (dest)
                    {
                        xBase* base = NULL;
                        if (link[i].paramWidgetAssetID)
                        {
                            base = zSceneFindObject(link[i].paramWidgetAssetID);
                            if (!base)
                            {
                                base = (xBase*)xSTFindAsset(link[i].paramWidgetAssetID, NULL);
                            }
                        }

                        zEntEvent(from, 0, dest, link[i].dstEvent, link[i].param, base,
                                  link[i].paramWidgetAssetID, FE_NO);
                    }
                }
            }
        }
        else
        {
            for (S32 i = 0; i < linkCount; i++)
            {
                if (event == link[i].srcEvent)
                {
                    xBase* base = NULL;
                    if (link[i].paramWidgetAssetID)
                    {
                        base = (xBase*)xSTFindAsset(link[i].paramWidgetAssetID, NULL);
                    }

                    signalHandler((zUI*)from, link[i].dstAssetID, link[i].dstEvent, link[i].param,
                                  base, link[i].paramWidgetAssetID, signalUser);
                }
            }
        }
    }

    // No GC symbols: always inlined (zLoadingScreen, zUIMgr).
    void HandleEvent(xBase* from, U32 toEvent, const F32* toParam, xBase* toParamWidget,
                     U32 toParamWidgetID)
    {
        if (custom && !custom->HandleEvent(from, toEvent, toParam, toParamWidget, toParamWidgetID))
        {
            return;
        }

        DoHandleEvent(from, toEvent, toParam, toParamWidget, toParamWidgetID);
    }

    // No GC symbol: inlined into zUI_Init.
    void Init()
    {
        DoInit();

        if (custom)
        {
            custom->Init();
        }
    }

    void Setup()
    {
        DoSetup();

        if (custom)
        {
            custom->Setup();
        }
    }

    void Reset()
    {
        DoReset();

        if (custom)
        {
            custom->Reset();
        }
    }

    // No GC symbol: inlined into zUICustomExtrasManager::ResetAura.
    void ResetMotion()
    {
        DoResetMotion();

        if (custom)
        {
            custom->ResetMotion();
        }
    }

    void Update(F32 dt, U32 buttonsPressed)
    {
        if (custom)
        {
            custom->PreUpdate(dt);
        }

        DoUpdate(dt, buttonsPressed);

        if (custom)
        {
            custom->PostUpdate(dt);
        }
    }

    void Render()
    {
        if (custom && !custom->PreRender())
        {
            return;
        }

        DoRender();

        if (custom)
        {
            custom->PostRender();
        }
    }

    void Exit()
    {
        if (custom)
        {
            custom->Exit();
        }

        DoExit();
    }
};

struct _zUI : zEnt
{
    zUIAsset* sasset;
    U32 uiFlags;
    U32 uiButton;
    U16 preUpdateIndex;
    U16 updateIndex;
};

struct zUIMgr : xMgr
{
    S32 m_preUpdateStart;
    S32 m_preUpdateEnd;
    U32 m_preUpdateMax;
    _zUI** m_preUpdate;
    S32 m_updateStart;
    S32 m_updateEnd;
    U32 m_updateMax;
    _zUI** m_update;

    void PreUpdate(zScene* s, F32 dt);
    void Update(zScene* s, F32 dt);
    void Setup(zScene* s);
    void Touch(_zUI* ui);
    void Remove(_zUI* ui);
    void Remove_PreUpdate(_zUI* ui);
    void Remove_Update(_zUI* ui);
    void Add(_zUI* ui);
    void Add_PreUpdate(_zUI* ui);
    void Add_Update(_zUI* ui);
};

extern S32 bad_card_available;
// Another .comm symbol is here ("uknown_common_bss_symbol").
// Maybe some unused variable that was still linked in?
WAD_COMMON _zUI* sSorted[768];
WAD_COMMON zUIMgr gUIMgr;

void zUI_Init();
void zUI_Init(void* ent, void* asset);
void zUI_Init(_zUI* ent, xEntAsset* asset);
void zUI_Init(zUI* ui, zUIAsset* asset);
void zUI_Save(_zUI* ent, xSerial* s);
void zUI_Load(_zUI* ent, xSerial* s);
void zUI_Reset(_zUI* ent);
void zUI_PreUpdate(_zUI* ent, xScene*, F32);
void zUI_Update(_zUI* ent, xScene*, F32 dt);
void zUIRenderAll();
void zUI_Render(xEnt* ent);
S32 zUIEventCB(xBase*, xBase* to, U32 toEvent, const F32* toParam, xBase*);
void zUI_ParseINI(xIniFile* ini);
S32 zUIPortalEventCB(xBase* from, xBase* to, U32 toEvent, const F32* toParam, xBase* toParamWidget);
void zUI_ScenePortalSetToCurrentLevel(zScene* zsc);
void zUI_ScenePortalInit(zScene* zsc);
void zUI_PortalToKrabs(U32 taskNum);
void zUI_ScenePortalUpdate();
void zUI_ScenePortalSave(xSerial* s);
void zUI_ScenePortalLoad(xSerial* s);

#endif
