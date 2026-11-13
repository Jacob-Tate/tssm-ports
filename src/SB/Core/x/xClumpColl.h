#ifndef XCLUMPCOLL_H
#define XCLUMPCOLL_H

#include <types.h>

#include <rwcore.h>
#include <rpworld.h>
#include <rpcollis.h>

struct xClumpCollBSPBranchNode
{
    U32 leftInfo;
    U32 rightInfo;
    F32 leftValue;
    F32 rightValue;
};

struct xClumpCollBSPVertInfo
{
    U16 atomIndex;
    U16 meshVertIndex;
};

struct xClumpCollBSPTriangle
{
    union
    {
        xClumpCollBSPVertInfo i;
        U32 rawIdx;
        RwV3d* p;
    } v;
    U8 flags;
    U8 detailed_info_cache_index;
    U16 matIndex;
};

struct xClumpCollBSPTree
{
    S32 numBranchNodes;
    xClumpCollBSPBranchNode* branchNodes;
    S32 numTriangles;
    xClumpCollBSPTriangle* triangles;
};

struct xClumpCollV3dGradient
{
    F32 dydx;
    F32 dzdx;
    F32 dxdy;
    F32 dzdy;
    F32 dxdz;
    F32 dydz;
};

extern U8 xClumpColl_FilterFlags;
extern U8 xClumpColl_FilterFlagsAllMask;
extern U8 xClumpColl_FilterFlagsAllEqual;

// Name and signature from the Ratatouille GC DWARF (absent from the PS2 DWARF).
inline U8 xClumpColl_IsCollidableTriangle(U8 flags)
{
    return (flags & xClumpColl_FilterFlags) ||
           xClumpColl_FilterFlagsAllEqual == (flags & xClumpColl_FilterFlagsAllMask);
}

typedef S32 (*xClumpCollIntersectionCallback)(xClumpCollBSPTriangle*, void*);

struct nodeInfo
{
    U32 type;
    U32 index;
};
struct RwMeshCache
{
    U32 lengthOfMeshesArray;
    RwResEntry* meshes[1];
};

struct TempAtomicList
{
    RpAtomic* atomic;
    RpGeometry* geom;
    RpMeshHeader* meshHeader;
    RwMeshCache* meshCache;
};

xClumpCollBSPTree*
xClumpColl_ForAllIntersections(xClumpCollBSPTree* tree, RpIntersection* intersection,
                               RpCollisionTriangle* (*callBack)(RpIntersection*, RpWorldSector*,
                                                                RpCollisionTriangle*, F32, void*),
                               void* data);
xClumpCollBSPTree*
xClumpColl_ForAllCapsuleLeafNodeIntersections(xClumpCollBSPTree* tree, RwLine* line, F32 radius,
                                              xClumpCollV3dGradient* grad,
                                              xClumpCollIntersectionCallback callBack, void* data);
xClumpCollBSPTree*
xClumpColl_ForAllLineLeafNodeIntersections(xClumpCollBSPTree* tree, RwLine* line,
                                           xClumpCollV3dGradient* grad,
                                           xClumpCollIntersectionCallback callBack, void* data);
void xClumpColl_InstancePointers(xClumpCollBSPTree* tree, RpClump* clump);
xClumpCollBSPTree* xClumpColl_StaticBufferInit(void* data, U32 param_2);

#endif
