#ifndef ZBOSSCAM_H
#define ZBOSSCAM_H

#include "zNMECommon.h"
#include "xGlobals.h"
#include "xCam.h"

struct xCamBlend;

struct _class_11
{
    union
    {
        xVec3 cart;
        xCamCoordCylinder cylinder;
        xCamCoordSphere sphere;
    };
};

struct _class_16
{
    union
    {
        xQuat quat;
        xCamOrientEuler euler;
    };
};

struct zBossCam_Binary : xCam
{
    zPlayer* owner;
    xVec3 cam_loc;
    xQuat cam_dir;
    void (*fun_moveover)(zBossCam_Binary*, F32, void*);
    void* ctxt_moveover;

    struct zone_data
    {
        F32 distance;
        F32 height;
        F32 height_focus;
    };

    struct config
    {
        zone_data zone_rest;
        zone_data zone_above;
        zone_data zone_below;
        F32 move_speed;
        F32 turn_speed;
        F32 stick_speed;
        F32 stick_yaw_vel;
        F32 max_yaw_vel;
        F32 margin_angle;
    };

    config cfg;
    S32 camera;
    xVec3* s1;
    xVec3* s2;
    F32 s2_radius;
    xVec2 stick_offset;

    // No GC symbol (always inlined); named after BfBB's xBinaryCamera::set_targets.
    void set_targets(const xVec3& s1, const xVec3& s2, F32 s2_radius)
    {
        this->s1 = (xVec3*)&s1;
        this->s2 = (xVec3*)&s2;
        this->s2_radius = s2_radius;
    }

    void move_to_rest();
    void update(xScene& scene, F32 dt);
    void attach(zPlayer* player);
    void start();
    void create();
};

extern zBossCam_Binary* gzBinaryCam;

#endif
