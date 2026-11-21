#ifndef XSKYDOME_H
#define XSKYDOME_H

#include "xEnt.h"

void xSkyDome_Setup();
void xSkyDome_AddEntity(xEnt* ent, S32 sortorder, S32 lockY, S32 lockXZ);
void xSkyDome_Render();

#ifdef PLATFORM_PC
// PC: the PS2 and Xbox builds' single-dome draw, which the GameCube build
// strips; only the Xbox water's refraction and reflection passes use it
// (iXboxWater.h). A dome is an entity whose render is xSkyDome_EmptyRender.
// Begin and end bracket the RenderSingle calls, in the current camera.
void xSkyDome_EmptyRender(xEnt*);
void xSkyDome_BeginRenderSingle();
void xSkyDome_RenderSingle(xEnt* ent);
void xSkyDome_EndRenderSingle();
#endif

#endif
