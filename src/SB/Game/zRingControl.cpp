#include "zRingControl.h"

#include "zBase.h"
#include "zEntCruiseBubble.h"
#include "zEntPlayer.h"
#include "zGlobals.h"
#include "zScene.h"

#include "xEvent.h"
#include "xSnd.h"
#include "xstransvc.h"

#include "iModel.h"

#include <stdio.h>
#include <string.h>

zRingControl* zRingControl::ringControlForScene;

void zRingControl::Load(xBase& data, xDynAsset& asset, size_t)
{
    if (ringControlForScene == NULL)
    {
        xBaseInit(&data, &asset);
        data.baseType = eBaseTypeRingControl;
        ((zRingControl&)data).ringControlAsset = (zRingControlType*)&asset;
        data.eventFunc = EventHandler;
        if (data.linkCount != 0)
        {
            data.link = (xLinkAsset*)(&((zRingControl&)data).ringControlAsset->ringList +
                                      ((zRingControl&)data).ringControlAsset->ringCount);
        }

        memset(((zRingControl&)data).ringList, 0, sizeof(((zRingControl&)data).ringList));
        ((zRingControl&)data).Reset();
        ringControlForScene = (zRingControl*)&data;
    }
}

void zRingControl::EventHandler(xBase* from, xBase* to, U32 toEvent, const F32* toParam,
                                xBase* toParamWidget, U32 toParamWidgetID)
{
    zRingControl& ringControl = (zRingControl&)*to;

    switch (toEvent)
    {
    case eEventRingChallengeStart:
        ringControl.state = eRingControlStart;
        ringControl.PlaySound(eRingSndAppear, ringControl.GetSoundPos());
        break;
    case eEventRingChallengeRun:
        ringControl.state = eRingControlRun;
        break;
    case eEventReset:
    case eEventRingChallengeReset:
        ringControl.Reset();
        break;
    case eEventRingChallengeSuccess:
    case eEventRingChallengeFailed:
        ringControl.state = eRingControlIdle;
        break;
    }
}

void zRingControl::Reset()
{
    U32 i;
    char errorMsg[100];
    RpAtomic* tempInstance;

    player = (eRingPlayer)ringControlAsset->player;
    defaultWarningTime = ringControlAsset->defaultWarningTime;
    ringCount = ringControlAsset->ringCount;
    ringHashList = &ringControlAsset->ringList;
    modelForRings = (RpAtomic*)xSTFindAsset(ringControlAsset->modelForRings, NULL);

    for (i = 0; i < ringCount; i++)
    {
        if (ringList[i] != NULL)
        {
            ringList[i]->Reset();
        }
    }

    activeRing = -1;
    previousActiveRing = -1;
    state = eRingControlIdle;
    sndHandle = (iSndHandle)-1;

    for (i = 0; i < ringCount; i++)
    {
        ringList[i] = (zRing*)zSceneFindObject(ringHashList[i]);
        // Message for a ring-not-found assert; retail still formats it.
        sprintf(errorMsg, "Can't find ring with id 0x%0X", ringHashList[i]);

        if (ringList[i]->warningTime < 0.0f)
        {
            ringList[i]->warningTime = defaultWarningTime;
        }

        ringList[i]->SetModel(modelForRings);
    }

    if (modelForRings != NULL)
    {
        for (i = 0; i < 5; i++)
        {
            if (modelInstances[i] != NULL)
            {
                xModelInstanceFree(modelInstances[i]);
            }
            modelInstances[i] = NULL;

            modelInstances[i] = xModelInstanceAlloc(modelForRings, NULL, 0, 0, NULL);
            modelInstances[i]->modelID = ringControlAsset->modelForRings;

            tempInstance = modelForRings;
            while ((tempInstance = iModelFile_RWMultiAtomic(tempInstance)) != NULL)
            {
                xModelInstanceAttach(xModelInstanceAlloc(tempInstance, NULL, 0x2000, 0, NULL),
                                     modelInstances[i]);
            }
        }
    }

    numNextRingsToShow =
        ringControlAsset->numNextRingsToShow > 5 ? 5 : ringControlAsset->numNextRingsToShow;
    isLastPosValid = 0;
}

void zRingControl::ShowNextRings() const
{
    xMat4x3* playerMatrix;

    for (U32 i = 0; i < numNextRingsToShow; i++)
    {
        if (activeRing + i >= ringCount)
        {
            break;
        }

        ringList[activeRing + i]->Show(modelInstances[i]);
    }

    playerMatrix = GetPlayerToCheck();
    if (playerMatrix != NULL)
    {
        lastPosChecked = playerMatrix->pos;
        isLastPosValid = 1;
    }
    else
    {
        isLastPosValid = 0;
    }
}

void zRingControl::Update(F32 dt)
{
    xMat4x3* playerMatrix;

    switch (state)
    {
    case eRingControlIdle:
        activeRing = -1;
        previousActiveRing = -1;
        break;
    case eRingControlStart:
        activeRing = 0;
        ShowNextRings();
        break;
    case eRingControlRun:
        if (activeRing < 0 || activeRing >= (S32)ringCount)
        {
            return;
        }

        ringList[activeRing]->Activate();

        ringList[activeRing]->Update(dt, modelInstances[0]);

        if (ringList[activeRing]->state == eRingWarning)
        {
            PlaySound(eRingSndWarning, GetSoundPos());
        }

        if (ringList[activeRing]->state == eRingIdle ||
            (!cruise_bubble::active() && player == eCruiseBubble))
        {
            previousActiveRing = activeRing;
            activeRing = -1;
            zEntEvent(this, eEventRingChallengeFailed);
            PlaySound(eRingSndFailed, NULL);
            return;
        }

        playerMatrix = GetPlayerToCheck();
        if (playerMatrix != NULL)
        {
            if (!isLastPosValid)
            {
                isLastPosValid = 1;
                lastPosChecked = playerMatrix->pos;
            }

            if (ringList[activeRing]->CheckIfPlayerCrossed(*playerMatrix, lastPosChecked))
            {
                previousActiveRing = activeRing;
                ringList[activeRing]->Hide();
                activeRing++;

                if (activeRing >= (S32)ringCount)
                {
                    zEntEvent(this, eEventRingChallengeSuccess);
                    PlaySound(eRingSndSuccess, NULL);
                    activeRing = -1;
                }
                else
                {
                    ShowNextRings();
                    PlaySound(eRingSndAppear, GetSoundPos());
                    if (player == eCruiseBubble)
                    {
                        cruise_bubble::reset_life();
                    }
                }
            }

            lastPosChecked = playerMatrix->pos;
        }
        else
        {
            isLastPosValid = 0;
        }
        break;
    }
}

// No symbol in TSSM (a real function in the PS2 build): always inlined.
inline xMat4x3* zRingControl::GetPlayerToCheck() const
{
    switch (player)
    {
    case ePattyWagon:
        if (gCurrentPlayer == eCurrentPlayerCar)
        {
            return (xMat4x3*)xglobals->___player_ent_dont_use_directly->model->Mat;
        }
        break;
    case eSpongePatSlide:
        if (gCurrentPlayer == eCurrentPlayerSpongePat)
        {
            return (xMat4x3*)xglobals->___player_ent_dont_use_directly->model->Mat;
        }
        break;
    case eToungeBoarding:
        if (gCurrentPlayer == eCurrentPlayerSpongeBob && (globals.player.SlideTrackSliding & 1))
        {
            return (xMat4x3*)xglobals->___player_ent_dont_use_directly->model->Mat;
        }
        break;
    case eSpongeBall:
        if (gCurrentPlayer == eCurrentPlayerSpongeBob && gSpongeBall)
        {
            return (xMat4x3*)xglobals->___player_ent_dont_use_directly->model->Mat;
        }
        break;
    case eCruiseBubble:
        if (gCurrentPlayer == eCurrentPlayerSpongeBob && cruise_bubble::active())
        {
            return cruise_bubble::GetMissileMat();
        }
        break;
    case eSpongeBobOrPatrick:
        if (gCurrentPlayer == eCurrentPlayerSpongeBob || gCurrentPlayer == eCurrentPlayerPatrick)
        {
            return (xMat4x3*)xglobals->___player_ent_dont_use_directly->model->Mat;
        }
        break;
    }

    return NULL;
}

// No GC symbol: inlined into zSceneRenderPreFX (through RenderAll). The name is ours.
// Draws the active ring and the next ones to show, like ShowNextRings.
void zRingControl::Render()
{
    if (activeRing >= 0)
    {
        for (U32 i = 0; i < numNextRingsToShow; i++)
        {
            if (activeRing + i >= ringCount)
            {
                break;
            }

            ringList[activeRing + i]->Render(modelInstances[i]);
        }
    }
}

// No GC symbol: inlined into zSceneRenderPreFX. The name is ours.
void zRingControl::RenderAll()
{
    zScene& scene = *globals.sceneCur;
    zRingControl* it = (zRingControl*)scene.baseList[eBaseTypeRingControl];
    zRingControl* end = it + scene.baseCount[eBaseTypeRingControl];

    for (; it != end; ++it)
    {
        it->Render();
    }
}

U8 zRingControl::IsRingChallengeActive()
{
    if (ringControlForScene != NULL)
    {
        return ringControlForScene->state != eRingControlIdle;
    }

    return 0;
}
