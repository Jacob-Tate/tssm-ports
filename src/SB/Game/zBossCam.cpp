// xDebugTweak.h first: retail .rodata opens with its tweak_callback templates.
#include "xDebug.h"
#include "xDebugTweak.h"
#include "zBossCam.h"

#include "xCamera.h"
#include "xMath.h"
#include "xMath3.h"
#include "xMathInlines.h"
#include "iCamera.h"
#include "iMath.h"
#include "zGlobals.h"
// Last: retail has its load_param<xColor_tag,S32> template just before update's CA template.
#include "zAutoTweak.h"

// No symbol (inlined). The tangent-point offsets along and across dir are computed before they
// are added to the center, as in the PS2 demo.
static inline void bound_sphere_xz(xVec3& Q1, xVec3& Q2, const xVec3& center, F32 radius,
                                   const xVec3& dir, F32 dist)
{
    F32 ratio = radius / dist;
    F32 side = ratio * xsqrt(dist * dist - radius * radius);
    F32 along = radius * ratio;
    F32 along_x = along * dir.x;
    F32 along_z = along * dir.z;
    F32 side_x = side * dir.x;
    F32 side_z = side * dir.z;

    Q1.x = along_x + (center.x + side_z);
    Q1.y = center.y;
    Q1.z = along_z + (center.z - side_x);

    Q2.x = along_x + (center.x - side_z);
    Q2.y = center.y;
    Q2.z = along_z + (center.z + side_x);
}

void zBossCam_Binary::create()
{
    xCam::create();
    coord_type = XCAM_COORD_CART;
    orient_type = XCAM_ORIENT_QUAT;
    cfg_common.priority = 0x80;
    cfg_common.blend_time = 1.0f;
    camera = 0;
    s1 = s2 = NULL;
}

void zBossCam_Binary::start()
{
    camera = 1;
    stick_offset = 0.0f;
    xCam::start();

    coord.cart = cam_loc;
    orient.quat = cam_dir;
    refresh_mat();
}

void zBossCam_Binary::attach(zPlayer* new_owner)
{
    owner = new_owner;
}

void zBossCam_Binary::update(xScene& scene, F32 dt)
{
    xVec3& A = mat.pos;
    xVec3& B = *s1;
    xVec3& C = *s2;

    xVec3 CA = { 0.0f, 0.0f, 0.0f };
    CA.x = A.x - C.x;
    CA.z = A.z - C.z;

    F32 dCA = CA.length();
    if (dCA < 0.01f)
    {
        CA.assign(A.x - B.x, 0.0f, A.z - B.z).right_normalize();
        dCA = 0.01f;
    }
    else
    {
        CA /= dCA;
    }

    F32 yaw_start = xatan2(B.x - A.x, B.z - A.z);
    F32 yaw_end;
    if (dCA > s2_radius)
    {
        xVec3 Q1, Q2;
        bound_sphere_xz(Q1, Q2, C, s2_radius, CA, dCA);
        F32 yaw_Q1 = xatan2(Q1.x - A.x, Q1.z - A.z);
        F32 yaw_Q2 = xatan2(Q2.x - A.x, Q2.z - A.z);
        F32 dyaw1 = xrmod(yaw_Q1 - yaw_start + PI) - PI;
        F32 dyaw2 = xrmod(yaw_Q2 - yaw_start + PI) - PI;

        F32 fov = 0.5f * (0.017453292f * iCameraGetFOV(globals.screen->icam)) + cfg.margin_angle;
        fov = range_limit(fov, 0.0f, PI);
        F32 max_dyaw = 0.5f * xabs(dyaw2 - dyaw1);
        if (max_dyaw > fov)
        {
            yaw_end = xatan2(C.x - B.x, C.z - B.z);
        }
        else if (dyaw1 >= dyaw2)
        {
            yaw_end = xatan2(C.x - B.x, C.z - B.z);
        }
        else if (dyaw1 >= -fov)
        {
            if (dyaw2 <= fov)
            {
                yaw_end = yaw_start;
            }
            else
            {
                yaw_end = yaw_start + (dyaw2 - fov);
            }
        }
        else
        {
            yaw_end = yaw_start + (dyaw1 + fov);
        }
    }
    else
    {
        yaw_end = xatan2(C.x - B.x, C.z - B.z);
    }

    F32 sstick = 1.0f - xexp(-cfg.stick_speed * dt);
    analog_data& stick = globals.pad0->analog[1];

    stick_offset.x += (cfg.stick_yaw_vel * stick.offset.x * dt - stick_offset.x) * sstick;
    yaw_end += stick_offset.x;

    F32 yaw_diff = xrmod(yaw_end - yaw_start + PI) - PI;
    F32 max_yaw_diff = cfg.max_yaw_vel * dt;
    if (xabs(yaw_diff) > max_yaw_diff)
    {
        if (yaw_diff < 0.0f)
        {
            if (max_yaw_diff > 0.0f)
            {
                max_yaw_diff = -max_yaw_diff;
            }
        }
        else
        {
            if (max_yaw_diff < 0.0f)
            {
                max_yaw_diff = -max_yaw_diff;
            }
        }
        yaw_end = yaw_start + max_yaw_diff;
    }

    stick_offset.y += (stick.offset.y - stick_offset.y) * sstick;

    F32 d, h, hf;
    if (stick_offset.y > 0.0f)
    {
        F32 s = stick_offset.y;
        d = xlerp(cfg.zone_rest.distance, cfg.zone_below.distance, s);
        h = xlerp(cfg.zone_rest.height, cfg.zone_below.height, s);
        hf = xlerp(cfg.zone_rest.height_focus, cfg.zone_below.height_focus, s);
    }
    else
    {
        F32 s = -stick_offset.y;
        d = xlerp(cfg.zone_rest.distance, cfg.zone_above.distance, s);
        h = xlerp(cfg.zone_rest.height, cfg.zone_above.height, s);
        hf = xlerp(cfg.zone_rest.height_focus, cfg.zone_above.height_focus, s);
    }

    xVec3 end_loc = { 0.0f, 0.0f, 0.0f };
    end_loc.x = B.x - d * isin(yaw_end);
    end_loc.y = B.y + h;
    end_loc.z = B.z - d * icos(yaw_end);

    F32 sloc = 1.0f - xexp(-cfg.move_speed * dt);

    cam_loc.x = xlerp(A.x, end_loc.x, sloc);
    cam_loc.y = xlerp(A.y, end_loc.y, sloc);
    cam_loc.z = xlerp(A.z, end_loc.z, sloc);

    xVec3 heading = { 0.0f, 0.0f, 0.0f };
    heading.x = B.x - end_loc.x;
    heading.y = B.y - end_loc.y + hf;
    heading.z = B.z - end_loc.z;

    if (heading.length2() >= 0.001f)
    {
        heading /= heading.length();

        xQuat end_dir;
        xMat3x3 mat;
        xMat3x3LookVec(&mat, &heading.invert());
        xQuatFromMat(&end_dir, &mat);

        xQuatSlerp(&cam_dir, &cam_dir, &end_dir, 1.0f - xexp(-cfg.turn_speed * dt));
    }

    orient.quat = cam_dir;
    xQuatToMat(&cam_dir, &mat);
    coord.cart = cam_loc;
    mat.pos = coord.cart;

    if (fun_moveover != NULL)
    {
        fun_moveover(this, dt, ctxt_moveover);
    }
}

void zBossCam_Binary::move_to_rest()
{
    xVec3 toTarget;
    toTarget.x = s2->x - s1->x;
    toTarget.y = 0.0f;
    toTarget.z = s2->z - s1->z;
    xVec3Normalize(&toTarget, &toTarget);

    cam_loc.x = s1->x + toTarget.x * -cfg.zone_rest.distance;
    cam_loc.y = cfg.zone_rest.height + (s1->y + toTarget.y * -cfg.zone_rest.distance);
    cam_loc.z = s1->z + toTarget.z * -cfg.zone_rest.distance;

    xVec3 look_loc;
    look_loc.x = s1->x;
    look_loc.y = s1->y + cfg.zone_rest.height_focus;
    look_loc.z = s1->z;
    look_loc.x = cam_loc.x - look_loc.x;
    look_loc.y = cam_loc.y - look_loc.y;
    look_loc.z = cam_loc.z - look_loc.z;
    xVec3Normalize(&look_loc, &look_loc);

    xMat3x3 look_mat;
    xMat3x3LookVec(&look_mat, &look_loc);
    xQuatFromMat(&cam_dir, &look_mat);

    orient.quat = cam_dir;
    xQuatToMat(&cam_dir, &mat);
    coord.cart = cam_loc;
    mat.pos = coord.cart;
}
