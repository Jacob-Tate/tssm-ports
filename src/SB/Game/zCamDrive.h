#ifndef ZCAMDRIVE_H
#define ZCAMDRIVE_H

#include "xCamera.h"
#include "xCam.h"

struct xEntFrame;

// Layout from the PS2 DWARF. Member functions in original source order; create is the key
// function that emits the vtable.
struct zCamDrive : xCam
{
    struct
    {
        U8 user_control_glance; // 0xC0
        F32 glance_angle;
        F32 angle_change;
        F32 angle_change_v;
        F32 angle_change_speed; // 0xD0
        F32 angle_change_max;
        F32 blend_time;
        F32 current_blend_time;
        F32 responsiveness;
        F32 responsiveness_v;
        F32 responsiveness_speed; // 0xE8
        F32 min_responsiveness;
        F32 min_responsiveness_v;
        F32 y;
        F32 y_v;
        F32 y_speed; // 0xFC
        F32 x;
        F32 x_v;
        F32 x_speed; // 0x108
        F32 up_height;
        F32 up_distance;
        F32 left_angle;
        F32 rest_angle; // 0x118
        F32 right_angle;
        F32 rest_height;
        F32 rest_distance; // 0x124
        F32 down_height;
        F32 down_distance; // 0x12C
    } input;
    U8 frozen; // 0x130
    U8 airborne;
    struct
    {
        F32 radius; // 0x134
        F32 distance;
        F32 distance_v;
        F32 distance_speed; // 0x140
        F32 angle_v;
        F32 angle_speed; // 0x148
    } collide_bounds;
    F32 distance;
    F32 height;
    F32 angle_speed; // 0x154
    F32 position_speed;
    F32 velocity_speed;
    F32 target_offset; // 0x160
    xVec3 target_position;
    F32 orientation_v;
    F32 position_v;
    F32 velocity_v;
    xQuat input_quat;
    xVec3 velocity;
    xVec3 lastPosition;
    U8 disableCameraControl; // 0x1A4
    U8 hardTurnCam;
    F32 hardTurnCamTime;
    F32 currenthardTurnCamTime;
    F32 timeToComeOutOfHardTurn; // 0x1B0
    F32 hardTurnCamVelocity;
    F32 hardTurnCamThreshold;
    F32 hardTurnCamMultiplier;
    F32 hardTurnCamSpeed; // 0x1C0
    F32 calculatedAngleSpeed;
    F32 hardTurnCamTargetOffsetX;
    F32 hardTurnCamTargetSpeed;
    F32 hardTurnCamCurrentTargetX; // 0x1D0
    F32 boostCamFOV;
    F32 boostCamFOVVelocity;
    F32 boostCamFOVSpeed;
    xEntFrame* frame; // 0x1E0

    void create();
    void start();
    void set_frame(xEntFrame* frame);
    void apply_standard_input(F32 dt);
    void collide(F32 dt);
    void update(xScene& scene, F32 dt);
};

#endif
