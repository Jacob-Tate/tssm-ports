#include "zCollGeom.h"
#include "xstransvc.h"
#include "xString.h"
#include "zBase.h"
#include "iModel.h"

struct zCollisionGeomTableEntry
{
    U32 sTableCount;
    zCollGeomTable* sTableList;
};

// 'COLL' asset header (the name is ours): the row count, then that many zCollGeomTable rows. Rows
// are reached with '+ 1' so that they stay at offset 4 where pointers are 8 bytes.
struct zCollGeomAssetHeader
{
    U32 count;
};

static U32 sCollisionGeomTableCount;
static zCollisionGeomTableEntry sCollisionGeomTable[20];

U32 zCollGeom_EntSetup(xEnt* ent)
{
    U32 i, j;
    bool auto_cam_coll;
    zCollGeomTable* tableList;

    auto_cam_coll = (ent->baseType == eBaseTypeStatic || ent->baseType == eBaseTypeButton ||
                     ent->baseType == eBaseTypePlatform);

    ent->collModel = NULL;
    ent->camcollModel = NULL;

    if (ent->model)
    {
        for (j = 0; j < sCollisionGeomTableCount; j++)
        {
            tableList = sCollisionGeomTable[j].sTableList;

            for (i = 0; i < sCollisionGeomTable[j].sTableCount; i++)
            {
                if (tableList[i].baseModel == ent->model->Data)
                {
                    RpAtomic* imodel;

                    if (imodel = tableList[i].colModel[0])
                    {
                        xModelInstance* model = xModelInstanceAlloc(imodel, ent, 0x2000, 0, NULL);

                        ent->collModel = model;
                        ent->collModel->Mat = ent->model->Mat;

                        while (imodel = iModelFile_RWMultiAtomic(imodel))
                        {
                            xModelInstanceAttach(xModelInstanceAlloc(imodel, ent, 0x2000, 0, NULL),
                                                 model);
                        }
                    }
                    else if (imodel = tableList[i].camcolModel)
                    {
                        xModelInstance* model = xModelInstanceAlloc(imodel, ent, 0x2000, 0, NULL);

                        ent->camcollModel = model;
                        ent->camcollModel->Mat = ent->model->Mat;

                        while (imodel = iModelFile_RWMultiAtomic(imodel))
                        {
                            xModelInstanceAttach(xModelInstanceAlloc(imodel, ent, 0x2000, 0, NULL),
                                                 model);
                        }
                    }
                    else
                    {
                        auto_cam_coll = false;
                    }
                }
            }
        }
    }

    if (auto_cam_coll && !ent->camcollModel)
    {
        ent->camcollModel = ent->model;
    }

    if (ent->collModel)
    {
        return 1;
    }

    ent->collModel = ent->model;
    return 0;
}

void zCollGeom_Init()
{
    U32 tmpsize, i, j, k;
    void* data;
    U32 hash;

    sCollisionGeomTableCount = xSTAssetCountByType('COLL');

    if (sCollisionGeomTableCount)
    {
        if (sCollisionGeomTableCount > 20)
        {
            sCollisionGeomTableCount = 20;
        }

        for (k = 0; k < sCollisionGeomTableCount; k++)
        {
            data = xSTFindAssetByType('COLL', k, &tmpsize);

            if (data)
            {
                sCollisionGeomTable[k].sTableCount = ((zCollGeomAssetHeader*)data)->count;

                if (!sCollisionGeomTable[k].sTableCount)
                {
                    sCollisionGeomTable[k].sTableList = NULL;
                }
                else
                {
                    // copy-at-load: COLL rows keep their RpAtomic pointers in 4-byte fields that
                    // hold asset IDs on disk and are resolved in place below, so a host with 8-byte
                    // pointers must widen the rows when the asset is loaded.
                    sCollisionGeomTable[k].sTableList =
                        (zCollGeomTable*)((zCollGeomAssetHeader*)data + 1);

                    for (i = 0; i < sCollisionGeomTable[k].sTableCount; i++)
                    {
                        if (sCollisionGeomTable[k].sTableList[i].baseModel)
                        {
                            hash = (U32)(UPtr)sCollisionGeomTable[k].sTableList[i].baseModel;
                            sCollisionGeomTable[k].sTableList[i].baseModel =
                                (RpAtomic*)xSTFindAsset(hash, NULL);

                            if (!sCollisionGeomTable[k].sTableList[i].baseModel)
                            {
                                sCollisionGeomTable[k].sTableList[i].baseModel =
                                    (RpAtomic*)xSTFindAsset(xStrHashCat(hash, ".dff"), NULL);
                            }
                        }

                        // colModel[] has one entry; the one-pass loop is Ratatouille's
                        for (j = 0; j < 1; j++)
                        {
                            if (sCollisionGeomTable[k].sTableList[i].colModel[j])
                            {
                                sCollisionGeomTable[k].sTableList[i].colModel[j] =
                                    (RpAtomic*)xSTFindAsset(
                                        (U32)(UPtr)sCollisionGeomTable[k].sTableList[i].colModel[j],
                                        NULL);
                            }
                        }

                        if (!sCollisionGeomTable[k].sTableList[i].colModel[0] &&
                            sCollisionGeomTable[k].sTableList[i].camcolModel)
                        {
                            sCollisionGeomTable[k].sTableList[i].camcolModel =
                                (RpAtomic*)xSTFindAsset(
                                    (U32)(UPtr)sCollisionGeomTable[k].sTableList[i].camcolModel,
                                    NULL);
                        }
                    }
                }
            }
        }
    }
}

void zCollGeom_CamEnable(xEnt* ent)
{
    if (!ent->camcollModel)
    {
        ent->camcollModel = ent->model;
    }
}

void zCollGeom_CamDisable(xEnt* ent)
{
    ent->camcollModel = NULL;
}
