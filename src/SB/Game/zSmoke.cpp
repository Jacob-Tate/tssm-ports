#include "zSmoke.h"

#include "iMath.h"
#include "xEvent.h"
#include "xMathInlines.h"
#include "xParticleBatch.h"
#include "xRand.h"
#include "xstransvc.h"
#include "xutil.h"

#include "zBase.h"
#include "zGlobals.h"
#include "zScene.h"

namespace
{
    // total size: 0x8
    struct smoke_type
    {
        RwTexture* texture;
        S32 system_id;
    };

    // total size: 0x50
    struct smoke_particle
    {
        xVec3 loc; // 0x0
        F32 size; // 0xC
        xColor_tag color; // 0x10
        xVec3 vel; // 0x14
        xVec2 uv[2]; // 0x20
        F32 age; // 0x30
        F32 age_rate; // 0x34
        F32 growth; // 0x38
        F32 wind; // 0x3C
        F32 dr; // 0x40
        F32 dg; // 0x44
        F32 db; // 0x48
        F32 da; // 0x4C
    };

    struct compare_types
    {
        S32 operator()(RwTexture* const& texture, const smoke_type& type) const
        {
            return texture - type.texture;
        }
    };

    smoke_type types[32];
    S32 types_size;
    F32 wind_magnitude_time;
    F32 wind_yaw_time;
    F32 wind_pitch_time;
    xVec3 wind_delta;

    S32 update_particles(U8* mem, S32 count, ptank_pool& apool, F32 dt, void*);

    inline void update_wind(F32 dt)
    {
        F32 magnitude;
        F32 yaw;
        F32 pitch;
        F32 t;

        wind_magnitude_time += 1.2566371f * dt;
        wind_magnitude_time = xrmod(wind_magnitude_time);
        magnitude = -0.5f + 0.75f * (1.0f + isin(wind_magnitude_time));

        wind_yaw_time += 0.31415927f * dt;
        wind_yaw_time = xrmod(wind_yaw_time);
        yaw = -0.7853982f + 0.7853982f * (1.0f + isin(wind_yaw_time));

        wind_pitch_time += 6.2831855f * dt;
        wind_pitch_time = xrmod(wind_pitch_time);
        pitch = -0.2617994f + 0.2617994f * (1.0f + isin(wind_pitch_time));

        F32 sy = isin(yaw);
        F32 cy = icos(yaw);
        F32 sp = isin(pitch);
        F32 cp = icos(pitch);
        t = magnitude * dt;
        wind_delta.x = cp * (t * sy);
        wind_delta.y = t * -sp;
        wind_delta.z = cp * (t * cy);
    }

    inline smoke_type* add_type(RwTexture* texture, bool alt_order)
    {
        // Appended without re-sorting even though get_type binary-searches the table (retail behavior)
        smoke_type* t = &types[types_size];
        types_size++;

        xParticleBatchSystem system;
        system.pool_type = PGT_POS_COLOR_SIZE_UV2;
        system.rs.texture = texture;
        system.rs.src_blend = rwBLENDSRCALPHA;
        system.rs.dst_blend = rwBLENDINVSRCALPHA;
        system.rs.flags = 0x4;
        system.order_group = 0x484f424f;
        // asset flag 0x2 selects the alternate render order slot
        system.order_index = alt_order ? 4 : 6;
        system.stride = sizeof(smoke_particle);
        system.update = update_particles;
        system.context = NULL;

        t->texture = texture;
        t->system_id = xParticleBatchAddSystem(system);
        return t;
    }

    inline smoke_type& get_type(const zSmokeEmitter& e)
    {
        RwTexture* texture =
            e.asset->texture == 0 ? NULL : (RwTexture*)xSTFindAsset(e.asset->texture, NULL);
        smoke_type* end = types + types_size;
        smoke_type* type = binary_find(types, end, texture, compare_types());
        if (type == end)
        {
            type = add_type(texture, e.asset->flags & 0x2);
        }
        return *type;
    }

    inline void perturb_dir(xVec3& dir, F32 angle)
    {
        F32 rand_scale = 2.3283064e-10f * angle;
        F32 ang_min = 0.5f * angle;
        xVec3 ang = { rand_scale * xrand_RandomInt32() - ang_min,
                      rand_scale * xrand_RandomInt32() - ang_min,
                      rand_scale * xrand_RandomInt32() - ang_min };
        xMat3x3 mat;
        xMat3x3Euler(&mat, &ang);
        xMat3x3LMulVec(&dir, &mat, &dir);
    }
} // namespace

namespace
{
    S32 update_particles(U8* mem, S32 count, ptank_pool& apool, F32 dt, void*)
    {
        ptank_pool__pos_color_size_uv2& pool = (ptank_pool__pos_color_size_uv2&)apool;
        smoke_particle* p = (smoke_particle*)mem;
        smoke_particle* end = p + count;

        while (p != end)
        {
            p->age += p->age_rate * dt;

            if (p->age >= 1.0f)
            {
                --end;
                *p = *end;
                continue;
            }

            p->vel.x += wind_delta.x * p->wind;
            p->vel.y += wind_delta.y * p->wind;
            p->vel.z += wind_delta.z * p->wind;

            p->loc.x += p->vel.x * dt;
            p->loc.y += p->vel.y * dt;
            p->loc.z += p->vel.z * dt;

            const F32 oldsize = p->size;
            p->size += p->age * p->growth;

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
                *pool.color = p->color;
                pool.color->r += (U8)(p->dr * t1 + 0.5f);
                pool.color->g += (U8)(p->dg * t1 + 0.5f);
                pool.color->b += (U8)(p->db * t1 + 0.5f);

                F32 t2 = t1 * t1;
                // cubic term first, as in the PS2 build; retail pools 6.722689f before the others
                F32 alpha = 6.722689f * (t1 * t2) + (8.067227f * t1 + -14.789916f * t2);
                if (alpha > 1.0f)
                {
                    alpha = 1.0f;
                }
                alpha *= p->da * t1 + p->color.a;
                pool.color->a = (U8)(alpha + 0.5f);

                *pool.pos = p->loc;
                pool.size->assign(p->size);
                pool.uv[0] = p->uv[0];
                pool.uv[1] = p->uv[1];
            }

            p->size = oldsize;
            ++p;
        }

        return end - (smoke_particle*)mem;
    }
} // namespace

void zSmokeSceneEnter()
{
    types_size = 0;
    wind_magnitude_time = wind_yaw_time = wind_pitch_time = 0.0f;
    wind_delta = 0.0f;
}

void zSmokeSceneExit()
{
}

void zSmokeUpdate(F32 dt)
{
    zSmokeEmitter* end;
    zSmokeEmitter* it;

    update_wind(dt);

    it = (zSmokeEmitter*)globals.sceneCur->baseList[eBaseTypeSmokeEmitter];
    end = it + globals.sceneCur->baseCount[eBaseTypeSmokeEmitter];

    for (; it != end; ++it)
    {
        if (it->flags & 0x1)
        {
            it->update(dt);
        }
    }
}

void zSmokeEmitter::load(xBase& data, xDynAsset& asset, size_t)
{
    ((zSmokeEmitter&)data).load((asset_type&)asset);
}

void zSmokeEmitter::load(const asset_type& a)
{
    xMat3x3 dir_mat;

    asset = (asset_type*)&a;
    xBaseInit(this, asset);
    baseType = eBaseTypeSmokeEmitter;
    eventFunc = cb_dispatch;

    if (linkCount != 0)
    {
        link = (xLinkAsset*)(asset + 1);
    }

    system_id = get_type(*this).system_id;

    refresh_mat();

    emitted = 0.0f;
    // flags is assigned twice in retail (the PS2 line table has the second line too)
    flags = asset->flags;
    flags = asset->flags;

    xMat3x3Euler(&dir_mat, &asset->vel_dir);
    vel_dir = dir_mat.at;

    max_frame = asset->texture_rows * asset->texture_columns;
    du = 1.0f / asset->texture_columns;
    dv = 1.0f / asset->texture_rows;
    age_rate_min = 1.0f / asset->life_max;
    age_rate_range = 1.0f / asset->life_min - age_rate_min;

    dr = (F32)asset->color_death.r - (F32)asset->color_birth.r;
    dg = (F32)asset->color_death.g - (F32)asset->color_birth.g;
    db = (F32)asset->color_death.b - (F32)asset->color_birth.b;
    da = (F32)asset->color_death.a - (F32)asset->color_birth.a;
}

void zSmokeEmitter::update(F32 dt)
{
    F32 rate = asset->rate;
    emitted += rate * dt;

    S32 emit = (S32)emitted;
    if (emit <= 0)
    {
        return;
    }

    emitted -= emit;

    while (emit > 0)
    {
        U8* mem;
        S32 have = xParticleBatchEmit(system_id, emit, &mem);
        if (have == 0)
        {
            break;
        }

        smoke_particle* p = (smoke_particle*)mem;
        smoke_particle* endp = p + have;

        for (; p != endp; ++p)
        {
            p->size = asset->size_min + (asset->size_max - asset->size_min) * xurand();
            p->age = 0.0f;
            p->age_rate = age_rate_min + age_rate_range * xurand();
            p->color = asset->color_birth;
            p->dr = dr;
            p->dg = dg;
            p->db = db;
            p->da = da;

            p->loc.assign(xurand(), xurand(), xurand());
            xMat4x3Toworld(&p->loc, &emit_mat, &p->loc);

            p->vel = vel_dir;
            perturb_dir(p->vel, asset->vel_dir_vary);
            p->vel *= asset->vel_min + (asset->vel_max - asset->vel_min) * xurand();

            p->growth = p->size * asset->growth;
            p->wind = asset->wind;

            S32 frame = xrand_RandomInt32() % max_frame;
            p->uv[0].assign(du * (frame % asset->texture_rows), dv * (frame / asset->texture_rows));
            p->uv[1].assign(p->uv[0].x + du, p->uv[0].y + dv);
        }

        emit -= have;
    }
}

// No GC or PS2 symbol: a plain member auto-inlined into load and dead-stripped (Ratatouille still
// has zSmokeEmitter::refresh_mat). Defined after update for retail's literal order.
void zSmokeEmitter::refresh_mat()
{
    xMat3x3Euler(&emit_mat, &asset->region.dir);
    emit_mat.right *= asset->region.scale.x;
    emit_mat.up *= asset->region.scale.y;
    emit_mat.at *= asset->region.scale.z;
    emit_mat.pos.assign(-0.5f, -0.5f, -0.5f);
    xMat3x3RMulVec(&emit_mat.pos, &emit_mat, &emit_mat.pos);
    emit_mat.pos += asset->region.loc;
}

void zSmokeEmitter::cb_dispatch(xBase*, xBase* to, U32 event, const F32*, xBase*, U32)
{
    zSmokeEmitter& e = *(zSmokeEmitter*)to;

    switch (event)
    {
    case eEventOn:
        e.flags |= 0x1;
        break;
    case eEventOff:
        e.flags &= ~0x1;
        e.emitted = 0.0f;
        break;
    }
}
