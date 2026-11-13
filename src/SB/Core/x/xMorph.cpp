#include "xMorph.h"
#include "iModel.h"
#include <string.h>

xMorphSeqFile* xMorphSeqSetup(void* data, xMorphFindAssetCallback FindAssetCB)
{
    S32 i, j;
    RpAtomic* model;
    xMorphTargetFile* mphtgt;
    xMorphSeqFile* header;
    F32* timelist;
    xMorphFrame* framelist;
    U32* assetlist;
    char* namelist;
    void* assetPtr;
    U32 skipsize;

    // copy-at-load: the frames' pointers (xMorphFrame::Model and Targets[]) are 4-byte slots on disk
    // holding indices, resolved in place below, so a host with 8-byte pointers must widen the frames
    // when the asset is loaded. assetlist[] stays 32-bit: the models and morph targets it points to
    // have to sit below 4 GB. (The Xbox data has no morph sequences, so a 64-bit PC build may
    // reject one at load time instead of widening it.)
    header = (xMorphSeqFile*)data;
    timelist = (F32*)(header + 1);
    framelist = (xMorphFrame*)(timelist + header->TimeCount);
    assetlist = (U32*)(framelist + header->TimeCount);
    namelist = (char*)(assetlist + header->ModelCount * 2);

    if (header->Flags & 0x80000000)
    {
        return header;
    }

    header->Flags |= 0x80000000;
    if (!(header->Flags & 0x1))
    {
        namelist = NULL;
    }

    for (i = 0; i < (S32)header->ModelCount * 2; i++)
    {
        if (assetlist[i])
        {
            assetPtr = FindAssetCB(assetlist[i], namelist);
            assetlist[i] = (U32)(UPtr)assetPtr;
        }
        if (namelist)
        {
            namelist = strlen(namelist) + 1 + namelist;
        }
    }

    for (i = 0; i < (S32)header->ModelCount; i++)
    {
        if (assetlist[i * 2 + 1])
        {
            model = (RpAtomic*)(UPtr)assetlist[i * 2];
            mphtgt = (xMorphTargetFile*)(UPtr)assetlist[i * 2 + 1];
            iMorphOptimize(model, mphtgt->Flags & 0x1);
        }
    }

    for (i = 0; i < (S32)header->TimeCount; i++)
    {
        model = (RpAtomic*)(UPtr)assetlist[(U32)(UPtr)framelist[i].Model * 2];
        mphtgt = (xMorphTargetFile*)(UPtr)assetlist[(U32)(UPtr)framelist[i].Model * 2 + 1];
        framelist[i].Model = model;
        for (j = 0; j < 4; j++)
        {
            if ((U32)(UPtr)framelist[i].Targets[j] == 0xFFFFFFFF)
            {
                framelist[i].Targets[j] = NULL;
            }
            else
            {
                skipsize = (mphtgt->NumVerts * 3 + 7) & ~7;
                if (mphtgt->Flags & 0x1)
                {
                    skipsize *= 2;
                }
                if (j == 0)
                {
                    framelist[i].Flags = mphtgt->Flags;
                    framelist[i].NumVerts = mphtgt->NumVerts;
                    framelist[i].Scale *= mphtgt->Scale;
                }
                framelist[i].Targets[j] =
                    (S16*)((U8*)(mphtgt + 1) + skipsize * (U32)(UPtr)framelist[i].Targets[j] * 2);
            }
        }
    }

    return header;
}

void xMorphRender(xMorphSeqFile* seq, RwMatrix* mat, F32 time)
{
    F32 lerp;
    U32 tidx, tcount;
    S16 weight[4];
    S16* wptr;
    F32* times;
    xMorphFrame* frame;

    times = (F32*)(seq + 1);
    tcount = seq->TimeCount;
    frame = (xMorphFrame*)(times + seq->TimeCount);

    if (time < 0.0f)
    {
        time = 0.0f;
    }
    if (time > times[tcount - 1])
    {
        time = times[tcount - 1];
    }

    tidx = (tcount - 1) & 0x3;
    while (times[tidx] < time)
    {
        tidx += 4;
    }
    while (tidx && time < times[tidx])
    {
        tidx--;
    }

    frame += tidx;

    if (!frame->Targets[0])
    {
        iModelRender(frame->Model, mat);
    }
    else
    {
        lerp = frame->RecipTime * (time - times[tidx]);
        if (lerp == 0.0f)
        {
            wptr = frame->WeightStart;
        }
        else
        {
            weight[0] =
                frame->WeightStart[0] + (S32)(lerp * (frame->WeightEnd[0] - frame->WeightStart[0]));
            weight[1] =
                frame->WeightStart[1] + (S32)(lerp * (frame->WeightEnd[1] - frame->WeightStart[1]));
            weight[2] =
                frame->WeightStart[2] + (S32)(lerp * (frame->WeightEnd[2] - frame->WeightStart[2]));
            weight[3] =
                frame->WeightStart[3] + (S32)(lerp * (frame->WeightEnd[3] - frame->WeightStart[3]));
            wptr = weight;
        }

        iMorphRender(frame->Model, mat, frame->Targets, wptr, frame->Flags & 0x1, frame->Scale);
    }
}

F32 xMorphSeqDuration(xMorphSeqFile* seq)
{
    return ((F32*)(seq + 1))[seq->TimeCount - 1];
}
