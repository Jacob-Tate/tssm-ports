#ifndef XJSP_H
#define XJSP_H

#include <types.h>
#include <rwcore.h>
#include <rpworld.h>
#include "xClumpColl.h"
#include "xMath3.h"

struct __rwMark
{
    U32 type;
    U32 length;
    U32 libraryID;
};

struct xJSPNodeInfo
{
    S32 originalMatIndex;
    U16 nodeFlags;
    S16 sortOrder;
};

struct xJSPNodeTreeBranch
{
    U16 leftNode;
    U16 rightNode;
    U8 leftType;
    U8 rightType;
    U16 coord;
    F32 leftValue;
    F32 rightValue;
};

struct xJSPNodeTreeLeaf
{
    S32 nodeIndex;
    S32 leafCount;
    RwBBox box;
};

struct xJSPNodeTree
{
    S32 numBranchNodes;
    xJSPNodeTreeBranch* branchNodes;
    S32 numLeafNodes;
    xJSPNodeTreeLeaf* leafNodes;
};

struct xJSPHeader
{
    char idtag[4];
    U32 version;
    U32 jspNodeCount;
    RpClump* clump;
    xClumpCollBSPTree* colltree;
    xJSPNodeInfo* jspNodeList;
    U32 stripVecCount; // 0x18
    RwV3d* stripVecList; // 0x1c
    U16 vertDataFlags; // 0x20
    U16 vertDataStride; // 0x22
    xJSPNodeTree* nodetree; // 0x24
    struct xJSPNodeLight* nodelight; // 0x28
};

struct xJSPExtraInfo
{
    char idtag[4];
    xBox bounds;
};

struct xJSPHeaderEx : xJSPHeader
{
    xJSPExtraInfo extraInfo;
};

struct xJSPMiniLightTie
{
    RwLLLink lightInWorldSector;
    RpLight* light;
};

struct xJSPNodeLight
{
    RpAtomic* atomic;
    S32 lightCount;
    RpTie dummyTie;
    RpWorldSector dummySector;
    xJSPMiniLightTie dummyLightTie[16];
};

xJSPNodeTree* xJSPNodeTreeBuild(xJSPHeader* jsp);
void xJSPNodeLightBuild(xJSPHeader* jsp);
void xJSP_MultiStreamRead(void* data, U32 size, xJSPHeader** jsp, U32 assetid);
void xJSP_Destroy(xJSPHeader* jsp);
U32 get_matid(xJSPHeader* jsp, U32 old_mat_id, U32 jspAssetID);

#endif
