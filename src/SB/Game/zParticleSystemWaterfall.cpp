#include "zParticleSystemWaterfall.h"

#include <PowerPC_EABI_Support/MSL_C++/MSL_Common/Include/new>

#include "iMath.h"
#include "xMath.h"
#include "xColor.h"
#include "xMemMgr.h"
#include "xstransvc.h"
#include "zGlobals.h"
#include "xRand.h"

namespace
{
    template <class T> inline void update_generators(zParticleSystem& system, F32 dt)
    {
        zParticleGenerator** it = system.generators;
        zParticleGenerator** end = it + system.generators_size;

        for (; it != end; ++it)
        {
            if ((*it)->flags & 0x1)
            {
                ((T*)*it)->update(dt);
            }
        }
    }

    inline bool sphere_visible(const xVec3& center, F32 radius)
    {
        RwSphere s;
        s.center.x = center.x;
        s.center.y = center.y;
        s.center.z = center.z;
        s.radius = radius;
        return RwCameraFrustumTestSphere(xglobals->screen->icam, &s) != rwSPHEREOUTSIDE;
    }
} // namespace

zParticleGenerator* zParticleSystemWaterfall::create_generator(zParticleAsset& asset)
{
    zParticleGeneratorWaterfall* gen =
        new (xMemAlloc(gActiveHeap, sizeof(zParticleGeneratorWaterfall), 0))
            zParticleGeneratorWaterfall;
    gen->create(*this, asset);
    return gen;
}

void zParticleSystemWaterfall::scene_enter()
{
    batch_group.create();
}

void zParticleSystemWaterfall::update(F32 dt)
{
    update_generators<zParticleGeneratorWaterfall>(*this, dt);
}

S32 zParticleSystemWaterfall::get_asset_size() const
{
    return sizeof(asset_data);
}

S32 zParticleSystemWaterfall::update_particles(U8* mem, S32 count, ptank_pool& apool, F32 dt, void*)
{
    ptank_pool__pos_color_size& pool = (ptank_pool__pos_color_size&)apool;
    particle_instance* p = (particle_instance*)mem;
    particle_instance* end = p + count;
    xVec4 v;

    while (p != end)
    {
        p->vel.y += p->gravity * dt;
        p->loc.y += p->vel.y * dt;
        p->height_vel += p->height_accel * dt;
        p->height += p->height_vel * dt;
        if (p->height > p->height_max)
        {
            p->height = p->height_max;
        }

        if (p->loc.y + 0.5f * p->height < p->ymin)
        {
            --end;
            *p = *end;
            continue;
        }

        p->loc.x += p->vel.x * dt;
        p->loc.z += p->vel.z * dt;

        v.x = p->loc.x;
        v.y = p->loc.y;
        v.z = p->loc.z;
        v.w = p->width > p->height ? p->width : p->height;

        if (sphere_visible((xVec3&)v, v.w))
        {
            pool.next();
            if (!pool.valid())
            {
                end = p;
                break;
            }

            *pool.pos = p->loc;
            *pool.color = p->color;
            pool.size->assign(p->width, p->height);
        }

        ++p;
    }

    return end - (particle_instance*)mem;
}

void zParticleGeneratorWaterfall::update(F32 dt)
{
    if (locator.activity == NULL)
    {
        return;
    }

    locator.update(dt);

    emitted += asset->rate * dt;
    S32 emit = (S32)emitted;
    if (emit <= 0)
    {
        return;
    }

    emitted -= emit;

    zParticleSystemWaterfall::asset_data& a = *(zParticleSystemWaterfall::asset_data*)(asset + 1);
    xMat4x3& mat = locator.get_frame();

    xVec3 vel_dir;
    if (a.flags & 0x1)
    {
        xMat3x3RMulVec(&vel_dir, &mat, &this->vel_dir);
    }
    else
    {
        vel_dir = this->vel_dir;
    }

    F32 width_min = a.width_min;
    F32 width_delta = 2.3283064e-10f * (a.width_max - a.width_min);
    F32 vel_min = a.vel_min;
    F32 vel_delta = 2.3283064e-10f * (a.vel_max - a.vel_min);
    F32 vel_dir_vary = a.vel_dir_vary;
    F32 intensity_min = a.intensity_min;
    F32 intensity_delta = 2.3283064e-10f * (a.intensity_max - intensity_min);
    F32 red = a.color.r * (255.99f / 255.0f);
    F32 green = a.color.g * (255.99f / 255.0f);
    F32 blue = a.color.b * (255.99f / 255.0f);
    U8 alpha = a.color.a;
    F32 gravity = -a.gravity;
    F32 ymin = mat.pos.y - a.kill_yoffset;
    F32 height_start_min = a.height_start_min;
    F32 height_start_delta = 2.3283064e-10f * (a.height_start_max - a.height_start_min);
    F32 height_vel = a.height_vel;
    F32 height_accel = a.height_accel;
    F32 height_max = a.height_max;

    while (emit > 0)
    {
        U8* mem;
        S32 have = xParticleBatchEmit(batch_id, emit, &mem);
        if (have == 0)
        {
            break;
        }

        zParticleSystemWaterfall::particle_instance* p =
            (zParticleSystemWaterfall::particle_instance*)mem;
        zParticleSystemWaterfall::particle_instance* endp = p + have;

        locator.make_locations(p, sizeof(zParticleSystemWaterfall::particle_instance), have, 0);

        for (; p != endp; ++p)
        {
            p->width = width_min + width_delta * xrand_RandomInt32();
            p->color = g_WHITE;
            p->ymin = ymin;
            p->vel = vel_dir;
            perturb_dir(p->vel, vel_dir_vary);
            p->vel *= vel_min + vel_delta * xrand_RandomInt32();

            F32 intensity = intensity_min + intensity_delta * xrand_RandomInt32();
            p->color.r = (U8)(intensity * red);
            p->color.g = (U8)(intensity * green);
            p->color.b = (U8)(intensity * blue);
            p->color.a = alpha;
            p->gravity = gravity;
            p->height = height_start_min + height_start_delta * xrand_RandomInt32();
            p->height_vel = height_vel;
            p->height_accel = height_accel;
            p->height_max = height_max;
        }

        emit -= have;
    }
}

void zParticleGeneratorWaterfall::reset()
{
    zParticleGenerator::reset();
    emitted = 0;
    locator.restart();
    locator.deactivate();
    if (flags & ~0xFFFFFFFE)
    {
        locator.activate(*asset, attach_to);
    }
}

void zParticleGeneratorWaterfall::activate()
{
    zParticleGenerator::activate();
    locator.activate(*asset, attach_to); // Make const
}

void zParticleGeneratorWaterfall::deactivate()
{
    zParticleGenerator::deactivate();
    locator.deactivate();
}

zParticleGenerator* zParticleSystemWaterfallMist::create_generator(zParticleAsset& asset)
{
    zParticleGeneratorWaterfallMist* gen =
        new (xMemAlloc(gActiveHeap, sizeof(zParticleGeneratorWaterfallMist), 0))
            zParticleGeneratorWaterfallMist;
    gen->create(*this, asset);
    return gen;
}

void zParticleSystemWaterfallMist::scene_enter()
{
    batch_group.create();
}

void zParticleSystemWaterfallMist::update(F32 dt)
{
    update_generators<zParticleGeneratorWaterfallMist>(*this, dt);
}

S32 zParticleSystemWaterfallMist::get_asset_size() const
{
    return sizeof(asset_data);
}

S32 zParticleSystemWaterfallMist::update_particles(U8* mem, S32 count, ptank_pool& apool, F32 dt,
                                                   void*)
{
    ptank_pool__pos_color_size_rot& pool = (ptank_pool__pos_color_size_rot&)apool;
    particle_instance* p = (particle_instance*)mem;
    particle_instance* end = p + count;
    F32 t;
    F32 age;
    xVec4 v;
    F32 t2;
    F32 alpha;

    while (p != end)
    {
        p->age += dt;
        t = p->age * p->age_rate;
        if (t >= 1.0f)
        {
            --end;
            *p = *end;
            continue;
        }

        age = p->age;
        v.x = p->x + p->dx * age;
        v.y = p->y + p->dy * age;
        v.z = p->z + p->dz * age;
        v.w = p->w + p->dw * t;
        v.y += 0.5f * v.w;

        if (sphere_visible((xVec3&)v, v.w))
        {
            pool.next();
            if (!pool.valid())
            {
                end = p;
                break;
            }

            pool.pos->assign(v.x, v.y, v.z);
            pool.size->assign(v.w);
            *pool.color = p->color;

            t2 = t * t;
            alpha = 12.5f * (t * t2) + (13.88889f * t + -26.38889f * t2);
            if (alpha < 1.0f)
            {
                pool.color->a = (U8)(alpha * p->color.a + 0.5f);
            }

            p->rot += p->rot_vel * dt;
            if (p->rot > PI)
            {
                p->rot -= 2 * PI;
            }
            else if (p->rot < -PI)
            {
                p->rot += 2 * PI;
            }
            *pool.rot = p->rot;
        }

        ++p;
    }

    return end - (particle_instance*)mem;
}

void zParticleGeneratorWaterfallMist::update(F32 dt)
{
    if (locator.activity == NULL)
    {
        return;
    }

    locator.update(dt);

    emitted += asset->rate * dt;
    S32 emit = (S32)emitted;
    if (emit <= 0)
    {
        return;
    }

    emitted -= emit;

    zParticleSystemWaterfallMist::asset_data& a =
        *(zParticleSystemWaterfallMist::asset_data*)(asset + 1);

    F32 life_min = a.life_min;
    F32 life_delta = 2.3283064e-10f * (a.life_max - life_min);
    F32 size_birth = a.size_birth;
    F32 size_death = a.size_death;
    F32 size_vary = 2.3283064e-10f * a.size_vary;
    F32 intensity_min = a.intensity_min;
    F32 intensity_delta = 2.3283064e-10f * (a.intensity_max - intensity_min);
    F32 red = a.color.r * (255.99f / 255.0f);
    F32 green = a.color.g * (255.99f / 255.0f);
    F32 blue = a.color.b * (255.99f / 255.0f);
    U8 alpha = a.color.a;
    F32 rot_vel_min = a.rot_vel_min;
    F32 rot_vel_delta = (1.0f / 65535.0f) * (a.rot_vel_max - rot_vel_min);
    F32 vel_min = a.vel_min;
    F32 vel_delta = 2.3283064e-10f * (a.vel_max - vel_min);
    F32 vel_pitch_delta = 2.3283064e-10f * a.vel_pitch_max;

    xVec3 center;
    locator.get_center(center);
    F32 cx = center.x;
    F32 cz = center.z;

    while (emit > 0)
    {
        U8* mem;
        S32 have = xParticleBatchEmit(batch_id, emit, &mem);
        if (have == 0)
        {
            break;
        }

        zParticleSystemWaterfallMist::particle_instance* p =
            (zParticleSystemWaterfallMist::particle_instance*)mem;
        zParticleSystemWaterfallMist::particle_instance* endp = p + have;

        locator.make_locations(p, sizeof(zParticleSystemWaterfallMist::particle_instance), have, 8);

        for (; p != endp; ++p)
        {
            // 'life', 'r' and 'sp' are not PS2 DWARF locals: one random value gives rot, rot_vel
            // and its sign, and the sine product is held across the icos call.
            F32 life = life_min + life_delta * xrand_RandomInt32();
            p->age = 0.0f;
            p->age_rate = 1.0f / life;

            F32 scale = 1.0f + size_vary * xrand_RandomInt32();
            p->w = scale * size_birth;
            p->dw = scale * size_death - p->w;

            F32 intensity = intensity_min + intensity_delta * xrand_RandomInt32();
            p->color.r = (U8)(intensity * red);
            p->color.g = (U8)(intensity * green);
            p->color.b = (U8)(intensity * blue);
            p->color.a = alpha;

            U32 r = xrand_RandomInt32();
            p->rot = 2.0f * PI / 32767.0f * (r & 0x7fff) - PI;
            p->rot_vel = rot_vel_min + (r >> 16) * rot_vel_delta;
            if (r & 0x8000)
            {
                p->rot_vel = -p->rot_vel;
            }

            F32 vel = vel_min + vel_delta * xrand_RandomInt32();
            F32 vel_pitch = vel_pitch_delta * xrand_RandomInt32();
            F32 dx = p->x - cx;
            F32 dz = p->z - cz;
            F32 dist2 = dx * dx + dz * dz;
            scale = dist2 <= 0.0001f ? 0.0f : xinvsqrt(dist2);

            F32 sp = vel * isin(vel_pitch);
            F32 cp = vel * icos(vel_pitch);
            p->dx = scale * (cp * dx);
            p->dy = sp;
            p->dz = scale * (cp * dz);
        }

        emit -= have;
    }
}

void zParticleGeneratorWaterfallMist::reset()
{
    zParticleGenerator::reset();
    emitted = 0;
    locator.restart();
    locator.deactivate();
    if (flags & ~0xFFFFFFFE)
    {
        locator.activate(*asset, attach_to);
    }
}

void zParticleGeneratorWaterfallMist::activate()
{
    zParticleGenerator::activate();
    locator.activate(*asset, attach_to); // Make const
}

void zParticleGeneratorWaterfallMist::deactivate()
{
    zParticleGenerator::deactivate();
    locator.deactivate();
}

zParticleGenerator* zParticleSystemWaterfallSplash::create_generator(zParticleAsset& asset)
{
    zParticleGeneratorWaterfallSplash* gen =
        new (xMemAlloc(gActiveHeap, sizeof(zParticleGeneratorWaterfallSplash), 0))
            zParticleGeneratorWaterfallSplash;
    gen->create(*this, asset);
    return gen;
}

void zParticleSystemWaterfallSplash::scene_enter()
{
    batch_group.create();
}

void zParticleSystemWaterfallSplash::update(F32 dt)
{
    update_generators<zParticleGeneratorWaterfallSplash>(*this, dt);
}

S32 zParticleSystemWaterfallSplash::get_asset_size() const
{
    return sizeof(asset_data);
}

S32 zParticleSystemWaterfallSplash::update_particles(U8* mem, S32 count, ptank_pool& apool, F32 dt,
                                                     void*)
{
    ptank_pool__pos_color_size_rot& pool = (ptank_pool__pos_color_size_rot&)apool;
    particle_instance* p = (particle_instance*)mem;
    particle_instance* end = p + count;
    F32 t;
    F32 age;
    F32 y;
    F32 w;
    xVec4 v;

    while (p != end)
    {
        p->age += dt;
        t = p->age * p->age_rate;
        if (t >= 1.0f)
        {
            --end;
            *p = *end;
            continue;
        }

        age = p->age;
        y = p->y + p->dy * age + p->ay * age * age;
        w = p->w + p->dw * t;

        if (y + 0.5f * w < p->ymin)
        {
            --end;
            *p = *end;
            continue;
        }

        v.x = p->x + p->dx * age;
        v.y = y;
        v.z = p->z + p->dz * age;
        v.w = w;

        if (sphere_visible((xVec3&)v, v.w))
        {
            pool.next();
            if (!pool.valid())
            {
                end = p;
                break;
            }

            pool.pos->assign(v.x, v.y, v.z);
            pool.size->assign(v.w);
            *pool.color = p->color;
            pool.color->a = (U8)(p->color.a * (1.0f - t) + 0.5f);
            *pool.rot = p->rot;
        }

        ++p;
    }

    return end - (particle_instance*)mem;
}

void zParticleGeneratorWaterfallSplash::update(F32 dt)
{
    if (locator.activity == NULL)
    {
        return;
    }

    locator.update(dt);

    emitted += asset->rate * dt;
    S32 emit = (S32)emitted;
    if (emit <= 0)
    {
        return;
    }

    emitted -= emit;

    zParticleSystemWaterfallSplash::asset_data& a =
        *(zParticleSystemWaterfallSplash::asset_data*)(asset + 1);
    xMat4x3& mat = locator.get_frame();

    F32 life_min = a.life_min;
    F32 life_delta = 2.3283064e-10f * (a.life_max - life_min);
    F32 size_birth = a.size_birth;
    F32 size_death = a.size_death;
    F32 size_vary = 2.3283064e-10f * a.size_vary;
    F32 ay = -0.5f * a.gravity;
    F32 ymin = mat.pos.y - a.kill_yoffset;
    F32 intensity_min = a.intensity_min;
    F32 intensity_delta = 2.3283064e-10f * (a.intensity_max - intensity_min);
    F32 red = a.color.r * (255.99f / 255.0f);
    F32 green = a.color.g * (255.99f / 255.0f);
    F32 blue = a.color.b * (255.99f / 255.0f);
    U8 alpha = a.color.a;
    F32 vel_min = a.vel_min;
    F32 vel_delta = 2.3283064e-10f * (a.vel_max - vel_min);
    F32 vel_pitch_min = a.vel_pitch_min;
    F32 vel_pitch_delta = 2.3283064e-10f * (a.vel_pitch_max - a.vel_pitch_min);

    xVec3 center;
    locator.get_center(center);
    F32 cx = center.x;
    F32 cz = center.z;

    while (emit > 0)
    {
        U8* mem;
        S32 have = xParticleBatchEmit(batch_id, emit, &mem);
        if (have == 0)
        {
            break;
        }

        zParticleSystemWaterfallSplash::particle_instance* p =
            (zParticleSystemWaterfallSplash::particle_instance*)mem;
        zParticleSystemWaterfallSplash::particle_instance* endp = p + have;

        locator.make_locations(p, sizeof(zParticleSystemWaterfallSplash::particle_instance), have,
                               8);

        for (; p != endp; ++p)
        {
            // 'life' and 'sp' are not PS2 DWARF locals; the sine product is held across the icos
            // call.
            F32 life = life_min + life_delta * xrand_RandomInt32();
            p->age = 0.0f;
            p->age_rate = 1.0f / life;

            F32 scale = 1.0f + size_vary * xrand_RandomInt32();
            p->w = scale * size_birth;
            p->dw = scale * size_death - p->w;
            p->ay = ay;
            p->ymin = ymin;

            F32 intensity = intensity_min + intensity_delta * xrand_RandomInt32();
            p->color.r = (U8)(intensity * red);
            p->color.g = (U8)(intensity * green);
            p->color.b = (U8)(intensity * blue);
            p->color.a = alpha;

            p->rot = 2.0f * PI / 4294967296.0f * xrand_RandomInt32() - PI;

            F32 vel = vel_min + vel_delta * xrand_RandomInt32();
            F32 vel_pitch = vel_pitch_min + vel_pitch_delta * xrand_RandomInt32();
            F32 dx = p->x - cx;
            F32 dz = p->z - cz;
            F32 dist2 = dx * dx + dz * dz;
            scale = dist2 <= 0.0001f ? 0.0f : xinvsqrt(dist2);

            F32 sp = vel * isin(vel_pitch);
            F32 cp = vel * icos(vel_pitch);
            p->dx = scale * (cp * dx);
            p->dy = sp;
            p->dz = scale * (cp * dz);
        }

        emit -= have;
    }
}

void zParticleGeneratorWaterfallSplash::reset()
{
    zParticleGenerator::reset();
    emitted = 0;
    locator.restart();
    locator.deactivate();
    if (flags & ~0xFFFFFFFE)
    {
        locator.activate(*asset, attach_to);
    }
}

void zParticleGeneratorWaterfallSplash::activate()
{
    zParticleGenerator::activate();
    locator.activate(*asset, attach_to); // Make const
}

void zParticleGeneratorWaterfallSplash::deactivate()
{
    zParticleGenerator::deactivate();
    locator.deactivate();
}
