#include "zCamDrive.h"
#include "zGlobals.h"
#include "xCollide.h"
#include "zAutoTweak.h"

F32 MIN_RESPONSIVENESS_NORMAL = 0.2f;
F32 MIN_RESPONSIVENESS_SMALL_VEL = 1.0f;
F32 TIME_TO_COME_OUT_OF_HARD_TURN = 0.1f;

// Not in the DWARF (always inlined; the name is a guess): the three-point blend that height and
// distance share.
static inline F32 range_interp(F32 low, F32 mid, F32 high, F32 t)
{
    return mid + t * (t >= 0.0f ? high - mid : mid - low);
}

void zCamDrive::create()
{
    xCam::create();
    coord_type = XCAM_COORD_CART;
    orient_type = XCAM_ORIENT_QUAT;
    frame = NULL;
    cfg_common.priority = 0x40;
    cfg_common.blend_time = 1.0f;
    airborne = 0;
    target_offset = 1.5f;
    input.blend_time = 2.0f;
    angle_speed = 15.0f;
    calculatedAngleSpeed = angle_speed;
    input.angle_change_max = 130.0f;
    input.angle_change_speed = 15.0f;
    position_speed = 1.0f;
    velocity_speed = 20.0f;
    input.rest_angle = 0.0f;
    input.left_angle = -90.0f;
    input.right_angle = 90.0f;
    input.rest_height = 2.0f;
    input.rest_distance = 7.0f;
    input.x_speed = 8.0f;
    input.y_speed = 8.0f;
    input.up_distance = 7.0f;
    input.up_height = 6.0f;
    input.down_distance = 3.0f;
    input.down_height = -2.0f;
    input.min_responsiveness = 0.01f;
    input.responsiveness_speed = 15.0f;
    input.user_control_glance = 0;
    collide_bounds.radius = 0.5f;
    collide_bounds.distance_speed = 25.0f;
    collide_bounds.angle_speed = 25.0f;
    disableCameraControl = 1;
    hardTurnCam = 1;
    currenthardTurnCamTime = 0.0f;
    timeToComeOutOfHardTurn = 0.0f;
    hardTurnCamTime = 0.1f;
    hardTurnCamVelocity = 0.0f;
    hardTurnCamThreshold = 0.15f;
    hardTurnCamSpeed = 1.5f;
    hardTurnCamMultiplier = 3.0f;
    hardTurnCamTargetOffsetX = 2.5f;
    hardTurnCamTargetSpeed = 0.0f;
    hardTurnCamCurrentTargetX = 0.0f;
    boostCamFOV = (PI / 180.0f) * 95.0f;
    boostCamFOVVelocity = 0.0f;
    boostCamFOVSpeed = 4.0f;
}

void zCamDrive::start()
{
    xCam::start();

    frozen = 0;
    orientation_v = 0.0f;
    position_v = 0.0f;
    velocity_v = 0.0f;
    input.y_v = 0.0f;
    input.y = 0.0f;
    input.x_v = 0.0f;
    input.x = 0.0f;
    input.angle_change_v = 0.0f;
    input.angle_change = 0.0f;
    input.responsiveness_v = 0.0f;
    input.min_responsiveness_v = 0.0f;
    input.responsiveness = input.min_responsiveness;
    collide_bounds.distance = distance;
    collide_bounds.distance_v = 0.0f;
    collide_bounds.angle_v = 0.0f;
    velocity = g_O3;
    lastPosition = frame->mat.pos;
    distance = input.rest_distance;
    height = input.rest_height;

    target_position = frame->mat.pos + frame->mat.up * target_offset;
    input.current_blend_time = 0.0f;

    xVec3 camera_position = frame->mat.pos - frame->mat.at * distance;
    xVec3 camera_to_player = target_position - camera_position;
    camera_to_player.safe_normalize(frame->mat.at);

    xMat3x3 dest;
    xMat3x3LookAt(&dest, &camera_to_player, &g_O3);
    xQuatFromMat(&orient.quat, &dest);

    coord.cart = target_position - dest.at * distance;
    coord.cart += frame->mat.up * height;
}

void zCamDrive::set_frame(xEntFrame* frame)
{
    this->frame = frame;
}

void zCamDrive::apply_standard_input(F32 dt)
{
    S32 x = globals.pad0->analog2.x;
    S32 y = globals.pad0->analog2.y;

    F32 input_vel_x;
    if (x > -globals.player.g.AnalogMin && x < globals.player.g.AnalogMin)
    {
        input_vel_x = 0.0f;
    }
    else
    {
        input_vel_x = -x / 127.0f;
    }

    F32 input_vel_y;
    if (y > -globals.player.g.AnalogMin && y < globals.player.g.AnalogMin)
    {
        input_vel_y = 0.0f;
    }
    else
    {
        input_vel_y = globals.pad0->analog2.y / 127.0f;
    }

    if (!disableCameraControl)
    {
        xCam::spring_interp(input.y, input.y_v, input.y, input_vel_y, dt, input.y_speed);
        xCam::spring_interp(input.angle_change, input.angle_change_v, input.angle_change,
                            input_vel_x * ((PI / 180.0f) * input.angle_change_max), dt,
                            input.angle_change_speed);
    }
    else
    {
        input_vel_x = 0.0f;
        input_vel_y = 0.0f;
    }

    height = range_interp(input.up_height, input.rest_height, input.down_height, input.y);
    distance = range_interp(input.up_distance, input.rest_distance, input.down_distance, input.y);

    xQuat rotation;
    xVec3 up = { 0.0f, 1.0f, 0.0f };
    xQuatFromAxisAngle(&rotation, &up, input.angle_change * dt);
    xQuatMul(&orient.quat, &orient.quat, &rotation);

    if (input_vel_x == 0.0f)
    {
        xVec3 camera_position = frame->mat.pos - frame->mat.at * distance;
        xVec3 camera_to_player = target_position - camera_position;
        camera_to_player.safe_normalize(frame->mat.at);

        xMat3x3 dest;
        xMat3x3LookAt(&dest, &camera_to_player, &g_O3);

        xQuat target_orientation;
        xQuatFromMat(&target_orientation, &dest);

        xQuat old_orientation = orient.quat;
        F32 old_orientation_v = orientation_v;
        xQuat new_orientation;

        F32 target_responsiveness =
            xlerp(input.min_responsiveness, 1.0f, velocity.length() / 20.0f);
        target_responsiveness = target_responsiveness * target_responsiveness;
        xCam::spring_interp(input.responsiveness, input.responsiveness_v, input.responsiveness,
                            target_responsiveness, dt, input.responsiveness_speed);

        if (input.responsiveness > 0.05f)
        {
            xCam::spring_interp(new_orientation, orientation_v, orient.quat, target_orientation, dt,
                                calculatedAngleSpeed * input.responsiveness);
        }
        else
        {
            new_orientation = orient.quat;
        }

        input.current_blend_time += dt;
        F32 blend = SQ(MIN(input.current_blend_time / input.blend_time, 1.0f));

        xQuatSlerp(&orient.quat, &old_orientation, &new_orientation, blend);
        orientation_v = xlerp(old_orientation_v, orientation_v, blend);
    }
    else
    {
        input.current_blend_time = 0.0f;
    }
}

// Placeholder for a dead-stripped function (name and body unknown) whose unreferenced {0, 1, 0}
// template remains in retail .rodata between apply_standard_input's and update's.
static void __deadstripped_zCamDrive()
{
    xVec3 up = { 0.0f, 1.0f, 0.0f };
}

void zCamDrive::collide(F32 dt)
{
    xQuat old_orient = orient.quat;
    xMat4x3 dest;
    xQuatToMat(&orient.quat, &dest);

    xSweptSphere sws;
    F32 target_distance = 0.0f;
    xVec3 start = target_position;
    xVec3 end = coord.cart;
    xSweptSpherePrepare(&sws, &start, &end, collide_bounds.radius);

    if (xSweptSphereToScene(&sws, xglobals->sceneCur, xglobals->___player_ent_dont_use_directly,
                            0x10, 0))
    {
        xSweptSphereGetResults(&sws);
        target_distance = sws.curdist;
    }
    else
    {
        target_distance = distance;
    }

    xCam::spring_interp(collide_bounds.distance, collide_bounds.distance_v, collide_bounds.distance,
                        target_distance, dt, collide_bounds.distance_speed);

    xQuatToMat(&orient.quat, &dest);
    coord.cart = target_position - dest.at * collide_bounds.distance;

    xVec3 camera_to_player = target_position - coord.cart;
    camera_to_player.safe_normalize(dest.at);

    xMat3x3LookAt(&dest, &camera_to_player, &g_O3);
    xQuatFromMat(&orient.quat, &dest);

    // The rotation delta (old^-1 * new) is computed but never used (retail).
    old_orient.v.invert();
    xQuatMul(&old_orient, &old_orient, &orient.quat);
}

void zCamDrive::update(xScene& scene, F32 dt)
{
    if (dt < 0.00001f)
    {
        return;
    }

    coord.cart += velocity * dt;

    xVec3 dPos;
    xVec3Sub(&dPos, &frame->mat.pos, &lastPosition);
    lastPosition = frame->mat.pos;

    // Not in the PS2 DWARF, but retail computes |dPos|^2 once, here, for the hard-turn tests below.
    F32 dist2 = dPos.length2();
    xVec3 target_velocity = dPos * (1.0f / dt);
    xCam::spring_interp(velocity, velocity_v, velocity, target_velocity, dt, velocity_speed);

    S32 x = globals.pad0->analog1.x;

    F32 input_vel_x;
    if (x > -globals.player.g.AnalogMin && x < globals.player.g.AnalogMin)
    {
        input_vel_x = 0.0f;
    }
    else
    {
        input_vel_x = -x / 127.0f;
    }

    if (xabs(input_vel_x) >= hardTurnCamThreshold)
    {
        currenthardTurnCamTime += dt;
        if (currenthardTurnCamTime >= hardTurnCamTime && dist2 >= 0.05f)
        {
            hardTurnCam = 1;
            timeToComeOutOfHardTurn = 0.0f;
        }
        else
        {
            if (dist2 >= 0.05f)
            {
                xCam::spring_interp(input.min_responsiveness, input.min_responsiveness_v,
                                    input.min_responsiveness, MIN_RESPONSIVENESS_NORMAL, dt,
                                    input.responsiveness_speed);
            }
            else
            {
                xCam::spring_interp(input.min_responsiveness, input.min_responsiveness_v,
                                    input.min_responsiveness, MIN_RESPONSIVENESS_SMALL_VEL, dt,
                                    input.responsiveness_speed);
            }

            if (hardTurnCam)
            {
                timeToComeOutOfHardTurn += dt;
                if (timeToComeOutOfHardTurn > TIME_TO_COME_OUT_OF_HARD_TURN)
                {
                    hardTurnCam = 0;
                    timeToComeOutOfHardTurn = 0.0f;
                }
            }
        }
    }
    else
    {
        xCam::spring_interp(input.min_responsiveness, input.min_responsiveness_v,
                            input.min_responsiveness, MIN_RESPONSIVENESS_NORMAL, dt,
                            input.responsiveness_speed);

        if (hardTurnCam)
        {
            timeToComeOutOfHardTurn += dt;
            if (timeToComeOutOfHardTurn > TIME_TO_COME_OUT_OF_HARD_TURN)
            {
                hardTurnCam = 0;
                currenthardTurnCamTime = 0.0f;
                timeToComeOutOfHardTurn = 0.0f;
            }
        }
    }

    F32 offsetX = 0.0f;
    if (hardTurnCam)
    {
        xCam::spring_interp(calculatedAngleSpeed, hardTurnCamVelocity, calculatedAngleSpeed,
                            angle_speed * hardTurnCamMultiplier * xabs(input_vel_x), dt,
                            hardTurnCamSpeed);

        offsetX = input_vel_x >= 0.0f ? hardTurnCamTargetOffsetX : -hardTurnCamTargetOffsetX;
        xCam::spring_interp(hardTurnCamCurrentTargetX, hardTurnCamTargetSpeed,
                            hardTurnCamCurrentTargetX, offsetX, dt, hardTurnCamSpeed);
    }
    else
    {
        calculatedAngleSpeed = angle_speed;
        xCam::spring_interp(hardTurnCamCurrentTargetX, hardTurnCamTargetSpeed,
                            hardTurnCamCurrentTargetX, offsetX, dt, hardTurnCamSpeed);
    }

    xCam::spring_interp(fov, boostCamFOVVelocity, fov, boostCamFOV, dt, boostCamFOVSpeed);

    xVec3 new_target_position = frame->mat.pos + frame->mat.up * target_offset;
    new_target_position += frame->mat.right * hardTurnCamCurrentTargetX;
    xCam::spring_interp(target_position, position_v, target_position, new_target_position, dt,
                        velocity_speed);

    apply_standard_input(dt);

    xMat4x3 dest;
    xQuatToMat(&orient.quat, &dest);
    coord.cart = target_position - dest.at * distance;

    xVec3 old_position = coord.cart;
    xQuat old_orientation = orient.quat;

    coord.cart += frame->mat.up * height;

    xVec3 camera_to_player = target_position - coord.cart;
    camera_to_player.safe_normalize(dest.at);

    xMat3x3LookAt(&dest, &camera_to_player, &g_O3);
    xQuatFromMat(&orient.quat, &dest);

    collide(dt);
    refresh_mat();

    coord.cart = old_position;
    orient.quat = old_orientation;

    if (input.user_control_glance)
    {
        xQuat rotation;
        xVec3 up = { 0.0f, 1.0f, 0.0f };
        xQuatFromAxisAngle(&rotation, &up, -input.glance_angle);
        xQuatMul(&orient.quat, &orient.quat, &rotation);
    }
}
