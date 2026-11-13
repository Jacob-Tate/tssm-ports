#include "xCollide.h"

struct xSphereHitsModel_context
{
    xCollis* coll;
    RpIntersection localx;
};

struct ParabolaCBData
{
    xParabola* p;
    xVec3 N;
    F32 d;
    xCollis* colls;
};

char* g_xCollis_FMT = "\thit=%s ave=%s cdist=%s ctohit=%s cdepen=%s\n\ttohit=<%.8f %.8f %.8f> "
                      "depen=<%.8f %.8f %.8f>\n\tnum_hits=%04d oidx=%d\n\tixtra[%s]";

static S32 sCollidingJSP;
static U8 cbnumcs;
static U8 cbmaxcs;
static U8 FLOOR;
static U8 NEXT2;
static U8 OTHER;
static F32 cbath;
static const xMat3x3* cbmat;
static xRay3 cbray;
static RpIntersection cbisx_local;

static RpCollisionTriangle* sphereHitsEnvCB(RpIntersection* isx, RpWorldSector* sector,
                                            RpCollisionTriangle* tri, F32 dist, void* data);
static RpCollisionTriangle* sphereHitsEnv3CB(RpIntersection* isx, RpWorldSector* sector,
                                             RpCollisionTriangle* tri, F32 dist, void* data);
static RpCollisionTriangle* rayHitsEnvCB(RpIntersection* isx, RpWorldSector* sector,
                                         RpCollisionTriangle* tri, F32 dist, void* data);
static RpCollisionTriangle* sphereHitsEnv4CB(RpIntersection* isx, RpWorldSector* sector,
                                             RpCollisionTriangle* tri, F32 dist, void* data);
static void properSphereIsectTri(const xVec3* center, F32 radius, xVec3* tohit, F32* dist,
                                 RpCollisionTriangle* tri);
static _xCollsIdx xCollideGetCollsIdxWithNormal(const xCollis* coll, const xVec3* tohit,
                                                const xVec3* normal, const xMat3x3* mat);
static U32 Mgc_BoxBoxTest(const xBox* a, const xMat4x3* matA, const xBox* b, const xMat4x3* matB);
static S32 xParabolaEnvCB(xClumpCollBSPTriangle* triangles, void* data);

enum Dimension
{
    XDIM,
    YDIM,
    ZDIM
};

_xCollsIdx xCollideGetCollsIdx(const xCollis* coll, const xVec3* tohit, const xMat3x3* mat)
{
    if (!(coll->flags & k_HIT_0x20000) && SQR(tohit->y) > SQR(tohit->x) + SQR(tohit->z))
    {
        if (coll->optr != NULL && coll->mptr->Surf != NULL ? zSurfaceGetStandOn(coll->mptr->Surf) :
                                                             1)
        {
            if (tohit->y < 0.0f)
            {
                return k_XCOLLS_IDX_FLOOR;
            }
            return k_XCOLLS_IDX_CEIL;
        }
    }

    F32 local_x = mat->right.x * tohit->x + mat->right.z * tohit->z;
    F32 local_z = mat->at.x * tohit->x + mat->at.z * tohit->z;

    if (local_x > 0.0f)
    {
        if (local_z > 0.0f)
        {
            if (local_x > local_z)
            {
                return k_XCOLLS_IDX_LEFT;
            }
            else
            {
                return k_XCOLLS_IDX_FRONT;
            }
        }
        else
        {
            if (local_x > -local_z)
            {
                return k_XCOLLS_IDX_LEFT;
            }
            else
            {
                return k_XCOLLS_IDX_REAR;
            }
        }
    }
    else
    {
        if (local_z > 0.0f)
        {
            if (local_x < -local_z)
            {
                return k_XCOLLS_IDX_RIGHT;
            }
            else
            {
                return k_XCOLLS_IDX_FRONT;
            }
        }
        else
        {
            if (local_x < local_z)
            {
                return k_XCOLLS_IDX_RIGHT;
            }
            else
            {
                return k_XCOLLS_IDX_REAR;
            }
        }
    }
}

static _xCollsIdx xCollideGetCollsIdxWithNormal(const xCollis* coll, const xVec3* tohit,
                                                const xVec3* normal, const xMat3x3* mat)
{
    if (!(coll->flags & k_HIT_0x20000) && SQR(tohit->y) > SQR(tohit->x) + SQR(tohit->z) &&
        SQR(normal->y) > SQR(normal->x) + SQR(normal->z))
    {
        if (coll->optr != NULL && coll->mptr->Surf != NULL ? zSurfaceGetStandOn(coll->mptr->Surf) :
                                                             1)
        {
            if (normal->y > 0.0f)
            {
                return k_XCOLLS_IDX_FLOOR;
            }
            return k_XCOLLS_IDX_CEIL;
        }
    }

    F32 local_x = mat->right.x * tohit->x + mat->right.z * tohit->z;
    F32 local_z = mat->at.x * tohit->x + mat->at.z * tohit->z;

    if (local_x > 0.0f)
    {
        if (local_z > 0.0f)
        {
            if (local_x > local_z)
            {
                return k_XCOLLS_IDX_LEFT;
            }
            else
            {
                return k_XCOLLS_IDX_FRONT;
            }
        }
        else
        {
            if (local_x > -local_z)
            {
                return k_XCOLLS_IDX_LEFT;
            }
            else
            {
                return k_XCOLLS_IDX_REAR;
            }
        }
    }
    else
    {
        if (local_z > 0.0f)
        {
            if (local_x < -local_z)
            {
                return k_XCOLLS_IDX_RIGHT;
            }
            else
            {
                return k_XCOLLS_IDX_FRONT;
            }
        }
        else
        {
            if (local_x < local_z)
            {
                return k_XCOLLS_IDX_RIGHT;
            }
            else
            {
                return k_XCOLLS_IDX_REAR;
            }
        }
    }
}

U32 xSphereHitsBox(const xSphere* a, const xBox* b, xCollis* coll)
{
    xIsect isx;
    F32 scale;

    iBoxIsectSphere(b, a, &isx);

    if (!(isx.penned <= 0.0f))
    {
        coll->flags &= ~k_HIT_IT;
        return 0;
    }

    if (isx.contained <= 0.0f)
    {
        coll->flags |= k_HIT_0x10;
    }

    coll->dist = isx.dist;

    if (coll->flags & k_HIT_0x400)
    {
        xVec3Copy(&coll->tohit, &isx.norm);
    }

    if (coll->flags & k_HIT_0x800)
    {
        if (isx.dist == 0.0f)
        {
            xVec3Copy(&coll->depen, &g_O3);
        }
        else
        {
            scale = isx.penned / isx.dist;
            xVec3SMul(&coll->depen, &isx.norm, scale);
        }
    }

    if (coll->flags & (k_HIT_0x200 | k_HIT_CALC_HDNG))
    {
        xVec3Normalize(&coll->hdng, &isx.norm);
    }

    if (coll->flags & k_HIT_0x200)
    {
        xVec3Inv(&coll->norm, &coll->hdng);
    }

    coll->flags |= k_HIT_IT;
    return 1;
}

U32 xSphereHitsOBB_nu(const xSphere* s, const xBox* b, const xMat4x3* m, xCollis* coll)
{
    xSphere xfs;
    xVec3 scale;
    xMat4x3 mnormal;

    xVec3NormalizeMacro(&mnormal.right, &m->right, &scale.x);
    xVec3NormalizeMacro(&mnormal.up, &m->up, &scale.y);
    xVec3NormalizeMacro(&mnormal.at, &m->at, &scale.z);
    mnormal.pos = m->pos;

    xBox sbox = *b;
    sbox.upper.x *= scale.x, sbox.upper.y *= scale.y, sbox.upper.z *= scale.z;
    sbox.lower.x *= scale.x, sbox.lower.y *= scale.y, sbox.lower.z *= scale.z;

    xMat4x3Tolocal(&xfs.center, &mnormal, &s->center);
    xfs.r = s->r;

    xSphereHitsBox(&xfs, &sbox, coll);

    if (!(coll->flags & k_HIT_IT))
    {
        return 0;
    }

    if (coll->flags & k_HIT_0x200)
    {
        xMat3x3RMulVec(&coll->norm, &mnormal, &coll->norm);
    }

    if (coll->flags & k_HIT_0x800)
    {
        xMat3x3RMulVec(&coll->depen, &mnormal, &coll->depen);
    }

    if (coll->flags & k_HIT_0x400)
    {
        xMat3x3RMulVec(&coll->tohit, &mnormal, &coll->tohit);
    }

    if (coll->flags & k_HIT_CALC_HDNG)
    {
        xMat3x3RMulVec(&coll->hdng, &mnormal, &coll->hdng);
    }

    return 1;
}

static S32 PointWithinTriangle(xVec3* _pt, xVec3** _tri, xVec3* _normal)
{
    RwV3d* pt = (RwV3d*)_pt;
    RwV3d** tri = (RwV3d**)_tri;
    RwV3d* normal = (RwV3d*)_normal;
    Dimension dimension;
    S32 inside = FALSE;
    S32 i;
    S32 j;
    F32 absX = iabs(normal->x);
    F32 absY = iabs(normal->y);
    F32 absZ = iabs(normal->z);

    if (absZ > absY)
    {
        if (absZ > absX)
        {
            dimension = ZDIM;
        }
        else
        {
            dimension = XDIM;
        }
    }
    else
    {
        if (absY > absX)
        {
            dimension = YDIM;
        }
        else
        {
            dimension = XDIM;
        }
    }

    switch (dimension)
    {
    case XDIM:
        for (i = 0, j = 2; i < 3; j = i++)
        {
            if (((tri[i]->y <= pt->y && pt->y < tri[j]->y) ||
                 (tri[j]->y <= pt->y && pt->y < tri[i]->y)) &&
                (pt->z < tri[i]->z + ((pt->y - tri[i]->y) * (tri[j]->z - tri[i]->z)) /
                                         (tri[j]->y - tri[i]->y)))
            {
                inside = !inside;
            }
        }
        break;
    case YDIM:
        for (i = 0, j = 2; i < 3; j = i++)
        {
            if (((tri[i]->z <= pt->z && pt->z < tri[j]->z) ||
                 (tri[j]->z <= pt->z && pt->z < tri[i]->z)) &&
                (pt->x < tri[i]->x + ((pt->z - tri[i]->z) * (tri[j]->x - tri[i]->x)) /
                                         (tri[j]->z - tri[i]->z)))
            {
                inside = !inside;
            }
        }
        break;
    case ZDIM:
        for (i = 0, j = 2; i < 3; j = i++)
        {
            if (((tri[i]->y <= pt->y && pt->y < tri[j]->y) ||
                 (tri[j]->y <= pt->y && pt->y < tri[i]->y)) &&
                (pt->x < tri[i]->x + ((pt->y - tri[i]->y) * (tri[j]->x - tri[i]->x)) /
                                         (tri[j]->y - tri[i]->y)))
            {
                inside = !inside;
            }
        }
        break;
    }

    return inside;
}

static inline void FindNearestPointOnLine(xVec3* result, xVec3* point, xVec3* start, xVec3* end)
{
    F32 mu;
    RwV3d line;
    RwV3d candidate;
    F32 lineLength2;

    RwV3dSubMacro(&line, (RwV3d*)end, (RwV3d*)start);
    mu = RwV3dDotProductMacro((RwV3d*)point, &line) - RwV3dDotProductMacro((RwV3d*)start, &line);
    if (mu <= 0.0f)
    {
        candidate = *(RwV3d*)start;
    }
    else
    {
        lineLength2 = RwV3dDotProductMacro(&line, &line);
        if (mu < lineLength2)
        {
            mu /= lineLength2;
            RwV3dScaleMacro(&candidate, &line, mu);
            RwV3dAddMacro(&candidate, &candidate, (RwV3d*)start);
        }
        else
        {
            candidate = *(RwV3d*)end;
        }
    }
    *(RwV3d*)result = candidate;
}

static void properSphereIsectTri(const xVec3* center, F32 radius, xVec3* tohit, F32* dist_ptr,
                                 RpCollisionTriangle* tri)
{
    xVec3 projPoint;
    F32 dist2plane;
    F32 dist;
    F32 dist2;
    F32 radius2;
    U32 i;
    xVec3 vertClosestPoint;
    F32 vertDist2;
    xVec3 temp;

    dist = *dist_ptr;
    dist2plane = xVec3Dot((xVec3*)&tri->normal, center);
    dist2plane = xVec3Dot((xVec3*)&tri->normal, (xVec3*)&tri->point) - dist2plane;

    xVec3SMul(&projPoint, (xVec3*)&tri->normal, dist2plane);
    xVec3Copy(tohit, &projPoint);
    xVec3AddTo(&projPoint, center);

    if (PointWithinTriangle(&projPoint, (xVec3**)&tri->vertices, (xVec3*)&tri->normal))
    {
        dist = iabs(dist2plane);
    }
    else
    {
        dist2 = SQR(dist);
        radius2 = SQR(radius);
        for (i = 0; i < 3; i++)
        {
            FindNearestPointOnLine(&vertClosestPoint, &projPoint, (xVec3*)tri->vertices[i],
                                   (xVec3*)tri->vertices[(i + 1) % 3]);
            xVec3Sub(&temp, &vertClosestPoint, center);
            vertDist2 = xVec3Length2(&temp);

            if (vertDist2 < dist2 && vertDist2 < radius2)
            {
                dist2 = vertDist2;
                xVec3Copy(tohit, &temp);
            }
        }
        dist = xsqrt(dist2);
    }
    *dist_ptr = dist;
}

// Dead-stripped placeholder for an unreferenced {0, 1, 0} template that retail's xWad1 .rodata
// keeps here (later offsets are 0xC short without it). Ratatouille's erased DWARF has the stripped
// function, ActualFloorPolyCheck, with an 'xVec3 upvec'; its body is unknown.
static void __deadstripped2_xCollide()
{
    xVec3 upvec = { 0.0f, 1.0f, 0.0f };
}

static RpCollisionTriangle* sphereHitsEnvCB(RpIntersection* isx, RpWorldSector* sector,
                                            RpCollisionTriangle* tri, F32 dist, void* data)
{
    xCollis* coll = (xCollis*)data;
    F32 radius = isx->t.sphere.radius;
    xVec3* center = (xVec3*)&isx->t.sphere.center;
    xVec3 tohit;
    F32 scale;

    if (coll->flags == 0)
    {
        coll->flags |= k_HIT_IT;
        return NULL;
    }

    dist = FLOAT_MAX;
    properSphereIsectTri(center, radius, &tohit, &dist, tri);

    if (dist < 0.0f)
    {
        return tri;
    }
    if (0.0f == dist)
    {
        return tri;
    }
    if (dist >= radius)
    {
        return tri;
    }
    if (dist >= coll->dist)
    {
        return tri;
    }

    coll->dist = dist;

    if (coll->flags & k_HIT_0x200)
    {
        xVec3Copy(&coll->norm, (xVec3*)&tri->normal);
    }

    if (coll->flags & k_HIT_0x400)
    {
        xVec3Copy(&coll->tohit, &tohit);
    }

    if (coll->flags & k_HIT_0x800)
    {
        scale = 1.0f - radius / coll->dist;
        scale -= 0.00001f;
        xVec3Copy(&coll->depen, &tohit);
        xVec3SMulBy(&coll->depen, scale);
    }

    if (coll->flags & k_HIT_CALC_HDNG)
    {
        xVec3Normalize(&coll->hdng, &tohit);
    }

    if (sector != NULL)
    {
        coll->oid = sector->triangles[tri->index].matIndex;
    }
    else if (sCollidingJSP)
    {
        coll->oid = ((xClumpCollBSPTriangle*)tri->index)->matIndex;
    }
    else
    {
        coll->oid = tri->index;
    }

    coll->flags |= k_HIT_IT;
    return tri;
}

static RpCollisionTriangle* sphereHitsModelCB(RpIntersection* isx, RpCollisionTriangle* tri,
                                              F32 dist, void* data)
{
    xSphereHitsModel_context* context = (xSphereHitsModel_context*)data;
    return sphereHitsEnvCB(&context->localx, NULL, tri, dist, context->coll);
}

U32 xSphereHitsModel(const xSphere* b, const xModelInstance* m, xCollis* coll)
{
    RpIntersection isx;
    xSphereHitsModel_context context;
    xMat4x3* mat;
    RwFrame* frame;
    F32 mscale;

    context.coll = coll;

    if (m->Flags & 0x800)
    {
        xModelAnimCollApply(*m);
    }

    isx.type = rpINTERSECTSPHERE;
    *(xSphere*)&isx.t.sphere = *b;

    mat = (xMat4x3*)m->Mat;
    frame = RpAtomicGetFrame(m->Data);
    RwFrameTransform(frame, (RwMatrix*)mat, rwCOMBINEREPLACE);

    mscale = xVec3Length(&mat->right);
    xMat4x3Tolocal((xVec3*)&context.localx.t.sphere.center, mat, &b->center);
    context.localx.t.sphere.radius = b->r / mscale;

    coll->flags &= ~k_HIT_IT;
    coll->dist = FLOAT_MAX;

    if (coll->flags & k_HIT_CALC_TRI)
    {
        coll->flags |= k_HIT_0x400;
    }

    RpAtomicForAllIntersections(m->Data, &isx, sphereHitsModelCB, &context);

    if ((coll->flags & k_HIT_CALC_TRI) && (coll->flags & k_HIT_IT))
    {
        xCollideCalcTri(coll->tri, *m, *(xVec3*)&context.localx.t.sphere.center, coll->tohit);
    }

    if (coll->flags & k_HIT_0x400)
    {
        xMat3x3RMulVec(&coll->tohit, mat, &coll->tohit);
    }

    if (coll->flags & k_HIT_0x800)
    {
        xMat3x3RMulVec(&coll->depen, mat, &coll->depen);
    }

    if (coll->flags & k_HIT_CALC_HDNG)
    {
        xMat3x3RMulVec(&coll->hdng, mat, &coll->hdng);
    }

    if (coll->flags & k_HIT_0x200)
    {
        xMat3x3RMulVec(&coll->norm, mat, &coll->norm);
        F32 mag2 = coll->norm.length2();
        if (mag2 && !xeq(mag2, 1.0f, 1e-5f))
        {
            coll->norm *= 1.0f / xsqrt(mag2);
        }
    }

    coll->dist *= mscale;

    if (m->Flags & 0x800)
    {
        xModelAnimCollRestore(*m);
    }

    return coll->flags & k_HIT_IT;
}

void xParabolaRecenter(xParabola* p, F32 newZeroT)
{
    xVec3 newPos;
    xVec3 newVel;

    xParabolaEvalPos(p, &newPos, newZeroT);
    xParabolaEvalVel(p, &newVel, newZeroT);

    xVec3Copy(&p->initPos, &newPos);
    xVec3Copy(&p->initVel, &newVel);

    p->maxTime -= newZeroT;
    p->minTime -= newZeroT;
}

static S32 xParabolaEnvCB(xClumpCollBSPTriangle* triangles, void* data)
{
    ParabolaCBData* pd = (ParabolaCBData*)data;
    xParabola* p = pd->p;
    xCollis* colls = pd->colls;

    do
    {
        if (xClumpColl_IsCollidableTriangle(triangles->flags))
        {
            xVec3* v0 = (xVec3*)triangles->v.p;
            xVec3* v1;
            xVec3* v2;
            if (triangles->flags & 0x2)
            {
                v1 = (xVec3*)triangles->v.p + 2;
                v2 = (xVec3*)triangles->v.p + 1;
            }
            else
            {
                v1 = (xVec3*)triangles->v.p + 1;
                v2 = (xVec3*)triangles->v.p + 2;
            }

            U8 tester = 0;
            if (xVec3Dot(&pd->N, v0) - pd->d > 0.0f)
            {
                tester |= 1;
            }
            else
            {
                tester |= 2;
            }
            if (xVec3Dot(&pd->N, v1) - pd->d > 0.0f)
            {
                tester |= 1;
            }
            else
            {
                tester |= 2;
            }
            if (xVec3Dot(&pd->N, v2) - pd->d > 0.0f)
            {
                tester |= 1;
            }
            else
            {
                tester |= 2;
            }

            if ((tester & 1) && (tester & 2))
            {
                F32 a, b, c, det;
                xVec3 v1p, v2p, N;
                F32 pdist;
                F32 t1 = -1.0f;
                F32 t2 = -1.0f;

                xVec3Sub(&v1p, v1, v0);
                xVec3Sub(&v2p, v2, v0);
                xVec3Cross(&N, &v1p, &v2p);
                if (xVec3Normalize(&N, &N))
                {
                    xVec3 s1, s2;
                    F32 c1, c2, c0;
                    F32 d11, d12, d22;
                    F32 p1, p2;
                    xVec3 pp;

                    pdist = -xVec3Dot(&N, v0);
                    a = -N.y * p->gravity * 0.5f;
                    b = N.x * p->initVel.x + N.y * p->initVel.y + N.z * p->initVel.z;
                    c = N.x * p->initPos.x + N.y * p->initPos.y + N.z * p->initPos.z + pdist;
                    if (a < 1e-5f && a > -1e-5f)
                    {
                        if (b > 1e-5f || b < -1e-5f)
                        {
                            t1 = -c / b;
                        }
                    }
                    else
                    {
                        det = b * b - 4.0f * a * c;
                        if (det > 0.0f)
                        {
                            t1 = (-b + xsqrt(det)) / (2.0f * a);
                            t2 = (-b - xsqrt(det)) / (2.0f * a);
                        }
                        else if (det == 0.0f)
                        {
                            t1 = -b / a;
                        }
                    }

                    d11 = xVec3Dot(&v1p, &v1p);
                    d12 = xVec3Dot(&v1p, &v2p);
                    d22 = xVec3Dot(&v2p, &v2p);

                    if (t1 >= p->minTime && t1 < p->maxTime && t1 < pd->colls->dist)
                    {
                        xVec3Copy(&s1, &p->initPos);
                        xVec3AddScaled(&s1, &p->initVel, t1);
                        s1.y -= 0.5f * p->gravity * t1 * t1;
                        xVec3Sub(&pp, &s1, v0);
                        p1 = xVec3Dot(&pp, &v1p);
                        p2 = xVec3Dot(&pp, &v2p);
                        c1 = (p1 * d22 - p2 * d12) / (d11 * d22 - d12 * d12);
                        c2 = (p2 * d11 - p1 * d12) / (d11 * d22 - d12 * d12);
                        c0 = 1.0f - c1 - c2;
                        if (c1 >= 0.0f && c2 >= 0.0f && c0 >= 0.0f)
                        {
                            colls->flags |= k_HIT_IT;
                            pd->colls->dist = t1;
                            xVec3Copy(&pd->colls->norm, &N);
                        }
                    }

                    if (t2 >= p->minTime && t2 < p->maxTime && t2 < pd->colls->dist)
                    {
                        xVec3Copy(&s2, &p->initPos);
                        xVec3AddScaled(&s2, &p->initVel, t2);
                        s2.y -= 0.5f * p->gravity * t2 * t2;
                        xVec3Sub(&pp, &s2, v0);
                        p1 = xVec3Dot(&pp, &v1p);
                        p2 = xVec3Dot(&pp, &v2p);
                        c1 = (p1 * d22 - p2 * d12) / (d11 * d22 - d12 * d12);
                        c2 = (p2 * d11 - p1 * d12) / (d11 * d22 - d12 * d12);
                        c0 = 1.0f - c1 - c2;
                        if (c1 >= 0.0f && c2 >= 0.0f && c0 >= 0.0f)
                        {
                            colls->flags |= k_HIT_IT;
                            pd->colls->dist = t2;
                            xVec3Copy(&pd->colls->norm, &N);
                        }
                    }
                }
            }
        }
    } while ((triangles++)->flags & 0x1);

    return 1;
}

S32 xParabolaHitsEnv(xParabola* p, const xEnv* env, xCollis* colls)
{
    RwBBox xb;
    F32 tmp;
    F32 extremumT;
    F32 extremum;
    ParabolaCBData data;
    S32 i;

    if (p->minTime >= p->maxTime)
    {
        return 0;
    }

    colls->flags = 0;
    colls->dist = FLOAT_MAX;

    xb.inf.x = p->initPos.x + p->initVel.x * p->minTime;
    xb.sup.x = p->initPos.x + p->initVel.x * p->maxTime;
    if (xb.inf.x > xb.sup.x)
    {
        tmp = xb.inf.x;
        xb.inf.x = xb.sup.x;
        xb.sup.x = tmp;
    }

    xb.inf.z = p->initPos.z + p->initVel.z * p->minTime;
    xb.sup.z = p->initPos.z + p->initVel.z * p->maxTime;
    if (xb.inf.z > xb.sup.z)
    {
        tmp = xb.inf.z;
        xb.inf.z = xb.sup.z;
        xb.sup.z = tmp;
    }

    xb.inf.y = p->initPos.y + p->initVel.y * p->minTime;
    xb.inf.y -= 0.5f * p->gravity * p->minTime * p->minTime;
    xb.sup.y = p->initPos.y + p->initVel.y * p->maxTime;
    xb.sup.y -= 0.5f * p->gravity * p->maxTime * p->maxTime;
    if (xb.inf.y > xb.sup.y)
    {
        tmp = xb.inf.y;
        xb.inf.y = xb.sup.y;
        xb.sup.y = tmp;
    }

    if (p->gravity > 1e-5f || p->gravity < -1e-5f)
    {
        extremumT = p->initVel.y / p->gravity;
        if (p->minTime < extremumT && extremumT < p->maxTime)
        {
            extremum = p->initPos.y + (p->initVel.y - 0.5f * p->gravity * extremumT) * extremumT;
            if (extremum < xb.inf.y)
            {
                xb.inf.y = extremum;
            }
            else if (extremum > xb.sup.y)
            {
                xb.sup.y = extremum;
            }
        }
    }

    data.p = p;
    data.N.x = -p->initVel.z;
    data.N.y = 0.0f;
    data.N.z = p->initVel.x;
    if (data.N.x < 1e-5f && data.N.x > -1e-5f && data.N.z < 1e-5f && data.N.z > -1e-5f)
    {
        data.N.x = 1.0f;
        data.N.z = 0.0f;
    }
    data.d = xVec3Dot(&data.N, &p->initPos);
    data.colls = colls;

    for (i = 0; i < env->geom->jsp_count; i++)
    {
        data.colls->test_dist = data.colls->dist;
        xClumpColl_ForAllBoxLeafNodeIntersections(env->geom->jsp_list[i]->colltree, &xb,
                                                  xParabolaEnvCB, &data);
        if (data.colls->dist < data.colls->test_dist)
        {
            data.colls->oid = get_matid(env->geom->jsp_list[i], data.colls->oid, 0);
        }
    }

    return colls->flags & k_HIT_IT;
}

// Dead-stripped placeholder for "CloseDist = %f\n", which retail's string pool (and the PS2
// demo's) keeps here with no reference. The function that printed it is unknown.
static const char* __deadstripped_xCollide()
{
    return "CloseDist = %f\n";
}

U32 xBoxHitsSphere(const xBox* a, const xSphere* b, xCollis* coll)
{
    xIsect isx;
    F32 scale;

    iBoxIsectSphere(a, b, &isx);

    if (!(isx.penned <= 0.0f))
    {
        coll->flags &= ~k_HIT_IT;
        return 0;
    }

    if (isx.contained <= 0.0f)
    {
        coll->flags |= k_HIT_0x10;
    }

    coll->dist = isx.dist;

    if (coll->flags & k_HIT_0x400)
    {
        xVec3Copy(&coll->tohit, &isx.norm);
    }

    if (coll->flags & k_HIT_0x800)
    {
        if (isx.dist == 0.0f)
        {
            xVec3Copy(&coll->depen, &g_O3);
        }
        else
        {
            scale = isx.penned / isx.dist;
            xVec3SMul(&coll->depen, &isx.norm, scale);
        }
    }

    if (coll->flags & (k_HIT_0x200 | k_HIT_CALC_HDNG))
    {
        xVec3Normalize(&coll->hdng, &isx.norm);
    }

    if (coll->flags & k_HIT_0x200)
    {
        xVec3Inv(&coll->norm, &coll->hdng);
    }

    coll->flags |= k_HIT_IT;
    return 1;
}

#define NORMALIZE(v, s)                                                                            \
    MACRO_START                                                                                    \
    {                                                                                              \
        F32 _mag = SQR((v)->x) + SQR((v)->y) + SQR((v)->z);                                        \
        if (xabs(_mag - 1.0f) > 0.000001f && _mag > 0.00001f)                                      \
        {                                                                                          \
            _mag = xsqrt(_mag);                                                                    \
            *(s) *= _mag;                                                                          \
            _mag = 1.0f / _mag;                                                                    \
            (v)->x *= _mag;                                                                        \
            (v)->y *= _mag;                                                                        \
            (v)->z *= _mag;                                                                        \
        }                                                                                          \
    }                                                                                              \
    MACRO_STOP

static U32 Mgc_BoxBoxTest(const xBox* a, const xMat4x3* matA, const xBox* b, const xMat4x3* matB)
{
    xVec3 akA[3], akB[3];
    F32 afEA[3], afEB[3];

    afEA[0] = 0.5f * (a->upper.x - a->lower.x);
    afEA[1] = 0.5f * (a->upper.y - a->lower.y);
    afEA[2] = 0.5f * (a->upper.z - a->lower.z);
    akA[0] = matA->right;
    akA[1] = matA->up;
    akA[2] = matA->at;
    NORMALIZE(&akA[0], &afEA[0]);
    NORMALIZE(&akA[1], &afEA[1]);
    NORMALIZE(&akA[2], &afEA[2]);

    afEB[0] = 0.5f * (b->upper.x - b->lower.x);
    afEB[1] = 0.5f * (b->upper.y - b->lower.y);
    afEB[2] = 0.5f * (b->upper.z - b->lower.z);
    akB[0] = matB->right;
    akB[1] = matB->up;
    akB[2] = matB->at;
    NORMALIZE(&akB[0], &afEB[0]);
    NORMALIZE(&akB[1], &afEB[1]);
    NORMALIZE(&akB[2], &afEB[2]);

    xVec3 centA, centB;
    centA.x = 0.5f * (a->upper.x + a->lower.x);
    centA.y = 0.5f * (a->upper.y + a->lower.y);
    centA.z = 0.5f * (a->upper.z + a->lower.z);
    centB.x = 0.5f * (b->upper.x + b->lower.x);
    centB.y = 0.5f * (b->upper.y + b->lower.y);
    centB.z = 0.5f * (b->upper.z + b->lower.z);
    xMat4x3Toworld(&centA, matA, &centA);
    xMat4x3Toworld(&centB, matB, &centB);

    xVec3 kD;
    kD.x = centB.x - centA.x;
    kD.y = centB.y - centA.y;
    kD.z = centB.z - centA.z;

    F32 aafC[3][3], aafAbsC[3][3];
    F32 afAD[3];
    F32 fR0, fR1, fR, fR01;

    aafC[0][0] = akA[0].x * akB[0].x + akA[0].y * akB[0].y + akA[0].z * akB[0].z;
    aafC[0][1] = akA[0].x * akB[1].x + akA[0].y * akB[1].y + akA[0].z * akB[1].z;
    aafC[0][2] = akA[0].x * akB[2].x + akA[0].y * akB[2].y + akA[0].z * akB[2].z;
    afAD[0] = akA[0].x * kD.x + akA[0].y * kD.y + akA[0].z * kD.z;
    aafAbsC[0][0] = xabs(aafC[0][0]);
    aafAbsC[0][1] = xabs(aafC[0][1]);
    aafAbsC[0][2] = xabs(aafC[0][2]);
    fR = xabs(afAD[0]);
    fR1 = afEB[0] * aafAbsC[0][0] + afEB[1] * aafAbsC[0][1] + afEB[2] * aafAbsC[0][2];
    fR01 = afEA[0] + fR1;
    if (fR > fR01)
        return 0;

    aafC[1][0] = akA[1].x * akB[0].x + akA[1].y * akB[0].y + akA[1].z * akB[0].z;
    aafC[1][1] = akA[1].x * akB[1].x + akA[1].y * akB[1].y + akA[1].z * akB[1].z;
    aafC[1][2] = akA[1].x * akB[2].x + akA[1].y * akB[2].y + akA[1].z * akB[2].z;
    afAD[1] = akA[1].x * kD.x + akA[1].y * kD.y + akA[1].z * kD.z;
    aafAbsC[1][0] = xabs(aafC[1][0]);
    aafAbsC[1][1] = xabs(aafC[1][1]);
    aafAbsC[1][2] = xabs(aafC[1][2]);
    fR = xabs(afAD[1]);
    fR1 = afEB[0] * aafAbsC[1][0] + afEB[1] * aafAbsC[1][1] + afEB[2] * aafAbsC[1][2];
    fR01 = afEA[1] + fR1;
    if (fR > fR01)
        return 0;

    aafC[2][0] = akA[2].x * akB[0].x + akA[2].y * akB[0].y + akA[2].z * akB[0].z;
    aafC[2][1] = akA[2].x * akB[1].x + akA[2].y * akB[1].y + akA[2].z * akB[1].z;
    aafC[2][2] = akA[2].x * akB[2].x + akA[2].y * akB[2].y + akA[2].z * akB[2].z;
    afAD[2] = akA[2].x * kD.x + akA[2].y * kD.y + akA[2].z * kD.z;
    aafAbsC[2][0] = xabs(aafC[2][0]);
    aafAbsC[2][1] = xabs(aafC[2][1]);
    aafAbsC[2][2] = xabs(aafC[2][2]);
    fR = xabs(afAD[2]);
    fR1 = afEB[0] * aafAbsC[2][0] + afEB[1] * aafAbsC[2][1] + afEB[2] * aafAbsC[2][2];
    fR01 = afEA[2] + fR1;
    if (fR > fR01)
        return 0;

    fR = xabs(akB[0].x * kD.x + akB[0].y * kD.y + akB[0].z * kD.z);
    fR0 = afEA[0] * aafAbsC[0][0] + afEA[1] * aafAbsC[1][0] + afEA[2] * aafAbsC[2][0];
    fR01 = fR0 + afEB[0];
    if (fR > fR01)
        return 0;

    fR = xabs(akB[1].x * kD.x + akB[1].y * kD.y + akB[1].z * kD.z);
    fR0 = afEA[0] * aafAbsC[0][1] + afEA[1] * aafAbsC[1][1] + afEA[2] * aafAbsC[2][1];
    fR01 = fR0 + afEB[1];
    if (fR > fR01)
        return 0;

    fR = xabs(akB[2].x * kD.x + akB[2].y * kD.y + akB[2].z * kD.z);
    fR0 = afEA[0] * aafAbsC[0][2] + afEA[1] * aafAbsC[1][2] + afEA[2] * aafAbsC[2][2];
    fR01 = fR0 + afEB[2];
    if (fR > fR01)
        return 0;

    fR = xabs(afAD[2] * aafC[1][0] - afAD[1] * aafC[2][0]);
    fR0 = afEA[1] * aafAbsC[2][0] + afEA[2] * aafAbsC[1][0];
    fR1 = afEB[1] * aafAbsC[0][2] + afEB[2] * aafAbsC[0][1];
    fR01 = fR0 + fR1;
    if (fR > fR01)
        return 0;

    fR = xabs(afAD[2] * aafC[1][1] - afAD[1] * aafC[2][1]);
    fR0 = afEA[1] * aafAbsC[2][1] + afEA[2] * aafAbsC[1][1];
    fR1 = afEB[0] * aafAbsC[0][2] + afEB[2] * aafAbsC[0][0];
    fR01 = fR0 + fR1;
    if (fR > fR01)
        return 0;

    fR = xabs(afAD[2] * aafC[1][2] - afAD[1] * aafC[2][2]);
    fR0 = afEA[1] * aafAbsC[2][2] + afEA[2] * aafAbsC[1][2];
    fR1 = afEB[0] * aafAbsC[0][1] + afEB[1] * aafAbsC[0][0];
    fR01 = fR0 + fR1;
    if (fR > fR01)
        return 0;

    fR = xabs(afAD[0] * aafC[2][0] - afAD[2] * aafC[0][0]);
    fR0 = afEA[0] * aafAbsC[2][0] + afEA[2] * aafAbsC[0][0];
    fR1 = afEB[1] * aafAbsC[1][2] + afEB[2] * aafAbsC[1][1];
    fR01 = fR0 + fR1;
    if (fR > fR01)
        return 0;

    fR = xabs(afAD[0] * aafC[2][1] - afAD[2] * aafC[0][1]);
    fR0 = afEA[0] * aafAbsC[2][1] + afEA[2] * aafAbsC[0][1];
    fR1 = afEB[0] * aafAbsC[1][2] + afEB[2] * aafAbsC[1][0];
    fR01 = fR0 + fR1;
    if (fR > fR01)
        return 0;

    fR = xabs(afAD[0] * aafC[2][2] - afAD[2] * aafC[0][2]);
    fR0 = afEA[0] * aafAbsC[2][2] + afEA[2] * aafAbsC[0][2];
    fR1 = afEB[0] * aafAbsC[1][1] + afEB[1] * aafAbsC[1][0];
    fR01 = fR0 + fR1;
    if (fR > fR01)
        return 0;

    fR = xabs(afAD[1] * aafC[0][0] - afAD[0] * aafC[1][0]);
    fR0 = afEA[0] * aafAbsC[1][0] + afEA[1] * aafAbsC[0][0];
    fR1 = afEB[1] * aafAbsC[2][2] + afEB[2] * aafAbsC[2][1];
    fR01 = fR0 + fR1;
    if (fR > fR01)
        return 0;

    fR = xabs(afAD[1] * aafC[0][1] - afAD[0] * aafC[1][1]);
    fR0 = afEA[0] * aafAbsC[1][1] + afEA[1] * aafAbsC[0][1];
    fR1 = afEB[0] * aafAbsC[2][2] + afEB[2] * aafAbsC[2][0];
    fR01 = fR0 + fR1;
    if (fR > fR01)
        return 0;

    fR = xabs(afAD[1] * aafC[0][2] - afAD[0] * aafC[1][2]);
    fR0 = afEA[0] * aafAbsC[1][2] + afEA[1] * aafAbsC[0][2];
    fR1 = afEB[0] * aafAbsC[2][1] + afEB[1] * aafAbsC[2][0];
    fR01 = fR0 + fR1;
    if (fR > fR01)
        return 0;

    return 1;
}

// No symbol: auto-inlined into its only caller, xBoundHitsBound, then dead-stripped. Name and
// parameters from the Incredibles and Ratatouille DWARF.
U32 xObbHitsObb(const xBox* a, const xMat4x3* a_mat, const xBox* b, const xMat4x3* b_mat,
                xCollis* coll)
{
    if (Mgc_BoxBoxTest(a, a_mat, b, b_mat))
    {
        coll->flags |= k_HIT_IT;
        return 1;
    }

    coll->flags &= ~k_HIT_IT;
    return 0;
}

U32 xBoxHitsObb(const xBox* a, const xBox* b, const xMat4x3* mat, xCollis* coll)
{
    if (Mgc_BoxBoxTest(a, &g_I3, b, mat))
    {
        coll->flags |= k_HIT_IT;
        return 1;
    }

    coll->flags &= ~k_HIT_IT;
    return 0;
}

void xCollideCalcTri(xCollis::tri_data& tri, const xModelInstance& model, const xVec3& center,
                     const xVec3& heading)
{
    xVec3 v[3];
    RpGeometry* geom = RpAtomicGetGeometry(model.Data);

    const xVec3* verts = model.anim_coll.verts;
    if (!verts)
    {
        const RpMorphTarget* mt = &geom->morphTarget[0];
        verts = (xVec3*)mt->verts;
    }

    const RpTriangle& t = geom->triangles[tri.index];
    for (S32 i = 0; i < 3; i++)
    {
        v[i] = verts[t.vertIndex[i]];
    }

    xVec3 e[3];
    e[0] = v[1] - v[0];
    e[1] = v[2] - v[1];
    e[2] = v[0] - v[2];

    struct
    {
        xVec3 norm;
        F32 D;
    } plane;

    plane.norm = e[0].cross(e[2]);
    plane.D = -v[0].dot(plane.norm);

    xVec3 p;
    F32 num = plane.D + plane.norm.dot(center);
    F32 denom = plane.norm.dot(heading);
    if (xfeq0(denom))
    {
        p = v[0];
    }
    else
    {
        p = heading * -(num / denom) + center;
    }

    xVec3 b, A, B, C, AxB, CxB;
    A = p - v[0];
    B = v[2] - v[1];
    C = v[1] - v[0];
    AxB = A.cross(B);
    CxB = C.cross(B);

    F32 len2 = AxB.length2();
    if (xfeq0(len2))
    {
        b = v[1];
    }
    else
    {
        b = v[0] + A * (CxB.dot(AxB) / len2);
    }

    {
        xVec3 d = b - v[0];
        xVec3 ad = d.get_abs();
        if (ad.x >= ad.y && ad.x >= ad.z)
        {
            tri.r = xfeq0(ad.x) ? 0.0f : (p.x - v[0].x) / d.x;
        }
        else if (ad.y >= ad.z)
        {
            tri.r = xfeq0(ad.y) ? 0.0f : (p.y - v[0].y) / d.y;
        }
        else
        {
            tri.r = xfeq0(ad.z) ? 0.0f : (p.z - v[0].z) / d.z;
        }
    }

    {
        xVec3 d = v[2] - v[1];
        xVec3 ad = d.get_abs();
        if (ad.x >= ad.y && ad.x >= ad.z)
        {
            tri.d = xfeq0(ad.x) ? 0.0f : (b.x - v[1].x) / d.x;
        }
        else if (ad.y >= ad.z)
        {
            tri.d = xfeq0(ad.y) ? 0.0f : (b.y - v[1].y) / d.y;
        }
        else
        {
            tri.d = xfeq0(ad.z) ? 0.0f : (b.z - v[1].z) / d.z;
        }
    }
}

xVec3 xCollisTriHit(const xCollis::tri_data& tri, const xModelInstance& model)
{
    const xMat4x3& m = *(xMat4x3*)model.Mat;
    xVec3 v[3];
    RpGeometry* geom = RpAtomicGetGeometry(model.Data);

    const xVec3* verts = model.anim_coll.verts;
    if (!verts)
    {
        const RpMorphTarget* mt = &geom->morphTarget[0];
        verts = (xVec3*)mt->verts;
    }

    const RpTriangle& t = geom->triangles[tri.index];
    for (S32 i = 0; i < 3; i++)
    {
        v[i] = verts[t.vertIndex[i]];
        xMat4x3Toworld(&v[i], &m, &v[i]);
    }

    xVec3 r[2];
    xVec3 d, A, B, C, AxB, CxB;

    r[0] = v[0] + (v[1] - v[0]) * tri.r;
    r[1] = v[0] + (v[2] - v[0]) * tri.r;
    d = r[0] + (r[1] - r[0]) * tri.d;
    A = d - v[0];
    B = r[1] - r[0];
    C = r[0] - v[0];
    AxB = A.cross(B);
    CxB = C.cross(B);

    F32 len2 = AxB.length2();
    xVec3 hit = (xfeq0(len2)) ? v[0] : v[0] + A * (CxB.dot(AxB) / len2);

    return hit;
}

// ax/ay/az hold the absolute distances to the box centre.
bool xSphereHitsBox(const xVec3& c, F32 r, const xBox& b)
{
    F32 ex = 0.5f * (b.upper.x - b.lower.x);
    F32 ey = 0.5f * (b.upper.y - b.lower.y);
    F32 ez = 0.5f * (b.upper.z - b.lower.z);

    F32 ax = xabs(c.x - (b.lower.x + ex));
    F32 ay = xabs(c.y - (b.lower.y + ey));
    F32 az = xabs(c.z - (b.lower.z + ez));

    F32 dx = ax - ex;
    F32 dy = ay - ey;
    F32 dz = az - ez;

    if (ax <= ex)
    {
        if (ay <= ey)
        {
            if (az <= ez)
            {
                return true;
            }
            else
            {
                return dz <= r;
            }
        }
        else
        {
            if (az <= ez)
            {
                return dy <= r;
            }
            else
            {
                return SQR(dy) + SQR(dz) <= SQR(r);
            }
        }
    }
    else
    {
        if (ay <= ey)
        {
            if (az <= ez)
            {
                return dx <= r;
            }
            else
            {
                return SQR(dx) + SQR(dz) <= SQR(r);
            }
        }
        else
        {
            if (az <= ez)
            {
                return SQR(dx) + SQR(dy) <= SQR(r);
            }
            else
            {
                return SQR(dx) + SQR(dy) + SQR(dz) <= SQR(r);
            }
        }
    }
}

bool xOBBHitsOBB(const xBox& a, const xMat4x3& amat, const xBox& b, const xMat4x3& bmat)
{
    return Mgc_BoxBoxTest(&a, &amat, &b, &bmat);

    // Unreachable: HI left the old separating-axis test below the early return (the PS2 line table
    // has code only on the return). These dead statements are why retail never inlines xOBBHitsOBB.
    // The test is BfBB's xOBBHitsOBB with TSSM's locals; which xVec3 locals are zero-initialized is
    // inferred from the three unreferenced zero templates in retail's .rodata.
    F32 ax, ay, az;
    F32 bx, by, bz;
    xVec3 acenter = {};
    xVec3 bcenter = {};
    xVec3 offset = bmat.pos - amat.pos;

    xVec3 aoffset = {};
    aoffset.x = amat.right.dot(offset);
    aoffset.y = amat.up.dot(offset);
    aoffset.z = amat.at.dot(offset);

    xMat3x3 xmat;
    xmat.right.x = amat.right.dot(bmat.right);
    xmat.right.y = amat.right.dot(bmat.up);
    xmat.right.z = amat.right.dot(bmat.at);
    xmat.up.x = amat.up.dot(bmat.right);
    xmat.up.y = amat.up.dot(bmat.up);
    xmat.up.z = amat.up.dot(bmat.at);
    xmat.at.x = amat.at.dot(bmat.right);
    xmat.at.y = amat.at.dot(bmat.up);
    xmat.at.z = amat.at.dot(bmat.at);

    xMat3x3 axmat;
    axmat.right = xmat.right.get_abs();
    axmat.up = xmat.up.get_abs();
    axmat.at = xmat.at.get_abs();

    F32 ar, br;
    F32 r;

    br = b.upper.x * axmat.right.x + b.upper.y * axmat.right.y + b.upper.z * axmat.right.z;
    ar = a.upper.x + br;
    if (xabs(aoffset.x) > ar)
        return false;

    br = b.upper.x * axmat.up.x + b.upper.y * axmat.up.y + b.upper.z * axmat.up.z;
    ar = a.upper.y + br;
    if (xabs(aoffset.y) > ar)
        return false;

    br = b.upper.x * axmat.at.x + b.upper.y * axmat.at.y + b.upper.z * axmat.at.z;
    ar = a.upper.z + br;
    if (xabs(aoffset.z) > ar)
        return false;

    r = bmat.right.dot(offset);
    ar = a.upper.x * axmat.right.x + a.upper.y * axmat.up.x + a.upper.z * axmat.at.x;
    br = ar + b.upper.x;
    if (xabs(r) > br)
        return false;

    r = bmat.up.dot(offset);
    ar = a.upper.x * axmat.right.y + a.upper.y * axmat.up.y + a.upper.z * axmat.at.y;
    br = ar + b.upper.y;
    if (xabs(r) > br)
        return false;

    r = bmat.at.dot(offset);
    ar = a.upper.x * axmat.right.z + a.upper.y * axmat.up.z + a.upper.z * axmat.at.z;
    br = ar + b.upper.z;
    if (xabs(r) > br)
        return false;

    if (axmat.right.x > 0.999f || axmat.right.y > 0.999f || axmat.right.z > 0.999f ||
        axmat.up.x > 0.999f || axmat.up.y > 0.999f || axmat.up.z > 0.999f || axmat.at.x > 0.999f ||
        axmat.at.y > 0.999f || axmat.at.z > 0.999f)
        return true;

    r = aoffset.z * xmat.up.x - aoffset.y * xmat.at.x;
    ar = a.upper.y * axmat.at.x + a.upper.z * axmat.up.x;
    br = b.upper.y * axmat.right.z + b.upper.z * axmat.right.y + ar;
    if (xabs(r) > br)
        return false;

    r = aoffset.z * xmat.up.y - aoffset.y * xmat.at.y;
    ar = a.upper.y * axmat.at.y + a.upper.z * axmat.up.y;
    br = b.upper.x * axmat.right.z + b.upper.z * axmat.right.x + ar;
    if (xabs(r) > br)
        return false;

    r = aoffset.z * xmat.up.z - aoffset.y * xmat.at.z;
    ar = a.upper.y * axmat.at.z + a.upper.z * axmat.up.z;
    br = b.upper.x * axmat.right.y + b.upper.y * axmat.right.x + ar;
    if (xabs(r) > br)
        return false;

    r = aoffset.x * xmat.at.x - aoffset.z * xmat.right.x;
    ar = a.upper.x * axmat.at.x + a.upper.z * axmat.right.x;
    br = b.upper.y * axmat.up.z + b.upper.z * axmat.up.y + ar;
    if (xabs(r) > br)
        return false;

    r = aoffset.x * xmat.at.y - aoffset.z * xmat.right.y;
    ar = a.upper.x * axmat.at.y + a.upper.z * axmat.right.y;
    br = b.upper.x * axmat.up.z + b.upper.z * axmat.up.x + ar;
    if (xabs(r) > br)
        return false;

    r = aoffset.x * xmat.at.z - aoffset.z * xmat.right.z;
    ar = a.upper.x * axmat.at.z + a.upper.z * axmat.right.z;
    br = b.upper.x * axmat.up.y + b.upper.y * axmat.up.x + ar;
    if (xabs(r) > br)
        return false;

    r = aoffset.y * xmat.right.x - aoffset.x * xmat.up.x;
    ar = a.upper.x * axmat.up.x + a.upper.y * axmat.right.x;
    br = b.upper.y * axmat.at.z + b.upper.z * axmat.at.y + ar;
    if (xabs(r) > br)
        return false;

    r = aoffset.y * xmat.right.y - aoffset.x * xmat.up.y;
    ar = a.upper.x * axmat.up.y + a.upper.y * axmat.right.y;
    br = b.upper.x * axmat.at.z + b.upper.z * axmat.at.x + ar;
    if (xabs(r) > br)
        return false;

    r = aoffset.y * xmat.right.z - aoffset.x * xmat.up.z;
    ar = a.upper.x * axmat.up.z + a.upper.y * axmat.right.z;
    br = b.upper.x * axmat.at.y + b.upper.y * axmat.at.x + ar;
    if (xabs(r) > br)
        return false;

    return true;
}

bool xSphereHitsVCylinder(const xVec3& sc, F32 sr, const xVec3& cc, F32 cr, F32 ch)
{
    F32 ydist = cc.y - sc.y;
    if (ydist < -sr || ydist > ch + sr)
    {
        return false;
    }

    if (ydist < 0.0f)
    {
        sr = xsqrt(SQR(sr) - SQR(ydist));
    }
    else if (ydist > ch)
    {
        sr = xsqrt(SQR(sr) - SQR(ydist - ch));
    }

    xVec2 xzloc1 = { sc.x, sc.z };
    xVec2 xzloc2 = { cc.x, cc.z };
    F32 max_xzdist = sr + cr;

    return !((xzloc2 - xzloc1).length2() > SQR(max_xzdist));
}

// Retail computes the squared distance once and reuses it for coll->dist.
U32 xSphereHitsVCylinder(const xSphere* sph, const xCylinder* cyl, xCollis* coll)
{
    F32 sr = sph->r;
    F32 ydist = cyl->center.y - sph->center.y;
    if (xabs(ydist) > cyl->h + sr)
    {
        return 0;
    }

    if (ydist < -cyl->h)
    {
        sr = xsqrt(SQR(sr) - SQR(ydist + cyl->h));
    }
    else if (ydist > cyl->h)
    {
        sr = xsqrt(SQR(sr) - SQR(ydist - cyl->h));
    }

    xVec2 xzloc1 = { sph->center.x, sph->center.z };
    xVec2 xzloc2 = { cyl->center.x, cyl->center.z };
    F32 xzdist2 = (xzloc2 - xzloc1).length2();
    F32 max_xzdist = sr + cyl->r;

    if (xzdist2 > SQR(max_xzdist))
    {
        return 0;
    }

    if ((coll->flags & k_HIT_0x200) || (coll->flags & k_HIT_0x400) || (coll->flags & k_HIT_0x800))
    {
        coll->norm = sph->center - cyl->center;
        coll->norm.y = 0.0f;
        coll->norm.safe_normalize(xVec3::m_Null);
    }

    if ((coll->flags & k_HIT_0x100) || (coll->flags & k_HIT_0x400) || (coll->flags & k_HIT_0x800))
    {
        coll->dist = max_xzdist - xsqrt(xzdist2);
    }

    if (coll->flags & k_HIT_0x400)
    {
        coll->tohit = coll->norm * -(sr - coll->dist);
        if (ydist < -cyl->h)
        {
            coll->tohit.y = -cyl->h;
        }
        else if (ydist > cyl->h)
        {
            coll->tohit.y = cyl->h;
        }
    }

    if (coll->flags & k_HIT_0x800)
    {
        coll->depen = coll->norm * -coll->dist;
    }

    coll->flags |= k_HIT_IT;
    return 1;
}

U32 xCylinderHitsSphere(const xCylinder* cyl, const xSphere* sph, xCollis* coll)
{
    F32 sr = sph->r;
    F32 ydist = cyl->center.y - sph->center.y;
    if (xabs(ydist) > cyl->h + sr)
    {
        return 0;
    }

    if (ydist < -cyl->h)
    {
        sr = xsqrt(SQR(sr) - SQR(ydist + cyl->h));
    }
    else if (ydist > cyl->h)
    {
        sr = xsqrt(SQR(sr) - SQR(ydist - cyl->h));
    }

    xVec2 xzloc1 = { sph->center.x, sph->center.z };
    xVec2 xzloc2 = { cyl->center.x, cyl->center.z };
    F32 xzdist2 = (xzloc2 - xzloc1).length2();
    F32 max_xzdist = sr + cyl->r;

    if (xzdist2 > SQR(max_xzdist))
    {
        return 0;
    }

    if ((coll->flags & k_HIT_0x200) || (coll->flags & k_HIT_0x400) || (coll->flags & k_HIT_0x800))
    {
        coll->norm = cyl->center - sph->center;
        coll->norm.y = 0.0f;
        coll->norm.safe_normalize(xVec3::m_Null);
    }

    if ((coll->flags & k_HIT_0x100) || (coll->flags & k_HIT_0x400) || (coll->flags & k_HIT_0x800))
    {
        coll->dist = max_xzdist - xsqrt(xzdist2);
    }

    if (coll->flags & k_HIT_0x400)
    {
        coll->tohit = coll->norm * -(cyl->r - coll->dist);
        if (ydist < -cyl->h)
        {
            coll->tohit.y = -cyl->h;
        }
        else if (ydist > cyl->h)
        {
            coll->tohit.y = cyl->h;
        }
    }

    if (coll->flags & k_HIT_0x800)
    {
        coll->depen = coll->norm * -coll->dist;
    }

    coll->flags |= k_HIT_IT;
    return 1;
}

bool xSphereHitsVCircle(const xVec3& sc, F32 sr, const xVec3& cc, F32 cr)
{
    F32 ydist = cc.y - sc.y;
    if (ydist < -sr || ydist > sr)
    {
        return false;
    }

    sr = xsqrt(SQR(sr) - SQR(ydist));

    xVec2 xzloc1 = { sc.x, sc.z };
    xVec2 xzloc2 = { cc.x, cc.z };
    F32 max_xzdist = sr + cr;

    return !((xzloc2 - xzloc1).length2() > SQR(max_xzdist));
}

bool xSphereHitsCone(const xVec3& center, F32 radius, const xVec3& kv0, F32 kr0, const xVec3& kv1,
                     F32 kr1)
{
    xMat4x3 mat;

    mat.up = kv1;
    mat.up -= kv0;
    F32 dist = mat.up.length();
    mat.up /= dist;

    mat.right = center;
    mat.right -= kv0;
    xVec3Cross(&mat.at, &mat.up, &mat.right);
    mat.at.up_normalize();
    xVec3Cross(&mat.right, &mat.at, &mat.up);
    mat.pos = kv0;

    xVec3 C;
    xMat4x3Tolocal(&C, &mat, &center);

    F32 Cx = C.x;
    F32 Cy = C.y;

    if (Cy < -radius || Cy > dist + radius)
    {
        return false;
    }

    F32 Ax = kr0;
    F32 Bx = kr1;
    F32 By = dist;
    F32 Dx = Bx - Ax;
    F32 Dy = By;

    F32 R = Ax + Dx * Cy / Dy;
    if (R >= Cx)
    {
        return true;
    }

    F32 Ex = Ax - Cx;
    F32 Ey = -Cy;

    F32 a = Dx * Dx + Dy * Dy;
    F32 b = 2.0f * (Dx * Ex + Dy * Ey);
    F32 c = Ex * Ex + Ey * Ey - radius * radius;
    F32 det = b * b - 4.0f * a * c;

    if (det < 0.0f)
    {
        return false;
    }

    det = xsqrt(det);

    F32 i2a = 0.5f / a;
    F32 t0 = i2a * (-b + det);
    F32 t1 = i2a * (-b - det);

    if ((t0 < 0.0f && t1 < 0.0f) || (t0 > 1.0f && t1 > 1.0f))
    {
        return false;
    }

    return true;
}

void xBoxForTriangle(xBox& box, const xVec3& p0, const xVec3& p1, const xVec3& p2)
{
    if (p0.x < p1.x)
    {
        box.lower.x = p0.x;
        box.upper.x = p1.x;
    }
    else
    {
        box.lower.x = p1.x;
        box.upper.x = p0.x;
    }

    if (p0.y < p1.y)
    {
        box.lower.y = p0.y;
        box.upper.y = p1.y;
    }
    else
    {
        box.lower.y = p1.y;
        box.upper.y = p0.y;
    }

    if (p0.z < p1.z)
    {
        box.lower.z = p0.z;
        box.upper.z = p1.z;
    }
    else
    {
        box.lower.z = p1.z;
        box.upper.z = p0.z;
    }

    if (box.upper.x < p2.x)
        box.upper.x = p2.x;
    if (box.upper.y < p2.y)
        box.upper.y = p2.y;
    if (box.upper.z < p2.z)
        box.upper.z = p2.z;
    if (box.lower.x > p2.x)
        box.lower.x = p2.x;
    if (box.lower.y > p2.y)
        box.lower.y = p2.y;
    if (box.lower.z > p2.z)
        box.lower.z = p2.z;
}

void xBoxForModel(xBox& o, const xModelInstance* m, bool childModels)
{
    xBoxForModelLocal(o, m, childModels);

    xVec3Add(&o.upper, &o.upper, (xVec3*)&m->Mat->pos);
    xVec3Add(&o.lower, &o.lower, (xVec3*)&m->Mat->pos);
}

void xBoxForModelLocal(xBox& o, const xModelInstance* m, bool childModels)
{
    RpAtomic* imodel;
    RpGeometry* geom;
    RpMorphTarget* mtgt;
    U16 idx;
    const xModelInstance* curModel;
    xVec3 lower;
    xVec3 upper;

    curModel = m;
    do
    {
        imodel = curModel->Data;
        geom = imodel->geometry;

        if (geom->triangles == NULL)
        {
            lower.x = imodel->boundingSphere.center.x - imodel->boundingSphere.radius;
            lower.y = imodel->boundingSphere.center.y - imodel->boundingSphere.radius;
            lower.z = imodel->boundingSphere.center.z - imodel->boundingSphere.radius;

            upper.x = imodel->boundingSphere.center.x + imodel->boundingSphere.radius;
            upper.y = imodel->boundingSphere.center.y + imodel->boundingSphere.radius;
            upper.z = imodel->boundingSphere.center.z + imodel->boundingSphere.radius;

            if (curModel == m)
            {
                o.lower = lower;
                o.upper = upper;
            }
            else
            {
                iBoxBoundVec(&o, &o, &lower);
                iBoxBoundVec(&o, &o, &upper);
            }
        }
        else
        {
            if (!(geom->numMorphTargets > 1 || geom->numMorphTargets >= 1))
            {
                return;
            }

            mtgt = geom->morphTarget;
            if (curModel == m)
            {
                iBoxInitBoundVec(&o, (xVec3*)mtgt->verts);
                idx = 1;
            }
            else
            {
                idx = 0;
            }

            for (; idx < geom->numVertices; idx++)
            {
                iBoxBoundVec(&o, &o, (xVec3*)&mtgt->verts[idx]);
            }
        }

        curModel = curModel->Next;
    } while (curModel != NULL && childModels);

    o.upper.x = MAX(o.lower.x + 0.01f, o.upper.x);
    o.upper.y = MAX(o.lower.y + 0.01f, o.upper.y);
    o.upper.z = MAX(o.lower.z + 0.01f, o.upper.z);
}

void xSphereForModel(xSphere& o, const xModelInstance* model, bool childModels)
{
    RpAtomic* imodel;
    RpGeometry* geom;
    RpMorphTarget* mtgt;
    U16 idx;
    const xModelInstance* cur_model;
    xSphere s;

    cur_model = model;
    do
    {
        imodel = cur_model->Data;
        geom = imodel->geometry;

        if (geom->triangles == NULL)
        {
            s.center.x = imodel->boundingSphere.center.x;
            s.center.y = imodel->boundingSphere.center.y;
            s.center.z = imodel->boundingSphere.center.z;
            s.r = imodel->boundingSphere.radius;
            if (cur_model == model)
            {
                o = s;
            }
            else
            {
                iSphereBoundSphere(&o, &o, &s);
            }
        }
        else
        {
            if (!(geom->numMorphTargets > 1 || geom->numMorphTargets >= 1))
            {
                return;
            }

            mtgt = geom->morphTarget;
            if (cur_model == model)
            {
                iSphereInitBoundVec(&o, (xVec3*)mtgt->verts);
                idx = 1;
            }
            else
            {
                idx = 0;
            }

            for (; idx < geom->numVertices; idx++)
            {
                iSphereBoundVec(&o, &o, (xVec3*)&mtgt->verts[idx]);
            }
        }

        cur_model = cur_model->Next;
    } while (childModels == true && cur_model != NULL);
}

static inline RpCollisionTriangle* rayHitsEnvBackwardCB(RpIntersection* isx, RpWorldSector* sector,
                                                        RpCollisionTriangle* tri, F32 dist,
                                                        void* data)
{
    xCollis* coll = (xCollis*)data;

    dist = 1.0f - dist;
    dist *= cbray.max_t;
    if (dist >= coll->dist)
    {
        return tri;
    }

    if (sector != NULL)
    {
        coll->oid = sector->triangles[tri->index].matIndex;
    }
    else if (sCollidingJSP)
    {
        coll->oid = ((xClumpCollBSPTriangle*)tri->index)->matIndex;
    }
    else
    {
        coll->oid = tri->index;
    }

    coll->dist = dist;
    coll->flags |= k_HIT_IT;
    coll->tri.index = tri->index;

    return tri;
}

static RpCollisionTriangle* rayHitsModelBackwardCB(RpIntersection* isx, RpCollisionTriangle* tri,
                                                   F32 dist, void* data)
{
    return rayHitsEnvBackwardCB(isx, NULL, tri, dist, data);
}

static RpCollisionTriangle* rayHitsEnvCB(RpIntersection* isx, RpWorldSector* sector,
                                         RpCollisionTriangle* tri, F32 dist, void* data)
{
    xCollis* coll = (xCollis*)data;
    U8 flags;

    dist *= cbray.max_t;
    if (dist >= coll->dist)
    {
        return tri;
    }

    if (sector != NULL)
    {
        coll->oid = sector->triangles[tri->index].matIndex;
    }
    else if (sCollidingJSP)
    {
        coll->oid = ((xClumpCollBSPTriangle*)tri->index)->matIndex;
        flags = ((xClumpCollBSPTriangle*)tri->index)->flags;
        if ((coll->flags & 0x4000) && !(flags & 0x8))
        {
            return tri;
        }

        if (flags & 0x10)
        {
            coll->flags |= k_HIT_0x20000;
        }
        else
        {
            coll->flags &= ~k_HIT_0x20000;
        }
    }
    else
    {
        coll->oid = tri->index;
    }

    coll->dist = dist;
    coll->flags |= k_HIT_IT;
    coll->tri.index = tri->index;
    if (coll->flags & k_HIT_0x200)
    {
        coll->norm = *(xVec3*)&tri->normal;
    }

    return tri;
}

static RpCollisionTriangle* rayHitsModelCB(RpIntersection* isx, RpCollisionTriangle* tri, F32 dist,
                                           void* data)
{
    return rayHitsEnvCB(isx, NULL, tri, dist, data);
}

// temp and center/heading are block-scoped, which gives retail's stack layout.
U32 xRayHitsModel(const xRay3* r, const xModelInstance* m, xCollis* coll)
{
    RpIntersection isx;
    F32 len;
    xMat4x3* mat;

    if (m->Flags & 0x800)
    {
        xModelAnimCollApply(*m);
    }

    isx.type = rpINTERSECTLINE;

    if (r->flags & 0x400)
    {
        F32 sx = r->dir.x * r->min_t;
        F32 sy = r->dir.y * r->min_t;
        F32 sz = r->dir.z * r->min_t;
        isx.t.line.start.x = r->origin.x + sx;
        isx.t.line.start.y = r->origin.y + sy;
        isx.t.line.start.z = r->origin.z + sz;
    }
    else
    {
        isx.t.line.start.x = r->origin.x;
        isx.t.line.start.y = r->origin.y;
        isx.t.line.start.z = r->origin.z;
    }

    if (r->flags & 0x800)
    {
        if (r->flags & 0x400)
        {
            len = r->max_t - r->min_t;
        }
        else
        {
            len = r->max_t;
        }

        isx.t.line.end.x = r->dir.x * len;
        isx.t.line.end.y = r->dir.y * len;
        isx.t.line.end.z = r->dir.z * len;
    }
    else
    {
        isx.t.line.end.x = r->dir.x;
        isx.t.line.end.y = r->dir.y;
        isx.t.line.end.z = r->dir.z;
    }

    isx.t.line.end.x = isx.t.line.start.x + isx.t.line.end.x;
    isx.t.line.end.y = isx.t.line.start.y + isx.t.line.end.y;
    isx.t.line.end.z = isx.t.line.start.z + isx.t.line.end.z;

    cbray = *r;
    if (!(r->flags & 0x800))
    {
        cbray.max_t = 1.0f;
    }
    if (r->flags & 0x400)
    {
        cbray.max_t -= cbray.min_t;
    }

    mat = (xMat4x3*)m->Mat;
    RwFrameTransform(RpAtomicGetFrame(m->Data), (RwMatrix*)mat, rwCOMBINEREPLACE);

    len = xVec3Length(&mat->right);
    cbray.max_t /= len;

    coll->flags &= ~k_HIT_IT;
    coll->dist = FLOAT_MAX;

    if (coll->flags & 0x2000)
    {
        coll->flags |= k_HIT_0x400;
    }

    RpAtomicForAllIntersections(m->Data, &isx, rayHitsModelCB, coll);

    {
        RwV3d temp = isx.t.line.start;
        isx.t.line.start = isx.t.line.end;
        isx.t.line.end = temp;
    }

    RpAtomicForAllIntersections(m->Data, &isx, rayHitsModelBackwardCB, coll);

    coll->dist *= len;
    if (r->flags & 0x400)
    {
        coll->dist += cbray.min_t;
    }

    if ((coll->flags & 0x2000) && (coll->flags & k_HIT_IT))
    {
        xVec3 center;
        xVec3 heading;

        xMat4x3Tolocal(&center, (xMat4x3*)m->Mat, &r->origin);
        xMat4x3Tolocal(&heading, (xMat4x3*)m->Mat, &coll->tohit);
        xCollideCalcTri(coll->tri, *m, center, heading);
    }

    if (m->Flags & 0x800)
    {
        xModelAnimCollRestore(*m);
    }

    return coll->flags & k_HIT_IT;
}

U32 xRayHitsEnv(const xRay3* r, const xEnv* env, xCollis* coll)
{
    RpIntersection isx;
    xVec3 delta;
    F32 len;
    S32 i;
    xIsect iBoxIsect;

    isx.type = rpINTERSECTLINE;

    if (r->flags & 0x400)
    {
        delta.x = r->dir.x * r->min_t;
        delta.y = r->dir.y * r->min_t;
        delta.z = r->dir.z * r->min_t;
        isx.t.line.start.x = r->origin.x + delta.x;
        isx.t.line.start.y = r->origin.y + delta.y;
        isx.t.line.start.z = r->origin.z + delta.z;
    }
    else
    {
        isx.t.line.start.x = r->origin.x;
        isx.t.line.start.y = r->origin.y;
        isx.t.line.start.z = r->origin.z;
    }

    if (r->flags & 0x800)
    {
        if (r->flags & 0x400)
        {
            len = r->max_t - r->min_t;
        }
        else
        {
            len = r->max_t;
        }

        isx.t.line.end.x = r->dir.x * len;
        isx.t.line.end.y = r->dir.y * len;
        isx.t.line.end.z = r->dir.z * len;
    }
    else
    {
        isx.t.line.end.x = r->dir.x;
        isx.t.line.end.y = r->dir.y;
        isx.t.line.end.z = r->dir.z;
    }

    isx.t.line.end.x = isx.t.line.start.x + isx.t.line.end.x;
    isx.t.line.end.y = isx.t.line.start.y + isx.t.line.end.y;
    isx.t.line.end.z = isx.t.line.start.z + isx.t.line.end.z;

    coll->flags &= ~k_HIT_IT;
    coll->dist = FLOAT_MAX;

    cbray = *r;
    if (!(r->flags & 0x800))
    {
        cbray.max_t = 1.0f;
    }
    if (r->flags & 0x400)
    {
        cbray.max_t -= cbray.min_t;
    }

    sCollidingJSP = 1;
    for (i = 0; i < env->geom->jsp_count; i++)
    {
        iBoxIsectRay(&env->geom->jsp_bound[i], r, &iBoxIsect);
        if (iBoxIsect.penned <= 0.0f)
        {
            coll->test_dist = coll->dist;
            xClumpColl_ForAllIntersections(env->geom->jsp_list[i]->colltree, &isx, rayHitsEnvCB,
                                           coll);
            if (coll->dist < coll->test_dist)
            {
                coll->oid = get_matid(env->geom->jsp_list[i], coll->oid, 0);
            }
        }
    }
    sCollidingJSP = 0;

    if (r->flags & 0x400)
    {
        coll->dist += cbray.min_t;
    }

    return coll->flags & k_HIT_IT;
}

static RpCollisionTriangle* sphereHitsEnv3CB(RpIntersection* isx, RpWorldSector* sector,
                                             RpCollisionTriangle* tri, F32 dist, void* data)
{
    xCollis* colls = (xCollis*)data;
    F32 radius;
    xVec3 tohit;
    xVec3 hdng;
    U8 idx;
    F32 dot;
    F32 odot;
    F32 ndot;
    F32 scale;

    if (cbnumcs == cbmaxcs)
    {
        return NULL;
    }

    radius = isx->t.sphere.radius;
    dist = FLOAT_MAX;
    properSphereIsectTri((xVec3*)&isx->t.sphere.center, radius, &tohit, &dist, tri);
    if (0.0f == dist)
    {
        return tri;
    }
    if (dist > radius)
    {
        return tri;
    }

    if (SQR(tohit.y) > SQR(tohit.x) + SQR(tohit.z))
    {
        if (FLOOR == 0xff)
        {
            idx = cbnumcs;
            FLOOR = cbnumcs;
            cbnumcs++;
        }
        else if (tohit.y < 0.0f)
        {
            if (colls[FLOOR].hdng.y > 0.0f || dist < colls[FLOOR].dist ||
                (iabs(dist - colls[FLOOR].dist) < 0.001f && tri->normal.y > colls[FLOOR].norm.y))
            {
                idx = FLOOR;
            }
            else
            {
                return tri;
            }
        }
        else
        {
            if (colls[FLOOR].hdng.y > 0.0f &&
                (dist < colls[FLOOR].dist ||
                 (iabs(dist - colls[FLOOR].dist) < 0.001f && tri->normal.y > colls[FLOOR].norm.y)))
            {
                idx = FLOOR;
            }
            else
            {
                return tri;
            }
        }

        xVec3SMul(&hdng, &tohit, 1.0f / dist);
    }
    else if (NEXT2 == 0xff)
    {
        if (OTHER == 0xff)
        {
            idx = cbnumcs;
            OTHER = cbnumcs;
            cbnumcs++;

            xVec3SMul(&hdng, &tohit, 1.0f / dist);
        }
        else
        {
            xVec3SMul(&hdng, &tohit, 1.0f / dist);
            dot = xVec3Dot(&hdng, &colls[OTHER].hdng);
            if (iabs(dot) > 0.70710677f)
            {
                if (dist < colls[OTHER].dist)
                {
                    idx = OTHER;
                }
                else
                {
                    return tri;
                }
            }
            else
            {
                idx = NEXT2 = cbnumcs++;
                if (dist < colls[OTHER].dist)
                {
                    U8 tmp = OTHER;
                    OTHER = NEXT2;
                    NEXT2 = tmp;
                    idx = OTHER;
                }
            }
        }
    }
    else
    {
        if (dist > colls[NEXT2].dist)
        {
            return tri;
        }

        xVec3SMul(&hdng, &tohit, 1.0f / dist);
        odot = xVec3Dot(&hdng, &colls[OTHER].hdng);
        if (dist < colls[OTHER].dist)
        {
            if (iabs(odot) > 0.70710677f)
            {
                ndot = xVec3Dot(&hdng, &colls[NEXT2].hdng);
                if (iabs(ndot) > 0.70710677f)
                {
                    if (NEXT2 < OTHER)
                    {
                        OTHER = NEXT2;
                    }
                    NEXT2 = 0xff;
                    cbnumcs--;
                    idx = OTHER;
                    colls[cbnumcs].flags &= ~k_HIT_IT;
                    colls[cbnumcs].dist = FLOAT_MAX;
                }
                else
                {
                    idx = OTHER;
                }
            }
            else
            {
                idx = NEXT2;
            }
        }
        else
        {
            if (iabs(odot) > 0.70710677f)
            {
                return tri;
            }
            idx = NEXT2;
        }
    }

    colls[idx].dist = dist;
    xVec3Copy(&colls[idx].hdng, &hdng);

    if (colls->flags & k_HIT_0x200)
    {
        xVec3Copy(&colls[idx].norm, (xVec3*)&tri->normal);
    }

    if (colls->flags & k_HIT_0x400)
    {
        xVec3Copy(&colls[idx].tohit, &tohit);
    }

    if (colls->flags & k_HIT_0x800)
    {
        scale = dist - radius;
        xVec3Copy(&colls[idx].depen, &hdng);
        xVec3SMulBy(&colls[idx].depen, scale);
    }

    colls[idx].flags |= k_HIT_IT;

    if (sector != NULL)
    {
        colls[idx].oid = sector->triangles[tri->index].matIndex;
    }
    else if (sCollidingJSP)
    {
        colls[idx].oid = ((xClumpCollBSPTriangle*)tri->index)->matIndex;
    }
    else
    {
        colls[idx].oid = tri->index;
    }

    colls[idx].tri.index = tri->index;

    if (cbnumcs == cbmaxcs)
    {
        return NULL;
    }

    return tri;
}

static RpCollisionTriangle* sphereHitsModel3CB(RpIntersection* isx, RpCollisionTriangle* tri,
                                               F32 dist, void* data)
{
    return sphereHitsEnv3CB(&cbisx_local, NULL, tri, dist, data);
}

S32 xSphereHitsModel3(const xSphere* b, const xModelInstance* m, xCollis* colls, U8 ncolls, F32 sth)
{
    RpIntersection isx;
    U8 idx;
    U8 i;
    xMat4x3* mat;
    RwFrame* frame;
    F32 mscale;
    xMat3x3 mnormal;

    if (m->Flags & 0x800)
    {
        xModelAnimCollApply(*m);
    }

    for (idx = 0; idx < ncolls; idx++)
    {
        if (colls[idx].flags & k_HIT_CALC_TRI)
        {
            colls[idx].flags |= k_HIT_0x400;
        }
    }

    isx.type = rpINTERSECTSPHERE;
    memcpy(&isx.t.sphere, b, sizeof(xSphere));

    mat = (xMat4x3*)m->Mat;
    frame = RpAtomicGetFrame(m->Data);
    RwFrameTransform(frame, (RwMatrix*)mat, rwCOMBINEREPLACE);

    mscale = xVec3Length(&mat->right);
    xMat3x3Normalize(&mnormal, mat);
    xMat4x3Tolocal((xVec3*)&cbisx_local.t.sphere.center, mat, &b->center);
    cbisx_local.t.sphere.radius = b->r / mscale;

    for (idx = 0; idx < ncolls; idx++)
    {
        colls[idx].flags &= ~k_HIT_IT;
        colls[idx].dist = FLOAT_MAX;
    }

    cbath = sth;
    cbnumcs = 0;
    cbmaxcs = ncolls;
    OTHER = 0xff;
    NEXT2 = 0xff;
    FLOOR = 0xff;

    RpAtomicForAllIntersections(m->Data, &isx, sphereHitsModel3CB, colls);

    if (colls->flags & (k_HIT_0xF00 | k_HIT_CALC_HDNG))
    {
        for (i = 0; i < cbnumcs; i++)
        {
            if (colls[i].flags & k_HIT_IT)
            {
                if (colls[i].flags & k_HIT_CALC_TRI)
                {
                    xCollideCalcTri(colls[i].tri, *m, *(xVec3*)&cbisx_local.t.sphere.center,
                                    colls[i].tohit);
                }

                xMat3x3RMulVec(&colls[i].tohit, mat, &colls[i].tohit);
                xMat3x3RMulVec(&colls[i].depen, mat, &colls[i].depen);
                xMat3x3RMulVec(&colls[i].hdng, &mnormal, &colls[i].hdng);
                xMat3x3RMulVec(&colls[i].norm, &mnormal, &colls[i].norm);
                colls[i].dist *= mscale;
            }
        }
    }

    if (m->Flags & 0x800)
    {
        xModelAnimCollRestore(*m);
    }

    return cbnumcs;
}

static RpCollisionTriangle* sphereHitsEnv4CB(RpIntersection* isx, RpWorldSector* sector,
                                             RpCollisionTriangle* tri, F32 dist, void* data)
{
    xCollis* colls = (xCollis*)data;
    F32 radius = isx->t.sphere.radius;
    xVec3 tohit;
    xCollis temp;
    U8 idx;
    xCollis* c;
    F32 ddist;

    dist = FLOAT_MAX;
    properSphereIsectTri((xVec3*)&isx->t.sphere.center, radius, &tohit, &dist, tri);
    if (0.0f == dist)
    {
        return tri;
    }
    if (dist > radius)
    {
        return tri;
    }

    if (sCollidingJSP)
    {
        if (((xClumpCollBSPTriangle*)tri->index)->flags & 0x10)
        {
            temp.flags = k_HIT_0x20000;
        }
        else
        {
            temp.flags = 0;
        }
    }
    else
    {
        temp.flags = 0;
    }

    temp.optr = NULL;
    idx = xCollideGetCollsIdxWithNormal(&temp, &tohit, (xVec3*)&tri->normal, cbmat);

    c = &colls[idx];
    ddist = dist - c->dist;

    if (!(ddist < 0.0f || (ddist < 0.001f && c->norm.y > tri->normal.y)))
    {
        return tri;
    }

    c->dist = dist;
    c->tohit = tohit;
    c->norm.x = tri->normal.x;
    c->norm.y = tri->normal.y;
    c->norm.z = tri->normal.z;
    c->tri.index = tri->index;
    c->flags |= k_HIT_IT;

    if (sCollidingJSP && ((xClumpCollBSPTriangle*)tri->index)->flags & 0x10)
    {
        c->flags |= k_HIT_0x20000;
    }

    if (sector != NULL)
    {
        c->oid = sector->triangles[tri->index].matIndex;
    }
    else if (sCollidingJSP)
    {
        c->oid = ((xClumpCollBSPTriangle*)tri->index)->matIndex;
    }
    else
    {
        c->oid = tri->index;
    }

    return tri;
}

S32 xSphereHitsEnv4(const xSphere* b, const xEnv* env, const xMat3x3* mat, xCollis* colls)
{
    RpIntersection isx;
    xCollis* c;
    xCollis* cend;
    S32 i;
    S32 numcs;
    F32 s;

    isx.type = rpINTERSECTSPHERE;
    memcpy(&isx.t.sphere, b, sizeof(xSphere));

    cbmat = mat;
    cend = colls + k_XCOLLS_IDX_COUNT;

    sCollidingJSP = 1;
    for (i = 0; i < env->geom->jsp_count; i++)
    {
        if (xSphereHitsBox(b->center, b->r, env->geom->jsp_bound[i]))
        {
            for (c = colls; c < cend; c++)
            {
                c->test_dist = c->dist;
            }

            xClumpColl_ForAllIntersections(env->geom->jsp_list[i]->colltree, &isx, sphereHitsEnv4CB,
                                           colls);

            for (c = colls; c < cend; c++)
            {
                if (c->dist < c->test_dist)
                {
                    c->oid = get_matid(env->geom->jsp_list[i], c->oid, 0);
                }
            }
        }
    }
    sCollidingJSP = 0;

    numcs = 0;
    for (c = colls; c < cend; c++)
    {
        if (c->flags & k_HIT_IT)
        {
            numcs++;

            s = 1.0f / c->dist;
            xVec3SMul(&c->hdng, &c->tohit, s);

            s = MIN(0.0f, c->dist - b->r);
            xVec3SMul(&c->depen, &c->hdng, s);

            if (xVec3Dot(&c->hdng, &c->norm) > 0.0f)
            {
                xVec3Inv(&c->norm, &c->norm);
            }
        }
    }

    return numcs;
}

S32 xSphereHitsEnv3(const xSphere* b, const xEnv* env, xCollis* colls, U8 ncolls, F32 sth)
{
    RpIntersection isx;
    U8 idx;
    S32 i;

    isx.type = rpINTERSECTSPHERE;
    memcpy(&isx.t.sphere, b, sizeof(xSphere));

    for (idx = 0; idx < ncolls; idx++)
    {
        colls[idx].flags &= ~k_HIT_IT;
        colls[idx].dist = FLOAT_MAX;
    }

    cbath = sth;
    cbnumcs = 0;
    cbmaxcs = ncolls;
    OTHER = 0xff;
    NEXT2 = 0xff;
    FLOOR = 0xff;

    sCollidingJSP = 1;
    for (i = 0; i < env->geom->jsp_count; i++)
    {
        if (xSphereHitsBox(b->center, b->r, env->geom->jsp_bound[i]))
        {
            for (idx = 0; idx < ncolls; idx++)
            {
                colls[idx].test_dist = colls[idx].dist;
            }

            xClumpColl_ForAllIntersections(env->geom->jsp_list[i]->colltree, &isx, sphereHitsEnv3CB,
                                           colls);

            for (idx = 0; idx < ncolls; idx++)
            {
                if (colls[idx].dist < colls[idx].test_dist)
                {
                    colls[idx].oid = get_matid(env->geom->jsp_list[i], colls[idx].oid, 0);
                }
            }
        }
    }
    sCollidingJSP = 0;

    return cbnumcs;
}

U32 xSphereHitsEnv(const xSphere* b, const xEnv* env, xCollis* coll)
{
    RpIntersection isx;
    S32 i;

    isx.type = rpINTERSECTSPHERE;
    memcpy(&isx.t.sphere, b, sizeof(xSphere));

    coll->flags &= ~k_HIT_IT;
    coll->dist = FLOAT_MAX;
    coll->mptr = NULL;
    coll->optr = NULL;

    sCollidingJSP = 1;
    for (i = 0; i < env->geom->jsp_count; i++)
    {
        if (xSphereHitsBox(b->center, b->r, env->geom->jsp_bound[i]))
        {
            coll->test_dist = coll->dist;
            xClumpColl_ForAllIntersections(env->geom->jsp_list[i]->colltree, &isx, sphereHitsEnvCB,
                                           coll);
            if (coll->dist < coll->test_dist)
            {
                coll->oid = get_matid(env->geom->jsp_list[i], coll->oid, 0);
            }
        }
    }
    sCollidingJSP = 0;

    return coll->flags & k_HIT_IT;
}
