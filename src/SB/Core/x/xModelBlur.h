#ifndef XMODELBLUR_H
#define XMODELBLUR_H

#include "xFX.h"

struct xModelBlur
{
    struct config_data
    {
        S32 flags;
        F32 radius;
        F32 color_red;
        F32 color_green;
        F32 color_blue;
        F32 color_alpha;
        F32 card_dist;
        F32 zbias;
        S32 max_card_renders;
    };

    struct activity_data;

    activity_data* activity;

    static activity_data* activity_buffer;
    static activity_data* activities[6];
    static S32 activities_used;

    U8 update(F32 dt);
    void set_lifetime(F32 value, F32 time);
    void move(const xVec3& center);
    void deactivate();
    U8 activate(F32 lifetime, F32 alpha, const config_data& cfg, xModelInstance* model);
    void create();

    static void render_enable(bool enabled);
    static void render_all();
    static void update_all(F32 dt);
    static void reset_all();
    static void scene_exit();
    static void scene_enter();
};

#endif
