#include "xCutscene.h"
#include "xCamera.h"
#include "xCam.h"
#include "xModel.h"
#include "xModelBucket.h"
#include "xMorph.h"
#include "xShadow.h"
#include "xSubTitles.h"
#include "xLightKit.h"
#include "xstransvc.h"
#include "iModel.h"
#include "iMorph.h"
#include "iParMgr.h"
#include "zCam.h"
#include "zCamera.h"

extern xCutscene sActiveCutscene;
static U32 sCutTocCount;
static xCutsceneInfo* sCutTocInfo;
static xModelInstance sCutsceneFakeModel[8];
static xCutsceneModelHack* sCutsceneModelHackBuf;
extern U32 gSoak;

struct JDeltaRun
{
    S32 VertIdx;
    S32 VertCt;
};

struct JDeltaTarget
{
    S32 skipSize;
    S32 numRuns;
    S32 numVerts;
    F32 scale;
    JDeltaRun runs[1];
};

// Chunk headers with no HI DWARF entry (MWCC emits none for a type used only in casts); the
// layouts are the ones the asm reads. Named after xCutsceneAnimHdr.
struct xCutsceneTocHdr // 'CTOC' asset: the count, then that many xCutsceneInfo
{
    U32 Count;
};

struct xCutsceneCamHdr // XCUTSCENEDATA_TYPE_CAMERA chunk
{
    U32 NumKeys;
    zFlyKey Keys[1];
};

struct xCutsceneMphHdr // XCUTSCENEDATA_TYPE_MORPHTARGET chunk
{
    U32 NumFrame;
    U32 NumRun;
    xCutsceneMphFrame Frames[1];
};

namespace
{
    // Retail's __vt__xCamCutscene is weak and follows __vt__10xGoalEmpty in .data: update is
    // declared first and defined 'inline', so the class has no out-of-line key function. The
    // vtable emitted before it is the dead-stripped xCamDebug's (see xCam.cpp).
    struct xCamCutscene : xCam
    {
        xCutscene* csn;
        xMat4x3 original_mat;

        void update(xScene& scene, F32 dt);
        void create();
        void start();
        void stop();
    };

    xCamCutscene* cutscene_cam;
} // namespace

namespace
{

    inline void xCamCutscene::update(xScene& scene, F32 dt)
    {
        xCutsceneData* data;
        U32 i;
        U32 dataIndex;
        xMat3x3 tmpMat;
        xQuat quats[2];
        F32 invlerp;
        F32 lerp;
        S32 frame;
        U32 count;
        zFlyKey* keys;

        data = (xCutsceneData*)(csn->Play + 1);

        for (i = 0; i < csn->Play->NumData; i++)
        {
            if (data->DataType == XCUTSCENEDATA_TYPE_CAMERA)
            {
                frame = std::floorf(30.0f * csn->CamTime);
                count = ((xCutsceneCamHdr*)(data + 1))->NumKeys;
                keys = ((xCutsceneCamHdr*)(data + 1))->Keys;

                if (frame < keys[0].frame)
                {
                    lerp = 0.0f;
                }
                else if (frame >= keys[count - 1].frame)
                {
                    lerp = 1.0f;
                    keys += count - 2;
                }
                else
                {
                    lerp = 30.0f * csn->CamTime - std::floorf(30.0f * csn->CamTime);
                    keys += frame - keys[0].frame;
                }

                invlerp = 1.0f - lerp;

                for (dataIndex = 0; dataIndex < 2; dataIndex++)
                {
                    tmpMat.right.x = -keys[dataIndex].matrix[0];
                    tmpMat.right.y = -keys[dataIndex].matrix[1];
                    tmpMat.right.z = -keys[dataIndex].matrix[2];
                    tmpMat.up.x = keys[dataIndex].matrix[3];
                    tmpMat.up.y = keys[dataIndex].matrix[4];
                    tmpMat.up.z = keys[dataIndex].matrix[5];
                    tmpMat.at.x = -keys[dataIndex].matrix[6];
                    tmpMat.at.y = -keys[dataIndex].matrix[7];
                    tmpMat.at.z = -keys[dataIndex].matrix[8];
                    xQuatFromMat(&quats[dataIndex], &tmpMat);
                }

                xQuatSlerp(&orient.quat, &quats[0], &quats[1], lerp);
                xQuatToMat(&orient.quat, &mat);

                mat.pos.x = keys[0].matrix[9] * invlerp + keys[1].matrix[9] * lerp;
                mat.pos.y = keys[0].matrix[10] * invlerp + keys[1].matrix[10] * lerp;
                mat.pos.z = keys[0].matrix[11] * invlerp + keys[1].matrix[11] * lerp;

                fov = 2.0f *
                      xatan2(12.7f * (keys[0].aperture[0] * invlerp + keys[1].aperture[0] * lerp),
                             keys[0].focal * invlerp + keys[1].focal * lerp);
            }

            data = (xCutsceneData*)((U8*)data + sizeof(xCutsceneData) +
                                    ((data->ChunkSize + 15) & ~15));
        }

        coord.cart = mat.pos;
    }

    void xCamCutscene::stop()
    {
        xCam::stop();
        mat = original_mat;
    }

    void xCamCutscene::start()
    {
        xCam::start();
        original_mat = mat;
        coord.cart = mat.pos;
        xQuatFromMat(&orient.quat, &mat);
    }

} // namespace

// namespace

void xCutscene_Init(void* toc)
{
    S32 i;
    S32 maxModels;
    S32 tocnum;
    xCutsceneInfo* cnfo;

    memset(&sActiveCutscene, 0, sizeof(xCutscene));
    sCutTocCount = 0;
    sCutTocInfo = NULL;

    if (toc != NULL)
    {
        sCutTocCount = ((xCutsceneTocHdr*)toc)->Count;
        sCutTocInfo = (xCutsceneInfo*)((xCutsceneTocHdr*)toc + 1);
    }

    for (i = 0; i < 8; i++)
    {
        sCutsceneFakeModel[i].Mat = (RwMatrix*)xMemAlloc(gActiveHeap, sizeof(RwMatrixTag) * 65, 0);
        sCutsceneFakeModel[i].Bucket =
            (xModelBucket**)xMemAlloc(gActiveHeap, sizeof(xModelBucket*) * 2, 0);
        sCutsceneFakeModel[i].Bucket[0] =
            (xModelBucket*)xMemAlloc(gActiveHeap, sizeof(xModelBucket), 0);
        sCutsceneFakeModel[i].Bucket[1] = sCutsceneFakeModel[i].Bucket[0];
        sCutsceneFakeModel[i].RedMultiplier = 1.0f;
        sCutsceneFakeModel[i].GreenMultiplier = 1.0f;
        sCutsceneFakeModel[i].BlueMultiplier = 1.0f;
        sCutsceneFakeModel[i].Alpha = 1.0f;
    }

    maxModels = 0;
    cnfo = sCutTocInfo;
    for (tocnum = 0; tocnum < sCutTocCount; tocnum++)
    {
        maxModels = MAX(maxModels, (S32)cnfo->NumData);
        cnfo = (xCutsceneInfo*)((U8*)cnfo + cnfo->HeaderSize);
    }

    if (maxModels != 0)
    {
        sCutsceneModelHackBuf =
            (xCutsceneModelHack*)xMemAlloc(gActiveHeap, maxModels * sizeof(xCutsceneModelHack), 0);
    }

    cutscene_cam = new (xMemAlloc(gActiveHeap, sizeof(xCamCutscene), 0)) xCamCutscene;
    cutscene_cam->create();
}

xCutscene* xCutscene_Create(U32 csnID, U32 subtitlesID)
{
    xCutscene* csn;
    S32 tocnum;
    xCutsceneInfo* cnfo;
    U32 maxload;

    xSndMgrPauseSounds(-2, TRUE, TRUE);

    csn = &sActiveCutscene;
    memset(csn, 0, sizeof(xCutscene));
    sActiveCutscene.PlaybackSpeed = 1.0f;

    cnfo = sCutTocInfo;
    for (tocnum = 0; tocnum < sCutTocCount; tocnum++)
    {
        if (cnfo->AssetID == csnID)
        {
            maxload = cnfo->MaxBufEven + cnfo->MaxBufOdd;
            if (cnfo->MaxModel > maxload)
            {
                maxload = cnfo->MaxModel;
            }

            csn->RawBuf = RwMalloc(maxload + 0x3c, 0);
            csn->AlignBuf = csn->RawBuf;
            while ((UPtr)csn->AlignBuf & 0x3f)
            {
                csn->AlignBuf = (void*)((UPtr)csn->AlignBuf + 4);
            }

            csn->Info = cnfo;
            csn->SubTitlesID = subtitlesID;
            csn->Data = (xCutsceneData*)(cnfo + 1);
            csn->TimeChunkOffs = (U32*)(csn->Data + cnfo->NumData);
            csn->Visibility = csn->TimeChunkOffs + (cnfo->NumTime + 1);
            csn->BreakList = (xCutsceneBreak*)(csn->Visibility + cnfo->VisSize);
            csn->Play = (xCutsceneTime*)csn->AlignBuf;
            csn->Stream = (xCutsceneTime*)((U8*)csn->AlignBuf + cnfo->MaxBufEven);
            csn->started = FALSE;
            csn->ModelHack = sCutsceneModelHackBuf;
            memset(sCutsceneModelHackBuf, 0, cnfo->NumData * sizeof(xCutsceneModelHack));

            return csn;
        }

        cnfo = (xCutsceneInfo*)((U8*)cnfo + cnfo->HeaderSize);
    }

    return NULL;
}

S32 xCutscene_Destroy(xCutscene* csn)
{
    U32 i;

    if (csn->started)
    {
        csn->started = FALSE;
        cutscene_cam->csn = NULL;
        zCamRemove(*cutscene_cam, true);
    }

    csn->Ready = 0;
    iSndSetExternalCallback(NULL);

    if (csn->SndStarted)
    {
        xSndMgrStop(csn->SndHandle[0]);
        if (csn->SndNumChannel == 2)
        {
            xSndMgrStop(csn->SndHandle[1]);
        }
        csn->SndStarted = 0;
    }

    xSndMgrPauseSounds(-2, false, true);
    xSndMgrUpdate();

    if (csn->Opened)
    {
        iCSFileClose(csn);
    }

    for (i = 0; i < csn->Info->NumData; i++)
    {
        if ((csn->Data[i].DataType & 0x80000000) && csn->Data[i].DataPtr != NULL)
        {
            if ((csn->Data[i].DataType & 0xfffffff) == XCUTSCENEDATA_TYPE_JDELTAMODEL)
            {
                RwFree(csn->Data[i].DataPtr);
            }
            else
            {
                iModelUnload((RpAtomic*)csn->Data[i].DataPtr);
            }

            csn->Data[i].DataType &= 0xfffffff;
        }
    }

    RwFree(csn->RawBuf);
    memset(csn, 0, sizeof(xCutscene));
    return 1;
}

S32 xCutscene_LoadStart(xCutscene* csn)
{
    if (iCSFileOpen(csn) <= 0)
    {
        return 0;
    }

    csn->Opened = 1;
    return 1;
}

// No GC symbol: always inlined into xCutscene_Update.
inline F32 xCutsceneConvertBreak(F32 time, xCutsceneBreak* breaklist, U32 breakcount, S32 idx)
{
    U32 i;

    for (i = 0; i < breakcount; i++)
    {
        if (idx == breaklist[i].Index && breaklist[i].Time - time > 0.0f &&
            breaklist[i].Time - time < (1.0f / 30.0f))
        {
            return breaklist[i].Time - (1.0f / 30.0f);
        }
    }

    return time;
}

S32 xCutscene_Update(xCutscene* csn, F32 dt)
{
    xCutsceneTime* oldChunk;

    if (csn->Ready && !csn->started)
    {
        csn->started = TRUE;
        cutscene_cam->csn = csn;
        zCamAdd(*cutscene_cam, true);
    }

    if (!csn->SndStarted && csn->SndNumChannel)
    {
        xSndMgrStartStereo(csn->SndHandle[0], csn->SndHandle[1]);
        csn->SndStarted = TRUE;
    }

    csn->Time += csn->PlaybackSpeed * dt;

    if (gSoak && csn->Time > 2.0f)
    {
        csn->PlayIndex = csn->Info->NumTime - 1;
    }

    csn->CamTime = xCutsceneConvertBreak(csn->Time, csn->BreakList, csn->Info->BreakCount, -1);

    if (csn->Time > csn->Play->EndTime || csn->BadReadPause)
    {
        if (csn->PlayIndex == csn->Info->NumTime - 1)
        {
            csn->Time = csn->Play->EndTime;
            return 0;
        }

        if (csn->BadReadPause && !csn->Waiting)
        {
            csn->BadReadPause = FALSE;
            xCutscene_SetSpeed(csn, csn->BadReadSpeed);
        }

        if (csn->CamTime != csn->Time)
        {
            return 1;
        }

        if (csn->Waiting)
        {
            csn->Time = csn->Play->EndTime;
            csn->CamTime =
                xCutsceneConvertBreak(csn->Time, csn->BreakList, csn->Info->BreakCount, -1);

            if (!csn->BadReadPause)
            {
                csn->BadReadSpeed = csn->PlaybackSpeed;
                xCutscene_SetSpeed(csn, 0.0f);
                csn->BadReadPause = TRUE;
            }

            return 1;
        }

        oldChunk = csn->Play;
        csn->Play = csn->Stream;
        csn->Stream = oldChunk;
        csn->PlayIndex++;

        if (csn->PlayIndex + 1 < csn->Info->NumTime)
        {
            iCSFileAsyncRead(csn, csn->Stream,
                             csn->TimeChunkOffs[csn->PlayIndex + 2] -
                                 csn->TimeChunkOffs[csn->PlayIndex + 1]);
        }
    }

    return 1;
}

// No GC symbol: a plain global function, auto-inlined into xCutscene_Update and dead-stripped.
// Compiled out of line here, it interns 4.0f and 0.001f at retail's .sdata2 position.
void xCutscene_SetSpeed(xCutscene* csn, F32 speed)
{
    if (csn->BadReadPause)
    {
        return;
    }

    if (speed > 4.0f)
    {
        speed = 4.0f;
    }

    if (speed < 0.001f)
    {
        speed = 0.0f;
    }

    csn->PlaybackSpeed = speed;
}

// Dead-stripped in retail; restored for the literals retail's .sdata2 has here. Name and signature
// from Ratatouille's erased DWARF; the body is BfBB's xCutscene_SetCamera in TSSM's form (the
// output matrix and the xCamScreen fov call are inferred).
static void xCutscene_SetCameraMatrix(xCutscene* csn, xMat4x3* mat)
{
    xCutsceneData* data;
    U32 i;
    U32 dataIndex;
    F32 camFOV;
    xMat4x3 camMat;
    xMat3x3 tmpMat;
    xQuat quats[2];
    xQuat qresult;
    F32 invlerp;
    F32 lerp;
    F32 tmpXap;
    F32 tmpFoc;
    S32 frame;
    U32 count;
    zFlyKey* keys;

    data = (xCutsceneData*)(csn->Play + 1);

    for (i = 0; i < csn->Play->NumData; i++)
    {
        if (data->DataType == XCUTSCENEDATA_TYPE_CAMERA)
        {
            frame = std::floorf(30.0f * csn->CamTime);
            count = ((xCutsceneCamHdr*)(data + 1))->NumKeys;
            keys = ((xCutsceneCamHdr*)(data + 1))->Keys;

            if (frame < keys[0].frame)
            {
                lerp = 0.0f;
            }
            else if (frame >= keys[count - 1].frame)
            {
                lerp = 1.0f;
                keys += count - 2;
            }
            else
            {
                lerp = 30.0f * csn->CamTime - std::floorf(30.0f * csn->CamTime);
                keys += frame - keys[0].frame;
            }

            invlerp = 1.0f - lerp;

            for (dataIndex = 0; dataIndex < 2; dataIndex++)
            {
                tmpMat.right.x = -keys[dataIndex].matrix[0];
                tmpMat.right.y = -keys[dataIndex].matrix[1];
                tmpMat.right.z = -keys[dataIndex].matrix[2];
                tmpMat.up.x = keys[dataIndex].matrix[3];
                tmpMat.up.y = keys[dataIndex].matrix[4];
                tmpMat.up.z = keys[dataIndex].matrix[5];
                tmpMat.at.x = -keys[dataIndex].matrix[6];
                tmpMat.at.y = -keys[dataIndex].matrix[7];
                tmpMat.at.z = -keys[dataIndex].matrix[8];
                xQuatFromMat(&quats[dataIndex], &tmpMat);
            }

            xQuatSlerp(&qresult, &quats[0], &quats[1], lerp);
            xQuatToMat(&qresult, &camMat);

            camMat.pos.x = keys[0].matrix[9] * invlerp + keys[1].matrix[9] * lerp;
            camMat.pos.y = keys[0].matrix[10] * invlerp + keys[1].matrix[10] * lerp;
            camMat.pos.z = keys[0].matrix[11] * invlerp + keys[1].matrix[11] * lerp;

            tmpXap = keys[0].aperture[0] * invlerp + keys[1].aperture[0] * lerp;
            tmpFoc = keys[0].focal * invlerp + keys[1].focal * lerp;
            camFOV = 114.59155f * atanf(12.7f * tmpXap / tmpFoc);

            *mat = camMat;
            xglobals->screen->set_fov((PI / 180.0f) * camFOV);
        }

        data = (xCutsceneData*)((U8*)data + sizeof(xCutsceneData) + ((data->ChunkSize + 15) & ~15));
    }
}

static void xcsCalcAnimMatrices(RwMatrixTag* animMat, RpAtomic* model, xCutsceneAnimHdr* ahdr,
                                F32 time, U32 tworoot)
{
    xQuat quatresult[65];
    xVec3 tranresult[65];
    void* afile;
    xMat4x3 m1;
    xMat4x3 m2;
    U32 numbone;
    U32 boneidx;
    xQuat* qqq;
    xVec3* ttt;

    afile = ahdr + 1;
    iAnimEval(afile, time, 0, tranresult, quatresult);

    if (iModelNumBones(model) != 0)
    {
        xMat4x3Identity((xMat4x3*)animMat);
        animMat->pos.x = ahdr->Translate[0];
        animMat->pos.y = ahdr->Translate[1];
        animMat->pos.z = ahdr->Translate[2];

        if (tworoot)
        {
            quatresult[1].s = -quatresult[1].s;
            quatresult[2].s = -quatresult[2].s;
            xQuatToMat(&quatresult[1], &m1);
            xQuatToMat(&quatresult[2], &m2);

            m1.pos = tranresult[1];
            m2.pos = tranresult[2];

            xMat4x3Mul(&m1, &m2, &m1);

            tranresult[2].x = 0.0f;
            tranresult[2].y = 0.0f;
            tranresult[2].z = 0.0f;
            quatresult[2].v.x = 0.0f;
            quatresult[2].v.y = 0.0f;
            quatresult[2].v.z = 0.0f;
            quatresult[2].s = 1.0f;

            xQuatFromMat(&quatresult[1], &m1);

            quatresult[1].s = -quatresult[1].s;
            tranresult[1] = m1.pos;
        }

        numbone = iModelNumBones(model);
        boneidx = 0;
        qqq = quatresult;
        ttt = tranresult;
        while (boneidx < numbone && boneidx <= ahdr->RootIndex)
        {
            animMat->pos.x += ttt->x;
            animMat->pos.y += ttt->y;
            animMat->pos.z += ttt->z;

            ttt->x = 0.0f;
            ttt->y = 0.0f;
            ttt->z = 0.0f;

            if (xabs(qqq->s) < 0.9999f)
            {
                break;
            }

            boneidx++;
            qqq++;
            ttt++;
        }

        iModelAnimMatrices(model, quatresult, tranresult, animMat + 1);
    }
    else
    {
        xQuatToMat(quatresult, (xMat4x3*)animMat);
        animMat->pos.x = tranresult[0].x + ahdr->Translate[0];
        animMat->pos.y = tranresult[0].y + ahdr->Translate[1];
        animMat->pos.z = tranresult[0].z + ahdr->Translate[2];
    }
}

static void JDeltaEval(RpAtomic* model, void* deltaModel, void* deltaAnim, F32 time)
{
    F32 outweight[128];
    F32* currweight;
    S32 i;
    S32 numFrames;
    S32 numWeights;
    F32* times;
    F32* weights;
    F32 lerp;
    F32 invlerp;
    RwV3d* outverts;
    S32 numRun;
    JDeltaTarget* dtgt;

    // Both buffers are indexed raw ('JDTA' and 'JDTM' headers); no header type is known.
    numFrames = ((S32*)deltaAnim)[1];
    numWeights = ((S32*)deltaAnim)[2];
    times = (F32*)deltaAnim + 3;
    weights = times + numFrames;

    if (time <= times[0] || numFrames == 1)
    {
        memcpy(outweight, weights, numWeights * sizeof(F32));
    }
    else if (time >= times[numFrames - 1])
    {
        memcpy(outweight, weights + (numFrames - 1) * numWeights, numWeights * sizeof(F32));
    }
    else
    {
        while (time > times[1])
        {
            weights += numWeights;
            times++;
        }

        lerp = (time - times[0]) / (times[1] - times[0]);
        invlerp = 1.0f - lerp;

        for (i = 0; i < numWeights; i++)
        {
            outweight[i] = invlerp * weights[i] + lerp * weights[i + numWeights];
        }
    }

    RpGeometryLock(model->geometry, rpGEOMETRYLOCKVERTICES);

    dtgt = (JDeltaTarget*)((U8*)deltaModel + 8); // after 'JDTM' and the target count
    outverts = model->geometry->morphTarget->verts;
    numRun = dtgt->numRuns;

    if (dtgt->scale)
    {
        F32 scale = dtgt->scale;
        S16* svert =
            (S16*)((U8*)deltaModel + ((((U8*)&dtgt->runs[numRun] - (U8*)deltaModel) + 15) & ~15));

        for (i = 0; i < numRun; i++)
        {
            S32 j = dtgt->runs[i].VertIdx;
            S32 cmpval = j + dtgt->runs[i].VertCt;

            for (; j < cmpval; j++)
            {
                outverts[j].x = scale * svert[0];
                outverts[j].y = scale * svert[1];
                outverts[j].z = scale * svert[2];
                svert += 3;
            }
        }
    }
    else
    {
        RwV3d* vert =
            (RwV3d*)((U8*)deltaModel + ((((U8*)&dtgt->runs[numRun] - (U8*)deltaModel) + 15) & ~15));

        for (i = 0; i < numRun; i++)
        {
            S32 j = dtgt->runs[i].VertIdx;
            S32 cmpval = j + dtgt->runs[i].VertCt;

            for (; j < cmpval; j++)
            {
                outverts[j] = *vert;
                vert++;
            }
        }
    }

    currweight = outweight;
    dtgt = (JDeltaTarget*)((U8*)dtgt + dtgt->skipSize);

    while (numWeights)
    {
        numRun = dtgt->numRuns;

        if (*currweight)
        {
            if (dtgt->scale)
            {
                F32 scale = dtgt->scale * *currweight;
                S16* svert = (S16*)((U8*)deltaModel +
                                    ((((U8*)&dtgt->runs[numRun] - (U8*)deltaModel) + 15) & ~15));

                for (i = 0; i < numRun; i++)
                {
                    S32 j = dtgt->runs[i].VertIdx;
                    S32 cmpval = j + dtgt->runs[i].VertCt;

                    for (; j < cmpval; j++)
                    {
                        outverts[j].x += scale * svert[0];
                        outverts[j].y += scale * svert[1];
                        outverts[j].z += scale * svert[2];
                        svert += 3;
                    }
                }
            }
            else
            {
                F32 scale = *currweight;
                RwV3d* vert = (RwV3d*)((U8*)deltaModel +
                                       ((((U8*)&dtgt->runs[numRun] - (U8*)deltaModel) + 15) & ~15));

                for (i = 0; i < numRun; i++)
                {
                    S32 j = dtgt->runs[i].VertIdx;
                    S32 cmpval = j + dtgt->runs[i].VertCt;

                    for (; j < cmpval; j++)
                    {
                        outverts[j].x += scale * vert->x;
                        outverts[j].y += scale * vert->y;
                        outverts[j].z += scale * vert->z;
                        vert++;
                    }
                }
            }
        }

        currweight++;
        numWeights--;
        dtgt = (JDeltaTarget*)((U8*)dtgt + dtgt->skipSize);
    }

    RpGeometryUnlock(model->geometry);
}

static void CutsceneShadowRender(CutsceneShadowModel* smod)
{
    RpAtomic* model = smod->model;
    U32 bits = smod->shadowBits;

    while (model != NULL)
    {
        if (bits & 1)
        {
            iModelRender(model, smod->animMat);
        }
        model = iModelFile_RWMultiAtomic(model);
        bits >>= 1;
    }
}

void xCutscene_Render(xCutscene* csn, xEnt**, S32*, F32*)
{
    U32 i;
    U32 dataIndex;
    U32 animIndex;
    U32 mphIndex;
    U32 visFlags;
    U32 visIdx;
    U32 fakeCount;
    U32 tworoot;
    U32 noshadow;
    xCutsceneData* data;
    xCutsceneData* mphdata;
    RpAtomic* model;
    RpAtomic* shadowModel;
    RwMatrixTag animMat[65];
    xVec3* camVec;
    xModelPipe* pipeCurr;
    XCSNNosey* nosey;

    fakeCount = 0;
    visFlags = 0xffffffff;
    camVec = (xVec3*)&RwFrameGetMatrix(RwCameraGetFrame(RwCameraGetCurrentCamera()))->pos;
    nosey = csn->cb_nosey;
    data = (xCutsceneData*)(csn->Play + 1);
    animIndex = 0;

    for (i = 0; i < csn->Play->NumData; i++)
    {
        if (data->DataType == XCUTSCENEDATA_TYPE_ANIMATION)
        {
            U32 tempSize;
            F32 breakTime = xCutsceneConvertBreak(csn->CamTime, csn->BreakList,
                                                  csn->Info->BreakCount, animIndex);

            model = NULL;
            tworoot = 0;
            noshadow = 0;
            pipeCurr = NULL;

            for (dataIndex = 0; dataIndex < csn->Info->NumData; dataIndex++)
            {
                if ((csn->Data[dataIndex].DataType & 0xfffffff) == XCUTSCENEDATA_TYPE_RW_MODEL &&
                    data->AssetID == csn->Data[dataIndex].AssetID)
                {
                    model = (RpAtomic*)csn->Data[dataIndex].DataPtr;
                    tworoot = csn->Data[dataIndex].DataType & 0x20000000;
                    noshadow = csn->Data[dataIndex].DataType & 0x10000000;
                    pipeCurr = csn->ModelHack[dataIndex].pipe;
                    break;
                }
            }

            if (model == NULL)
            {
                model = (RpAtomic*)xSTFindAsset(data->AssetID, &tempSize);
            }

            if (model != NULL)
            {
                shadowModel = model;
                visFlags = 0xffffffff;

                if (csn->Info->VisCount)
                {
                    U32 viscnt;
                    U32* currvis;
                    U32 subIndex;
                    U32 frameMin;
                    U32 frameMax;
                    U32 frameIndex;

                    viscnt = csn->Info->VisCount;
                    currvis = csn->Visibility;

                    while (viscnt)
                    {
                        subIndex = currvis[0] >> 16;
                        frameMin = currvis[1] & 0xffff;
                        frameMax = currvis[1] >> 16;

                        if ((currvis[0] & 0xffff) == animIndex)
                        {
                            frameIndex = 30.0f * breakTime;
                            if (frameIndex > frameMax - frameMin)
                            {
                                frameIndex = frameMax - frameMin;
                            }

                            if (!((currvis[(frameIndex >> 5) + 2] >> (frameIndex & 31)) & 1))
                            {
                                visFlags &= ~(1 << subIndex);
                            }
                        }

                        currvis += ((frameMax - frameMin) >> 5) + 3;
                        viscnt--;
                    }
                }

                xcsCalcAnimMatrices(animMat, model, (xCutsceneAnimHdr*)(data + 1),
                                    breakTime - csn->Play->StartTime, tworoot);

                if (nosey != NULL && (nosey->flg_nosey & 2))
                {
                    nosey->UpdatedAnimated(model, animMat, animIndex, i, visFlags);
                }

                U32 shadowBits = 0;
                xBox combinedAnimBound;
                visIdx = 0;
                combinedAnimBound.upper.x = -1e38f;

                while (model != NULL)
                {
                    if (visFlags & (1 << visIdx))
                    {
                        mphdata = (xCutsceneData*)(csn->Play + 1);

                        for (mphIndex = 0; mphIndex < csn->Play->NumData; mphIndex++)
                        {
                            U32 morphAnimIndex = mphdata->AssetID & 0xffff;
                            U32 morphModelIndex = mphdata->AssetID >> 16;

                            if ((mphdata->DataType == XCUTSCENEDATA_TYPE_MORPHTARGET ||
                                 mphdata->DataType == XCUTSCENEDATA_TYPE_JDELTAANIM) &&
                                morphAnimIndex == animIndex && morphModelIndex == visIdx)
                            {
                                if (mphdata->DataType == XCUTSCENEDATA_TYPE_MORPHTARGET)
                                {
                                    U32 numFrame = ((xCutsceneMphHdr*)(mphdata + 1))->NumFrame;
                                    U32 numRun = ((xCutsceneMphHdr*)(mphdata + 1))->NumRun;
                                    xCutsceneMphFrame* mphFrame =
                                        ((xCutsceneMphHdr*)(mphdata + 1))->Frames;
                                    xCutsceneMphRun* mphRun =
                                        (xCutsceneMphRun*)(mphFrame + numFrame);
                                    // In words, the header's 2 included: byte-sized and
                                    // pointer-aligned forms of this rounding change the codegen.
                                    xMorphTargetFile* mphFile =
                                        (xMorphTargetFile*)((U32*)(mphdata + 1) +
                                                            ((2 + numFrame * 2 + numRun * 2 + 3) &
                                                             ~3));
                                    S16* v_array[4] = { NULL, NULL, NULL, NULL };
                                    S16 weight[4] = { 0, 0, 0, 0 };
                                    U32 skipsize;
                                    xVec3* csnTmpArray;
                                    xVec3* currtmp;
                                    xVec3* outv;
                                    U32 j;
                                    U32 cmpval;
                                    U32 frameIndex;

                                    for (frameIndex = 1; frameIndex < numFrame - 1; frameIndex++)
                                    {
                                        if (mphFrame[frameIndex].Time >= breakTime)
                                        {
                                            break;
                                        }
                                    }

                                    skipsize = (mphFile->NumVerts * 3 + 7) & ~7;
                                    if (mphFile->Flags & 1)
                                    {
                                        skipsize <<= 1;
                                    }

                                    v_array[0] = (S16*)(mphFile + 1) + skipsize * (frameIndex - 1);
                                    v_array[1] = (S16*)(mphFile + 1) + skipsize * frameIndex;

                                    weight[1] =
                                        16384.0f * (breakTime - mphFrame[frameIndex - 1].Time) /
                                        (mphFrame[frameIndex].Time - mphFrame[frameIndex - 1].Time);
                                    if (weight[1] > 0x4000)
                                    {
                                        weight[1] = 0x4000;
                                    }
                                    weight[0] = 0x4000 - weight[1];

                                    csnTmpArray = (xVec3*)&gRenderArr;
                                    FastS16weight2((F32*)csnTmpArray, v_array, weight,
                                                   mphFile->NumVerts * 3,
                                                   (1.0f / 16384.0f) * mphFile->Scale);

                                    RpGeometryLock(model->geometry, rpGEOMETRYLOCKVERTICES);

                                    currtmp = csnTmpArray;
                                    outv = (xVec3*)model->geometry->morphTarget->verts;

                                    for (dataIndex = 0; dataIndex < numRun; dataIndex++)
                                    {
                                        j = mphRun[dataIndex].VertIdx;
                                        cmpval = j + mphRun[dataIndex].VertCt;

                                        for (; j < cmpval; j++)
                                        {
                                            outv[j] = *currtmp;
                                            currtmp++;
                                        }
                                    }

                                    RpGeometryUnlock(model->geometry);
                                }
                                else
                                {
                                    void* deltaAnim = mphdata + 1;
                                    void* deltaModel = NULL;

                                    for (dataIndex = 0; dataIndex < csn->Info->NumData; dataIndex++)
                                    {
                                        if ((csn->Data[dataIndex].DataType & 0xfffffff) ==
                                                XCUTSCENEDATA_TYPE_JDELTAMODEL &&
                                            data->AssetID == csn->Data[dataIndex].AssetID)
                                        {
                                            deltaModel = csn->Data[dataIndex].DataPtr;
                                            break;
                                        }
                                    }

                                    JDeltaEval(model, deltaModel, deltaAnim, breakTime);
                                }

                                if (pipeCurr != NULL && (pipeCurr->Flags & 0xff00))
                                {
                                    if (fakeCount < 8)
                                    {
                                        sCutsceneFakeModel[fakeCount].Data = model;
                                        sCutsceneFakeModel[fakeCount].BoneCount =
                                            iModelNumBones(model);
                                        memcpy(sCutsceneFakeModel[fakeCount].Mat, animMat,
                                               sizeof(animMat));
                                        sCutsceneFakeModel[fakeCount].Flags = 1;
                                        sCutsceneFakeModel[fakeCount].FadeStart = 100.0f;
                                        sCutsceneFakeModel[fakeCount].FadeEnd = 100.0f;
                                        sCutsceneFakeModel[fakeCount].Bucket[0]->Data = model;
                                        sCutsceneFakeModel[fakeCount].Alpha =
                                            model->geometry->matList.materials[0]->color.alpha /
                                            255.0f;
                                        sCutsceneFakeModel[fakeCount].Pipe = *pipeCurr;
                                        sCutsceneFakeModel[fakeCount].LightKit = gLastLightKit;

                                        if (sCutsceneFakeModel[fakeCount].BoneCount)
                                        {
                                            iModelAnimBoundWorld(
                                                &sCutsceneFakeModel[fakeCount].animBound,
                                                &combinedAnimBound, model, animMat, animMat + 1);
                                        }

                                        xModelBucket_Add(&sCutsceneFakeModel[fakeCount]);
                                        fakeCount++;
                                    }
                                }
                                else
                                {
                                    S32 culled = 0;
                                    xBox worldbox;

                                    if (iModelNumBones(model))
                                    {
                                        iModelAnimBoundWorld(&worldbox, &combinedAnimBound, model,
                                                             animMat, animMat + 1);
                                        culled = xModelBoxCull(&worldbox);
                                        iModelSetWorldBoundFromBox(model, &worldbox);
                                    }
                                    else
                                    {
                                        iModelWorldBoundSphere(model, animMat, NULL);
                                    }

                                    if (!culled)
                                    {
                                        iModelRender(model, animMat);
                                    }

                                    if (iModelNumBones(model) >= 5)
                                    {
                                        shadowBits |= 1 << visIdx;
                                    }
                                }

                                // goto as in the PS2 line table; break or if/else add compares.
                                goto nextmodel;
                            }

                            mphdata = (xCutsceneData*)((U8*)mphdata + sizeof(xCutsceneData) +
                                                       ((mphdata->ChunkSize + 15) & ~15));
                        }

                        if (pipeCurr != NULL && (pipeCurr->Flags & 0xff00))
                        {
                            if (fakeCount < 8)
                            {
                                sCutsceneFakeModel[fakeCount].Data = model;
                                sCutsceneFakeModel[fakeCount].BoneCount = iModelNumBones(model);
                                memcpy(sCutsceneFakeModel[fakeCount].Mat, animMat, sizeof(animMat));
                                sCutsceneFakeModel[fakeCount].Flags = 1;
                                sCutsceneFakeModel[fakeCount].FadeStart = 100.0f;
                                sCutsceneFakeModel[fakeCount].FadeEnd = 100.0f;
                                sCutsceneFakeModel[fakeCount].Bucket[0]->Data = model;
                                sCutsceneFakeModel[fakeCount].Alpha =
                                    model->geometry->matList.materials[0]->color.alpha / 255.0f;
                                sCutsceneFakeModel[fakeCount].Pipe = *pipeCurr;
                                sCutsceneFakeModel[fakeCount].LightKit = gLastLightKit;

                                if (sCutsceneFakeModel[fakeCount].BoneCount)
                                {
                                    iModelAnimBoundWorld(&sCutsceneFakeModel[fakeCount].animBound,
                                                         &combinedAnimBound, model, animMat,
                                                         animMat + 1);
                                }

                                xModelBucket_Add(&sCutsceneFakeModel[fakeCount]);
                                fakeCount++;
                            }
                        }
                        else
                        {
                            S32 culled = 0;
                            xBox worldbox;

                            if (iModelNumBones(model))
                            {
                                iModelAnimBoundWorld(&worldbox, &combinedAnimBound, model, animMat,
                                                     animMat + 1);
                                culled = xModelBoxCull(&worldbox);
                                iModelSetWorldBoundFromBox(model, &worldbox);
                            }
                            else
                            {
                                iModelWorldBoundSphere(model, animMat, NULL);
                            }

                            if (!culled)
                            {
                                iModelRender(model, animMat);
                            }

                            if (iModelNumBones(model) >= 5)
                            {
                                shadowBits |= 1 << visIdx;
                            }
                        }
                    }

                nextmodel:
                    if (pipeCurr != NULL)
                    {
                        pipeCurr++;
                    }

                    visIdx++;
                    model = iModelFile_RWMultiAtomic(model);
                }

                if (shadowBits && xVec3Dist(camVec, (xVec3*)&animMat[0].pos) < 25.0f && !noshadow)
                {
                    CutsceneShadowModel smod;
                    xShadowCache scache;
                    xVec3 center;
                    static xVec3 shadVec = { 0.0f, -1.0f, 0.0f };
                    F32 radius;
                    // not in the DWARF: gLastLightKit is read once and passed to xLightKit_Enable.
                    xLightKit* lastLightKit;

                    center.x = 0.5f * (combinedAnimBound.upper.x + combinedAnimBound.lower.x);
                    center.y = 0.5f * (combinedAnimBound.upper.y + combinedAnimBound.lower.y);
                    center.z = 0.5f * (combinedAnimBound.upper.z + combinedAnimBound.lower.z);

                    radius =
                        xsqrt(SQR(0.5f * (combinedAnimBound.upper.x - combinedAnimBound.lower.x)) +
                              SQR(0.5f * (combinedAnimBound.upper.y - combinedAnimBound.lower.y)) +
                              SQR(0.5f * (combinedAnimBound.upper.z - combinedAnimBound.lower.z)));

                    smod.model = shadowModel;
                    smod.animMat = animMat;
                    smod.shadowBits = shadowBits;

                    lastLightKit = gLastLightKit;
                    xLightKit_Enable(NULL, xglobals->currWorld);

                    xShadowSetLight(&center, &shadVec, 1.0f);
                    xShadowVertical_FillCache(&scache, &center, radius, 6.0f, 0.0871557f);
                    xShadowCameraUpdate(&smod, (void (*)(void*))CutsceneShadowRender, &center,
                                        radius, 0);
                    xShadowVertical_DrawCache(&scache, 0.3f, 0.0f, 0, NULL, NULL);

                    xLightKit_Enable(lastLightKit, xglobals->currWorld);
                }
            }

            animIndex++;
        }

        data = (xCutsceneData*)((U8*)data + sizeof(xCutsceneData) + ((data->ChunkSize + 15) & ~15));
    }

    if (nosey != NULL && (nosey->flg_nosey & 1))
    {
        nosey->CanRenderNow();
    }

    xSubTitlesRender(csn->SubTitlesID, csn->Time);
}

// namespace

xCutscene* xCutscene_CurrentCutscene()
{
    return &sActiveCutscene;
}

namespace
{

    void xCamCutscene::create()
    {
        xCam::create();
        coord_type = XCAM_COORD_CART;
        orient_type = XCAM_ORIENT_QUAT;
        cfg_common.priority = 0xff;
        cfg_common.blend_time = 0.0f;
    }
} // namespace
