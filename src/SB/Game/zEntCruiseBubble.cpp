#include "zEntCruiseBubble.h"

#include <rpskin.h>
#include <string.h>

#include "iDraw.h"
#include "iModel.h"

#include "xAnim.h"
#include "xCamera.h"
#include "xDebug.h"
#include "xDebugTweak.h"
#include "xDecal.h"
#include "xFX.h"
#include "xFXHighDynamicRange.h"
#include "xFont.h"
#include "xGrid.h"
#include "xMath.h"
#include "xMath2.h"
#include "xMath3.h"
#include "xModel.h"
#include "xScreenWarp.h"
#include "xString.h"
#include "xstransvc.h"
#include "containers.h"

#include "zCam.h"
#include "zCamera.h"
#include "zEnt.h"
#include "zEntButton.h"
#include "zEntPlayer.h"
#include "zEntSimpleObj.h"
#include "zEntTrigger.h"
#include "zFX.h"
#include "zGameExtras.h"
#include "zGrid.h"
#include "zGlobals.h"
#include "zGrid.h"
#include "zNMEHazard.h"
#include "zNMEParFX.h"
#include "zNMESupport.h"
#include "zPlatform.h"
#include "zRingControl.h"
#include "zRenderState.h"
#include "zScene.h"
#include "zShrapnel.h"
#include "zStreamedSound.h"
#include "zTalkBox.h"
#include "zAutoTweak.h"
#include "xRand.h"

#ifdef PLATFORM_PC
#include "iHudAnchor.h"
#include "iXboxDistort.h"
#endif

namespace cruise_bubble
{
    namespace CruiseBubble
    {
        enum state_enum
        {
            STATE_INVALID = -1,
            BEGIN_STATE_PLAYER,
            BACKUP_STATE_PLAYER = -1,
            STATE_PLAYER_HALT,
            STATE_PLAYER_AIM,
            STATE_PLAYER_FIRE,
            STATE_PLAYER_WAIT,
            END_STATE_PLAYER,
            BEGIN_STATE_MISSLE = 0x4,
            BACKUP_STATE_MISSLE = 0x3,
            STATE_MISSLE_APPEAR,
            STATE_MISSLE_FLY,
            STATE_MISSLE_EXPLODE,
            END_STATE_MISSLE,
            BEGIN_STATE_CAMERA = 0x7,
            BACKUP_STATE_CAMERA = 0x6,
            STATE_CAMERA_AIM,
            STATE_CAMERA_SEIZE,
            STATE_CAMERA_ATTACH,
            STATE_CAMERA_SURVEY,
            STATE_CAMERA_RESTORE,
            END_STATE_CAMERA,
            MAX_STATE = 0xc
        };

        enum thread_enum
        {
            THREAD_PLAYER,
            THREAD_MISSLE,
            THREAD_CAMERA,
            MAX_THREAD
        };

        struct state_type
        {
            state_enum type;

            state_type(state_enum t) : type(t)
            {
            }

            virtual void start()
            {
            }
            virtual void stop()
            {
            }
            virtual state_enum update(F32 dt) = 0;
            virtual void render()
            {
            }
            virtual void abort()
            {
            }
        };

        // Retail has every state member as a weak inline (emitted after the code, with the state
        // vtables), so the members are declared inline and the classes have no key function.
        struct state_player_halt : state_type
        {
            U8 first_update;
            F32 time;
            xVec3 last_motion;

            state_player_halt() : state_type(STATE_PLAYER_HALT)
            {
            }

            inline void start();
            inline void stop();
            inline state_enum update(F32 dt);
        };

        struct state_player_wait : state_type
        {
            state_player_wait() : state_type(STATE_PLAYER_WAIT)
            {
            }

            inline void start();
            inline state_enum update(F32 dt);
        };

        // Speaker placement relative to the player. Before tweak_group: retail .rodata has them
        // before register_tweaks' xVec3 initializers.
        namespace Options
        {
            namespace SpeakerMini
            {
                static const xVec3 vec_offPlyr = { -1.12f, 0.25f, 0.4f };
            } // namespace SpeakerMini

            namespace SpeakerUber
            {
                static const xVec3 vec_offPlyr = { -1.12f, 0.25f, 0.4f };
            } // namespace SpeakerUber
        } // namespace Options

        struct tweak_group
        {
            F32 aim_delay;
            struct
            {
                F32 halt_time;
                struct
                {
                    F32 turn_speed;
                    F32 anim_delta;
                } aim;
                struct
                {
                    F32 delay_wand;
                } fire;
            } player;
            struct
            {
                F32 life;
                F32 hit_dist;
                F32 crash_angle;
                F32 collide_twist;
                S32 hit_tests;
                struct
                {
                    F32 delay_show;
                    F32 delay_show_super;
                    F32 delay_fly;
                    F32 delay_fly_super;
                    xVec3 offset;
                } appear;
                struct
                {
                    F32 accel;
                    F32 max_vel;
                    F32 engine_pitch_max;
                    F32 engine_pitch_sensitivity;
                    F32 flash_interval;
                    struct
                    {
                        F32 xdelta;
                        F32 ydelta;
                        F32 xdecay;
                        F32 ydecay;
                        F32 ybound;
                        F32 roll_frac;
                    } turn;
                } fly;
                struct
                {
                    F32 hit_radius;
                    F32 hit_radius_super;
                    F32 hit_duration;
                } explode;
            } missle;
            struct
            {
                struct
                {
                    F32 dist;
                    F32 height;
                    F32 pitch;
                    F32 accel;
                    F32 max_vel;
                    F32 stick_decel;
                    F32 stick_accel;
                    F32 stick_max_vel;
                    F32 turn_speed;
                } aim;
                struct
                {
                    F32 delay;
                    F32 delay_super;
                    F32 blend_time;
                    F32 fade_dist;
                    F32 hide_dist;
                    F32 fov;
                } seize;
                struct
                {
                    F32 duration;
                    F32 min_duration;
                    F32 min_dist;
                    F32 cut_dist;
                    F32 drift_dist;
                    F32 drift_softness;
                    F32 jerk_offset;
                    F32 jerk_deflect;
                } survey;
                struct
                {
                    F32 control_delay;
                } restore;
            } camera;
            struct
            {
                F32 env_alpha;
                F32 env_coeff;
                U32 env_texture;
                F32 fresnel_alpha;
                F32 fresnel_coeff;
                U32 fresnel_texture;
            } material;
            struct
            {
                F32 dist_min;
                F32 dist_max;
                F32 ang_show;
                F32 ang_hide;
                F32 ang_show_super;
                F32 ang_hide_super;
                F32 delay_retarget;
            } reticle;
            struct
            {
                F32 sample_rate;
                F32 bubble_rate;
                F32 bubble_emit_radius;
                F32 wake_emit_radius;
            } trail;
            struct
            {
                U32 emit;
                F32 radius;
                F32 vel;
                F32 rand_vel;
            } blast;
            struct
            {
                F32 dist_min;
                F32 dist_max;
                U32 emit_min;
                U32 emit_max;
                F32 vel_min;
                F32 vel_max;
                F32 vel_perturb;
                F32 vel_angle;
                F32 rot_vel_max;
            } droplet;
            struct
            {
                F32 glow_size;
                F32 time_fade;
                F32 time_glow;
                struct
                {
                    F32 size;
                    F32 du;
                    F32 dv;
                } swirl;
                struct
                {
                    F32 size;
                    F32 du;
                    F32 dv;
                } wind;
                struct
                {
                    F32 size;
                } reticle;
                struct
                {
                    F32 size;
                } target;
                struct
                {
                    S32 font;
                    F32 font_width;
                    F32 font_height;
                    F32 x;
                    F32 y;
                    F32 glow_size;
                } timer;
            } hud;
            struct
            {
                F32 freq;
                F32 decay;
                F32 min_freq;
            } dialog;
            void* context;
            tweak_callback cb_missle_model;

            void load(xModelAssetParam* params, U32 size)
            {
                register_tweaks(true, params, size, NULL);
            }

            void register_tweaks(bool init, xModelAssetParam* ap, U32 apsize, const char*)
            {
                // Vector defaults of the tweak table. V0 (BfBB's name) is unused, but retail still
                // has its zero initializer next to V1's; V1's name is a guess.
                xVec3 V0 = { 0.0f, 0.0f, 0.0f };
                xVec3 V1 = { -0.049f, 1.75f, 1.922f };

                if (init)
                {
                    aim_delay = 0.2f;
                    auto_tweak::load_param<F32, F32>(aim_delay, 1.0f, 0.0f, 1.0f, ap, apsize,
                                                     "aim_delay");
                }

                if (init)
                {
                    player.halt_time = 0.5f;
                    auto_tweak::load_param<F32, F32>(player.halt_time, 1.0f, 0.0f, 1.0f, ap, apsize,
                                                     "player.halt_time");
                }

                if (init)
                {
                    player.aim.turn_speed = 0.05f;
                    auto_tweak::load_param<F32, F32>(player.aim.turn_speed, 1.0f, 0.01f,
                                                     1000000000.0f, ap, apsize,
                                                     "player.aim.turn_speed");
                }

                if (init)
                {
                    player.aim.anim_delta = 0.5f;
                    auto_tweak::load_param<F32, F32>(player.aim.anim_delta, 1.0f, 0.0f,
                                                     1000000000.0f, ap, apsize,
                                                     "player.aim.anim_delta");
                }

                if (init)
                {
                    player.fire.delay_wand = 0.06666667f;
                    auto_tweak::load_param<F32, F32>(player.fire.delay_wand, 1.0f, 0.0f, 100.0f, ap,
                                                     apsize, "player.fire.delay_wand");
                }

                if (init)
                {
                    missle.life = 6.0f;
                    auto_tweak::load_param<F32, F32>(missle.life, 1.0f, 0.01f, 1000000000.0f, ap,
                                                     apsize, "missle.life");
                }

                if (init)
                {
                    missle.hit_dist = 0.3f;
                    auto_tweak::load_param<F32, F32>(missle.hit_dist, 1.0f, 0.0f, 1.0f, ap, apsize,
                                                     "missle.hit_dist");
                }

                if (init)
                {
                    missle.crash_angle = 30.0f;
                    auto_tweak::load_param<F32, F32>(missle.crash_angle, DEG2RAD(1), 0.0f, 60.0f,
                                                     ap, apsize, "missle.crash_angle");
                }

                if (init)
                {
                    missle.collide_twist = 0.025f;
                    auto_tweak::load_param<F32, F32>(missle.collide_twist, 1.0f, 0.0f, 1.0f, ap,
                                                     apsize, "missle.collide_twist");
                }

                if (init)
                {
                    missle.hit_tests = 4;
                    auto_tweak::load_param<S32, S32>(missle.hit_tests, 1, 1, 100, ap, apsize,
                                                     "missle.hit_tests");
                }

                if (init)
                {
                    missle.appear.delay_show = 0.3f;
                    auto_tweak::load_param<F32, F32>(missle.appear.delay_show, 1.0f, 0.0f, 100.0f,
                                                     ap, apsize, "missle.appear.delay_show");
                }

                if (init)
                {
                    missle.appear.delay_show_super = 0.45f;
                    auto_tweak::load_param<F32, F32>(missle.appear.delay_show_super, 1.0f, 0.0f,
                                                     100.0f, ap, apsize,
                                                     "missle.appear.delay_show_super");
                }

                if (init)
                {
                    missle.appear.delay_fly = 0.3f;
                    auto_tweak::load_param<F32, F32>(missle.appear.delay_fly, 1.0f, 0.0f, 100.0f,
                                                     ap, apsize, "missle.appear.delay_fly");
                }

                if (init)
                {
                    missle.appear.delay_fly_super = 0.45f;
                    auto_tweak::load_param<F32, F32>(missle.appear.delay_fly_super, 1.0f, 0.0f,
                                                     100.0f, ap, apsize,
                                                     "missle.appear.delay_fly_super");
                }

                if (init)
                {
                    missle.appear.offset = V1;
                    auto_tweak::load_param<xVec3, S32>(missle.appear.offset, 0, 0, 0, ap, apsize,
                                                       "missle.appear.offset");
                }

                if (init)
                {
                    missle.fly.accel = 6.0f;
                    auto_tweak::load_param<F32, F32>(missle.fly.accel, 1.0f, 0.01f, 1000000000.0f,
                                                     ap, apsize, "missle.fly.accel");
                }

                if (init)
                {
                    missle.fly.max_vel = 12.0f;
                    auto_tweak::load_param<F32, F32>(missle.fly.max_vel, 1.0f, 0.01f, 1000000000.0f,
                                                     ap, apsize, "missle.fly.max_vel");
                }

                if (init)
                {
                    missle.fly.engine_pitch_max = 10.0f;
                    auto_tweak::load_param<F32, F32>(missle.fly.engine_pitch_max, 1.0f, 0.01f,
                                                     1000000000.0f, ap, apsize,
                                                     "missle.fly.engine_pitch_max");
                }

                if (init)
                {
                    missle.fly.engine_pitch_sensitivity = 0.005f;
                    auto_tweak::load_param<F32, F32>(missle.fly.engine_pitch_sensitivity, 1.0f,
                                                     0.0f, 1.0f, ap, apsize,
                                                     "missle.fly.engine_pitch_sensitivity");
                }

                if (init)
                {
                    missle.fly.flash_interval = 2.0f;
                    auto_tweak::load_param<F32, F32>(missle.fly.flash_interval, 1.0f, 0.0f,
                                                     1000000000.0f, ap, apsize,
                                                     "missle.fly.flash_interval");
                }

                if (init)
                {
                    missle.fly.turn.xdelta = 5.0f;
                    auto_tweak::load_param<F32, F32>(missle.fly.turn.xdelta, 1.0f, 0.01f,
                                                     1000000000.0f, ap, apsize,
                                                     "missle.fly.turn.xdelta");
                }

                if (init)
                {
                    missle.fly.turn.ydelta = 4.0f;
                    auto_tweak::load_param<F32, F32>(missle.fly.turn.ydelta, 1.0f, 0.01f,
                                                     1000000000.0f, ap, apsize,
                                                     "missle.fly.turn.ydelta");
                }

                if (init)
                {
                    missle.fly.turn.xdecay = 0.99f;
                    auto_tweak::load_param<F32, F32>(missle.fly.turn.xdecay, 1.0f, 0.0f, 1.0f, ap,
                                                     apsize, "missle.fly.turn.xdecay");
                }

                if (init)
                {
                    missle.fly.turn.ydecay = 0.99f;
                    auto_tweak::load_param<F32, F32>(missle.fly.turn.ydecay, 1.0f, 0.0f, 1.0f, ap,
                                                     apsize, "missle.fly.turn.ydecay");
                }

                if (init)
                {
                    missle.fly.turn.ybound = 0.6f;
                    auto_tweak::load_param<F32, F32>(missle.fly.turn.ybound, PI / 2, 0.0f, 1.0f, ap,
                                                     apsize, "missle.fly.turn.ybound");
                }

                if (init)
                {
                    missle.fly.turn.roll_frac = 0.2f;
                    auto_tweak::load_param<F32, F32>(missle.fly.turn.roll_frac, 1.0f, -1.0f, 1.0f,
                                                     ap, apsize, "missle.fly.turn.roll_frac");
                }

                if (init)
                {
                    missle.explode.hit_radius = 1.0f;
                    auto_tweak::load_param<F32, F32>(missle.explode.hit_radius, 1.0f, 0.0f, 10.0f,
                                                     ap, apsize, "missle.explode.hit_radius");
                }

                if (init)
                {
                    missle.explode.hit_radius_super = 4.0f;
                    auto_tweak::load_param<F32, F32>(missle.explode.hit_radius_super, 1.0f, 0.0f,
                                                     10.0f, ap, apsize,
                                                     "missle.explode.hit_radius_super");
                }

                if (init)
                {
                    missle.explode.hit_duration = 0.25f;
                    auto_tweak::load_param<F32, F32>(missle.explode.hit_duration, 1.0f, 0.0f, 10.0f,
                                                     ap, apsize, "missle.explode.hit_duration");
                }

                if (init)
                {
                    camera.aim.dist = 2.0f;
                    auto_tweak::load_param<F32, F32>(camera.aim.dist, 1.0f, 0.0f, 100.0f, ap,
                                                     apsize, "camera.aim.dist");
                }

                if (init)
                {
                    camera.aim.height = 1.5f;
                    auto_tweak::load_param<F32, F32>(camera.aim.height, 1.0f, -10.0f, 10.0f, ap,
                                                     apsize, "camera.aim.height");
                }

                if (init)
                {
                    camera.aim.pitch = 0.0f;
                    auto_tweak::load_param<F32, F32>(camera.aim.pitch, DEG2RAD(1), -90.0f, 90.0f,
                                                     ap, apsize, "camera.aim.pitch");
                }

                if (init)
                {
                    camera.aim.accel = 10.0f;
                    auto_tweak::load_param<F32, F32>(camera.aim.accel, 1.0f, 0.01f, 1000000000.0f,
                                                     ap, apsize, "camera.aim.accel");
                }

                if (init)
                {
                    camera.aim.max_vel = 5.0f;
                    auto_tweak::load_param<F32, F32>(camera.aim.max_vel, 1.0f, 0.01f, 1000000000.0f,
                                                     ap, apsize, "camera.aim.max_vel");
                }

                if (init)
                {
                    camera.aim.stick_decel = 720.0f;
                    auto_tweak::load_param<F32, F32>(camera.aim.stick_decel, DEG2RAD(1), 0.01f,
                                                     1000000000.0f, ap, apsize,
                                                     "camera.aim.stick_decel");
                }

                if (init)
                {
                    camera.aim.stick_accel = 360.0f;
                    auto_tweak::load_param<F32, F32>(camera.aim.stick_accel, DEG2RAD(1), 0.01f,
                                                     1000000000.0f, ap, apsize,
                                                     "camera.aim.stick_accel");
                }

                if (init)
                {
                    camera.aim.stick_max_vel = 135.0f;
                    auto_tweak::load_param<F32, F32>(camera.aim.stick_max_vel, DEG2RAD(1), 0.01f,
                                                     1000000000.0f, ap, apsize,
                                                     "camera.aim.stick_max_vel");
                }

                if (init)
                {
                    camera.aim.turn_speed = 0.2f;
                    auto_tweak::load_param<F32, F32>(camera.aim.turn_speed, 1.0f, 0.001f,
                                                     1000000000.0f, ap, apsize,
                                                     "camera.aim.turn_speed");
                }

                if (init)
                {
                    camera.seize.delay = 0.25f;
                    auto_tweak::load_param<F32, F32>(camera.seize.delay, 1.0f, 0.0f, 1000000000.0f,
                                                     ap, apsize, "camera.seize.delay");
                }

                if (init)
                {
                    camera.seize.delay_super = 0.4f;
                    auto_tweak::load_param<F32, F32>(camera.seize.delay_super, 1.0f, 0.0f,
                                                     1000000000.0f, ap, apsize,
                                                     "camera.seize.delay_super");
                }

                if (init)
                {
                    camera.seize.blend_time = 1.5f;
                    auto_tweak::load_param<F32, F32>(camera.seize.blend_time, 1.0f, 0.0f,
                                                     1000000000.0f, ap, apsize,
                                                     "camera.seize.blend_time");
                }

                if (init)
                {
                    camera.seize.fade_dist = 2.0f;
                    auto_tweak::load_param<F32, F32>(camera.seize.fade_dist, 1.0f, 0.0f, 10.0f, ap,
                                                     apsize, "camera.seize.fade_dist");
                }

                if (init)
                {
                    camera.seize.hide_dist = 1.0f;
                    auto_tweak::load_param<F32, F32>(camera.seize.hide_dist, 1.0f, 0.0f, 10.0f, ap,
                                                     apsize, "camera.seize.hide_dist");
                }

                if (init)
                {
                    camera.seize.fov = 95.0f;
                    auto_tweak::load_param<F32, F32>(camera.seize.fov, 1.0f, 10.0f, 180.0f, ap,
                                                     apsize, "camera.seize.fov");
                }

                if (init)
                {
                    camera.survey.duration = 2.0f;
                    auto_tweak::load_param<F32, F32>(camera.survey.duration, 1.0f, 0.0f, 10.0f, ap,
                                                     apsize, "camera.survey.duration");
                }

                if (init)
                {
                    camera.survey.min_duration = 0.25f;
                    auto_tweak::load_param<F32, F32>(camera.survey.min_duration, 1.0f, 0.0f, 10.0f,
                                                     ap, apsize, "camera.survey.min_duration");
                }

                if (init)
                {
                    camera.survey.min_dist = 10.0f;
                    auto_tweak::load_param<F32, F32>(camera.survey.min_dist, 1.0f, 0.0f, 100.0f, ap,
                                                     apsize, "camera.survey.min_dist");
                }

                if (init)
                {
                    camera.survey.cut_dist = 6.0f;
                    auto_tweak::load_param<F32, F32>(camera.survey.cut_dist, 1.0f, 0.0f, 100.0f, ap,
                                                     apsize, "camera.survey.cut_dist");
                }

                if (init)
                {
                    camera.survey.drift_dist = 8.0f;
                    auto_tweak::load_param<F32, F32>(camera.survey.drift_dist, 1.0f, 0.0f, 100.0f,
                                                     ap, apsize, "camera.survey.drift_dist");
                }

                if (init)
                {
                    camera.survey.drift_softness = 0.1f;
                    auto_tweak::load_param<F32, F32>(camera.survey.drift_softness, 1.0f, 0.0f, 0.5f,
                                                     ap, apsize, "camera.survey.drift_softness");
                }

                if (init)
                {
                    camera.survey.jerk_offset = 0.8f;
                    auto_tweak::load_param<F32, F32>(camera.survey.jerk_offset, 1.0f, 0.0f, 2.0f,
                                                     ap, apsize, "camera.survey.jerk_offset");
                }

                if (init)
                {
                    camera.survey.jerk_deflect = 0.6f;
                    auto_tweak::load_param<F32, F32>(camera.survey.jerk_deflect, 1.0f, 0.0f, 1.0f,
                                                     ap, apsize, "camera.survey.jerk_deflect");
                }

                if (init)
                {
                    camera.restore.control_delay = 0.25f;
                    auto_tweak::load_param<F32, F32>(camera.restore.control_delay, 1.0f, 0.0f, 1.0f,
                                                     ap, apsize, "camera.restore.control_delay");
                }

                if (init)
                {
                    material.env_alpha = 0.5f;
                    auto_tweak::load_param<F32, F32>(material.env_alpha, 1.0f, 0.0f, 1.0f, ap,
                                                     apsize, "material.env_alpha");
                }

                if (init)
                {
                    material.env_coeff = 0.5f;
                    auto_tweak::load_param<F32, F32>(material.env_coeff, 1.0f, 0.0f, 1.0f, ap,
                                                     apsize, "material.env_coeff");
                }

                if (init)
                {
                    material.fresnel_alpha = 0.0f;
                    auto_tweak::load_param<F32, F32>(material.fresnel_alpha, 1.0f, 0.0f, 1.0f, ap,
                                                     apsize, "material.fresnel_alpha");
                }

                if (init)
                {
                    material.fresnel_coeff = 0.75f;
                    auto_tweak::load_param<F32, F32>(material.fresnel_coeff, 1.0f, 0.0f, 1.0f, ap,
                                                     apsize, "material.fresnel_coeff");
                }

                if (init)
                {
                    reticle.dist_min = 3.0f;
                    auto_tweak::load_param<F32, F32>(reticle.dist_min, 1.0f, 1.0f, 10.0f, ap,
                                                     apsize, "reticle.dist_min");
                }

                if (init)
                {
                    reticle.dist_max = 40.0f;
                    auto_tweak::load_param<F32, F32>(reticle.dist_max, 1.0f, 1.0f, 10000.0f, ap,
                                                     apsize, "reticle.dist_max");
                }

                if (init)
                {
                    reticle.ang_show = 4.0f;
                    auto_tweak::load_param<F32, F32>(reticle.ang_show, DEG2RAD(1), 0.0f, 90.0f, ap,
                                                     apsize, "reticle.ang_show");
                }

                if (init)
                {
                    reticle.ang_hide = 22.5f;
                    auto_tweak::load_param<F32, F32>(reticle.ang_hide, DEG2RAD(1), 0.0f, 90.0f, ap,
                                                     apsize, "reticle.ang_hide");
                }

                if (init)
                {
                    reticle.ang_show_super = 20.0f;
                    auto_tweak::load_param<F32, F32>(reticle.ang_show_super, DEG2RAD(1), 0.0f,
                                                     90.0f, ap, apsize, "reticle.ang_show_super");
                }

                if (init)
                {
                    reticle.ang_hide_super = 45.5f;
                    auto_tweak::load_param<F32, F32>(reticle.ang_hide_super, DEG2RAD(1), 0.0f,
                                                     90.0f, ap, apsize, "reticle.ang_hide_super");
                }

                if (init)
                {
                    reticle.delay_retarget = 0.25f;
                    auto_tweak::load_param<F32, F32>(reticle.delay_retarget, 1.0f, 0.0f, 5.0f, ap,
                                                     apsize, "reticle.delay_retarget");
                }

                if (init)
                {
                    trail.sample_rate = 60.0f;
                    auto_tweak::load_param<F32, F32>(trail.sample_rate, 1.0f, 0.0f, 10000.0f, ap,
                                                     apsize, "trail.sample_rate");
                }

                if (init)
                {
                    trail.bubble_rate = 0.0f;
                    auto_tweak::load_param<F32, F32>(trail.bubble_rate, 1.0f, 0.0f, 10000.0f, ap,
                                                     apsize, "trail.bubble_rate");
                }

                if (init)
                {
                    trail.bubble_emit_radius = 0.5f;
                    auto_tweak::load_param<F32, F32>(trail.bubble_emit_radius, 1.0f, 0.0f, 10.0f,
                                                     ap, apsize, "trail.bubble_emit_radius");
                }

                if (init)
                {
                    trail.wake_emit_radius = 0.1f;
                    auto_tweak::load_param<F32, F32>(trail.wake_emit_radius, 1.0f, 0.0f, 10.0f, ap,
                                                     apsize, "trail.wake_emit_radius");
                }

                if (init)
                {
                    blast.emit = 300;
                    auto_tweak::load_param<U32, S32>(blast.emit, 1, 0, 1000, ap, apsize,
                                                     "blast.emit");
                }

                if (init)
                {
                    blast.radius = 0.0f;
                    auto_tweak::load_param<F32, F32>(blast.radius, 1.0f, 0.0f, 10.0f, ap, apsize,
                                                     "blast.radius");
                }

                if (init)
                {
                    blast.vel = 5.0f;
                    auto_tweak::load_param<F32, F32>(blast.vel, 1.0f, -100000.0f, 100000.0f, ap,
                                                     apsize, "blast.vel");
                }

                if (init)
                {
                    blast.rand_vel = 0.5f;
                    auto_tweak::load_param<F32, F32>(blast.rand_vel, 1.0f, -100000.0f, 100000.0f,
                                                     ap, apsize, "blast.rand_vel");
                }

                if (init)
                {
                    droplet.dist_min = 1.0f;
                    auto_tweak::load_param<F32, F32>(droplet.dist_min, 1.0f, 0.0f, 10.0f, ap,
                                                     apsize, "droplet.dist_min");
                }

                if (init)
                {
                    droplet.dist_max = 2.0f;
                    auto_tweak::load_param<F32, F32>(droplet.dist_max, 1.0f, 0.0f, 10.0f, ap,
                                                     apsize, "droplet.dist_max");
                }

                if (init)
                {
                    droplet.emit_min = 10;
                    auto_tweak::load_param<U32, S32>(droplet.emit_min, 1, 0, 30, ap, apsize,
                                                     "droplet.emit_min");
                }

                if (init)
                {
                    droplet.emit_max = 20;
                    auto_tweak::load_param<U32, S32>(droplet.emit_max, 1, 0, 30, ap, apsize,
                                                     "droplet.emit_max");
                }

                if (init)
                {
                    droplet.vel_min = 2.0f;
                    auto_tweak::load_param<F32, F32>(droplet.vel_min, 1.0f, 0.0f, 100000.0f, ap,
                                                     apsize, "droplet.vel_min");
                }

                if (init)
                {
                    droplet.vel_max = 6.0f;
                    auto_tweak::load_param<F32, F32>(droplet.vel_max, 1.0f, 0.0f, 100000.0f, ap,
                                                     apsize, "droplet.vel_max");
                }

                if (init)
                {
                    droplet.vel_perturb = 0.25f;
                    auto_tweak::load_param<F32, F32>(droplet.vel_perturb, 1.0f, 0.0f, 100000.0f, ap,
                                                     apsize, "droplet.vel_perturb");
                }

                if (init)
                {
                    droplet.vel_angle = 60.0f;
                    auto_tweak::load_param<F32, F32>(droplet.vel_angle, DEG2RAD(1), 0.0f, 100000.0f,
                                                     ap, apsize, "droplet.vel_angle");
                }

                if (init)
                {
                    droplet.rot_vel_max = 360.0f;
                    auto_tweak::load_param<F32, F32>(droplet.rot_vel_max, DEG2RAD(1), 0.0f,
                                                     100000.0f, ap, apsize, "droplet.rot_vel_max");
                }

                if (init)
                {
                    hud.glow_size = 0.05f;
                    auto_tweak::load_param<F32, F32>(hud.glow_size, 1.0f, 0.001f, 10.0f, ap, apsize,
                                                     "hud.glow_size");
                }

                if (init)
                {
                    hud.time_fade = 0.25f;
                    auto_tweak::load_param<F32, F32>(hud.time_fade, 1.0f, 0.001f, 10.0f, ap, apsize,
                                                     "hud.time_fade");
                }

                if (init)
                {
                    hud.time_glow = 0.5f;
                    auto_tweak::load_param<F32, F32>(hud.time_glow, 1.0f, 0.001f, 10.0f, ap, apsize,
                                                     "hud.time_glow");
                }

                if (init)
                {
                    hud.wind.size = 0.75f;
                    auto_tweak::load_param<F32, F32>(hud.wind.size, 1.0f, 0.0f, 100.0f, ap, apsize,
                                                     "hud.wind.size");
                }

                if (init)
                {
                    hud.wind.du = 0.0f;
                    auto_tweak::load_param<F32, F32>(hud.wind.du, 1.0f, -1000000000.0f,
                                                     1000000000.0f, ap, apsize, "hud.wind.du");
                }

                if (init)
                {
                    hud.wind.dv = 4.0f;
                    auto_tweak::load_param<F32, F32>(hud.wind.dv, 1.0f, -1000000000.0f,
                                                     1000000000.0f, ap, apsize, "hud.wind.dv");
                }

                if (init)
                {
                    hud.reticle.size = 0.1f;
                    auto_tweak::load_param<F32, F32>(hud.reticle.size, 1.0f, 0.0f, 100.0f, ap,
                                                     apsize, "hud.reticle.size");
                }

                if (init)
                {
                    hud.target.size = 0.1f;
                    auto_tweak::load_param<F32, F32>(hud.target.size, 1.0f, 0.0f, 100.0f, ap,
                                                     apsize, "hud.target.size");
                }

                if (init)
                {
                    hud.timer.font = 2;
                    auto_tweak::load_param<S32, S32>(hud.timer.font, 1, 0, 4, ap, apsize,
                                                     "hud.timer.font");
                }

                if (init)
                {
                    hud.timer.font_width = 0.0275f;
                    auto_tweak::load_param<F32, F32>(hud.timer.font_width, 1.0f, 0.001f, 1.0f, ap,
                                                     apsize, "hud.timer.font_width");
                }

                if (init)
                {
                    hud.timer.font_height = 0.047f;
                    auto_tweak::load_param<F32, F32>(hud.timer.font_height, 1.0f, 0.001f, 1.0f, ap,
                                                     apsize, "hud.timer.font_height");
                }

                if (init)
                {
                    hud.timer.x = 0.78f;
                    auto_tweak::load_param<F32, F32>(hud.timer.x, 1.0f, 0.0f, 1.0f, ap, apsize,
                                                     "hud.timer.x");
                }

                if (init)
                {
                    hud.timer.y = 0.86f;
                    auto_tweak::load_param<F32, F32>(hud.timer.y, 1.0f, 0.0f, 1.0f, ap, apsize,
                                                     "hud.timer.y");
                }

                if (init)
                {
                    hud.timer.glow_size = 0.01f;
                    auto_tweak::load_param<F32, F32>(hud.timer.glow_size, 1.0f, 0.0f, 10.0f, ap,
                                                     apsize, "hud.timer.glow_size");
                }

                if (init)
                {
                    dialog.freq = 1.0f;
                    auto_tweak::load_param<F32, F32>(dialog.freq, 1.0f, 0.0f, 1.0f, ap, apsize,
                                                     "dialog.freq");
                }

                if (init)
                {
                    dialog.decay = 0.75f;
                    auto_tweak::load_param<F32, F32>(dialog.decay, 1.0f, 0.0f, 1.0f, ap, apsize,
                                                     "dialog.decay");
                }

                if (init)
                {
                    dialog.min_freq = 0.1f;
                    auto_tweak::load_param<F32, F32>(dialog.min_freq, 1.0f, 0.0f, 1.0f, ap, apsize,
                                                     "dialog.min_freq");
                }

                if (init)
                {
                    material.fresnel_texture = xStrHash("gloss_edge");
                    material.env_texture = xStrHash("rainbowfilm_smooth32");
                }
            }
        };

        struct uv_animated_model
        {
            RpAtomic* model;
            RwTexCoords* uv;
            S32 uvsize;
            xVec2 offset;
            xVec2 offset_vel;

            // No symbols in TSSM (always inlined); bodies from BfBB.
            bool init(RpAtomic* m)
            {
                model = m;
                if (m == NULL)
                {
                    return false;
                }

                if (!clone_uv(uv, uvsize, m))
                {
                    return false;
                }

                offset.assign(0.0f, 0.0f);
                offset_vel.assign(0.0f, 0.0f);
                return true;
            }

            bool clone_uv(RwTexCoords*& coords, S32& size, RpAtomic* m) const
            {
                RwTexCoords* c;
                if (!get_uv(c, size, m))
                {
                    return false;
                }

                coords = (RwTexCoords*)xMemAlloc(gActiveHeap, size * sizeof(RwTexCoords), 0);
                if (coords == NULL)
                {
                    return false;
                }

                memcpy(coords, c, size * sizeof(RwTexCoords));
                return true;
            }

            bool get_uv(RwTexCoords*& coords, S32& size, RpAtomic* m) const
            {
                coords = NULL;
                size = 0;

                RpGeometry* geo = m->geometry;
                if (geo == NULL)
                {
                    return false;
                }

                size = geo->numVertices;
                if (size <= 0)
                {
                    return false;
                }

                coords = geo->texCoords[0];
                return coords != NULL;
            }

            // No symbols in TSSM (always inlined); bodies from BfBB.
            void update(F32 dt)
            {
                if (0.0f == offset_vel.x && 0.0f == offset_vel.y)
                {
                    return;
                }

                offset += offset_vel * dt;
                offset.x = xfmod(offset.x, 1.0f);
                offset.y = xfmod(offset.y, 1.0f);
                refresh();
            }

            void refresh()
            {
                RpGeometry* geo = model->geometry;
                RwTexCoords* src = uv;
                RwTexCoords* end = uv + uvsize;
                RwTexCoords* dst = geo->texCoords[0];

                RpGeometryLock(geo, rpGEOMETRYLOCKTEXCOORDS1);

                while (src < end)
                {
                    dst->u = src->u + offset.x;
                    dst->v = src->v + offset.y;
                    ++src;
                    ++dst;
                }

                RpGeometryUnlock(geo);
            }
        };

        struct sound_config
        {
            const char* name;
            F32 volume;
            F32 radius_inner;
            F32 radius_outer;
            U8 streamed;
            U8 looping;
            const char* rumble_name;
            S32 first;
            U32 id;
            iSndHandle handle;
            xBase* rumble_effect;
        };

        struct hud_gizmo
        {
            S32 flags;
            basic_rect<F32> bound;
            F32 alpha;
            F32 alpha_vel;
            F32 glow;
            F32 glow_vel;
            F32 opacity;
            const xVec3* target;
            xModelInstance* model;
        };

        struct missle_record_data
        {
            xVec3 loc;
            F32 roll;

            missle_record_data()
            {
            }

            // No symbol in TSSM (always inlined); body from BfBB.
            missle_record_data(const xVec3& loc, F32 roll) : loc(loc), roll(roll)
            {
            }
        };

        namespace CBFXtra
        {
            enum en_tunefx
            {
                TUNE_BANG_LEFT,
                TUNE_BANG_RYHT,
                TUNE_SPEAKER,
                TUNE_FLIGHT,
                TUNE_FIZZLE,
                TUNE_EXPLODE,
                TUNE_NOMORE
            };

            enum en_tunetran
            {
                TUNE_TRAN_ENTER,
                TUNE_TRAN_EXIT,
                TUNE_TRAN_UPDATE,
                TUNE_TRAN_NOMORE
            };

            struct CBFXData
            {
                zNMEHazard* haz_speaker;
                zNMEHazard* haz_flight;
            };

            struct HazGone : HAZNotify
            {
                S32 Notify(en_haznote note, zNMEHazard* haz);
            };

            void Speak_Update(F32 dt);
            void Bang_Right();
            void Bang_Left();
            void RockStar_Update(F32 dt);
            void RockStar_Action(en_tunefx tunefx, en_tunetran tran);
            void SceneTimestep(F32 dt);
        } // namespace CBFXtra

        void render_rings();
        void RenderOneRing(xVec3 pos, F32 radius, xColor_tag color, RxObjSpace3DVertex** vertList,
                           S32 first);
        void check_lock_target(const xEnt* ent);
        void lock_target(S32 index, const xVec3* target, F32 opacity);
        void notify_triggers(xScene& s, const xSphere& o, const xVec3& dir);
        void damage_entity(xEnt& ent, const xVec3& loc, const xVec3& dir, const xVec3& hit_norm,
                           F32 radius, bool explosive);
        void render_hud();
        void update_hud(F32 dt);
        void render_timer(F32 alpha, F32 glow);
        void init_hud();
        void update_trail(F32 dt);
        void add_trail_sample(const xVec3& loc0, const xVec3& dir0, const xVec3& loc1,
                              const xVec3& dir1, F32 dt);
        void reset_wake_ribbons();
        void init_states();
        U32 check_anim_aim(xAnimTransition*, xAnimSingle*, void*);
        RpAtomic* custom_bubble_render(RpAtomic* atomic);
        void kill(bool reset_camera, bool abortive);
        void load_settings();
    } // namespace CruiseBubble
} // namespace cruise_bubble

using namespace cruise_bubble::CruiseBubble;

extern xEnt* gReticleTarget[2];
// zEntPlayer.cpp's statics, defined later in this wad (CW gives the later static definitions
// internal linkage). Not standard C++; the original probably reached them through an inlined
// zEntPlayer.cpp helper.
extern F32 sReticleAlpha[2];
extern S32 sTypeOfTarget[2];

void (*cruise_bubble::Cam_Launch)();
void (*cruise_bubble::Cam_Return)();

namespace cruise_bubble
{
    namespace CruiseBubble
    {
        tweak_group normal_tweak;
        tweak_group cheat_tweak;
        xBase base;
        xMat4x3 start_cam_mat;
        tweak_group* current_tweak = &normal_tweak;
        static zEntSimpleObj* curSourceSimpObj;

        const char* start_anim_states[17] = { "Idle01", "SlipIdle01", "Inactive01", "Inactive02",
                                              "Walk01", "Run01",      "Run02",      "Run03",
                                              "Land01", "LandRun01",  "LandHigh01", "WallLand01",
                                              "Hit01",  "Hit02",      "Hit03",      "Hit04",
                                              "Hit05" };

        struct
        {
            S32 flags;
            state_type* state[MAX_THREAD];
            state_type* states[MAX_STATE];
            xVec2 last_sp;
            xVec2 sp;
            xVec3 hit_loc;
            xVec3 hit_norm;
            xModelInstance* missle_model;
            xEnt* hits[32];
            S32 hits_size;
            U32 player_health;
            xVec3 player_motion;
            F32 fov_default;
            zShrapnelAsset* droplet_shrapnel;
            F32 dialog_freq;
            F32 last_check_dist;
            struct
            {
                F32 samples;
                F32 bubbles;
                xMat4x3 mat;
                xQuat dir;
            } trail;
            struct
            {
                struct
                {
                    xAnimState* aim;
                    xAnimState* fire;
                    xAnimState* fire2;
                    xAnimState* idle;
                } player;
                struct
                {
                    xAnimState* fire;
                    xAnimState* fly;
                } missle;
            } astate;
            struct
            {
                struct
                {
                    xAnimTransition* aim;
                    xAnimTransition* fire;
                    xAnimTransition* fire2;
                    xAnimTransition* idle;
                    xAnimTransition* end;
                } player;
                struct
                {
                    xAnimTransition* fly;
                } missle;
            } atran;
        } shared = { 0x1 };

        fixed_queue<missle_record_data, 127> missle_record;
        xFXRibbon wake_ribbon[2];
        xDecalEmitter explode_decal;

        const xFXRibbon::curve_node wake_ribbon_curve[7] = {
            { 0.0f, { 0xFF, 0x00, 0x00, 0x32 }, 0.0f }, { 0.1f, { 0xFF, 0x00, 0xFF, 0x64 }, 0.5f },
            { 0.3f, { 0x00, 0x00, 0xFF, 0x32 }, 1.0f }, { 0.5f, { 0x00, 0xFF, 0xFF, 0x32 }, 0.6f },
            { 0.7f, { 0x00, 0xFF, 0x00, 0x32 }, 0.8f }, { 0.9f, { 0xFF, 0xFF, 0x00, 0x32 }, 0.3f },
            { 1.0f, { 0xFF, 0xFF, 0xFF, 0x00 }, 0.0f },
        };

        const xFXRibbon::curve_node cheat_wake_ribbon_curve[4] = {
            { 0.0f, { 0xFF, 0x9B, 0x9B, 0x64 }, 0.0f },
            { 0.1f, { 0xFF, 0x9B, 0x9B, 0x64 }, 0.5f },
            { 0.9f, { 0xFF, 0x9B, 0x9B, 0x64 }, 2.0f },
            { 1.0f, { 0x00, 0x00, 0x00, 0x00 }, 0.0f },
        };

        const xDecalEmitter::curve_node explode_curve[3] = {
            { 0.0f, { 0xFF, 0xFF, 0xFF, 0xFF }, 0.1f },
            { 0.5f, { 0xFF, 0xFF, 0xFF, 0xFF }, 2.55f },
            { 1.0f, { 0xFF, 0xFF, 0xFF, 0x00 }, 5.0f },
        };

        const xDecalEmitter::curve_node cheat_explode_curve[3] = {
            { 0.0f, { 0xFF, 0xFF, 0xFF, 0xFF }, 0.1f },
            { 0.5f, { 0xFF, 0xFF, 0xFF, 0xFF }, 3.55f },
            { 1.0f, { 0xFF, 0xFF, 0xFF, 0x00 }, 7.0f },
        };

        xVec3 ringPos;
        F32 ringRadius;
        S32 ringAlpha;
        U8 renderRing;
        RwTexture* ringTex;

        sound_config sounds[8] = {
            { "SB_cruise_start_grp", 0.7f, 5.0f, 20.0f, 0, 0, "RUMBLE_CRUISE_BUBBLE_LAUNCH" },
            { "SB_cruise_hit_grp", 1.0f, 15.0f, 60.0f, 0, 0, "RUMBLE_CRUISE_BUBBLE_EXPLODE" },
            { "SB_cruise_nav_loop_grp", 0.5f, 0.0f, 10.0f, 0, 1, NULL },
            { NULL, 1.0f, 0.0f, 0.0f, 1, 0, NULL, 11 },
            { "SB_GUITAR2_LAUNCH_GRP", 0.7f, 5.0f, 20.0f, 0, 0, "RUMBLE_CRUISE_BUBBLE_LAUNCH" },
            { "SB_GUITAR2_HIT_GRP", 0.9f, 15.0f, 60.0f, 0, 0, "RUMBLE_CRUISE_BUBBLE_EXPLODE" },
            { "SB_GUITAR2_LOOP_GRP", 0.5f, 0.0f, 10.0f, 0, 1, NULL },
            { "SB_GUITAR2_LOCK_GRP", 0.4f, 0.0f, 10.0f, 0, 0, NULL },
        };

        struct
        {
            U8 hiding;
            F32 alpha;
            F32 alpha_vel;
            F32 glow;
            F32 glow_vel;
            struct
            {
                xModelInstance* reticle;
                xModelInstance* target;
                xModelInstance* swirl;
                xModelInstance* wind;
            } model;
            hud_gizmo gizmo[33];
            U32 gizmos_used;
            uv_animated_model uv_swirl;
            uv_animated_model uv_wind;
        } hud;

        // No symbol in TSSM (always inlined); body from BfBB.
        static inline void show_gizmo(hud_gizmo& gizmo, const basic_rect<F32>& rect,
                                      xModelInstance* m)
        {
            gizmo.flags = 0x1;
            gizmo.bound = rect;
            gizmo.alpha = 0.0f;
            gizmo.alpha_vel = 1.0f / current_tweak->hud.time_fade;
            gizmo.glow = 1.0f;
            gizmo.glow_vel = -1.0f / current_tweak->hud.time_glow;
            gizmo.opacity = 1.0f;
            gizmo.target = NULL;
            gizmo.model = m;
        }

        // No symbol in TSSM (always inlined); body from BfBB.
        static inline S32 find_locked_target(const xVec3* target)
        {
            for (S32 i = 1; i < hud.gizmos_used; ++i)
            {
                if (hud.gizmo[i].target == target)
                {
                    return i;
                }
            }

            return -1;
        }

        // No symbol in TSSM (always inlined); body from BfBB.
        static inline xVec3 world_to_screen(const xVec3& loc)
        {
            iCameraUpdatePos(globals.oldSkoolCamera.lo_cam, &globals.oldSkoolCamera.mat);

            xMat4x3& view_mat = *(xMat4x3*)&globals.oldSkoolCamera.lo_cam->viewMatrix;
            xVec3 world_loc;
            xMat4x3Toworld(&world_loc, &view_mat, &loc);

            xVec3 screen_loc;
            F32 iz = 1.0f / world_loc.z;
            screen_loc.assign(world_loc.x * iz, world_loc.y * iz, 1.0f);

            return screen_loc;
        }

        // No symbols in TSSM (always inlined); bodies from BfBB/PS2 DWARF.
        static inline void set_state(thread_enum thread, state_enum state)
        {
            state_type** st = &shared.state[thread];
            if (*st != NULL)
            {
                (*st)->stop();
                *st = NULL;
            }

            if (state != STATE_INVALID)
            {
                *st = shared.states[state];
                (*st)->start();
            }
        }

        static inline void render_state()
        {
            for (S32 i = THREAD_PLAYER; i < MAX_THREAD; ++i)
            {
                if (shared.state[i] != NULL)
                {
                    shared.state[i]->render();
                }
            }
        }

        static inline void render_player()
        {
            zEnt* player = xglobals->___player_ent_dont_use_directly;
            zEntPlayer_MinimalRender(player);
        }

        static inline xMat4x3* get_player_mat()
        {
            return (xMat4x3*)xglobals->___player_ent_dont_use_directly->model->Mat;
        }

        static inline xVec3& get_player_loc()
        {
            return *(xVec3*)&xglobals->___player_ent_dont_use_directly->model->Mat->pos;
        }

        static inline void capture_camera()
        {
            zCameraDisableInput();
            zCameraDisableTracking(CO_CRUISE_BUBBLE);
            xCameraDoCollisions(0, 1);
        }

        static inline void release_camera()
        {
            zCameraEnableInput();
            zCameraEnableTracking(CO_CRUISE_BUBBLE);
            xCameraDoCollisions(1, 1);
        }

        // No symbol in TSSM (always inlined); counterpart of hide_wand.
        static inline void show_wand()
        {
            if (globals.player.model_spongebob != NULL)
            {
                xModelInstance* wand = globals.player.sb_models[8];
                if (!(wand->Flags & 0x1))
                {
                    zEntPlayerPopOffOnAtomic(wand, 200.0f);
                }
                wand->Flags |= 0x1;
            }
        }

        static inline void hide_wand()
        {
            if (globals.player.model_spongebob != NULL)
            {
                xModelInstance* wand = globals.player.sb_models[8];
                if (wand->Flags & 0x1)
                {
                    zEntPlayerPopOffOnAtomic(wand, 200.0f);
                }
                wand->Flags &= ~0x1;
            }
        }

        // No symbol in TSSM (always inlined); BfBB's camera_taken, with retail's tracking mask.
        static inline bool camera_taken()
        {
            return zCameraGetConvers() != 0 ||
                   (zCameraIsTrackingDisabled() & ~(CO_CRUISE_BUBBLE | CO_BOSSCRUISELAUNCH));
        }

        static inline void show_missle()
        {
            shared.missle_model->Flags |= 0x3;
        }

        static inline void hide_missle()
        {
            shared.missle_model->Flags &= ~0x3;
        }

        // No symbols in TSSM (always inlined); bodies from BfBB, sound calls adapted to TSSM's
        // xSndMgr/zEntPlayer sound API as the retail code shows.
        static inline xMat4x3* get_missle_mat()
        {
            return (xMat4x3*)shared.missle_model->Mat;
        }

        static inline void flash_hud()
        {
            hud.glow = 1.0f;
            hud.glow_vel = -1.0f / current_tweak->hud.time_glow;
        }

        static inline iSndHandle play_sound(S32 which, F32 volFactor)
        {
            sound_config* s = &sounds[which];

            if (s->id == 0)
            {
                if (s->streamed)
                {
                    zEntPlayer_SNDPlayStream((_tagePlayerStreamSnd)s->first);
                }
                else
                {
                    zEntPlayer_SNDPlay((_tagePlayerSnd)s->first, true);
                }

                s->handle = (iSndHandle)-1;
            }
            else
            {
                F32 vol = s->volume * volFactor;
                s->handle =
                    xSndMgrPlay(xSndMgrGetSoundGroup(s->id), 0, NULL, NULL, NULL, &vol, NULL);
            }

            if (s->rumble_effect != NULL)
            {
                zEntEvent(s->rumble_effect, eEventStartRumbleEffect, globals.currentActivePad, 0.0f,
                          0.0f, 0.0f);
            }

            return s->handle;
        }

        // Retail tests the handle against 0 here (stop_sound tests against -1).
        static inline void set_pitch(S32 which, F32 pitch, iSndHandle handle)
        {
            sound_config* s = &sounds[which];

            if (s->id == 0)
            {
                zEntPlayer_SNDSetPitch((_tagePlayerSnd)s->first, pitch);
                return;
            }

            if (handle == 0)
            {
                handle = s->handle;
            }

            if (handle != 0)
            {
                xSndMgrSetPitch(handle, pitch);
            }
        }

        // The position comes first: retail evaluates the sound index before the position, and CW
        // evaluates the arguments of an inlined call from right to left.
        static inline iSndHandle play_sound(const xVec3* pos, S32 which, F32 volFactor)
        {
            sound_config* s = &sounds[which];

            if (s->id != 0)
            {
                F32 vol = s->volume * volFactor;
                s->handle =
                    xSndMgrPlay(xSndMgrGetSoundGroup(s->id), 0, pos, NULL, NULL, &vol, NULL);
            }

            if (s->rumble_effect != NULL)
            {
                zEntEvent(s->rumble_effect, eEventStartRumbleEffect, globals.currentActivePad, 0.0f,
                          0.0f, 0.0f);
            }

            return s->handle;
        }

        static inline void stop_sound(S32 which, iSndHandle handle)
        {
            sound_config* s = &sounds[which];

            if (s->id == 0)
            {
                if (s->streamed)
                {
                    zStreamedSound::StopLastPlayedSound();
                }
                else
                {
                    zEntPlayer_SNDStop((_tagePlayerSnd)s->first);
                }
                return;
            }

            if (handle == (iSndHandle)-1)
            {
                handle = s->handle;
            }

            if (handle != (iSndHandle)-1)
            {
                xSndMgrStop(handle);
            }

            s->handle = (iSndHandle)-1;
        }

        // No symbol in TSSM (always inlined); BfBB's can_damage without its destructible-object
        // test (retail has none).
        static inline U8 can_damage(xEnt* ent)
        {
            if (ent == NULL)
            {
                return false;
            }
            if (!(ent->moreFlags & 0x10))
            {
                return false;
            }
            if (ent->baseType == eBaseTypeNPC && !((zNMECommon*)ent)->IsHealthy())
            {
                return false;
            }

            return true;
        }

        // No symbol in TSSM (always inlined); body from BfBB.
        static inline U8 was_damaged(xEnt* ent)
        {
            xEnt** it = shared.hits;
            xEnt** end = it + shared.hits_size;
            for (; it != end; ++it)
            {
                if (ent == *it)
                {
                    return true;
                }
            }

            return false;
        }

        static inline void hide_hud()
        {
            hud.gizmos_used = 0;
            if (hud.model.wind != NULL)
            {
                hud.model.wind->Alpha = 0.0f;
            }
        }

        static inline void stop_trail()
        {
            shared.flags &= ~0x80;
        }

#ifdef PLATFORM_PC
        // PLATFORM_PC: empty on the GameCube. The Xbox's (0x1af2e0) keeps the amount for its
        // screen distortion, drawn by zSceneRenderPostFX (iXboxDistort.h).
        static inline void distort_screen(F32 amount)
        {
            iXboxDistortSetAmount(amount);
        }
#else
        static inline void distort_screen(F32)
        {
        }
#endif

        static inline void exit_triggers(xScene& s)
        {
            zEntTrigger** it = (zEntTrigger**)s.trigs;
            zEntTrigger** end = it + s.num_trigs;
            for (; it != end; ++it)
            {
                zEntTrigger& trig = **it;
                if (xBaseIsEnabled(&trig) && (trig.entered & 0x2))
                {
                    trig.entered &= ~0x2;

                    xLinkAsset* link = trig.link;
                    xLinkAsset* end_link = link + trig.linkCount;
                    for (; link != end_link; ++link)
                    {
                        if (link->srcEvent == eEventExitCruise)
                        {
                            zEntEvent(&trig, eEventExitCruise);
                            break;
                        }
                    }
                }
            }
        }

        static inline xModelInstance* load_model(U32 aid)
        {
            U32 size;
            void* data = xSTFindAsset(xStrHashCat(aid, ".minf"), &size);
            if (data != NULL)
            {
                return zEntRecurseModelInfo(data, NULL);
            }

            data = xSTFindAsset(aid, &size);
            if (data == NULL)
            {
                data = xSTFindAsset(xStrHashCat(aid, ".dff"), &size);
            }
            if (data == NULL)
            {
                return NULL;
            }

            return xModelInstanceAlloc((RpAtomic*)data, NULL, 0, 0, NULL);
        }

        // No symbols in TSSM (always inlined); bodies from BfBB.
        static inline void refresh_trail(xMat4x3& mat, xQuat& quat)
        {
            xModelGetBoneMat(mat, *shared.missle_model, 8);
            xQuatFromMat(&quat, &mat);
        }

        static inline void start_trail()
        {
            if (shared.flags & 0x80)
            {
                return;
            }

            shared.flags |= 0x180;
            shared.trail.bubbles = 0.0f;
            shared.trail.samples = 0.0f;
            refresh_trail(shared.trail.mat, shared.trail.dir);
        }

        static inline void start_damaging()
        {
            shared.hits_size = 0;
        }

        static inline void signal_event(U32 toEvent)
        {
            zEntEvent(xglobals->___player_ent_dont_use_directly,
                      xglobals->___player_ent_dont_use_directly, toEvent);
        }

        static inline void update_gizmo(hud_gizmo& gizmo, F32 dt)
        {
            gizmo.alpha = range_limit<F32>(gizmo.alpha_vel * dt + gizmo.alpha, 0.0f, 1.0f);
            gizmo.glow = range_limit<F32>(gizmo.glow_vel * dt + gizmo.glow, 0.0f, 1.0f);
        }

        static inline void lerp(U8& x, F32 t, U8 a, U8 b)
        {
            x = 0.5f + ((F32)a + t * ((F32)b - (F32)a));
        }

        static inline void lerp(iColor_tag& c, F32 t, iColor_tag a, iColor_tag b)
        {
            lerp(c.r, t, a.r, b.r);
            lerp(c.g, t, a.g, b.g);
            lerp(c.b, t, a.b, b.b);
            lerp(c.a, t, a.a, b.a);
        }

    } // namespace CruiseBubble
} // namespace cruise_bubble

static inline void load_cheat_tweak()
{
    cheat_tweak.missle.crash_angle = 0.7536f;
    cheat_tweak.missle.collide_twist = 0.05f;
    cheat_tweak.missle.appear.delay_fly = 0.0f;
    cheat_tweak.missle.fly.accel = 12.0f;
    cheat_tweak.missle.fly.turn.xdelta = 7.0f;
    cheat_tweak.missle.fly.turn.ydelta = 5.0f;
    cheat_tweak.missle.fly.turn.xdecay = 0.985f;
    cheat_tweak.missle.fly.turn.ydecay = 0.985f;
    cheat_tweak.missle.fly.turn.ybound = 1.24248f;
    cheat_tweak.missle.fly.turn.roll_frac = 0.1f;
    cheat_tweak.missle.explode.hit_radius = 2.0f;
    cheat_tweak.missle.explode.hit_radius_super = 4.0f;
    cheat_tweak.camera.seize.blend_time = 0.75f;
    cheat_tweak.camera.survey.duration = 1.0f;
    cheat_tweak.camera.survey.min_duration = 0.1f;
    cheat_tweak.camera.survey.drift_dist = 10.0f;
    cheat_tweak.material.env_alpha = 0.2f;
    cheat_tweak.material.env_texture = xStrHash("aura2");
    cheat_tweak.material.fresnel_alpha = 0.1f;
    cheat_tweak.material.fresnel_coeff = 1.0f;
    cheat_tweak.material.fresnel_texture = xStrHash("par_cruise_explode");
    cheat_tweak.trail.bubble_rate = 90.0f;
    cheat_tweak.trail.bubble_emit_radius = 0.75f;
    cheat_tweak.trail.wake_emit_radius = 0.3f;
    cheat_tweak.blast.emit = 400;
    cheat_tweak.blast.vel = 7.5f;
    cheat_tweak.droplet.emit_min = 15;
    cheat_tweak.droplet.vel_min = 4.0f;
    cheat_tweak.droplet.vel_max = 8.0f;
}

void cruise_bubble::CruiseBubble::damage_entity(xEnt& ent, const xVec3& loc, const xVec3& dir,
                                                const xVec3& hit_norm, F32 radius, bool explosive)
{
    if (shared.hits_size >= 32)
    {
        return;
    }
    shared.hits[shared.hits_size] = &ent;
    shared.hits_size++;

    switch (ent.baseType)
    {
    case eBaseTypeButton:
        zEntButton_Press((_zEntButton*)&ent, 0x10);
        return;

    case eBaseTypePlatform:
        switch (ent.subType)
        {
        case ZPLATFORM_SUBTYPE_PADDLE:
            if (!(((zPlatform*)&ent)->plat_asset->paddle.paddleFlags & 0x20))
            {
                return;
            }

            xCollis coll;
            coll.optr = &ent;
            coll.mptr = ent.collModel != NULL ? ent.collModel : ent.model;

            if (explosive)
            {
                coll.flags = 0x600;

                xSphere o;
                o.center = loc;
                o.r = radius;
                xSphereHitsBound(&o, &ent.bound, &coll);

                if (!(coll.flags & 0x1))
                {
                    return;
                }

                if (ent.collLev == 0x5)
                {
                    xSphereHitsModel(&o, coll.mptr, &coll);

                    if (!(coll.flags & 0x1))
                    {
                        return;
                    }
                }

                xVec3 hit_dir = coll.tohit.up_normal();
                zPlatform_PaddleCollide(&coll, &loc, &hit_dir, 1);
                return;
            }

            coll.flags = 0x201;
            coll.norm = hit_norm;
            zPlatform_PaddleCollide(&coll, &loc, &dir, 1);
            return;
        }
        break;

    case eBaseTypeNPC:
        if (explosive)
        {
            xVec3 edir = (*xEntGetCenter(&ent) - loc).up_normal();
            ((zNMECommon*)&ent)->Damage(DMGTYP_CRUISEBUBBLE, &base, &edir, -1, ePowerupLevelNormal);
        }
        else
        {
            ((zNMECommon*)&ent)->Damage(DMGTYP_CRUISEBUBBLE, &base, &dir, -1, ePowerupLevelNormal);
        }
        return;
    }

    // (S8): retail converts explosive through a signed char, as does the PS2 demo; a plain bool
    // would be zero-extended
    zEntEvent(&ent, eEventHit_Cruise, (S8)explosive, 0.0f, 0.0f, 0.0f);
}

void cruise_bubble::CruiseBubble::notify_triggers(xScene& s, const xSphere& o, const xVec3& dir)
{
    zEntTrigger** it = (zEntTrigger**)s.trigs;
    zEntTrigger** end = it + s.num_trigs;
    for (; it != end; ++it)
    {
        zEntTrigger& trig = **it;
        if (xBaseIsEnabled(&trig))
        {
            bool want_enter = false;
            bool want_exit = false;
            xLinkAsset* link = trig.link;
            xLinkAsset* end_link = link + trig.linkCount;
            for (; link != end_link; ++link)
            {
                if (link->srcEvent == eEventEnterCruise)
                {
                    want_enter = true;
                }
                else if (link->srcEvent == eEventExitCruise)
                {
                    want_exit = true;
                }
            }
            want_enter = want_enter && !(trig.entered & 0x2);
            want_exit = want_exit && (trig.entered & 0x2);

            if (want_enter || want_exit)
            {
                bool inside = zEntTriggerHitsSphere(trig, o, dir);
                if (inside)
                {
                    trig.entered |= 0x2;
                }
                else
                {
                    trig.entered &= ~0x2;
                }

                if (want_enter && inside)
                {
                    zEntEvent(&trig, eEventEnterCruise);
                }
                else if (want_exit && !inside)
                {
                    zEntEvent(&trig, eEventExitCruise);
                }
            }
        }
    }
}

// State members defined out of class are inline too: retail has every state member as a weak
// inline (emitted with the state vtables).
inline void cruise_bubble::CruiseBubble::state_player_halt::start()
{
    this->first_update = true;
}

inline void cruise_bubble::CruiseBubble::state_player_halt::stop()
{
    shared.flags &= ~0x20;
}

inline cruise_bubble::CruiseBubble::state_enum
cruise_bubble::CruiseBubble::state_player_halt::update(F32 dt)
{
    this->time += dt;

    if (this->first_update)
    {
        this->first_update = false;
        this->last_motion = shared.player_motion;
        return STATE_PLAYER_HALT;
    }

    xVec3 dmotion = shared.player_motion - this->last_motion;
    if (dmotion.length2() < 0.0001f)
    {
        return STATE_PLAYER_AIM;
    }

    if (this->time > current_tweak->player.halt_time)
    {
        return STATE_INVALID;
    }

    return STATE_PLAYER_HALT;
}

inline void cruise_bubble::CruiseBubble::state_player_wait::start()
{
    hide_wand();
}

inline cruise_bubble::CruiseBubble::state_enum
cruise_bubble::CruiseBubble::state_player_wait::update(F32)
{
    return STATE_PLAYER_WAIT;
}

// The state classes come after the shared data, as in the original source; retail has all their
// members (and vtables) as weak inlines.
namespace cruise_bubble
{
    namespace CruiseBubble
    {
        struct state_player_aim : state_type
        {
            F32 yaw;
            F32 yaw_vel;
            F32 turn_delay;

            state_player_aim() : state_type(STATE_PLAYER_AIM)
            {
            }

            // Defined out of class below; inline, as retail has every member as a weak inline.
            inline void start();
            inline void stop();

            state_enum update(F32 dt)
            {
                this->turn_delay += dt;

                if (this->turn_delay >= current_tweak->aim_delay)
                {
                    this->face_camera(dt);
                }

                this->apply_yaw();
                this->update_animation(dt);

                if (!(globals.pad0->on & 0x1000))
                {
                    return STATE_PLAYER_FIRE;
                }

                return STATE_PLAYER_AIM;
            }

            // No symbols in TSSM (always inlined into update); bodies from BfBB.
            void update_animation(F32 dt)
            {
                F32 r = range_limit<F32>(this->yaw_vel * current_tweak->player.aim.anim_delta,
                                         -1.0f, 1.0f);
                xAnimSingle* s = xglobals->___player_ent_dont_use_directly->model->Anim->Single;
                s->BilinearLerp[0] = 0.5f * ((1.0f + s->BilinearLerp[0]) + r);
            }

            void apply_yaw()
            {
                xMat4x3* m = get_player_mat();
                m->at.assign(isin(this->yaw), 0.0f, icos(this->yaw));
                m->right.assign(m->at.z, 0.0f, -m->at.x);
                m->up.assign(0.0f, 1.0f, 0.0f);
            }

            void face_camera(F32 dt)
            {
                xMat4x3* mat = &globals.oldSkoolCamera.mat;

                F32 new_yaw;
                if (mat->at.x >= -0.00001f && mat->at.x <= 0.00001f && mat->at.z >= -0.00001f &&
                    mat->at.z <= 0.00001f)
                {
                    new_yaw = this->yaw;
                }
                else
                {
                    new_yaw = xatan2(mat->at.x, mat->at.z);
                }

                F32 diff = new_yaw - this->yaw;
                if (diff > PI)
                {
                    diff -= PI * 2;
                }
                else if (diff < -PI)
                {
                    diff += PI * 2;
                }

                F32 tspeed = current_tweak->player.aim.turn_speed * xexp(dt);
                if (tspeed > 1.0f)
                {
                    tspeed = 1.0f;
                }

                tspeed = diff * tspeed;
                this->yaw_vel = tspeed / dt;
                this->yaw = this->yaw + tspeed;
                this->yaw = xrmod(this->yaw);
            }
        };

        struct state_missle_appear : state_type
        {
            state_missle_appear() : state_type(STATE_MISSLE_APPEAR)
            {
            }

            void start()
            {
                show_missle();
                shared.missle_model->Flags &= ~0x1;
                shared.missle_model->Alpha = 1.0f;

                xAnimPlay* aplay = shared.missle_model->Anim;
                xAnimSingle* asingle = aplay->Single;
                xAnimPlaySetState(asingle, shared.astate.missle.fire, 0.0f);

                this->move();
            }

            // Defined out of class below; inline, as retail has every member as a weak inline.
            inline void stop();

            state_enum update(F32 dt)
            {
                F32 time = shared.missle_model->Anim->Single->Time + dt;
                if (time >= current_tweak->missle.appear.delay_show)
                {
                    shared.missle_model->Flags |= 0x1;
                    start_trail();
                }

                this->move();

                if (zEntPlayer_PowerupLevel(PLYR_ATAK_SB_GUITAR) > ePowerupLevelNormal)
                {
                    if (time >= current_tweak->missle.appear.delay_fly_super)
                    {
                        CBFXtra::RockStar_Action(CBFXtra::TUNE_FLIGHT, CBFXtra::TUNE_TRAN_ENTER);
                        return STATE_MISSLE_FLY;
                    }
                }
                else if (time >= current_tweak->missle.appear.delay_fly)
                {
                    return STATE_MISSLE_FLY;
                }

                return STATE_MISSLE_APPEAR;
            }

            // No symbol in TSSM (always inlined into start and update); body from BfBB.
            void move()
            {
                xMat4x3& mat = *get_missle_mat();
                xVec3 euler;
                xVec3 prod;

                mat = *get_player_mat();
                xMat3x3GetEuler(&mat, &euler);
                xMat3x3Euler(&mat, &euler);
                xMat3x3RMulVec(&prod, &mat, &current_tweak->missle.appear.offset);
                mat.pos += prod;
            }
        };
    } // namespace CruiseBubble
} // namespace cruise_bubble

namespace cruise_bubble
{
    namespace CruiseBubble
    {
        struct state_player_fire : state_type
        {
            U8 wand_shown;
            S32 flg_bangLeft : 1;
            S32 flg_bangRight : 1;
            S32 flg_speakerOn : 1;
            S32 flg_toBeUsed : 29;
            F32 tmr_bangLeft;
            F32 tmr_bangRight;
            F32 tmr_showSpeaker;

            state_player_fire() : state_type(STATE_PLAYER_FIRE)
            {
            }

            void start()
            {
                this->wand_shown = false;

                en_plyrpup pup = zEntPlayer_PowerupLevel(PLYR_ATAK_SB_GUITAR);
                if (pup == ePowerupLevelNormal)
                {
                    play_sound(&get_missle_mat()->pos, 0, 1.0f);
                    xAnimPlayStartTransition(xglobals->___player_ent_dont_use_directly->model->Anim,
                                             shared.atran.player.fire);
                }
                else
                {
                    play_sound(&get_missle_mat()->pos, 4, 1.0f);
                    xAnimPlayStartTransition(xglobals->___player_ent_dont_use_directly->model->Anim,
                                             shared.atran.player.fire2);
                }

                set_state(THREAD_MISSLE, STATE_MISSLE_APPEAR);

                this->SpeakerFind();

                if (zEntPlayer_PowerupLevel(PLYR_ATAK_SB_GUITAR) > ePowerupLevelNormal)
                {
                    this->tmr_bangLeft = 0.15f;
                    this->tmr_bangRight = 0.35f;
                    this->tmr_showSpeaker = 0.55f;
                    this->flg_bangLeft = 0;
                    this->flg_bangRight = 0;
                    this->flg_speakerOn = 0;
                }
                else
                {
                    this->tmr_bangLeft = -1.0f;
                    this->tmr_bangRight = -1.0f;
                    this->tmr_showSpeaker = 0.15f;
                    this->flg_bangLeft = 1;
                    this->flg_bangRight = 1;
                    this->flg_speakerOn = 0;
                }

                if (xurand() <= shared.dialog_freq)
                {
                    play_sound(3, 1.0f);
                    shared.dialog_freq *= current_tweak->dialog.decay;

                    if (shared.dialog_freq < current_tweak->dialog.min_freq)
                    {
                        shared.dialog_freq = current_tweak->dialog.min_freq;
                    }
                }
            }

            void stop()
            {
                hide_wand();
            }

            state_enum update(F32 dt)
            {
                this->ConcertStuff_Update(dt);

                xAnimSingle* asingle =
                    xglobals->___player_ent_dont_use_directly->model->Anim->Single;
                xAnimState* astate = asingle->State;

                if (astate == shared.astate.player.fire || astate == shared.astate.player.fire2)
                {
                    F32 max_time = astate->Data->Duration;
                    F32 time = asingle->Time + dt;

                    if (!this->wand_shown && time >= current_tweak->player.fire.delay_wand)
                    {
                        show_wand();
                    }

                    if (time >= max_time)
                    {
                        return STATE_PLAYER_WAIT;
                    }
                }

                return STATE_PLAYER_FIRE;
            }

            void ConcertStuff_Update(F32 dt)
            {
                if (!this->flg_bangLeft && this->tmr_bangLeft < 0.0f)
                {
                    this->flg_bangLeft = 1;
                    CBFXtra::RockStar_Action(CBFXtra::TUNE_BANG_LEFT, CBFXtra::TUNE_TRAN_ENTER);
                }

                if (!this->flg_bangRight && this->tmr_bangRight < 0.0f)
                {
                    this->flg_bangRight = 1;
                    CBFXtra::RockStar_Action(CBFXtra::TUNE_BANG_RYHT, CBFXtra::TUNE_TRAN_ENTER);
                }

                if (!this->flg_speakerOn && this->tmr_showSpeaker < 0.0f)
                {
                    this->flg_speakerOn = 1;

                    this->SpeakerShow();
                    CBFXtra::RockStar_Action(CBFXtra::TUNE_SPEAKER, CBFXtra::TUNE_TRAN_ENTER);
                }

                this->tmr_bangLeft = MAX(-1.0f, this->tmr_bangLeft - dt);
                this->tmr_bangRight = MAX(-1.0f, this->tmr_bangRight - dt);
                this->tmr_showSpeaker = MAX(-1.0f, this->tmr_showSpeaker - dt);
            }

            // No symbol in TSSM (always inlined into ConcertStuff_Update); the Xbox build keeps it
            // out of line, including the zEntEvent. Name invented, after SpeakerFind.
            void SpeakerShow()
            {
                xVec3 vec_off;
                if (zEntPlayer_PowerupLevel(PLYR_ATAK_SB_GUITAR) > ePowerupLevelNormal)
                {
                    vec_off = Options::SpeakerUber::vec_offPlyr;
                }
                else
                {
                    vec_off = Options::SpeakerMini::vec_offPlyr;
                }

                xMat4x3* mat_plyr = get_player_mat();
                xVec3 pos = mat_plyr->pos;
                pos += mat_plyr->right * vec_off.x;
                pos += mat_plyr->up * vec_off.y;
                pos += mat_plyr->at * vec_off.z;

                xVec3Copy((xVec3*)&curSourceSimpObj->model->Mat->pos, &pos);
                *(xMat3x3*)curSourceSimpObj->model->Mat = *mat_plyr;

                zEntEvent(curSourceSimpObj, eEventVisible);
            }

            // No symbol in TSSM (always inlined into start); its local statics carry its name.
            void SpeakerFind()
            {
                static U32 hash_uber = xStrHash("SIMP_GUITAR_AMP2");
                static U32 hash_mini = xStrHash("SIMP_GUITAR_AMP1");

                if (zEntPlayer_PowerupLevel(PLYR_ATAK_SB_GUITAR) > ePowerupLevelNormal)
                {
                    curSourceSimpObj = (zEntSimpleObj*)zSceneFindObject(hash_uber);
                }
                else
                {
                    curSourceSimpObj = (zEntSimpleObj*)zSceneFindObject(hash_mini);
                }
            }
        };

        // No symbol in TSSM (always inlined into state_camera_seize::start); body from BfBB.
        static inline void show_hud()
        {
            hud.gizmos_used = 1;
            basic_rect<F32> reticle_bound;
            reticle_bound.set_size(current_tweak->hud.reticle.size).center(0.5f, 0.5f);
            show_gizmo(hud.gizmo[0], reticle_bound, hud.model.reticle);

            hud.model.wind->Alpha = 0.0f;
            hud.alpha = 0.0f;
            hud.alpha_vel = 1.0f / current_tweak->hud.time_fade;

            flash_hud();
        }

        static inline bool camera_leave()
        {
            return zCameraGetConvers() != 0;
        }

        struct state_missle_fly : state_type
        {
            xVec3 rot;
            xVec3 rot_vel;
            xVec3 last_loc;
            xVec3 boost_target_pos;
            F32 life;
            F32 vel;
            F32 engine_pitch;
            F32 flash_time;
            S32 boost;

            state_missle_fly() : state_type(STATE_MISSLE_FLY)
            {
            }

            void start()
            {
                shared.flags |= 0x8;
                this->life = current_tweak->missle.life;

                show_missle();
                start_trail();
                start_damaging();

                this->vel = 0.0f;
                xMat3x3GetEuler(get_missle_mat(), &this->rot);
                this->rot_vel = 0.0f;
                this->engine_pitch = 0.0f;
                this->flash_time = 0.0f;
                this->boost = 0;

                this->last_loc = xglobals->___player_ent_dont_use_directly->bound.sph.center;

                missle_record.reset();
                missle_record.push_front(missle_record_data(this->last_loc, this->rot.z));
                missle_record.push_front(missle_record_data(get_missle_mat()->pos, this->rot.z));

                play_sound(&get_missle_mat()->pos,
                           zEntPlayer_PowerupLevel(PLYR_ATAK_SB_GUITAR) > ePowerupLevelNormal ? 6 :
                                                                                                2,
                           1.0f);
                signal_event(eEventCruiseFired);
            }

            void stop()
            {
                shared.flags &= ~0x8;

                hide_missle();
                stop_trail();
                stop_sound(2, (iSndHandle)-1);
                stop_sound(6, (iSndHandle)-1);
                signal_event(eEventCruiseDied);

                xSphere o;
                o.center = 1e38f;
                o.r = 0.0f;
                notify_triggers(*globals.sceneCur, o, g_O3);
            }

            inline void abort();

            state_enum update(F32 dt)
            {
                if (zEntPlayer_PowerupLevel(PLYR_ATAK_SB_GUITAR) == ePowerupLevelNormal)
                {
                    if (!globals.player.ControlOff && (globals.pad0->pressed & 0x1000))
                    {
                        this->life = 0.0f;
                    }
                }
                else if (zEntPlayer_PowerupLevel(PLYR_ATAK_SB_GUITAR) > ePowerupLevelNormal)
                {
                    if (!this->boost)
                    {
                        if (!globals.player.ControlOff && (globals.pad0->pressed & 0x1000))
                        {
                            if (gReticleTarget[0] != NULL)
                            {
                                this->boost = 1;
                                xVec3Copy(&this->boost_target_pos,
                                          &gReticleTarget[0]->bound.sph.center);
                                shared.last_check_dist =
                                    xVec3Dist(&this->boost_target_pos, &get_missle_mat()->pos);
                                play_sound(7, 1.0f);
                                zEntPlayer_SNDPlayStream(ePlayerStreamSnd_SBSonicGuitarReleased);
                            }
                            else
                            {
                                this->life = 0.0f;
                            }
                        }
                    }
                    else
                    {
                        F32 dist = xVec3Dist(&this->boost_target_pos, &get_missle_mat()->pos);
                        // not in the DWARF: retail computes the radius once, before xMat3x3Tolocal
                        F32 range = current_tweak->missle.explode.hit_radius_super - 0.1f;
                        xVec3 target_screen_pos;
                        xMat4x3Tolocal(&target_screen_pos, get_missle_mat(),
                                       &this->boost_target_pos);

                        if ((dist < range || -target_screen_pos.z > range) &&
                            (dist > shared.last_check_dist || dist < 1.0f))
                        {
                            this->life = 0.0f;
                        }

                        shared.last_check_dist = dist;
                    }
                }

                if (!this->boost)
                {
                    this->life -= dt;
                }

                if (this->life <= 0.0f)
                {
                    shared.hit_loc = get_missle_mat()->pos;
                    shared.hit_norm = get_missle_mat()->at;

                    return STATE_MISSLE_EXPLODE;
                }

                this->update_turn(dt);
                this->update_move(dt);

                if (missle_record.full())
                {
                    missle_record.pop_back();
                }

                if (this->collide() || this->collide_hazards())
                {
                    missle_record.push_front(missle_record_data(shared.hit_loc, this->rot.z));
                    return STATE_MISSLE_EXPLODE;
                }

                missle_record.push_front(missle_record_data(get_missle_mat()->pos, this->rot.z));

                this->last_loc = get_missle_mat()->pos;
                this->update_engine_sound(dt);
                this->update_flash(dt);

                xSphere o;
                o.center = get_missle_mat()->pos;
                o.r = current_tweak->missle.hit_dist;
                notify_triggers(*globals.sceneCur, o, get_missle_mat()->at);

                return STATE_MISSLE_FLY;
            }

            // No PS2 DWARF (the demo inlines it into update); local names from BfBB's fork.
            void update_turn(F32 dt)
            {
                xVec2 last_sp;
                xVec2 sp;
                // Dead store: it gives retail's register allocation for the steering products
                sp.y = 0.0f;
                if (this->boost)
                {
                    static F32 steerFactor = 0.4f;

                    xVec3 target_pos;
                    xMat4x3Tolocal(&target_pos, get_missle_mat(), &this->boost_target_pos);
                    sp.x = -steerFactor * target_pos.x;
                    sp.y = steerFactor * target_pos.y;
                    // not in any DWARF (inlined code): retail reads z before clampMaxAdd's guard
                    F32 dist = target_pos.z;

                    static F32 clampMaxAdd = 2.0f;

                    // not in any DWARF (inlined code); declared after dist for retail's registers
                    F32 dist_max = current_tweak->reticle.dist_max;
                    F32 clampMax = 2.0f + clampMaxAdd * (dist_max - dist) / dist_max;
                    sp.x = MAX(-clampMax, sp.x);
                    if (sp.x > clampMax)
                    {
                        sp.x = clampMax;
                    }
                    sp.y = MAX(-clampMax, sp.y);
                    if (sp.y > clampMax)
                    {
                        sp.y = clampMax;
                    }
                    last_sp = shared.last_sp;
                }
                else
                {
                    sp = shared.sp;
                    last_sp = shared.last_sp;
                }

                F32 xdelta;
                F32 ydelta;
                F32 xdecay = current_tweak->missle.fly.turn.xdecay;
                F32 ydecay = current_tweak->missle.fly.turn.ydecay;
                F32 ybound = current_tweak->missle.fly.turn.ybound;
                F32 roll_frac = -current_tweak->missle.fly.turn.roll_frac;
                if (this->boost)
                {
                    xdelta = -3.0f * current_tweak->missle.fly.turn.xdelta;
                    ydelta = -3.0f * current_tweak->missle.fly.turn.ydelta;
                }
                else
                {
                    xdelta = -current_tweak->missle.fly.turn.xdelta;
                    ydelta = -current_tweak->missle.fly.turn.ydelta;
                }

                xVec2 d0, d1, v0, v1, a0, a1;

                d0.x = this->rot.x;
                d0.y = this->rot.y;

                v0.x = this->rot_vel.x * xpow(1.0f - xdecay, dt);
                v0.y = this->rot_vel.y * xpow(1.0f - ydecay, dt);

                F32 damp;
                if (d0.y * (ydelta * sp.y) <= 0.0f)
                {
                    damp = 1.0f;
                }
                else
                {
                    damp = 1.0f - xabs(d0.y) / ybound;
                }

                a0.x = xdelta * last_sp.x;
                a0.y = damp * (ydelta * last_sp.y);
                a1.x = xdelta * sp.x;
                a1.y = damp * (ydelta * sp.y);

                this->calculate_rotation(d1, v1, dt, d0, v0, a0, a1);

                this->rot.x = d1.x;
                this->rot.y = d1.y;
                this->rot_vel.x = v1.x;
                this->rot_vel.y = v1.y;

                if (!this->boost)
                {
                    this->rot.z = roll_frac * (this->rot_vel.x * this->rot_vel.x);
                    if (this->rot_vel.x < 0.0f)
                    {
                        this->rot.z *= -1.0f;
                    }
                }

                xMat3x3Euler(get_missle_mat(), &this->rot);
            }

            // No symbol in TSSM (always inlined into update_turn); body from BfBB.
            void calculate_rotation(xVec2& d1, xVec2& v1, F32 dt, const xVec2& d0, const xVec2& v0,
                                    const xVec2& a0, const xVec2& a1) const
            {
                v1.x = dt * (0.5f * (a0.x + a1.x)) + v0.x;
                v1.y = dt * (0.5f * (a0.y + a1.y)) + v0.y;
                d1.x = d0.x + (dt * (dt * ((1.0f / 6.0f) * (a1.x + (a0.x + a0.x)))) + v0.x * dt);
                d1.y = d0.y + (dt * (dt * ((1.0f / 6.0f) * (a1.y + (a0.y + a0.y)))) + v0.y * dt);
            }

            void update_move(F32 dt)
            {
                F32 accel;
                F32 max_vel;
                if (this->boost)
                {
                    accel = 3.0f * current_tweak->missle.fly.accel;
                    max_vel = 3.0f * current_tweak->missle.fly.max_vel;
                }
                else
                {
                    accel = current_tweak->missle.fly.accel;
                    max_vel = current_tweak->missle.fly.max_vel;
                }

                F32 move = 0.0f;
                xAccelMove(move, this->vel, accel, dt, max_vel);

                xMat4x3* mat = get_missle_mat();
                mat->pos += mat->at * move;
            }

            void update_flash(F32 dt)
            {
                this->flash_time += dt;

                F32 tflash = current_tweak->missle.fly.flash_interval *
                             (this->life / current_tweak->missle.life);
                if (this->flash_time > tflash)
                {
                    flash_hud();
                    F32 floor_frac = std::floorf(this->flash_time / tflash);
                    this->flash_time = this->flash_time - floor_frac * tflash;
                }
            }

            void update_engine_sound(F32 dt)
            {
                F32 tmp = current_tweak->missle.fly.engine_pitch_max *
                          (iabs(shared.last_sp.x) + iabs(shared.last_sp.y));

                this->engine_pitch +=
                    current_tweak->missle.fly.engine_pitch_sensitivity * (tmp - this->engine_pitch);

                set_pitch(2, this->engine_pitch, (iSndHandle)-1);
            }

            U8 collide();
            U8 hit_test(xVec3& hit_loc, xVec3& hit_norm, xVec3& hit_depen, xEnt*& hit_ent) const;

            U8 collide_hazards()
            {
                zNMEHazard* c = NULL;
                HAZ_Iterate(hazard_check, &c, 0xa000);

                if (c == NULL)
                {
                    return false;
                }

                if (c->flg_hazard & 0x200000)
                {
                    c->flg_hazard |= 0x4;
                }

                shared.hit_loc = get_missle_mat()->pos;
                return true;
            }

            static bool hazard_check(zNMEHazard& haz, void* context)
            {
                xVec3& loc = get_missle_mat()->pos;
                xVec3 offset = haz.pos_hazard - loc;
                F32 max_dist = current_tweak->missle.hit_dist + haz.custdata.typical.rad_cur;

                if (offset.length2() < max_dist * max_dist)
                {
                    *(zNMEHazard**)context = &haz;
                    return false;
                }

                return true;
            }
        };
    } // namespace CruiseBubble
} // namespace cruise_bubble

// Placeholder for dead-stripped code (owner unknown) whose all-zero xVec3 template remains,
// unreferenced, in retail .rodata after cheat_explode_curve's.
static void __deadstripped_zEntCruiseBubble()
{
    xVec3 v = {};
}

// No symbol in TSSM (always inlined into collide); body from BfBB. Retail clears the swept
// sphere before preparing it, as the PS2 build does.
inline U8 cruise_bubble::CruiseBubble::state_missle_fly::hit_test(xVec3& hit_loc, xVec3& hit_norm,
                                                                  xVec3& hit_depen,
                                                                  xEnt*& hit_ent) const
{
    xScene* s = globals.sceneCur;
    const xVec3* loc = &get_missle_mat()->pos;
    xSweptSphere ss;
    memset(&ss, 0, sizeof(xSweptSphere));
    xSweptSpherePrepare(&ss, &this->last_loc, loc, current_tweak->missle.hit_dist);
    ss.optr = NULL;
    if (!xSweptSphereToScene(&ss, s, NULL, 0x10, 0))
    {
        return false;
    }

    xSweptSphereGetResults(&ss);

    xVec3 overshoot = {};
    overshoot.x = loc->x - ss.worldPos.x;
    overshoot.y = loc->y - ss.worldPos.y;
    overshoot.z = loc->z - ss.worldPos.z;

    hit_loc = ss.worldPos + ss.worldTangent * overshoot.dot(ss.worldTangent);
    hit_depen = hit_loc - *loc;
    hit_norm = ss.worldNormal;
    hit_ent = (xEnt*)ss.optr;

    return true;
}

// Defined after hit_test: retail .rodata has hit_test's initializer before this function's.
inline void cruise_bubble::CruiseBubble::state_missle_fly::abort()
{
    stop_sound(2, (iSndHandle)-1);
    signal_event(eEventCruiseDied);

    xSphere o;
    o.center = 1e38f;
    o.r = 0.0f;
    xVec3 vec = {};
    notify_triggers(*globals.sceneCur, o, vec);
}

// Placeholder for dead-stripped code (in BfBB, state_missle_explode::perturb_direction) whose
// all-zero xVec3 template remains, unreferenced, in retail .rodata.
static void __deadstripped2_zEntCruiseBubble()
{
    xVec3 v = {};
}

// Weak in retail (emitted after its only caller, state_missle_fly::update). Unlike BfBB,
// hit_test and damage_entity take shared.hit_* directly; only the crash-angle dot product reads
// the normal through the hit_norm pointer.
inline U8 cruise_bubble::CruiseBubble::state_missle_fly::collide()
{
    xVec3 hit_depen;
    S32 i = 0;
    xEnt* hit_ent;

    do
    {
        if (!this->hit_test(shared.hit_loc, shared.hit_norm, hit_depen, hit_ent))
        {
            return false;
        }

        if (can_damage(hit_ent))
        {
            damage_entity(*hit_ent, shared.hit_loc, get_missle_mat()->at, shared.hit_norm, 0.0f,
                          false);
            return true;
        }

        xMat4x3* mat = get_missle_mat();
        xVec3* hit_norm = &shared.hit_norm;
        F32 ang = xasin(mat->at.dot(*hit_norm));
        if (ang < -current_tweak->missle.crash_angle)
        {
            return true;
        }

        mat->pos += hit_depen;
        xVec3 diff = mat->pos - this->last_loc;
        F32 len = diff.length2();
        if (len < 0.001f)
        {
            return false;
        }

        len = xsqrt(len);
        xVec3 diff_normalized = diff * (1.0f / len);
        F32 sin = -xasin(diff_normalized.y);
        if (F32(iabs(sin)) > (PI / 2) * current_tweak->missle.fly.turn.ybound)
        {
            return true;
        }

        F32 tan = xatan2(diff_normalized.x, diff_normalized.z);
        F32 mod = xrmod(PI + (tan - this->rot.x)) - PI;
        this->rot.x += mod * current_tweak->missle.collide_twist;
        this->rot.y += (sin - this->rot.y) * current_tweak->missle.collide_twist;
        xMat3x3Euler(mat, &this->rot);

        if (hit_depen.length2() < 1.0000001e-6f)
        {
            return false;
        }
    } while (++i < current_tweak->missle.hit_tests);

    F32 dist = 0.1f * current_tweak->missle.hit_dist;
    if (hit_depen.length2() < dist * dist)
    {
        return false;
    }

    return this->hit_test(shared.hit_loc, shared.hit_norm, hit_depen, hit_ent);
}

namespace cruise_bubble
{
    namespace CruiseBubble
    {
        struct state_camera_aim : state_type
        {
            F32 phi;
            F32 phi_vel;
            F32 height;
            F32 height_vel;
            F32 dist;
            F32 dist_vel;
            xQuat facing;
            xQuat target;
            F32 control_delay;
            F32 seize_delay;

            state_camera_aim() : state_type(STATE_CAMERA_AIM)
            {
            }

            void start()
            {
                capture_camera();
                xSndMgrSelectListenerMode(xSndListener_CAMERA);
                xCameraUpdate(&globals.oldSkoolCamera, 0.001f);

                this->phi_vel = this->height_vel = this->dist_vel = 0.0f;
                xMat4x3& mat = globals.oldSkoolCamera.mat;
                xVec3& ploc = get_player_loc();
                F32 x = mat.pos.x - ploc.x;
                F32 y = mat.pos.z - ploc.z;
                xVec2 offset = {};
                offset.x = x;
                offset.y = y;

                this->phi = xatan2(offset.x, offset.y);
                this->height = mat.pos.y - ploc.y;
                this->dist = offset.length();
                this->control_delay = this->seize_delay = 0.0f;

                xQuatFromMat(&this->facing, &mat);
                start_cam_mat = mat;
                start_cam_mat.pos -= ploc;
            }

            void stop()
            {
                release_camera();
            }

            state_enum update(F32 dt)
            {
                this->control_delay += dt;
                // not in the TSSM DWARF (register-only); BfBB's lists it, and it is tested twice
                bool control = false;
                if ((shared.flags & 0x20) && this->control_delay >= current_tweak->aim_delay)
                {
                    control = true;
                }

                if (control)
                {
                    this->move(dt);
                }
                else
                {
                    this->stop(dt);
                }

                this->apply_motion();
                this->collide_inward();

                if (control || xCamLaunchCam == NULL)
                {
                    this->turn(dt);
                }

                this->apply_turn();

                if (shared.flags & 0x8)
                {
                    this->seize_delay += dt;

                    en_plyrpup pup = zEntPlayer_PowerupLevel(PLYR_ATAK_SB_GUITAR);
                    if (pup > ePowerupLevelNormal)
                    {
                        if (this->seize_delay >= current_tweak->camera.seize.delay_super)
                        {
                            return STATE_CAMERA_SEIZE;
                        }
                    }
                    else if (this->seize_delay >= current_tweak->camera.seize.delay)
                    {
                        return STATE_CAMERA_SEIZE;
                    }
                }

                return STATE_CAMERA_AIM;
            }

            // No symbols in TSSM (always inlined into update); bodies from BfBB.
            void apply_turn() const
            {
                xMat3x3 mat;

                xQuatToMat(&this->facing, &mat);
                xCameraRotate(&globals.oldSkoolCamera, mat, 0.0f, 0.0f, 0.0f);
            }

            void turn(F32 dt)
            {
                xVec3 loc = get_player_loc();
                loc.y += current_tweak->camera.aim.height;

                xVec3 dir = globals.oldSkoolCamera.mat.pos - loc;

                xMat3x3 mat;
                xMat3x3LookVec(&mat, &dir);
                xQuatFromMat(&this->target, &mat);

                F32 t = current_tweak->camera.aim.turn_speed * xexp(dt);
                if (t >= 1.0f)
                {
                    this->facing = this->target;
                }
                else
                {
                    xQuatSlerp(&this->facing, &this->facing, &this->target, t);
                }
            }

            void collide_inward()
            {
                zGlobals& g = globals;
                xSweptSphere sws;

                xVec3 tgtpos = get_player_loc();
                tgtpos.y += current_tweak->camera.aim.height;

                xSweptSpherePrepare(&sws, &tgtpos, &globals.oldSkoolCamera.mat.pos, 0.07f);
                xSweptSphereToEnv(&sws, globals.sceneCur->env);

                xRay3 ray;
                xVec3Copy(&ray.origin, &sws.start);
                xVec3Sub(&ray.dir, &sws.end, &sws.start);

                ray.max_t = xVec3Length(&ray.dir);

                F32 one_len = 1.0f / MAX(ray.max_t, 1e-5f);
                xVec3SMul(&ray.dir, &ray.dir, one_len);

                ray.flags = 0x800;
                if (!(ray.flags & 0x400))
                {
                    ray.flags |= 0x400;
                    ray.min_t = 0.0f;
                }

                xRayHitsGrid(&colls_grid, globals.sceneCur, &ray, SweptSphereHitsCameraEnt,
                             &sws.qcd, &sws);
                xRayHitsGrid(&colls_oso_grid, globals.sceneCur, &ray, SweptSphereHitsCameraEnt,
                             &sws.qcd, &sws);

                if (sws.curdist != sws.dist)
                {
                    F32 stopdist = MAX(sws.curdist, 0.5f);
                    g.oldSkoolCamera.mat.pos.x = stopdist * ray.dir.x + ray.origin.x;
                    g.oldSkoolCamera.mat.pos.y = stopdist * ray.dir.y + ray.origin.y;
                    g.oldSkoolCamera.mat.pos.z = stopdist * ray.dir.z + ray.origin.z;
                }
            }

            void apply_motion() const
            {
                xVec3 loc = get_player_loc();

                loc.x += this->dist * isin(this->phi);
                loc.y += this->height;
                loc.z += this->dist * icos(this->phi);

                xCameraMove(&globals.oldSkoolCamera, loc);
            }

            void stop(F32 dt)
            {
                xAccelStop(this->height, this->height_vel, current_tweak->camera.aim.accel, dt);
                xAccelStop(this->dist, this->dist_vel, current_tweak->camera.aim.accel, dt);
                xAccelStop(this->phi, this->phi_vel, current_tweak->camera.aim.stick_decel, dt);

                this->phi = xrmod(this->phi);
            }

            void move(F32 dt)
            {
                xAccelMove(this->height, this->height_vel, current_tweak->camera.aim.accel, dt,
                           current_tweak->camera.aim.height, current_tweak->camera.aim.max_vel);
                xAccelMove(this->dist, this->dist_vel, current_tweak->camera.aim.accel, dt,
                           current_tweak->camera.aim.dist, current_tweak->camera.aim.max_vel);

                F32 stick = -shared.sp.x;
                F32 mag = xabs(stick);
                F32 accel = mag * current_tweak->camera.aim.stick_accel +
                            (1.0f - mag) * current_tweak->camera.aim.stick_decel;

                if (stick < 0.0f)
                {
                    if (accel > 0.0f)
                    {
                        accel = -accel;
                    }
                }
                else if (accel < 0.0f)
                {
                    accel = -accel;
                }

                xAccelMove(this->phi, this->phi_vel, accel, dt,
                           mag * current_tweak->camera.aim.stick_max_vel);

                this->phi = xrmod(this->phi);
            }
        };
    } // namespace CruiseBubble
} // namespace cruise_bubble

void cruise_bubble::CruiseBubble::kill(bool reset_camera, bool abortive)
{
    if (abortive)
    {
        for (S32 i = THREAD_PLAYER; i < MAX_THREAD; ++i)
        {
            if (shared.state[i] != NULL)
            {
                shared.state[i]->abort();
                shared.state[i] = NULL;
            }
        }
    }
    else
    {
        for (S32 i = THREAD_PLAYER; i < MAX_THREAD; ++i)
        {
            set_state((thread_enum)i, STATE_INVALID);
        }
    }

    shared.flags = 0x3;
    xCameraDoCollisions(1, 1);
    zCameraEnableTracking(CO_CRUISE_BUBBLE);
    ztalkbox::permit(0xffffffff, 0);
    if (reset_camera)
    {
        zCameraEnableInput();
        globals.oldSkoolCamera.fov = shared.fov_default;
    }

    hide_wand();
    hide_missle();
    hide_hud();
    stop_trail();
    distort_screen(0.0f);
    xSndMgrSelectListenerMode(xSndListener_PLAYER);

    xAnimPlay* aplay = xglobals->___player_ent_dont_use_directly->model->Anim;
    xAnimState* astate = aplay->Single->State;
    if (astate == shared.astate.player.aim || astate == shared.astate.player.fire ||
        astate == shared.astate.player.fire2 || astate == shared.astate.player.idle)
    {
        xAnimPlayStartTransition(aplay, shared.atran.player.end);
    }

    exit_triggers(*globals.sceneCur);

    xCam* defaultCam = zCamGetDefault();
    if (defaultCam->cfg_common.priority == 0x7e)
    {
        defaultCam->cfg_common.priority = 0;
        xglobals->cam->priority_force_cut();
    }
    zCameraEnableTracking(CO_BOSSCRUISELAUNCH);

    if (curSourceSimpObj != NULL)
    {
        zEntEvent(curSourceSimpObj, eEventInvisible);
        curSourceSimpObj = NULL;
    }

    if (gReticleTarget[0] != NULL)
    {
        sReticleAlpha[0] = 0.0f;
    }
    gReticleTarget[0] = NULL;
    sTypeOfTarget[0] = 4;
}

RpAtomic* cruise_bubble::CruiseBubble::custom_bubble_render(RpAtomic* atomic)
{
    F32 fade = shared.missle_model->Alpha;
    RwCullMode old_cull_mode;
    F32 fresnel_coeff;
    F32 env_coeff;

    RwRenderStateGet(rwRENDERSTATECULLMODE, (void*)&old_cull_mode);
    RwRenderStateSet(rwRENDERSTATECULLMODE, (void*)rwCULLMODECULLNONE);
    iDrawSetFBMSK(0xFFFFFFFF, 0xFFFFFF, true);
    AtomicDisableMatFX(atomic);
    RpSkinAtomicSetType(atomic, rpSKINTYPEMATFX);
    gAtomicRenderCallBack(atomic);
    iDrawSetFBMSK(0, 0xFFFFFF, true);

    if (current_tweak->material.fresnel_texture != 0)
    {
        fresnel_coeff = current_tweak->material.fresnel_coeff * fade;
        iModelSetMaterialAlpha(atomic,
                               255.0f * current_tweak->material.fresnel_alpha * fade + 0.5f);
        gFXSurfaceFlags = 0x10;
        xFXAtomicEnvMapSetup(atomic, current_tweak->material.fresnel_texture, fresnel_coeff);
        gFXSurfaceFlags = 0;
        gAtomicRenderCallBack(atomic);
    }

    if (current_tweak->material.env_texture != 0)
    {
        env_coeff = current_tweak->material.env_coeff * fade;
        iModelSetMaterialAlpha(atomic, 255.0f * current_tweak->material.env_alpha * fade + 0.5f);
        AtomicDisableMatFX(atomic);
        gFXSurfaceFlags = 0x10;
        xFXAtomicEnvMapSetup(atomic, current_tweak->material.env_texture, env_coeff);
        gFXSurfaceFlags = 0;
        gAtomicRenderCallBack(atomic);
    }

    RwRenderStateSet(rwRENDERSTATECULLMODE, (void*)old_cull_mode);

    return atomic;
}

namespace cruise_bubble
{
    namespace CruiseBubble
    {
        struct state_missle_explode : state_type
        {
            F32 hit_time;

            struct cb_damage_ent
            {
                F32 radius;

                // No symbols in TSSM (always inlined into xGridCheckBound<cb_damage_ent>); bodies
                // from BfBB.
                cb_damage_ent(F32 radius) : radius(radius)
                {
                }

                bool operator()(xEnt& ent)
                {
                    if (!(ent.chkby & 0x10))
                    {
                        return true;
                    }

                    if (!can_damage(&ent))
                    {
                        return true;
                    }

                    xSphere o = { shared.hit_loc, radius };
                    if (!xSphereHitsBound(o, ent.bound))
                    {
                        return true;
                    }

                    if (was_damaged(&ent))
                    {
                        return true;
                    }

                    if (ent.collLev == 5)
                    {
                        xCollis coll;
                        coll.flags = 0;
                        xSphereHitsModel(&o, ent.model, &coll);
                        if (!(coll.flags & 0x1))
                        {
                            return true;
                        }
                    }

                    damage_entity(ent, shared.hit_loc, get_missle_mat()->at, shared.hit_norm,
                                  radius, true);
                    return true;
                }
            };

            state_missle_explode() : state_type(STATE_MISSLE_EXPLODE)
            {
            }

            void start()
            {
                shared.flags |= 0x40;
                hit_time = 0.0f;

                if (current_tweak->missle.explode.hit_duration <= 0.0f)
                {
                    if (zEntPlayer_PowerupLevel(PLYR_ATAK_SB_GUITAR) == ePowerupLevelNormal)
                    {
                        apply_damage(current_tweak->missle.explode.hit_radius);
                    }
                    else if (zEntPlayer_PowerupLevel(PLYR_ATAK_SB_GUITAR) > ePowerupLevelNormal)
                    {
                        apply_damage(current_tweak->missle.explode.hit_radius_super);
                        start_super_hazard(&get_missle_mat()->pos);
                    }
                }

                // dist is not in the DWARF (min_dist is), but retail needs both as plain variables
                // for the inlined set_state below.
                F32 dist = (shared.hit_loc - get_player_loc()).length2();
                F32 min_dist = current_tweak->camera.survey.min_dist;
                set_state(THREAD_CAMERA,
                          dist <= min_dist * min_dist ? STATE_CAMERA_RESTORE : STATE_CAMERA_SURVEY);

                stop_sound(0, (iSndHandle)-1);
                stop_sound(4, (iSndHandle)-1);
                stop_sound(7, (iSndHandle)-1);
                play_sound(&get_missle_mat()->pos,
                           zEntPlayer_PowerupLevel(PLYR_ATAK_SB_GUITAR) > ePowerupLevelNormal ? 5 :
                                                                                                1,
                           1.0f);

                start_effects();

                if (gReticleTarget[0] != NULL)
                {
                    sReticleAlpha[0] = 0.0f;
                }
                gReticleTarget[0] = NULL;
                sTypeOfTarget[0] = 4;

                renderRing = TRUE;
                ringTex = (RwTexture*)xSTFindAsset(xStrHash("fx_ring"), NULL);

                xScreenWarp::emit(shared.hit_loc, get_hit_radius(),
                                  current_tweak->missle.explode.hit_duration * 4.0f,
                                  xScreenWarp::TYPE_SHOCKWAVE, 1.5f,
                                  current_tweak->missle.explode.hit_duration * 4.0f, 0);
            }

            void stop()
            {
                renderRing = FALSE;
                shared.flags &= ~0x40;
            }

            state_enum update(F32 dt)
            {
                hit_time += dt;

                if (hit_time <= current_tweak->missle.explode.hit_duration)
                {
                    apply_damage(get_radius());

                    ringPos = shared.hit_loc;
                    ringRadius = 0.5f * get_radius();
                    renderRing = TRUE;
                    ringAlpha = 0xff;
                }
                else if (hit_time <= 4.0f * current_tweak->missle.explode.hit_duration)
                {
                    if (hit_time <= 2.0f * current_tweak->missle.explode.hit_duration)
                    {
                        ringAlpha = 0xff;
                        ringRadius =
                            get_hit_radius() *
                            (0.5f + 0.5f * (hit_time - current_tweak->missle.explode.hit_duration) /
                                        current_tweak->missle.explode.hit_duration);
                    }
                    else
                    {
                        ringAlpha =
                            (U8)(255.0f *
                                 (2.0f * current_tweak->missle.explode.hit_duration -
                                  (hit_time - 2.0f * current_tweak->missle.explode.hit_duration)) /
                                 (2.0f * current_tweak->missle.explode.hit_duration));
                        ringRadius = get_hit_radius();
                    }
                }
                else
                {
                    renderRing = FALSE;
                }

                return STATE_MISSLE_EXPLODE;
            }

            // No symbol in TSSM (always inlined); name unknown. Retail leaves the radius undefined
            // when the guitar powerup is disabled (kept as is).
            static F32 get_hit_radius()
            {
                F32 radius;
                if (zEntPlayer_PowerupLevel(PLYR_ATAK_SB_GUITAR) == ePowerupLevelNormal)
                {
                    radius = current_tweak->missle.explode.hit_radius;
                }
                else if (zEntPlayer_PowerupLevel(PLYR_ATAK_SB_GUITAR) > ePowerupLevelNormal)
                {
                    radius = current_tweak->missle.explode.hit_radius_super;
                }
                return radius;
            }

            // No symbol in TSSM (always inlined); BfBB's get_radius with TSSM's powerup radius.
            F32 get_radius() const
            {
                F32 t_frac = hit_time / current_tweak->missle.explode.hit_duration;
                return t_frac * get_hit_radius();
            }

            void start_effects()
            {
                zFX_SpawnBubbleBlast(&get_missle_mat()->pos, current_tweak->blast.emit,
                                     current_tweak->blast.radius, current_tweak->blast.vel,
                                     current_tweak->blast.rand_vel);
                CBFXtra::RockStar_Action(CBFXtra::TUNE_EXPLODE, CBFXtra::TUNE_TRAN_ENTER);
            }

            // No symbols in TSSM (always inlined); bodies from BfBB.
            void apply_damage(F32 radius)
            {
                xBound bound;
                bound.type = XBOUND_TYPE_SPHERE;
                bound.sph.center = shared.hit_loc;
                bound.sph.r = radius;
                xQuickCullForSphere(&bound.qcd, &bound.sph);

                cb_damage_ent cb(radius);
                xGridCheckBound(colls_grid, bound, bound.qcd, cb);
                xGridCheckBound(colls_oso_grid, bound, bound.qcd, cb);
                xGridCheckBound(npcs_grid, bound, bound.qcd, cb);

                apply_damage_hazards(radius);
            }

            void apply_damage_hazards(F32 param)
            {
                HAZ_Iterate(hazard_check, *(void**)&param, 0x208000);
            }

            // No symbol in TSSM (always inlined into start); the PS2 build computes the missile
            // position before HAZ_Acquire as well, so the position is a parameter.
            void start_super_hazard(const xVec3* pos)
            {
                zNMEHazard* haz = HAZ_Acquire();
                if (haz == NULL)
                {
                    return;
                }

                if (!haz->ConfigHelper(HAZ_TYP_POWERUP_EXPLOSION_V1))
                {
                    haz->Discard();
                    return;
                }

                haz->Start(pos, -1.0f);
            }

            static bool hazard_check(zNMEHazard& haz, void* context)
            {
                F32 radius = *(F32*)&context;
                xVec3 offset = haz.pos_hazard - shared.hit_loc;
                F32 max_dist = radius + haz.custdata.typical.rad_cur;

                if (offset.length2() < max_dist * max_dist &&
                    haz.typ_hazard != HAZ_TYP_PLYRBANG_V1 &&
                    haz.typ_hazard != HAZ_TYP_POWERUP_EXPLOSION_V1)
                {
                    haz.flg_hazard |= 0x4;
                }

                return true;
            }
        };
    } // namespace CruiseBubble
} // namespace cruise_bubble

namespace cruise_bubble
{
    namespace CruiseBubble
    {
        struct state_camera_seize : state_type
        {
            F32 blend_time;
            xVec3 start_loc;
            xQuat start_dir;
            xQuat end_dir;
            xQuat cur_dir;
            F32 last_s;
            F32 fov;
            F32 wipe_bubbles;

            state_camera_seize() : state_type(STATE_CAMERA_SEIZE)
            {
            }

            void start()
            {
                capture_camera();

                this->blend_time = 0.0f;
                this->start_loc = globals.oldSkoolCamera.mat.pos;
                xQuatFromMat(&this->start_dir, &globals.oldSkoolCamera.mat);
                this->last_s = 0.0f;
                this->fov = 0.0f;
                this->wipe_bubbles = 0.0f;

                show_hud();
                distort_screen(0.0f);
            }

            void stop()
            {
                release_camera();
                xCameraSetFOV(&globals.oldSkoolCamera, shared.fov_default);
            }

            state_enum update(F32 dt)
            {
                this->blend_time += dt;
                if (this->blend_time >= current_tweak->camera.seize.blend_time)
                {
                    return STATE_CAMERA_ATTACH;
                }

                F32 s = xSCurve(this->blend_time / current_tweak->camera.seize.blend_time);
                this->update_move(s);
                this->update_turn(s);

                xVec3 offset = get_missle_mat()->pos - globals.oldSkoolCamera.mat.pos;
                this->refresh_missle_alpha(offset.length());

                xCameraSetFOV(&globals.oldSkoolCamera,
                              s * (current_tweak->camera.seize.fov - shared.fov_default) +
                                  shared.fov_default);
                distort_screen(s);

                return STATE_CAMERA_SEIZE;
            }

            // No symbols in TSSM (always inlined into update); bodies from BfBB.
            void refresh_missle_alpha(F32 dist)
            {
                F32 fade = current_tweak->camera.seize.fade_dist;
                F32 hide = current_tweak->camera.seize.hide_dist;

                if (dist >= fade)
                {
                    show_missle();
                    shared.missle_model->Alpha = 1.0f;
                }
                else if (dist <= hide)
                {
                    hide_missle();
                    shared.missle_model->Flags |= 0x2;
                }
                else
                {
                    shared.missle_model->Alpha = (hide - dist) / (hide - fade);
                }
            }

            void update_turn(F32 s)
            {
                if (iabs(s - 1.0f) <= 0.0001f)
                {
                    xCameraRotate(&globals.oldSkoolCamera, *get_missle_mat(), 0.0f, 0.0f, 0.0f);
                }
                else
                {
                    xQuatFromMat(&this->end_dir, get_missle_mat());
                    xQuatSlerp(&this->cur_dir, &this->start_dir, &this->end_dir,
                               (s - this->last_s) / (1.0f - this->last_s));
                    this->start_dir = this->cur_dir;
                    this->last_s = s;

                    xMat3x3 mat;
                    xQuatToMat(&this->cur_dir, &mat);
                    xCameraRotate(&globals.oldSkoolCamera, mat, 0.0f, 0.0f, 0.0f);
                }
            }

            void update_move(F32 s)
            {
                const xVec3& pos = get_missle_mat()->pos;
                xVec3 dir = pos - this->start_loc;
                xVec3 loc = this->start_loc + dir * s;

                xCameraMove(&globals.oldSkoolCamera, loc);
            }
        };

        struct state_camera_attach : state_type
        {
            struct cb_lock_targets
            {
                // No symbol in TSSM (always inlined into xGridCheckBound<cb_lock_targets>); body
                // from BfBB.
                bool operator()(xEnt& ent)
                {
                    if (!(ent.chkby & 0x10))
                    {
                        return true;
                    }

                    if (!can_damage(&ent))
                    {
                        return true;
                    }

                    check_lock_target(&ent);
                    return true;
                }
            };

            xEnt* best_target_ent;
            F32 best_target;
            F32 reticle_delay;

            state_camera_attach() : state_type(STATE_CAMERA_ATTACH)
            {
            }

            // Defined out of class below; inline, as retail has every member as a weak inline.
            inline void start();
            inline void stop();

            state_enum update(F32 dt)
            {
                xMat4x3* mat = get_missle_mat();
                if (!((state_missle_fly*)shared.states[STATE_MISSLE_FLY])->boost)
                {
                    xCameraMove(&globals.oldSkoolCamera, mat->pos);
                    xCameraRotate(&globals.oldSkoolCamera, *mat, 0.0f, 0.0f, 0.0f);
                }

                if (!zRingControl::IsRingChallengeActive())
                {
                    this->lock_targets();
                }

                return STATE_CAMERA_ATTACH;
            }

            // No symbols in TSSM (always inlined into update); bodies from BfBB. TSSM drops the
            // hazard targets and hands the best target to the player's reticle with the guitar
            // powerup.
            void lock_targets()
            {
                cb_lock_targets targets;
                xBound bound;

                this->get_view_bound(bound);

                this->best_target = 1.0f;
                this->best_target_ent = NULL;

                xGridCheckBound(colls_grid, bound, bound.qcd, targets);
                xGridCheckBound(colls_oso_grid, bound, bound.qcd, targets);
                xGridCheckBound(npcs_grid, bound, bound.qcd, targets);

                if (zEntPlayer_PowerupLevel(PLYR_ATAK_SB_GUITAR) > ePowerupLevelNormal)
                {
                    // not in any DWARF (inlined code): retail loads the target once
                    xEnt* target = this->best_target_ent;
                    if (gReticleTarget[0] != target)
                    {
                        sReticleAlpha[0] = 0.0f;
                    }
                    gReticleTarget[0] = target;
                    sTypeOfTarget[0] = 4;
                }
            }

            void get_view_bound(xBound& bound) const
            {
                bound.type = XBOUND_TYPE_BOX;

                xMat4x3& mat = globals.oldSkoolCamera.mat;
                F32 dist = current_tweak->reticle.dist_max - current_tweak->reticle.dist_min;
                F32 s = isin(current_tweak->reticle.ang_hide);
                F32 r1 = s * current_tweak->reticle.dist_min;
                F32 r2 = s * current_tweak->reticle.dist_max;

                xVec3 center = mat.pos + mat.at * current_tweak->reticle.dist_min;

                xBoxFromCone(bound.box.box, center, mat.at, dist, r1, r2);
                bound.box.center = (bound.box.box.upper + bound.box.box.lower) * 0.5f;

                xQuickCullForBox(&bound.qcd, &bound.box.box);
            }
        };

        struct state_camera_survey : state_type
        {
            F32 time;
            xVec2 start_sp;
            F32 path_distance[127];

            state_camera_survey() : state_type(STATE_CAMERA_SURVEY)
            {
            }

            void start()
            {
                if (camera_taken())
                {
                    release_camera();
                    this->time = current_tweak->camera.survey.duration;
                    return;
                }

                capture_camera();
                this->time = 0.0f;
                this->init_path();
                this->move();
                this->start_sp = shared.sp;
            }

            void stop()
            {
                release_camera();
            }

            state_enum update(F32 dt)
            {
                this->time += dt;

                if (camera_taken())
                {
                    return STATE_CAMERA_RESTORE;
                }

                if (this->time >= current_tweak->camera.survey.duration)
                {
                    return STATE_CAMERA_RESTORE;
                }

                // 0x3F33F3: any button
                if (this->time >= current_tweak->camera.survey.min_duration &&
                    ((globals.pad0->pressed & 0x3F33F3) || this->control_jerked()))
                {
                    return STATE_CAMERA_RESTORE;
                }

                this->move();
                return STATE_CAMERA_SURVEY;
            }

            // No symbol in TSSM (always inlined into update); body from BfBB.
            bool control_jerked() const
            {
                F32 offset = current_tweak->camera.survey.jerk_offset;
                F32 deflect = current_tweak->camera.survey.jerk_deflect;

                xVec2 dsp = shared.sp - this->start_sp;
                bool jerked = false;

                if (dsp.length2() >= offset * offset && shared.sp.length2() >= deflect * deflect)
                {
                    jerked = true;
                }

                return jerked;
            }

            void move()
            {
                F32 s = xSCurve(this->time / current_tweak->camera.survey.duration,
                                current_tweak->camera.survey.drift_softness);

                xVec3 loc;
                F32 roll;
                this->eval_missle_path(s * (current_tweak->camera.survey.drift_dist -
                                            current_tweak->camera.survey.cut_dist) +
                                           current_tweak->camera.survey.cut_dist,
                                       loc, roll);

                xVec3 dir = (shared.hit_loc - loc).up_normal();

                xCameraMove(&globals.oldSkoolCamera, loc);
                roll *= 1.0f - s;
                xCameraRotate(&globals.oldSkoolCamera, dir, roll, 0.0f, 0.0f, 0.0f);
            }

            // No symbol in TSSM (always inlined into start); body from BfBB.
            void init_path()
            {
                F32 dist = 0.0f;

                fixed_queue<missle_record_data, 127>::iterator it = missle_record.begin();
                fixed_queue<missle_record_data, 127>::iterator end = missle_record.end();

                // The first record through a reference, like rec in the loop, as retail's registers
                // show.
                const missle_record_data& first = *it;
                xVec3 last = first.loc;
                F32* d = this->path_distance;

                while (it != end)
                {
                    const missle_record_data& rec = *it;

                    *d = dist + (rec.loc - last).length();
                    dist = *d;
                    last = rec.loc;

                    ++it;
                    d++;
                }
            }

            // No symbols in TSSM (always inlined into move); bodies from BfBB.
            void eval_missle_path(F32 dist, xVec3& loc, F32& roll) const
            {
                S32 index = this->find_nearest(dist);

                if (index <= 0)
                {
                    index++;
                }
                else if (index >= (S32)missle_record.size())
                {
                    index = missle_record.size() - 1;
                }

                const missle_record_data& rec0 = missle_record[index - 1];
                const missle_record_data& rec1 = missle_record[index];

                F32 d0 = this->path_distance[index - 1];
                F32 d1 = this->path_distance[index];

                if (xabs(d0 - d1) <= 0.0001f)
                {
                    loc = rec0.loc;
                    roll = rec0.roll;
                }
                else
                {
                    F32 t = (dist - d0) / (d1 - d0);

                    this->lerp(loc, t, rec0.loc, rec1.loc);
                    this->lerp(roll, t, rec0.roll, rec1.roll);
                }
            }

            S32 find_nearest(F32 radius) const
            {
                S32 i = 0;
                S32 range = missle_record.size();

                while (i < range)
                {
                    S32 j = (i + range) / 2;

                    if (radius <= this->path_distance[j])
                    {
                        range = j;
                    }
                    else if (radius > this->path_distance[j])
                    {
                        i = j + 1;
                    }
                }

                return i;
            }

            void lerp(F32& a, F32 b, F32 c, F32 d) const
            {
                a = b * (d - c) + c;
            }

            void lerp(xVec3& a, F32 b, const xVec3& c, const xVec3& d) const
            {
                this->lerp(a.x, b, c.x, d.x);
                this->lerp(a.y, b, c.y, d.y);
                this->lerp(a.z, b, c.z, d.z);
            }
        };

        struct state_camera_restore : state_type
        {
            F32 control_delay;

            state_camera_restore() : state_type(STATE_CAMERA_RESTORE)
            {
            }

            void start()
            {
                xCam* defaultCam = zCamGetDefault();
                if (defaultCam->cfg_common.priority == 0x7e)
                {
                    defaultCam->cfg_common.priority = 0;
                    xglobals->cam->priority_force_cut();
                }
                zCameraEnableTracking(CO_BOSSCRUISELAUNCH);

                this->control_delay = 0.0f;
                hide_hud();

                if (!camera_leave())
                {
                    xVec3 loc = get_player_loc() + start_cam_mat.pos;
                    xCameraMove(&globals.oldSkoolCamera, loc);
                    xCameraRotate(&globals.oldSkoolCamera, start_cam_mat, 0.0f, 0.0f, 0.0f);
                }

                if (camera_taken())
                {
                    release_camera();
                }
                else
                {
                    capture_camera();
                }

                xSndMgrSelectListenerMode(xSndListener_PLAYER);
            }

            // Defined out of class below; inline, as retail has every member as a weak inline.
            inline void stop();
            inline state_enum update(F32 dt);
        };
    } // namespace CruiseBubble
} // namespace cruise_bubble

inline void cruise_bubble::CruiseBubble::state_player_aim::start()
{
    shared.flags |= 0x20;

    xVec3& player_dir = get_player_mat()->at;
    this->yaw = xatan2(player_dir.x, player_dir.z);
    this->yaw_vel = 0.0f;
    this->turn_delay = 0.0f;

    xAnimPlayStartTransition(xglobals->___player_ent_dont_use_directly->model->Anim,
                             shared.atran.player.aim);
    set_state(THREAD_CAMERA, STATE_CAMERA_AIM);
}

inline void cruise_bubble::CruiseBubble::state_player_aim::stop()
{
    shared.flags &= ~0x20;
}

inline void cruise_bubble::CruiseBubble::state_missle_appear::stop()
{
    hide_missle();
}

inline void cruise_bubble::CruiseBubble::state_camera_attach::start()
{
    capture_camera();
    xMat4x3* mat = get_missle_mat();
    xCameraMove(&globals.oldSkoolCamera, mat->pos);
    xCameraRotate(&globals.oldSkoolCamera, *mat, 0.0f, 0.0f, 0.0f);
    CBFXtra::RockStar_Action(CBFXtra::TUNE_SPEAKER, CBFXtra::TUNE_TRAN_EXIT);
    zEntEvent(curSourceSimpObj, eEventInvisible);
    xCameraSetFOV(&globals.oldSkoolCamera, current_tweak->camera.seize.fov);
    distort_screen(1.0f);
}

inline void cruise_bubble::CruiseBubble::state_camera_attach::stop()
{
    xCameraSetFOV(&globals.oldSkoolCamera, shared.fov_default);
    release_camera();
    hide_hud();
    distort_screen(0.0f);
}

inline void cruise_bubble::CruiseBubble::state_camera_restore::stop()
{
    set_state(THREAD_PLAYER, STATE_INVALID);
    release_camera();
}

inline cruise_bubble::CruiseBubble::state_enum
cruise_bubble::CruiseBubble::state_camera_restore::update(F32 dt)
{
    this->control_delay += dt;
    if (this->control_delay >= current_tweak->camera.restore.control_delay)
    {
        return STATE_INVALID;
    }

    return STATE_CAMERA_RESTORE;
}

void cruise_bubble::CruiseBubble::init_states()
{
    static state_player_halt player_halt;
    shared.states[STATE_PLAYER_HALT] = &player_halt;

    static state_player_aim player_aim;
    shared.states[STATE_PLAYER_AIM] = &player_aim;

    static state_player_fire player_fire;
    shared.states[STATE_PLAYER_FIRE] = &player_fire;

    static state_player_wait player_wait;
    shared.states[STATE_PLAYER_WAIT] = &player_wait;

    static state_missle_appear missle_appear;
    shared.states[STATE_MISSLE_APPEAR] = &missle_appear;

    static state_missle_fly missle_fly;
    shared.states[STATE_MISSLE_FLY] = &missle_fly;

    static state_missle_explode missle_explode;
    shared.states[STATE_MISSLE_EXPLODE] = &missle_explode;

    static state_camera_aim camera_aim;
    shared.states[STATE_CAMERA_AIM] = &camera_aim;

    static state_camera_seize camera_seize;
    shared.states[STATE_CAMERA_SEIZE] = &camera_seize;

    static state_camera_attach camera_attach;
    shared.states[STATE_CAMERA_ATTACH] = &camera_attach;

    static state_camera_survey camera_survey;
    shared.states[STATE_CAMERA_SURVEY] = &camera_survey;

    static state_camera_restore camera_return;
    shared.states[STATE_CAMERA_RESTORE] = &camera_return;
}

void cruise_bubble::CruiseBubble::reset_wake_ribbons()
{
    const char* nam_wakeRibbonTexture;
    static xFXRibbon::config_type ribcfg;

    xFXRibbon::load_default_config(ribcfg);
    nam_wakeRibbonTexture = "cruise_guitar_staff";
    ribcfg.blend_src = 5;
    ribcfg.blend_dst = 2;
    ribcfg.raster = NMEC_FindRWRaster(nam_wakeRibbonTexture);
    ribcfg.life_time = 1.5f;
    ribcfg.pivot = 1.0f;

    if (!(shared.flags & 0x200))
    {
        ribcfg.curve = (xFXRibbon::curve_node*)wake_ribbon_curve;
        ribcfg.curve_size = 7;
    }
    else
    {
        ribcfg.curve = (xFXRibbon::curve_node*)cheat_wake_ribbon_curve;
        ribcfg.curve_size = 4;
    }

    wake_ribbon[0].set_config(&ribcfg);
    wake_ribbon[1].set_config(&ribcfg);
}

xCam* cruise_bubble::xCamLaunchCam;

namespace cruise_bubble
{
    namespace CruiseBubble
    {
        namespace CBFXtra
        {
            static CBFXData cbfxdata;

            // No symbol (inlined into RockStar_Update); named after Speak_Update/Flight_Start.
            static inline void Flight_Update(F32 dt)
            {
                if (cbfxdata.haz_flight == NULL)
                {
                    return;
                }

                xMat4x3 mat_missile = *(xMat4x3*)shared.missle_model->Mat;

                cbfxdata.haz_flight->PosSet(&mat_missile.pos);
                cbfxdata.haz_flight->TypData_RotMatSet(&mat_missile);
                cbfxdata.haz_flight->LifespanSet(0.2f, 1.0f);
            }

            // No symbol (inlined into RockStar_Action).
            static inline void Speak_Start()
            {
                if (curSourceSimpObj == NULL)
                {
                    return;
                }

                zNMEHazard* haz = HAZ_Acquire();
                if (haz == NULL)
                {
                    return;
                }

                S32 rc = haz->ConfigHelper(HAZ_TYP_TUNEBORN);
                if (!rc)
                {
                    return;
                }

                haz->npc_owner = NULL;

                xMat4x3 mat_speak = *(xMat4x3*)curSourceSimpObj->model->Mat;
                xVec3 pos_launch = mat_speak.pos;

                haz->TypData_RotMatSet(&mat_speak);

                cbfxdata.haz_speaker = haz;
                static HazGone byeByeNotice;
                haz->cb_notify = &byeByeNotice;

                haz->Start(&pos_launch, 1.0f);
            }

            // No symbol (inlined into RockStar_Action).
            static inline void Flight_Start()
            {
                zNMEHazard* haz = HAZ_Acquire();
                if (haz == NULL)
                {
                    return;
                }

                S32 rc = haz->ConfigHelper(HAZ_TYP_TUNEFLYT);
                if (!rc)
                {
                    return;
                }

                haz->npc_owner = NULL;

                xMat4x3 mat_missile = *(xMat4x3*)shared.missle_model->Mat;
                xVec3 pos_launch = mat_missile.pos;

                haz->TypData_RotMatSet(&mat_missile);

                cbfxdata.haz_flight = haz;
                static HazGone byeByeNotice;
                haz->cb_notify = &byeByeNotice;

                haz->Start(&pos_launch, 1.0f);
            }

            // No symbol (inlined into RockStar_Action); the Xbox build keeps it out of line. Name
            // invented, after Speak_Start/Speak_Update.
            static inline void Speak_Stop()
            {
                if (cbfxdata.haz_speaker != NULL)
                {
                    cbfxdata.haz_speaker->flg_hazard |= 0x4;
                    cbfxdata.haz_speaker = NULL;
                }
            }

            // No symbol (inlined into RockStar_Action); the Xbox build keeps it out of line. Name
            // invented, after Flight_Start/Flight_Update.
            static inline void Flight_Stop()
            {
                if (cbfxdata.haz_flight != NULL)
                {
                    cbfxdata.haz_flight->flg_hazard |= 0x4;
                    cbfxdata.haz_flight = NULL;
                }
            }

            // No symbol (always inlined into RockStar_Action); the Xbox build keeps it out of line.
            // Name invented, after Speak_Start/Flight_Start and HAZ_TYP_TUNEBOOM.
            static inline void Boom_Start()
            {
                zNMEHazard* haz = HAZ_Acquire();
                if (haz == NULL)
                {
                    return;
                }

                S32 rc = haz->ConfigHelper(HAZ_TYP_TUNEBOOM);
                if (!rc)
                {
                    return;
                }

                haz->npc_owner = NULL;

                xMat4x3 mat_missile = *(xMat4x3*)shared.missle_model->Mat;
                xVec3 pos_launch = mat_missile.pos;

                haz->TypData_RotMatSet(&mat_missile);
                haz->Start(&pos_launch, 1.0f);
            }
        } // namespace CBFXtra
    } // namespace CruiseBubble
} // namespace cruise_bubble

void cruise_bubble::CruiseBubble::add_trail_sample(const xVec3& loc0, const xVec3& dir0,
                                                   const xVec3& loc1, const xVec3& dir1, F32 dt)
{
    shared.trail.bubbles += dt * current_tweak->trail.bubble_rate;
    U32 bubbles = shared.trail.bubbles;

    if (bubbles != 0)
    {
        shared.trail.bubbles -= bubbles;

        xVec3 emit_range = { 0.0f, 0.0f, 0.0f };
        zFX_SpawnBubbleTrail(&loc0, &loc1, bubbles, &emit_range, NULL);

        xVec3 offset0 = dir0 * current_tweak->trail.bubble_emit_radius;
        xVec3 offset1 = dir1 * current_tweak->trail.bubble_emit_radius;
        xVec3 prop_loc0[2] = { loc0 + offset0, loc0 - offset0 };
        xVec3 prop_loc1[2] = { loc1 + offset1, loc1 - offset1 };
        zFX_SpawnBubbleTrail(&prop_loc0[0], &prop_loc1[0], bubbles, &emit_range, NULL);
        zFX_SpawnBubbleTrail(&prop_loc0[1], &prop_loc1[1], bubbles, &emit_range, NULL);
    }

    // Not in the PS2 DWARF, but retail reads shared.flags once, before the first insert.
    S32 flags = shared.flags & 0x100 ? 1 : 0;
    xVec3 offset = dir1 * current_tweak->trail.wake_emit_radius;
    wake_ribbon[0].insert(loc1 + offset, dir1, 1.0f, 1.0f, flags);
    wake_ribbon[1].insert(loc1 - offset, -dir1, 1.0f, 1.0f, flags);
}

void cruise_bubble::CruiseBubble::update_trail(F32 dt)
{
    if (!(shared.flags & 0x80))
    {
        return;
    }

    shared.trail.samples += dt * current_tweak->trail.sample_rate;
    S32 samples = (S32)shared.trail.samples;

    if (samples <= 0)
    {
        shared.trail.samples = 0.0f;
        samples = 1;
    }
    else
    {
        shared.trail.samples -= samples;
    }

    xMat4x3 end_mat;
    xQuat end_dir;
    refresh_trail(end_mat, end_dir);

    F32 ds = 1.0f / samples;
    F32 ddt = dt * ds;
    xVec3 dloc = (end_mat.pos - shared.trail.mat.pos) * ds;
    xMat4x3 mat[2];
    mat[0] = shared.trail.mat;
    S32 flip = 0;
    F32 s = ds;

    for (S32 i = 0; i < samples; ++i)
    {
        xMat4x3& mat0 = mat[flip];
        flip ^= 1;
        xMat4x3& mat1 = mat[flip];

        xQuat subdir;
        xQuatSlerp(&subdir, &shared.trail.dir, &end_dir, s);
        xQuatToMat(&subdir, &mat1);
        mat1.pos = mat0.pos + dloc;
        add_trail_sample(mat0.pos, mat0.right, mat1.pos, mat1.right, ddt);
        s += ds;
    }

    shared.trail.mat = end_mat;
    shared.trail.dir = end_dir;
    shared.flags &= ~0x100;
}

namespace cruise_bubble
{
    namespace CruiseBubble
    {
        // No symbol in TSSM (always inlined); body from BfBB. Defined after add_trail_sample for
        // retail's .rodata order; the alpha byte is computed once, before the model loop.
        static inline void render_model_2d(xModelInstance* model, const basic_rect<F32>& r,
                                           F32 alpha)
        {
            xVec3 from = { 0.0f, 0.0f, 1.0f };
            xVec3 to = { 0.0f, 0.0f, -0.01f };

            xMat4x3 frame;
            frame.right.assign(1.01f, 0.0f, 0.0f);
            frame.up.assign(0.0f, 1.01f, 0.0f);
            frame.at.assign(0.0f, 0.0f, 0.01f);
            frame.pos = 0.0f;

            U8 a = 255.0f * alpha + 0.5f;
            for (; model != NULL; model = model->Next)
            {
                xModelSetMaterialAlpha(model, a);
                xModelSetFrame(model, &frame);
                xModelRender2D(*model, r, from, to);
            }
        }

        // No symbol in TSSM GC (always inlined; the PS2 build has it); body from BfBB.
        static inline void render_glow(xModelInstance* model, const basic_rect<F32>& r, F32 glow,
                                       F32 alpha)
        {
            alpha *= glow;

            F32 dsize = (1.0f / 3.0f) * (glow * current_tweak->hud.glow_size);
            F32 dloc = 0.5f * -dsize;
            F32 dalpha = 0.25f * -alpha;

            basic_rect<F32> bound = r;

            for (S32 i = 0; i < 3; i++)
            {
                render_model_2d(model, bound, alpha);

                bound.x += dloc;
                bound.y += dloc;
                bound.w += dsize;
                bound.h += dsize;
                alpha += dalpha;
            }
        }
    } // namespace CruiseBubble
} // namespace cruise_bubble

void cruise_bubble::CruiseBubble::init_hud()
{
    hud.hiding = false;
    hud.alpha = 0.0f;
    hud.alpha_vel = 0.0f;
    hud.glow = 0.0f;
    hud.gizmos_used = 0;

    hud.model.reticle = load_model(xStrHash("ui_3dicon_reticle"));
    hud.model.target = load_model(xStrHash("ui_3dicon_target_lock"));
    hud.model.wind = load_model(xStrHash("ui_3dicon_missile_frame02"));

    hud.uv_wind.init(hud.model.wind->Data);
    hud.uv_wind.offset_vel.assign(current_tweak->hud.wind.du, current_tweak->hud.wind.dv);
}

void cruise_bubble::CruiseBubble::render_timer(F32 alpha, F32 glow)
{
    state_missle_fly* state = (state_missle_fly*)shared.state[THREAD_MISSLE];
    if (state == NULL || state->type != STATE_MISSLE_FLY)
    {
        return;
    }

    F32 life = state->life;
    char buffer[16];
    sprintf(buffer, "%02d:%02d", (S32)life, (S32)(100.0f * life) - 100 * (S32)life);

    F32 dsize = glow * current_tweak->hud.timer.glow_size;
    xfont font =
        xfont::create(current_tweak->hud.timer.font, current_tweak->hud.timer.font_width + dsize,
                      current_tweak->hud.timer.font_height + dsize, 0.0f, g_WHITE, screen_bounds);

    iColor_tag lo = { 0x80, 0x00, 0x00, 0xFF };
    iColor_tag hi = { 0xFF, 0x14, 0x14, 0xFF };
    lerp(font.color, glow, lo, hi);
    font.color.a = 255.0f * alpha + 0.5f;

    basic_rect<F32> bound = font.bounds(buffer);
    F32 x = current_tweak->hud.timer.x - bound.x - 0.5f * bound.w;
    F32 y = current_tweak->hud.timer.y - bound.y - 0.5f * bound.h;

    if (glow > 0.95f)
    {
        xFXHighDynamicRangeBrighten(true);
    }

    font.render(buffer, x, y);

    xFXHighDynamicRangeBrighten(false);
}

void cruise_bubble::CruiseBubble::update_hud(F32 dt)
{
    if (hud.gizmos_used == 0)
    {
        return;
    }

    hud.alpha = range_limit<F32>(hud.alpha_vel * dt + hud.alpha, 0.0f, 1.0f);
    hud.glow = range_limit<F32>(hud.glow_vel * dt + hud.glow, 0.0f, 1.0f);

    F32 vel_frac = ((state_missle_fly*)shared.states[STATE_MISSLE_FLY])->vel /
                   current_tweak->missle.fly.max_vel;

    hud.uv_wind.offset_vel.assign(current_tweak->hud.wind.du, current_tweak->hud.wind.dv);
    hud.uv_wind.offset_vel *= vel_frac;
    hud.model.wind->Alpha = vel_frac;
    hud.uv_wind.update(dt);

    for (U32 i = 1; i < hud.gizmos_used; ++i)
    {
        if (!(hud.gizmo[i].flags & 0x1))
        {
            hud.gizmo[i].alpha_vel = -1.0f / current_tweak->hud.time_fade;
        }
    }

    U32 i = 0;
    while (i < hud.gizmos_used)
    {
        update_gizmo(hud.gizmo[i], dt);
        if (hud.gizmo[i].alpha <= 0.0f)
        {
            hud.gizmos_used--;
            hud.gizmo[i] = hud.gizmo[hud.gizmos_used];
        }
        else
        {
            ++i;
        }
    }

    for (U32 i = 1; i < hud.gizmos_used; ++i)
    {
        hud.gizmo[i].flags &= ~0x1;
    }
}

void cruise_bubble::CruiseBubble::render_hud()
{
    if (hud.gizmos_used == 0)
    {
        return;
    }

    zRenderState(SDRS_CruiseHUD);

    if (hud.model.wind->Alpha > 0.0f)
    {
        RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)rwBLENDSRCALPHA);
        RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDONE);

        basic_rect<F32> bound;
        bound.set_size(current_tweak->hud.wind.size, current_tweak->hud.wind.size);
        bound.center(0.5f, 0.5f);

        render_model_2d(hud.model.wind, bound, hud.model.wind->Alpha);
    }

    RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)rwBLENDSRCALPHA);
    RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDINVSRCALPHA);

    for (U32 i = 0; i < hud.gizmos_used; ++i)
    {
        hud_gizmo& gizmo = hud.gizmo[i];
        F32 alpha = hud.alpha * gizmo.alpha * gizmo.opacity;
        if (!(alpha <= 0.0f))
        {
            render_model_2d(gizmo.model, gizmo.bound, alpha);
        }
    }

    RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)rwBLENDSRCALPHA);
    RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDONE);

    for (U32 i = 0; i < hud.gizmos_used; ++i)
    {
        hud_gizmo& gizmo = hud.gizmo[i];

        F32 glow = gizmo.glow + hud.glow;
        if (glow > 1.0f)
        {
            glow = 1.0f;
        }
        else if (glow <= 0.0f)
        {
            continue;
        }

        F32 alpha = hud.alpha * gizmo.alpha * gizmo.opacity;
        if (!(alpha <= 0.0f))
        {
            render_glow(gizmo.model, gizmo.bound, glow, alpha);
        }
    }

#ifdef PLATFORM_PC
    // PC: the countdown goes to its corner of a screen that is not 4:3 with the HUD at the
    // screen's edges (iHudAnchor.h); the sights above stay on what they aim at.
    iHudAnchorScope anchor(current_tweak->hud.timer.x, current_tweak->hud.timer.y);
#endif
    render_timer(hud.alpha, hud.glow);
}

void cruise_bubble::CruiseBubble::lock_target(S32 index, const xVec3* target, F32 opacity)
{
    if (index <= -1 && hud.gizmos_used >= 33)
    {
        return;
    }

    if (index <= -1)
    {
        index = hud.gizmos_used++;
        show_gizmo(hud.gizmo[index], hud.gizmo[index].bound, hud.model.target);
    }

    hud_gizmo& gizmo = hud.gizmo[index];
    xVec3 screen_loc = world_to_screen(*target);

    gizmo.bound.set_size(current_tweak->hud.target.size);
    gizmo.bound.center(screen_loc.x, screen_loc.y);
    gizmo.flags = 0x1;
    gizmo.alpha_vel = 1.0f / current_tweak->hud.time_fade;
    gizmo.model = hud.model.target;
    gizmo.target = target;
    gizmo.opacity = opacity;
}

void cruise_bubble::CruiseBubble::check_lock_target(const xEnt* ent)
{
    const xVec3* target = &ent->bound.sph.center;
    xMat4x3& mat = globals.oldSkoolCamera.mat;
    xVec3 offset = *target - mat.pos;
    F32 ang = offset.dot(mat.at);

    if (ang < current_tweak->reticle.dist_min || ang > current_tweak->reticle.dist_max)
    {
        return;
    }

    ang = offset.length();
    if (ang >= -0.00001f && ang <= 0.00001f)
    {
        ang = 0.0f;
    }
    else
    {
        ang = xacos(offset.dot(mat.at) / ang);
    }

    F32 min_ang;
    F32 max_ang;
    if (zEntPlayer_PowerupLevel(PLYR_ATAK_SB_GUITAR) == ePowerupLevelNormal)
    {
        min_ang = current_tweak->reticle.ang_show;
        max_ang = current_tweak->reticle.ang_hide;
    }
    else if (zEntPlayer_PowerupLevel(PLYR_ATAK_SB_GUITAR) > ePowerupLevelNormal)
    {
        min_ang = current_tweak->reticle.ang_show_super;
        max_ang = current_tweak->reticle.ang_hide_super;
    }

    if (ang >= max_ang)
    {
        return;
    }

    state_camera_attach& attach = *(state_camera_attach*)shared.states[STATE_CAMERA_ATTACH];
    if (ang < attach.best_target && find_locked_target(target) >= 0)
    {
        attach.best_target = ang;
        attach.best_target_ent = (xEnt*)ent;
    }

    if (ang >= min_ang)
    {
        S32 index = find_locked_target(target);
        if (index >= 0)
        {
            lock_target(index, target, (max_ang - ang) / (max_ang - min_ang));
        }
    }
    else
    {
        lock_target(find_locked_target(target), target, 1.0f);
    }
}

U32 cruise_bubble::CruiseBubble::check_anim_aim(xAnimTransition*, xAnimSingle*, void*)
{
    return 0;
}

void cruise_bubble::CruiseBubble::load_settings()
{
    U16 params_size;
    xModelAssetParam* params =
        zEntGetModelParams(xStrHash("cruise_bubble_bind.MINF"), &params_size);

    if (params == NULL)
    {
        params_size = 0;
    }

    normal_tweak.load(params, params_size);
    memcpy(&cheat_tweak, &normal_tweak, sizeof(tweak_group));
    load_cheat_tweak();
}

void cruise_bubble::CruiseBubble::RenderOneRing(xVec3 pos, F32 radius, xColor_tag color,
                                                RxObjSpace3DVertex** vertList, S32 first)
{
    xVec3 h = xglobals->cam->mat.right;
    xVec3 w = xglobals->cam->mat.up;
    RxObjSpace3DVertex* vert = *vertList;

    h *= radius;
    w *= radius;

    if (!first)
    {
        vert += 2;
    }

    RwIm3DVertexSetPos(vert, pos.x - w.x - h.x, pos.y - w.y - h.y, pos.z - w.z - h.z);
    RwIm3DVertexSetUV(vert, 0.0f, 0.0f);
    RwIm3DVertexSetRGBA(vert, color.r, color.g, color.b, color.a);
    vert++;

    RwIm3DVertexSetPos(vert, pos.x - w.x + h.x, pos.y - w.y + h.y, pos.z - w.z + h.z);
    RwIm3DVertexSetUV(vert, 0.0f, 1.0f);
    RwIm3DVertexSetRGBA(vert, color.r, color.g, color.b, color.a);
    vert++;

    RwIm3DVertexSetPos(vert, pos.x + w.x - h.x, pos.y + w.y - h.y, pos.z + w.z - h.z);
    RwIm3DVertexSetUV(vert, 1.0f, 0.0f);
    RwIm3DVertexSetRGBA(vert, color.r, color.g, color.b, color.a);
    vert++;

    RwIm3DVertexSetPos(vert, pos.x + w.x + h.x, pos.y + w.y + h.y, pos.z + w.z + h.z);
    RwIm3DVertexSetUV(vert, 1.0f, 1.0f);
    RwIm3DVertexSetRGBA(vert, color.r, color.g, color.b, color.a);
    vert++;

    if (!first)
    {
        // Degenerate triangles joining this ring's strip to the previous one.
        RxObjSpace3DVertex* copyVert = *vertList;
        copyVert[0] = copyVert[-1];
        copyVert[1] = copyVert[2];
    }

    *vertList = vert;
}

void cruise_bubble::CruiseBubble::render_rings()
{
    RxObjSpace3DVertex stripVert[24];
    RxObjSpace3DVertex* currVert;
    xVec3 pos;
    F32 radius;
    xColor_tag color;

    if (!renderRing)
    {
        return;
    }

    currVert = stripVert;

    pos = ringPos;
    radius = 1.75f * ringRadius;

    RwRenderStateSet(rwRENDERSTATETEXTURERASTER, (void*)ringTex->raster);
    RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)rwBLENDSRCALPHA);
    RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDONE);
    RwRenderStateSet(rwRENDERSTATEZTESTENABLE, (void*)FALSE);
    RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)FALSE);

    color.r = 0xFF;
    color.g = 0;
    color.b = 0xFF;
    color.a = ringAlpha;
    RenderOneRing(pos, radius, color, &currVert, TRUE);

    color.r = 0;
    color.g = 0;
    color.b = 0xFF;
    color.a = ringAlpha;
    RenderOneRing(pos, radius / 2.0f, color, &currVert, FALSE);

    color.r = 0xFF;
    color.g = 0xFF;
    color.b = 0;
    color.a = ringAlpha;
    RenderOneRing(pos, radius / 4.0f, color, &currVert, FALSE);

    color.r = 0xFF;
    color.g = 0xFF;
    color.b = 0xFF;
    color.a = ringAlpha;
    RenderOneRing(pos, radius / 8.0f, color, &currVert, FALSE);

    if (RwIm3DTransform(stripVert, currVert - stripVert, NULL,
                        rwIM3D_VERTEXUV | rwIM3D_VERTEXXYZ | rwIM3D_VERTEXRGBA))
    {
        RwIm3DRenderPrimitive(rwPRIMTYPETRISTRIP);
        RwIm3DEnd();
    }

    RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)rwBLENDSRCALPHA);
    RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDINVSRCALPHA);
    RwRenderStateSet(rwRENDERSTATEZTESTENABLE, (void*)TRUE);
    RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)TRUE);
}

void cruise_bubble::xCamLaunch()
{
    xCam* defaultCam;

    globals.oldSkoolCamera.fov = (180.0f / PI) * xCamLaunchCam->fov;
    xCameraMove(&globals.oldSkoolCamera, xCamLaunchCam->mat.pos);
    xCameraRotate(&globals.oldSkoolCamera, xCamLaunchCam->mat, 0.0f, 0.0f, 0.0f);
    zCameraDisableTracking(CO_BOSSCRUISELAUNCH);

    defaultCam = zCamGetDefault();
    defaultCam->cfg_common.priority = 0x7e;
    xglobals->cam->priority_force_cut();
}

void cruise_bubble::xCamReturn()
{
}

void cruise_bubble::deinit()
{
#ifdef PLATFORM_PC
    // PLATFORM_PC: the Xbox's deinit (0x91ac7) ends the distortion too.
    distort_screen(0.0f);
#endif
    hud.model.wind = NULL;
    hud.model.reticle = NULL;
    hud.model.swirl = NULL;
    hud.model.target = NULL;
    renderRing = FALSE;
    shared.flags &= ~0x2;
    curSourceSimpObj = NULL;
    CBFXtra::cbfxdata.haz_speaker = NULL;
    CBFXtra::cbfxdata.haz_flight = NULL;
}

// No symbol in TSSM (always inlined into init); BfBB's init_sound.
static inline void init_sound()
{
    sound_config* s;
    sound_config* end = &sounds[8];
    for (s = &sounds[0]; s != end; ++s)
    {
        s->id = (s->name == NULL) ? 0 : xStrHash(s->name);
        s->handle = (iSndHandle)-1;
        if (s->rumble_name != NULL)
        {
            s->rumble_effect = zSceneFindObject(xStrHash(s->rumble_name));
        }
        else
        {
            s->rumble_effect = NULL;
        }
    }
}

// No symbol in TSSM (always inlined into init); BfBB's init_missle_model.
static inline void init_missle_model()
{
    xModelInstance* model =
        zEntRecurseModelInfo(xSTFindAsset(xStrHash("cruise_bubble_bind.MINF"), NULL), NULL);
    RpAtomicSetRenderCallBack(model->Data, custom_bubble_render);
    shared.missle_model = model;
}

// No symbol in TSSM (always inlined into init); BfBB's init_wake_ribbons.
static inline void init_wake_ribbons()
{
    wake_ribbon[0].create(0);
    wake_ribbon[1].create(0);
    reset_wake_ribbons();
}

// No symbol in TSSM (always inlined into init); BfBB's init_explode_decal.
static inline void init_explode_decal()
{
    explode_decal.init(1, "Cruise Bubble Explosion");
    explode_decal.set_default_config();
    explode_decal.cfg.flags = 0x3;
    explode_decal.cfg.life_time = 0.5f;
    explode_decal.cfg.blend_src = 5;
    explode_decal.cfg.blend_dst = 2;
    explode_decal.set_curve(explode_curve, 3);
    explode_decal.set_texture("par_cruise_explode");
    explode_decal.refresh_config();
}

// No symbol in TSSM (always inlined into launch); BfBB's reset_explode_decal.
static inline void reset_explode_decal()
{
    explode_decal.set_default_config();
    explode_decal.cfg.flags = 0x3;
    explode_decal.cfg.blend_src = 5;
    explode_decal.cfg.blend_dst = 2;

    if (!(shared.flags & 0x200))
    {
        explode_decal.cfg.life_time = 0.5f;
        explode_decal.set_curve(explode_curve, 3);
        explode_decal.set_texture("par_cruise_explode");
    }
    else
    {
        explode_decal.cfg.life_time = 0.5f;
        explode_decal.set_curve(cheat_explode_curve, 3);
        explode_decal.set_texture("par_cruise_explode");
    }

    explode_decal.refresh_config();
}

// No symbol in TSSM (always inlined into init); BfBB's init_shrapnel.
static inline void init_shrapnel()
{
    shared.droplet_shrapnel =
        (zShrapnelAsset*)xSTFindAsset(xStrHash("cruise_bubble_droplet_shrapnel"), NULL);
}

void cruise_bubble::init()
{
    base.id = 0;
    base.baseType = eBaseTypeCruiseBubble;
    base.linkCount = 0;
    base.baseFlags = 0;
    base.link = NULL;
    base.eventFunc = NULL;

    if ((shared.flags & 0x1) != 0x1)
    {
        return;
    }

    init_sound();

    shared.flags |= 0x2;

    load_settings();
    init_states();

    init_missle_model();
    init_wake_ribbons();
    init_explode_decal();
    init_shrapnel();
    init_hud();

    shared.fov_default = globals.oldSkoolCamera.fov;
    shared.dialog_freq = current_tweak->dialog.freq;

    zNMEParFX_PartyInvite(NPAR_TYP_SPARKS, NULL, 0);
    zNMEParFX_PartyInvite(NPAR_TYP_SPARKY, NULL, 0);
    zNMEParFX_PartyInvite(NPAR_TYP_COLORS, NULL, 0);
    zNMEParFX_PartyInvite(NPAR_TYP_CLOUDS, NULL, 0);
    zNMEParFX_PartyInvite(NPAR_TYP_EXHAUST, NULL, 0);
    zNMEParFX_PartyInvite(NPAR_TYP_HEATED, NULL, 0);
    zNMEParFX_PartyInvite(NPAR_TYP_NOTES, NULL, 0);
    zNMEParFX_PartyInvite(NPAR_TYP_NOTES_UPGRADE, NULL, 0);
    zNMEParFX_PartyInvite(NPAR_TYP_RKOWAVES, NULL, 0);
}

void cruise_bubble::launch()
{
    if ((shared.flags & 0x13) != 0x3)
    {
        return;
    }

    if (Cam_Launch != NULL)
    {
        Cam_Launch();
    }

    if (zGameExtras_CheatFlags() & 0x20000000)
    {
        shared.flags |= 0x200;
        current_tweak = &cheat_tweak;
    }
    else
    {
        current_tweak = &normal_tweak;
    }

    reset_wake_ribbons();
    reset_explode_decal();

    shared.flags |= 0x14;
    shared.last_sp = shared.sp = globals.pad0->analog[0].offset;
    shared.player_health = globals.player.s->Health;
    shared.player_motion = 0.0f;
    shared.fov_default = globals.oldSkoolCamera.fov;

    ztalkbox::permit(0, 0xffffffff);
    set_state(THREAD_PLAYER, BEGIN_STATE_PLAYER);

    if (zEntPlayer_PowerupLevel(PLYR_ATAK_SB_GUITAR) == ePowerupLevelNormal)
    {
        zEntPlayer_SNDPlayStream(ePlayerStreamSnd_SBSonicGuitar);
    }

    zEntPlayer_SNDStop(ePlayerSnd_Sneak);
}

namespace cruise_bubble
{
    namespace CruiseBubble
    {
        // No symbols in TSSM (always inlined into update); bodies from BfBB.
        static inline bool check_launch()
        {
            bool can_cruise_bubble =
                (!(globals.player.ControlOff || globals.player.cheat_mode) &&
                 globals.player.g.PowerUp[3] && globals.player.s != NULL &&
                 gCurrentPlayer == eCurrentPlayerSpongeBob && !globals.player.IsBubbleBowling &&
                 (globals.pad0->pressed & 0x1000));

            if (!can_cruise_bubble)
            {
                return false;
            }

            xAnimState* state =
                xglobals->___player_ent_dont_use_directly->model->Anim->Single->State;
            for (U32 i = 0; i < sizeof(start_anim_states) / sizeof(start_anim_states[0]); ++i)
            {
                if (stricmp(start_anim_states[i], state->Name) == 0)
                {
                    return true;
                }
            }

            return false;
        }

        static inline void refresh_controls()
        {
            shared.last_sp = shared.sp;
            shared.sp = globals.pad0->analog[0].offset;
        }

        static inline void update_state(xScene* s, F32 dt)
        {
            for (S32 i = THREAD_PLAYER; i < MAX_THREAD; ++i)
            {
                state_type** state = &shared.state[i];
                state_enum newtype;

                if (*state != NULL)
                {
                    newtype = (*state)->update(dt);

                    if (newtype != (*state)->type)
                    {
                        (*state)->stop();
                        *state = NULL;

                        if (newtype != STATE_INVALID)
                        {
                            *state = shared.states[newtype];
                            (*state)->start();
                        }
                    }
                }
            }

            if (shared.state[THREAD_PLAYER] == NULL)
            {
                kill(true, false);
            }
        }

        static inline void update_player(xScene& s, F32 dt)
        {
            xVec3 pre_update_loc = get_player_loc();
            xVec3 drive_motion;

            bool stop = zEntPlayer_MinimalUpdate(xglobals->___player_ent_dont_use_directly, &s, dt,
                                                 drive_motion) ||
                        globals.player.s->Health < shared.player_health;

            if (!stop)
            {
                shared.player_motion += get_player_loc() - pre_update_loc - drive_motion;

                if (shared.player_motion.length2() > 0.25f)
                {
                    stop = true;
                }
            }

            if (stop)
            {
                kill(true, false);
            }
        }

        static inline void update_missle(xScene& s, F32 dt)
        {
            xModelInstance* m = shared.missle_model;
            if (!(m->Flags & 0x2))
            {
                return;
            }

            xModelUpdate(m, dt);
            xModelEval(m);
            update_trail(dt);
        }
    } // namespace CruiseBubble
} // namespace cruise_bubble

U8 cruise_bubble::update(xScene* sc, F32 dt)
{
    if ((shared.flags & 0x3) != 0x3)
    {
        return false;
    }

    if (!(shared.flags & 0x10))
    {
        if (check_launch())
        {
            launch();
        }
        else
        {
            return false;
        }
    }

    if (globals.player.ControlOff)
    {
        kill(true, false);
        return false;
    }

    if (!((state_missle_fly*)shared.states[STATE_MISSLE_FLY])->boost)
    {
        refresh_controls();
    }

    update_state(sc, dt);

    if (!(shared.flags & 0x10))
    {
        return false;
    }

    update_player(*sc, dt);
    update_missle(*sc, dt);
    update_hud(dt);
    CBFXtra::SceneTimestep(dt);
    return true;
}

void cruise_bubble::reset()
{
    if ((shared.flags & 0x3) == 0x3)
    {
        kill(true, false);
        CBFXtra::cbfxdata.haz_speaker = NULL;
        CBFXtra::cbfxdata.haz_flight = NULL;
    }
}

// No symbol: inlined into zEntPlayer_Render.
bool cruise_bubble::render()
{
    if ((shared.flags & 0x7) != 0x7)
    {
        return false;
    }

    CruiseBubble::render_state();
    render_player();
    return true;
}

void cruise_bubble::render_screen()
{
    if ((shared.flags & 0x7) == 0x7 && !((state_missle_fly*)shared.states[STATE_MISSLE_FLY])->boost)
    {
        render_hud();
    }
}

void cruise_bubble::insert_player_animations(xAnimTable& table)
{
    shared.astate.player.aim =
        xAnimTableNewState(&table, "cruise_bubble_aim", 0x10, 0, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                           xAnimDefaultBeforeEnter, NULL, NULL);
    shared.astate.player.fire =
        xAnimTableNewState(&table, "cruise_bubble_fire", 0x20, 0, 1.0f, NULL, NULL, 0.0f, NULL,
                           NULL, xAnimDefaultBeforeEnter, NULL, NULL);
    shared.astate.player.fire2 =
        xAnimTableNewState(&table, "cruise_bubble_fire2", 0x20, 0, 1.0f, NULL, NULL, 0.0f, NULL,
                           NULL, xAnimDefaultBeforeEnter, NULL, NULL);
    shared.astate.player.idle =
        xAnimTableNewState(&table, "cruise_bubble_idle", 0x10, 0, 1.0f, NULL, NULL, 0.0f, NULL,
                           NULL, xAnimDefaultBeforeEnter, NULL, NULL);

    char* start_from = (char*)RwMalloc(0x110, 0);
    memset(start_from, 0, 0x110);

    char* s = start_from;
    *s = '\0';
    for (U32 i = 0; i < sizeof(start_anim_states) / sizeof(start_anim_states[0]); ++i)
    {
        strcat(s, start_anim_states[i]);
        s += strlen(s);
        *s = ' ';
        *++s = '\0';
    }

    shared.atran.player.aim =
        xAnimTableNewTransition(&table, start_from, "cruise_bubble_aim", check_anim_aim, NULL, 0, 0,
                                0.0f, 0.0f, 0, 0, 0.15f, NULL);
    shared.atran.player.fire =
        xAnimTableNewTransition(&table, "cruise_bubble_aim", "cruise_bubble_fire", NULL, NULL, 0, 0,
                                0.0f, 0.0f, 0, 0, 0.15f, NULL);
    shared.atran.player.fire2 =
        xAnimTableNewTransition(&table, "cruise_bubble_aim", "cruise_bubble_fire2", NULL, NULL, 0,
                                0, 0.0f, 0.0f, 0, 0, 0.15f, NULL);
    shared.atran.player.idle =
        xAnimTableNewTransition(&table, "cruise_bubble_fire", "cruise_bubble_idle", NULL, NULL,
                                0x10, 0, 0.0f, 0.0f, 0, 0, 0.15f, NULL);
    // The fire2 -> idle transition overwrites the fire -> idle one (as in retail).
    shared.atran.player.idle =
        xAnimTableNewTransition(&table, "cruise_bubble_fire2", "cruise_bubble_idle", NULL, NULL,
                                0x10, 0, 0.0f, 0.0f, 0, 0, 0.15f, NULL);
    shared.atran.player.end = xAnimTableNewTransition(
        &table, "cruise_bubble_aim cruise_bubble_fire cruise_bubble_fire2 cruise_bubble_idle",
        "Idle01", NULL, NULL, 0, 0, 0.0f, 0.0f, 0, 0, 0.15f, NULL);

    RwFree(start_from);
}

xAnimTable* cruise_bubble::anim_table()
{
    xAnimTable& table = *xAnimTableNew("Cruise Bubble", 0);

    shared.astate.missle.fire = xAnimTableNewState(&table, "fire", 0x20, 0, 1.0f, NULL, NULL, 0.0f,
                                                   NULL, NULL, xAnimDefaultBeforeEnter, NULL, NULL);
    shared.astate.missle.fly = xAnimTableNewState(&table, "fly", 0x10, 0, 1.0f, NULL, NULL, 0.0f,
                                                  NULL, NULL, xAnimDefaultBeforeEnter, NULL, NULL);
    shared.atran.missle.fly = xAnimTableNewTransition(&table, "fire", "fly", NULL, NULL, 0x10, 0,
                                                      0.0f, 0.0f, 0, 0, 0.15f, NULL);

    return &table;
}

U8 cruise_bubble::active()
{
    return (shared.flags & 0x10) != 0;
}

F32 cruise_bubble::flying()
{
    state_missle_fly* state = (state_missle_fly*)shared.state[THREAD_MISSLE];
    if (state == NULL || state->type != STATE_MISSLE_FLY)
    {
        return 0.0f;
    }

    return state->life;
}

xVec3& cruise_bubble::location()
{
    return ((xMat4x3*)shared.missle_model->Mat)->pos;
}

// No GC symbol: inlined into event_handler (and with it into zEntPlayerEventCB); the out-of-line
// copy is dead-stripped.
void cruise_bubble::add_life(F32 life, F32 max)
{
    state_missle_fly* state = (state_missle_fly*)shared.state[THREAD_MISSLE];
    if (state == NULL || state->type != STATE_MISSLE_FLY)
    {
        return;
    }

    state->life += life;

    if (max < 0.0f)
    {
        max = current_tweak->missle.life;
    }

    if (max > 0.0f && state->life > max)
    {
        state->life = max;
    }
}

// No GC symbol: inlined into event_handler (and with it into zEntPlayerEventCB); the out-of-line
// copy is dead-stripped.
void cruise_bubble::set_life(F32 life)
{
    state_missle_fly* state = (state_missle_fly*)shared.state[THREAD_MISSLE];
    if (state == NULL || state->type != STATE_MISSLE_FLY)
    {
        return;
    }

    state->life = life;
}

void cruise_bubble::reset_life()
{
    state_missle_fly* state = (state_missle_fly*)shared.state[THREAD_MISSLE];
    if (state == NULL || state->type != STATE_MISSLE_FLY)
    {
        return;
    }

    state->life = current_tweak->missle.life;
}

// No GC symbol: inlined into zEntPlayerEventCB, its only caller; the out-of-line copy is
// dead-stripped.
bool cruise_bubble::event_handler(xBase* from, U32 event, const F32* fparam, xBase* to)
{
    switch (event)
    {
    case eEventCruiseAddLife:
        add_life(fparam[0], fparam[1]);
        return true;

    case eEventCruiseSetLife:
        set_life(fparam[0]);
        return true;

    case eEventCruiseResetLife:
        reset_life();
        return true;

    case eEventCruiseFired:
    case eEventCruiseDied:
        return true;
    }

    return false;
}

xMat4x3* cruise_bubble::GetMissileMat()
{
    return (xMat4x3*)shared.missle_model->Mat;
}

U8 cruise_bubble::CheckAbilityToLaunch()
{
    U8 canLaunch = FALSE;

    if (!globals.player.ControlOff && !globals.player.cheat_mode && globals.player.g.PowerUp[3] &&
        globals.player.s != NULL && gCurrentPlayer == eCurrentPlayerSpongeBob &&
        !globals.player.IsBubbleBowling)
    {
        canLaunch = TRUE;
    }

    return canLaunch;
}

S32 cruise_bubble::CruiseBubble::CBFXtra::HazGone::Notify(en_haznote note, zNMEHazard* haz)
{
    if (note == HAZ_NOTE_DISCARD)
    {
        if (haz == cbfxdata.haz_speaker)
        {
            cbfxdata.haz_speaker = NULL;
        }

        if (haz == cbfxdata.haz_flight)
        {
            cbfxdata.haz_flight = NULL;
        }
    }

    return 0;
}

void cruise_bubble::CruiseBubble::CBFXtra::SceneTimestep(F32 dt)
{
    RockStar_Update(dt);
}

void cruise_bubble::CruiseBubble::CBFXtra::RockStar_Action(en_tunefx tunefx, en_tunetran tran)
{
    switch (tunefx)
    {
    case TUNE_BANG_LEFT:
        Bang_Left();
        break;
    case TUNE_BANG_RYHT:
        Bang_Right();
        break;
    case TUNE_SPEAKER:
        if (tran == TUNE_TRAN_ENTER)
        {
            Speak_Start();
        }
        else if (tran == TUNE_TRAN_EXIT)
        {
            Speak_Stop();
        }
        break;
    case TUNE_FLIGHT:
        Flight_Start();
        break;
    case TUNE_EXPLODE:
        Flight_Stop();
        Boom_Start();
        break;
    }
}

void cruise_bubble::CruiseBubble::CBFXtra::RockStar_Update(F32 dt)
{
    if (cbfxdata.haz_speaker != NULL)
    {
        Speak_Update(dt);
    }

    if (cbfxdata.haz_flight != NULL)
    {
        Flight_Update(dt);
    }
}

namespace cruise_bubble
{
    namespace CruiseBubble
    {
        namespace CBFXtra
        {
            namespace BangLeft
            {
                static const xVec3 vec_offset = { 0.5f, 0.0f, -0.25f };
                static const xVec3 vec_tilt = { 1.0f, 2.0f, 1.0f };
            } // namespace BangLeft
        } // namespace CBFXtra
    } // namespace CruiseBubble
} // namespace cruise_bubble

void cruise_bubble::CruiseBubble::CBFXtra::Bang_Left()
{
    zNMEHazard* haz = HAZ_Acquire();
    if (haz == NULL)
    {
        return;
    }

    S32 rc = haz->ConfigHelper(HAZ_TYP_TUNEBANG);
    if (!rc)
    {
        return;
    }

    haz->npc_owner = NULL;

    xMat4x3 mat_plyr = *(xMat4x3*)xglobals->___player_ent_dont_use_directly->model->Mat;

    xVec3 pos_launch = *xEntGetPos(xglobals->___player_ent_dont_use_directly);
    pos_launch += mat_plyr.right * BangLeft::vec_offset.x;
    pos_launch += mat_plyr.up * BangLeft::vec_offset.y;
    pos_launch += mat_plyr.at * BangLeft::vec_offset.z;

    xVec3 vec_orient = g_Y3;
    if (BangLeft::vec_tilt.length2() > 0.00001f)
    {
        vec_orient = mat_plyr.right * BangLeft::vec_tilt.x;
        vec_orient += mat_plyr.up * BangLeft::vec_tilt.y;
        vec_orient += mat_plyr.at * BangLeft::vec_tilt.z;
    }

    haz->OrientToDir(&vec_orient, 0);
    haz->Start(&pos_launch, -1.0f);
}

namespace cruise_bubble
{
    namespace CruiseBubble
    {
        namespace CBFXtra
        {
            namespace BangRight
            {
                static const xVec3 vec_offset = { -0.5f, 0.0f, -0.5f };
                static const xVec3 vec_tilt = { -1.0f, 2.0f, 1.0f };
            } // namespace BangRight
        } // namespace CBFXtra
    } // namespace CruiseBubble
} // namespace cruise_bubble

void cruise_bubble::CruiseBubble::CBFXtra::Bang_Right()
{
    zNMEHazard* haz = HAZ_Acquire();
    if (haz == NULL)
    {
        return;
    }

    S32 rc = haz->ConfigHelper(HAZ_TYP_TUNEBANG);
    if (!rc)
    {
        return;
    }

    haz->npc_owner = NULL;

    xMat4x3 mat_plyr = *(xMat4x3*)xglobals->___player_ent_dont_use_directly->model->Mat;

    xVec3 pos_launch = *xEntGetPos(xglobals->___player_ent_dont_use_directly);
    pos_launch += mat_plyr.right * BangRight::vec_offset.x;
    pos_launch += mat_plyr.up * BangRight::vec_offset.y;
    pos_launch += mat_plyr.at * BangRight::vec_offset.z;

    xVec3 vec_orient = g_Y3;
    if (BangRight::vec_tilt.length2() > 0.00001f)
    {
        vec_orient = mat_plyr.right * BangRight::vec_tilt.x;
        vec_orient += mat_plyr.up * BangRight::vec_tilt.y;
        vec_orient += mat_plyr.at * BangRight::vec_tilt.z;
    }

    haz->OrientToDir(&vec_orient, 0);
    haz->Start(&pos_launch, -1.0f);
}

namespace cruise_bubble
{
    namespace CruiseBubble
    {
        namespace CBFXtra
        {
            namespace Speaker
            {
                static const xVec3 vec_offset = { 0.0f, 0.5f, 0.25f };
                static const xVec3 vec_tilt = { 0.0f, 0.5f, 1.0f };
            } // namespace Speaker
        } // namespace CBFXtra
    } // namespace CruiseBubble
} // namespace cruise_bubble

void cruise_bubble::CruiseBubble::CBFXtra::Speak_Update(F32 dt)
{
    if (cbfxdata.haz_speaker == NULL)
    {
        return;
    }

    if (curSourceSimpObj == NULL)
    {
        return;
    }

    xMat4x3 mat_speak = *(xMat4x3*)curSourceSimpObj->model->Mat;

    xVec3 pos_launch = mat_speak.pos;
    pos_launch += mat_speak.right * Speaker::vec_offset.x;
    pos_launch += mat_speak.up * Speaker::vec_offset.y;
    pos_launch += mat_speak.at * Speaker::vec_offset.z;

    xVec3 vec_orient = mat_speak.at;
    if (Speaker::vec_tilt.length() > 0.00001f)
    {
        vec_orient = mat_speak.right * Speaker::vec_tilt.x;
        vec_orient += mat_speak.up * Speaker::vec_tilt.y;
        vec_orient += mat_speak.at * Speaker::vec_tilt.z;
    }

    cbfxdata.haz_speaker->OrientToDir(&vec_orient, 0);
    cbfxdata.haz_speaker->LifespanSet(0.2f, 1.0f);
}

// namespace cruise_bubble

void zEntCruiseBubbleRenderFX()
{
    render_rings();
}
