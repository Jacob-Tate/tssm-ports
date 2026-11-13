#include "xCollide.h"
#include "xCollideSpherePolygon.h"
#include "xCollideSpherePolygonUnprocessed.h"
#include "xClumpColl.h"
#include "xCollideFast.h"
#include "xBound.h"
#include "xEnt.h"
#include "xMath3.h"
#include "xModel.h"
#include "xQuickCull.h"
#include "xRay3.h"
#include "xScene.h"
#include "xVec3Inlines.h"
#include "zGrid.h"

#include <rwsdk/rpcollis.h>

struct zSweptSphereGlobals
{
    S32 hitFound;
    xMat4x3* modelMat;
};

struct SweptSphereCollParam
{
    RpGeometry* geometry;
    xSweptSphere* sws;
};

static zSweptSphereGlobals sSwsGlobal;
static S32 sSweptSphereEntFound;
static xEnt* sSweptSphereMover;
static U8 sSweptSphereCollType;
static S32 sSweptSpherePenby;
static U32 sSweptSphereIgnoreMovers;

static void SweptSphereHitsEntCB(xScene*, xRay3* ray, xQCData* qcd, xEnt* ent, void* data);
bool TestSingleEnvTri(xSweptSphere* sws, xClumpCollBSPTriangle* triangle,
                      xCollideSphereToPolygonResults& tri_collide, xPlane& surface);

// Normalizes v and returns its old length (0, leaving v alone, for lengths under 0.0001).
// No symbol (always inlined); name from the Ratatouille GC DWARF.
static inline F32 Normalize(xVec3& v)
{
    F32 len = v.length();

    if (len < 0.0001f)
    {
        return 0.0f;
    }

    xVec3SMulBy(&v, 1.0f / len);

    return len;
}

void xSweptSpherePrepare(xSweptSphere* sws, const xVec3* start, const xVec3* end, F32 radius)
{
    sws->jsp = NULL;
    sws->bspTriangle = NULL;
    sws->type = exCOLLIDESPHEREHITTYPE_UNKNOWN;
    sws->start = *start;
    sws->end = *end;
    sws->radius = radius;
    sws->optr = NULL;
    sws->mptr = NULL;

    xVec3Sub(&sws->dir, &sws->end, &sws->start);
    sws->dist = xVec3NormalizeSafe(sws->dir);

    if (sws->dist < 0.0001f)
    {
        sws->dist = 0.0f;
        sws->curdist = 0.0f;
        return;
    }

    sws->curdist = sws->dist;
    sws->init_collide = FALSE;
    sws->detect_initial_penetration = FALSE;

    xCapsule tmpC;
    tmpC.start = *start;
    tmpC.end = *end;
    tmpC.r = radius;
    xBoxInitBoundCapsule(&sws->box, &tmpC);

    // Probably the 2-arg xQuickCullForBox inline originally (same code). That inline is only
    // defined in xWad4.cpp, after its out-of-line callers there, so it is not visible here.
    xQuickCullForBox(&xqc_def_ctrl, &sws->qcd, &sws->box);
}

void xSweptSphereGetResults(xSweptSphere* sws)
{
    F32 tandot;
    xVec3 tanplane;
    xVec3 in_plane;

    sws->hitIt = 0;

    if (sws->init_collide)
    {
        return;
    }

    if (!sws->dist)
    {
        return;
    }

    if (sws->curdist == sws->dist)
    {
        return;
    }

    sws->hitIt = 1;

    xVec3AddScaled(sws->worldPos, sws->start, sws->dir, sws->curdist);

    sws->worldNormal.x = sws->worldPos.x - sws->worldContact.x;
    sws->worldNormal.y = sws->worldPos.y - sws->worldContact.y;
    sws->worldNormal.z = sws->worldPos.z - sws->worldContact.z;

    xVec3Normalize(&sws->worldNormal, &sws->worldNormal);

    xVec3Cross(&tanplane, &sws->dir, &sws->worldNormal);
    if (xVec3Length2(&tanplane) < 1e-7f)
    {
        // The swept direction is parallel to the normal: build the tangent plane from a vector
        // across the direction instead (dir x Y, or dir x X when dir is close to the X axis).
        if (sws->dir.x > 0.8f || sws->dir.x < -0.8f)
        {
            in_plane.x = -sws->dir.z;
            in_plane.y = 0.0f;
            in_plane.z = sws->dir.x;
        }
        else
        {
            in_plane.x = 0.0f;
            in_plane.y = sws->dir.z;
            in_plane.z = -sws->dir.y;
        }

        xVec3NormalizeSafe(in_plane);
        xVec3Cross(&tanplane, &sws->dir, &in_plane);
    }

    xVec3Cross(&sws->worldTangent, &tanplane, &sws->worldNormal);
    xVec3Normalize(&sws->worldTangent, &sws->worldTangent);

    tandot = sws->worldTangent.x * sws->dir.x + sws->worldTangent.y * sws->dir.y +
             sws->worldTangent.z * sws->dir.z;
    if (tandot < 0.0f)
    {
        sws->worldTangent.x = -sws->worldTangent.x;
        sws->worldTangent.y = -sws->worldTangent.y;
        sws->worldTangent.z = -sws->worldTangent.z;
    }
}

inline bool TestSingleEnvTri(xSweptSphere* sws, xClumpCollBSPTriangle* triangle,
                             xCollideSphereToPolygonResults& tri_collide, xPlane& surface)
{
    if (!xClumpColl_IsCollidableTriangle(triangle->flags))
    {
        return false;
    }

    xVec3* p0;
    xVec3* p1;
    xVec3* p2;

    if (triangle->flags & 0x2)
    {
        p0 = (xVec3*)&triangle->v.p[0];
        p1 = (xVec3*)&triangle->v.p[1];
        p2 = (xVec3*)&triangle->v.p[2];
    }
    else
    {
        p0 = (xVec3*)&triangle->v.p[0];
        p1 = (xVec3*)&triangle->v.p[2];
        p2 = (xVec3*)&triangle->v.p[1];
    }

    xBox tri_box;
    xBoxForTriangle(tri_box, *p0, *p1, *p2);

    if (!xBoxHitsBox(tri_box, sws->box))
    {
        return false;
    }

    return xSweptSphereToTriangleUnprocessed(*sws, *p0, *p1, *p2, tri_collide, surface);
}

static S32 SweptSphereLeafNodeCB(xClumpCollBSPTriangle* triangles, void* data)
{
    xSweptSphere* sws = (xSweptSphere*)data;
    xCollideSphereToPolygonResults tri_collide;
    xPlane surface;

    while (true)
    {
        if (TestSingleEnvTri(sws, triangles, tri_collide, surface) == true)
        {
            sws->worldContact = tri_collide.contact;
            sws->tri_plane.norm = surface.norm;
            sws->tri_plane.d = surface.d;
            sws->curdist = tri_collide.dist;
            sws->oid = triangles->matIndex;
            sws->optr = NULL;
            sws->mptr = NULL;
            sws->bspTriangle = triangles;
            sws->type = tri_collide.type;
            sSwsGlobal.hitFound = 1;
        }

        if (!(triangles->flags & 0x1))
        {
            break;
        }

        triangles++;
    }

    return 1;
}

S32 xSweptSphereToEnv(xSweptSphere* sws, xEnv* env)
{
    if (!sws->curdist)
    {
        return 0;
    }

    if (sws->detect_initial_penetration)
    {
        xCollis coll;
        xSphere s;

        coll.optr = NULL;
        coll.mptr = NULL;
        coll.flags = k_HIT_0x200 | k_HIT_0x400;

        s.center = sws->start;
        s.r = sws->radius;

        xSphereHitsEnv(&s, env, &coll);

        if (coll.flags & k_HIT_IT)
        {
            sws->init_collide = 1;
            sws->worldPos = sws->start;
            sws->worldNormal = coll.norm;
            sws->worldContact = coll.tohit + sws->start;
            sws->optr = coll.optr;
            sws->mptr = coll.mptr;
            sws->oid = coll.oid;
            return 1;
        }
    }

    sSwsGlobal.hitFound = 0;

    RwLine line;
    line.start = *(RwV3d*)&sws->start;
    line.end = *(RwV3d*)&sws->end;

    RwV3d delta;
    RwV3dSubMacro(&delta, &line.end, &line.start);

    xClumpCollV3dGradient grad;
    F32 recip;

    recip = (delta.x != 0.0f) ? (1.0f / delta.x) : 0.0f;
    grad.dydx = delta.y * recip;
    grad.dzdx = delta.z * recip;

    recip = (delta.y != 0.0f) ? (1.0f / delta.y) : 0.0f;
    grad.dxdy = delta.x * recip;
    grad.dzdy = delta.z * recip;

    recip = (delta.z != 0.0f) ? (1.0f / delta.z) : 0.0f;
    grad.dxdz = delta.x * recip;
    grad.dydz = delta.y * recip;

    for (S32 i = 0; i < env->geom->jsp_count; i++)
    {
        if (!xBoxHitsBox(sws->box, env->ienv.jsp_bound[i]))
        {
            continue;
        }

        F32 testdist = sws->curdist;

        sws->bspTriangle = NULL;
        sws->jsp = env->geom->jsp_list[i];

        xClumpColl_ForAllCapsuleLeafNodeIntersections(env->geom->jsp_list[i]->colltree, &line,
                                                      sws->radius, &grad, SweptSphereLeafNodeCB,
                                                      sws);

        if (sws->curdist >= testdist)
        {
            continue;
        }

        sws->oid = get_matid(env->geom->jsp_list[i], sws->oid, 0);

        xSweptSphere newsws;
        newsws = *sws;

        for (i++; i < env->geom->jsp_count; i++)
        {
            if (!xBoxHitsBox(newsws.box, env->ienv.jsp_bound[i]))
            {
                continue;
            }

            F32 testdist = newsws.curdist;

            newsws.bspTriangle = NULL;
            newsws.jsp = env->geom->jsp_list[i];

            xClumpColl_ForAllCapsuleLeafNodeIntersections(env->geom->jsp_list[i]->colltree, &line,
                                                          newsws.radius, &grad,
                                                          SweptSphereLeafNodeCB, &newsws);

            if (newsws.curdist >= testdist)
            {
                continue;
            }

            *sws = newsws;
        }

        break;
    }

    return sSwsGlobal.hitFound;
}

static S32 SweptSphereModelCB(S32 numTriangles, S32 triOffset, void* data)
{
    // Unused, but retail keeps this static in xWad1's .bss.
    static xCollideProcessedTri sSweptSphereModelCB_triangle;
    SweptSphereCollParam* isData = (SweptSphereCollParam*)data;
    RpGeometry* geometry = isData->geometry;
    xSweptSphere* sws = isData->sws;
    RwV3d* vertices = geometry->morphTarget->verts;
    RpTriangle* triangles = geometry->triangles;
    S32 triSlot;
    U16* triIndex = RpCollTreeGetEntryMap(RpCollisionGeometryGetCollTree(geometry));

    if (triIndex)
    {
        triIndex += triOffset;
        triSlot = 0;
    }
    else
    {
        triSlot = triOffset;
    }

    while (numTriangles--)
    {
        if (triIndex)
        {
            triSlot = *triIndex++;
        }
        else
        {
            triSlot++;
        }

        RpTriangle* tri = &triangles[triSlot];
        S32 vertIndex0 = tri->vertIndex[0];
        S32 vertIndex1 = tri->vertIndex[1];
        S32 vertIndex2 = tri->vertIndex[2];
        RwV3d* v0 = &vertices[vertIndex0];
        RwV3d* v1 = &vertices[vertIndex1];
        RwV3d* v2 = &vertices[vertIndex2];

        xBox tri_box;
        xBoxForTriangle(tri_box, *(xVec3*)v0, *(xVec3*)v1, *(xVec3*)v2);

        if (xBoxHitsBox(tri_box, sws->box))
        {
            xCollideSphereToPolygonResults tri_collide;

            if (xSweptSphereToTriangleDoubleSidedUnprocessed(*sws, *(xVec3*)v0, *(xVec3*)v1,
                                                             *(xVec3*)v2, tri_collide,
                                                             sws->tri_plane) == true)
            {
                xMat4x3Toworld(&sws->worldContact, sSwsGlobal.modelMat, &tri_collide.contact);
                sws->curdist = tri_collide.dist;
                sws->type = tri_collide.type;
                sSwsGlobal.hitFound = 1;
            }
        }
    }

    return 1;
}

// Capsule variant of RenderWare's RpCollSectorLineIt: a line iterator over a collision tree with
// every sector widened by the capsule radius. Layout from the PS2 DWARF; macro names from the
// Ratatouille GC DWARF.
struct RpCollSectorCapsuleIt
{
    RpCollTree* tree;
    RwV3d start;
    RwV3d delta;
    RwV3d recip;
    F32 radius;
    RpCollSector dummy;
    RpCollSectorLineItNodeInfo stack[rpCOLLTREE_MAXDEPTH + 1];
    RpCollSectorLineItNodeInfo* curr;
};

#define _rpCollSectorCapsuleItFinishedMacro(_it) ((_it)->curr <= (_it)->stack)

/*
 *  Test intersection of the line (_start to _end on one axis) with the interval (_inf, _sup)
 *  widened by _rad. (pa,pb) are parameters for the range of the line and on completion give
 *  the range of the intersection.
 */
#define _rpCollCapsuleIntervalTestMacro(_result, _start, _end, _inf, _sup, _rad, _pa, _pb)         \
    MACRO_START                                                                                    \
    {                                                                                              \
        if ((_start) == (_end))                                                                    \
        {                                                                                          \
            (_result) = ((_start) >= (_inf) - (_rad) && (_start) <= (_sup) + (_rad));              \
        }                                                                                          \
        else                                                                                       \
        {                                                                                          \
            F32 recip = 1.0f / ((_end) - (_start));                                                \
            F32 p0;                                                                                \
            F32 p1;                                                                                \
                                                                                                   \
            if (recip > 0.0f)                                                                      \
            {                                                                                      \
                p0 = recip * (((_inf) - (_rad)) - (_start));                                       \
                p1 = recip * (((_sup) + (_rad)) - (_start));                                       \
            }                                                                                      \
            else                                                                                   \
            {                                                                                      \
                p1 = recip * (((_inf) - (_rad)) - (_start));                                       \
                p0 = recip * (((_sup) + (_rad)) - (_start));                                       \
            }                                                                                      \
                                                                                                   \
            (_result) = ((_pa) <= p1) && ((_pb) >= p0);                                            \
            if (_result)                                                                           \
            {                                                                                      \
                (_pa) = RwRealMax2((_pa), p0);                                                     \
                (_pb) = RwRealMin2((_pb), p1);                                                     \
            }                                                                                      \
        }                                                                                          \
    }                                                                                              \
    MACRO_STOP

#define _rpCollSectorCapsuleItInitMacro(_it, _tree, _line, _radius)                                \
    MACRO_START                                                                                    \
    {                                                                                              \
        F32 pa = 0.0f;                                                                             \
        F32 pb = 1.0f;                                                                             \
        S32 result;                                                                                \
                                                                                                   \
        _rpCollCapsuleIntervalTestMacro(result, (_line)->start.x, (_line)->end.x,                  \
                                        (_tree)->bbox.inf.x, (_tree)->bbox.sup.x, (_radius), pa,   \
                                        pb);                                                       \
        if (result)                                                                                \
        {                                                                                          \
            _rpCollCapsuleIntervalTestMacro(result, (_line)->start.y, (_line)->end.y,              \
                                            (_tree)->bbox.inf.y, (_tree)->bbox.sup.y, (_radius),   \
                                            pa, pb);                                               \
            if (result)                                                                            \
            {                                                                                      \
                _rpCollCapsuleIntervalTestMacro(result, (_line)->start.z, (_line)->end.z,          \
                                                (_tree)->bbox.inf.z, (_tree)->bbox.sup.z,          \
                                                (_radius), pa, pb);                                \
            }                                                                                      \
        }                                                                                          \
                                                                                                   \
        if (result)                                                                                \
        {                                                                                          \
            (_it)->tree = (_tree);                                                                 \
            (_it)->dummy.index = 0;                                                                \
            (_it)->dummy.contents = rpCOLLSECTOR_CONTENTS_SPLIT;                                   \
            if ((_tree)->numSplits == 0)                                                           \
            {                                                                                      \
                (_it)->dummy.contents = (RwUInt8)(_tree)->numEntries;                              \
            }                                                                                      \
                                                                                                   \
            (_it)->start = (_line)->start;                                                         \
            RwV3dSubMacro(&(_it)->delta, &(_line)->end, &(_line)->start);                          \
                                                                                                   \
            (_it)->recip.x = ((_it)->delta.x != 0) ? (1 / (_it)->delta.x) : 0;                     \
            (_it)->recip.y = ((_it)->delta.y != 0) ? (1 / (_it)->delta.y) : 0;                     \
            (_it)->recip.z = ((_it)->delta.z != 0) ? (1 / (_it)->delta.z) : 0;                     \
            (_it)->radius = (_radius);                                                             \
                                                                                                   \
            (_it)->stack[0].sector = &(_it)->dummy;                                                \
            (_it)->stack[0].pa = pa - 0.00001f;                                                    \
            (_it)->stack[0].pb = pb + 0.00001f;                                                    \
            (_it)->curr = (_it)->stack + 1;                                                        \
        }                                                                                          \
        else                                                                                       \
        {                                                                                          \
            (_it)->curr = (_it)->stack;                                                            \
        }                                                                                          \
    }                                                                                              \
    MACRO_STOP

/*
 *  Process a branch node of the collision tree during capsule iteration
 *
 *  _it                 pointer to RpCollSectorCapsuleIt
 *  _axis               0,4,8 for x,y,z
 *  _left, _right       pointers to left and right sectors
 *  _leftHi, _rightLo   values for left and right sector boundaries
 */
#define _rpCollCapsuleItStepMacro(_it, _axis, _left, _right, _leftHi, _rightLo)                    \
    MACRO_START                                                                                    \
    {                                                                                              \
        F32 del, sta, end, rec, rad;                                                               \
                                                                                                   \
        del = GETCOORD((_it)->delta, (_axis));                                                     \
        sta = GETCOORD((_it)->start, (_axis));                                                     \
        rad = (_it)->radius;                                                                       \
        end = sta + (_it)->curr->pb * del;                                                         \
        sta = sta + (_it)->curr->pa * del;                                                         \
                                                                                                   \
        if (del > 0)                                                                               \
        {                                                                                          \
            /* Line goes from left to right */                                                     \
            if ((_rightLo) > end + rad)                                                            \
            {                                                                                      \
                /* Completely in left */                                                           \
                (_it)->curr->sector = (_left);                                                     \
            }                                                                                      \
            else if ((_leftHi) < sta - rad)                                                        \
            {                                                                                      \
                /* Completely in right */                                                          \
                (_it)->curr->sector = (_right);                                                    \
            }                                                                                      \
            else                                                                                   \
            {                                                                                      \
                (_it)->curr++;                                                                     \
                rec = GETCOORD((_it)->recip, (_axis));                                             \
                                                                                                   \
                /* Go left (near sector) */                                                        \
                (_it)->curr->sector = (_left);                                                     \
                (_it)->curr->pa = ((_it)->curr - 1)->pa;                                           \
                (_it)->curr->pb = ((_it)->curr - 1)->pb;                                           \
                if ((_leftHi) < end)                                                               \
                {                                                                                  \
                    /* Clip end for near line segment */                                           \
                    (_it)->curr->pb += rec * ((_leftHi) - end) + 0.00001f;                         \
                }                                                                                  \
                                                                                                   \
                /* Stack right (far sector) */                                                     \
                ((_it)->curr - 1)->sector = (_right);                                              \
                if ((_rightLo) > sta)                                                              \
                {                                                                                  \
                    /* Clip start for far line segment */                                          \
                    ((_it)->curr - 1)->pa += rec * ((_rightLo) - sta) - 0.00001f;                  \
                }                                                                                  \
            }                                                                                      \
        }                                                                                          \
        else                                                                                       \
        {                                                                                          \
            /* Line goes from right to left */                                                     \
            if ((_leftHi) < end - rad)                                                             \
            {                                                                                      \
                /* Completely in right */                                                          \
                (_it)->curr->sector = (_right);                                                    \
            }                                                                                      \
            else if ((_rightLo) > sta + rad)                                                       \
            {                                                                                      \
                /* Completely in left */                                                           \
                (_it)->curr->sector = (_left);                                                     \
            }                                                                                      \
            else                                                                                   \
            {                                                                                      \
                (_it)->curr++;                                                                     \
                rec = GETCOORD((_it)->recip, (_axis));                                             \
                                                                                                   \
                /* Go right (near sector) */                                                       \
                (_it)->curr->sector = (_right);                                                    \
                (_it)->curr->pa = ((_it)->curr - 1)->pa;                                           \
                (_it)->curr->pb = ((_it)->curr - 1)->pb;                                           \
                if ((_rightLo) > end)                                                              \
                {                                                                                  \
                    /* Clip end for near line segment */                                           \
                    (_it)->curr->pb += rec * ((_rightLo) - end) + 0.00001f;                        \
                }                                                                                  \
                                                                                                   \
                /* Stack left (far sector) */                                                      \
                ((_it)->curr - 1)->sector = (_left);                                               \
                if ((_leftHi) < sta)                                                               \
                {                                                                                  \
                    /* Clip start for far line segment */                                          \
                    ((_it)->curr - 1)->pa += rec * ((_leftHi) - sta) - 0.00001f;                   \
                }                                                                                  \
            }                                                                                      \
        }                                                                                          \
    }                                                                                              \
    MACRO_STOP

// Calls callBack(count, start, data) for every leaf of the collision tree that a capsule of the
// given radius around the line can touch.
void RpCollTreeForAllCapsuleIntsec(RpCollTree* tree, RwLine* line, F32 radius,
                                   S32 (*callBack)(S32, S32, void*), void* data)
{
    RpCollSectorCapsuleIt it;

    _rpCollSectorCapsuleItInitMacro(&it, tree, line, radius);

    while (!_rpCollSectorCapsuleItFinishedMacro(&it))
    {
        it.curr--;

        while (it.curr->sector->contents == rpCOLLSECTOR_CONTENTS_SPLIT)
        {
            RpCollSplit* split = it.tree->splits + it.curr->sector->index;

            _rpCollCapsuleItStepMacro(&it, split->leftSector.type & rpCOLLSECTOR_TYPE_AXISMASK,
                                      &split->leftSector, &split->rightSector,
                                      split->leftSector.value, split->rightSector.value);
        }

        callBack(it.curr->sector->contents, it.curr->sector->index, data);
    }
}

// No symbol (inlined into its only caller); name from the Ratatouille GC DWARF. Returns 1 when the
// triangle's bounds cannot touch the swept sphere's box.
static U8 GrossCullTri(xSweptSphere* sws, xVec3* v0, xVec3* v1, xVec3* v2)
{
    F32 minX = MIN(v0->x, v1->x);
    F32 maxX = MAX(v0->x, v1->x);
    minX = MIN(minX, v2->x);
    maxX = MAX(maxX, v2->x);

    if (sws->box.upper.x <= minX || sws->box.lower.x >= maxX)
    {
        return 1;
    }

    F32 minY = MIN(v0->y, v1->y);
    F32 maxY = MAX(v0->y, v1->y);
    minY = MIN(minY, v2->y);
    maxY = MAX(maxY, v2->y);

    if (sws->box.upper.y <= minY || sws->box.lower.y >= maxY)
    {
        return 1;
    }

    F32 minZ = MIN(v0->z, v1->z);
    F32 maxZ = MAX(v0->z, v1->z);
    minZ = MIN(minZ, v2->z);
    maxZ = MAX(maxZ, v2->z);

    if (sws->box.upper.z <= minZ || sws->box.lower.z >= maxZ)
    {
        return 1;
    }

    return 0;
}

// Sweeps the sphere against a model: the sweep is moved into the model's local space (radius and
// distances divided by the model's scale), tested against the model's collision tree (or every
// triangle when it has none), and then restored.
S32 xSweptSphereToModel(xSweptSphere* sws, RpAtomic* model, const RwMatrix* mat)
{
    if (sws->dist <= 0.0f || sws->curdist <= 0.0f)
    {
        return 0;
    }

    sSwsGlobal.hitFound = 0;
    sSwsGlobal.modelMat = (xMat4x3*)mat;

    xVec3 old_start = sws->start;
    xVec3 old_end = sws->end;
    xVec3 old_dir = sws->dir;
    F32 old_radius = sws->radius;
    xBox old_box = sws->box;
    F32 old_dist = sws->dist;
    F32 model_scale = xVec3Length((const xVec3*)&mat->at);

    xMat4x3Tolocal(&sws->start, (const xMat4x3*)mat, &old_start);
    xMat4x3Tolocal(&sws->end, (const xMat4x3*)mat, &old_end);
    xVec3Sub(&sws->dir, &sws->end, &sws->start);

    sws->dist = Normalize(sws->dir);

    if (sws->dist == 0.0f)
    {
        sws->start = old_start;
        sws->end = old_end;
        sws->dir = old_dir;
        sws->dist = old_dist;
        return 0;
    }

    F32 old_curdist = sws->curdist;
    sws->radius /= model_scale;
    sws->curdist /= model_scale;
    F32 start_curdist = sws->curdist;

    xBoxFromCapsule(sws->box, sws->start, sws->end, sws->radius);

    RpGeometry* geom = model->geometry;
    RpCollisionData* colldata = _rpCollisionGeometryGetDataMacro(geom);

    if (colldata && colldata->tree)
    {
        F32 scale;
        xsqrtfast(scale, SQR(mat->right.x) + SQR(mat->right.y) + SQR(mat->right.z));

        RwLine line;
        *(xVec3*)&line.start = sws->start;
        *(xVec3*)&line.end = sws->end;

        SweptSphereCollParam isData;
        isData.geometry = geom;
        isData.sws = sws;

        RpCollTreeForAllCapsuleIntsec(colldata->tree, &line, sws->radius / scale,
                                      SweptSphereModelCB, &isData);
    }
    else
    {
        RpTriangle* tri = geom->triangles;
        RwV3d* vert = geom->morphTarget->verts;

        for (S32 i = 0; i < geom->numTriangles; i++)
        {
            xVec3* p0 = (xVec3*)&vert[tri->vertIndex[0]];
            xVec3* p1 = (xVec3*)&vert[tri->vertIndex[1]];
            xVec3* p2 = (xVec3*)&vert[tri->vertIndex[2]];

            if (!GrossCullTri(sws, p0, p1, p2))
            {
                xCollideSphereToPolygonResults tri_collide;
                xPlane surface;

                if (xSweptSphereToTriangleDoubleSidedUnprocessed(*sws, *p0, *p1, *p2, tri_collide,
                                                                 surface) == true)
                {
                    xMat4x3Toworld(&sws->worldContact, sSwsGlobal.modelMat, &tri_collide.contact);
                    sws->curdist = tri_collide.dist;
                    sws->type = tri_collide.type;
                    sSwsGlobal.hitFound = 1;
                }
            }

            tri++;
        }
    }

    sws->start = old_start;
    sws->end = old_end;
    sws->dir = old_dir;
    sws->radius = old_radius;
    sws->dist = old_dist;

    if (sws->curdist < start_curdist)
    {
        sws->curdist *= model_scale;
    }
    else
    {
        sws->curdist = old_curdist;
    }

    sws->box = old_box;

    return sSwsGlobal.hitFound;
}

// One 'ratio' local shared by both branches, as in the PS2 DWARF. dist_between comes from
// Normalize, which keeps this body small enough that the weak xSweptSphereToSphere wrapper and
// xSweptSphereToBoxInside stay out of line, as in retail.
S32 xSweptSphereToSphere(xSweptSphere& sws, const xVec3& center, F32 radius)
{
    if (sws.curdist <= 0.0f)
    {
        return 0;
    }

    xVec3 center_to_center;
    xVec3Sub(&center_to_center, &center, &sws.start);
    F32 dist_along = center_to_center.dot(sws.dir);

    if (dist_along < 0.001f)
    {
        return 0;
    }

    xVec3 along;
    xVec3SMul(&along, &sws.dir, dist_along);
    xVec3 out;
    xVec3Sub(&out, &center_to_center, &along);
    F32 out_dist_squ = out.length2();
    F32 combined_raidus = sws.radius + radius;
    F32 combined_radius_squ = combined_raidus * combined_raidus;

    if (out_dist_squ + 0.0001f >= combined_radius_squ)
    {
        return 0;
    }

    F32 stop_dist = xsqrt(combined_radius_squ - out_dist_squ);
    F32 dist_to_collide = dist_along - stop_dist;

    if (dist_to_collide < 0.0001f)
    {
        // Already touching: report the contact on the line between the centers.
        sws.curdist = 0.0f;

        F32 dist_between = Normalize(center_to_center);

        xVec3AddScaled(sws.worldContact, center, center_to_center,
                       -dist_between * (radius / combined_raidus));
        return 1;
    }

    if (sws.curdist <= dist_to_collide)
    {
        return 0;
    }

    sws.curdist = dist_to_collide;

    F32 ratio = sws.radius / combined_raidus;
    xVec3AddScaled(sws.worldContact, sws.start, out, ratio);
    xVec3AddScaled(&sws.worldContact, &sws.dir, ratio * stop_dist + sws.curdist);
    return 1;
}

S32 xSweptSphereToBound(xSweptSphere* sws, const xBound* bound)
{
    switch (bound->type)
    {
    case XBOUND_TYPE_SPHERE:
        return xSweptSphereToSphere(sws, &bound->sph);
    case XBOUND_TYPE_BOX:
        return xSweptSphereToBox(*sws, bound->box.box);
    case XBOUND_TYPE_OBB:
        return xSweptSphereToOBB(sws, &bound->box.box, bound->mat);
    }

    return 0;
}

S32 xSweptSphereToTriangle(xSweptSphere& sws, const xVec3& v0, const xVec3& v1, const xVec3& v2)
{
    xPlane surface;
    xCollideSphereToPolygonResults results;

    if (!xSweptSphereToTriangleUnprocessed(sws, v0, v1, v2, results, surface))
    {
        return 0;
    }

    sws.curdist = results.dist;
    sws.worldContact = results.contact;
    sws.type = results.type;

    return 1;
}

static void SweptSphereHitsEntCB(xScene*, xRay3* ray, xQCData* qcd, xEnt* ent, void* data)
{
    xSweptSphere* sws = (xSweptSphere*)data;

    if (!xQuickCullIsects(qcd, &ent->bound.qcd))
    {
        return;
    }

    if (ent == sSweptSphereMover)
    {
        return;
    }

    if (!(sSweptSphereCollType & ent->chkby))
    {
        return;
    }

    if (sSweptSpherePenby && !(sSweptSpherePenby & ent->penby))
    {
        return;
    }

    if (sSweptSphereIgnoreMovers)
    {
        if ((ent->collModel ? ent->collModel : ent->model)->Flags & 0x800)
        {
            return;
        }

        if (ent->frame)
        {
            xMat4x3* m1 = &ent->frame->oldmat;
            xMat4x3* m2 = (xMat4x3*)ent->model->Mat;

            if (m1->pos.x != m2->pos.x || m1->pos.y != m2->pos.y || m1->pos.z != m2->pos.z ||
                m1->right.x != m2->right.x || m1->right.y != m2->right.y ||
                m1->right.z != m2->right.z || m1->up.y != m2->up.y || m1->up.z != m2->up.z ||
                m1->at.z != m2->at.z)
            {
                return;
            }
        }
    }

    sSweptSphereEntFound |= xSweptSphereToEnt(sws, ent, ray);
}

S32 xSweptSphereToEnt(xSweptSphere* sws, xEnt* ent, xRay3* ray)
{
    if (ent->collLev == 5)
    {
        U32 result = 0;

        switch (ent->bound.type)
        {
        case XBOUND_TYPE_SPHERE:
        {
            F32 oldrad = ent->bound.sph.r;

            ent->bound.sph.r += sws->radius;

            result = xRayHitsSphereFast(ray, &ent->bound.sph);

            ent->bound.sph.r = oldrad;

            break;
        }
        case XBOUND_TYPE_BOX:
        {
            xBox tmpbox;
            tmpbox.upper.x = ent->bound.box.box.upper.x + sws->radius;
            tmpbox.upper.y = ent->bound.box.box.upper.y + sws->radius;
            tmpbox.upper.z = ent->bound.box.box.upper.z + sws->radius;
            tmpbox.lower.x = ent->bound.box.box.lower.x - sws->radius;
            tmpbox.lower.y = ent->bound.box.box.lower.y - sws->radius;
            tmpbox.lower.z = ent->bound.box.box.lower.z - sws->radius;

            result = xRayHitsBoxFast(ray, &tmpbox);

            break;
        }
        case XBOUND_TYPE_OBB:
        {
            xBox tmpbox;
            xRay3 lr;
            xMat3x3 mn;

            F32 ms = xVec3Length(&ent->bound.mat->right);

            xMat3x3Normalize(&mn, ent->bound.mat);
            xMat4x3Tolocal(&lr.origin, ent->bound.mat, &ray->origin);
            xMat3x3Tolocal(&lr.dir, &mn, &ray->dir);

            lr.max_t = ray->max_t / ms;
            lr.min_t = ray->min_t / ms;
            lr.flags = ray->flags;

            tmpbox.upper.x = ent->bound.box.box.upper.x + sws->radius / ms;
            tmpbox.upper.y = ent->bound.box.box.upper.y + sws->radius / ms;
            tmpbox.upper.z = ent->bound.box.box.upper.z + sws->radius / ms;
            tmpbox.lower.x = ent->bound.box.box.lower.x - sws->radius / ms;
            tmpbox.lower.y = ent->bound.box.box.lower.y - sws->radius / ms;
            tmpbox.lower.z = ent->bound.box.box.lower.z - sws->radius / ms;

            result = xRayHitsBoxFast(&lr, &tmpbox);

            break;
        }
        }

        if (!result)
        {
            return 0;
        }

        xModelInstance* collmod = ent->collModel ? ent->collModel : ent->model;

        if (collmod->Flags & 0x800)
        {
            xModelAnimCollApply(*collmod);
        }

        result = 0;

        if (xModelIsPreinstanced(collmod))
        {
            return 0;
        }

        if (xSweptSphereToModel(sws, collmod->Data, collmod->Mat))
        {
            sws->mptr = ent->model;
            sws->oid = ent->id;
            sws->optr = ent;
            result = 1;
        }

        if (collmod->Flags & 0x800)
        {
            xModelAnimCollRestore(*collmod);
        }

        return result;
    }

    switch (ent->bound.type)
    {
    case XBOUND_TYPE_SPHERE:
        if (xSweptSphereToSphere(sws, &ent->bound.sph))
        {
            sws->mptr = ent->model;
            sws->oid = ent->id;
            sws->optr = ent;
            return 1;
        }
        break;
    case XBOUND_TYPE_BOX:
        if (xSweptSphereToBox(*sws, ent->bound.box.box))
        {
            sws->mptr = ent->model;
            sws->oid = ent->id;
            sws->optr = ent;
            return 1;
        }
        break;
    case XBOUND_TYPE_OBB:
        if (xSweptSphereToOBB(sws, &ent->bound.box.box, ent->bound.mat))
        {
            sws->mptr = ent->model;
            sws->oid = ent->id;
            sws->optr = ent;
            return 1;
        }
        break;
    }

    return 0;
}

S32 xSweptSphereToScene(xSweptSphere* sws, xScene* sc, xEnt* mover, U8 collType, S32 penby)
{
    sSweptSphereEntFound = 0;
    sSweptSphereMover = mover;
    sSweptSphereCollType = collType;
    sSweptSpherePenby = penby;

    S32 envcollfound = xSweptSphereToEnv(sws, sc->env);

    xRay3 ray;
    xVec3Copy(&ray.origin, &sws->start);
    xVec3Sub(&ray.dir, &sws->end, &sws->start);
    ray.max_t = xVec3Length(&ray.dir);

    F32 one_len = 1.0f / MAX(ray.max_t, 0.00001f);
    xVec3SMul(&ray.dir, &ray.dir, one_len);

    ray.flags = XRAY3_USE_MAX;
    if (!(ray.flags & XRAY3_USE_MIN))
    {
        ray.flags |= XRAY3_USE_MIN;
        ray.min_t = 0.0f;
    }

    xRayHitsGrid(&colls_grid, sc, &ray, SweptSphereHitsEntCB, &sws->qcd, sws);
    xRayHitsGrid(&colls_oso_grid, sc, &ray, SweptSphereHitsEntCB, &sws->qcd, sws);
    xRayHitsGrid(&npcs_grid, sc, &ray, SweptSphereHitsEntCB, &sws->qcd, sws);

    return sSweptSphereEntFound || envcollfound;
}

S32 xSweptSphereToNonMoving(xSweptSphere* sws, xScene* sc, xEnt* mover, U8 collType, S32 penby)
{
    sSweptSphereEntFound = 0;
    sSweptSphereMover = mover;
    sSweptSphereCollType = collType;
    sSweptSpherePenby = penby;

    S32 envcollfound = xSweptSphereToEnv(sws, sc->env);

    xVec3 diff;
    diff = sws->end - sws->start;
    if (diff.length() <= 0.00001f)
    {
        return envcollfound;
    }

    xRay3 ray = {};
    xVec3Copy(&ray.origin, &sws->start);
    xVec3Sub(&ray.dir, &sws->end, &sws->start);
    ray.max_t = xVec3Length(&ray.dir);

    F32 one_len = 1.0f / MAX(ray.max_t, 0.00001f);
    xVec3SMul(&ray.dir, &ray.dir, one_len);

    ray.flags = XRAY3_USE_MAX;
    if (!(ray.flags & XRAY3_USE_MIN))
    {
        ray.flags |= XRAY3_USE_MIN;
        ray.min_t = 0.0f;
    }

    sSweptSphereIgnoreMovers = 1;
    xRayHitsGrid(&colls_grid, sc, &ray, SweptSphereHitsEntCB, &sws->qcd, sws);
    xRayHitsGrid(&colls_oso_grid, sc, &ray, SweptSphereHitsEntCB, &sws->qcd, sws);
    sSweptSphereIgnoreMovers = 0;

    return sSweptSphereEntFound || envcollfound;
}

S32 xSweptSphereToStatDyn(xSweptSphere* sws, xScene* sc, xEnt* mover, U8 collType, S32 penby)
{
    sSweptSphereEntFound = 0;
    sSweptSphereMover = mover;
    sSweptSphereCollType = collType;
    sSweptSpherePenby = penby;

    if (!(sws->start.x - sws->end.x) && !(sws->start.y - sws->end.y) &&
        !(sws->start.z - sws->end.z))
    {
        return 0;
    }

    xRay3 ray;
    xVec3Copy(&ray.origin, &sws->start);
    xVec3Sub(&ray.dir, &sws->end, &sws->start);
    ray.max_t = xVec3Length(&ray.dir);

    F32 one_len = 1.0f / MAX(ray.max_t, 0.00001f);
    xVec3SMul(&ray.dir, &ray.dir, one_len);

    ray.flags = XRAY3_USE_MAX;
    if (!(ray.flags & XRAY3_USE_MIN))
    {
        ray.flags |= XRAY3_USE_MIN;
        ray.min_t = 0.0f;
    }

    xRayHitsGrid(&colls_grid, sc, &ray, SweptSphereHitsEntCB, &sws->qcd, sws);
    xRayHitsGrid(&colls_oso_grid, sc, &ray, SweptSphereHitsEntCB, &sws->qcd, sws);

    return sSweptSphereEntFound;
}

S32 xSweptSphereToNPC(xSweptSphere* sws, xScene* sc, xEnt* mover, U8 collType, S32 penby)
{
    sSweptSphereEntFound = 0;
    sSweptSphereMover = mover;
    sSweptSphereCollType = collType;
    sSweptSpherePenby = penby;

    if (!(sws->start.x - sws->end.x) && !(sws->start.y - sws->end.y) &&
        !(sws->start.z - sws->end.z))
    {
        return 0;
    }

    xRay3 ray;
    xVec3Copy(&ray.origin, &sws->start);
    xVec3Sub(&ray.dir, &sws->end, &sws->start);
    ray.max_t = xVec3Length(&ray.dir);

    F32 one_len = 1.0f / MAX(ray.max_t, 0.00001f);
    xVec3SMul(&ray.dir, &ray.dir, one_len);

    ray.flags = XRAY3_USE_MAX;
    if (!(ray.flags & XRAY3_USE_MIN))
    {
        ray.flags |= XRAY3_USE_MIN;
        ray.min_t = 0.0f;
    }

    xRayHitsGrid(&npcs_grid, sc, &ray, SweptSphereHitsEntCB, &sws->qcd, sws);

    return sSweptSphereEntFound;
}
