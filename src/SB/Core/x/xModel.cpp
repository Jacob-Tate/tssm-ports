#include "xModel.h"
#include "xDebug.h"
#include "iModel.h"
#include "iCamera.h"
#include "xLightKit.h"
#include "xMathInlines.h"
#include "xMemMgr.h"
#include "xMemory.h"
#include "xMorph.h"
#include "xstransvc.h"
#ifdef PLATFORM_PC
#include "iScreen.h"
#endif

static xModelPool* sxModelPoolList;
static RwCamera* subcamera;
S32 xModelPipeNumTables;
S32 xModelPipeCount[16];
xModelPipeTable* xModelPipeData[16];
S32 xModelLookupCount;
xModelPipeLookup* xModelLookupList;
S32 xModelInstStaticAlloc;

xModelPipe xModelGetPipe(RpAtomic* model)
{
    S32 i;

    for (i = 0; i < xModelLookupCount; i++)
    {
        if (xModelLookupList[i].model == model)
        {
            return xModelLookupList[i].Pipe;
        }
    }

    static xModelPipe zeroPipe = {};
    return zeroPipe;
}

void xModelInit()
{
    iModelInit();
    sxModelPoolList = NULL;
    xModelPipeNumTables = 0;
}

inline void CameraDestroy(RwCamera* camera)
{
    RwRaster* raster;
    RwFrame* frame;

    if (camera != NULL)
    {
        _rwFrameSyncDirty();

        frame = RwCameraGetFrame(camera);
        if (frame != NULL)
        {
            RwCameraSetFrame(camera, NULL);
            _rwFrameSyncDirty();
            RwFrameDestroy(frame);
        }

        raster = RwCameraGetRaster(camera);
        if (raster != NULL)
        {
            RwRasterDestroy(raster);
            RwCameraSetRaster(camera, NULL);
        }

        raster = RwCameraGetZRaster(camera);
        if (raster != NULL)
        {
            RwRasterDestroy(raster);
            RwCameraSetZRaster(camera, NULL);
        }

        RwCameraDestroy(camera);
    }
}

static inline RwCamera* CameraCreate(S32 width, S32 height, S32 zBuffer)
{
    RwCamera* camera = iCameraRwCreate();

    if (camera != NULL)
    {
        RwCameraSetFrame(camera, RwFrameCreate());
        RwCameraSetRaster(camera, RwRasterCreate(width, height, 0, rwRASTERTYPECAMERA));
        if (zBuffer)
        {
            RwCameraSetZRaster(camera, RwRasterCreate(width, height, 0, rwRASTERTYPEZBUFFER));
        }

        if (RwCameraGetFrame(camera) != NULL && RwCameraGetRaster(camera) != NULL &&
            (!zBuffer || RwCameraGetZRaster(camera) != NULL))
        {
            return camera;
        }
    }

    CameraDestroy(camera);
    return NULL;
}

void xModelInitOnce()
{
    if (subcamera == NULL)
    {
        subcamera = CameraCreate(0, 0, 1);
    }
}

void xModelPoolInit(U32 count, U32 numMatrices)
{
    S32 i;
    U8* buffer;
    RwMatrixTag* mat;
    xModelPool* pool;
    xModelPool* curr;
    xModelPool** prev;
    xModelInstance* inst;

    if (numMatrices < 1)
    {
        numMatrices = 1;
    }

    buffer = (U8*)xMemAllocSizeAlign(count * numMatrices * sizeof(RwMatrixTag) +
                                         sizeof(xModelPool) + count * sizeof(xModelInstance),
                                     16);

    mat = (RwMatrixTag*)buffer;
    buffer += count * numMatrices * sizeof(RwMatrixTag);

    pool = (xModelPool*)buffer;
    buffer += sizeof(xModelPool);

    inst = (xModelInstance*)buffer;

    for (i = 0; i < (S32)count; i++)
    {
        inst[i].Next = &inst[i + 1];
        inst[i].Pool = pool;
        inst[i].Mat = mat;
        mat += numMatrices;
    }
    inst[count - 1].Next = NULL;

    pool->NumMatrices = numMatrices;
    pool->List = inst;

    curr = sxModelPoolList;
    prev = &sxModelPoolList;
    while (curr && numMatrices < curr->NumMatrices)
    {
        prev = &curr->Next;
        curr = curr->Next;
    }
    pool->Next = curr;
    *prev = pool;
}

xModelInstance* xModelInstanceAllocReuse(RpAtomic* data, void* object, U16 flags, U8 boneIndex,
                                         U8* boneRemap, xModelInstance* reuse_instance)
{
    S32 i;
    U32 boneCount;
    U32 matCount;
    xModelPool* curr;
    xModelPool* found;
    xModelInstance* dude;
    RwMatrixTag* allocmats;
    bool nosrcblend;
    bool nodestblend;

    found = NULL;
    boneCount = iModelNumBones(data);
    matCount = 1 + boneCount + ((flags >> 6) & 0x1);

    if (reuse_instance)
    {
        dude = reuse_instance;
    }
    else if (xModelInstStaticAlloc)
    {
        if (flags & 0x2000)
        {
            dude = (xModelInstance*)xMemAllocSize(sizeof(xModelInstance));
            dude->Pool = NULL;
            dude->Mat = NULL;
        }
        else
        {
            allocmats = (RwMatrixTag*)xMemAllocSizeAlign(
                matCount * sizeof(RwMatrixTag) + sizeof(xModelInstance), 16);
            dude = (xModelInstance*)(allocmats + matCount);
            dude->Pool = NULL;
            dude->Mat = allocmats;
        }
    }
    else
    {
        if (flags & 0x2000)
        {
            flags &= (U16)~0x2000;
            flags |= 0x8;
        }

        curr = sxModelPoolList;
        while (curr)
        {
            if (curr->List && matCount <= curr->NumMatrices)
            {
                found = curr;
            }
            curr = curr->Next;
        }

        if (!found)
        {
            return NULL;
        }

        dude = found->List;
        found->List = dude->Next;
    }

    dude->Next = NULL;
    dude->Parent = NULL;
    dude->Anim = NULL;
    dude->Data = data;
    dude->Object = object;
    dude->Flags = flags | 0x3;
    dude->BoneCount = boneCount;
    dude->BoneIndex = boneIndex;
    dude->BoneRemap = boneRemap;
    dude->modelID = 0;
    dude->shadowID = 0xDEADBEEF;
    dude->Scale.x = 0.0f;
    dude->Scale.y = 0.0f;
    dude->Scale.z = 0.0f;
    dude->RedMultiplier = 1.0f;
    dude->GreenMultiplier = 1.0f;
    dude->BlueMultiplier = 1.0f;
    dude->Alpha = data->geometry->matList.materials[0]->color.alpha / 255.0f;
    dude->Surf = NULL;
    dude->FadeStart = 9e37f;
    dude->FadeEnd = 1e38f;

    if (dude->Mat)
    {
        for (i = 0; i < (S32)matCount; i++)
        {
            xMat4x3Identity((xMat4x3*)&dude->Mat[i]);
        }
    }

    dude->Bucket = xModelBucket_GetBuckets(dude->Data);
    dude->LightKit = NULL;

    if (dude->Bucket)
    {
        dude->Pipe = dude->Bucket[0]->Pipe;
    }
    else
    {
        dude->Pipe = xModelGetPipe(dude->Data);
    }

    if (dude->Pipe.Layer == 0)
    {
        nosrcblend = ((dude->Pipe.Flags >> 8) & 0xf) == 0;
        nodestblend = ((dude->Pipe.Flags >> 12) & 0xf) == 0;

        if (((dude->Pipe.Flags & 0x300000) || (nosrcblend && nodestblend)) == FALSE)
        {
            dude->Pipe.Layer = 24;
        }
    }

    dude->anim_coll.verts = NULL;
    dude->sortBias = 0;

    return dude;
}

static xModelInstance* FindChild(xModelInstance* model)
{
    xModelInstance* child = model->Next;
    while (child != NULL)
    {
        if (child->Parent == model)
        {
            return child;
        }
        child = child->Next;
    }
    return NULL;
}

// prev is the link node itself, as in BfBB upstream; the PS2 DWARF's
// 'xModelInstance** prev' swaps the curr/prev registers.
void xModelInstanceFree(xModelInstance* modelInst)
{
    xModelInstance* prev;
    xModelInstance* curr;

    while (prev = FindChild(modelInst), prev != NULL)
    {
        xModelInstanceFree(prev);
    }
    prev = modelInst->Parent;
    if (prev != NULL)
    {
        for (curr = prev->Next; (curr != NULL) && (curr != modelInst); curr = curr->Next)
        {
            prev = curr;
        }
        prev->Next = curr->Next;
    }
    if (modelInst->Anim != NULL)
    {
        xAnimPoolFree(modelInst->Anim);
        modelInst->Anim = NULL;
    }
    if (modelInst->Pool != NULL)
    {
        modelInst->Next = modelInst->Pool->List;
        modelInst->Pool->List = modelInst;
    }
}

void xModelInstanceAttach(xModelInstance* inst, xModelInstance* parent)
{
    xModelInstance* curr = parent;
    while (curr->Next != NULL)
    {
        curr = curr->Next;
    }
    curr->Next = inst;
    inst->Parent = parent;
    if ((inst->Flags & 0x2000) != 0)
    {
        inst->Mat = parent->Mat;
    }
}

void xModelInstanceUpgradeBrotherShared(xModelInstance* inst, U32 flags)
{
    if ((inst->Flags & 0x2000) && !(flags & 0x2000))
    {
        U32 boneCount = iModelNumBones(inst->Data);
        RwMatrixTag* allocmats = (RwMatrixTag*)xMemAllocSizeAlign(
            (1 + boneCount + ((flags >> 6) & 0x1)) * sizeof(RwMatrixTag), 16);
        inst->Mat = allocmats;
        inst->Flags = flags;
    }
}

void xModelUpdate(xModelInstance* modelInst, F32 timeDelta)
{
    for (; modelInst != NULL; modelInst = modelInst->Next)
    {
        if (modelInst->Anim != NULL)
        {
            if ((modelInst->Flags & 4) != 0)
            {
                if ((modelInst->Flags & 0x100) != 0)
                {
                    xAnimPlayChooseTransition(modelInst->Anim);
                }
                xAnimPlayUpdate(modelInst->Anim, timeDelta);
            }
        }
    }
}

void xModelEvalSingle(xModelInstance* modelInst, xBox* combinedAnimBound)
{
    S32 i;
    U16 flags = modelInst->Flags;
    if (flags & 0x2)
    {
        modelInst->Flags = flags & (U16)~0x1000;

        if (modelInst->Anim)
        {
            xAnimPlayEval(modelInst->Anim);
        }

        xModelInstance* dad = modelInst->Parent;
        if (dad)
        {
            // The matrices are copied 16 bytes at a time (quadword copies on PS2).
            if (flags & 0x8)
            {
                xMemoryCopyUpA128(modelInst->Mat, dad->Mat,
                                  (modelInst->BoneCount + 1) * sizeof(RwMatrix));
            }
            else if (flags & 0x10)
            {
                U8* remap = modelInst->BoneRemap;
                xMemoryCopyUpA128(modelInst->Mat, dad->Mat, sizeof(RwMatrix));
                for (i = 0; i < modelInst->BoneCount; i++)
                {
                    xMemoryCopyUpA128(&modelInst->Mat[i + 1], &dad->Mat[remap[i] + 1],
                                      sizeof(RwMatrix));
                }
            }
            else if (flags & 0x20)
            {
                if (dad->BoneCount && modelInst->BoneIndex)
                {
                    xMat4x3Mul((xMat4x3*)modelInst->Mat,
                               (xMat4x3*)&dad->Mat[modelInst->BoneIndex + 1], (xMat4x3*)dad->Mat);
                }
                else
                {
                    xMemoryCopyUpA128(modelInst->Mat, dad->Mat, sizeof(RwMatrix));
                }

                if (flags & 0x40)
                {
                    xMat4x3Mul((xMat4x3*)modelInst->Mat,
                               (xMat4x3*)&modelInst->Mat[modelInst->BoneCount + 1],
                               (xMat4x3*)modelInst->Mat);
                }
            }
        }
    }

    if ((flags & 0x1) && modelInst->BoneCount)
    {
        if (modelInst->Scale.x != 0.0f)
        {
            xMat4x3& mat = *(xMat4x3*)modelInst->Mat;
            xMat3x3 temp_mat = mat;

            xMat3x3MulScale(&mat, &mat, &modelInst->Scale);

            iModelAnimBoundWorld(&modelInst->animBound, combinedAnimBound, modelInst->Data,
                                 modelInst->Mat, modelInst->Mat + 1);

            xMat3x3Copy(&mat, &temp_mat);
        }
        else
        {
            iModelAnimBoundWorld(&modelInst->animBound, combinedAnimBound, modelInst->Data,
                                 modelInst->Mat, modelInst->Mat + 1);
        }
    }
}

void xModelEval(xModelInstance* modelInst)
{
    xBox* combinedAnimBound = &modelInst->combinedAnimBound;
    combinedAnimBound->upper.x = -1e38f;

    while (modelInst != NULL)
    {
        xModelEvalSingle(modelInst, combinedAnimBound);
        modelInst = modelInst->Next;
    }
}

void xModelRenderSingle(xModelInstance* modelInst)
{
    if ((modelInst->Flags & 0x401) != 0x1)
    {
        return;
    }

    U8 reset = FALSE;
    xMat3x3 tmpmat;

    if (modelInst->Scale.x)
    {
        tmpmat = *(xMat3x3*)modelInst->Mat;

        modelInst->Mat->right.x *= modelInst->Scale.x;
        modelInst->Mat->right.y *= modelInst->Scale.x;
        modelInst->Mat->right.z *= modelInst->Scale.x;
        modelInst->Mat->up.x *= modelInst->Scale.y;
        modelInst->Mat->up.y *= modelInst->Scale.y;
        modelInst->Mat->up.z *= modelInst->Scale.y;
        modelInst->Mat->at.x *= modelInst->Scale.z;
        modelInst->Mat->at.y *= modelInst->Scale.z;
        modelInst->Mat->at.z *= modelInst->Scale.z;
    }

    if (!xModelCullSingle(modelInst))
    {
        xLightKit_Enable(modelInst->LightKit, xglobals->currWorld);

        if (modelInst->RedMultiplier != 1.0f || modelInst->GreenMultiplier != 1.0f ||
            modelInst->BlueMultiplier != 1.0f)
        {
            reset = TRUE;
            iModelMaterialMul(modelInst->Data, modelInst->RedMultiplier, modelInst->GreenMultiplier,
                              modelInst->BlueMultiplier);
        }

        U8 alpha = modelInst->Alpha * 255.0f;
        if (alpha != 0xFF)
        {
            reset = TRUE;
            iModelSetMaterialAlpha(modelInst->Data, alpha);
        }

        if (modelInst->Flags & 0x80)
        {
            xAnimPlay* a = modelInst->Anim;
            for (U16 i = 0; i < a->NumSingle; i++)
            {
                if (a->Single[i].SingleFlags & 0x8000)
                {
                    if (a->Single[i].State)
                    {
                        xMorphRender((xMorphSeqFile*)a->Single[i].State->Data->RawData[0],
                                     modelInst->Mat,
                                     xAnimFileRawTime(a->Single[i].State->Data, a->Single[i].Time));
                    }
                    break;
                }
            }
        }
        else
        {
            iModelRender(modelInst->Data, modelInst->Mat);
        }
    }

    if (reset)
    {
        iModelResetMaterial(modelInst->Data);
    }

    if (modelInst->Scale.x)
    {
        *(xMat3x3*)modelInst->Mat = tmpmat;
    }
}

void xModelRender(xModelInstance* model)
{
    while (model != NULL)
    {
        if (xModelBucketEnabled)
        {
            xModelBucket_Add(model);
        }
        else
        {
            xModelRenderSingle(model);
        }
        model = model->Next;
    }
}

void xModelRender2D(const xModelInstance& model, const basic_rect<F32>& r, const xVec3& from,
                    const xVec3& to)
{
    if (r.w <= 0.0f || r.h <= 0.0f || r.x + r.w < 0.0f || r.x > 1.0f || r.y + r.h < 0.0f ||
        r.y > 1.0f)
    {
        return;
    }

    RwCamera* camera = RwCameraGetCurrentCamera();
    RwMatrix* cammat = RwFrameGetLTM(RwCameraGetFrame(camera));

    xMat4x3 objmat, shearmat, temp1, temp2;
    xMat3x3LookAt(&objmat, &to, &from);
    xMat3x3Transpose(&objmat, &objmat);
    objmat.pos.x = 0.0f;
    objmat.pos.y = 0.0f;
    objmat.pos.z = xVec3Dist(&to, &from);
    objmat.flags = 0;

    const RwV2d* camvw = RwCameraGetViewWindow(camera);
#ifdef PLATFORM_PC
    // PC: the rect is in the 4:3 UI box, which is this share of a screen
    // camera's view window (iScreen.h), so the model lands on the 2D beside it
    // and keeps its shape. The identity at 4:3.
    RwV2d boxvw;
    RwRaster* raster = RwCameraGetRaster(camera);
    if (raster != NULL && RwRasterGetWidth(raster) == iScreenWidth() &&
        RwRasterGetHeight(raster) == iScreenHeight())
    {
        boxvw.x = camvw->x * iScreenUIFracXF();
        boxvw.y = camvw->y * iScreenUIFracYF();
        camvw = &boxvw;
    }
#endif
    F32 viewscale = camvw->x * r.w * 2.0f;
    F32 shearX = camvw->x * -(r.x * 2.0f + r.w - 1.0f);
    F32 shearY = camvw->y * -(r.y * 2.0f + r.h - 1.0f);
#ifdef PLATFORM_PC
    // PC: a HUD model goes to the screen edge with its widget's 2D (iScreenSetAnchor).
    if (camvw == &boxvw)
    {
        shearX -= camvw->x * 2.0f * iScreenAnchorX(0.0f);
        shearY -= camvw->y * 2.0f * iScreenAnchorY(0.0f);
    }
#endif

    shearmat.right.x = viewscale;
    shearmat.right.y = 0.0f;
    shearmat.right.z = 0.0f;
    shearmat.up.x = 0.0f;
    shearmat.up.y = viewscale;
    shearmat.up.z = 0.0f;
    shearmat.at.x = shearX;
    shearmat.at.y = shearY;
    shearmat.at.z = 1.0f;
    shearmat.pos.x = 0.0f;
    shearmat.pos.y = 0.0f;
    shearmat.pos.z = 0.0f;
    shearmat.flags = 0;

    xMat4x3Mul(&temp1, &objmat, &shearmat);
    xMat4x3Mul(&temp2, &temp1, (xMat4x3*)cammat);
    xMat4x3Mul(&objmat, (xMat4x3*)model.Mat, &temp2);

    xVec3 vscale;
    if (model.Scale.x != 0.0f)
    {
        xVec3Init(&vscale, model.Scale.x, model.Scale.y, model.Scale.z);
    }
    else
    {
        xVec3Init(&vscale, 1.0f, 1.0f, 1.0f);
    }

    xMat4x3 objmat1;
    xMat4x3Scale(&objmat1, &vscale);
    xMat4x3Mul(&objmat1, &objmat1, &objmat);

    *(RwMatrix*)&temp1 = *model.Mat;
    *model.Mat = *(RwMatrix*)&objmat1;

    iModelRender(model.Data, model.Mat);

    *model.Mat = *(RwMatrix*)&temp1;
}

void xModel_SceneEnter(RpWorld* world)
{
    RpWorldAddCamera(world, subcamera);
}

void xModel_SceneExit(RpWorld* world)
{
    RpWorldRemoveCamera(world, subcamera);
    iModelDeInit();
}

void xModelAnimCollStart(xModelInstance& model)
{
    model.Flags = (model.Flags & ~0x1000) | 0x800;
    if (model.anim_coll.verts == NULL)
    {
        U32 size = iModelVertCount(model.Data);
        if (size != 0)
        {
            model.anim_coll.verts = (xVec3*)xMemAlloc(gActiveHeap, size * sizeof(xVec3), 0);
        }
    }
}

void xModelAnimCollRefresh(const xModelInstance& cm)
{
    xModelInstance& m = (xModelInstance&)cm;
    U32 size = iModelVertCount(m.Data);
    xMat4x3& mat = *(xMat4x3*)m.Mat;
    xMat4x3 old_mat = mat;

    mat = g_I3;
    iModelVertEval(m.Data, 0, size, m.Mat, NULL, m.anim_coll.verts);
    mat = old_mat;

    m.Flags |= 0x1000;
}

void xModelGetBoneLocation(xVec3& loc, const xModelInstance& model, size_t index)
{
    xMat4x3& root_mat = *(xMat4x3*)model.Mat;
    xMat4x3& anim_mat = *(xMat4x3*)(model.Mat + index);

    if (index == 0)
    {
        loc = root_mat.pos;
    }
    else
    {
        xMat4x3Toworld(&loc, &root_mat, &anim_mat.pos);
    }
}

void xModelGetBoneMat(xMat4x3& mat, const xModelInstance& model, size_t index)
{
    xMat4x3& root_mat = *(xMat4x3*)model.Mat;

    if (index == 0)
    {
        mat = root_mat;
    }
    else
    {
        xMat4x3Mul(&mat, (xMat4x3*)(model.Mat + index), &root_mat);
    }
}

void xModelGetBoneLocationScaled(xVec3& loc, const xModelInstance& model, size_t index)
{
    xMat4x3 temp_mat;
    const xMat4x3* root_mat = (const xMat4x3*)model.Mat;

    if (model.Scale.x != 0.0f)
    {
        xMat3x3MulScaleC(&temp_mat, root_mat, model.Scale.x, model.Scale.y, model.Scale.z);
        temp_mat.pos = root_mat->pos;
        root_mat = &temp_mat;
    }

    if (index == 0)
    {
        loc = root_mat->pos;
        return;
    }

    const xMat4x3& anim_mat = *(const xMat4x3*)(model.Mat + index);
    xMat4x3Toworld(&loc, root_mat, &anim_mat.pos);
}

S32 xModelSphereCull(xSphere* sphere)
{
    return RwCameraFrustumTestSphere(RwCameraGetCurrentCamera(), (RwSphere*)sphere) ==
           rwSPHEREOUTSIDE;
}

// goto: the PS2 line table puts the shadow test after the "all planes inside" return. The
// shadow dots use RW's dot product on the xVec3, as retail's registers need. The asserts are
// compiled-out placeholders (content unknown) that keep this from being auto-inlined.
S32 xModelSphereCullPlusShadow(xSphere* sphere, xVec3* shadowVec, S32* shadowOutside)
{
    xASSERT_PLACEHOLDER();
    xASSERT_PLACEHOLDER();

    RwSphere* worldsph = (RwSphere*)sphere;
    RwFrustumPlane* frustumPlane = RwCameraGetCurrentCamera()->frustumPlanes;
    S32 numPlanes = 6;

    while (numPlanes--)
    {
        F32 nDot = RwV3dDotProductMacro(&worldsph->center, &frustumPlane->plane.normal);
        nDot -= frustumPlane->plane.distance;

        if (nDot > worldsph->radius)
        {
            goto outside;
        }

        frustumPlane++;
    }

    *shadowOutside = FALSE;
    return FALSE;

outside:

    if (RwV3dDotProductMacro((RwV3d*)shadowVec, &frustumPlane->plane.normal) -
            frustumPlane->plane.distance >
        worldsph->radius)
    {
        *shadowOutside = TRUE;
        return TRUE;
    }

    frustumPlane++;
    while (numPlanes--)
    {
        F32 nDot = RwV3dDotProductMacro(&worldsph->center, &frustumPlane->plane.normal);
        nDot -= frustumPlane->plane.distance;
        F32 sDot = RwV3dDotProductMacro((RwV3d*)shadowVec, &frustumPlane->plane.normal);
        sDot -= frustumPlane->plane.distance;

        if (nDot > worldsph->radius && sDot > worldsph->radius)
        {
            *shadowOutside = TRUE;
            return TRUE;
        }

        frustumPlane++;
    }

    *shadowOutside = FALSE;
    return TRUE;
}

S32 xModelBoxCull(xBox* box)
{
    RwFrustumPlane* frustumPlane;
    S32 numPlanes;
    RwFrustumTestResult result;
    F32 centerX;
    F32 centerY;
    F32 centerZ;
    F32 sizeX;
    F32 sizeY;
    F32 sizeZ;
    F32 centerDot;
    F32 nearDot;

    centerX = 0.5f * (box->upper.x + box->lower.x);
    centerY = 0.5f * (box->upper.y + box->lower.y);
    centerZ = 0.5f * (box->upper.z + box->lower.z);
    sizeX = 0.5f * (box->upper.x - box->lower.x);
    sizeY = 0.5f * (box->upper.y - box->lower.y);
    sizeZ = 0.5f * (box->upper.z - box->lower.z);

    frustumPlane = RwCameraGetCurrentCamera()->frustumPlanes;
    numPlanes = 6;
    result = rwSPHEREINSIDE;

    while (numPlanes--)
    {
        centerDot = centerX * frustumPlane->plane.normal.x +
                    centerY * frustumPlane->plane.normal.y + centerZ * frustumPlane->plane.normal.z;
        centerDot -= frustumPlane->plane.distance;
        nearDot = xabs(sizeX * frustumPlane->plane.normal.x) +
                  xabs(sizeY * frustumPlane->plane.normal.y) +
                  xabs(sizeZ * frustumPlane->plane.normal.z);

        if (centerDot > nearDot)
        {
            result = rwSPHEREOUTSIDE;
            break;
        }

        if (centerDot > -nearDot)
        {
            result = rwSPHEREBOUNDARY;
        }

        frustumPlane++;
    }

    return result == rwSPHEREOUTSIDE;
}

// goto as in xModelSphereCullPlusShadow: the PS2 line table puts the shadow test after the
// "all planes inside" return.
S32 xModelBoxCullPlusShadow(xBox* box, xVec3* shadowVec, S32* shadowOutside)
{
    RwFrustumPlane* frustumPlane;
    S32 numPlanes;
    RwCamera* camera;
    F32 nearDot;
    F32 centerX;
    F32 centerY;
    F32 centerZ;
    F32 sizeX;
    F32 sizeY;
    F32 sizeZ;

    camera = RwCameraGetCurrentCamera();
    if (box->upper.x == -1e38f)
    {
        return TRUE;
    }

    centerX = 0.5f * (box->upper.x + box->lower.x);
    centerY = 0.5f * (box->upper.y + box->lower.y);
    centerZ = 0.5f * (box->upper.z + box->lower.z);
    sizeX = 0.5f * (box->upper.x - box->lower.x);
    sizeY = 0.5f * (box->upper.y - box->lower.y);
    sizeZ = 0.5f * (box->upper.z - box->lower.z);

    frustumPlane = camera->frustumPlanes;
    numPlanes = 6;

    while (numPlanes--)
    {
        F32 centerDot = centerX * frustumPlane->plane.normal.x +
                        centerY * frustumPlane->plane.normal.y +
                        centerZ * frustumPlane->plane.normal.z;
        centerDot -= frustumPlane->plane.distance;
        nearDot = xabs(sizeX * frustumPlane->plane.normal.x) +
                  xabs(sizeY * frustumPlane->plane.normal.y) +
                  xabs(sizeZ * frustumPlane->plane.normal.z);

        if (centerDot > nearDot)
        {
            goto outside;
        }

        frustumPlane++;
    }

    *shadowOutside = FALSE;
    return FALSE;

outside:

    if (shadowVec->x * frustumPlane->plane.normal.x + shadowVec->y * frustumPlane->plane.normal.y +
            shadowVec->z * frustumPlane->plane.normal.z - frustumPlane->plane.distance >
        nearDot)
    {
        *shadowOutside = TRUE;
        return TRUE;
    }

    frustumPlane++;
    while (numPlanes--)
    {
        F32 centerDot = centerX * frustumPlane->plane.normal.x +
                        centerY * frustumPlane->plane.normal.y +
                        centerZ * frustumPlane->plane.normal.z;
        centerDot -= frustumPlane->plane.distance;
        F32 shadowDot = shadowVec->x * frustumPlane->plane.normal.x +
                        shadowVec->y * frustumPlane->plane.normal.y +
                        shadowVec->z * frustumPlane->plane.normal.z;
        shadowDot -= frustumPlane->plane.distance;
        nearDot = xabs(sizeX * frustumPlane->plane.normal.x) +
                  xabs(sizeY * frustumPlane->plane.normal.y) +
                  xabs(sizeZ * frustumPlane->plane.normal.z);

        if (centerDot > nearDot && shadowDot > nearDot)
        {
            *shadowOutside = TRUE;
            return TRUE;
        }

        frustumPlane++;
    }

    *shadowOutside = FALSE;
    return TRUE;
}

S32 xModelCullGroup(xModelInstance* minst)
{
    if (minst->BoneCount != 0)
    {
        return xModelBoxCull(&minst->combinedAnimBound);
    }

    return xModelSphereCull(iModelWorldBoundSphere(minst->Data, minst->Mat, NULL));
}

S32 xModelBoxCullPlusShadow(xBox* box, xVec3* shadowVec, S32* shadowOutside);

S32 xModelCullGroupPlusShadow(xModelInstance* minst, F32 shadowDepth, S32* shadowOutside)
{
    xVec3 shadVec;

    if (minst->BoneCount != 0)
    {
        xBox* bbox = &minst->combinedAnimBound;
        shadVec.x = 0.5f * (bbox->upper.x + bbox->lower.x);
        shadVec.y =
            0.5f * (bbox->upper.x - bbox->lower.x) + xModelGetFrame(minst)->pos.y - shadowDepth;
        shadVec.z = 0.5f * (bbox->upper.z + bbox->lower.z);
        return xModelBoxCullPlusShadow(bbox, &shadVec, shadowOutside);
    }

    shadVec.x = xModelGetFrame(minst)->pos.x;
    shadVec.y = xModelGetFrame(minst)->pos.y - 10.0f;
    shadVec.z = xModelGetFrame(minst)->pos.z;
    return xModelSphereCullPlusShadow(iModelWorldBoundSphere(minst->Data, minst->Mat, NULL),
                                      &shadVec, shadowOutside);
}

S32 xModelCullSingle(xModelInstance* minst)
{
    S32 result;

    if (minst->BoneCount != 0)
    {
        result = xModelBoxCull(&minst->animBound);
        if (!result)
        {
            iModelSetWorldBoundFromBox(minst->Data, &minst->animBound);
        }
        return result;
    }

    return xModelSphereCull(iModelWorldBoundSphere(minst->Data, minst->Mat, NULL));
}

void xModelSetLightKit(xModelInstance* minst, xLightKit* lightKit)
{
    while (minst != NULL)
    {
        if ((minst->Pipe.Flags & 0xC0) != 0x40)
        {
            minst->LightKit = lightKit;
        }
        else
        {
            minst->LightKit = NULL;
        }
        minst = minst->Next;
    }
}

bool xModelIsPreinstanced(xModelInstance* model)
{
    RpGeometry* geom = RpAtomicGetGeometry(model->Data);
    if (geom == NULL)
    {
        return true;
    }

    RpMorphTarget* mt = geom->morphTarget;
    return mt == NULL || mt->verts == NULL;
}

namespace
{
    RpAtomic* find_first_atomic(xModelAssetInfo* minf)
    {
        xModelAssetInst* inst = (xModelAssetInst*)(minf + 1);
        xModelAssetInst* end_inst = inst + minf->NumModelInst;

        for (; inst != end_inst; inst++)
        {
            if (inst->ModelID == 0)
            {
                continue;
            }

            void* data = xSTFindAsset(inst->ModelID, NULL);
            if (data == NULL)
            {
                continue;
            }

            if (*(U32*)data == 'FNIM')
            {
                RpAtomic* child_atomic = find_first_atomic((xModelAssetInfo*)data);
                if (child_atomic != NULL)
                {
                    return child_atomic;
                }
            }
            else
            {
                return (RpAtomic*)data;
            }
        }

        return NULL;
    }
} // namespace

RpAtomic* xModelFindAtomic(U32 id, U32* actual_id)
{
    RpAtomic* atomic = NULL;
    U32 aid = id;

    if (id != 0)
    {
        atomic = (RpAtomic*)xSTFindAsset(id, NULL);
        if (atomic == NULL)
        {
            aid = xStrHashCat(id, ".dff");
            atomic = (RpAtomic*)xSTFindAsset(aid, NULL);
            if (atomic == NULL)
            {
                aid = xStrHashCat(id, ".minf");
                xModelAssetInfo* minf = (xModelAssetInfo*)xSTFindAsset(aid, NULL);
                if (minf != NULL)
                {
                    atomic = find_first_atomic(minf);
                }
            }
        }
    }

    if (actual_id != NULL)
    {
        *actual_id = atomic == NULL ? id : aid;
    }

    return atomic;
}
