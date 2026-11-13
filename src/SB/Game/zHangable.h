#ifndef ZHANGABLE_H
#define ZHANGABLE_H

#include <types.h>

#include "xBase.h"
#include "xDynAsset.h"
#include "xEnt.h"
#include "xMath3.h"
#include "xVec3.h"

struct zHangableAsset : xDynAsset
{
    U32 object;
    xVec3 pivot;
    xVec3 handle;
    F32 onGravity;
    F32 offGravity;
    F32 maxAngVel;
    F32 minArcDegrees;
    U32 hangFlags;
};

struct zHangable : xBase
{
    zHangableAsset* hangableAsset;
    U32 flags;
};

struct zHangableObject
{
    zHangable* hangable;
    xEnt* object;
    F32 radius;
    xVec3 vel;
    xVec3 accel;
    xVec3 handle;
    xMat3x3 centerMat;
    xVec3 rotAxis;
    xVec3 lastDisp;
    xVec3 objVel;
};

#endif
