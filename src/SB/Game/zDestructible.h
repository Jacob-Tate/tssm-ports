#ifndef ZDESTRUCTIBLE_H
#define ZDESTRUCTIBLE_H

#include "xEnt.h"
#include "iSnd.h"

struct zShrapnelAsset;
struct zParEmitter;
namespace xRumble
{
    struct sphericalEmitter;
}

struct xDestructibleAssetAttachedAnimList
{
    U32 nanimations;
    U32* animationIDs;
};

// Size: 0x34. In the asset data the animation list is stored inline at 'animlist' (count then
// IDs), and the next state follows the last ID.
struct xDestructibleAssetState
{
    U32 percent;
    U32 modelID;
    U32 shrapnelID;
    U32 shrapnelhitID;
    U32 soundgroupidleID;
    U32 soundgroupfxID;
    U32 soundgrouphitID;
    U32 soundgroupfxIDswitch;
    U32 soundgrouphitIDswitch;
    U32 rumbleIDhit; // 0x24
    U32 rumbleIDswitch; // 0x28
    U32 fx_flags; // 0x2C
    xDestructibleAssetAttachedAnimList* animlist; // 0x30
};

// Size: 0x2C. The states are stored inline starting at 'states'.
struct xDestructibleAsset
{
    U32 id;
    U32 nstates;
    U32 hit_points;
    U32 hit_filter;
    U32 launch_flag;
    U32 behaviour;
    U32 flags;
    U32 soundgroupidleID;
    F32 respawn;
    U8 target_priority;
    xDestructibleAssetState* states; // 0x28
};

struct zDestructibleStateFXParams
{
    F32 timer;
    U32 flags;
    zParEmitter* emitter;
    U32 (*fn)(F32);
};

struct zDestructibleStateFXList
{
    U32 nfx;
    zDestructibleStateFXParams* fxParams;
};

struct zDestructibleStateAttachedAnimList
{
    U32 nanimations;
    void** animData;
};

// Size: 0x44
struct zDestructibleState
{
    xModelInstance* model;
    zShrapnelAsset* shrapnel;
    zShrapnelAsset* shrapnelhit;
    U32 percent;
    iSndGroupHandle sg_handle_idle;
    iSndGroupHandle sg_handle_hit;
    iSndGroupHandle sg_handle_hit_switch;
    iSndHandle sh_handle_idle;
    iSndHandle sh_handle_hit;
    iSndHandle sh_handle_hit_switch;
    U32 sg_fx_id;
    U32 sg_fx_id_switch;
    xRumble::sphericalEmitter* rumbleHit; // 0x30
    xRumble::sphericalEmitter* rumbleSwitch; // 0x34
    zDestructibleStateAttachedAnimList* animList; // 0x38
    zDestructibleStateFXList fxList; // 0x3C
};

struct zDestructible
{
    xDestructibleAsset* asset;
    U32 id;
    U32 cur_hit_pts;
    U32 curstateidx;
    U32 nstates;
    U32 hit_pts;
    U32 hit_filter;
    U32 launch_flag;
    U32 behaviour;
    U32 base_modelID; // 0x24
    iSndGroupHandle sg_handle_idle;
    iSndHandle sh_handle_idle;
    xModelInstance* base_model; // 0x30
    xModelInstance* base_collision;
    zDestructibleState* dstates;
    F32 hit_to_destroy_timer;
    F32 timer;
    F32 respawn_time;
    void (*destroy_notify)(zDestructible*, void*);
    void* notify_context;
    xEnt* root_ent;
    U32 flags;
    U8 has_animation; // 0x58
    U8 active;
};

zDestructible* zDestructibleGetFromEntity(xEnt* ent);
void zDestructibleAsset_Update(xEnt* ent, zDestructible* dest, xScene* sc, F32 dt);
void zDestructibleAsset_EventCB(xBase* from, xBase* root_base, zDestructible* to, U32 toEvent,
                                const F32* toParam, xBase* toParamWidget, U32 toParamWidgetID);
void zDestructibleAsset_CopyLODDistances(xEnt* ent, zDestructible* destructible);
void zDestructibleAsset_Init(U32 assetID, zDestructible*& dest, xEnt* ent, U32);
void zDestructibleAsset_ReReset(zDestructible*& dest, xEnt* ent);
void zDestructibleAsset_Reset(zDestructible*& dest, xEnt* ent);

#endif
