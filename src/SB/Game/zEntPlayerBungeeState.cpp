#include "zEntPlayerBungeeState.h"

#include <types.h>
#include <string.h>

#include "xAnim.h"
#include "xBase.h"
#include "xCamera.h"
#include "xCollide.h"
#include "xDebug.h"
#include "xDebugTweak.h"
#include "xEnt.h"
#include "xEnv.h"
#include "xFX.h"
#include "xIni.h"
#include "xMarkerAsset.h"
#include "xMath.h"
#include "xMath2.h"
#include "xMathInlines.h"
#include "xModel.h"
#include "xScene.h"
#include "xString.h"
#include "xVec3.h"
#include "xVec3Inlines.h"
#include "xstransvc.h"
#include "iMath.h"
#include "iScrFX.h"
#include "zBase.h"
#include "zCamera.h"
#include "zEntButton.h"
#include "zEntPlayer.h"
#include "zGlobals.h"
#include "zScene.h"
#include "zSurface.h"
#include "zTextBox.h"

namespace bungee_state
{
    U32 helpHash;
    U32 helpNoDiveHash;
    U32 helpNoQuitHash;
    U32 helpNoQuitNoDiveHash;
    ztextbox* helpTextBox;
    F32 oldShadowCastDistance;

    namespace BungeeState
    {
        struct sound_group
        {
            S32 first;
            bool streamed;
        };

        static const sound_group sound_groups[9] = {
            { ePlayerSnd_BungeeAttach, false },
            { ePlayerStreamSnd_BungeeAttachComment, true },
            { ePlayerSnd_BungeeRelease, false },
            { ePlayerSnd_BungeeDive, false },
            { ePlayerSnd_Invalid, false },
            { ePlayerSnd_BungeeWind, false },
            { ePlayerSnd_Ouch, false },
            { ePlayerSnd_Ouch, false },
            { ePlayerStreamSnd_BungeeDeath, true },
        };

        enum sound_enum
        {
            SOUND_INVALID = -1,
            BEGIN_SOUND = 0,
            SOUND_ATTACH = 0,
            SOUND_ATTACH_COMMENT = 1,
            SOUND_DETACH = 2,
            SOUND_DIVE = 3,
            SOUND_PEAK = 4,
            SOUND_WIND_LOOP = 5,
            SOUND_THUMP = 6,
            SOUND_DAMAGE = 7,
            SOUND_DEATH = 8,
            END_SOUND = 9,
            MAX_SOUND = 9,
        };

        static inline void play_sound(S32 which)
        {
            const sound_group& sg = sound_groups[which];
            if (sg.streamed)
            {
                zEntPlayer_SNDPlayStream((_tagePlayerStreamSnd)sg.first);
            }
            else
            {
                zEntPlayer_SNDPlay((_tagePlayerSnd)sg.first, true);
            }
        }

        static inline void stop_sound(S32 which)
        {
            const sound_group& sg = sound_groups[which];
            if (sg.streamed)
            {
                return;
            }

            zEntPlayer_SNDStop((_tagePlayerSnd)sg.first);
        }

        static inline void set_volume(S32 which, F32 new_vol)
        {
            const sound_group& sg = sound_groups[which];
            if (sg.streamed)
            {
                return;
            }

            zEntPlayer_SNDSetVol((_tagePlayerSnd)sg.first, new_vol);
        }

        // The d-pad overrides the analog stick. PS2 indexes the analog pair by controller.
        static inline xVec2 stick_position(U32 controller)
        {
            xVec2 v;

            if (globals.pad0->on & XPAD_BUTTON_LEFT)
            {
                v.x = -1.0f;
            }
            else
            {
                v.x = (globals.pad0->on & XPAD_BUTTON_RIGHT) ? 1.0f : 0.0f;
            }

            if (globals.pad0->on & XPAD_BUTTON_UP)
            {
                v.y = -1.0f;
            }
            else
            {
                v.y = (globals.pad0->on & XPAD_BUTTON_DOWN) ? 1.0f : 0.0f;
            }

            if (globals.pad0->on &
                (XPAD_BUTTON_UP | XPAD_BUTTON_RIGHT | XPAD_BUTTON_DOWN | XPAD_BUTTON_LEFT))
            {
                return v;
            }

            _tagPadAnalog& a = (&globals.pad0->analog1)[controller];
            v.x = normalize_analog(a.x, -128, 127, 0, -40, 40);
            v.y = normalize_analog(a.y, -128, 127, 0, -40, 40);
            return v;
        }
    } // namespace BungeeState

    using namespace BungeeState;

    namespace
    {
        static struct
        {
            F32 bottom_anim_frac;
            F32 top_anim_frac;
            F32 bottom_anim_time;
            F32 top_anim_time;
            F32 hit_anim_time;
            F32 damage_rot;
            F32 death_time;
            F32 vel_blur;
            F32 fade_dist;
            struct
            {
                F32 edge_zone;
                F32 sway;
                F32 decay;
            } horizontal;
            struct
            {
                F32 time;
                F32 anim_out_time;
                F32 min_dist;
                F32 max_dist;
            } dive;
        } fixed;

        class state_type;

        static struct
        {
            S32 flags;
            state_type* state;
            state_type* states[2];
            struct
            {
                xAnimTransition* start;
                xAnimTransition* rise;
                xAnimTransition* fall;
                xAnimTransition* stop;
                xAnimTransition* dive_start;
                xAnimTransition* dive_stop;
                xAnimTransition* top_start;
                xAnimTransition* top_stop;
                xAnimTransition* bottom_start;
                xAnimTransition* bottom_stop;
                xAnimTransition* hit;
            } anim_tran;
            hook_type* hook;
            hook_type* hook_cache[8];
            const drop_asset* drop_cache[32];
            xMarkerAsset* drop_marker_cache[32];
            S32 hook_cache_size;
            S32 drop_cache_size;
            xModelInstance* root_model;
            xModelInstance* ass_model;
            xModelInstance* pants_model;
            xVec3 hook_loc;
            xVec3 drop_loc;
            bool drop_set_view_angle;
            F32 drop_view_angle;
            F32 away_timer;
            S32 anim_state;
        } shared = { 1 };

        static F32 old_pants_clip_radius;

        // A plain static, not an inline: retail auto-inlines it into hanging_state_type::start but
        // pools its 500.0f at this position.
        static void show_models()
        {
            shared.ass_model->Flags |= 0x3;
            shared.pants_model->Flags &= 0xfff7;
            shared.pants_model->Flags |= 0x22;

            if (globals.player.sb_models[5] != NULL)
            {
                globals.player.sb_models[5]->Flags &= ~0x1;
            }

            if (globals.player.sb_models[6] != NULL)
            {
                globals.player.sb_models[6]->Flags &= ~0x1;
            }

            old_pants_clip_radius = shared.pants_model->Data->boundingSphere.radius;
            shared.pants_model->Data->boundingSphere.radius = 500.0f;
        }

        static inline void hide_models()
        {
            shared.ass_model->Flags &= ~0x3;
            shared.pants_model->Flags &= 0xffdc;
            shared.pants_model->Flags |= 0x8;
            shared.pants_model->Data->boundingSphere.radius = old_pants_clip_radius;
        }

        static inline void trigger(U32 toEvent)
        {
            zEntEvent(shared.hook, shared.hook, toEvent);
        }

        static inline void render_player(bool fade)
        {
            xEntRender(xglobals->___player_ent_dont_use_directly);

            if (fade)
            {
                xFXRenderProximityFade(*shared.pants_model, 1.0f, fixed.fade_dist);
            }
        }

        static inline void init_models()
        {
            shared.root_model = xglobals->___player_ent_dont_use_directly->model;
            shared.ass_model = globals.player.sb_models[1];
            shared.pants_model = globals.player.sb_models[2];
            xModelInstanceUpgradeBrotherShared(shared.pants_model,
                                               (shared.pants_model->Flags & 0xdfff) | 0x8);
        }

        // No GC symbol (inlined into attaching_state_type::start and dead-stripped); the name is a
        // guess. A plain function, so its strings are pooled with this file's code as in retail.
        static void init_help()
        {
            helpHash = xStrHash("text_bungee_help");
            helpNoDiveHash = xStrHash("text_bungee_help_nodive");
            helpNoQuitHash = xStrHash("text_bungee_help_noquit");
            helpNoQuitNoDiveHash = xStrHash("text_bungee_help_noquit_nodive");
            helpTextBox = (ztextbox*)zSceneFindObject(xStrHash("TEXTBOX_BUNGEE_HELP"));
        }

        static inline void update_hook_loc()
        {
            if (shared.hook == NULL || shared.hook->ent == NULL)
            {
                return;
            }

            shared.hook_loc = *xEntGetPos(shared.hook->ent) + shared.hook->asset->center;
        }

        // The separate 'near_dist2 *= near_dist2;' gives retail's in-place square.
        static inline bool find_drop_off()
        {
            S32 near_index = -1;
            F32 near_dist2 = shared.hook->asset->detach.dist;
            near_dist2 *= near_dist2;

            for (S32 i = 0; i < shared.drop_cache_size; ++i)
            {
                xVec3 dloc = shared.drop_marker_cache[i]->pos - shared.hook_loc;
                if (dloc.length2() >= near_dist2)
                {
                    continue;
                }

                near_index = i;
                near_dist2 = dloc.length2();
            }

            if (near_index != -1)
            {
                shared.drop_loc = shared.drop_marker_cache[near_index]->pos;
                shared.drop_set_view_angle = shared.drop_cache[near_index]->set_view_angle;
                shared.drop_view_angle = shared.drop_cache[near_index]->view_angle;
                return true;
            }

            return false;
        }

        void move_wedgie(const xVec3& stretch_loc);

        enum state_enum
        {
            STATE_INVALID = -1,
            BEGIN_STATE = 0,
            STATE_ATTACHING = 0,
            STATE_HANGING = 1,
            END_STATE = 2,
            MAX_STATE = 2,
        };

        class state_type
        {
        public:
            state_type(state_enum type) : type(type)
            {
            }

            state_enum type;

            virtual void start()
            {
            }

            virtual void stop()
            {
            }

            virtual state_enum update(xScene& s, F32& dt) = 0;

            virtual void render()
            {
            }
        };

        class attaching_state_type : public state_type
        {
        public:
            attaching_state_type() : state_type(STATE_ATTACHING)
            {
            }

            xVec3* loc;
            xVec3* vel;
            xVec3 last_hook_loc;
            xVec3 hook_vel;
            F32 time_left;
            F32 time;
            F32 end_time;
            xVec3 player_loc;
            xVec3 player_vel;

            // Retail's attaching/hanging vtables are weak: every virtual is defined in the class
            // body, so there is no key function and the bodies follow the file's non-inline code.
            void start()
            {
                loc = &xglobals->___player_ent_dont_use_directly->frame->mat.pos;
                vel = &xglobals->___player_ent_dont_use_directly->frame->vel;

                init_models();

                init_help();

                *loc = ((xMat4x3*)shared.root_model->Mat)->pos;

                update_hook_loc();

                last_hook_loc = shared.hook_loc;
                hook_vel = 0.0f;

                F32 travel_time = shared.hook->asset->attach.travel_time;
                time_left = travel_time;
                *vel = (shared.hook_loc - *loc) * (1.0f / travel_time);

                xAnimPlayStartTransition(xglobals->___player_ent_dont_use_directly->model->Anim,
                                         shared.anim_tran.start);

                oldShadowCastDistance =
                    xglobals->___player_ent_dont_use_directly->entShadow->dst_cast;
                xglobals->___player_ent_dont_use_directly->entShadow->dst_cast = 100.0f;

                trigger(eEventMount);
            }

            void stop()
            {
                *loc = shared.hook_loc;
                *vel = 0.0f;
            }

            state_enum update(xScene&, F32& dt)
            {
                update_hook_loc();

                time += dt;
                if (time >= end_time)
                {
                    dt = time - end_time;
                    return STATE_HANGING;
                }

                if (time < end_time)
                {
                    F32 g = -globals.player.g.Gravity;

                    player_loc.x += player_vel.x * dt;
                    player_loc.y += player_vel.y * dt + dt * (0.5f * g * dt);
                    player_loc.z += player_vel.z * dt;
                    player_vel.y += g * dt;

                    *loc = player_loc + (shared.hook_loc - player_loc) * xSCurve(time / end_time);
                    *vel = 0.0f;
                }
                else
                {
                    dt = time - end_time;
                }

                ((xMat4x3*)shared.root_model->Mat)->pos = *loc;

                xModelUpdate(xglobals->___player_ent_dont_use_directly->model, dt);
                xModelEval(xglobals->___player_ent_dont_use_directly->model);

                if (time < end_time)
                {
                    return STATE_ATTACHING;
                }

                dt = time - end_time;
                return STATE_HANGING;
            }

            void render()
            {
                render_player(false);
            }
        };

        class hanging_state_type : public state_type
        {
        public:
            enum ent_type
            {
                ENT_INVALID = -1,
                ENT_NPC = 0,
                ENT_DYN = 1,
                ENT_STAT = 2,
            };

            struct ent_info
            {
                ent_type type;
                xEnt* ent;
                S32 hits;
            };

            struct env_info
            {
                xEnv* env;
                bool collide;
                S32 hits;
            };

            hanging_state_type() : state_type(STATE_HANGING)
            {
            }

            xVec3 loc;
            xVec3 vel;
            xVec3 last_loc;
            xVec3 last_hook_loc;
            xVec3 cam_loc;
            xVec3 cam_vel;
            xVec3 cam_dir;
            xVec3 cam_dir_vel;
            F32 dive_remaining;
            F32 rot;
            F32 rot_vel;
            xVec2 stick_loc;
            F32 stick_ang;
            F32 stick_mag;
            F32 stick_frac;
            xVec3 collide_accel;
            F32 roll_offset;
            bool detaching;
            xVec3 drop_off_vel;
            F32 max_yvel;
            bool dying;
            F32 damage_timer;
            F32 control_lag_timer;
            F32 control_lag_max;
            bool has_dived;
            bool can_dive;
            U32 last_health;
            xModelInstance* root_model;
            xModelInstance* ass_model;
            xModelInstance* pants_model;
            hook_asset h;
            struct
            {
                struct
                {
                    F32 rest_dist;
                    F32 emax;
                    F32 spring;
                    F32 alpha;
                    F32 omega;
                } vertical;
                struct
                {
                    F32 vscale;
                    F32 hscale;
                    F32 roll_decay;
                } camera;
            } eh;
            ent_info ent_cache[256];
            S32 ent_cache_size;
            env_info env_cache;

            void start()
            {
                show_help();
                has_dived = false;
                allow_dive(false);

                last_health = globals.player.s->Health;
                dying = false;
                damage_timer = 0.0f;
                control_lag_timer = 0.0f;
                control_lag_max = 1.0f;
                detaching = false;

                show_models();
                update_hook_loc();

                shared.flags = (shared.flags | 0x8) & ~0x10;
                zCameraDisableTracking(CO_BUNGEE);
                xCameraDoCollisions(0, CO_BUNGEE);
                shared.anim_state = 0;

                h = *shared.hook->asset;

                update_hook_loc();
                last_hook_loc = shared.hook_loc;

                reset_props_turn();
                reset_props_vertical();
                h.detach.accel *= -h.vertical.gravity;
                reset_props_camera();

                loc = world_to_local(xglobals->___player_ent_dont_use_directly->frame->mat.pos);
                last_loc = loc;
                vel = xglobals->___player_ent_dont_use_directly->frame->vel;
                dive_remaining = 0.0f;

                init_camera();

                xVec3 eu;
                xMat3x3GetEuler((const xMat3x3*)shared.root_model->Mat, &eu);
                rot = eu.x;
                rot_vel = 0.0f;

                reset_props_collision();

                xAnimPlayStartTransition(xglobals->___player_ent_dont_use_directly->model->Anim,
                                         shared.anim_tran.start);

                play_sound(SOUND_ATTACH);
                play_sound(SOUND_WIND_LOOP);

                // The debug tweak registration (xDebugAddTweak) is compiled out of retail; only its
                // callback tables remain.
                static const tweak_callback turn_cb = { (void (*)(tweak_info&))on_tweak_turn };
                static const tweak_callback vertical_cb = { (
                    void (*)(tweak_info&))on_tweak_vertical };
                static const tweak_callback horizontal_cb = { (
                    void (*)(tweak_info&))on_tweak_horizontal };
                static const tweak_callback camera_cb = { (void (*)(tweak_info&))on_tweak_camera };
                static const tweak_callback collision_cb = { (
                    void (*)(tweak_info&))on_tweak_collision };
            }

            void stop()
            {
                zCameraEnableTracking(CO_BUNGEE);
                xglobals->___player_ent_dont_use_directly->entShadow->dst_cast =
                    oldShadowCastDistance;
                iCameraSetBlurriness(0.0f);
                hide_help();

                shared.flags &= ~0x8;
                xCameraDoCollisions(1, CO_BUNGEE);
                shared.away_timer = shared.hook->asset->detach.free_fall_time;

                hide_models();

                if (!dying)
                {
                    xMat4x3& mm = *(xMat4x3*)shared.root_model->Mat;
                    xglobals->___player_ent_dont_use_directly->frame->mat.pos = mm.pos =
                        shared.hook_loc;
                    xglobals->___player_ent_dont_use_directly->frame->vel = drop_off_vel;
                }

                xAnimPlayStartTransition(xglobals->___player_ent_dont_use_directly->model->Anim,
                                         shared.anim_tran.stop);

                stop_sound(SOUND_WIND_LOOP);

                if (!dying)
                {
                    zCameraEnableTracking(CO_BUNGEE);
                }

                trigger(eEventDismount);

                xEnt* ent = shared.hook->ent;
                if (ent != NULL)
                {
                    ent->chkby &= 0xef;
                    ent->penby &= 0xef;
                }
            }

            state_enum update(xScene& s, F32& dt)
            {
                if (globals.pad0->pressed & XPAD_BUTTON_TRIANGLE && !dying && !detaching &&
                    find_drop_off())
                {
                    start_detaching();
                }

                if (detaching)
                {
                    return detach_update(s, dt);
                }

                globals.player.DamageTimer -= dt;
                control_lag_timer -= dt;

                last_hook_loc = shared.hook_loc;
                update_hook_loc();

                stick_frac = 1.0f;
                if (control_lag_timer > 0.0f)
                {
                    if (control_lag_max <= 0.0f)
                    {
                        stick_frac = 0.1f;
                    }
                    else
                    {
                        stick_frac = (control_lag_max - control_lag_timer) / control_lag_max;
                    }
                }

                stick_loc = stick_position(0);
                stick_mag =
                    stick_frac * xsqrt(stick_loc.x * stick_loc.x + stick_loc.y * stick_loc.y);
                stick_ang = xatan2(stick_loc.y, stick_loc.x) - h.camera.view_angle;
                stick_loc.y = stick_mag * isin(stick_ang);
                stick_loc.x = stick_mag * icos(stick_ang);

                last_loc = loc;

                update_movement(dt);
                update_animation(dt);
                update_camera(dt);
                update_collision(s, dt);
                check_damage(false);

                xglobals->___player_ent_dont_use_directly->bound.sph = player_bound();

                update_sound(dt);
                update_blur(dt);

                xQuickCullForBound(&xglobals->___player_ent_dont_use_directly->bound.qcd,
                                   &xglobals->___player_ent_dont_use_directly->bound);
                zEntPickup_CheckAllPickupsAgainstPlayer(&s, dt);

                if (dying)
                {
                    return STATE_INVALID;
                }

                show_help();
                return STATE_HANGING;
            }

            void render()
            {
                render_player(true);
            }

            void calc_drop_off_velocity(xVec3& v, const xVec3& from, const xVec3& to, F32 g, F32 t)
            {
                v = (to - from) * (1.0f / t);
                v.y = 0.5f * g * t + v.y;
            }

            void start_detaching()
            {
                detaching = true;
                calc_drop_off_velocity(drop_off_vel, shared.hook_loc, shared.drop_loc,
                                       globals.player.g.Gravity,
                                       shared.hook->asset->detach.free_fall_time);
            }

            void show_help()
            {
                if (helpTextBox == NULL || helpTextBox->baseType != eBaseTypeTextBox)
                {
                    return;
                }

                bool can_quit = find_drop_off();
                if (!can_quit)
                {
                    helpTextBox->set_text(can_dive ? helpNoQuitHash : helpNoQuitNoDiveHash);
                }
                else
                {
                    helpTextBox->set_text(can_dive ? helpHash : helpNoDiveHash);
                }

                helpTextBox->activate(false);
            }

            void hide_help()
            {
                if (helpTextBox == NULL || helpTextBox->baseType != eBaseTypeTextBox)
                {
                    return;
                }

                helpTextBox->deactivate();
            }

            static void on_tweak_turn(const tweak_info& ti);
            void reset_props_turn()
            {
                if (shared.hook == NULL)
                {
                    return;
                }

                h.turn = shared.hook->asset->turn;
            }

            static void on_tweak_vertical(const tweak_info& ti);
            void reset_props_vertical();

            // ek/vout: local names from the BfBB PS2 DWARF of spring_velocity.
            F32 spring_velocity(F32 x, F32 v, F32 e, F32 k, F32 g, F32 xc) const
            {
                F32 ek = e - spring_potential_energy(x, k, g, xc);
                if (ek <= 0.0f)
                {
                    return 0.0f;
                }

                F32 vout = xsqrt(2.0f * ek);

                if (v < 0.0f)
                {
                    if (vout > 0.0f)
                    {
                        vout = -vout;
                    }
                }
                else if (vout < 0.0f)
                {
                    vout = -vout;
                }

                return vout;
            }

            F32 spring_energy(F32 x, F32 v, F32 k, F32 g, F32 xc) const
            {
                return kinetic_energy(v) + spring_potential_energy(x, k, g, xc);
            }

            F32 spring_potential_energy(F32 x, F32 k, F32 g, F32 xc) const
            {
                return spring_potential_energy(x, k) - g * (x - 0.5f * xc);
            }

            F32 spring_potential_energy(F32 x, F32 k) const
            {
                return 0.5f * k * x * x;
            }

            F32 kinetic_energy(F32 v) const
            {
                return 0.5f * v * v;
            }

            F32 find_spring_min(F32 min_dist, F32 max_dist, F32 gravity, F32 damp) const
            {
                F32 e = xexp(-PI * damp * xsqrt(-(damp * damp - 1.0f)));
                return (2.0f * min_dist + max_dist * (e - 1.0f)) / (1.0f + e);
            }

            static void on_tweak_horizontal(const tweak_info& ti);
            void reset_props_horizontal()
            {
                if (shared.hook == NULL)
                {
                    return;
                }

                h.horizontal = shared.hook->asset->horizontal;
            }

            static void on_tweak_camera(const tweak_info& ti);
            void reset_props_camera()
            {
                if (shared.hook == NULL)
                {
                    return;
                }

                h.camera = shared.hook->asset->camera;
                h.camera.vel_scale /= max_yvel;
                h.camera.view_angle *= PI / 180.0f;
                h.camera.offset_dir *= PI / 180.0f;
                h.camera.offset_dir -= h.camera.view_angle;
                eh.camera.roll_decay = 1.0f - h.camera.roll_speed;
            }

            static void on_tweak_collision(const tweak_info& ti);
            void reset_props_collision()
            {
                if (shared.hook == NULL)
                {
                    return;
                }

                h.collision = shared.hook->asset->collision;

                if (h.collision.damage_velocity == 0.0f)
                {
                    h.collision.damage_velocity = FLOAT_MIN;
                }
                else
                {
                    h.collision.damage_velocity *= max_yvel;
                }

                if (h.collision.hit_velocity == 0.0f)
                {
                    h.collision.hit_velocity = FLOAT_MIN;
                }
                else
                {
                    h.collision.hit_velocity *= max_yvel;
                }
            }

            inline void check_damage(bool thump);

            void multiply_dampening(F32 s)
            {
                h.vertical.damp *= s;
                if (h.vertical.damp > 1.0f)
                {
                    h.vertical.damp = 1.0f;
                }

                eh.vertical.alpha = -h.vertical.damp * xsqrt(eh.vertical.spring);
                eh.vertical.omega =
                    xsqrt(eh.vertical.spring - eh.vertical.alpha * eh.vertical.alpha);
                eh.vertical.emax = spring_energy(h.vertical.max_dist, 0.0f, eh.vertical.spring,
                                                 h.vertical.gravity, eh.vertical.rest_dist);
            }

            inline void update_collision(xScene& s, F32 dt);
            inline bool repath(const xScene& s);
            inline bool boundary_repath(const xScene& s);

            bool hit_boundary(xVec3& norm, xVec3& depen, const xVec3& v) const
            {
                S32 hits = clip_nearest(norm.x, depen.x, v.x, -h.horizontal.max_dist,
                                        h.horizontal.max_dist);
                hits +=
                    clip_nearest(norm.y, depen.y, v.y, h.vertical.max_dist, h.vertical.min_dist);
                hits += clip_nearest(norm.z, depen.z, v.z, -h.horizontal.max_dist,
                                     h.horizontal.max_dist);

                switch (hits)
                {
                case 1:
                    return true;
                case 2:
                    norm *= 0.70710677f;
                    return true;
                case 3:
                    norm *= 0.57735026f;
                    return true;
                }

                return false;
            }

            S32 clip_nearest(F32& norm, F32& depen, F32 x, F32 min, F32 max) const
            {
                S32 hits = 0;

                if (x > max)
                {
                    norm = -1.0f;
                    depen = max - x;
                    hits = 1;
                }
                else if (x < min)
                {
                    norm = 1.0f;
                    depen = min - x;
                    hits = 1;
                }
                else
                {
                    norm = 0.0f;
                    depen = 0.0f;
                }

                return hits;
            }

            inline bool env_repath(const xScene& s);
            inline void trigger_collision(env_info& ei, F32 mag, const xCollis& coll);
            inline void ouchie(bool thump);
            inline bool ents_repath(const xScene& s);

            bool collide(xCollis& coll, const xSphere& o, const xEnt& ent) const
            {
                if (!(ent.chkby & XENT_COLLTYPE_PLYR) || !(ent.penby & XENT_COLLTYPE_PLYR))
                {
                    return false;
                }

                xSphereHitsBound(&o, &ent.bound, &coll);
                if (!(coll.flags & k_HIT_IT))
                {
                    return false;
                }

                if (ent.collLev == 5)
                {
                    xSphereHitsModel(&o, ent.model, &coll);
                    if (!(coll.flags & k_HIT_IT))
                    {
                        return false;
                    }
                }

                return true;
            }

            xSphere player_bound() const
            {
                xSphere o;
                o.r = xglobals->___player_ent_dont_use_directly->bound.sph.r;
                o.center = local_to_world(loc);
                return o;
            }

            xVec3 local_to_world(const xVec3& vl) const
            {
                return vl + shared.hook_loc;
            }

            xVec3 world_to_local(const xVec3& vw) const
            {
                return vw - shared.hook_loc;
            }

            void init_camera()
            {
                const xMat4x3& cm = globals.oldSkoolCamera.mat;

                cam_loc = world_to_local(cm.pos);

                xVec3 eu;
                xMat3x3GetEuler(&cm, &eu);

                F32 cy = icos(eu.y);
                cam_dir.x = cy * isin(eu.x);
                cam_dir.y = -isin(eu.y);
                cam_dir.z = cy * icos(eu.x);

                roll_offset = -(eu.x - h.camera.view_angle);
                roll_offset = xrmod(PI + roll_offset);
                if (roll_offset < 0.0f)
                {
                    roll_offset += 2.0f * PI;
                }
                roll_offset = roll_offset - PI;

                cam_vel = 0.0f;
                cam_dir_vel = 0.0f;
            }

            inline void collide_start(xScene& s);

            // No symbol (always inlined); the name is a guess. collide_start runs it once per
            // entity list (npcs, dyns, stats).
            void cache_ents(xEnt** it, xEnt** end, ent_type type, const xSphere& o)
            {
                xCollis coll;
                coll.flags = 0;

                for (; it != end && ent_cache_size < 256; ++it)
                {
                    xEnt& ent = **it;
                    if (collide(coll, o, ent))
                    {
                        ent_info& info = ent_cache[ent_cache_size];
                        info.type = type;
                        info.ent = &ent;
                        info.hits = 0;
                        ent_cache_size++;
                    }
                }
            }

            inline void update_camera_direction(F32 dt);

            void rotate_camera()
            {
                F32 yaw;
                F32 pitch;

                if (cam_dir.y <= -1.0f)
                {
                    yaw = 0.0f;
                    pitch = PI / 2;
                }
                else
                {
                    yaw = xatan2(cam_dir.x, cam_dir.z);
                    pitch = -xasin(cam_dir.y);
                }

                F32 roll = roll_offset + (yaw - h.camera.view_angle);
                roll_offset *= eh.camera.roll_decay;

                xCameraLookYPR(&globals.oldSkoolCamera, 0, yaw, pitch, roll, 0.0f, 0.0f, 0.0f);
            }

            inline void interpolate_camera_loc(const xVec3& dest, F32 dt);

            inline void update_movement(F32 dt);

            void update_heading(F32 dt)
            {
                F32 angle = xrmod(PI / 2 - stick_ang - rot);
                rot_vel += stick_frac * (angle - PI) * h.turn.spring * stick_mag * dt;
                rot += rot_vel * dt;
                rot_vel *= h.turn.decay;
            }

            inline void update_vmovement(F32 dt);

            // Damped spring solution: advances (x0, v0) by dt around the rest point xc.
            void update_vmovement(F32& x, F32& v, F32 x0, F32 v0, F32 dt, F32 xc, F32 alpha,
                                  F32 omega) const
            {
                F32 s = isin(omega * dt);
                F32 c = icos(omega * dt);
                F32 dx = x0 - xc;
                F32 b = (v0 - dx * alpha) / omega;
                F32 e = xexp(alpha * dt);
                F32 vb = b * alpha - dx * omega;

                x = e * (dx * c + b * s) + xc;
                v = e * (v0 * c + vb * s);
            }

            void allow_dive(bool allowed)
            {
                can_dive = allowed;

                bool can_quit = find_drop_off();
                if (helpTextBox == NULL || helpTextBox->baseType != eBaseTypeTextBox)
                {
                    return;
                }

                if (can_dive)
                {
                    helpTextBox->set_text(!can_quit ? helpNoQuitHash : helpHash);
                }
                else
                {
                    helpTextBox->set_text(!can_quit ? helpNoQuitNoDiveHash : helpNoDiveHash);
                }
            }

            inline void update_animation(F32 dt);
            // update_camera and trigger_collision are defined in the class, below start(): CW
            // compiles in-class bodies in reverse order, which gives retail's .rodata order, and
            // start then inlines hook_asset's operator= as retail does.

            // Not in the PS2 demo. The camera distance (rest_dist + vscale) is a named local to get
            // retail's register order.
            void update_camera(F32 dt)
            {
                xMat3x3 m;
                xVec3 dir;
                xVec3 offset;
                xVec3 up;
                xVec3 goal;
                xVec3 down = { 0.0f, -1.0f, 0.0f };

                if (loc.y >= 0.0f)
                {
                    dir = down;
                }
                else
                {
                    F32 dist = loc.length();
                    if (dist < h.camera.rest_dist)
                    {
                        if (dist < 0.01f)
                        {
                            dir = down;
                        }
                        else
                        {
                            F32 frac = dist / h.camera.rest_dist;
                            dir = down * (1.0f - frac) + loc * (frac / dist);
                            dir.normalize();
                        }
                    }
                    else
                    {
                        dir = loc * (1.0f / dist);
                    }
                }

                F32 vscale = (vel.y >= 0.0f) ? 0.0f : -vel.y * h.camera.vel_scale;

                F32 dist = h.camera.rest_dist + vscale;

                offset = dir * -dist;

                up.x = 0.0f;
                up.y = dir.z;
                up.z = -dir.y;
                up.normalize();

                xMat3x3Rot(&m, &dir, h.camera.offset_dir);
                xMat3x3RMulVec(&up, &m, &up);

                offset += up * h.camera.offset;

                goal = loc + offset;

                if ((goal - cam_loc).length2() > 0.0001f)
                {
                    xVec3 v = cam_dir;

                    interpolate_camera_loc(goal, dt);

                    const xVec3 dv = cam_dir - v;
                    if (((dv.x < 0.0f) ? 1 : 0) != ((vel.x < 0.0f) ? 1 : 0))
                    {
                        cam_dir.x = v.x;
                    }
                    if (((dv.y < 0.0f) ? 1 : 0) != ((vel.y < 0.0f) ? 1 : 0))
                    {
                        cam_dir.y = v.y;
                    }
                    if (((dv.z < 0.0f) ? 1 : 0) != ((vel.z < 0.0f) ? 1 : 0))
                    {
                        cam_dir.z = v.z;
                    }
                }

                xCameraMove(&globals.oldSkoolCamera, local_to_world(cam_loc));
                update_camera_direction(dt);
            }

            F32 trigger_collision(ent_info& ei, F32 mag, const xCollis&)
            {
                ei.hits++;

                if (dying)
                {
                    return 1.0f;
                }

                xSurface* surf = ei.ent->model->Surf;
                if (surf != NULL && !surf->state && zSurfaceGetDamageType(surf))
                {
                    ouchie(true);
                }

                if (mag >= h.collision.hit_velocity)
                {
                    if (ei.type == ENT_NPC)
                    {
                        return 1.0f;
                    }

                    switch (ei.ent->baseType)
                    {
                    case eBaseTypeButton:
                        zEntButton_Press((_zEntButton*)ei.ent, 0x20);
                        break;
                    case eBaseTypeStatic:
                    {
                        F32 hitParams[4] = { 1.0f, 9.0f, 0.0f, 0.0f };
                        zEntEvent(ei.ent, eEventHit, hitParams);
                        break;
                    }
                    }
                }

                if (mag >= h.collision.damage_velocity && ei.type != ENT_NPC &&
                    ei.ent->baseType != eBaseTypeButton && ei.ent->baseType != eBaseTypeStatic)
                {
                    ouchie(true);
                }

                return 1.0f;
            }

            void update_sound(F32 dt)
            {
                F32 vol = xabs(vel.y) / max_yvel;
                if (vol > 1.0f)
                {
                    vol = 1.0f;
                }

                set_volume(SOUND_WIND_LOOP, vol * vol);
            }

            void update_blur(F32 dt)
            {
                F32 blur = xabs(vel.y) / max_yvel;
                if (blur > 1.0f)
                {
                    blur = 1.0f;
                }

                iCameraSetBlurriness(blur * (fixed.vel_blur * blur));
            }

            inline state_enum detach_update(xScene& scene, F32& dt);

            void update_hmovement(F32 dt)
            {
                update_hmovement(loc.x, vel.x, loc.x, vel.x, dt, -stick_loc.x);
                update_hmovement(loc.z, vel.z, loc.z, vel.z, dt, -stick_loc.y);
            }

            void update_hmovement(F32& x, F32& v, F32 x0, F32 v0, F32 dt, F32 stick)
            {
                F32 goal = (1.0f + fixed.horizontal.edge_zone) * (h.horizontal.max_dist * stick);
                if (goal > h.horizontal.max_dist)
                {
                    goal = h.horizontal.max_dist;
                }
                else if (goal < -h.horizontal.max_dist)
                {
                    goal = -h.horizontal.max_dist;
                }

                F32 accel = stick_frac * fixed.horizontal.sway * (goal - x0);
                v = accel * dt + v0;
                x = x0 + (dt * (0.5f * accel * dt) + v * dt);
                v *= fixed.horizontal.decay;
            }
        };

        state_enum hanging_state_type::detach_update(xScene& scene, F32& dt)
        {
            last_hook_loc = shared.hook_loc;
            update_hook_loc();

            stick_loc.x = stick_loc.y = 0.0f;
            last_loc = loc;

            xVec3 deltaVel;
            if (loc.length() > 0.00001f)
            {
                deltaVel = loc.normal() * -h.detach.accel;
            }
            else
            {
                deltaVel.assign(0.0f, 0.0f, 0.0f);
            }

            vel += deltaVel * dt;
            loc += deltaVel * dt * dt + vel * dt;

            update_animation(dt);
            update_camera(dt);

            xglobals->___player_ent_dont_use_directly->bound.sph = player_bound();

            update_sound(dt);
            update_blur(dt);

            if (loc.y >= 0.0f || loc.length2() <= 0.1f)
            {
                play_sound(SOUND_DETACH);
                return STATE_INVALID;
            }

            return STATE_HANGING;
        }

        void hanging_state_type::update_vmovement(F32 dt)
        {
            F32 dv;
            F32 v;
            F32 range;
            F32 ybottom;

            if (can_dive && globals.pad0->pressed & XPAD_BUTTON_X && control_lag_timer <= 0.0f &&
                !dying && dive_remaining <= 0.0f)
            {
                has_dived = true;
                allow_dive(false);
                dive_remaining = fixed.dive.time;
            }

            if (dive_remaining > 0.0f)
            {
                dive_remaining -= dt;

                if (dive_remaining < 0.0f)
                {
                    dv = h.vertical.dive * (dt + dive_remaining);
                    dive_remaining = 0.0f;
                }
                else
                {
                    dv = h.vertical.dive * dt;
                }

                if (spring_energy(loc.y, vel.y, eh.vertical.spring, h.vertical.gravity,
                                  eh.vertical.rest_dist) < eh.vertical.emax)
                {
                    vel.y += dv;

                    if (spring_energy(loc.y, vel.y, eh.vertical.spring, h.vertical.gravity,
                                      eh.vertical.rest_dist) > eh.vertical.emax)
                    {
                        vel.y = spring_velocity(loc.y, vel.y, eh.vertical.emax, eh.vertical.spring,
                                                h.vertical.gravity, eh.vertical.rest_dist);
                    }
                }
            }

            update_vmovement(loc.y, v, loc.y, vel.y, dt, eh.vertical.rest_dist, eh.vertical.alpha,
                             eh.vertical.omega);

            if (v - vel.y < 0.0f && loc.y < h.vertical.min_dist)
            {
                if (spring_energy(loc.y, v, eh.vertical.spring, h.vertical.gravity,
                                  eh.vertical.rest_dist) > eh.vertical.emax)
                {
                    v = spring_velocity(loc.y, v, eh.vertical.emax, eh.vertical.spring,
                                        h.vertical.gravity, eh.vertical.rest_dist);
                }

                if (v > vel.y)
                {
                    v = vel.y;
                }
            }

            vel.y = v;

            if (vel.y > 0.1f * max_yvel)
            {
                has_dived = false;
            }
            else if (!has_dived)
            {
                range = h.vertical.max_dist - h.vertical.min_dist;
                ybottom = -h.vertical.min_dist - range * fixed.dive.min_dist;
                F32 ytop = -h.vertical.min_dist - range * fixed.dive.max_dist;

                if (loc.y <= ybottom && loc.y <= ytop)
                {
                    if (!can_dive)
                    {
                        allow_dive(true);
                    }
                }
                else if (can_dive)
                {
                    allow_dive(false);
                }
            }
        }

        void hanging_state_type::update_movement(F32 dt)
        {
            update_heading(dt);
            update_vmovement(dt);
            update_hmovement(dt);
        }

        void hanging_state_type::update_animation(F32 dt)
        {
            xAnimPlay* ap = xglobals->___player_ent_dont_use_directly->model->Anim;

            if (shared.anim_state & 0x80 && ap->Single->CurrentSpeed == 0.0f && !xScrFxIsFading())
            {
                globals.player.DamageTimer = fixed.death_time;

                xColor_tag black = { 0x00, 0x00, 0x00, 0xFF };
                xColor_tag clear = { 0x00, 0x00, 0x00, 0x00 };
                xScrFxFade(&clear, &black, fixed.death_time, 1);
            }

            RwMatrix& mm = *xglobals->___player_ent_dont_use_directly->model->Mat;
            ((xMat4x3&)mm).pos = local_to_world(loc);
            xMat3x3Euler((xMat3x3*)&mm, rot, 0.0f, 0.0f);

            F32 dist_frac =
                (loc.y - h.vertical.min_dist) / (h.vertical.max_dist - h.vertical.min_dist);

            if (dive_remaining > 0.0f)
            {
                shared.anim_state = shared.anim_state & ~0x3c | 0x1;
            }
            else if (dist_frac <= fixed.top_anim_frac)
            {
                shared.anim_state = shared.anim_state & ~0x33 | 0x4;
            }
            else if (dist_frac >= 1.0f - fixed.bottom_anim_frac)
            {
                shared.anim_state = shared.anim_state & ~0xf | 0x10;
            }

            switch (shared.anim_state)
            {
            case 0x1:
                play_sound(SOUND_DIVE);
                play_sound(SOUND_ATTACH);
                xAnimPlayStartTransition(ap, shared.anim_tran.dive_start);
                shared.anim_state |= 0x2;
                break;
            case 0x2:
                xAnimPlayStartTransition(ap, shared.anim_tran.dive_stop);
                shared.anim_state &= ~0x2;
                break;
            case 0x4:
                play_sound(SOUND_PEAK);
                xAnimPlayStartTransition(ap, shared.anim_tran.top_start);
                shared.anim_state |= 0x8;
                break;
            case 0x8:
                xAnimPlayStartTransition(ap, shared.anim_tran.top_stop);
                shared.anim_state &= ~0x8;
                break;
            case 0x10:
                xAnimPlayStartTransition(ap, shared.anim_tran.bottom_start);
                shared.anim_state |= 0x20;
                break;
            case 0x20:
                xAnimPlayStartTransition(ap, shared.anim_tran.bottom_stop);
                shared.anim_state &= ~0x20;
                break;
            }

            shared.anim_state &= ~0x15;
            if (shared.anim_state == 0)
            {
                F32 vel_frac =
                    xabs(vel.y / spring_velocity(eh.vertical.rest_dist, 1.0f, eh.vertical.emax,
                                                 eh.vertical.spring, h.vertical.gravity,
                                                 eh.vertical.rest_dist));

                xAnimSingle& xas = *xglobals->___player_ent_dont_use_directly->model->Anim->Single;
                xas.BilinearLerp[0] = 3.0f * dist_frac;
                xas.BilinearLerp[1] = 2.0f * vel_frac;
            }

            move_wedgie(shared.hook_loc);

            xModelUpdate(xglobals->___player_ent_dont_use_directly->model, dt);
            xModelEval(xglobals->___player_ent_dont_use_directly->model);
        }

        void hanging_state_type::interpolate_camera_loc(const xVec3& dest, F32 dt)
        {
            xMat3x3 m;
            xMat3x3 im;
            xVec3 curr;
            xVec3 goal;

            F32 ang = h.camera.view_angle - h.camera.offset_dir;

            m.right.assign(icos(ang), 0.0f, isin(ang));
            m.up.assign(0.0f, 1.0f, 0.0f);
            m.at.assign(-m.right.z, 0.0f, m.right.x);
            m.right.normalize();
            m.at.normalize();

            xMat3x3RMulVec(&curr, &m, &cam_loc);
            xMat3x3RMulVec(&goal, &m, &dest);

            curr += h.camera.speed * (goal - curr);

            xMat3x3Transpose(&im, &m);
            xMat3x3RMulVec(&cam_loc, &im, &curr);
        }

        void hanging_state_type::update_camera_direction(F32 dt)
        {
            xVec3 start = cam_dir;
            xVec3 dir = (loc - cam_loc).normal();

            F32 scale = h.camera.turn_speed < 0.1f ? 50.0f : 1.0f;

            cam_dir = start + (dir - start) * h.camera.turn_speed * scale * dt;
            xVec3Normalize(&cam_dir, &cam_dir);

            rotate_camera();
        }

        // 'ent' is the player: its pointer is loaded before the length computation, its radius
        // after.
        void hanging_state_type::collide_start(xScene& s)
        {
            xEnt& ent = *xglobals->___player_ent_dont_use_directly;
            xSphere o;
            o.r = ent.bound.sph.r + 0.5f * (last_loc - loc).length();
            o.center = (last_loc + loc) * 0.5f;
            o.center = local_to_world(o.center);

            ent_cache_size = 0;
            cache_ents(s.npcs, s.npcs + s.num_npcs, ENT_NPC, o);
            cache_ents(s.dyns, s.dyns + s.num_dyns, ENT_DYN, o);
            cache_ents(s.stats, s.stats + s.num_stats, ENT_STAT, o);

            xCollis coll;
            coll.flags = 0;
            env_cache.env = s.env;
            env_cache.collide = xSphereHitsEnv(&o, s.env, &coll);
            env_cache.hits = 0;
        }

        bool hanging_state_type::ents_repath(const xScene&)
        {
            if (ent_cache_size <= 0)
            {
                return false;
            }

            xCollis coll;
            coll.flags = k_HIT_0x200 | k_HIT_0x800;

            bool hit = false;
            ent_info* it = ent_cache;
            ent_info* end = it + ent_cache_size;
            for (; it != end; ++it)
            {
                xSphere o = player_bound();
                if (collide(coll, o, *it->ent))
                {
                    F32 mag = trigger_collision(*it, -vel.y * xabs(coll.norm.y), coll);
                    if (mag != 0.0f)
                    {
                        hit = true;
                        xVec3AddScaled(&loc, &coll.depen, mag);
                        F32 d = mag * coll.norm.dot(vel);
                        vel.x -= coll.norm.x * d;
                        vel.y -= coll.norm.y * d;
                        vel.z -= coll.norm.z * d;
                    }
                }
            }

            return hit;
        }

        void hanging_state_type::ouchie(bool thump)
        {
            if (globals.player.DamageTimer > 0.0f || dying || !globals.player.g.TakeDamage)
            {
                return;
            }

            if (globals.player.s->Health != 0)
            {
                globals.player.s->Health--;
            }

            check_damage(thump);
        }

        void hanging_state_type::trigger_collision(env_info& ei, F32 mag, const xCollis& coll)
        {
            ei.hits++;

            if (dying)
            {
                return;
            }

            xSurface* surf = zSurfaceGetSurface(&coll);
            if (surf != NULL && !surf->state && zSurfaceGetDamageType(surf))
            {
                ouchie(true);
            }

            if (mag >= h.collision.damage_velocity)
            {
                ouchie(true);
            }
        }

        bool hanging_state_type::env_repath(const xScene& s)
        {
            if (!env_cache.collide)
            {
                return false;
            }

            xCollis coll;
            memset(&coll, 0, sizeof(coll));
            coll.flags = k_HIT_0x200 | k_HIT_0x800;

            xSphere o = player_bound();
            if (!xSphereHitsEnv(&o, s.env, &coll))
            {
                return false;
            }

            loc += coll.depen;

            F32 mag = -coll.norm.dot(vel);
            xVec3AddScaled(&vel, &coll.norm, mag);

            trigger_collision(env_cache, mag, coll);

            return true;
        }

        bool hanging_state_type::boundary_repath(const xScene&)
        {
            xVec3 norm;
            xVec3 depen;

            if (!hit_boundary(norm, depen, loc))
            {
                return false;
            }

            loc += depen;
            F32 d = norm.dot(vel);
            vel.x -= norm.x * d;
            vel.y -= norm.y * d;
            vel.z -= norm.z * d;

            return true;
        }

        bool hanging_state_type::repath(const xScene& s)
        {
            bool hit = false;
            hit = ents_repath(s) || hit;
            hit = env_repath(s) || hit;
            hit = boundary_repath(s) || hit;
            return hit;
        }

        void hanging_state_type::update_collision(xScene& s, F32)
        {
            collide_start(s);

            bool again = true;
            for (S32 i = 0; again && i < 8; i++)
            {
                again = repath(s);
            }
        }

        void hanging_state_type::check_damage(bool thump)
        {
            if (dying)
            {
                return;
            }

            U32 old_last_health = last_health;
            last_health = globals.player.s->Health;
            if (globals.player.s->Health >= old_last_health)
            {
                return;
            }

            control_lag_timer = control_lag_max = globals.player.DamageTimer = 2.0f;
            rot_vel += 2.0f * PI * fixed.damage_rot * (xurand() - 0.5f);

            shared.anim_state |= 0x40;
            xAnimPlayStartTransition(xglobals->___player_ent_dont_use_directly->model->Anim,
                                     shared.anim_tran.hit);

            if (thump)
            {
                play_sound(SOUND_THUMP);
            }
            else
            {
                play_sound(SOUND_DAMAGE);
            }

            // BfBB had this block as short_drop_sudden_stop(); TSSM has no symbol for it, and as a
            // separate inline CW will not inline it here (it holds the auto-inlined zEntEventAll).
            if (globals.player.s->Health == 0)
            {
                dying = true;
                gBungeeDeath = 1;
                globals.player.DamageTimer = 20.0f;
                zEntEventAll(NULL, 0, eEventPlayerDeath, NULL);
                control_lag_timer = globals.player.DamageTimer;
                control_lag_max = 0.0f;
                dive_remaining = 0.0f;
                multiply_dampening(5.0f);
                shared.anim_state |= 0x80;
                xglobals->___player_ent_dont_use_directly->entShadow->dst_cast =
                    oldShadowCastDistance;
            }
        }

        void move_wedgie(const xVec3& stretch_loc)
        {
            if (shared.ass_model == NULL || shared.pants_model == NULL)
            {
                return;
            }

            shared.pants_model->Mat[2] = shared.root_model->Mat[2];

            xMat4x3 tm;
            xMat4x3 mworld;
            xMat4x3 mlocal;
            xMat4x3OrthoInv(&tm, (const xMat4x3*)shared.root_model->Mat);
            xMat3x3Identity(&mworld);
            mworld.pos = stretch_loc;

            static xVec3 tweak_cord_off = { 0.0f, 0.0f, 0.0f };
            static bool registered = false;
            if (!registered)
            {
                registered = true;
            }

            mworld.pos += tweak_cord_off;
            xMat4x3Mul(&mlocal, &mworld, &tm);
            shared.pants_model->Mat[34] = *(RwMatrix*)&mlocal;
        }

        void hanging_state_type::on_tweak_turn(const tweak_info& ti)
        {
            ((hanging_state_type*)ti.context)->reset_props_turn();
        }

        void hanging_state_type::reset_props_vertical()
        {
            if (shared.hook == NULL)
            {
                return;
            }

            h.vertical = shared.hook->asset->vertical;

            h.vertical.gravity *= -1.0f * h.vertical.frequency * h.vertical.frequency;
            h.vertical.dive *= h.vertical.gravity;
            h.vertical.damp = range_limit(h.vertical.damp, 0.0f, 1.0f);
            h.vertical.min_dist *= -1.0f;
            h.vertical.max_dist *= -1.0f;
            h.vertical.min_dist = find_spring_min(h.vertical.min_dist, h.vertical.max_dist,
                                                  h.vertical.gravity, h.vertical.damp);
            eh.vertical.rest_dist = 0.5f * (h.vertical.max_dist + h.vertical.min_dist);
            eh.vertical.spring = h.vertical.gravity / eh.vertical.rest_dist;
            eh.vertical.alpha = -h.vertical.damp * xsqrt(eh.vertical.spring);
            eh.vertical.omega = xsqrt(eh.vertical.spring - eh.vertical.alpha * eh.vertical.alpha);
            eh.vertical.emax = spring_energy(h.vertical.max_dist, 0.0f, eh.vertical.spring,
                                             h.vertical.gravity, eh.vertical.rest_dist);
            max_yvel =
                spring_velocity(eh.vertical.rest_dist, 1.0f, eh.vertical.emax, eh.vertical.spring,
                                h.vertical.gravity, eh.vertical.rest_dist);
            h.camera.vel_scale = shared.hook->asset->camera.vel_scale / max_yvel;
        }

        void hanging_state_type::on_tweak_vertical(const tweak_info& ti)
        {
            ((hanging_state_type*)ti.context)->reset_props_vertical();
        }

        void hanging_state_type::on_tweak_horizontal(const tweak_info& ti)
        {
            ((hanging_state_type*)ti.context)->reset_props_horizontal();
        }

        void hanging_state_type::on_tweak_camera(const tweak_info& ti)
        {
            ((hanging_state_type*)ti.context)->reset_props_camera();
        }

        void hanging_state_type::on_tweak_collision(const tweak_info& ti)
        {
            ((hanging_state_type*)ti.context)->reset_props_collision();
        }

        U32 check_anim_start(xAnimTransition*, xAnimSingle*, void*)
        {
            return 0;
        }

        U32 check_anim_mount_to_cycle_start(xAnimTransition*, xAnimSingle*, void*)
        {
            return 1;
        }

        U32 check_anim_hit_to_dive(xAnimTransition*, xAnimSingle*, void*)
        {
            shared.anim_state &= ~0x40;
            return shared.anim_state & 0x2 && !(shared.anim_state & 0x80);
        }

        U32 check_anim_hit_to_top(xAnimTransition*, xAnimSingle*, void*)
        {
            shared.anim_state &= ~0x40;
            return shared.anim_state & 0x8 && !(shared.anim_state & 0x80);
        }

        U32 check_anim_hit_to_bottom(xAnimTransition*, xAnimSingle*, void*)
        {
            shared.anim_state &= ~0x40;
            return shared.anim_state & 0x20 && !(shared.anim_state & 0x80);
        }

        U32 check_anim_hit_to_cycle(xAnimTransition*, xAnimSingle*, void*)
        {
            shared.anim_state &= ~0x40;
            return !(shared.anim_state & 0x2a) && !(shared.anim_state & 0x80);
        }

        U32 check_anim_hit_to_death(xAnimTransition*, xAnimSingle*, void*)
        {
            shared.anim_state &= ~0x40;
            return shared.anim_state & 0x80;
        }

        S32 find_nearest_hook(const xVec3& loc)
        {
            S32 found = -1;
            F32 closest = 10000000000.0f;
            for (S32 i = 0; i < shared.hook_cache_size; ++i)
            {
                F32 attach_dist = shared.hook_cache[i]->asset->attach.dist;
                xVec3 dloc = *xEntGetPos(shared.hook_cache[i]->ent) +
                             shared.hook_cache[i]->asset->center - loc;
                F32 len2 = dloc.length2();
                if (len2 <= SQR(attach_dist) && len2 < closest)
                {
                    found = i;
                    closest = len2;
                }
            }
            return found;
        }

        void start()
        {
            if ((shared.flags & 0x7) != 0x3)
            {
                return;
            }

            xEnt& player = *xglobals->___player_ent_dont_use_directly;
            const char* anim_name = player.model->Anim->Single->State->Name;
            bool found = shared.hook == NULL && globals.player.s != NULL &&
                         gCurrentPlayer == eCurrentPlayerSpongeBob &&
                         (strcmp(anim_name, "JumpStart01") || strcmp(anim_name, "JumpLift01") ||
                          strcmp(anim_name, "JumpApex01") || strcmp(anim_name, "DJumpStart01") ||
                          strcmp(anim_name, "DJumpLift01") || strcmp(anim_name, "Fall01") ||
                          strcmp(anim_name, "FallHigh01")) &&
                         globals.player.cheat_mode == 0 && globals.player.ControlOff == 0;

            shared.hook = NULL;
            if (found)
            {
                S32 i = find_nearest_hook(player.frame->mat.pos);
                if (i >= 0)
                {
                    shared.hook = shared.hook_cache[i];
                    shared.hook->ent = shared.hook_cache[i]->ent;
                }
            }

            if (shared.hook == NULL)
            {
                return;
            }

            shared.flags |= 0x4;
            shared.state = shared.states[STATE_ATTACHING];
            shared.state->start();
        }

        void cache_hook(hook_type& hook)
        {
            shared.hook_cache[shared.hook_cache_size] = &hook;
            shared.hook_cache_size++;
        }

        void cache_drop(const drop_asset& drop, U32 size)
        {
            xMarkerAsset* marker = (xMarkerAsset*)xSTFindAsset(drop.marker, &size);
            if (marker == NULL)
            {
                return;
            }

            if (size != sizeof(xMarkerAsset))
            {
                return;
            }

            shared.drop_cache[shared.drop_cache_size] = &drop;
            shared.drop_marker_cache[shared.drop_cache_size] = marker;
            shared.drop_cache_size++;
        }

        void init_cache()
        {
            shared.hook_cache_size = 0;

            zScene& s = *globals.sceneCur;
            hook_type* it = (hook_type*)s.baseList[eBaseTypeBungeeHook];
            hook_type* end = it + s.baseCount[eBaseTypeBungeeHook];
            for (; it != end; ++it)
            {
                cache_hook(*it);
            }

            shared.drop_cache_size = 0;

            S32 imax = xSTAssetCountByType('DYNA');
            U32 drop_type_id = xStrHash(drop_asset::type_name());
            for (S32 i = 0; i < imax; ++i)
            {
                U32 size = 0;
                xDynAsset* a = (xDynAsset*)xSTFindAssetByType('DYNA', i, &size);
                if (a == NULL)
                {
                    continue;
                }

                if (a->type == drop_type_id)
                {
                    cache_drop(*(drop_asset*)a, size);
                }
            }
        }
    } // namespace

    void load(xBase& data, xDynAsset& asset, size_t)
    {
        xBaseInit(&data, &asset);

        hook_type& hook = (hook_type&)data;
        hook.asset = (hook_asset*)&asset;
        if (hook.linkCount != 0)
        {
            hook.link = (xLinkAsset*)(hook.asset + 1);
        }

        hook.ent = (xEnt*)zSceneFindObject(hook.asset->entity);
    }

    void load_settings(xIniFile& ini)
    {
        fixed.bottom_anim_frac = xIniGetFloat(&ini, "SB.state.bungee.bottom_anim_frac", 0.05f);
        if (fixed.bottom_anim_frac < 0.0f)
        {
            fixed.bottom_anim_frac = 0.0f;
        }
        if (fixed.bottom_anim_frac > 0.5f)
        {
            fixed.bottom_anim_frac = 0.5f;
        }

        fixed.top_anim_frac = xIniGetFloat(&ini, "SB.state.bungee.top_anim_frac", 0.1f);
        if (fixed.top_anim_frac < 0.0f)
        {
            fixed.top_anim_frac = 0.0f;
        }
        if (fixed.top_anim_frac > 0.5f)
        {
            fixed.top_anim_frac = 0.5f;
        }

        fixed.bottom_anim_time = xIniGetFloat(&ini, "SB.state.bungee.bottom_anim_time", 0.1f);
        if (fixed.bottom_anim_time < 0.0f)
        {
            fixed.bottom_anim_time = 0.0f;
        }
        if (fixed.bottom_anim_time > 2.0f)
        {
            fixed.bottom_anim_time = 2.0f;
        }

        fixed.top_anim_time = xIniGetFloat(&ini, "SB.state.bungee.top_anim_time", 0.1f);
        if (fixed.top_anim_time < 0.0f)
        {
            fixed.top_anim_time = 0.0f;
        }
        if (fixed.top_anim_time > 2.0f)
        {
            fixed.top_anim_time = 2.0f;
        }

        fixed.hit_anim_time = xIniGetFloat(&ini, "SB.state.bungee.hit_anim_time", 0.1f);
        if (fixed.hit_anim_time < 0.0f)
        {
            fixed.hit_anim_time = 0.0f;
        }
        if (fixed.hit_anim_time > 2.0f)
        {
            fixed.hit_anim_time = 2.0f;
        }

        fixed.damage_rot = xIniGetFloat(&ini, "SB.state.bungee.damage_rot", 0.0f);
        if (fixed.damage_rot < 10.0f)
        {
            fixed.damage_rot = 10.0f;
        }
        if (fixed.damage_rot > 1000.0f)
        {
            fixed.damage_rot = 1000.0f;
        }

        fixed.death_time = xIniGetFloat(&ini, "SB.state.bungee.death_time", 3.0f);
        if (fixed.death_time < 0.0f)
        {
            fixed.death_time = 0.0f;
        }
        if (fixed.death_time > 20.0f)
        {
            fixed.death_time = 20.0f;
        }

        fixed.vel_blur = xIniGetFloat(&ini, "SB.state.bungee.vel_blur", 0.7f);
        if (fixed.vel_blur < 0.0f)
        {
            fixed.vel_blur = 0.0f;
        }
        if (fixed.vel_blur > 1.0f)
        {
            fixed.vel_blur = 1.0f;
        }

        fixed.fade_dist = xIniGetFloat(&ini, "SB.state.bungee.fade_dist", 2.0f);
        if (fixed.fade_dist < 1.0f)
        {
            fixed.fade_dist = 1.0f;
        }
        if (fixed.fade_dist > 10000.0f)
        {
            fixed.fade_dist = 10000.0f;
        }

        fixed.horizontal.edge_zone =
            xIniGetFloat(&ini, "SB.state.bungee.horizontal.edge_zone", 0.05f);
        if (fixed.horizontal.edge_zone < 0.0f)
        {
            fixed.horizontal.edge_zone = 0.0f;
        }
        if (fixed.horizontal.edge_zone > 0.25f)
        {
            fixed.horizontal.edge_zone = 0.25f;
        }

        fixed.horizontal.sway = xIniGetFloat(&ini, "SB.state.bungee.horizontal.sway", 3.0f);
        if (fixed.horizontal.sway < 0.0f)
        {
            fixed.horizontal.sway = 0.0f;
        }
        if (fixed.horizontal.sway > 10000.0f)
        {
            fixed.horizontal.sway = 10000.0f;
        }

        fixed.horizontal.decay = xIniGetFloat(&ini, "SB.state.bungee.horizontal.decay", 0.95f);
        if (fixed.horizontal.decay < 0.0f)
        {
            fixed.horizontal.decay = 0.0f;
        }
        if (fixed.horizontal.decay > 1.0f)
        {
            fixed.horizontal.decay = 1.0f;
        }

        fixed.dive.time = xIniGetFloat(&ini, "SB.state.bungee.dive.time", 0.5f);
        if (fixed.dive.time < 0.01f)
        {
            fixed.dive.time = 0.01f;
        }
        if (fixed.dive.time > 5.0f)
        {
            fixed.dive.time = 5.0f;
        }

        fixed.dive.anim_out_time = xIniGetFloat(&ini, "SB.state.bungee.dive.anim_out_time", 0.5f);
        if (fixed.dive.anim_out_time < 0.0f)
        {
            fixed.dive.anim_out_time = 0.0f;
        }
        if (fixed.dive.anim_out_time > 5.0f)
        {
            fixed.dive.anim_out_time = 5.0f;
        }

        fixed.dive.min_dist = xIniGetFloat(&ini, "SB.state.bungee.dive.min_dist", 0.0f);
        if (fixed.dive.min_dist < 0.0f)
        {
            fixed.dive.min_dist = 0.0f;
        }
        if (fixed.dive.min_dist > 1.0f)
        {
            fixed.dive.min_dist = 1.0f;
        }

        fixed.dive.max_dist = xIniGetFloat(&ini, "SB.state.bungee.dive.max_dist", 0.6f);
        if (fixed.dive.max_dist < 0.51f)
        {
            fixed.dive.max_dist = 0.51f;
        }
        if (fixed.dive.max_dist > 1.0f)
        {
            fixed.dive.max_dist = 1.0f;
        }
    }

    void deinit()
    {
        shared.flags &= ~0x2;
    }

    // No symbol (GC or PS2): inlined into zEntPlayerReset.
    void reset()
    {
        if ((shared.flags & 0x3) == 0x3)
        {
            shared.flags = 0x3;
            shared.state = NULL;
        }
    }

    bool update(xScene* sc, F32 dt)
    {
        if ((shared.flags & 0x3) != 0x3)
        {
            return false;
        }

        shared.away_timer -= dt;
        if (shared.away_timer > 0.0f)
        {
            return false;
        }

        start();

        if (shared.state == NULL)
        {
            return false;
        }

        for (;;)
        {
            state_enum newtype = shared.state->update(*sc, dt);
            if (newtype == shared.state->type)
            {
                break;
            }

            if (newtype == STATE_INVALID)
            {
                stop();
                break;
            }

            shared.state->stop();
            shared.state = shared.states[newtype];
            shared.state->start();
        }

        xglobals->___player_ent_dont_use_directly->CollideTrigger(sc);
        return shared.state != NULL;
    }

    // No GC symbol: inlined into zEntPlayer_SpongeBobInit and dead-stripped. Defined after update:
    // retail's pool has "game_object:bungee_drop" (init_cache) ahead of update's strings.
    void init()
    {
        if ((shared.flags & 0x1) != 0x1)
        {
            return;
        }

        shared.flags = 0x3;
        shared.state = NULL;
        shared.hook = NULL;

        static attaching_state_type attaching_state;
        shared.states[STATE_ATTACHING] = &attaching_state;

        static hanging_state_type hanging_state;
        shared.states[STATE_HANGING] = &hanging_state;

        init_cache();
    }

    // No symbol: inlined into zEntPlayer_Render.
    bool render()
    {
        if ((shared.flags & 0x7) != 0x7)
        {
            return false;
        }

        shared.state->render();
        return true;
    }

    // No GC symbol: inlined into update and dead-stripped.
    void stop()
    {
        if ((shared.flags & 0x7) != 0x7)
        {
            return;
        }

        if (shared.state != NULL)
        {
            shared.state->stop();
        }

        shared.flags &= ~0x4;
        shared.state = NULL;
        shared.hook = NULL;
    }

    // No symbol: inlined into CheckPickupAgainstPlayer.
    bool active()
    {
        return shared.state != NULL;
    }

    void insert_animations(xAnimTable& table)
    {
        xAnimTableNewState(&table, "bungee_bottom_0", 0x10, 0, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                           xAnimDefaultBeforeEnter, NULL, NULL);
        xAnimTableNewState(&table, "bungee_top_0", 0x10, 0, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                           xAnimDefaultBeforeEnter, NULL, NULL);
        xAnimTableNewState(&table, "bungee_dive_0", 0, 0, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                           xAnimDefaultBeforeEnter, NULL, NULL);
        xAnimTableNewState(&table, "bungee_hit_0", 0x20, 0, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                           xAnimDefaultBeforeEnter, NULL, NULL);
        xAnimTableNewState(&table, "bungee_mount_0", 0x20, 0, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                           xAnimDefaultBeforeEnter, NULL, NULL);
        xAnimTableNewState(&table, "bungee_cycle_0", 0x10, 0, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                           xAnimDefaultBeforeEnter, NULL, NULL);
        xAnimTableNewState(&table, "bungee_death_0", 0, 0, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                           xAnimDefaultBeforeEnter, NULL, NULL);

        static const char* start_from =
            "JumpStart01 JumpLift01 JumpApex01 DJumpStart01 DJumpLift01 Fall01 FallHigh01";
        static const char* start_to = "bungee_mount_0";

        shared.anim_tran.start =
            xAnimTableNewTransition(&table, start_from, start_to, check_anim_start, NULL, 0x10, 0,
                                    0.0f, 0.0f, 1, 0, 0.5f, NULL);
        xAnimTableNewTransition(&table, "bungee_mount_0", "bungee_cycle_0",
                                check_anim_mount_to_cycle_start, NULL, 0x10, 0, 0.0f, 0.0f, 0, 0,
                                0.5f, NULL);
        shared.anim_tran.dive_start =
            xAnimTableNewTransition(&table, "bungee_cycle_0", "bungee_dive_0", NULL, NULL, 0x10, 0,
                                    0.0f, 0.0f, 0, 0, fixed.dive.time, NULL);
        shared.anim_tran.dive_stop =
            xAnimTableNewTransition(&table, "bungee_dive_0", "bungee_cycle_0", NULL, NULL, 0x10, 0,
                                    0.0f, 0.0f, 0, 0, fixed.dive.anim_out_time, NULL);
        shared.anim_tran.top_start =
            xAnimTableNewTransition(&table, "bungee_cycle_0", "bungee_top_0", NULL, NULL, 0x10, 0,
                                    0.0f, 0.0f, 0, 0, fixed.top_anim_time, NULL);
        shared.anim_tran.top_stop =
            xAnimTableNewTransition(&table, "bungee_top_0", "bungee_cycle_0", NULL, NULL, 0x10, 0,
                                    0.0f, 0.0f, 0, 0, fixed.top_anim_time, NULL);
        shared.anim_tran.bottom_start =
            xAnimTableNewTransition(&table, "bungee_cycle_0", "bungee_bottom_0", NULL, NULL, 0x10,
                                    0, 0.0f, 0.0f, 0, 0, fixed.bottom_anim_time, NULL);
        shared.anim_tran.bottom_stop =
            xAnimTableNewTransition(&table, "bungee_bottom_0", "bungee_cycle_0", NULL, NULL, 0x10,
                                    0, 0.0f, 0.0f, 0, 0, fixed.bottom_anim_time, NULL);
        shared.anim_tran.hit = xAnimTableNewTransition(
            &table, "bungee_cycle_0 bungee_dive_0 bungee_top_0 bungee_bottom_0", "bungee_hit_0",
            NULL, NULL, 0x10, 0, 0.0f, 0.0f, 0, 0, fixed.hit_anim_time, NULL);
        xAnimTableNewTransition(&table, "bungee_hit_0", "bungee_dive_0", check_anim_hit_to_dive,
                                NULL, 0x10, 0, 0.0f, 0.0f, 0, 0, fixed.hit_anim_time, NULL);
        xAnimTableNewTransition(&table, "bungee_hit_0", "bungee_top_0", check_anim_hit_to_top, NULL,
                                0x10, 0, 0.0f, 0.0f, 0, 0, fixed.hit_anim_time, NULL);
        xAnimTableNewTransition(&table, "bungee_hit_0", "bungee_bottom_0", check_anim_hit_to_bottom,
                                NULL, 0x10, 0, 0.0f, 0.0f, 0, 0, fixed.hit_anim_time, NULL);
        xAnimTableNewTransition(&table, "bungee_hit_0", "bungee_cycle_0", check_anim_hit_to_cycle,
                                NULL, 0x10, 0, 0.0f, 0.0f, 0, 0, fixed.hit_anim_time, NULL);
        xAnimTableNewTransition(&table, "bungee_hit_0", "bungee_death_0", check_anim_hit_to_death,
                                NULL, 0x10, 0, 0.0f, 0.0f, 0, 0, fixed.hit_anim_time, NULL);
        shared.anim_tran.stop =
            xAnimTableNewTransition(&table,
                                    "bungee_cycle_0 bungee_dive_0 bungee_top_0 bungee_bottom_0",
                                    "Fall01", NULL, NULL, 0x10, 0, 0.0f, 0.0f, 0, 0, 0.5f, NULL);
    }
} // namespace bungee_state
