#ifndef ZREACTIVEANIMATION_H
#define ZREACTIVEANIMATION_H

#include <types.h>

#include "iSnd.h"
#include "xAnim.h"
#include "zNPC.h"
#include "zEntSimpleObj.h"

struct RpAtomic;
struct xSphere;
struct zEntSimpleObj;
struct zSimpleMgr;

struct zReactiveGenre
{
    RpAtomic* model_static;
    RpAtomic* model_bound;
    RpAtomic* model_burnt;
    iSndGroupHandle sound_idle;
    iSndGroupHandle sound_move_through;
    iSndGroupHandle sound_hit;
    xAnimTable* table;
    xAnimState* anim_idle;
    xAnimState* anim_move_through;
    xAnimState* anim_hit;
    xAnimState* anim_burn;
    F32 lod_dist2;
    F32 burn_fuel;
    F32 burn_flame_size;
    F32 burn_emit_scale;
    F32 move_through_radius2;
};

struct zReactiveAnimationData
{
    S32 kill_flags;
    zEntSimpleObj* owner;
    zReactiveGenre* genre;
    iSndHandle sound_idle_handle;
};

void zReactiveAddExtraSimpleMgr(zEntSimpleObj& ent, zSimpleMgr* smgr);
void zReactivePreBucket(zEntSimpleObj& ent);
bool zReactiveNeedExtraModel(zEntSimpleObj& ent);
S32 zReactiveCountExtraModels(zEntSimpleObj** ents, U32 size);
bool zReactiveSelectLOD(zEntSimpleObj& ent, F32 dist2);
void zReactiveDeactivate(zEntSimpleObj& ent, bool immediate);
void zReactiveDamage(zEntSimpleObj& ent, zHitSource hs, F32 damage);
S32 zReactiveHitTestSphere(zEntSimpleObj** hits, S32 max_hits, const xSphere& sphere);
void zReactiveRender();
void zReactiveUpdate(F32 dt);
void zReactiveReset();
void zReactiveSceneEnter();

inline U8 zReactiveIsReactive(zEntSimpleObj& ent)
{
    return (ent.sflags & 0x200) != 0;
}

inline U8 zReactiveIsReactive(xEnt& ent)
{
    if (ent.baseType != eBaseTypeStatic)
    {
        return FALSE;
    }

    zEntSimpleObj& simp = (zEntSimpleObj&)ent;
    return zReactiveIsReactive(simp);
}

#endif
