#include "xJSP.h"
#include "xGlobals.h"
#include "xMemMgr.h"
#include "xString.h"
#include "xstransvc.h"

struct TempSplitChild
{
    S32 node;
    S32 type;
};

struct TempSplit
{
    TempSplitChild leftChild;
    TempSplitChild rightChild;
    S32 leftCount;
    S32 rightCount;
    S32 axis;
    F32 leftValue;
    F32 rightValue;
};

static RwV3d* sCurrVert;
static U32 sAtomicStartCount;
static RwV3d** sAtomicStartVert;
static TempSplit* sSplitList;
static TempSplit* sSplitCurr;
static xJSPNodeTreeLeaf* sSplitLeaf;

static void RecurseSplitJSPNode(xJSPNodeTreeLeaf* tleaf, S32 count, TempSplitChild* childptr);
static S32 ChooseSplit_MeanMinSum(xJSPNodeTreeLeaf* tleaf, S32 count, S32& bestaxis,
                                  F32& bestcoord);
static F32 GetSplitVolumeSum(xJSPNodeTreeLeaf* tleaf, S32 count, S32 axis, F32 coord);
static RpAtomic* ListAtomicCB(RpAtomic* atomic, void* data);
static RpAtomic* AddAtomicPrecalcedVertCB(RpAtomic* atomic, void* data);
static RpAtomic* AddAtomicCB(RpAtomic* atomic, void* data);
static RpMesh* AddMeshCB(RpMesh* mesh, RpMeshHeader*, void* pData);
static RpAtomic* CountAtomicCB(RpAtomic* atomic, void* data);

// The split axis is a byte offset (0, 4 or 8) of the x/y/z component inside an RwV3d.
#define JSP_AXIS(v, axis) (*(F32*)((U8*)&(v) + (axis)))

static RpAtomic* CountAtomicCB(RpAtomic* atomic, void* data)
{
    sAtomicStartCount++;
    *(U32*)data += atomic->geometry->mesh->totalIndicesInMesh;
    return atomic;
}

static RpMesh* AddMeshCB(RpMesh* mesh, RpMeshHeader*, void* pData)
{
    RwV3d** stripVert = (RwV3d**)pData;
    for (U32 i = 0; i < mesh->numIndices; i++)
    {
        **stripVert = sCurrVert[mesh->indices[i]];
        (*stripVert)++;
    }
    return mesh;
}

static RpAtomic* AddAtomicCB(RpAtomic* atomic, void* data)
{
    sAtomicStartVert[--sAtomicStartCount] = *(RwV3d**)data;
    sCurrVert = atomic->geometry->morphTarget->verts;
    _rpMeshHeaderForAllMeshes(atomic->geometry->mesh, AddMeshCB, data);
    return atomic;
}

static RpAtomic* AddAtomicPrecalcedVertCB(RpAtomic* atomic, void* data)
{
    sAtomicStartVert[--sAtomicStartCount] = *(RwV3d**)data;
    *(RwV3d**)data += atomic->geometry->mesh->totalIndicesInMesh;
    return atomic;
}

static RpAtomic* ListAtomicCB(RpAtomic* atomic, void* data)
{
    RpAtomic*** aList = (RpAtomic***)data;
    **aList = atomic;
    (*aList)++;
    return atomic;
}

U32 get_matid(xJSPHeader* jsp, U32 old_mat_id, U32 jspAssetID)
{
    char num[100];
    U32 matid;
    S32 count;
    xJSPHeader** jsplist;
    S32 i;

    sprintf(num, "%d", old_mat_id);

    matid = 0;
    if (jspAssetID)
    {
        matid = xStrHashCat(jspAssetID, num);
    }
    else
    {
        count = xglobals->sceneCur->env->geom->jsp_count;
        jsplist = xglobals->sceneCur->env->geom->jsp_list;
        for (i = 0; i < count; i++)
        {
            if (jsp == jsplist[i])
            {
                matid = xStrHashCat(xglobals->sceneCur->env->geom->jsp_aid[i], num);
            }
        }
    }

    return matid;
}

void xJSP_MultiStreamRead(void* data, U32 size, xJSPHeader** jsp, U32 assetid)
{
    S32 i;
    RpClump* clump;
    xClumpCollBSPTree* colltree;
    xJSPHeaderEx* hdr;
    __rwMark mark;
    __rwMark* mp;

    if (!*jsp)
    {
        hdr = (xJSPHeaderEx*)RwMalloc(sizeof(xJSPHeaderEx), 0);
        *jsp = hdr;
        memset(hdr, 0, sizeof(xJSPHeaderEx));
    }
    else
    {
        hdr = (xJSPHeaderEx*)*jsp;
    }

    mp = (__rwMark*)data;
    memmove(&mark, mp, sizeof(__rwMark));
    RwMemNative32(&mark, sizeof(__rwMark));

    if (mark.type == 0xBEEF01)
    {
        data = mp + 1;
        colltree = xClumpColl_StaticBufferInit(data, mark.length);

        size -= mark.length + sizeof(__rwMark);
        mp = (__rwMark*)((U8*)data + mark.length);

        memmove(&mark, mp, sizeof(__rwMark));
        RwMemNative32(&mark, sizeof(__rwMark));

        data = mp + 1;

        xJSPHeader* tmphdr = (xJSPHeader*)data;
        strncpy(hdr->idtag, tmphdr->idtag, sizeof(hdr->idtag));
        hdr->version = tmphdr->version;
        hdr->jspNodeCount = tmphdr->jspNodeCount;
        hdr->colltree = colltree;

        if (tmphdr->version < 5)
        {
            hdr->jspNodeList = (xJSPNodeInfo*)((U8*)data + mark.length) - hdr->jspNodeCount;

            if (size >= sizeof(__rwMark))
            {
                mp = (__rwMark*)((U8*)data + mark.length);
                memmove(&mark, mp, sizeof(__rwMark));
                RwMemNative32(&mark, sizeof(__rwMark));

                if (mark.type == 0xBEEF03)
                {
                    data = mp + 1;
                    hdr->stripVecCount = *(U32*)data;
                    data = (U32*)data + 1;
                    hdr->stripVecList = (RwV3d*)data;

                    sAtomicStartCount = RpClumpGetNumAtomics(hdr->clump);
                    sAtomicStartVert = (RwV3d**)RwMalloc(sAtomicStartCount * sizeof(RwV3d*), 0);

                    RwV3d* currVec = hdr->stripVecList;
                    RpClumpForAllAtomics(hdr->clump, AddAtomicPrecalcedVertCB, &currVec);
                }
            }

            if (hdr->stripVecCount == 0)
            {
                hdr->stripVecCount = 0;
                sAtomicStartCount = 0;

                RpClumpForAllAtomics(hdr->clump, CountAtomicCB, &hdr->stripVecCount);

                hdr->stripVecList = (RwV3d*)RwMalloc(hdr->stripVecCount * sizeof(RwV3d), 0);
                sAtomicStartVert = (RwV3d**)RwMalloc(sAtomicStartCount * sizeof(RwV3d*), 0);

                RwV3d* currVec = hdr->stripVecList;
                RpClumpForAllAtomics(hdr->clump, AddAtomicCB, &currVec);
            }

            for (i = 0; i < colltree->numTriangles; i++)
            {
                colltree->triangles[i].v.p =
                    sAtomicStartVert[colltree->triangles[i].v.i.atomIndex] +
                    colltree->triangles[i].v.i.meshVertIndex;
            }

            RwFree(sAtomicStartVert);

            hdr->nodetree = xJSPNodeTreeBuild(hdr);
        }
        else if (tmphdr->version == 5)
        {
            hdr->vertDataFlags = tmphdr->vertDataFlags;
            hdr->vertDataStride = tmphdr->vertDataStride;
            hdr->jspNodeList = (xJSPNodeInfo*)(tmphdr + 1);

            xJSPNodeTree* tmpNodeTree = (xJSPNodeTree*)(hdr->jspNodeList + hdr->jspNodeCount);

            hdr->nodetree = (xJSPNodeTree*)RwMalloc(sizeof(xJSPNodeTree), 0);
            *hdr->nodetree = *tmpNodeTree;
            hdr->nodetree->branchNodes = (xJSPNodeTreeBranch*)(tmpNodeTree + 1);
            hdr->nodetree->leafNodes =
                (xJSPNodeTreeLeaf*)(hdr->nodetree->branchNodes + hdr->nodetree->numBranchNodes);

            U32* stripvecdata = (U32*)(hdr->nodetree->leafNodes + hdr->nodetree->numLeafNodes);
            hdr->stripVecCount = *stripvecdata;
            hdr->stripVecList = (RwV3d*)(stripvecdata + 1);

            for (i = 0; i < colltree->numTriangles; i++)
            {
                colltree->triangles[i].v.p = hdr->stripVecList + colltree->triangles[i].v.rawIdx;
            }
        }

        __rwMark* mp2 = (__rwMark*)((U8*)data + mark.length);
        __rwMark mark2;
        memmove(&mark2, mp2, sizeof(__rwMark));
        RwMemNative32(&mark2, sizeof(__rwMark));

        if (mark2.type == 0xBEEF04)
        {
            mark = mark2;
            memcpy(&hdr->extraInfo, mp2 + 1, sizeof(xJSPExtraInfo));
        }

        for (i = 0; i < colltree->numTriangles; i++)
        {
            colltree->triangles[i].matIndex =
                hdr->jspNodeList[colltree->triangles[i].matIndex].originalMatIndex;
        }

        xJSPNodeLightBuild(hdr);
    }
    else
    {
        RwMemory rwmem;
        rwmem.start = (RwUInt8*)data;
        rwmem.length = size;

        RwStream* stream = RwStreamOpen(rwSTREAMMEMORY, rwSTREAMREAD, &rwmem);
        RwStreamFindChunk(stream, rwID_CLUMP, NULL, NULL);
        clump = RpClumpStreamRead(stream);
        RwStreamClose(stream, NULL);

        if (!hdr->clump)
        {
            hdr->clump = clump;
        }
        else
        {
            S32 i;
            S32 atomCount;

            atomCount = RpClumpGetNumAtomics(clump);
            if (atomCount)
            {
                RpAtomic** atomList = (RpAtomic**)RwMalloc(atomCount * sizeof(RpAtomic*), 0);
                RpAtomic** atomCurr = atomList;
                RpClumpForAllAtomics(clump, ListAtomicCB, &atomCurr);

                for (i = atomCount - 1; i >= 0; i--)
                {
                    RpClumpRemoveAtomic(clump, atomList[i]);
                    RpClumpAddAtomic(hdr->clump, atomList[i]);
                    RpAtomicSetFrame(atomList[i], RpClumpGetFrame(hdr->clump));
                }

                RwFree(atomList);
            }

            RpClumpDestroy(clump);
        }
    }
}

void xJSP_Destroy(xJSPHeader* jsp)
{
    S32 i;
    RwLLLink* cur;
    RwLLLink* end;
    RpAtomic* apAtom;

    for (i = 0; i < xglobals->sceneCur->env->geom->jsp_count; i++)
    {
        if (xglobals->sceneCur->env->geom->jsp_list[i] == jsp &&
            xglobals->sceneCur->env->geom->world)
        {
            RpWorldDestroy(xglobals->sceneCur->env->geom->world);
            xglobals->sceneCur->env->geom->world = NULL;
            break;
        }
    }

    cur = rwLinkListGetFirstLLLink(&jsp->clump->atomicList);
    end = rwLinkListGetTerminator(&jsp->clump->atomicList);
    while (cur != end)
    {
        apAtom = rwLLLinkGetData(cur, RpAtomic, inClumpLink);
        rwLinkListInitialize(&apAtom->llWorldSectorsInAtomic);
        cur = rwLLLinkGetNext(cur);
    }

    RpClumpDestroy(jsp->clump);
    RwFree(jsp->colltree);
    RwFree(jsp->nodelight);
    RwFree(jsp->nodetree);

    if (jsp->version < 5)
    {
        // The strip vectors were loaded in place right after the 0xBEEF03 chunk's
        // __rwMark (3 words) and vector count (1 word); only free a separate allocation.
        U32* tp = (U32*)jsp->stripVecList - 4;
        U32 test = *tp;
        RwMemNative32(&test, sizeof(U32));
        if (test != 0xBEEF03)
        {
            RwFree(jsp->stripVecList);
        }
    }

    RwFree(jsp);
}

void xJSPNodeLightBuild(xJSPHeader* jsp)
{
    xJSPNodeLight* nlight;
    RwLLLink* cur;
    RwLLLink* end;
    RpClump* clump;
    RpAtomic* apAtom;

    jsp->nodelight = (xJSPNodeLight*)RwMalloc(jsp->jspNodeCount * sizeof(xJSPNodeLight), 0);
    memset(jsp->nodelight, 0, jsp->jspNodeCount * sizeof(xJSPNodeLight));

    clump = jsp->clump;
    nlight = jsp->nodelight;
    cur = rwLinkListGetFirstLLLink(&clump->atomicList);
    end = rwLinkListGetTerminator(&clump->atomicList);
    while (cur != end)
    {
        apAtom = rwLLLinkGetData(cur, RpAtomic, inClumpLink);
        nlight->atomic = apAtom;

        nlight->dummyTie.worldSector = &nlight->dummySector;
        rwLLLinkInitialize(&nlight->dummyTie.lWorldSectorInAtomic);
        rwLinkListAddLLLink(&apAtom->llWorldSectorsInAtomic,
                            &nlight->dummyTie.lWorldSectorInAtomic);

        rwLinkListInitialize(&nlight->dummySector.lightsInWorldSector);

        nlight++;
        cur = rwLLLinkGetNext(cur);
    }
}

static F32 GetSplitVolumeSum(xJSPNodeTreeLeaf* tleaf, S32 count, S32 axis, F32 coord)
{
    S32 i;
    S32 numleft = 0;
    S32 numright = 0;
    RwBBox leftbox;
    RwBBox rightbox;

    for (i = 0; i < count; i++)
    {
        if (JSP_AXIS(tleaf[i].box.inf, axis) < coord)
        {
            if (numleft == 0)
            {
                leftbox = tleaf[i].box;
            }
            else
            {
                leftbox.inf.x = MIN(leftbox.inf.x, tleaf[i].box.inf.x);
                leftbox.inf.y = MIN(leftbox.inf.y, tleaf[i].box.inf.y);
                leftbox.inf.z = MIN(leftbox.inf.z, tleaf[i].box.inf.z);
                leftbox.sup.x = MAX(leftbox.sup.x, tleaf[i].box.sup.x);
                leftbox.sup.y = MAX(leftbox.sup.y, tleaf[i].box.sup.y);
                leftbox.sup.z = MAX(leftbox.sup.z, tleaf[i].box.sup.z);
            }
            numleft++;
        }

        if (JSP_AXIS(tleaf[i].box.sup, axis) > coord || coord == JSP_AXIS(tleaf[i].box.inf, axis))
        {
            if (numright == 0)
            {
                rightbox = tleaf[i].box;
            }
            else
            {
                rightbox.inf.x = MIN(rightbox.inf.x, tleaf[i].box.inf.x);
                rightbox.inf.y = MIN(rightbox.inf.y, tleaf[i].box.inf.y);
                rightbox.inf.z = MIN(rightbox.inf.z, tleaf[i].box.inf.z);
                rightbox.sup.x = MAX(rightbox.sup.x, tleaf[i].box.sup.x);
                rightbox.sup.y = MAX(rightbox.sup.y, tleaf[i].box.sup.y);
                rightbox.sup.z = MAX(rightbox.sup.z, tleaf[i].box.sup.z);
            }
            numright++;
        }
    }

    if (numleft == count || numright == count)
    {
        return 1e38f;
    }

    return (leftbox.sup.x - leftbox.inf.x) * (leftbox.sup.y - leftbox.inf.y) *
               (leftbox.sup.z - leftbox.inf.z) +
           (rightbox.sup.x - rightbox.inf.x) * (rightbox.sup.y - rightbox.inf.y) *
               (rightbox.sup.z - rightbox.inf.z);
}

static S32 ChooseSplit_MeanMinSum(xJSPNodeTreeLeaf* tleaf, S32 count, S32& bestaxis, F32& bestcoord)
{
    F32 bestvolume = 1e38f;
    S32 i;
    S32 axis;
    RwV3d mean = { 0.0f, 0.0f, 0.0f };
    F32 testarray[3];
    F32 meancoord;
    F32 coord;
    F32 testvol;

    for (i = 0; i < count; i++)
    {
        mean.x += tleaf[i].box.inf.x + tleaf[i].box.sup.x;
        mean.y += tleaf[i].box.inf.y + tleaf[i].box.sup.y;
        mean.z += tleaf[i].box.inf.z + tleaf[i].box.sup.z;
    }

    mean.x /= count * 2;
    mean.y /= count * 2;
    mean.z /= count * 2;

    for (axis = 0; axis < sizeof(RwV3d); axis += sizeof(F32))
    {
        meancoord = JSP_AXIS(mean, axis);
        testarray[0] = -1e38f;
        testarray[1] = meancoord;
        testarray[2] = 1e38f;

        for (i = 0; i < count; i++)
        {
            coord = JSP_AXIS(tleaf[i].box.inf, axis);
            if (coord <= meancoord && coord > testarray[0])
            {
                testarray[0] = coord;
            }
            if (coord < testarray[2] && coord >= meancoord)
            {
                testarray[2] = coord;
            }
        }

        for (i = 0; i < 3; i++)
        {
            testvol = GetSplitVolumeSum(tleaf, count, axis, testarray[i]);
            if (testvol < bestvolume)
            {
                bestaxis = axis;
                bestvolume = testvol;
                bestcoord = testarray[i];
            }
        }
    }

    return bestvolume != 1e38f;
}

static void RecurseSplitJSPNode(xJSPNodeTreeLeaf* tleaf, S32 count, TempSplitChild* childptr)
{
    S32 i;
    S32 bestaxis;
    S32 lastright;
    F32 bestcoord;
    xJSPNodeTreeLeaf templeaf;
    TempSplit* thisSplit;

    if (count >= 2 && ChooseSplit_MeanMinSum(tleaf, count, bestaxis, bestcoord))
    {
        lastright = count;
        for (i = 0; i < lastright; i++)
        {
            if (0.5f *
                    (JSP_AXIS(tleaf[i].box.inf, bestaxis) + JSP_AXIS(tleaf[i].box.sup, bestaxis)) >=
                bestcoord)
            {
                templeaf = tleaf[i];
                lastright--;
                tleaf[i] = tleaf[lastright];
                tleaf[lastright] = templeaf;
                i--;
            }
        }

        if (lastright == count || lastright == 0)
        {
            if (childptr)
            {
                childptr->node = tleaf - sSplitLeaf;
                childptr->type = 1;
            }
            tleaf->leafCount = count;
        }
        else
        {
            sSplitCurr->axis = bestaxis;
            sSplitCurr->rightCount = count - lastright;
            sSplitCurr->leftCount = lastright;
            sSplitCurr->rightValue = 1e38f;
            sSplitCurr->leftValue = -1e38f;

            for (i = 0; i < lastright; i++)
            {
                sSplitCurr->leftValue =
                    MAX(sSplitCurr->leftValue, JSP_AXIS(tleaf[i].box.sup, bestaxis));
            }

            for (; i < count; i++)
            {
                sSplitCurr->rightValue =
                    MIN(sSplitCurr->rightValue, JSP_AXIS(tleaf[i].box.inf, bestaxis));
            }

            if (childptr)
            {
                childptr->node = sSplitCurr - sSplitList;
                childptr->type = 2;
            }

            thisSplit = sSplitCurr;
            sSplitCurr++;
            RecurseSplitJSPNode(tleaf, lastright, &thisSplit->leftChild);
            RecurseSplitJSPNode(tleaf + lastright, count - lastright, &thisSplit->rightChild);
        }
    }
    else
    {
        if (childptr)
        {
            childptr->node = tleaf - sSplitLeaf;
            childptr->type = 1;
        }
        tleaf->leafCount = count;
    }
}

xJSPNodeTree* xJSPNodeTreeBuild(xJSPHeader* jsp)
{
    RpClump* clump = jsp->clump;
    S32 i;
    S32 j;
    S32 atomCount;
    RpAtomic** atomList;
    RpAtomic** atomCurr;
    xJSPNodeTreeLeaf* tleaf;
    RpGeometry* geom;
    S32 numVert;
    RwV3d* vert;
    xJSPNodeTree* jntree;

    atomCount = RpClumpGetNumAtomics(clump);
    if (!atomCount)
    {
        return NULL;
    }

    atomList = (RpAtomic**)RwMalloc(atomCount * sizeof(RpAtomic*), 0);
    atomCurr = atomList;
    RpClumpForAllAtomics(clump, ListAtomicCB, &atomCurr);

    tleaf = (xJSPNodeTreeLeaf*)RwMalloc(atomCount * sizeof(xJSPNodeTreeLeaf), 0);

    atomCurr = atomList;
    for (i = 0; i < atomCount; i++)
    {
        geom = (*atomCurr)->geometry;
        numVert = geom->numVertices;
        vert = geom->morphTarget->verts;

        tleaf[i].nodeIndex = i;
        tleaf[i].box.inf.x = 1e38f;
        tleaf[i].box.inf.y = 1e38f;
        tleaf[i].box.inf.z = 1e38f;
        tleaf[i].box.sup.x = -1e38f;
        tleaf[i].box.sup.y = -1e38f;
        tleaf[i].box.sup.z = -1e38f;

        for (j = 0; j < numVert; j++)
        {
            tleaf[i].box.inf.x = MIN(tleaf[i].box.inf.x, vert->x);
            tleaf[i].box.inf.y = MIN(tleaf[i].box.inf.y, vert->y);
            tleaf[i].box.inf.z = MIN(tleaf[i].box.inf.z, vert->z);
            tleaf[i].box.sup.x = MAX(tleaf[i].box.sup.x, vert->x);
            tleaf[i].box.sup.y = MAX(tleaf[i].box.sup.y, vert->y);
            tleaf[i].box.sup.z = MAX(tleaf[i].box.sup.z, vert->z);
            vert++;
        }

        atomCurr++;
    }

    sSplitList = (TempSplit*)RwMalloc(atomCount * sizeof(TempSplit), 0);
    sSplitCurr = sSplitList;
    sSplitLeaf = tleaf;

    RecurseSplitJSPNode(tleaf, atomCount, NULL);

    jntree = (xJSPNodeTree*)RwMalloc(sizeof(xJSPNodeTree) + atomCount * sizeof(xJSPNodeTreeLeaf) +
                                         (sSplitCurr - sSplitList) * sizeof(xJSPNodeTreeBranch),
                                     0);
    jntree->numBranchNodes = sSplitCurr - sSplitList;
    jntree->branchNodes = (xJSPNodeTreeBranch*)(jntree + 1);
    jntree->numLeafNodes = atomCount;
    jntree->leafNodes = (xJSPNodeTreeLeaf*)(jntree->branchNodes + jntree->numBranchNodes);
    memcpy(jntree->leafNodes, tleaf, atomCount * sizeof(xJSPNodeTreeLeaf));

    for (i = 0; i < jntree->numBranchNodes; i++)
    {
        jntree->branchNodes[i].leftNode = sSplitList[i].leftChild.node;
        jntree->branchNodes[i].leftType = sSplitList[i].leftChild.type;
        jntree->branchNodes[i].rightNode = sSplitList[i].rightChild.node;
        jntree->branchNodes[i].rightType = sSplitList[i].rightChild.type;
        jntree->branchNodes[i].coord = sSplitList[i].axis;
        jntree->branchNodes[i].leftValue = sSplitList[i].leftValue;
        jntree->branchNodes[i].rightValue = sSplitList[i].rightValue;
    }

    RwFree(atomList);
    RwFree(tleaf);
    RwFree(sSplitList);
    sSplitList = NULL;

    return jntree;
}
