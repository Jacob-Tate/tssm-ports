#ifndef ZRINGCONTROL_H
#define ZRINGCONTROL_H

#include "xBase.h"
#include "xDynAsset.h"
#include "xMath3.h"
#include "xModel.h"
#include "xSnd.h"

#include "zRing.h"

#include <rwcore.h>
#include <rpworld.h>

enum eRingControlState
{
    eRingControlIdle,
    eRingControlStart,
    eRingControlRun
};

enum eRingPlayer
{
    ePattyWagon,
    eSpongeBobOrPatrick,
    eSpongeBall,
    eToungeBoarding,
    eSpongePatSlide,
    eCruiseBubble
};

enum eRingSound
{
    eRingSndAppear,
    eRingSndSuccess,
    eRingSndFailed,
    eRingSndWarning,
    eRingSndCount
};

// Size: 0x3C
struct zRingControlType : xDynAsset
{
    S32 player;
    U32 modelForRings;
    F32 defaultWarningTime;
    U32 ringCount;
    U32 notUsedOffset;
    U32 sounds[eRingSndCount];
    U32 numNextRingsToShow;
    U32 ringList; // first of ringCount ring IDs
};

// Size: 0x380
class zRingControl : xBase
{
public:
    zRingControlType* ringControlAsset;
    RpAtomic* modelForRings;
    F32 defaultWarningTime;
    U32* ringHashList;
    U32 ringCount;
    S32 activeRing;
    S32 previousActiveRing;
    zRing* ringList[200];
    U32 numNextRingsToShow;
    iSndHandle sndHandle;
    xModelInstance* modelInstances[5];
    eRingPlayer player;
    eRingControlState state;
    // Written by the const ShowNextRings.
    mutable xVec3 lastPosChecked;
    mutable U8 isLastPosValid;

    static zRingControl* ringControlForScene;

    static U8 IsRingChallengeActive();
    xMat4x3* GetPlayerToCheck() const;
    void ShowNextRings() const;

    void Render();
    static void RenderAll();

    // GetSoundPos and PlaySound have no symbol and no PS2 entry (the PS2 build inlines the
    // same code). The names are guesses.
    const xVec3* GetSoundPos() const
    {
        if (activeRing >= 0)
        {
            return &ringList[activeRing]->matrix.pos;
        }

        if (state == eRingControlStart)
        {
            return &ringList[0]->matrix.pos;
        }

        return NULL;
    }

    // Plays one of the asset's ring sounds, stopping the previous one.
    void PlaySound(eRingSound sound, const xVec3* pos)
    {
        U32 soundID = ringControlAsset->sounds[sound];

        if (soundID != 0)
        {
            iSndGroupHandle group = xSndMgrGetSoundGroup(soundID);

            if (sndHandle != (iSndHandle)-1)
            {
                xSndMgrStop(sndHandle);
                sndHandle = (iSndHandle)-1;
            }

            if (soundID != 0)
            {
                if (pos != NULL)
                {
                    sndHandle = xSndMgrPlay(group, 0, pos, NULL, NULL, NULL, NULL);
                }
                else
                {
                    sndHandle = xSndMgrPlay(group, 0, NULL, NULL, NULL, NULL, NULL);
                }
            }
        }
    }

    void Update(F32 dt);
    void Reset();
    static void EventHandler(xBase* from, xBase* to, U32 toEvent, const F32* toParam,
                             xBase* toParamWidget, U32 toParamWidgetID);
    static void Load(xBase& data, xDynAsset& asset, size_t);
};

#endif
