#include "xShadowSimple.h"

struct ShadowSimpleCBParam
{
    xShadowSimpleCache* cache;
    RpIntersection* isx;
};

static xShadowSimpleQueue sCollQueue[2];
static RxRenderStateVector xrsv;
static RxObjSpace3DVertex sShadVert[384];
static RwRaster* sShadRasters[64];
static RwRaster* sShadRaster;
static U32 sShadVertCount;
static RwMatrixTag* sModelMat;

static void xShadowSimple_AddVerts(xShadowSimpleCache* cache);
static void xShadowSimple_CalcCorners(xShadowSimpleCache* cache, const xMat4x3* mat, F32 radius,
                                      F32 ecc);
static void xShadowSimple_SceneCollide(xShadowSimpleCache* cache, const xVec3* pos, F32 depth);
static S32 shadowRayEntCB(xEnt* ent, void* cbdata);
static RpCollisionTriangle* shadowRayModelCB(RpIntersection* isx, RpCollisionTriangle* tri,
                                             F32 dist, void* data);
static RpCollisionTriangle* shadowRayCB(RpIntersection*, RpWorldSector*, RpCollisionTriangle* tri,
                                        F32 dist, void* data);

static RpCollisionTriangle* shadowRayCB(RpIntersection*, RpWorldSector*, RpCollisionTriangle* tri,
                                        F32 dist, void* data)
{
    xVec3 xformnorm;
    xVec3* norm;
    F32* testdist;
    xShadowSimpleCache* cache = (xShadowSimpleCache*)data;

    if (sModelMat)
    {
        testdist = &cache->shadowHeight;
        xMat3x3RMulVec(&xformnorm, (xMat3x3*)sModelMat, (xVec3*)&tri->normal);
        xVec3Normalize(&xformnorm, &xformnorm);
        norm = &xformnorm;
    }
    else
    {
        testdist = &cache->envHeight;
        norm = (xVec3*)&tri->normal;
    }

    if (dist >= *testdist || norm->y < 0.0871557f)
    {
        return tri;
    }

    *testdist = dist;

    cache->poly.vert[0] = *(xVec3*)tri->vertices[0];
    cache->poly.vert[1] = *(xVec3*)tri->vertices[1];
    cache->poly.vert[2] = *(xVec3*)tri->vertices[2];
    cache->poly.norm = *(xVec3*)&tri->normal;

    return tri;
}

static RpCollisionTriangle* shadowRayModelCB(RpIntersection* isx, RpCollisionTriangle* tri,
                                             F32 dist, void* data)
{
    return shadowRayCB(isx, NULL, tri, dist, data);
}

static S32 shadowRayEntCB(xEnt* ent, void* cbdata)
{
    ShadowSimpleCBParam* cbparam = (ShadowSimpleCBParam*)cbdata;
    xModelInstance* m;
    F32 oldHeight;

    if (!(ent->baseFlags & 0x10))
    {
        return 1;
    }

    if (cbparam->cache->collSkipsItem == ent)
    {
        return 1;
    }

    m = ent->model;

    if (iModelNumBones(m->Data) > 1)
    {
        return 1;
    }

    oldHeight = cbparam->cache->shadowHeight;

    RwFrameTransform(RpAtomicGetFrame(m->Data), m->Mat, rwCOMBINEREPLACE);

    sModelMat = m->Mat;
    RpAtomicForAllIntersections(m->Data, cbparam->isx, shadowRayModelCB, cbparam->cache);
    sModelMat = NULL;

    if (cbparam->cache->shadowHeight != oldHeight)
    {
        cbparam->cache->castOnEnt = ent;
    }

    return 1;
}

static void xShadowSimple_SceneCollide(xShadowSimpleCache* cache, const xVec3* pos, F32 depth)
{
    xEnv* env;
    RpIntersection isx;
    S32 i;
    ShadowSimpleCBParam cbparam;
    xQCData qcd;

    cache->envHeight = 1e38f;
    cache->shadowHeight = 1e38f;
    cache->castOnEnt = NULL;
    cache->collPriority = 0;

    env = xglobals->sceneCur->env;

    isx.type = rpINTERSECTLINE;
    isx.t.line.start.x = pos->x;
    isx.t.line.start.y = 0.1f + pos->y;
    isx.t.line.start.z = pos->z;
    isx.t.line.end.x = pos->x;
    isx.t.line.end.y = pos->y - depth;
    isx.t.line.end.z = pos->z;

    for (i = 0; i < env->geom->jsp_count; i++)
    {
        xClumpColl_ForAllIntersections(env->geom->jsp_list[i]->colltree, &isx, shadowRayCB, cache);
    }

    if (1e38f != cache->envHeight)
    {
        cache->envHeight =
            isx.t.line.start.y + cache->envHeight * (isx.t.line.end.y - isx.t.line.start.y);
        isx.t.line.end.y = cache->envHeight;
    }

    if (!(cache->flags & 0x10))
    {
        xQuickCullForLine(&xqc_def_ctrl, &qcd, (xLine3*)&isx.t.line);

        cbparam.cache = cache;
        cbparam.isx = &isx;

        xGridCheckPosition(&colls_grid, (xVec3*)&isx.t.line.start, &qcd, shadowRayEntCB, &cbparam);
        xGridCheckPosition(&colls_oso_grid, (xVec3*)&isx.t.line.start, &qcd, shadowRayEntCB,
                           &cbparam);
    }

    if (1e38f != cache->shadowHeight)
    {
        cache->shadowHeight =
            isx.t.line.start.y + cache->shadowHeight * (isx.t.line.end.y - isx.t.line.start.y);
    }
    else
    {
        cache->shadowHeight = cache->envHeight;
    }

    if (1e38f != cache->shadowHeight && cache->castOnEnt == NULL)
    {
        cache->dydx = -cache->poly.norm.x / cache->poly.norm.y;
        cache->dydz = -cache->poly.norm.z / cache->poly.norm.y;
    }
}

static void xShadowSimple_CalcCorners(xShadowSimpleCache* cache, const xMat4x3* mat, F32 radius,
                                      F32 ecc)
{
    xVec3 tempnorm;
    // shadowRight/shadowAt are not in the PS2 DWARF. The statement order (x, z, then y) follows
    // the PS2 line table.
    xVec3 shadowRight;
    xVec3 shadowAt;

    if (1e38f == cache->shadowHeight)
    {
        return;
    }

    if (cache->castOnEnt)
    {
        xMat3x3RMulVec(&tempnorm, (xMat3x3*)cache->castOnEnt->model->Mat, &cache->poly.norm);
        xVec3Normalize(&tempnorm, &tempnorm);

        cache->dydx = -tempnorm.x / tempnorm.y;
        cache->dydz = -tempnorm.z / tempnorm.y;
    }

    if (cache->flags & 2)
    {
        shadowRight.x = ecc * (mat->right.x * radius);
        shadowRight.z = ecc * (mat->right.z * radius);
        shadowRight.y = shadowRight.x * cache->dydx + shadowRight.z * cache->dydz;
        shadowAt.x = mat->at.x * radius;
        shadowAt.z = mat->at.z * radius;
        shadowAt.y = shadowAt.x * cache->dydx + shadowAt.z * cache->dydz;
    }
    else
    {
        shadowRight.x = radius * ecc;
        shadowRight.z = 0.0f;
        shadowRight.y = shadowRight.x * cache->dydx;
        shadowAt.x = 0.0f;
        shadowAt.z = radius;
        shadowAt.y = radius * cache->dydz;
    }

    cache->corner[0].x = mat->pos.x + shadowRight.x;
    cache->corner[0].y = 0.02f + (cache->shadowHeight + shadowRight.y);
    cache->corner[0].z = mat->pos.z + shadowRight.z;

    cache->corner[1].x = mat->pos.x + shadowAt.x;
    cache->corner[1].y = 0.02f + (cache->shadowHeight + shadowAt.y);
    cache->corner[1].z = mat->pos.z + shadowAt.z;

    cache->corner[2].x = mat->pos.x - shadowAt.x;
    cache->corner[2].y = 0.02f + (cache->shadowHeight - shadowAt.y);
    cache->corner[2].z = mat->pos.z - shadowAt.z;

    cache->corner[3].x = mat->pos.x - shadowRight.x;
    cache->corner[3].y = 0.02f + (cache->shadowHeight - shadowRight.y);
    cache->corner[3].z = mat->pos.z - shadowRight.z;
}

static void xShadowSimple_AddVerts(xShadowSimpleCache* cache)
{
    if (1e38f == cache->shadowHeight || sShadVertCount >= 384)
    {
        return;
    }

    RwIm3DVertexSetPos(sShadVert + sShadVertCount + 0, cache->corner[0].x, cache->corner[0].y,
                       cache->corner[0].z);
    RwIm3DVertexSetPos(sShadVert + sShadVertCount + 1, cache->corner[1].x, cache->corner[1].y,
                       cache->corner[1].z);
    RwIm3DVertexSetPos(sShadVert + sShadVertCount + 2, cache->corner[2].x, cache->corner[2].y,
                       cache->corner[2].z);
    RwIm3DVertexSetPos(sShadVert + sShadVertCount + 3, cache->corner[1].x, cache->corner[1].y,
                       cache->corner[1].z);
    RwIm3DVertexSetPos(sShadVert + sShadVertCount + 4, cache->corner[2].x, cache->corner[2].y,
                       cache->corner[2].z);
    RwIm3DVertexSetPos(sShadVert + sShadVertCount + 5, cache->corner[3].x, cache->corner[3].y,
                       cache->corner[3].z);

    RwIm3DVertexSetRGBA(sShadVert + sShadVertCount + 0, 0, 0, 0, cache->alpha);
    RwIm3DVertexSetRGBA(sShadVert + sShadVertCount + 1, 0, 0, 0, cache->alpha);
    RwIm3DVertexSetRGBA(sShadVert + sShadVertCount + 2, 0, 0, 0, cache->alpha);
    RwIm3DVertexSetRGBA(sShadVert + sShadVertCount + 3, 0, 0, 0, cache->alpha);
    RwIm3DVertexSetRGBA(sShadVert + sShadVertCount + 4, 0, 0, 0, cache->alpha);
    RwIm3DVertexSetRGBA(sShadVert + sShadVertCount + 5, 0, 0, 0, cache->alpha);

    sShadRasters[sShadVertCount / 6] = cache->ptr_raster;

    sShadVertCount += 6;
}

void xShadowSimple_Init()
{
    RwTexture* tex;
    U32 i;

    memset(sCollQueue, 0, sizeof(sCollQueue));

    tex = (RwTexture*)xSTFindAsset(0x1A2A0413, NULL); // "shadow_round"

    sShadRaster = (tex != NULL) ? tex->raster : NULL;

    memset(sShadVert, 0, sizeof(sShadVert));

    for (i = 0; i < 64; i++)
    {
        sShadVert[i * 6 + 1].u = 1.0f;
        sShadVert[i * 6 + 2].v = 1.0f;
        sShadVert[i * 6 + 3].u = 1.0f;
        sShadVert[i * 6 + 4].v = 1.0f;
        sShadVert[i * 6 + 5].u = 1.0f;
        sShadVert[i * 6 + 5].v = 1.0f;

        RwIm3DVertexSetNormal(&sShadVert[i * 6 + 0], 0.0f, 1.0f, 0.0f);
        RwIm3DVertexSetNormal(&sShadVert[i * 6 + 1], 0.0f, 1.0f, 0.0f);
        RwIm3DVertexSetNormal(&sShadVert[i * 6 + 2], 0.0f, 1.0f, 0.0f);
        RwIm3DVertexSetNormal(&sShadVert[i * 6 + 3], 0.0f, 1.0f, 0.0f);
        RwIm3DVertexSetNormal(&sShadVert[i * 6 + 4], 0.0f, 1.0f, 0.0f);
        RwIm3DVertexSetNormal(&sShadVert[i * 6 + 5], 0.0f, 1.0f, 0.0f);
    }
}

void xShadowSimple_Reset()
{
    memset(sCollQueue, 0, sizeof(sCollQueue));
}

void xShadowSimple_CacheInit(xShadowSimpleCache* cache, xEnt* ent, U8 alpha)
{
    S32 i;
    S32 n;
    U32 j;
    zSimpleShadowTableHeader* sst;
    RwRaster* raster;
    U32 flags;
    U32 size;
    RwTexture* tex;

    memset(cache, 0, sizeof(xShadowSimpleCache));

    cache->envHeight = 1e38f;
    cache->shadowHeight = 1e38f;
    cache->flags = 1;
    cache->alpha = alpha;
    cache->collSkipsItem = ent;

    if (ent->model == NULL || ent->model->shadowID != 0xDEADBEEF)
    {
        return;
    }

    raster = NULL;
    flags = 0;

    n = xSTAssetCountByType('SHDW');

    for (i = 0; i < n; i++)
    {
        sst = (zSimpleShadowTableHeader*)xSTFindAssetByType('SHDW', i, &size);

        for (j = 0; j < sst->num; j++)
        {
            zSimpleShadow* table = (zSimpleShadow*)(sst + 1);

            if (ent->model->modelID == table[j].modelID)
            {
                tex = (RwTexture*)xSTFindAsset(table[j].shadowID, NULL);

                if (tex != NULL)
                {
                    raster = tex->raster;
                    flags = table[j].flags;
                }
                else
                {
                    raster = (RwRaster*)0xDEADBEEF;
                }
            }

            if (raster != NULL)
            {
                break;
            }
        }

        if (raster != NULL)
        {
            break;
        }
    }

    if (raster == NULL || raster == (RwRaster*)0xDEADBEEF)
    {
        raster = sShadRaster;
    }

    cache->ptr_raster = raster;
    cache->flags |= (U16)flags;
    // shadowID is only ever compared with the 0xDEADBEEF sentinel, so its low 32 bits are enough.
    ent->model->shadowID = (U32)(UPtr)raster;
}

void xShadowSimple_Add(xShadowSimpleCache* cache, xEnt* ent, F32 radius, F32 ecc)
{
    xShadowSimple_Add_Expert(cache, (xMat4x3*)ent->model->Mat, cache->alpha, radius, 10.0f, ecc);
}

void xShadowSimple_Render()
{
    xShadowSimpleCache* cache;
    S32 qnum;
    U32 i;
    U32 j;
    RwRaster* raster;

    for (qnum = 0; qnum < 2; qnum++)
    {
        cache = sCollQueue[qnum].cache;

        if (cache)
        {
            xShadowSimple_SceneCollide(cache, &sCollQueue[qnum].modelMatrix->pos,
                                       sCollQueue[qnum].height);

            cache->pos = sCollQueue[qnum].modelMatrix->pos;
            cache->at = sCollQueue[qnum].modelMatrix->at;

            xShadowSimple_CalcCorners(cache, sCollQueue[qnum].modelMatrix, sCollQueue[qnum].radius,
                                      sCollQueue[qnum].ecc);
            xShadowSimple_AddVerts(cache);
        }

        sCollQueue[qnum].cache = NULL;
    }

    if (sShadVertCount == 0)
    {
        return;
    }

    RxRenderStateVectorLoadDriverState(&xrsv);

    RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)rwBLENDINVSRCCOLOR);
    RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDDESTCOLOR);
    RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)TRUE);
    RwRenderStateSet(rwRENDERSTATESHADEMODE, (void*)rwSHADEMODEGOURAUD);
    RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)FALSE);

    for (i = 0; i < sShadVertCount; i = j)
    {
        raster = sShadRasters[i / 6];

        for (j = i; j < sShadVertCount && sShadRasters[j / 6] == raster; j += 6)
        {
        }

        RwRenderStateSet(rwRENDERSTATETEXTURERASTER, raster);

        if (RwIm3DTransform(&sShadVert[i], j - i, NULL,
                            rwIM3D_VERTEXUV | rwIM3D_VERTEXXYZ | rwIM3D_VERTEXRGBA))
        {
            RwIm3DRenderPrimitive(rwPRIMTYPETRILIST);
            RwIm3DEnd();
        }
    }

    RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)xrsv.SrcBlend);
    RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)xrsv.DestBlend);
    RwRenderStateSet(rwRENDERSTATESHADEMODE, (void*)xrsv.ShadeMode);
    RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)((xrsv.Flags >> 2) & 1));
    RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)((xrsv.Flags >> 3) & 1));

    sShadVertCount = 0;
}

void xShadowSimple_CacheInit_Expert(xShadowSimpleCache* cache, RwRaster* rast, U8 alpha)
{
    memset(cache, 0, sizeof(xShadowSimpleCache));

    cache->envHeight = 1e38f;
    cache->shadowHeight = 1e38f;
    cache->flags = 3;
    cache->alpha = alpha;
    cache->collSkipsItem = NULL;
    cache->radiusOptional = -1.0f;
    cache->tol_movement = 0.1f;

    if (rast)
    {
        cache->ptr_raster = rast;
    }
    else
    {
        cache->ptr_raster = sShadRaster;
    }
}

inline void XSSC_Add_NudgeItAndFudgeIt(xShadowSimpleCache* cache, const xMat4x3* mat_owner)
{
    xVec3* vert;
    xVec3 xformvert[3];
    xVec3 xformnorm;

    if (cache->castOnEnt)
    {
        xEnt* castOnEnt = cache->castOnEnt;

        xMat4x3Toworld(&xformvert[0], (xMat4x3*)castOnEnt->model->Mat, &cache->poly.vert[0]);
        xMat4x3Toworld(&xformvert[1], (xMat4x3*)castOnEnt->model->Mat, &cache->poly.vert[1]);
        xMat4x3Toworld(&xformvert[2], (xMat4x3*)castOnEnt->model->Mat, &cache->poly.vert[2]);

        xMat3x3RMulVec(&xformnorm, (xMat3x3*)castOnEnt->model->Mat, &cache->poly.norm);
        xVec3Normalize(&xformnorm, &xformnorm);

        // BUG: should be xabs(xformnorm.y) > 1e-5f
        if (xabs(xformnorm.y > 1e-5f))
        {
            cache->dydx = -xformnorm.x / xformnorm.y;
            cache->dydz = -xformnorm.z / xformnorm.y;
        }
        else
        {
            F32 poo = (xformnorm.y > 0.0f) ? 1e-5f : -1e-5f;
            cache->dydx = -xformnorm.x / poo;
            cache->dydz = -xformnorm.z / poo;
        }

        vert = &xformvert[0];
    }
    else
    {
        vert = &cache->poly.vert[0];
        if (vert->x == 1e38f)
        {
            return;
        }
    }

    F32 nx, nz, pdot;
    for (S32 i = 0; i < 3; i++)
    {
        xVec3* v0 = &vert[i];
        xVec3* v1 = &vert[i == 2 ? 0 : i + 1];
        nx = v0->z - v1->z;
        nz = v1->x - v0->x;
        pdot = nx * (mat_owner->pos.x - v0->x) + nz * (mat_owner->pos.z - v0->z);
        if (pdot > 1e-5f)
        {
            cache->collPriority += 20;
            break;
        }
    }

    cache->shadowHeight = cache->dydx * (mat_owner->pos.x - vert->x) + vert->y +
                          cache->dydz * (mat_owner->pos.z - vert->z);
}

inline S32 XSSC_Add_ShoveIntoQueue(xShadowSimpleCache* cache, const xMat4x3* mat_owner, F32 radius,
                                   F32 height, F32 ecc)
{
    for (S32 i = 0; i < 2; i++)
    {
        if (!sCollQueue[i].cache)
        {
            sCollQueue[i].cache = cache;
            sCollQueue[i].priority = sCollQueue[i].cache->collPriority;
            sCollQueue[i].modelMatrix = (xMat4x3*)mat_owner;
            sCollQueue[i].radius = radius;
            sCollQueue[i].height = height;
            sCollQueue[i].ecc = ecc;
            return 1;
        }

        if (cache->collPriority > sCollQueue[i].cache->collPriority)
        {
            if (sCollQueue[1].cache)
            {
                xShadowSimple_CalcCorners(sCollQueue[1].cache, sCollQueue[1].modelMatrix,
                                          sCollQueue[1].radius, sCollQueue[1].ecc);
                xShadowSimple_AddVerts(sCollQueue[1].cache);
            }

            for (S32 j = 0; j >= i; j--)
            {
                sCollQueue[j + 1] = sCollQueue[j];
            }

            sCollQueue[i].cache = cache;
            sCollQueue[i].priority = sCollQueue[i].cache->collPriority;
            sCollQueue[i].modelMatrix = (xMat4x3*)mat_owner;
            sCollQueue[i].radius = radius;
            sCollQueue[i].height = height;
            sCollQueue[i].ecc = ecc;
            return 1;
        }
    }

    return 0;
}

void xShadowSimple_Add_Expert(xShadowSimpleCache* cache, const xMat4x3* mat_owner, U8 alpha,
                              F32 radius, F32 height, F32 ecc)
{
    cache->alpha = alpha;

    if (radius >= 0.0f)
    {
        cache->radiusOptional = radius;
    }

    if (radius < 0.0f && cache->radiusOptional >= 0.0f)
    {
        radius = cache->radiusOptional;
    }

    if (cache->flags & 0x1)
    {
        cache->flags &= (U16)~0x1;
        cache->poly.vert[0].x = 1e38f;

        xShadowSimple_SceneCollide(cache, &mat_owner->pos, height);

        cache->pos = mat_owner->pos;
        cache->at = mat_owner->at;

        if (cache->shadowHeight != 1e38f)
        {
            xShadowSimple_CalcCorners(cache, mat_owner, radius, ecc);
            xShadowSimple_AddVerts(cache);
        }
    }
    else
    {
        U32 ownerMoved;

        if (cache->flags & 0x8)
        {
            xVec3 delta = cache->pos - mat_owner->pos;
            F32 ds2_moved = delta.length2();
            ownerMoved = (ds2_moved > SQR(cache->tol_movement));
        }
        else
        {
            ownerMoved = (cache->pos.x != mat_owner->pos.x || cache->pos.y != mat_owner->pos.y ||
                          cache->pos.z != mat_owner->pos.z);
        }

        if ((cache->flags & 0x8) && (ownerMoved || cache->castOnEnt))
        {
            XSSC_Add_NudgeItAndFudgeIt(cache, mat_owner);
        }
        else if (cache->shadowHeight != 1e38f && (ownerMoved || cache->castOnEnt))
        {
            XSSC_Add_NudgeItAndFudgeIt(cache, mat_owner);
            if (ownerMoved)
            {
                cache->envHeight = 1e38f;
            }
        }

        if (ownerMoved)
        {
            cache->collPriority += 6;
        }
        else if (cache->castOnEnt)
        {
            cache->collPriority += 3;
        }
        else
        {
            cache->collPriority += 2;
        }

        if (!XSSC_Add_ShoveIntoQueue(cache, mat_owner, radius, height, ecc))
        {
            xShadowSimple_CalcCorners(cache, mat_owner, radius, ecc);
            xShadowSimple_AddVerts(cache);
        }
    }
}
