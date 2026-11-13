#include "zTaxi.h"

#include "xEnt.h"
#include "xEvent.h"
#include "xMath3.h"
#include "xString.h"
#include "iCollide.h"

#include "zEntPlayer.h"
#include "zGlobals.h"
#include "zScene.h"
#include "zTalkBox.h"

// No GC symbol (inlined into zSceneSetup and dead-stripped); a real function in the PS2 build.
void zTaxi_Setup(zTaxi* taxi)
{
    zSceneFindObject(taxi->basset->cameraID);
    zSceneFindObject(taxi->basset->portalID);
    zSceneFindObject(taxi->basset->talkBoxID);
    xSTFindAsset(taxi->basset->textID, NULL);

    taxi->taxi = (zEnt*)zSceneFindObject(taxi->basset->taxiID);
    taxi->portalTimer = -1.0f;
    taxi->baseFlags |= (U16)2;
}

void zTaxi_Update(xBase* to, xScene*, F32 dt)
{
    zTaxi* taxi = (zTaxi*)to;
    xCylinder c;
    xIsect check;
    ztalkbox* tb;

    xVec3Copy(&c.center, &taxi->pos);
    c.r = 1.8f;
    c.h = 5.0f;

    iCylinderIsectVec(&c, (xVec3*)&xglobals->___player_ent_dont_use_directly->model->Mat->pos,
                      &check);

    if (check.penned == -1.0f && xBaseIsEnabled(taxi))
    {
        if (taxi->currState == 0)
        {
            taxi->currState = 1;
        }
    }
    else
    {
        taxi->currState = 0;
    }

    switch (taxi->currState)
    {
    case 1:
        if (gCurrentPlayer == eCurrentPlayerSpongeBob && globals.player.floor_surf == NULL)
        {
            if (globals.pad0->pressed & 0x10000)
            {
                taxi->currState = 3;
                zEntPlayer_SNDPlay(ePlayerSnd_Taxi, true);
            }

            if (globals.pad0->pressed & XPAD_MOVE(0xc0000))
            {
                taxi->currState = 2;
            }
        }
        break;
    case 2:
        if (globals.pad0->released & XPAD_MOVE(0xc0000))
        {
            zEntPlayerControlOn(CONTROL_OWNER_TAXI);
        }
        break;
    case 3:
    case 5:
        break;
    }

    if (taxi->prevState != taxi->currState)
    {
        switch (taxi->currState)
        {
        case 0:
            zEntEvent(taxi->basset->talkBoxID, eEventEndConversation);
            break;
        case 1:
            tb = (ztalkbox*)zSceneFindObject(taxi->basset->talkBoxID);
            tb->start_talk(taxi->basset->textID, NULL, NULL);

            if (gCurrentPlayer == eCurrentPlayerSpongeBob)
            {
                zEntPlayerControlOff(CONTROL_OWNER_TAXI);
                zEntEvent(taxi->basset->cameraID, eEventEndConversation);
                zEntEvent(taxi->basset->taxiID, eEventInvisible);
                tb->set_text(taxi->basset->textID);
            }
            else
            {
                tb->set_text(xStrHash("MNU5 TAXI WARNING TXT"));
            }
            break;
        case 2:
            zEntEvent(taxi->basset->talkBoxID, eEventEndConversation);
            break;
        case 3:
            zEntEvent(taxi->basset->talkBoxID, eEventEndConversation);
            zEntEvent(taxi->basset->cameraID, eEventStartConversation);
            zEntEvent(taxi->basset->taxiID, eEventVisible);
            taxi->invTimer = taxi->basset->invDelay;
            taxi->portalTimer = taxi->basset->portalDelay;
            break;
        case 4:
            xEntHide(xglobals->___player_ent_dont_use_directly);
            break;
        case 5:
            zEntEvent(taxi->basset->portalID, eEventTeleportPlayer);
            break;
        }
    }

    taxi->prevState = taxi->currState;

    if (0.0f == taxi->invTimer && taxi->currState == 3)
    {
        taxi->currState = 4;
    }

    if (0.0f == taxi->portalTimer)
    {
        taxi->currState = 5;
    }

    if (taxi->portalTimer > 0.0f)
    {
        taxi->portalTimer -= dt;
        if (taxi->portalTimer < 0.0f)
        {
            taxi->portalTimer = 0.0f;
        }
    }

    if (taxi->invTimer > 0.0f)
    {
        taxi->invTimer -= dt;
        if (taxi->invTimer < 0.0f)
        {
            taxi->invTimer = 0.0f;
        }
    }
}
