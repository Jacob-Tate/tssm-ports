#include "iModel.h"
#include "iCamera.h"
#include "zMain.h"
#include "iD0Stats.h"

#include <rpskin.h>
#include <rtskinsp.h>

#define IMODEL_MAX_DIRECTIONAL_LIGHTS 4
#define IMODEL_MAX_MATERIALS 23

RpWorld* instance_world;
RwCamera* instance_camera;

static U32 gLastAtomicCount;
static RpAtomic* gLastAtomicList[256];
static RpLight* sEmptyDirectionalLight[IMODEL_MAX_DIRECTIONAL_LIGHTS];
static RpLight* sEmptyAmbientLight;
static RwRGBA sMaterialColor[IMODEL_MAX_MATERIALS];
static RwTexture* sMaterialTexture[IMODEL_MAX_MATERIALS];
static U8 sMaterialAlpha[IMODEL_MAX_MATERIALS];

static RpMaterial* iModelMaterialMulCB(RpMaterial* material, void* data);
static RpMaterial* iModelSetMaterialTextureCB(RpMaterial* material, void* data);
static U32 iModelTagInternal(xModelTag* tag, const RpAtomic* model, F32 x, F32 y, F32 z,
                             S32 closeV);
static RpAtomic* NextAtomicCallback(RpAtomic* atomic, void* data);
static RpAtomic* FindAndInstanceAtomicCallback(RpAtomic* atomic, void* data);
static RpAtomic* iModelStreamRead(RwStream* stream);

static RwFrame* GetChildFrameHierarchy(RwFrame* frame, void* data)
{
    RpHAnimHierarchy* hierarchy = RpHAnimFrameGetHierarchy(frame);
    if (hierarchy == NULL)
    {
        RwFrameForAllChildren(frame, GetChildFrameHierarchy, data);
        return frame;
    }

    *(RpHAnimHierarchy**)data = hierarchy;
    return NULL;
}

RpHAnimHierarchy* iModelGetHierarchy(RpAtomic* imodel)
{
    RpHAnimHierarchy* hierarchy = NULL;
    RwFrame* frame = RpAtomicGetFrame(imodel);
    GetChildFrameHierarchy(frame, &hierarchy);
    return hierarchy;
}

void iModelInit()
{
    RwFrame* frame;
    RwRGBAReal black = { 0.0f, 0.0f, 0.0f, 0.0f };
    S32 i;

    if (sEmptyDirectionalLight[0] == NULL)
    {
        for (i = 0; i < IMODEL_MAX_DIRECTIONAL_LIGHTS; i++)
        {
            sEmptyDirectionalLight[i] = RpLightCreate(rpLIGHTDIRECTIONAL);
            RpLightSetColor(sEmptyDirectionalLight[i], &black);
            frame = RwFrameCreate();
            RpLightSetFrame(sEmptyDirectionalLight[i], frame);
        }

        sEmptyAmbientLight = RpLightCreate(rpLIGHTAMBIENT);
        RpLightSetColor(sEmptyAmbientLight, &black);
    }
}

void iModelDeInit()
{
    if (sEmptyDirectionalLight[0] != NULL)
    {
        for (S32 i = 0; i < IMODEL_MAX_DIRECTIONAL_LIGHTS; i++)
        {
            RwFrame* frame = RpLightGetFrame(sEmptyDirectionalLight[i]);
            _rwFrameSyncDirty();
            RwFrameDestroy(frame);
            RpLightDestroy(sEmptyDirectionalLight[i]);
            sEmptyDirectionalLight[i] = NULL;
        }

        RpLightDestroy(sEmptyAmbientLight);
        sEmptyAmbientLight = NULL;
    }
}

static RpAtomic* FindAndInstanceAtomicCallback(RpAtomic* atomic, void* data)
{
    RpHAnimHierarchy* pHier = iModelGetHierarchy(atomic);
    RpGeometry* pGeom = atomic->geometry;
    RpSkin* pSkin = RpSkinGeometryGetSkin(pGeom);
    RwFrame* root;

    if (pSkin != NULL && pHier == NULL)
    {
        pHier = RpHAnimHierarchyCreate(RpSkinGetNumBones(pSkin), NULL, NULL,
                                       rpHANIMHIERARCHYLOCALSPACEMATRICES, 0x24);
        RpHAnimFrameSetHierarchy(RpAtomicGetFrame(atomic), pHier);
    }

    if (pHier != NULL && pSkin != NULL)
    {
        RpSkinAtomicSetHAnimHierarchy(atomic, pHier);
    }

    if (pHier != NULL)
    {
        pHier->flags = rpHANIMHIERARCHYLOCALSPACEMATRICES;
    }

    if (pHier != NULL && pSkin != NULL)
    {
        iModelAnimBoundPrepare(atomic);
    }

    root = RwFrameGetRoot(RpAtomicGetFrame(atomic));

    if (gLastAtomicCount < 256)
    {
        gLastAtomicList[gLastAtomicCount++] = atomic;
    }

    return atomic;
}

static RpAtomic* iModelStreamRead(RwStream* stream)
{
    RpClump* clump;
    U32 i;
    U32 maxIndex;
    F32 maxRadius;
    F32 testRadius;

    static S32 num_models = 0;

    if (stream == NULL)
    {
        return NULL;
    }

    if (RwStreamFindChunk(stream, rwID_CLUMP, NULL, NULL) == 0)
    {
        RwStreamClose(stream, NULL);
        return NULL;
    }

    clump = RpClumpStreamRead(stream);
    RwStreamClose(stream, NULL);

    if (clump == NULL)
    {
        return NULL;
    }

    RwBBox tmpbbox = { { 1000.0f, 1000.0f, 1000.0f }, { -1000.0f, -1000.0f, -1000.0f } };

    instance_world = RpWorldCreate(&tmpbbox);
    instance_camera = iCameraCreate(FB_XRES, FB_YRES, 0);
    RpWorldAddCamera(instance_world, instance_camera);

    gLastAtomicCount = 0;
    RpClumpForAllAtomics(clump, FindAndInstanceAtomicCallback, NULL);

    RpWorldRemoveCamera(instance_world, instance_camera);
    iCameraDestroy(instance_camera);
    RpWorldDestroy(instance_world);

    if (gLastAtomicCount > 1)
    {
        maxRadius = -1.0f;
        maxIndex = 0;

        for (i = 0; i < gLastAtomicCount; i++)
        {
            if (gLastAtomicList[i]->boundingSphere.radius > maxRadius)
            {
                maxRadius = gLastAtomicList[i]->boundingSphere.radius;
                maxIndex = i;
            }
        }

        for (i = 0; i < gLastAtomicCount; i++)
        {
            if (i != maxIndex)
            {
                testRadius = gLastAtomicList[i]->boundingSphere.radius +
                             xVec3Dist((xVec3*)&gLastAtomicList[i]->boundingSphere.center,
                                       (xVec3*)&gLastAtomicList[maxIndex]->boundingSphere.center);
                if (testRadius > maxRadius)
                {
                    maxRadius = testRadius;
                }
            }
        }

        maxRadius *= 1.05f;

        for (i = 0; i < gLastAtomicCount; i++)
        {
            if (i != maxIndex)
            {
                gLastAtomicList[i]->boundingSphere.center =
                    gLastAtomicList[maxIndex]->boundingSphere.center;
            }
            gLastAtomicList[i]->boundingSphere.radius = maxRadius;
#ifndef PLATFORM_PC
            gLastAtomicList[i]->interpolator.flags &= ~rpINTERPOLATORDIRTYSPHERE;
#endif
            // PORT (treedome, pc/iModel.cpp): the bit tells RenderWare not to
            // recompute the sphere just written. librw never recomputes it, and
            // the PC RpAtomic has no interpolator (rwsdk/rpworld.h drops it so
            // that reaching for it is a compile error, not a wrong read).
        }
    }

#ifndef PLATFORM_PC
    for (S32 i = 0; i < gLastAtomicCount; i++)
    {
        RpAtomic* atomic = gLastAtomicList[i];
        RpGeometry* geom = atomic->geometry;
        if (geom != NULL)
        {
            RpSkin* skin = RpSkinGeometryGetSkin(geom);
            if (skin != NULL)
            {
                RtSkinSplitAtomicSplitGeometry(atomic, 10);
            }
        }
    }
#endif
    // TSSM_D0: what that split would have to work with (iD0Stats.h).
    for (S32 i = 0; i < gLastAtomicCount; i++)
    {
        iD0SkinGeometry(gLastAtomicList[i]);
    }

    // PORT: the GameCube skins through a 10-matrix palette, so retail splits
    // every skinned geometry into pieces of at most 10 bones. librw's skinning
    // takes the whole hierarchy in one draw, and has no RtSkinSplit to call.

    return gLastAtomicList[0];
}

RpAtomic* iModelFileNew(void* buffer, U32 size)
{
    RwMemory rwmem;
    rwmem.start = (U8*)buffer;
    rwmem.length = size;
    return iModelStreamRead(RwStreamOpen(rwSTREAMMEMORY, rwSTREAMREAD, &rwmem));
}

void iModelUnload(RpAtomic* userdata)
{
    RpClump* clump;
    RwFrame* frame;
    RwFrame* root;

    clump = RpAtomicGetClump(userdata);
    frame = RpClumpGetFrame(clump);
    if (frame != NULL)
    {
        root = RwFrameGetRoot(frame);
        if (root != NULL)
        {
            frame = root;
        }
        RwFrameDestroyHierarchy(frame);
        RpClumpSetFrame(clump, NULL);
    }

    if (clump != NULL)
    {
        RpClumpDestroy(clump);
    }
}

static RpAtomic* NextAtomicCallback(RpAtomic* atomic, void* data)
{
    RpAtomic** nextModel = (RpAtomic**)data;

    if (*nextModel == atomic)
    {
        *nextModel = NULL;
    }
    else if (*nextModel == NULL)
    {
        *nextModel = atomic;
    }

    return atomic;
}

RpAtomic* iModelFile_RWMultiAtomic(RpAtomic* model)
{
    RpClump* clump;
    RpAtomic* nextModel;

    if (model == NULL)
    {
        return NULL;
    }

    clump = RpAtomicGetClump(model);
    nextModel = model;
    RpClumpForAllAtomics(clump, NextAtomicCallback, &nextModel);
    return nextModel;
}

U32 iModelNumBones(RpAtomic* model)
{
    RpHAnimHierarchy* hierarchy = iModelGetHierarchy(model);
    if (hierarchy == NULL)
    {
        return 0;
    }
    return hierarchy->numNodes;
}

void iModelQuatToMat(xQuat* quat, xVec3* tran, RwMatrixTag* mat)
{
    quat->s = -quat->s;
    xQuatToMat(quat, (xMat3x3*)mat);
    quat->s = -quat->s;
    mat->pos.x = tran->x;
    mat->pos.y = tran->y;
    mat->pos.z = tran->z;
}

void iModelAnimMatrices(RpAtomic* model, xQuat* quat, xVec3* tran, RwMatrixTag* mat)
{
    RpHAnimHierarchy* pHierarchy;
    RwMatrixTag matrixStack[32];
    RwMatrixTag* pMatrixStackTop;
    RpHAnimNodeInfo* pCurrentFrame;
    S32 currentFrameFlags;
    S32 i;
    S32 numFrames;
    RwMatrixTag* pMatrixArray;
    RwMatrixTag parentMatrix;
    RwMatrixTag frameMatrix;
    RwMatrixTag tempMatrix;
    RwMatrixTag* ptrMatrix;

    pHierarchy = iModelGetHierarchy(model);
    if (pHierarchy == NULL)
    {
        return;
    }

    pMatrixStackTop = matrixStack;

    RwMatrixSetIdentity(&parentMatrix);

    *pMatrixStackTop++ = parentMatrix;

    numFrames = pHierarchy->numNodes;
    pCurrentFrame = pHierarchy->pNodeInfo;
    pMatrixArray = mat;

    for (i = 0; i < numFrames; i++)
    {
        currentFrameFlags = pCurrentFrame->flags;

        if (currentFrameFlags & rpHANIMPUSHPARENTMATRIX)
        {
            *pMatrixStackTop++ = parentMatrix;
        }

        iModelQuatToMat(quat, tran, &frameMatrix);
        xMat4x3Mul((xMat4x3*)&tempMatrix, (const xMat4x3*)&frameMatrix,
                   (const xMat4x3*)&parentMatrix);
        *pMatrixArray = tempMatrix;

        if (currentFrameFlags & rpHANIMPOPPARENTMATRIX)
        {
            ptrMatrix = --pMatrixStackTop;
        }
        else
        {
            ptrMatrix = &tempMatrix;
        }

        parentMatrix = *ptrMatrix;

        pMatrixArray++;
        quat++;
        tran++;
        pCurrentFrame++;
    }
}

// Evicts all cached instance data from the RenderWare resource arena by allocating as much of it
// as possible, filling it with a marker and freeing it again.
static inline void iModelPurgeResourceArena()
{
    // PORT: RenderWare keeps instanced geometry in a resource arena, and this
    // flushes it. librw instances through each atomic's pipeline and has no
    // arena, so there is nothing to evict.
#ifndef PLATFORM_PC
    RwInt32 arenaSize = RwResourcesGetArenaSize();
    RwResEntry* entry = RwResourcesAllocateResEntry(NULL, NULL, arenaSize, NULL);

    while (entry == NULL && arenaSize > 0)
    {
        arenaSize -= 0x400;
        entry = RwResourcesAllocateResEntry(NULL, NULL, arenaSize, NULL);
    }

    if (entry != NULL)
    {
        U32* data = (U32*)(entry + 1);
        for (U32 i = 0; i < (U32)arenaSize / 4; i++)
        {
            data[i] = 0xDEADBEEF;
        }
        RwResourcesFreeResEntry(entry);
    }
#endif
}

void iModelRender(RpAtomic* model, RwMatrixTag* mat)
{
    RpHAnimHierarchy* pHierarchy;
    RwMatrixTag* pAnimOldMatrix;
    RwFrame* frame;

    pHierarchy = iModelGetHierarchy(model);

    static S32 draw_all = 1;

    if (pHierarchy != NULL)
    {
        pAnimOldMatrix = pHierarchy->pMatrixArray;
        pHierarchy->pMatrixArray = mat + 1;
    }

    frame = RpAtomicGetFrame(model);
    frame->ltm = *mat;
    RwMatrixUpdate(&frame->ltm);

    if (pHierarchy != NULL && model->geometry->numTriangles > 3000)
    {
        iModelPurgeResourceArena();
    }

    model->renderCallBack(model);

    if (pHierarchy != NULL)
    {
        pHierarchy->pMatrixArray = pAnimOldMatrix;
    }
}

xSphere* iModelWorldBoundSphere(RpAtomic* model, RwMatrixTag* mat, xSphere* sphere)
{
    RwSphere* modelsphere = &model->boundingSphere;
    F32 xScale2;
    F32 yScale2;
    F32 zScale2;

    sphere = sphere ? sphere : (xSphere*)&model->worldBoundingSphere;

    RwV3dTransformPoints((RwV3d*)&sphere->center, &modelsphere->center, 1, mat);

    xScale2 =
        mat->right.x * mat->right.x + mat->right.y * mat->right.y + mat->right.z * mat->right.z;
    yScale2 = mat->up.x * mat->up.x + mat->up.y * mat->up.y + mat->up.z * mat->up.z;
    zScale2 = mat->at.x * mat->at.x + mat->at.y * mat->at.y + mat->at.z * mat->at.z;

    sphere->r = modelsphere->radius * xsqrt(xScale2 >= (yScale2 >= zScale2 ? yScale2 : zScale2) ?
                                                xScale2 :
                                                (yScale2 >= zScale2 ? yScale2 : zScale2));

    return sphere;
}

void iModelSetWorldBoundFromBox(RpAtomic* model, xBox* box)
{
    F32 sizeX;
    F32 sizeY;
    F32 sizeZ;

    model->worldBoundingSphere.center.x = 0.5f * (box->upper.x + box->lower.x);
    model->worldBoundingSphere.center.y = 0.5f * (box->upper.y + box->lower.y);
    model->worldBoundingSphere.center.z = 0.5f * (box->upper.z + box->lower.z);

    sizeX = 0.5f * (box->upper.x - box->lower.x);
    sizeY = 0.5f * (box->upper.y - box->lower.y);
    sizeZ = 0.5f * (box->upper.z - box->lower.z);

    model->worldBoundingSphere.radius = xsqrt(sizeX * sizeX + sizeY * sizeY + sizeZ * sizeZ);
}

U32 iModelVertCount(RpAtomic* model)
{
    return model->geometry->numVertices;
}

// Skinning of positions: blends the vertex through up to four bone matrices (skin-to-bone matrix
// times bone matrix, cached per call in scratch memory), then transforms it by the root matrix.
// Note: catMatFlags only covers 64 bones, but bone indices up to 255 are tested (as in retail).
static inline void SkinXform(xVec3* dest, const xVec3* vert, RwMatrixTag* mat,
                             const RwMatrixTag* skinmat, const F32* wt, const U32* idx, U32 count)
{
    U32 catMatFlags[2] = { 0, 0 };
    RwMatrixTag* catmat = (RwMatrixTag*)xMemScratchPush(0x1000, 0);
    RwMatrixTag* rootmat = mat;
    mat++;

    while (count != 0)
    {
        for (U32 i = 0; i < 4; i++)
        {
            U32 midx = (*idx >> (i << 3)) & 0xFF;
            if (!(catMatFlags[midx >> 5] & (1 << (midx & 0x1F))))
            {
                xMat4x3Mul((xMat4x3*)(catmat + midx), (const xMat4x3*)(skinmat + midx),
                           (const xMat4x3*)(mat + midx));
                catMatFlags[midx >> 5] |= 1 << (midx & 0x1F);
            }
        }

        xVec3 accumV;
        accumV.x = accumV.y = accumV.z = 0.0f;

        RwMatrixTag* pMatrix;
        const F32* fwt = wt;
        U32 wtidx = *idx;
        U32 maxwt = 4;

        while (*fwt && maxwt)
        {
            pMatrix = catmat + (wtidx & 0xFF);
            wtidx >>= 8;

            accumV.x += *fwt * (pMatrix->right.x * vert->x + pMatrix->up.x * vert->y +
                                pMatrix->at.x * vert->z + pMatrix->pos.x);
            accumV.y += *fwt * (pMatrix->right.y * vert->x + pMatrix->up.y * vert->y +
                                pMatrix->at.y * vert->z + pMatrix->pos.y);
            accumV.z += *fwt * (pMatrix->right.z * vert->x + pMatrix->up.z * vert->y +
                                pMatrix->at.z * vert->z + pMatrix->pos.z);

            fwt++;
            maxwt--;
        }

        dest->x = rootmat->right.x * accumV.x + rootmat->up.x * accumV.y +
                  rootmat->at.x * accumV.z + rootmat->pos.x;
        dest->y = rootmat->right.y * accumV.x + rootmat->up.y * accumV.y +
                  rootmat->at.y * accumV.z + rootmat->pos.y;
        dest->z = rootmat->right.z * accumV.x + rootmat->up.z * accumV.y +
                  rootmat->at.z * accumV.z + rootmat->pos.z;

        vert++;
        idx++;
        wt += 4;
        count--;
        dest++;
    }

    xMemScratchPop(catmat);
}

U32 iModelVertEval(RpAtomic* model, U32 index, U32 count, RwMatrixTag* mat, xVec3* vert,
                   xVec3* dest)
{
    RpGeometry* geom = RpAtomicGetGeometry(model);
    U32 numV;
    RpSkin* skin;

    if (vert == NULL)
    {
        numV = geom->numVertices;
        if (index >= numV || count == 0)
        {
            return 0;
        }

        count = (count < numV - index) ? count : (numV - index);
        vert = (xVec3*)geom->morphTarget[0].verts;
        vert += index;
    }

    skin = RpSkinGeometryGetSkin(geom);
    if (skin != NULL)
    {
        SkinXform(dest, vert, mat, RpSkinGetSkinToBoneMatrices(skin),
                  (const F32*)(RpSkinGetVertexBoneWeights(skin) + index),
                  RpSkinGetVertexBoneIndices(skin) + index, count);
    }
    else
    {
        RwV3dTransformPoints((RwV3d*)dest, (const RwV3d*)vert, count, mat);
    }

    return count;
}

// Skinning of normals: like the vertex skinning, but only the renormalized rotation part of the
// bone matrices is applied. Param and local names as in the Rat erased DWARF (SkinNormals is
// always inlined there too).
static inline void SkinNormals(xVec3* dest, const xVec3* vert, const RwMatrixTag* mat,
                               const RwMatrixTag* skinmat, const F32* wt, const U32* idx, U32 count)
{
    U32 catMatFlags[2] = { 0, 0 };
    RwMatrixTag* catmat = (RwMatrixTag*)xMemScratchPush(0x4000, 0);
    const RwMatrixTag* rootmat = mat;
    mat++;

    while (count != 0)
    {
        for (U32 i = 0; i < 4; i++)
        {
            U32 midx = (*idx >> (i << 3)) & 0xFF;
            if (!(catMatFlags[midx >> 5] & (1 << (midx & 0x1F))))
            {
                // cm: not in the PS2 DWARF (SkinNormals is inlined there); name and type from the
                // Rat erased DWARF of SkinNormals (xMat4x3& cm).
                xMat4x3& cm = *(xMat4x3*)(catmat + midx);
                xMat3x3Mul(&cm, (const xMat3x3*)(skinmat + midx), (const xMat3x3*)(mat + midx));
                xMat3x3Normalize(&cm, &cm);
                catMatFlags[midx >> 5] |= 1 << (midx & 0x1F);
            }
        }

        xVec3 accumV;
        accumV.x = accumV.y = accumV.z = 0.0f;

        RwMatrixTag* pMatrix;
        const F32* fwt = wt;
        U32 wtidx = *idx;
        U32 maxwt = 4;

        while (*fwt && maxwt)
        {
            pMatrix = catmat + (wtidx & 0xFF);
            wtidx >>= 8;

            accumV.x += *fwt * (pMatrix->right.x * vert->x + pMatrix->up.x * vert->y +
                                pMatrix->at.x * vert->z);
            accumV.y += *fwt * (pMatrix->right.y * vert->x + pMatrix->up.y * vert->y +
                                pMatrix->at.y * vert->z);
            accumV.z += *fwt * (pMatrix->right.z * vert->x + pMatrix->up.z * vert->y +
                                pMatrix->at.z * vert->z);

            fwt++;
            maxwt--;
        }

        dest->x = rootmat->right.x * accumV.x + rootmat->up.x * accumV.y + rootmat->at.x * accumV.z;
        dest->y = rootmat->right.y * accumV.x + rootmat->up.y * accumV.y + rootmat->at.y * accumV.z;
        dest->z = rootmat->right.z * accumV.x + rootmat->up.z * accumV.y + rootmat->at.z * accumV.z;

        vert++;
        idx++;
        wt += 4;
        count--;
        dest++;
    }

    xMemScratchPop(catmat);
}

U32 iModelNormalEval(xVec3* out, const RpAtomic& m, const RwMatrixTag* mat, size_t index, S32 size,
                     const xVec3* in)
{
    RpGeometry* geom;
    S32 max_size;
    RpSkin* skin;
    const RwMatrixTag* skin_mats;
    const F32* bone_weights;
    const U32* bone_indices;
    xMat4x3 nmat;

    geom = RpAtomicGetGeometry(&m);

    if (in == NULL)
    {
        max_size = geom->numVertices - index;
        if (size < 0 || size > max_size)
        {
            size = max_size;
        }
        in = (const xVec3*)geom->morphTarget[0].normals;
    }

    if (size <= 0)
    {
        return 0;
    }

    in += index;

    skin = RpSkinGeometryGetSkin(geom);
    if (skin != NULL)
    {
        skin_mats = RpSkinGetSkinToBoneMatrices(skin);
        bone_weights = (const F32*)RpSkinGetVertexBoneWeights(skin) + index;
        bone_indices = RpSkinGetVertexBoneIndices(skin) + index;
        SkinNormals(out, in, mat, skin_mats, bone_weights, bone_indices, size);
    }
    else
    {
        xMat3x3Normalize(&nmat, (const xMat3x3*)mat);
        nmat.pos.assign(0.0f, 0.0f, 0.0f);
        RwV3dTransformVectors((RwV3d*)out, (const RwV3d*)in, size, (RwMatrixTag*)&nmat);
    }

    return size;
}

// 'HI_Tags' user data. The struct name is ours (no HI DWARF has one; Rat's locals are just numTags
// and tagList); the asm reads the count at +0 and the xModelTag array from +4.
struct iModelTagHdr
{
    S32 NumTags;
    xModelTag Tags[1];
};

static U32 iModelTagUserData(xModelTag* tag, const RpAtomic* model, F32 x, F32 y, F32 z, S32 closeV)
{
    S32 i;
    S32 count;
    RpUserDataArray* array;
    RpUserDataArray* testarray;
    F32 distSqr;
    F32 closeDistSqr;
    S32 numTags;
    S32 t;
    xModelTag* tagList;

    count = RpGeometryGetUserDataArrayCount(model->geometry);
    array = NULL;

    for (i = 0; i < count; i++)
    {
        testarray = RpGeometryGetUserDataArray(model->geometry, i);
        if (strcmp(testarray->name, "HI_Tags") == 0)
        {
            array = testarray;
            break;
        }
    }

    if (array == NULL)
    {
        memset(tag, 0, sizeof(xModelTag));
        return 0;
    }

    numTags = ((iModelTagHdr*)array->data)->NumTags;
    closeDistSqr = 1.0e9f;
    tagList = ((iModelTagHdr*)array->data)->Tags;

    if (closeV < 0 || closeV > numTags)
    {
        closeV = 0;
        for (t = 0; t < numTags; t++)
        {
            distSqr = SQR(tagList[t].v.x - x) + SQR(tagList[t].v.y - y) + SQR(tagList[t].v.z - z);
            if (distSqr < closeDistSqr)
            {
                closeV = t;
                closeDistSqr = distSqr;
            }
        }

        if (tag != NULL)
        {
            *tag = tagList[closeV];
        }
    }
    else
    {
        if (tag != NULL)
        {
            *tag = tagList[closeV];
        }
    }

    return closeV;
}

static U32 iModelTagInternal(xModelTag* tag, const RpAtomic* model, F32 x, F32 y, F32 z, S32 closeV)
{
    RpGeometry* geom;
    RwV3d* vert;
    S32 v;
    S32 numV;
    F32 distSqr;
    F32 closeDistSqr;
    RpSkin* skin;
    const RwMatrixWeights* wt;

    geom = model->geometry;
    vert = geom->morphTarget[0].verts;

    if (vert == NULL)
    {
        return iModelTagUserData(tag, model, x, y, z, closeV);
    }

    numV = geom->numVertices;
    closeDistSqr = 1.0e9f;

    if (closeV < 0 || closeV > numV)
    {
        closeV = 0;
        for (v = 0; v < numV; v++)
        {
            distSqr = SQR(vert[v].x - x) + SQR(vert[v].y - y) + SQR(vert[v].z - z);
            if (distSqr < closeDistSqr)
            {
                closeV = v;
                closeDistSqr = distSqr;
            }
        }

        if (tag != NULL)
        {
            tag->v.x = x;
            tag->v.y = y;
            tag->v.z = z;
        }
    }
    else
    {
        if (tag != NULL)
        {
            tag->v.x = vert[closeV].x;
            tag->v.y = vert[closeV].y;
            tag->v.z = vert[closeV].z;
        }
    }

    if (tag != NULL)
    {
        skin = RpSkinGeometryGetSkin(model->geometry);
        if (skin != NULL)
        {
            wt = RpSkinGetVertexBoneWeights(skin) + closeV;
            tag->matidx = RpSkinGetVertexBoneIndices(skin)[closeV];
            tag->wt[0] = wt->w0;
            tag->wt[1] = wt->w1;
            tag->wt[2] = wt->w2;
            tag->wt[3] = wt->w3;
        }
        else
        {
            tag->matidx = 0;
            tag->wt[0] = 0.0f;
            tag->wt[1] = 0.0f;
            tag->wt[2] = 0.0f;
            tag->wt[3] = 0.0f;
        }
    }

    return closeV;
}

U32 iModelTagSetup(xModelTag* tag, const RpAtomic* model, F32 x, F32 y, F32 z)
{
    return iModelTagInternal(tag, model, x, y, z, -1);
}

U32 iModelTagSetup(xModelTagWithNormal* tag, const RpAtomic* model, F32 x, F32 y, F32 z)
{
    U32 index = iModelTagInternal(tag, model, x, y, z, -1);
    xVec3* normals = (xVec3*)model->geometry->morphTarget[0].normals;
    if (normals != NULL)
    {
        tag->normal = normals[index];
    }
    return index;
}

void iModelTagEval(RpAtomic* model, const xModelTag* tag, RwMatrixTag* mat, xVec3* dest)
{
    RpGeometry* geom;
    RpSkin* skin;
    const RwMatrixTag* skinmat;

    if (tag->wt[0])
    {
        geom = RpAtomicGetGeometry(model);
        skin = RpSkinGeometryGetSkin(geom);
        skinmat = RpSkinGetSkinToBoneMatrices(skin);
        SkinXform(dest, &tag->v, mat, skinmat, tag->wt, &tag->matidx, 1);
    }
    else
    {
        RwV3dTransformPoints((RwV3d*)dest, (const RwV3d*)&tag->v, 1, mat);
    }
}

void iModelTagEval(RpAtomic* model, const xModelTagWithNormal* tag, RwMatrixTag* mat, xVec3* dest,
                   xVec3* normal)
{
    iModelTagEval(model, tag, mat, dest);

    if (tag->wt[0])
    {
        RpSkin* skin = RpSkinGeometryGetSkin(model->geometry);
        const RwMatrixTag* skinmat = RpSkinGetSkinToBoneMatrices(skin);
        SkinNormals(normal, &tag->normal, mat, skinmat, tag->wt, &tag->matidx, 1);
    }
    else
    {
        RwV3dTransformVectors((RwV3d*)normal, (const RwV3d*)&tag->normal, 1, mat);
    }
}

static U32 sMaterialIdx;
static U32 sMaterialFlags;
static RpAtomic* sLastMaterial;

void iModelSetMaterialAlpha(RpAtomic* model, U8 alpha)
{
    RpGeometry* geom;
    U32 i;
    RpMaterial* material;
    RwRGBA* col;

    geom = RpAtomicGetGeometry(model);

    if (model != sLastMaterial)
    {
        sMaterialFlags = 0;
    }

    geom->flags |= rpGEOMETRYMODULATEMATERIALCOLOR;

    sMaterialIdx = geom->matList.numMaterials;
    if (sMaterialIdx > IMODEL_MAX_MATERIALS)
    {
        sMaterialIdx = IMODEL_MAX_MATERIALS;
    }

    for (i = 0; i < sMaterialIdx; i++)
    {
        material = geom->matList.materials[i];
        col = RpMaterialGetColor(material);
        sMaterialAlpha[i] = col->alpha;

        RwRGBA new_col = *col;
        new_col.alpha = alpha;
        RpMaterialSetColor(material, &new_col);
    }

    sMaterialFlags |= 0x1;
    sLastMaterial = model;
}

void iModelResetMaterial(RpAtomic* model)
{
    RpGeometry* geom;
    S32 i;
    RpMaterial* material;

    if (model != sLastMaterial)
    {
        sMaterialFlags = 0;
    }

    geom = model->geometry;

    for (i = 0; i < geom->matList.numMaterials; i++)
    {
        material = geom->matList.materials[i];

        if ((sMaterialFlags & 0x3) == 0x3)
        {
            RwRGBA newColor = sMaterialColor[i];
            newColor.alpha = sMaterialAlpha[i];
            RpMaterialSetColor(material, &newColor);
        }
        else
        {
            if (sMaterialFlags & 0x2)
            {
                RwRGBA newColor = sMaterialColor[i];
                newColor.alpha = material->color.alpha;
                RpMaterialSetColor(material, &newColor);
            }

            if (sMaterialFlags & 0x1)
            {
                RwRGBA newColor = material->color;
                newColor.alpha = sMaterialAlpha[i];
                RpMaterialSetColor(material, &newColor);
            }
        }

        if (sMaterialFlags & 0x4)
        {
            RpMaterialSetTexture(material, sMaterialTexture[i]);
        }
    }

    sMaterialFlags = 0;
}

static RpMaterial* iModelSetMaterialTextureCB(RpMaterial* material, void* data)
{
    sMaterialTexture[sMaterialIdx++] = material->texture;
    RpMaterialSetTexture(material, (RwTexture*)data);
    return material;
}

void iModelSetMaterialTexture(RpAtomic* model, void* texture)
{
    RpGeometry* geom;

    if (model != sLastMaterial)
    {
        sMaterialFlags = 0;
    }

    geom = RpAtomicGetGeometry(model);
    sMaterialIdx = 0;
    RpGeometryForAllMaterials(geom, iModelSetMaterialTextureCB, texture);

    sMaterialFlags |= 0x4;
    sLastMaterial = model;
}

static inline void U8_COLOR_CLAMP(U8& destu8, F32 srcf32)
{
    if (srcf32 < 0.0f)
    {
        srcf32 = 0.0f;
    }
    else if (srcf32 > 255.0f)
    {
        srcf32 = 255.0f;
    }

    destu8 = (U8)srcf32;
}

static RpMaterial* iModelMaterialMulCB(RpMaterial* material, void* data)
{
    const RwRGBA* rw_col = RpMaterialGetColor(material);
    RwRGBA col = sMaterialColor[sMaterialIdx++] = *rw_col;
    F32 tmp;
    F32* mods = (F32*)data;

    tmp = col.red * mods[0];
    U8_COLOR_CLAMP(col.red, tmp);

    tmp = col.green * mods[1];
    U8_COLOR_CLAMP(col.green, tmp);

    tmp = col.blue * mods[2];
    U8_COLOR_CLAMP(col.blue, tmp);

    RpMaterialSetColor(material, &col);

    return material;
}

void iModelMaterialMul(RpAtomic* model, F32 rm, F32 gm, F32 bm)
{
    RpGeometry* geom = RpAtomicGetGeometry(model);
    F32 cols[3];

    if (model != sLastMaterial)
    {
        sMaterialFlags = 0;
    }

    geom->flags |= rpGEOMETRYMODULATEMATERIALCOLOR;

    cols[0] = rm;
    cols[1] = gm;
    cols[2] = bm;

    sMaterialIdx = 0;
    RpGeometryForAllMaterials(geom, iModelMaterialMulCB, cols);

    sMaterialFlags |= 0x2;
    sLastMaterial = model;
}

RwTexCoords* iModelLockAndGetTextureCoords(RpAtomic* pModel, S32* numVertices)
{
    RpGeometry* pGeom = RpAtomicGetGeometry(pModel);
    if (numVertices != NULL)
    {
        *numVertices = pGeom->numVertices;
    }
    RpGeometryLock(pGeom, rpGEOMETRYLOCKTEXCOORDS);
    return pGeom->texCoords[0];
}

void iModelUnlock(RpAtomic* pModel)
{
    RpGeometryUnlock(RpAtomicGetGeometry(pModel));
}

void iModelAnimBoundPrepare(RpAtomic* model)
{
    RpHAnimHierarchy* pHier;
    S32 numbones;
    S32 i;
    S32 j;
    S32 count;
    RpUserDataArray* usrdat;

    pHier = iModelGetHierarchy(model);
    if (pHier == NULL)
    {
        return;
    }

    numbones = pHier->numNodes;

    count = RpGeometryGetUserDataArrayCount(model->geometry);
    for (i = 0; i < count; i++)
    {
        usrdat = RpGeometryGetUserDataArray(model->geometry, i);
        if (*(U32*)usrdat->name == 'HIAB')
        {
            // already prepared: retail (GC and PS2) branches straight to the realignment below;
            // break plus 'if (i == count)' adds a compare and moves usrdat to a saved register
            goto realign;
        }
    }

    {
        RpSkin* skin;
        const U32* boneidx;
        const RwMatrixWeights* bonewt;
        RwV3d* vert;
        S32 numVert;
        S32* cntarr;
        xBox* boxarr;

        skin = RpSkinGeometryGetSkin(model->geometry);
        numVert = model->geometry->numVertices;

        cntarr = (S32*)xMemPushTemp(numbones * sizeof(S32));
        boxarr = (xBox*)xMemPushTemp(numbones * sizeof(xBox));
        memset(cntarr, 0, numbones * sizeof(S32));

        boneidx = RpSkinGetVertexBoneIndices(skin);
        bonewt = RpSkinGetVertexBoneWeights(skin);
        vert = model->geometry->morphTarget->verts;

        for (i = 0; i < numVert; i++)
        {
            const F32* currwt = (const F32*)bonewt;
            U32 curridx = *boneidx;

            for (j = 0; j < 4 && *currwt != 0.0f; j++, currwt++)
            {
                U32 matidx = curridx & 0xFF;

                if (cntarr[matidx] == 0)
                {
                    boxarr[matidx].upper = *(xVec3*)vert;
                    boxarr[matidx].lower = *(xVec3*)vert;
                }
                else
                {
                    boxarr[matidx].upper.x = MAX(boxarr[matidx].upper.x, vert->x);
                    boxarr[matidx].upper.y = MAX(boxarr[matidx].upper.y, vert->y);
                    boxarr[matidx].upper.z = MAX(boxarr[matidx].upper.z, vert->z);
                    boxarr[matidx].lower.x = MIN(boxarr[matidx].lower.x, vert->x);
                    boxarr[matidx].lower.y = MIN(boxarr[matidx].lower.y, vert->y);
                    boxarr[matidx].lower.z = MIN(boxarr[matidx].lower.z, vert->z);
                }

                cntarr[matidx]++;
                curridx >>= 8;
            }

            vert++;
            boneidx++;
            bonewt++;
        }

        xSphere* spharr = (xSphere*)boxarr;
        for (i = 0; i < numbones; i++)
        {
            spharr[i].center.x = 0.5f * (boxarr[i].upper.x + boxarr[i].lower.x);
            spharr[i].center.y = 0.5f * (boxarr[i].upper.y + boxarr[i].lower.y);
            spharr[i].center.z = 0.5f * (boxarr[i].upper.z + boxarr[i].lower.z);
            spharr[i].r = 0.0f;
        }

        boneidx = RpSkinGetVertexBoneIndices(skin);
        bonewt = RpSkinGetVertexBoneWeights(skin);
        vert = model->geometry->morphTarget->verts;

        for (i = 0; i < numVert; i++)
        {
            const F32* currwt = (const F32*)bonewt;
            U32 curridx = *boneidx;

            for (j = 0; j < 4 && *currwt != 0.0f; j++, currwt++)
            {
                F32 dist2 = SQR(vert->x - spharr[curridx & 0xFF].center.x) +
                            SQR(vert->y - spharr[curridx & 0xFF].center.y) +
                            SQR(vert->z - spharr[curridx & 0xFF].center.z);
                spharr[curridx & 0xFF].r = MAX(spharr[curridx & 0xFF].r, dist2);

                curridx >>= 8;
            }

            vert++;
            boneidx++;
            bonewt++;
        }

        S32 numused = 0;
        const RwMatrixTag* skinmat = RpSkinGetSkinToBoneMatrices(skin);
        for (i = 0; i < numbones; i++)
        {
            if (cntarr[i] != 0)
            {
                numused++;
                spharr[i].r = xsqrt(spharr[i].r);
                xMat4x3Toworld(&spharr[i].center, (const xMat4x3*)&skinmat[i], &spharr[i].center);
            }
        }

        // header (2) + bone indices (numused) + spheres (4 * numused) + slack to 16-align the spheres
        U32 AnimBoundUsrDatArray[2] = { 'HIAB', 0 };
        S32 index = RpGeometryAddUserDataArray(model->geometry, (RwChar*)AnimBoundUsrDatArray,
                                               rpINTUSERDATA, numused * 5 + 5);
        usrdat = RpGeometryGetUserDataArray(model->geometry, index);

        S32* idata = (S32*)usrdat->data;
        idata[0] = numused;
        S32* idata_boneidx = idata + 2;
        xSphere* idata_sphere = (xSphere*)(idata_boneidx + numused);
        idata[1] = numused + 2;

        for (i = 0; i < numbones; i++)
        {
            if (cntarr[i] != 0)
            {
                *idata_boneidx = i;
                *idata_sphere = spharr[i];
                idata_boneidx++;
                idata_sphere++;
            }
        }

        xMemPopTemp(boxarr);
        xMemPopTemp(cntarr);
    }

realign:
    S32* idata = (S32*)usrdat->data;
    // idata_boneidx is optimized out of the PS2 DWARF; the Ratatouille GC debug DWARF lists it here
    S32* idata_boneidx = idata + 2;
    S32* idata_sphstart = idata_boneidx + idata[0];
    S32* idata_sphactual = idata + idata[1];
    S32 numel = idata[0] * 4;

    while ((UPtr)idata_sphstart & 0xF)
    {
        idata_sphstart++;
    }

    if (idata_sphstart == idata_sphactual)
    {
        return;
    }

    idata[1] = idata_sphstart - idata;

    if (idata_sphstart > idata_sphactual)
    {
        for (i = numel - 1; i >= 0; i--)
        {
            idata_sphstart[i] = idata_sphactual[i];
        }
    }
    else
    {
        for (i = 0; i < numel; i++)
        {
            idata_sphstart[i] = idata_sphactual[i];
        }
    }
}

void iModelAnimBoundWorld(xBox* box, xBox* combinedBox, RpAtomic* model, RwMatrixTag* mat,
                          RwMatrixTag* bonemat)
{
    S32 i;
    S32* data;
    S32 count;
    RpUserDataArray* usrdat;
    S32 numbones;
    S32* boneidx;
    xSphere* bonesph;
    F32 maxscale;
    F32 scaleX;
    F32 scaleY;
    F32 scaleZ;
    xVec3 sphcenter;
    F32 scalerad;

    count = RpGeometryGetUserDataArrayCount(model->geometry);
    usrdat = NULL;

    for (i = 0; i < count; i++)
    {
        usrdat = RpGeometryGetUserDataArray(model->geometry, i);
        if (*(S32*)usrdat->name == 'HIAB')
        {
            break;
        }
    }

    data = (S32*)usrdat->data;
    numbones = data[0];
    boneidx = data + 2;
    bonesph = (xSphere*)(data + data[1]);

    scaleX =
        mat->right.x * mat->right.x + mat->right.y * mat->right.y + mat->right.z * mat->right.z;
    scaleY = mat->up.x * mat->up.x + mat->up.y * mat->up.y + mat->up.z * mat->up.z;
    scaleZ = mat->at.x * mat->at.x + mat->at.y * mat->at.y + mat->at.z * mat->at.z;

    maxscale = MAX(MAX(scaleX, scaleY), scaleZ);
    maxscale = xsqrt(maxscale);

    box->upper.x = -1e38f;
    box->upper.y = -1e38f;
    box->upper.z = -1e38f;
    box->lower.x = 1e38f;
    box->lower.y = 1e38f;
    box->lower.z = 1e38f;

    for (i = 0; i < numbones; i++)
    {
        scalerad = bonesph->r * maxscale;

        xMat4x3Toworld(&sphcenter, (xMat4x3*)&bonemat[*boneidx], &bonesph->center);
        xMat4x3Toworld(&sphcenter, (xMat4x3*)mat, &sphcenter);

        box->upper.x = MAX(box->upper.x, sphcenter.x + scalerad);
        box->upper.y = MAX(box->upper.y, sphcenter.y + scalerad);
        box->upper.z = MAX(box->upper.z, sphcenter.z + scalerad);
        box->lower.x = MIN(box->lower.x, sphcenter.x - scalerad);
        box->lower.y = MIN(box->lower.y, sphcenter.y - scalerad);
        box->lower.z = MIN(box->lower.z, sphcenter.z - scalerad);

        bonesph++;
        boneidx++;
    }

    if (combinedBox != NULL)
    {
        if (combinedBox->upper.x == -1e38f)
        {
            *combinedBox = *box;
        }
        else
        {
            combinedBox->upper.x = MAX(combinedBox->upper.x, box->upper.x);
            combinedBox->upper.y = MAX(combinedBox->upper.y, box->upper.y);
            combinedBox->upper.z = MAX(combinedBox->upper.z, box->upper.z);
            combinedBox->lower.x = MIN(combinedBox->lower.x, box->lower.x);
            combinedBox->lower.y = MIN(combinedBox->lower.y, box->lower.y);
            combinedBox->lower.z = MIN(combinedBox->lower.z, box->lower.z);
        }
    }
}
