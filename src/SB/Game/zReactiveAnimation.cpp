#include "zReactiveAnimation.h"

#include <types.h>

#include "xAnim.h"
#include "xBound.h"
#include "xGrid.h"
#include "xMemMgr.h"
#include "xModel.h"
#include "xModelBucket.h"
#include "xQuickCull.h"
#include "xSnd.h"
#include "xString.h"
#include "xstransvc.h"

#include "zBase.h"
#include "zEntSimpleObj.h"
#include "zFire.h"
#include "zGlobals.h"
#include "zGrid.h"
#include "zScene.h"

struct xTableAsset : xBaseAsset
{
    U32 version;
    U32 numRows;
};

static bool sAllowReactiveAnims;

namespace
{
    struct asset_entry
    {
        U32 model_static;
        U32 model_bound;
        F32 lod_dist;
        U32 anim_idle;
        U32 anim_move_through;
        U32 anim_hit;
        U32 sound_idle;
        U32 sound_move_through;
        U32 sound_hit;
        U32 model_burnt;
        U32 anim_burn;
        F32 burn_fuel;
        F32 burn_flame_size;
        F32 burn_emit_scale;
        F32 move_through_radius;
    };

    struct asset_type : xTableAsset
    {
        asset_entry entries[0];
    };

    struct check_bound
    {
        zEntSimpleObj** hit;
        zEntSimpleObj** end_hit;
        xBound& bound;

        check_bound(zEntSimpleObj** hit, zEntSimpleObj** end_hit, xBound& bound)
            : hit(hit), end_hit(end_hit), bound(bound)
        {
        }

        bool operator()(xEnt& ent)
        {
            if (hit == end_hit)
            {
                return false;
            }

            if (ent.baseType == eBaseTypeStatic && (((zEntSimpleObj&)ent).sflags & 0x200) &&
                xBoundHitsBound(bound, ent.bound))
            {
                *hit = (zEntSimpleObj*)&ent;
                hit++;
            }

            return true;
        }
    };

    S32 total_reactives;
    zReactiveGenre* genres;
    S32 genres_size;
    zReactiveAnimationData* activities;
    S32 activities_size;
    S32 activities_used;

    void animate_hit(zEntSimpleObj& ent);
    void apply_fire_damage(zEntSimpleObj& ent, F32 damage);
    bool update_activity(zReactiveAnimationData& react, F32 dt);
    void deactivate_entity(zEntSimpleObj& ent);
    void load_genres();
    void load_genre(zReactiveGenre& genre, const asset_entry& entry);
    U32 AlwaysConditional(xAnimTransition*, xAnimSingle*, void*);

    // Switches the entity's model instance over to another atomic, picking up its render buckets
    // and pipeline settings. swap_model, set_static_model and set_burnt_model are always
    // inlined; their names are from Ratatouille.
    inline void swap_model(zEntSimpleObj& ent, RpAtomic* atomic)
    {
        if (ent.model->Data == atomic)
        {
            return;
        }

        xModelInstance* model = ent.model;

        model->Data = atomic;
        model->Bucket = xModelBucket_GetBuckets(atomic);

        if (model->Bucket)
        {
            model->Pipe = model->Bucket[0]->Pipe;
        }
        else
        {
            model->Pipe = xModelGetPipe(atomic);
        }

        if (!model->Pipe.Layer)
        {
            model->Pipe.Layer = 24;
        }
    }

    // Swaps the active simple manager with the extra one, carrying the visibility over.
    inline void swap_simple_mgr(zEntSimpleObj& ent)
    {
        zSimpleMgr* temp = ent.smgr;
        U32 visible = temp->flags & 0x1;

        ent.smgr = ent.smgrExtra;
        ent.smgrExtra = temp;
        ent.smgr->flags |= visible;
        ent.smgrExtra->flags &= ~0x1;
    }

    // Releases an activity slot by moving the last active slot into it.
    inline void free_activity(zReactiveAnimationData* act)
    {
        activities_used--;
        zReactiveAnimationData* tail_act = activities + activities_used;
        if (tail_act != act)
        {
            *act = *tail_act;
            act->owner->reactiveAnimationData = act;
        }
    }

    inline void set_static_model(zEntSimpleObj& ent, zReactiveGenre& genre)
    {
        if (ent.model->Data == genre.model_static)
        {
            return;
        }

        swap_model(ent, genre.model_static);
        ent.model->RedMultiplier = ent.model->GreenMultiplier = ent.model->BlueMultiplier = 1.0f;
        ent.model->Flags &= (U16)~0x4000;
        swap_simple_mgr(ent);
    }

    inline void set_burnt_model(zEntSimpleObj& ent)
    {
        zReactiveGenre& genre = *ent.reactGenre;
        if (!genre.model_burnt)
        {
            return;
        }

        swap_model(ent, genre.model_burnt);
        ent.model->RedMultiplier = ent.model->GreenMultiplier = ent.model->BlueMultiplier = 0.4f;
        ent.model->Flags |= 0x4000;
        swap_simple_mgr(ent);
    }

    inline void setup_entity(zEntSimpleObj& ent, zReactiveGenre& genre)
    {
        ent.atbl = genre.table;
        ent.sflags |= 0x200;
        ent.moreFlags |= 0x10;
        ent.collType = 0;
        ent.chkby = 0;
        ent.collLev = 4;
        ent.reactiveAnimationData = NULL;
        ent.reactGenre = &genre;
    }

    inline zReactiveGenre* find_genre(zEntSimpleObj& ent)
    {
        if (!ent.model)
        {
            return NULL;
        }

        RpAtomic* model = ent.model->Data;
        zReactiveGenre* it = genres;
        zReactiveGenre* end = genres + genres_size;

        for (; it != end; ++it)
        {
            if (model == it->model_static)
            {
                return it;
            }
        }

        return NULL;
    }

    inline void setup_entities()
    {
        S32 total = 0;
        zScene& scene = *globals.sceneCur;
        zEntSimpleObj* it = (zEntSimpleObj*)scene.baseList[eBaseTypeStatic];
        zEntSimpleObj* end = it + scene.baseCount[eBaseTypeStatic];

        for (; it != end; ++it)
        {
            zReactiveGenre* genre = find_genre(*it);
            if (genre)
            {
                total++;
                setup_entity(*it, *genre);
            }
        }

        total_reactives = total;
        activities_size = total;

        if (activities_size > 0)
        {
            if (activities_size > 64)
            {
                activities_size = 64;
            }

            activities = (zReactiveAnimationData*)xMemAlloc(
                gActiveHeap, activities_size * sizeof(zReactiveAnimationData), 0);
        }
    }

    // activate_entity and play_anim are out of line in the PS2 build, but the GC object has no
    // bodies for them (every call is inlined).
    inline bool activate_entity(zEntSimpleObj& ent, zReactiveGenre& genre)
    {
        if (ent.reactiveAnimationData)
        {
            return true;
        }

        if (activities_used >= activities_size)
        {
            return false;
        }

        xModelInstance* model = xModelInstanceAlloc(genre.model_bound, &ent, 0, 0, NULL);
        if (!model)
        {
            return false;
        }

        xAnimPlay* play = xAnimPoolAlloc(&globals.sceneCur->mempool, &ent, ent.atbl, model);
        if (!play)
        {
            xModelInstanceFree(model);
            return false;
        }

        ent.model = model;
        ent.model->LightKit = ent.smgr->model->LightKit;
        *ent.model->Mat = *ent.smgr->model->Mat;
        xModelUpdate(ent.model, 0.0f);
        xModelEval(ent.model);

        zReactiveAnimationData& react = activities[activities_used];
        activities_used++;
        react.kill_flags = 0;
        react.owner = &ent;
        react.genre = &genre;
        react.sound_idle_handle = (iSndHandle)-1;

        if (!genre.anim_idle)
        {
            react.kill_flags |= 0x2;
        }

        ent.reactiveAnimationData = &react;
        return true;
    }

    // Makes sure the entity has its animated model, unless it has already burnt down.
    // Returns false if the entity cannot be animated (burnt, burning, or out of resources).
    // Inlined everywhere; name and signature from Ratatouille's DWARF.
    inline bool activate_if_possible(zEntSimpleObj& ent, zReactiveGenre& genre)
    {
        if (!ent.reactiveAnimationData)
        {
            if (ent.model->Data == genre.model_burnt)
            {
                return false;
            }

            if (!activate_entity(ent, genre))
            {
                return false;
            }
        }
        else if (ent.model->Anim->Single->State == genre.anim_burn)
        {
            return false;
        }

        return true;
    }

    inline void play_anim(zEntSimpleObj& ent, xAnimState* state, F32 speed, bool force_restart)
    {
        xModelInstance* model = ent.model;
        xAnimPlay* play = model->Anim;
        if (!play)
        {
            return;
        }

        xAnimSingle* single = play->Single;
        if (single->State == state)
        {
            return;
        }

        // NOTE: never true, the check above already returned for this state, so force_restart has
        // no effect (the PS2 build and both GC call sites show the same redundant pair of checks).
        if (single->State == state && !force_restart)
        {
            return;
        }

        xAnimTransition* tran = xAnimTempTransitionAlloc(NULL);
        if (!tran)
        {
            return;
        }

        tran->Dest = state;
        tran->Callback = NULL;
        tran->SrcTime = 0.0f;
        tran->DestTime = 0.0f;
        tran->BlendRecip = 4.0f;
        tran->BlendOffset = NULL;
        xAnimPlayStartTransition(model->Anim, tran);
        single->CurrentSpeed *= speed;

        F32 params[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
        zEntEvent(&ent, eEventAnimPlay, params);
    }
} // namespace

namespace
{

    inline xAnimState* add_anim_state(xAnimTable* table, U32 id, const char* name,
                                      xAnimState* transitionTo, bool loop)
    {
        if (!id)
        {
            return NULL;
        }

        void* anim_data = xSTFindAsset(xStrHashCat(id, ".anm"), NULL);
        if (!anim_data)
        {
            return NULL;
        }

        xAnimFile* afile = xAnimFileNew(anim_data, "", 0, NULL);

        U32 end = loop ? 0x10 : (transitionTo ? 0x20 : 0);

        xAnimState* state = xAnimTableNewState(table, name, end, 0, 1.0f, NULL, NULL, 0.0f, NULL,
                                               NULL, xAnimDefaultBeforeEnter, NULL, NULL);
        xAnimTableAddFile(table, afile, name);

        if (transitionTo)
        {
            xAnimTableNewTransition(table, name, transitionTo->Name, AlwaysConditional, NULL, 0x10,
                                    0, 0.0f, 0.0f, 0, 0, 0.0f, NULL);
        }

        return state;
    }
} // namespace

namespace
{
    U32 AlwaysConditional(xAnimTransition*, xAnimSingle*, void*)
    {
        return 1;
    }

    void load_genre(zReactiveGenre& genre, const asset_entry& entry)
    {
        U32 model_static_id;
        genre.model_static = xModelFindAtomic(entry.model_static, &model_static_id);
        genre.model_bound = xModelFindAtomic(entry.model_bound, NULL);
        genre.model_burnt = xModelFindAtomic(entry.model_burnt, NULL);
        genre.sound_idle = xSndMgrGetSoundGroup(entry.sound_idle);
        genre.sound_move_through = xSndMgrGetSoundGroup(entry.sound_move_through);
        genre.sound_hit = xSndMgrGetSoundGroup(entry.sound_hit);

        genre.table = xAnimTableNew("", 0);
        genre.anim_idle = add_anim_state(genre.table, entry.anim_idle, "ReactiveIdle", NULL, true);
        genre.anim_move_through = add_anim_state(genre.table, entry.anim_move_through,
                                                 "MoveThrough", genre.anim_idle, false);
        genre.anim_hit = add_anim_state(genre.table, entry.anim_hit, "Hit", genre.anim_idle, false);
        genre.anim_burn = add_anim_state(genre.table, entry.anim_burn, "Burn", NULL, false);

        genre.lod_dist2 = entry.lod_dist * entry.lod_dist;
        genre.burn_fuel = entry.burn_fuel;
        genre.burn_flame_size = entry.burn_flame_size;
        genre.burn_emit_scale = entry.burn_emit_scale;
        genre.move_through_radius2 = entry.move_through_radius * entry.move_through_radius;

        if (genre.anim_burn)
        {
            genre.anim_burn->Speed = genre.anim_burn->Data->Duration / genre.burn_fuel;
        }
    }

    void load_genres()
    {
        S32 tables = xSTAssetCountByType('RANM');
        genres_size = 0;

        // NOTE: the original always looks up table 0, so every loop iteration reads the same
        // table (only matters if a scene has more than one RANM asset).
        for (S32 i = 0; i < tables; i++)
        {
            U32 asset_size;
            asset_type* asset = (asset_type*)xSTFindAssetByType('RANM', 0, &asset_size);
            genres_size += asset->numRows;
        }

        if (genres_size > 0)
        {
            genres = new (gActiveHeap) zReactiveGenre[genres_size];
            zReactiveGenre* genre = genres;

            for (S32 i = 0; i < tables; i++)
            {
                U32 asset_size;
                asset_type* asset = (asset_type*)xSTFindAssetByType('RANM', 0, &asset_size);

                for (S32 j = 0; j < (S32)asset->numRows; j++)
                {
                    load_genre(*genre, asset->entries[j]);
                    genre++;
                }
            }
        }
    }

    // No PS2 DWARF entry (inlined there); the locals follow Ratatouille's DWARF.
    void deactivate_entity(zEntSimpleObj& ent)
    {
        if (!ent.reactiveAnimationData)
        {
            return;
        }

        zReactiveGenre& genre = *ent.reactGenre;
        zReactiveAnimationData& react = *ent.reactiveAnimationData;
        bool burning = ent.model->Anim->Single->State == genre.anim_burn;

        xModelInstanceFree(ent.model);
        ent.model = ent.smgr->model;

        if (react.sound_idle_handle != -1)
        {
            xSndMgrStop(react.sound_idle_handle);
        }

        free_activity(&react);
        ent.reactiveAnimationData = NULL;

        if (burning)
        {
            set_burnt_model(ent);
        }
    }

    bool update_activity(zReactiveAnimationData& react, F32 dt)
    {
        zReactiveGenre& genre = *react.genre;
        zEntSimpleObj& ent = *react.owner;
        xModelInstance* model = ent.model;
        xAnimSingle* single = model->Anim->Single;
        xAnimState* state = single->State;
        F32 time = single->Time;
        F32 duration = state->Data->Duration;
        F32 time_left = duration - time;

        if (react.kill_flags && dt >= time_left)
        {
            deactivate_entity(ent);
            return false;
        }

        if (state == genre.anim_burn)
        {
            model->RedMultiplier = model->GreenMultiplier = model->BlueMultiplier =
                1.0f - 0.6f * (time / duration);
        }
        else if (state == genre.anim_idle && genre.sound_idle != -1 &&
                 react.sound_idle_handle == -1)
        {
            react.sound_idle_handle =
                xSndMgrPlay(genre.sound_idle, 0, &ent.frame->mat.pos, NULL, NULL, NULL, NULL);
        }

        xModelUpdate(ent.model, dt);
        xModelEval(ent.model);
        return true;
    }

    // The GC build takes the damage amount, but any damage sets the entity on fire.
    void apply_fire_damage(zEntSimpleObj& ent, F32 damage)
    {
        zReactiveGenre& genre = *ent.reactGenre;
        if (!genre.model_burnt)
        {
            return;
        }

        if (!activate_if_possible(ent, genre))
        {
            return;
        }

        ent.reactiveAnimationData->kill_flags |= 0x4;
        ent.model->Flags |= 0x4000;
        ent.model->RedMultiplier = ent.model->GreenMultiplier = ent.model->BlueMultiplier = 1.0f;
        play_anim(ent, genre.anim_burn, 1.0f, false);

        static zFire::property_set fire_props;
        fire_props.damage = 0.0f;
        fire_props.knockback = 0.0f;

        zFire::spawn_data fire_spawn;
        fire_spawn.flags = 0x21;
        fire_spawn.up.assign(0.0f, 1.0f, 0.0f);
        fire_spawn.radius = genre.burn_flame_size;
        fire_spawn.fuel = genre.burn_fuel;
        fire_spawn.heat = 0.0f;
        fire_spawn.heat_absorb = 0.0f;
        fire_spawn.props = &fire_props;

        zFire::emit_sphere(fire_spawn, ent.bound.sph.center,
                           ent.model->Scale.x == 0.0f ? genre.burn_emit_scale :
                                                        ent.model->Scale.x * genre.burn_emit_scale);
    }

    void animate_hit(zEntSimpleObj& ent)
    {
        zReactiveGenre& genre = *ent.reactGenre;
        if (genre.anim_hit && activate_if_possible(ent, genre))
        {
            play_anim(ent, genre.anim_hit, 1.0f, true);
            xSndMgrPlay(genre.sound_hit, 0, NULL, NULL, NULL, NULL, NULL);
        }
    }
} // namespace

void zReactiveSceneEnter()
{
    activities_used = 0;
    activities_size = 0;
    load_genres();

    if (genres_size > 0)
    {
        setup_entities();

        if (activities_size > 0)
        {
            sAllowReactiveAnims = true;

            switch (globals.sceneCur->sceneID)
            {
            case 0:
                sAllowReactiveAnims = false;
                break;
            }
        }
    }
}

void zReactiveReset()
{
    zReactiveAnimationData* act = activities;
    zReactiveAnimationData* end_act = act + activities_used;

    for (; act != end_act; ++act)
    {
        deactivate_entity(*act->owner);
    }

    activities_used = 0;

    zScene& scene = *globals.sceneCur;
    zEntSimpleObj* it = (zEntSimpleObj*)scene.baseList[eBaseTypeStatic];
    zEntSimpleObj* end = it + scene.baseCount[eBaseTypeStatic];

    for (; it != end; ++it)
    {
        zReactiveGenre* genre = it->reactGenre;
        if (genre)
        {
            setup_entity(*it, *genre);
            set_static_model(*it, *genre);
        }
    }
}

void zReactiveUpdate(F32 dt)
{
    zReactiveAnimationData* act = activities;
    zReactiveAnimationData* end_act = act + activities_used;

    while (act != end_act)
    {
        if (update_activity(*act, dt))
        {
            ++act;
        }
        else
        {
            --end_act;
        }
    }
}

void zReactiveRender()
{
    zReactiveAnimationData* act = activities;
    zReactiveAnimationData* end_act = act + activities_used;

    for (; act != end_act; ++act)
    {
        xModelRender(act->owner->model);
    }
}

S32 zReactiveHitTestSphere(zEntSimpleObj** hits, S32 max_hits, const xSphere& sphere)
{
    xBound bound;
    xBoundFromSphere(bound, sphere.center, sphere.r);

    // not in the DWARF: grid callback object passed by reference.
    check_bound cb(hits, hits + max_hits, bound);
    xGridCheckBound(colls_grid, bound, bound.qcd, cb);
    xGridCheckBound(colls_oso_grid, bound, bound.qcd, cb);

    return cb.hit - hits;
}

void zReactiveDamage(zEntSimpleObj& ent, zHitSource hs, F32 damage)
{
    switch (hs)
    {
    case zHS_PROJECTILE:
    case zHS_EXPLOSION:
    case zHS_LASER:
    case zHS_ENERGY:
    case zHS_FIRE:
    case zHS_LASERBEAM:
        if (damage > 0.0f)
        {
            apply_fire_damage(ent, damage);
        }
        break;
    default:
        animate_hit(ent);
        break;
    }
}

void zReactiveDeactivate(zEntSimpleObj& ent, bool immediate)
{
    if (!ent.reactiveAnimationData)
    {
        return;
    }

    xAnimState* state = ent.model->Anim->Single->State;
    zReactiveGenre& genre = *ent.reactGenre;
    if (immediate && state != genre.anim_burn)
    {
        deactivate_entity(ent);
    }
    else
    {
        ent.reactiveAnimationData->kill_flags |= 0x8;
    }
}

bool zReactiveSelectLOD(zEntSimpleObj& ent, F32 dist2)
{
    zReactiveGenre& genre = *ent.reactGenre;
    bool was_active = ent.reactiveAnimationData != NULL;
    bool active = dist2 <= genre.lod_dist2 / 2.0f;
    if (!sAllowReactiveAnims)
    {
        active = false;
    }

    if (!was_active && !active)
    {
        return false;
    }

    if (!active)
    {
        ent.reactiveAnimationData->kill_flags |= 0x1;
        return true;
    }

    if (!was_active && !activate_if_possible(ent, genre))
    {
        return false;
    }

    ent.reactiveAnimationData->kill_flags &= ~0x9;
    return true;
}

S32 zReactiveCountExtraModels(zEntSimpleObj** ents, U32 size)
{
    S32 total = 0;
    zEntSimpleObj** it = ents;
    zEntSimpleObj** end = ents + size;

    for (; it != end; ++it)
    {
        zEntSimpleObj& ent = **it;
        if ((ent.sflags & 0x200) && zReactiveNeedExtraModel(ent))
        {
            total++;
        }
    }

    return total;
}

bool zReactiveNeedExtraModel(zEntSimpleObj& ent)
{
    return ent.reactGenre->model_burnt != NULL;
}

void zReactivePreBucket(zEntSimpleObj& ent)
{
    if (!ent.reactGenre->model_burnt)
    {
        return;
    }

    if (ent.model->Data == ent.reactGenre->model_static)
    {
        swap_model(ent, ent.reactGenre->model_burnt);
    }
    else
    {
        swap_model(ent, ent.reactGenre->model_static);
    }
}

void zReactiveAddExtraSimpleMgr(zEntSimpleObj& ent, zSimpleMgr* smgr)
{
    zSimpleMgr* smgr_static;
    zSimpleMgr* smgr_burnt;
    U32 visible;

    if (smgr->lodAtomic[0] == ent.reactGenre->model_static)
    {
        smgr_static = smgr;
        smgr_burnt = ent.smgr;
    }
    else
    {
        smgr_static = ent.smgr;
        smgr_burnt = smgr;
    }

    visible = ent.smgr->flags & 0x1;

    ent.smgr = smgr_static;
    ent.smgrExtra = smgr_burnt;
    ent.smgr->flags = (ent.smgr->flags & ~0x1) | visible;
    ent.smgrExtra->flags &= ~0x1;
    ent.smgr->model = ent.smgrExtra->model = ent.model;
    ent.smgr->mat = ent.smgrExtra->mat = ent.model->Mat;
}
