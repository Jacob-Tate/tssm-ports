#ifndef ZSLIDECAM_H
#define ZSLIDECAM_H

#include "xCamera.h"
#include "xCam.h"

// Layout from the PS2 DWARF. Members are in the original source order; create is the key
// function that emits the vtable.
struct zSlideCam : xCam
{
    struct config
    {
        F32 spring_len;
        F32 spring_const;
        F32 damp_const;
        F32 vertical_offset;
    };

    xMat4x3* target; // 0xC0
    config cfg; // 0xC4
    xVec3 curr_tgt_pos; // 0xD4
    xVec3 prev_tgt_pos; // 0xE0
    xVec3 cam_pos; // 0xEC

    void create();
    void start();

    void setCurrTgtPos(const xVec3& pos, const xVec3& up);
    void update(xScene& scene, F32 dt);
};

#endif
