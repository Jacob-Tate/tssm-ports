#include "zEntButton.h"

#include <types.h>

#include "zCollGeom.h"
#include "zEntPlayer.h"
#include "zFX.h"
#include "zShrapnel.h"
#include "zGlobals.h"
#include "zStreamedSound.h"
#include "xMath.h"
#include "xMath3.h"
#include "xModel.h"
#include "xVec3Inlines.h"
#include "xRand.h"
#include "xSnd.h"
#include "xString.h"
#include "xstransvc.h"
#include "xUpdateCull.h"

static F32 sRedMultiplier = 1.0f;
static F32 sGreenMultiplier = 1.0f;
static F32 sBlueMultiplier = 1.0f;
static F32 sColorMultiplier = 1.0f;
static S32 sColorMultiplierSign = 1;

// zEntPlayer.cpp's static, defined later in this wad (CW gives the later static definition
// internal linkage). Not standard C++; the original probably reached it through an inlined
// zEntPlayer.cpp helper.
extern zStreamedSoundList sPlayerOneLiners[4];

void zEntButtonEventCB(xBase* from, xBase* to, U32 toEvent, const F32* toParam,
                       xBase* toParamWidget, U32 toParamWidgetID);

static inline void _PressButtonSound(U32 mask)
{
    if (mask & 0x8)
    {
        zEntPlayer_SNDPlayStream(ePlayerStreamSnd_BowlComment1);
    }
    else if (!(mask & 0x800) && xrand_RandomInt32() % 4 == 3)
    {
        zStreamedSoundList* list = &sPlayerOneLiners[gCurrentPlayer];
        if (ePlayerStreamSnd_PushButton < list->mNumSounds)
        {
            list->mSounds[ePlayerStreamSnd_PushButton].PlaySoundDelayed(0.2f);
        }
    }
}

void zEntButton_Init(void* ent, void* asset)
{
    zEntButton_Init((_zEntButton*)ent, (xEntAsset*)asset);
}

void zEntButton_Init(_zEntButton* ent, xEntAsset* asset)
{
    zEntButtonAsset* passet;
    xEntMotionAsset* emasset;
    U32 bufsize;
    void* info;

    zEntInit(ent, asset, 'BUTN');

    passet = (zEntButtonAsset*)(asset + 1);
    emasset = (xEntMotionAsset*)(passet + 1);
    ent->basset = passet;

    info = xSTFindAsset(passet->modelPressedInfoID, &bufsize);
    if (info != NULL)
    {
        ent->modelPressed = xEntLoadModel(NULL, (RpAtomic*)info, NULL);
    }
    else
    {
        ent->modelPressed = NULL;
    }

    if (asset->modelInfoID == xStrHash("button"))
    {
        ent->topHeight = 0.36f;
    }
    else if (asset->modelInfoID == xStrHash("plate_pressure"))
    {
        ent->topHeight = 0.32524f;
    }
    else
    {
        ent->topHeight = 0.0f;
    }

    ent->pflags |= XENT_PFLAGS_IS_MOVING;
    ent->penby |= XENT_COLLTYPE_PLYR;
    ent->chkby |= XENT_COLLTYPE_PLYR;

    ent->update = (xEntUpdateCallback)zEntButton_Update;
    ent->move = (xEntMoveCallback)zEntButton_Move;
    ent->eventFunc = zEntButtonEventCB;
    ent->render = (xEntRenderCallback)zEntButton_Render;

    if (ent->linkCount != 0)
    {
        ent->link = (xLinkAsset*)(emasset + 1);
    }
    else
    {
        ent->link = NULL;
    }

    ent->eventFunc = zEntButtonEventCB;

    ent->state = 0;
    ent->oldState = ent->state;
    ent->holdTimer = 0.0f;
    ent->counter = ent->basset->resetDelay;

    xEntReset(ent);
    xEntMotionInit(&ent->motion, ent, emasset);
    xEntMotionRun(&ent->motion);
    ent->oldMotState = ent->motion.mech.state;
}

void zEntButton_Move(_zEntButton* ent, xScene* s, F32 dt, xEntFrame* frame)
{
    xEntFrame* dframe;

    if (ent->driver != NULL)
    {
        if (ent->driveMode == 0)
        {
            xVec3 dpos;
            dframe = ent->driver->frame;
            xVec3Sub(&dpos, &dframe->mat.pos, &dframe->oldmat.pos);
            xVec3Add(&ent->motion.mech.original_position, &ent->motion.mech.original_position,
                     &dpos);
            xVec3Add(&ent->motion.mech.destination_position, &ent->motion.mech.destination_position,
                     &dpos);
        }
    }

    xEntMotionMove(&ent->motion, s, dt, frame);
}

void zEntButton_Setup(_zEntButton* ent, xScene* sc)
{
    zEntSetup(ent);
    xEntMotionSetup(&ent->motion, sc);
}

void zEntButton_Save(_zEntButton* ent, xSerial* s)
{
    zEntSave(ent, s);

    if (ent->state == 3)
    {
        s->Write_b1(1);
    }
    else
    {
        s->Write_b1(0);
    }

    if (ent->state == 1)
    {
        s->Write_b1(1);
    }
    else
    {
        s->Write_b1(0);
    }
}

void zEntButton_Load(_zEntButton* ent, xSerial* s)
{
    S32 pressed;
    S32 pushing;

    zEntLoad(ent, s);

    pressed = 0;
    pushing = 0;

    s->Read_b1(&pressed);
    if (pressed)
    {
        ent->state = 1;
    }

    s->Read_b1(&pushing);
    if (pushing)
    {
        ent->state = 1;
    }
}

void zEntButton_Reset(_zEntButton* ent, xScene* sc)
{
    xEntReset(ent);
    xEntMotionInit(&ent->motion, ent, (xEntMotionAsset*)(ent->basset + 1));
    xEntMotionReset(&ent->motion, sc);

    ent->pflags |= XENT_PFLAGS_IS_MOVING;
    ent->penby |= XENT_COLLTYPE_PLYR;

    if (ent->asset->flags & 0x1)
    {
        xEntShow(ent);
    }

    ent->chkby |= XENT_COLLTYPE_PLYR;

    ent->bupdate(ent, (xVec3*)&ent->model->Mat->pos);

    ent->state = 0;
    xEntMotionStop(&ent->motion);
    ent->oldState = 5;
    ent->oldMotState = 6;
}

void zEntButton_Update(_zEntButton* ent, xScene* sc, F32 dt)
{
    if (ent->modelPressed != NULL)
    {
        ent->modelPressed->Mat = ent->model->Mat;
    }

    xEntUpdate(ent, sc, dt);

    if (ent->motion.mech.state != ent->oldMotState)
    {
        switch (ent->motion.mech.state)
        {
        case 0:
            if (ent->state == 4)
            {
                ent->state = 0;
            }
            break;
        case 2:
            if (!xEntMotionIsStopped(&ent->motion))
            {
                ent->state = 3;
            }
            break;
        case 3:
            ent->state = 4;
            break;
        case 4:
        case 5:
        case 6:
            break;
        }
    }

    ent->oldMotState = ent->motion.mech.state;

    if (ent->state != ent->oldState)
    {
        switch (ent->state)
        {
        case 1:
            xEntMotionRun(&ent->motion);
            break;
        case 3:
            xEntMotionStop(&ent->motion);
            break;
        case 4:
            xEntMotionRun(&ent->motion);
            break;
        case 0:
            if (ent->motion.mech.state == 0)
            {
                xEntMotionStop(&ent->motion);
            }
            if (ent->basset->actMethod == 1)
            {
                zEntEvent(ent, ent, eEventUnpress);
                xSndMgrPlay(xSndMgrGetSoundGroup(xStrHash("Button_up")), 0,
                            (xVec3*)&ent->model->Mat->pos, NULL, NULL, NULL, NULL);
            }
            break;
        }

        ent->oldState = ent->state;
    }

    if (ent->basset->actMethod == 0)
    {
        if (ent->basset->isReset != 0 && ent->state != 0)
        {
            ent->counter += dt;
        }

        if (ent->counter > ent->basset->resetDelay && ent->state == 3)
        {
            zEntEvent(ent, ent, eEventUnpress);
            xSndMgrPlay(xSndMgrGetSoundGroup(xStrHash("Button_up")), 0,
                        (xVec3*)&ent->model->Mat->pos, NULL, NULL, NULL, NULL);
        }
    }
    else if (ent->basset->actMethod == 1 && ent->hold == 0)
    {
        zEntButton_SetReady(ent);
    }

    ent->holdTimer -= dt;
    if (ent->holdTimer < 0.0f)
    {
        ent->holdTimer = 0.5f;
        ent->hold = 0;
    }
}

void zEntButton_Render(_zEntButton* ent)
{
    xModelInstance* model = ent->model;

    if (ent->state == 3 && ent->modelPressed != NULL)
    {
        ent->modelPressed->LightKit = model->LightKit;
        ent->model = ent->modelPressed;
    }
    else if (ent->basset->actMethod == 0)
    {
        model->RedMultiplier = sRedMultiplier;
        model->GreenMultiplier = sGreenMultiplier;
        model->BlueMultiplier = sBlueMultiplier;
    }

    if (ent->collType != XENT_COLLTYPE_TRIG)
    {
        xEntRender(ent);
    }

    ent->model = model;
}

void zEntButton_Press(_zEntButton* ent, U32 mask)
{
    if (ent->basset->buttonActFlags & mask && ent->state == 0)
    {
        zEntEvent(ent, eEventPress);
        xSndMgrPlay(xSndMgrGetSoundGroup(xStrHash("Button_press")), 0,
                    (xVec3*)&ent->model->Mat->pos, NULL, NULL, NULL, NULL);
        _PressButtonSound(mask);
    }
}

void zEntButton_Hold(_zEntButton* ent, U32 mask)
{
    xVec3* player;
    xBound* bound;
    xVec3 lv;

    if (!(ent->basset->buttonActFlags & mask))
    {
        return;
    }

    if (mask & 0x400)
    {
        switch (ent->bound.type)
        {
        case XBOUND_TYPE_CYL:
            break;
        case XBOUND_TYPE_OBB:
            player = xEntGetPos(xglobals->___player_ent_dont_use_directly);
            bound = &ent->bound;
            xMat4x3Tolocal(&lv, bound->mat, player);
            lv.y = 0.0f;
            if (!xPointInBox(&bound->box.box, &lv))
            {
                return;
            }
            break;
        }
    }

    if (ent->state == 0)
    {
        zEntEvent(ent, eEventPress);
        xSndMgrPlay(xSndMgrGetSoundGroup(xStrHash("Button_press")), 0,
                    (xVec3*)&ent->model->Mat->pos, NULL, NULL, NULL, NULL);
        _PressButtonSound(0);
    }
    else
    {
        ent->hold = 1;
    }
}

void zEntButton_SceneUpdate(F32 dt)
{
    sColorMultiplier += dt * sColorMultiplierSign * 2.5f;

    if (sColorMultiplier > 1.0f)
    {
        sColorMultiplierSign *= -1;
        sColorMultiplier = 1.0f;
    }

    if (sColorMultiplier < 0.0f)
    {
        sColorMultiplierSign *= -1;
        sColorMultiplier = 0.0f;
    }

    // Retail stores red, green, blue in this order; a chained assignment would store blue first.
    sRedMultiplier = 0.39999998f * sColorMultiplier + 0.6f;
    sGreenMultiplier = sRedMultiplier;
    sBlueMultiplier = sRedMultiplier;
}

void zEntButtonEventCB(xBase* from, xBase* to, U32 toEvent, const F32* toParam,
                       xBase* toParamWidget, U32 toParamWidgetID)
{
    _zEntButton* s;
    FloatAndVoid dist;
    zShrapnelAsset* shrap;

    s = (_zEntButton*)to;

    switch (toEvent)
    {
    case eEventCollision_Visible_On:
        s->chkby |= XENT_COLLTYPE_PLYR;
        xEntShow(s);
        s->bupdate(s, (xVec3*)&s->model->Mat->pos);
        if (toParam != NULL && (S32)(*toParam + 0.5f) == 77)
        {
            zFXPopOn(*s, toParam[1], toParam[2]);
        }
        break;
    case eEventCollision_Visible_Off:
        s->chkby &= (U8)~XENT_COLLTYPE_PLYR;
        xEntHide(s);
        if (toParam != NULL && (S32)(*toParam + 0.5f) == 77)
        {
            zFXPopOff(*s, toParam[1], toParam[2]);
        }
        break;
    case eEventVisible:
    case eEventFastVisible:
        xEntShow(s);
        s->bupdate(s, (xVec3*)&s->model->Mat->pos);
        if (toParam != NULL && (S32)(*toParam + 0.5f) == 77)
        {
            zFXPopOn(*s, toParam[1], toParam[2]);
        }
        break;
    case eEventCollisionOn:
        s->chkby |= XENT_COLLTYPE_PLYR;
        break;
    case eEventCollisionOff:
        s->chkby &= (U8)~XENT_COLLTYPE_PLYR;
        break;
    case eEventCollisionReset:
        s->chkby |= XENT_COLLTYPE_PLYR;
        break;
    case eEventInvisible:
    case eEventFastInvisible:
        xEntHide(s);
        if (toParam != NULL && (S32)(*toParam + 0.5f) == 77)
        {
            zFXPopOff(*s, toParam[1], toParam[2]);
        }
        break;
    case eEventCameraCollidePartial:
        zCollGeom_CamEnable(s);
        break;
    case eEventCameraCollideOn:
        zCollGeom_CamEnable(s);
        break;
    case eEventCameraCollideOff:
        zCollGeom_CamDisable(s);
        break;
    case eEventReset:
        zEntButton_Reset(s, globals.sceneCur);
        break;
    case eEventPress:
        zEntButton_Press(s);
        break;
    case eEventUnpress:
        if (s->state != 0)
        {
            zEntButton_SetReady(s);
        }
        break;
    case eEventToggle:
        if (s->state != 0)
        {
            zEntEvent(s, s, eEventUnpress);
            xSndMgrPlay(xSndMgrGetSoundGroup(xStrHash("Button_up")), 0, (xVec3*)&s->model->Mat->pos,
                        NULL, NULL, NULL, NULL);
        }
        else
        {
            zEntEvent(s, s, eEventPress);
            xSndMgrPlay(xSndMgrGetSoundGroup(xStrHash("Button_press")), 0,
                        (xVec3*)&s->model->Mat->pos, NULL, NULL, NULL, NULL);
        }
        break;
    case eEventAnimPlay:
    case eEventAnimPlayLoop:
    case eEventAnimStop:
    case eEventAnimPause:
    case eEventAnimResume:
    case eEventAnimTogglePause:
    case eEventAnimPlayRandom:
    case eEventAnimPlayMaybe:
        zEntAnimEvent(s, toEvent, toParam);
        break;
    case eEventSetUpdateDistance:
        if (globals.updateMgr)
        {
            if (*toParam <= 0.0f)
            {
                xUpdateCull_SetCB(globals.updateMgr, s, xUpdateCull_AlwaysTrueCB, NULL);
            }
            else
            {
                dist.f = SQR(*toParam);
                xUpdateCull_SetCB(globals.updateMgr, s, xUpdateCull_DistanceSquaredCB, dist.v);
            }
        }
        break;
    case eEventLaunchShrapnel:
        shrap = (zShrapnelAsset*)toParamWidget;
        if (shrap && shrap->initCB)
        {
            shrap->initCB(shrap, s->model, NULL, NULL);
        }
        break;
    case eEventSetLightKit:
        xModelSetLightKit(s->model, (xLightKit*)toParamWidget);
        if (s->modelPressed != NULL)
        {
            xModelSetLightKit(s->modelPressed, (xLightKit*)toParamWidget);
        }
        break;
    }
}
