#include "xLinkAsset.h"
#include <types.h>

#include "xBase.h"
#include "xEvent.h"
#include "xScene.h"

#include "zBase.h"
#include "zCameraFly.h"
#include "zCamera.h"
#include "zGlobals.h"
#include "zMusic.h"

U32 zcam_flyasset_current;

static inline void zCameraFly_Init(zCameraFly* data, CameraFly_asset* asset);

void zCameraFly_Init(xBase& data, xDynAsset& asset, size_t)
{
    zCameraFly_Init((zCameraFly*)&data, (CameraFly_asset*)&asset);
}

static inline void zCameraFly_Init(zCameraFly* data, CameraFly_asset* asset)
{
    xBaseInit(data, asset);
    data->casset = asset;
    data->eventFunc = zCameraFlyEventCB;

    if (data->linkCount != 0)
    {
        data->link = (xLinkAsset*)(asset + 1);
    }
    else
    {
        data->link = NULL;
    }
}

void zCameraFly_Setup(zCameraFly* fly)
{
    fly->baseFlags |= (U16)2;
}

void zCameraFly_Update(xBase* to, xScene* scene, F32 dt)
{
}

void zCameraFly_Save(zCameraFly* fly, xSerial* s)
{
    xBaseSave((xBase*)fly, s);
}

void zCameraFly_Load(zCameraFly* fly, xSerial* s)
{
    xBaseLoad((xBase*)fly, s);
}

U32 zCameraFlyProcessStopEvent()
{
    zScene* s = globals.sceneCur;

    for (U16 i = 0; i < s->num_base; i++)
    {
        xBase* entry = s->base[i];
        if (entry->baseType == eBaseTypeCameraFly)
        {
            U32 id = ((zCameraFly*)entry)->casset->flyID;
            if (id == zcam_flyasset_current)
            {
                zEntEvent(entry, eEventStop);
                return 1;
            }
        }
    }

    return 0;
}

void zCameraFlyEventCB(xBase*, xBase* to, U32 toEvent, const F32*, xBase*, U32)
{
    zCameraFly* fly = (zCameraFly*)to;

    switch (toEvent)
    {
    case eEventEnable:
        fly->baseFlags |= 1;
        break;

    case eEventDisable:
        fly->baseFlags &= ~1;
        break;

    case eEventRun:
        if (fly->baseFlags & 1)
        {
            zCameraFlyStart(fly->casset->flyID);
        }
        break;

    case eEventStop:
        break;

    case eEventSceneBegin:
        break;

    default:
        break;
    }
}
