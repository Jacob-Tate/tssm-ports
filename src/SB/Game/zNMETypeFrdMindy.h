#ifndef ZNMETYPEFRDMINDY_H
#define ZNMETYPEFRDMINDY_H

#include "zNMETypeFriendly.h"
#include "xJaw.h"
#include "xRand.h"
#include "xSnd.h"
#include "zCam.h"
#include "zGlobals.h"

struct ztextbox;
struct ztaskbox;
struct NMESysEvent;
struct xAnimTransition;
struct xAnimSingle;

enum eMindyState
{
    eOutofRange,
    eInRange,
    eTalking,
    eFinishedTalking
};

enum eShellState
{
    eShellClosed,
    eShellOpening,
    eShellOpened,
    eShellClosing
};

enum eTVStaticState
{
    eTVStatic,
    eTVStaticMindyAppear,
    eTVStaticMindyAppearFadeInStart,
    eTVStaticMindyAppearFadeInEnd,
    eTVStaticMindyDisappear,
    eTVStaticMindyDisappearFadeOutStart
};

struct zNMEAssetMindy : zNMEAsset
{
    U32 taskBoxID;
    F32 clamOpenDistance;
    F32 clamCloseDistance;
    U32 textBoxID;
    U32 primaryCharacter;
    U32 secondaryTaskBoxID;
};

struct TVStaticTimes
{
    F32 time;
    U8 tvStatic1;
    U8 tvStatic2;
};

struct MindyTalkData
{
    S32 anim;
    iSndGroupHandle sndid;
    void* data;
    F32 time;
};

struct zNMEMindy : zNMEFriendly
{
    zNMEMindy()
    {
    }
    eMindyState currentState; // 0x1C0
    eShellState currentShellState;
    eTVStaticState currentTVStaticState;
    F32 clamOpenDistanceSqXZ;
    F32 clamCloseDistanceSqXZ;
    F32 updateCullDistanceXZ;
    F32 updateCullDistanceSqXZ;
    zNMEAssetMindy* mindyAsset;
    ztextbox* textBox;
    ztaskbox* taskBox;
    ztaskbox* secondaryTaskBox;
    U32 mindyModelID;
    MindyTalkData talkData;
    U8 celebrationAnimPlay;
    F32 tvStaticTimer;
    S32 tvStaticTimeIndex;
    U8 makeMindyAppear;
    U8 makeMindyDisappear;
    F32 yaw;
    F32 pitch;
    F32 roll;
    F32 yawAmplitude;
    F32 yawFrequency;
    F32 pitchAmplitude;
    F32 pitchFrequency;
    F32 rollAmplitude;
    F32 rollFrequency;
    F32 wiggleTimer;
    F32 wiggleAmplitude;
    F32 wiggleFrequency;
    F32 quatT;
    RwMatrixTag matCopy; // 0x244
    F32 goingDownTimeMultiplier;
    F32 offsetY;
    F32 offsetYTimer;
    F32 goingUpTimeMultiplier;
    F32 fadeStep;

    U8 ColChkFlags() const
    {
        return 0;
    }
    U8 ColPenFlags() const
    {
        return 0;
    }
    U8 ColChkByFlags() const
    {
        return XENT_COLLTYPE_PLYR;
    }
    U8 ColPenByFlags() const
    {
        return XENT_COLLTYPE_PLYR;
    }
    U8 PhysicsFlags() const
    {
        return 0;
    }

    // Source order (the PS2 DWARF lists them reversed); PostSetup is the key function.
    S32 WannaPoolShadow()
    {
        return 2;
    }
    void PostSetup();
    void Init(xEntAsset* asset);
    void Setup();
    void Reset();
    void Destroy();
    static char* mindyAnims[13];
    static U32 hashMindyAnims[13];
    static TVStaticTimes tvStaticTimes[7];
    static TVStaticTimes mindyAppearTimes[7];
    static TVStaticTimes* currentStaticTimes;
    static F32 oldMusicVolume;

    static U32 SpatulaGrabCB(xAnimTransition* tran, xAnimSingle* anim, void* object);
    static U32 SpatulaGrabStopCB(xAnimTransition* tran, xAnimSingle* anim, void* object);
    static U32 SpatulaGrabCheck(xAnimTransition* tran, xAnimSingle* anim, void* object);
    static U32 OpenShellCheck(xAnimTransition* tran, xAnimSingle* anim, void* object);
    static U32 OpenShellDone(xAnimTransition* tran, xAnimSingle* anim, void* object);
    static U32 CloseShellCheck(xAnimTransition* tran, xAnimSingle* anim, void* object);
    static U32 CloseShellDone(xAnimTransition* tran, xAnimSingle* anim, void* object);
    xAnimTable* CreateAnimTable(xAnimTable* table);
    void UpdateTVStatic(F32 dt);
    void BUpdate(xVec3* pos);
    void Process(F32 dt);
    void Render();
    S32 TypeHandleMail(NMEMsg* mail);
    S32 MindySysMail(NMESysEvent* sysmail);

    // No GC symbol (always inlined into MindySysMail); the name is not known. Looks up the jaw
    // data of the talk sound and starts a talk animation (anim -1 picks a random one).
    void TalkStart(S32 anim, iSndGroupHandle sndid)
    {
        talkData.data = xJaw_FindData(xSndMgrGetSoundId(sndid, 0));
        if (talkData.data != NULL)
        {
            if (anim == -1)
            {
                anim = (xrand_RandomInt32() >> 13) & 3;
            }
            talkData.anim = anim;
            talkData.sndid = sndid;
            talkData.time = 0.0f;
            AnimStart(hashMindyAnims[9 + talkData.anim], 0);
        }
    }

    bool IsTalking();

    // No GC symbol (always inlined into Process); the name is not known. Whether the player is
    // within the clam open distance (on XZ) and within 5 units vertically of Mindy.
    bool PlayerInRange()
    {
        xVec3* pos = &xglobals->___player_ent_dont_use_directly->frame->mat.pos;
        F32 dx = mindyAsset->ent_asset.pos.x - pos->x;
        F32 dz = mindyAsset->ent_asset.pos.z - pos->z;
        F32 dstSq = dx * dx + dz * dz;
        bool inY = pos->y <= mindyAsset->ent_asset.pos.y + 5.0f &&
                   pos->y >= mindyAsset->ent_asset.pos.y - 5.0f;

        return dstSq <= clamOpenDistanceSqXZ && inY;
    }

    // No GC symbol (always inlined into Process); the name is not known. Whether the camera is
    // farther than the given distance (on XZ) or more than 5 units above or below Mindy (never
    // while driving the car).
    bool CameraOutOfRange(F32 distSqXZ)
    {
        xVec3* pos = &zCamGetMatrix().pos;
        F32 dx = mindyAsset->ent_asset.pos.x - pos->x;
        F32 dz = mindyAsset->ent_asset.pos.z - pos->z;
        F32 dstSq = dx * dx + dz * dz;
        bool inY = pos->y <= mindyAsset->ent_asset.pos.y + 5.0f &&
                   pos->y >= mindyAsset->ent_asset.pos.y - 5.0f;

        return (dstSq >= distSqXZ || !inY) && gCurrentPlayer != eCurrentPlayerCar;
    }

    // No GC symbol (always inlined into MindySysMail and Process); the name is not known. Ends
    // the talk animation and goes back to an idle one.
    void TalkStop()
    {
        if (talkData.anim != -1)
        {
            talkData.anim = -1;
            model->Anim->Single->BilinearLerp[0] = 0.0f;

            U32 idle = (xrand_RandomInt32() >> 13) & 3;
            if (itemType != NME_TYPE_MINDY)
            {
                AnimStart(hashMindyAnims[2 + idle], 0);
            }
            else
            {
                AnimStart(hashMindyAnims[8], 0);
            }
        }
    }
};

#endif
