#ifndef ZBUSSTOP_H
#define ZBUSSTOP_H

#include "xVec3.h"
#include "xBase.h"
#include "xDynAsset.h"

#include "zEnt.h"

struct busstop_asset : xDynAsset
{
    U32 marker;
    U32 character;
    U32 cameraID;
    U32 busID;
    F32 delay;
};

struct zBusStop : xBase
{
    busstop_asset* basset; // 0x10
    zEnt* bus; // 0x14
    xVec3 pos; // 0x18
    U32 currState; // 0x24
    U32 prevState; // 0x28
    F32 switchTimer; // 0x2C
};

extern U32 gBusStopIsRunning;
extern S32 gBusStopIsWaiting;

struct xScene;

void zBusStopEventCB(xBase*, xBase* to, U32 toEvent, const F32*, xBase*, U32);
void zBusStop_Update(xBase* to, xScene*, F32 dt);
void zBusStop_Setup(zBusStop* bstop);
void zBusStop_Init(xBase& data, xDynAsset& asset, size_t);

#endif
