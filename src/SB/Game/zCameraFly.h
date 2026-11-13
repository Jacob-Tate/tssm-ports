#ifndef ZCAMERAFLY_H
#define ZCAMERAFLY_H

#include <types.h>
#include "xBase.h"
#include "xDynAsset.h"
#include "xScene.h"

struct CameraFly_asset : xDynAsset
{
    U32 flyID;
};

struct zCameraFly : xBase
{
    CameraFly_asset* casset;
};

extern U32 zcam_flyasset_current;

void zCameraFly_Init(xBase& data, xDynAsset& asset, size_t);
void zCameraFly_Setup(zCameraFly* fly);
void zCameraFly_Update(xBase* to, xScene* scene, F32 dt);
void zCameraFly_Save(zCameraFly* fly, xSerial* s);
void zCameraFly_Load(zCameraFly* fly, xSerial* s);
void zCameraFlyStart(U32 id);
U32 zCameraFlyProcessStopEvent();
void zCameraFlyEventCB(xBase*, xBase* to, U32 toEvent, const F32*, xBase*, U32);

#endif
