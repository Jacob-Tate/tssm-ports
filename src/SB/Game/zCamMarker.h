#ifndef ZCAMMARKER_H
#define ZCAMMARKER_H

#include <types.h>
#include "zCamera.h"
#include "xBase.h"
#include "xCamera.h"
#include "xEvent.h"

struct zCamMarker : xBase
{
    xCamAsset* asset;
};

void zCamMarkerSceneEnter();
void zCamMarkerReset();
void zCamMarkerInit(xBase* b, xCamAsset* asset);
void zCamMarkerSave(zCamMarker* m, xSerial* s);
void zCamMarkerLoad(zCamMarker* m, xSerial* s);
void zCamMarkerEventCB(xBase* from, xBase* to, U32 toEvent, const F32* toParam, xBase* b3, U32);

#endif
