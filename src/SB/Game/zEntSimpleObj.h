#ifndef ZENTSIMPLEOBJ_H
#define ZENTSIMPLEOBJ_H

#include "zEnt.h"
#include "xShadow.h"
#include "xString.h"
#include "xstransvc.h"
#include "iAnim.h"
#include "iModel.h"
#include "zFX.h"
#include "xEnt.h"
#include "zCollGeom.h"
#include "zGlobals.h"
#include "xSkyDome.h"
#include "zGoo.h"
#include "zShrapnel.h"
#include "xMath.h"
#include "zGame.h"
#include "zLOD.h"

struct xSimpleObjAsset
{
    F32 animSpeed;
    U32 initAnimState;
    U8 collType;
    U8 flags;
};

struct zReactiveAnimationData;
struct zReactiveGenre;
struct zSimpleMgr;
struct zDestructible;

// GC layout (size 0x100): the reactive-animation fields share storage with the simple-anim fields
// (reactive objects have sflags 0x200 and never use anim or the pre-baked fastMatList).
struct zEntSimpleObj : zEnt
{
    xSimpleObjAsset* sasset; // 0xDC
    U32 sflags; // 0xE0
    union
    {
        struct
        {
            zReactiveAnimationData* reactiveAnimationData; // 0xE4
            zReactiveGenre* reactGenre; // 0xE8
            zSimpleMgr* smgrExtra; // 0xEC
        };
        struct
        {
            void* anim; // 0xE4
            F32 animTime; // 0xE8
            RwMatrixTag* fastMatList; // 0xEC
        };
    };
    zDestructible* destructible; // 0xF0
    zSimpleMgr* smgr; // 0xF4
    xEntOpacity opacity; // 0xF8
};

struct zSimpleMgr
{
    xSphere worldBound;
    F32 noRenderDist;
    F32 lodDist0;
    F32 fadeDist;
    U32 flags;
    xModelBucket** lodBucket[2];
    RpAtomic* lodAtomic[2];
    RwMatrixTag* mat;
    xModelInstance* model;
    zEntSimpleObj* ent;
    xLightKit* lkit;
};

struct zScene;

extern void iModelAnimMatrices(RpAtomic*, xQuat*, xVec3*, RwMatrixTag*);

void zEntSimpleObj_MgrInit(zEntSimpleObj** entList, U32 entCount);
void zEntSimpleObj_MgrUpdateRender(RpWorld* world, F32 dt);
void zEntSimpleObj_MgrCustomUpdate(zScene* s, F32 dt);
void zEntSimpleObj_MgrCustomRender();
void zEntTrackPhysics_Init(void* ent, void* asset);
void zEntSimpleObj_Init(void* ent, void* asset);
U8 zEntSimpleObj_UsePartialCameraCollision(zEntSimpleObj* ent);
void zEntSimpleObj_Init(zEntSimpleObj* ent, xEntAsset* asset, bool physparams, bool candestroy);
void zEntSimpleObj_Setup(zEntSimpleObj* ent);
void zEntSimpleObj_Update(zEntSimpleObj* ent, xScene* scene, F32 dt);
void zEntSimpleObj_Save(zEntSimpleObj* ent, xSerial* s);
void zEntSimpleObj_Load(zEntSimpleObj* ent, xSerial* s);
void zEntSimpleObj_Reset(zEntSimpleObj* ent, xScene* scene);
void zEntSimpleObjEventCB(xBase* from, xBase* to, U32 toEvent, const F32* toParam,
                          xBase* toParamWidget, U32 toParamWidgetID);

#endif
