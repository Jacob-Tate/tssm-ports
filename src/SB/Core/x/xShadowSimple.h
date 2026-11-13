#ifndef XSHADOWSIMPLE_H
#define XSHADOWSIMPLE_H

#include "xEnt.h"

struct xShadowSimplePoly
{
    xVec3 vert[3];
    xVec3 norm;
};

// Size: 0xA4
struct xShadowSimpleCache
{
    U16 flags;
    U8 alpha;
    U8 pad;

    // Offset: 0x4
    U32 collPriority;
    xVec3 pos;
    xVec3 at;

    // Offset: 0x20
    F32 tol_movement;
    F32 radiusOptional;
    xEnt* castOnEnt;
    xShadowSimplePoly poly;
    F32 envHeight;
    F32 shadowHeight;
    union
    {
        U32 raster;
        RwRaster* ptr_raster;
    };
    F32 dydx;
    F32 dydz;
    xVec3 corner[4];
    void* collSkipsItem;
};

struct zSimpleShadowTableHeader
{
    U32 num;
};

// 'num' entries follow the header in a SHDW asset
struct zSimpleShadow
{
    U32 modelID;
    U32 shadowID;
    U32 flags;
};

struct xShadowSimpleQueue
{
    xShadowSimpleCache* cache;
    U32 priority;
    xMat4x3* modelMatrix;
    F32 radius;
    F32 height;
    F32 ecc;
};

void xShadowSimple_Render();
void xShadowSimple_Add(xShadowSimpleCache* cache, xEnt* ent, F32 radius, F32 ecc);
void xShadowSimple_Add_Expert(xShadowSimpleCache* cache, const xMat4x3* mat_owner, U8 alpha,
                              F32 radius, F32 height, F32 ecc);
void xShadowSimple_CacheInit(xShadowSimpleCache* cache, xEnt* ent, U8 alpha);
void xShadowSimple_CacheInit_Expert(xShadowSimpleCache* cache, RwRaster* rast, U8 alpha);
void xShadowSimple_Reset();
void xShadowSimple_Init();

#endif
