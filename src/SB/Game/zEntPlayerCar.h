#ifndef ZENTPLAYERCAR_H
#define ZENTPLAYERCAR_H

#include "xEnt.h"
#include "zCar.h"

void zEntPlayerCarInit(xEnt* ent);
void zEntPlayerCarHandleEvents(xBase* from, xBase* to, U32 toEvent, const F32* toParam,
                               xBase* toParamWidget, U32 toParamWidgetID);
void zEntPlayerCarUpdateHealth(xEnt* ent, F32 dt);
void zEntPlayerCarRenderFX(xEnt* ent);
U8 zEntPlayerCarUpdate(xEnt* ent, xScene* sc, F32 dt);
void zEntPlayerCarExit();
//zCar* zEntPlayerCarGetCar();

extern struct zCamCar* carCam;

#endif
