#ifndef ZTHROWN_H
#define ZTHROWN_H

#include <types.h>
#include "iColor.h"
#include "xEnt.h"
#include "xEntDrive.h"
#include "zShrapnel.h"

struct xScene;
struct zScene;
struct zThrownStruct;

struct CarryableStats
{
    F32 killTimer;
};

struct LaunchStats
{
    F32 throwSpeedXZ;
    F32 throwSpeedY;
    F32 collResetTimer;
};

struct ThrowableStats
{
    char* name;
    void (*collCB)(zThrownStruct*, xEntCollis*, F32*, F32*);
    CarryableStats* carry;
    LaunchStats* launch;
    xColor_tag glowColor;
    char* shrapName;
    F32 stackHeight;
    U32 nameHash;
    U32 nameHashMINF;
    zShrapnelAsset* shrapAsset;
};

struct zThrownStruct
{
    xEnt* ent;
    xEnt* stackEnt;
    xEnt* stackTgt;
    xEnt* patLauncher;
    void (*oldupdate)(xEnt*, xScene*, F32);
    xVec3 vel;
    xVec3 oldcollpos;
    F32 collResetTimer;
    F32 killTimer;
    ThrowableStats* stats;
    U32 oldRecShadow;
    S32 newUpdate;
    S32 hthrown;
    xEntDrive drv;
    S32 driveDebounce;
    xEnt* driveLastFloor;
    xEntFrame frame;
};

S32 zThrown_IsStacked(xEnt* ent);
S32 zThrown_IsFruit(xEnt* ent, F32* stackHeight);
S32 zThrown_KillFruit(xEnt* ent);
void zThrown_Remove(xEnt* ent);
void zThrown_Reset();
void zThrown_ResetAll();
void zThrown_AddFruit(xEnt* ent);
void zThrown_PatrickLauncher(xEnt* ent, xEnt* launcher);
void zThrown_LaunchStack(xEnt* ent, xEnt* stackTgt);
S32 zThrown_LaunchPos(xEnt* ent, xVec3* pos, xVec3* dir);
void zThrown_LaunchDir(xEnt* ent, xVec3* dir);
void zThrown_LaunchVel(xEnt* ent, xVec3* vel);
void zThrown_NMEUpdate(xEnt* ent, xScene* sc, F32 dt);
void zThrown_Setup(zScene* sc);

#endif
