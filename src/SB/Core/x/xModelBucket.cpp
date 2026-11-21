#include "xModelBucket.h"
#include "xModel.h"
#include "iModel.h"
#include "iCamera.h"
#include "iDraw.h"
#include "xEnt.h"
#include "xFXHighDynamicRange.h"
#include "xMemMgr.h"
#include "xRenderState.h"
#include "xGlobals.h"
#include "xCam.h"
#include "xCamera.h"
#include "xutil.h"
#include <rpskin.h>
#include <rpmatfx.h>
#include <stdlib.h>

S32 xModelBucketEnabled;
static RpWorld* sBucketDummyWorld;
static RwCamera* sBucketDummyCamera;
static S32 sBucketCurr;
static S32 sBucketCount;
static xModelBucket* sBucketList;
static S32 sBucketClipCullCurr;
static S32 sBucketClipCullCount;
static xModelBucket** sBucketClipCullList;
static S32 sAlphaCurr;
static S32 sAlphaStart;
static S32 sAlphaCount;
static xModelAlphaBucket* sAlphaList;
static S32 sBucketOpaqueRenderStart;

namespace
{
    S32 compare_opaque_buckets(const void* e1, const void* e2);

    struct CmpAlphaBucket
    {
        S32 operator()(const void* _a, const void* _b) const
        {
            const xModelAlphaBucket& a = *(const xModelAlphaBucket*)_a;
            const xModelAlphaBucket& b = *(const xModelAlphaBucket*)_b;

            if (a.Layer != b.Layer)
            {
                return a.Layer - b.Layer;
            }
            if (a.SortValue < b.SortValue)
            {
                return 1;
            }
            if (a.SortValue > b.SortValue)
            {
                return -1;
            }
            return 0;
        }
    };
} // namespace

static void xModelBucket_RenderAlphaLayerInternal(S32 maxLayer);

namespace
{
    S32 compare_opaque_buckets(const void* e1, const void* e2)
    {
        const xModelBucket& b1 = *(const xModelBucket*)e1;
        const xModelBucket& b2 = *(const xModelBucket*)e2;
        S32 use_layer1 = b1.Pipe.Flags & 0x2000000;
        S32 use_layer2 = b2.Pipe.Flags & 0x2000000;

        if (use_layer1 != use_layer2)
        {
            return use_layer1 - use_layer2;
        }
        if (b1.Pipe.Layer != b2.Pipe.Layer)
        {
            return b1.Pipe.Layer - b2.Pipe.Layer;
        }
        if (b1.Pipe.Flags != b2.Pipe.Flags)
        {
            return b1.Pipe.Flags - b2.Pipe.Flags;
        }
        if (b1.Pipe.AlphaDiscard != b2.Pipe.AlphaDiscard)
        {
            return b1.Pipe.AlphaDiscard - b2.Pipe.AlphaDiscard;
        }
        if (b1.OriginalData != b2.OriginalData)
        {
#ifdef PLATFORM_PC
            // PLATFORM_PC: the console's order of the atomics' addresses, which its RenderWare
            // heap hands out from the top down, comes from the order they were created in
            // (iModelOrder.h); the host's heap has no such order.
            return iModelCreationOrderCompare(b1.OriginalData, b2.OriginalData);
#else
            return (U32)(UPtr)b1.OriginalData - (U32)(UPtr)b2.OriginalData;
#endif
        }
        return b2.ClipFlags - b1.ClipFlags;
    }
} // namespace

void xModelBucket_PreCountReset()
{
    sBucketCurr = 0;
    sBucketCount = 0;
    sBucketList = NULL;
    sBucketClipCullCurr = 0;
    sBucketClipCullCount = 0;
    sBucketClipCullList = NULL;
    sAlphaCurr = 0;
    sAlphaCount = 0;
    sAlphaList = NULL;
    sAlphaStart = 0;
}

void xModelBucket_PreCountBucket(RpAtomic* data, xModelPipe& pipe, U32 subObjects)
{
    S32 numbuckets;
    S32 modelcount;

    switch (((pipe.Flags & ~0x7) | 0x2) & 0x7)
    {
    case 1:
        numbuckets = 1;
        break;
    case 2:
        numbuckets = 1;
        break;
    case 3:
    case 4:
        numbuckets = 2;
        break;
    case 0:
    default:
        if (iModelNumBones(data))
        {
            numbuckets = 1;
        }
        else
        {
            numbuckets = 2;
        }
        break;
    }

    modelcount = 0;
    do
    {
        if (subObjects & 0x1)
        {
            modelcount++;
        }
        subObjects >>= 1;
    } while ((data = iModelFile_RWMultiAtomic(data)));

    sBucketCount += modelcount * numbuckets;
    sBucketClipCullCount += modelcount * 2;
}

void xModelBucket_PreCountAlloc(S32 maxAlphaModels)
{
    sAlphaCount = maxAlphaModels;
    sBucketList = (xModelBucket*)xMemAlloc(gActiveHeap, sBucketCount * sizeof(xModelBucket), 0);
    sBucketClipCullList =
        (xModelBucket**)xMemAlloc(gActiveHeap, sBucketClipCullCount * sizeof(xModelBucket*), 0);
    sAlphaList =
        (xModelAlphaBucket*)xMemAlloc(gActiveHeap, sAlphaCount * sizeof(xModelAlphaBucket), 0);

    RwBBox bbox;
    bbox.sup.x = 100000.0f;
    bbox.sup.y = 100000.0f;
    bbox.sup.z = 100000.0f;
    bbox.inf.x = -100000.0f;
    bbox.inf.y = -100000.0f;
    bbox.inf.z = -100000.0f;

    sBucketDummyWorld = RpWorldCreate(&bbox);
    sBucketDummyCamera = iCameraCreate(0, 0, 0);
    RpWorldAddCamera(sBucketDummyWorld, sBucketDummyCamera);
}

static RpAtomic* GimmeAtomicCallback(RpAtomic* atomic, void* tmp)
{
    *(RpAtomic**)tmp = atomic;
    return atomic;
}

static RpAtomic* GimmeAtomicCallback(RpAtomic* atomic, void* tmp);

void FullAtomicDupe(RpAtomic* atomic, S32 count, RpAtomic** output)
{
    S32 i;
    RwStream* stream;
    RwMemory rwmemA = { NULL, 0 };
    RpAtomic* tempAtom;
    RpClump* clump;

    stream = RwStreamOpen(rwSTREAMMEMORY, rwSTREAMWRITE, &rwmemA);
    if (stream)
    {
        clump = RpAtomicGetClump(atomic);

        RwLinkList backupClump_atomicList = clump->atomicList;
        RwLLLink backupAtomic_inClumpLink = atomic->inClumpLink;

        clump->atomicList.link.next = &atomic->inClumpLink;
        clump->atomicList.link.prev = &atomic->inClumpLink;
        atomic->inClumpLink.next = &clump->atomicList.link;
        atomic->inClumpLink.prev = &clump->atomicList.link;

        RpClumpStreamWrite(clump, stream);

        clump->atomicList = backupClump_atomicList;
        atomic->inClumpLink = backupAtomic_inClumpLink;

        RwStreamClose(stream, &rwmemA);
    }

    for (i = 0; i < count; i++)
    {
        stream = RwStreamOpen(rwSTREAMMEMORY, rwSTREAMREAD, &rwmemA);
        RwStreamFindChunk(stream, rwID_CLUMP, NULL, NULL);
        RpClump* tempClump = RpClumpStreamRead(stream);
        RwStreamClose(stream, &rwmemA);
        RpClumpForAllAtomics(tempClump, GimmeAtomicCallback, &tempAtom);

        RwMemory rwmemB = { NULL, 0 };
        stream = RwStreamOpen(rwSTREAMMEMORY, rwSTREAMWRITE, &rwmemB);
        if (stream)
        {
            RpClumpStreamWrite(tempClump, stream);
            RwStreamClose(stream, &rwmemB);
        }

        stream = RwStreamOpen(rwSTREAMMEMORY, rwSTREAMREAD, &rwmemB);
        if (stream)
        {
            RwStreamFindChunk(stream, rwID_CLUMP, NULL, NULL);
            RpClump* outClump = RpClumpStreamRead(stream);
            RpClumpForAllAtomics(outClump, GimmeAtomicCallback, &output[i]);
            RwStreamClose(stream, &rwmemB);
        }
        else
        {
            output[i] = NULL;
        }

        RwFree(rwmemB.start);

        _rwFrameSyncDirty();
        iModelUnload(tempAtom);

        if (output[i])
        {
            RpHAnimHierarchy* pHier = iModelGetHierarchy(output[i]);
            RpSkin* pSkin = RpSkinGeometryGetSkin(RpAtomicGetGeometry(output[i]));
            if (pHier && pSkin)
            {
                RpSkinAtomicSetHAnimHierarchy(output[i], pHier);
            }
        }
    }

    RwFree(rwmemA.start);
}

static inline void xModelBucket_SetMaterialFX(RpAtomic* model, xModelPipe& pipe)
{
    RpGeometry* geom;
    U8 skinned;
    S32 numMaterials;
    S32 i;
    RpMaterial* material;

    if (pipe.Flags & 0x1800000)
    {
        for (; model; model = iModelFile_RWMultiAtomic(model))
        {
            RpMatFXAtomicEnableEffects(model);

            geom = model->geometry;
            skinned = RpSkinGeometryGetSkin(geom) != NULL;
            if (skinned)
            {
                RpSkinAtomicSetType(model, rpSKINTYPEMATFX);
            }

            numMaterials = geom->matList.numMaterials;
            for (i = 0; i < numMaterials; i++)
            {
                material = geom->matList.materials[i];
                if (pipe.Flags & 0x1000000)
                {
                    RpMatFXMaterialSetEffects(material, rpMATFXEFFECTDUALUVTRANSFORM);
                }
                else
                {
                    RpMatFXMaterialSetEffects(material, rpMATFXEFFECTUVTRANSFORM);
                }
            }
        }
    }
}

void xModelBucket_InsertBucket(RpAtomic* data, xModelPipe& pipe, U32 subObjects)
{
    U32 pipeFlags;
    S32 numbuckets;
    S32 pipeSetting[2];
    RpAtomic* extraData;

    xModelBucket_SetMaterialFX(data, pipe);

    pipeFlags = (pipe.Flags & ~0x7) | 0x2;

    switch (pipeFlags & 0x7)
    {
    case 1:
        numbuckets = 1;
        pipeSetting[0] = 1;
        break;
    case 2:
        numbuckets = 1;
        pipeSetting[0] = 2;
        break;
    case 3:
    case 4:
        numbuckets = 2;
        pipeSetting[0] = 2;
        pipeSetting[1] = 1;
        break;
    case 0:
    default:
        if (iModelNumBones(data))
        {
            numbuckets = 1;
            pipeSetting[0] = 1;
        }
        else
        {
            numbuckets = 2;
            pipeSetting[0] = 2;
            pipeSetting[1] = 1;
        }
        break;
    }

    extraData = NULL;

    do
    {
        if (subObjects & 0x1)
        {
            sBucketClipCullList[sBucketClipCullCurr] = &sBucketList[sBucketCurr];
            sBucketClipCullList[sBucketClipCullCurr + 1] = &sBucketList[sBucketCurr];
            sBucketList[sBucketCurr].Data = data;
            sBucketList[sBucketCurr].OriginalData = data;
            sBucketList[sBucketCurr].BackRef = &sBucketClipCullList[sBucketClipCullCurr + 1];
            sBucketList[sBucketCurr].ClipFlags = pipeSetting[0];
            sBucketList[sBucketCurr].Pipe = pipe;
            if (numbuckets == 2)
            {
                FullAtomicDupe(data, 1, &extraData);
                sBucketList[sBucketCurr + 1].Data = extraData;
                sBucketList[sBucketCurr + 1].OriginalData = data;
                sBucketList[sBucketCurr + 1].BackRef = &sBucketClipCullList[sBucketClipCullCurr];
                sBucketList[sBucketCurr + 1].ClipFlags = pipeSetting[1];
                sBucketList[sBucketCurr + 1].Pipe = pipe;
                sBucketClipCullList[sBucketClipCullCurr] = &sBucketList[sBucketCurr + 1];
                extraData = NULL;
            }
            sBucketCurr += numbuckets;
            sBucketClipCullCurr += 2;
        }
        subObjects >>= 1;
    } while ((data = iModelFile_RWMultiAtomic(data)));
}

void xModelBucket_Init()
{
    S32 i;
    xModelBucket* bucket;
    xModelBucket** backref;

    if (sBucketDummyCamera)
    {
        RpWorldRemoveCamera(sBucketDummyWorld, sBucketDummyCamera);
        iCameraDestroy(sBucketDummyCamera);
        RpWorldDestroy(sBucketDummyWorld);
        sBucketDummyCamera = NULL;
        sBucketDummyWorld = NULL;
    }

    qsort(sBucketList, sBucketCount, sizeof(xModelBucket), compare_opaque_buckets);

    for (i = 0; i < sBucketCount; i++)
    {
        bucket = &sBucketList[i];
        backref = bucket->BackRef;
        if (backref > sBucketClipCullList && backref[0] == backref[-1])
        {
            backref[-1] = bucket;
        }
        backref[0] = bucket;
        bucket->List = NULL;
    }
}

xModelBucket** xModelBucket_GetBuckets(RpAtomic* data)
{
    S32 i;

    for (i = 0; i < sBucketClipCullCount; i += 2)
    {
        if (sBucketClipCullList[i]->OriginalData == data)
        {
            return &sBucketClipCullList[i];
        }
    }
    return NULL;
}

void xModelBucket_Begin()
{
    S32 i;

    for (i = 0; i < sBucketCount; i++)
    {
        sBucketList[i].List = NULL;
    }
    xModelBucketEnabled = 1;
}

void xModelBucket_Add(xModelInstance* minst)
{
    RpAtomic* model;
    xMat4x3* cammat;
    F32 camdist2;
    xModelBucket* bucket;
    F32 camdot;
    F32 alphaFade;
    bool alphaFlags;
    bool alphaProg;

    if ((minst->Flags & 0x401) != 0x1)
    {
        return;
    }

    model = minst->Data;
    if (xModelCullSingle(minst))
    {
        return;
    }

    cammat = &xglobals->cam->mat;
    camdist2 = SQR(model->worldBoundingSphere.center.x - cammat->pos.x) +
               SQR(model->worldBoundingSphere.center.y - cammat->pos.y) +
               SQR(model->worldBoundingSphere.center.z - cammat->pos.z);
    if (camdist2 >= SQR(minst->FadeEnd))
    {
        return;
    }

    camdot = cammat->at.x * (model->worldBoundingSphere.center.x - cammat->pos.x) +
             cammat->at.y * (model->worldBoundingSphere.center.y - cammat->pos.y) +
             cammat->at.z * (model->worldBoundingSphere.center.z - cammat->pos.z);
    if (camdot < 1.5f * model->worldBoundingSphere.radius)
    {
        bucket = minst->Bucket[1];
    }
    else
    {
        bucket = minst->Bucket[0];
    }

    alphaFade = 1.0f;
    if (camdist2 > SQR(minst->FadeStart))
    {
        alphaFade = (minst->FadeEnd - xsqrt(camdist2)) / (minst->FadeEnd - minst->FadeStart);
        if (alphaFade <= 0.0f)
        {
            return;
        }
        alphaFade = MIN(alphaFade, 1.0f);
    }

    alphaFlags = (minst->Pipe.Flags & 0xFF00) != 0;
    alphaProg = (alphaFade != 1.0f || minst->Alpha != 1.0f);

    if (minst->Pipe.Flags & 0x2000000)
    {
        alphaProg = false;
        alphaFlags = false;
    }
    else if ((minst->Pipe.Flags & 0x300000) == 0x100000)
    {
        alphaFlags = false;
    }
    else if ((minst->Pipe.Flags & 0x300000) == 0x200000)
    {
        alphaProg = false;
        alphaFlags = false;
    }

    if (alphaFlags || alphaProg)
    {
        if (sAlphaCurr < sAlphaCount)
        {
            sAlphaList[sAlphaCurr].Data = bucket->Data;
            sAlphaList[sAlphaCurr].MInst = minst;
            sAlphaList[sAlphaCurr].AlphaFade = alphaFade;
            sAlphaList[sAlphaCurr].SortValue = camdot;
            sAlphaList[sAlphaCurr].SortValue += minst->sortBias / 32.0f;
            sAlphaList[sAlphaCurr].Layer = minst->Pipe.Layer;
            sAlphaCurr++;
        }
    }
    else
    {
        minst->BucketNext = bucket->List;
        bucket->List = minst;
    }
}

void xModelBucket_AddAlphaFade(xModelInstance* minst, F32 alphaFade)
{
    RpAtomic* model;
    xMat4x3* cammat;
    xModelBucket* bucket;
    F32 camdot;
    bool alphaFlags;
    bool alphaProg;

    if (alphaFade <= 0.0f)
    {
        return;
    }

    if ((minst->Flags & 0x401) != 0x1)
    {
        return;
    }

    model = minst->Data;
    if (xModelCullSingle(minst))
    {
        return;
    }

    cammat = &xglobals->cam->mat;
    if (SQR(model->worldBoundingSphere.center.x - cammat->pos.x) +
            SQR(model->worldBoundingSphere.center.y - cammat->pos.y) +
            SQR(model->worldBoundingSphere.center.z - cammat->pos.z) >=
        SQR(minst->FadeEnd))
    {
        return;
    }

    camdot = cammat->at.x * (model->worldBoundingSphere.center.x - cammat->pos.x) +
             cammat->at.y * (model->worldBoundingSphere.center.y - cammat->pos.y) +
             cammat->at.z * (model->worldBoundingSphere.center.z - cammat->pos.z);
    if (camdot < 1.5f * model->worldBoundingSphere.radius)
    {
        bucket = minst->Bucket[1];
    }
    else
    {
        bucket = minst->Bucket[0];
    }

    alphaFlags = (minst->Pipe.Flags & 0xFF00) != 0;
    alphaProg = (alphaFade != 1.0f || minst->Alpha != 1.0f);

    if (minst->Pipe.Flags & 0x2000000)
    {
        alphaProg = false;
        alphaFlags = false;
    }
    else if ((minst->Pipe.Flags & 0x300000) == 0x100000)
    {
        alphaFlags = false;
    }
    else if ((minst->Pipe.Flags & 0x300000) == 0x200000)
    {
        alphaProg = false;
        alphaFlags = false;
    }

    if (alphaFlags || alphaProg)
    {
        if (sAlphaCurr < sAlphaCount)
        {
            sAlphaList[sAlphaCurr].Data = bucket->Data;
            sAlphaList[sAlphaCurr].MInst = minst;
            sAlphaList[sAlphaCurr].AlphaFade = alphaFade;
            sAlphaList[sAlphaCurr].SortValue = camdot;
            sAlphaList[sAlphaCurr].SortValue += minst->sortBias / 32.0f;
            sAlphaList[sAlphaCurr].Layer = minst->Pipe.Layer;
            sAlphaCurr++;
        }
    }
    else
    {
        minst->BucketNext = bucket->List;
        bucket->List = minst;
    }
}

void xModelBucket_RenderOpaqueBegin()
{
    sBucketOpaqueRenderStart = 0;
}

void xModelBucket_RenderOpaque(S32 maxLayer)
{
    S32 i;
    xModelBucket* bucket;
    RpAtomic* data;
    xModelInstance* minst;
    S32 flags;
    RpAtomic* oldmodel;

    xModelBucketEnabled = 0;

    for (i = sBucketOpaqueRenderStart; i < sBucketCount; i++)
    {
        bucket = &sBucketList[i];
        data = bucket->Data;
        minst = bucket->List;

        if ((bucket->Pipe.Flags & 0x2000000) && bucket->Pipe.Layer > maxLayer)
        {
            break;
        }

        while (minst)
        {
            if (minst->Surf)
            {
                xEntSetupPipeline(minst->Surf, data);
            }

            oldmodel = minst->Data;
            minst->Data = data;

            flags = minst->Pipe.Flags;

            if ((flags & 0x30) == 0x20)
            {
                RwRenderStateSet(rwRENDERSTATECULLMODE, (void*)rwCULLMODECULLBACK);
            }
            if (flags & 0x8)
            {
                iCameraSetFog(false);
            }
            if ((flags & 0x30000) == 0x10000)
            {
                RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)FALSE);
            }
            if (flags & 0xF00)
            {
                RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)((flags >> 8) & 0xF));
            }
            if (flags & 0xF000)
            {
                RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)((flags >> 12) & 0xF));
            }
            if (flags & 0x400000)
            {
                if (HDR_brightening != TRUE)
                {
                    HDR_brightening = TRUE;
                    iDrawSetFBMSK(0x00000000, 0xFFFFFFFF, true);
                }
            }

            xModelRenderSingle(minst);

            if (flags & 0x8)
            {
                iCameraSetFog(true);
            }
            if ((flags & 0x30) == 0x20)
            {
                RwRenderStateSet(rwRENDERSTATECULLMODE, (void*)rwCULLMODECULLNONE);
            }
            if ((flags & 0x30000) == 0x10000)
            {
                RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)TRUE);
            }
            if (flags & 0xF00)
            {
                RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)rwBLENDSRCALPHA);
            }
            if (flags & 0xF000)
            {
                RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDINVSRCALPHA);
            }
            if (flags & 0x400000)
            {
                if (HDR_brightening != FALSE)
                {
                    HDR_brightening = FALSE;
                    iDrawSetFBMSK(0xFF000000, 0xFFFFFFFF, true);
                }
            }

            minst->Data = oldmodel;
            minst = minst->BucketNext;
        }

        bucket->List = NULL;
    }

    sBucketOpaqueRenderStart = i;
}

void xModelBucket_RenderAlphaBegin()
{
    xModelBucketEnabled = 0;
    sAlphaStart = 0;
    if (sAlphaCurr)
    {
        quicksort(sAlphaList, sAlphaCurr, sizeof(xModelAlphaBucket), CmpAlphaBucket());
    }
}

static void xModelBucket_RenderAlphaLayerInternal(S32 maxLayer)
{
    U32 pipeMask;
    U32 curPipeFlags;
    U32 lastPipeFlags;
    U8 curAlphaDiscard;
    RwFogType oldfogtype;
    xModelInstance* minst;
    RpAtomic* oldmodel;
    F32 oldAlpha;
    F32 fade;
    U32 xorPipeFlags;

    pipeMask = 0xFFF3FF38;
    lastPipeFlags = 0x6500;
    oldfogtype = xglobals->fog.type;
    if (oldfogtype == rwFOGTYPENAFOGTYPE)
    {
        pipeMask &= ~0x8;
    }

    while (sAlphaStart < sAlphaCurr)
    {
        if ((S32)sAlphaList[sAlphaStart].Layer > maxLayer)
        {
            break;
        }

        minst = sAlphaList[sAlphaStart].MInst;
        oldmodel = minst->Data;
        minst->Data = sAlphaList[sAlphaStart].Data;

        if (minst->Surf)
        {
            xEntSetupPipeline(minst->Surf, minst->Data);
        }

        curPipeFlags = minst->Pipe.Flags;
        if (!(curPipeFlags & 0xF00))
        {
            curPipeFlags |= 0x500;
        }
        if (!(curPipeFlags & 0xF000))
        {
            curPipeFlags |= 0x6000;
        }
        curPipeFlags &= pipeMask;

        xorPipeFlags = curPipeFlags ^ lastPipeFlags;
        fade = sAlphaList[sAlphaStart].AlphaFade;
        curAlphaDiscard = minst->Pipe.AlphaDiscard;

        oldAlpha = minst->Alpha;
        minst->Alpha *= fade;

        if (xorPipeFlags)
        {
            if (xorPipeFlags & 0xF00)
            {
                RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)((curPipeFlags >> 8) & 0xF));
            }
            if (xorPipeFlags & 0xF000)
            {
                RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)((curPipeFlags >> 12) & 0xF));
            }
            if (xorPipeFlags & 0x30000)
            {
                if ((curPipeFlags & 0x30000) == 0x10000)
                {
                    RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)FALSE);
                }
                else
                {
                    RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)TRUE);
                }
            }
            if (xorPipeFlags & 0x30)
            {
                if ((curPipeFlags & 0x30) == 0x20)
                {
                    RwRenderStateSet(rwRENDERSTATECULLMODE, (void*)rwCULLMODECULLBACK);
                }
                else if ((curPipeFlags & 0x30) <= 0x10)
                {
                    RwRenderStateSet(rwRENDERSTATECULLMODE, (void*)rwCULLMODECULLNONE);
                }
            }
            if (xorPipeFlags & 0x8)
            {
                if (curPipeFlags & 0x8)
                {
                    iCameraSetFog(false);
                }
                else
                {
                    iCameraSetFog(true);
                }
            }
            if (xorPipeFlags & 0x400000)
            {
                if (HDR_brightening != ((curPipeFlags & 0x400000) != 0))
                {
                    HDR_brightening = (curPipeFlags & 0x400000) != 0;
                    iDrawSetFBMSK((curPipeFlags & 0x400000) ? 0x00000000 : 0xFF000000, 0xFFFFFFFF,
                                  true);
                }
            }
            lastPipeFlags = curPipeFlags;
        }

        xRenderStateSetAlphaDiscard(curAlphaDiscard);

        if (curPipeFlags & 0x400000)
        {
            xRenderFixUntexturedBegin(minst->Data);
        }

        if ((curPipeFlags & 0x30) == 0x30)
        {
            RwRenderStateSet(rwRENDERSTATECULLMODE, (void*)rwCULLMODECULLFRONT);
            xModelRenderSingle(minst);
            RwRenderStateSet(rwRENDERSTATECULLMODE, (void*)rwCULLMODECULLBACK);
            xModelRenderSingle(minst);
        }
        else if ((curPipeFlags & 0x30000) == 0x20000)
        {
            iDrawSetFBMSK(0xFFFFFFFF, 0x00FFFFFF, true);
            xModelRenderSingle(minst);
            iDrawSetFBMSK(0x00000000, 0x00FFFFFF, true);
            xModelRenderSingle(minst);
        }
        else
        {
            xModelRenderSingle(minst);
        }

        if (curPipeFlags & 0x400000)
        {
            xRenderFixUntexturedEnd(minst->Data);
        }

        xRenderStateResetAlphaDiscard();

        minst->Alpha = oldAlpha;
        if (minst->Surf)
        {
            xEntRestorePipeline(minst->Surf, minst->Data);
        }
        minst->Data = oldmodel;

        sAlphaStart++;
    }

    if (lastPipeFlags != 0x6500)
    {
        if (((lastPipeFlags >> 8) & 0xF) != rwBLENDSRCALPHA)
        {
            RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)rwBLENDSRCALPHA);
        }
        if (((lastPipeFlags >> 12) & 0xF) != rwBLENDINVSRCALPHA)
        {
            RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDINVSRCALPHA);
        }
        if ((lastPipeFlags & 0x30000) == 0x10000)
        {
            RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)TRUE);
        }
        if ((lastPipeFlags & 0x30) == 0x20 || (lastPipeFlags & 0x30) == 0x30)
        {
            RwRenderStateSet(rwRENDERSTATECULLMODE, (void*)rwCULLMODECULLNONE);
        }
        if (lastPipeFlags & 0x8)
        {
            iCameraSetFog(true);
        }
        if (lastPipeFlags & 0x400000)
        {
            if (HDR_brightening != FALSE)
            {
                HDR_brightening = FALSE;
                iDrawSetFBMSK(0xFF000000, 0xFFFFFFFF, true);
            }
        }
    }
}

void xModelBucket_RenderAlphaLayer(S32 maxLayer)
{
    S32 lastLayer = 0;
    S32 layer;

    while (sAlphaStart < sAlphaCurr)
    {
        layer = sAlphaList[sAlphaStart].Layer;
        if (layer > maxLayer)
        {
            layer = maxLayer;
        }

        if (layer == lastLayer && maxLayer != 36)
        {
            break;
        }

        xModelBucket_RenderOpaque(layer);
        xModelBucket_RenderAlphaLayerInternal(layer);
        lastLayer = layer;
    }

    if (lastLayer < maxLayer)
    {
        xModelBucket_RenderOpaque(maxLayer);
    }
}

bool xModelBucket_RenderAny(S32 maxLayer)
{
    if (sAlphaStart < sAlphaCurr && (S32)sAlphaList[sAlphaStart].Layer <= maxLayer)
    {
        return true;
    }
    return sBucketOpaqueRenderStart < sBucketCount &&
           sBucketList[sBucketOpaqueRenderStart].Pipe.Layer <= maxLayer;
}

void xModelBucket_RenderAlphaEnd()
{
    sAlphaCurr = 0;
}

void xModelBucket_Deinit()
{
    S32 i;

    for (i = 0; i < sBucketCount; i++)
    {
        if (sBucketList[i].Data != sBucketList[i].OriginalData)
        {
            _rwFrameSyncDirty();
            iModelUnload(sBucketList[i].Data);
        }
    }

    xModelBucket_PreCountReset();
}
