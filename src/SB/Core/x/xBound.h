#ifndef XBOUND_H
#define XBOUND_H

#include "xMathInlines.h"
#include "xQuickCull.h"
#include "xMath3.h"
#include "xCollide.h"

// Size: 0x4C
struct xBound
{
    xQCData qcd;

    U8 type; // Offset: 0x20 (or 0x84)
    U8 pad[3];
    union
    {
        xSphere sph;
        xBBox box;
        xCylinder cyl;
    };
    xMat4x3* mat;
};

#define XBOUND_TYPE_NA 0 // None/invalid
#define XBOUND_TYPE_SPHERE 1 // Sphere - xBound::sph
#define XBOUND_TYPE_BOX 2 // Axis aligned bounding box - xBound::box
#define XBOUND_TYPE_CYL 3 // Cylinder - xBound::cyl (unused?)
#define XBOUND_TYPE_OBB 4 // Oriented bounding box - xBound::box and xBound::mat

// PS2 DWARF (zWadNME); same values as the XBOUND_TYPE_* defines.
enum xBoundType
{
    k_XBOUNDTYPE_NONE,
    k_XBOUNDTYPE_SPHERE,
    k_XBOUNDTYPE_BOX,
    k_XBOUNDTYPE_CYLINDER,
    k_XBOUNDTYPE_OBB,
    k_XBOUNDTYPE_COUNT,
    k_XBOUNDTYPE_SHIFT = 0x3
};

void xBoundMakeSphere(xBound& b, const xVec3& center, F32 r);
void xBoundGetBox(xBox& box, const xBound& bound);
void xBoundGetSphere(xSphere& o, const xBound& bound);
void xBoundHitsModel(const xBound* b, const xModelInstance* m, xCollis* colls, U8 ncolls, F32 sth);
void xBoundDraw(const xBound* bound);
xVec3* xBoundCenter(xBound* b);
const xVec3* xBoundCenter(const xBound* b);
void xBoundUpdate(xBound* b);
void xBoundHitsBound(const xBound* a, const xBound* b, xCollis* c);
bool xBoundHitsBound(const xBound& a, const xBound& b);
void xBoundHitsModel(const xBound* b, const xModelInstance* m, xCollis* colls, U8 ncolls, F32 sth);
void xRayHitsBound(const xRay3* r, const xBound* b, xCollis* c);
void xSphereHitsBound(const xSphere* o, const xBound* b, xCollis* c);
void xVecHitsBound(const xVec3* v, const xBound* b, xCollis* c);
bool xSphereHitsBound(const xVec3& c, F32 r, const xBound& b);
bool xOBBHitsBound(const xBox& box, const xMat4x3& mat, const xBound& b);
void xBoundGetCenter(const xBound* b, xVec3* center);

inline bool xSphereHitsBound(const xSphere& o, const xBound& b)
{
    return xSphereHitsBound(o.center, o.r, b);
}

inline xVec3* xBoundCenter(xBound* b)
{
    return &b->sph.center;
}

inline const xVec3* xBoundCenter(const xBound* b)
{
    return &b->sph.center;
}

// No TSSM symbol (always inlined); name and signature from the Ratatouille GC DWARF. It reads r
// before copying c, as for call arguments.
inline void xBoundFromSphere(xBound& bound, const xVec3& c, F32 r)
{
    bound.type = XBOUND_TYPE_SPHERE;
    bound.sph.center = c;
    bound.sph.r = r;
    xQuickCullForBound(&bound.qcd, &bound);
}

#endif
