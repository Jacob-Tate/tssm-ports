#ifndef XSHADOW_H
#define XSHADOW_H

#include "xEnt.h"

#include <rwcore.h>
#include <rpworld.h>

struct xShadowPoly
{
    xVec4 vert[3];
};

struct xShadowCache
{
    xVec3 pos;
    F32 radius;
    S32 entCount;
    S32 polyCount;
    F32 polyRayDepth[5];
    U16 castOnEnt;
    U16 castOnPoly;
    U32 pad;
    xEnt* ent[16];
    xShadowPoly poly[256];
};

struct xShadowMgr
{
    xEnt* ent;
    xShadowCache* cache;
    int priority;
    int cacheReady;
};

extern F32 gShadowObjectRadius;

void xShadowInit();
void xShadow_ListAdd(xEnt* ent);
void xShadowSetWorld(RpWorld* world);
void xShadowSetLight(xVec3* param1, xVec3* param2, F32 param3);
void xShadowCameraUpdate(void* model, void (*renderCB)(void*), xVec3* center, F32 radius,
                         S32 shadowMode);
void xShadowManager_Init(S32 numEnts, S32);
void xShadowManager_Add(xEnt* ent);
void xShadowManager_Remove(xEnt* ent);
U32 xShadowReceiveShadowSetup(xEnt* ent);
void xShadowReceiveShadow(xEnt* ent, F32 shadowFactor, S32 shadowMode, RwMatrixTag* shadowMat,
                          RwRaster* shadowRast);
void xShadowVertical_FillCache(xShadowCache* cache, xVec3* pos, F32 r, F32 depth, F32 minNormY);
void xShadowVertical_DrawCache(xShadowCache* cache, F32 shadowFactor, F32 fadeDist, S32 shadowMode,
                               RwMatrixTag* shadowMat, RwRaster* shadowRast);
S32 Im2DRenderQuad(F32 x1, F32 y1, F32 x2, F32 y2, F32 z, F32 recipCamZ, F32 uvOffset);
void xShadowManager_Reset();
void xShadowManager_Render();
#ifdef PLATFORM_PC
void xShadowManager_PreRender(RwCamera* sceneCamera);
#endif
void xShadowRender(xEnt* ent, F32 max_dist);

#endif
