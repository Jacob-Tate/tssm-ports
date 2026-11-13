#ifndef XCOLLIDE_H
#define XCOLLIDE_H

#include "xEnv.h"
#include "xModel.h"
#include "xQuickCull.h"
#include "iMath3.h"

#define k_HIT_IT ((U32)(1 << 0))
#define k_HIT_0x2 ((U32)(1 << 1))
#define k_HIT_0x4 ((U32)(1 << 2))
#define k_HIT_0x8 ((U32)(1 << 3))
#define k_HIT_0x10 ((U32)(1 << 4))
#define k_HIT_0x100 ((U32)(1 << 8))
#define k_HIT_0x200 ((U32)(1 << 9))
#define k_HIT_0x400 ((U32)(1 << 10))
#define k_HIT_0x800 ((U32)(1 << 11))
#define k_HIT_0xF00 (k_HIT_0x100 | k_HIT_0x200 | k_HIT_0x400 | k_HIT_0x800)
#define k_HIT_CALC_HDNG ((U32)(1 << 12))
#define k_HIT_CALC_TRI ((U32)(1 << 13))
#define k_HIT_0x20000 ((U32)(1 << 17))

struct xModelInstance;

struct xCollis
{
    struct tri_data
    {
        U32 index;
        F32 r;
        F32 d;
    };

    U32 flags;
    U32 oid;
    void* optr;
    xModelInstance* mptr;
    F32 dist;
    F32 test_dist;
    xVec3 norm;
    xVec3 tohit;
    xVec3 depen;
    xVec3 hdng;
    union
    {
        struct
        {
            F32 t;
            F32 u;
            F32 v;
        } tuv;
        tri_data tri;
    };
};

struct xParabola
{
    xVec3 initPos;
    xVec3 initVel;
    F32 gravity;
    F32 minTime;
    F32 maxTime;
};

struct xPlane
{
    xVec3 norm;
    F32 d;

    // No GC symbols (always inlined); members from the Ratatouille GC DWARF (xMath3.h).
    void Init(const xVec3& pt)
    {
        d = norm.dot(pt);
    }
    F32 Dist(const xVec3& pt) const
    {
        return norm.dot(pt) - d;
    }
};

enum xCollideSphereHitType
{
    exCOLLIDESPHEREHITTYPE_UNKNOWN,
    exCOLLIDESPHEREHITTYPE_INTERIOR,
    exCOLLIDESPHEREHITTYPE_EDGE,
    exCOLLIDESPHEREHITTYPE_VERTEX
};

struct xJSPHeader;
struct xClumpCollBSPTriangle;

// Size: 0xc8 (layout from the PS2 DWARF)
struct xSweptSphere
{
    U8 detect_initial_penetration;
    U8 init_collide;
    xVec3 start;
    xVec3 end;
    F32 radius;
    // Offset: 0x20
    F32 dist;
    xVec3 dir;
    // Offset: 0x30
    xBox box;
    // Offset: 0x48
    xQCData qcd;
    // Offset: 0x68
    F32 curdist;
    U32 oid;
    void* optr;
    xModelInstance* mptr;
    // Offset: 0x78
    xVec3 worldContact;
    S32 hitIt;
    xVec3 worldPos;
    xVec3 worldNormal;
    xVec3 worldTangent;
    // Offset: 0xac
    xJSPHeader* jsp;
    xClumpCollBSPTriangle* bspTriangle;
    xCollideSphereHitType type;
    // Offset: 0xb8
    xPlane tri_plane;

    // Empty user constructor: retail zEntPlayer_Update calls it out of line.
    xSweptSphere()
    {
    }
};

// Size: 0x1c
struct xCollideLineSegment
{
    xVec3 start;
    xVec3 dir;
    F32 length;
};

// Size: 0x94; xEdge is nested per the GC mangled names.
struct xCollideProcessedTri
{
    // Size: 0x2c
    struct xEdge
    {
        xCollideLineSegment segment;
        xPlane edge;
    };

    xPlane surface;
    xEdge edges[3];
};

enum _xCollsIdx
{
    k_XCOLLS_IDX_FLOOR,
    k_XCOLLS_IDX_CEIL,
    k_XCOLLS_IDX_FRONT,
    k_XCOLLS_IDX_LEFT,
    k_XCOLLS_IDX_REAR,
    k_XCOLLS_IDX_RIGHT,
    k_XCOLLS_IDX_COUNT
};

struct xScene;
struct xEnt;
struct xBound;
struct xRay3;

void xCollideInit(xScene* sc);
S32 xSweptSphereToNPC(xSweptSphere* sws, xScene* sc, xEnt* mover, U8 collType, S32 penby);
S32 xSweptSphereToStatDyn(xSweptSphere* sws, xScene* sc, xEnt* mover, U8 collType, S32 penby);
S32 xSweptSphereToEnt(xSweptSphere* sws, xEnt* ent, xRay3* ray);
S32 xSweptSphereToTriangle(xSweptSphere& sws, const xVec3& v0, const xVec3& v1, const xVec3& v2);
S32 xSweptSphereToBound(xSweptSphere* sws, const xBound* bound);
S32 xSweptSphereToSphere(xSweptSphere& sws, const xVec3& center, F32 radius);
// Weak in retail: called out of line by xSweptSphereToEnt and xSweptSphereToBound, inlined into
// zNMETongueSpin::DoCapsuleTests.
inline S32 xSweptSphereToSphere(xSweptSphere* sws, const xSphere* sph)
{
    return xSweptSphereToSphere(*sws, sph->center, sph->r);
}
S32 xSweptSphereToOBB(xSweptSphere* sws, const xBox* box, const xMat4x3* mat);
S32 xSweptSphereToBox(xSweptSphere& sws, const xBox& box);
S32 xSweptSphereToModel(xSweptSphere* sws, RpAtomic* model, const RwMatrix* mat);
S32 xSweptSphereToScene(xSweptSphere* sws, xScene* sc, xEnt* mover, U8 collType, S32 penby);
S32 xSweptSphereToEnv(xSweptSphere* sws, xEnv* env);
void xSweptSpherePrepare(xSweptSphere* sws, const xVec3* start, const xVec3* end, F32 radius);
void xSweptSphereGetResults(xSweptSphere* sws);
S32 xSweptSphereToNonMoving(xSweptSphere* sws, xScene* sc, xEnt* mover, U8 collType, S32 penby);
void RpCollTreeForAllCapsuleIntsec(struct RpCollTree* tree, RwLine* line, F32 radius,
                                   S32 (*callBack)(S32, S32, void*), void* data);
U32 xSphereHitsOBB_nu(const xSphere* s, const xBox* b, const xMat4x3* m, xCollis* coll);
U32 xSphereHitsBox(const xSphere* a, const xBox* b, xCollis* coll);
U32 xBoxHitsSphere(const xBox* a, const xSphere* b, xCollis* coll);
U32 xObbHitsObb(const xBox* a, const xMat4x3* a_mat, const xBox* b, const xMat4x3* b_mat,
                xCollis* coll);
U32 xBoxHitsObb(const xBox* a, const xBox* b, const xMat4x3* mat, xCollis* coll);
U32 xCylinderHitsSphere(const xCylinder* cyl, const xSphere* sph, xCollis* coll);
U32 xSphereHitsVCylinder(const xSphere* sph, const xCylinder* cyl, xCollis* coll);
bool xSphereHitsVCylinder(const xVec3& sc, F32 sr, const xVec3& cc, F32 cr, F32 ch);
bool xSphereHitsVCircle(const xSphere& s, const xVec3& c, F32 r);
bool xSphereHitsVCircle(const xVec3& sc, F32 sr, const xVec3& cc, F32 cr);
bool xSphereHitsCone(const xVec3& center, F32 radius, const xVec3& kv0, F32 kr0, const xVec3& kv1,
                     F32 kr1);
U32 xSphereHitsModel(const xSphere* b, const xModelInstance* m, xCollis* coll);
U32 xSphereHitsEnv(const xSphere* b, const xEnv* env, xCollis* coll);
bool xSphereHitsBox(const xVec3& c, F32 r, const xBox& b);
bool xOBBHitsOBB(const xBox& a, const xMat4x3& amat, const xBox& b, const xMat4x3& bmat);

// Name and signature from the Ratatouille GC DWARF (xCollide.h).
inline bool xBoxHitsBox(const xBox& a, const xBox& b)
{
    return a.upper.x >= b.lower.x && a.upper.y >= b.lower.y && a.upper.z >= b.lower.z &&
           a.lower.x <= b.upper.x && a.lower.y <= b.upper.y && a.lower.z <= b.upper.z;
}

inline bool xSphereHitsSphere(const xVec3& loc1, F32 r1, const xVec3& loc2, F32 r2)
{
    F32 dist2 = (loc2 - loc1).length2();
    F32 max_dist = r1 + r2;
    return dist2 <= SQR(max_dist);
}

inline bool xSphereHitsSphere(const xSphere& o1, const xSphere& o2)
{
    return xSphereHitsSphere(o1.center, o1.r, o2.center, o2.r);
}

inline bool xSphereHitsBox(const xSphere& o, const xBox& b)
{
    return xSphereHitsBox(o.center, o.r, b);
}

inline bool xSphereHitsOBB(const xVec3& c, F32 r, const xBox& b, const xMat4x3& mat)
{
    xVec3 lc;
    xMat4x3Tolocal(&lc, &mat, &c);
    return xSphereHitsBox(lc, r, b);
}

inline bool xSphereHitsOBB(const xSphere& o, const xBox& b, const xMat4x3& mat)
{
    return xSphereHitsOBB(o.center, o.r, b, mat);
}
void xSphereForModel(xSphere& o, const xModelInstance* model, bool childModels);
void xBoxForModelLocal(xBox& o, const xModelInstance* m, bool childModels);
void xBoxForModel(xBox& o, const xModelInstance* m, bool childModels);
void xBoxForTriangle(xBox& box, const xVec3& p0, const xVec3& p1, const xVec3& p2);
_xCollsIdx xCollideGetCollsIdx(const xCollis* coll, const xVec3* tohit, const xMat3x3* mat);

S32 xParabolaHitsEnv(xParabola* p, const xEnv* env, xCollis* colls);
void xParabolaEvalPos(const xParabola*, xVec3*, F32);
void xParabolaEvalVel(const xParabola*, xVec3*, F32);
void xParabolaRecenter(xParabola* p, F32 newZeroT);

xVec3 xCollisTriHit(const xCollis::tri_data& tri, const xModelInstance& model);
void xCollideCalcTri(xCollis::tri_data& tri, const xModelInstance& model, const xVec3& center,
                     const xVec3& heading);
inline void xParabolaEvalPos(const xParabola* p, xVec3* pos, F32 time)
{
    xVec3Copy(pos, &p->initPos);
    xVec3AddScaled(pos, &p->initVel, time);
    pos->y -= 0.5f * p->gravity * time * time;
}

inline void xParabolaEvalVel(const xParabola* p, xVec3* vel, F32 time)
{
    xVec3Copy(vel, &p->initVel);
    vel->y -= p->gravity * time;
}

inline bool xSphereHitsVCircle(const xSphere& s, const xVec3& c, F32 r)
{
    return xSphereHitsVCircle(s.center, s.r, c, r);
}

#endif
