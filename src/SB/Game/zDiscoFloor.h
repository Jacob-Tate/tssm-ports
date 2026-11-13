#ifndef ZDISCOFLOOR_H
#define ZDISCOFLOOR_H

#include "xBase.h"
#include "xEnt.h"
#include "xMath3.h"
#include "zEntSimpleObj.h"

#include <stddef.h>

struct xScene;

struct z_disco_floor_asset : xBaseAsset
{
    U32 flags;
    struct
    {
        F32 transition;
        F32 state;
    } interval;
    struct
    {
        U32 off;
        U32 transition;
        U32 on;
    } prefix_offset;
    U32 state_mask_size;
    U32 states_offset;
    U32 states_size;
};

struct z_disco_floor : xBase
{
    z_disco_floor_asset* asset; // 0x10
    struct
    {
        char* off;
        char* transition;
        char* on;
    } prefix; // 0x14
    U8** state_masks; // 0x20
    U8* active_state_mask; // 0x24
    U8* next_state_mask; // 0x28
    struct
    {
        xEnt** off;
        xEnt** transition;
        xEnt** on;
        U32 size;
    } object_list; // 0x2c
    U32 min_state; // 0x3c
    U32 max_state; // 0x40
    U32 state; // 0x44
    U32 next_state; // 0x48
    U32 state_counter; // 0x4c
    F32 transition_delay; // 0x50
    F32 state_delay; // 0x54
    F32 transition_time; // 0x58
    F32 state_time; // 0x5c
    bool enabled; // 0x60
    bool forward; // 0x61

    static void init();
    static void init(void* ent, void* asset);

    void load(z_disco_floor_asset& asset);
    void setup();
    void reset();
    void update(xScene&, F32 dt);
    void set_state(size_t state, bool immediate);
    void set_state_range(S32 min, S32 max, bool immediate);
};

#endif
