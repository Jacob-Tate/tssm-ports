#include "zEntPlayerOOBState.h"
#ifdef PLATFORM_PC
#include "iScreen.h"
#endif

#include <types.h>
#include <string.h>
#include <rwcore.h>

#include "xAnim.h"
#include "xCamera.h"
#include "xCam.h"
#include "xEnt.h"
#include "xEntBoulder.h"
#include "xLightKit.h"
#include "xMath.h"
#include "xMathInlines.h"
#include "xModel.h"
#include "xScrFx.h"
#include "xString.h"
#include "xstransvc.h"
#include "iColor.h"
#include "iDraw.h"
#include "iModel.h"
#include "zCamera.h"
#include "zCameraTweak.h"
#include "zEntCruiseBubble.h"
#include "zEntPlayer.h"
#include "zEntPlayerFX.h"
#include "zGameState.h"
#include "zGlobals.h"
#include "zMain.h"
#include "zRenderState.h"
#include "zSurface.h"
#include "zUICustom.h"

namespace oob_state
{
    namespace OOBState
    {
        _class_21 shared = { 1,
                             NULL,
                             { NULL, NULL, NULL, NULL },
                             0,
                             0.0f,
                             0.0f,
                             0.0f,
                             0.0f,
                             NULL,
                             { 0.0f, 0.0f },
                             { 0.0f, 0.0f },
                             0.0f,
                             0,
                             0,
                             0,
                             0.0f,
                             0.0f,
                             { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, NULL, NULL } };

        tagFixed fixed;
        xMat4x3 shared_target;
        static xVec3 move_posAdjust;
        static void (*old_before_animmat)(xAnimPlay*, xQuat*, xVec3*, S32);

        static inline bool player_is_sponge_ball()
        {
            return gCurrentPlayer == eCurrentPlayerSpongeBob && gSpongeBall;
        }

        static inline bool player_is_boulder_rolling()
        {
            return boulderRollShouldStart && boulderVehicle;
        }

        static inline xEnt& player_ent()
        {
            if (boulderVehicle)
            {
                return *boulderVehicle;
            }

            return *xglobals->___player_ent_dont_use_directly;
        }

        static inline void render_model(xModelInstance& model, const xVec3& loc, const xVec3& size,
                                        const xVec3& ypr)
        {
            basic_rect<F32> screen_rect = { 0.0f, 0.0f, 1.0f, 1.0f };
            screen_rect.x = loc.x;
            screen_rect.y = loc.y;

            xVec3 from = { 0.0f, 0.0f, 1.0f };
            xVec3 to = { 0.0f, 0.0f, 0.0f };
            to.z = -loc.z;

            xMat3x3 scaledMat;
            xMat3x3 eulerMat;
            xMat4x3 outMat;
            xMat3x3ScaleC(&scaledMat, size.x * (1.0f + loc.z), size.y * (1.0f + loc.z),
                          1.0f + loc.z);
            xMat3x3Euler(&eulerMat, &ypr);
            xMat3x3Mul(&outMat, &eulerMat, &scaledMat);

            outMat.pos = 0.0f;
            outMat.flags = 0;
            xModelSetFrame(&model, &outMat);

            xModelRender2D(model, screen_rect, from, to);
        }

        static inline void set_rect_vert(rwGameCube2DVertex& vert, F32 x, F32 y, F32 z,
                                         iColor_tag c, F32 rcz)
        {
            vert.x = x;
            vert.y = y;
            vert.z = z;
            vert.emissiveColor.red = c.r;
            vert.emissiveColor.green = c.g;
            vert.emissiveColor.blue = c.b;
            vert.emissiveColor.alpha = c.a;
        }

        static inline void set_rect_verts(rwGameCube2DVertex* verts, F32 x, F32 y, F32 w, F32 h,
                                          iColor_tag c, F32 rcz, F32 nsz)
        {
            set_rect_vert(verts[0], x, y, rcz, c, nsz);
            set_rect_vert(verts[1], x, y + h, rcz, c, nsz);
            set_rect_vert(verts[2], x + w, y, rcz, c, nsz);
            set_rect_vert(verts[3], x + w, y + h, rcz, c, nsz);
        }

        static inline void render_fade()
        {
            iColor_tag color = {};
            color.a = 255.0f * (1.0f - shared.fade_alpha) + 0.5f;

            zRenderState(SDRS_OOBFade);
            RwRenderStateSet(rwRENDERSTATETEXTURERASTER, NULL);
            F32 nsz = 1.0f / ((RwCamera*)RWSRCGLOBAL(curCamera))->farPlane;
            F32 rcz = RwIm2DGetFarScreenZ();

            RwIm2DVertex vert[4];
            set_rect_verts(vert, 0.0f, 0.0f, FB_XRES, FB_YRES, color, rcz, nsz);
#ifdef PLATFORM_PC
            // PC: the fade covers the whole screen, not the 4:3 UI box (iScreen.h).
            iScreen2DScreenSpaceBegin();
#endif
            RwIm2DRenderPrimitive(rwPRIMTYPETRISTRIP, vert, 4);
#ifdef PLATFORM_PC
            iScreen2DScreenSpaceEnd();
#endif
        }

        static inline void render_hand()
        {
            zRenderState(SDRS_OOBHand);

            xVec3 modelLoc = { 0.0f, 0.0f, 0.0f };
            modelLoc.x = shared.loc.x;
            modelLoc.y = shared.loc.y;

            xVec3 modelSize = { 0.0f, 0.0f, 1.0f };
            modelSize.x = fixed.hand_size_x;
            modelSize.y = fixed.hand_size_y;

            xVec3 modelYpr = { 0.0f, 0.0f, 0.0f };
            modelYpr.x = fixed.hand_yaw;
            modelYpr.y = fixed.hand_pitch;
            modelYpr.z = fixed.hand_roll;

            render_model(*shared.model, modelLoc, modelSize, modelYpr);
        }

        static inline void move_hand(F32 dt)
        {
            F32 dd = 0.5f * shared.accel * dt * dt + shared.vel * dt;

            shared.vel += shared.accel * dt;
            shared.loc += shared.dir * dd;
        }

        static inline void move_up(xVec3& loc, F32 x)
        {
            xMat4x3& mat = globals.cam->mat;
            xVec3 sloc;

            xMat4x3Tolocal(&sloc, &mat, &loc);
            move_posAdjust += mat.up * x * sloc.z;
        }

        static inline void move_right(xVec3& loc, F32 x)
        {
            xMat4x3& mat = globals.cam->mat;
            xVec3 sloc;

            xMat4x3Tolocal(&sloc, &mat, &loc);
#ifdef PLATFORM_PC
            // PC: the hand is drawn in the 4:3 UI box, so its drag uses the box's
            // shape rather than the screen's (iScreen.h). The same at 4:3.
            move_posAdjust += mat.right * x * sloc.z * (iScreenUIWidthF() / iScreenUIHeightF());
#else
            move_posAdjust += mat.right * x * sloc.z * ((F32)FB_XRES / (F32)FB_YRES);
#endif
        }

        static inline void update_max_out_time(const xSurface& surface)
        {
            F32 delay = zSurfaceGetOutOfBoundsDelay(surface);
            if (delay < 0.0f)
            {
                delay = fixed.out_time;
            }

            F32 old_max_out_time = shared.max_out_time;
            shared.max_out_time = delay;
            shared.out_time -= old_max_out_time - delay;
        }

        static inline void set_camera()
        {
            shared.cam_data.tgt_mat = globals.oldSkoolCamera.tgt_mat;
            shared.cam_data.tgt_omat = globals.oldSkoolCamera.tgt_omat;

            shared_target = g_I3;
            shared_target.pos = *(xVec3*)&player_ent().model->Mat->pos;

            globals.oldSkoolCamera.tgt_omat = &shared_target;
            globals.oldSkoolCamera.tgt_mat = &shared_target;

            zCameraDisableInput();
            zCameraSetBbounce(0);
            zCameraSetLongbounce(0);
            zCameraSetHighbounce(0);
            zCameraTweakGlobal_Init();
            xCameraDoCollisions(0, 6);
            zCameraDisableTracking(CO_OOB);
        }

        static inline void reset_camera()
        {
            globals.oldSkoolCamera.tgt_mat = shared.cam_data.tgt_mat;
            globals.oldSkoolCamera.tgt_omat = shared.cam_data.tgt_omat;

            zCameraEnableInput();
            zCameraTweakGlobal_Init();
            xCameraDoCollisions(1, 6);
            zCameraEnableTracking(CO_OOB);
        }

        static void oob_before_anim_mat(xAnimPlay* play, xQuat* quat, xVec3* tran, S32 boneCount);

        struct state_type
        {
            state_enum type;

            state_type(state_enum type) : type(type)
            {
            }

            virtual void start()
            {
            }

            virtual void stop()
            {
            }

            virtual state_enum update(xScene& s, F32& dt) = 0;
        };

        struct in_state_type : state_type
        {
            in_state_type() : state_type(STATE_IN)
            {
            }

            void start()
            {
                shared.reset_time = 0.0f;
                shared.out_time = 0.0f;
                shared.control = FALSE;
            }

            void stop()
            {
            }

            state_enum update(xScene& s, F32& dt)
            {
                // not in the DWARF: the floor surface is tested and used twice.
                xSurface* floor_surf = globals.player.floor_surf;
                U8 oob = FALSE;

                if (floor_surf != NULL)
                {
                    oob = zSurfaceOutOfBounds(*floor_surf);
                    if (oob)
                    {
                        update_max_out_time(*floor_surf);
                    }
                }

                return oob ? STATE_OUT : STATE_IN;
            }
        };

        struct out_state_type : state_type
        {
            out_state_type() : state_type(STATE_OUT)
            {
            }

            void start()
            {
                shared.out_time = shared.max_out_time;
                shared.reset_time = fixed.reset_time;
            }

            void stop()
            {
            }

            state_enum update(xScene& s, F32& dt)
            {
                // not in the DWARF: the floor surface is tested and used twice.
                xSurface* floor_surf = globals.player.floor_surf;
                U8 oob = FALSE;

                if (floor_surf != NULL)
                {
                    oob = zSurfaceOutOfBounds(*floor_surf);
                    if (oob)
                    {
                        update_max_out_time(*floor_surf);
                    }
                }

                shared.out_time -= dt;

                if (oob)
                {
                    shared.reset_time = fixed.reset_time;
                    if (shared.out_time <= 0.0f)
                    {
                        return STATE_GRAB;
                    }
                }
                else
                {
                    if (globals.player.floor_surf == NULL)
                    {
                        shared.reset_time = 0.0f;
                    }

                    shared.reset_time -= dt;
                    if (shared.reset_time <= 0.0f)
                    {
                        return STATE_IN;
                    }
                }

                return STATE_OUT;
            }
        };

        struct grab_state_type : state_type
        {
            enum substate_enum
            {
                SS_INVALID = -1,
                SS_REORIENT,
                SS_BEGIN_WAIT,
                SS_MOVING_IN,
                SS_STOPPING,
                SS_STOPPED,
                SS_STARTING,
                SS_MOVING_OUT,
                SS_START_FADE_OUT,
                SS_FADE_OUT,
                MAX_SS
            };

            substate_enum move_substate;
            substate_enum fade_substate;
            xVec3 player_start;
            F32 reorient_time;
            F32 angle_delta;
            F32 delay;
            F32 fade_start_time;
            F32 fade_time;
            U32 scene_reset;
            substate_enum (*updatess[MAX_SS])(grab_state_type&, xScene&, F32&);

            grab_state_type() : state_type(STATE_GRAB)
            {
                updatess[SS_REORIENT] = &supdate_reorient;
                updatess[SS_BEGIN_WAIT] = &supdate_begin_wait;
                updatess[SS_MOVING_IN] = &supdate_moving_in;
                updatess[SS_STOPPING] = &supdate_stopping;
                updatess[SS_STOPPED] = &supdate_stopped;
                updatess[SS_STARTING] = &supdate_starting;
                updatess[SS_MOVING_OUT] = &supdate_moving_out;
                updatess[SS_START_FADE_OUT] = &supdate_start_fade_out;
                updatess[SS_FADE_OUT] = &supdate_fade_out;
            }

            void start()
            {
                xEnt& player = player_ent();

                zEntPlayerControlOff(CONTROL_OWNER_OOB);
                globals.player.ControlOffTimer = FLOAT_MAX;

                if (player.model->Data->boundingSphere.radius > 3.0f)
                {
                    fixed.out_loc.x = 1.2f;
                    fixed.cam_dist = 5.5f;
                    fixed.cam_height = 2.0f;
                }
                else if (player.model->Data->boundingSphere.radius > 2.0f)
                {
                    fixed.out_loc.x = 1.15f;
                    fixed.cam_dist = 3.5f;
                    fixed.cam_height = 1.0f;
                }
                else
                {
                    fixed.out_loc.x = 1.02f;
                    fixed.cam_dist = 3.0f;
                    fixed.cam_height = 1.0f;
                }

                move_posAdjust = 0.0f;
                shared.first_reset_frame = FALSE;
                cruise_bubble::reset();

                shared.flags |= 0x4;
                shared.vertical = xabs(fixed.in_loc.y - fixed.out_loc.y) > 0.01f;
                shared.control = TRUE;
                shared.camlerp = 0.0f;

                U32 bufsize;
                U32 cheatAlternateHand =
                    zUICustomExtrasManager::IsCheatEnabled(eExtrasScreenCheats_AlternateHand);
                void* info = xSTFindAsset(cheatAlternateHand ? xStrHash("hand_alternate") :
                                                               xStrHash(fixed.hand_model),
                                          &bufsize);
                shared.model = info == NULL ? NULL : xEntLoadModel(NULL, (RpAtomic*)info, NULL);

                move_substate = shared.model != NULL ? SS_REORIENT : SS_INVALID;
                reorient_time = fixed.reorient_time;

                set_camera();

                shared.loc = fixed.out_loc;
                shared.dir = (fixed.in_loc - fixed.out_loc).normal();
                shared.vel = fixed.grab.in_vel;
                shared.accel = 0.0f;

                fade_substate = SS_START_FADE_OUT;
                fade_start_time = fixed.grab.fade_start_time;
                fade_time = fixed.grab.fade_time;

                shared.fade_alpha = 1.0f;
                player_start = player.frame->mat.pos;

                xVec3 dir;
                xMat3x3GetEuler(&player.frame->mat, &dir);
                player.frame->rot.angle = xrmod(dir.x);

                angle_delta = xrmod(PI + (globals.oldSkoolCamera.pcur - dir.x)) - PI;
                angle_delta /= fixed.reorient_time;
                angle_delta = 0.0f;

                xModelUpdate(player_ent().model, 0.001f);
                scene_reset = FALSE;
            }

            void stop()
            {
            }

            state_enum update(xScene& s, F32& dt)
            {
                if (shared.model == NULL)
                {
                    return STATE_DROP;
                }

                F32 movedt = dt;
                while (move_substate != SS_INVALID)
                {
                    substate_enum newstate = updatess[move_substate](*this, s, movedt);
                    if (newstate == move_substate)
                    {
                        break;
                    }

                    move_substate = newstate;
                }

                F32 fadedt = dt;
                while (fade_substate != SS_INVALID)
                {
                    substate_enum newstate = updatess[fade_substate](*this, s, fadedt);
                    if (newstate == fade_substate)
                    {
                        break;
                    }

                    fade_substate = newstate;
                }

                if (move_substate == SS_INVALID && fade_substate == SS_INVALID)
                {
                    if (scene_reset)
                    {
                        return STATE_DROP;
                    }

                    scene_reset = TRUE;
                    zGameStateSwitch(eGameState_LoseChance);
                }

                xModelInstance* model = player_ent().model;

                if (move_substate == SS_STARTING || move_substate == SS_MOVING_OUT)
                {
                    xMat4x3& pm = *(xMat4x3*)model->Mat;
                    pm.pos = player_start;

                    move_posAdjust = 0.0f;
                    move_up(pm.pos, fixed.in_loc.y - shared.loc.y);
                    move_right(pm.pos, fixed.in_loc.x - shared.loc.x);
                }

                if (move_substate == SS_STARTING)
                {
                    zEntPlayerFX::SFX::PlayFailure();
                }

                model->Flags &= (U16)~0x100;
                xModelUpdate(model, 0.0f);
                model->Flags |= 0x100;

                old_before_animmat = model->Anim->BeforeAnimMatrices;
                model->Anim->BeforeAnimMatrices = oob_before_anim_mat;
                xModelEval(model);
                model->Anim->BeforeAnimMatrices = old_before_animmat;

                return STATE_GRAB;
            }

            substate_enum update_reorient(xScene& s, F32& dt)
            {
                return SS_BEGIN_WAIT;
            }

            substate_enum update_begin_wait(xScene& s, F32& dt)
            {
                delay -= dt;
                if (delay > 0.0f)
                {
                    return SS_BEGIN_WAIT;
                }

                shared.render_hand = TRUE;
                return SS_MOVING_IN;
            }

            substate_enum update_moving_in(xScene& s, F32& dt)
            {
                move_hand(dt);

                F32 dist2 = (fixed.in_loc - shared.loc).length2();
                if (dist2 > SQR(fixed.grab.in_stop_dist))
                {
                    return SS_MOVING_IN;
                }

                // not in the DWARF: retail reads the velocity before the inlined xsqrt and keeps it
                // in a register across it
                F32 vel = shared.vel;
                shared.accel = (-vel * vel) / (2.0f * xsqrt(dist2));
                return SS_STOPPING;
            }

            substate_enum update_stopping(xScene& s, F32& dt)
            {
                move_hand(dt);

                if (shared.vel > 0.0f)
                {
                    return SS_STOPPING;
                }

                shared.loc = fixed.in_loc;
                shared.vel = shared.accel = 0.0f;
                delay = fixed.grab.out_wait_time;

                return SS_STOPPED;
            }

            substate_enum update_stopped(xScene& s, F32& dt)
            {
                delay -= dt;
                if (delay > 0.0f)
                {
                    return SS_STOPPED;
                }

                dt += delay;
                shared.accel =
                    (-fixed.grab.out_vel * fixed.grab.out_vel) / (2.0f * fixed.grab.out_start_dist);
                return SS_STARTING;
            }

            substate_enum update_starting(xScene& s, F32& dt)
            {
                move_hand(dt);

                if (shared.vel > -fixed.grab.out_vel)
                {
                    return SS_STARTING;
                }

                shared.vel = -fixed.grab.out_vel;
                shared.accel = 0.0f;
                return SS_MOVING_OUT;
            }

            substate_enum update_moving_out(xScene& s, F32& dt)
            {
                move_hand(dt);

                if ((shared.loc - fixed.out_loc).dot(shared.dir) >= 0.0f)
                {
                    return SS_MOVING_OUT;
                }

                dt = 0.0f;
                return SS_INVALID;
            }

            substate_enum update_start_fade_out(xScene& s, F32& dt)
            {
                fade_start_time -= dt;
                if (fade_start_time > 0.0f)
                {
                    return SS_START_FADE_OUT;
                }

                dt += fade_start_time;
                return SS_FADE_OUT;
            }

            substate_enum update_fade_out(xScene& s, F32& dt)
            {
                fade_time -= dt;
                shared.fade_alpha = fade_time / fixed.grab.fade_time;
                if (shared.fade_alpha < 0.0f)
                {
                    shared.fade_alpha = 0.0f;
                }

                if (fade_time > 0.0f)
                {
                    return SS_FADE_OUT;
                }

                dt += fade_time;
                return SS_INVALID;
            }

            static substate_enum supdate_reorient(grab_state_type& gst, xScene& s, F32& dt)
            {
                return gst.update_reorient(s, dt);
            }

            static substate_enum supdate_begin_wait(grab_state_type& gst, xScene& s, F32& dt)
            {
                return gst.update_begin_wait(s, dt);
            }

            static substate_enum supdate_moving_in(grab_state_type& gst, xScene& s, F32& dt)
            {
                return gst.update_moving_in(s, dt);
            }

            static substate_enum supdate_stopping(grab_state_type& gst, xScene& s, F32& dt)
            {
                return gst.update_stopping(s, dt);
            }

            static substate_enum supdate_stopped(grab_state_type& gst, xScene& s, F32& dt)
            {
                return gst.update_stopped(s, dt);
            }

            static substate_enum supdate_starting(grab_state_type& gst, xScene& s, F32& dt)
            {
                return gst.update_starting(s, dt);
            }

            static substate_enum supdate_moving_out(grab_state_type& gst, xScene& s, F32& dt)
            {
                return gst.update_moving_out(s, dt);
            }

            static substate_enum supdate_start_fade_out(grab_state_type& gst, xScene& s, F32& dt)
            {
                return gst.update_start_fade_out(s, dt);
            }

            static substate_enum supdate_fade_out(grab_state_type& gst, xScene& s, F32& dt)
            {
                return gst.update_fade_out(s, dt);
            }
        };

        struct drop_state_type : state_type
        {
            enum substate_enum
            {
                SS_INVALID = -1,
                SS_MOVING_IN,
                SS_STOPPING,
                SS_STOPPED,
                SS_STARTING,
                SS_MOVING_OUT,
                SS_START_FADE_IN,
                SS_FADE_IN,
                MAX_SS
            };

            substate_enum move_substate;
            substate_enum fade_substate;
            xVec3 player_start;
            F32 stop_time;
            F32 fade_start_time;
            F32 fade_time;
            substate_enum (*updatess[MAX_SS])(drop_state_type&, xScene&, F32&);

            drop_state_type() : state_type(STATE_DROP)
            {
                updatess[SS_MOVING_IN] = &supdate_moving_in;
                updatess[SS_STOPPING] = &supdate_stopping;
                updatess[SS_STOPPED] = &supdate_stopped;
                updatess[SS_STARTING] = &supdate_starting;
                updatess[SS_MOVING_OUT] = &supdate_moving_out;
                updatess[SS_START_FADE_IN] = &supdate_start_fade_in;
                updatess[SS_FADE_IN] = &supdate_fade_in;
            }

            void start()
            {
                player_start = globals.player.checkpoint.pos;
                shared.first_reset_frame = TRUE;
                move_substate = shared.model != NULL ? SS_MOVING_IN : SS_INVALID;

                shared.vel = fixed.drop.in_vel;
                shared.accel = 0.0f;
                shared.loc.x = fixed.out_loc.x;
                shared.loc.y = fixed.out_loc.y;

                fade_substate = SS_START_FADE_IN;
                fade_start_time = fixed.drop.fade_start_time;
                fade_time = fixed.drop.fade_time;

                zEntPlayerUpdateModel();
                zEntPlayerControlOff(CONTROL_OWNER_OOB);
                zCameraDisableTracking(CO_OOB);
                globals.player.ControlOffTimer = FLOAT_MAX;
                xScrFxStopFade();

                xEnt& p = player_ent();
                xModelInstance& m = *p.model;
                xEntFrame& f = *p.frame;

                f.rot.axis = g_Y3;
                f.rot.angle = globals.player.checkpoint.rot;
                f.vel = g_O3;

                xMat3x3Euler(&f.mat, f.rot.angle, 0.0f, 0.0f);
                f.mat.pos = globals.player.checkpoint.pos;
                *(xMat4x3*)m.Mat = f.mat;
                shared_target = f.mat;

                globals.oldSkoolCamera.tgt_omat = &shared_target;
                globals.oldSkoolCamera.tgt_mat = &shared_target;
                zCameraReset(&globals.oldSkoolCamera);

                xAnimState* idle_anim_state = xAnimTableGetState(m.Anim->Table, "Idle01");
                xAnimPlaySetState(m.Anim->Single, idle_anim_state, 0.0f);
            }

            void stop()
            {
                zEntPlayerControlOn(CONTROL_OWNER_OOB);
                globals.player.ControlOffTimer = 1.0f;

                reset_camera();

                shared.render_hand = FALSE;
                shared.fade_alpha = 1.0f;
                shared.flags &= ~0x4;

                if (shared.model != NULL)
                {
                    xModelInstanceFree(shared.model);
                    shared.model = NULL;
                }
            }

            state_enum update(xScene& s, F32& dt)
            {
                if (shared.model == NULL)
                {
                    return STATE_IN;
                }

                F32 movedt = dt;
                while (move_substate != SS_INVALID)
                {
                    substate_enum newstate = updatess[move_substate](*this, s, movedt);
                    if (newstate == move_substate)
                    {
                        break;
                    }

                    move_substate = newstate;
                }

                F32 fadedt = dt;
                while (fade_substate != SS_INVALID)
                {
                    substate_enum newstate = updatess[fade_substate](*this, s, fadedt);
                    if (newstate == fade_substate)
                    {
                        break;
                    }

                    fade_substate = newstate;
                }

                if (move_substate == SS_INVALID && fade_substate == SS_INVALID)
                {
                    return STATE_IN;
                }

                if (move_substate == SS_MOVING_IN || move_substate == SS_STOPPING)
                {
                    xMat4x3& pm = *(xMat4x3*)player_ent().model->Mat;
                    pm.pos = player_start;

                    move_posAdjust = 0.0f;
                    move_up(pm.pos, fixed.in_loc.y - shared.loc.y);
                    move_right(pm.pos, fixed.in_loc.x - shared.loc.x);
                }

                if (shared.control)
                {
                    xAnimSingle* asingle = player_ent().model->Anim->Single;
                    if (asingle->State->Data->NumAnims[0] == 3)
                    {
                        asingle->BilinearLerp[0] = 1.0f;
                    }

                    player_ent().model->Flags &= (U16)~0x100;
                    xModelUpdate(player_ent().model, 0.0f);
                    player_ent().model->Flags |= 0x100;

                    old_before_animmat = player_ent().model->Anim->BeforeAnimMatrices;
                    player_ent().model->Anim->BeforeAnimMatrices = oob_before_anim_mat;
                    xModelEval(player_ent().model);
                    player_ent().model->Anim->BeforeAnimMatrices = old_before_animmat;
                }

                return STATE_DROP;
            }

            substate_enum update_moving_in(xScene& s, F32& dt)
            {
                move_hand(dt);

                F32 dist2 = (fixed.in_loc - shared.loc).length2();
                if (dist2 > SQR(fixed.drop.in_stop_dist))
                {
                    return SS_MOVING_IN;
                }

                // not in the DWARF: retail reads the velocity before the inlined xsqrt and keeps it
                // in a register across it
                F32 vel = shared.vel;
                shared.accel = (-vel * vel) / (2.0f * xsqrt(dist2));
                return SS_STOPPING;
            }

            substate_enum update_stopping(xScene& s, F32& dt)
            {
                move_hand(dt);

                if (shared.vel > 0.0f)
                {
                    return SS_STOPPING;
                }

                shared.loc = fixed.in_loc;
                shared.vel = shared.accel = 0.0f;
                stop_time = fixed.drop.out_wait_time;

                return SS_STOPPED;
            }

            substate_enum update_stopped(xScene& s, F32& dt)
            {
                stop_time -= dt;
                if (stop_time > 0.0f)
                {
                    return SS_STOPPED;
                }

                dt += stop_time;
                shared.accel =
                    (-fixed.drop.out_vel * fixed.drop.out_vel) / (2.0f * fixed.drop.out_start_dist);
                return SS_STARTING;
            }

            substate_enum update_starting(xScene& s, F32& dt)
            {
                move_hand(dt);

                if (shared.vel > -fixed.drop.out_vel)
                {
                    return SS_STARTING;
                }

                shared.vel = -fixed.drop.out_vel;
                shared.accel = 0.0f;
                return SS_MOVING_OUT;
            }

            substate_enum update_moving_out(xScene& s, F32& dt)
            {
                move_hand(dt);

                if ((shared.loc - fixed.out_loc).dot(shared.dir) >= 0.0f)
                {
                    return SS_MOVING_OUT;
                }

                dt = 0.0f;
                return SS_INVALID;
            }

            substate_enum update_start_fade_in(xScene& s, F32& dt)
            {
                fade_start_time -= dt;
                if (fade_start_time > 0.0f)
                {
                    return SS_START_FADE_IN;
                }

                dt += fade_start_time;
                return SS_FADE_IN;
            }

            substate_enum update_fade_in(xScene& s, F32& dt)
            {
                fade_time -= dt;
                shared.fade_alpha = 1.0f - fade_time / fixed.drop.fade_time;
                if (shared.fade_alpha > 1.0f)
                {
                    shared.fade_alpha = 1.0f;
                }

                if (fade_time > 0.0f)
                {
                    return SS_FADE_IN;
                }

                dt += fade_time;
                return SS_INVALID;
            }

            static substate_enum supdate_moving_in(drop_state_type& gst, xScene& s, F32& dt)
            {
                return gst.update_moving_in(s, dt);
            }

            static substate_enum supdate_stopping(drop_state_type& gst, xScene& s, F32& dt)
            {
                return gst.update_stopping(s, dt);
            }

            static substate_enum supdate_stopped(drop_state_type& gst, xScene& s, F32& dt)
            {
                return gst.update_stopped(s, dt);
            }

            static substate_enum supdate_starting(drop_state_type& gst, xScene& s, F32& dt)
            {
                return gst.update_starting(s, dt);
            }

            static substate_enum supdate_moving_out(drop_state_type& gst, xScene& s, F32& dt)
            {
                return gst.update_moving_out(s, dt);
            }

            static substate_enum supdate_start_fade_in(drop_state_type& gst, xScene& s, F32& dt)
            {
                return gst.update_start_fade_in(s, dt);
            }

            static substate_enum supdate_fade_in(drop_state_type& gst, xScene& s, F32& dt)
            {
                return gst.update_fade_in(s, dt);
            }
        };
    } // namespace OOBState
} // namespace oob_state

namespace oob_state
{
    using namespace OOBState;
}

namespace oob_state
{
    namespace OOBState
    {
        static void oob_before_anim_mat(xAnimPlay* play, xQuat* quat, xVec3* tran, S32 boneCount)
        {
            if (old_before_animmat != NULL)
            {
                old_before_animmat(play, quat, tran, boneCount);
            }

            xVec3 adjustTran;
            xMat3x3Tolocal(&adjustTran, (xMat3x3*)play->ModelInst->Mat, &move_posAdjust);
            *tran += adjustTran;
        }
    } // namespace OOBState
} // namespace oob_state

void oob_state::OOBState::render_ghost()
{
    if (shared.first_reset_frame)
    {
        shared.first_reset_frame = FALSE;
        return;
    }

    xEnt& ent = player_ent();

    iDrawSetFBMSK(0xFFFFFFFF, 0xFFFFFF, true);
    zRenderState(SDRS_OOBPlayerZ);

    for (xModelInstance* xm = ent.model; xm != NULL; xm = xm->Next)
    {
        RpAtomic& model = *xm->Data;

        if (xm->Flags & 0x1)
        {
            if (xm != ent.model)
            {
                memcpy(xm->Mat, ent.model->Mat, xm->BoneCount * sizeof(RwMatrix));
            }

            iModelSetWorldBoundFromBox(&model, &xm->animBound);
            iModelRender(&model, xm->Mat);
        }
    }

    iDrawSetFBMSK(0, 0xFFFFFF, true);
    zRenderState(SDRS_OOBPlayerAlpha);
    xLightKit_Enable(ent.model->LightKit, globals.currWorld);

    U8 alpha = 255.0f * (1.0f - shared.fade_alpha) + 0.5f;

    for (xModelInstance* xm = ent.model; xm != NULL; xm = xm->Next)
    {
        RpAtomic& model = *xm->Data;

        if (xm->Flags & 0x1)
        {
            if (xm != ent.model)
            {
                memcpy(xm->Mat, ent.model->Mat, xm->BoneCount * sizeof(RwMatrix));
            }

            iModelSetMaterialAlpha(&model, alpha);
            iModelRender(&model, xm->Mat);
            iModelResetMaterial(&model);
        }
    }

    xLightKit_Enable(NULL, globals.currWorld);
}

void oob_state::load_settings(xIniFile& ini)
{
    fixed.hand_model = (char*)xIniGetString(&ini, "player.state.out_of_bounds.hand_model", "hand");
    fixed.out_time = xIniGetFloat(&ini, "player.state.out_of_bounds.out_time", 9.0f);
    fixed.reset_time = xIniGetFloat(&ini, "player.state.out_of_bounds.reset_time", 3.0f);
    fixed.cam_dist = xIniGetFloat(&ini, "player.state.out_of_bounds.cam_dist", 3.0f);
    fixed.cam_height = xIniGetFloat(&ini, "player.state.out_of_bounds.cam_height", 1.0f);
    fixed.cam_pitch =
        0.017453292f * xIniGetFloat(&ini, "player.state.out_of_bounds.cam_pitch", 10.0f);
    fixed.reorient_time = xIniGetFloat(&ini, "player.state.out_of_bounds.reorient_time", 0.25f);
    fixed.in_loc.x = xIniGetFloat(&ini, "player.state.out_of_bounds.in_loc.x", 0.2f);
    fixed.in_loc.y = xIniGetFloat(&ini, "player.state.out_of_bounds.in_loc.y", 0.0f);
    fixed.out_loc.x = xIniGetFloat(&ini, "player.state.out_of_bounds.out_loc.x", 1.0f);
    fixed.out_loc.y = xIniGetFloat(&ini, "player.state.out_of_bounds.out_loc.y", 0.0f);
    fixed.grab.in_wait_time =
        xIniGetFloat(&ini, "player.state.out_of_bounds.grab.in_wait_time", 0.0f);
    fixed.grab.in_vel = xIniGetFloat(&ini, "player.state.out_of_bounds.grab.in_vel", 2.0f);
    fixed.grab.in_stop_dist =
        xIniGetFloat(&ini, "player.state.out_of_bounds.grab.in_stop_dist", 0.05f);
    fixed.grab.out_wait_time =
        xIniGetFloat(&ini, "player.state.out_of_bounds.grab.out_wait_time", 0.5f);
    fixed.grab.out_vel = xIniGetFloat(&ini, "player.state.out_of_bounds.grab.out_vel", 0.5f);
    fixed.grab.out_start_dist =
        xIniGetFloat(&ini, "player.state.out_of_bounds.grab.out_start_dist", 0.1f);
    fixed.grab.fade_start_time =
        xIniGetFloat(&ini, "player.state.out_of_bounds.grab.fade_start_time", 0.0f);
    fixed.grab.fade_time = xIniGetFloat(&ini, "player.state.out_of_bounds.grab.fade_time", 2.0f);
    fixed.drop.in_vel = xIniGetFloat(&ini, "player.state.out_of_bounds.drop.in_vel", 0.5f);
    fixed.drop.in_stop_dist =
        xIniGetFloat(&ini, "player.state.out_of_bounds.drop.in_stop_dist", 0.1f);
    fixed.drop.out_wait_time =
        xIniGetFloat(&ini, "player.state.out_of_bounds.drop.out_wait_time", 0.5f);
    fixed.drop.out_vel = xIniGetFloat(&ini, "player.state.out_of_bounds.drop.out_vel", 2.0f);
    fixed.drop.out_start_dist =
        xIniGetFloat(&ini, "player.state.out_of_bounds.drop.out_start_dist", 0.05f);
    fixed.drop.fade_start_time =
        xIniGetFloat(&ini, "player.state.out_of_bounds.drop.fade_start_time", 0.0f);
    fixed.drop.fade_time = xIniGetFloat(&ini, "player.state.out_of_bounds.drop.fade_time", 1.5f);
    fixed.hand_size_x = xIniGetFloat(&ini, "player.state.out_of_bounds.hand_size_x", 0.4f);
    fixed.hand_size_y = xIniGetFloat(&ini, "player.state.out_of_bounds.hand_size_y", 0.4f);
    fixed.hand_yaw = 0.017453292f * xIniGetFloat(&ini, "player.state.out_of_bounds.hand_yaw", 0.0f);
    fixed.hand_pitch =
        0.017453292f * xIniGetFloat(&ini, "player.state.out_of_bounds.hand_pitch", 0.0f);
    fixed.hand_roll =
        0.017453292f * xIniGetFloat(&ini, "player.state.out_of_bounds.hand_roll", 90.0f);
}

void oob_state::reset()
{
    shared.control = FALSE;
    zEntPlayerControlOn(CONTROL_OWNER_OOB);

    if ((shared.flags & 0x7) == 0x3)
    {
        shared.state->stop();
        shared.state = shared.states[OOBState::STATE_IN];
        shared.state->start();
    }
}

U8 oob_state::update(xScene& s, F32 dt)
{
    if ((shared.flags & 0x3) != 0x3)
    {
        return FALSE;
    }

    for (;;)
    {
        OOBState::state_enum newtype = shared.state->update(s, dt);
        if (newtype == shared.state->type)
        {
            break;
        }

        shared.state->stop();
        shared.state = shared.states[newtype];
        shared.state->start();
    }

    return shared.control;
}

bool oob_state::IsPlayerInControl()
{
    return shared.control == 0;
}

bool oob_state::render()
{
    if ((shared.flags & 0x3) != 0x3)
    {
        return false;
    }

    if (!shared.control)
    {
        return false;
    }

    if (player_is_sponge_ball() || player_is_boulder_rolling())
    {
        return true;
    }

    xLightKit_Enable(player_ent().model->LightKit, globals.currWorld);
    xEntRender(&player_ent());
    xLightKit_Enable(NULL, globals.currWorld);

    return true;
}

void oob_state::fx_render()
{
    if ((shared.flags & 0x3) != 0x3)
    {
        return;
    }

    if (shared.control && shared.fade_alpha < 1.0f)
    {
        render_fade();

        if (!player_is_sponge_ball() && !player_is_boulder_rolling())
        {
            render_ghost();
        }
    }

    if (shared.render_hand && shared.model != NULL)
    {
        render_hand();
    }
}

void oob_state::force_start()
{
    if ((shared.flags & 0x7) == 0x3)
    {
        shared.state->stop();
        shared.state = shared.states[OOBState::STATE_GRAB];
        shared.state->start();
    }
}

// No GC symbol: zEntPlayerCommonInit inlines it and the linker dropped the out-of-line copy.
void oob_state::init()
{
    memset(&shared, 0, sizeof(shared));
    shared.flags = 0x1;

    if ((shared.flags & 0x1) != 0x1)
    {
        return;
    }

    shared.flags |= 0x2;

    static in_state_type in_state;
    shared.states[OOBState::STATE_IN] = &in_state;

    static out_state_type out_state;
    shared.states[OOBState::STATE_OUT] = &out_state;

    static grab_state_type grab_state;
    shared.states[OOBState::STATE_GRAB] = &grab_state;

    static drop_state_type drop_state;
    shared.states[OOBState::STATE_DROP] = &drop_state;

    shared.state = shared.states[OOBState::STATE_IN];
    shared.max_out_time = fixed.out_time;
    shared.fade_alpha = 1.0f;
    shared.render_hand = FALSE;
}

U8 oob_state::enabled()
{
    if ((shared.flags & 0x3) != 0x3)
    {
        return FALSE;
    }

    return shared.control;
}

U8 oob_state::update_camera(xMat4x3* matrix, F32 dt)
{
    if (shared.control)
    {
        shared.camlerp += 3.0f * dt;
    }
    else
    {
        shared.camlerp -= 3.0f * dt;
    }

    if (shared.camlerp < 0.0f)
    {
        shared.camlerp = 0.0f;
        return FALSE;
    }

    if (shared.camlerp > 1.0f)
    {
        shared.camlerp = 1.0f;
    }

    F32 lerp = -2.0f * SQR(shared.camlerp) * shared.camlerp + 3.0f * SQR(shared.camlerp);

    xVec3 fromTarget;
    xVec3 currEuler;
    xVec3 idealEuler;
    xMat3x3 tmpMat;
    xMat4x3 resultMat;

    fromTarget = matrix->pos - shared_target.pos;
    xMat3x3LookVec(&tmpMat, &fromTarget);
    xMat3x3GetEuler(&tmpMat, &idealEuler);
    idealEuler.y = fixed.cam_pitch;
    idealEuler.z = 0.0f;
    xMat3x3GetEuler(matrix, &currEuler);

    S32 i;
    F32* currf = &currEuler.x;
    F32* idealf = &idealEuler.x;
    for (i = 0; i < 3; i++, currf++, idealf++)
    {
        if (*currf - *idealf > PI)
        {
            *idealf += 2.0f * PI;
        }
        else if (*idealf - *currf > PI)
        {
            *currf += 2.0f * PI;
        }
    }

    xVec3Lerp(&currEuler, &currEuler, &idealEuler, lerp);
    xMat3x3Euler(&resultMat, &currEuler);

    // not in the DWARF: 'mag' is preloaded with 1.0f and divided only for a non-zero length, so the
    // length is a second value.
    F32 len = xsqrt(SQR(tmpMat.at.x) + SQR(tmpMat.at.z));
    F32 mag = 1.0f;
    if (len != 0.0f)
    {
        mag = 1.0f / len;
    }

    // Retail multiplies each component as a plain value, i.e. through an inlined a*(1-s) + b*s
    // helper; xCam::blend (Ratatouille's xCam.h) has that shape.
    xCam::blend(resultMat.pos.x, matrix->pos.x,
                shared_target.pos.x - tmpMat.at.x * (fixed.cam_dist * mag), lerp);
    xCam::blend(resultMat.pos.y, matrix->pos.y, shared_target.pos.y + fixed.cam_height, lerp);
    xCam::blend(resultMat.pos.z, matrix->pos.z,
                shared_target.pos.z - tmpMat.at.z * (fixed.cam_dist * mag), lerp);

    *matrix = resultMat;
    globals.screen->set_world_matrix(resultMat);
    return TRUE;
}
