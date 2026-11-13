#ifndef ZENTPLAYEROOBSTATE_H
#define ZENTPLAYEROOBSTATE_H

#include "xserializer.h"
#include "xMath2.h"
#include "xMath3.h"

struct xModelInstance;
struct xScene;
struct xIniFile;
struct xAnimPlay;

namespace oob_state
{
    U8 update_camera(xMat4x3* matrix, F32 dt);
    U8 enabled();
    void fx_render();
    bool render();
    bool IsPlayerInControl();
    U8 update(xScene& s, F32 dt);
    void force_start();
    void reset();
    void init();
    void load_settings(xIniFile& ini);

    namespace OOBState
    {
        enum state_enum
        {
            STATE_INVALID = -1,
            BEGIN_STATE = 0,
            STATE_IN = 0,
            STATE_OUT = 1,
            STATE_GRAB = 2,
            STATE_DROP = 3,
            END_STATE = 4,
            MAX_STATE = 4,
        };

        struct state_type;

        struct _class_43
        {
            F32 near_d;
            F32 near_h;
            F32 near_pitch;
            F32 far_d;
            F32 far_h;
            F32 far_pitch;
            xMat4x3* tgt_mat;
            xMat4x3* tgt_omat;
        };

        struct _class_21
        {
            S32 flags;
            state_type* state;
            state_type* states[4];
            U8 control;
            F32 camlerp;
            F32 out_time;
            F32 max_out_time;
            F32 reset_time;
            xModelInstance* model;
            xVec2 loc;
            xVec2 dir;
            F32 fade_alpha;
            U8 render_hand;
            U8 vertical;
            U8 first_reset_frame;
            F32 vel;
            F32 accel;
            _class_43 cam_data;
        };

        struct _class_54
        {
            F32 in_wait_time;
            F32 in_vel;
            F32 in_stop_dist;
            F32 out_wait_time;
            F32 out_vel;
            F32 out_start_dist;
            F32 fade_start_time;
            F32 fade_time;
        };

        struct _class_74
        {
            F32 in_vel;
            F32 in_stop_dist;
            F32 out_wait_time;
            F32 out_vel;
            F32 out_start_dist;
            F32 fade_start_time;
            F32 fade_time;
        };

        struct tagFixed
        {
            F32 out_time;
            F32 reset_time;
            F32 cam_dist;
            F32 cam_height;
            F32 cam_pitch;
            F32 reorient_time;
            char* hand_model;
            xVec2 in_loc;
            xVec2 out_loc;
            _class_54 grab;
            _class_74 drop;
            F32 hand_size_x;
            F32 hand_size_y;
            F32 hand_yaw;
            F32 hand_pitch;
            F32 hand_roll;
        };

        extern _class_21 shared;
        extern tagFixed fixed;
        extern xMat4x3 shared_target;

        void render_ghost();
    } // namespace OOBState

    inline F32 oob_timer()
    {
        if (OOBState::shared.reset_time == OOBState::fixed.reset_time)
        {
            return OOBState::shared.out_time;
        }

        return -1.0f;
    }
} // namespace oob_state

#endif
