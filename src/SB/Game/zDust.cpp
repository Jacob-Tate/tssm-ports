#include "zDust.h"

#include <types.h>
#include <rwcore.h>
#include <rpptank.h>

#include "xMath.h"
#include "xMathInlines.h"
#include "xMath2.h"
#include "xVec3.h"
#include "xMemMgr.h"
#include "xstransvc.h"
#include "xRand.h"
#include "xPtankPool.h"
#include "xParticleBatch.h"
#include "xGlobals.h"
#include "zGlobals.h"

namespace
{
    struct dust_particle
    {
        xVec3 loc;
        F32 size;
        xVec3 vel;
        F32 rot;
        F32 age;
        F32 age_rate;
        F32 dsize;
        xColor_tag color;
        F32 rot_vel;
        xVec2* uv;
        F32 pad1;
        F32 pad2;
    };

    struct ring_chain_data
    {
        F32 emit_rate;
        S32 type;
        S32 segments;
        F32 rot_vel;
        F32 rot_mag;
        F32 dcos;
        F32 dsin;
    };

    namespace tweak
    {
        namespace ring
        {
            const ring_chain_data chain[2] = {
                { 0.4f, 0, 8, 1.0f, 1.0f, 0.70710677f, 0.70710677f },
                { 0.6f, 2, 6, 1.5f, 0.3f, 0.70710677f, 0.70710677f },
            };
        } // namespace ring

        namespace dirt
        {
            S32 texture_rows = 2;
            S32 texture_columns = 2;
        } // namespace dirt
    } // namespace tweak

    // Dust alpha fade cubic; constants here, as retail has no data for them.
    const F32 alpha_coeff1 = 17.77778f;
    const F32 alpha_coeff2 = -30.66667f;
    const F32 alpha_coeff3 = 12.88889f;

    S32 dirt_system_id;
    F32 ring_vel_decay;
    xVec2* dirt_uvs;
    bool refreshed_ring_chains;
    F32 ring_chain_ang[2];
    xVec2 ring_chain[2][8];

    S32 update_dust_particles_uv2(U8* mem, S32 count, ptank_pool& apool, F32 dt, void*);
    S32 update_dust_particles(U8* mem, S32 count, ptank_pool& apool, F32 dt, void*);

    inline void set_uv_frames(xVec2* uv, S32 rows, S32 columns)
    {
        F32 du = 1.0f / columns;
        F32 dv = 1.0f / rows;
        F32 v = 0.0f;

        for (S32 row = 0; row < rows; row++)
        {
            F32 u = 0.0f;

            for (S32 col = columns; col > 0; col--)
            {
                uv[0].x = u;
                uv[0].y = v;
                uv[1].x = u + du;
                uv[1].y = v + dv;
                uv += 2;
                u += du;
            }

            v += dv;
        }
    }

    inline void create_dirt_system()
    {
        S32 uvpairs = tweak::dirt::texture_rows * tweak::dirt::texture_columns;
        dirt_uvs = (xVec2*)xMemAlloc(gActiveHeap, uvpairs * sizeof(xVec2) * 2, 0);
        set_uv_frames(dirt_uvs, tweak::dirt::texture_rows, tweak::dirt::texture_columns);

        RwTexture* dirt_texture = (RwTexture*)xSTFindAsset(0x3DC63096, NULL);

        xParticleBatchSystem system;
        system.pool_type = uvpairs == 1 ? PGT_POS_COLOR_SIZE_ROT : PGT_POS_COLOR_SIZE_ROT_UV2;
        system.rs.texture = dirt_texture;
        system.rs.src_blend = rwBLENDSRCALPHA;
        system.rs.dst_blend = rwBLENDINVSRCALPHA;
        system.rs.flags = 0;
        system.order_group = 0;
        system.order_index = 0;
        system.stride = sizeof(dust_particle);
        system.update = uvpairs == 1 ? update_dust_particles : update_dust_particles_uv2;
        system.context = NULL;
        dirt_system_id = xParticleBatchAddSystem(system);
    }
} // namespace

S32 zDustRingEmitter::activities_used;
zDustRingEmitter::activity_data* zDustRingEmitter::activities;

namespace
{
    // Not in the PS2 DWARF (always inlined); name and parameters from Ratatouille. Fills the chain
    // with successive rotations of (x, z) by the step (dcos, dsin).
    inline void move_ring_chain(xVec2* chain, xVec2* end_chain, F32 x, F32 z, F32 dcos, F32 dsin)
    {
        chain->assign(x, z);

        for (++chain; chain != end_chain; ++chain)
        {
            chain->x = dcos * x - dsin * z;
            z = dcos * z + dsin * x;
            x = chain->x;
            chain->y = z;
        }
    }

    // Inlined into kickup on GC (out of line in the PS2 demo).
    inline void refresh_ring_chains()
    {
        if (refreshed_ring_chains)
        {
            return;
        }

        refreshed_ring_chains = true;
        F32 dt = globals.update_dt;

        for (S32 i = 0; i < 2; i++)
        {
            const ring_chain_data& data = tweak::ring::chain[i];

            if (data.type == 0)
            {
                continue;
            }

            ring_chain_ang[i] = xrmod(ring_chain_ang[i] + data.rot_vel * dt);

            F32 ang;
            if (data.type == 1)
            {
                ang = ring_chain_ang[i];
            }
            else
            {
                ang = data.rot_mag * isin(ring_chain_ang[i]);
            }

            move_ring_chain(ring_chain[i], ring_chain[i] + data.segments, icos(ang), isin(ang),
                            data.dcos, data.dsin);
        }
    }

    // Not in the PS2 DWARF (always inlined); the name is a guess.
    inline void reset_ring_chains()
    {
        for (S32 i = 0; i < 2; i++)
        {
            ring_chain_ang[i] = 2.0f * PI / 4294967296.0f * xrand_RandomInt32();
        }

        refreshed_ring_chains = false;
    }

    // Not in the PS2 DWARF (always inlined); names from Ratatouille. Picks a random direction: any
    // angle for a type 0 chain, else a chain segment.
    inline void get_ring_dir(F32& dx, F32& dz, const ring_chain_data& data, const xVec2* chain)
    {
        if (data.type == 0)
        {
            F32 rand_ang = (2.0f * PI / 4294967296.0f) * xrand_RandomInt32() - PI;
            dx = icos(rand_ang);
            dz = isin(rand_ang);
        }
        else
        {
            S32 seg = xrand_RandomInt32() % data.segments;
            dx = chain[seg].x;
            dz = chain[seg].y;
        }
    }
} // namespace

namespace
{
    // The color is copied as one 32-bit word, not as a struct, as in retail and the PS2 demo.
    S32 update_dust_particles(U8* mem, S32 count, ptank_pool& apool, F32 dt, void*)
    {
        ptank_pool__pos_color_size_rot& pool = (ptank_pool__pos_color_size_rot&)apool;
        F32 vel_decay = ring_vel_decay;
        dust_particle* p = (dust_particle*)mem;
        dust_particle* end = p + count;

        while (p != end)
        {
            p->age += p->age_rate * dt;

            if (p->age >= 1.0f)
            {
                --end;
                *p = *end;
                continue;
            }

            p->size += p->dsize * dt;
            F32 hsize = 0.5f * p->size;
            p->vel *= vel_decay;
            p->loc.x += p->vel.x * dt;
            p->loc.y += p->vel.y * dt + hsize;
            p->loc.z += p->vel.z * dt;
            p->rot += dt * (p->rot_vel * (1.0f - p->age));

            if (p->rot > PI)
            {
                p->rot -= 2.0f * PI;
            }
            else if (p->rot < -PI)
            {
                p->rot += 2.0f * PI;
            }

            RwSphere worldsph;
            worldsph.center.x = p->loc.x;
            worldsph.center.y = p->loc.y;
            worldsph.center.z = p->loc.z;
            worldsph.radius = p->size;

            if (RwCameraFrustumTestSphere(xglobals->screen->icam, &worldsph) != rwSPHEREOUTSIDE)
            {
                pool.next();

                if (!pool.valid())
                {
                    end = p;
                    break;
                }

                F32 t1 = p->age;
                F32 t2 = t1 * t1;
                F32 alpha = alpha_coeff1 * t1 + alpha_coeff2 * t2 + alpha_coeff3 * (t1 * t2);

                *(U32*)pool.color = *(U32*)&p->color;

                if (alpha < 0.0f)
                {
                    pool.color->a = 0;
                }
                else if (alpha < 1.0f)
                {
                    pool.color->a = (U8)(alpha * p->color.a + 0.5f);
                }

                *pool.pos = p->loc;
                pool.size->assign(p->size);
                *pool.rot = p->rot;
            }

            p->loc.y -= hsize;
            ++p;
        }

        return end - (dust_particle*)mem;
    }

    // Same 32-bit color copy as update_dust_particles.
    S32 update_dust_particles_uv2(U8* mem, S32 count, ptank_pool& apool, F32 dt, void*)
    {
        ptank_pool__pos_color_size_rot_uv2& pool = (ptank_pool__pos_color_size_rot_uv2&)apool;
        F32 vel_decay = ring_vel_decay;
        dust_particle* p = (dust_particle*)mem;
        dust_particle* end = p + count;

        while (p != end)
        {
            p->age += p->age_rate * dt;

            if (p->age >= 1.0f)
            {
                --end;
                *p = *end;
                continue;
            }

            p->size += p->dsize * dt;
            F32 hsize = 0.5f * p->size;
            p->vel *= vel_decay;
            p->loc.x += p->vel.x * dt;
            p->loc.y += p->vel.y * dt + hsize;
            p->loc.z += p->vel.z * dt;
            p->rot += dt * (p->rot_vel * (1.0f - p->age));

            if (p->rot > PI)
            {
                p->rot -= 2.0f * PI;
            }
            else if (p->rot < -PI)
            {
                p->rot += 2.0f * PI;
            }

            RwSphere worldsph;
            worldsph.center.x = p->loc.x;
            worldsph.center.y = p->loc.y;
            worldsph.center.z = p->loc.z;
            worldsph.radius = p->size;

            if (RwCameraFrustumTestSphere(xglobals->screen->icam, &worldsph) != rwSPHEREOUTSIDE)
            {
                pool.next();

                if (!pool.valid())
                {
                    end = p;
                    break;
                }

                F32 t1 = p->age;
                F32 t2 = t1 * t1;
                F32 alpha = alpha_coeff1 * t1 + alpha_coeff2 * t2 + alpha_coeff3 * (t1 * t2);

                *(U32*)pool.color = *(U32*)&p->color;

                if (alpha < 0.0f)
                {
                    pool.color->a = 0;
                }
                else if (alpha < 1.0f)
                {
                    pool.color->a = (U8)(alpha * p->color.a + 0.5f);
                }

                *pool.pos = p->loc;
                pool.size->assign(p->size);
                *pool.rot = p->rot;
                pool.uv[0] = p->uv[0];
                pool.uv[1] = p->uv[1];
            }

            p->loc.y -= hsize;
            ++p;
        }

        return end - (dust_particle*)mem;
    }
} // namespace

void zDustSceneEnter()
{
    create_dirt_system();
    zDustRingEmitter::scene_enter();
    zDustReset();
}

void zDustReset()
{
    reset_ring_chains();
    zDustRingEmitter::reset_all();
}

void zDustUpdate(F32 dt)
{
    zDustRingEmitter::update_all(dt);
}

void zDustRender()
{
    refreshed_ring_chains = false;
}

void zDustRingEmitter::create()
{
    data = NULL;
}

void zDustRingEmitter::set_config(const config_type* cfg)
{
    this->cfg = cfg;

    if (data != NULL)
    {
        refresh_activity();
    }
}

bool zDustRingEmitter::activate()
{
    if (data != NULL)
    {
        return true;
    }

    if (activities_used >= 8)
    {
        return false;
    }

    S32 i = activities_used++;
    data = &activities[i];
    data->owner = this;
    data->emitted[0] = 0.0f;
    data->emitted[1] = 0.0f;
    refresh_activity();
    return true;
}

void zDustRingEmitter::deactivate()
{
    if (data == NULL)
    {
        return;
    }

    stop_activity(data);
    data = NULL;
}

// size_max, emit_rate and rval are not in the PS2 DWARF (names from Ratatouille), but retail
// needs them as locals.
void zDustRingEmitter::kickup(const xVec3& center, F32 force, F32 dt)
{
    refresh_ring_chains();

    F32 cx = center.x;
    F32 cy = center.y;
    F32 cz = center.z;
    F32 radius_min = cfg->radius_inner;
    F32 radius_delta = cfg->radius_outer - radius_min;
    F32 size_min = 0.3f * force;
    F32 size_max = 0.4f * force;
    F32 size_delta = (1.0f / 65535.0f) * (size_max - size_min);
    F32 vmag_min = 5.0f * force;
    F32 vmag_delta = (2.0f / 4294967296.0f) * force;
    F32 intensity_min = data->intensity_min;
    F32 intensity_delta = data->intensity_delta;
    U8 alpha = data->alpha;
    S32 dirt_frames = tweak::dirt::texture_rows * tweak::dirt::texture_columns;

    for (S32 i = 0; i < 2; i++)
    {
        F32 emit_rate = cfg->rate_scale * (500.0f * tweak::ring::chain[i].emit_rate);
        data->emitted[i] += dt * (emit_rate * force);
        S32 emit = (S32)data->emitted[i];

        if (emit <= 0)
        {
            continue;
        }

        data->emitted[i] -= emit;

        while (emit > 0)
        {
            U8* mem;
            S32 have = xParticleBatchEmit(dirt_system_id, emit, &mem);

            if (have == 0)
            {
                break;
            }

            dust_particle* p = (dust_particle*)mem;
            dust_particle* endp = p + have;

            for (; p != endp; ++p)
            {
                U32 rval = xrand_RandomInt32();
                F32 dx;
                F32 dz;
                get_ring_dir(dx, dz, tweak::ring::chain[i], ring_chain[i]);

                F32 rr = 1.0f - (1.0f / 65535.0f) * (rval >> 16);
                rr *= rr;
                F32 rand_radius = radius_delta * rr + radius_min;
                p->loc.x = rand_radius * dx + cx;
                p->loc.y = cy;
                p->loc.z = rand_radius * dz + cz;

                F32 vmag = vmag_delta * xrand_RandomInt32() + vmag_min;
                p->vel.x = dx * vmag;
                p->vel.y = 0.0f;
                p->vel.z = dz * vmag;

                rval = xrand_RandomInt32();
                p->age = 0.0f;
                // life 1.0 to 1.2: retail's folded constant is (1.2f - 1.0f) / 65535 in float
                p->age_rate = 1.0f / ((1.0f / 65535.0f) * (1.2f - 1.0f) * (rval & 0xFFFF) + 1.0f);
                p->size = (rval >> 16) * size_delta + size_min;
                p->dsize = p->age_rate * (1.5f * p->size);

                rval = xrand_RandomInt32();
                p->color.r = p->color.g = p->color.b =
                    (U8)((rval >> 16) * intensity_delta + intensity_min + 0.5f);
                p->color.a = alpha;
                p->uv = dirt_uvs + 2 * ((S32)(rval & 0xFFFF) % dirt_frames);

                rval = xrand_RandomInt32();
                p->rot = (2.0f * PI / 65535.0f) * (rval & 0xFFFF) - PI;
                p->rot_vel = (8.0f * PI / 65535.0f) * (rval >> 16) + 4.0f;

                if (rval & 0x10000)
                {
                    p->rot_vel = -p->rot_vel;
                }
            }

            emit -= have;
        }
    }
}

// The members below are auto-inlined into all their callers and dead-stripped in retail. They
// follow kickup: retail interns their literals before kickup's (codegen runs in reverse order).
void zDustRingEmitter::refresh_activity()
{
    F32 intensity_min = 255.9f * 0.75f * cfg->intensity;
    if (intensity_min > 255.9f)
    {
        intensity_min = 255.9f;
    }

    F32 intensity_max = 255.9f * cfg->intensity;
    if (intensity_max > 255.9f)
    {
        intensity_max = 255.9f;
    }

    F32 intensity_delta = intensity_max - intensity_min;

    data->intensity_min = intensity_min;
    data->intensity_delta = (1.0f / 4294967296.0f) * intensity_delta;
    data->alpha = (U8)(255.0f * cfg->alpha + 0.5f);
}

void zDustRingEmitter::scene_enter()
{
    activities = (activity_data*)xMemAlloc(gActiveHeap, 8 * sizeof(activity_data), 0);
    activities_used = 0;
}

// Retail bug: the range end is taken after activities_used was cleared, so no owner is
// ever detached.
void zDustRingEmitter::reset_all()
{
    activities_used = 0;

    activity_data* const end_act = activities + activities_used;
    for (activity_data* act = activities; act != end_act; ++act)
    {
        act->owner = NULL;
    }
}

void zDustRingEmitter::update_all(F32 dt)
{
    ring_vel_decay = xexp(-3.0f * dt);
}

// Moves the last active slot into the freed one.
void zDustRingEmitter::stop_activity(activity_data* activity)
{
    activities_used--;
    activity_data* tail_activity = &activities[activities_used];

    if (activity != tail_activity)
    {
        *activity = *tail_activity;
        activity->owner->data = activity;
    }
}
