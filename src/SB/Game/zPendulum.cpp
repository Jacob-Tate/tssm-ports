#include "zPendulum.h"
#include "zGlobals.h"
#include "zCollGeom.h"
#include "zShrapnel.h"

#include "xEnt.h"
#include "xEntMotion.h"
#include "xEvent.h"
#include "xMath.h"
#include "xModel.h"
#include "xUpdateCull.h"

#include <types.h>

inline void zPendulum_Init(_zPendulum* pend, xEntAsset* asset)
{
    zEntInit(pend, asset, 'PEND');

    xEntMotionAsset* motionAsset = (xEntMotionAsset*)(asset + 1);

    if (pend->linkCount)
    {
        pend->link = (xLinkAsset*)(motionAsset + 1);
    }
    else
    {
        pend->link = NULL;
    }

    pend->update = (xEntUpdateCallback)zPendulum_Update;
    pend->move = (xEntMoveCallback)zPendulum_Move;
    pend->eventFunc = zPendulumEventCB;
    pend->transl = zPendulumTranslate;

    zEntReset(pend);

    xEntMotionInit(&pend->motion, pend, motionAsset);
}

void zPendulum_Init(void* pend, void* asset)
{
    zPendulum_Init((_zPendulum*)pend, (xEntAsset*)asset);
}

inline void zPendulum_Reset(_zPendulum* pend, xScene* sc)
{
    zEntReset(pend);

    xEntMotionReset(&pend->motion, sc);

    xEntMotionPenData* aspen = &pend->motion.asset->pen;

    pend->lt = 0.0f;
    pend->q1t = 1.0f - aspen->phase / (2.0f * PI);
    pend->q3t = 0.5f - aspen->phase / (2.0f * PI);

    if (pend->q1t <= 0.0f)
    {
        pend->q1t += 1.0f;
    }

    if (pend->q1t > 1.0f)
    {
        pend->q1t -= 1.0f;
    }

    if (pend->q3t <= 0.0f)
    {
        pend->q3t += 1.0f;
    }

    if (pend->q3t > 1.0f)
    {
        pend->q3t -= 1.0f;
    }

    pend->q1t *= aspen->period;
    pend->q3t *= aspen->period;
}

// No GC or PS2 symbol (inlined into zSceneSetup); body as in BfBB, plus the motion setup after
// zEntSetup.
inline void zPendulum_Setup(_zPendulum* pend, xScene* sc)
{
    zEntSetup(pend);
    xEntMotionSetup(&pend->motion, sc);
}

void zPendulum_Update(_zPendulum* pend, xScene* sc, F32 dt)
{
    xEntUpdate(pend, sc, dt);

    xEntMotionPenData* aspen = &pend->motion.asset->pen;
    F32 t = pend->motion.t;

    if (t < pend->lt)
    {
        t += aspen->period;
    }

    if (pend->lt <= pend->q1t && t > pend->q1t)
    {
        zEntEvent(pend, eEventSwoosh);
    }
    else if (pend->lt <= pend->q3t && t > pend->q3t)
    {
        zEntEvent(pend, eEventSwoosh);
    }

    pend->lt = pend->motion.t;
}

void zPendulum_Move(_zPendulum* pend, xScene* s, F32 dt, xEntFrame* frame)
{
    xEntMotionMove(&pend->motion, s, dt, frame);
}

void zPendulumTranslate(xEnt* xent, xVec3* dpos, xMat4x3* dmat)
{
    _zPendulum* pend = (_zPendulum*)xent;

    xEntDefaultTranslate(xent, dpos, dmat);
    xEntMotionTranslate(&pend->motion, dpos, dmat);
}

void zPendulumEventCB(xBase* from, xBase* to, U32 toEvent, const F32* toParam, xBase* toParamWidget,
                      U32 toParamWidgetID)
{
    _zPendulum* pend = (_zPendulum*)to;

    switch (toEvent)
    {
    case eEventRun:
        xEntMotionRun(&pend->motion);
        break;
    case eEventStop:
        xEntMotionStop(&pend->motion);
        break;
    case eEventReset:
        zPendulum_Reset(pend, globals.sceneCur);
        break;
    case eEventVisible:
    case eEventFastVisible:
        xEntShow(pend);
        break;
    case eEventInvisible:
    case eEventFastInvisible:
        xEntHide(pend);
        break;
    case eEventCollision_Visible_On:
        xEntShow(pend);
    case eEventCollisionOn:
        pend->chkby = XENT_COLLTYPE_PLYR | XENT_COLLTYPE_NPC;
        pend->bupdate(pend, (xVec3*)&pend->model->Mat->pos);
        break;
    case eEventCollision_Visible_Off:
        xEntHide(pend);
    case eEventCollisionOff:
        pend->chkby = XENT_COLLTYPE_NONE;
        break;
    case eEventCameraCollidePartial:
        zCollGeom_CamEnable(pend);
        break;
    case eEventCameraCollideOn:
        zCollGeom_CamEnable(pend);
        break;
    case eEventCameraCollideOff:
        zCollGeom_CamDisable(pend);
        break;
    case eEventAnimPlay:
    case eEventAnimPlayLoop:
    case eEventAnimStop:
    case eEventAnimPause:
    case eEventAnimResume:
    case eEventAnimTogglePause:
    case eEventAnimPlayRandom:
    case eEventAnimPlayMaybe:
        zEntAnimEvent(pend, toEvent, toParam);
        break;
    case eEventSetUpdateDistance:
        if (globals.updateMgr)
        {
            if (*toParam <= 0.0f)
            {
                xUpdateCull_SetCB(globals.updateMgr, pend, xUpdateCull_AlwaysTrueCB, NULL);
            }
            else
            {
                FloatAndVoid dist;
                dist.f = SQR(*toParam);
                xUpdateCull_SetCB(globals.updateMgr, pend, xUpdateCull_DistanceSquaredCB, dist.v);
            }
        }
        break;
    case eEventLaunchShrapnel:
    {
        zShrapnelAsset* shrap = (zShrapnelAsset*)toParamWidget;
        if (shrap && shrap->initCB)
        {
            shrap->initCB(shrap, pend->model, NULL, NULL);
        }
        break;
    }
    case eEventSetLightKit:
        xModelSetLightKit(pend->model, (xLightKit*)toParamWidget);
        break;
    }
}
