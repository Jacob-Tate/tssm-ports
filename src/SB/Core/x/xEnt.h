#ifndef XENT_H
#define XENT_H

#include <rwcore.h>
#include <rpworld.h>
#include <string.h>

#include "xBase.h"
#include "xMath3.h"
#include "xModel.h"
#include "xLightKit.h"
#include "xGrid.h"
#include "xBound.h"
#include "xFFX.h"
#include "xCollide.h"

struct xEntAsset : xBaseAsset
{
    // Offset: 0x8
    U8 flags;
    U8 subtype;
    U8 pflags;
    U8 moreFlags;
    //U8 pad;
    //U8 padding[3]; // this padding is added automatically. it should not be here

    // Offset: 0x10
    U32 surfaceID;

    // Offset: 0x14
    xVec3 ang;

    // Offset: 0x20
    xVec3 pos;

    // Offset: 0x2C
    xVec3 scale;

    // Offset: 0x38
    F32 redMult;
    F32 greenMult;
    F32 blueMult;
    F32 seeThru;

    // Offset: 0x48
    F32 seeThruSpeed;
    U32 modelInfoID;
    U32 animListID;
};

struct xEnt;
struct xScene;

struct xEntFrame
{
    xMat4x3 mat;

    // Offset: 0x40
    xMat4x3 oldmat;

    // Offset: 0x80
    xVec3 oldvel;

    // Offset: 0x8C
    xRot oldrot;

    // Offset: 0x9C
    xRot drot;
    xRot rot;

    xVec3 dvel;
    xVec3 vel;
    U32 mode;
    xVec3 dpos;
};

struct xEntCollis
{
    U8 chk;
    U8 pen;
    U8 env_sidx;
    U8 env_eidx;

    U8 npc_sidx;
    U8 npc_eidx;
    U8 dyn_sidx;
    U8 dyn_eidx;

    U8 stat_sidx;
    U8 stat_eidx;
    U8 idx;

    xCollis colls[18];
    void (*post)(xEnt*, xScene*, F32, xEntCollis*);
    U32 (*depenq)(xEnt*, xEnt*, xScene*, F32, xCollis*);
};

struct xShadowSimpleCache;
struct xEntShadow;

typedef void (*xEntUpdateCallback)(xEnt*, xScene*, F32);
typedef void (*xEntBoundUpdateCallback)(xEnt*, xVec3*);
typedef void (*xEntMoveCallback)(xEnt*, xScene*, F32, xEntFrame*);
typedef void (*xEntRenderCallback)(xEnt*);
typedef void (*xEntTranslateCallback)(xEnt*, xVec3*, xMat4x3*);

// Size: 0xD0
struct xEnt : xBase
{
    struct anim_coll_data
    {
        U32 flags;
        U32 bones;
        xMat4x3 old_mat;
        xMat4x3 new_mat;
        U32 verts_size;
        xVec3* verts;
        xVec3* normals;
    };

    xEntAsset* asset;
    U16 idx;
    U8 flags;
    U8 miscflags;
    U8 subType;
    U8 pflags;
    U16 moreFlags;
    struct
    {
        U8 _isCulled : 2;
        U8 collisionEventReceived : 2;
    };
    U8 driving_count;
    U8 num_ffx;
    U8 collType;
    U8 collLev;
    U8 chkby;
    U8 penby;
    void (*visUpdate)(xEnt*);
    xModelInstance* model;
    xModelInstance* collModel;
    xModelInstance* camcollModel;
    void (*update)(xEnt*, xScene*, F32);
    void (*endUpdate)(xEnt*, xScene*, F32);
    void (*bupdate)(xEnt*, xVec3*);
    void (*move)(xEnt*, xScene*, F32, xEntFrame*);
    void (*render)(xEnt*);
    xEntFrame* frame;
    xEntCollis* collis;
    xGridBound gridb;
    xBound bound;
    void (*transl)(xEnt*, xVec3*, xMat4x3*);
    xFFX* ffx;
    xEnt* driver;
    xEnt* driven;
    S32 driveMode;
    xShadowSimpleCache* simpShadow;
    xEntShadow* entShadow;
    anim_coll_data* anim_coll;
    void* user_data;
};

struct xEntOpacity
{
    F32 delta_opacity;
    F32 delta_opacity_2;
};

void xEntOpacityInit(xEntOpacity* opacity, xEnt* ent);
void xEntOpacityUpdate(xEntOpacity* opacity, xEnt* ent, F32 dt);

inline U32 xEntIsVisible(const xEnt* ent)
{
    return (ent->flags & 0x85) == 0x1;
}

// Entity flags (xEnt::flags), as in BfBB upstream's xEnt.h
#define XENT_IS_VISIBLE ((U8)(1 << 0))
#define XENT_IS_STACKED ((U8)(1 << 1))

// More flags (xEnt::moreFlags), as in BfBB upstream's xEnt.h (these shift an int)
#define XENT_MORE_FLAGS_0x8 ((U8)1 << 3)
#define XENT_MORE_FLAGS_HITTABLE ((U8)1 << 4)
#define XENT_MORE_FLAGS_ANIM_COLL ((U8)1 << 5)

// Physics flags (xEnt::pflags). U8-typed like BfBB upstream's xEnt.h: OR-ing a U8-typed
// constant into a U8 field schedules differently from a plain int literal (zEntButton_Init).
#define XENT_PFLAGS_IS_MOVING ((U8)(1 << 0))
#define XENT_PFLAGS_HAS_VELOCITY ((U8)(1 << 1))
#define XENT_PFLAGS_HAS_GRAVITY ((U8)(1 << 2))
#define XENT_PFLAGS_HAS_DRAG ((U8)(1 << 3))
#define XENT_PFLAGS_HAS_FRICTION ((U8)(1 << 4))

// Collision types (xEnt::collType, chkby, penby), U8-typed like BfBB upstream's xEnt.h
#define XENT_COLLTYPE_NONE ((U8)0)
#define XENT_COLLTYPE_TRIG ((U8)(1 << 0)) // trigger (TRIG)
#define XENT_COLLTYPE_STAT ((U8)(1 << 1)) // static (SIMP)
#define XENT_COLLTYPE_DYN ((U8)(1 << 2)) // dynamic (PLAT)
#define XENT_COLLTYPE_NPC ((U8)(1 << 3)) // npc/enemy (VIL)
#define XENT_COLLTYPE_PLYR ((U8)(1 << 4)) // player (PLYR)
#define XENT_COLLTYPE_ENV ((U8)(1 << 5)) // environment (ENV)

// Named after the weak xEntMoves(const xEnt&) of the Ratatouille GC builds (xEnt.h).
inline bool xEntMoves(const xEnt& ent)
{
    return (ent.pflags & 0x1) ||
           (ent.collType & (XENT_COLLTYPE_DYN | XENT_COLLTYPE_NPC | XENT_COLLTYPE_PLYR));
}

// Size: 0x40
struct xEntShadow
{
    enum radius_enum
    {
        RADIUS_CACHE,
        RADIUS_RASTER,
        MAX_RADIUS
    };

    xVec3 pos;
    xVec3 vec;
    RpAtomic* shadowModel;
    F32 dst_cast;
    F32 radius[2];
    struct
    {
        S32 flg_castOnOneDFF : 1;
        S32 flg_castOnAllDFF : 1;
        S32 flg_disableEnvCast : 1;
        S32 flg_shadowUnused : 29;
    };
};

extern S32 xent_entent;

void xEntEnable(xEnt* ent);
xVec3* xEntGetCenter(const xEnt* ent);
xVec3* xEntGetPos(const xEnt* ent);
void xEntHide(xEnt* ent);
void xEntShow(xEnt* ent);
void xEntInitShadow(xEnt& ent, xEntShadow& shadow);
void xEntReposition(xEnt& ent, const xMat4x3& mat);
bool xEntValidType(U8 type);
void xEntAnimateCollision(xEnt& ent, bool on);
xBox* xEntGetAllEntsBox();
void xEntSetNostepNormAngle(F32 angle);
void xEntCollideWalls(xEnt* p, xScene* sc, F32 dt);
void xEntCollideFloor(xEnt* p, xScene* sc, F32 dt);
xEnt* xEntCollCheckOneEntNoDepen(xEnt* ent, xScene* sc, void* data);
void xEntCollCheckNPCs(xEnt* p, xScene* sc, xEnt* (*hitIt)(xEnt*, xScene*, void*));
void xEntCollCheckDyns(xEnt* p, xScene* sc, xEnt* (*hitIt)(xEnt*, xScene*, void*));
void xEntCollCheckStats(xEnt* p, xScene* sc, xEnt* (*hitIt)(xEnt*, xScene*, void*));
void xEntCollCheckNPCsByGrid(xEnt* p, xScene* sc, xEnt* (*hitIt)(xEnt*, xScene*, void*));
void xEntCollCheckByGrid(xEnt* p, xScene* sc, xEnt* (*hitIt)(xEnt*, xScene*, void*));
void xEntCollCheckEnv(xEnt* p, xScene* sc);
void xEntEndCollide(xEnt* ent, xScene* sc, F32 dt);
void xEntBeginCollide(xEnt* ent, xScene* sc, F32 dt);
void xEntCollide(xEnt* ent, xScene* sc, F32 dt);
void xEntApplyPhysics(xEnt* ent, xScene* sc, F32 dt);
void xEntMove(xEnt* ent, xScene* sc, F32 dt);
void xEntMotionToMatrix(xEnt* ent, xEntFrame* frame);
void xEntDefaultTranslate(xEnt* ent, xVec3* dpos, xMat4x3* dmat);
void xEntDefaultBoundUpdate(xEnt* ent, xVec3* pos);
void xEntEndUpdate(xEnt* ent, xScene* sc, F32 dt);
void xEntBeginUpdate(xEnt* ent, xScene* sc, F32 dt);
void xEntUpdate(xEnt* ent, xScene* sc, F32 dt);
void xEntRender(xEnt* ent);
void xEntRestorePipeline(xSurface*, RpAtomic* model);
void xEntRestorePipeline(xModelInstance* model);
void xEntSetupPipeline(xSurface* surf, RpAtomic* model);
void xEntSetupPipeline(xModelInstance* model);

inline void xEntAddToPos(xEnt* ent, const xVec3* v)
{
    xVec3AddTo(&ent->frame->mat.pos, v);
}

xModelInstance* xEntLoadModel(xEnt* ent, RpAtomic* imodel, xModelInstance* reuse_instance);
void xEntReset(xEnt* ent);
void xEntLoad(xEnt* ent, xSerial* s);
void xEntSave(xEnt* ent, xSerial* s);
void xEntSetup(xEnt* ent);
void xEntInitForType(xEnt* ent);
void xEntInit(xEnt* ent, xEntAsset* asset);
void xEntAddHittableFlag(xEnt* ent);
void xEntSceneExit();
void xEntSceneInit();
void xEntGrowGlobalBoundingBox(const xVec3* pos);
void xEntStoreCheckPoint();
void xEntSetTimePassed(F32 sec);

// Needs to be [gw] but not inline?
inline void xEntHide(xEnt* ent)
{
    ent->flags &= ~0x1;

    if (ent->visUpdate)
    {
        ent->visUpdate(ent);
    }
}

inline void xEntShow(xEnt* ent)
{
    ent->flags |= 0x1;

    if (ent->visUpdate)
    {
        ent->visUpdate(ent);
    }
}

inline void xEntVisibilityCullOn(xEnt* ent)
{
    ent->flags |= 0x4;

    if (ent->visUpdate)
    {
        ent->visUpdate(ent);
    }
}

inline void xEntVisibilityCullOff(xEnt* ent)
{
    ent->flags &= (U8)~0x4;

    if (ent->visUpdate)
    {
        ent->visUpdate(ent);
    }
}

inline xMat4x3* xEntGetFrame(const xEnt* ent)
{
    return xModelGetFrame(ent->model);
}

inline xVec3* xEntGetPos(const xEnt* ent)
{
    return &xModelGetFrame(ent->model)->pos;
}

inline xVec3* xEntGetCenter(const xEnt* ent)
{
    return (xVec3*)xBoundCenter(&ent->bound);
}

#endif
