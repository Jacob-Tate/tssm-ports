#ifndef XGLOBALS_H
#define XGLOBALS_H

#include "xCamera.h"
#include "xPad.h"
#include "xUpdateCull.h"
#include "iCamera.h"
#include "iTime.h"
#include "zScene.h"
#include "xEnt.h"
#include "zEnt.h"
#include "zNPC.h"

#include <rwcore.h>
#include <rpworld.h>

struct xCamGroup;
struct xCamScreen;

// U8's may be bools
// chars were originally int8

enum sceDemoEndReason
{
    SCE_DEMO_ENDREASON_ATTRACT_INTERRUPTED,
    SCE_DEMO_ENDREASON_ATTRACT_COMPLETE,
    SCE_DEMO_ENDREASON_PLAYABLE_INACTIVITY_TIMEOUT,
    SCE_DEMO_ENDREASON_PLAYABLE_GAMEPLAY_TIMEOUT,
    SCE_DEMO_ENDREASON_PLAYABLE_COMPLETE,
    SCE_DEMO_ENDREASON_PLAYABLE_QUIT,
    SCE_DEMO_ENDREASON_NETCONFIG_REQUEST,
    SCE_DEMO_ENDREASON_NETCONFIG_COMPLETE
};

// struct zPlayer
// {
//     // Why empty? (iFMV dwarf shows this)
//     // Non-empty ones exist in the zWads
// };

struct zInteraction;

// PS2 DWARF (zWad3): playerTalkData, size 0x10
struct TalkData
{
    S32 anim;
    iSndGroupHandle sndid;
    void* data;
    F32 time;
};

// Polymorphic in TSSM: virtual order per __vt__7zPlayer. CW places the vtable pointer at the
// first virtual declaration, so it sits at 0xDC, before trigLastFramePos (0xE0 in
// zPlayerSB::Reset, zPlayerFlags 0xEC in SetEnemyIsNear).
struct zPlayer : zEnt
{
    virtual void Init(xEntAsset* asset);
    virtual void SceneSetup()
    {
    }
    virtual void BoundUpdate(xVec3* pos) = 0;
    virtual U8 AllowInteraction(zInteraction* interaction)
    {
        return 1;
    }
    virtual void AttackHit(bool hit)
    {
    }
    virtual xEnt* GetGrabbed()
    {
        return NULL;
    }
    virtual void BeginUpdate(F32 dt) = 0;
    virtual void Update(F32 dt) = 0;
    virtual void EndUpdate(F32 dt) = 0;
    virtual void Move(xScene* sc, F32 dt, xEntFrame* frame) = 0;
    virtual void Render() = 0;
    virtual void HandleEvent(xBase* from, U32 toEvent, const F32* toParam, xBase* toParamWidget,
                             U32 toParamWidgetID) = 0;
    virtual void Reset() = 0;
    virtual U8 IsDead() = 0;
    virtual void Exit() = 0;
    virtual U8 Damage(xBase* src, S32 damage, zHitSource source, zHitTarget target,
                      const xVec3* hitPos);
    virtual void PostRenderCleanup()
    {
    }
    virtual void RenderCustomUI()
    {
    }
    virtual U8 CanTakeDamage() const
    {
        return 0;
    }
    virtual void LoadCheckPoint()
    {
    }
    virtual void StoreCheckPoint()
    {
    }
    virtual void SetCamera()
    {
    }
    virtual void RenderEffects()
    {
    }
    virtual void SetEnemyIsNear(bool isit);

    xVec3 trigLastFramePos;
    S32 zPlayerFlags;
    U32 lorezModelID;
    xModelInstance* lorezModel;
    xModelInstance* hirezModel;

    static TalkData playerTalkData;

    void CollideTrigger(xScene* sc);
    static U32 TalkCheck(xAnimTransition* tran, xAnimSingle*, void*);
    static U32 TalkDoneCheck(xAnimTransition* tran, xAnimSingle*, void*);
    void SpeakStop();
    void SpeakStart(iSndGroupHandle hSoundGroup, S32 anim);
    static void zPlayerEventCB(xBase* from, xBase* to, U32 toEvent, const F32* toParam,
                               xBase* toParamWidget, U32 toParamWidgetID);
};

struct PS2DemoGlobals
{
    U16 language;
    U16 aspect;
    U16 play_mode;
    U16 inactive_timeout;
    U16 gameplay_timeout;
    sceDemoEndReason exit_code;
    struct
    {
        U32 FMV_playing : 1;
        U32 more_padding : 31;
    };
    F32 bail_timer;
    F32 inactive_timer;
    F32 gameplay_timer;
    s8 subdir[16];
    U16 quit; // 0x30
    U16 vmode;
};

struct xGlobals
{
    xCamera oldSkoolCamera;
    xCamGroup* cam;
    xCamScreen* screen;
    xVec4 frustplane[12];
    // The GC build overlays pad0-pad3 and pad[4] (target: pad0 at 0x3F8, updateMgr at
    // 0x70C, currWorld at 0x734, sceneCur at 0x7F8).
    union
    {
        struct
        {
            _tagxPad* pad0; // 0x3f8
            _tagxPad* pad1; // 0x3fc
            _tagxPad* pad2;
            _tagxPad* pad3;
        };
        _tagxPad* pad[4];
    };
    S32 profile;
    char profFunc[6][128];
    xUpdateCullMgr* updateMgr;
    S32 sceneFirst;
    char sceneStart[32];
    RpWorld* currWorld; // 0x734
    iFogParams fog;
    iFogParams fogA;
    iFogParams fogB;
    S64 fog_t0;
    S64 fog_t1;
    S32 option_vibration;
    S32 option_subtitles;
    U32 slowdown;
    F32 update_dt;
    S16 ForceCinematic;
    S16 ForceAllCinematics;
    S32 useHIPHOP;
    U8 NoMusic;
    U8 NoCutscenes;
    U8 NoPadCheck;
    char currentActivePad;
    bool firstStartPressed;
    U8 fromLauncher;
    U8 FlashWIP;
    U8 inLoadingScreen;
    U8 LoadingScene;
    U8 InitializingLoadingScreen;
    U8 ForceMono;
    U32 minVSyncCnt;
    U8 dontShowPadMessageDuringLoadingOrCutScene;
    U8 autoSaveFeature;
    S32 asyncLoadingScreen;
    S32 asyncLoadingFlags;
    char fromLauncherUser[32];
    zPlayer* ___player_ent_dont_use_directly; // 0x7f4
    zScene* sceneCur; // 0x7f8
    zScene* scenePreload; // 0x7fc
    PS2DemoGlobals* PS2demo;
    char watermark[127];
    U8 watermarkAlpha;
    F32 watermarkSize;
};

#endif
