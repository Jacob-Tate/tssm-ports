#include "zSlideCam.h"

#include "xVec3Inlines.h"

void zSlideCam::create()
{
    xCam::create();
    coord_type = XCAM_COORD_CART;
    orient_type = XCAM_ORIENT_QUAT;
    cfg_common.priority = 0x80;
    cfg_common.blend_time = 1.0f;
    cfg.spring_len = 2.5f;
    cfg.spring_const = 3.5f;
    cfg.damp_const = 3.2f;
    cfg.vertical_offset = 2.0f;
}

void zSlideCam::start()
{
    xCam::start();
    prev_tgt_pos = curr_tgt_pos;
    cam_pos = curr_tgt_pos;
}

// No GC symbol (inlined into update). Name, parameters and locals from Ratatouille's DWARF.
// Returns currPos moved by a damped spring towards trgPos.
static xVec3 SpringDamp(const xVec3& currPos, const xVec3& trgPos, const xVec3& prevTrgPos,
                        F32 deltaTime, F32 springConst, F32 dampConst, F32 springLen)
{
    xVec3 disp;
    xVec3 velocity;
    F32 forceMag;
    F32 len;
    F32 dot;
    xVec3 r;

    disp = currPos - trgPos;
    velocity = (prevTrgPos - trgPos) * deltaTime;
    len = disp.length();
    dot = xVec3Dot(&disp, &velocity);
    xVec3Normalize(&disp, &disp);
    forceMag = springConst * (springLen - len) + dampConst * (dot / len);
    disp *= forceMag * deltaTime;
    xVec3Add(&r, &currPos, &disp);
    return r;
}

// No GC symbol: auto-inlined into update and dead-stripped. Ratatouille defines it here.
void zSlideCam::setCurrTgtPos(const xVec3& pos, const xVec3& up)
{
    curr_tgt_pos = pos;
    curr_tgt_pos += up * cfg.vertical_offset;
}

void zSlideCam::update(xScene& scene, F32 dt)
{
    setCurrTgtPos(target->pos, target->up);

    F32 len = xVec3Length(curr_tgt_pos.x - prev_tgt_pos.x, curr_tgt_pos.y - prev_tgt_pos.y,
                          curr_tgt_pos.z - prev_tgt_pos.z);
    if (len > 0.001f)
    {
        cam_pos = SpringDamp(cam_pos, curr_tgt_pos, prev_tgt_pos, dt, cfg.spring_const,
                             cfg.damp_const, cfg.spring_len);
    }

    xVec3 y_axis = { 0.0f, 1.0f, 0.0f };
    mat.up = y_axis;
    mat.at = prev_tgt_pos - cam_pos;
    xVec3Normalize(&mat.at, &mat.at);
    xVec3Cross(&mat.right, &mat.up, &mat.at);
    xVec3Normalize(&mat.right, &mat.right);
    xVec3Cross(&mat.up, &mat.at, &mat.right);
    xVec3Normalize(&mat.up, &mat.up);

    coord.cart = cam_pos;
    mat.pos = coord.cart;
    xQuatFromMat(&orient.quat, &mat);
}
