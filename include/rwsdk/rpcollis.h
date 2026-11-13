#ifndef RPCOLLIS_H
#define RPCOLLIS_H

#include "rwcore.h"
#include "rpworld.h"

typedef struct RpCollisionTriangle;

union RpIntersectData
{
    RwLine line;
    RwV3d point;
    RwSphere sphere;
    RwBBox box;
    void* object;
};

enum RpIntersectType
{
    rpINTERSECTNONE = 0,
    rpINTERSECTLINE,
    rpINTERSECTPOINT,
    rpINTERSECTSPHERE,
    rpINTERSECTBOX,
    rpINTERSECTATOMIC,
    rpINTERSECTTYPEFORCEENUMSIZEINT = RWFORCEENUMSIZEINT
};

struct RpIntersection
{
    RpIntersectData t;
    RpIntersectType type;
};

struct RpCollisionTriangle
{
    RwV3d normal;
    RwV3d point;
    RwInt32 index;
    RwV3d* vertices[3];
};

// RpCollTree (RW 3.6+), layouts from the PS2 DWARF of this game.
typedef struct RpCollTree RpCollTree;

struct RpCollSector
{
    RwUInt8 type;
    RwUInt8 contents;
    RwUInt16 index;
    RwReal value;
};

typedef struct RpCollSplit RpCollSplit;
struct RpCollSplit
{
    RpCollSector leftSector;
    RpCollSector rightSector;
};

struct RpCollTree
{
    RwUInt32 flags;
    RwBBox bbox;
    RwUInt16 numEntries;
    RwUInt16 numSplits;
    RpCollSplit* splits;
    RwUInt16* map;
};

#define rpCOLLTREE_MAXDEPTH 32

#define rpCOLLSECTOR_CONTENTS_SPLIT 0xff

#define rpCOLLSECTOR_TYPE_AXISMASK (0x08 | 0x04)

typedef struct RpCollSectorLineItNodeInfo RpCollSectorLineItNodeInfo;
struct RpCollSectorLineItNodeInfo
{
    RpCollSector* sector;
    RwReal pa; /* Start parameter for segment of line in sector */
    RwReal pb; /* End parameter for segment of line in sector */
};

#define RpCollTreeGetEntryMapMacro(_tree) ((_tree)->map)
#define RpCollTreeGetEntryMap(_tree) RpCollTreeGetEntryMapMacro(_tree)

typedef struct RpCollisionData RpCollisionData;
struct RpCollisionData
{
    RpCollTree* tree;
};

#define _rpCollisionGeometryGetDataMacro(_geometry)                                                \
    RWPLUGINOFFSET(RpCollisionData, _geometry, _rpCollisionGeometryDataOffset)

#define _rpCollisionWorldSectorGetDataMacro(_sector)                                               \
    RWPLUGINOFFSET(RpCollisionData, _sector, _rpCollisionWorldSectorDataOffset)

#define RpCollisionGeometryGetCollTree(_geometry)                                                  \
    (_rpCollisionGeometryGetDataMacro(_geometry)->tree)

#define RpCollisionWorldSectorGetCollTree(_sector)                                                 \
    (_rpCollisionWorldSectorGetDataMacro(_sector)->tree)

typedef RpCollisionTriangle* (*RpIntersectionCallBackGeometryTriangle)(
    RpIntersection* intersection, RpCollisionTriangle* collTriangle, RwReal distance, void* data);

typedef RpCollisionTriangle* (*RpIntersectionCallBackWorldTriangle)(
    RpIntersection* intersection, RpWorldSector* sector, RpCollisionTriangle* collTriangle,
    RwReal distance, void* data);

#ifdef __cplusplus
extern "C" {
#endif

extern RwInt32 _rpCollisionGeometryDataOffset;
extern RwInt32 _rpCollisionWorldSectorDataOffset;

extern RpAtomic* RpAtomicForAllIntersections(RpAtomic* atomic, RpIntersection* intersection,
                                             RpIntersectionCallBackGeometryTriangle callBack,
                                             void* data);
extern RpWorld* RpCollisionWorldForAllIntersections(RpWorld* world, RpIntersection* intersection,
                                                    RpIntersectionCallBackWorldTriangle callBack,
                                                    void* data);
extern RwBool RpCollisionPluginAttach(void);

#ifdef __cplusplus
}
#endif

#endif