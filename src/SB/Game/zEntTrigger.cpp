#include "zEntTrigger.h"

#include "zScene.h"

#include "xEvent.h"
#include "xCollide.h"
#include "xMemMgr.h"
#include "iCollide.h"

#include <string.h>

void zEntTriggerInit(void* ent, void* asset)
{
    zEntTriggerInit((zEntTrigger*)ent, (xEntAsset*)asset);
}

void zEntTriggerInit(zEntTrigger* ent, xEntAsset* asset)
{
    zEntInit(ent, asset, 'TRIG');

    ent->frame = (xEntFrame*)xMemAllocSize(sizeof(xEntFrame));
    memset(ent->frame, 0, sizeof(xEntFrame));

    if (ent->subType == ZENTTRIGGER_TYPE_BOX)
    {
        xTriggerAsset* tasset;
        xVec3 boxUpper;
        xVec3 boxLower;
        xVec3 center;

        xMat3x3Euler(&ent->frame->mat, &asset->ang);

        tasset = (xTriggerAsset*)(asset + 1);

        boxUpper = tasset->p[1];
        boxLower = tasset->p[0];

        xVec3Sub(&boxUpper, &boxUpper, &asset->pos);
        xVec3Sub(&boxLower, &boxLower, &asset->pos);
        xVec3Lerp(&center, &boxLower, &boxUpper, 0.5f);

        xMat3x3RMulVec(&center, &ent->frame->mat, &center);

        xVec3Add(&ent->frame->mat.pos, &center, &asset->pos);

        ent->triggerBox.lower.x = -0.5f * (boxUpper.x - boxLower.x);
        ent->triggerBox.lower.y = -0.5f * (boxUpper.y - boxLower.y);
        ent->triggerBox.lower.z = -0.5f * (boxUpper.z - boxLower.z);
        ent->triggerBox.upper.x = -ent->triggerBox.lower.x;
        ent->triggerBox.upper.y = -ent->triggerBox.lower.y;
        ent->triggerBox.upper.z = -ent->triggerBox.lower.z;
    }

    ent->move = NULL;
    ent->update = (xEntUpdateCallback)zEntTriggerUpdate;
    ent->eventFunc = zEntTriggerEventCB;

    if (ent->linkCount)
    {
        ent->link = (xLinkAsset*)(zEntTriggerAsset(*ent) + 1);
    }
    else
    {
        ent->link = NULL;
    }

    ent->entered = 0;

    xEntDriveInit(&ent->drive, ent);
    ent->drive.flags = 1;
}

void zEntTriggerUpdate(zEntTrigger* trig, xScene* sc, F32 dt)
{
    if (xBaseIsEnabled(trig))
    {
        U32 i;
        xLinkAsset* link;
        xTriggerAsset* tasset;

        xEntDriveUpdate(&trig->drive, sc, dt, NULL);

        link = trig->link;
        tasset = zEntTriggerAsset(*trig);

        for (i = 0; i < trig->linkCount; i++)
        {
            if (link->chkAssetID && link->srcEvent >= eEventEnterEntity &&
                link->srcEvent <= eEventExitEntityFLAG)
            {
                xIsect isect;
                S32 collide = 0;

                if (link->chkAssetID)
                {
                    xEnt* chkEnt = (xEnt*)zSceneFindObject(link->chkAssetID);

                    if (chkEnt)
                    {
                        xVec3* chkPos;

                        if (chkEnt->bound.type == XBOUND_TYPE_SPHERE)
                        {
                            chkPos = &chkEnt->bound.sph.center;
                        }
                        else
                        {
                            chkPos = (xVec3*)&chkEnt->model->Mat->pos;
                        }

                        switch (trig->subType)
                        {
                        case ZENTTRIGGER_TYPE_BOX:
                        {
                            xVec3 xformVec;

                            xMat4x3Tolocal(&xformVec, &trig->frame->mat, chkPos);
                            iBoxIsectVec(&trig->triggerBox, &xformVec, &isect);

                            collide = (isect.penned <= 0.0f);

                            break;
                        }
                        case ZENTTRIGGER_TYPE_SPHERE:
                        {
                            xSphere sphere;
                            xIsect isect;

                            sphere.center = tasset->p[0];
                            sphere.r = tasset->p[1].x;

                            iSphereIsectVec(&sphere, chkPos, &isect);

                            collide = (isect.penned <= 0.0f);

                            break;
                        }
                        case ZENTTRIGGER_TYPE_VCYLINDER:
                        {
                            xCylinder cylinder;
                            xIsect isect;

                            cylinder.center = tasset->p[0];
                            cylinder.r = tasset->p[1].x;
                            cylinder.h = tasset->p[1].y;

                            iCylinderIsectVec(&cylinder, chkPos, &isect);

                            collide = (isect.penned <= 0.0f);

                            break;
                        }
                        case ZENTTRIGGER_TYPE_VSPHERE:
                        {
                            xSphere sphere;
                            xIsect isect;

                            sphere.center = tasset->p[0];
                            sphere.r = tasset->p[1].x;

                            iSphereIsectVec(&sphere, chkPos, &isect);

                            collide = (isect.penned <= 0.0f);

                            break;
                        }
                        case ZENTTRIGGER_TYPE_4:
                        case ZENTTRIGGER_TYPE_5:
                        {
                            break;
                        }
                        }
                    }
                }

                if (collide)
                {
                    if (link->srcEvent == eEventEnterEntity)
                    {
                        link->srcEvent = eEventEnterEntityFLAG;

                        zEntEvent(trig, eEventEnterEntity, zSceneFindObject(link->dstAssetID),
                                  link->dstEvent, link->param,
                                  zSceneFindObject(link->paramWidgetAssetID),
                                  link->paramWidgetAssetID, FE_NO);
                    }

                    if (link->srcEvent == eEventExitEntity)
                    {
                        link->srcEvent = eEventExitEntityFLAG;
                    }
                }
                else
                {
                    if (link->srcEvent == eEventExitEntityFLAG)
                    {
                        link->srcEvent = eEventExitEntity;

                        zEntEvent(trig, eEventExitEntity, zSceneFindObject(link->dstAssetID),
                                  link->dstEvent, link->param,
                                  zSceneFindObject(link->paramWidgetAssetID),
                                  link->paramWidgetAssetID, FE_NO);
                    }

                    if (link->srcEvent == eEventEnterEntityFLAG)
                    {
                        link->srcEvent = eEventEnterEntity;
                    }
                }
            }

            link++;
        }
    }
}

void zEntTriggerEventCB(xBase*, xBase* to, U32 toEvent, const F32* toParam, xBase* toParamWidget,
                        U32)
{
    zEntTrigger* trig = (zEntTrigger*)to;

    switch (toEvent)
    {
    case eEventReset:
    {
        zEntTriggerReset(trig);
        break;
    }
    case eEventMount:
    {
        if (toParamWidget)
        {
            F32 mt = 0.00001f;
            if (toParam[0])
            {
                mt = toParam[0];
            }
            xEntDriveMount(&trig->drive, (xEnt*)toParamWidget, mt, NULL);
        }
        break;
    }
    case eEventDismount:
    {
        if (toParamWidget)
        {
            F32 mt = 0.00001f;
            if (toParam[0])
            {
                mt = toParam[0];
            }
            xEntDriveDismount(&trig->drive, mt);
        }
        break;
    }
    }
}

void zEntTriggerSave(zEntTrigger* ent, xSerial* s)
{
    zEntSave(ent, s);
}

void zEntTriggerLoad(zEntTrigger* ent, xSerial* s)
{
    zEntLoad(ent, s);
}

void zEntTriggerReset(zEntTrigger* ent)
{
    ent->entered = 0;

    zEntReset(ent);

    switch (ent->subType)
    {
    case ZENTTRIGGER_TYPE_BOX:
    {
        xTriggerAsset* tasset;
        xVec3 boxUpper;
        xVec3 boxLower;
        xVec3 center;

        xMat3x3Euler(&ent->frame->mat, &ent->asset->ang);

        tasset = zEntTriggerAsset(*ent);

        boxUpper = tasset->p[1];
        boxLower = tasset->p[0];

        xVec3Sub(&boxUpper, &boxUpper, &ent->asset->pos);
        xVec3Sub(&boxLower, &boxLower, &ent->asset->pos);
        xVec3Lerp(&center, &boxLower, &boxUpper, 0.5f);

        xMat3x3RMulVec(&center, &ent->frame->mat, &center);

        xVec3Add(&ent->frame->mat.pos, &center, &ent->asset->pos);
        break;
    }
    }
}

bool zEntTriggerHitsSphere(const zEntTrigger& trig, const xSphere& o, const xVec3& dir)
{
    xTriggerAsset* tasset;

    if (!xBaseIsEnabled(&trig))
    {
        return false;
    }

    tasset = zEntTriggerAsset(trig);

    if (tasset->flags & 0x1 && dir.dot(tasset->direction) <= 0.0f)
    {
        return false;
    }

    switch (trig.subType)
    {
    case ZENTTRIGGER_TYPE_BOX:
    {
        return xSphereHitsOBB(o, trig.triggerBox, trig.frame->mat);
    }
    case ZENTTRIGGER_TYPE_SPHERE:
    {
        return xSphereHitsSphere(o.center, o.r, tasset->p[0], tasset->p[1].x);
    }
    case ZENTTRIGGER_TYPE_VCYLINDER:
    {
        return xSphereHitsVCylinder(o.center, o.r, tasset->p[0], tasset->p[1].x, tasset->p[1].y);
    }
    case ZENTTRIGGER_TYPE_VSPHERE:
    {
        return xSphereHitsVCircle(o, tasset->p[0], tasset->p[1].x);
    }
    }

    return false;
}
