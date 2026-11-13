#include "zBusStop.h"

#include "xEvent.h"
#include "xMarkerAsset.h"
#include "xString.h"
#include "xstransvc.h"

#include "zEntCruiseBubble.h"
#include "zEntPlayer.h"
#include "zGlobals.h"
#include "zScene.h"
#include "zTextBox.h"

U32 gBusStopIsRunning;
S32 gBusStopIsWaiting;
static ztextbox* sBusStopUI;

inline void zBusStop_Init(zBusStop* bstop, busstop_asset* asset);

void zBusStop_Init(xBase& data, xDynAsset& asset, size_t)
{
    zBusStop_Init((zBusStop*)&data, (busstop_asset*)&asset);
}

inline void zBusStop_Init(zBusStop* bstop, busstop_asset* asset)
{
    xBaseInit(bstop, asset);
    bstop->basset = asset;
    bstop->eventFunc = zBusStopEventCB;

    if (bstop->linkCount)
    {
        bstop->link = (xLinkAsset*)(asset + 1);
    }
    else
    {
        bstop->link = NULL;
    }

    U32 size;
    xMarkerAsset* marker = (xMarkerAsset*)xSTFindAsset(asset->marker, &size);
    if (marker != NULL && size == sizeof(xMarkerAsset))
    {
        xVec3Copy(&bstop->pos, &marker->pos);
        bstop->prevState = 0;
        bstop->currState = 0;
        gBusStopIsWaiting = 0;
    }
}

void zBusStop_Setup(zBusStop* bstop)
{
    xBase* simpPtr = zSceneFindObject(bstop->basset->cameraID);
    bstop->bus = (zEnt*)zSceneFindObject(bstop->basset->busID);
    bstop->switchTimer = -1.0f;
    sBusStopUI = (ztextbox*)zSceneFindObject(xStrHash("MESSAGE_02_TEXTBOX"));
}

void zBusStop_Update(xBase* to, xScene*, F32 dt)
{
    zBusStop* bstop = (zBusStop*)to;
    zEnt* player = xglobals->___player_ent_dont_use_directly;

    F32 dx__ = player->bound.box.center.x - bstop->pos.x;
    F32 dy__ = player->bound.box.center.y - bstop->pos.y;
    F32 dz__ = player->bound.box.center.z - bstop->pos.z;
    if (SQR(dx__) + SQR(dy__) + SQR(dz__) < SQR(1.3f))
    {
        if (bstop->currState == 0)
        {
            bstop->currState = 1;
        }
    }
    else
    {
        bstop->currState = 0;
    }

    switch (bstop->currState)
    {
    case 0:
        break;

    case 1:
        if (globals.player.JumpState == 0 && !cruise_bubble::active() &&
            globals.pad0->pressed & 0x1000)
        {
            bstop->currState = 2;
            zEntPlayer_SNDStop(ePlayerSnd_Bus);
            zEntPlayer_SNDPlay(ePlayerSnd_Bus, true);
        }

        // Retail tests the ztextbox with xEntIsVisible, hence the cast.
        if (cruise_bubble::active())
        {
            zEntEvent("MESSAGE_02_TEXTBOX", eEventInvisible);
        }
        else if (sBusStopUI != NULL && !xEntIsVisible((xEnt*)sBusStopUI))
        {
            sBusStopUI->set_text("{i:MNUC BUSSTOP TXT}");
            zEntEvent("MESSAGE_02_TEXTBOX", eEventVisible);
        }
        break;

    case 2:
        break;

    case 3:
        xAnimSingle* single = bstop->bus->model->Anim->Single;
        if (single->Time < 0.1f || single->LastTime < 0.1f)
        {
            bstop->currState = 1;
        }
        break;
    }

    if (bstop->prevState != bstop->currState)
    {
        switch (bstop->currState)
        {
        case 0:
            zEntEvent("MESSAGE_02_TEXTBOX", eEventInvisible);
            gBusStopIsWaiting = 0;
            break;

        case 1:
            zEntEvent(bstop->basset->busID, eEventSpawnBubblesOff);
            sBusStopUI->set_text("{i:MNUC BUSSTOP TXT}");
            zEntEvent("MESSAGE_02_TEXTBOX", eEventVisible);
            zEntPlayerControlOn(CONTROL_OWNER_BUS_STOP);
            zEntEvent(bstop->basset->cameraID, eEventEndConversation);
            gBusStopIsRunning = 0;
            gBusStopIsWaiting = 1;
            break;

        case 2:
            zEntEvent(bstop->basset->busID, eEventSpawnBubblesOn, 0.0f, 0.0f, 0.0f, 0.0f);
            zEntEvent("MESSAGE_02_TEXTBOX", eEventInvisible);
            zEntPlayerControlOff(CONTROL_OWNER_BUS_STOP);
            zEntEvent(bstop->basset->cameraID, eEventStartConversation);
            zEntEvent(bstop->basset->cameraID, eEventSwitch, 0.5f, 0.0f, 0.0f, 0.0f);
            zEntEvent(bstop->basset->busID, eEventAnimPlay, 2.0f, 0.0f, 0.0f, 0.0f);
            bstop->switchTimer = bstop->basset->delay;
            gBusStopIsRunning = 1;
            gBusStopIsWaiting = 0;
            break;

        case 3:
            if (gCurrentPlayer == eCurrentPlayerSpongeBob)
            {
                gCurrentPlayer = eCurrentPlayerPatrick;
            }
            else
            {
                gCurrentPlayer = eCurrentPlayerSpongeBob;
            }
            zEntPlayer_SNDPlayStream(ePlayerStreamSnd_StartingOut);
            zEntEvent(bstop, eEventBusStopSwitchChr);
            break;
        }
    }

    bstop->prevState = bstop->currState;
    if (0.0f == bstop->switchTimer && bstop->currState == 2)
    {
        bstop->currState = 3;
    }

    if (bstop->switchTimer > 0.0f)
    {
        bstop->switchTimer -= dt;
        if (bstop->switchTimer < 0.0f)
        {
            bstop->switchTimer = 0.0f;
        }
    }
}

void zBusStopEventCB(xBase*, xBase* to, U32 toEvent, const F32*, xBase*, U32)
{
    zBusStop* bstop = (zBusStop*)to;

    switch (toEvent)
    {
    case eEventReset:
        break;
    // Retail has an empty case range starting here.
    case eEventPadPressL1:
    case eEventPadPressL2:
        break;
    case eEventBusStopSwitchChr:
        break;
    case eEventSceneReset:
        zEntEvent(bstop->basset->busID, eEventSpawnBubblesOff);
        break;
    }
}
