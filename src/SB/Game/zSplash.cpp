#include "zSplash.h"

#include <types.h>
#include <rwcore.h>
#include <rpptank.h>

#include "iColor.h"
#include "xCamera.h"
#include "xDebug.h"
#include "xIM.h"
#include "xMath2.h"
#include "xMath3.h"
#include "xMathInlines.h"
#include "xMemory.h"
#include "xParticleBatch.h"
#include "xPtankPool.h"
#include "xstransvc.h"
#include "xutil.h"
#include "xVec3.h"
#include "zGlobals.h"
#include "zRenderState.h"
#include "xRand.h"

namespace
{
    enum type_enum
    {
        TYPE_SPLASH,
        TYPE_PLUME,
        TYPE_BUBBLE,
        TYPE_CHAIN,
        MAX_TYPE
    };

    struct splash_emitter
    {
        F32 age;
        F32 age_rate;
        xVec3 center;
        F32 radius;
        F32 radius_vel;
        F32 dxz;
        F32 dxz_vel;
        F32 dy;
        F32 dy_vel;
        F32 dy_accel;
        F32 alpha_start;
        F32 alpha_delta;
        F32 u;
        struct
        {
            F32 emit_end;
            F32 rate;
            F32 emitted;
            F32 age_rate;
            F32 size_start;
            F32 size_delta;
            F32 size_vel;
            xColor_tag color;
            F32 y_vel;
            F32 y_accel;
        } mist;
        struct
        {
            F32 emit_begin;
            F32 emit_end;
            F32 rate;
            F32 emitted;
            F32 age_rate_min;
            F32 age_rate_delta;
            F32 size_min;
            F32 size_delta;
            xColor_tag color;
        } ring;
        struct
        {
            F32 emit_end;
            F32 rate;
            F32 emitted;
            F32 age_rate_min;
            F32 age_rate_delta;
            F32 vel_min;
            F32 vel_delta;
            F32 width;
            F32 length;
            xColor_tag color;
            F32 y_accel;
        } streak;
    };

    struct splash_config
    {
        struct
        {
            F32 life;
            F32 radius_birth;
            F32 radius_death;
            F32 dxz_birth;
            F32 dxz_death;
            F32 dy_birth;
            F32 dy_max;
            F32 dy_airtime;
            S32 alpha_birth;
            S32 alpha_death;
            U32 sound_group_id;
        } cone;
        struct
        {
            F32 rate;
            F32 size_min;
            F32 size_max;
            F32 size_scale;
            F32 life;
            F32 y_vel;
            F32 y_accel;
            xColor_tag color;
        } mist;
        struct
        {
            F32 emit_begin;
            F32 emit_end;
            F32 rate;
            F32 life_min;
            F32 life_max;
            F32 size_min;
            F32 size_max;
            xColor_tag color;
        } ring;
        struct
        {
            F32 emit_end;
            F32 rate;
            F32 life_min;
            F32 life_max;
            F32 vel_min;
            F32 vel_max;
            F32 width;
            F32 length;
            F32 y_accel;
            xColor_tag color;
        } streak;
        splash_emitter initial;
    };

    struct plume_emitter
    {
        F32 emit_time;
        F32 emit_end;
        F32 emitted;
        F32 rate;
        xVec3 center;
        F32 radius;
        F32 vel_min;
        F32 vel_delta;
        F32 vel_angle;
        F32 size_min;
        F32 size_delta;
        F32 size_scale;
        F32 age_rate_min;
        F32 age_rate_delta;
        F32 y_accel;
        xColor_tag color;
    };

    struct plume_config
    {
        F32 emit_end;
        F32 rate;
        F32 radius;
        F32 life_min;
        F32 life_max;
        F32 vel_min;
        F32 vel_max;
        F32 vel_angle;
        F32 size_min;
        F32 size_max;
        F32 size_scale;
        F32 y_accel;
        xColor_tag color;
        plume_emitter initial;
    };

    struct bubble_emitter
    {
        F32 age;
        F32 age_rate;
        xVec3 center;
    };

    struct bubble_config
    {
        F32 rate;
        F32 life;
        bubble_emitter initial;
    };

    struct chain_link
    {
        F32 time;
        S32 type;
    };

    struct chain_config
    {
        chain_link links[8];
        chain_link ordered_links[8];
        S32 links_size;
    };

    struct chain_emitter
    {
        chain_config* chain;
        xVec3 loc;
        F32 time;
        S32 next_link;
    };

    struct splash_planner
    {
        char name[64];
        type_enum type;
        union
        {
            splash_config splash;
            plume_config plume;
            bubble_config bubble;
            chain_config chain;
        };
    };

    struct mist_particle
    {
        xVec3 loc;
        F32 size;
        F32 age;
        F32 age_rate;
        F32 y_vel;
        F32 y_accel;
        F32 size_vel;
        xColor_tag color;
        F32 alpha;
        F32 pad;
    };

    struct ring_particle
    {
        xVec3 loc;
        F32 size;
        F32 age;
        F32 age_rate;
        F32 rx;
        F32 rz;
        F32 size_vel;
        xColor_tag color;
        F32 alpha;
        U8 fade_in;
        U8 pad[3];
    };

    struct plume_particle
    {
        xVec3 loc;
        F32 size;
        F32 age;
        F32 age_rate;
        F32 y_accel;
        F32 size_vel;
        xVec3 vel;
        xColor_tag color;
    };

    struct streak_particle
    {
        xVec3 loc;
        F32 width;
        F32 length;
        xVec3 vel;
        F32 age;
        F32 age_rate;
        F32 length_vel;
        F32 length_max;
        F32 y_accel;
        xColor_tag color;
        F32 alpha;
        F32 pad1;
    };

    // Plume alpha fade cubic. Tweakables in Ratatouille; retail has no data for them, so constants.
    const F32 alpha_coeff1 = 8.067227f;
    const F32 alpha_coeff2 = -14.789916f;
    const F32 alpha_coeff3 = 6.722689f;

    U32 texture_ids[8];
    S32 cone_texture_repeats = 4;
    RwTexture* cone_texture;
    splash_emitter* splash_emitters;
    S32 splash_emitters_size;
    plume_emitter* plume_emitters;
    S32 plume_emitters_size;
    bubble_emitter* bubble_emitters;
    S32 bubble_emitters_size;
    chain_emitter* chain_emitters;
    S32 chain_emitters_size;
    S32 mist_system_id;
    S32 ring_system_id;
    S32 streak_system_id;
    S32 plume_system_id;
    S32 trail_system_id;
    S32 drips_system_id;
    splash_planner* planners;
    S32 types_size;
    F32 old_zbias;

    void render_cones();
    void update_plume_emitters(F32 dt);
    void update_splash_emitters(F32 dt);
    S32 update_streak_particles(U8* mem, S32 count, ptank_pool& apool, F32 dt, void*);
    void emit_splash_internal(S32 type, const xVec3& loc);

    // Inlined into zSplashUpdate (separate function in the PS2 build).
    inline void update_chain_emitters(F32 dt)
    {
        chain_emitter* e = chain_emitters;

        while (e < chain_emitters + chain_emitters_size)
        {
            e->time += dt;

            chain_link* links = e->chain->ordered_links;
            S32 links_size = e->chain->links_size;
            chain_link* link = links + e->next_link;
            chain_link* end_link = links + links_size;

            while (link != end_link && link->time <= e->time)
            {
                emit_splash_internal(link->type, e->loc);
                ++link;
            }

            e->next_link = link - links;

            if (e->next_link >= links_size)
            {
                --chain_emitters_size;
                *e = chain_emitters[chain_emitters_size];
            }
            else
            {
                ++e;
            }
        }
    }

    // Not present in the PS2 DWARF (always inlined).
    inline void update_bubble_emitters(F32 dt)
    {
        bubble_emitter* e = bubble_emitters;
        bubble_emitter* end = e + bubble_emitters_size;

        while (e != end)
        {
            e->age += e->age_rate * dt;

            if (e->age >= 1.0f)
            {
                --end;
                --bubble_emitters_size;
                *e = bubble_emitters[bubble_emitters_size];
            }
            else
            {
                ++e;
            }
        }
    }
} // namespace

namespace
{
    void refresh_splash(splash_config& cfg)
    {
        splash_emitter& initial = cfg.initial;
        F32 ilife = 1.0f / cfg.cone.life;

        initial.age = 0.0f;
        initial.age_rate = ilife;
        initial.radius = cfg.cone.radius_birth;
        initial.radius_vel = ilife * (cfg.cone.radius_death - cfg.cone.radius_birth);
        initial.dxz = cfg.cone.dxz_birth;
        initial.dxz_vel = ilife * (cfg.cone.dxz_death - cfg.cone.dxz_birth);
        initial.alpha_start = 0.5f + cfg.cone.alpha_birth;
        initial.alpha_delta = (F32)cfg.cone.alpha_death - (F32)cfg.cone.alpha_birth;
        initial.u = 0.0f;

        F32 Y0 = cfg.cone.dy_birth;
        F32 Ym = cfg.cone.dy_max;
        F32 iT1 = 1.0f / cfg.cone.dy_airtime;
        F32 dYr = xsqrt(Ym * (Ym - Y0));

        initial.dy = cfg.cone.dy_birth;
        initial.dy_vel = iT1 * (2.0f * (dYr + (Ym - Y0)));
        initial.dy_accel = iT1 * (iT1 * (-2.0f * (2.0f * Ym - Y0 + 2.0f * dYr)));

        initial.mist.emit_end = cfg.cone.dy_airtime * ilife;
        initial.mist.rate = cfg.mist.rate;
        initial.mist.emitted = 0.0f;
        initial.mist.size_start = cfg.mist.size_min;
        initial.mist.size_delta = cfg.mist.size_max - cfg.mist.size_min;
        initial.mist.size_vel = cfg.mist.size_scale * cfg.mist.life;
        initial.mist.age_rate = 1.0f / cfg.mist.life;
        initial.mist.color = cfg.mist.color;
        initial.mist.y_vel = cfg.mist.y_vel;
        initial.mist.y_accel = cfg.mist.y_accel;

        initial.ring.emit_begin = cfg.ring.emit_begin * ilife;
        initial.ring.emit_end = cfg.ring.emit_end * ilife;
        initial.ring.rate = cfg.ring.rate;
        initial.ring.emitted = 0.0f;
        initial.ring.age_rate_min = 1.0f / cfg.ring.life_max;
        initial.ring.age_rate_delta = 1.0f / cfg.ring.life_min - initial.ring.age_rate_min;
        initial.ring.size_min = cfg.ring.size_min;
        initial.ring.size_delta = cfg.ring.size_max - cfg.ring.size_min;
        initial.ring.color = cfg.ring.color;

        initial.streak.emit_end = cfg.streak.emit_end * ilife;
        initial.streak.rate = cfg.streak.rate;
        initial.streak.emitted = 0.0f;
        initial.streak.age_rate_min = 1.0f / cfg.streak.life_max;
        initial.streak.age_rate_delta = 1.0f / cfg.streak.life_min - initial.streak.age_rate_min;
        initial.streak.vel_min = cfg.streak.vel_min;
        initial.streak.vel_delta = cfg.streak.vel_max - cfg.streak.vel_min;
        initial.streak.width = cfg.streak.width;
        initial.streak.length = cfg.streak.length;
        initial.streak.y_accel = cfg.streak.y_accel;
        initial.streak.color = cfg.streak.color;
    }

    // Not present in the GC symbols (always inlined).
    inline void refresh_plume(plume_config& cfg)
    {
        plume_emitter& initial = cfg.initial;

        initial.emit_time = 0.0f;
        initial.emit_end = cfg.emit_end;
        initial.emitted = 0.0f;
        initial.rate = cfg.rate;
        initial.radius = cfg.radius;
        initial.vel_min = cfg.vel_min;
        initial.vel_delta = cfg.vel_max - cfg.vel_min;
        initial.vel_angle = (PI / 180.0f) * cfg.vel_angle;
        initial.size_min = cfg.size_min;
        initial.size_delta = cfg.size_max - cfg.size_min;
        initial.size_scale = cfg.size_scale;
        initial.age_rate_min = 1.0f / cfg.life_max;
        initial.age_rate_delta = 1.0f / cfg.life_min - initial.age_rate_min;
        initial.y_accel = cfg.y_accel;
        initial.color = cfg.color;
    }

    // Not present in the GC symbols (always inlined).
    inline void refresh_chain(chain_config& chain)
    {
        xMemoryCopyUpA32(chain.ordered_links, chain.links, chain.links_size * sizeof(chain_link));
        quicksort_float(chain.ordered_links, chain.links_size, sizeof(chain_link), 0);
    }

    // No GC symbols (always inlined); names from Ratatouille. The caller checks the capacity.
    inline splash_emitter* alloc_splash_emitter()
    {
        return &splash_emitters[splash_emitters_size++];
    }

    inline plume_emitter* alloc_plume_emitter()
    {
        return &plume_emitters[plume_emitters_size++];
    }

    inline bubble_emitter* alloc_bubble_emitter()
    {
        return &bubble_emitters[bubble_emitters_size++];
    }

    inline chain_emitter* alloc_chain_emitter()
    {
        return &chain_emitters[chain_emitters_size++];
    }

    // Retail calls this out of line; GC/2.6 only stops auto-inlining at 30 statements, so the
    // compiled-out asserts count. The first is Ratatouille's; the others' conditions are unknown.
    void emit_splash_internal(S32 type, const xVec3& loc)
    {
        xASSERT((type >= 0) && (type < types_size));
        xASSERT_PLACEHOLDER();

        splash_planner& plan = planners[type];

        switch (plan.type)
        {
        case TYPE_SPLASH:
            if (splash_emitters_size < 64)
            {
                splash_emitter& splash = *alloc_splash_emitter();
                splash = plan.splash.initial;
                splash.center = loc;
                splash.u = xurand();
                xSndMgrPlay(xSndMgrGetSoundGroup(plan.splash.cone.sound_group_id), 0, &loc, NULL,
                            NULL, NULL, NULL);
            }
            break;
        case TYPE_PLUME:
            if (plume_emitters_size < 16)
            {
                plume_emitter& plume = *alloc_plume_emitter();
                plume = plan.plume.initial;
                plume.center = loc;
            }
            break;
        case TYPE_BUBBLE:
            if (bubble_emitters_size < 16)
            {
                bubble_emitter& bubble = *alloc_bubble_emitter();
                bubble = plan.bubble.initial;
                bubble.center = loc;
            }
            break;
        case TYPE_CHAIN:
            if (chain_emitters_size < 32)
            {
                chain_emitter& emitter = *alloc_chain_emitter();
                emitter.chain = &plan.chain;
                emitter.loc = loc;
                emitter.time = 0.0f;
                emitter.next_link = 0;
            }
            break;
        default:
            xASSERT_PLACEHOLDER();
            break;
        }
    }

    S32 update_mist_particles(U8* mem, S32 count, ptank_pool& apool, F32 dt, void*)
    {
        ptank_pool__pos_color_size& pool = (ptank_pool__pos_color_size&)apool;
        mist_particle* p = (mist_particle*)mem;
        mist_particle* end = p + count;

        while (p != end)
        {
            p->age += p->age_rate * dt;
            F32 t = p->age;

            if (t >= 1.0f)
            {
                *p = *--end;
                continue;
            }

            F32 dv = p->y_accel * dt;
            p->loc.y += dt * (p->y_vel + 0.5f * dv);
            p->y_vel += dv;
            p->size += p->size_vel * dt;

            // GC-only frustum cull (absent from the PS2 build)
            RwSphere worldsph;
            worldsph.center.x = p->loc.x;
            worldsph.center.y = p->loc.y;
            worldsph.center.z = p->loc.z;
            worldsph.radius = p->size;

            if (RwCameraFrustumTestSphere(xglobals->screen->icam, &worldsph))
            {
                pool.next();

                if (!pool.valid())
                {
                    end = p;
                    break;
                }

                *pool.color = p->color;
                pool.color->a = p->alpha * (1.0f - t);
                *pool.pos = p->loc;
                pool.size->assign(p->size);
            }

            ++p;
        }

        return end - (mist_particle*)mem;
    }

    S32 update_ring_particles(U8* mem, S32 count, ptank_pool& apool, F32 dt, void*)
    {
        ptank_pool__color_mat& pool = (ptank_pool__color_mat&)apool;
        ring_particle* p = (ring_particle*)mem;
        ring_particle* end = p + count;

        while (p != end)
        {
            p->age += p->age_rate * dt;
            F32 t = p->age;

            if (t >= 1.0f)
            {
                *p = *--end;
                continue;
            }

            p->size += p->size_vel * dt;

            // GC-only frustum cull (absent from the PS2 build)
            RwSphere worldsph;
            worldsph.center.x = p->loc.x;
            worldsph.center.y = p->loc.y;
            worldsph.center.z = p->loc.z;
            worldsph.radius = p->size;

            if (RwCameraFrustumTestSphere(xglobals->screen->icam, &worldsph))
            {
                pool.next();

                if (!pool.valid())
                {
                    end = p;
                    break;
                }

                *pool.color = p->color;

                if (p->fade_in && t < 0.2f)
                {
                    pool.color->a = p->alpha * (t / 0.2f);
                }
                else
                {
                    pool.color->a = p->alpha * (1.0f - t);
                }

                F32 rx = p->rx * p->size;
                F32 rz = p->rz * p->size;
                pool.mat->right.assign(rx, 0.0f, rz);
                pool.mat->up.assign(-rz, 0.0f, rx);
                pool.mat->pos = p->loc;
            }

            ++p;
        }

        return end - (ring_particle*)mem;
    }

    S32 update_streak_particles(U8* mem, S32 count, ptank_pool& apool, F32 dt, void*)
    {
        ptank_pool__color_mat& pool = (ptank_pool__color_mat&)apool;
        const xMat4x3& cam_mat =
            *(xMat4x3*)RwFrameGetMatrix(RwCameraGetFrame(globals.screen->icam));
        F32 ax = cam_mat.at.x;
        F32 ay = cam_mat.at.y;
        F32 az = cam_mat.at.z;
        streak_particle* p = (streak_particle*)mem;
        streak_particle* end = p + count;

        while (p != end)
        {
            p->age += p->age_rate * dt;
            F32 t = p->age;

            if (t >= 1.0f)
            {
                *p = *--end;
                continue;
            }

            p->vel.y += p->y_accel * dt;
            p->loc.x += p->vel.x * dt;
            p->loc.y += p->vel.y * dt;
            p->loc.z += p->vel.z * dt;
            p->length += p->length_vel * dt;

            // GC: RenderWare frustum test (the PS2 build tested the sphere on VU0)
            RwSphere worldsph;
            worldsph.center.x = p->loc.x;
            worldsph.center.y = p->loc.y;
            worldsph.center.z = p->loc.z;
            worldsph.radius = p->width;

            if (RwCameraFrustumTestSphere(xglobals->screen->icam, &worldsph))
            {
                pool.next();

                if (!pool.valid())
                {
                    end = p;
                    break;
                }

                if (p->length > p->length_max)
                {
                    p->length = p->length_max;
                }

                *pool.color = p->color;
                pool.color->a = p->alpha * (1.0f - t);

                pool.mat->right.x = p->vel.y * az - ay * p->vel.z;
                pool.mat->right.y = p->vel.z * ax - az * p->vel.x;
                pool.mat->right.z = p->vel.x * ay - ax * p->vel.y;

                F32 mag = pool.mat->right.length();
                pool.mat->right *= (mag == 0.0f) ? 0.0f : p->width / mag;

                mag = p->vel.length();

                if (mag == 0.0f)
                {
                    pool.mat->up.assign(0.0f, 0.0f, 0.0f);
                }
                else
                {
                    mag = p->length / mag;
                    pool.mat->up.assign(p->vel.x * mag, p->vel.y * mag, p->vel.z * mag);
                }

                pool.mat->pos.x = p->loc.x - 0.5f * pool.mat->up.x;
                pool.mat->pos.y = p->loc.y - 0.5f * pool.mat->up.y;
                pool.mat->pos.z = p->loc.z - 0.5f * pool.mat->up.z;
            }

            ++p;
        }

        return end - (streak_particle*)mem;
    }

    // Declared inline like emit_splash_streak/ring, but too big to inline, so it stays out of line.
    inline void emit_splash_mist(splash_emitter& splash, F32 r0, F32 y0, F32 dt)
    {
        splash.mist.emitted += splash.mist.rate * dt;
        S32 emit = (S32)splash.mist.emitted;

        if (emit <= 0)
        {
            return;
        }

        splash.mist.emitted -= emit;

        F32 r1 = splash.radius + splash.dxz;
        F32 y1 = splash.dy;
        F32 dr = r1 - r0;
        F32 dy = y1 - y0;
        F32 cx = splash.center.x;
        F32 cy = splash.center.y + y0;
        F32 cz = splash.center.z;
        F32 size_start = splash.mist.size_start;
        F32 size_delta = splash.mist.size_delta;
        F32 A = splash.mist.y_accel;
        F32 iA = 1.0f / A;
        F32 V0;

        if (splash.dy_vel <= 0.0f)
        {
            V0 = 0.0f;
        }
        else if (splash.dy_vel > splash.mist.y_vel)
        {
            V0 = splash.mist.y_vel;
        }
        else
        {
            V0 = splash.dy_vel;
        }

        while (emit > 0)
        {
            U8* mem;
            S32 have = xParticleBatchEmit(mist_system_id, emit, &mem);

            if (!have)
            {
                break;
            }

            mist_particle* p = (mist_particle*)mem;
            mist_particle* endp = p + have;

            while (p != endp)
            {
                F32 ang = 2 * PI * xurand();
                F32 ca = icos(ang);
                F32 sa = isin(ang);
                F32 t = xurand();
                F32 r = r0 + dr * t;

                p->loc.x = cx + r * ca;
                p->loc.y = cy + dy * t;
                p->loc.z = cz + r * sa;
                p->size = size_start + size_delta * xurand();
                p->size_vel = splash.mist.size_vel * p->size;

                F32 Yc = p->loc.y + 0.5f * p->size;
                F32 Vc = V0 + 0.5f * p->size_vel;
                F32 det = Vc * Vc - 2.0f * A * (Yc - cy);

                if (det < 0.0f)
                {
                    continue;
                }

                F32 max_age = iA * (-Vc - xsqrt(det));

                if (max_age < 1.0f && max_age > 0.001f)
                {
                    p->age_rate = 1.0f / max_age;
                }
                else
                {
                    p->age_rate = splash.mist.age_rate;
                }

                p->age = 0.0f;
                p->y_vel = V0;
                p->y_accel = A;
                p->color = splash.mist.color;
                p->alpha = 1.0039216f * splash.mist.color.a - 0.01f;
                ++p;
            }

            emit -= have;
        }
    }

    // Not present in the GC symbols (always inlined).
    inline void emit_splash_streak(splash_emitter& splash, F32 dt)
    {
        splash.streak.emitted += splash.streak.rate * dt;
        S32 emit = (S32)splash.streak.emitted;

        if (emit <= 0)
        {
            return;
        }

        splash.streak.emitted -= emit;

        F32 cx = splash.center.x;
        F32 cy = splash.center.y;
        F32 cz = splash.center.z;

        while (emit > 0)
        {
            U8* mem;
            S32 have = xParticleBatchEmit(streak_system_id, emit, &mem);

            if (!have)
            {
                break;
            }

            streak_particle* p = (streak_particle*)mem;
            streak_particle* endp = p + have;

            while (p != endp)
            {
                F32 ang = 2 * PI * xurand();
                F32 ca = icos(ang);
                F32 sa = isin(ang);

                p->loc.assign(cx + splash.radius * ca, cy, cz + splash.radius * sa);

                F32 vel = splash.streak.vel_min + splash.streak.vel_delta * xurand();

                p->vel.assign(vel * (splash.dxz * ca), splash.dy * vel, vel * (splash.dxz * sa));
                p->width = splash.streak.width;
                p->age = 0.0f;
                p->age_rate = splash.streak.age_rate_min + splash.streak.age_rate_delta * xurand();
                p->length = 0.0f;
                p->length_max = splash.streak.length;
                p->length_vel = vel;
                p->y_accel = splash.streak.y_accel;
                p->color = splash.streak.color;
                p->alpha = 1.0039216f * splash.streak.color.a - 0.01f;
                ++p;
            }

            emit -= have;
        }
    }

    // Not present in the GC symbols (always inlined).
    inline void emit_splash_ring(splash_emitter& splash, F32 r0, F32 y0, F32 dt)
    {
        splash.ring.emitted += splash.ring.rate * dt;
        S32 emit = (S32)splash.ring.emitted;

        if (emit <= 0)
        {
            return;
        }

        splash.ring.emitted -= emit;

        F32 r1 = splash.radius + splash.dxz;
        F32 dr = r1 - r0;
        F32 cx = splash.center.x;
        F32 cy = splash.center.y;
        F32 cz = splash.center.z;

        while (emit > 0)
        {
            U8* mem;
            S32 have = xParticleBatchEmit(ring_system_id, emit, &mem);

            if (!have)
            {
                break;
            }

            ring_particle* p = (ring_particle*)mem;
            ring_particle* endp = p + have;

            while (p != endp)
            {
                F32 ang = 2 * PI * xurand();
                F32 ca = icos(ang);
                F32 sa = isin(ang);
                F32 r = r0 + dr * xurand();

                p->loc.x = cx + r * ca;
                p->loc.y = cy;
                p->loc.z = cz + r * sa;
                p->age = 0.0f;
                p->age_rate = splash.ring.age_rate_min + splash.ring.age_rate_delta * xurand();
                p->size = 0.0f;
                p->size_vel =
                    p->age_rate * (splash.ring.size_min + splash.ring.size_delta * xurand());
                p->color = splash.ring.color;
                p->alpha = 1.0039216f * splash.ring.color.a - 0.01f;

                F32 rot = 2 * PI * xurand();
                p->rx = icos(rot);
                p->rz = isin(rot);
                p->fade_in = 0;
                ++p;
            }

            emit -= have;
        }
    }

    void update_splash_emitters(F32 dt)
    {
        splash_emitter* e = splash_emitters;
        splash_emitter* ende = e + splash_emitters_size;

        while (e != ende)
        {
            e->age += e->age_rate * dt;

            if (e->age >= 1.0f)
            {
                --ende;
                --splash_emitters_size;
                *e = splash_emitters[splash_emitters_size];
                continue;
            }

            F32 r0 = e->radius + e->dxz;
            F32 y0 = e->dy;

            e->radius += e->radius_vel * dt;
            e->dxz += e->dxz_vel * dt;

            F32 dyv = e->dy_accel * dt;
            e->dy += 0.5f * dyv * dt + e->dy_vel * dt;
            e->dy_vel += dyv;

            if (e->dy < 0.0f)
            {
                e->dy = 0.0f;
            }

            emit_splash_mist(*e, r0, y0, dt);

            if (e->age >= e->ring.emit_begin && e->age <= e->ring.emit_end)
            {
                emit_splash_ring(*e, r0, y0, dt);
            }

            if (e->age <= e->streak.emit_end)
            {
                emit_splash_streak(*e, dt);
            }

            if (e->age >= e->mist.emit_end)
            {
                e->mist.rate = 0.0f;
            }

            ++e;
        }
    }

    S32 update_plume_particles(U8* mem, S32 count, ptank_pool& apool, F32 dt, void*)
    {
        ptank_pool__pos_color_size& pool = (ptank_pool__pos_color_size&)apool;
        plume_particle* p = (plume_particle*)mem;
        plume_particle* end = p + count;

        while (p != end)
        {
            p->age += p->age_rate * dt;
            F32 t1 = p->age;

            if (t1 >= 1.0f)
            {
                *p = *--end;
                continue;
            }

            p->vel.y += p->y_accel * dt;
            p->loc.x += p->vel.x * dt;
            p->loc.y += p->vel.y * dt;
            p->loc.z += p->vel.z * dt;
            p->size += p->size_vel * dt;

            // GC: RenderWare frustum test (the PS2 build tested the sphere on VU0)
            RwSphere worldsph;
            worldsph.center.x = p->loc.x;
            worldsph.center.y = p->loc.y;
            worldsph.center.z = p->loc.z;
            worldsph.radius = p->size;

            if (RwCameraFrustumTestSphere(xglobals->screen->icam, &worldsph))
            {
                pool.next();

                if (!pool.valid())
                {
                    end = p;
                    break;
                }

                *pool.color = p->color;

                F32 t2 = t1 * t1;
                F32 alpha = alpha_coeff1 * t1 + alpha_coeff2 * t2 + alpha_coeff3 * (t1 * t2);

                if (alpha < 1.0f)
                {
                    pool.color->a = alpha * p->color.a + 0.5f;
                }

                *pool.pos = p->loc;
                pool.size->assign(p->size);
            }

            ++p;
        }

        return end - (plume_particle*)mem;
    }

    void update_plume_emitters(F32 dt)
    {
        plume_emitter* e = plume_emitters;
        plume_emitter* ende = e + plume_emitters_size;

        while (e != ende)
        {
            e->emit_time += dt;

            if (e->emit_time >= e->emit_end)
            {
                --ende;
                --plume_emitters_size;
                *e = plume_emitters[plume_emitters_size];
                continue;
            }

            e->emitted += e->rate * dt;
            S32 emit = (S32)e->emitted;

            // e only advances once particles were emitted (same in the PS2 build).
            if (emit <= 0)
            {
                continue;
            }

            e->emitted -= emit;

            F32 cx = e->center.x;
            F32 cy = e->center.y;
            F32 cz = e->center.z;

            while (emit > 0)
            {
                U8* mem;
                S32 have = xParticleBatchEmit(plume_system_id, emit, &mem);

                if (!have)
                {
                    break;
                }

                plume_particle* p = (plume_particle*)mem;
                plume_particle* endp = p + have;

                while (p != endp)
                {
                    F32 ang = 2 * PI * xurand();
                    F32 ca = icos(ang);
                    F32 sa = isin(ang);
                    F32 rr = e->radius * xsqrt(xurand());

                    p->loc.x = cx + rr * ca;
                    p->loc.y = cy;
                    p->loc.z = cz + rr * sa;

                    F32 vel_mag = e->vel_min + e->vel_delta * (xurand() - 0.5f);
                    F32 vel_pitch = e->vel_angle * (xurand() - 0.5f);
                    F32 vel_roll = e->vel_angle * (xurand() - 0.5f);
                    F32 svp = isin(vel_pitch);
                    F32 cvp = icos(vel_pitch);
                    F32 svr = isin(vel_roll);
                    F32 cvr = icos(vel_roll);

                    p->vel.x = svr * (vel_mag * cvp);
                    p->vel.y = cvr * (vel_mag * cvp);
                    p->vel.z = vel_mag * -svp;
                    p->age = 0.0f;
                    p->age_rate = e->age_rate_min + e->age_rate_delta * xurand();
                    p->y_accel = e->y_accel;
                    p->color = e->color;
                    p->size = e->size_min + e->size_delta * xurand();

                    F32 end_size = p->size * e->size_scale;
                    p->size_vel = p->age_rate * (end_size - p->size);
                    ++p;
                }

                emit -= have;
            }

            ++e;
        }
    }

    inline void begin_render_cones()
    {
        zRenderState(SDRS_SplashCones);

        RwRaster* raster = !cone_texture ? NULL : RwTextureGetRaster(cone_texture);
        RwRenderStateSet(rwRENDERSTATETEXTURERASTER, raster);

        // GC: the z bias is applied by pushing the camera near plane out
        // (the PS2 build scaled vu1DataXYZShift instead).
        RwCamera* cam = RwCameraGetCurrentCamera();
        old_zbias = !cam ? 0.0f : RwCameraGetNearClipPlane(cam);

        F32 zbias = old_zbias + 5.0f;
        cam = RwCameraGetCurrentCamera();

        if (cam)
        {
            F32 nearPlane = RwCameraGetNearClipPlane(cam);
            F32 farPlane = RwCameraGetFarClipPlane(cam);

            if (zbias != nearPlane)
            {
                RwCameraEndUpdate(cam);
                RwCameraSetNearClipPlane(cam, zbias);
                RwCameraSetFarClipPlane(cam, zbias + farPlane - nearPlane);
                RwCameraBeginUpdate(cam);
            }
        }
    }

    inline void end_render_cones()
    {
        F32 zbias = old_zbias;
        RwCamera* cam = RwCameraGetCurrentCamera();

        if (cam)
        {
            F32 nearPlane = RwCameraGetNearClipPlane(cam);
            F32 farPlane = RwCameraGetFarClipPlane(cam);

            if (zbias != nearPlane)
            {
                RwCameraEndUpdate(cam);
                RwCameraSetNearClipPlane(cam, zbias);
                RwCameraSetFarClipPlane(cam, zbias + farPlane - nearPlane);
                RwCameraBeginUpdate(cam);
            }
        }
    }

    void render_cones()
    {
        if (splash_emitters_size <= 0)
        {
            return;
        }

        begin_render_cones();

        xIMLockContext lock;
        xIMLock(lock, XIM_TRISTRIP, splash_emitters_size * 32 + (splash_emitters_size - 1) * 2);

        xVec3* pos = lock.pos;
        xVec2* uv = lock.uv;
        U32* color = (U32*)lock.color;
        F32 du = (1.0f / 15.0f) * cone_texture_repeats;

        const splash_emitter* splash = splash_emitters;
        const splash_emitter* end_splash = splash + splash_emitters_size;

        for (; splash != end_splash; ++splash)
        {
            F32 cx = splash->center.x;
            F32 cy = splash->center.y;
            F32 cz = splash->center.z;
            F32 radius = splash->radius;
            F32 dxz = splash->dxz;
            F32 dy = splash->dy;
            // xColor_tag packed as a big-endian U32 (r,g,b,a): alpha in the low byte
            U32 splash_color =
                0xffffff00 + (U32)(splash->alpha_start + splash->alpha_delta * splash->age);
            F32 u = splash->u;
            F32 s = 0.0f;
            F32 c = 1.0f;

            if (splash != splash_emitters)
            {
                pos->x = cx;
                pos->y = cy;
                pos->z = cz + radius;
                ++pos;
                ++uv;
                ++color;
            }

            F32 Ax, Ay, Az;
            F32 Bz;
            xVec3* const end_pos = pos + 30;

            while (pos != end_pos)
            {
                Ax = cx + radius * s;
                Ay = cy;
                Az = cz + radius * c;
                Bz = Az + dxz * c;

                pos[0].x = Ax;
                pos[0].y = Ay;
                pos[0].z = Az;
                uv[0].x = 0.0f;
                uv[0].y = u;
                color[0] = splash_color;

                pos[1].x = Ax + dxz * s;
                pos[1].y = cy + dy;
                pos[1].z = Bz;
                uv[1].x = 1.0f;
                uv[1].y = u;
                color[1] = splash_color;

                pos += 2;
                uv += 2;
                color += 2;
                u += du;

                F32 tempc = c;
                c = 0.9135454f * c - 0.40673664f * s;
                s = 0.9135454f * s + 0.40673664f * tempc;
            }

            Az = cz + radius;
            Bz = Az + dxz;

            pos[0].x = cx;
            pos[0].y = cy;
            pos[0].z = Az;
            uv[0].x = 0.0f;
            uv[0].y = u;
            color[0] = splash_color;

            pos[1].x = cx;
            pos[1].y = cy + dy;
            pos[1].z = Bz;
            uv[1].x = 1.0f;
            uv[1].y = u;
            color[1] = splash_color;

            pos += 2;
            uv += 2;
            color += 2;

            if (splash + 1 != end_splash)
            {
                pos->x = cx;
                pos->y = cy + dy;
                pos->z = Bz;
                ++pos;
                ++uv;
                ++color;
            }
        }

        xIMRender(lock, NULL);
        xIMUnlock(lock);

        end_render_cones();
    }

    // Dead-stripped placeholders, not recovered code: retail's .sdata2 keeps these literals from
    // the stripped startup code that sat between render_cones and zSplashSceneEnter. Two
    // functions, since a function's conversion doubles follow its float literals. Bodies unknown.
    static void __deadstripped2_zSplash(F32* consts)
    {
        consts[0] = 0.01f;
        consts[1] = 0.2f;
        consts[2] = 2.0f;
    }

    static void __deadstripped_zSplash(F32* consts, S32 i, U32 u)
    {
        consts[0] = 6.2831855f;
        consts[1] = 2.3283064e-10f;
        consts[2] = 0.5f;
        consts[3] = 3.0f;
        consts[4] = 0.00001f;
        consts[5] = 100000.0f;
        consts[6] = i;
        consts[7] = u;
    }
} // namespace

namespace
{
    // Not present in the GC symbols (always inlined).
    inline void refresh_system(S32& system_id, ptank_group_type pool_type, U32 texture_id,
                               S32 stride, S32 (*update)(U8*, S32, ptank_pool&, F32, void*))
    {
        xParticleBatchSystem system;
        system.rs.src_blend = rwBLENDSRCALPHA;
        system.rs.dst_blend = rwBLENDONE;
        system.rs.flags = 0;
        system.pool_type = pool_type;
        system.rs.texture = !texture_id ? NULL : (RwTexture*)xSTFindAsset(texture_id, NULL);
        system.stride = stride;
        system.update = update;
        system_id = xParticleBatchAddSystem(system);
    }

    // Not present in the GC symbols (always inlined).
    inline void refresh_textures()
    {
        cone_texture = !texture_ids[0] ? NULL : (RwTexture*)xSTFindAsset(texture_ids[0], NULL);

        refresh_system(mist_system_id, PGT_POS_COLOR_SIZE, texture_ids[1], sizeof(mist_particle),
                       update_mist_particles);
        refresh_system(ring_system_id, PGT_COLOR_MAT, texture_ids[2], sizeof(ring_particle),
                       update_ring_particles);
        refresh_system(streak_system_id, PGT_COLOR_MAT, texture_ids[3], sizeof(streak_particle),
                       update_streak_particles);
        refresh_system(plume_system_id, PGT_POS_COLOR_SIZE, texture_ids[4], sizeof(plume_particle),
                       update_plume_particles);
        refresh_system(trail_system_id, PGT_COLOR_MAT, texture_ids[6], sizeof(ring_particle),
                       update_ring_particles);
        refresh_system(drips_system_id, PGT_POS_COLOR_SIZE, texture_ids[7], sizeof(plume_particle),
                       update_plume_particles);
    }
} // namespace

void zSplashSceneEnter()
{
    splash_emitters_size = 0;
    plume_emitters_size = 0;
    bubble_emitters_size = 0;
    chain_emitters_size = 0;
    mist_system_id = -1;
    ring_system_id = -1;
    streak_system_id = -1;
    plume_system_id = -1;
    trail_system_id = -1;
    drips_system_id = -1;

    refresh_textures();

    if (cone_texture)
    {
        splash_emitters = (splash_emitter*)xMemAlloc(gActiveHeap, 64 * sizeof(splash_emitter), 0);
        plume_emitters = (plume_emitter*)xMemAlloc(gActiveHeap, 16 * sizeof(plume_emitter), 0);
        bubble_emitters = (bubble_emitter*)xMemAlloc(gActiveHeap, 16 * sizeof(bubble_emitter), 0);
        chain_emitters = (chain_emitter*)xMemAlloc(gActiveHeap, 32 * sizeof(chain_emitter), 0);
    }
    else
    {
        splash_emitters = NULL;
    }

    splash_planner* plan = planners;
    splash_planner* end_plan = plan + types_size;

    for (; plan != end_plan; ++plan)
    {
        switch (plan->type)
        {
        case TYPE_SPLASH:
            refresh_splash(plan->splash);
            break;
        case TYPE_PLUME:
            refresh_plume(plan->plume);
            break;
        case TYPE_BUBBLE:
            break;
        case TYPE_CHAIN:
            refresh_chain(plan->chain);
            break;
        }
    }
}

void zSplashSceneExit()
{
}

void zSplashUpdate(F32 dt)
{
    if (!splash_emitters)
    {
        return;
    }

    update_chain_emitters(dt);
    update_splash_emitters(dt);
    update_plume_emitters(dt);
    update_bubble_emitters(dt);
}

void zSplashRender()
{
    if (!splash_emitters)
    {
        return;
    }

    render_cones();
}
