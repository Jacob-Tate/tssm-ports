#include "zLOD.h"

#include "xMathInlines.h"
#include "xMemMgr.h"
#include "xstransvc.h"
#include "zDestructible.h"
#include "zGlobals.h"

#include <string.h>

// 'LODT' asset header: the row count, then that many zLODTable rows. The name is ours (data is
// void* in the PS2 and Ratatouille DWARF); the layout is the one the asm reads. Rows are reached
// with '+ 1' so that they stay at offset 4 where pointers are 8 bytes.
struct zLODAssetHeader
{
    S32 count;
};

static U32 sLODTableCount;
static zLODTable* sLODTableList;
static U32 sManagerIndex;
static U32 sManagerCount;
static F32 sLODFadeDistance = 4.0f;
static zLODManager sManagerList[2048];

static void AddToLODList(xEnt* ent)
{
    xModelInstance* model = ent->model;

    for (U32 i = 0; i < sLODTableCount; i++)
    {
        if (sLODTableList[i].baseBucket != NULL &&
            (*sLODTableList[i].baseBucket)->OriginalData == model->Data)
        {
            xModelInstance* minst = model->Next;
            U32 numextra = 0;
            while (minst != NULL)
            {
                minst = minst->Next;
                numextra++;
            }

            if (sManagerCount < 2048)
            {
                F32 distscale = model->Mat->right.x * model->Mat->right.x +
                                model->Mat->right.y * model->Mat->right.y +
                                model->Mat->right.z * model->Mat->right.z;
                minst = model;
                if (distscale < 0.0001f)
                {
                    distscale = 1.0f;
                }

                while (minst != NULL)
                {
                    minst->FadeEnd = xsqrt(distscale * sLODTableList[i].noRenderDist);
                    minst->FadeStart = minst->FadeEnd - sLODFadeDistance;
                    minst = minst->Next;
                }

                sManagerList[sManagerCount].numextra = numextra;
                sManagerList[sManagerCount].lod = &sLODTableList[i];
                sManagerList[sManagerCount].model = ent->model;
                sManagerList[sManagerCount].adjustNoRenderDist =
                    (10.0f + xsqrt(sLODTableList[i].noRenderDist)) *
                    (10.0f + xsqrt(sLODTableList[i].noRenderDist));

                zDestructible* destructible = zDestructibleGetFromEntity(ent);
                if (destructible != NULL)
                {
                    zDestructibleAsset_CopyLODDistances(ent, destructible);
                }

                sManagerCount++;
            }
        }
    }
}

static xEnt* AddToLODList(xEnt* ent, xScene* scene, void* data)
{
    if (!ent->model)
    {
        return ent;
    }

    AddToLODList(ent);

    return ent;
}

void zLOD_Setup(F32 fadeDistance)
{
    U32 tmpsize;
    U32 i;
    U32 j;
    void* data;
    U32 count;
    zLODTable* tableCurr;
    // not in the DWARF: the distance scale is chosen once and applied to every LOD table.
    F32 distScale = 1.0f;

    switch (globals.sceneCur->sceneID)
    {
    case 0:
        distScale = 0.1f;
        break;
    }

    sLODFadeDistance = fadeDistance;
    sLODTableCount = 0;
    sLODTableList = NULL;
    sManagerCount = 0;

    count = xSTAssetCountByType('LODT');
    if (count == 0)
    {
        return;
    }

    for (i = 0; i < count; i++)
    {
        data = xSTFindAssetByType('LODT', i, &tmpsize);
        sLODTableCount += ((zLODAssetHeader*)data)->count;
    }

    if (sLODTableCount == 0)
    {
        return;
    }

    sLODTableList = (zLODTable*)xMemAlloc(gActiveHeap, sLODTableCount * sizeof(zLODTable), 0);
    tableCurr = sLODTableList;

    for (i = 0; i < count; i++)
    {
        data = xSTFindAssetByType('LODT', i, &tmpsize);
        // copy-at-load: LODT rows keep their bucket pointers in 4-byte fields that hold asset IDs
        // on disk, and the copy assumes the rows have zLODTable's layout. A host with 8-byte
        // pointers must widen the rows (to sizeof(zLODTable)) when the asset is loaded.
        memcpy(tableCurr, (zLODAssetHeader*)data + 1,
               ((zLODAssetHeader*)data)->count * sizeof(zLODTable));
        tableCurr += ((zLODAssetHeader*)data)->count;
    }

    for (i = 0; i < sLODTableCount; i++)
    {
        sLODTableList[i].lodDist[0] *= distScale;
        sLODTableList[i].lodDist[1] *= distScale;
        sLODTableList[i].lodDist[2] *= distScale;
        sLODTableList[i].noRenderDist *= distScale;
        sLODTableList[i].noRenderDist *= sLODTableList[i].noRenderDist;

        if (sLODTableList[i].baseBucket)
        {
            RpAtomic* model = (RpAtomic*)xSTFindAsset((U32)(UPtr)sLODTableList[i].baseBucket, NULL);
            if (model)
            {
                sLODTableList[i].baseBucket = xModelBucket_GetBuckets(model);
            }
            else
            {
                sLODTableList[i].baseBucket = NULL;
            }
        }

        for (j = 0; j < 3; j++)
        {
            if (sLODTableList[i].lodBucket[j])
            {
                RpAtomic* model =
                    (RpAtomic*)xSTFindAsset((U32)(UPtr)sLODTableList[i].lodBucket[j], NULL);
                if (model)
                {
                    sLODTableList[i].lodBucket[j] = xModelBucket_GetBuckets(model);
                }
                else
                {
                    sLODTableList[i].lodBucket[j] = NULL;
                }
            }
            sLODTableList[i].lodDist[j] *= sLODTableList[i].lodDist[j];
        }
    }

    sManagerCount = 0;
    xSceneForAllEnts(globals.sceneCur, AddToLODList, NULL);
}

void zLOD_Update(U32 percent_update)
{
    xVec3* campos = &globals.cam->mat.pos;
    U32 numUpdates;
    zLODTable* lod;
    xModelInstance* model;
    F32 camdist2;
    F32 distscale;
    S32 i;

    if (sManagerCount == 0)
    {
        return;
    }

    numUpdates = (sManagerCount * percent_update) / 100;
    if (numUpdates == 0)
    {
        numUpdates++;
    }

    while (numUpdates--)
    {
        sManagerIndex++;
        if (sManagerIndex >= sManagerCount)
        {
            sManagerIndex = 0;
        }

        lod = sManagerList[sManagerIndex].lod;
        model = sManagerList[sManagerIndex].model;

        if (lod == NULL)
        {
            continue;
        }

        camdist2 = 0.0f;

        if (lod->flags & 1)
        {
            distscale = 1.0f;
        }
        else
        {
            distscale = model->Mat->right.x * model->Mat->right.x +
                        model->Mat->right.y * model->Mat->right.y +
                        model->Mat->right.z * model->Mat->right.z;
            if (distscale < 0.0001f)
            {
                distscale = 1.0f;
            }
        }

        if (model->Mat)
        {
            camdist2 = ((campos->x - model->Mat->pos.x) * (campos->x - model->Mat->pos.x) +
                        (campos->y - model->Mat->pos.y) * (campos->y - model->Mat->pos.y) +
                        (campos->z - model->Mat->pos.z) * (campos->z - model->Mat->pos.z)) /
                       distscale;
        }

        if (camdist2 >= sManagerList[sManagerIndex].adjustNoRenderDist)
        {
            model->Flags |= 0x400;

            if (sManagerList[sManagerIndex].numextra)
            {
                for (model = model->Next; model != NULL; model = model->Next)
                {
                    model->Flags |= 0x400;
                }
            }
        }
        else
        {
            i = 0;
            model->Flags &= (U16)~0x400;

            if (lod->baseBucket && lod->lodBucket[0])
            {
                model->Bucket = lod->baseBucket;
                model->Data = (*model->Bucket)->OriginalData;
            }

            for (; i < 3 && lod->lodBucket[i] && camdist2 > lod->lodDist[i]; i++)
            {
                model->Bucket = lod->lodBucket[i];
                model->Data = (*model->Bucket)->OriginalData;
            }

            if (sManagerList[sManagerIndex].numextra)
            {
                if (i == 0)
                {
                    for (model = model->Next; model != NULL; model = model->Next)
                    {
                        model->Flags &= (U16)~0x400;
                    }
                }
                else
                {
                    for (model = model->Next; model != NULL; model = model->Next)
                    {
                        model->Flags |= 0x400;
                    }
                }
            }
        }
    }
}

zLODTable* zLOD_Get(xEnt* ent)
{
    if (!ent->model)
    {
        return 0;
    }

    for (U32 i = 0; i < sLODTableCount; i++)
    {
        if (sLODTableList[i].baseBucket != NULL)
        {
            if ((*sLODTableList[i].baseBucket)->OriginalData == ent->model->Data)
            {
                return &sLODTableList[i];
            }
        }
    }
    return 0;
}
