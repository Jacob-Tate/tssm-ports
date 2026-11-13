#include "zDiscoFloor.h"

#include "zGlobals.h"
#include "zGrid.h"
#include "zSurface.h"

#include "xDebug.h"
#include "xDebugTweak.h"
#include "xMath.h"
#include "xMemMgr.h"
#include "xModel.h"
#include "xRand.h"
#include "xString.h"
#include "xstransvc.h"
#include "zScene.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace
{
    U32 current_disco_floor;

    inline size_t state_byte_size(size_t mask_size)
    {
        return ((mask_size << 1) + 7) >> 3;
    }

    inline U32 get_tile(const U8* mask, size_t index)
    {
        size_t bit = index << 1;
        U32 tile = (mask[bit >> 3] >> (bit & 7)) & 0x3;

        if (tile == 2)
        {
            static S32 bit_index = 32;
            static U32 r;

            bit_index++;

            if (bit_index >= 32)
            {
                bit_index = 0;
                r = xrand_GenRandInt32();
            }

            tile = (r & (1 << bit_index)) != 0;
        }

        return tile;
    }

    inline void set_tile(U8* mask, size_t index, U32 tile)
    {
        size_t bit = index << 1;
        size_t byte = bit >> 3;
        size_t shift = bit & 7;

        mask[byte] = (mask[byte] & ~(0x3 << shift)) | (tile << shift);
    }

    inline void translate_mask(U8* dst, const U8* src, size_t size)
    {
        for (size_t i = 0; i < size; i++)
        {
            set_tile(dst, i, get_tile(src, i));
        }
    }

    void event_handler(xBase*, xBase* to, U32 event, const F32* argf, xBase*, U32)
    {
        z_disco_floor& f = *(z_disco_floor*)to;

        switch (event)
        {
        case eEventReset:
            f.reset();
            break;
        case eEventEnable:
            f.enabled = true;
            break;
        case eEventDisable:
            f.enabled = false;
            break;
        case eEventSetState:
            f.set_state(argf[0], false);
            break;
        case eEventSetStateRange:
            f.set_state_range(argf[0], argf[1], false);
            break;
        case eEventSetStateDelay:
            f.state_delay = argf[0];
            break;
        case eEventSetTransitionDelay:
            f.transition_delay = argf[0];
            break;
        }
    }

    void object_prefix(const char* name, size_t& size, size_t& first)
    {
        first = size = 0;

        size_t namelen = strlen(name);

        if (namelen)
        {
            for (S32 i = namelen - 1; i >= 0; i--)
            {
                if (name[i] < '0' || name[i] > '9')
                {
                    size = i + 1;

                    if (size < namelen)
                    {
                        first = atoi(name + size);
                    }

                    break;
                }
            }
        }
    }

    xEnt* find_object(const char* name)
    {
        // Not in the PS2 DWARF (always inlined). Retail's register allocation in setup needs base
        // and object as two named values.
        xBase* base = zSceneFindObject(xStrHash(name));
        xEnt* object = (xEnt*)base;

        if (!object)
        {
            object = NULL;
        }

        return object;
    }

    size_t find_objects(const char* name, xEnt** objects, size_t size)
    {
        char buffer[256];
        size_t prefix_size, start;

        object_prefix(name, prefix_size, start);
        memcpy(buffer, name, prefix_size);

        size_t i = start;
        size_t total;

        while (true)
        {
            total = i - start;

            if (total >= size)
            {
                return total;
            }

            objects[total] = NULL;

            for (S32 j = 1; j < 5; j++)
            {
                sprintf(buffer + prefix_size, "%0*d", j, i);

                objects[total] = find_object(buffer);

                if (objects[total])
                {
                    break;
                }
            }

            if (!objects[total])
            {
                return total;
            }

            i++;
        }
    }

    inline void set_object_state(const z_disco_floor& df, size_t index, S32 state)
    {
        xEnt* off = df.object_list.off[index];
        xEnt* transition = df.object_list.transition[index];
        xEnt* on = df.object_list.on[index];

        if (state == 0)
        {
            xEntShow(off);
        }
        else
        {
            xEntHide(off);
        }

        if (state == 1)
        {
            xEntShow(transition);
        }
        else
        {
            xEntHide(transition);
        }

        if (state == 2)
        {
            xEntShow(on);
        }
        else
        {
            xEntHide(on);
        }

        {
            static const U8 off_flag[3] = { 0, 0x4, 0x4 };
            static const U8 transition_flag[3] = { 0x4, 0, 0x4 };
            static const U8 on_flag[3] = { 0x4, 0x4, 0 };
            static const U8 mask = 0x4;

            // moreFlags is U16 here, but retail truncates the new value to 8 bits
            off->moreFlags = (U8)(off_flag[state] | (off->moreFlags & ~mask));
            transition->moreFlags = (U8)(transition_flag[state] | (transition->moreFlags & ~mask));
            on->moreFlags = (U8)(on_flag[state] | (on->moreFlags & ~mask));
        }

        {
            static const U8 off_flag[3] = { XENT_COLLTYPE_NPC | XENT_COLLTYPE_PLYR, 0, 0 };
            static const U8 transition_flag[3] = { 0, XENT_COLLTYPE_NPC | XENT_COLLTYPE_PLYR, 0 };
            static const U8 on_flag[3] = { 0, 0, XENT_COLLTYPE_NPC | XENT_COLLTYPE_PLYR };
            static const U8 mask = XENT_COLLTYPE_NPC | XENT_COLLTYPE_PLYR;

            off->chkby = off_flag[state] | (off->chkby & ~mask);
            transition->chkby = transition_flag[state] | (transition->chkby & ~mask);
            on->chkby = on_flag[state] | (on->chkby & ~mask);
        }

        zGridUpdateEnt(off);
        zGridUpdateEnt(transition);
        zGridUpdateEnt(on);
    }

    void refresh_state(z_disco_floor& df)
    {
        current_disco_floor = df.id;

        if (df.next_state != df.state)
        {
            for (U32 i = 0; i < df.object_list.size; i++)
            {
                U32 t1 = get_tile(df.active_state_mask, i);
                U32 t2 = get_tile(df.next_state_mask, i);

                if (t2 == 0)
                {
                    set_object_state(df, i, 0);
                }
                else if (t1 == 1)
                {
                    set_object_state(df, i, 2);
                }
                else
                {
                    set_object_state(df, i, 1);
                }
            }
        }
        else
        {
            for (U32 i = 0; i < df.object_list.size; i++)
            {
                U32 t = get_tile(df.active_state_mask, i);

                if (t == 1)
                {
                    set_object_state(df, i, 2);
                }
                else
                {
                    set_object_state(df, i, 0);
                }
            }
        }
    }

    F32 update_transition(z_disco_floor& df, F32 dt)
    {
        df.transition_time += dt;

        if (df.transition_time < df.transition_delay)
        {
            return 0.0f;
        }

        if (df.transition_time - dt < df.transition_delay)
        {
            dt = df.transition_time - df.transition_delay;
        }

        df.state = df.next_state;
        df.state_counter++;

        memcpy(df.active_state_mask, df.next_state_mask,
               state_byte_size(df.asset->state_mask_size));

        refresh_state(df);

        return dt;
    }

    void update_state(z_disco_floor& df, F32 dt)
    {
        df.state_time += dt;

        if (df.state_time < df.state_delay)
        {
            return;
        }

        if (df.state_time - dt < df.state_delay)
        {
            dt = df.state_time - df.state_delay;
        }

        df.state_time = 0.0f;
        df.next_state = df.state + (df.forward ? 1 : -1);

        if (df.next_state < df.min_state)
        {
            if (df.asset->flags & 0x1)
            {
                df.next_state = df.max_state;
            }
            else
            {
                df.next_state = df.state;
            }
        }

        if (df.next_state > df.max_state)
        {
            if (df.asset->flags & 0x1)
            {
                df.next_state = df.min_state;
            }
            else
            {
                df.next_state = df.state;
            }
        }

        translate_mask(df.next_state_mask, df.state_masks[df.next_state], df.object_list.size);

        if (df.transition_delay <= dt)
        {
            df.state = df.next_state;
            df.state_counter++;

            memcpy(df.active_state_mask, df.next_state_mask,
                   state_byte_size(df.asset->state_mask_size));
        }
        else
        {
            df.transition_time = 0.0f;
        }

        refresh_state(df);
    }

    xSurface& get_damage_surface()
    {
        static xSurface surf;
        static zSurfaceProps props;
        static zSurfAssetIN asset;
        static bool inited = false;

        if (inited)
        {
            return surf;
        }

        const xSurface& defsurf = zSurfaceGetDefault();

        surf = defsurf;
        props = *(zSurfaceProps*)defsurf.moprops;
        asset = *props.asset;

        surf.moprops = &props;
        props.asset = &asset;
        asset.game_damage_type = 0x4;

        return surf;
    }

    void on_change_goto_disco_floor(const tweak_info& ti)
    {
        z_disco_floor& df = *(z_disco_floor*)ti.context;
        xVec3& loc = xModelGetFrame(xglobals->___player_ent_dont_use_directly->model)->pos;

        loc = xModelGetFrame(df.object_list.off[0]->model)->pos;
        loc.y += 1.0f;

        xglobals->___player_ent_dont_use_directly->frame->oldmat.pos = loc;
        xglobals->___player_ent_dont_use_directly->frame->mat.pos = loc;
    }

    void on_change_state(const tweak_info& ti)
    {
        z_disco_floor& df = *(z_disco_floor*)ti.context;

        df.set_state(df.state, true);
    }

    void add_tweaks(z_disco_floor& df)
    {
        // The tweak registrations (xDebugAddTweak) are compiled out of retail; only the
        // name building and the callback tables remain.
        static const tweak_callback tweak_goto_cb = { (
            void (*)(tweak_info&))on_change_goto_disco_floor };
        static const tweak_callback tweak_state_cb = { (void (*)(tweak_info&))on_change_state };

        char buffer[128];

        sprintf(buffer, "Disco Floor|%s|", xSTAssetName(df.asset));

        char* name = buffer + strlen(buffer);

        if (df.object_list.size)
        {
            strcpy(name, "\x01- {c=ffff4444}Go To");
        }

        strcpy(name, "State");
        strcpy(name, "Enabled");
        strcpy(name, "Transition Interval");
        strcpy(name, "State Interval");
        strcpy(name, "State Minimum");
        strcpy(name, "State Maximum");
    }
} // namespace

void z_disco_floor::init()
{
}

void z_disco_floor::init(void* ent, void* asset)
{
    ((z_disco_floor*)ent)->load(*(z_disco_floor_asset*)asset);
}

inline void z_disco_floor::load(z_disco_floor_asset& asset)
{
    xBaseInit(this, &asset);

    baseType = eBaseTypeDiscoFloor;
    this->asset = &asset;
    eventFunc = event_handler;

    if (linkCount != 0)
    {
        link = (xLinkAsset*)(&asset + 1);
    }

    U8* data = (U8*)((xBaseAsset*)&asset + 1);

    prefix.off = (char*)(data + asset.prefix_offset.off);
    prefix.transition = (char*)(data + asset.prefix_offset.transition);
    prefix.on = (char*)(data + asset.prefix_offset.on);

    state_masks = (U8**)xMemAllocSize(asset.states_size * sizeof(U8*));

    U32* state_offsets = (U32*)(data + asset.states_offset);

    for (U32 i = 0; i < asset.states_size; i++)
    {
        state_masks[i] = data + state_offsets[i];
    }

    active_state_mask = (U8*)xMemAllocSize(state_byte_size(asset.state_mask_size));
    next_state_mask = (U8*)xMemAllocSize(state_byte_size(asset.state_mask_size));

    if (asset.interval.transition < 0.0f)
    {
        asset.interval.transition = 0.25f;
    }

    if (asset.interval.state < 0.0f)
    {
        asset.interval.state = 1.0f;
    }

    enabled = (asset.flags & 0x2) != 0;
}

void z_disco_floor::setup()
{
    U32 data_size;
    U32 i;

    current_disco_floor = id;
    object_list.size = asset->state_mask_size;

    if (object_list.size)
    {
        data_size = object_list.size * sizeof(xEnt*);

        object_list.off = (xEnt**)xMemAllocSize(data_size);
        find_objects(prefix.off, object_list.off, object_list.size);

        object_list.transition = (xEnt**)xMemAllocSize(data_size);
        find_objects(prefix.transition, object_list.transition, object_list.size);

        object_list.on = (xEnt**)xMemAllocSize(data_size);
        find_objects(prefix.on, object_list.on, object_list.size);

        for (i = 0; i < object_list.size; i++)
        {
            xSurface*& surf = object_list.on[i]->model->Surf;

            if (!surf)
            {
                surf = &get_damage_surface();
            }
            else
            {
                ((zSurfaceProps*)surf->moprops)->asset->game_damage_type = 0x4;
            }
        }
    }
    else
    {
        prefix.off = prefix.transition = prefix.on = NULL;
    }

    add_tweaks(*this);
    reset();
}

void z_disco_floor::reset()
{
    forward = true;
    transition_delay = asset->interval.transition;
    state_delay = asset->interval.state;
    min_state = 0;
    max_state = asset->states_size - 1;
    state_counter = 0;

    set_state(0, true);

    enabled = (asset->flags & 0x2) != 0;
}

void z_disco_floor::update(xScene&, F32 dt)
{
    if (!enabled)
    {
        return;
    }

    if (transition_time < transition_delay)
    {
        dt = update_transition(*this, dt);
    }

    if (dt > 0.0f)
    {
        update_state(*this, dt);
    }
}

void z_disco_floor::set_state(size_t state, bool immediate)
{
    if (state < min_state)
    {
        state = min_state;
    }
    else if (state > max_state)
    {
        state = max_state;
    }

    if (immediate)
    {
        next_state = state;
        this->state = state;
        transition_time = transition_delay;

        translate_mask(next_state_mask, state_masks[next_state], object_list.size);
        memcpy(active_state_mask, next_state_mask, state_byte_size(asset->state_mask_size));
    }
    else
    {
        next_state = state;
        transition_time = 0.0f;

        translate_mask(next_state_mask, state_masks[next_state], object_list.size);
    }

    state_time = 0.0f;

    refresh_state(*this);
}

void z_disco_floor::set_state_range(S32 min, S32 max, bool immediate)
{
    size_t min_state = range_limit<S32>(min, 0, asset->states_size - 1);
    size_t max_state = range_limit<S32>(max, min, asset->states_size - 1);

    if (min_state == this->min_state && max_state == this->max_state)
    {
        return;
    }

    this->min_state = min_state;
    this->max_state = max_state;

    if (immediate)
    {
        size_t newstate = range_limit<size_t>(state, min_state, max_state);

        if (newstate != state)
        {
            set_state(newstate, true);
        }
    }
    else
    {
        size_t newstate = range_limit<size_t>(next_state, min_state, max_state);

        if (newstate != next_state)
        {
            set_state(newstate, false);
        }
    }
}
