#ifndef ZDUST_H
#define ZDUST_H

#include <types.h>

struct xVec3;

struct zDustRingEmitter
{
    struct config_type
    {
        F32 radius_inner;
        F32 radius_outer;
        F32 intensity;
        F32 alpha;
        F32 rate_scale;
    };

    struct activity_data
    {
        zDustRingEmitter* owner;
        F32 emitted[2];
        F32 intensity_min;
        F32 intensity_delta;
        U8 alpha;
        U8 pad1;
        U8 pad2;
        U8 pad3;
    };

    const config_type* cfg;
    activity_data* data;

    static S32 activities_used;
    static activity_data* activities;

    void kickup(const xVec3& center, F32 force, F32 dt);
    void deactivate();
    bool activate();
    void set_config(const config_type* cfg);
    void create();

    // Not in the PS2 DWARF (always inlined and dead-stripped); names from Ratatouille.
    void refresh_activity();
    static void scene_enter();
    static void reset_all();
    static void update_all(F32 dt);
    static void stop_activity(activity_data* activity);
};

void zDustRender();
void zDustUpdate(F32 dt);
void zDustReset();
void zDustSceneEnter();

#endif
