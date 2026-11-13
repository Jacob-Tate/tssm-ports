#ifndef ZTAXI_H
#define ZTAXI_H

#include <types.h>
#include "xBase.h"
#include "xDynAsset.h"
#include "zEnt.h"

struct xScene;

struct taxi_asset : xDynAsset
{
    U32 marker;
    U32 cameraID;
    U32 portalID;
    U32 talkBoxID;
    U32 textID;
    U32 taxiID;
    F32 invDelay;
    F32 portalDelay;
};

struct zTaxi : xBase
{
    taxi_asset* basset;
    zEnt* taxi;
    xVec3 pos;
    U32 currState;
    U32 prevState;
    F32 portalTimer;
    F32 invTimer;
};

void zTaxi_Setup(zTaxi* taxi);
void zTaxi_Update(xBase* to, xScene*, F32 dt);

#endif
