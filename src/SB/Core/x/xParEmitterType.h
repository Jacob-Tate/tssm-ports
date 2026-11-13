#ifndef XPAREMITTERTYPE_H
#define XPAREMITTERTYPE_H

#include "xMath3.h"

struct xPECircle
{
    F32 radius;
    F32 deflection;
    xVec3 dir;
};

struct xPESphere
{
    F32 radius;
};

struct xPERect
{
    F32 x_len;
    F32 z_len;
};

struct xPELine
{
    xVec3 pos1;
    xVec3 pos2;
    F32 radius;
};

struct xPEVolume
{
    U32 emit_volumeID;
};

struct xPEOffsetPoint
{
    xVec3 offset;
};

struct xPEVCyl
{
    F32 height;
    F32 radius;
    F32 deflection;
};

struct xPEEntBone
{
    U8 flags;
    U8 type;
    U8 bone;
    U8 pad1;
    xVec3 offset;
    F32 radius;
    F32 deflection;
};

struct xPEEntBound
{
    U8 flags;
    U8 type;
    U8 pad1;
    U8 pad2;
    F32 expand;
    F32 deflection;
};

struct xPar;
struct xParEmitter;
struct xParEmitterAsset;
struct xEnt;
struct xVolume;

void xParEmitterEmitEntBound(xPar* p, xParEmitterAsset* a, F32 dt, const xEnt* ent);
void xParEmitterEmitEntBone(xPar* p, xParEmitterAsset* a, F32 dt, const xMat4x3& mat);
void xParEmitterTransformEntBone(xVec3& loc, xVec3& vel, const xParEmitterAsset& a,
                                 const xMat4x3& mat);
xMat4x3* xParEmitterTransformEntBone(xVec3& loc, xVec3& vel, const xParEmitterAsset& a,
                                     const xEnt& ent);
void xParEmitterEmitOCircle(xPar* p, xParEmitterAsset* a, F32 dt);
void xParEmitterEmitOCircleEdge(xPar* p, xParEmitterAsset* a, F32 dt);
void xParEmitterEmitVCylEdge(xPar* p, xParEmitterAsset* a, F32 dt);
void xParEmitterEmitOffsetPoint(xParEmitter* pe, xPar* p, xParEmitterAsset* a, F32 dt, xEnt* ent);
void xParEmitterEmitEntity(xPar* p, xParEmitterAsset* a, F32 dt, xEnt* ent);
void xParEmitterEmitVolume(xPar* p, xParEmitterAsset* a, F32 dt, xVolume* vol);
void xParEmitterEmitSphereEdge(xPar* p, xParEmitterAsset* a, F32 dt, S32 subtype);
void xParEmitterEmitSphere(xPar* p, xParEmitterAsset* a, F32 dt);
void xParEmitterEmitLine(xPar* p, xParEmitterAsset* a, F32 dt);
void xParEmitterEmitRectEdge(xPar* p, xParEmitterAsset* a, F32 dt);
void xParEmitterEmitRect(xPar* p, xParEmitterAsset* a, F32 dt);
void xParEmitterEmitCircle(xPar* p, xParEmitterAsset* a, F32 dt);
void xParEmitterEmitCircleEdge(xPar* p, xParEmitterAsset* a, F32 dt);
void xParEmitterEmitPoint(xPar* p, xParEmitterAsset* a, F32 dt);

#endif
